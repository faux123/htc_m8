/*
 * Handles the M-Systems DiskOnChip G3 chip
 *
 * Copyright (C) 2011 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _MTD_DOCG3_H
#define _MTD_DOCG3_H

#include <linux/mtd/mtd.h>

#define DOC_IOSPACE_IPL			0x0000
#define DOC_IOSPACE_DATA		0x0800
#define DOC_IOSPACE_SIZE		0x2000


#define DOC_ADDR_PAGE_MASK		0x3f
#define DOC_ADDR_BLOCK_SHIFT		6
#define DOC_LAYOUT_NBPLANES		2
#define DOC_LAYOUT_PAGES_PER_BLOCK	64
#define DOC_LAYOUT_PAGE_SIZE		512
#define DOC_LAYOUT_OOB_SIZE		16
#define DOC_LAYOUT_WEAR_SIZE		8
#define DOC_LAYOUT_PAGE_OOB_SIZE				\
	(DOC_LAYOUT_PAGE_SIZE + DOC_LAYOUT_OOB_SIZE)
#define DOC_LAYOUT_WEAR_OFFSET		(DOC_LAYOUT_PAGE_OOB_SIZE * 2)
#define DOC_LAYOUT_BLOCK_SIZE					\
	(DOC_LAYOUT_PAGES_PER_BLOCK * DOC_LAYOUT_PAGE_SIZE)

#define DOC_ECC_BCH_M			14
#define DOC_ECC_BCH_T			4
#define DOC_ECC_BCH_PRIMPOLY		0x4443
#define DOC_ECC_BCH_SIZE		7
#define DOC_ECC_BCH_COVERED_BYTES				\
	(DOC_LAYOUT_PAGE_SIZE + DOC_LAYOUT_OOB_PAGEINFO_SZ +	\
	 DOC_LAYOUT_OOB_HAMMING_SZ)
#define DOC_ECC_BCH_TOTAL_BYTES					\
	(DOC_ECC_BCH_COVERED_BYTES + DOC_LAYOUT_OOB_BCH_SZ)

#define DOC_LAYOUT_BLOCK_BBT		0
#define DOC_LAYOUT_BLOCK_OTP		0
#define DOC_LAYOUT_BLOCK_FIRST_DATA	6

#define DOC_LAYOUT_PAGE_BBT		4

#define DOC_LAYOUT_OOB_PAGEINFO_OFS	0
#define DOC_LAYOUT_OOB_HAMMING_OFS	7
#define DOC_LAYOUT_OOB_BCH_OFS		8
#define DOC_LAYOUT_OOB_UNUSED_OFS	15
#define DOC_LAYOUT_OOB_PAGEINFO_SZ	7
#define DOC_LAYOUT_OOB_HAMMING_SZ	1
#define DOC_LAYOUT_OOB_BCH_SZ		7
#define DOC_LAYOUT_OOB_UNUSED_SZ	1


#define DOC_CHIPID_G3			0x200
#define DOC_ERASE_MARK			0xaa
#define DOC_MAX_NBFLOORS		4
#define DOC_CHIPID			0x1000
#define DOC_TEST			0x1004
#define DOC_BUSLOCK			0x1006
#define DOC_ENDIANCONTROL		0x1008
#define DOC_DEVICESELECT		0x100a
#define DOC_ASICMODE			0x100c
#define DOC_CONFIGURATION		0x100e
#define DOC_INTERRUPTCONTROL		0x1010
#define DOC_READADDRESS			0x101a
#define DOC_DATAEND			0x101e
#define DOC_INTERRUPTSTATUS		0x1020

#define DOC_FLASHSEQUENCE		0x1032
#define DOC_FLASHCOMMAND		0x1034
#define DOC_FLASHADDRESS		0x1036
#define DOC_FLASHCONTROL		0x1038
#define DOC_NOP				0x103e

#define DOC_ECCCONF0			0x1040
#define DOC_ECCCONF1			0x1042
#define DOC_ECCPRESET			0x1044
#define DOC_HAMMINGPARITY		0x1046
#define DOC_BCH_HW_ECC(idx)		(0x1048 + idx)

#define DOC_PROTECTION			0x1056
#define DOC_DPS0_KEY			0x105c
#define DOC_DPS1_KEY			0x105e
#define DOC_DPS0_ADDRLOW		0x1060
#define DOC_DPS0_ADDRHIGH		0x1062
#define DOC_DPS1_ADDRLOW		0x1064
#define DOC_DPS1_ADDRHIGH		0x1066
#define DOC_DPS0_STATUS			0x106c
#define DOC_DPS1_STATUS			0x106e

#define DOC_ASICMODECONFIRM		0x1072
#define DOC_CHIPID_INV			0x1074
#define DOC_POWERMODE			0x107c

#define DOC_SEQ_RESET			0x00
#define DOC_SEQ_PAGE_SIZE_532		0x03
#define DOC_SEQ_SET_FASTMODE		0x05
#define DOC_SEQ_SET_RELIABLEMODE	0x09
#define DOC_SEQ_READ			0x12
#define DOC_SEQ_SET_PLANE1		0x0e
#define DOC_SEQ_SET_PLANE2		0x10
#define DOC_SEQ_PAGE_SETUP		0x1d
#define DOC_SEQ_ERASE			0x27
#define DOC_SEQ_PLANES_STATUS		0x31

#define DOC_CMD_READ_PLANE1		0x00
#define DOC_CMD_SET_ADDR_READ		0x05
#define DOC_CMD_READ_ALL_PLANES		0x30
#define DOC_CMD_READ_PLANE2		0x50
#define DOC_CMD_READ_FLASH		0xe0
#define DOC_CMD_PAGE_SIZE_532		0x3c

#define DOC_CMD_PROG_BLOCK_ADDR		0x60
#define DOC_CMD_PROG_CYCLE1		0x80
#define DOC_CMD_PROG_CYCLE2		0x10
#define DOC_CMD_PROG_CYCLE3		0x11
#define DOC_CMD_ERASECYCLE2		0xd0
#define DOC_CMD_READ_STATUS		0x70
#define DOC_CMD_PLANES_STATUS		0x71

#define DOC_CMD_RELIABLE_MODE		0x22
#define DOC_CMD_FAST_MODE		0xa2

#define DOC_CMD_RESET			0xff

#define DOC_CTRL_VIOLATION		0x20
#define DOC_CTRL_CE			0x10
#define DOC_CTRL_UNKNOWN_BITS		0x08
#define DOC_CTRL_PROTECTION_ERROR	0x04
#define DOC_CTRL_SEQUENCE_ERROR		0x02
#define DOC_CTRL_FLASHREADY		0x01

#define DOC_ASICMODE_RESET		0x00
#define DOC_ASICMODE_NORMAL		0x01
#define DOC_ASICMODE_POWERDOWN		0x02
#define DOC_ASICMODE_MDWREN		0x04
#define DOC_ASICMODE_BDETCT_RESET	0x08
#define DOC_ASICMODE_RSTIN_RESET	0x10
#define DOC_ASICMODE_RAM_WE		0x20

#define DOC_ECCCONF0_WRITE_MODE		0x0000
#define DOC_ECCCONF0_READ_MODE		0x8000
#define DOC_ECCCONF0_AUTO_ECC_ENABLE	0x4000
#define DOC_ECCCONF0_HAMMING_ENABLE	0x1000
#define DOC_ECCCONF0_BCH_ENABLE		0x0800
#define DOC_ECCCONF0_DATA_BYTES_MASK	0x07ff

#define DOC_ECCCONF1_BCH_SYNDROM_ERR	0x80
#define DOC_ECCCONF1_UNKOWN1		0x40
#define DOC_ECCCONF1_PAGE_IS_WRITTEN	0x20
#define DOC_ECCCONF1_UNKOWN3		0x10
#define DOC_ECCCONF1_HAMMING_BITS_MASK	0x0f

#define DOC_PROTECT_FOUNDRY_OTP_LOCK	0x01
#define DOC_PROTECT_CUSTOMER_OTP_LOCK	0x02
#define DOC_PROTECT_LOCK_INPUT		0x04
#define DOC_PROTECT_STICKY_LOCK		0x08
#define DOC_PROTECT_PROTECTION_ENABLED	0x10
#define DOC_PROTECT_IPL_DOWNLOAD_LOCK	0x20
#define DOC_PROTECT_PROTECTION_ERROR	0x80

#define DOC_DPS_OTP_PROTECTED		0x01
#define DOC_DPS_READ_PROTECTED		0x02
#define DOC_DPS_WRITE_PROTECTED		0x04
#define DOC_DPS_HW_LOCK_ENABLED		0x08
#define DOC_DPS_KEY_OK			0x80

#define DOC_CONF_IF_CFG			0x80
#define DOC_CONF_MAX_ID_MASK		0x30
#define DOC_CONF_VCCQ_3V		0x01

#define DOC_READADDR_INC		0x8000
#define DOC_READADDR_ONE_BYTE		0x4000
#define DOC_READADDR_ADDR_MASK		0x1fff

#define DOC_POWERDOWN_READY		0x80

#define DOC_PLANES_STATUS_FAIL		0x01
#define DOC_PLANES_STATUS_PLANE0_KO	0x02
#define DOC_PLANES_STATUS_PLANE1_KO	0x04

/*
 * DPS key management
 *
 * Each floor of docg3 has 2 protection areas: DPS0 and DPS1. These areas span
 * across block boundaries, and define whether these blocks can be read or
 * written.
 * The definition is dynamically stored in page 0 of blocks (2,3) for DPS0, and
 * page 0 of blocks (4,5) for DPS1.
 */
