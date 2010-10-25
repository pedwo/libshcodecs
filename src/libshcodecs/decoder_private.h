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

#ifndef _DECODER_PRIVATE_H_
#define _DECODER_PRIVATE_H_

#define CFRAME_NUM		4

typedef TAVCBD_FMEM FrameInfo;

struct SHCodecs_Decoder {
	void	*vpu;
	int		*context;	/* Pointer to context */
	int		context_size;	/* Size of context */
	int		format;		/* Type of stream */
	unsigned char   *input_buf;	/* Pointer to input buffer */
	unsigned char	*nal_buf;	/* NAL Buffer for H.264 */ 
	int		input_pos;	/* Current position in input stream */
	int		input_len;	/* Size of current frame/slice */
	size_t		input_size;	/* Total size of input data */
	FrameInfo	*frames;
	int		num_frames;	/* Number of frames in temp frame list */
	int		si_fx;		/* Width of frame */
	int		si_fy;		/* Height of frame */
	int		si_max_fx;	/* Maximum frame width */
	int		si_max_fy;	/* Maximum frame height */
	int		si_mbnum;	/* Size in macro blocks */
	long		*vpuwork1;	/* Data partition pointers */
	long		*vpuwork2;	/* Only valid for MPEG-4 data. */
	TAVCBD_VUI_PARAMETERS *vui_data; 	/* Only for H.264 data. */
	TAVCBD_SEI 	*sei_data;	/* Only for H.264 data. */

	SHCodecs_Decoded_Callback decoded_cb;
	void		*decoded_cb_data;

	int		needs_finalization;
	int		frame_by_frame;
	int		use_physical;
	unsigned long frame_addr_y;
	unsigned long frame_addr_c;
	int		frame_count;
	int		last_cb_ret;
	int		max_nal_size;
};


#endif /* _DECODER_PRIVATE_H_ */
