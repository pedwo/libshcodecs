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
#include <string.h>
#include <stdlib.h>

#include "avcbencsmp.h"

#include <shcodecs/shcodecs_encoder.h>

/* サブ関数 */
/* キーワードが一致する行を探し、その行の"="と";"の間の文字列を引数buf_valueに入れて返す */
static int ReadUntilKeyMatch(FILE * fp_in, const char *key_word, char *buf_value)
{
	char buf_line[256], buf_work_value[256], *pos;
	int line_length, keyword_length, try_count;

	keyword_length = strlen(key_word);

	try_count = 1;

      retry:;
	while (fgets(buf_line, 256, fp_in)) {
		line_length = strlen(buf_line);
		if (line_length < keyword_length) {
			continue;
		}

		if (strncmp(key_word, &buf_line[0], keyword_length) == 0) {
			pos = strchr(&buf_line[keyword_length], '=');
			if (pos == NULL) {
				return 0;
				/* キーワードに一致する行は見つかったが、"="が見つからなかった */
				;
			}
			strcpy(buf_work_value, (pos + 2));
			pos = strchr(&buf_work_value[1], ';');
			if (pos == NULL) {
				return 0;
				/* キーワードに一致する行は見つかったが、";"が見つからなかった */
				;
			} else {
				*pos = '\0';
			}

			strcpy(buf_value, buf_work_value);
			return 1;	/* 見つかった */
		}
	}

	/* 見つからなかったときは、ファイルの先頭に戻る */
	if (try_count == 1) {
		rewind(fp_in);
		try_count = 2;
		goto retry;
	} else {
		return 0;	/* 見つからなった */
	}
}

static int GetStringFromCtrlFile(FILE * fp_in, const char *key_word,
			   char *return_string)
{
	int found = 0;

	if ((fp_in == NULL) || (key_word == NULL) || (return_string == NULL))
		return found;

	if (ReadUntilKeyMatch(fp_in, key_word, return_string))
		found = 1;

	return found;
}

static int GetValueFromCtrlFile(FILE * fp_in, const char *key_word,
			  long *value)
{
	char buf_line[256];
	long found;

	found = 0;
	*value = 0;

	if ((fp_in == NULL) || (key_word == NULL)) {
		return 0;
	}

	if (ReadUntilKeyMatch(fp_in, key_word, &buf_line[0])) {
		found = 1;
		*value = atoi((const char *) &buf_line[0]);
	}

	return found;
}


struct enc_options_t
{
	const char *name;
	long (*set_fn)(SHCodecs_Encoder * encoder, long value);
};

struct enc_uoptions_t
{
	const char *name;
	unsigned long (*set_fn)(SHCodecs_Encoder * encoder, unsigned long value);
};

static const struct enc_options_t options[] = {
	{ "stream_type", &shcodecs_encoder_set_stream_type},
	{ "bitrate", &shcodecs_encoder_set_bitrate },
	{ "x_pic_size", &shcodecs_encoder_set_xpic_size },
	{ "y_pic_size", &shcodecs_encoder_set_ypic_size },
	{ "frame_rate", &shcodecs_encoder_set_frame_rate },
	{ "I_vop_interval", &shcodecs_encoder_set_I_vop_interval },
	{ "mv_mode", &shcodecs_encoder_set_mv_mode },
	{ "fcode_forward", &shcodecs_encoder_set_fcode_forward },
	{ "search_mode", &shcodecs_encoder_set_search_mode },
	{ "search_time_fixed", &shcodecs_encoder_set_search_time_fixed },
	{ "rate_ctrl_skip_enable", &shcodecs_encoder_set_ratecontrol_skip_enable },
	{ "rate_ctrl_use_prevquant", &shcodecs_encoder_set_ratecontrol_use_prevquant },
	{ "rate_ctrl_respect_type ", &shcodecs_encoder_set_ratecontrol_respect_type },
	{ "rate_ctrl_intra_thr_changeable", &shcodecs_encoder_set_ratecontrol_intra_thr_changeable },
	{ "control_bitrate_length", &shcodecs_encoder_set_control_bitrate_length },
	{ "intra_macroblock_refresh_cycle", &shcodecs_encoder_set_intra_macroblock_refresh_cycle },
	{ "video_format", &shcodecs_encoder_set_video_format },
	{ "noise_reduction", &shcodecs_encoder_set_noise_reduction },
	{ "reaction_param_coeff", &shcodecs_encoder_set_reaction_param_coeff },
	{ "weightedQ_mode", &shcodecs_encoder_set_weightedQ_mode },
};

