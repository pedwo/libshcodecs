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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "avcbe.h"		/* SuperH MEPG-4&H.264 Video Encode Library Header */
#include "m4iph_vpu4.h"		/* SuperH MEPG-4&H.264 Video Driver Library Header */
#include "encoder_common.h"
#include "m4driverif.h"

#include "avcbe_inner.h"
#include "QuantMatrix.h"

#include "encoder_private.h"

#define OUTPUT_ERROR_MSGS

#define USE_BVOP


#ifdef OUTPUT_ERROR_MSGS
#define MSG_LEN 127
static long
vpu_err(SHCodecs_Encoder *enc, const char *func, int line, long rc)
{
	char msg[MSG_LEN+1];
	msg[MSG_LEN] = 0;
	snprintf(msg, MSG_LEN, "%s, line %d: returned %ld", func, line, rc);
	m4iph_avcbe_perror(msg, rc);
	enc->error_return_code = rc;
	return rc;
}
#else
#define vpu_err(enc, func, line, rc) (rc)
#endif


enum {SEI, SPS, PPS, AUD, IDATA, PDATA, BDATA, FILL, END};
#ifdef OUTPUT_STREAM_INFO
static const char*data_name[] =
	{"SEI", "SPS", "PPS", "AUD", "I", "P", "B", "FILL", "END"};
#endif

static int
output_data(SHCodecs_Encoder *enc, int type, void *buf, long length)
{
#ifdef OUTPUT_STREAM_INFO
	fprintf (stderr, "output %s (%d bytes)\n", data_name[type], (int)length);
#endif
	if (enc->output) {
		return enc->output(enc,
				(unsigned char *)buf, length,
				enc->output_user_data);
	}

	return 0;
}


static int
mpeg4_encode_init_other_options (SHCodecs_Encoder *enc)
{
	/* This was commented out in the original sample code */
#if 0
	/*** avcbe_other_options_mpeg4 ***/
	enc->other_options_mpeg4.avcbe_out_vos = AVCBE_ON;
	enc->other_options_mpeg4.avcbe_out_gov = AVCBE_ON;
	enc->other_options_mpeg4.avcbe_aspect_ratio_info_type =
	    AVCBE_AUTO;
	enc->other_options_mpeg4.avcbe_aspect_ratio_info_value = 3;

	enc->other_options_mpeg4.avcbe_vos_profile_level_type =
	    AVCBE_AUTO;
	enc->other_options_mpeg4.avcbe_vos_profile_level_value = 1;
	enc->other_options_mpeg4.avcbe_out_visual_object_identifier =
	    AVCBE_OFF;
	enc->other_options_mpeg4.avcbe_visual_object_verid = 0;
	enc->other_options_mpeg4.avcbe_visual_object_priority = 7;
	enc->other_options_mpeg4.avcbe_video_object_type_indication =
	    0;
	enc->other_options_mpeg4.avcbe_out_object_layer_identifier =
	    AVCBE_OFF;
	enc->other_options_mpeg4.avcbe_video_object_layer_verid = 0;
	enc->other_options_mpeg4.avcbe_video_object_layer_priority = 7;

	/* 'AVCBE_ERM_VIDEO_PACKET' -> 'AVCBE_ERM_NORMAL' changed at Version2 */
	enc->other_options_mpeg4.avcbe_error_resilience_mode =
	    AVCBE_ERM_NORMAL;
	enc->other_options_mpeg4.avcbe_video_packet_size_mb = 0;
	enc->other_options_mpeg4.avcbe_video_packet_size_bit = 0;
	enc->other_options_mpeg4.avcbe_video_packet_header_extention =
	    AVCBE_OFF;
	/* 'AVCBE_ON' -> 'AVCBE_OFF' changed at Version2 */
	enc->other_options_mpeg4.avcbe_data_partitioned = AVCBE_OFF;
	enc->other_options_mpeg4.avcbe_reversible_vlc = AVCBE_OFF;

	enc->other_options_mpeg4.avcbe_high_quality = AVCBE_HQ_QUALITY;
	enc->other_options_mpeg4.avcbe_param_changeable = AVCBE_OFF;
	enc->other_options_mpeg4.avcbe_changeable_max_bitrate = 0;
	enc->other_options_mpeg4.avcbe_Ivop_quant_initial_value = 16;
	enc->other_options_mpeg4.avcbe_Pvop_quant_initial_value = 16;
	enc->other_options_mpeg4.avcbe_use_dquant = AVCBE_ON;
	/* '10' -> '4' changed at Version2 */
	enc->other_options_mpeg4.avcbe_clip_dquant_frame = 4;
	enc->other_options_mpeg4.avcbe_quant_min = 6;
	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_quant_min_Ivop_under_range = 2;
	enc->other_options_mpeg4.avcbe_quant_max = 26;	/* '31' -> '26' changed at Version2 */

	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_skipcheck_enable = AVCBE_ON;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_Ivop_noskip = AVCBE_ON;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_remain_zero_skip_enable = AVCBE_ON;	/* added at Version2 */

	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_buffer_unit_size = 16384;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_buffer_mode = AVCBE_AUTO;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_max_size = 70;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_offset = 20;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_ratecontrol_vbv_offset_rate = 50;	/* added at Version2 */

	enc->other_options_mpeg4.avcbe_quant_type =
	    AVCBE_QUANTISATION_TYPE_2;
	enc->other_options_mpeg4.avcbe_use_AC_prediction = AVCBE_ON;
	enc->other_options_mpeg4.avcbe_vop_min_mode = AVCBE_MANUAL;	/* added at Version2 */
	enc->other_options_mpeg4.avcbe_vop_min_size = 0;
	enc->other_options_mpeg4.avcbe_intra_thr = 6000;
	enc->other_options_mpeg4.avcbe_b_vop_num = 0;
#endif

	return 0;
}
/*---------------------------------------------------------------------*/

