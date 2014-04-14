/* 
* Copyright (C) ST-Ericsson AP Pte Ltd 2010 
*
* ISP1763 Linux OTG Controller driver : host
* 
* This program is free software; you can redistribute it and/or modify it under the terms of 
* the GNU General Public License as published by the Free Software Foundation; version 
* 2 of the License. 
* 
* This program is distributed in the hope that it will be useful, but WITHOUT ANY  
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS  
* FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more  
* details. 
* 
* You should have received a copy of the GNU General Public License 
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA. 
* 
* This is a host controller driver file.  QTD processing is handled here.
* 
* Author : wired support <wired.support@stericsson.com>
*
*/



#define	QUEUE_HEAD_NOT_EMPTY	0x001


static void
phci_hcd_release_td_ptd_index(struct ehci_qh *qh)
{
	td_ptd_map_buff_t *td_ptd_buff = &td_ptd_map_buff[qh->type];
	td_ptd_map_t *td_ptd_map = &td_ptd_buff->map_list[qh->qtd_ptd_index];
	pehci_entry("++	%s: Entered\n",	__FUNCTION__);
	
	td_ptd_map->state = TD_PTD_NEW;
	qh->qh_state = QH_STATE_IDLE;
	td_ptd_map->qh = NULL;
	td_ptd_map->qtd	= NULL;

	td_ptd_buff->active_ptd_bitmap &= ~td_ptd_map->ptd_bitmap;

	
	td_ptd_buff->pending_ptd_bitmap	&= ~td_ptd_map->ptd_bitmap;

	pehci_entry("--	%s: Exit\n", __FUNCTION__);

}

static void
print_ehci_qtd(struct ehci_qtd *qtd)
{
	pehci_print("hwnext 0x%08x, altnext 0x%08x,token 0x%08x, length	%d\n",
		    qtd->hw_next, qtd->hw_alt_next,
		    le32_to_cpu(qtd->hw_token),	qtd->length);

	pehci_print("buf[0] 0x%08x\n", qtd->hw_buf[0]);

}

static void
phci_hcd_qtd_list_free(phci_hcd	* ehci,
		       struct urb *urb,	struct list_head *qtd_list)
{
	struct list_head *entry, *temp;

	pehci_entry("++	%s: Entered\n",	__FUNCTION__);

	list_for_each_safe(entry, temp,	qtd_list) {
		struct ehci_qtd	*qtd;
		qtd = list_entry(entry,	struct ehci_qtd, qtd_list);
	if(!list_empty(&qtd->qtd_list))
		list_del_init(&qtd->qtd_list);
		qha_free(qha_cache, qtd);
	}

	pehci_entry("--	%s: Exit \n", __FUNCTION__);
}


static void
phci_hcd_urb_free_priv(phci_hcd	* hcd,
		       urb_priv_t * urb_priv_to_remove,	struct ehci_qh *qh)
{
	int i =	0;
	struct ehci_qtd	*qtd;
	for (i = 0; i <	urb_priv_to_remove->length; i++) {
		if (urb_priv_to_remove->qtd[i])	{
			qtd = urb_priv_to_remove->qtd[i];

			if(!list_empty(&qtd->qtd_list))
				list_del_init(&qtd->qtd_list);

			if (qtd->length)
				phci_hcd_mem_free(&qtd->mem_addr);

			qha_free(qha_cache, qtd);
			urb_priv_to_remove->qtd[i] = 0;
			qtd = 0;
		}

	}
	
	return;
}


struct ehci_qtd	*
phci_hcd_qtd_allocate(int mem_flags)
{

	struct ehci_qtd	*qtd = 0;
	qtd = kmalloc(sizeof *qtd, mem_flags);
	if (!qtd)
	{
		return 0;
	}
	
	memset(qtd, 0, sizeof *qtd);
	qtd->qtd_dma = cpu_to_le32(qtd);
	qtd->hw_next = EHCI_LIST_END;
	qtd->hw_alt_next = EHCI_LIST_END;
	qtd->state = QTD_STATE_NEW;
	INIT_LIST_HEAD(&qtd->qtd_list);
	return qtd;
}

static int
phci_hcd_qtd_fill(struct urb *urb,
		  struct ehci_qtd *qtd,
		  dma_addr_t buf, size_t len, int token, int *status)
{
	int count = 0;

	qtd->hw_buf[0] = (u32) buf;
	
	if (len	> HC_ATL_PL_SIZE) {
		count =	HC_ATL_PL_SIZE;
	} else {
		count =	len;
	}
	qtd->hw_token =	cpu_to_le32((count << 16) | token);
	qtd->length = count;

	pehci_print("%s:qtd %p,	token %8x bytes	%d dma %x\n",
		__FUNCTION__, qtd, le32_to_cpu(qtd->hw_token), count,
		qtd->hw_buf[0]);

	return count;
}