static const struct enc_uoptions_t h264_options[] = {
	{ "Ivop_quant_initial_value", &shcodecs_encoder_set_h264_Ivop_quant_initial_value },
	{ "Pvop_quant_initial_value", &shcodecs_encoder_set_h264_Pvop_quant_initial_value },
	{ "use_dquant", &shcodecs_encoder_set_h264_use_dquant },
	{ "clip_dquant_next_mb", &shcodecs_encoder_set_h264_clip_dquant_next_mb },
	{ "clip_dquant_frame", &shcodecs_encoder_set_h264_clip_dquant_frame },
	{ "quant_min", &shcodecs_encoder_set_h264_quant_min },
	{ "quant_min_Ivop_under_range", &shcodecs_encoder_set_h264_quant_min_Ivop_under_range },
	{ "quant_max", &shcodecs_encoder_set_h264_quant_max },

	{ "rate_ctrl_cpb_skipcheck_enable ", &shcodecs_encoder_set_h264_ratecontrol_cpb_skipcheck_enable },
	{ "rate_ctrl_cpb_Ivop_noskip", &shcodecs_encoder_set_h264_ratecontrol_cpb_Ivop_noskip },
	{ "rate_ctrl_cpb_remain_zero_skip_enable", &shcodecs_encoder_set_h264_ratecontrol_cpb_remain_zero_skip_enable },
	{ "rate_ctrl_cpb_offset", &shcodecs_encoder_set_h264_ratecontrol_cpb_offset },
	{ "rate_ctrl_cpb_offset_rate", &shcodecs_encoder_set_h264_ratecontrol_cpb_offset_rate },
	{ "rate_ctrl_cpb_buffer_mode", &shcodecs_encoder_set_h264_ratecontrol_cpb_buffer_mode },
	{ "rate_ctrl_cpb_max_size", &shcodecs_encoder_set_h264_ratecontrol_cpb_max_size },
	{ "rate_ctrl_cpb_buffer_unit_size", &shcodecs_encoder_set_h264_ratecontrol_cpb_buffer_unit_size },
	{ "intra_thr_1", &shcodecs_encoder_set_h264_intra_thr_1 },
	{ "intra_thr_2", &shcodecs_encoder_set_h264_intra_thr_2 },
	{ "sad_intra_bias", &shcodecs_encoder_set_h264_sad_intra_bias },
	{ "regularly_inserted_I_type", &shcodecs_encoder_set_h264_regularly_inserted_I_type },
	{ "call_unit", &shcodecs_encoder_set_h264_call_unit },
	{ "use_slice", &shcodecs_encoder_set_h264_use_slice },
	{ "slice_size_mb", &shcodecs_encoder_set_h264_slice_size_mb },
	{ "slice_size_bit", &shcodecs_encoder_set_h264_slice_size_bit },
	{ "slice_type_value_pattern", &shcodecs_encoder_set_h264_slice_type_value_pattern },
	{ "use_mb_partition", &shcodecs_encoder_set_h264_use_mb_partition },
	{ "mb_partition_vector_thr", &shcodecs_encoder_set_h264_mb_partition_vector_thr },
	{ "deblocking_mode", &shcodecs_encoder_set_h264_deblocking_mode },
	{ "use_deblocking_filter_control", &shcodecs_encoder_set_h264_use_deblocking_filter_control },
	{ "me_skip_mode", &shcodecs_encoder_set_h264_me_skip_mode },
	{ "put_start_code", &shcodecs_encoder_set_h264_put_start_code },
	{ "param_changeable", &shcodecs_encoder_set_h264_param_changeable },
	{ "changeable_max_bitrate", &shcodecs_encoder_set_h264_changeable_max_bitrate },

	/* SequenceHeaderParameter */
	{ "seq_param_set_id", &shcodecs_encoder_set_h264_seq_param_set_id },
	{ "profile", &shcodecs_encoder_set_h264_profile },
	{ "constraint_set_flag", &shcodecs_encoder_set_h264_constraint_set_flag },
	{ "level_type", &shcodecs_encoder_set_h264_level_type },
	{ "level_value", &shcodecs_encoder_set_h264_level_value },
	{ "out_vui_parameters", &shcodecs_encoder_set_h264_out_vui_parameters },
	{ "constrained_intra_pred", &shcodecs_encoder_set_h264_constrained_intra_pred },
};

static const struct enc_options_t h264_options2[] = {
	{ "deblocking_alpha_offset", &shcodecs_encoder_set_h264_deblocking_alpha_offset },
	{ "deblocking_beta_offset", &shcodecs_encoder_set_h264_deblocking_beta_offset },
	{ "chroma_qp_index_offset", &shcodecs_encoder_set_h264_chroma_qp_index_offset },
};