#define DOC_LAYOUT_DPS_KEY_LENGTH	8

struct docg3_cascade {
	struct mtd_info *floors[DOC_MAX_NBFLOORS];
	void __iomem *base;
	struct bch_control *bch;
	struct mutex lock;
};

struct docg3 {
	struct device *dev;
	struct docg3_cascade *cascade;
	unsigned int device_id:4;
	unsigned int if_cfg:1;
	unsigned int reliable:2;
	int max_block;
	u8 *bbt;
	loff_t oob_write_ofs;
	int oob_autoecc;
	u8 oob_write_buf[DOC_LAYOUT_OOB_SIZE];
	struct dentry *debugfs_root;
};

#define doc_err(fmt, arg...) dev_err(docg3->dev, (fmt), ## arg)
#define doc_info(fmt, arg...) dev_info(docg3->dev, (fmt), ## arg)
#define doc_dbg(fmt, arg...) dev_dbg(docg3->dev, (fmt), ## arg)
#define doc_vdbg(fmt, arg...) dev_vdbg(docg3->dev, (fmt), ## arg)

#define DEBUGFS_RO_ATTR(name, show_fct) \
	static int name##_open(struct inode *inode, struct file *file) \
	{ return single_open(file, show_fct, inode->i_private); }      \
	static const struct file_operations name##_fops = { \
		.owner = THIS_MODULE, \
		.open = name##_open, \
		.llseek = seq_lseek, \
		.read = seq_read, \
		.release = single_release \
	};
#endif

#undef TRACE_SYSTEM
#define TRACE_SYSTEM docg3

#if !defined(_MTD_DOCG3_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define _MTD_DOCG3_TRACE

#include <linux/tracepoint.h>

TRACE_EVENT(docg3_io,
	    TP_PROTO(int op, int width, u16 reg, int val),
	    TP_ARGS(op, width, reg, val),
	    TP_STRUCT__entry(
		    __field(int, op)
		    __field(unsigned char, width)
		    __field(u16, reg)
		    __field(int, val)),
	    TP_fast_assign(
		    __entry->op = op;
		    __entry->width = width;
		    __entry->reg = reg;
		    __entry->val = val;),
	    TP_printk("docg3: %s%02d reg=%04x, val=%04x",
		      __entry->op ? "write" : "read", __entry->width,
		      __entry->reg, __entry->val)
	);
#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE docg3
#include <trace/define_trace.h>