struct list_head *
phci_hcd_make_qtd(phci_hcd * hcd,
		  struct list_head *head, struct urb *urb, int *status)
{

	struct ehci_qtd	*qtd, *qtd_prev;
	dma_addr_t buf,	map_buf;
	int len, maxpacket;
	int is_input;
	u32 token;
	int cnt	= 0;
	urb_priv_t *urb_priv = (urb_priv_t *) urb->hcpriv;

	pehci_entry("++	%s, Entered\n",	__FUNCTION__);

	qtd = urb_priv->qtd[cnt];
	if (unlikely(!qtd)) {
		*status	= -ENOMEM;
		return 0;
	}

	qtd_prev = 0;
	list_add_tail(&qtd->qtd_list, head);

	qtd->urb = urb;

	token =	QTD_STS_ACTIVE;
	token |= (EHCI_TUNE_CERR << 10);

	len = urb->transfer_buffer_length;

	is_input = usb_pipein(urb->pipe);

	if (usb_pipecontrol(urb->pipe))	{
		
		if (phci_hcd_qtd_fill(urb, qtd,	cpu_to_le32(urb->setup_packet),
			sizeof(struct usb_ctrlrequest),
			token |	(2 	<< 8),
			status)	<	0) {
			goto cleanup;
		}

		cnt++;		
		print_ehci_qtd(qtd);
		
		token ^= QTD_TOGGLE;
		qtd_prev = qtd;
		qtd = urb_priv->qtd[cnt];
		if (unlikely(!qtd)) {
			*status	= -ENOMEM;
			goto cleanup;
		}
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(qtd->qtd_dma);
		list_add_tail(&qtd->qtd_list, head);
	}

	len = urb->transfer_buffer_length;
	if (likely(len > 0)) {
		
		buf = cpu_to_le32(urb->transfer_buffer);
	} else {
		buf = map_buf =	cpu_to_le32(0);	
	}

	
	if (!buf || usb_pipein(urb->pipe)) {
		token |= (1 	<< 8);
	}
	
	maxpacket = usb_maxpacket(urb->dev, urb->pipe,
				  usb_pipeout(urb->pipe)) & 0x07ff;



	for (;;) {
		int this_qtd_len;
		this_qtd_len =
			phci_hcd_qtd_fill(urb, qtd, buf, len, token, status);
		if (this_qtd_len < 0)
			goto cleanup;
		print_ehci_qtd(qtd);
		len -= this_qtd_len;
		buf += this_qtd_len;
		cnt++;
		
		if ((maxpacket & (this_qtd_len + (maxpacket - 1))) == 0) {
			token ^= QTD_TOGGLE;
		}

		if (likely(len <= 0)) {
			break;
		}
		qtd_prev = qtd;
		qtd = urb_priv->qtd[cnt];
		if (unlikely(!qtd)) {
			goto cleanup;
		}
		qtd->urb = urb;
		qtd_prev->hw_next = QTD_NEXT(qtd->qtd_dma);
		list_add_tail(&qtd->qtd_list, head);
	}

	if (likely(buf != 0)) {
		int one_more = 0;
		if (usb_pipecontrol(urb->pipe))	{
			one_more = 1;
			token ^= 0x0100;	
			token |= QTD_TOGGLE;	

		} else if (usb_pipebulk(urb->pipe)	
			&&(urb->transfer_flags & URB_ZERO_PACKET)
			&& !(urb->transfer_buffer_length % maxpacket)) {
			one_more = 1;
		}
		if (one_more) {
			qtd_prev = qtd;
			qtd = urb_priv->qtd[cnt];
			if (unlikely(!qtd)) {
				goto cleanup;
			}

			qtd->urb = urb;
			qtd_prev->hw_next = QTD_NEXT(qtd->qtd_dma);
			list_add_tail(&qtd->qtd_list, head);
			phci_hcd_qtd_fill(urb, qtd, 0, 0, token, status);
			print_ehci_qtd(qtd);
			cnt++;
		}
	}

	
	qtd->state |= QTD_STATE_LAST;

	
	if (urb_priv->length !=	cnt) {
		err("Never Error: number of tds	allocated %d exceeding %d\n",
		    urb_priv->length, cnt);
	}
	
