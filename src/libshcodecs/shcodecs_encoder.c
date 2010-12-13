/*
 * libshcodecs: A library for controlling SH-Mobile hardware codecs
 * Copyright (C) 2009 Renesas Technology Corp.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "m4driverif.h"
#include "encoder_private.h"

#undef MAX
#define MAX(a,b) ((a>b)?(a):(b))

/* Minimum size as this buffer is used for data other than encoded frames */
/* TODO min size has not been verified, just takem from sample code */
#define MIN_STREAM_BUFF_SIZE (160000*4)


static unsigned long
work_area_size (SHCodecs_Encoder * encoder)
{
	if (encoder->format == SHCodecs_Format_H264) {
		return 256*1024;
	} else {
		/* All sizes fail at 64KB, but work at 65KB */
		return 65*1024;
	}
}

static unsigned long
dimension_stream_buff_size (int width, int height)
{
	long wh = width * height;
	long size = wh + wh/2;	/* Size of uncompressed YCbCr420 frame */

	/* Apply minimum compression ratio */
	if (wh >= 720 * 480) /* >= D1 */ {
		size = size / 4;
	} else {
		size = size / 2;
	}

	if (size < MIN_STREAM_BUFF_SIZE)
		size = MIN_STREAM_BUFF_SIZE;

	/* Make size a multiple of 32 */
	size = (size + 31) & ~31;

	return size;
}

static unsigned long
encoder_stream_buff_size (SHCodecs_Encoder * encoder)
{
	return dimension_stream_buff_size (encoder->width, encoder->height);
}

static int
init_other_API_enc_param(OTHER_API_ENC_PARAM * other_API_enc_param)
{
	memset(&(other_API_enc_param->vui_main_param), 0,
	       sizeof(avcbe_vui_main_param));

	other_API_enc_param->out_buffering_period_SEI = 0;
	other_API_enc_param->out_pic_timing_SEI = 0;
	other_API_enc_param->out_pan_scan_rect_SEI = 0;
	other_API_enc_param->out_filler_payload_SEI = 0;
	other_API_enc_param->out_recovery_point_SEI = 0;
	other_API_enc_param->out_dec_ref_pic_marking_repetition_SEI = 0;

	memset(&(other_API_enc_param->sei_buffering_period_param), 0,
	       sizeof(avcbe_sei_buffering_period_param));
	memset(&(other_API_enc_param->sei_pic_timing_param), 0,
	       sizeof(avcbe_sei_pic_timing_param));
	memset(&(other_API_enc_param->sei_pan_scan_rect_param),
	       0, sizeof(avcbe_sei_pan_scan_rect_param));
	memset(&(other_API_enc_param->sei_filler_payload_param),
	       0, sizeof(avcbe_sei_filler_payload_param));
	memset(&(other_API_enc_param->sei_recovery_point_param),
	       0, sizeof(avcbe_sei_recovery_point_param));

	return 0;
}

/**
 * Deallocate resources used to initialize the VPU4 for encoding,
 * and reset it for future use.
 * \param encoder The SHCodecs_Encoder* handle
 */
void shcodecs_encoder_close(SHCodecs_Encoder * encoder)
{
	long i, width_height;
	unsigned long buf_size;

	if (encoder == NULL) return;

	width_height = ROUND_UP_16(encoder->width) * ROUND_UP_16(encoder->height);
	width_height += (width_height / 2);

	/* Local input frame */
	if (encoder->input_frame) {
		m4iph_sdr_free(encoder->vpu, encoder->input_frame, width_height);
	}

	/* Local decode images */
	for (i=0; i<NUM_LDEC_FRAMES; i++) {
		if (encoder->local_frames[i].Y_fmemp)
			m4iph_sdr_free(encoder->vpu, encoder->local_frames[i].Y_fmemp, width_height);
	}

	if (encoder->format == SHCodecs_Format_H264) {
		h264_encode_close(encoder);
	}

	free(encoder->backup_area.area_top);
	free(encoder->work_area.area_top);
	free(encoder->stream_buff_info.buff_top);
	free(encoder->end_code_buff_info.buff_top);

	m4iph_vpu_close(encoder->vpu);

	free(encoder);
}

