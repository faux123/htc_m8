/*
 *
 * include/linux/coh901318.h
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * DMA driver for COH 901 318
 * Author: Per Friden <per.friden@stericsson.com>
 */

#ifndef COH901318_H
#define COH901318_H

#include <linux/device.h>
#include <linux/dmaengine.h>

#define MAX_DMA_PACKET_SIZE_SHIFT 11
#define MAX_DMA_PACKET_SIZE (1 << MAX_DMA_PACKET_SIZE_SHIFT)

struct coh901318_lli {
	u32 control;
	dma_addr_t src_addr;
	dma_addr_t dst_addr;
	dma_addr_t link_addr;

	void *virt_link_addr;
	dma_addr_t phy_this;
};
struct coh901318_params {
	u32 config;
	u32 ctrl_lli_last;
	u32 ctrl_lli;
	u32 ctrl_lli_chained;
};
struct coh_dma_channel {
	const char name[32];
	const int number;
	const int desc_nbr_max;
	const int priority_high;
	const struct coh901318_params param;
	const dma_addr_t dev_addr;
};

typedef void (*dma_access_memory_state_t)(struct device *dev,
					  bool active);

struct powersave {
	spinlock_t lock;
	u64 started_channels;
};
struct coh901318_platform {
	const int *chans_slave;
	const int *chans_memcpy;
	const dma_access_memory_state_t access_memory_state;
	const struct coh_dma_channel *chan_conf;
	const int max_channels;
};

#ifdef CONFIG_COH901318
bool coh901318_filter_id(struct dma_chan *chan, void *chan_id);
#else
static inline bool coh901318_filter_id(struct dma_chan *chan, void *chan_id)
{
	return false;
}
#endif


#define COH901318_MOD32_MASK					(0x1F)
#define COH901318_WORD_MASK					(0xFFFFFFFF)
#define COH901318_INT_STATUS1					(0x0000)
#define COH901318_INT_STATUS2					(0x0004)
#define COH901318_TC_INT_STATUS1				(0x0008)
#define COH901318_TC_INT_STATUS2				(0x000C)
#define COH901318_TC_INT_CLEAR1					(0x0010)
#define COH901318_TC_INT_CLEAR2					(0x0014)
#define COH901318_RAW_TC_INT_STATUS1				(0x0018)
#define COH901318_RAW_TC_INT_STATUS2				(0x001C)
#define COH901318_BE_INT_STATUS1				(0x0020)
#define COH901318_BE_INT_STATUS2				(0x0024)
#define COH901318_BE_INT_CLEAR1					(0x0028)
#define COH901318_BE_INT_CLEAR2					(0x002C)
#define COH901318_RAW_BE_INT_STATUS1				(0x0030)
#define COH901318_RAW_BE_INT_STATUS2				(0x0034)

#define COH901318_CX_CFG					(0x0100)
#define COH901318_CX_CFG_SPACING				(0x04)
#define COH901318_CX_CFG_CH_ENABLE				(0x00000001)
#define COH901318_CX_CFG_CH_DISABLE				(0x00000000)
#define COH901318_CX_CFG_RM_MASK				(0x00000006)
#define COH901318_CX_CFG_RM_MEMORY_TO_MEMORY			(0x0 << 1)
#define COH901318_CX_CFG_RM_PRIMARY_TO_MEMORY			(0x1 << 1)
#define COH901318_CX_CFG_RM_MEMORY_TO_PRIMARY			(0x1 << 1)
#define COH901318_CX_CFG_RM_PRIMARY_TO_SECONDARY		(0x3 << 1)
#define COH901318_CX_CFG_RM_SECONDARY_TO_PRIMARY		(0x3 << 1)
#define COH901318_CX_CFG_LCRF_SHIFT				3
#define COH901318_CX_CFG_LCRF_MASK				(0x000001F8)
#define COH901318_CX_CFG_LCR_DISABLE				(0x00000000)
#define COH901318_CX_CFG_TC_IRQ_ENABLE				(0x00000200)
#define COH901318_CX_CFG_TC_IRQ_DISABLE				(0x00000000)
#define COH901318_CX_CFG_BE_IRQ_ENABLE				(0x00000400)
#define COH901318_CX_CFG_BE_IRQ_DISABLE				(0x00000000)

#define COH901318_CX_STAT					(0x0200)
#define COH901318_CX_STAT_SPACING				(0x04)
#define COH901318_CX_STAT_RBE_IRQ_IND				(0x00000008)
#define COH901318_CX_STAT_RTC_IRQ_IND				(0x00000004)
#define COH901318_CX_STAT_ACTIVE				(0x00000002)
#define COH901318_CX_STAT_ENABLED				(0x00000001)

