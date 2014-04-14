/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2011 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */


#ifndef __BNA_HW_DEFS_H__
#define __BNA_HW_DEFS_H__

#include "bfi_reg.h"

#define BFI_ENET_DEF_TXQ		1
#define BFI_ENET_DEF_RXP		1
#define BFI_ENET_DEF_UCAM		1
#define BFI_ENET_DEF_RITSZ		1

#define BFI_ENET_MAX_MCAM		256

#define BFI_INVALID_RID			-1

#define BFI_IBIDX_SIZE			4

#define BFI_VLAN_WORD_SHIFT		5	
#define BFI_VLAN_WORD_MASK		0x1F
#define BFI_VLAN_BLOCK_SHIFT		9	
#define BFI_VLAN_BMASK_ALL		0xFF

#define BFI_COALESCING_TIMER_UNIT	5	
#define BFI_MAX_COALESCING_TIMEO	0xFF	
#define BFI_MAX_INTERPKT_COUNT		0xFF
#define BFI_MAX_INTERPKT_TIMEO		0xF	
#define BFI_TX_COALESCING_TIMEO		20	
#define BFI_TX_INTERPKT_COUNT		32
#define	BFI_RX_COALESCING_TIMEO		12	
#define	BFI_RX_INTERPKT_COUNT		6	
#define	BFI_RX_INTERPKT_TIMEO		3	

#define BFI_TXQ_WI_SIZE			64	
#define BFI_RXQ_WI_SIZE			8	
#define BFI_CQ_WI_SIZE			16	
#define BFI_TX_MAX_WRR_QUOTA		0xFFF

#define BFI_TX_MAX_VECTORS_PER_WI	4
#define BFI_TX_MAX_VECTORS_PER_PKT	0xFF
#define BFI_TX_MAX_DATA_PER_VECTOR	0xFFFF
#define BFI_TX_MAX_DATA_PER_PKT		0xFFFFFF

#define BFI_SMALL_RXBUF_SIZE		128

#define BFI_TX_MAX_PRIO			8
#define BFI_TX_PRIO_MAP_ALL		0xFF


#define BNA_PCI_REG_CT_ADDRSZ		(0x40000)

#define ct_reg_addr_init(_bna, _pcidev)					\
{									\
	struct bna_reg_offset reg_offset[] =				\
	{{HOSTFN0_INT_STATUS, HOSTFN0_INT_MSK},				\
	 {HOSTFN1_INT_STATUS, HOSTFN1_INT_MSK},				\
	 {HOSTFN2_INT_STATUS, HOSTFN2_INT_MSK},				\
	 {HOSTFN3_INT_STATUS, HOSTFN3_INT_MSK} };			\
									\
	(_bna)->regs.fn_int_status = (_pcidev)->pci_bar_kva +		\
				reg_offset[(_pcidev)->pci_func].fn_int_status;\
	(_bna)->regs.fn_int_mask = (_pcidev)->pci_bar_kva +		\
				reg_offset[(_pcidev)->pci_func].fn_int_mask;\
}

#define ct_bit_defn_init(_bna, _pcidev)					\
{									\
	(_bna)->bits.mbox_status_bits = (__HFN_INT_MBOX_LPU0 |		\
					__HFN_INT_MBOX_LPU1);		\
	(_bna)->bits.mbox_mask_bits = (__HFN_INT_MBOX_LPU0 |		\
					__HFN_INT_MBOX_LPU1);		\
	(_bna)->bits.error_status_bits = (__HFN_INT_ERR_MASK);		\
	(_bna)->bits.error_mask_bits = (__HFN_INT_ERR_MASK);		\
	(_bna)->bits.halt_status_bits = __HFN_INT_LL_HALT;		\
	(_bna)->bits.halt_mask_bits = __HFN_INT_LL_HALT;		\
}

#define ct2_reg_addr_init(_bna, _pcidev)				\
{									\
	(_bna)->regs.fn_int_status = (_pcidev)->pci_bar_kva +		\
				CT2_HOSTFN_INT_STATUS;			\
	(_bna)->regs.fn_int_mask = (_pcidev)->pci_bar_kva +		\
				CT2_HOSTFN_INTR_MASK;			\
}

#define ct2_bit_defn_init(_bna, _pcidev)				\
{									\
	(_bna)->bits.mbox_status_bits = (__HFN_INT_MBOX_LPU0_CT2 |	\
					__HFN_INT_MBOX_LPU1_CT2);	\
	(_bna)->bits.mbox_mask_bits = (__HFN_INT_MBOX_LPU0_CT2 |	\
					__HFN_INT_MBOX_LPU1_CT2);	\
	(_bna)->bits.error_status_bits = (__HFN_INT_ERR_MASK_CT2);	\
	(_bna)->bits.error_mask_bits = (__HFN_INT_ERR_MASK_CT2);	\
	(_bna)->bits.halt_status_bits = __HFN_INT_CPQ_HALT_CT2;		\
	(_bna)->bits.halt_mask_bits = __HFN_INT_CPQ_HALT_CT2;		\
}

