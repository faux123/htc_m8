/*
 * Driver for the VoIP USB phones with CM109 chipsets.
 *
 * Copyright (C) 2007 - 2008 Alfred E. Heggestad <aeh@db.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rwsem.h>
#include <linux/usb/input.h>

#define DRIVER_VERSION "20080805"
#define DRIVER_AUTHOR  "Alfred E. Heggestad"
#define DRIVER_DESC    "CM109 phone driver"

static char *phone = "kip1000";
module_param(phone, charp, S_IRUSR);
MODULE_PARM_DESC(phone, "Phone name {kip1000, gtalk, usbph01, atcom}");

enum {
	
	HID_IR0 = 0x00, 
	HID_IR1 = 0x01, 
	HID_IR2 = 0x02, 
	HID_IR3 = 0x03, 
	HID_OR0 = 0x00, 
	HID_OR1 = 0x01, 
	HID_OR2 = 0x02, 
	HID_OR3 = 0x03, 

	
	RECORD_MUTE   = 1 << 3,
	PLAYBACK_MUTE = 1 << 2,
	VOLUME_DOWN   = 1 << 1,
	VOLUME_UP     = 1 << 0,

	
	/* bits 7-6
	   0: HID_OR1-2 are used for GPO; HID_OR0, 3 are used for buzzer
	      and SPDIF
	   1: HID_OR0-3 are used as generic HID registers
	   2: Values written to HID_OR0-3 are also mapped to MCU_CTRL,
	      EEPROM_DATA0-1, EEPROM_CTRL (see Note)
	   3: Reserved
	 */
	HID_OR_GPO_BUZ_SPDIF   = 0 << 6,
	HID_OR_GENERIC_HID_REG = 1 << 6,
	HID_OR_MAP_MCU_EEPROM  = 2 << 6,

	BUZZER_ON = 1 << 5,

	
	KEYMAP_SIZE = 256 + 16,
};

struct cm109_ctl_packet {
	u8 byte[4];
} __attribute__ ((packed));

enum { USB_PKT_LEN = sizeof(struct cm109_ctl_packet) };

struct cm109_dev {
	struct input_dev *idev;	 
	struct usb_device *udev; 
	struct usb_interface *intf;

	
	struct cm109_ctl_packet *irq_data;
	dma_addr_t irq_dma;
	struct urb *urb_irq;

	
	struct cm109_ctl_packet *ctl_data;
	dma_addr_t ctl_dma;
	struct usb_ctrlrequest *ctl_req;
	struct urb *urb_ctl;
	unsigned irq_urb_pending:1;	
	unsigned ctl_urb_pending:1;	
	unsigned buzzer_pending:1;	
	spinlock_t ctl_submit_lock;

	unsigned char buzzer_state;	

	
	unsigned open:1;
	unsigned resetting:1;
	unsigned shutdown:1;

	
	struct mutex pm_mutex;

	unsigned short keymap[KEYMAP_SIZE];

	char phys[64];		
	int key_code;		
	int keybit;		
	u8 gpi;			
};


static unsigned short special_keymap(int code)
{
	if (code > 0xff) {
		switch (code - 0xff) {
		case RECORD_MUTE:	return KEY_MUTE;
		case PLAYBACK_MUTE:	return KEY_MUTE;
		case VOLUME_DOWN:	return KEY_VOLUMEDOWN;
		case VOLUME_UP:		return KEY_VOLUMEUP;
		}
	}
	return KEY_RESERVED;
}

static unsigned short keymap_kip1000(int scancode)
{
	switch (scancode) {				
	case 0x82: return KEY_NUMERIC_0;		
	case 0x14: return KEY_NUMERIC_1;		
	case 0x12: return KEY_NUMERIC_2;		
	case 0x11: return KEY_NUMERIC_3;		
	case 0x24: return KEY_NUMERIC_4;		
	case 0x22: return KEY_NUMERIC_5;		
	case 0x21: return KEY_NUMERIC_6;		
	case 0x44: return KEY_NUMERIC_7;		
	case 0x42: return KEY_NUMERIC_8;		
	case 0x41: return KEY_NUMERIC_9;		
	case 0x81: return KEY_NUMERIC_POUND;		
	case 0x84: return KEY_NUMERIC_STAR;		
	case 0x88: return KEY_ENTER;			
	case 0x48: return KEY_ESC;			
	case 0x28: return KEY_LEFT;			
	case 0x18: return KEY_RIGHT;			
	default:   return special_keymap(scancode);
	}
}

