/*
 * sgiseeq.c: Seeq8003 ethernet driver for SGI machines.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */

#undef DEBUG

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>
#include <asm/sgi/seeq.h>

#include "sgiseeq.h"

static char *sgiseeqstr = "SGI Seeq8003";


#define SEEQ_RX_BUFFERS  16
#define SEEQ_TX_BUFFERS  16

#define PKT_BUF_SZ       1584

#define NEXT_RX(i)  (((i) + 1) & (SEEQ_RX_BUFFERS - 1))
#define NEXT_TX(i)  (((i) + 1) & (SEEQ_TX_BUFFERS - 1))
#define PREV_RX(i)  (((i) - 1) & (SEEQ_RX_BUFFERS - 1))
#define PREV_TX(i)  (((i) - 1) & (SEEQ_TX_BUFFERS - 1))

#define TX_BUFFS_AVAIL(sp) ((sp->tx_old <= sp->tx_new) ? \
			    sp->tx_old + (SEEQ_TX_BUFFERS - 1) - sp->tx_new : \
			    sp->tx_old - sp->tx_new - 1)

#define VIRT_TO_DMA(sp, v) ((sp)->srings_dma +                                 \
				  (dma_addr_t)((unsigned long)(v) -            \
					       (unsigned long)((sp)->rx_desc)))

static int rx_copybreak = 100;

#define PAD_SIZE    (128 - sizeof(struct hpc_dma_desc) - sizeof(void *))

struct sgiseeq_rx_desc {
	volatile struct hpc_dma_desc rdma;
	u8 padding[PAD_SIZE];
	struct sk_buff *skb;
};

struct sgiseeq_tx_desc {
	volatile struct hpc_dma_desc tdma;
	u8 padding[PAD_SIZE];
	struct sk_buff *skb;
};

struct sgiseeq_init_block { 
	struct sgiseeq_rx_desc rxvector[SEEQ_RX_BUFFERS];
	struct sgiseeq_tx_desc txvector[SEEQ_TX_BUFFERS];
};

struct sgiseeq_private {
	struct sgiseeq_init_block *srings;
	dma_addr_t srings_dma;

	
	struct sgiseeq_rx_desc *rx_desc;
	struct sgiseeq_tx_desc *tx_desc;

	char *name;
	struct hpc3_ethregs *hregs;
	struct sgiseeq_regs *sregs;

	
	unsigned int rx_new, tx_new;
	unsigned int rx_old, tx_old;

	int is_edlc;
	unsigned char control;
	unsigned char mode;

	spinlock_t tx_lock;
};

static inline void dma_sync_desc_cpu(struct net_device *dev, void *addr)
{
	dma_cache_sync(dev->dev.parent, addr, sizeof(struct sgiseeq_rx_desc),
		       DMA_FROM_DEVICE);
}

static inline void dma_sync_desc_dev(struct net_device *dev, void *addr)
{
	dma_cache_sync(dev->dev.parent, addr, sizeof(struct sgiseeq_rx_desc),
		       DMA_TO_DEVICE);
}

static inline void hpc3_eth_reset(struct hpc3_ethregs *hregs)
{
	hregs->reset = HPC3_ERST_CRESET | HPC3_ERST_CLRIRQ;
	udelay(20);
	hregs->reset = 0;
}

static inline void reset_hpc3_and_seeq(struct hpc3_ethregs *hregs,
				       struct sgiseeq_regs *sregs)
{
	hregs->rx_ctrl = hregs->tx_ctrl = 0;
	hpc3_eth_reset(hregs);
}

#define RSTAT_GO_BITS (SEEQ_RCMD_IGOOD | SEEQ_RCMD_IEOF | SEEQ_RCMD_ISHORT | \
		       SEEQ_RCMD_IDRIB | SEEQ_RCMD_ICRC)

static inline void seeq_go(struct sgiseeq_private *sp,
			   struct hpc3_ethregs *hregs,
			   struct sgiseeq_regs *sregs)
{
	sregs->rstat = sp->mode | RSTAT_GO_BITS;
	hregs->rx_ctrl = HPC3_ERXCTRL_ACTIVE;
}

