/*
 * ti113x.h 1.16 1999/10/25 20:03:34
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_TI113X_H
#define _LINUX_TI113X_H



#define TI113X_SYSTEM_CONTROL		0x0080	
#define  TI113X_SCR_SMIROUTE		0x04000000
#define  TI113X_SCR_SMISTATUS		0x02000000
#define  TI113X_SCR_SMIENB		0x01000000
#define  TI113X_SCR_VCCPROT		0x00200000
#define  TI113X_SCR_REDUCEZV		0x00100000
#define  TI113X_SCR_CDREQEN		0x00080000
#define  TI113X_SCR_CDMACHAN		0x00070000
#define  TI113X_SCR_SOCACTIVE		0x00002000
#define  TI113X_SCR_PWRSTREAM		0x00000800
#define  TI113X_SCR_DELAYUP		0x00000400
#define  TI113X_SCR_DELAYDOWN		0x00000200
#define  TI113X_SCR_INTERROGATE		0x00000100
#define  TI113X_SCR_CLKRUN_SEL		0x00000080
#define  TI113X_SCR_PWRSAVINGS		0x00000040
#define  TI113X_SCR_SUBSYSRW		0x00000020
#define  TI113X_SCR_CB_DPAR		0x00000010
#define  TI113X_SCR_CDMA_EN		0x00000008
#define  TI113X_SCR_ASYNC_IRQ		0x00000004
#define  TI113X_SCR_KEEPCLK		0x00000002
#define  TI113X_SCR_CLKRUN_ENA		0x00000001  

#define  TI122X_SCR_SER_STEP		0xc0000000
#define  TI122X_SCR_INTRTIE		0x20000000
#define  TIXX21_SCR_TIEALL		0x10000000
#define  TI122X_SCR_CBRSVD		0x00400000
#define  TI122X_SCR_MRBURSTDN		0x00008000
#define  TI122X_SCR_MRBURSTUP		0x00004000
#define  TI122X_SCR_RIMUX		0x00000001

#define TI1250_MULTIMEDIA_CTL		0x0084	
#define  TI1250_MMC_ZVOUTEN		0x80
#define  TI1250_MMC_PORTSEL		0x40
#define  TI1250_MMC_ZVEN1		0x02
#define  TI1250_MMC_ZVEN0		0x01

#define TI1250_GENERAL_STATUS		0x0085	
#define TI1250_GPIO0_CONTROL		0x0088	
#define TI1250_GPIO1_CONTROL		0x0089	
#define TI1250_GPIO2_CONTROL		0x008a	
#define TI1250_GPIO3_CONTROL		0x008b	
#define TI1250_GPIO_MODE_MASK		0xc0

#define TI122X_MFUNC			0x008c	
#define TI122X_MFUNC0_MASK		0x0000000f
#define TI122X_MFUNC1_MASK		0x000000f0
#define TI122X_MFUNC2_MASK		0x00000f00
#define TI122X_MFUNC3_MASK		0x0000f000
#define TI122X_MFUNC4_MASK		0x000f0000
#define TI122X_MFUNC5_MASK		0x00f00000
#define TI122X_MFUNC6_MASK		0x0f000000

#define TI122X_MFUNC0_INTA		0x00000002
#define TI125X_MFUNC0_INTB		0x00000001
#define TI122X_MFUNC1_INTB		0x00000020
#define TI122X_MFUNC3_IRQSER		0x00001000


#define TI113X_RETRY_STATUS		0x0090	
#define  TI113X_RSR_PCIRETRY		0x80
#define  TI113X_RSR_CBRETRY		0x40
#define  TI113X_RSR_TEXP_CBB		0x20
#define  TI113X_RSR_MEXP_CBB		0x10
#define  TI113X_RSR_TEXP_CBA		0x08
#define  TI113X_RSR_MEXP_CBA		0x04
#define  TI113X_RSR_TEXP_PCI		0x02
#define  TI113X_RSR_MEXP_PCI		0x01

#define TI113X_CARD_CONTROL		0x0091	
#define  TI113X_CCR_RIENB		0x80
#define  TI113X_CCR_ZVENABLE		0x40
#define  TI113X_CCR_PCI_IRQ_ENA		0x20
#define  TI113X_CCR_PCI_IREQ		0x10
#define  TI113X_CCR_PCI_CSC		0x08
#define  TI113X_CCR_SPKROUTEN		0x02
#define  TI113X_CCR_IFG			0x01

#define  TI1220_CCR_PORT_SEL		0x20
#define  TI122X_CCR_AUD2MUX		0x04

#define TI113X_DEVICE_CONTROL		0x0092	
#define  TI113X_DCR_5V_FORCE		0x40
#define  TI113X_DCR_3V_FORCE		0x20
#define  TI113X_DCR_IMODE_MASK		0x06
#define  TI113X_DCR_IMODE_ISA		0x02
#define  TI113X_DCR_IMODE_SERIAL	0x04

#define  TI12XX_DCR_IMODE_PCI_ONLY	0x00
#define  TI12XX_DCR_IMODE_ALL_SERIAL	0x06

#define TI113X_BUFFER_CONTROL		0x0093	
#define  TI113X_BCR_CB_READ_DEPTH	0x08
#define  TI113X_BCR_CB_WRITE_DEPTH	0x04
#define  TI113X_BCR_PCI_READ_DEPTH	0x02
#define  TI113X_BCR_PCI_WRITE_DEPTH	0x01

#define TI1250_DIAGNOSTIC		0x0093	
#define  TI1250_DIAG_TRUE_VALUE		0x80
#define  TI1250_DIAG_PCI_IREQ		0x40
#define  TI1250_DIAG_PCI_CSC		0x20
#define  TI1250_DIAG_ASYNC_CSC		0x01

#define TI113X_DMA_0			0x0094	
#define TI113X_DMA_1			0x0098	

#define TI113X_IO_OFFSET(map)		(0x36+((map)<<1))

#define ENE_TEST_C9			0xc9	
#define ENE_TEST_C9_TLTENABLE		0x02
#define ENE_TEST_C9_PFENABLE_F0		0x04
#define ENE_TEST_C9_PFENABLE_F1		0x08
#define ENE_TEST_C9_PFENABLE		(ENE_TEST_C9_PFENABLE_F0 | ENE_TEST_C9_PFENABLE_F1)
#define ENE_TEST_C9_WPDISALBLE_F0	0x40
#define ENE_TEST_C9_WPDISALBLE_F1	0x80
#define ENE_TEST_C9_WPDISALBLE		(ENE_TEST_C9_WPDISALBLE_F0 | ENE_TEST_C9_WPDISALBLE_F1)

#define ti_sysctl(socket)	((socket)->private[0])
#define ti_cardctl(socket)	((socket)->private[1])
#define ti_devctl(socket)	((socket)->private[2])
#define ti_diag(socket)		((socket)->private[3])
#define ti_mfunc(socket)	((socket)->private[4])
#define ene_test_c9(socket)	((socket)->private[5])

static void ti_save_state(struct yenta_socket *socket)
{
	ti_sysctl(socket) = config_readl(socket, TI113X_SYSTEM_CONTROL);
	ti_mfunc(socket) = config_readl(socket, TI122X_MFUNC);
	ti_cardctl(socket) = config_readb(socket, TI113X_CARD_CONTROL);
	ti_devctl(socket) = config_readb(socket, TI113X_DEVICE_CONTROL);
	ti_diag(socket) = config_readb(socket, TI1250_DIAGNOSTIC);

	if (socket->dev->vendor == PCI_VENDOR_ID_ENE)
		ene_test_c9(socket) = config_readb(socket, ENE_TEST_C9);
}

static void ti_restore_state(struct yenta_socket *socket)
{
	config_writel(socket, TI113X_SYSTEM_CONTROL, ti_sysctl(socket));
	config_writel(socket, TI122X_MFUNC, ti_mfunc(socket));
	config_writeb(socket, TI113X_CARD_CONTROL, ti_cardctl(socket));
	config_writeb(socket, TI113X_DEVICE_CONTROL, ti_devctl(socket));
	config_writeb(socket, TI1250_DIAGNOSTIC, ti_diag(socket));

	if (socket->dev->vendor == PCI_VENDOR_ID_ENE)
		config_writeb(socket, ENE_TEST_C9, ene_test_c9(socket));
}


static void ti_zoom_video(struct pcmcia_socket *sock, int onoff)
{
	u8 reg;
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	reg = config_readb(socket, TI113X_CARD_CONTROL);
	if (onoff)
		
		reg |= TI113X_CCR_ZVENABLE;
	else
		reg &= ~TI113X_CCR_ZVENABLE;
	config_writeb(socket, TI113X_CARD_CONTROL, reg);
}

 
static void ti1250_zoom_video(struct pcmcia_socket *sock, int onoff)
{	
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	int shift = 0;
	u8 reg;

	ti_zoom_video(sock, onoff);

	reg = config_readb(socket, TI1250_MULTIMEDIA_CTL);
	reg |= TI1250_MMC_ZVOUTEN;	

	if(PCI_FUNC(socket->dev->devfn)==1)
		shift = 1;
	
	if(onoff)
	{
		reg &= ~(1<<6); 	
		reg |= shift<<6;	
		reg |= 1<<shift;	
	}
	else
	{
		reg &= ~(1<<6); 	
		reg |= (1^shift)<<6;	
		reg &= ~(1<<shift);	
	}

	config_writeb(socket, TI1250_MULTIMEDIA_CTL, reg);
}

static void ti_set_zv(struct yenta_socket *socket)
{
	if(socket->dev->vendor == PCI_VENDOR_ID_TI)
	{
		switch(socket->dev->device)
		{
			
			case PCI_DEVICE_ID_TI_1220:
			case PCI_DEVICE_ID_TI_1221:
			case PCI_DEVICE_ID_TI_1225:
			case PCI_DEVICE_ID_TI_4510:
				socket->socket.zoom_video = ti_zoom_video;
				break;	
			case PCI_DEVICE_ID_TI_1250:
			case PCI_DEVICE_ID_TI_1251A:
			case PCI_DEVICE_ID_TI_1251B:
			case PCI_DEVICE_ID_TI_1450:
				socket->socket.zoom_video = ti1250_zoom_video;
		}
	}
}


static int ti_init(struct yenta_socket *socket)
{
	u8 new, reg = exca_readb(socket, I365_INTCTL);

	new = reg & ~I365_INTR_ENA;
	if (socket->dev->irq)
		new |= I365_INTR_ENA;
	if (new != reg)
		exca_writeb(socket, I365_INTCTL, new);
	return 0;
}

static int ti_override(struct yenta_socket *socket)
{
	u8 new, reg = exca_readb(socket, I365_INTCTL);

	new = reg & ~I365_INTR_ENA;
	if (new != reg)
		exca_writeb(socket, I365_INTCTL, new);

	ti_set_zv(socket);

	return 0;
}

static void ti113x_use_isa_irq(struct yenta_socket *socket)
{
	int isa_irq = -1;
	u8 intctl;
	u32 isa_irq_mask = 0;

	if (!isa_probe)
		return;

	
	isa_irq_mask = yenta_probe_irq(socket, isa_interrupts);
	if (!isa_irq_mask)
		return; 

	
	for (; isa_irq_mask; isa_irq++)
		isa_irq_mask >>= 1;
	socket->cb_irq = isa_irq;

	exca_writeb(socket, I365_CSCINT, (isa_irq << 4));

	intctl = exca_readb(socket, I365_INTCTL);
	intctl &= ~(I365_INTR_ENA | I365_IRQ_MASK);     
	exca_writeb(socket, I365_INTCTL, intctl);

	dev_info(&socket->dev->dev,
		"Yenta TI113x: using isa irq %d for CardBus\n", isa_irq);
}


static int ti113x_override(struct yenta_socket *socket)
{
	u8 cardctl;

	cardctl = config_readb(socket, TI113X_CARD_CONTROL);
	cardctl &= ~(TI113X_CCR_PCI_IRQ_ENA | TI113X_CCR_PCI_IREQ | TI113X_CCR_PCI_CSC);
	if (socket->dev->irq)
		cardctl |= TI113X_CCR_PCI_IRQ_ENA | TI113X_CCR_PCI_CSC | TI113X_CCR_PCI_IREQ;
	else
		ti113x_use_isa_irq(socket);

	config_writeb(socket, TI113X_CARD_CONTROL, cardctl);

	return ti_override(socket);
}


static void ti12xx_irqroute_func0(struct yenta_socket *socket)
{
	u32 mfunc, mfunc_old, devctl;
	u8 gpio3, gpio3_old;
	int pci_irq_status;

	mfunc = mfunc_old = config_readl(socket, TI122X_MFUNC);
	devctl = config_readb(socket, TI113X_DEVICE_CONTROL);
	dev_printk(KERN_INFO, &socket->dev->dev,
		   "TI: mfunc 0x%08x, devctl 0x%02x\n", mfunc, devctl);

	
	ti_init(socket);

	
	pci_irq_status = yenta_probe_cb_irq(socket);
	if (pci_irq_status)
		goto out;

	dev_printk(KERN_INFO, &socket->dev->dev,
		   "TI: probing PCI interrupt failed, trying to fix\n");

	
	if ((devctl & TI113X_DCR_IMODE_MASK) == TI12XX_DCR_IMODE_ALL_SERIAL) {
		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1250:
		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
		case PCI_DEVICE_ID_TI_1451A:
		case PCI_DEVICE_ID_TI_4450:
		case PCI_DEVICE_ID_TI_4451:
			
			break;

		default:
			mfunc = (mfunc & ~TI122X_MFUNC3_MASK) | TI122X_MFUNC3_IRQSER;

			
			if (mfunc != mfunc_old) {
				config_writel(socket, TI122X_MFUNC, mfunc);

				pci_irq_status = yenta_probe_cb_irq(socket);
				if (pci_irq_status == 1) {
					dev_printk(KERN_INFO, &socket->dev->dev,
					    "TI: all-serial interrupts ok\n");
					mfunc_old = mfunc;
					goto out;
				}

				
				mfunc = mfunc_old;
				config_writel(socket, TI122X_MFUNC, mfunc);

				if (pci_irq_status == -1)
					goto out;
			}
		}

		
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "TI: falling back to parallel PCI interrupts\n");
		devctl &= ~TI113X_DCR_IMODE_MASK;
		devctl |= TI113X_DCR_IMODE_SERIAL; 
		config_writeb(socket, TI113X_DEVICE_CONTROL, devctl);
	}

	
	switch (socket->dev->device) {
	case PCI_DEVICE_ID_TI_1250:
	case PCI_DEVICE_ID_TI_1251A:
	case PCI_DEVICE_ID_TI_1251B:
	case PCI_DEVICE_ID_TI_1450:
		
		gpio3 = gpio3_old = config_readb(socket, TI1250_GPIO3_CONTROL);
		gpio3 &= ~TI1250_GPIO_MODE_MASK;
		if (gpio3 != gpio3_old)
			config_writeb(socket, TI1250_GPIO3_CONTROL, gpio3);
		break;

	default:
		gpio3 = gpio3_old = 0;

		mfunc = (mfunc & ~TI122X_MFUNC0_MASK) | TI122X_MFUNC0_INTA;
		if (mfunc != mfunc_old)
			config_writel(socket, TI122X_MFUNC, mfunc);
	}

	
	pci_irq_status = yenta_probe_cb_irq(socket);
	if (pci_irq_status == 1) {
		mfunc_old = mfunc;
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "TI: parallel PCI interrupts ok\n");
	} else {
		
		mfunc = mfunc_old;
		config_writel(socket, TI122X_MFUNC, mfunc);
		if (gpio3 != gpio3_old)
			config_writeb(socket, TI1250_GPIO3_CONTROL, gpio3_old);
	}

out:
	if (pci_irq_status < 1) {
		socket->cb_irq = 0;
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "Yenta TI: no PCI interrupts. Fish. "
			   "Please report.\n");
	}
}


static int ti12xx_align_irqs(struct yenta_socket *socket, int *old_irq)
{
	struct pci_dev *func0;

	
	func0 = pci_get_slot(socket->dev->bus, socket->dev->devfn & ~0x07);
	if (!func0)
		return 0;

	if (old_irq)
		*old_irq = socket->cb_irq;
	socket->cb_irq = socket->dev->irq = func0->irq;

	pci_dev_put(func0);

	return 1;
}

static int ti12xx_tie_interrupts(struct yenta_socket *socket, int *old_irq)
{
	u32 sysctl;
	int ret;

	sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
	if (sysctl & TI122X_SCR_INTRTIE)
		return 0;

	
	ret = ti12xx_align_irqs(socket, old_irq);
	if (!ret)
		return 0;

	
	sysctl |= TI122X_SCR_INTRTIE;
	config_writel(socket, TI113X_SYSTEM_CONTROL, sysctl);

	return 1;
}

static void ti12xx_untie_interrupts(struct yenta_socket *socket, int old_irq)
{
	u32 sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
	sysctl &= ~TI122X_SCR_INTRTIE;
	config_writel(socket, TI113X_SYSTEM_CONTROL, sysctl);

	socket->cb_irq = socket->dev->irq = old_irq;
}

static void ti12xx_irqroute_func1(struct yenta_socket *socket)
{
	u32 mfunc, mfunc_old, devctl, sysctl;
	int pci_irq_status;

	mfunc = mfunc_old = config_readl(socket, TI122X_MFUNC);
	devctl = config_readb(socket, TI113X_DEVICE_CONTROL);
	dev_printk(KERN_INFO, &socket->dev->dev,
		   "TI: mfunc 0x%08x, devctl 0x%02x\n",
		   mfunc, devctl);

	
	sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
	if (sysctl & TI122X_SCR_INTRTIE)
		ti12xx_align_irqs(socket, NULL);

	
	ti_init(socket);

	
	pci_irq_status = yenta_probe_cb_irq(socket);
	if (pci_irq_status)
		goto out;

	dev_printk(KERN_INFO, &socket->dev->dev,
		   "TI: probing PCI interrupt failed, trying to fix\n");

	
	if ((devctl & TI113X_DCR_IMODE_MASK) == TI12XX_DCR_IMODE_ALL_SERIAL) {
		int old_irq;

		if (ti12xx_tie_interrupts(socket, &old_irq)) {
			pci_irq_status = yenta_probe_cb_irq(socket);
			if (pci_irq_status == 1) {
				dev_printk(KERN_INFO, &socket->dev->dev,
					"TI: all-serial interrupts, tied ok\n");
				goto out;
			}

			ti12xx_untie_interrupts(socket, old_irq);
		}
	}
	
	else {
		int old_irq;

		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1250:
			
			break;

		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
			mfunc = (mfunc & ~TI122X_MFUNC0_MASK) | TI125X_MFUNC0_INTB;
			break;

		default:
			mfunc = (mfunc & ~TI122X_MFUNC1_MASK) | TI122X_MFUNC1_INTB;
			break;
		}

		
		if (mfunc != mfunc_old) {
			config_writel(socket, TI122X_MFUNC, mfunc);

			pci_irq_status = yenta_probe_cb_irq(socket);
			if (pci_irq_status == 1) {
				dev_printk(KERN_INFO, &socket->dev->dev,
					   "TI: parallel PCI interrupts ok\n");
				goto out;
			}

			mfunc = mfunc_old;
			config_writel(socket, TI122X_MFUNC, mfunc);

			if (pci_irq_status == -1)
				goto out;
		}

		
		if (ti12xx_tie_interrupts(socket, &old_irq)) {
			pci_irq_status = yenta_probe_cb_irq(socket);
			if (pci_irq_status == 1) {
				dev_printk(KERN_INFO, &socket->dev->dev,
				    "TI: parallel PCI interrupts, tied ok\n");
				goto out;
			}

			ti12xx_untie_interrupts(socket, old_irq);
		}
	}

out:
	if (pci_irq_status < 1) {
		socket->cb_irq = 0;
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "TI: no PCI interrupts. Fish. Please report.\n");
	}
}


static int ti12xx_2nd_slot_empty(struct yenta_socket *socket)
{
	struct pci_dev *func;
	struct yenta_socket *slot2;
	int devfn;
	unsigned int state;
	int ret = 1;
	u32 sysctl;

	
	switch (socket->dev->device) {
	case PCI_DEVICE_ID_TI_1220:
	case PCI_DEVICE_ID_TI_1221:
	case PCI_DEVICE_ID_TI_1225:
	case PCI_DEVICE_ID_TI_1251A:
	case PCI_DEVICE_ID_TI_1251B:
	case PCI_DEVICE_ID_TI_1420:
	case PCI_DEVICE_ID_TI_1450:
	case PCI_DEVICE_ID_TI_1451A:
	case PCI_DEVICE_ID_TI_1520:
	case PCI_DEVICE_ID_TI_1620:
	case PCI_DEVICE_ID_TI_4520:
	case PCI_DEVICE_ID_TI_4450:
	case PCI_DEVICE_ID_TI_4451:
		break;

	case PCI_DEVICE_ID_TI_XX12:
	case PCI_DEVICE_ID_TI_X515:
	case PCI_DEVICE_ID_TI_X420:
	case PCI_DEVICE_ID_TI_X620:
	case PCI_DEVICE_ID_TI_XX21_XX11:
	case PCI_DEVICE_ID_TI_7410:
	case PCI_DEVICE_ID_TI_7610:
		sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
		if (sysctl & TIXX21_SCR_TIEALL)
			return 0;

		break;

	
	default:
		return 1;
	}

	
	devfn = socket->dev->devfn & ~0x07;
	func = pci_get_slot(socket->dev->bus,
	                    (socket->dev->devfn & 0x07) ? devfn : devfn | 0x01);
	if (!func)
		return 1;

	if (socket->dev->device != func->device)
		goto out;

	slot2 = pci_get_drvdata(func);
	if (!slot2)
		goto out;

	
	yenta_get_status(&slot2->socket, &state);
	if (state & SS_DETECT) {
		ret = 0;
		goto out;
	}

out:
	pci_dev_put(func);
	return ret;
}

static int ti12xx_power_hook(struct pcmcia_socket *sock, int operation)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	u32 mfunc, devctl, sysctl;
	u8 gpio3;

	
	if ((operation != HOOK_POWER_PRE) && (operation != HOOK_POWER_POST))
		return 0;

	devctl = config_readb(socket, TI113X_DEVICE_CONTROL);
	sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
	mfunc = config_readl(socket, TI122X_MFUNC);

	if (((devctl & TI113X_DCR_IMODE_MASK) == TI12XX_DCR_IMODE_ALL_SERIAL) &&
	    (pwr_irqs_off || ti12xx_2nd_slot_empty(socket))) {
		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1250:
		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
		case PCI_DEVICE_ID_TI_1451A:
		case PCI_DEVICE_ID_TI_4450:
		case PCI_DEVICE_ID_TI_4451:
			
			break;

		default:
			if (operation == HOOK_POWER_PRE)
				mfunc = (mfunc & ~TI122X_MFUNC3_MASK);
			else
				mfunc = (mfunc & ~TI122X_MFUNC3_MASK) | TI122X_MFUNC3_IRQSER;
		}

		return 0;
	}

	
	if ((PCI_FUNC(socket->dev->devfn) == 0) ||
	    ((sysctl & TI122X_SCR_INTRTIE) &&
	     (pwr_irqs_off || ti12xx_2nd_slot_empty(socket)))) {
		
		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1250:
		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
			
			gpio3 = config_readb(socket, TI1250_GPIO3_CONTROL);
			if (operation == HOOK_POWER_PRE)
				gpio3 = (gpio3 & ~TI1250_GPIO_MODE_MASK) | 0x40;
			else
				gpio3 &= ~TI1250_GPIO_MODE_MASK;
			config_writeb(socket, TI1250_GPIO3_CONTROL, gpio3);
			break;

		default:
			
			if (operation == HOOK_POWER_PRE)
				mfunc &= ~TI122X_MFUNC0_MASK;
			else
				mfunc |= TI122X_MFUNC0_INTA;
			config_writel(socket, TI122X_MFUNC, mfunc);
		}
	} else {
		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
			
			if (operation == HOOK_POWER_PRE)
				mfunc &= ~TI122X_MFUNC0_MASK;
			else
				mfunc |= TI125X_MFUNC0_INTB;
			config_writel(socket, TI122X_MFUNC, mfunc);

			break;

		default:
			
			if (operation == HOOK_POWER_PRE)
				mfunc &= ~TI122X_MFUNC1_MASK;
			else
				mfunc |= TI122X_MFUNC1_INTB;
			config_writel(socket, TI122X_MFUNC, mfunc);
		}
	}

	return 0;
}

static int ti12xx_override(struct yenta_socket *socket)
{
	u32 val, val_orig;

	
	val_orig = val = config_readl(socket, TI113X_SYSTEM_CONTROL);
	if (disable_clkrun && PCI_FUNC(socket->dev->devfn) == 0) {
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "Disabling CLKRUN feature\n");
		val |= TI113X_SCR_KEEPCLK;
	}
	if (!(val & TI122X_SCR_MRBURSTUP)) {
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "Enabling burst memory read transactions\n");
		val |= TI122X_SCR_MRBURSTUP;
	}
	if (val_orig != val)
		config_writel(socket, TI113X_SYSTEM_CONTROL, val);

	val = config_readb(socket, TI1250_DIAGNOSTIC);
	dev_printk(KERN_INFO, &socket->dev->dev,
		   "Using %s to route CSC interrupts to PCI\n",
		   (val & TI1250_DIAG_PCI_CSC) ? "CSCINT" : "INTVAL");
	dev_printk(KERN_INFO, &socket->dev->dev,
		   "Routing CardBus interrupts to %s\n",
		   (val & TI1250_DIAG_PCI_IREQ) ? "PCI" : "ISA");

	
	if (PCI_FUNC(socket->dev->devfn) == 0)
		ti12xx_irqroute_func0(socket);
	else
		ti12xx_irqroute_func1(socket);

	
	socket->socket.power_hook = ti12xx_power_hook;

	return ti_override(socket);
}


static int ti1250_override(struct yenta_socket *socket)
{
	u8 old, diag;

	old = config_readb(socket, TI1250_DIAGNOSTIC);
	diag = old & ~(TI1250_DIAG_PCI_CSC | TI1250_DIAG_PCI_IREQ);
	if (socket->cb_irq)
		diag |= TI1250_DIAG_PCI_CSC | TI1250_DIAG_PCI_IREQ;

	if (diag != old) {
		dev_printk(KERN_INFO, &socket->dev->dev,
			   "adjusting diagnostic: %02x -> %02x\n",
			   old, diag);
		config_writeb(socket, TI1250_DIAGNOSTIC, diag);
	}

	return ti12xx_override(socket);
}



#ifdef CONFIG_YENTA_ENE_TUNE
#define DEVID(_vend,_dev,_subvend,_subdev,mask,bits) {		\
		.vendor		= _vend,			\
		.device		= _dev,				\
		.subvendor	= _subvend,			\
		.subdevice	= _subdev,			\
		.driver_data	= ((mask) << 8 | (bits)),	\
	}
static struct pci_device_id ene_tune_tbl[] = {
	
	DEVID(PCI_VENDOR_ID_MOTOROLA, 0x1801, 0xECC0, PCI_ANY_ID,
		ENE_TEST_C9_TLTENABLE | ENE_TEST_C9_PFENABLE, ENE_TEST_C9_TLTENABLE),
	DEVID(PCI_VENDOR_ID_MOTOROLA, 0x3410, 0xECC0, PCI_ANY_ID,
		ENE_TEST_C9_TLTENABLE | ENE_TEST_C9_PFENABLE, ENE_TEST_C9_TLTENABLE),

	{}
};

static void ene_tune_bridge(struct pcmcia_socket *sock, struct pci_bus *bus)
{
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	struct pci_dev *dev;
	struct pci_device_id *id = NULL;
	u8 test_c9, old_c9, mask, bits;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		id = (struct pci_device_id *) pci_match_id(ene_tune_tbl, dev);
		if (id)
			break;
	}

	test_c9 = old_c9 = config_readb(socket, ENE_TEST_C9);
	if (id) {
		mask = (id->driver_data >> 8) & 0xFF;
		bits = id->driver_data & 0xFF;

		test_c9 = (test_c9 & ~mask) | bits;
	}
	else
		
		test_c9 &= ~ENE_TEST_C9_TLTENABLE;

	dev_printk(KERN_INFO, &socket->dev->dev,
		   "EnE: chaning testregister 0xC9, %02x -> %02x\n",
		   old_c9, test_c9);
	config_writeb(socket, ENE_TEST_C9, test_c9);
}

static int ene_override(struct yenta_socket *socket)
{
	
	socket->socket.tune_bridge = ene_tune_bridge;

	return ti1250_override(socket);
}
#else
#  define ene_override ti1250_override
#endif 

#endif 

