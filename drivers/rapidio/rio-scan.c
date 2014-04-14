/*
 * RapidIO enumeration and discovery support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Alex Bounine <alexandre.bounine@idt.com>
 * - Added Port-Write/Error Management initialization and handling
 *
 * Copyright 2009 Sysgo AG
 * Thomas Moll <thomas.moll@sysgo.com>
 * - Added Input- Output- enable functionality, to allow full communication
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/rio_regs.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/slab.h>

#include "rio.h"

LIST_HEAD(rio_devices);
static LIST_HEAD(rio_switches);

static void rio_enum_timeout(unsigned long);

static void rio_init_em(struct rio_dev *rdev);

DEFINE_SPINLOCK(rio_global_list_lock);

static int next_destid = 0;
static int next_net = 0;
static int next_comptag = 1;

static struct timer_list rio_enum_timer =
TIMER_INITIALIZER(rio_enum_timeout, 0, 0);

static int rio_mport_phys_table[] = {
	RIO_EFB_PAR_EP_ID,
	RIO_EFB_PAR_EP_REC_ID,
	RIO_EFB_SER_EP_ID,
	RIO_EFB_SER_EP_REC_ID,
	-1,
};

static u16 rio_get_device_id(struct rio_mport *port, u16 destid, u8 hopcount)
{
	u32 result;

	rio_mport_read_config_32(port, destid, hopcount, RIO_DID_CSR, &result);

	return RIO_GET_DID(port->sys_size, result);
}

/**
 * rio_set_device_id - Set the base/extended device id for a device
 * @port: RIO master port
 * @destid: Destination ID of device
 * @hopcount: Hopcount to device
 * @did: Device ID value to be written
 *
 * Writes the base/extended device id from a device.
 */
static void rio_set_device_id(struct rio_mport *port, u16 destid, u8 hopcount, u16 did)
{
	rio_mport_write_config_32(port, destid, hopcount, RIO_DID_CSR,
				  RIO_SET_DID(port->sys_size, did));
}

/**
 * rio_local_set_device_id - Set the base/extended device id for a port
 * @port: RIO master port
 * @did: Device ID value to be written
 *
 * Writes the base/extended device id from a device.
 */
static void rio_local_set_device_id(struct rio_mport *port, u16 did)
{
	rio_local_write_config_32(port, RIO_DID_CSR, RIO_SET_DID(port->sys_size,
				did));
}

static int rio_clear_locks(struct rio_mport *port)
{
	struct rio_dev *rdev;
	u32 result;
	int ret = 0;

	
	rio_local_write_config_32(port, RIO_HOST_DID_LOCK_CSR,
				  port->host_deviceid);
	rio_local_read_config_32(port, RIO_HOST_DID_LOCK_CSR, &result);
	if ((result & 0xffff) != 0xffff) {
		printk(KERN_INFO
		       "RIO: badness when releasing host lock on master port, result %8.8x\n",
		       result);
		ret = -EINVAL;
	}
	list_for_each_entry(rdev, &rio_devices, global_list) {
		rio_write_config_32(rdev, RIO_HOST_DID_LOCK_CSR,
				    port->host_deviceid);
		rio_read_config_32(rdev, RIO_HOST_DID_LOCK_CSR, &result);
		if ((result & 0xffff) != 0xffff) {
			printk(KERN_INFO
			       "RIO: badness when releasing host lock on vid %4.4x did %4.4x\n",
			       rdev->vid, rdev->did);
			ret = -EINVAL;
		}

		
		rio_read_config_32(rdev,
				   rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				   &result);
		result |= RIO_PORT_GEN_DISCOVERED | RIO_PORT_GEN_MASTER;
		rio_write_config_32(rdev,
				    rdev->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				    result);
	}

	return ret;
}

static int rio_enum_host(struct rio_mport *port)
{
	u32 result;

	
	rio_local_write_config_32(port, RIO_HOST_DID_LOCK_CSR,
				  port->host_deviceid);

	rio_local_read_config_32(port, RIO_HOST_DID_LOCK_CSR, &result);
	if ((result & 0xffff) != port->host_deviceid)
		return -1;

	
	rio_local_set_device_id(port, port->host_deviceid);

	if (next_destid == port->host_deviceid)
		next_destid++;

	return 0;
}

