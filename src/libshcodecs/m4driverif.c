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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <uiomux/uiomux.h>
#include <shcodecs/shcodecs_common.h>
#include <m4iph_vpu4.h>
#include <avcbd.h>
#include <avcbe.h>
#include "avcbd_optionaldata.h"
#include "m4driverif.h"

/* Minimum size as this buffer is used for data other than encoded frames */
/* TODO min size has not been verified, just taken from sample code */
#define MIN_STREAM_BUFF_SIZE (160000*4)
#define minCR 4

struct uio_map {
	unsigned long address;
	unsigned long size;
	void *iomem;
};

/* VPU data - common for all encoder & decoder instances */
typedef struct _SHCodecs_vpu {
	UIOMux *uiomux;
	struct uio_map uio_mmio;

	M4IPH_VPU4_INIT_OPTION params;
	unsigned long work_buff_size;
	void *work_buff;
} SHCodecs_vpu;

/* The current instance in use */
static SHCodecs_vpu *current_vpu = NULL;

void *m4iph_vpu_open(int stream_buf_size)
{
	SHCodecs_vpu *vpu;
	int ret;
	void *virt;

	vpu = calloc(1, sizeof(*vpu));
	if (!vpu)
		return NULL;

	vpu->uiomux = uiomux_open();
	if (!vpu->uiomux)
		goto err;

	ret = uiomux_get_mmio (vpu->uiomux, UIOMUX_SH_VPU,
		&vpu->uio_mmio.address,
		&vpu->uio_mmio.size,
		&vpu->uio_mmio.iomem);
	if (!ret)
		goto err;

	/* Work buffer must fit largest NAL/VOP. Assume minCR (minimum compression
	   ratio) at this size is 4. */
	vpu->work_buff_size = SHCODECS_MAX_FX * SHCODECS_MAX_FY / minCR;

	/* Work buffer is also used for other encoder data; apply minimum size constraint */
	if (vpu->work_buff_size < MIN_STREAM_BUFF_SIZE)
		vpu->work_buff_size = MIN_STREAM_BUFF_SIZE;

	/* Note: This must be done outside vpu lock as UIOMux malloc also locks the vpu */
	virt = uiomux_malloc_shared (vpu->uiomux, UIOMUX_SH_VPU, vpu->work_buff_size, 32);
	vpu->work_buff = (void *)uiomux_virt_to_phys (vpu->uiomux, UIOMUX_SH_VPU, virt);
	if (!vpu->work_buff)
		goto err;

	vpu->params.m4iph_vpu_base_address = vpu->uio_mmio.address;

	/* Little endian */
	vpu->params.m4iph_vpu_endian = 0x3ff;

#ifdef DISABLE_INT
	vpu->params.m4iph_vpu_interrupt_enable = M4IPH_OFF;
#else
	vpu->params.m4iph_vpu_interrupt_enable = M4IPH_ON;
#endif

	vpu->params.m4iph_vpu_clock_supply_control = M4IPH_CTL_FRAME_UNIT;
	vpu->params.m4iph_vpu_mask_address_disable = M4IPH_OFF;

	vpu->params.m4iph_temporary_buff_address = (unsigned long)vpu->work_buff;
	vpu->params.m4iph_temporary_buff_size = vpu->work_buff_size;

	/* Initialize VPU */
	m4iph_vpu_lock(vpu);
	ret = m4iph_vpu4_init(&vpu->params);
	m4iph_vpu_unlock(vpu);

	if (ret)
		goto err;

	return vpu;

err:
	m4iph_vpu_close(vpu);
	return NULL;
}

void m4iph_vpu_close(void *vpu_data)
{
	SHCodecs_vpu *vpu = (SHCodecs_vpu *)vpu_data;

	if (vpu) {
		if (vpu->uiomux)
			uiomux_close(vpu->uiomux);
		free(vpu);
	}
}

