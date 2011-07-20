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
#include <getopt.h>

#include <shcodecs/shcodecs_decoder.h>

#define DEFAULT_WIDTH 320
#define DEFAULT_HEIGHT 240


struct dec_opts {
	int w;
	int h;
	int format;
};

struct shdec {
	unsigned char	*input_buffer;	/* Pointer to input buffer */
	size_t		si_isize;	/* Total size of input data */

	long max_nal_size;
};


static void
usage (const char * progname)
{
	printf ("Usage: %s [options] ...\n", progname);
	printf ("Decode an H.264 or MPEG-4 Elementary Stream to YCbCr 4:2:0 raw image data using the SH-Mobile VPU\n");
	printf ("Input on stdin, output on stdout\n");
	printf ("\nEncoding format\n");
	printf ("  -f, --format           Set the input format [h264, mpeg4]\n");
	printf ("\nDimensions\n");
	printf ("  -w, --width            Set the input image width in pixels\n");
	printf ("  -h, --height           Set the input image height in pixels\n");
	printf ("  -s, --size             Set the input image size [qcif, cif, qvga, vga, 720p]\n");
	printf ("\nMiscellaneous options\n");
	printf ("  --help                 Display this help and exit\n");
	printf ("  -v, --version          Output version information and exit\n");
	printf ("\nPlease report bugs to <linux-sh@vger.kernel.org>\n");
}

static char * optstring = "f:w:h:s:Hv";

#ifdef HAVE_GETOPT_LONG
static struct option long_options[] = {
	{ "format", required_argument, NULL, 'f'},
	{ "width" , required_argument, NULL, 'w'},
	{ "height", required_argument, NULL, 'h'},
	{ "size", required_argument, NULL, 's'},
	{ "help", no_argument, 0, 'H'},
	{ "version", no_argument, 0, 'v'},
};
#endif


/* local output callback */
static int
frame_decoded (SHCodecs_Decoder * decoder,
		    unsigned char * y_buf, int y_size,
		    unsigned char * c_buf, int c_size,
		    void * user_data)
{
	struct shdec * dec = (struct shdec *)user_data;

	fwrite(y_buf, 1, y_size, stdout);
	fwrite(c_buf, 1, c_size, stdout);

	return 0;
}

int get_dec_opts(int argc, char **argv, struct dec_opts *opts)
{
	int c, i;

	opts->w = DEFAULT_WIDTH;
	opts->h = DEFAULT_HEIGHT;
	opts->format = -1;

	while (1) {
#ifdef HAVE_GETOPT_LONG
		c = getopt_long(argc, argv, optstring, long_options, &i);
#else
		c = getopt (argc, argv, optstring);
#endif
		if (c == -1)
			break;
		if (c == ':') {
			return -1;
		}
		switch (c) {
		case 'f':
			if (strncmp(optarg, "mpeg4", 5) == 0)
				opts->format = SHCodecs_Format_MPEG4;
			else if (strncmp(optarg, "h264", 4) == 0)
				opts->format = SHCodecs_Format_H264;
			break;
		case 'w':
			if (optarg)
				opts->w = strtoul(optarg, NULL, 10);
			break;
		case 'h':
			if (optarg)
				opts->h = strtoul(optarg, NULL, 10);
			break;
		case 's':
			if (optarg) {
				if (!strncasecmp (optarg, "qcif", 4)) {
					opts->w = 176;
					opts->h = 144;
				} else if (!strncmp (optarg, "cif", 3)) {
					opts->w = 352;
					opts->h = 288;
				} else if (!strncmp (optarg, "qvga", 4)) {
					opts->w = 320;
					opts->h = 240;
				} else if (!strncmp (optarg, "vga", 3)) {
					opts->w = 640;
					opts->h = 480;
				} else if (!strncmp (optarg, "720p", 4)) {
					opts->w = 1280;
					opts->h = 720;
				}
			}
			break;
		default:
			return -1;
		}
	}

	if (opts->w < SHCODECS_MIN_FX || opts->w > SHCODECS_MAX_FX ||
	    opts->h < SHCODECS_MIN_FY || opts->h > SHCODECS_MAX_FY) {
		fprintf(stderr, "Invalid width and/or height specified.\n");
		return -1;
	}
	if (optind > argc){
		fprintf(stderr, "Too many arguments.\n");
		return -1;
	}

	if (opts->format == -1) {
		fprintf(stderr, "Format not specified\n");
		return -1;
	}

	return 0;
}

int decode(struct dec_opts *opts)
{
	struct shdec dec1;
	struct shdec * dec = &dec1;
	SHCodecs_Decoder * decoder;
	int bytes_decoded;
	ssize_t n;

	/* H.264 spec: Max NAL size is the size of an uncompressed image divided
	   by the "Minimum Compression Ratio", MinCR. This is 2 for most levels
	   but is 4 for levels 3.1 to 4. Since we don't know the level, we just
	   use MinCR=2. */
	dec->max_nal_size = (opts->w * opts->h * 3) / 2; /* YCbCr420 */
	dec->max_nal_size /= 2;                          /* Apply MinCR */


	if ((decoder = shcodecs_decoder_init(opts->w, opts->h, opts->format)) == NULL) {
		return -1;
	}
	shcodecs_decoder_set_decoded_callback (decoder, frame_decoded, dec);

	/* Allocate memory for input buffer */
	dec->input_buffer = malloc(dec->max_nal_size);
	if (dec->input_buffer == NULL) {
		perror(NULL);
		return -1;
	}

	/* Fill input buffer */
	if ((dec->si_isize = fread(dec->input_buffer, 1, dec->max_nal_size, stdin)) <= 0) {
		perror(NULL);
		return -1;
	}

	/* decode main loop */
	do {
		int rem;

		bytes_decoded = shcodecs_decode (decoder, dec->input_buffer, dec->si_isize);
		
		rem = dec->si_isize - bytes_decoded;
		memmove(dec->input_buffer, dec->input_buffer + bytes_decoded, rem);
		n = fread (dec->input_buffer + rem, 1, dec->max_nal_size - rem, stdin);
		if (n < 0) break;

		dec->si_isize = rem + n;
	} while (!(n == 0 && bytes_decoded == 0));

	bytes_decoded = shcodecs_decode (decoder, dec->input_buffer, dec->si_isize);

	/* Finalize the decode output, in case a final frame is available */
	shcodecs_decoder_finalize (decoder);

	fprintf(stderr, "Decoded %d frames\n", shcodecs_decoder_get_frame_count(decoder));

	shcodecs_decoder_close(decoder);
	free(dec->input_buffer);

	return 0;
}

int main(int argc, char **argv)
{
	int ret=0, i;
	char * progname = argv[0];
	int show_help = 0, show_version = 0;
	struct dec_opts opts;

	if (argc == 1) {
		usage(progname);
		return 0;
	}

	/* Check for help or version */
	for (i=1; i<argc; i++) {
		if (!strcmp(argv[i], "-H") || !strcmp(argv[i], "--help"))
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

	ret = get_dec_opts(argc, argv, &opts);

	if (ret == 0)
		ret = decode(&opts);

	if (ret < 0)
		fprintf(stderr, "Error decoding stream\n");

	return ret;
}

