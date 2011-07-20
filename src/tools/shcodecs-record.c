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

 The diagram below give a very rough idea of the data flow & processing.
 Note that the blend only libshbeu is present, otherwise the second VEU writes
 directly to the frame buffer.

     v4l2                  scaled
    capture             encoder input              encoded
   +-------+             +----------+               file
   |       +-+      VEU  |          |++    VPU   +--------+
   |       | | +--+----> |          | |+-------->|        |
   +-------+ |    |      +----------+ |          +--------+
     +-------+    |        +----------+
                  |
                  | VEU
                  |         scaled                frame buffer
                  |      blend input            +---------------+
                  |       +--------+    BEU     |               |++
                  +-----> |        | +--------> |               | |
                          +--------+            |               | |
                                                +---------------+ |
                                                  +---------------+
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
#include <pthread.h>
#include <errno.h>

#include <uiomux/uiomux.h>
#include <shveu/shveu.h>
#ifdef HAVE_SHBEU
#include <shbeu/shbeu.h>
#endif
#include <shcodecs/shcodecs_encoder.h>

#include "avcbencsmp.h"
#include "capture.h"
#include "ControlFileUtil.h"
#include "framerate.h"
#include "display.h"
#include "thrqueue.h"

#define DEBUG

/* Maximum number of cameras handled by the program */
#define MAX_CAMERAS 8

/* Maximum number of encoders per camera */
#define MAX_ENCODERS 8

#define MAX_BLEND_INPUTS 2

struct camera_data {
	char * devicename;
	int index;

	/* Captured frame information */
	capture *ceu;
	unsigned long cap_w;
	unsigned long cap_h;

	pthread_t capture_thread;
	int alive;
};

struct encode_data {
	pthread_t thread;
	int alive;

	SHCodecs_Encoder *encoder;

	char ctrl_filename[MAXPATHLEN];
	APPLI_INFO ainfo;

	struct camera_data * camera;

	long stream_type;

	struct Queue * enc_input_q;
	struct Queue * enc_input_empty_q;

	struct ren_vid_surface enc_surface;

	struct framerate * enc_framerate;
	int skipsize;
	int skipcount;

	double ifps;
	double mfps;

	double ibps;
	double mbps;
};

struct private_data {
	UIOMux * uiomux;

	int nr_cameras;
	struct camera_data cameras[MAX_CAMERAS];

	int nr_encoders;
	struct encode_data encdata[MAX_ENCODERS];

	int do_preview;
	DISPLAY *display;
	SHVEU *veu;
#ifdef HAVE_SHBEU
	SHBEU *beu;
	struct shbeu_surface beu_in[MAX_BLEND_INPUTS];
#endif
	int rotate_cap;
};


static char * optstring = "r:Phvb";

#ifdef HAVE_GETOPT_LONG
static struct option long_options[] = {
	{ "rotate", required_argument, NULL, 'r'},
	{ "no-preview", no_argument, NULL, 'P'},
	{ "help", no_argument, 0, 'h'},
	{ "version", no_argument, 0, 'v'},
	{ "show-bitrate", no_argument, 0, 'b'},
	{ 0, 0, 0, 0 }
};
#endif

