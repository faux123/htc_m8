/*
 * ispccdc.h
 *
 * TI OMAP3 ISP - CCDC module
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef OMAP3_ISP_CCDC_H
#define OMAP3_ISP_CCDC_H

#include <linux/omap3isp.h>
#include <linux/workqueue.h>

#include "ispvideo.h"

enum ccdc_input_entity {
	CCDC_INPUT_NONE,
	CCDC_INPUT_PARALLEL,
	CCDC_INPUT_CSI2A,
	CCDC_INPUT_CCP2B,
	CCDC_INPUT_CSI2C
};

#define CCDC_OUTPUT_MEMORY	(1 << 0)
#define CCDC_OUTPUT_PREVIEW	(1 << 1)
#define CCDC_OUTPUT_RESIZER	(1 << 2)

#define	OMAP3ISP_CCDC_NEVENTS	16

struct ispccdc_syncif {
	u8 ccdc_mastermode;
	u8 fldstat;
	u8 datsz;
	u8 fldmode;
	u8 datapol;
	u8 fldpol;
	u8 hdpol;
	u8 vdpol;
	u8 fldout;
	u8 hs_width;
	u8 vs_width;
	u8 ppln;
	u8 hlprf;
	u8 bt_r656_en;
};

struct ispccdc_vp {
	unsigned int pixelclk;
};

enum ispccdc_lsc_state {
	LSC_STATE_STOPPED = 0,
	LSC_STATE_STOPPING = 1,
	LSC_STATE_RUNNING = 2,
	LSC_STATE_RECONFIG = 3,
};

struct ispccdc_lsc_config_req {
	struct list_head list;
	struct omap3isp_ccdc_lsc_config config;
	unsigned char enable;
	u32 table;
	struct iovm_struct *iovm;
};

struct ispccdc_lsc {
	enum ispccdc_lsc_state state;
	struct work_struct table_work;

	
	spinlock_t req_lock;
	struct ispccdc_lsc_config_req *request;	
	struct ispccdc_lsc_config_req *active;	
	struct list_head free_queue;	
};

#define CCDC_STOP_NOT_REQUESTED		0x00
#define CCDC_STOP_REQUEST		0x01
#define CCDC_STOP_EXECUTED		(0x02 | CCDC_STOP_REQUEST)
#define CCDC_STOP_CCDC_FINISHED		0x04
#define CCDC_STOP_LSC_FINISHED		0x08
#define CCDC_STOP_FINISHED		\
	(CCDC_STOP_EXECUTED | CCDC_STOP_CCDC_FINISHED | CCDC_STOP_LSC_FINISHED)

#define CCDC_EVENT_VD1			0x10
#define CCDC_EVENT_VD0			0x20
#define CCDC_EVENT_LSC_DONE		0x40

#define CCDC_PAD_SINK			0
#define CCDC_PAD_SOURCE_OF		1
#define CCDC_PAD_SOURCE_VP		2
#define CCDC_PADS_NUM			3

struct isp_ccdc_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[CCDC_PADS_NUM];
	struct v4l2_mbus_framefmt formats[CCDC_PADS_NUM];

	enum ccdc_input_entity input;
	unsigned int output;
	struct isp_video video_out;

	unsigned int alaw:1,
		     lpf:1,
		     obclamp:1,
		     fpc_en:1;
	struct omap3isp_ccdc_blcomp blcomp;
	struct omap3isp_ccdc_bclamp clamp;
	struct omap3isp_ccdc_fpc fpc;
	struct ispccdc_lsc lsc;
	unsigned int update;
	unsigned int shadow_update;

	struct ispccdc_syncif syncif;
	struct ispccdc_vp vpcfg;

	unsigned int underrun:1;
	enum isp_pipeline_stream_state state;
	spinlock_t lock;
	wait_queue_head_t wait;
	unsigned int stopping;
	struct mutex ioctl_lock;
};

struct isp_device;

int omap3isp_ccdc_init(struct isp_device *isp);
void omap3isp_ccdc_cleanup(struct isp_device *isp);
int omap3isp_ccdc_register_entities(struct isp_ccdc_device *ccdc,
	struct v4l2_device *vdev);
void omap3isp_ccdc_unregister_entities(struct isp_ccdc_device *ccdc);

int omap3isp_ccdc_busy(struct isp_ccdc_device *isp_ccdc);
int omap3isp_ccdc_isr(struct isp_ccdc_device *isp_ccdc, u32 events);
void omap3isp_ccdc_restore_context(struct isp_device *isp);
void omap3isp_ccdc_max_rate(struct isp_ccdc_device *ccdc,
	unsigned int *max_rate);

#endif	
