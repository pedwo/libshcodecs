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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sched.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include "shcodecs/shcodecs_decoder.h"
#include "avcbd.h"
#include "avcbd_optionaldata.h"
#include "decoder_private.h"
#include "m4driverif.h"

/* #define DEBUG */
#define OUTPUT_ERROR_MSGS

#ifdef OUTPUT_ERROR_MSGS
#define MSG_LEN 127
static long
vpu_err(SHCodecs_Decoder *dec, const char *func, int line, long rc)
{
	char msg[MSG_LEN+1];
	msg[MSG_LEN] = 0;
	snprintf(msg, MSG_LEN, "%s, line %d: returned %ld", func, line, rc);
	m4iph_avcbd_perror(msg, rc);
	exit(1);
	return rc;
}
#else
#define vpu_err(enc, func, line, rc) (rc)
#endif

void debug_printf(const char *fmt, ...)
{
#ifdef DEBUG
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
#endif
}


/* Forward declarations */
static int decode_frame(SHCodecs_Decoder * decoder);
static int extract_frame(SHCodecs_Decoder * decoder, long frame_index);
static int get_input(SHCodecs_Decoder * decoder, void *dst);

static int stream_init(SHCodecs_Decoder * decoder);
static int decoder_init(SHCodecs_Decoder * decoder);
static int decoder_start(SHCodecs_Decoder * decoder);

/***********************************************************/

/*
 * init ()
 */
SHCodecs_Decoder *shcodecs_decoder_init(int width, int height, SHCodecs_Format format)
{
	SHCodecs_Decoder *decoder;

	if ((decoder = calloc(1, sizeof(*decoder))) == NULL)
		return NULL;

	decoder->format = format;
	decoder->si_max_fx = width;
	decoder->si_max_fy = height;

	decoder->decoded_cb = NULL;
	decoder->decoded_cb_data = NULL;

	decoder->frame_count = 0;
	decoder->last_cb_ret = 0;

	/* H.264 spec: Max NAL size is the size of an uncomrpessed immage divided
	   by the "Minimum Compression Ratio", MinCR. This is 2 for most levels
	   but is 4 for levels 3.1 to 4. Since we don't know the level, we use
	   MinCR=2 for sizes upto D1 and MinCR=4 for over D1. */
	decoder->max_nal_size = (width * height * 3) / 2; /* YCbCr420 */
	decoder->max_nal_size /= 2; /* Apply MinCR */
	if (width*height > 720*576)
		decoder->max_nal_size /= 2; /* Apply MinCR */

	/* Initialize m4iph */
	if (m4iph_vpu_open(decoder->max_nal_size) < 0) {
		shcodecs_decoder_close (decoder);
		return NULL;
	}

	/* Stream initialize */
	if (stream_init(decoder)) {
		/* stream_init() prints the specific error message */
		shcodecs_decoder_close(decoder);
		return NULL;
	}
	/* Decoder initialize */
	if (decoder_init(decoder)) {
		shcodecs_decoder_close(decoder);
		return NULL;
	}

	return decoder;
}

/*
 * close ()
 */
void shcodecs_decoder_close(SHCodecs_Decoder * decoder)
{
	int i;
	int size_of_Y;

	if (!decoder) return;

	size_of_Y = ((decoder->si_max_fx + 15) & ~15) * ((decoder->si_max_fy + 15) & ~15);

	if (decoder->context) {
		free (decoder->context);
		decoder->context = NULL;
	}
	if (decoder->nal_buf) {
		free (decoder->nal_buf);
		decoder->nal_buf = NULL;
	}
	if (decoder->frames) {
		for (i = 0; i < decoder->num_frames; i++) {
			if (decoder->frames[i].Y_fmemp)
				m4iph_sdr_free(decoder->frames[i].Y_fmemp, size_of_Y);
		}
		free (decoder->frames);
		decoder->frames = NULL;
	}
	if (decoder->vui_data) {
		free (decoder->vui_data);
		decoder->vui_data = NULL;
	}
	if (decoder->sei_data) {
		free (decoder->sei_data);
		decoder->sei_data = NULL;
	}
	if (decoder->vpuwork1) {
		m4iph_sdr_free(decoder->vpuwork1, (size_of_Y * 16)/256);
		decoder->vpuwork1 = NULL;
	}
	if (decoder->vpuwork2) {
		m4iph_sdr_free(decoder->vpuwork2, (size_of_Y * 64)/256);
		decoder->vpuwork2 = NULL;
	}

	m4iph_vpu_close();

	free(decoder);
}