static int
shcodecs_encoder_global_init (SHCodecs_Encoder *encoder)
{
	if ((encoder->vpu = m4iph_vpu_open(dimension_stream_buff_size (encoder->width, encoder->height))) == NULL)
		return -1;

	m4iph_vpu_lock(encoder->vpu);
	avcbe_start_encoding();
	m4iph_vpu_unlock(encoder->vpu);

	return 0;
}

/**
 * Initialize the VPU4 for encoding a given video format.
 * \param width The video image width
 * \param height The video image height
 * \param format SHCodecs_Format_MPEG4 or SHCODECS_Format_H264
 * \return encoder The SHCodecs_Encoder* handle
 */
SHCodecs_Encoder *shcodecs_encoder_init(int width, int height,
					SHCodecs_Format format)
{
	SHCodecs_Encoder *encoder;
	long return_code;
	long width_height;
	unsigned long buf_size;
	unsigned char *pY;
	int i;

	encoder = calloc(1, sizeof(SHCodecs_Encoder));
	if (encoder == NULL)
		return NULL;

	encoder->width = width;
	encoder->height = height;
	encoder->format = format;

	encoder->input = NULL;
	encoder->output = NULL;

	encoder->error_return_code = 0;

	encoder->initialized = 0;

	encoder->frm = 0;
	encoder->frame_no_increment = 1;
	encoder->frame_skip_num = 0;
	encoder->frame_num_delta = 0;

	encoder->output_filler_enable = 0;
	encoder->output_filler_data = 0;

	if (shcodecs_encoder_global_init (encoder) < 0)
		goto err;

	width_height = ROUND_UP_16(encoder->width) * ROUND_UP_16(encoder->height);
	width_height += (width_height / 2);

	encoder->y_bytes = (((encoder->width + 15) / 16) * 16) * (((encoder->height + 15) / 16) * 16);

	/* Local decode images. This is the number of reference frames plus one
	   for the locally decoded output */
	for (i=0; i<NUM_LDEC_FRAMES; i++) {
		pY = m4iph_sdr_malloc(encoder->vpu, width_height, 32);
		if (!pY) goto err;
		encoder->local_frames[i].Y_fmemp = pY;
		encoder->local_frames[i].C_fmemp = pY + encoder->y_bytes;
	}

	buf_size = work_area_size(encoder);
	encoder->work_area.area_size = buf_size;
	encoder->work_area.area_top = memalign(4, buf_size);
	if (!encoder->work_area.area_top)
		goto err;

	buf_size = 5500;
	encoder->backup_area.area_size = buf_size;
	encoder->backup_area.area_top = memalign(4, buf_size);
	if (!encoder->backup_area.area_top)
		goto err;

	buf_size = encoder_stream_buff_size(encoder);
	encoder->stream_buff_info.buff_size = buf_size;
	encoder->stream_buff_info.buff_top = memalign(32, buf_size);
	if (!encoder->stream_buff_info.buff_top)
		goto err;

	encoder->end_code_buff_info.buff_size = END_CODE_BUFF_SIZE;
	encoder->end_code_buff_info.buff_top = memalign(32, END_CODE_BUFF_SIZE);
	if (!encoder->end_code_buff_info.buff_top)
		goto err;

	init_other_API_enc_param(&encoder->other_API_enc_param);

	if (encoder->format == SHCodecs_Format_H264) {
		encoder->stream_type = AVCBE_H264;
		return_code = h264_encode_init (encoder);
	} else {
		encoder->stream_type = AVCBE_MPEG4;
		return_code = mpeg4_encode_init (encoder);
	}
	if (return_code < 0)
		goto err;

	encoder->initialized = 1;

	return encoder;

err:
	shcodecs_encoder_close(encoder);
	return NULL;
}

