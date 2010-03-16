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

/*
 * This program captures v4l2 input (e.g. from a camera), optionally crops
 * and rotates this, encodes this and shows it on the display.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>
#include <stdarg.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <linux/videodev2.h>	/* For pixel formats */
#include <linux/ioctl.h>
#include <pthread.h>
#include <errno.h>

#include <shveu/shveu.h>
#include <shcodecs/shcodecs_encoder.h>

#include "avcbencsmp.h"
#include "capture.h"
#include "ControlFileUtil.h"
#include "framerate.h"
#include "display.h"
#include "thrqueue.h"

#define DEBUG

#define MAX_ENCODERS 8

struct encode_data {
	pthread_mutex_t encode_start_mutex;

	unsigned long enc_w;
	unsigned long enc_h;
};

struct private_data {
	void *display;

	pthread_t convert_thread;
	pthread_t capture_thread;

	struct Queue * captured_queue;

	FILE *output_fp;

	int nr_encoders;

	APPLI_INFO ainfo;	/* Capture params */
	SHCodecs_Encoder *encoders[MAX_ENCODERS];

	struct encode_data encdata[MAX_ENCODERS];

	pthread_mutex_t capture_start_mutex;

	/* Captured frame information */
	capture *ceu;

	unsigned long cap_w;
	unsigned long cap_h;

	int rotate_cap;

	struct framerate * cap_framerate;
	struct framerate * enc_framerate;

	int captured_frames;
	int output_frames;
};


static char * optstring = "i:r:hv";

#ifdef HAVE_GETOPT_LONG
static struct option long_options[] = {
	{ "input" , required_argument, NULL, 'i'},
	{ "rotate", required_argument, NULL, 'r'},
	{ "help", no_argument, 0, 'h'},
	{ "version", no_argument, 0, 'v'},
};
#endif

static void
usage (const char * progname)
{
  printf ("Usage: %s [options] <control file>\n", progname);
  printf ("Encode video from a V4L2 device using the SH-Mobile VPU, with preview\n");
  printf ("\nFile options\n");
  printf ("  -i, --input          Set the v4l2 configuration file\n");
  printf ("\nCapture options\n");
  printf ("  -r 90, --rotate 90   Rotate the camera capture buffer 90 degrees and crop\n");
  printf ("\nMiscellaneous options\n");
  printf ("  -h, --help           Display this help and exit\n");
  printf ("  -v, --version        Output version information and exit\n");
  printf ("\nPlease report bugs to <linux-sh@vger.kernel.org>\n");
}

void debug_printf(const char *fmt, ...)
{
#ifdef DEBUG
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
#endif
}


/*****************************************************************************/

/* Callback for frame capture */
static void
capture_image_cb(capture *ceu, const unsigned char *frame_data, size_t length,
		 void *user_data)
{
	struct private_data *pvt = (struct private_data*)user_data;

	queue_enq (pvt->captured_queue, frame_data);
	pvt->captured_frames++;
}

void *capture_main(void *data)
{
	struct private_data *pvt = (struct private_data*)data;

	while(1){
		/* This mutex is unlocked once the capture buffer is free */
		pthread_mutex_lock(&pvt->capture_start_mutex);

		framerate_wait(pvt->cap_framerate);
		capture_get_frame(pvt->ceu, capture_image_cb, pvt);
	}
}