int
shcodecs_decoder_set_frame_by_frame (SHCodecs_Decoder * decoder, int frame_by_frame)
{
	if (!decoder) return -1;

	decoder->frame_by_frame = frame_by_frame;

	return 0;
}

int
shcodecs_decoder_set_use_physical (SHCodecs_Decoder * decoder, int use_physical)
{
	if (!decoder) return -1;

	decoder->use_physical = use_physical;

	return 0;
}

int
shcodecs_decoder_set_decoded_callback(SHCodecs_Decoder * decoder,
				      SHCodecs_Decoded_Callback decoded_cb,
				      void *user_data)
{
	if (!decoder) return -1;

	decoder->decoded_cb = decoded_cb;
	decoder->decoded_cb_data = user_data;

	return 0;
}

/*
 * Returns number of bytes used.
 */
int
shcodecs_decode(SHCodecs_Decoder * decoder, unsigned char *data, int len)
{
	int nused=0, total_used=0;

	decoder->input_buf = data;
	decoder->last_cb_ret = 0;

	while (len > 0) {
		decoder->input_buf += nused;
		decoder->input_pos = 0;
		decoder->input_len = MIN (decoder->max_nal_size, len);
		decoder->input_size = decoder->input_len;

		if ((nused = decoder_start(decoder)) <= 0)
			break;

		total_used += nused;
		len -= nused;

		if (decoder->last_cb_ret != 0)
			break;
	}

	return total_used;
}

int
shcodecs_decoder_finalize (SHCodecs_Decoder * decoder)
{
	decoder->needs_finalization = 1;

	return decoder_start (decoder);
}

int
shcodecs_decoder_get_frame_count (SHCodecs_Decoder * decoder)
{
	if (decoder == NULL) return -1;

	return decoder->frame_count;
}

/***********************************************************/

/*
 * stream_init.
 *
 */
static int stream_init(SHCodecs_Decoder * decoder)
{
	int i;
	int size_of_Y;
	void *pv_wk_buff;
	long stream_mode;
	unsigned char *pBuf;
	long rc;
	const long max_pic_params = 256;

	avcbd_start_decoding();

	stream_mode = (decoder->format == SHCodecs_Format_H264) ? AVCBD_TYPE_AVC : AVCBD_TYPE_MPEG4;

	/* Get context size */
	decoder->context_size = avcbd_get_workarea_size(stream_mode,
								decoder->si_max_fx,
								decoder->si_max_fy,
								max_pic_params);
	if (decoder->context_size < 0)
		return vpu_err(decoder, __func__, __LINE__, decoder->context_size);

	/* Allocate context memory */
	decoder->context = calloc(1, decoder->context_size);
	if (!decoder->context) goto err;

	if (decoder->format == SHCodecs_Format_H264) {
		decoder->nal_buf = malloc(decoder->max_nal_size);
		if (!decoder->nal_buf) goto err;

		decoder->vui_data = calloc(1, sizeof(TAVCBD_VUI_PARAMETERS));
		if (!decoder->vui_data) goto err;

		decoder->sei_data = malloc(sizeof(TAVCBD_SEI));
		if (!decoder->sei_data) goto err;
	}

	size_of_Y = ((decoder->si_max_fx + 15) & ~15) * ((decoder->si_max_fy + 15) & ~15);
	decoder->si_mbnum = size_of_Y >> 8;

	/* Number of reference frames */
	/* For > D1, limit the number of reference frames to 2. This is a
	   pragmatic approach when we don't know the number of reference
	   frames in the stream... */
	decoder->num_frames = CFRAME_NUM;
	if (size_of_Y > (720*576)) {
		decoder->num_frames = 2;
	}
	decoder->frames = calloc(decoder->num_frames, sizeof(FrameInfo));
	if (!decoder->frames) goto err;

	for (i = 0; i < decoder->num_frames; i++) {
		/*
 		 * Frame memory should be aligned on a 32-byte boundary.
		 * Although the VPU requires 16 bytes alignment, the
		 * cache line size is 32 bytes on the SH4.
		 */
		pBuf = m4iph_sdr_malloc(size_of_Y + size_of_Y/2, 32);
		if (!pBuf) goto err;

		decoder->frames[i].Y_fmemp = pBuf;
		decoder->frames[i].C_fmemp = pBuf + size_of_Y;
	}

	decoder->vpuwork1 = m4iph_sdr_malloc((size_of_Y * 16)/256, 32);
	if (!decoder->vpuwork1) goto err;

	decoder->vpuwork2 = m4iph_sdr_malloc((size_of_Y * 64)/256, 32);
	if (!decoder->vpuwork2) goto err;

	rc = avcbd_init_sequence(
			decoder->context, decoder->context_size,
			decoder->num_frames, decoder->frames,
			decoder->si_max_fx, decoder->si_max_fy,
			max_pic_params,
			decoder->vpuwork1,
			decoder->vpuwork2, stream_mode,
			&pv_wk_buff);
	if (rc != 0)
		return vpu_err(decoder, __func__, __LINE__, rc);


	if (decoder->format == SHCodecs_Format_H264) {
		avcbd_set_resume_err (decoder->context, 0, AVCBD_CNCL_REF_TYPE1);
	}

	return 0;

err:
	return -1;
}