	if (likely(!(urb->transfer_flags & URB_NO_INTERRUPT))) {
		qtd->hw_token |= __constant_cpu_to_le32(QTD_IOC);
	}

	pehci_entry("--	%s, Exit\n", __FUNCTION__);
	return head;

	cleanup:
	phci_hcd_qtd_list_free(hcd, urb, head);
	return 0;
}

struct ehci_qh *
phci_hcd_qh_alloc(phci_hcd * hcd)
{

	struct ehci_qh *qh = kmalloc(sizeof(struct ehci_qh), GFP_ATOMIC);
	if (!qh)
	{
		return qh;
	}
	
	memset(qh, 0, sizeof *qh);
	atomic_set(&qh->refcount, 1);
	init_waitqueue_head(&qh->waitforcomplete);
	qh->qh_dma = (u32) qh;
	INIT_LIST_HEAD(&qh->qtd_list);
	INIT_LIST_HEAD(&qh->itd_list);
	qh->next_uframe	= -1;
	return qh;
}

static int
phci_hcd_fill_ptd_addresses(td_ptd_map_t * td_ptd_map, int index, int bufftype)
{
	int i =	0;
	unsigned long tdlocation = 0;
	unsigned long payloadlocation =	0;
	unsigned long payloadsize = 0;
	pehci_entry("++	%s: enter\n", __FUNCTION__);
	switch (bufftype) {
		
	case TD_PTD_BUFF_TYPE_ATL:
		tdlocation = 0x0c00;
		
		payloadsize = 0x1000;
		payloadlocation	= 0x1000;
		break;
	case TD_PTD_BUFF_TYPE_INTL:
		tdlocation = 0x0800;
		
		payloadlocation	= 0x1000;
		payloadsize = 0x1000;
		break;

	case TD_PTD_BUFF_TYPE_ISTL:

		tdlocation = 0x0400;
		
		payloadlocation	= 0x1000;
		payloadsize = 0x1000;

		break;
	}


	i = index;
	payloadlocation	+= (i) * payloadsize;	
	tdlocation += (i) * PHCI_QHA_LENGTH;	
	td_ptd_map->ptd_header_addr = tdlocation;
	td_ptd_map->ptd_data_addr = payloadlocation;
	td_ptd_map->ptd_ram_data_addr =	((payloadlocation - 0x0400) >> 3);
	pehci_print
		("Index: %d, Header: 0x%08x, Payload: 0x%08x,Data start	address: 0x%08x\n",
		 index,	td_ptd_map->ptd_header_addr, td_ptd_map->ptd_data_addr,
		 td_ptd_map->ptd_ram_data_addr);
	pehci_entry("--	%s: Exit", __FUNCTION__);
	return payloadlocation;
}


static void
phci_hcd_get_qtd_ptd_index(struct ehci_qh *qh,
			   struct ehci_qtd *qtd, struct	ehci_itd *itd)
{
	u8 buff_type = td_ptd_pipe_x_buff_type[qh->type];
	u8 qtd_ptd_index;	
	u8 bitmap = 0x1;
	u8 max_ptds;

	td_ptd_map_buff_t *ptd_map_buff	= &(td_ptd_map_buff[buff_type]);
	pehci_entry("++	%s, Entered, buffer type %d\n",	__FUNCTION__,
		    buff_type);

	
	max_ptds = (buff_type == TD_PTD_BUFF_TYPE_ATL)
		? TD_PTD_MAX_BUFF_TDS :	ptd_map_buff->max_ptds;

	for (qtd_ptd_index = 0;	qtd_ptd_index <	max_ptds; qtd_ptd_index++) {	
		if (ptd_map_buff->map_list[qtd_ptd_index].state	== TD_PTD_NEW) {
			
			if (qh->qtd_ptd_index == TD_PTD_INV_PTD_INDEX) {
				qh->qtd_ptd_index = qtd_ptd_index;
			}
			ptd_map_buff->map_list[qtd_ptd_index].datatoggle = 0;
			
			ptd_map_buff->map_list[qtd_ptd_index].state =
				TD_PTD_ACTIVE;
			ptd_map_buff->map_list[qtd_ptd_index].qtd = qtd;
			
			ptd_map_buff->map_list[qtd_ptd_index].itd = itd;
			
			ptd_map_buff->map_list[qtd_ptd_index].qh = qh;
			ptd_map_buff->map_list[qtd_ptd_index].ptd_bitmap =
				bitmap << qtd_ptd_index;
			phci_hcd_fill_ptd_addresses(&ptd_map_buff->
				map_list[qtd_ptd_index],
				qh->qtd_ptd_index,
				buff_type);
			ptd_map_buff->map_list[qtd_ptd_index].lasttd = 0;
			ptd_map_buff->total_ptds++;	
			
			ptd_map_buff->active_ptd_bitmap	|=
				(bitmap	<< qtd_ptd_index);
			break;
		}
	}
	pehci_entry("--	%s, Exit\n", __FUNCTION__);
	return;

}				