static inline void __sgiseeq_set_mac_address(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	int i;

	sregs->tstat = SEEQ_TCMD_RB0;
	for (i = 0; i < 6; i++)
		sregs->rw.eth_addr[i] = dev->dev_addr[i];
}

static int sgiseeq_set_mac_address(struct net_device *dev, void *addr)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sockaddr *sa = addr;

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	spin_lock_irq(&sp->tx_lock);
	__sgiseeq_set_mac_address(dev);
	spin_unlock_irq(&sp->tx_lock);

	return 0;
}

#define TCNTINFO_INIT (HPCDMA_EOX | HPCDMA_ETXD)
#define RCNTCFG_INIT  (HPCDMA_OWN | HPCDMA_EORP | HPCDMA_XIE)
#define RCNTINFO_INIT (RCNTCFG_INIT | (PKT_BUF_SZ & HPCDMA_BCNT))

static int seeq_init_ring(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	int i;

	netif_stop_queue(dev);
	sp->rx_new = sp->tx_new = 0;
	sp->rx_old = sp->tx_old = 0;

	__sgiseeq_set_mac_address(dev);

	
	for(i = 0; i < SEEQ_TX_BUFFERS; i++) {
		sp->tx_desc[i].tdma.cntinfo = TCNTINFO_INIT;
		dma_sync_desc_dev(dev, &sp->tx_desc[i]);
	}

	
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		if (!sp->rx_desc[i].skb) {
			dma_addr_t dma_addr;
			struct sk_buff *skb = netdev_alloc_skb(dev, PKT_BUF_SZ);

			if (skb == NULL)
				return -ENOMEM;
			skb_reserve(skb, 2);
			dma_addr = dma_map_single(dev->dev.parent,
						  skb->data - 2,
						  PKT_BUF_SZ, DMA_FROM_DEVICE);
			sp->rx_desc[i].skb = skb;
			sp->rx_desc[i].rdma.pbuf = dma_addr;
		}
		sp->rx_desc[i].rdma.cntinfo = RCNTINFO_INIT;
		dma_sync_desc_dev(dev, &sp->rx_desc[i]);
	}
	sp->rx_desc[i - 1].rdma.cntinfo |= HPCDMA_EOR;
	dma_sync_desc_dev(dev, &sp->rx_desc[i - 1]);
	return 0;
}

static void seeq_purge_ring(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	int i;

	
	for (i = 0; i < SEEQ_TX_BUFFERS; i++) {
		if (sp->tx_desc[i].skb) {
			dev_kfree_skb(sp->tx_desc[i].skb);
			sp->tx_desc[i].skb = NULL;
		}
	}

	
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		if (sp->rx_desc[i].skb) {
			dev_kfree_skb(sp->rx_desc[i].skb);
			sp->rx_desc[i].skb = NULL;
		}
	}
}

#ifdef DEBUG
static struct sgiseeq_private *gpriv;
static struct net_device *gdev;