static int rio_device_has_destid(struct rio_mport *port, int src_ops,
				 int dst_ops)
{
	u32 mask = RIO_OPS_READ | RIO_OPS_WRITE | RIO_OPS_ATOMIC_TST_SWP | RIO_OPS_ATOMIC_INC | RIO_OPS_ATOMIC_DEC | RIO_OPS_ATOMIC_SET | RIO_OPS_ATOMIC_CLR;

	return !!((src_ops | dst_ops) & mask);
}

static void rio_release_dev(struct device *dev)
{
	struct rio_dev *rdev;

	rdev = to_rio_dev(dev);
	kfree(rdev);
}

static int rio_is_switch(struct rio_dev *rdev)
{
	if (rdev->pef & RIO_PEF_SWITCH)
		return 1;
	return 0;
}

static void rio_switch_init(struct rio_dev *rdev, int do_enum)
{
	struct rio_switch_ops *cur = __start_rio_switch_ops;
	struct rio_switch_ops *end = __end_rio_switch_ops;

	while (cur < end) {
		if ((cur->vid == rdev->vid) && (cur->did == rdev->did)) {
			pr_debug("RIO: calling init routine for %s\n",
				 rio_name(rdev));
			cur->init_hook(rdev, do_enum);
			break;
		}
		cur++;
	}

	if ((cur >= end) && (rdev->pef & RIO_PEF_STD_RT)) {
		pr_debug("RIO: adding STD routing ops for %s\n",
			rio_name(rdev));
		rdev->rswitch->add_entry = rio_std_route_add_entry;
		rdev->rswitch->get_entry = rio_std_route_get_entry;
		rdev->rswitch->clr_table = rio_std_route_clr_table;
	}

	if (!rdev->rswitch->add_entry || !rdev->rswitch->get_entry)
		printk(KERN_ERR "RIO: missing routing ops for %s\n",
		       rio_name(rdev));
}

static int __devinit rio_add_device(struct rio_dev *rdev)
{
	int err;

	err = device_add(&rdev->dev);
	if (err)
		return err;

	spin_lock(&rio_global_list_lock);
	list_add_tail(&rdev->global_list, &rio_devices);
	spin_unlock(&rio_global_list_lock);

	rio_create_sysfs_dev_files(rdev);

	return 0;
}

inline int rio_enable_rx_tx_port(struct rio_mport *port,
				 int local, u16 destid,
				 u8 hopcount, u8 port_num) {
#ifdef CONFIG_RAPIDIO_ENABLE_RX_TX_PORTS
	u32 regval;
	u32 ext_ftr_ptr;

	pr_debug("rio_enable_rx_tx_port(local = %d, destid = %d, hopcount = "
		 "%d, port_num = %d)\n", local, destid, hopcount, port_num);

	ext_ftr_ptr = rio_mport_get_physefb(port, local, destid, hopcount);

	if (local) {
		rio_local_read_config_32(port, ext_ftr_ptr +
				RIO_PORT_N_CTL_CSR(0),
				&regval);
	} else {
		if (rio_mport_read_config_32(port, destid, hopcount,
		ext_ftr_ptr + RIO_PORT_N_CTL_CSR(port_num), &regval) < 0)
			return -EIO;
	}

	if (regval & RIO_PORT_N_CTL_P_TYP_SER) {
		
		regval = regval | RIO_PORT_N_CTL_EN_RX_SER
				| RIO_PORT_N_CTL_EN_TX_SER;
	} else {
		
		regval = regval | RIO_PORT_N_CTL_EN_RX_PAR
				| RIO_PORT_N_CTL_EN_TX_PAR;
	}

	if (local) {
		rio_local_write_config_32(port, ext_ftr_ptr +
					  RIO_PORT_N_CTL_CSR(0), regval);
	} else {
		if (rio_mport_write_config_32(port, destid, hopcount,
		    ext_ftr_ptr + RIO_PORT_N_CTL_CSR(port_num), regval) < 0)
			return -EIO;
	}
#endif
	return 0;
}