static void
usage (const char * progname)
{
  printf ("Usage: %s [options] <control file> ...\n", progname);
  printf ("Encode video from a V4L2 device using the SH-Mobile VPU, with preview\n");
  printf ("\nCapture options\n");
  printf ("  -r 90, --rotate 90   Rotate the camera capture buffer 90 degrees and crop\n");
  printf ("\nDisplay options\n");
  printf ("  -P, --no-preview     Disable framebuffer preview\n");
  printf ("\nMiscellaneous options\n");
  printf ("  -h, --help           Display this help and exit\n");
  printf ("  -v, --version        Output version information and exit\n");
  printf ("  -b, --show-bitrate   Show the current and average bitrate\n");
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

struct private_data pvt_data;

static int alive=1;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*****************************************************************************/

static void dbg(const char *str1, int l, const char *str2, const struct ren_vid_surface *s)
{
//	fprintf(stderr, "%20s:%d: %10s: (%dx%d) pitch=%d py=%p, pc=%p, pa=%p\n", str1, l, str2, s->w, s->h, s->pitch, s->py, s->pc, s->pa);
}

#ifdef HAVE_SHBEU
static void blend(
	SHBEU *beu,
	DISPLAY *display,
	struct shbeu_surface *overlay,
	int nr_inputs)
{
	void *bb = display_get_back_buff(display);
	int lcd_w = display_get_width(display);
	int lcd_h = display_get_height(display);
	struct shbeu_surface dst;

	/* Destination surface info */
	dst.s.py = bb;
	dst.s.pc = NULL;
	dst.s.pa = NULL;
	dst.s.w = overlay->s.w;
	dst.s.h = overlay->s.h;
	dst.s.pitch = lcd_w;
	dst.s.format = REN_RGB565;

	dbg(__func__, __LINE__, "blend in", &overlay[0]);
	dbg(__func__, __LINE__, "blend out", &dst);

	if (nr_inputs == 3)
		shbeu_blend(beu, &overlay[0], &overlay[1], &overlay[2], &dst);
	else if (nr_inputs == 2)
		shbeu_blend(beu, &overlay[0], &overlay[1], NULL, &dst);
	else if (nr_inputs == 1)
		shbeu_blend(beu, &overlay[0], NULL, NULL, &dst);

	display_flip(display);
}
#endif

/* Callback for frame capture */
static void
capture_image_cb(capture *ceu, const unsigned char *frame_data, size_t length,
		 void *user_data)
{
	struct camera_data *cam = (struct camera_data*)user_data;
	struct private_data *pvt = &pvt_data;
	struct encode_data *encdata;
	struct ren_vid_surface cap_surface;
	struct ren_vid_surface *enc_surface;
	void *cap_y, *cap_c;
	int i;
	int enc_alive = 0;
	int nr_inputs;

	cap_y = (void*)frame_data;
	cap_c = cap_y + (cam->cap_w * cam->cap_h);

	cap_surface.format = REN_NV12;
	cap_surface.w = cam->cap_w;
	cap_surface.h = cam->cap_h;
	cap_surface.pitch = cap_surface.w;
	cap_surface.py = (void*)frame_data;
	cap_surface.pc = cap_surface.py + (cap_surface.pitch * cap_surface.h);
	cap_surface.pa = NULL;

	dbg(__func__, __LINE__, "cap out", &cap_surface);

	for (i=0; i < pvt->nr_encoders; i++) {
		if (pvt->encdata[i].camera != cam) continue;

		encdata = &pvt->encdata[i];
		enc_surface = &pvt->encdata[i].enc_surface;

		if (!encdata->alive) continue;

		if (encdata->skipcount == 0) {
			/* Get an empty encoder input frame */
			enc_surface->py = queue_deq(encdata->enc_input_empty_q);
			enc_surface->pc = enc_surface->py + (enc_surface->pitch * enc_surface->h);

			dbg(__func__, __LINE__, "enc in", enc_surface);

			/* Hardware resize */
			if (shveu_resize(pvt->veu, &cap_surface, enc_surface) != 0) {
				fprintf(stderr, "shveu_resize failed!\n");
				exit(0);
			}

			encdata->alive = alive;

			queue_enq(encdata->enc_input_q, enc_surface->py);
		}

		encdata->skipcount++;
		encdata->skipcount %= encdata->skipsize;
	}

	/* Can only stop the camera when all encoders have been told to stop */
	for (i=0; i < pvt->nr_encoders; i++) {
		if (pvt->encdata[i].camera == cam) {
			enc_alive |= pvt->encdata[i].alive;
		}
	}
	cam->alive = enc_alive;

#ifdef HAVE_SHBEU
	if (pvt->do_preview && (cam->index < MAX_BLEND_INPUTS)) {
		struct shbeu_surface * beu_input = &pvt->beu_in[cam->index];

		/* Copy the camera image to a BEU input buffer */
		pthread_mutex_lock(&mutex);
		shveu_resize(pvt->veu, &cap_surface, &beu_input->s);
		pthread_mutex_unlock(&mutex);
	}

	capture_queue_buffer (cam->ceu, frame_data);

	/* Blend when processing the camera with the smallest frames (assume the last camera) */
	nr_inputs = (pvt->nr_cameras > MAX_BLEND_INPUTS) ? MAX_BLEND_INPUTS : pvt->nr_cameras;
	if (pvt->do_preview && (cam->index == nr_inputs-1)) {
		pthread_mutex_lock(&mutex);
		blend (pvt->beu, pvt->display, &pvt->beu_in[0], nr_inputs);
		pthread_mutex_unlock(&mutex);
	}
#else
	if (cam == pvt->encdata[0].camera && pvt->do_preview) {
		display_update(pvt->display, &cap_surface);
	}

	capture_queue_buffer (cam->ceu, frame_data);
#endif
}

void *capture_main(void *data)
{
	struct camera_data *cam = (struct camera_data*)data;

	while(cam->alive){
		capture_get_frame(cam->ceu, capture_image_cb, cam);
	}

	capture_stop_capturing(cam->ceu);

	return NULL;
}


/* SHCodecs_Encoder_Input_Release callback */
static int release_input_buf(SHCodecs_Encoder * encoder,
                             unsigned char *py,
                             unsigned char *pc,
                             void *user_data)
{
	struct encode_data *encdata = (struct encode_data*)user_data;

	queue_enq (encdata->enc_input_empty_q, py);

	return 0;
}

/* SHCodecs_Encoder_Input callback for acquiring an image */
static int get_input(SHCodecs_Encoder *encoder, void *user_data)
{
	struct encode_data *encdata = (struct encode_data*)user_data;
	void *py, *pc;

	/* Get a scaled frame from the queue */
	py = queue_deq(encdata->enc_input_q);
	pc = py + (encdata->enc_surface.w * encdata->enc_surface.h);

	shcodecs_encoder_input_provide (encoder, py, pc);

	if (encdata->enc_framerate == NULL) {
		encdata->enc_framerate = framerate_new_measurer ();
	}

	return (encdata->alive?0:1);
}

/* SHCodecs_Encoder_Output callback for writing out the encoded data */
static int write_output(SHCodecs_Encoder *encoder,
			unsigned char *data, int length, void *user_data)
{
	struct encode_data *encdata = (struct encode_data*)user_data;

	/* count number of bytes passed */
	framerate_add_bytes (encdata->enc_framerate, length);

	if (shcodecs_encoder_get_frame_num_delta(encoder) > 0 &&
			encdata->enc_framerate != NULL) {
		if (encdata->enc_framerate->nr_handled >= encdata->ainfo.frames_to_encode &&
				encdata->ainfo.frames_to_encode > 0)
			return 1;
		framerate_mark (encdata->enc_framerate);
		encdata->ifps = framerate_instantaneous_fps (encdata->enc_framerate);
		encdata->mfps = framerate_mean_fps (encdata->enc_framerate);

		encdata->ibps = framerate_instantaneous_bps (encdata->enc_framerate);
		encdata->mbps = framerate_mean_bps (encdata->enc_framerate);
	}

	if (write_output_file(&encdata->ainfo, data, length))
		return -1;

	return (encdata->alive?0:1);
}

int cleanup (void)
{
	double time;
	struct private_data *pvt = &pvt_data;
	int i;
	void *thread_ret;
	int rc;

	alive=0;
	usleep (300000);

	for (i=0; i < pvt->nr_cameras; i++) {
		if (pvt->cameras[i].capture_thread != 0) {
			pthread_join (pvt->cameras[i].capture_thread, &thread_ret);
			capture_close(pvt->cameras[i].ceu);
		}
	}

	/* Display number of frames encoded */
	fprintf (stderr, "Encoded  :");
	for (i=0; i < pvt->nr_encoders; i++) {
		fprintf (stderr, "\t%-6d ", pvt->encdata[i].enc_framerate->nr_handled);
	}
	fprintf (stderr, "\tframes\n");

	/* Display mean frame rate */
	fprintf (stderr, "Encoded  @");
	for (i=0; i < pvt->nr_encoders; i++) {
		fprintf (stderr, "\t%6.2f ", pvt->encdata[i].mfps);
	}
	fprintf (stderr, "\tFPS\n");

	fprintf (stderr, "Status   :");
	for (i=0; i < pvt->nr_encoders; i++) {
		struct encode_data * encdata = &pvt->encdata[i];

		if (encdata->thread != 0) {
			pthread_join (encdata->thread, &thread_ret);
			if ((int)thread_ret < 0) {
				rc = (int)thread_ret;
				fprintf (stderr, "\tErr %d", i);
			} else {
				fprintf (stderr, "\tOK");
			}
		}
		shcodecs_encoder_close(encdata->encoder);
		close_output_file(&encdata->ainfo);
		framerate_destroy(encdata->enc_framerate);
	}
	fprintf (stderr, "\n");

	/* Free all encoder input frames */
	for (i=0; i < pvt->nr_encoders; i++) {
		struct encode_data * encdata = &pvt->encdata[i];
		int size = (encdata->enc_surface.w * encdata->enc_surface.h * 3) / 2;
		struct Queue * q;

		q = encdata->enc_input_q;
		while (queue_length(q) > 0) {
			uiomux_free(pvt->uiomux, UIOMUX_SH_VPU, queue_deq(q), size);
		}
		q = encdata->enc_input_empty_q;
		while (queue_length(q) > 0) {
			uiomux_free(pvt->uiomux, UIOMUX_SH_VPU, queue_deq(q), size);
		}
	}

	if (pvt->do_preview) {
		int nr_inputs = (pvt->nr_cameras > MAX_BLEND_INPUTS) ? MAX_BLEND_INPUTS : pvt->nr_cameras;
		void * user;
		int size;

		display_close(pvt->display);

#ifdef HAVE_SHBEU
		/* Free blend input surfaces */
		for (i=0; i < nr_inputs; i++) {
			struct ren_vid_surface *s = &pvt->beu_in[i].s;
			size = (s->pitch * s->h * 3) / 2;
			uiomux_free(pvt->uiomux, UIOMUX_SH_BEU, s->py, size);

			if (s->pa) {
				size = s->pitch * s->h;
				uiomux_free(pvt->uiomux, UIOMUX_SH_BEU, s->pa, size);
			}
		}

		shbeu_close(pvt->beu);
#endif
	}
	shveu_close(pvt->veu);

	uiomux_close (pvt->uiomux);

	return (int)thread_ret;
}

void sig_handler(int sig)
{
	cleanup ();

	/* Send ourselves the signal: see http://www.cons.org/cracauer/sigint.html */
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}

struct camera_data * get_camera (char * devicename, int width, int height)
{
	struct private_data *pvt = &pvt_data;
	int i;

	for (i=0; i < MAX_CAMERAS; i++) {
		if (pvt->cameras[i].devicename == NULL)
			break;

		if (!strcmp (pvt->cameras[i].devicename, devicename)) {
			return &pvt->cameras[i];
		}
	}

	if (i == MAX_CAMERAS) return NULL;

	pvt->cameras[i].devicename = devicename;
	pvt->cameras[i].cap_w = width;
	pvt->cameras[i].cap_h = height;
	pvt->cameras[i].index = i;

	pvt->nr_cameras = i+1;

	return &pvt->cameras[i];
}

void * encode_main (void * data)
{
	struct encode_data *encdata = (struct encode_data *)data;
	int ret = -1;

	ret = shcodecs_encoder_run (encdata->encoder);
	encdata->alive = 0;

	return (void *)ret;
}

#ifdef HAVE_SHBEU
int create_alpha(UIOMux *uiomux, struct shbeu_surface *s, int i, int w, int h)
{
	unsigned char *alpha;
	int tmp;
	int size;
	int x, y;

	size = w * h;
	s->s.pa = alpha = uiomux_malloc (uiomux, UIOMUX_SH_BEU, size, 32);
	if (!alpha) {
		perror("uiomux_malloc");
		return -1;
	}

	for (y=0; y<h; y++) {
		for (x=0; x<w; x++) {
			tmp = (2 * 255 * (w/2- abs(x - w/2))) / (w/2);
			if (tmp > 255) tmp = 255;
			*alpha++ = (unsigned char)tmp;
		}
	}
	return 0;
}

/* This is only used when blending multiple camera outputs onto the screen */
int setup_input_surface(DISPLAY *disp, UIOMux *uiomux, struct shbeu_surface *s, int i, int w, int h)
{
	float lcd_aspect, cam_aspect;
	void *user;
	int size;
	int lcd_w = display_get_width(disp);
	int lcd_h = display_get_height(disp);

	/* Limit the size of the images used in blend to the LCD */
	lcd_aspect = (float)lcd_w / lcd_h;
	cam_aspect = (float)w / h;

	if (cam_aspect > lcd_aspect) {
		if (w > lcd_w) w = lcd_w;
		h = (int) ((float)w / cam_aspect);
		h = h & ~3;
	} else {
		if (h > lcd_h) h = lcd_h;
		w = (int)((float)h * cam_aspect);
		w = w & ~3;
	}

	size = (w * h * 3)/2;
	user = uiomux_malloc (uiomux, UIOMUX_SH_BEU, size, 32);
	if (!user) {
		perror("uiomux_malloc");
		return -1;
	}

	s->s.format = REN_NV12;
	s->s.w = w;
	s->s.h = h;
	s->s.pitch = w;
	s->s.py = user;
	s->s.pc = s->s.py + (w * h);
	s->s.pa = 0;

	s->alpha = 255 - i*70;	/* 1st layer opaque, others semi-transparent */
	s->x = 0;
	s->y = 0;

	if (i > 0)
		create_alpha(uiomux, s, i, w, h);

	return 0;
}
#endif

int main(int argc, char *argv[])
{
	struct private_data *pvt;
	int return_code, rc;
	void *thread_ret;
	unsigned int pixel_format;
	int c, i=0, j, size;
	long target_fps10;
	double target_mbps;
	unsigned long rotate_input;
	int running = 1;

	char * progname;
	int show_version = 0;
	int show_help = 0;
	int show_bitrate = 0;

	progname = argv[0];

	if (argc == 1) {
		usage(progname);
		return 0;
	}

	pvt = &pvt_data;

	memset (pvt, 0, sizeof(struct private_data));

	pvt->do_preview = 1;

	pvt->rotate_cap = SHVEU_NO_ROT;

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
		case 'b': /* bitrate */
			show_bitrate = 1;
			break;
		case 'r':
			if (optarg) {
				rotate_input = strtoul(optarg, NULL, 10);
				if (rotate_input == 1 || rotate_input == 90) {
					pvt->rotate_cap = SHVEU_ROT_90;
				}
			}
			break;
		case 'P':
			pvt->do_preview = 0;
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

	while (optind < argc) {
		strncpy(pvt->encdata[i++].ctrl_filename, argv[optind++], MAXPATHLEN-1);
	}

	pvt->nr_encoders = i;

	if (!(pvt->uiomux = uiomux_open())) {
		fprintf (stderr, "Could not open UIOMux, exiting\n");
		return -1;
	}

	/* VEU initialisation */
	if (!(pvt->veu = shveu_open())) {
		fprintf (stderr, "Could not open VEU, exiting\n");
		return -1;
	}

	if (pvt->do_preview) {
		if (!(pvt->display = display_open())) {
			fprintf (stderr, "Could not open display, exiting\n");
			return -1;
		}

#ifdef HAVE_SHBEU
		if (!(pvt->beu = shbeu_open())) {
			fprintf (stderr, "Could not open BEU, exiting\n");
			return -1;
		}
#endif
	}


	for (i=0; i < pvt->nr_encoders; i++) {
		struct encode_data *encdata = &pvt->encdata[i];

		if ( (strcmp(encdata->ctrl_filename, "-") == 0) ||
				(encdata->ctrl_filename[0] == '\0') ){
			fprintf(stderr, "Invalid v4l2 configuration file - %s\n",
				encdata->ctrl_filename);
			return -1;
		}

		return_code = ctrlfile_get_params(encdata->ctrl_filename,
				&encdata->ainfo, &encdata->stream_type);
		if (return_code < 0) {
			fprintf(stderr, "Error opening control file %s.\n", encdata->ctrl_filename);
			return -2;
		}

		encdata->camera = get_camera (encdata->ainfo.input_file_name_buf, encdata->ainfo.xpic, encdata->ainfo.ypic);

		debug_printf("[%d] Input file: %s\n", i, encdata->ainfo.input_file_name_buf);
		debug_printf("[%d] Output file: %s\n", i, encdata->ainfo.output_file_name_buf);

		/* Initialize the queues */
		encdata->enc_input_q = queue_init();
		encdata->enc_input_empty_q = queue_init();
	}

	for (i=0; i < pvt->nr_cameras; i++) {
		struct camera_data *cam = &pvt->cameras[i];

		/* Camera capture initialisation */
		cam->ceu = capture_open_userio(cam->devicename, cam->cap_w, cam->cap_h, pvt->uiomux);
		if (cam->ceu == NULL) {
			fprintf(stderr, "capture_open failed, exiting\n");
			return -3;
		}
		cam->cap_w = capture_get_width(cam->ceu);
		cam->cap_h = capture_get_height(cam->ceu);

		pixel_format = capture_get_pixel_format (cam->ceu);
		if (pixel_format != V4L2_PIX_FMT_NV12) {
			fprintf(stderr, "Camera capture pixel format is not supported\n");
			return -4;
		}
		debug_printf("Camera %d resolution:  %dx%d\n", i, cam->cap_w, cam->cap_h);

#ifdef HAVE_SHBEU
		/* BEU input */
		if (pvt->do_preview && (i < MAX_BLEND_INPUTS)) {
			if (setup_input_surface(pvt->display, pvt->uiomux, &pvt->beu_in[i], i, cam->cap_w, cam->cap_h) < 0) {
				fprintf(stderr, "Failed to allocate UIO memory (BEU).\n");
				return -1;
			}
		}
#endif
	}

	signal (SIGINT, sig_handler);
	signal (SIGPIPE, sig_handler);

	fprintf (stderr, "\nEncoders\n");
	fprintf (stderr, "--------\n");
	fprintf (stderr, "Size:     ");
	for (i=0; i < pvt->nr_encoders; i++) {
		struct encode_data *encdata = &pvt->encdata[i];

#if 0
		if (pvt->rotate_cap == SHVEU_NO_ROT) {
			encdata->enc_w = pvt->cap_w;
			encdata->enc_h = pvt->cap_h;
		} else {
			encdata->enc_w = pvt->cap_h;
			encdata->enc_h = pvt->cap_h * pvt->cap_h / pvt->cap_w;
			/* Round down to nearest multiple of 16 for VPU */
			encdata->enc_w = encdata->enc_w - (encdata->enc_w % 16);
			encdata->enc_h = encdata->enc_h - (encdata->enc_h % 16);
			debug_printf("[%d] Rotating & cropping camera image ...\n", i);
		}
#else
		encdata->enc_surface.format = REN_NV12;
		encdata->enc_surface.w = encdata->ainfo.xpic;
		encdata->enc_surface.h = encdata->ainfo.ypic;
		encdata->enc_surface.pitch = encdata->enc_surface.w;
		encdata->enc_surface.py = NULL;
		encdata->enc_surface.pc = NULL;
		encdata->enc_surface.pa = NULL;

		debug_printf("\t%dx%d", encdata->enc_surface.w, encdata->enc_surface.h);
#endif

		/* VPU Encoder initialisation */
		if (open_output_file(&encdata->ainfo)) {
			return -8;
		}

		encdata->encoder = shcodecs_encoder_init(encdata->enc_surface.w, encdata->enc_surface.h, encdata->stream_type);
		if (encdata->encoder == NULL) {
			fprintf(stderr, "shcodecs_encoder_init failed, exiting\n");
			return -5;
		}
		shcodecs_encoder_set_input_callback(encdata->encoder, get_input,encdata);
		shcodecs_encoder_set_output_callback(encdata->encoder, write_output, encdata);
		shcodecs_encoder_set_input_release_callback(encdata->encoder, release_input_buf, encdata);

		return_code = ctrlfile_set_enc_param(encdata->encoder, encdata->ctrl_filename);
		if (return_code < 0) {
			fprintf (stderr, "Problem with encoder params in control file!\n");
			return -9;
		}

		/* Allocate encoder input frames & and add them to empty queue */
		size = (encdata->enc_surface.w * encdata->enc_surface.h * 3) / 2;
		for (j=0; j<shcodecs_encoder_get_min_input_frames(encdata->encoder)+1; j++) {
			void *user = uiomux_malloc (pvt->uiomux, UIOMUX_SH_VPU, size, 32);
			queue_enq (encdata->enc_input_empty_q, user);
		}

		encdata->alive = 1;
	}

	/* Set up framedropping */
	fprintf (stderr, "\nTarget   @");
	for (i=0; i < pvt->nr_encoders; i++) {
		target_fps10 = shcodecs_encoder_get_frame_rate(pvt->encdata[i].encoder);
		target_mbps = shcodecs_encoder_get_bitrate(pvt->encdata[i].encoder) / 1000000;

		fprintf (stderr, "\t%6.2f ", target_fps10/10.0);
		if (show_bitrate)
			fprintf (stderr, "(%6.2f) ", target_mbps);
		pvt->encdata[i].skipsize = 300 / target_fps10;
		pvt->encdata[i].skipcount = 0;

		pvt->encdata[i].ifps = 0;
		pvt->encdata[i].mfps = 0;

		pvt->encdata[i].ibps = 0;
		pvt->encdata[i].mbps = 0;
	}
	if (!show_bitrate)
		fprintf (stderr, "\tFPS\n");
	else
		fprintf (stderr, "\tFPS (Mbps)\n");

	for (i=0; i < pvt->nr_cameras; i++) {
		capture_start_capturing(pvt->cameras[i].ceu);

		pvt->cameras[i].alive = 1;
		rc = pthread_create(&pvt->cameras[i].capture_thread, NULL, capture_main, &pvt->cameras[i]);
		if (rc){
			fprintf(stderr, "pthread_create failed, exiting\n");
			return -6;
		}
	}

	for (i=0; i < pvt->nr_encoders; i++) {
		rc = pthread_create (&pvt->encdata[i].thread, NULL, encode_main, &pvt->encdata[i]);
		if (rc)
			fprintf (stderr, "Thread %d failed: %s\n", i, strerror(rc));
	}

	while (running) {
		fprintf (stderr, "Encoding @");
		for (i=0; i < pvt->nr_encoders; i++) {
			fprintf (stderr, "\t%6.2f ",
				pvt->encdata[i].ifps);
			if (show_bitrate)
				fprintf (stderr, "(%6.2f/%6.2f) ",
					pvt->encdata[i].ibps / 1000000,
					pvt->encdata[i].mbps / 1000000);
		}
		if (!show_bitrate)
			fprintf (stderr, "\tFPS\r");
		else
			fprintf (stderr, "\tFPS (Mbps/Mbps)\r");

		usleep (300000);

		running = 0;
		for (i=0; i < pvt->nr_encoders; i++) {
			running |= pvt->encdata[i].alive;
		}
	}

	rc = cleanup ();

	return rc;

exit_err:
	/* General exit, prior to thread creation */
	exit (1);
}