static void sgiseeq_dump_rings(void)
{
	static int once;
	struct sgiseeq_rx_desc *r = gpriv->rx_desc;
	struct sgiseeq_tx_desc *t = gpriv->tx_desc;
	struct hpc3_ethregs *hregs = gpriv->hregs;
	int i;

	if (once)
		return;
	once++;
	printk("RING DUMP:\n");
	for (i = 0; i < SEEQ_RX_BUFFERS; i++) {
		printk("RX [%d]: @(%p) [%08x,%08x,%08x] ",
		       i, (&r[i]), r[i].rdma.pbuf, r[i].rdma.cntinfo,
		       r[i].rdma.pnext);
		i += 1;
		printk("-- [%d]: @(%p) [%08x,%08x,%08x]\n",
		       i, (&r[i]), r[i].rdma.pbuf, r[i].rdma.cntinfo,
		       r[i].rdma.pnext);
	}
	for (i = 0; i < SEEQ_TX_BUFFERS; i++) {
		printk("TX [%d]: @(%p) [%08x,%08x,%08x] ",
		       i, (&t[i]), t[i].tdma.pbuf, t[i].tdma.cntinfo,
		       t[i].tdma.pnext);
		i += 1;
		printk("-- [%d]: @(%p) [%08x,%08x,%08x]\n",
		       i, (&t[i]), t[i].tdma.pbuf, t[i].tdma.cntinfo,
		       t[i].tdma.pnext);
	}
	printk("INFO: [rx_new = %d rx_old=%d] [tx_new = %d tx_old = %d]\n",
	       gpriv->rx_new, gpriv->rx_old, gpriv->tx_new, gpriv->tx_old);
	printk("RREGS: rx_cbptr[%08x] rx_ndptr[%08x] rx_ctrl[%08x]\n",
	       hregs->rx_cbptr, hregs->rx_ndptr, hregs->rx_ctrl);
	printk("TREGS: tx_cbptr[%08x] tx_ndptr[%08x] tx_ctrl[%08x]\n",
	       hregs->tx_cbptr, hregs->tx_ndptr, hregs->tx_ctrl);
}
#endif

#define TSTAT_INIT_SEEQ (SEEQ_TCMD_IPT|SEEQ_TCMD_I16|SEEQ_TCMD_IC|SEEQ_TCMD_IUF)
#define TSTAT_INIT_EDLC ((TSTAT_INIT_SEEQ) | SEEQ_TCMD_RB2)

static int init_seeq(struct net_device *dev, struct sgiseeq_private *sp,
		     struct sgiseeq_regs *sregs)
{
	struct hpc3_ethregs *hregs = sp->hregs;
	int err;

	reset_hpc3_and_seeq(hregs, sregs);
	err = seeq_init_ring(dev);
	if (err)
		return err;

	
	if (sp->is_edlc) {
		sregs->tstat = TSTAT_INIT_EDLC;
		sregs->rw.wregs.control = sp->control;
		sregs->rw.wregs.frame_gap = 0;
	} else {
		sregs->tstat = TSTAT_INIT_SEEQ;
	}

	hregs->rx_ndptr = VIRT_TO_DMA(sp, sp->rx_desc);
	hregs->tx_ndptr = VIRT_TO_DMA(sp, sp->tx_desc);

	seeq_go(sp, hregs, sregs);
	return 0;
}

static void record_rx_errors(struct net_device *dev, unsigned char status)
{
	if (status & SEEQ_RSTAT_OVERF ||
	    status & SEEQ_RSTAT_SFRAME)
		dev->stats.rx_over_errors++;
	if (status & SEEQ_RSTAT_CERROR)
		dev->stats.rx_crc_errors++;
	if (status & SEEQ_RSTAT_DERROR)
		dev->stats.rx_frame_errors++;
	if (status & SEEQ_RSTAT_REOF)
		dev->stats.rx_errors++;
}

static inline void rx_maybe_restart(struct sgiseeq_private *sp,
				    struct hpc3_ethregs *hregs,
				    struct sgiseeq_regs *sregs)
{
	if (!(hregs->rx_ctrl & HPC3_ERXCTRL_ACTIVE)) {
		hregs->rx_ndptr = VIRT_TO_DMA(sp, sp->rx_desc + sp->rx_new);
		seeq_go(sp, hregs, sregs);
	}
}

