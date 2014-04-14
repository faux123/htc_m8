/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2010-2012 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#ifndef _VFDI_H
#define _VFDI_H

/**
 * DOC: Virtual Function Driver Interface
 *
 * This file contains software structures used to form a two way
 * communication channel between the VF driver and the PF driver,
 * named Virtual Function Driver Interface (VFDI).
 *
 * For the purposes of VFDI, a page is a memory region with size and
 * alignment of 4K.  All addresses are DMA addresses to be used within
 * the domain of the relevant VF.
 *
 * The only hardware-defined channels for a VF driver to communicate
 * with the PF driver are the event mailboxes (%FR_CZ_USR_EV
 * registers).  Writing to these registers generates an event with
 * EV_CODE = EV_CODE_USR_EV, USER_QID set to the index of the mailbox
 * and USER_EV_REG_VALUE set to the value written.  The PF driver may
 * direct or disable delivery of these events by setting
 * %FR_CZ_USR_EV_CFG.
 *
 * The PF driver can send arbitrary events to arbitrary event queues.
 * However, for consistency, VFDI events from the PF are defined to
 * follow the same form and be sent to the first event queue assigned
 * to the VF while that queue is enabled by the VF driver.
 *
 * The general form of the variable bits of VFDI events is:
 *
 *       0             16                       24   31
 *      | DATA        | TYPE                   | SEQ   |
 *
 * SEQ is a sequence number which should be incremented by 1 (modulo
 * 256) for each event.  The sequence numbers used in each direction
 * are independent.
 *
 * The VF submits requests of type &struct vfdi_req by sending the
 * address of the request (ADDR) in a series of 4 events:
 *
 *       0             16                       24   31
 *      | ADDR[0:15]  | VFDI_EV_TYPE_REQ_WORD0 | SEQ   |
 *      | ADDR[16:31] | VFDI_EV_TYPE_REQ_WORD1 | SEQ+1 |
 *      | ADDR[32:47] | VFDI_EV_TYPE_REQ_WORD2 | SEQ+2 |
 *      | ADDR[48:63] | VFDI_EV_TYPE_REQ_WORD3 | SEQ+3 |
 *
 * The address must be page-aligned.  After receiving such a valid
 * series of events, the PF driver will attempt to read the request
 * and write a response to the same address.  In case of an invalid
 * sequence of events or a DMA error, there will be no response.
 *
 * The VF driver may request that the PF driver writes status
 * information into its domain asynchronously.  After writing the
 * status, the PF driver will send an event of the form:
 *
 *       0             16                       24   31
 *      | reserved    | VFDI_EV_TYPE_STATUS    | SEQ   |
 *
 * In case the VF must be reset for any reason, the PF driver will
 * send an event of the form:
 *
 *       0             16                       24   31
 *      | reserved    | VFDI_EV_TYPE_RESET     | SEQ   |
 *
 * It is then the responsibility of the VF driver to request
 * reinitialisation of its queues.
 */
#define VFDI_EV_SEQ_LBN 24
#define VFDI_EV_SEQ_WIDTH 8
#define VFDI_EV_TYPE_LBN 16
#define VFDI_EV_TYPE_WIDTH 8
#define VFDI_EV_TYPE_REQ_WORD0 0
#define VFDI_EV_TYPE_REQ_WORD1 1
#define VFDI_EV_TYPE_REQ_WORD2 2
#define VFDI_EV_TYPE_REQ_WORD3 3
#define VFDI_EV_TYPE_STATUS 4
#define VFDI_EV_TYPE_RESET 5
#define VFDI_EV_DATA_LBN 0
#define VFDI_EV_DATA_WIDTH 16

struct vfdi_endpoint {
	u8 mac_addr[ETH_ALEN];
	__be16 tci;
};

enum vfdi_op {
	VFDI_OP_RESPONSE = 0,
	VFDI_OP_INIT_EVQ = 1,
	VFDI_OP_INIT_RXQ = 2,
	VFDI_OP_INIT_TXQ = 3,
	VFDI_OP_FINI_ALL_QUEUES = 4,
	VFDI_OP_INSERT_FILTER = 5,
	VFDI_OP_REMOVE_ALL_FILTERS = 6,
	VFDI_OP_SET_STATUS_PAGE = 7,
	VFDI_OP_CLEAR_STATUS_PAGE = 8,
	VFDI_OP_LIMIT,
};

#define VFDI_RC_SUCCESS		0
#define VFDI_RC_ENOMEM		(-12)
#define VFDI_RC_EINVAL		(-22)
#define VFDI_RC_EOPNOTSUPP	(-95)
#define VFDI_RC_ETIMEDOUT	(-110)

struct vfdi_req {
	u32 op;
	u32 reserved1;
	s32 rc;
	u32 reserved2;
	union {
		struct {
			u32 index;
			u32 buf_count;
			u64 addr[];
		} init_evq;
		struct {
			u32 index;
			u32 buf_count;
			u32 evq;
			u32 label;
			u32 flags;
#define VFDI_RXQ_FLAG_SCATTER_EN 1
			u32 reserved;
			u64 addr[];
		} init_rxq;
		struct {
			u32 index;
			u32 buf_count;
			u32 evq;
			u32 label;
			u32 flags;
#define VFDI_TXQ_FLAG_IP_CSUM_DIS 1
#define VFDI_TXQ_FLAG_TCPUDP_CSUM_DIS 2
			u32 reserved;
			u64 addr[];
		} init_txq;
		struct {
			u32 rxq;
			u32 flags;
#define VFDI_MAC_FILTER_FLAG_RSS 1
#define VFDI_MAC_FILTER_FLAG_SCATTER 2
		} mac_filter;
		struct {
			u64 dma_addr;
			u64 peer_page_count;
			u64 peer_page_addr[];
		} set_status_page;
	} u;
};

/**
 * struct vfdi_status - Status provided by PF driver to VF driver
 * @generation_start: A generation count DMA'd to VF *before* the
 *	rest of the structure.
 * @generation_end: A generation count DMA'd to VF *after* the
 *	rest of the structure.
 * @version: Version of this structure; currently set to 1.  Later
 *	versions must either be layout-compatible or only be sent to VFs
 *	that specifically request them.
 * @length: Total length of this structure including embedded tables
 * @vi_scale: log2 the number of VIs available on this VF. This quantity
 *	is used by the hardware for register decoding.
 * @max_tx_channels: The maximum number of transmit queues the VF can use.
 * @rss_rxq_count: The number of receive queues present in the shared RSS
 *	indirection table.
 * @peer_count: Total number of peers in the complete peer list. If larger
 *	than ARRAY_SIZE(%peers), then the VF must provide sufficient
 *	additional pages each of which is filled with vfdi_endpoint structures.
 * @local: The MAC address and outer VLAN tag of *this* VF
 * @peers: Table of peer addresses.  The @tci fields in these structures
 *	are currently unused and must be ignored.  Additional peers are
 *	written into any additional pages provided by the VF.
 * @timer_quantum_ns: Timer quantum (nominal period between timer ticks)
 *	for interrupt moderation timers, in nanoseconds. This member is only
 *	present if @length is sufficiently large.
 */
struct vfdi_status {
	u32 generation_start;
	u32 generation_end;
	u32 version;
	u32 length;
	u8 vi_scale;
	u8 max_tx_channels;
	u8 rss_rxq_count;
	u8 reserved1;
	u16 peer_count;
	u16 reserved2;
	struct vfdi_endpoint local;
	struct vfdi_endpoint peers[256];

	
	u32 timer_quantum_ns;
};

#endif