#define bna_reg_addr_init(_bna, _pcidev)				\
{									\
	switch ((_pcidev)->device_id) {					\
	case PCI_DEVICE_ID_BROCADE_CT:					\
		ct_reg_addr_init((_bna), (_pcidev));			\
		ct_bit_defn_init((_bna), (_pcidev));			\
		break;							\
	case BFA_PCI_DEVICE_ID_CT2:					\
		ct2_reg_addr_init((_bna), (_pcidev));			\
		ct2_bit_defn_init((_bna), (_pcidev));			\
		break;							\
	}								\
}

#define bna_port_id_get(_bna) ((_bna)->ioceth.ioc.port_id)

#define IB_STATUS_BITS		0x0000ffff

#define BNA_IS_MBOX_INTR(_bna, _intr_status)				\
	((_intr_status) & (_bna)->bits.mbox_status_bits)

#define BNA_IS_HALT_INTR(_bna, _intr_status)				\
	((_intr_status) & (_bna)->bits.halt_status_bits)

#define BNA_IS_ERR_INTR(_bna, _intr_status)	\
	((_intr_status) & (_bna)->bits.error_status_bits)

#define BNA_IS_MBOX_ERR_INTR(_bna, _intr_status)	\
	(BNA_IS_MBOX_INTR(_bna, _intr_status) |		\
	BNA_IS_ERR_INTR(_bna, _intr_status))

#define BNA_IS_INTX_DATA_INTR(_intr_status)		\
		((_intr_status) & IB_STATUS_BITS)

#define bna_halt_clear(_bna)						\
do {									\
	u32 init_halt;						\
	init_halt = readl((_bna)->ioceth.ioc.ioc_regs.ll_halt);	\
	init_halt &= ~__FW_INIT_HALT_P;					\
	writel(init_halt, (_bna)->ioceth.ioc.ioc_regs.ll_halt);	\
	init_halt = readl((_bna)->ioceth.ioc.ioc_regs.ll_halt);	\
} while (0)

#define bna_intx_disable(_bna, _cur_mask)				\
{									\
	(_cur_mask) = readl((_bna)->regs.fn_int_mask);		\
	writel(0xffffffff, (_bna)->regs.fn_int_mask);		\
}

#define bna_intx_enable(bna, new_mask)					\
	writel((new_mask), (bna)->regs.fn_int_mask)
#define bna_mbox_intr_disable(bna)					\
do {									\
	u32 mask;							\
	mask = readl((bna)->regs.fn_int_mask);				\
	writel((mask | (bna)->bits.mbox_mask_bits |			\
		(bna)->bits.error_mask_bits), (bna)->regs.fn_int_mask); \
	mask = readl((bna)->regs.fn_int_mask);				\
} while (0)

#define bna_mbox_intr_enable(bna)					\
do {									\
	u32 mask;							\
	mask = readl((bna)->regs.fn_int_mask);				\
	writel((mask & ~((bna)->bits.mbox_mask_bits |			\
		(bna)->bits.error_mask_bits)), (bna)->regs.fn_int_mask);\
	mask = readl((bna)->regs.fn_int_mask);				\
} while (0)

#define bna_intr_status_get(_bna, _status)				\
{									\
	(_status) = readl((_bna)->regs.fn_int_status);			\
	if (_status) {							\
		writel(((_status) & ~(_bna)->bits.mbox_status_bits),	\
			(_bna)->regs.fn_int_status);			\
	}								\
}

#define	BNA_IB_MAX_ACK_EVENTS		(1 << 15)

#define BNA_DOORBELL_Q_PRD_IDX(_pi)	(0x80000000 | (_pi))
#define BNA_DOORBELL_Q_STOP		(0x40000000)

#define BNA_DOORBELL_IB_INT_ACK(_timeout, _events)			\
	(0x80000000 | ((_timeout) << 16) | (_events))
#define BNA_DOORBELL_IB_INT_DISABLE	(0x40000000)

#define bna_ib_coalescing_timer_set(_i_dbell, _cls_timer)		\
	((_i_dbell)->doorbell_ack = BNA_DOORBELL_IB_INT_ACK((_cls_timer), 0));

#define bna_ib_ack_disable_irq(_i_dbell, _events)			\
	(writel(BNA_DOORBELL_IB_INT_ACK(0, (_events)), \
		(_i_dbell)->doorbell_addr));

#define bna_ib_ack(_i_dbell, _events)					\
	(writel(((_i_dbell)->doorbell_ack | (_events)), \
		(_i_dbell)->doorbell_addr));

#define bna_ib_start(_bna, _ib, _is_regular)				\
{									\
	u32 intx_mask;						\
	struct bna_ib *ib = _ib;					\
	if ((ib->intr_type == BNA_INTR_T_INTX)) {			\
		bna_intx_disable((_bna), intx_mask);			\
		intx_mask &= ~(ib->intr_vector);			\
		bna_intx_enable((_bna), intx_mask);			\
	}								\
	bna_ib_coalescing_timer_set(&ib->door_bell,			\
			ib->coalescing_timeo);				\
	if (_is_regular)						\
		bna_ib_ack(&ib->door_bell, 0);				\
}