static void
phci_hcd_qh_link_async(phci_hcd	* hcd, struct ehci_qh *qh, int *status)
{
	struct ehci_qtd	*qtd = 0;
	struct list_head *qtd_list = &qh->qtd_list;

#ifdef MSEC_INT_BASED
	td_ptd_map_buff_t *ptd_map_buff;
	td_ptd_map_t *td_ptd_map;
#endif

	qtd = list_entry(qtd_list->next, struct	ehci_qtd, qtd_list);

	pehci_entry("++	%s: Entered\n",	__FUNCTION__);

	

	qh->qtd_ptd_index = TD_PTD_INV_PTD_INDEX;
	phci_hcd_get_qtd_ptd_index(qh, qtd, NULL);	
	if (qh->qtd_ptd_index == TD_PTD_INV_PTD_INDEX) {
		err("can not find the location in our buffer\n");
		*status	= -ENOSPC;
		return;
	}
#ifdef MSEC_INT_BASED
	
	ptd_map_buff = &(td_ptd_map_buff[qh->type]);
	td_ptd_map = &ptd_map_buff->map_list[qh->qtd_ptd_index];
	ptd_map_buff->pending_ptd_bitmap |= td_ptd_map->ptd_bitmap;

#endif
	
	qh->hw_token &=	~__constant_cpu_to_le32(QTD_STS_HALT);
	qh->qh_state = QH_STATE_LINKED;
	qh->qh_state |=	QH_STATE_TAKE_NEXT;
	pehci_entry("--	%s: Exit , qh %p\n", __FUNCTION__, qh);


}



static inline void
phci_hcd_qh_update(phci_hcd * ehci, struct ehci_qh *qh,	struct ehci_qtd	*qtd)
{
	
	qh->hw_current = QTD_NEXT(qtd->qtd_dma);
	qh->hw_qtd_next	= QTD_NEXT(qtd->qtd_dma);
	qh->hw_alt_next	= EHCI_LIST_END;
	
	wmb();
	qh->hw_token &=	__constant_cpu_to_le32(QTD_TOGGLE | QTD_STS_PING);
}

struct ehci_qh *
phci_hcd_make_qh(phci_hcd * hcd,
		 struct	urb *urb, struct list_head *qtd_list, int *status)
{
	struct ehci_qh *qh = 0;
	u32 info1 = 0, info2 = 0;
	int is_input, type;
	int maxp = 0;
	int mult = 0;
	int bustime = 0;
	struct ehci_qtd	*qtd =
		list_entry(qtd_list->next, struct ehci_qtd, qtd_list);


	pehci_entry("++	%s: Entered\n",	__FUNCTION__);

	qh = phci_hcd_qh_alloc(hcd);
	if (!qh) {
		*status	= -ENOMEM;
		return 0;
	}

	info1 |= usb_pipeendpoint(urb->pipe) <<	8;
	info1 |= usb_pipedevice(urb->pipe) << 0;

	is_input = usb_pipein(urb->pipe);
	type = usb_pipetype(urb->pipe);
	maxp = usb_maxpacket(urb->dev, urb->pipe, !is_input);
	mult = 1 + ((maxp >> 11) & 0x3);

	
	qh->qtd_ptd_index = TD_PTD_INV_PTD_INDEX;

	switch (type) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		qh->type = TD_PTD_BUFF_TYPE_ATL;
		break;

