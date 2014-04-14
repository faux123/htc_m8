/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/hyperv.h>
#include <asm/hyperv.h>
#include "hyperv_vmbus.h"


struct vmbus_connection vmbus_connection = {
	.conn_state		= DISCONNECTED,
	.next_gpadl_handle	= ATOMIC_INIT(0xE1E10),
};

int vmbus_connect(void)
{
	int ret = 0;
	int t;
	struct vmbus_channel_msginfo *msginfo = NULL;
	struct vmbus_channel_initiate_contact *msg;
	unsigned long flags;

	
	vmbus_connection.conn_state = CONNECTING;
	vmbus_connection.work_queue = create_workqueue("hv_vmbus_con");
	if (!vmbus_connection.work_queue) {
		ret = -ENOMEM;
		goto cleanup;
	}

	INIT_LIST_HEAD(&vmbus_connection.chn_msg_list);
	spin_lock_init(&vmbus_connection.channelmsg_lock);

	INIT_LIST_HEAD(&vmbus_connection.chn_list);
	spin_lock_init(&vmbus_connection.channel_lock);

	vmbus_connection.int_page =
	(void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, 0);
	if (vmbus_connection.int_page == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	vmbus_connection.recv_int_page = vmbus_connection.int_page;
	vmbus_connection.send_int_page =
		(void *)((unsigned long)vmbus_connection.int_page +
			(PAGE_SIZE >> 1));

	vmbus_connection.monitor_pages =
	(void *)__get_free_pages((GFP_KERNEL|__GFP_ZERO), 1);
	if (vmbus_connection.monitor_pages == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	msginfo = kzalloc(sizeof(*msginfo) +
			  sizeof(struct vmbus_channel_initiate_contact),
			  GFP_KERNEL);
	if (msginfo == NULL) {
		ret = -ENOMEM;
		goto cleanup;
	}

	init_completion(&msginfo->waitevent);

	msg = (struct vmbus_channel_initiate_contact *)msginfo->msg;

	msg->header.msgtype = CHANNELMSG_INITIATE_CONTACT;
	msg->vmbus_version_requested = VMBUS_REVISION_NUMBER;
	msg->interrupt_page = virt_to_phys(vmbus_connection.int_page);
	msg->monitor_page1 = virt_to_phys(vmbus_connection.monitor_pages);
	msg->monitor_page2 = virt_to_phys(
			(void *)((unsigned long)vmbus_connection.monitor_pages +
				 PAGE_SIZE));

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_add_tail(&msginfo->msglistentry,
		      &vmbus_connection.chn_msg_list);

	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	ret = vmbus_post_msg(msg,
			       sizeof(struct vmbus_channel_initiate_contact));
	if (ret != 0) {
		spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
		list_del(&msginfo->msglistentry);
		spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock,
					flags);
		goto cleanup;
	}

	
	t =  wait_for_completion_timeout(&msginfo->waitevent, 5*HZ);
	if (t == 0) {
		spin_lock_irqsave(&vmbus_connection.channelmsg_lock,
				flags);
		list_del(&msginfo->msglistentry);
		spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock,
					flags);
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	spin_lock_irqsave(&vmbus_connection.channelmsg_lock, flags);
	list_del(&msginfo->msglistentry);
	spin_unlock_irqrestore(&vmbus_connection.channelmsg_lock, flags);

	
	if (msginfo->response.version_response.version_supported) {
		vmbus_connection.conn_state = CONNECTED;
	} else {
		pr_err("Unable to connect, "
			"Version %d not supported by Hyper-V\n",
			VMBUS_REVISION_NUMBER);
		ret = -ECONNREFUSED;
		goto cleanup;
	}

	kfree(msginfo);
	return 0;

cleanup:
	vmbus_connection.conn_state = DISCONNECTED;

	if (vmbus_connection.work_queue)
		destroy_workqueue(vmbus_connection.work_queue);

	if (vmbus_connection.int_page) {
		free_pages((unsigned long)vmbus_connection.int_page, 0);
		vmbus_connection.int_page = NULL;
	}

	if (vmbus_connection.monitor_pages) {
		free_pages((unsigned long)vmbus_connection.monitor_pages, 1);
		vmbus_connection.monitor_pages = NULL;
	}

	kfree(msginfo);

	return ret;
}


struct vmbus_channel *relid2channel(u32 relid)
{
	struct vmbus_channel *channel;
	struct vmbus_channel *found_channel  = NULL;
	unsigned long flags;

	spin_lock_irqsave(&vmbus_connection.channel_lock, flags);
	list_for_each_entry(channel, &vmbus_connection.chn_list, listentry) {
		if (channel->offermsg.child_relid == relid) {
			found_channel = channel;
			break;
		}
	}
	spin_unlock_irqrestore(&vmbus_connection.channel_lock, flags);

	return found_channel;
}

static void process_chn_event(u32 relid)
{
	struct vmbus_channel *channel;
	unsigned long flags;

	channel = relid2channel(relid);

	if (!channel) {
		pr_err("channel not found for relid - %u\n", relid);
		return;
	}


	spin_lock_irqsave(&channel->inbound_lock, flags);
	if (channel->onchannel_callback != NULL)
		channel->onchannel_callback(channel->channel_callback_context);
	else
		pr_err("no channel callback for relid - %u\n", relid);

	spin_unlock_irqrestore(&channel->inbound_lock, flags);
}

void vmbus_on_event(unsigned long data)
{
	u32 dword;
	u32 maxdword = MAX_NUM_CHANNELS_SUPPORTED >> 5;
	int bit;
	u32 relid;
	u32 *recv_int_page = vmbus_connection.recv_int_page;

	
	if (!recv_int_page)
		return;
	for (dword = 0; dword < maxdword; dword++) {
		if (!recv_int_page[dword])
			continue;
		for (bit = 0; bit < 32; bit++) {
			if (sync_test_and_clear_bit(bit,
				(unsigned long *)&recv_int_page[dword])) {
				relid = (dword << 5) + bit;

				if (relid == 0)
					continue;

				process_chn_event(relid);
			}
		}
	}
}

int vmbus_post_msg(void *buffer, size_t buflen)
{
	union hv_connection_id conn_id;
	int ret = 0;
	int retries = 0;

	conn_id.asu32 = 0;
	conn_id.u.id = VMBUS_MESSAGE_CONNECTION_ID;

	while (retries < 3) {
		ret =  hv_post_message(conn_id, 1, buffer, buflen);
		if (ret != HV_STATUS_INSUFFICIENT_BUFFERS)
			return ret;
		retries++;
		msleep(100);
	}
	return ret;
}

int vmbus_set_event(u32 child_relid)
{
	
	sync_set_bit(child_relid & 31,
		(unsigned long *)vmbus_connection.send_int_page +
		(child_relid >> 5));

	return hv_signal_event();
}
