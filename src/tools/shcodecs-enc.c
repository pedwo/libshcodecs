/*****************************************************************************
*                                                                            *
*  SuperH MEPG-4 Video Encode Library                                        *
*    (User Application Sample source for VPU4)                               *
*                                                                            *
*    Copyright (C) Renesas Technology Corp., 2005. All rights reserved.      *
*                                                                            *
*    Version@2.0 : avcbencsmp_mpeg4.c					     *
*				 2005/07/27 14:30 Renesas Technology Corp.   *
*                                                                            *
*****************************************************************************/

#include <stdio.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/time.h>

#include "avcbe.h"		/* SuperH MEPG-4&H.264 Video Encode Library Header */
#include "m4iph_vpu4.h"		/* SuperH MEPG-4&H.264 Video Driver Library Header */
#include "avcbencsmp.h"		/* User Application Sample Header */

#include "avcbe_inner.h"
#include "QuantMatrix.h"

#include <shcodecs/shcodecs_encoder.h>

extern char *dummy_nal_buf;

/*** Stream-output buffer to receive an encoding result in every one frame ***/

//static unsigned long sdr_read_my_stream_buff[MY_STREAM_BUFF_SIZE/4];          /* 4 bytes alignmen */   

/*** Other work-field area ***/
// #ifdef MULTI_STREAM             /* In the case of multiple streams */
// #define MY_MB_WORK_AREA_SIZE       78000   /* QCIF size DataPartioning=ON, Bitrate=64000/256000 : (6500 + 21500) + (9500 + 40000 + more) */
// #else   /* In the case of the number of the streams to encode is one */
// #define MY_MB_WORK_AREA_SIZE       28000   /* QCIF size DataPartioning=ON, Bitrate=64000 : 6500 + 21500 */
// #endif /* MULTI_STREAM */

//#define MY_MB_WORK_AREA_SIZE  76800  /*TODO hardcoded, remove later */
//static unsigned long my_mb_work_area[MY_MB_WORK_AREA_SIZE/4]; /* 4 bytes alignmen */
//#define my_sdr_mb_work_area sdr_base+(MY_MB_WORK_AREA_SIZE*2) /* 4 bytes alignmen */

extern avcbe_stream_info *my_context;
extern TAVCBE_FMEM LDEC_ARRY[];
extern TAVCBE_FMEM CAPTF_ARRY[];

int open_input_image_file(APPLI_INFO *);
int open_output_file(APPLI_INFO *);
void disp_context_info(void *context);

extern int GetFromCtrlFTop(const char *control_filepath,
			   ENC_EXEC_INFO * enc_exec_info,
			   long *stream_type);

/* Top of the user application sample source to encode */
/*int mpeg4_enc(void) */
APPLI_INFO ainfo;		/* User Application Data */

unsigned long m4iph_vpu4_reg_base = 0xfe900000;
#define KERNEL_MEMORY_FOR_VPU_BOTTOM 0xadffffff

void get_new_stream_buf(avcbe_stream_info * context,
			char *previous_stream_buff, long output_size,
			char **next_stream_buff, long *stream_buff_size);

void file_name_copy(void)
{
	strcpy(ainfo.file_path_buf_1, ainfo.enc_exec_info.buf_input_yuv_file_with_path);	/* 入力ディレクトリ */
	strcpy(ainfo.file_path_buf_2, ainfo.enc_exec_info.buf_output_directry);	/* 出力ディレクトリ */
	strcpy(ainfo.input_file_name_buf, ainfo.file_path_buf_1);
	strcat(ainfo.input_file_name_buf, "\\");
	strcpy(ainfo.output_file_name_buf, ainfo.file_path_buf_2);
	strcat(ainfo.output_file_name_buf, "\\");
	strcpy(ainfo.local_decode_file_name_buf, ainfo.file_path_buf_2);
	strcat(ainfo.local_decode_file_name_buf, "\\");
	strcpy(ainfo.log_file_name_buf, ainfo.file_path_buf_2);
//              strcat(ainfo.log_file_name_buf, "\\");  // <--- NG
	strcpy(ainfo.capt_file_name_buf, ainfo.file_path_buf_2);
	strcat(ainfo.capt_file_name_buf, "\\");
	strcpy(ainfo.rate_log_file_name_buf, ainfo.file_path_buf_2);
	strcat(ainfo.rate_log_file_name_buf, "\\");

	printf("ainfo.input_file_name_buf = %s \n",
	       ainfo.input_file_name_buf);
	printf("ainfo.output_file_name_buf = %s \n",
	       ainfo.output_file_name_buf);
}

/* SHCodecs_Encoder_Input callback for acquiring an image from the input file */
static int get_input(SHCodecs_Encoder * encoder,
		     unsigned long *addr_y, unsigned long *addr_c,
		     void *user_data)
{
	APPLI_INFO *appli_info = (APPLI_INFO *) user_data;
	return load_1frame_from_image_file(appli_info, addr_y, addr_c);
}