static struct rio_dev __devinit *rio_setup_device(struct rio_net *net,
					struct rio_mport *port, u16 destid,
					u8 hopcount, int do_enum)
{
	int ret = 0;
	struct rio_dev *rdev;
	struct rio_switch *rswitch = NULL;
	int result, rdid;
	size_t size;
	u32 swpinfo = 0;

	size = sizeof(struct rio_dev);
	if (rio_mport_read_config_32(port, destid, hopcount,
				     RIO_PEF_CAR, &result))
		return NULL;

	if (result & (RIO_PEF_SWITCH | RIO_PEF_MULTIPORT)) {
		rio_mport_read_config_32(port, destid, hopcount,
					 RIO_SWP_INFO_CAR, &swpinfo);
		if (result & RIO_PEF_SWITCH) {
			size += (RIO_GET_TOTAL_PORTS(swpinfo) *
				sizeof(rswitch->nextdev[0])) + sizeof(*rswitch);
		}
	}

	rdev = kzalloc(size, GFP_KERNEL);
	if (!rdev)
		return NULL;

	rdev->net = net;
	rdev->pef = result;
	rdev->swpinfo = swpinfo;
	rio_mport_read_config_32(port, destid, hopcount, RIO_DEV_ID_CAR,
				 &result);
	rdev->did = result >> 16;
	rdev->vid = result & 0xffff;
	rio_mport_read_config_32(port, destid, hopcount, RIO_DEV_INFO_CAR,
				 &rdev->device_rev);
	rio_mport_read_config_32(port, destid, hopcount, RIO_ASM_ID_CAR,
				 &result);
	rdev->asm_did = result >> 16;
	rdev->asm_vid = result & 0xffff;
	rio_mport_read_config_32(port, destid, hopcount, RIO_ASM_INFO_CAR,
				 &result);
	rdev->asm_rev = result >> 16;
	if (rdev->pef & RIO_PEF_EXT_FEATURES) {
		rdev->efptr = result & 0xffff;
		rdev->phys_efptr = rio_mport_get_physefb(port, 0, destid,
							 hopcount);

		rdev->em_efptr = rio_mport_get_feature(port, 0, destid,
						hopcount, RIO_EFB_ERR_MGMNT);
	}

	rio_mport_read_config_32(port, destid, hopcount, RIO_SRC_OPS_CAR,
				 &rdev->src_ops);
	rio_mport_read_config_32(port, destid, hopcount, RIO_DST_OPS_CAR,
				 &rdev->dst_ops);

	if (do_enum) {
		
		if (next_comptag >= 0x10000) {
			pr_err("RIO: Component Tag Counter Overflow\n");
			goto cleanup;
		}
		rio_mport_write_config_32(port, destid, hopcount,
					  RIO_COMPONENT_TAG_CSR, next_comptag);
		rdev->comp_tag = next_comptag++;
	}  else {
		rio_mport_read_config_32(port, destid, hopcount,
					 RIO_COMPONENT_TAG_CSR,
					 &rdev->comp_tag);
	}

	if (rio_device_has_destid(port, rdev->src_ops, rdev->dst_ops)) {
		if (do_enum) {
			rio_set_device_id(port, destid, hopcount, next_destid);
			rdev->destid = next_destid++;
			if (next_destid == port->host_deviceid)
				next_destid++;
		} else
			rdev->destid = rio_get_device_id(port, destid, hopcount);

		rdev->hopcount = 0xff;
	} else {
		rdev->destid = destid;
		rdev->hopcount = hopcount;
	}

	
	if (rio_is_switch(rdev)) {
		rswitch = rdev->rswitch;
		rswitch->switchid = rdev->comp_tag & RIO_CTAG_UDEVID;
		rswitch->port_ok = 0;
		rswitch->route_table = kzalloc(sizeof(u8)*
					RIO_MAX_ROUTE_ENTRIES(port->sys_size),
					GFP_KERNEL);
		if (!rswitch->route_table)
			goto cleanup;
		
		for (rdid = 0; rdid < RIO_MAX_ROUTE_ENTRIES(port->sys_size);
				rdid++)
			rswitch->route_table[rdid] = RIO_INVALID_ROUTE;
		dev_set_name(&rdev->dev, "%02x:s:%04x", rdev->net->id,
			     rswitch->switchid);
		rio_switch_init(rdev, do_enum);

		if (do_enum && rswitch->clr_table)
			rswitch->clr_table(port, destid, hopcount,
					   RIO_GLOBAL_TABLE);

		list_add_tail(&rswitch->node, &rio_switches);

	} else {
		if (do_enum)
			
			rio_enable_rx_tx_port(port, 0, destid, hopcount, 0);

		dev_set_name(&rdev->dev, "%02x:e:%04x", rdev->net->id,
			     rdev->destid);
	}

	rdev->dev.bus = &rio_bus_type;
	rdev->dev.parent = &rio_bus;

	device_initialize(&rdev->dev);
	rdev->dev.release = rio_release_dev;
	rio_dev_get(rdev);

	rdev->dma_mask = DMA_BIT_MASK(32);
	rdev->dev.dma_mask = &rdev->dma_mask;
	rdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	if (rdev->dst_ops & RIO_DST_OPS_DOORBELL)
		rio_init_dbell_res(&rdev->riores[RIO_DOORBELL_RESOURCE],
				   0, 0xffff);

	ret = rio_add_device(rdev);
	if (ret)
		goto cleanup;

	return rdev;

cleanup:
	if (rswitch)
		kfree(rswitch->route_table);

	kfree(rdev);
	return NULL;
}