static inline void sgiseeq_rx(struct net_device *dev, struct sgiseeq_private *sp,
			      struct hpc3_ethregs *hregs,
			      struct sgiseeq_regs *sregs)
{
	struct sgiseeq_rx_desc *rd;
	struct sk_buff *skb = NULL;
	struct sk_buff *newskb;
	unsigned char pkt_status;
	int len = 0;
	unsigned int orig_end = PREV_RX(sp->rx_new);

	
	rd = &sp->rx_desc[sp->rx_new];
	dma_sync_desc_cpu(dev, rd);
	while (!(rd->rdma.cntinfo & HPCDMA_OWN)) {
		len = PKT_BUF_SZ - (rd->rdma.cntinfo & HPCDMA_BCNT) - 3;
		dma_unmap_single(dev->dev.parent, rd->rdma.pbuf,
				 PKT_BUF_SZ, DMA_FROM_DEVICE);
		pkt_status = rd->skb->data[len];
		if (pkt_status & SEEQ_RSTAT_FIG) {
			
			
			if (memcmp(rd->skb->data + 6, dev->dev_addr, ETH_ALEN)) {
				if (len > rx_copybreak) {
					skb = rd->skb;
					newskb = netdev_alloc_skb(dev, PKT_BUF_SZ);
					if (!newskb) {
						newskb = skb;
						skb = NULL;
						goto memory_squeeze;
					}
					skb_reserve(newskb, 2);
				} else {
					skb = netdev_alloc_skb_ip_align(dev, len);
					if (skb)
						skb_copy_to_linear_data(skb, rd->skb->data, len);

					newskb = rd->skb;
				}
memory_squeeze:
				if (skb) {
					skb_put(skb, len);
					skb->protocol = eth_type_trans(skb, dev);
					netif_rx(skb);
					dev->stats.rx_packets++;
					dev->stats.rx_bytes += len;
				} else {
					printk(KERN_NOTICE "%s: Memory squeeze, deferring packet.\n",
						dev->name);
					dev->stats.rx_dropped++;
				}
			} else {
				
				newskb = rd->skb;
			}
		} else {
			record_rx_errors(dev, pkt_status);
			newskb = rd->skb;
		}
		rd->skb = newskb;
		rd->rdma.pbuf = dma_map_single(dev->dev.parent,
					       newskb->data - 2,
					       PKT_BUF_SZ, DMA_FROM_DEVICE);

		
		rd->rdma.cntinfo = RCNTINFO_INIT;
		sp->rx_new = NEXT_RX(sp->rx_new);
		dma_sync_desc_dev(dev, rd);
		rd = &sp->rx_desc[sp->rx_new];
		dma_sync_desc_cpu(dev, rd);
	}
	dma_sync_desc_cpu(dev, &sp->rx_desc[orig_end]);
	sp->rx_desc[orig_end].rdma.cntinfo &= ~(HPCDMA_EOR);
	dma_sync_desc_dev(dev, &sp->rx_desc[orig_end]);
	dma_sync_desc_cpu(dev, &sp->rx_desc[PREV_RX(sp->rx_new)]);
	sp->rx_desc[PREV_RX(sp->rx_new)].rdma.cntinfo |= HPCDMA_EOR;
	dma_sync_desc_dev(dev, &sp->rx_desc[PREV_RX(sp->rx_new)]);
	rx_maybe_restart(sp, hregs, sregs);
}

static inline void tx_maybe_reset_collisions(struct sgiseeq_private *sp,
					     struct sgiseeq_regs *sregs)
{
	if (sp->is_edlc) {
		sregs->rw.wregs.control = sp->control & ~(SEEQ_CTRL_XCNT);
		sregs->rw.wregs.control = sp->control;
	}
}

static inline void kick_tx(struct net_device *dev,
			   struct sgiseeq_private *sp,
			   struct hpc3_ethregs *hregs)
{
	struct sgiseeq_tx_desc *td;
	int i = sp->tx_old;

	td = &sp->tx_desc[i];
	dma_sync_desc_cpu(dev, td);
	while ((td->tdma.cntinfo & (HPCDMA_XIU | HPCDMA_ETXD)) ==
	      (HPCDMA_XIU | HPCDMA_ETXD)) {
		i = NEXT_TX(i);
		td = &sp->tx_desc[i];
		dma_sync_desc_cpu(dev, td);
	}
	if (td->tdma.cntinfo & HPCDMA_XIU) {
		hregs->tx_ndptr = VIRT_TO_DMA(sp, td);
		hregs->tx_ctrl = HPC3_ETXCTRL_ACTIVE;
	}
}