static unsigned short keymap_gtalk(int scancode)
{
	switch (scancode) {
	case 0x11: return KEY_NUMERIC_0;
	case 0x21: return KEY_NUMERIC_1;
	case 0x41: return KEY_NUMERIC_2;
	case 0x81: return KEY_NUMERIC_3;
	case 0x12: return KEY_NUMERIC_4;
	case 0x22: return KEY_NUMERIC_5;
	case 0x42: return KEY_NUMERIC_6;
	case 0x82: return KEY_NUMERIC_7;
	case 0x14: return KEY_NUMERIC_8;
	case 0x24: return KEY_NUMERIC_9;
	case 0x44: return KEY_NUMERIC_POUND;	
	case 0x84: return KEY_NUMERIC_STAR;	
	case 0x18: return KEY_ENTER;		
	case 0x28: return KEY_ESC;		
	case 0x48: return KEY_UP;		
	case 0x88: return KEY_DOWN;		
	default:   return special_keymap(scancode);
	}
}

static unsigned short keymap_usbph01(int scancode)
{
	switch (scancode) {
	case 0x11: return KEY_NUMERIC_0;		
	case 0x21: return KEY_NUMERIC_1;		
	case 0x41: return KEY_NUMERIC_2;		
	case 0x81: return KEY_NUMERIC_3;		
	case 0x12: return KEY_NUMERIC_4;		
	case 0x22: return KEY_NUMERIC_5;		
	case 0x42: return KEY_NUMERIC_6;		
	case 0x82: return KEY_NUMERIC_7;		
	case 0x14: return KEY_NUMERIC_8;		
	case 0x24: return KEY_NUMERIC_9;		
	case 0x44: return KEY_NUMERIC_POUND;		
	case 0x84: return KEY_NUMERIC_STAR;		
	case 0x18: return KEY_ENTER;			
	case 0x28: return KEY_ESC;			
	case 0x48: return KEY_LEFT;			
	case 0x88: return KEY_RIGHT;			
	default:   return special_keymap(scancode);
	}
}

static unsigned short keymap_atcom(int scancode)
{
	switch (scancode) {				
	case 0x82: return KEY_NUMERIC_0;		
	case 0x11: return KEY_NUMERIC_1;		
	case 0x12: return KEY_NUMERIC_2;		
	case 0x14: return KEY_NUMERIC_3;		
	case 0x21: return KEY_NUMERIC_4;		
	case 0x22: return KEY_NUMERIC_5;		
	case 0x24: return KEY_NUMERIC_6;		
	case 0x41: return KEY_NUMERIC_7;		
	case 0x42: return KEY_NUMERIC_8;		
	case 0x44: return KEY_NUMERIC_9;		
	case 0x84: return KEY_NUMERIC_POUND;		
	case 0x81: return KEY_NUMERIC_STAR;		
	case 0x18: return KEY_ENTER;			
	case 0x28: return KEY_ESC;			
	case 0x48: return KEY_LEFT;			
	case 0x88: return KEY_RIGHT;			
	default:   return special_keymap(scancode);
	}
}

static unsigned short (*keymap)(int) = keymap_kip1000;

static void report_key(struct cm109_dev *dev, int key)
{
	struct input_dev *idev = dev->idev;

	if (dev->key_code >= 0) {
		
		input_report_key(idev, dev->key_code, 0);
	}

	dev->key_code = key;
	if (key >= 0) {
		
		input_report_key(idev, key, 1);
	}

	input_sync(idev);
}