static int
rio_sport_is_active(struct rio_mport *port, u16 destid, u8 hopcount, int sport)
{
	u32 result = 0;
	u32 ext_ftr_ptr;

	ext_ftr_ptr = rio_mport_get_efb(port, 0, destid, hopcount, 0);

	while (ext_ftr_ptr) {
		rio_mport_read_config_32(port, destid, hopcount,
					 ext_ftr_ptr, &result);
		result = RIO_GET_BLOCK_ID(result);
		if ((result == RIO_EFB_SER_EP_FREE_ID) ||
		    (result == RIO_EFB_SER_EP_FREE_ID_V13P) ||
		    (result == RIO_EFB_SER_EP_FREC_ID))
			break;

		ext_ftr_ptr = rio_mport_get_efb(port, 0, destid, hopcount,
						ext_ftr_ptr);
	}

	if (ext_ftr_ptr)
		rio_mport_read_config_32(port, destid, hopcount,
					 ext_ftr_ptr +
					 RIO_PORT_N_ERR_STS_CSR(sport),
					 &result);

	return result & RIO_PORT_N_ERR_STS_PORT_OK;
}

static int
rio_lock_device(struct rio_mport *port, u16 destid, u8 hopcount, int wait_ms)
{
	u32 result;
	int tcnt = 0;

	
	rio_mport_write_config_32(port, destid, hopcount,
				  RIO_HOST_DID_LOCK_CSR, port->host_deviceid);
	rio_mport_read_config_32(port, destid, hopcount,
				 RIO_HOST_DID_LOCK_CSR, &result);

	while (result != port->host_deviceid) {
		if (wait_ms != 0 && tcnt == wait_ms) {
			pr_debug("RIO: timeout when locking device %x:%x\n",
				destid, hopcount);
			return -EINVAL;
		}

		
		mdelay(1);
		tcnt++;
		
		rio_mport_write_config_32(port, destid,
			hopcount,
			RIO_HOST_DID_LOCK_CSR,
			port->host_deviceid);
		rio_mport_read_config_32(port, destid,
			hopcount,
			RIO_HOST_DID_LOCK_CSR, &result);
	}

	return 0;
}

static int
rio_unlock_device(struct rio_mport *port, u16 destid, u8 hopcount)
{
	u32 result;

	
	rio_mport_write_config_32(port, destid,
				  hopcount,
				  RIO_HOST_DID_LOCK_CSR,
				  port->host_deviceid);
	rio_mport_read_config_32(port, destid, hopcount,
		RIO_HOST_DID_LOCK_CSR, &result);
	if ((result & 0xffff) != 0xffff) {
		pr_debug("RIO: badness when releasing device lock %x:%x\n",
			 destid, hopcount);
		return -EINVAL;
	}

	return 0;
}