void *convert_main(void *data)
{
	struct private_data *pvt = (struct private_data*)data;
	int pitch, offset;
	void *ptr;
	unsigned long enc_y, enc_c;
	unsigned long src_w, src_h;
	unsigned long cap_y, cap_c;
	int i;

	while(1){
		cap_y = (unsigned long) queue_deq(pvt->captured_queue);
		cap_c = cap_y + (pvt->cap_w * pvt->cap_h);

		if (pvt->rotate_cap == SHVEU_NO_ROT) {
			src_w = pvt->cap_w;
			src_h = pvt->cap_h;
		} else {
			src_w = pvt->encdata[0].enc_h;
			src_h = pvt->encdata[0].enc_w;
		}

		for (i=0; i < pvt->nr_encoders; i++) {
			shcodecs_encoder_get_input_physical_addr (pvt->encoders[i], (unsigned int *)&enc_y, (unsigned int *)&enc_c);

			/* We are clipping, not scaling, as we need to perform a rotation,
		   	but the VEU cannot do a rotate & scale at the same time. */
			shveu_operation(0,
				cap_y, cap_c,
				src_w, src_h, pvt->cap_w, SHVEU_YCbCr420,
				enc_y, enc_c,
				pvt->encdata[i].enc_w, pvt->encdata[i].enc_h, pvt->encdata[i].enc_w, SHVEU_YCbCr420,
				pvt->rotate_cap);

			/* Let the encoder get_input function return */
			pthread_mutex_unlock(&pvt->encdata[i].encode_start_mutex);
		}


		/* Use the VEU to scale the encoder input buffer to the frame buffer */
		display_update(pvt->display,
				cap_y, cap_c,
				src_w, src_h, src_w,
				V4L2_PIX_FMT_NV12);

		capture_queue_buffer (pvt->ceu, cap_y);

		pvt->output_frames++;
	}
}


/* SHCodecs_Encoder_Input callback for acquiring an image */
static int get_input(SHCodecs_Encoder *encoder, void *user_data)
{
	struct private_data *pvt = (struct private_data*)user_data;

	/* This mutex is unlocked once the capture buffer has been copied to the
	   encoder input buffer */
	pthread_mutex_lock(&pvt->encdata[0].encode_start_mutex);

	if (pvt->enc_framerate == NULL) {
		pvt->enc_framerate = framerate_new_measurer ();
	}

	pthread_mutex_unlock(&pvt->capture_start_mutex);
	return 0;
}

/* SHCodecs_Encoder_Output callback for writing out the encoded data */
static int write_output(SHCodecs_Encoder *encoder,
			unsigned char *data, int length, void *user_data)
{
	struct private_data *pvt = (struct private_data*)user_data;
	double ifps, mfps;

	if (shcodecs_encoder_get_frame_num_delta(encoder) > 0 &&
			pvt->enc_framerate != NULL) {
		if (pvt->enc_framerate->nr_handled >= pvt->ainfo.frames_to_encode &&
				pvt->ainfo.frames_to_encode > 0)
			return 1;
		framerate_mark (pvt->enc_framerate);
		ifps = framerate_instantaneous_fps (pvt->enc_framerate);
		mfps = framerate_mean_fps (pvt->enc_framerate);
		if (pvt->enc_framerate->nr_handled % 10 == 0) {
			fprintf (stderr, "  Encoding @ %4.2f fps \t(avg %4.2f fps)\r", ifps, mfps);
		}
	}

	if (fwrite(data, 1, length, pvt->output_fp) < (size_t)length)
		return -1;

	return 0;
}

struct private_data pvt_data;

void cleanup (void)
{
	double time;
	struct private_data *pvt = &pvt_data;
	int i;

	time = (double)framerate_elapsed_time (pvt->cap_framerate);
	time /= 1000000;

	debug_printf ("\n");
	debug_printf("Elapsed time (capture): %0.3g s\n", time);

	debug_printf("Captured %d frames (%.2f fps)\n", pvt->captured_frames,
		 	(double)pvt->captured_frames/time);
	debug_printf("Displayed %d frames (%.2f fps)\n", pvt->output_frames,
			(double)pvt->output_frames/time);

	framerate_destroy (pvt->cap_framerate);

	time = (double)framerate_elapsed_time (pvt->enc_framerate);
	time /= 1000000;

	debug_printf("Elapsed time (encode): %0.3g s\n", time);
	debug_printf("Encoded %d frames (%.2f fps)\n",
			pvt->enc_framerate->nr_handled,
		 	framerate_mean_fps (pvt->enc_framerate));
	framerate_destroy (pvt->enc_framerate);

	capture_stop_capturing(pvt->ceu);
	shcodecs_encoder_close(pvt->encoders[0]);
	capture_close(pvt->ceu);
	close_output_file(pvt->output_fp);

	pthread_cancel (pvt->convert_thread);
	display_close(pvt->display);

	pthread_cancel (pvt->capture_thread);
	shveu_close();

	for (i=0; i , pvt->nr_encoders; i++)
		pthread_mutex_destroy (&pvt->encdata[i].encode_start_mutex);

	pthread_mutex_destroy (&pvt->capture_start_mutex);
}