static void cm109_submit_buzz_toggle(struct cm109_dev *dev)
{
	int error;

	if (dev->buzzer_state)
		dev->ctl_data->byte[HID_OR0] |= BUZZER_ON;
	else
		dev->ctl_data->byte[HID_OR0] &= ~BUZZER_ON;

	error = usb_submit_urb(dev->urb_ctl, GFP_ATOMIC);
	if (error)
		err("%s: usb_submit_urb (urb_ctl) failed %d", __func__, error);
}

static void cm109_urb_irq_callback(struct urb *urb)
{
	struct cm109_dev *dev = urb->context;
	const int status = urb->status;
	int error;

	dev_dbg(&urb->dev->dev, "### URB IRQ: [0x%02x 0x%02x 0x%02x 0x%02x] keybit=0x%02x\n",
	     dev->irq_data->byte[0],
	     dev->irq_data->byte[1],
	     dev->irq_data->byte[2],
	     dev->irq_data->byte[3],
	     dev->keybit);

	if (status) {
		if (status == -ESHUTDOWN)
			return;
		err("%s: urb status %d", __func__, status);
	}

	
	if (dev->irq_data->byte[HID_IR0] & 0x0f) {
		const int code = (dev->irq_data->byte[HID_IR0] & 0x0f);
		report_key(dev, dev->keymap[0xff + code]);
	}

	
	if (dev->keybit == 0xf) {

		
		if ((dev->gpi & 0xf0) == (dev->irq_data->byte[HID_IR1] & 0xf0))
			goto out;

		dev->gpi = dev->irq_data->byte[HID_IR1] & 0xf0;
		dev->keybit = 0x1;
	} else {
		report_key(dev, dev->keymap[dev->irq_data->byte[HID_IR1]]);

		dev->keybit <<= 1;
		if (dev->keybit > 0x8)
			dev->keybit = 0xf;
	}

 out:

	spin_lock(&dev->ctl_submit_lock);

	dev->irq_urb_pending = 0;

	if (likely(!dev->shutdown)) {

		if (dev->buzzer_state)
			dev->ctl_data->byte[HID_OR0] |= BUZZER_ON;
		else
			dev->ctl_data->byte[HID_OR0] &= ~BUZZER_ON;

		dev->ctl_data->byte[HID_OR1] = dev->keybit;
		dev->ctl_data->byte[HID_OR2] = dev->keybit;

		dev->buzzer_pending = 0;
		dev->ctl_urb_pending = 1;

		error = usb_submit_urb(dev->urb_ctl, GFP_ATOMIC);
		if (error)
			err("%s: usb_submit_urb (urb_ctl) failed %d",
				__func__, error);
	}

	spin_unlock(&dev->ctl_submit_lock);
}

static void cm109_urb_ctl_callback(struct urb *urb)
{
	struct cm109_dev *dev = urb->context;
	const int status = urb->status;
	int error;

	dev_dbg(&urb->dev->dev, "### URB CTL: [0x%02x 0x%02x 0x%02x 0x%02x]\n",
	     dev->ctl_data->byte[0],
	     dev->ctl_data->byte[1],
	     dev->ctl_data->byte[2],
	     dev->ctl_data->byte[3]);

	if (status)
		err("%s: urb status %d", __func__, status);

	spin_lock(&dev->ctl_submit_lock);

	dev->ctl_urb_pending = 0;

	if (likely(!dev->shutdown)) {

		if (dev->buzzer_pending) {
			dev->buzzer_pending = 0;
			dev->ctl_urb_pending = 1;
			cm109_submit_buzz_toggle(dev);
		} else if (likely(!dev->irq_urb_pending)) {
			
			dev->irq_urb_pending = 1;
			error = usb_submit_urb(dev->urb_irq, GFP_ATOMIC);
			if (error)
				err("%s: usb_submit_urb (urb_irq) failed %d",
					__func__, error);
		}
	}

	spin_unlock(&dev->ctl_submit_lock);
}

