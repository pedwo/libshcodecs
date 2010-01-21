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

/*
 *  V4L2 video capture example
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <getopt.h>		/* getopt_long() */

#include <fcntl.h>		/* low-level i/o */
#include <unistd.h>
#include <errno.h>

#include "capture.h"
#include "framerate.h"

#define CLEAR(x) memset (&(x), 0, sizeof (x))

static void usage(FILE * fp, int argc, char **argv)
{
	fprintf(fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		"-d | --device name   Video device name [/dev/video]\n"
		"-h | --help  Print this message\n" "", argv[0]);
	fprintf (fp, "\nPlease report bugs to <linux-sh@vger.kernel.org>\n");
}

static const char short_options[] = "d:hmru";

static const struct option long_options[] = {
	{"device", required_argument, NULL, 'd'},
	{"help", no_argument, NULL, 'h'},
	{0, 0, 0, 0}
};

static void
process_image(capture * ceu, const unsigned char *frame_data, size_t length, void *user_data)
{
	struct framerate *cap_framerate = (struct framerate *)user_data;
	fputc('.', stdout);
	fflush(stdout);
	framerate_wait(cap_framerate);
}

int main(int argc, char **argv)
{
	capture *ceu;
	char *dev_name = "/dev/video0";
	unsigned int count, x;
	double time;
	struct framerate * cap_framerate;

	for (;;) {
		int index;
		int c;

		c = getopt_long(argc, argv,
				short_options, long_options, &index);

		if (-1 == c)
			break;

		switch (c) {
		case 0:	/* getopt_long() flag */
			break;

		case 'd':
			dev_name = optarg;
			break;

		case 'h':
			usage(stdout, argc, argv);
			exit(EXIT_SUCCESS);

		default:
			usage(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}

	ceu = capture_open(dev_name, 640, 480);
	cap_framerate = framerate_new_timer (30.0);

	capture_start_capturing(ceu);

	count = 100;

	for (x=0; x<count; x++)
		capture_get_frame(ceu, process_image, cap_framerate);

	capture_stop_capturing(ceu);
	capture_close(ceu);

	time = (double)framerate_elapsed_time (cap_framerate);
	time /= 1000000;
	printf("\nCaptured %d frames (%.2f fps)\n", count, (double)count/time);

	framerate_destroy (cap_framerate);

	exit(EXIT_SUCCESS);

	return 0;
}