#define bna_ib_stop(_bna, _ib)						\
{									\
	u32 intx_mask;						\
	struct bna_ib *ib = _ib;					\
	writel(BNA_DOORBELL_IB_INT_DISABLE,				\
		ib->door_bell.doorbell_addr);				\
	if (ib->intr_type == BNA_INTR_T_INTX) {				\
		bna_intx_disable((_bna), intx_mask);			\
		intx_mask |= ib->intr_vector;				\
		bna_intx_enable((_bna), intx_mask);			\
	}								\
}

#define bna_txq_prod_indx_doorbell(_tcb)				\
	(writel(BNA_DOORBELL_Q_PRD_IDX((_tcb)->producer_index), \
		(_tcb)->q_dbell));

#define bna_rxq_prod_indx_doorbell(_rcb)				\
	(writel(BNA_DOORBELL_Q_PRD_IDX((_rcb)->producer_index), \
		(_rcb)->q_dbell));


#define BNA_TXQ_WI_SEND			(0x402)	
#define BNA_TXQ_WI_SEND_LSO		(0x403)	
#define BNA_TXQ_WI_EXTENSION		(0x104)	

#define BNA_TXQ_WI_CF_FCOE_CRC		(1 << 8)
#define BNA_TXQ_WI_CF_IPID_MODE		(1 << 5)
#define BNA_TXQ_WI_CF_INS_PRIO		(1 << 4)
#define BNA_TXQ_WI_CF_INS_VLAN		(1 << 3)
#define BNA_TXQ_WI_CF_UDP_CKSUM		(1 << 2)
#define BNA_TXQ_WI_CF_TCP_CKSUM		(1 << 1)
#define BNA_TXQ_WI_CF_IP_CKSUM		(1 << 0)

#define BNA_TXQ_WI_L4_HDR_N_OFFSET(_hdr_size, _offset) \
		(((_hdr_size) << 10) | ((_offset) & 0x3FF))

#define	BNA_CQ_EF_MAC_ERROR	(1 <<  0)
#define	BNA_CQ_EF_FCS_ERROR	(1 <<  1)
#define	BNA_CQ_EF_TOO_LONG	(1 <<  2)
#define	BNA_CQ_EF_FC_CRC_OK	(1 <<  3)

#define	BNA_CQ_EF_RSVD1		(1 <<  4)
#define	BNA_CQ_EF_L4_CKSUM_OK	(1 <<  5)
#define	BNA_CQ_EF_L3_CKSUM_OK	(1 <<  6)
#define	BNA_CQ_EF_HDS_HEADER	(1 <<  7)

#define	BNA_CQ_EF_UDP		(1 <<  8)
#define	BNA_CQ_EF_TCP		(1 <<  9)
#define	BNA_CQ_EF_IP_OPTIONS	(1 << 10)
#define	BNA_CQ_EF_IPV6		(1 << 11)

#define	BNA_CQ_EF_IPV4		(1 << 12)
#define	BNA_CQ_EF_VLAN		(1 << 13)
#define	BNA_CQ_EF_RSS		(1 << 14)
#define	BNA_CQ_EF_RSVD2		(1 << 15)

#define	BNA_CQ_EF_MCAST_MATCH   (1 << 16)
#define	BNA_CQ_EF_MCAST		(1 << 17)
#define BNA_CQ_EF_BCAST		(1 << 18)
#define	BNA_CQ_EF_REMOTE	(1 << 19)

#define	BNA_CQ_EF_LOCAL		(1 << 20)


struct bna_reg_offset {
	u32 fn_int_status;
	u32 fn_int_mask;
};

struct bna_bit_defn {
	u32 mbox_status_bits;
	u32 mbox_mask_bits;
	u32 error_status_bits;
	u32 error_mask_bits;
	u32 halt_status_bits;
	u32 halt_mask_bits;
};

struct bna_reg {
	void __iomem *fn_int_status;
	void __iomem *fn_int_mask;
};

struct bna_dma_addr {
	u32		msb;
	u32		lsb;
};

struct bna_txq_wi_vector {
	u16		reserved;
	u16		length;		
	struct bna_dma_addr host_addr; 
};

struct bna_txq_entry {
	union {
		struct {
			u8 reserved;
			u8 num_vectors;	
			u16 opcode; 
						    
						    
			u16 flags; 
			u16 l4_hdr_size_n_offset;
			u16 vlan_tag;
			u16 lso_mss;	
			u32 frame_length;	
		} wi;

		struct {
			u16 reserved;
			u16 opcode; 
						    
			u32 reserved2[3];	
						
		} wi_ext;
	} hdr;
	struct bna_txq_wi_vector vector[4];
};

struct bna_rxq_entry {		
	struct bna_dma_addr host_addr; 
};

struct bna_cq_entry {
	u32 flags;
	u16 vlan_tag;
	u16 length;
	u32 rss_hash;
	u8 valid;
	u8 reserved1;
	u8 reserved2;
	u8 rxq_id;
};

#endif 