static int
rio_route_add_entry(struct rio_dev *rdev,
		    u16 table, u16 route_destid, u8 route_port, int lock)
{
	int rc;

	if (lock) {
		rc = rio_lock_device(rdev->net->hport, rdev->destid,
				     rdev->hopcount, 1000);
		if (rc)
			return rc;
	}

	rc = rdev->rswitch->add_entry(rdev->net->hport, rdev->destid,
				      rdev->hopcount, table,
				      route_destid, route_port);
	if (lock)
		rio_unlock_device(rdev->net->hport, rdev->destid,
				  rdev->hopcount);

	return rc;
}

static int
rio_route_get_entry(struct rio_dev *rdev, u16 table,
		    u16 route_destid, u8 *route_port, int lock)
{
	int rc;

	if (lock) {
		rc = rio_lock_device(rdev->net->hport, rdev->destid,
				     rdev->hopcount, 1000);
		if (rc)
			return rc;
	}

	rc = rdev->rswitch->get_entry(rdev->net->hport, rdev->destid,
				      rdev->hopcount, table,
				      route_destid, route_port);
	if (lock)
		rio_unlock_device(rdev->net->hport, rdev->destid,
				  rdev->hopcount);

	return rc;
}

static u16 rio_get_host_deviceid_lock(struct rio_mport *port, u8 hopcount)
{
	u32 result;

	rio_mport_read_config_32(port, RIO_ANY_DESTID(port->sys_size), hopcount,
				 RIO_HOST_DID_LOCK_CSR, &result);

	return (u16) (result & 0xffff);
}