	case PIPE_INTERRUPT:
		qh->type = TD_PTD_BUFF_TYPE_INTL;
		break;
	case PIPE_ISOCHRONOUS:
		qh->type = TD_PTD_BUFF_TYPE_ISTL;
		break;

	}



	if (type == PIPE_INTERRUPT) {
		
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
		bustime = usb_check_bandwidth(urb->dev, urb);

		if (bustime < 0) {
			*status = -ENOSPC;
			goto done;
		}

		usb_claim_bandwidth(urb->dev, urb, bustime,
			usb_pipeisoc(urb->pipe));
#else
#endif
		qh->usecs = bustime;

		qh->start = NO_FRAME;

		if (urb->dev->speed == USB_SPEED_HIGH) {
			qh->c_usecs = 0;
			qh->gap_uf = 0;
			
			qh->period = urb->interval >> 3;
			if (qh->period < 1) {
				printk("intr period %d uframes,\n",
				urb->interval);
			}
			
			qh->period = urb->interval;

		} else {
			
			qh->gap_uf = 1 + 7;	

			if (is_input) {	

				qh->c_usecs = qh->usecs	+ 1;	
				qh->usecs = 10;	
			} else {	
				qh->usecs += 10;	
				qh->c_usecs = 1;	
			}


			qh->period = urb->interval;
		}
	}

	
	switch (urb->dev->speed) {
	case USB_SPEED_LOW:
		info1 |= (1 << 12);	
		

	case USB_SPEED_FULL:
		
		if (type != PIPE_INTERRUPT) {
			info1 |= (EHCI_TUNE_RL_TT << 28);
		}
		if (type == PIPE_CONTROL) {
			info1 |= (1 << 27);	
			info1 |= 1 << 14;	
		}
		info1 |= maxp << 16;

		info2 |= (EHCI_TUNE_MULT_TT << 30);
		info2 |= urb->dev->ttport << 23;
		info2 |= urb->dev->tt->hub->devnum << 16;
		break;


	case USB_SPEED_HIGH:	
		info1 |= (2 << 12);	
		if (type == PIPE_CONTROL) {
			info1 |= (EHCI_TUNE_RL_HS << 28);
			info1 |= 64 << 16;	

			info1 |= 1 << 14;	
			info2 |= (EHCI_TUNE_MULT_HS << 30);
		} else if (type	== PIPE_BULK) {
			info1 |= (EHCI_TUNE_RL_HS << 28);
			info1 |= 512 <<	16;	
			info2 |= (EHCI_TUNE_MULT_HS << 30);
		} else {	
			info1 |= (maxp & 0x7ff)	 <<16;
			info2 |= mult   << 30;
		}
		break;

	default:
		pehci_print("bogus dev %p speed	%d", urb->dev, urb->dev->speed);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	done:
#else
#endif
		qha_free(qha_cache, qh);
		return 0;
	}			

	

	
	qh->qh_state = QH_STATE_IDLE;
	qh->hw_info1 = cpu_to_le32(info1);
	qh->hw_info2 = cpu_to_le32(info2);
	
	list_splice(qtd_list, &qh->qtd_list);
	phci_hcd_qh_update(hcd,	qh, qtd);
	qh->hw_token = cpu_to_le32(QTD_STS_HALT);
	if (!usb_pipecontrol(urb->pipe)) {
		usb_settoggle(urb->dev,	usb_pipeendpoint(urb->pipe), !is_input,
			1);
	}
	pehci_entry("--	%s: Exit, qh %p\n", __FUNCTION__, qh);
	return qh;
}


static inline void
phci_hcd_clear_toggle(struct usb_device	*udev, int ep, int is_out,
		      struct ehci_qh *qh)
{
	pehci_print("clear toggle, dev %d ep 0x%x-%s\n",
		    udev->devnum, ep, is_out ? "out" : "in");
	qh->hw_token &=	~__constant_cpu_to_le32(QTD_TOGGLE);
	usb_settoggle(udev, ep,	is_out,	1);
}