static const struct enc_uoptions_t mpeg4_options[] = {
	{ "out_vos", &shcodecs_encoder_set_mpeg4_out_vos },
	{ "out_gov", &shcodecs_encoder_set_mpeg4_out_gov },
	{ "aspect_ratio_info_type", &shcodecs_encoder_set_mpeg4_aspect_ratio_info_type },
	{ "aspect_ratio_info_value", &shcodecs_encoder_set_mpeg4_aspect_ratio_info_value },
	{ "vos_profile_level_type",	shcodecs_encoder_set_mpeg4_vos_profile_level_type },
	{ "vos_profile_level_value", &shcodecs_encoder_set_mpeg4_vos_profile_level_value },
	{ "out_visual_object_identifier", &shcodecs_encoder_set_mpeg4_out_visual_object_identifier },
	{ "visual_object_verid", &shcodecs_encoder_set_mpeg4_visual_object_verid },
	{ "visual_object_priority", &shcodecs_encoder_set_mpeg4_visual_object_priority },
	{ "video_object_type_indication", &shcodecs_encoder_set_mpeg4_video_object_type_indication },
	{ "out_object_layer_identifier", &shcodecs_encoder_set_mpeg4_out_object_layer_identifier },
	{ "video_object_layer_verid", &shcodecs_encoder_set_mpeg4_video_object_layer_verid },
	{ "video_object_layer_priority", &shcodecs_encoder_set_mpeg4_video_object_layer_priority },
	{ "error_resilience_mode", &shcodecs_encoder_set_mpeg4_error_resilience_mode },
	{ "video_packet_size_mb", &shcodecs_encoder_set_mpeg4_video_packet_size_mb },
	{ "video_packet_size_bit", &shcodecs_encoder_set_mpeg4_video_packet_size_bit },
	{ "video_packet_header_extension", &shcodecs_encoder_set_mpeg4_video_packet_header_extension },
	{ "data_partitioned", &shcodecs_encoder_set_mpeg4_data_partitioned },
	{ "reversible_vlc", &shcodecs_encoder_set_mpeg4_reversible_vlc },
	{ "high_quality", &shcodecs_encoder_set_mpeg4_high_quality },
	{ "param_changeable", &shcodecs_encoder_set_mpeg4_param_changeable },
	{ "changeable_max_bitrate", &shcodecs_encoder_set_mpeg4_changeable_max_bitrate },
	{ "Ivop_quant_initial_value", &shcodecs_encoder_set_mpeg4_Ivop_quant_initial_value },
	{ "Pvop_quant_initial_value", &shcodecs_encoder_set_mpeg4_Pvop_quant_initial_value },
	{ "use_dquant", &shcodecs_encoder_set_mpeg4_use_dquant },
	{ "clip_dquant_frame", &shcodecs_encoder_set_mpeg4_clip_dquant_frame },
	{ "quant_min", &shcodecs_encoder_set_mpeg4_quant_min },
	{ "quant_min_Ivop_under_range", &shcodecs_encoder_set_mpeg4_quant_min_Ivop_under_range },
	{ "quant_max", &shcodecs_encoder_set_mpeg4_quant_max },
	{ "rate_ctrl_vbv_skipcheck_enable", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_skipcheck_enable },
	{ "rate_ctrl_vbv_Ivop_noskip", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_Ivop_noskip },
	{ "rate_ctrl_vbv_remain_zero_skip_enable", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_remain_zero_skip_enable },
	{ "rate_ctrl_vbv_buffer_unit_size", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_buffer_unit_size },
	{ "rate_ctrl_vbv_buffer_mode", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_buffer_mode },
	{ "rate_ctrl_vbv_max_size", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_max_size },
	{ "rate_ctrl_vbv_offset", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_offset },
	{ "rate_ctrl_vbv_offset_rate", &shcodecs_encoder_set_mpeg4_ratecontrol_vbv_offset_rate },
	{ "quant_type", &shcodecs_encoder_set_mpeg4_quant_type },
	{ "use_AC_prediction", &shcodecs_encoder_set_mpeg4_use_AC_prediction },
	{ "vop_min_mode", &shcodecs_encoder_set_mpeg4_vop_min_mode },
	{ "vop_min_size", &shcodecs_encoder_set_mpeg4_vop_min_size },
	{ "intra_thr", &shcodecs_encoder_set_mpeg4_intra_thr },
	{ "b_vop_num", &shcodecs_encoder_set_mpeg4_b_vop_num },
};

static int SetPropsFromFile(FILE * fp_in, SHCodecs_Encoder * encoder, const struct enc_options_t *options, int nr_options)
{
	int found, i;
	long value;

	for (i=0; i<nr_options; i++) {
		if (GetValueFromCtrlFile(fp_in, options[i].name, &value))
			(*options[i].set_fn)(encoder, value);
	}

	return (1);
}