/* SHCodecs_Encoder_Output callback for writing encoded data to the output file */
static int write_output(SHCodecs_Encoder * encoder,
			unsigned char *data, int length, void *user_data)
{
	APPLI_INFO *appli_info = (APPLI_INFO *) user_data;
	return fwrite(data, 1, length, appli_info->output_file_fp);
}

int main(int argc, char *argv[])
{
	int encode_return_code;
	char message_buf[256];
	int return_code;
	long stream_type, width_height, max_frame;	/* 041201 */
	SHCodecs_Encoder *encoder;

	if (argc == 2) {	/* 第1引数=コントロールファイル */
		strcpy(ainfo.ctrl_file_name_buf, argv[1]);
		return_code =
		    GetFromCtrlFTop((const char *)
				    ainfo.ctrl_file_name_buf,
				    &(ainfo.enc_exec_info), &stream_type);
		if (return_code < 0) {
			printf("Can't Open to Ctrolfile : %s\n", ainfo.ctrl_file_name_buf);	/* 041217 */
			return (-1);
		}
#if 1
		strcpy(ainfo.file_path_buf_1, ainfo.enc_exec_info.buf_input_yuv_file_with_path);	/* 入力ディレクトリ */
		strcpy(ainfo.file_path_buf_2, ainfo.enc_exec_info.buf_output_directry);	/* 出力ディレクトリ */
		strcpy(ainfo.input_file_name_buf, ainfo.file_path_buf_1);
		strcat(ainfo.input_file_name_buf, "/");
		strcat(ainfo.input_file_name_buf,
		       ainfo.enc_exec_info.buf_input_yuv_file);
		strcpy(ainfo.output_file_name_buf, ainfo.file_path_buf_2);
		strcat(ainfo.output_file_name_buf, "/");
		strcat(ainfo.output_file_name_buf,
		       ainfo.enc_exec_info.buf_output_stream_file);
#if 1
		strcpy(ainfo.local_decode_file_name_buf,
		       ainfo.file_path_buf_2);
		strcat(ainfo.local_decode_file_name_buf, "/");
		strcpy(ainfo.log_file_name_buf, ainfo.file_path_buf_2);
//              strcat(ainfo.log_file_name_buf, "/");   // <--- NG
		strcpy(ainfo.capt_file_name_buf, ainfo.file_path_buf_2);
		strcat(ainfo.capt_file_name_buf, "/");
		strcpy(ainfo.rate_log_file_name_buf,
		       ainfo.file_path_buf_2);
		strcat(ainfo.rate_log_file_name_buf, "/");
		printf
		    ("!!!!!!!! ainfo.rate_log_file_name_buf = %s !!!!!!!!\n",
		     ainfo.rate_log_file_name_buf);
		printf("ainfo.input_file_name_buf = %s \n",
		       ainfo.input_file_name_buf);
		printf("ainfo.output_file_name_buf = %s \n",
		       ainfo.output_file_name_buf);
#endif

#else
		file_name_copy();
#endif				/*  */
	} else {
		DisplayMessage
		    ("usage argv[0] case_number input_base_dir output_base_dir",
		     1);
		return -1;
	}

	encoder =
	    shcodecs_encoder_init(ainfo.enc_exec_info.xpic,
				  ainfo.enc_exec_info.ypic, stream_type, &ainfo);

	shcodecs_encoder_set_input_callback(encoder, get_input, &ainfo);
	shcodecs_encoder_set_output_callback(encoder, write_output,
					     &ainfo);

	/*--- open input YUV data file (one of the user application's own
	 *  functions) ---*/
	return_code = open_input_image_file(&ainfo);
	if (return_code != 0) {	/* error */
		DisplayMessage(" open_input_image_file ERROR! ", 1);
		return (-6);
	}

	/*--- open output file (one of the user application's own functions) ---*/
	return_code = open_output_file(&ainfo);
	if (return_code != 0) {	/* error */
		DisplayMessage("  encode_1file:open_output_file ERROR! ",
			       1);
		return (-6);
	}

	encode_return_code = shcodecs_encoder_run(encoder, &ainfo);

	if (encode_return_code < 0) {	/* encode error */
		sprintf(message_buf, "Encode Error  code=%d ",
			encode_return_code);
		DisplayMessage(message_buf, 1);
	} else {		/* encode success */
		sprintf(message_buf, "Encode Success ");
		DisplayMessage(message_buf, 1);
	}

	shcodecs_encoder_close(encoder);

	free(dummy_nal_buf);
	printf("Total encode time = %d(msec)\n", encode_time_get());
	printf("Total sleep  time = %d(msec)\n", m4iph_sleep_time_get());
	/* TODO vpu4_reg_close(); */
}