static void cm109_toggle_buzzer_async(struct cm109_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->ctl_submit_lock, flags);

	if (dev->ctl_urb_pending) {
		
		dev->buzzer_pending = 1;
	} else {
		dev->ctl_urb_pending = 1;
		cm109_submit_buzz_toggle(dev);
	}

	spin_unlock_irqrestore(&dev->ctl_submit_lock, flags);
}

static void cm109_toggle_buzzer_sync(struct cm109_dev *dev, int on)
{
	int error;

	if (on)
		dev->ctl_data->byte[HID_OR0] |= BUZZER_ON;
	else
		dev->ctl_data->byte[HID_OR0] &= ~BUZZER_ON;

	error = usb_control_msg(dev->udev,
				usb_sndctrlpipe(dev->udev, 0),
				dev->ctl_req->bRequest,
				dev->ctl_req->bRequestType,
				le16_to_cpu(dev->ctl_req->wValue),
				le16_to_cpu(dev->ctl_req->wIndex),
				dev->ctl_data,
				USB_PKT_LEN, USB_CTRL_SET_TIMEOUT);
	if (error < 0 && error != -EINTR)
		err("%s: usb_control_msg() failed %d", __func__, error);
}

static void cm109_stop_traffic(struct cm109_dev *dev)
{
	dev->shutdown = 1;
	smp_wmb();

	usb_kill_urb(dev->urb_ctl);
	usb_kill_urb(dev->urb_irq);

	cm109_toggle_buzzer_sync(dev, 0);

	dev->shutdown = 0;
	smp_wmb();
}

static void cm109_restore_state(struct cm109_dev *dev)
{
	if (dev->open) {
		cm109_toggle_buzzer_async(dev);
	}
}


static int cm109_input_open(struct input_dev *idev)
{
	struct cm109_dev *dev = input_get_drvdata(idev);
	int error;

	error = usb_autopm_get_interface(dev->intf);
	if (error < 0) {
		err("%s - cannot autoresume, result %d",
		    __func__, error);
		return error;
	}

	mutex_lock(&dev->pm_mutex);

	dev->buzzer_state = 0;
	dev->key_code = -1;	
	dev->keybit = 0xf;

	
	dev->ctl_data->byte[HID_OR0] = HID_OR_GPO_BUZ_SPDIF;
	dev->ctl_data->byte[HID_OR1] = dev->keybit;
	dev->ctl_data->byte[HID_OR2] = dev->keybit;
	dev->ctl_data->byte[HID_OR3] = 0x00;

	error = usb_submit_urb(dev->urb_ctl, GFP_KERNEL);
	if (error)
		err("%s: usb_submit_urb (urb_ctl) failed %d", __func__, error);
	else
		dev->open = 1;

	mutex_unlock(&dev->pm_mutex);

	if (error)
		usb_autopm_put_interface(dev->intf);

	return error;
}

static void cm109_input_close(struct input_dev *idev)
{
	struct cm109_dev *dev = input_get_drvdata(idev);

	mutex_lock(&dev->pm_mutex);

	cm109_stop_traffic(dev);
	dev->open = 0;

	mutex_unlock(&dev->pm_mutex);

	usb_autopm_put_interface(dev->intf);
}

static int cm109_input_ev(struct input_dev *idev, unsigned int type,
			  unsigned int code, int value)
{
	struct cm109_dev *dev = input_get_drvdata(idev);

	dev_dbg(&dev->udev->dev,
		"input_ev: type=%u code=%u value=%d\n", type, code, value);

	if (type != EV_SND)
		return -EINVAL;

	switch (code) {
	case SND_TONE:
	case SND_BELL:
		dev->buzzer_state = !!value;
		if (!dev->resetting)
			cm109_toggle_buzzer_async(dev);
		return 0;

	default:
		return -EINVAL;
	}
}



struct driver_info {
	char *name;
};

static const struct driver_info info_cm109 = {
	.name = "CM109 USB driver",
};

enum {
	VENDOR_ID        = 0x0d8c, 
	PRODUCT_ID_CM109 = 0x000e, 
};