static inline void sgiseeq_tx(struct net_device *dev, struct sgiseeq_private *sp,
			      struct hpc3_ethregs *hregs,
			      struct sgiseeq_regs *sregs)
{
	struct sgiseeq_tx_desc *td;
	unsigned long status = hregs->tx_ctrl;
	int j;

	tx_maybe_reset_collisions(sp, sregs);

	if (!(status & (HPC3_ETXCTRL_ACTIVE | SEEQ_TSTAT_PTRANS))) {
		
		if (status & SEEQ_TSTAT_R16)
			dev->stats.tx_aborted_errors++;
		if (status & SEEQ_TSTAT_UFLOW)
			dev->stats.tx_fifo_errors++;
		if (status & SEEQ_TSTAT_LCLS)
			dev->stats.collisions++;
	}

	
	for (j = sp->tx_old; j != sp->tx_new; j = NEXT_TX(j)) {
		td = &sp->tx_desc[j];

		dma_sync_desc_cpu(dev, td);
		if (!(td->tdma.cntinfo & (HPCDMA_XIU)))
			break;
		if (!(td->tdma.cntinfo & (HPCDMA_ETXD))) {
			if (!(status & HPC3_ETXCTRL_ACTIVE)) {
				hregs->tx_ndptr = VIRT_TO_DMA(sp, td);
				hregs->tx_ctrl = HPC3_ETXCTRL_ACTIVE;
			}
			break;
		}
		dev->stats.tx_packets++;
		sp->tx_old = NEXT_TX(sp->tx_old);
		td->tdma.cntinfo &= ~(HPCDMA_XIU | HPCDMA_XIE);
		td->tdma.cntinfo |= HPCDMA_EOX;
		if (td->skb) {
			dev_kfree_skb_any(td->skb);
			td->skb = NULL;
		}
		dma_sync_desc_dev(dev, td);
	}
}

static irqreturn_t sgiseeq_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct hpc3_ethregs *hregs = sp->hregs;
	struct sgiseeq_regs *sregs = sp->sregs;

	spin_lock(&sp->tx_lock);

	
	hregs->reset = HPC3_ERST_CLRIRQ;

	
	sgiseeq_rx(dev, sp, hregs, sregs);

	
	if (sp->tx_old != sp->tx_new)
		sgiseeq_tx(dev, sp, hregs, sregs);

	if ((TX_BUFFS_AVAIL(sp) > 0) && netif_queue_stopped(dev)) {
		netif_wake_queue(dev);
	}
	spin_unlock(&sp->tx_lock);

	return IRQ_HANDLED;
}