int
mpeg4_encode_init (SHCodecs_Encoder *enc)
{
	long rc;

	enc->error_return_code = 0;

	mpeg4_encode_init_other_options(enc);

	/* Set default values for the parameters */
	rc = avcbe_set_default_param(enc->stream_type, AVCBE_RATE_NO_SKIP,
				    &(enc->encoding_property),
				    (void *)&(enc->other_options_mpeg4));
	if (rc != 0)
		return vpu_err(enc, __func__, __LINE__, rc);

	return 0;
}

static int
mpeg4_encode_deferred_init(SHCodecs_Encoder *enc)
{
	long rc;
	unsigned long nrefframe = 1;

	/* Initialize VPU parameters & local frame memory */
	rc = avcbe_init_encode(&(enc->encoding_property),
					&(enc->paramR),
					&(enc->other_options_mpeg4),
					(avcbe_buf_continue_userproc_ptr) NULL,
					&enc->work_area, NULL,
					&enc->stream_info);
	if (rc != 0)
		return vpu_err(enc, __func__, __LINE__, rc);

	if (enc->other_options_mpeg4.avcbe_quant_type == 1) {
		rc = SetQuantMatrix(enc->stream_info,
				QMAT_MPEG_TYPE_ANIME1_INTRA,
				QMAT_MPEG_TYPE_ANIME1_NONINTRA);
		if (rc != 0)
			return vpu_err(enc, __func__, __LINE__, rc);
	}

#ifdef USE_BVOP
	if (enc->other_options_mpeg4.avcbe_b_vop_num > 0)
		nrefframe = enc->other_options_mpeg4.avcbe_b_vop_num;
//TODO	m4vse_output_local_image_of_b_vop = AVCBE_ON;
#endif

	rc = avcbe_init_memory(enc->stream_info,
				nrefframe,
				(nrefframe+1), enc->local_frames,
				ROUND_UP_16(enc->width), ROUND_UP_16(enc->height));
	if (rc != 0)
		return vpu_err(enc, __func__, __LINE__, rc);

	enc->initialized = 2;

	return 0;
}

static int
clip_image_data_for_H263(SHCodecs_Encoder *enc,
			 unsigned char *addr_y, unsigned char *addr_c)
{
	long width, height, index;
	unsigned char *YCbCr_ptr;
	int return_value = 0;
	unsigned char *addr_y_2, *addr_c_2;

	width = enc->width;
	height = enc->height;
	addr_y_2 = malloc(width * height);
	addr_c_2 = malloc(width * height / 2);
	m4iph_sdr_read(addr_y, addr_y_2, width * height);

	/* Clip Y-data */
	YCbCr_ptr = addr_y_2;
	for (index = 0; index < (width * height); index++) {
		if ((*YCbCr_ptr) < 1) {
			*YCbCr_ptr = 1;
			return_value = 1;	/* clipped */
		} else if ((*YCbCr_ptr) > 254) {
			*YCbCr_ptr = 254;
			return_value = 1;	/* clipped */
		}
		YCbCr_ptr++;
	}
	m4iph_sdr_write(addr_y, addr_y_2, width * height);

	/* Clip Cb-data, Cr-data */
	m4iph_sdr_read(addr_c, addr_c_2, width * height / 2);
	YCbCr_ptr = addr_c_2;
	for (index = 0; index < ((width * height) / 2); index++) {
		if ((*YCbCr_ptr) < 1) {
			*YCbCr_ptr = 1;
			return_value = 1;	/* clipped */
		} else if ((*YCbCr_ptr) > 254) {
			*YCbCr_ptr = 254;
			return_value = 1;	/* clipped */
		}
		YCbCr_ptr++;
	}
	m4iph_sdr_write(addr_c, addr_c_2, width * height / 2);

	free(addr_y_2);
	free(addr_c_2);
	return (return_value);
}