#define COH901318_CX_CTRL					(0x0400)
#define COH901318_CX_CTRL_SPACING				(0x10)
#define COH901318_CX_CTRL_TC_ENABLE				(0x00001000)
#define COH901318_CX_CTRL_TC_DISABLE				(0x00000000)
#define COH901318_CX_CTRL_TC_VALUE_MASK				(0x00000FFF)
#define COH901318_CX_CTRL_BURST_COUNT_MASK			(0x0000E000)
#define COH901318_CX_CTRL_BURST_COUNT_64_BYTES			(0x7 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_48_BYTES			(0x6 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_32_BYTES			(0x5 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_16_BYTES			(0x4 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_8_BYTES			(0x3 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_4_BYTES			(0x2 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_2_BYTES			(0x1 << 13)
#define COH901318_CX_CTRL_BURST_COUNT_1_BYTE			(0x0 << 13)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_MASK			(0x00030000)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_32_BITS			(0x2 << 16)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_16_BITS			(0x1 << 16)
#define COH901318_CX_CTRL_SRC_BUS_SIZE_8_BITS			(0x0 << 16)
#define COH901318_CX_CTRL_SRC_ADDR_INC_ENABLE			(0x00040000)
#define COH901318_CX_CTRL_SRC_ADDR_INC_DISABLE			(0x00000000)
#define COH901318_CX_CTRL_DST_BUS_SIZE_MASK			(0x00180000)
#define COH901318_CX_CTRL_DST_BUS_SIZE_32_BITS			(0x2 << 19)
#define COH901318_CX_CTRL_DST_BUS_SIZE_16_BITS			(0x1 << 19)
#define COH901318_CX_CTRL_DST_BUS_SIZE_8_BITS			(0x0 << 19)
#define COH901318_CX_CTRL_DST_ADDR_INC_ENABLE			(0x00200000)
#define COH901318_CX_CTRL_DST_ADDR_INC_DISABLE			(0x00000000)
#define COH901318_CX_CTRL_MASTER_MODE_MASK			(0x00C00000)
#define COH901318_CX_CTRL_MASTER_MODE_M2R_M1W			(0x3 << 22)
#define COH901318_CX_CTRL_MASTER_MODE_M1R_M2W			(0x2 << 22)
#define COH901318_CX_CTRL_MASTER_MODE_M2RW			(0x1 << 22)
#define COH901318_CX_CTRL_MASTER_MODE_M1RW			(0x0 << 22)
#define COH901318_CX_CTRL_TCP_ENABLE				(0x01000000)
#define COH901318_CX_CTRL_TCP_DISABLE				(0x00000000)
#define COH901318_CX_CTRL_TC_IRQ_ENABLE				(0x02000000)
#define COH901318_CX_CTRL_TC_IRQ_DISABLE			(0x00000000)
#define COH901318_CX_CTRL_HSP_ENABLE				(0x04000000)
#define COH901318_CX_CTRL_HSP_DISABLE				(0x00000000)
#define COH901318_CX_CTRL_HSS_ENABLE				(0x08000000)
#define COH901318_CX_CTRL_HSS_DISABLE				(0x00000000)
#define COH901318_CX_CTRL_DDMA_MASK				(0x30000000)
#define COH901318_CX_CTRL_DDMA_LEGACY				(0x0 << 28)
#define COH901318_CX_CTRL_DDMA_DEMAND_DMA1			(0x1 << 28)
#define COH901318_CX_CTRL_DDMA_DEMAND_DMA2			(0x2 << 28)
#define COH901318_CX_CTRL_PRDD_MASK				(0x40000000)
#define COH901318_CX_CTRL_PRDD_DEST				(0x1 << 30)
#define COH901318_CX_CTRL_PRDD_SOURCE				(0x0 << 30)

#define COH901318_CX_SRC_ADDR					(0x0404)
#define COH901318_CX_SRC_ADDR_SPACING				(0x10)

#define COH901318_CX_DST_ADDR					(0x0408)
#define COH901318_CX_DST_ADDR_SPACING				(0x10)

#define COH901318_CX_LNK_ADDR					(0x040C)
#define COH901318_CX_LNK_ADDR_SPACING				(0x10)
#define COH901318_CX_LNK_LINK_IMMEDIATE				(0x00000001)
#endif 