struct ehci_qh *
phci_hcd_qh_append_tds(phci_hcd	* hcd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	struct usb_host_endpoint *ep,
#else
#endif
	struct urb *urb,	struct list_head *qtd_list,
	void **ptr, int *status)
{

	int epnum;

	struct ehci_qh *qh = 0;
	struct ehci_qtd	*qtd =
		list_entry(qtd_list->next, struct ehci_qtd, qtd_list);
	td_ptd_map_buff_t *ptd_map_buff;
	td_ptd_map_t *td_ptd_map;



	pehci_entry("++	%s: Entered\n",	__FUNCTION__);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	epnum = ep->desc.bEndpointAddress;
#else
	epnum = urb->ep->desc.bEndpointAddress;
#endif

	qh = (struct ehci_qh *)	*ptr;
	if (likely(qh != 0)) {
		u32 hw_next = QTD_NEXT(qtd->qtd_dma);
		pehci_print("%Queue head already %p\n",	qh);

		ptd_map_buff = &(td_ptd_map_buff[qh->type]);
		td_ptd_map = &ptd_map_buff->map_list[qh->qtd_ptd_index];

		
		if (unlikely
			(epnum == 0	&& le32_to_cpu(qh->hw_info1 & 0x7f) == 0)) {
			qh->hw_info1 |=	cpu_to_le32(usb_pipedevice(urb->pipe));
		}

		
		if (unlikely(!list_empty(&qh->qtd_list))) {
			struct ehci_qtd	*last_qtd;
			
			last_qtd = list_entry(qh->qtd_list.prev,
				struct ehci_qtd, qtd_list);

			last_qtd->hw_next = hw_next;

			
			*status	= (u32)	QUEUE_HEAD_NOT_EMPTY;

			
		} else {

	


			
			if (usb_pipebulk(urb->pipe)
				&& unlikely(!usb_gettoggle(urb->dev, (epnum	& 0x0f),
				!(epnum & 0x80)))) {

				phci_hcd_clear_toggle(urb->dev,
					epnum & 0x0f,
					!(epnum &	0x80), qh);

				

				qh->datatoggle = 0;
				qh->ping = 0;

			}
			phci_hcd_qh_update(hcd,	qh, qtd);
		}
		
		ptd_map_buff->pending_ptd_bitmap |= td_ptd_map->ptd_bitmap;
		list_splice(qtd_list, qh->qtd_list.prev);
	} else {
		qh = phci_hcd_make_qh(hcd, urb,	qtd_list, status);
		*ptr = qh;
	}
	pehci_entry("--	%s: Exit qh %p\n", __FUNCTION__, qh);
	return qh;
}

struct ehci_qh *
phci_hcd_submit_async(phci_hcd * hcd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	struct usb_host_endpoint *ep,
#else
#endif
		      struct list_head *qtd_list, struct urb *urb, int *status)
{
	struct ehci_qtd	*qtd;
	struct hcd_dev *dev;
	int epnum;

#ifndef THREAD_BASED
	unsigned long flags;
#endif

	
	struct ehci_qh *qh = 0;

	urb_priv_t *urb_priv = urb->hcpriv;

	qtd = list_entry(qtd_list->next, struct	ehci_qtd, qtd_list);
	dev = (struct hcd_dev *) urb->hcpriv;
	epnum =	usb_pipeendpoint(urb->pipe);
	if (usb_pipein(urb->pipe) && !usb_pipecontrol(urb->pipe)) {
		epnum |= 0x10;
	}

	pehci_entry("++	%s, enter\n", __FUNCTION__);

#ifndef THREAD_BASED
	spin_lock_irqsave(&hcd->lock, flags);
#endif

	spin_lock(&hcd_data_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	usb_hcd_link_urb_to_ep(&hcd->usb_hcd, urb);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	qh = phci_hcd_qh_append_tds(hcd, ep, urb, qtd_list, &ep->hcpriv,
		status);
#else
	qh = phci_hcd_qh_append_tds(hcd, urb, qtd_list, &urb->ep->hcpriv,
		status);
#endif
	if (!qh	|| *status < 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
		usb_hcd_unlink_urb_from_ep(&hcd->usb_hcd, urb);
#endif
		goto cleanup;
	}

	

	if (qh->qh_state == QH_STATE_UNLINK) {
		pehci_info("%s:	free the urb,qh->state %x\n", __FUNCTION__,
			   qh->qh_state);
		phci_hcd_qtd_list_free(hcd, urb, &qh->qtd_list);
		spin_unlock(&hcd_data_lock);
		
#ifndef THREAD_BASED			
		spin_unlock_irqrestore(&hcd->lock, flags);
#endif
		*status	= -ENODEV;
		return 0;
	}

	if (likely(qh != 0)) {
		urb_priv->qh = qh;
		if (likely(qh->qh_state	== QH_STATE_IDLE))
			phci_hcd_qh_link_async(hcd, qh,	status);
	}

	cleanup:
	spin_unlock(&hcd_data_lock);

#ifndef THREAD_BASED			
	
	spin_unlock_irqrestore(&hcd->lock, flags);
#endif
	
	
	if (unlikely(qh	== 0) || *status < 0) {
		phci_hcd_qtd_list_free(hcd, urb, qtd_list);
		return qh;
	}
	return qh;
}

static int
phci_hcd_qhint_schedule(phci_hcd * hcd,
			struct ehci_qh *qh,
			struct ehci_qtd	*qtd,
			struct _isp1763_qhint *qha, struct urb *urb)
{
	int i =	0;
	u32 td_info3 = 0;
	u32 td_info5 = 0;
	u32 period = 0;
	u32 usofmask = 1;
	u32 usof = 0;
	u32 ssplit = 0,	csplit = 0xFF;
	int maxpacket;
	u32 numberofusofs = 0;

	
	maxpacket = usb_maxpacket(urb->dev, urb->pipe, !usb_pipein(urb->pipe));
	maxpacket &= 0x7ff;
	