void sig_handler(int sig)
{
	cleanup ();

#ifdef DEBUG
	fprintf (stderr, "Got signal %d\n", sig);
#endif

	/* Send ourselves the signal: see http://www.cons.org/cracauer/sigint.html */
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}

int main(int argc, char *argv[])
{
	struct private_data *pvt;
	int return_code, rc;
	long stream_type;
	unsigned int pixel_format;
	char ctrl_filename[MAXPATHLEN];
	int c, i;
	long target_fps10;
	unsigned long rotate_input;

	char * progname;
	int show_version = 0;
	int show_help = 0;

	pvt = &pvt_data;

	progname = argv[0];

	if (argc == 1) {
		usage(progname);
		return 0;
	}
	ctrl_filename[0] = '\0';

	pvt->captured_frames = 0;
	pvt->output_frames = 0;
	pvt->rotate_cap = SHVEU_NO_ROT;

	pvt->enc_framerate = NULL;

	while (1) {
#ifdef HAVE_GETOPT_LONG
		c = getopt_long(argc, argv, optstring, long_options, &i);
#else
		c = getopt (argc, argv, optstring);
#endif
		if (c == -1)
			break;
		if (c == ':') {
			usage (progname);
			goto exit_err;
		}

		switch (c) {
		case 'h': /* help */
			show_help = 1;
			break;
		case 'v': /* version */
			show_version = 1;
			break;
		case 'i':
			if (optarg)
				strncpy(ctrl_filename, optarg, MAXPATHLEN-1);
			break;
		case 'r':
			if (optarg) {
				rotate_input = strtoul(optarg, NULL, 10);
				if (rotate_input == 1 || rotate_input == 90) {
					pvt->rotate_cap = SHVEU_ROT_90;
				}
			}
			break;
		default:
			usage(progname);
			goto exit_err;
		}
	}

	if (show_version) {
		printf ("%s version " VERSION "\n", progname);
	}

	if (show_help) {
		usage (progname);
	}

	if (show_version || show_help) {
		return 0;
	}

	if (optind >= argc) {
	      usage (progname);
	      goto exit_err;
	}

	if (optind == (argc-1) && ctrl_filename[0] == '\0') {
		strncpy(ctrl_filename, argv[optind++], MAXPATHLEN-1);
	}
	if ( (strcmp(ctrl_filename, "-") == 0) || (ctrl_filename[0] == '\0') ){
		fprintf(stderr, "Invalid v4l2 configuration file.\n");
		return -1;
	}

	return_code = ctrlfile_get_params(ctrl_filename, &pvt->ainfo, &stream_type);
	if (return_code < 0) {
		fprintf(stderr, "Error opening control file.\n");
		return -2;
	}


	debug_printf("Input file: %s\n", pvt->ainfo.input_file_name_buf);
	debug_printf("Output file: %s\n", pvt->ainfo.output_file_name_buf);

	pvt->nr_encoders = 1;

	/* Initalise the mutexes */
	pthread_mutex_init(&pvt->capture_start_mutex, NULL);
	pthread_mutex_lock(&pvt->capture_start_mutex);

	for (i=0; i < pvt->nr_encoders; i++) {
		pthread_mutex_init(&pvt->encdata[i].encode_start_mutex, NULL);
		pthread_mutex_unlock(&pvt->encdata[i].encode_start_mutex);
	}

	/* Initialize the queues */
	pvt->captured_queue = queue_init();
	queue_limit (pvt->captured_queue, 2);

	/* Create the threads */
	rc = pthread_create(&pvt->capture_thread, NULL, capture_main, pvt);
	if (rc){
		fprintf(stderr, "pthread_create failed, exiting\n");
		return -6;
	}
	rc = pthread_create(&pvt->convert_thread, NULL, convert_main, pvt);
	if (rc){
		fprintf(stderr, "pthread_create failed, exiting\n");
		return -7;
	}

	signal (SIGINT, sig_handler);
	signal (SIGPIPE, sig_handler);

	/* VEU Scaler initialisation */
	if (shveu_open() < 0) {
		fprintf (stderr, "Could not open VEU, exiting\n");
	}

	/* Camera capture initialisation */
	pvt->ceu = capture_open_userio(pvt->ainfo.input_file_name_buf, pvt->ainfo.xpic, pvt->ainfo.ypic);
	if (pvt->ceu == NULL) {
		fprintf(stderr, "capture_open failed, exiting\n");
		return -3;
	}
	capture_set_use_physical(pvt->ceu, 1);
	pvt->cap_w = capture_get_width(pvt->ceu);
	pvt->cap_h = capture_get_height(pvt->ceu);

	pixel_format = capture_get_pixel_format (pvt->ceu);
	if (pixel_format != V4L2_PIX_FMT_NV12) {
		fprintf(stderr, "Camera capture pixel format is not supported\n");
		return -4;
	}

	debug_printf("Camera resolution:  %dx%d\n", pvt->cap_w, pvt->cap_h);

	for (i=0; i < pvt->nr_encoders; i++) {
		if (pvt->rotate_cap == SHVEU_NO_ROT) {
			pvt->encdata[i].enc_w = pvt->cap_w;
			pvt->encdata[i].enc_h = pvt->cap_h;
		} else {
			pvt->encdata[i].enc_w = pvt->cap_h;
			pvt->encdata[i].enc_h = pvt->cap_h * pvt->cap_h / pvt->cap_w;
			/* Round down to nearest multiple of 16 for VPU */
			pvt->encdata[i].enc_w = pvt->encdata[i].enc_w - (pvt->encdata[i].enc_w % 16);
			pvt->encdata[i].enc_h = pvt->encdata[i].enc_h - (pvt->encdata[i].enc_h % 16);
			debug_printf("Rotating & cropping camera image for encode %d...\n", i);
		}
	}

	debug_printf("Encode resolution:  %dx%d\n", pvt->encdata[0].enc_w, pvt->encdata[0].enc_h);

	pvt->display = display_open(0);
	if (!pvt->display) {
		return -5;
	}

	/* open output file */
	pvt->output_fp = open_output_file(pvt->ainfo.output_file_name_buf);
	if (pvt->output_fp == NULL) {
		fprintf(stderr, "Error opening output file\n");
		return -8;
	}

	/* VPU Encoder initialisation */
	for (i=0; i < pvt->nr_encoders; i++) {
		pvt->encoders[i] = shcodecs_encoder_init(pvt->encdata[i].enc_w, pvt->encdata[i].enc_h, stream_type);
		if (pvt->encoders[i] == NULL) {
			fprintf(stderr, "shcodecs_encoder_init failed, exiting\n");
			return -5;
		}
		shcodecs_encoder_set_input_callback(pvt->encoders[i], get_input, pvt);
		shcodecs_encoder_set_output_callback(pvt->encoders[i], write_output, pvt);

		return_code = ctrlfile_set_enc_param(pvt->encoders[i], ctrl_filename);
		if (return_code < 0) {
			fprintf (stderr, "Problem with encoder params in control file!\n");
			return -9;
		}
		/* Override the encoding frame size as it may not be the same size as the
	   	camera capture size */
		shcodecs_encoder_set_xpic_size(pvt->encoders[i], pvt->encdata[i].enc_w);
		shcodecs_encoder_set_ypic_size(pvt->encoders[i], pvt->encdata[i].enc_h);

		/* Set up the frame rate timer to match the encode framerate */
		target_fps10 = shcodecs_encoder_get_frame_rate(pvt->encoders[i]);
		fprintf (stderr, "Target framerate:   %.1f fps\n", target_fps10 / 10.0);
	}

	/* Initialize framerate timer */
	pvt->cap_framerate = framerate_new_timer (target_fps10 / 10.0);

	capture_start_capturing(pvt->ceu);

	rc = shcodecs_encoder_run_multiple(pvt->encoders, pvt->nr_encoders);
	if (rc != 0) {
		fprintf(stderr, "Error encoding, error code=%d\n", rc);
		rc = -10;
	}

	cleanup ();

	return rc;

exit_err:
	/* General exit, prior to thread creation */
	exit (1);
}

