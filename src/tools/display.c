/**
 * SH display
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/fb.h>
#include <uiomux/uiomux.h>
#include <shveu/shveu.h>

#include "display.h"

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, __u32)
#endif

#define min(a, b) ((a) < (b) ? (a) : (b))

#define HW_ALIGN 2
#define RGB_BPP 2

struct DISPLAY {
	int fb_handle;
	struct fb_fix_screeninfo fb_fix;
	struct fb_var_screeninfo fb_var;
	unsigned char *back_buf;
	unsigned char *iomem;
	int fb_size;
	int fb_index;
	int lcd_w;
	int lcd_h;

	int fullscreen;
	struct ren_vid_rect dst_sel;

	SHVEU *veu;
};


DISPLAY *display_open(void)
{
	const char *device;
	DISPLAY *disp;

	disp = calloc(1, sizeof(*disp));
	if (!disp)
		return NULL;

	disp->veu = shveu_open();
	if (!disp->veu) {
		free(disp);
		return NULL;
	}

	/* Initialize display */
	device = getenv("FRAMEBUFFER");
	if (!device) {
		if (access("/dev/.devfsd", F_OK) == 0) {
			device = "/dev/fb/0";
		} else {
			device = "/dev/fb0";
		}
	}

	if ((disp->fb_handle = open(device, O_RDWR)) < 0) {
		fprintf(stderr, "Open %s: %s.\n", device, strerror(errno));
		free(disp);
		return 0;
	}
	if (ioctl(disp->fb_handle, FBIOGET_FSCREENINFO, &disp->fb_fix) < 0) {
		fprintf(stderr, "Ioctl FBIOGET_FSCREENINFO error.\n");
		free(disp);
		return 0;
	}
	if (ioctl(disp->fb_handle, FBIOGET_VSCREENINFO, &disp->fb_var) < 0) {
		fprintf(stderr, "Ioctl FBIOGET_VSCREENINFO error.\n");
		free(disp);
		return 0;
	}
	if (disp->fb_fix.type != FB_TYPE_PACKED_PIXELS) {
		fprintf(stderr, "Frame buffer isn't packed pixel.\n");
		free(disp);
		return 0;
	}

	/* clear framebuffer and back buffer */
	disp->fb_size = (RGB_BPP * disp->fb_var.xres * disp->fb_var.yres * disp->fb_var.bits_per_pixel) / 8;
	disp->iomem = mmap(0, disp->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, disp->fb_handle, 0);
	if (disp->iomem != MAP_FAILED) {
		memset(disp->iomem, 0, disp->fb_size);
	}

	/* Register the framebuffer with UIOMux */
	uiomux_register (disp->iomem, disp->fb_fix.smem_start, disp->fb_size);

	disp->lcd_w = disp->fb_var.xres;
	disp->lcd_h = disp->fb_var.yres;

	disp->back_buf = disp->iomem;
	disp->fb_index = 0;
	display_flip(disp);

	display_set_fullscreen(disp);

	return disp;
}

void display_close(DISPLAY *disp)
{
	disp->fb_var.xoffset = 0;
	disp->fb_var.yoffset = 0;

	uiomux_unregister(disp->iomem);
	munmap(disp->iomem, disp->fb_size);

	/* Restore the framebuffer to the front buffer */
	ioctl(disp->fb_handle, FBIOPAN_DISPLAY, &disp->fb_var);

	close(disp->fb_handle);
	shveu_close(disp->veu);
	free(disp);
}

int display_get_format(DISPLAY *disp)
{
	return REN_RGB565;
}

int display_get_width(DISPLAY *disp)
{
	return disp->lcd_w;
}

int display_get_height(DISPLAY *disp)
{
	return disp->lcd_h;
}

unsigned char *display_get_back_buff(DISPLAY *disp)
{
	int frame_offset = RGB_BPP * (1-disp->fb_index) * disp->lcd_w * disp->lcd_h;
	return (disp->iomem + frame_offset);
}

int display_flip(DISPLAY *disp)
{
	struct fb_var_screeninfo fb_screen = disp->fb_var;
	unsigned long crt = 0;

	fb_screen.xoffset = 0;
	fb_screen.yoffset = 0;
	if (disp->fb_index==0)
		fb_screen.yoffset = disp->fb_var.yres;
	if (-1 == ioctl(disp->fb_handle, FBIOPAN_DISPLAY, &fb_screen))
		return 0;

	/* Point to the back buffer */
	disp->back_buf = disp->iomem;
	if (disp->fb_index!=0)
		disp->back_buf += disp->fb_fix.line_length * disp->fb_var.yres;

	disp->fb_index = (disp->fb_index+1) & 1;

	/* wait for vsync interrupt */
	ioctl(disp->fb_handle, FBIO_WAITFORVSYNC, &crt);

	return 1;
}


int display_update(
	DISPLAY *disp,
	struct ren_vid_surface *src)
{
	float scale, aspect_x, aspect_y;
	int ret;
	struct ren_vid_surface src2 = *src;
	struct ren_vid_surface dst;
	struct ren_vid_rect src_sel;
	struct ren_vid_rect dst_sel;

	dst.format = REN_RGB565;
	dst.w = disp->lcd_w;
	dst.h = disp->lcd_h;
	dst.pitch = disp->lcd_w;
	dst.py = disp->back_buf;
	dst.pc = dst.py + (disp->lcd_w * disp->lcd_h);
	dst.pa = NULL;

	if (disp->fullscreen) {
		/* Stick with the source aspect ratio */
		aspect_x = (float) disp->lcd_w / src->w;
		aspect_y = (float) disp->lcd_h / src->h;
		if (aspect_x > aspect_y) {
			scale = aspect_y;
		} else {
			scale = aspect_x;
		}

		/* Center it */
		dst_sel.w = (int) (src->w * scale);
		dst_sel.h = (int) (src->h * scale);
		dst_sel.x = (disp->lcd_w - dst.w)/2;
		dst_sel.y = (disp->lcd_h - dst.h)/2;
		get_sel_surface(&dst, &dst, &dst_sel);

	} else {
		dst_sel = disp->dst_sel;

		src_sel.w = src->w;
		src_sel.h = src->h;
		src_sel.x = 0;
		src_sel.y = 0;

		/* TODO Handle output off-surface to the left or above by using part of the input */

		/* Handle output off-surface to the right or below by cropping the input & output */
		if ((dst_sel.x + dst_sel.w) > disp->lcd_w) {
			src_sel.w = (int)((disp->lcd_w - dst_sel.x) / scale);
			dst_sel.w = disp->lcd_w - dst_sel.x;
		}
		if ((dst_sel.y + dst_sel.h) > disp->lcd_h) {
			src_sel.h = (int)((disp->lcd_h - dst_sel.y) / scale);
			dst_sel.h = disp->lcd_h - dst_sel.y;
		}

		if (src_sel.w <= 0 || src_sel.h <= 0)
			return 0;
		if (dst_sel.w <= 0 || dst_sel.h <= 0)
			return 0;

		get_sel_surface(&src2, src,  &src_sel);
		get_sel_surface(&dst,  &dst, &dst_sel);
	}

	/* Hardware resize */
	ret = shveu_resize(disp->veu, &src2, &dst);

	if (!ret)
		display_flip(disp);

	return ret;
}

void display_set_fullscreen(DISPLAY *disp)
{
	disp->fullscreen = 1;
}

void display_set_position(DISPLAY *disp, int w, int h, int x, int y)
{
	disp->fullscreen = 0;
	disp->dst_sel.w = w;
	disp->dst_sel.h = h;
	disp->dst_sel.x = x;
	disp->dst_sel.y = y;
}