	maxpacket = XFER_PER_UFRAME(qha->td_info1) * maxpacket;

	
	numberofusofs =	urb->transfer_buffer_length / maxpacket;
	
	if (urb->transfer_buffer_length	% maxpacket) {
		numberofusofs += 1;
	}

	for (i = 0; i <	numberofusofs; i++) {
		usofmask <<= i;
		usof |=	usofmask;

	}

	if (urb->dev->speed != USB_SPEED_HIGH && usb_pipeint(urb->pipe)) {
		
		
		
		ssplit = usof;
		
		csplit = 0x1C;
		qha->td_info6 =	csplit;
		period = qh->period;
		if (period >= 32) {
			period = qh->period / 2;
		}
		td_info3 = period;
		goto done;

	} else {
		if (qh->period >= 8) {
			period = qh->period / 8;
		} else {
			period = qh->period;
		}
	}
	
	if (period >= 32) {
		period = 32;
		
		period >>= 1;
	}
	if (qh->period >= 8) {
		
		td_info3 = (period << 3);
	} else {
		
		
		td_info3 = period;
		usof = 0x11;
	}

	done:
	td_info5 = usof;
	qha->td_info3 |= td_info3;
	qha->td_info5 |= usof;
	return numberofusofs;
}

struct ehci_qh *
phci_hcd_submit_interrupt(phci_hcd * hcd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	struct usb_host_endpoint *ep,
#else
#endif
			  struct list_head *qtd_list,
			  struct urb *urb, int *status)
{
	struct ehci_qtd	*qtd;
	struct _hcd_dev	*dev;
	int epnum;
	unsigned long flags;
	struct ehci_qh *qh = 0;
	urb_priv_t *urb_priv = (urb_priv_t *) urb->hcpriv;

	qtd = list_entry(qtd_list->next, struct	ehci_qtd, qtd_list);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	dev = (struct hcd_dev *) urb->hcpriv;
	epnum = ep->desc.bEndpointAddress;

	pehci_entry("++ %s, enter\n", __FUNCTION__);


	
	qh = ep->hcpriv;
#else
	dev = (struct _hcd_dev *) (urb->hcpriv);
	epnum = urb->ep->desc.bEndpointAddress;

	pehci_entry("++ %s, enter\n", __FUNCTION__);


	
	qh = (struct ehci_qh *) urb->ep->hcpriv;
#endif

	spin_lock_irqsave(&hcd->lock, flags);
	if (unlikely(qh	!= 0)) {
		if (!list_empty(&qh->qtd_list))	{
			*status	= -EBUSY;
			goto done;
		} else {
			td_ptd_map_buff_t *ptd_map_buff;
			td_ptd_map_t *td_ptd_map;
			ptd_map_buff = &(td_ptd_map_buff[qh->type]);
			td_ptd_map = &ptd_map_buff->map_list[qh->qtd_ptd_index];
			ptd_map_buff->pending_ptd_bitmap |=
				td_ptd_map->ptd_bitmap;
			  td_ptd_map->qtd = qtd;
			
			if (unlikely(!usb_gettoggle(urb->dev, epnum & 0x0f,
				!(epnum & 0x80)))) {

				
				td_ptd_map->datatoggle = 0;
				usb_settoggle(urb->dev,	epnum &	0x0f,
					!(epnum &	0x80), 1);
				qh->datatoggle = 0;
			}
			
			list_splice(qtd_list, &qh->qtd_list);
		}
	}