/* Encode a whole frame for MPEG-4/H.263 */
static long
mpeg4_encode_frame (SHCodecs_Encoder *enc,
	unsigned char *py, unsigned char *pc)
{
	long unit_size;
	long rc;
	TAVCBE_FMEM input_buf;
	avcbe_frame_stat frame_stat;
	long pic_type;
	int cb_ret = 0;

	input_buf.Y_fmemp = py;
	input_buf.C_fmemp = pc;

	if (enc->stream_type != AVCBE_MPEG4) {
		//TODO this doesn't modify any data and we don't do anything
		// with the return value...
		rc = clip_image_data_for_H263(enc, py, pc);
	}

	if (enc->frame_counter != 0) {
		/* Restore stream context */
		rc = avcbe_set_backup(enc->stream_info, &enc->backup_area);
		if (rc != 0)
			return vpu_err(enc, __func__, __LINE__, rc);
	}

	/* Specify the input frame address */
	rc = avcbe_set_image_pointer(enc->stream_info,
				    &input_buf, enc->ldec, enc->ref1, 0);
	if (rc != 0)
		return vpu_err(enc, __func__, __LINE__, rc);

	/* Encode the frame */
	rc = avcbe_encode_picture(enc->stream_info, enc->frm,
				 AVCBE_ANY_VOP,
				 AVCBE_OUTPUT_NONE,
				 &enc->stream_buff_info, NULL);
	if (rc != 0)
		return vpu_err(enc, __func__, __LINE__, rc);

	if (rc == AVCBE_FRAME_SKIPPED) {
		enc->frame_num_delta++;
		enc->frame_skip_num++;
	}

	if ((rc == AVCBE_ENCODE_SUCCESS)
	    || (rc == AVCBE_B_VOP_OUTPUTTED)
	    || (rc == AVCBE_EMPTY_VOP_OUTPUTTED)) {

		if (rc == AVCBE_ENCODE_SUCCESS) {
			if (enc->ldec == 0) {
				enc->ldec = 1;
				enc->ref1 = 0;
			} else {
				enc->ldec = 0;
				enc->ref1 = 1;
			}
		}

		avcbe_get_last_frame_stat(enc->stream_info, &frame_stat);
		unit_size = (frame_stat.avcbe_frame_n_bits + 7) / 8;
		pic_type = frame_stat.avcbe_frame_type;

		enc->frame_num_delta++;

		/* Output frame data */
		if (pic_type == AVCBE_I_VOP) {
			cb_ret = output_data(enc, IDATA,
					enc->stream_buff_info.buff_top, unit_size);
		} else if (pic_type == AVCBE_P_VOP) {
			cb_ret = output_data(enc, PDATA,
					enc->stream_buff_info.buff_top, unit_size);
		} else {
			cb_ret = output_data(enc, BDATA,
					enc->stream_buff_info.buff_top, unit_size);
		}

		enc->frame_num_delta = 0;
	}

	enc->frm += enc->frame_no_increment;
	enc->frame_counter++;

	/* Save stream context */
	rc = avcbe_get_backup(enc->stream_info, &enc->backup_area);
	if (rc != 0)
		return vpu_err(enc, __func__, __LINE__, rc);

	return cb_ret;
}

/* Release all input buffers that aren't in use */
static int
mpeg4_release_input_buffers(SHCodecs_Encoder *enc)
{
	long rc;
	unsigned long i;

#ifdef USE_BVOP
	// TODO this is highly suspect - I guess we should be passing the middleware a list of input buffers
	AVCBE_FRAME_CHECK frame_check_array[17];

	if (enc->other_options_mpeg4.avcbe_b_vop_num > 0) {

		rc = avcbe_get_buffer_check(enc->stream_info,
					   &frame_check_array[0]);
		if (rc < 0) {
			return vpu_err(enc, __func__, __LINE__, rc);
		}

		for (i=0; i < (enc->other_options_mpeg4.avcbe_b_vop_num + 1); i++) {
			if (frame_check_array[i].avcbe_status == AVCBE_UNLOCK) {
				if (enc->release)
					enc->release(enc, enc->addr_y_tbl[i], enc->addr_c_tbl[i], enc->release_user_data);
			}
		}
	}
#else
	if (enc->release)
		enc->release(enc, enc->addr_y, enc->addr_c, enc->release_user_data);
#endif
	return 0;
}

