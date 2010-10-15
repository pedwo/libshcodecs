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
#include <limits.h>
#include <ctype.h>
#include <string.h>

#include "avcbencsmp.h"

#include <shcodecs/shcodecs_encoder.h>

/* Read 1 frame of YCbCr420 semi-planar data */
int read_1frame_YCbCr420sp(FILE *fh, int w, int h, unsigned char *pY, unsigned char *pC)
{
	int read;

	/* Luma */	
	read = fread(pY, 1, w * h, fh);
	if (read < (w * h))
		return 1;

	/* Packed chroma */	
	read = fread(pC, 1, w * h / 2, fh);
	if (read < ( w * h / 2))
		return 1;

	return 0;
}

int open_input_image_file(APPLI_INFO * appli_info)
{
	appli_info->frame_counter_of_input = 0;

	if (!strcmp (appli_info->input_file_name_buf, "-"))
		appli_info->input_yuv_fp = stdin;
	else
		appli_info->input_yuv_fp = fopen(appli_info->input_file_name_buf, "rb");


	if (appli_info->input_yuv_fp == NULL) {
		fprintf(stderr, "ERROR: can't open file %s \n",
		        appli_info->input_file_name_buf);
		return (-1);
	}

	return (0);
}

void close_input_file(APPLI_INFO * appli_info)
{
	FILE *fp = appli_info->input_yuv_fp;

	if (fp != NULL)
		fclose(fp);
}

/* copy yuv data to the image-capture-field area each frame */
int load_1frame_from_image_file(SHCodecs_Encoder * encoder,
                                APPLI_INFO * appli_info)
{
	int read_size;
	long hsiz, ysiz, index, index2;
	FILE *input_yuv_fp;
	unsigned char *w_addr_yuv;
	unsigned long wnum;
	unsigned char *CbCr_ptr, *Cb_buf_ptr, *Cr_buf_ptr, *ptr;

	if (appli_info->frame_counter_of_input == appli_info->frames_to_encode) {
		return (1);
	}
	input_yuv_fp = appli_info->input_yuv_fp;
	hsiz = shcodecs_encoder_get_width(encoder);
	ysiz = shcodecs_encoder_get_height(encoder);
	appli_info->frame_counter_of_input++;

	if (input_yuv_fp == NULL) {
		return (-1);
	}
	/* Input file data to user memory */
	w_addr_yuv = malloc(hsiz * ysiz * 2);
	if (w_addr_yuv == NULL) {
		fprintf(stderr, "load_1frame_from_image_file: malloc error.\n");
		exit(-1);
	}
	memset(w_addr_yuv, 0, hsiz * ysiz);
	read_size = fread(w_addr_yuv, 1, hsiz * ysiz, input_yuv_fp);
	if (read_size <= 1) {
		free(w_addr_yuv);
		return (1);
	}

	/* write memory data size offset make to multiples of 16 */
	wnum = (((hsiz + 15) / 16) * 16) * (((ysiz + 15) / 16) * 16);
	/* Input file data to user memory */
	CbCr_ptr = malloc(hsiz * ysiz / 2);
	Cb_buf_ptr = malloc(hsiz * ysiz / 2);
	Cr_buf_ptr = malloc(hsiz * ysiz / 2);
	if (Cb_buf_ptr == NULL || Cr_buf_ptr == NULL) {
		fprintf(stderr, "malloc error \n");
		exit(-1);
	}
	if (appli_info->yuv_CbCr_format == 1) {	/* 1:CbSCrS */
		ptr = CbCr_ptr;
		read_size =
		    fread(Cb_buf_ptr, 1, (hsiz * ysiz / 4), input_yuv_fp);
		read_size =
		    fread(Cr_buf_ptr, 1, (hsiz * ysiz / 4), input_yuv_fp);
		for (index = 0; index < (hsiz * ysiz / 4); index++) {
			*ptr++ = *(Cb_buf_ptr + index);
			*ptr++ = *(Cr_buf_ptr + index);
		}
	} else if (appli_info->yuv_CbCr_format == 2) {	/* 2:Cb0,Cr0,Cb1,Cr1,... */
		read_size =
		    fread(CbCr_ptr, 1, ((hsiz * ysiz / 4) * 2),
			  input_yuv_fp);
		if (read_size <= 1) {
			return (-1);
		}
	} else if (appli_info->yuv_CbCr_format == 3) {	/* 3:Cb1C,Cr1C,... */
		ptr = CbCr_ptr;
		for (index = 0; index < (ysiz / 2); index++) {
			read_size =
			    fread(Cb_buf_ptr, (sizeof(unsigned char)),
				  (hsiz / 2), input_yuv_fp);
			read_size =
			    fread(Cr_buf_ptr, (sizeof(unsigned char)),
				  (hsiz / 2), input_yuv_fp);
			for (index2 = 0; index2 < (hsiz / 2); index2++) {
				*ptr++ = *(Cb_buf_ptr + index2);
				*ptr++ = *(Cr_buf_ptr + index2);
			}
		}
	} else {
		read_size =
		    fread(CbCr_ptr, 1, ((hsiz * ysiz / 4) * 2),
			  input_yuv_fp);
		if (read_size <= 1) {
			return (-1);
		}
	}

	/* Write image data to kernel memory for VPU */
	shcodecs_encoder_encode_1frame(encoder, w_addr_yuv, CbCr_ptr, NULL, 0);

	free(w_addr_yuv);
	free(Cb_buf_ptr);
	free(Cr_buf_ptr);
	free(CbCr_ptr);

	return (0);
}


int open_output_file(APPLI_INFO * appli_info)
{
	const char *fname = appli_info->output_file_name_buf;
	FILE *fp;

	if (!strcmp (fname, "-"))
		fp = stdout;
	else
		fp = fopen(fname, "wb");

	appli_info->output_file_fp = fp;

	if (fp == NULL) {
		perror("Error opening output file\n");
		return -1;
	}

	return 0;
}

void close_output_file(APPLI_INFO * appli_info)
{
	FILE *fp = appli_info->output_file_fp;

	if (fp != NULL) {
		fflush(fp);
		fclose(fp);
	}
}

int write_output_file(APPLI_INFO * appli_info, unsigned char *data, int length)
{
	if (fwrite(data, 1, length, appli_info->output_file_fp) == (size_t)length) {
		return 0;
	} else {
		return -1;
	}
}