static int __devinit rio_enum_peer(struct rio_net *net, struct rio_mport *port,
			 u8 hopcount, struct rio_dev *prev, int prev_port)
{
	int port_num;
	int cur_destid;
	int sw_destid;
	int sw_inport;
	struct rio_dev *rdev;
	u16 destid;
	u32 regval;
	int tmp;

	if (rio_mport_chk_dev_access(port,
			RIO_ANY_DESTID(port->sys_size), hopcount)) {
		pr_debug("RIO: device access check failed\n");
		return -1;
	}

	if (rio_get_host_deviceid_lock(port, hopcount) == port->host_deviceid) {
		pr_debug("RIO: PE already discovered by this host\n");
		rio_mport_read_config_32(port, RIO_ANY_DESTID(port->sys_size),
				hopcount, RIO_COMPONENT_TAG_CSR, &regval);

		if (regval) {
			rdev = rio_get_comptag((regval & 0xffff), NULL);

			if (rdev && prev && rio_is_switch(prev)) {
				pr_debug("RIO: redundant path to %s\n",
					 rio_name(rdev));
				prev->rswitch->nextdev[prev_port] = rdev;
			}
		}

		return 0;
	}

	
	rio_mport_write_config_32(port, RIO_ANY_DESTID(port->sys_size),
				  hopcount,
				  RIO_HOST_DID_LOCK_CSR, port->host_deviceid);
	while ((tmp = rio_get_host_deviceid_lock(port, hopcount))
	       < port->host_deviceid) {
		
		mdelay(1);
		
		rio_mport_write_config_32(port, RIO_ANY_DESTID(port->sys_size),
					  hopcount,
					  RIO_HOST_DID_LOCK_CSR,
					  port->host_deviceid);
	}

	if (rio_get_host_deviceid_lock(port, hopcount) > port->host_deviceid) {
		pr_debug(
		    "RIO: PE locked by a higher priority host...retreating\n");
		return -1;
	}

	
	rdev = rio_setup_device(net, port, RIO_ANY_DESTID(port->sys_size),
					hopcount, 1);
	if (rdev) {
		
		list_add_tail(&rdev->net_list, &net->devices);
		rdev->prev = prev;
		if (prev && rio_is_switch(prev))
			prev->rswitch->nextdev[prev_port] = rdev;
	} else
		return -1;

	if (rio_is_switch(rdev)) {
		sw_inport = RIO_GET_PORT_NUM(rdev->swpinfo);
		rio_route_add_entry(rdev, RIO_GLOBAL_TABLE,
				    port->host_deviceid, sw_inport, 0);
		rdev->rswitch->route_table[port->host_deviceid] = sw_inport;

		for (destid = 0; destid < next_destid; destid++) {
			if (destid == port->host_deviceid)
				continue;
			rio_route_add_entry(rdev, RIO_GLOBAL_TABLE,
					    destid, sw_inport, 0);
			rdev->rswitch->route_table[destid] = sw_inport;
		}

		pr_debug(
		    "RIO: found %s (vid %4.4x did %4.4x) with %d ports\n",
		    rio_name(rdev), rdev->vid, rdev->did,
		    RIO_GET_TOTAL_PORTS(rdev->swpinfo));
		sw_destid = next_destid;
		for (port_num = 0;
		     port_num < RIO_GET_TOTAL_PORTS(rdev->swpinfo);
		     port_num++) {
			
			rio_enable_rx_tx_port(port, 0,
					      RIO_ANY_DESTID(port->sys_size),
					      hopcount, port_num);

			if (sw_inport == port_num) {
				rdev->rswitch->port_ok |= (1 << port_num);
				continue;
			}

			cur_destid = next_destid;

			if (rio_sport_is_active
			    (port, RIO_ANY_DESTID(port->sys_size), hopcount,
			     port_num)) {
				pr_debug(
				    "RIO: scanning device on port %d\n",
				    port_num);
				rdev->rswitch->port_ok |= (1 << port_num);
				rio_route_add_entry(rdev, RIO_GLOBAL_TABLE,
						RIO_ANY_DESTID(port->sys_size),
						port_num, 0);

				if (rio_enum_peer(net, port, hopcount + 1,
						  rdev, port_num) < 0)
					return -1;

				
				if (next_destid > cur_destid) {
					for (destid = cur_destid;
					     destid < next_destid; destid++) {
						if (destid == port->host_deviceid)
							continue;
						rio_route_add_entry(rdev,
								    RIO_GLOBAL_TABLE,
								    destid,
								    port_num,
								    0);
						rdev->rswitch->
						    route_table[destid] =
						    port_num;
					}
				}
			} else {
				if (rdev->em_efptr)
					rio_set_port_lockout(rdev, port_num, 1);

				rdev->rswitch->port_ok &= ~(1 << port_num);
			}
		}

		
		if ((rdev->src_ops & RIO_SRC_OPS_PORT_WRITE) &&
		    (rdev->em_efptr)) {
			rio_write_config_32(rdev,
					rdev->em_efptr + RIO_EM_PW_TGT_DEVID,
					(port->host_deviceid << 16) |
					(port->sys_size << 15));
		}

		rio_init_em(rdev);

		
		if (next_destid == sw_destid) {
			next_destid++;
			if (next_destid == port->host_deviceid)
				next_destid++;
		}

		rdev->destid = sw_destid;
	} else
		pr_debug("RIO: found %s (vid %4.4x did %4.4x)\n",
		    rio_name(rdev), rdev->vid, rdev->did);

	return 0;
}

static int rio_enum_complete(struct rio_mport *port)
{
	u32 regval;

	rio_local_read_config_32(port, port->phys_efptr + RIO_PORT_GEN_CTL_CSR,
				 &regval);
	return (regval & RIO_PORT_GEN_DISCOVERED) ? 1 : 0;
}