static long
mpeg4_encode_start (SHCodecs_Encoder *enc)
{
	long rc;
	int cb_ret;
	unsigned long i;

	if (enc->allocate) {
#ifdef USE_BVOP
		for (i=0; i < (enc->other_options_mpeg4.avcbe_b_vop_num + 1); i++) {
			enc->addr_y_tbl[i] = enc->input_frames[i].Y_fmemp;
			enc->addr_c_tbl[i] = enc->input_frames[i].C_fmemp;
		}
#endif

		rc = mpeg4_release_input_buffers(enc);
		if (rc != 0)
			return rc;

		/* Fixed input buffer if client doesn't change it */
		enc->addr_y = enc->input_frames[0].Y_fmemp;
		enc->addr_c = enc->input_frames[0].C_fmemp;
	}

	enc->ldec = 0;		/* Index number of the image-work-field area */
	enc->ref1 = 0;
	enc->frm = 0;		/* Frame number to be encoded */

	enc->frame_counter = 0;
	enc->frame_skip_num = 0;

	enc->initialized = 3;

	return 0;
}

int
mpeg4_encode_finish (SHCodecs_Encoder *enc)
{
	long rc, length;

	m4iph_vpu_lock(enc->vpu);
	length = avcbe_put_end_code(enc->stream_info, &enc->end_code_buff_info, AVCBE_VOSE);
	m4iph_vpu_unlock(enc->vpu);
	if (length <= 0)
		return vpu_err(enc, __func__, __LINE__, length);

	rc = output_data(enc, END, enc->end_code_buff_info.buff_top, length);
	if (rc != 0)
		return rc;

	return 0;
}

int
mpeg4_encode_1frame(SHCodecs_Encoder *enc, unsigned char *py, unsigned char *pc, int phys)
{
	unsigned char *phys_py = py;
	unsigned char *phys_pc = pc;
	int rc;

	// TODO this will not work if encoding B-VOPS
	if (!phys) {
		/* Copy to contiguous buffer that the VPU can access */
		phys_py = enc->input_frames[0].Y_fmemp;
		phys_pc = enc->input_frames[0].C_fmemp;

		m4iph_vpu_lock(enc->vpu);
		m4iph_sdr_write(phys_py, py, enc->y_bytes);
		m4iph_sdr_write(phys_pc, pc, enc->y_bytes / 2);
		m4iph_vpu_unlock(enc->vpu);

		if (enc->release)
			enc->release(enc, py, pc, enc->release_user_data);
	}

	if (enc->initialized < 3) {
		rc = mpeg4_encode_start(enc);
		if (rc != 0)
			return rc;
	}

	m4iph_vpu_lock(enc->vpu);
	rc = mpeg4_encode_frame(enc, phys_py, phys_pc);
	m4iph_vpu_unlock(enc->vpu);

	if (phys && enc->release)
		enc->release(enc, py, pc, enc->release_user_data);

	return rc;
}

int
mpeg4_encode_run (SHCodecs_Encoder *enc)
{
	long rc;
	int cb_ret;

	if (enc->initialized < 3) {
		rc = mpeg4_encode_start(enc);
		if (rc != 0)
			return rc;
	}

	/* For all frames to encode */
	while (1) {
		/* Get the encoder input frame */
		if (enc->input) {
			cb_ret = enc->input(enc, enc->input_user_data);
			if (cb_ret != 0) {
				enc->error_return_code = cb_ret;
				return cb_ret;
			}
		}

		m4iph_vpu_lock(enc->vpu);

		/* Encode the frame */
		rc = mpeg4_encode_frame(enc, enc->addr_y, enc->addr_c);
		if (rc != 0) {
			m4iph_vpu_unlock(enc->vpu);
			return rc;
		}

		rc = mpeg4_release_input_buffers(enc);
		m4iph_vpu_unlock(enc->vpu);
		if (rc != 0)
			return rc;
	}

	rc = mpeg4_encode_finish(enc);

	return rc;
}


