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
#include <unistd.h>
#include <signal.h>

#include <shcodecs/shcodecs_encoder.h>

#include "ControlFileUtil.h"
#include "avcbencsmp.h"

static int nr_in=0;
static int nr_out=0;
static SHCodecs_Encoder *encoder; /* Encoder */
static long stream_type;
static int width;
static int height;

static void
usage (const char * progname)
{
	printf ("Usage: %s <control file> ...\n", progname);
	printf ("Encode raw YCbCr4:2:0 image data to a video Elementary Stream using the SH-Mobile VPU\n");
	printf ("Input on stdin, output on stdout\n");
	printf ("\nMiscellaneous options\n");
	printf ("  -h, --help             Display this help and exit\n");
	printf ("  -v, --version          Output version information and exit\n");
	printf ("\nPlease report bugs to <linux-sh@vger.kernel.org>\n");
}

/* SHCodecs_Encoder_Output callback for writing encoded data to the output file */
static int write_output(SHCodecs_Encoder * encoder,
			unsigned char *data, int length, void *user_data)
{
	nr_out += shcodecs_encoder_get_frame_num_delta(encoder);

	if (fwrite(data, 1, length, stdout) == (size_t)length) {
		return 0;
	} else {
		return -1;
	}
}

static void cleanup ()
{
	shcodecs_encoder_close(encoder);
}

static void sig_handler(int sig)
{
	cleanup ();

	/* Send ourselves the signal: see http://www.cons.org/cracauer/sigint.html */
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
}

static int setup_enc(char * ctl_file)
{
	int ret;

	ret = ctrlfile_get_size_type(ctl_file, &width, &height, &stream_type);
	if (ret < 0) {
		fprintf(stderr, "Error opening control file");
		goto err;
	}

	/* Setup encoder */
	encoder = shcodecs_encoder_init(width, height, stream_type);
	if (!encoder) {
		fprintf(stderr, "Error initialising encoder");
		goto err;
	}

	shcodecs_encoder_set_output_callback(encoder, write_output, NULL);

	/* set parameters for use in encoding */
	ret = ctrlfile_set_enc_param(encoder, ctl_file);
	if (ret < 0) {
		fprintf(stderr, "Problem with encoder params in control file!\n");
		goto err;
	}

	return 0;

err:
	cleanup();
	return -1;
}

int convert_main(char *ctl_file)
{
	unsigned char *pY;
	unsigned char *pC;
	int ret = -1;

	if (setup_enc(ctl_file) < 0)
		return -1;

	pY = malloc(width * height);
	pC = malloc(width * height / 2);

	if (pY && pC) {

		while (read_1frame_YCbCr420sp(stdin,
						width, height, pY, pC) == 0) {
			nr_in++;
			ret = shcodecs_encoder_encode_1frame(encoder, pY, pC, 0);
			if (ret != 0)
				break;
		}
		ret = shcodecs_encoder_finish(encoder);
		cleanup ();
	}

	free (pY);
	free (pC);

	return ret;
}

int main(int argc, char *argv[])
{
	char * progname = argv[0];
	int show_help = 0, show_version = 0;
	int ret, i;

	if (argc == 1) {
		usage(progname);
		return 0;
	}

	/* Check for help or version */
	for (i=1; i<argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
			show_help = 1;

		if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version"))
			show_version = 1;
	}
	if (show_version)
		printf ("%s version " VERSION "\n", progname);

	if (show_help)
		usage (progname);

	if (show_version || show_help)
		return 0;


	signal (SIGINT, sig_handler);
	signal (SIGPIPE, sig_handler);

	ret = convert_main(argv[1]);
	if (ret < 0)
		fprintf(stderr, "Error encoding\n");

	fprintf(stderr, "%d frames input\n", nr_in);
	fprintf(stderr, "%d frames output (%d skipped to meet bitrate)\n", nr_out, nr_in-nr_out);

	return ret;
}