static const struct usb_device_id cm109_usb_table[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_DEVICE |
				USB_DEVICE_ID_MATCH_INT_INFO,
		.idVendor = VENDOR_ID,
		.idProduct = PRODUCT_ID_CM109,
		.bInterfaceClass = USB_CLASS_HID,
		.bInterfaceSubClass = 0,
		.bInterfaceProtocol = 0,
		.driver_info = (kernel_ulong_t) &info_cm109
	},
	
	{ }
};

static void cm109_usb_cleanup(struct cm109_dev *dev)
{
	kfree(dev->ctl_req);
	if (dev->ctl_data)
		usb_free_coherent(dev->udev, USB_PKT_LEN,
				  dev->ctl_data, dev->ctl_dma);
	if (dev->irq_data)
		usb_free_coherent(dev->udev, USB_PKT_LEN,
				  dev->irq_data, dev->irq_dma);

	usb_free_urb(dev->urb_irq);	
	usb_free_urb(dev->urb_ctl);	
	kfree(dev);
}

static void cm109_usb_disconnect(struct usb_interface *interface)
{
	struct cm109_dev *dev = usb_get_intfdata(interface);

	usb_set_intfdata(interface, NULL);
	input_unregister_device(dev->idev);
	cm109_usb_cleanup(dev);
}

static int cm109_usb_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct driver_info *nfo = (struct driver_info *)id->driver_info;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct cm109_dev *dev;
	struct input_dev *input_dev = NULL;
	int ret, pipe, i;
	int error = -ENOMEM;

	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->ctl_submit_lock);
	mutex_init(&dev->pm_mutex);

	dev->udev = udev;
	dev->intf = intf;

	dev->idev = input_dev = input_allocate_device();
	if (!input_dev)
		goto err_out;

	
	dev->irq_data = usb_alloc_coherent(udev, USB_PKT_LEN,
					   GFP_KERNEL, &dev->irq_dma);
	if (!dev->irq_data)
		goto err_out;

	dev->ctl_data = usb_alloc_coherent(udev, USB_PKT_LEN,
					   GFP_KERNEL, &dev->ctl_dma);
	if (!dev->ctl_data)
		goto err_out;

	dev->ctl_req = kmalloc(sizeof(*(dev->ctl_req)), GFP_KERNEL);
	if (!dev->ctl_req)
		goto err_out;

	
	dev->urb_irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->urb_irq)
		goto err_out;

	dev->urb_ctl = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->urb_ctl)
		goto err_out;

	
	pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
	ret = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
	if (ret != USB_PKT_LEN)
		err("invalid payload size %d, expected %d", ret, USB_PKT_LEN);

	
	usb_fill_int_urb(dev->urb_irq, udev, pipe, dev->irq_data,
			 USB_PKT_LEN,
			 cm109_urb_irq_callback, dev, endpoint->bInterval);
	dev->urb_irq->transfer_dma = dev->irq_dma;
	dev->urb_irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	dev->urb_irq->dev = udev;

	
	dev->ctl_req->bRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE |
					USB_DIR_OUT;
	dev->ctl_req->bRequest = USB_REQ_SET_CONFIGURATION;
	dev->ctl_req->wValue = cpu_to_le16(0x200);
	dev->ctl_req->wIndex = cpu_to_le16(interface->desc.bInterfaceNumber);
	dev->ctl_req->wLength = cpu_to_le16(USB_PKT_LEN);

	usb_fill_control_urb(dev->urb_ctl, udev, usb_sndctrlpipe(udev, 0),
			     (void *)dev->ctl_req, dev->ctl_data, USB_PKT_LEN,
			     cm109_urb_ctl_callback, dev);
	dev->urb_ctl->transfer_dma = dev->ctl_dma;
	dev->urb_ctl->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	dev->urb_ctl->dev = udev;

	
	usb_make_path(udev, dev->phys, sizeof(dev->phys));
	strlcat(dev->phys, "/input0", sizeof(dev->phys));

	
	input_dev->name = nfo->name;
	input_dev->phys = dev->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, dev);
	input_dev->open = cm109_input_open;
	input_dev->close = cm109_input_close;
	input_dev->event = cm109_input_ev;

	input_dev->keycode = dev->keymap;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(dev->keymap);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_SND);
	input_dev->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);

	
	for (i = 0; i < KEYMAP_SIZE; i++) {
		unsigned short k = keymap(i);
		dev->keymap[i] = k;
		__set_bit(k, input_dev->keybit);
	}
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	error = input_register_device(dev->idev);
	if (error)
		goto err_out;

	usb_set_intfdata(intf, dev);

	return 0;

 err_out:
	input_free_device(input_dev);
	cm109_usb_cleanup(dev);
	return error;
}