static int sgiseeq_open(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	unsigned int irq = dev->irq;
	int err;

	if (request_irq(irq, sgiseeq_interrupt, 0, sgiseeqstr, dev)) {
		printk(KERN_ERR "Seeq8003: Can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}

	err = init_seeq(dev, sp, sregs);
	if (err)
		goto out_free_irq;

	netif_start_queue(dev);

	return 0;

out_free_irq:
	free_irq(irq, dev);

	return err;
}

static int sgiseeq_close(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	unsigned int irq = dev->irq;

	netif_stop_queue(dev);

	
	reset_hpc3_and_seeq(sp->hregs, sregs);
	free_irq(irq, dev);
	seeq_purge_ring(dev);

	return 0;
}

static inline int sgiseeq_reset(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct sgiseeq_regs *sregs = sp->sregs;
	int err;

	err = init_seeq(dev, sp, sregs);
	if (err)
		return err;

	dev->trans_start = jiffies; 
	netif_wake_queue(dev);

	return 0;
}

static int sgiseeq_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	struct hpc3_ethregs *hregs = sp->hregs;
	unsigned long flags;
	struct sgiseeq_tx_desc *td;
	int len, entry;

	spin_lock_irqsave(&sp->tx_lock, flags);

	
	len = skb->len;
	if (len < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN)) {
			spin_unlock_irqrestore(&sp->tx_lock, flags);
			return NETDEV_TX_OK;
		}
		len = ETH_ZLEN;
	}

	dev->stats.tx_bytes += len;
	entry = sp->tx_new;
	td = &sp->tx_desc[entry];
	dma_sync_desc_cpu(dev, td);

	td->skb = skb;
	td->tdma.pbuf = dma_map_single(dev->dev.parent, skb->data,
				       len, DMA_TO_DEVICE);
	td->tdma.cntinfo = (len & HPCDMA_BCNT) |
	                   HPCDMA_XIU | HPCDMA_EOXP | HPCDMA_XIE | HPCDMA_EOX;
	dma_sync_desc_dev(dev, td);
	if (sp->tx_old != sp->tx_new) {
		struct sgiseeq_tx_desc *backend;

		backend = &sp->tx_desc[PREV_TX(sp->tx_new)];
		dma_sync_desc_cpu(dev, backend);
		backend->tdma.cntinfo &= ~HPCDMA_EOX;
		dma_sync_desc_dev(dev, backend);
	}
	sp->tx_new = NEXT_TX(sp->tx_new); 

	
	if (!(hregs->tx_ctrl & HPC3_ETXCTRL_ACTIVE))
		kick_tx(dev, sp, hregs);

	if (!TX_BUFFS_AVAIL(sp))
		netif_stop_queue(dev);
	spin_unlock_irqrestore(&sp->tx_lock, flags);

	return NETDEV_TX_OK;
}

static void timeout(struct net_device *dev)
{
	printk(KERN_NOTICE "%s: transmit timed out, resetting\n", dev->name);
	sgiseeq_reset(dev);

	dev->trans_start = jiffies; 
	netif_wake_queue(dev);
}

static void sgiseeq_set_multicast(struct net_device *dev)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	unsigned char oldmode = sp->mode;

	if(dev->flags & IFF_PROMISC)
		sp->mode = SEEQ_RCMD_RANY;
	else if ((dev->flags & IFF_ALLMULTI) || !netdev_mc_empty(dev))
		sp->mode = SEEQ_RCMD_RBMCAST;
	else
		sp->mode = SEEQ_RCMD_RBCAST;


	if (oldmode != sp->mode)
		sgiseeq_reset(dev);
}

static inline void setup_tx_ring(struct net_device *dev,
				 struct sgiseeq_tx_desc *buf,
				 int nbufs)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	int i = 0;

	while (i < (nbufs - 1)) {
		buf[i].tdma.pnext = VIRT_TO_DMA(sp, buf + i + 1);
		buf[i].tdma.pbuf = 0;
		dma_sync_desc_dev(dev, &buf[i]);
		i++;
	}
	buf[i].tdma.pnext = VIRT_TO_DMA(sp, buf);
	dma_sync_desc_dev(dev, &buf[i]);
}

static inline void setup_rx_ring(struct net_device *dev,
				 struct sgiseeq_rx_desc *buf,
				 int nbufs)
{
	struct sgiseeq_private *sp = netdev_priv(dev);
	int i = 0;

	while (i < (nbufs - 1)) {
		buf[i].rdma.pnext = VIRT_TO_DMA(sp, buf + i + 1);
		buf[i].rdma.pbuf = 0;
		dma_sync_desc_dev(dev, &buf[i]);
		i++;
	}
	buf[i].rdma.pbuf = 0;
	buf[i].rdma.pnext = VIRT_TO_DMA(sp, buf);
	dma_sync_desc_dev(dev, &buf[i]);
}