/*
 * decoder_init
 *
 */
static int decoder_init(SHCodecs_Decoder * decoder)
{
	if (decoder->format == SHCodecs_Format_H264) {
		avcbd_init_memory_optional(decoder->context, AVCBD_VUI,
					   decoder->vui_data,
					   sizeof(TAVCBD_VUI_PARAMETERS));
		avcbd_init_memory_optional(decoder->context, AVCBD_SEI,
					   decoder->sei_data,
					   sizeof(TAVCBD_SEI));
#ifdef ANNEX_B
		avcbd_set_decode_mode(decoder->context, AVCBD_UNIT_NAL);
#else
		avcbd_set_decode_mode(decoder->context, AVCBD_UNIT_NO_ANNEX_B);
#endif
	}

	decoder->frame_count = 0;
	return 0;
err:
	return -1;
}

/*
 * decoder_start
 *
 */
static int decoder_start(SHCodecs_Decoder * decoder)
{
	int decoded, dpb_mode;
	int cb_ret=0;

	do {
		decoded = 0;

		if (decode_frame(decoder) < 0) {
			debug_printf("%s: %d frames decoded\n", __func__, decoder->frame_count);

			if (!decoder->needs_finalization) {
				debug_printf("%s: need data!\n", __func__);
				goto need_data;
			}

			decoded = 0;
			if (decoder->format == SHCodecs_Format_H264)
				dpb_mode = 0;
			else {
				dpb_mode = 1;
			}
		} else {
			decoded = 1;
			dpb_mode = 0;
		}

		while (cb_ret == 0) {
			long index = avcbd_get_decoded_frame(decoder->context, dpb_mode);

			if (index < 0) {
				decoder->last_cb_ret = cb_ret;
				break;
			} else {
				cb_ret = extract_frame(decoder, index);
				decoder->last_cb_ret = cb_ret;
			}
		}

		debug_printf("%s: %16d,dpb_mode=%d\n", __func__, decoder->frame_count, dpb_mode);
	} while (decoded && cb_ret == 0);

need_data:

	/* Return number of bytes consumed */
	return decoder->input_pos;
}

/*
 * increment_input()
 *
 * Work through the passed-in buffer.
 */
static int increment_input (SHCodecs_Decoder * decoder, int len)
{
	int current_pos = decoder->input_pos + len;
	int rem = decoder->input_size - current_pos;
	int count;

	debug_printf("%s: IN, rem=%d\n", __func__, rem);

	if ((size_t)current_pos <= decoder->input_size) {
		decoder->input_pos = current_pos;
	} else {
		return -1;
	}

	return 0;
}

/*
 * decode_frame()
 *
 * Decode one frame.
 */