static int __devinit
rio_disc_peer(struct rio_net *net, struct rio_mport *port, u16 destid,
	      u8 hopcount, struct rio_dev *prev, int prev_port)
{
	u8 port_num, route_port;
	struct rio_dev *rdev;
	u16 ndestid;

	
	if ((rdev = rio_setup_device(net, port, destid, hopcount, 0))) {
		
		list_add_tail(&rdev->net_list, &net->devices);
		rdev->prev = prev;
		if (prev && rio_is_switch(prev))
			prev->rswitch->nextdev[prev_port] = rdev;
	} else
		return -1;

	if (rio_is_switch(rdev)) {
		
		rdev->destid = destid;

		pr_debug(
		    "RIO: found %s (vid %4.4x did %4.4x) with %d ports\n",
		    rio_name(rdev), rdev->vid, rdev->did,
		    RIO_GET_TOTAL_PORTS(rdev->swpinfo));
		for (port_num = 0;
		     port_num < RIO_GET_TOTAL_PORTS(rdev->swpinfo);
		     port_num++) {
			if (RIO_GET_PORT_NUM(rdev->swpinfo) == port_num)
				continue;

			if (rio_sport_is_active
			    (port, destid, hopcount, port_num)) {
				pr_debug(
				    "RIO: scanning device on port %d\n",
				    port_num);

				rio_lock_device(port, destid, hopcount, 1000);

				for (ndestid = 0;
				     ndestid < RIO_ANY_DESTID(port->sys_size);
				     ndestid++) {
					rio_route_get_entry(rdev,
							    RIO_GLOBAL_TABLE,
							    ndestid,
							    &route_port, 0);
					if (route_port == port_num)
						break;
				}

				if (ndestid == RIO_ANY_DESTID(port->sys_size))
					continue;
				rio_unlock_device(port, destid, hopcount);
				if (rio_disc_peer(net, port, ndestid,
					hopcount + 1, rdev, port_num) < 0)
					return -1;
			}
		}
	} else
		pr_debug("RIO: found %s (vid %4.4x did %4.4x)\n",
		    rio_name(rdev), rdev->vid, rdev->did);

	return 0;
}

static int rio_mport_is_active(struct rio_mport *port)
{
	u32 result = 0;
	u32 ext_ftr_ptr;
	int *entry = rio_mport_phys_table;

	do {
		if ((ext_ftr_ptr =
		     rio_mport_get_feature(port, 1, 0, 0, *entry)))
			break;
	} while (*++entry >= 0);

	if (ext_ftr_ptr)
		rio_local_read_config_32(port,
					 ext_ftr_ptr +
					 RIO_PORT_N_ERR_STS_CSR(port->index),
					 &result);

	return result & RIO_PORT_N_ERR_STS_PORT_OK;
}

static struct rio_net __devinit *rio_alloc_net(struct rio_mport *port)
{
	struct rio_net *net;

	net = kzalloc(sizeof(struct rio_net), GFP_KERNEL);
	if (net) {
		INIT_LIST_HEAD(&net->node);
		INIT_LIST_HEAD(&net->devices);
		INIT_LIST_HEAD(&net->mports);
		list_add_tail(&port->nnode, &net->mports);
		net->hport = port;
		net->id = next_net++;
	}
	return net;
}

static void rio_update_route_tables(struct rio_mport *port)
{
	struct rio_dev *rdev, *swrdev;
	struct rio_switch *rswitch;
	u8 sport;
	u16 destid;

	list_for_each_entry(rdev, &rio_devices, global_list) {

		destid = rdev->destid;

		list_for_each_entry(rswitch, &rio_switches, node) {

			if (rio_is_switch(rdev)	&& (rdev->rswitch == rswitch))
				continue;

			if (RIO_INVALID_ROUTE == rswitch->route_table[destid]) {
				swrdev = sw_to_rio_dev(rswitch);

				
				if (swrdev->destid == destid)
					continue;

				sport = RIO_GET_PORT_NUM(swrdev->swpinfo);

				if (rswitch->add_entry)	{
					rio_route_add_entry(swrdev,
						RIO_GLOBAL_TABLE, destid,
						sport, 0);
					rswitch->route_table[destid] = sport;
				}
			}
		}
	}
}

static void rio_init_em(struct rio_dev *rdev)
{
	if (rio_is_switch(rdev) && (rdev->em_efptr) &&
	    (rdev->rswitch->em_init)) {
		rdev->rswitch->em_init(rdev);
	}
}

static void rio_pw_enable(struct rio_mport *port, int enable)
{
	if (port->ops->pwenable)
		port->ops->pwenable(port, enable);
}