static const struct net_device_ops sgiseeq_netdev_ops = {
	.ndo_open		= sgiseeq_open,
	.ndo_stop		= sgiseeq_close,
	.ndo_start_xmit		= sgiseeq_start_xmit,
	.ndo_tx_timeout		= timeout,
	.ndo_set_rx_mode	= sgiseeq_set_multicast,
	.ndo_set_mac_address	= sgiseeq_set_mac_address,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static int __devinit sgiseeq_probe(struct platform_device *pdev)
{
	struct sgiseeq_platform_data *pd = pdev->dev.platform_data;
	struct hpc3_regs *hpcregs = pd->hpc;
	struct sgiseeq_init_block *sr;
	unsigned int irq = pd->irq;
	struct sgiseeq_private *sp;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof (struct sgiseeq_private));
	if (!dev) {
		err = -ENOMEM;
		goto err_out;
	}

	platform_set_drvdata(pdev, dev);
	sp = netdev_priv(dev);

	
	sr = dma_alloc_noncoherent(&pdev->dev, sizeof(*sp->srings),
				&sp->srings_dma, GFP_KERNEL);
	if (!sr) {
		printk(KERN_ERR "Sgiseeq: Page alloc failed, aborting.\n");
		err = -ENOMEM;
		goto err_out_free_dev;
	}
	sp->srings = sr;
	sp->rx_desc = sp->srings->rxvector;
	sp->tx_desc = sp->srings->txvector;

	
	setup_rx_ring(dev, sp->rx_desc, SEEQ_RX_BUFFERS);
	setup_tx_ring(dev, sp->tx_desc, SEEQ_TX_BUFFERS);

	memcpy(dev->dev_addr, pd->mac, ETH_ALEN);

#ifdef DEBUG
	gpriv = sp;
	gdev = dev;
#endif
	sp->sregs = (struct sgiseeq_regs *) &hpcregs->eth_ext[0];
	sp->hregs = &hpcregs->ethregs;
	sp->name = sgiseeqstr;
	sp->mode = SEEQ_RCMD_RBCAST;

	
	sp->hregs->pconfig = 0x161;
	sp->hregs->dconfig = HPC3_EDCFG_FIRQ | HPC3_EDCFG_FEOP |
			     HPC3_EDCFG_FRXDC | HPC3_EDCFG_PTO | 0x026;

	
	sp->hregs->pconfig = 0x161;
	sp->hregs->dconfig = HPC3_EDCFG_FIRQ | HPC3_EDCFG_FEOP |
			     HPC3_EDCFG_FRXDC | HPC3_EDCFG_PTO | 0x026;

	
	hpc3_eth_reset(sp->hregs);

	sp->is_edlc = !(sp->sregs->rw.rregs.collision_tx[0] & 0xff);
	if (sp->is_edlc)
		sp->control = SEEQ_CTRL_XCNT | SEEQ_CTRL_ACCNT |
			      SEEQ_CTRL_SFLAG | SEEQ_CTRL_ESHORT |
			      SEEQ_CTRL_ENCARR;

	dev->netdev_ops		= &sgiseeq_netdev_ops;
	dev->watchdog_timeo	= (200 * HZ) / 1000;
	dev->irq		= irq;

	if (register_netdev(dev)) {
		printk(KERN_ERR "Sgiseeq: Cannot register net device, "
		       "aborting.\n");
		err = -ENODEV;
		goto err_out_free_page;
	}

	printk(KERN_INFO "%s: %s %pM\n", dev->name, sgiseeqstr, dev->dev_addr);

	return 0;

err_out_free_page:
	free_page((unsigned long) sp->srings);
err_out_free_dev:
	free_netdev(dev);

err_out:
	return err;
}

static int __exit sgiseeq_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct sgiseeq_private *sp = netdev_priv(dev);

	unregister_netdev(dev);
	dma_free_noncoherent(&pdev->dev, sizeof(*sp->srings), sp->srings,
			     sp->srings_dma);
	free_netdev(dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver sgiseeq_driver = {
	.probe	= sgiseeq_probe,
	.remove	= __exit_p(sgiseeq_remove),
	.driver = {
		.name	= "sgiseeq",
		.owner	= THIS_MODULE,
	}
};

module_platform_driver(sgiseeq_driver);

MODULE_DESCRIPTION("SGI Seeq 8003 driver");
MODULE_AUTHOR("Linux/MIPS Mailing List <linux-mips@linux-mips.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sgiseeq");