static int cm109_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct cm109_dev *dev = usb_get_intfdata(intf);

	dev_info(&intf->dev, "cm109: usb_suspend (event=%d)\n", message.event);

	mutex_lock(&dev->pm_mutex);
	cm109_stop_traffic(dev);
	mutex_unlock(&dev->pm_mutex);

	return 0;
}

static int cm109_usb_resume(struct usb_interface *intf)
{
	struct cm109_dev *dev = usb_get_intfdata(intf);

	dev_info(&intf->dev, "cm109: usb_resume\n");

	mutex_lock(&dev->pm_mutex);
	cm109_restore_state(dev);
	mutex_unlock(&dev->pm_mutex);

	return 0;
}

static int cm109_usb_pre_reset(struct usb_interface *intf)
{
	struct cm109_dev *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->pm_mutex);

	dev->resetting = 1;
	smp_wmb();

	cm109_stop_traffic(dev);

	return 0;
}

static int cm109_usb_post_reset(struct usb_interface *intf)
{
	struct cm109_dev *dev = usb_get_intfdata(intf);

	dev->resetting = 0;
	smp_wmb();

	cm109_restore_state(dev);

	mutex_unlock(&dev->pm_mutex);

	return 0;
}

static struct usb_driver cm109_driver = {
	.name		= "cm109",
	.probe		= cm109_usb_probe,
	.disconnect	= cm109_usb_disconnect,
	.suspend	= cm109_usb_suspend,
	.resume		= cm109_usb_resume,
	.reset_resume	= cm109_usb_resume,
	.pre_reset	= cm109_usb_pre_reset,
	.post_reset	= cm109_usb_post_reset,
	.id_table	= cm109_usb_table,
	.supports_autosuspend = 1,
};

static int __init cm109_select_keymap(void)
{
	
	if (!strcasecmp(phone, "kip1000")) {
		keymap = keymap_kip1000;
		printk(KERN_INFO KBUILD_MODNAME ": "
			"Keymap for Komunikate KIP1000 phone loaded\n");
	} else if (!strcasecmp(phone, "gtalk")) {
		keymap = keymap_gtalk;
		printk(KERN_INFO KBUILD_MODNAME ": "
			"Keymap for Genius G-talk phone loaded\n");
	} else if (!strcasecmp(phone, "usbph01")) {
		keymap = keymap_usbph01;
		printk(KERN_INFO KBUILD_MODNAME ": "
			"Keymap for Allied-Telesis Corega USBPH01 phone loaded\n");
	} else if (!strcasecmp(phone, "atcom")) {
		keymap = keymap_atcom;
		printk(KERN_INFO KBUILD_MODNAME ": "
			"Keymap for ATCom AU-100 phone loaded\n");
	} else {
		printk(KERN_ERR KBUILD_MODNAME ": "
			"Unsupported phone: %s\n", phone);
		return -EINVAL;
	}

	return 0;
}

static int __init cm109_init(void)
{
	int err;

	err = cm109_select_keymap();
	if (err)
		return err;

	err = usb_register(&cm109_driver);
	if (err)
		return err;

	printk(KERN_INFO KBUILD_MODNAME ": "
		DRIVER_DESC ": " DRIVER_VERSION " (C) " DRIVER_AUTHOR "\n");

	return 0;
}

static void __exit cm109_exit(void)
{
	usb_deregister(&cm109_driver);
}

module_init(cm109_init);
module_exit(cm109_exit);

MODULE_DEVICE_TABLE(usb, cm109_usb_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