/**
 * Set the callback for libshcodecs to call when raw YUV data is required.
 * \param encoder The SHCodecs_Encoder* handle
 * \param get_input_cb The callback function
 * \param user_data Additional data to pass to the callback function
 */
int
shcodecs_encoder_set_input_callback(SHCodecs_Encoder * encoder,
				    SHCodecs_Encoder_Input input_cb,
				    void *user_data)
{
	encoder->input = input_cb;
	encoder->input_user_data = user_data;

	return 0;
}

/**
 * Set the callback for libshcodecs to call when it no longer requires
 * access to a previously input YUV buffer.
 * \param encoder The SHCodecs_Encoder* handle
 * \param release_cb The callback function
 * \param user_data Additional data to pass to the callback function
 */
int
shcodecs_encoder_set_input_release_callback (SHCodecs_Encoder * encoder,
                                             SHCodecs_Encoder_Input_Release release_cb,
                                             void * user_data)
{
	encoder->release = release_cb;
	encoder->release_user_data = user_data;

	return 0;
}

void *
shcodecs_encoder_get_input_user_data(SHCodecs_Encoder *encoder)
{
	return encoder->release_user_data_buffer;
}

/**
 * Set the callback for libshcodecs to call when encoded data is available.
 * \param encoder The SHCodecs_Encoder* handle
 * \param encodec_cb The callback function
 * \param user_data Additional data to pass to the callback function
 */
int
shcodecs_encoder_set_output_callback(SHCodecs_Encoder * encoder,
				     SHCodecs_Encoder_Output output_cb,
				     void *user_data)
{
	encoder->output = output_cb;
	encoder->output_user_data = user_data;

	return 0;
}

/**
 * Run the encoder.
 * \param encoder The SHCodecs_Encoder* handle
 * \retval 0 Success
 */
int shcodecs_encoder_run(SHCodecs_Encoder * encoder)
{
	if (encoder == NULL)
			return -1;

	if (encoder->format == SHCodecs_Format_H264) {
		return h264_encode_run (encoder);
	} else {
		return mpeg4_encode_run (encoder);
	}
}

int
shcodecs_encoder_encode_1frame(SHCodecs_Encoder * encoder,
	void *y_input,
	void *c_input,
	void *user_data)
{
	if (encoder == NULL)
		return -1;

	if (encoder->format == SHCodecs_Format_H264)
		return h264_encode_1frame (encoder, y_input, c_input, user_data);
	else
		return mpeg4_encode_1frame (encoder, y_input, c_input, user_data);
}

int
shcodecs_encoder_finish(SHCodecs_Encoder * encoder)
{
	if (encoder == NULL)
		return -1;

	if (encoder->format == SHCodecs_Format_H264) {
		return h264_encode_finish (encoder);
	} else {
		return mpeg4_encode_finish (encoder);
	}
}

int
shcodecs_encoder_input_provide (SHCodecs_Encoder * encoder, 
				void *y_input, void *c_input)
{
	if (encoder == NULL) return -1;

	encoder->addr_y = y_input;
	encoder->addr_c = c_input;

	return 0;
}

int
shcodecs_encoder_get_width (SHCodecs_Encoder * encoder)
{
	if (encoder == NULL) return -1;

	return encoder->width;
}

int
shcodecs_encoder_get_height (SHCodecs_Encoder * encoder)
{
	if (encoder == NULL) return -1;

	return encoder->height;
}

int
shcodecs_encoder_get_frame_num_delta(SHCodecs_Encoder *encoder)
{
	if (encoder == NULL) return -1;

	return encoder->frame_num_delta;
}

int
shcodecs_encoder_get_min_input_frames(SHCodecs_Encoder *encoder)
{
	if (encoder == NULL) return -1;

	if (encoder->format == SHCodecs_Format_H264) {
		return 1;
	} else {
		// TODO BVOPs will need several frames
		return 1;
	}
}