void m4iph_vpu_lock(void *vpu_data)
{
	SHCodecs_vpu *vpu = (SHCodecs_vpu *)vpu_data;
	uiomux_lock (vpu->uiomux, UIOMUX_SH_VPU);
	current_vpu = vpu;
}

void m4iph_vpu_unlock(void *vpu_data)
{
	SHCodecs_vpu *vpu = (SHCodecs_vpu *)vpu_data;
	current_vpu = NULL;
	uiomux_unlock (vpu->uiomux, UIOMUX_SH_VPU);
}

void m4iph_avcbd_perror(char *msg, int error)
{
	fprintf(stderr, "%s: ", msg);
	switch (error) {
	case AVCBD_PARAM_ERROR:
		fprintf(stderr, "Parameter Error\n");
		break;
	case AVCBD_PIC_TIMEOUT:
		fprintf(stderr, "Timeout\n");
		break;
	case AVCBD_PIC_ABORT:
		fprintf(stderr, "Abort\n");
		break;
	case AVCBD_PIC_EOS:
		fprintf(stderr, "End of Stream\n");
		break;
	case AVCBD_PIC_LARGE:
		fprintf(stderr, "Picture too large\n");
		break;
	case AVCBD_PIC_FEWERMB:
		fprintf(stderr, "AVCBD_PIC_FEWERMB Error\n");
		break;
	case AVCBD_PIC_FMTERROR:
		fprintf(stderr, "Format Error\n");
		break;
	case AVCBD_PIC_ERROR:
		fprintf(stderr, "Picture Error\n");
		break;
	case AVCBD_MB_OVERRUN:
		fprintf(stderr, "Overrun Error\n");
		break;
	case AVCBD_ILLEGAL_DATA:
		fprintf(stderr, "Illegal Data\n");
		break;
	case AVCBD_ILLEGAL_CODE:
		fprintf(stderr, "Illegal Code\n");
		break;
	case AVCBD_ILLEGAL_MTYPE:
		fprintf(stderr, "Illegal Mtype\n");
		break;
	case AVCBD_BUFFER_OVERRUN:
		fprintf(stderr, "Buffer Overrun\n");
		break;
	case AVCBD_PIC_EXCESS_FRAME:
		fprintf(stderr, "Excess Frames\n");
		break;
	case AVCBD_PIC_EXCESS_PPS:
		fprintf(stderr, "Excess PPS\n");
		break;
	case AVCBD_TEMPORARY_BUFFER_SHORT_ERROR:
		fprintf(stderr, "Temporary buffer too short\n");
		break;
	case AVCBD_VPU_ERROR:
		fprintf(stderr, "General VPU Error\n");
		break;
	default:
		fprintf(stderr, "Unknown error %d\n", error);
	}
}

void m4iph_avcbe_perror(char *msg, int error)
{
	fprintf(stderr, "%s: ", msg);
	switch (error) {
	case AVCBE_ENCODE_ERROR:
		fprintf(stderr, "Parameter Error\n");
		break;
	case AVCBE_NOT_IN_ORDER_ERROR:
		fprintf(stderr, "Function call not-in-order\n");
		break;
	case AVCBE_OUTPUT_BUFFER_SHORT_ERROR:
		fprintf(stderr, "Stream-output buffer is short\n");
		break;
	case AVCBE_VPU_ERROR_AFTER_ENCODING:
		fprintf(stderr, "End of Stream\n");
		break;
	case AVCBE_TEMPORARY_BUFFER_SHORT_ERROR:
		fprintf(stderr, "Temporary buffer too short\n");
		break;
	case AVCBE_VUI_PARAMETERS_NOT_SPECIFIED_ERROR:
		fprintf(stderr, "avcbe_set_vui_parameters was not called\n");
		break;
	case AVCBE_QUANT_MATRIX_NOT_SPECIFIED_ERROR:
		fprintf(stderr, "avcbe_set_quant_type1 function was not called\n");
		break;
	case AVCBE_WORK_AREA_SHORT_ERROR:
		fprintf(stderr, "size of work-field area is short\n");
		break;
	default:
		fprintf(stderr, "Unknown error %d\n", error);
	}
}