static int decode_frame(SHCodecs_Decoder * decoder)
{
	int err, ret, i;
	int max_mb;
	TAVCBD_FRAME_SIZE frame_size;
	static long counter = 0;
	int input_len;
	TAVCBD_LAST_FRAME_STATUS status;

	max_mb = decoder->si_mbnum;
	do {
		int curr_len = 0;
		err = -1;

		if ((input_len = get_input(decoder, decoder->nal_buf) <= 0)) {
			return -2;
		}
		if (decoder->format == SHCodecs_Format_H264) {
			unsigned char *input = (unsigned char *)decoder->nal_buf;
			long len = decoder->input_len;

			debug_printf
			    ("%s: H.264 len %d: %02x%02x%02x%02x %02x%02x%02x%02x\n", __func__,
			     decoder->input_len, input[0], input[1],
			     input[2], input[3], input[4], input[5],
			     input[6], input[7]);

#ifndef ANNEX_B
			/* skip "00.. 00 01" to simulate RTP */
			while (*input == 0) {
				input++;
				len--;
			}
			input++;
			len--;
#endif
			ret = avcbd_set_stream_pointer(decoder->context, input, len, NULL);
			if (ret < 0)
				return vpu_err(decoder, __func__, __LINE__, ret);
		} else {
			unsigned char *ptr;
			long hosei = 0;

			debug_printf("%s: MPEG4 ptr = input + ipos %d\n", __func__, decoder->input_pos);

			ptr = decoder->input_buf + decoder->input_pos;

			ret = avcbd_search_vop_header(decoder->context,
						      ptr,
						      decoder->input_len);

			if (ret < 0) {
				(void) vpu_err(decoder, __func__, __LINE__, ret);

				if (decoder->needs_finalization) {
					if (*ptr != 0 || *(ptr + 1) != 0) {
						break;
					}
				} else {
					break;
				}
			}

			if (decoder->input_len & 31)
				hosei = 31;
			else
				hosei = 0;
#if 0 /* WTF? */
			if (counter) {
				for (i = 0; i < 16; i++) {
					*(ptr + decoder->input_len + i) = 0;
				}
			}
#endif
			ret = avcbd_set_stream_pointer(decoder->context,
						 decoder->input_buf + decoder->input_pos,
						 decoder->input_len + hosei, NULL);
			if (ret < 0)
				return vpu_err(decoder, __func__, __LINE__, ret);
		}

		m4iph_vpu_lock();
		ret = avcbd_decode_picture(decoder->context, decoder->input_len * 8);
		if (ret < 0)
			(void) vpu_err(decoder, __func__, __LINE__, ret);

		debug_printf
		    ("%s: avcbd_decode_picture returned %d\n", __func__, ret);
		ret = avcbd_get_last_frame_stat(decoder->context, &status);
		m4iph_vpu_unlock();
		if (ret < 0)
			return vpu_err(decoder, __func__, __LINE__, ret);

		counter = 1;

		if (decoder->format == SHCodecs_Format_H264) {
			curr_len = decoder->input_len;
		} else {
			curr_len = (unsigned) (status.read_bits + 7) >> 3;
			decoder->input_len -= curr_len;
			avcbd_get_frame_size(decoder->context, &frame_size);
			decoder->si_fx = frame_size.width;
			decoder->si_fy = frame_size.height;
		}

		if (status.error_num < 0) {
#ifdef DEBUG
			m4iph_avcbd_perror("avcbd_decode_picture()", status.error_num);
#endif
#if 1
			switch (status.error_num) {
			case AVCBD_MB_OVERRUN:
				increment_input(decoder, curr_len);
				err = 0;
				break;
			default:
				break;
			}
#endif
			break;
		}

		/* This is where actual input data is read */
		if (increment_input(decoder, curr_len) < 0) {
			err = 0;
			break;
		}

		debug_printf
		    ("%s: status.read_slices = %d\n", __func__, status.read_slices);
		debug_printf
		    ("%s: status.last_macroblock_pos = %d (< max_mb %d?)\n", __func__,
		     status.last_macroblock_pos,
		     max_mb);

		if (status.detect_param & AVCBD_SPS) {
			avcbd_get_frame_size(decoder->context, &frame_size);
			decoder->si_fx = frame_size.width;
			decoder->si_fy = frame_size.height;
			max_mb = ((unsigned)(frame_size.width + 15) >> 4) *
				((unsigned)(frame_size.height + 15) >> 4);
			decoder->si_mbnum = max_mb;
		}
		err = 0;
	}
	while ((status.read_slices == 0)
	       || (status.last_macroblock_pos < max_mb));

	if (!err) {
		return 0;
	} else {
		return -1;
	}
}

