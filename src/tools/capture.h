/*
 *  V4L2 video capture
 *
 *  This program can be used and distributed without restrictions.
 */

#ifndef __CAPTURE_H__
#define __CAPTURE_H__

#define NUM_CAPTURE_BUFS 2

struct capture_;
typedef struct capture_ capture;

typedef void (*capture_callback) (capture * cap, const unsigned char *frame_data,
				     size_t length, void *user_data);

capture *capture_open(const char *device_name, int width, int height);

capture *capture_open_userio(const char *device_name, int width, int height);

void capture_close(capture * cap);

void capture_start_capturing(capture * cap);

void capture_stop_capturing(capture * cap);

void capture_get_frame(capture * cap, capture_callback cb,
			  void *user_data);

void capture_queue_buffer(capture * cap, const void * buffer_data);

/* Get the properties of the captured frames
 * The v4l device may not support the request size
 */
int capture_get_width(capture * cap);
int capture_get_height(capture * cap);
unsigned int capture_get_pixel_format(capture * cap);

#endif /* __CAPTURE_H__ */