void *m4iph_addr_to_virt(void *vpu_data, void *address)
{
	SHCodecs_vpu *vpu = (SHCodecs_vpu *)vpu_data;
	return uiomux_phys_to_virt (vpu->uiomux, UIOMUX_SH_VPU, (unsigned long)address);
}


/* User defined functions as specified by the Encoder/Decoder middleware
 * documents.
 */
long m4iph_sleep(void)
{
	SHCodecs_vpu *vpu = current_vpu;

#ifdef DISABLE_INT
	while (m4iph_vpu4_status() != 0);
#else
	uiomux_sleep(vpu->uiomux, UIOMUX_SH_VPU);
#endif
	m4iph_vpu4_int_handler();

	return 0;
}

void m4iph_start(void)
{
	/* Do nothing for Linux */
}

void m4iph_restart(void)
{
	/* Do nothing for Linux */
}

unsigned long m4iph_reg_table_read(unsigned long *addr,
				   unsigned long *data, long nr_longs)
{
	unsigned long *reg_base = current_vpu->uio_mmio.iomem;
	int k;

	reg_base += ((unsigned long) addr - current_vpu->uio_mmio.address) / 4;

	for (k = 0; k < nr_longs; k++)
		data[k] = reg_base[k];
	return nr_longs;
}

void m4iph_reg_table_write(unsigned long *addr, unsigned long *data,
			   long nr_longs)
{
	unsigned long *reg_base = current_vpu->uio_mmio.iomem;
	int k;

	reg_base += ((unsigned long) addr - current_vpu->uio_mmio.address) / 4;

	for (k = 0; k < nr_longs; k++)
		reg_base[k] = data[k];
}

unsigned long m4iph_sdr_read(unsigned char *src_phys, unsigned char *dest_virt,
			     unsigned long count)
{
	SHCodecs_vpu *vpu = current_vpu;
	unsigned char *src_virt;

	src_virt = uiomux_phys_to_virt (vpu->uiomux, UIOMUX_SH_VPU, (unsigned long)src_phys);
	if (!src_virt) {
		fprintf(stderr, "%s: src_phys %p invalid\n", __func__, src_phys);
		return 0;
	}
	memcpy(dest_virt, src_virt, count);
	return count;
}

/* Same arg order as memcpy; does alignment on dest */
void m4iph_sdr_write(unsigned char *dest_phys, unsigned char *src_virt,
		     unsigned long count)
{
	SHCodecs_vpu *vpu = current_vpu;
	unsigned char *dest_virt;

	dest_virt = uiomux_phys_to_virt (vpu->uiomux, UIOMUX_SH_VPU, (unsigned long)dest_phys);
	if (!dest_virt)
		fprintf(stderr, "%s: dest_phys %p invalid\n", __func__, dest_phys);
	memcpy(dest_virt, src_virt, count);
}

/* Allocate sdr memory */
void *m4iph_sdr_malloc(void *vpu_data, unsigned long count, int align)
{
	SHCodecs_vpu *vpu = (SHCodecs_vpu *)vpu_data;
	void *virt = uiomux_malloc (vpu->uiomux, UIOMUX_SH_VPU, count, align);
	return (void *)uiomux_virt_to_phys (vpu->uiomux, UIOMUX_SH_VPU, virt);
}

void m4iph_sdr_free(void *vpu_data, void *address, unsigned long count)
{
	SHCodecs_vpu *vpu = (SHCodecs_vpu *)vpu_data;
	void *virt = uiomux_phys_to_virt (vpu->uiomux, UIOMUX_SH_VPU, (unsigned long)address);
	uiomux_free (vpu->uiomux, UIOMUX_SH_VPU, virt, count);
}


void avcbd_get_time_code(long h, long m, long s)
{
}

long m4iph_dec_continue(void)
{
	return 0;
}

long m4iph_enc_continue(long output_bits)
{
	return 1;
}

