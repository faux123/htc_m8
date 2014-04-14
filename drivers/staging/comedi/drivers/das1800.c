/*
    comedi/drivers/das1800.c
    Driver for Keitley das1700/das1800 series boards
    Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

************************************************************************
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "../comedidev.h"

#include <linux/ioport.h>
#include <asm/dma.h>

#include "8253.h"
#include "comedi_fc.h"

#define DAS1800_SIZE           16	
#define FIFO_SIZE              1024	
#define TIMER_BASE             200	
#define UNIPOLAR               0x4	
#define DMA_BUF_SIZE           0x1ff00	

#define DAS1800_FIFO            0x0
#define DAS1800_QRAM            0x0
#define DAS1800_DAC             0x0
#define DAS1800_SELECT          0x2
#define   ADC                     0x0
#define   QRAM                    0x1
#define   DAC(a)                  (0x2 + a)
#define DAS1800_DIGITAL         0x3
#define DAS1800_CONTROL_A       0x4
#define   FFEN                    0x1
#define   CGEN                    0x4
#define   CGSL                    0x8
#define   TGEN                    0x10
#define   TGSL                    0x20
#define   ATEN                    0x80
#define DAS1800_CONTROL_B       0x5
#define   DMA_CH5                 0x1
#define   DMA_CH6                 0x2
#define   DMA_CH7                 0x3
#define   DMA_CH5_CH6             0x5
#define   DMA_CH6_CH7             0x6
#define   DMA_CH7_CH5             0x7
#define   DMA_ENABLED             0x3	
#define   DMA_DUAL                0x4
#define   IRQ3                    0x8
#define   IRQ5                    0x10
#define   IRQ7                    0x18
#define   IRQ10                   0x28
#define   IRQ11                   0x30
#define   IRQ15                   0x38
#define   FIMD                    0x40
#define DAS1800_CONTROL_C       0X6
#define   IPCLK                   0x1
#define   XPCLK                   0x3
#define   BMDE                    0x4
#define   CMEN                    0x8
#define   UQEN                    0x10
#define   SD                      0x40
#define   UB                      0x80
#define DAS1800_STATUS          0x7
#define   CLEAR_INTR_MASK         (CVEN_MASK | 0x1f)
#define   INT                     0x1
#define   DMATC                   0x2
#define   CT0TC                   0x8
#define   OVF                     0x10
#define   FHF                     0x20
#define   FNE                     0x40
#define   CVEN_MASK               0x40	
#define   CVEN                    0x80
#define DAS1800_BURST_LENGTH    0x8
#define DAS1800_BURST_RATE      0x9
#define DAS1800_QRAM_ADDRESS    0xa
#define DAS1800_COUNTER         0xc

#define IOBASE2                   0x400	

enum {
	das1701st, das1701st_da, das1702st, das1702st_da, das1702hr,
	das1702hr_da,
	das1701ao, das1702ao, das1801st, das1801st_da, das1802st, das1802st_da,
	das1802hr, das1802hr_da, das1801hc, das1802hc, das1801ao, das1802ao
};

static int das1800_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int das1800_detach(struct comedi_device *dev);
static int das1800_probe(struct comedi_device *dev);
static int das1800_cancel(struct comedi_device *dev,
			  struct comedi_subdevice *s);
static irqreturn_t das1800_interrupt(int irq, void *d);
static int das1800_ai_poll(struct comedi_device *dev,
			   struct comedi_subdevice *s);
static void das1800_ai_handler(struct comedi_device *dev);
static void das1800_handle_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s, unsigned int status);
static void das1800_flush_dma(struct comedi_device *dev,
			      struct comedi_subdevice *s);
static void das1800_flush_dma_channel(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      unsigned int channel, uint16_t *buffer);
static void das1800_handle_fifo_half_full(struct comedi_device *dev,
					  struct comedi_subdevice *s);
static void das1800_handle_fifo_not_empty(struct comedi_device *dev,
					  struct comedi_subdevice *s);
static int das1800_ai_do_cmdtest(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_cmd *cmd);
static int das1800_ai_do_cmd(struct comedi_device *dev,
			     struct comedi_subdevice *s);
static int das1800_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);
static int das1800_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);
static int das1800_di_rbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);
static int das1800_do_wbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);

static int das1800_set_frequency(struct comedi_device *dev);
static unsigned int burst_convert_arg(unsigned int convert_arg, int round_mode);
static unsigned int suggest_transfer_size(struct comedi_cmd *cmd);

static const struct comedi_lrange range_ai_das1801 = {
	8,
	{
	 RANGE(-5, 5),
	 RANGE(-1, 1),
	 RANGE(-0.1, 0.1),
	 RANGE(-0.02, 0.02),
	 RANGE(0, 5),
	 RANGE(0, 1),
	 RANGE(0, 0.1),
	 RANGE(0, 0.02),
	 }
};

static const struct comedi_lrange range_ai_das1802 = {
	8,
	{
	 RANGE(-10, 10),
	 RANGE(-5, 5),
	 RANGE(-2.5, 2.5),
	 RANGE(-1.25, 1.25),
	 RANGE(0, 10),
	 RANGE(0, 5),
	 RANGE(0, 2.5),
	 RANGE(0, 1.25),
	 }
};

struct das1800_board {
	const char *name;
	int ai_speed;		
	int resolution;		
	int qram_len;		
	int common;		
	int do_n_chan;		
	int ao_ability;		
	int ao_n_chan;		
	const struct comedi_lrange *range_ai;	
};

static const struct das1800_board das1800_boards[] = {
	{
	 .name = "das-1701st",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1701st-da",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1702st",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1702st-da",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1702hr",
	 .ai_speed = 20000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1702hr-da",
	 .ai_speed = 20000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1701ao",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1702ao",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1801st",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1801st-da",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1802st",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1802st-da",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1802hr",
	 .ai_speed = 10000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1802hr-da",
	 .ai_speed = 10000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1801hc",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 64,
	 .common = 0,
	 .do_n_chan = 8,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1802hc",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 64,
	 .common = 0,
	 .do_n_chan = 8,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1801ao",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1802ao",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
};

#define thisboard ((const struct das1800_board *)dev->board_ptr)

struct das1800_private {
	volatile unsigned int count;	
	unsigned int divisor1;	
	unsigned int divisor2;	
	int do_bits;		
	int irq_dma_bits;	
	int dma_bits;
	unsigned int dma0;	
	unsigned int dma1;
	volatile unsigned int dma_current;	
	uint16_t *ai_buf0;	
	uint16_t *ai_buf1;
	uint16_t *dma_current_buf;	
	unsigned int dma_transfer_size;	
	unsigned long iobase2;	
	short ao_update_bits;	
};

#define devpriv ((struct das1800_private *)dev->private)

static const struct comedi_lrange range_ao_1 = {
	1,
	{
	 RANGE(-10, 10),
	 }
};


static struct comedi_driver driver_das1800 = {
	.driver_name = "das1800",
	.module = THIS_MODULE,
	.attach = das1800_attach,
	.detach = das1800_detach,
	.num_names = ARRAY_SIZE(das1800_boards),
	.board_name = &das1800_boards[0].name,
	.offset = sizeof(struct das1800_board),
};

static int __init driver_das1800_init_module(void)
{
	return comedi_driver_register(&driver_das1800);
}

static void __exit driver_das1800_cleanup_module(void)
{
	comedi_driver_unregister(&driver_das1800);
}

module_init(driver_das1800_init_module);
module_exit(driver_das1800_cleanup_module);

static int das1800_init_dma(struct comedi_device *dev, unsigned int dma0,
			    unsigned int dma1)
{
	unsigned long flags;

	
	if (dev->irq && dma0) {
		
		switch ((dma0 & 0x7) | (dma1 << 4)) {
		case 0x5:	
			devpriv->dma_bits |= DMA_CH5;
			break;
		case 0x6:	
			devpriv->dma_bits |= DMA_CH6;
			break;
		case 0x7:	
			devpriv->dma_bits |= DMA_CH7;
			break;
		case 0x65:	
			devpriv->dma_bits |= DMA_CH5_CH6;
			break;
		case 0x76:	
			devpriv->dma_bits |= DMA_CH6_CH7;
			break;
		case 0x57:	
			devpriv->dma_bits |= DMA_CH7_CH5;
			break;
		default:
			dev_err(dev->hw_dev, " only supports dma channels 5 through 7\n"
				" Dual dma only allows the following combinations:\n"
				" dma 5,6 / 6,7 / or 7,5\n");
			return -EINVAL;
			break;
		}
		if (request_dma(dma0, driver_das1800.driver_name)) {
			dev_err(dev->hw_dev, "failed to allocate dma channel %i\n",
				dma0);
			return -EINVAL;
		}
		devpriv->dma0 = dma0;
		devpriv->dma_current = dma0;
		if (dma1) {
			if (request_dma(dma1, driver_das1800.driver_name)) {
				dev_err(dev->hw_dev, "failed to allocate dma channel %i\n",
					dma1);
				return -EINVAL;
			}
			devpriv->dma1 = dma1;
		}
		devpriv->ai_buf0 = kmalloc(DMA_BUF_SIZE, GFP_KERNEL | GFP_DMA);
		if (devpriv->ai_buf0 == NULL)
			return -ENOMEM;
		devpriv->dma_current_buf = devpriv->ai_buf0;
		if (dma1) {
			devpriv->ai_buf1 =
			    kmalloc(DMA_BUF_SIZE, GFP_KERNEL | GFP_DMA);
			if (devpriv->ai_buf1 == NULL)
				return -ENOMEM;
		}
		flags = claim_dma_lock();
		disable_dma(devpriv->dma0);
		set_dma_mode(devpriv->dma0, DMA_MODE_READ);
		if (dma1) {
			disable_dma(devpriv->dma1);
			set_dma_mode(devpriv->dma1, DMA_MODE_READ);
		}
		release_dma_lock(flags);
	}
	return 0;
}

static int das1800_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = it->options[0];
	unsigned int irq = it->options[1];
	unsigned int dma0 = it->options[2];
	unsigned int dma1 = it->options[3];
	unsigned long iobase2;
	int board;
	int retval;

	
	if (alloc_private(dev, sizeof(struct das1800_private)) < 0)
		return -ENOMEM;

	printk(KERN_DEBUG "comedi%d: %s: io 0x%lx", dev->minor,
	       driver_das1800.driver_name, iobase);
	if (irq) {
		printk(KERN_CONT ", irq %u", irq);
		if (dma0) {
			printk(KERN_CONT ", dma %u", dma0);
			if (dma1)
				printk(KERN_CONT " and %u", dma1);
		}
	}
	printk(KERN_CONT "\n");

	if (iobase == 0) {
		dev_err(dev->hw_dev, "io base address required\n");
		return -EINVAL;
	}

	
	if (!request_region(iobase, DAS1800_SIZE, driver_das1800.driver_name)) {
		printk
		    (" I/O port conflict: failed to allocate ports 0x%lx to 0x%lx\n",
		     iobase, iobase + DAS1800_SIZE - 1);
		return -EIO;
	}
	dev->iobase = iobase;

	board = das1800_probe(dev);
	if (board < 0) {
		dev_err(dev->hw_dev, "unable to determine board type\n");
		return -ENODEV;
	}

	dev->board_ptr = das1800_boards + board;
	dev->board_name = thisboard->name;

	
	if (thisboard->ao_ability == 2) {
		iobase2 = iobase + IOBASE2;
		if (!request_region(iobase2, DAS1800_SIZE,
				    driver_das1800.driver_name)) {
			printk
			    (" I/O port conflict: failed to allocate ports 0x%lx to 0x%lx\n",
			     iobase2, iobase2 + DAS1800_SIZE - 1);
			return -EIO;
		}
		devpriv->iobase2 = iobase2;
	}

	
	if (irq) {
		if (request_irq(irq, das1800_interrupt, 0,
				driver_das1800.driver_name, dev)) {
			dev_dbg(dev->hw_dev, "unable to allocate irq %u\n",
				irq);
			return -EINVAL;
		}
	}
	dev->irq = irq;

	
	switch (irq) {
	case 0:
		break;
	case 3:
		devpriv->irq_dma_bits |= 0x8;
		break;
	case 5:
		devpriv->irq_dma_bits |= 0x10;
		break;
	case 7:
		devpriv->irq_dma_bits |= 0x18;
		break;
	case 10:
		devpriv->irq_dma_bits |= 0x28;
		break;
	case 11:
		devpriv->irq_dma_bits |= 0x30;
		break;
	case 15:
		devpriv->irq_dma_bits |= 0x38;
		break;
	default:
		dev_err(dev->hw_dev, "irq out of range\n");
		return -EINVAL;
		break;
	}

	retval = das1800_init_dma(dev, dma0, dma1);
	if (retval < 0)
		return retval;

	if (devpriv->ai_buf0 == NULL) {
		devpriv->ai_buf0 =
		    kmalloc(FIFO_SIZE * sizeof(uint16_t), GFP_KERNEL);
		if (devpriv->ai_buf0 == NULL)
			return -ENOMEM;
	}

	if (alloc_subdevices(dev, 4) < 0)
		return -ENOMEM;

	
	s = dev->subdevices + 0;
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_DIFF | SDF_GROUND | SDF_CMD_READ;
	if (thisboard->common)
		s->subdev_flags |= SDF_COMMON;
	s->n_chan = thisboard->qram_len;
	s->len_chanlist = thisboard->qram_len;
	s->maxdata = (1 << thisboard->resolution) - 1;
	s->range_table = thisboard->range_ai;
	s->do_cmd = das1800_ai_do_cmd;
	s->do_cmdtest = das1800_ai_do_cmdtest;
	s->insn_read = das1800_ai_rinsn;
	s->poll = das1800_ai_poll;
	s->cancel = das1800_cancel;

	
	s = dev->subdevices + 1;
	if (thisboard->ao_ability == 1) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = thisboard->ao_n_chan;
		s->maxdata = (1 << thisboard->resolution) - 1;
		s->range_table = &range_ao_1;
		s->insn_write = das1800_ao_winsn;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	
	s = dev->subdevices + 2;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das1800_di_rbits;

	
	s = dev->subdevices + 3;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = thisboard->do_n_chan;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das1800_do_wbits;

	das1800_cancel(dev, dev->read_subdev);

	
	outb(devpriv->do_bits, dev->iobase + DAS1800_DIGITAL);

	
	if (thisboard->ao_ability == 1) {
		
		outb(DAC(thisboard->ao_n_chan - 1),
		     dev->iobase + DAS1800_SELECT);
		outw(devpriv->ao_update_bits, dev->iobase + DAS1800_DAC);
	}

	return 0;
};

static int das1800_detach(struct comedi_device *dev)
{
	
	if (dev->iobase)
		release_region(dev->iobase, DAS1800_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->private) {
		if (devpriv->iobase2)
			release_region(devpriv->iobase2, DAS1800_SIZE);
		if (devpriv->dma0)
			free_dma(devpriv->dma0);
		if (devpriv->dma1)
			free_dma(devpriv->dma1);
		kfree(devpriv->ai_buf0);
		kfree(devpriv->ai_buf1);
	}

	dev_dbg(dev->hw_dev, "comedi%d: %s: remove\n", dev->minor,
		driver_das1800.driver_name);

	return 0;
};

static int das1800_probe(struct comedi_device *dev)
{
	int id;
	int board;

	id = (inb(dev->iobase + DAS1800_DIGITAL) >> 4) & 0xf;	
	board = ((struct das1800_board *)dev->board_ptr) - das1800_boards;

	switch (id) {
	case 0x3:
		if (board == das1801st_da || board == das1802st_da ||
		    board == das1701st_da || board == das1702st_da) {
			dev_dbg(dev->hw_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800st-da series\n");
		return das1801st;
		break;
	case 0x4:
		if (board == das1802hr_da || board == das1702hr_da) {
			dev_dbg(dev->hw_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1802hr-da\n");
		return das1802hr;
		break;
	case 0x5:
		if (board == das1801ao || board == das1802ao ||
		    board == das1701ao || board == das1702ao) {
			dev_dbg(dev->hw_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800ao series\n");
		return das1801ao;
		break;
	case 0x6:
		if (board == das1802hr || board == das1702hr) {
			dev_dbg(dev->hw_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1802hr\n");
		return das1802hr;
		break;
	case 0x7:
		if (board == das1801st || board == das1802st ||
		    board == das1701st || board == das1702st) {
			dev_dbg(dev->hw_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800st series\n");
		return das1801st;
		break;
	case 0x8:
		if (board == das1801hc || board == das1802hc) {
			dev_dbg(dev->hw_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800hc series\n");
		return das1801hc;
		break;
	default:
		printk
		    (" Board model: probe returned 0x%x (unknown, please report)\n",
		     id);
		return board;
		break;
	}
	return -1;
}

static int das1800_ai_poll(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	unsigned long flags;

	
	spin_lock_irqsave(&dev->spinlock, flags);
	das1800_ai_handler(dev);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return s->async->buf_write_count - s->async->buf_read_count;
}

static irqreturn_t das1800_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned int status;

	if (dev->attached == 0) {
		comedi_error(dev, "premature interrupt");
		return IRQ_HANDLED;
	}

	spin_lock(&dev->spinlock);
	status = inb(dev->iobase + DAS1800_STATUS);

	
	if (!(status & INT)) {
		spin_unlock(&dev->spinlock);
		return IRQ_NONE;
	}
	
	outb(CLEAR_INTR_MASK & ~INT, dev->iobase + DAS1800_STATUS);
	
	das1800_ai_handler(dev);

	spin_unlock(&dev->spinlock);
	return IRQ_HANDLED;
}

static void das1800_ai_handler(struct comedi_device *dev)
{
	struct comedi_subdevice *s = dev->subdevices + 0;	
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status = inb(dev->iobase + DAS1800_STATUS);

	async->events = 0;
	
	outb(ADC, dev->iobase + DAS1800_SELECT);
	
	if (devpriv->irq_dma_bits & DMA_ENABLED) {
		
		das1800_handle_dma(dev, s, status);
	} else if (status & FHF) {	
		das1800_handle_fifo_half_full(dev, s);
	} else if (status & FNE) {	
		das1800_handle_fifo_not_empty(dev, s);
	}

	async->events |= COMEDI_CB_BLOCK;
	
	if (status & OVF) {
		
		outb(CLEAR_INTR_MASK & ~OVF, dev->iobase + DAS1800_STATUS);
		comedi_error(dev, "DAS1800 FIFO overflow");
		das1800_cancel(dev, s);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		return;
	}
	
	
	if (status & CT0TC) {
		
		outb(CLEAR_INTR_MASK & ~CT0TC, dev->iobase + DAS1800_STATUS);
		
		if (devpriv->irq_dma_bits & DMA_ENABLED)
			das1800_flush_dma(dev, s);
		else
			das1800_handle_fifo_not_empty(dev, s);
		das1800_cancel(dev, s);	
		async->events |= COMEDI_CB_EOA;
	} else if (cmd->stop_src == TRIG_COUNT && devpriv->count == 0) {	
		das1800_cancel(dev, s);	
		async->events |= COMEDI_CB_EOA;
	}

	comedi_event(dev, s);

	return;
}

static void das1800_handle_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s, unsigned int status)
{
	unsigned long flags;
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	flags = claim_dma_lock();
	das1800_flush_dma_channel(dev, s, devpriv->dma_current,
				  devpriv->dma_current_buf);
	
	set_dma_addr(devpriv->dma_current,
		     virt_to_bus(devpriv->dma_current_buf));
	set_dma_count(devpriv->dma_current, devpriv->dma_transfer_size);
	enable_dma(devpriv->dma_current);
	release_dma_lock(flags);

	if (status & DMATC) {
		
		outb(CLEAR_INTR_MASK & ~DMATC, dev->iobase + DAS1800_STATUS);
		
		if (dual_dma) {
			
			if (devpriv->dma_current == devpriv->dma0) {
				devpriv->dma_current = devpriv->dma1;
				devpriv->dma_current_buf = devpriv->ai_buf1;
			} else {
				devpriv->dma_current = devpriv->dma0;
				devpriv->dma_current_buf = devpriv->ai_buf0;
			}
		}
	}

	return;
}

static inline uint16_t munge_bipolar_sample(const struct comedi_device *dev,
					    uint16_t sample)
{
	sample += 1 << (thisboard->resolution - 1);
	return sample;
}

static void munge_data(struct comedi_device *dev, uint16_t * array,
		       unsigned int num_elements)
{
	unsigned int i;
	int unipolar;

	
	unipolar = inb(dev->iobase + DAS1800_CONTROL_C) & UB;

	
	if (!unipolar) {
		for (i = 0; i < num_elements; i++)
			array[i] = munge_bipolar_sample(dev, array[i]);
	}
}

static void das1800_flush_dma_channel(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      unsigned int channel, uint16_t *buffer)
{
	unsigned int num_bytes, num_samples;
	struct comedi_cmd *cmd = &s->async->cmd;

	disable_dma(channel);

	clear_dma_ff(channel);

	
	num_bytes = devpriv->dma_transfer_size - get_dma_residue(channel);
	num_samples = num_bytes / sizeof(short);

	
	if (cmd->stop_src == TRIG_COUNT && devpriv->count < num_samples)
		num_samples = devpriv->count;

	munge_data(dev, buffer, num_samples);
	cfc_write_array_to_buffer(s, buffer, num_bytes);
	if (s->async->cmd.stop_src == TRIG_COUNT)
		devpriv->count -= num_samples;

	return;
}

static void das1800_flush_dma(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	unsigned long flags;
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	flags = claim_dma_lock();
	das1800_flush_dma_channel(dev, s, devpriv->dma_current,
				  devpriv->dma_current_buf);

	if (dual_dma) {
		
		if (devpriv->dma_current == devpriv->dma0) {
			devpriv->dma_current = devpriv->dma1;
			devpriv->dma_current_buf = devpriv->ai_buf1;
		} else {
			devpriv->dma_current = devpriv->dma0;
			devpriv->dma_current_buf = devpriv->ai_buf0;
		}
		das1800_flush_dma_channel(dev, s, devpriv->dma_current,
					  devpriv->dma_current_buf);
	}

	release_dma_lock(flags);

	
	das1800_handle_fifo_not_empty(dev, s);

	return;
}

static void das1800_handle_fifo_half_full(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	int numPoints = 0;	
	struct comedi_cmd *cmd = &s->async->cmd;

	numPoints = FIFO_SIZE / 2;
	
	if (cmd->stop_src == TRIG_COUNT && devpriv->count < numPoints)
		numPoints = devpriv->count;
	insw(dev->iobase + DAS1800_FIFO, devpriv->ai_buf0, numPoints);
	munge_data(dev, devpriv->ai_buf0, numPoints);
	cfc_write_array_to_buffer(s, devpriv->ai_buf0,
				  numPoints * sizeof(devpriv->ai_buf0[0]));
	if (cmd->stop_src == TRIG_COUNT)
		devpriv->count -= numPoints;
	return;
}

static void das1800_handle_fifo_not_empty(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	short dpnt;
	int unipolar;
	struct comedi_cmd *cmd = &s->async->cmd;

	unipolar = inb(dev->iobase + DAS1800_CONTROL_C) & UB;

	while (inb(dev->iobase + DAS1800_STATUS) & FNE) {
		if (cmd->stop_src == TRIG_COUNT && devpriv->count == 0)
			break;
		dpnt = inw(dev->iobase + DAS1800_FIFO);
		
		if (!unipolar)
			;
		dpnt = munge_bipolar_sample(dev, dpnt);
		cfc_write_to_buffer(s, dpnt);
		if (cmd->stop_src == TRIG_COUNT)
			devpriv->count--;
	}

	return;
}

static int das1800_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	outb(0x0, dev->iobase + DAS1800_STATUS);	
	outb(0x0, dev->iobase + DAS1800_CONTROL_B);	
	outb(0x0, dev->iobase + DAS1800_CONTROL_A);	
	if (devpriv->dma0)
		disable_dma(devpriv->dma0);
	if (devpriv->dma1)
		disable_dma(devpriv->dma1);
	return 0;
}

static int das1800_ai_do_cmdtest(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;
	unsigned int tmp_arg;
	int i;
	int unipolar;

	

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW | TRIG_EXT;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_FOLLOW | TRIG_TIMER | TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_EXT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	

	
	if (cmd->start_src != TRIG_NOW && cmd->start_src != TRIG_EXT)
		err++;
	if (cmd->scan_begin_src != TRIG_FOLLOW &&
	    cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->scan_begin_src != TRIG_EXT)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
		err++;
	if (cmd->stop_src != TRIG_COUNT &&
	    cmd->stop_src != TRIG_NONE && cmd->stop_src != TRIG_EXT)
		err++;
	
	if (cmd->scan_begin_src != TRIG_FOLLOW &&
	    cmd->convert_src != TRIG_TIMER)
		err++;

	if (err)
		return 2;

	

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < thisboard->ai_speed) {
			cmd->convert_arg = thisboard->ai_speed;
			err++;
		}
	}
	if (!cmd->chanlist_len) {
		cmd->chanlist_len = 1;
		err++;
	}
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
		break;
	case TRIG_NONE:
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
		break;
	default:
		break;
	}

	if (err)
		return 3;

	

	if (cmd->convert_src == TRIG_TIMER) {
		
		if (cmd->scan_begin_src == TRIG_FOLLOW) {
			tmp_arg = cmd->convert_arg;
			
			i8253_cascade_ns_to_timer_2div(TIMER_BASE,
						       &(devpriv->divisor1),
						       &(devpriv->divisor2),
						       &(cmd->convert_arg),
						       cmd->
						       flags & TRIG_ROUND_MASK);
			if (tmp_arg != cmd->convert_arg)
				err++;
		}
		
		else {
			
			tmp_arg = cmd->convert_arg;
			cmd->convert_arg =
			    burst_convert_arg(cmd->convert_arg,
					      cmd->flags & TRIG_ROUND_MASK);
			if (tmp_arg != cmd->convert_arg)
				err++;

			if (cmd->scan_begin_src == TRIG_TIMER) {
				
				if (cmd->convert_arg * cmd->chanlist_len >
				    cmd->scan_begin_arg) {
					cmd->scan_begin_arg =
					    cmd->convert_arg *
					    cmd->chanlist_len;
					err++;
				}
				tmp_arg = cmd->scan_begin_arg;
				
				i8253_cascade_ns_to_timer_2div(TIMER_BASE,
							       &(devpriv->
								 divisor1),
							       &(devpriv->
								 divisor2),
							       &(cmd->
								 scan_begin_arg),
							       cmd->
							       flags &
							       TRIG_ROUND_MASK);
				if (tmp_arg != cmd->scan_begin_arg)
					err++;
			}
		}
	}

	if (err)
		return 4;

	
	if (cmd->chanlist) {
		unipolar = CR_RANGE(cmd->chanlist[0]) & UNIPOLAR;
		for (i = 1; i < cmd->chanlist_len; i++) {
			if (unipolar != (CR_RANGE(cmd->chanlist[i]) & UNIPOLAR)) {
				comedi_error(dev,
					     "unipolar and bipolar ranges cannot be mixed in the chanlist");
				err++;
				break;
			}
		}
	}

	if (err)
		return 5;

	return 0;
}



static int control_a_bits(struct comedi_cmd cmd)
{
	int control_a;

	control_a = FFEN;	
	if (cmd.stop_src == TRIG_EXT)
		control_a |= ATEN;
	switch (cmd.start_src) {
	case TRIG_EXT:
		control_a |= TGEN | CGSL;
		break;
	case TRIG_NOW:
		control_a |= CGEN;
		break;
	default:
		break;
	}

	return control_a;
}

static int control_c_bits(struct comedi_cmd cmd)
{
	int control_c;
	int aref;

	aref = CR_AREF(cmd.chanlist[0]);
	control_c = UQEN;	
	if (aref != AREF_DIFF)
		control_c |= SD;
	if (aref == AREF_COMMON)
		control_c |= CMEN;
	
	if (CR_RANGE(cmd.chanlist[0]) & UNIPOLAR)
		control_c |= UB;
	switch (cmd.scan_begin_src) {
	case TRIG_FOLLOW:	
		switch (cmd.convert_src) {
		case TRIG_TIMER:
			
			control_c |= IPCLK;
			break;
		case TRIG_EXT:
			
			control_c |= XPCLK;
			break;
		default:
			break;
		}
		break;
	case TRIG_TIMER:
		
		control_c |= BMDE | IPCLK;
		break;
	case TRIG_EXT:
		
		control_c |= BMDE | XPCLK;
		break;
	default:
		break;
	}

	return control_c;
}

static int setup_counters(struct comedi_device *dev, struct comedi_cmd cmd)
{
	
	switch (cmd.scan_begin_src) {
	case TRIG_FOLLOW:	
		if (cmd.convert_src == TRIG_TIMER) {
			
			i8253_cascade_ns_to_timer_2div(TIMER_BASE,
						       &(devpriv->divisor1),
						       &(devpriv->divisor2),
						       &(cmd.convert_arg),
						       cmd.
						       flags & TRIG_ROUND_MASK);
			if (das1800_set_frequency(dev) < 0)
				return -1;
		}
		break;
	case TRIG_TIMER:	
		
		i8253_cascade_ns_to_timer_2div(TIMER_BASE, &(devpriv->divisor1),
					       &(devpriv->divisor2),
					       &(cmd.scan_begin_arg),
					       cmd.flags & TRIG_ROUND_MASK);
		if (das1800_set_frequency(dev) < 0)
			return -1;
		break;
	default:
		break;
	}

	
	if (cmd.stop_src == TRIG_EXT) {
		
		i8254_load(dev->iobase + DAS1800_COUNTER, 0, 0, 1, 0);
	}

	return 0;
}

static void setup_dma(struct comedi_device *dev, struct comedi_cmd cmd)
{
	unsigned long lock_flags;
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	if ((devpriv->irq_dma_bits & DMA_ENABLED) == 0)
		return;

	
	devpriv->dma_transfer_size = suggest_transfer_size(&cmd);
	lock_flags = claim_dma_lock();
	disable_dma(devpriv->dma0);
	clear_dma_ff(devpriv->dma0);
	set_dma_addr(devpriv->dma0, virt_to_bus(devpriv->ai_buf0));
	
	set_dma_count(devpriv->dma0, devpriv->dma_transfer_size);
	devpriv->dma_current = devpriv->dma0;
	devpriv->dma_current_buf = devpriv->ai_buf0;
	enable_dma(devpriv->dma0);
	
	if (dual_dma) {
		disable_dma(devpriv->dma1);
		clear_dma_ff(devpriv->dma1);
		set_dma_addr(devpriv->dma1, virt_to_bus(devpriv->ai_buf1));
		
		set_dma_count(devpriv->dma1, devpriv->dma_transfer_size);
		enable_dma(devpriv->dma1);
	}
	release_dma_lock(lock_flags);

	return;
}

static void program_chanlist(struct comedi_device *dev, struct comedi_cmd cmd)
{
	int i, n, chan_range;
	unsigned long irq_flags;
	const int range_mask = 0x3;	
	const int range_bitshift = 8;

	n = cmd.chanlist_len;
	
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(QRAM, dev->iobase + DAS1800_SELECT);	
	outb(n - 1, dev->iobase + DAS1800_QRAM_ADDRESS);	
	
	for (i = 0; i < n; i++) {
		chan_range =
		    CR_CHAN(cmd.
			    chanlist[i]) | ((CR_RANGE(cmd.chanlist[i]) &
					     range_mask) << range_bitshift);
		outw(chan_range, dev->iobase + DAS1800_QRAM);
	}
	outb(n - 1, dev->iobase + DAS1800_QRAM_ADDRESS);	
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return;
}

static int das1800_ai_do_cmd(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	int ret;
	int control_a, control_c;
	struct comedi_async *async = s->async;
	struct comedi_cmd cmd = async->cmd;

	if (!dev->irq) {
		comedi_error(dev,
			     "no irq assigned for das-1800, cannot do hardware conversions");
		return -1;
	}

	if (cmd.flags & (TRIG_WAKE_EOS | TRIG_RT))
		devpriv->irq_dma_bits &= ~DMA_ENABLED;
	else
		devpriv->irq_dma_bits |= devpriv->dma_bits;
	
	if (cmd.flags & TRIG_WAKE_EOS) {
		
		devpriv->irq_dma_bits &= ~FIMD;
	} else {
		
		devpriv->irq_dma_bits |= FIMD;
	}
	
	if (cmd.stop_src == TRIG_COUNT)
		devpriv->count = cmd.stop_arg * cmd.chanlist_len;

	das1800_cancel(dev, s);

	
	control_a = control_a_bits(cmd);
	control_c = control_c_bits(cmd);

	
	program_chanlist(dev, cmd);
	ret = setup_counters(dev, cmd);
	if (ret < 0) {
		comedi_error(dev, "Error setting up counters");
		return ret;
	}
	setup_dma(dev, cmd);
	outb(control_c, dev->iobase + DAS1800_CONTROL_C);
	
	if (control_c & BMDE) {
		
		outb(cmd.convert_arg / 1000 - 1,
		     dev->iobase + DAS1800_BURST_RATE);
		outb(cmd.chanlist_len - 1, dev->iobase + DAS1800_BURST_LENGTH);
	}
	outb(devpriv->irq_dma_bits, dev->iobase + DAS1800_CONTROL_B);	
	outb(control_a, dev->iobase + DAS1800_CONTROL_A);	
	outb(CVEN, dev->iobase + DAS1800_STATUS);	

	return 0;
}

static int das1800_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	int i, n;
	int chan, range, aref, chan_range;
	int timeout = 1000;
	short dpnt;
	int conv_flags = 0;
	unsigned long irq_flags;

	
	aref = CR_AREF(insn->chanspec);
	conv_flags |= UQEN;
	if (aref != AREF_DIFF)
		conv_flags |= SD;
	if (aref == AREF_COMMON)
		conv_flags |= CMEN;
	
	if (CR_RANGE(insn->chanspec) & UNIPOLAR)
		conv_flags |= UB;

	outb(conv_flags, dev->iobase + DAS1800_CONTROL_C);	
	outb(CVEN, dev->iobase + DAS1800_STATUS);	
	outb(0x0, dev->iobase + DAS1800_CONTROL_A);	
	outb(FFEN, dev->iobase + DAS1800_CONTROL_A);

	chan = CR_CHAN(insn->chanspec);
	
	range = CR_RANGE(insn->chanspec) & 0x3;
	chan_range = chan | (range << 8);
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(QRAM, dev->iobase + DAS1800_SELECT);	
	outb(0x0, dev->iobase + DAS1800_QRAM_ADDRESS);	
	outw(chan_range, dev->iobase + DAS1800_QRAM);
	outb(0x0, dev->iobase + DAS1800_QRAM_ADDRESS);	
	outb(ADC, dev->iobase + DAS1800_SELECT);	

	for (n = 0; n < insn->n; n++) {
		
		outb(0, dev->iobase + DAS1800_FIFO);
		for (i = 0; i < timeout; i++) {
			if (inb(dev->iobase + DAS1800_STATUS) & FNE)
				break;
		}
		if (i == timeout) {
			comedi_error(dev, "timeout");
			n = -ETIME;
			goto exit;
		}
		dpnt = inw(dev->iobase + DAS1800_FIFO);
		
		if ((conv_flags & UB) == 0)
			dpnt += 1 << (thisboard->resolution - 1);
		data[n] = dpnt;
	}
exit:
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return n;
}

static int das1800_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int update_chan = thisboard->ao_n_chan - 1;
	short output;
	unsigned long irq_flags;

	
	output = data[0] - (1 << (thisboard->resolution - 1));
	
	if (chan == update_chan)
		devpriv->ao_update_bits = output;
	
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(DAC(chan), dev->iobase + DAS1800_SELECT);	
	outw(output, dev->iobase + DAS1800_DAC);
	
	if (chan != update_chan) {
		outb(DAC(update_chan), dev->iobase + DAS1800_SELECT);	
		outw(devpriv->ao_update_bits, dev->iobase + DAS1800_DAC);
	}
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return 1;
}

static int das1800_di_rbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{

	data[1] = inb(dev->iobase + DAS1800_DIGITAL) & 0xf;
	data[0] = 0;

	return 2;
}

static int das1800_do_wbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	unsigned int wbits;

	
	data[0] &= (1 << s->n_chan) - 1;
	wbits = devpriv->do_bits;
	wbits &= ~data[0];
	wbits |= data[0] & data[1];
	devpriv->do_bits = wbits;

	outb(devpriv->do_bits, dev->iobase + DAS1800_DIGITAL);

	data[1] = devpriv->do_bits;

	return 2;
}

static int das1800_set_frequency(struct comedi_device *dev)
{
	int err = 0;

	
	if (i8254_load(dev->iobase + DAS1800_COUNTER, 0, 1, devpriv->divisor1,
		       2))
		err++;
	
	if (i8254_load(dev->iobase + DAS1800_COUNTER, 0, 2, devpriv->divisor2,
		       2))
		err++;
	if (err)
		return -1;

	return 0;
}

static unsigned int burst_convert_arg(unsigned int convert_arg, int round_mode)
{
	unsigned int micro_sec;

	
	if (convert_arg > 64000)
		convert_arg = 64000;

	
	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		micro_sec = (convert_arg + 500) / 1000;
		break;
	case TRIG_ROUND_DOWN:
		micro_sec = convert_arg / 1000;
		break;
	case TRIG_ROUND_UP:
		micro_sec = (convert_arg - 1) / 1000 + 1;
		break;
	}

	
	return micro_sec * 1000;
}

static unsigned int suggest_transfer_size(struct comedi_cmd *cmd)
{
	unsigned int size = DMA_BUF_SIZE;
	static const int sample_size = 2;	
	unsigned int fill_time = 300000000;	
	unsigned int max_size;	

	
	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:	
		if (cmd->convert_src == TRIG_TIMER)
			size = (fill_time / cmd->convert_arg) * sample_size;
		break;
	case TRIG_TIMER:
		size = (fill_time / (cmd->scan_begin_arg * cmd->chanlist_len)) *
		    sample_size;
		break;
	default:
		size = DMA_BUF_SIZE;
		break;
	}

	
	max_size = DMA_BUF_SIZE;
	
	if (cmd->stop_src == TRIG_COUNT &&
	    cmd->stop_arg * cmd->chanlist_len * sample_size < max_size)
		max_size = cmd->stop_arg * cmd->chanlist_len * sample_size;

	if (size > max_size)
		size = max_size;
	if (size < sample_size)
		size = sample_size;

	return size;
}

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
