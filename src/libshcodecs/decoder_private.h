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
	int		si_valid;	/* Is stream valid ? */
	int		*si_ctxt;	/* Pointer to context */
	int		si_ctxt_size;	/* Size of context */
	int		si_type;	/* Type of stream */
	unsigned char   *si_input;	/* Pointer to input buffer */
	unsigned char	*si_nalb;	/* NAL Buffer for H.264 */ 
	int		si_ipos;	/* Current position in input stream */
	int		si_ilen;	/* Size of current frame/slice */
	size_t		si_isize;	/* Total size of input data */
	FrameInfo	*si_flist;
	int		si_fnum;	/* Number of frames in temp frame list */
	int		si_fx;		/* Width of frame */
	int		si_fy;		/* Height of frame */
	int		si_max_fx;	/* Maximum frame width */
	int		si_max_fy;	/* Maximum frame height */
	int		si_mbnum;	/* Size in macro blocks */
	long		*si_dp_264;	/* Data partition pointers */
	long		*si_dp_m4;	/* Only valid for MPEG-4 data. */
	TAVCBD_VUI_PARAMETERS *si_vui; 	/* Only for H.264 data. */
	TAVCBD_SEI 	*si_sei;	/* Only for H.264 data. */
	FrameInfo	si_ff;		/* Filtered frame */

	TAVCBD_LAST_FRAME_STATUS last_frame_status;

	SHCodecs_Decoded_Callback decoded_cb;
	void		*decoded_cb_data;

	long		index_old;
	int		needs_finalization;
	int		frame_by_frame;
	int		use_physical;
	int		frame_count;
	int		last_cb_ret;
	int		max_nal_size;
};


#endif /* _DECODER_PRIVATE_H_ */