	if (!qh) {
		qh = phci_hcd_make_qh(hcd, urb,	qtd_list, status);
		if (likely(qh == 0)) {
			*status	= -ENOMEM;
			goto done;
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
		ep->hcpriv = qh;
#else
		urb->ep->hcpriv = qh;
#endif
	}

	if (likely(qh != 0)) {
		urb_priv->qh = qh;
		if (likely(qh->qh_state	== QH_STATE_IDLE)) {
			phci_hcd_qh_link_async(hcd, qh,	status);
		}
	}


	done:
	
	spin_unlock_irqrestore(&hcd->lock, flags);
	
	if (unlikely(qh	== 0) || *status < 0) {
		phci_hcd_qtd_list_free(hcd, urb, qtd_list);
		return qh;
	}
	return qh;
}




void *
phci_hcd_qha_from_qtd(phci_hcd * hcd,
	struct ehci_qtd *qtd,
	struct urb *urb,
	void *ptd, u32 ptd_data_addr, struct ehci_qh *qh)
{
	u8 toggle = qh->datatoggle;
	u32 token = 0;
	u32 td_info1 = 0;
	u32 td_info3 = 0;
	u32 td_info4 = 0;
	int maxpacket =	0;
	u32 length = 0,	temp = 0;
	
	u32 portnum = 0;
	u32 hubnum = 0;
	u32 se = 0, rl = 0x0, nk = 0x0;
	u8 datatoggle =	0;
	struct isp1763_mem_addr	*mem_addr = &qtd->mem_addr;
	u32 data_addr =	0;
	u32 multi = 0;
	struct _isp1763_qha *qha = (isp1763_qha	*) ptd;
	pehci_entry("++	%s: Entered\n",	__FUNCTION__);

	maxpacket = usb_maxpacket(urb->dev, urb->pipe, usb_pipeout(urb->pipe));

	multi =	1 + ((maxpacket	>> 11) & 0x3);

	maxpacket &= 0x7ff;

	
	length = qtd->length;
	td_info1 = QHA_VALID;
	td_info1 |= (length << 3);
	td_info1 |= (maxpacket << 18);
	td_info1 |= (usb_pipeendpoint(urb->pipe) << 31);
	td_info1 |= MULTI(multi);
	
	qha->td_info1 =	td_info1;

	pehci_print("%s: length	%d, 1st	word 0x%08x\n",	__FUNCTION__, length,
		    qha->td_info1);

	
	temp = qtd->hw_token;


	temp = temp & 0x0300;
	temp = temp >> 8;
	
	token =	(usb_pipeendpoint(urb->pipe) & 0xE) >> 1;
	token |= usb_pipedevice(urb->pipe) << 3;

	if (urb->dev->speed != USB_SPEED_HIGH) {
		pehci_print("device is full/low	speed, %d\n", urb->dev->speed);
		token |= 1 << 14;
		portnum	= urb->dev->ttport;
		  hubnum = urb->dev->tt->hub->devnum;
		token |= portnum << 18;
		token |= hubnum	<< 25;
		rl = 0x0;
		nk = 0x0;

	}

	
	if (urb->dev->speed == USB_SPEED_LOW) {
		se = 0x2;
	}

	if (usb_pipeint(urb->pipe)) {
		rl = 0x0;
	}

	token |= se << 16;
	
	token |= temp << 10;

	if (usb_pipebulk(urb->pipe)) {
		token |= EPTYPE_BULK;
	} else if (usb_pipeint(urb->pipe)) {
		token |= EPTYPE_INT;
	} else if (usb_pipeisoc(urb->pipe)) {
		token |= EPTYPE_ISO;
	}


	qha->td_info2 =	token;

	pehci_print("%s: second	word 0x%08x, qtd token 0x%08x\n",
		    __FUNCTION__, qha->td_info2, temp);

	

	

	data_addr = ((u32) (mem_addr->phy_addr)	& 0xffff) - 0x400;
	data_addr >>= 3;
	pehci_print("data start	address	%x\n", data_addr);
	if (length) {
		td_info3 = data_addr <<	8;
	}
	
	td_info3 |= (rl	<< 25);
	qha->td_info3 =	td_info3;

	pehci_print("%s: third word 0x%08x, tdinfo 0x%08x\n",
		__FUNCTION__, qha->td_info3, td_info3);


	

	if (usb_pipecontrol(urb->pipe))	{
		datatoggle = qtd->hw_token >> 31;
	} else {
		datatoggle = toggle;
	}

	td_info4 = QHA_ACTIVE;
	
	td_info4 |= datatoggle << 25;	
	
	if (PTD_PID(qha->td_info2) == SETUP_PID) {
		td_info4 |= (3 << 23);
	} else {
		td_info4 |= (0 << 23);
	}
	
	
	td_info4 |= (nk	<< 19);

	td_info4 |= (qh->ping << 26);
	qha->td_info4 =	td_info4;
#ifdef PTD_DUMP_SCHEDULE
	printk("SCHEDULE PTD DUMPE\n") ;
	printk("SDW0: 0x%08x\n",qha->td_info1);
	printk("SDW1: 0x%08x\n",qha->td_info2);
	printk("SDW2: 0x%08x\n",qha->td_info3);
	printk("SDW3: 0x%08x\n",qha->td_info4);
#endif
	pehci_print("%s: fourt word 0x%08x\n", __FUNCTION__, qha->td_info4);
	pehci_entry("--	%s: Exit, qha %p\n", __FUNCTION__, qha);
	return qha;

}