int __devinit rio_enum_mport(struct rio_mport *mport)
{
	struct rio_net *net = NULL;
	int rc = 0;

	printk(KERN_INFO "RIO: enumerate master port %d, %s\n", mport->id,
	       mport->name);
	
	if (rio_enum_host(mport) < 0) {
		printk(KERN_INFO
		       "RIO: master port %d device has been enumerated by a remote host\n",
		       mport->id);
		rc = -EBUSY;
		goto out;
	}

	
	if (rio_mport_is_active(mport)) {
		if (!(net = rio_alloc_net(mport))) {
			printk(KERN_ERR "RIO: failed to allocate new net\n");
			rc = -ENOMEM;
			goto out;
		}

		
		rio_enable_rx_tx_port(mport, 1, 0, 0, 0);

		
		rio_local_write_config_32(mport, RIO_COMPONENT_TAG_CSR,
					  next_comptag++);

		if (rio_enum_peer(net, mport, 0, NULL, 0) < 0) {
			
			printk(KERN_INFO
			       "RIO: master port %d device has lost enumeration to a remote host\n",
			       mport->id);
			rio_clear_locks(mport);
			rc = -EBUSY;
			goto out;
		}
		rio_update_route_tables(mport);
		rio_clear_locks(mport);
		rio_pw_enable(mport, 1);
	} else {
		printk(KERN_INFO "RIO: master port %d link inactive\n",
		       mport->id);
		rc = -EINVAL;
	}

      out:
	return rc;
}

static void rio_build_route_tables(void)
{
	struct rio_dev *rdev;
	int i;
	u8 sport;

	list_for_each_entry(rdev, &rio_devices, global_list)
		if (rio_is_switch(rdev)) {
			rio_lock_device(rdev->net->hport, rdev->destid,
					rdev->hopcount, 1000);
			for (i = 0;
			     i < RIO_MAX_ROUTE_ENTRIES(rdev->net->hport->sys_size);
			     i++) {
				if (rio_route_get_entry(rdev,
					RIO_GLOBAL_TABLE, i, &sport, 0) < 0)
					continue;
				rdev->rswitch->route_table[i] = sport;
			}

			rio_unlock_device(rdev->net->hport,
					  rdev->destid,
					  rdev->hopcount);
		}
}

static void rio_enum_timeout(unsigned long data)
{
	
	*(int *)data = 1;
}

int __devinit rio_disc_mport(struct rio_mport *mport)
{
	struct rio_net *net = NULL;
	int enum_timeout_flag = 0;

	printk(KERN_INFO "RIO: discover master port %d, %s\n", mport->id,
	       mport->name);

	
	if (rio_mport_is_active(mport)) {
		if (!(net = rio_alloc_net(mport))) {
			printk(KERN_ERR "RIO: Failed to allocate new net\n");
			goto bail;
		}

		pr_debug("RIO: wait for enumeration complete...");

		rio_enum_timer.expires =
		    jiffies + CONFIG_RAPIDIO_DISC_TIMEOUT * HZ;
		rio_enum_timer.data = (unsigned long)&enum_timeout_flag;
		add_timer(&rio_enum_timer);
		while (!rio_enum_complete(mport)) {
			mdelay(1);
			if (enum_timeout_flag) {
				del_timer_sync(&rio_enum_timer);
				goto timeout;
			}
		}
		del_timer_sync(&rio_enum_timer);

		pr_debug("done\n");

		
		rio_local_read_config_32(mport, RIO_DID_CSR,
					 &mport->host_deviceid);
		mport->host_deviceid = RIO_GET_DID(mport->sys_size,
						   mport->host_deviceid);

		if (rio_disc_peer(net, mport, RIO_ANY_DESTID(mport->sys_size),
					0, NULL, 0) < 0) {
			printk(KERN_INFO
			       "RIO: master port %d device has failed discovery\n",
			       mport->id);
			goto bail;
		}

		rio_build_route_tables();
	}

	return 0;

      timeout:
	pr_debug("timeout\n");
      bail:
	return -EBUSY;
}