/*
 * extract_frame: extract a decoded frame from the VPU
 *
 */
static int extract_frame(SHCodecs_Decoder * decoder, long frame_index)
{
	FrameInfo *frame = &decoder->frames[frame_index];
	unsigned char *yf, *cf;
	int cb_ret=0;
	int size_of_Y = decoder->si_max_fx * decoder->si_max_fy;

	debug_printf("%s: output frame %d, frame_index=%d\n", __func__, decoder->frame_count, frame_index);

	/* Call user's output callback */
	if (decoder->decoded_cb) {
		if (decoder->use_physical) {
			yf = frame->Y_fmemp;
		} else {
			yf = m4iph_addr_to_virt(frame->Y_fmemp);
		}

		cf = yf + size_of_Y;

		cb_ret = decoder->decoded_cb(decoder,
			yf, size_of_Y,
			cf, size_of_Y/2,
			decoder->decoded_cb_data);
	}

	decoder->frame_count++;

	return cb_ret;
}

/*
 * usr_get_input_h264()
 *
 * Set up a slice (H.264)
 *
 */
static int usr_get_input_h264(SHCodecs_Decoder * decoder, void *dst)
{
	long len, size = 0;

	len = decoder->input_size - decoder->input_pos;

	/* Always keep a buffer of lookahead data, unless we are finalizing.
	 * The amount to keep is a heuristic based on the likely size of a
	 * large encoded frame.
	 * By returning 0 early, we force the application to either push more
	 * data or (if there is no more) to finalize.
	 */
	if (!decoder->needs_finalization && len < (decoder->si_max_fx*decoder->si_max_fy/4)) {
		debug_printf("%s: not enough data, going back for more\n", __func__);
		return 0;
	}

	/* skip pre-gap */
	size = avcbd_search_start_code(
	              decoder->input_buf + decoder->input_pos,
	              (decoder->input_size - decoder->input_pos) * 8,
		  0x01);

	if (size < 0) {
#ifdef DEBUG
		m4iph_avcbd_perror("avcbd_search_start_code()", size);
#endif
		return -1;
	}

	decoder->input_pos += size;

	/* transfer one block excluding "(00 00) 03" */
	size = avcbd_extract_nal(
		decoder->input_buf + decoder->input_pos,
		dst,
		decoder->input_size - decoder->input_pos,
		3);

	if (size <= 0) {
		m4iph_avcbd_perror("avcbd_extract_nal()", size);
	} else
		decoder->input_len = size;

	return size;
}

/*
 * usr_get_input_mpeg4()
 *
 * Set up one frame (MPEG-4/H.263)
 *
 */
static int usr_get_input_mpeg4(SHCodecs_Decoder * decoder, void *dst)
{
	long len, ret=0;
	int i;
	unsigned char * c = decoder->input_buf + decoder->input_pos;

	/* When receiving data frame-by-frame, there is no need to go
	 * looking for frame boundaries.  */
	if (decoder->frame_by_frame)
		return decoder->input_len;

	len = decoder->input_size - decoder->input_pos;
	if (len < 3) return -2;

	if (decoder->needs_finalization) {
		debug_printf("%s: finalizing, returning %ld\n", __func__, len);
		return len;
	}

	/* Always keep a buffer of lookahead data, unless we are finalizing.
	 * The amount to keep is a heuristic based on the likely size of a
	 * large encoded frame.
	 * By returning 0 early, we force the application to either push more
	 * data or (if there is no more) to finalize.
	 */
	if (len < (decoder->si_max_fx*decoder->si_max_fy/4)) {
		debug_printf("%s: not enough data, going back for more\n", __func__);
		return 0;
	}

	return len;
}

/*
 * get_input()
 *
 * Set up a slice (H.264) or frame (MPEG-4/H.263)
 */
static int get_input(SHCodecs_Decoder * decoder, void *dst)
{
	if (decoder->format == SHCodecs_Format_H264) {
		return usr_get_input_h264(decoder, dst);
	} else {
		return usr_get_input_mpeg4(decoder, dst);
	}
}