static int SetUnsignedPropsFromFile(FILE * fp_in, SHCodecs_Encoder * encoder, const struct enc_uoptions_t *options, int nr_options)
{
	int found, i;
	long value;

	for (i=0; i<nr_options; i++) {
		if (GetValueFromCtrlFile(fp_in, options[i].name, &value))
			(*options[i].set_fn)(encoder, value);
	}

	return (1);
}

/*****************************************************************************
 * Function Name	: ctrlfile_get_params
 * Description		: コントロールファイルから、入力ファイル、出力先、ストリームタイプを得る
 * Parameters		: 省略
 * Return Value		: 1: 正常終了、-1: エラー
 *****************************************************************************/
int ctrlfile_get_params(const char *ctrl_file,
		    APPLI_INFO * appli_info, long *stream_type)
{
	FILE *fp_in;
	int found;
	long value;
	char path_buf[256 + 8];
	char file_buf[64 + 8];

	if ((ctrl_file == NULL) ||
	    (appli_info == NULL) || (stream_type == NULL)) {
		return (-1);
	}

	fp_in = fopen(ctrl_file, "rt");
	if (fp_in == NULL) {
		return (-1);
	}

	GetStringFromCtrlFile(fp_in, "input_yuv_path", path_buf);
	GetStringFromCtrlFile(fp_in, "input_yuv_file", file_buf);
	if (!strcmp (file_buf, "-")) {
		snprintf(appli_info->input_file_name_buf, 256, "-");
	} else {
		snprintf(appli_info->input_file_name_buf, 256, "%s/%s", path_buf, file_buf);
	}
	GetStringFromCtrlFile(fp_in, "output_directry", path_buf);
	GetStringFromCtrlFile(fp_in, "output_stream_file", file_buf);
	if (!strcmp (file_buf, "-")) {
		snprintf (appli_info->output_file_name_buf, 256, "-");
	} else {
		snprintf(appli_info->output_file_name_buf, 256, "%s/%s", path_buf, file_buf);
	}

	if (GetValueFromCtrlFile(fp_in, "stream_type", &value))
		*stream_type = value;
	if (GetValueFromCtrlFile(fp_in, "x_pic_size", &value))
		appli_info->xpic = value;
	if (GetValueFromCtrlFile(fp_in, "y_pic_size", &value))
		appli_info->ypic = value;

	appli_info->yuv_CbCr_format = 2;	/* 指定されなかったときのデフォルト値(2:Cb0,Cr0,Cb1,Cr1,...) *//* 050520 */
	if (GetValueFromCtrlFile(fp_in, "yuv_CbCr_format", &value))
		appli_info->yuv_CbCr_format = (char) value;

	if (GetValueFromCtrlFile(fp_in, "frame_number_to_encode", &value))
		appli_info->frames_to_encode = value;

	fclose(fp_in);

	return 1;
}

/* Read encoder options from a control file and set them */
int ctrlfile_set_enc_param(SHCodecs_Encoder * encoder, const char *ctrl_file)
{
	FILE *fp_in;
	int found;
	long value;
	long stream_type;

	fp_in = fopen(ctrl_file, "rt");
	if (fp_in == NULL) {
		return -1;
	}

	SetPropsFromFile(fp_in, encoder, options,
			sizeof(options) / sizeof(struct enc_options_t));

	stream_type = shcodecs_encoder_get_stream_type (encoder);

	if (stream_type == SHCodecs_Format_H264) {

		SetUnsignedPropsFromFile(fp_in, encoder, h264_options,
				sizeof(h264_options) / sizeof(struct enc_uoptions_t));

		SetPropsFromFile(fp_in, encoder, h264_options2,
				sizeof(h264_options2) / sizeof(struct enc_options_t));

		if (GetValueFromCtrlFile(fp_in, "filler_output_on", &value))
			shcodecs_encoder_set_output_filler_enable (encoder, value);
	} else {
		SetUnsignedPropsFromFile(fp_in, encoder, mpeg4_options,
				sizeof(mpeg4_options) / sizeof(struct enc_uoptions_t));
	}

	fclose(fp_in);

	return 0;
}

int ctrlfile_get_size_type(const char *ctrl_file,
		    int *w, int *h, long *type)
{
	FILE *fp_in;
	long value;

	if ((ctrl_file == NULL) ||
	    (w == NULL) || (h == NULL) || (type == NULL)) {
		return (-1);
	}

	fp_in = fopen(ctrl_file, "rt");
	if (fp_in == NULL) {
		return (-1);
	}

	if (GetValueFromCtrlFile(fp_in, "stream_type", &value))
		*type = value;
	if (GetValueFromCtrlFile(fp_in, "x_pic_size", &value))
		*w = value;
	if (GetValueFromCtrlFile(fp_in, "y_pic_size", &value))
		*h = value;

	fclose(fp_in);

	return 1;
}

