 /*
  * Copyright (c) 1997-2000 LAN Media Corporation (LMC)
  * All rights reserved.  www.lanmedia.com
  * Generic HDLC port Copyright (C) 2008 Krzysztof Halasa <khc@pm.waw.pl>
  *
  * This code is written by:
  * Andrew Stanley-Jones (asj@cban.com)
  * Rob Braun (bbraun@vix.com),
  * Michael Graff (explorer@vix.com) and
  * Matt Thomas (matt@3am-software.com).
  *
  * With Help By:
  * David Boggs
  * Ron Crane
  * Alan Cox
  *
  * This software may be used and distributed according to the terms
  * of the GNU General Public License version 2, incorporated herein by reference.
  *
  * Driver for the LanMedia LMC5200, LMC5245, LMC1000, LMC1200 cards.
  *
  * To control link specific options lmcctl is required.
  * It can be obtained from ftp.lanmedia.com.
  *
  * Linux driver notes:
  * Linux uses the device struct lmc_private to pass private information
  * around.
  *
  * The initialization portion of this driver (the lmc_reset() and the
  * lmc_dec_reset() functions, as well as the led controls and the
  * lmc_initcsrs() functions.
  *
  * The watchdog function runs every second and checks to see if
  * we still have link, and that the timing source is what we expected
  * it to be.  If link is lost, the interface is marked down, and
  * we no longer can transmit.
  *
  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdlc.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/inet.h>
#include <linux/bitops.h>
#include <asm/processor.h>             
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/uaccess.h>

#define DRIVER_MAJOR_VERSION     1
#define DRIVER_MINOR_VERSION    34
#define DRIVER_SUB_VERSION       0

#define DRIVER_VERSION  ((DRIVER_MAJOR_VERSION << 8) + DRIVER_MINOR_VERSION)

#include "lmc.h"
#include "lmc_var.h"
#include "lmc_ioctl.h"
#include "lmc_debug.h"
#include "lmc_proto.h"

static int LMC_PKT_BUF_SZ = 1542;

static DEFINE_PCI_DEVICE_TABLE(lmc_pci_tbl) = {
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_FAST,
	  PCI_VENDOR_ID_LMC, PCI_ANY_ID },
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_FAST,
	  PCI_ANY_ID, PCI_VENDOR_ID_LMC },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, lmc_pci_tbl);
MODULE_LICENSE("GPL v2");


static netdev_tx_t lmc_start_xmit(struct sk_buff *skb,
					struct net_device *dev);
static int lmc_rx (struct net_device *dev);
static int lmc_open(struct net_device *dev);
static int lmc_close(struct net_device *dev);
static struct net_device_stats *lmc_get_stats(struct net_device *dev);
static irqreturn_t lmc_interrupt(int irq, void *dev_instance);
static void lmc_initcsrs(lmc_softc_t * const sc, lmc_csrptr_t csr_base, size_t csr_size);
static void lmc_softreset(lmc_softc_t * const);
static void lmc_running_reset(struct net_device *dev);
static int lmc_ifdown(struct net_device * const);
static void lmc_watchdog(unsigned long data);
static void lmc_reset(lmc_softc_t * const sc);
static void lmc_dec_reset(lmc_softc_t * const sc);
static void lmc_driver_timeout(struct net_device *dev);

int lmc_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd) 
{
    lmc_softc_t *sc = dev_to_sc(dev);
    lmc_ctl_t ctl;
    int ret = -EOPNOTSUPP;
    u16 regVal;
    unsigned long flags;

    lmc_trace(dev, "lmc_ioctl in");


    switch (cmd) {
    case LMCIOCGINFO: 
	if (copy_to_user(ifr->ifr_data, &sc->ictl, sizeof(lmc_ctl_t)))
		ret = -EFAULT;
	else
		ret = 0;
        break;

    case LMCIOCSINFO: 
        if (!capable(CAP_NET_ADMIN)) {
            ret = -EPERM;
            break;
        }

        if(dev->flags & IFF_UP){
            ret = -EBUSY;
            break;
        }

	if (copy_from_user(&ctl, ifr->ifr_data, sizeof(lmc_ctl_t))) {
		ret = -EFAULT;
		break;
	}

	spin_lock_irqsave(&sc->lmc_lock, flags);
        sc->lmc_media->set_status (sc, &ctl);

        if(ctl.crc_length != sc->ictl.crc_length) {
            sc->lmc_media->set_crc_length(sc, ctl.crc_length);
	    if (sc->ictl.crc_length == LMC_CTL_CRC_LENGTH_16)
		sc->TxDescriptControlInit |=  LMC_TDES_ADD_CRC_DISABLE;
	    else
		sc->TxDescriptControlInit &= ~LMC_TDES_ADD_CRC_DISABLE;
        }
	spin_unlock_irqrestore(&sc->lmc_lock, flags);

        ret = 0;
        break;

    case LMCIOCIFTYPE: 
        {
	    u16 old_type = sc->if_type;
	    u16	new_type;

	    if (!capable(CAP_NET_ADMIN)) {
		ret = -EPERM;
		break;
	    }

	    if (copy_from_user(&new_type, ifr->ifr_data, sizeof(u16))) {
		ret = -EFAULT;
		break;
	    }

            
	    if (new_type == old_type)
	    {
		ret = 0 ;
		break;				
            }
            
	    spin_lock_irqsave(&sc->lmc_lock, flags);
            lmc_proto_close(sc);

            sc->if_type = new_type;
            lmc_proto_attach(sc);
	    ret = lmc_proto_open(sc);
	    spin_unlock_irqrestore(&sc->lmc_lock, flags);
	    break;
	}

    case LMCIOCGETXINFO: 
	spin_lock_irqsave(&sc->lmc_lock, flags);
        sc->lmc_xinfo.Magic0 = 0xBEEFCAFE;

        sc->lmc_xinfo.PciCardType = sc->lmc_cardtype;
        sc->lmc_xinfo.PciSlotNumber = 0;
        sc->lmc_xinfo.DriverMajorVersion = DRIVER_MAJOR_VERSION;
        sc->lmc_xinfo.DriverMinorVersion = DRIVER_MINOR_VERSION;
        sc->lmc_xinfo.DriverSubVersion = DRIVER_SUB_VERSION;
        sc->lmc_xinfo.XilinxRevisionNumber =
            lmc_mii_readreg (sc, 0, 3) & 0xf;
        sc->lmc_xinfo.MaxFrameSize = LMC_PKT_BUF_SZ;
        sc->lmc_xinfo.link_status = sc->lmc_media->get_link_status (sc);
        sc->lmc_xinfo.mii_reg16 = lmc_mii_readreg (sc, 0, 16);
	spin_unlock_irqrestore(&sc->lmc_lock, flags);

        sc->lmc_xinfo.Magic1 = 0xDEADBEEF;

        if (copy_to_user(ifr->ifr_data, &sc->lmc_xinfo,
			 sizeof(struct lmc_xinfo)))
		ret = -EFAULT;
	else
		ret = 0;

        break;

    case LMCIOCGETLMCSTATS:
	    spin_lock_irqsave(&sc->lmc_lock, flags);
	    if (sc->lmc_cardtype == LMC_CARDTYPE_T1) {
		    lmc_mii_writereg(sc, 0, 17, T1FRAMER_FERR_LSB);
		    sc->extra_stats.framingBitErrorCount +=
			    lmc_mii_readreg(sc, 0, 18) & 0xff;
		    lmc_mii_writereg(sc, 0, 17, T1FRAMER_FERR_MSB);
		    sc->extra_stats.framingBitErrorCount +=
			    (lmc_mii_readreg(sc, 0, 18) & 0xff) << 8;
		    lmc_mii_writereg(sc, 0, 17, T1FRAMER_LCV_LSB);
		    sc->extra_stats.lineCodeViolationCount +=
			    lmc_mii_readreg(sc, 0, 18) & 0xff;
		    lmc_mii_writereg(sc, 0, 17, T1FRAMER_LCV_MSB);
		    sc->extra_stats.lineCodeViolationCount +=
			    (lmc_mii_readreg(sc, 0, 18) & 0xff) << 8;
		    lmc_mii_writereg(sc, 0, 17, T1FRAMER_AERR);
		    regVal = lmc_mii_readreg(sc, 0, 18) & 0xff;

		    sc->extra_stats.lossOfFrameCount +=
			    (regVal & T1FRAMER_LOF_MASK) >> 4;
		    sc->extra_stats.changeOfFrameAlignmentCount +=
			    (regVal & T1FRAMER_COFA_MASK) >> 2;
		    sc->extra_stats.severelyErroredFrameCount +=
			    regVal & T1FRAMER_SEF_MASK;
	    }
	    spin_unlock_irqrestore(&sc->lmc_lock, flags);
	    if (copy_to_user(ifr->ifr_data, &sc->lmc_device->stats,
			     sizeof(sc->lmc_device->stats)) ||
		copy_to_user(ifr->ifr_data + sizeof(sc->lmc_device->stats),
			     &sc->extra_stats, sizeof(sc->extra_stats)))
		    ret = -EFAULT;
	    else
		    ret = 0;
	    break;

    case LMCIOCCLEARLMCSTATS:
	    if (!capable(CAP_NET_ADMIN)) {
		    ret = -EPERM;
		    break;
	    }

	    spin_lock_irqsave(&sc->lmc_lock, flags);
	    memset(&sc->lmc_device->stats, 0, sizeof(sc->lmc_device->stats));
	    memset(&sc->extra_stats, 0, sizeof(sc->extra_stats));
	    sc->extra_stats.check = STATCHECK;
	    sc->extra_stats.version_size = (DRIVER_VERSION << 16) +
		    sizeof(sc->lmc_device->stats) + sizeof(sc->extra_stats);
	    sc->extra_stats.lmc_cardtype = sc->lmc_cardtype;
	    spin_unlock_irqrestore(&sc->lmc_lock, flags);
	    ret = 0;
	    break;

    case LMCIOCSETCIRCUIT: 
        if (!capable(CAP_NET_ADMIN)){
            ret = -EPERM;
            break;
        }

        if(dev->flags & IFF_UP){
            ret = -EBUSY;
            break;
        }

	if (copy_from_user(&ctl, ifr->ifr_data, sizeof(lmc_ctl_t))) {
		ret = -EFAULT;
		break;
	}
	spin_lock_irqsave(&sc->lmc_lock, flags);
        sc->lmc_media->set_circuit_type(sc, ctl.circuit_type);
        sc->ictl.circuit_type = ctl.circuit_type;
	spin_unlock_irqrestore(&sc->lmc_lock, flags);
        ret = 0;

        break;

    case LMCIOCRESET: 
        if (!capable(CAP_NET_ADMIN)){
            ret = -EPERM;
            break;
        }

	spin_lock_irqsave(&sc->lmc_lock, flags);
        
        printk (" REG16 before reset +%04x\n", lmc_mii_readreg (sc, 0, 16));
        lmc_running_reset (dev);
        printk (" REG16 after reset +%04x\n", lmc_mii_readreg (sc, 0, 16));

        LMC_EVENT_LOG(LMC_EVENT_FORCEDRESET, LMC_CSR_READ (sc, csr_status), lmc_mii_readreg (sc, 0, 16));
	spin_unlock_irqrestore(&sc->lmc_lock, flags);

        ret = 0;
        break;

#ifdef DEBUG
    case LMCIOCDUMPEVENTLOG:
	if (copy_to_user(ifr->ifr_data, &lmcEventLogIndex, sizeof(u32))) {
		ret = -EFAULT;
		break;
	}
	if (copy_to_user(ifr->ifr_data + sizeof(u32), lmcEventLogBuf,
			 sizeof(lmcEventLogBuf)))
		ret = -EFAULT;
	else
		ret = 0;

        break;
#endif 
    case LMCIOCT1CONTROL: 
        if (sc->lmc_cardtype != LMC_CARDTYPE_T1){
            ret = -EOPNOTSUPP;
            break;
        }
        break;
    case LMCIOCXILINX: 
        {
            struct lmc_xilinx_control xc; 

            if (!capable(CAP_NET_ADMIN)){
                ret = -EPERM;
                break;
            }

            netif_stop_queue(dev);

	    if (copy_from_user(&xc, ifr->ifr_data, sizeof(struct lmc_xilinx_control))) {
		ret = -EFAULT;
		break;
	    }
            switch(xc.command){
            case lmc_xilinx_reset: 
                {
                    u16 mii;
		    spin_lock_irqsave(&sc->lmc_lock, flags);
                    mii = lmc_mii_readreg (sc, 0, 16);

                    lmc_gpio_mkinput(sc, 0xff);

                    lmc_gpio_mkoutput(sc, LMC_GEP_RESET);


                    sc->lmc_gpio &= ~LMC_GEP_RESET;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);


                    udelay(50);

                    sc->lmc_gpio |= LMC_GEP_RESET;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);


                    lmc_gpio_mkinput(sc, 0xff);

                    
                    sc->lmc_media->set_link_status (sc, 1);
                    sc->lmc_media->set_status (sc, NULL);

                    {
                        int i;
                        for(i = 0; i < 5; i++){
                            lmc_led_on(sc, LMC_DS3_LED0);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED0);
                            lmc_led_on(sc, LMC_DS3_LED1);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED1);
                            lmc_led_on(sc, LMC_DS3_LED3);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED3);
                            lmc_led_on(sc, LMC_DS3_LED2);
                            mdelay(100);
                            lmc_led_off(sc, LMC_DS3_LED2);
                        }
                    }
		    spin_unlock_irqrestore(&sc->lmc_lock, flags);
                    
                    

                    ret = 0x0;

                }

                break;
            case lmc_xilinx_load_prom: 
                {
                    u16 mii;
                    int timeout = 500000;
		    spin_lock_irqsave(&sc->lmc_lock, flags);
                    mii = lmc_mii_readreg (sc, 0, 16);

                    lmc_gpio_mkinput(sc, 0xff);

                    lmc_gpio_mkoutput(sc,  LMC_GEP_DP | LMC_GEP_RESET);


                    sc->lmc_gpio &= ~(LMC_GEP_RESET | LMC_GEP_DP);
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);


                    udelay(50);

                    sc->lmc_gpio |= LMC_GEP_DP | LMC_GEP_RESET;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

                    while( (LMC_CSR_READ(sc, csr_gp) & LMC_GEP_INIT) == 0 &&
                           (timeout-- > 0))
                        cpu_relax();


                    lmc_gpio_mkinput(sc, 0xff);
		    spin_unlock_irqrestore(&sc->lmc_lock, flags);

                    ret = 0x0;
                    

                    break;

                }

            case lmc_xilinx_load: 
                {
                    char *data;
                    int pos;
                    int timeout = 500000;

                    if (!xc.data) {
                            ret = -EINVAL;
                            break;
                    }

                    data = kmalloc(xc.len, GFP_KERNEL);
                    if (!data) {
                            ret = -ENOMEM;
                            break;
                    }
                    
                    if(copy_from_user(data, xc.data, xc.len))
                    {
                    	kfree(data);
                    	ret = -ENOMEM;
                    	break;
                    }

                    printk("%s: Starting load of data Len: %d at 0x%p == 0x%p\n", dev->name, xc.len, xc.data, data);

		    spin_lock_irqsave(&sc->lmc_lock, flags);
                    lmc_gpio_mkinput(sc, 0xff);


                    sc->lmc_gpio = 0x00;
                    sc->lmc_gpio &= ~LMC_GEP_DP;
                    sc->lmc_gpio &= ~LMC_GEP_RESET;
                    sc->lmc_gpio |=  LMC_GEP_MODE;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

                    lmc_gpio_mkoutput(sc, LMC_GEP_MODE | LMC_GEP_DP | LMC_GEP_RESET);

                    udelay(50);

                    lmc_gpio_mkinput(sc, LMC_GEP_DP | LMC_GEP_RESET);

                    sc->lmc_gpio = 0x00;
                    sc->lmc_gpio |= LMC_GEP_MODE;
                    sc->lmc_gpio |= LMC_GEP_DATA;
                    sc->lmc_gpio |= LMC_GEP_CLK;
                    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);
                    
                    lmc_gpio_mkoutput(sc, LMC_GEP_DATA | LMC_GEP_CLK | LMC_GEP_MODE );

                    while( (LMC_CSR_READ(sc, csr_gp) & LMC_GEP_INIT) == 0 &&
                           (timeout-- > 0))
                        cpu_relax();

                    printk(KERN_DEBUG "%s: Waited %d for the Xilinx to clear it's memory\n", dev->name, 500000-timeout);

                    for(pos = 0; pos < xc.len; pos++){
                        switch(data[pos]){
                        case 0:
                            sc->lmc_gpio &= ~LMC_GEP_DATA; 
                            break;
                        case 1:
                            sc->lmc_gpio |= LMC_GEP_DATA; 
                            break;
                        default:
                            printk(KERN_WARNING "%s Bad data in xilinx programming data at %d, got %d wanted 0 or 1\n", dev->name, pos, data[pos]);
                            sc->lmc_gpio |= LMC_GEP_DATA; 
                        }
                        sc->lmc_gpio &= ~LMC_GEP_CLK; 
                        sc->lmc_gpio |= LMC_GEP_MODE;
                        LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);
                        udelay(1);
                        
                        sc->lmc_gpio |= LMC_GEP_CLK; 
                        sc->lmc_gpio |= LMC_GEP_MODE;
                        LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);
                        udelay(1);
                    }
                    if((LMC_CSR_READ(sc, csr_gp) & LMC_GEP_INIT) == 0){
                        printk(KERN_WARNING "%s: Reprogramming FAILED. Needs to be reprogrammed. (corrupted data)\n", dev->name);
                    }
                    else if((LMC_CSR_READ(sc, csr_gp) & LMC_GEP_DP) == 0){
                        printk(KERN_WARNING "%s: Reprogramming FAILED. Needs to be reprogrammed. (done)\n", dev->name);
                    }
                    else {
                        printk(KERN_DEBUG "%s: Done reprogramming Xilinx, %d bits, good luck!\n", dev->name, pos);
                    }

                    lmc_gpio_mkinput(sc, 0xff);
                    
                    sc->lmc_miireg16 |= LMC_MII16_FIFO_RESET;
                    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

                    sc->lmc_miireg16 &= ~LMC_MII16_FIFO_RESET;
                    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
		    spin_unlock_irqrestore(&sc->lmc_lock, flags);

                    kfree(data);
                    
                    ret = 0;
                    
                    break;
                }
            default: 
                ret = -EBADE;
                break;
            }

            netif_wake_queue(dev);
            sc->lmc_txfull = 0;

        }
        break;
    default: 
        
        ret = lmc_proto_ioctl (sc, ifr, cmd);
        break;
    }

    lmc_trace(dev, "lmc_ioctl out");

    return ret;
}


static void lmc_watchdog (unsigned long data) 
{
    struct net_device *dev = (struct net_device *)data;
    lmc_softc_t *sc = dev_to_sc(dev);
    int link_status;
    u32 ticks;
    unsigned long flags;

    lmc_trace(dev, "lmc_watchdog in");

    spin_lock_irqsave(&sc->lmc_lock, flags);

    if(sc->check != 0xBEAFCAFE){
        printk("LMC: Corrupt net_device struct, breaking out\n");
	spin_unlock_irqrestore(&sc->lmc_lock, flags);
        return;
    }



    LMC_CSR_WRITE (sc, csr_15, 0x00000011);
    sc->lmc_cmdmode |= TULIP_CMD_TXRUN | TULIP_CMD_RXRUN;
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    if (sc->lmc_ok == 0)
        goto kick_timer;

    LMC_EVENT_LOG(LMC_EVENT_WATCHDOG, LMC_CSR_READ (sc, csr_status), lmc_mii_readreg (sc, 0, 16));

    if (sc->lmc_taint_tx == sc->lastlmc_taint_tx &&
	sc->lmc_device->stats.tx_packets > sc->lasttx_packets &&
	sc->tx_TimeoutInd == 0)
    {

        
        sc->tx_TimeoutInd = 1;
    }
    else if (sc->lmc_taint_tx == sc->lastlmc_taint_tx &&
	     sc->lmc_device->stats.tx_packets > sc->lasttx_packets &&
	     sc->tx_TimeoutInd)
    {

        LMC_EVENT_LOG(LMC_EVENT_XMTINTTMO, LMC_CSR_READ (sc, csr_status), 0);

        sc->tx_TimeoutDisplay = 1;
	sc->extra_stats.tx_TimeoutCnt++;

        
        lmc_running_reset (dev);


        
        LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);

        LMC_EVENT_LOG(LMC_EVENT_RESET2, lmc_mii_readreg (sc, 0, 16), lmc_mii_readreg (sc, 0, 17));

        
        sc->tx_TimeoutInd = 0;
        sc->lastlmc_taint_tx = sc->lmc_taint_tx;
	sc->lasttx_packets = sc->lmc_device->stats.tx_packets;
    } else {
        sc->tx_TimeoutInd = 0;
        sc->lastlmc_taint_tx = sc->lmc_taint_tx;
	sc->lasttx_packets = sc->lmc_device->stats.tx_packets;
    }

    


    link_status = sc->lmc_media->get_link_status (sc);

    if ((link_status == 0) && (sc->last_link_status != 0)) {
        printk(KERN_WARNING "%s: hardware/physical link down\n", dev->name);
        sc->last_link_status = 0;
        

        
	netif_carrier_off(dev);
    }

     if (link_status != 0 && sc->last_link_status == 0) {
         printk(KERN_WARNING "%s: hardware/physical link up\n", dev->name);
         sc->last_link_status = 1;
         

	 netif_carrier_on(dev);
     }

    
    sc->lmc_media->watchdog(sc);

    LMC_CSR_WRITE(sc, csr_rxpoll, 0);

    if(sc->failed_ring == 1){
        sc->failed_ring = 0;
        lmc_softreset(sc);
    }
    if(sc->failed_recv_alloc == 1){
        sc->failed_recv_alloc = 0;
        lmc_softreset(sc);
    }


kick_timer:

    ticks = LMC_CSR_READ (sc, csr_gp_timer);
    LMC_CSR_WRITE (sc, csr_gp_timer, 0xffffffffUL);
    sc->ictl.ticks = 0x0000ffff - (ticks & 0x0000ffff);

    sc->timer.expires = jiffies + (HZ);
    add_timer (&sc->timer);

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_watchdog out");

}

static int lmc_attach(struct net_device *dev, unsigned short encoding,
		      unsigned short parity)
{
	if (encoding == ENCODING_NRZ && parity == PARITY_CRC16_PR1_CCITT)
		return 0;
	return -EINVAL;
}

static const struct net_device_ops lmc_ops = {
	.ndo_open       = lmc_open,
	.ndo_stop       = lmc_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = lmc_ioctl,
	.ndo_tx_timeout = lmc_driver_timeout,
	.ndo_get_stats  = lmc_get_stats,
};

static int __devinit lmc_init_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	lmc_softc_t *sc;
	struct net_device *dev;
	u16 subdevice;
	u16 AdapModelNum;
	int err;
	static int cards_found;

	

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "lmc: pci enable failed: %d\n", err);
		return err;
	}

	err = pci_request_regions(pdev, "lmc");
	if (err) {
		printk(KERN_ERR "lmc: pci_request_region failed\n");
		goto err_req_io;
	}

	sc = kzalloc(sizeof(lmc_softc_t), GFP_KERNEL);
	if (!sc) {
		err = -ENOMEM;
		goto err_kzalloc;
	}

	dev = alloc_hdlcdev(sc);
	if (!dev) {
		printk(KERN_ERR "lmc:alloc_netdev for device failed\n");
		goto err_hdlcdev;
	}


	dev->type = ARPHRD_HDLC;
	dev_to_hdlc(dev)->xmit = lmc_start_xmit;
	dev_to_hdlc(dev)->attach = lmc_attach;
	dev->netdev_ops = &lmc_ops;
	dev->watchdog_timeo = HZ; 
	dev->tx_queue_len = 100;
	sc->lmc_device = dev;
	sc->name = dev->name;
	sc->if_type = LMC_PPP;
	sc->check = 0xBEAFCAFE;
	dev->base_addr = pci_resource_start(pdev, 0);
	dev->irq = pdev->irq;
	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	lmc_proto_attach(sc);

	

	spin_lock_init(&sc->lmc_lock);
	pci_set_master(pdev);

	printk(KERN_INFO "%s: detected at %lx, irq %d\n", dev->name,
	       dev->base_addr, dev->irq);

	err = register_hdlc_device(dev);
	if (err) {
		printk(KERN_ERR "%s: register_netdev failed.\n", dev->name);
		free_netdev(dev);
		goto err_hdlcdev;
	}

    sc->lmc_cardtype = LMC_CARDTYPE_UNKNOWN;
    sc->lmc_timing = LMC_CTL_CLOCK_SOURCE_EXT;

    if ((subdevice = pdev->subsystem_device) == PCI_VENDOR_ID_LMC)
	    subdevice = pdev->subsystem_vendor;

    switch (subdevice) {
    case PCI_DEVICE_ID_LMC_HSSI:
	printk(KERN_INFO "%s: LMC HSSI\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_HSSI;
        sc->lmc_media = &lmc_hssi_media;
        break;
    case PCI_DEVICE_ID_LMC_DS3:
	printk(KERN_INFO "%s: LMC DS3\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_DS3;
        sc->lmc_media = &lmc_ds3_media;
        break;
    case PCI_DEVICE_ID_LMC_SSI:
	printk(KERN_INFO "%s: LMC SSI\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_SSI;
        sc->lmc_media = &lmc_ssi_media;
        break;
    case PCI_DEVICE_ID_LMC_T1:
	printk(KERN_INFO "%s: LMC T1\n", dev->name);
        sc->lmc_cardtype = LMC_CARDTYPE_T1;
        sc->lmc_media = &lmc_t1_media;
        break;
    default:
	printk(KERN_WARNING "%s: LMC UNKNOWN CARD!\n", dev->name);
        break;
    }

    lmc_initcsrs (sc, dev->base_addr, 8);

    lmc_gpio_mkinput (sc, 0xff);
    sc->lmc_gpio = 0;		

    sc->lmc_media->defaults (sc);

    sc->lmc_media->set_link_status (sc, LMC_LINK_UP);

    AdapModelNum = (lmc_mii_readreg (sc, 0, 3) & 0x3f0) >> 4;

    if ((AdapModelNum != LMC_ADAP_T1 || 
	 subdevice != PCI_DEVICE_ID_LMC_T1) &&
	(AdapModelNum != LMC_ADAP_SSI || 
	 subdevice != PCI_DEVICE_ID_LMC_SSI) &&
	(AdapModelNum != LMC_ADAP_DS3 || 
	 subdevice != PCI_DEVICE_ID_LMC_DS3) &&
	(AdapModelNum != LMC_ADAP_HSSI || 
	 subdevice != PCI_DEVICE_ID_LMC_HSSI))
	    printk(KERN_WARNING "%s: Model number (%d) miscompare for PCI"
		   " Subsystem ID = 0x%04x\n",
		   dev->name, AdapModelNum, subdevice);

    LMC_CSR_WRITE (sc, csr_gp_timer, 0xFFFFFFFFUL);

    sc->board_idx = cards_found++;
    sc->extra_stats.check = STATCHECK;
    sc->extra_stats.version_size = (DRIVER_VERSION << 16) +
	    sizeof(sc->lmc_device->stats) + sizeof(sc->extra_stats);
    sc->extra_stats.lmc_cardtype = sc->lmc_cardtype;

    sc->lmc_ok = 0;
    sc->last_link_status = 0;

    lmc_trace(dev, "lmc_init_one out");
    return 0;

err_hdlcdev:
	pci_set_drvdata(pdev, NULL);
	kfree(sc);
err_kzalloc:
	pci_release_regions(pdev);
err_req_io:
	pci_disable_device(pdev);
	return err;
}

static void __devexit lmc_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		printk(KERN_DEBUG "%s: removing...\n", dev->name);
		unregister_hdlc_device(dev);
		free_netdev(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}

static int lmc_open(struct net_device *dev)
{
    lmc_softc_t *sc = dev_to_sc(dev);
    int err;

    lmc_trace(dev, "lmc_open in");

    lmc_led_on(sc, LMC_DS3_LED0);

    lmc_dec_reset(sc);
    lmc_reset(sc);

    LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ(sc, csr_status), 0);
    LMC_EVENT_LOG(LMC_EVENT_RESET2, lmc_mii_readreg(sc, 0, 16),
		  lmc_mii_readreg(sc, 0, 17));

    if (sc->lmc_ok){
        lmc_trace(dev, "lmc_open lmc_ok out");
        return 0;
    }

    lmc_softreset (sc);

    
    if (request_irq (dev->irq, lmc_interrupt, IRQF_SHARED, dev->name, dev)){
        printk(KERN_WARNING "%s: could not get irq: %d\n", dev->name, dev->irq);
        lmc_trace(dev, "lmc_open irq failed out");
        return -EAGAIN;
    }
    sc->got_irq = 1;

    
    sc->lmc_miireg16 |= LMC_MII16_LED_ALL;
    sc->lmc_media->set_link_status (sc, LMC_LINK_UP);

    sc->lmc_media->set_status (sc, NULL);

    sc->TxDescriptControlInit = (
                                 LMC_TDES_INTERRUPT_ON_COMPLETION
                                 | LMC_TDES_FIRST_SEGMENT
                                 | LMC_TDES_LAST_SEGMENT
                                 | LMC_TDES_SECOND_ADDR_CHAINED
                                 | LMC_TDES_DISABLE_PADDING
                                );

    if (sc->ictl.crc_length == LMC_CTL_CRC_LENGTH_16) {
        
        sc->TxDescriptControlInit |= LMC_TDES_ADD_CRC_DISABLE;
    }
    sc->lmc_media->set_crc_length(sc, sc->ictl.crc_length);
    

    

    if ((err = lmc_proto_open(sc)) != 0)
	    return err;

    netif_start_queue(dev);
    sc->extra_stats.tx_tbusy0++;

    sc->lmc_intrmask = 0;
    
    sc->lmc_intrmask |= (TULIP_STS_NORMALINTR
                         | TULIP_STS_RXINTR
                         | TULIP_STS_TXINTR
                         | TULIP_STS_ABNRMLINTR
                         | TULIP_STS_SYSERROR
                         | TULIP_STS_TXSTOPPED
                         | TULIP_STS_TXUNDERFLOW
                         | TULIP_STS_RXSTOPPED
		         | TULIP_STS_RXNOBUF
                        );
    LMC_CSR_WRITE (sc, csr_intr, sc->lmc_intrmask);

    sc->lmc_cmdmode |= TULIP_CMD_TXRUN;
    sc->lmc_cmdmode |= TULIP_CMD_RXRUN;
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    sc->lmc_ok = 1; 


    sc->last_link_status = 1;

    init_timer (&sc->timer);
    sc->timer.expires = jiffies + HZ;
    sc->timer.data = (unsigned long) dev;
    sc->timer.function = lmc_watchdog;
    add_timer (&sc->timer);

    lmc_trace(dev, "lmc_open out");

    return 0;
}


static void lmc_running_reset (struct net_device *dev) 
{
    lmc_softc_t *sc = dev_to_sc(dev);

    lmc_trace(dev, "lmc_runnig_reset in");

    
    
    LMC_CSR_WRITE (sc, csr_intr, 0x00000000);

    lmc_dec_reset (sc);
    lmc_reset (sc);
    lmc_softreset (sc);
    
    sc->lmc_media->set_link_status (sc, 1);
    sc->lmc_media->set_status (sc, NULL);

    netif_wake_queue(dev);

    sc->lmc_txfull = 0;
    sc->extra_stats.tx_tbusy0++;

    sc->lmc_intrmask = TULIP_DEFAULT_INTR_MASK;
    LMC_CSR_WRITE (sc, csr_intr, sc->lmc_intrmask);

    sc->lmc_cmdmode |= (TULIP_CMD_TXRUN | TULIP_CMD_RXRUN);
    LMC_CSR_WRITE (sc, csr_command, sc->lmc_cmdmode);

    lmc_trace(dev, "lmc_runnin_reset_out");
}


static int lmc_close(struct net_device *dev)
{
    
    lmc_softc_t *sc = dev_to_sc(dev);

    lmc_trace(dev, "lmc_close in");

    sc->lmc_ok = 0;
    sc->lmc_media->set_link_status (sc, 0);
    del_timer (&sc->timer);
    lmc_proto_close(sc);
    lmc_ifdown (dev);

    lmc_trace(dev, "lmc_close out");

    return 0;
}

static int lmc_ifdown (struct net_device *dev) 
{
    lmc_softc_t *sc = dev_to_sc(dev);
    u32 csr6;
    int i;

    lmc_trace(dev, "lmc_ifdown in");

    
    
    netif_stop_queue(dev);
    sc->extra_stats.tx_tbusy1++;

    
    
    LMC_CSR_WRITE (sc, csr_intr, 0x00000000);

    
    csr6 = LMC_CSR_READ (sc, csr_command);
    csr6 &= ~LMC_DEC_ST;		
    csr6 &= ~LMC_DEC_SR;		
    LMC_CSR_WRITE (sc, csr_command, csr6);

    sc->lmc_device->stats.rx_missed_errors +=
	    LMC_CSR_READ(sc, csr_missed_frames) & 0xffff;

    
    if(sc->got_irq == 1){
        free_irq (dev->irq, dev);
        sc->got_irq = 0;
    }

    
    for (i = 0; i < LMC_RXDESCS; i++)
    {
        struct sk_buff *skb = sc->lmc_rxq[i];
        sc->lmc_rxq[i] = NULL;
        sc->lmc_rxring[i].status = 0;
        sc->lmc_rxring[i].length = 0;
        sc->lmc_rxring[i].buffer1 = 0xDEADBEEF;
        if (skb != NULL)
            dev_kfree_skb(skb);
        sc->lmc_rxq[i] = NULL;
    }

    for (i = 0; i < LMC_TXDESCS; i++)
    {
        if (sc->lmc_txq[i] != NULL)
            dev_kfree_skb(sc->lmc_txq[i]);
        sc->lmc_txq[i] = NULL;
    }

    lmc_led_off (sc, LMC_MII16_LED_ALL);

    netif_wake_queue(dev);
    sc->extra_stats.tx_tbusy0++;

    lmc_trace(dev, "lmc_ifdown out");

    return 0;
}

static irqreturn_t lmc_interrupt (int irq, void *dev_instance) 
{
    struct net_device *dev = (struct net_device *) dev_instance;
    lmc_softc_t *sc = dev_to_sc(dev);
    u32 csr;
    int i;
    s32 stat;
    unsigned int badtx;
    u32 firstcsr;
    int max_work = LMC_RXDESCS;
    int handled = 0;

    lmc_trace(dev, "lmc_interrupt in");

    spin_lock(&sc->lmc_lock);

    csr = LMC_CSR_READ (sc, csr_status);

    if ( ! (csr & sc->lmc_intrmask)) {
        goto lmc_int_fail_out;
    }

    firstcsr = csr;

    
    while (csr & sc->lmc_intrmask) {
	handled = 1;

        LMC_CSR_WRITE (sc, csr_status, csr);

        if (csr & TULIP_STS_ABNRMLINTR){
            lmc_running_reset (dev);
            break;
        }
        
        if (csr & TULIP_STS_RXINTR){
            lmc_trace(dev, "rx interrupt");
            lmc_rx (dev);
            
        }
        if (csr & (TULIP_STS_TXINTR | TULIP_STS_TXNOBUF | TULIP_STS_TXSTOPPED)) {

	    int		n_compl = 0 ;
            
	    sc->extra_stats.tx_NoCompleteCnt = 0;

            badtx = sc->lmc_taint_tx;
            i = badtx % LMC_TXDESCS;

            while ((badtx < sc->lmc_next_tx)) {
                stat = sc->lmc_txring[i].status;

                LMC_EVENT_LOG (LMC_EVENT_XMTINT, stat,
						 sc->lmc_txring[i].length);
                if (stat & 0x80000000)
                    break;

		n_compl++ ;		
                if (sc->lmc_txq[i] == NULL)
                    continue;

		if (stat & 0x8000) {
			sc->lmc_device->stats.tx_errors++;
			if (stat & 0x4104)
				sc->lmc_device->stats.tx_aborted_errors++;
			if (stat & 0x0C00)
				sc->lmc_device->stats.tx_carrier_errors++;
			if (stat & 0x0200)
				sc->lmc_device->stats.tx_window_errors++;
			if (stat & 0x0002)
				sc->lmc_device->stats.tx_fifo_errors++;
		} else {
			sc->lmc_device->stats.tx_bytes += sc->lmc_txring[i].length & 0x7ff;

			sc->lmc_device->stats.tx_packets++;
                }

                
                dev_kfree_skb_irq(sc->lmc_txq[i]);
                sc->lmc_txq[i] = NULL;

                badtx++;
                i = badtx % LMC_TXDESCS;
            }

            if (sc->lmc_next_tx - badtx > LMC_TXDESCS)
            {
                printk ("%s: out of sync pointer\n", dev->name);
                badtx += LMC_TXDESCS;
            }
            LMC_EVENT_LOG(LMC_EVENT_TBUSY0, n_compl, 0);
            sc->lmc_txfull = 0;
            netif_wake_queue(dev);
	    sc->extra_stats.tx_tbusy0++;


#ifdef DEBUG
	    sc->extra_stats.dirtyTx = badtx;
	    sc->extra_stats.lmc_next_tx = sc->lmc_next_tx;
	    sc->extra_stats.lmc_txfull = sc->lmc_txfull;
#endif
            sc->lmc_taint_tx = badtx;

        }			

        if (csr & TULIP_STS_SYSERROR) {
            u32 error;
            printk (KERN_WARNING "%s: system bus error csr: %#8.8x\n", dev->name, csr);
            error = csr>>23 & 0x7;
            switch(error){
            case 0x000:
                printk(KERN_WARNING "%s: Parity Fault (bad)\n", dev->name);
                break;
            case 0x001:
                printk(KERN_WARNING "%s: Master Abort (naughty)\n", dev->name);
                break;
            case 0x010:
                printk(KERN_WARNING "%s: Target Abort (not so naughty)\n", dev->name);
                break;
            default:
                printk(KERN_WARNING "%s: This bus error code was supposed to be reserved!\n", dev->name);
            }
            lmc_dec_reset (sc);
            lmc_reset (sc);
            LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
            LMC_EVENT_LOG(LMC_EVENT_RESET2,
                          lmc_mii_readreg (sc, 0, 16),
                          lmc_mii_readreg (sc, 0, 17));

        }

        
        if(max_work-- <= 0)
            break;
        
        csr = LMC_CSR_READ (sc, csr_status);
    }				
    LMC_EVENT_LOG(LMC_EVENT_INT, firstcsr, csr);

lmc_int_fail_out:

    spin_unlock(&sc->lmc_lock);

    lmc_trace(dev, "lmc_interrupt out");
    return IRQ_RETVAL(handled);
}

static netdev_tx_t lmc_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
    lmc_softc_t *sc = dev_to_sc(dev);
    u32 flag;
    int entry;
    unsigned long flags;

    lmc_trace(dev, "lmc_start_xmit in");

    spin_lock_irqsave(&sc->lmc_lock, flags);

    

    entry = sc->lmc_next_tx % LMC_TXDESCS;

    sc->lmc_txq[entry] = skb;
    sc->lmc_txring[entry].buffer1 = virt_to_bus (skb->data);

    LMC_CONSOLE_LOG("xmit", skb->data, skb->len);

#ifndef GCOM
    
    if (sc->lmc_next_tx - sc->lmc_taint_tx < LMC_TXDESCS / 2)
    {
        
        flag = 0x60000000;
        netif_wake_queue(dev);
    }
    else if (sc->lmc_next_tx - sc->lmc_taint_tx == LMC_TXDESCS / 2)
    {
        
        flag = 0xe0000000;
        netif_wake_queue(dev);
    }
    else if (sc->lmc_next_tx - sc->lmc_taint_tx < LMC_TXDESCS - 1)
    {
        
        flag = 0x60000000;
        netif_wake_queue(dev);
    }
    else
    {
        
        flag = 0xe0000000;
        sc->lmc_txfull = 1;
        netif_stop_queue(dev);
    }
#else
    flag = LMC_TDES_INTERRUPT_ON_COMPLETION;

    if (sc->lmc_next_tx - sc->lmc_taint_tx >= LMC_TXDESCS - 1)
    {				
        sc->lmc_txfull = 1;
	netif_stop_queue(dev);
	sc->extra_stats.tx_tbusy1++;
        LMC_EVENT_LOG(LMC_EVENT_TBUSY1, entry, 0);
    }
#endif


    if (entry == LMC_TXDESCS - 1)	
	flag |= LMC_TDES_END_OF_RING;	

    
    flag = sc->lmc_txring[entry].length = (skb->len) | flag |
						sc->TxDescriptControlInit;


    sc->extra_stats.tx_NoCompleteCnt++;
    sc->lmc_next_tx++;

    
    LMC_EVENT_LOG(LMC_EVENT_XMT, flag, entry);
    sc->lmc_txring[entry].status = 0x80000000;

    
    LMC_CSR_WRITE (sc, csr_txpoll, 0);

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_start_xmit_out");
    return NETDEV_TX_OK;
}


static int lmc_rx(struct net_device *dev)
{
    lmc_softc_t *sc = dev_to_sc(dev);
    int i;
    int rx_work_limit = LMC_RXDESCS;
    unsigned int next_rx;
    int rxIntLoopCnt;		
    int localLengthErrCnt = 0;
    long stat;
    struct sk_buff *skb, *nsb;
    u16 len;

    lmc_trace(dev, "lmc_rx in");

    lmc_led_on(sc, LMC_DS3_LED3);

    rxIntLoopCnt = 0;		

    i = sc->lmc_next_rx % LMC_RXDESCS;
    next_rx = sc->lmc_next_rx;

    while (((stat = sc->lmc_rxring[i].status) & LMC_RDES_OWN_BIT) != DESC_OWNED_BY_DC21X4)
    {
        rxIntLoopCnt++;		
        len = ((stat & LMC_RDES_FRAME_LENGTH) >> RDES_FRAME_LENGTH_BIT_NUMBER);
        if ((stat & 0x0300) != 0x0300) {  
		if ((stat & 0x0000ffff) != 0x7fff) {
			
			sc->lmc_device->stats.rx_length_errors++;
			goto skip_packet;
		}
	}

	if (stat & 0x00000008) { 
		sc->lmc_device->stats.rx_errors++;
		sc->lmc_device->stats.rx_frame_errors++;
		goto skip_packet;
	}


	if (stat & 0x00000004) { 
		sc->lmc_device->stats.rx_errors++;
		sc->lmc_device->stats.rx_crc_errors++;
		goto skip_packet;
	}

	if (len > LMC_PKT_BUF_SZ) {
		sc->lmc_device->stats.rx_length_errors++;
		localLengthErrCnt++;
		goto skip_packet;
	}

	if (len < sc->lmc_crcSize + 2) {
		sc->lmc_device->stats.rx_length_errors++;
		sc->extra_stats.rx_SmallPktCnt++;
		localLengthErrCnt++;
		goto skip_packet;
	}

        if(stat & 0x00004000){
            printk(KERN_WARNING "%s: Receiver descriptor error, receiver out of sync?\n", dev->name);
        }

        len -= sc->lmc_crcSize;

        skb = sc->lmc_rxq[i];

        
        if (!skb) {
            nsb = dev_alloc_skb (LMC_PKT_BUF_SZ + 2);
            if (nsb) {
                sc->lmc_rxq[i] = nsb;
                nsb->dev = dev;
                sc->lmc_rxring[i].buffer1 = virt_to_bus(skb_tail_pointer(nsb));
            }
            sc->failed_recv_alloc = 1;
            goto skip_packet;
        }
        
	sc->lmc_device->stats.rx_packets++;
	sc->lmc_device->stats.rx_bytes += len;

        LMC_CONSOLE_LOG("recv", skb->data, len);

        
        if(len > (LMC_MTU - (LMC_MTU>>2))){ 
        give_it_anyways:

            sc->lmc_rxq[i] = NULL;
            sc->lmc_rxring[i].buffer1 = 0x0;

            skb_put (skb, len);
            skb->protocol = lmc_proto_type(sc, skb);
            skb_reset_mac_header(skb);
            
            skb->dev = dev;
            lmc_proto_netif(sc, skb);

            nsb = dev_alloc_skb (LMC_PKT_BUF_SZ + 2);
            if (nsb) {
                sc->lmc_rxq[i] = nsb;
                nsb->dev = dev;
                sc->lmc_rxring[i].buffer1 = virt_to_bus(skb_tail_pointer(nsb));
                
            }
            else {
		sc->extra_stats.rx_BuffAllocErr++;
                LMC_EVENT_LOG(LMC_EVENT_RCVINT, stat, len);
                sc->failed_recv_alloc = 1;
                goto skip_out_of_mem;
            }
        }
        else {
            nsb = dev_alloc_skb(len);
            if(!nsb) {
                goto give_it_anyways;
            }
            skb_copy_from_linear_data(skb, skb_put(nsb, len), len);
            
            nsb->protocol = lmc_proto_type(sc, nsb);
            skb_reset_mac_header(nsb);
            
            nsb->dev = dev;
            lmc_proto_netif(sc, nsb);
        }

    skip_packet:
        LMC_EVENT_LOG(LMC_EVENT_RCVINT, stat, len);
        sc->lmc_rxring[i].status = DESC_OWNED_BY_DC21X4;

        sc->lmc_next_rx++;
        i = sc->lmc_next_rx % LMC_RXDESCS;
        rx_work_limit--;
        if (rx_work_limit < 0)
            break;
    }


    
    if (rxIntLoopCnt > sc->extra_stats.rxIntLoopCnt)
	    sc->extra_stats.rxIntLoopCnt = rxIntLoopCnt; 

#ifdef DEBUG
    if (rxIntLoopCnt == 0)
    {
        for (i = 0; i < LMC_RXDESCS; i++)
        {
            if ((sc->lmc_rxring[i].status & LMC_RDES_OWN_BIT)
                != DESC_OWNED_BY_DC21X4)
            {
                rxIntLoopCnt++;
            }
        }
        LMC_EVENT_LOG(LMC_EVENT_RCVEND, rxIntLoopCnt, 0);
    }
#endif


    lmc_led_off(sc, LMC_DS3_LED3);

skip_out_of_mem:

    lmc_trace(dev, "lmc_rx out");

    return 0;
}

static struct net_device_stats *lmc_get_stats(struct net_device *dev)
{
    lmc_softc_t *sc = dev_to_sc(dev);
    unsigned long flags;

    lmc_trace(dev, "lmc_get_stats in");

    spin_lock_irqsave(&sc->lmc_lock, flags);

    sc->lmc_device->stats.rx_missed_errors += LMC_CSR_READ(sc, csr_missed_frames) & 0xffff;

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_get_stats out");

    return &sc->lmc_device->stats;
}

static struct pci_driver lmc_driver = {
	.name		= "lmc",
	.id_table	= lmc_pci_tbl,
	.probe		= lmc_init_one,
	.remove		= __devexit_p(lmc_remove_one),
};

static int __init init_lmc(void)
{
    return pci_register_driver(&lmc_driver);
}

static void __exit exit_lmc(void)
{
    pci_unregister_driver(&lmc_driver);
}

module_init(init_lmc);
module_exit(exit_lmc);

unsigned lmc_mii_readreg (lmc_softc_t * const sc, unsigned devaddr, unsigned regno) 
{
    int i;
    int command = (0xf6 << 10) | (devaddr << 5) | regno;
    int retval = 0;

    lmc_trace(sc->lmc_device, "lmc_mii_readreg in");

    LMC_MII_SYNC (sc);

    lmc_trace(sc->lmc_device, "lmc_mii_readreg: done sync");

    for (i = 15; i >= 0; i--)
    {
        int dataval = (command & (1 << i)) ? 0x20000 : 0;

        LMC_CSR_WRITE (sc, csr_9, dataval);
        lmc_delay ();
        
        LMC_CSR_WRITE (sc, csr_9, dataval | 0x10000);
        lmc_delay ();
        
    }

    lmc_trace(sc->lmc_device, "lmc_mii_readreg: done1");

    for (i = 19; i > 0; i--)
    {
        LMC_CSR_WRITE (sc, csr_9, 0x40000);
        lmc_delay ();
        
        retval = (retval << 1) | ((LMC_CSR_READ (sc, csr_9) & 0x80000) ? 1 : 0);
        LMC_CSR_WRITE (sc, csr_9, 0x40000 | 0x10000);
        lmc_delay ();
        
    }

    lmc_trace(sc->lmc_device, "lmc_mii_readreg out");

    return (retval >> 1) & 0xffff;
}

void lmc_mii_writereg (lmc_softc_t * const sc, unsigned devaddr, unsigned regno, unsigned data) 
{
    int i = 32;
    int command = (0x5002 << 16) | (devaddr << 23) | (regno << 18) | data;

    lmc_trace(sc->lmc_device, "lmc_mii_writereg in");

    LMC_MII_SYNC (sc);

    i = 31;
    while (i >= 0)
    {
        int datav;

        if (command & (1 << i))
            datav = 0x20000;
        else
            datav = 0x00000;

        LMC_CSR_WRITE (sc, csr_9, datav);
        lmc_delay ();
        
        LMC_CSR_WRITE (sc, csr_9, (datav | 0x10000));
        lmc_delay ();
        
        i--;
    }

    i = 2;
    while (i > 0)
    {
        LMC_CSR_WRITE (sc, csr_9, 0x40000);
        lmc_delay ();
        
        LMC_CSR_WRITE (sc, csr_9, 0x50000);
        lmc_delay ();
        
        i--;
    }

    lmc_trace(sc->lmc_device, "lmc_mii_writereg out");
}

static void lmc_softreset (lmc_softc_t * const sc) 
{
    int i;

    lmc_trace(sc->lmc_device, "lmc_softreset in");

    
    sc->lmc_txfull = 0;
    sc->lmc_next_rx = 0;
    sc->lmc_next_tx = 0;
    sc->lmc_taint_rx = 0;
    sc->lmc_taint_tx = 0;


    for (i = 0; i < LMC_RXDESCS; i++)
    {
        struct sk_buff *skb;

        if (sc->lmc_rxq[i] == NULL)
        {
            skb = dev_alloc_skb (LMC_PKT_BUF_SZ + 2);
            if(skb == NULL){
                printk(KERN_WARNING "%s: Failed to allocate receiver ring, will try again\n", sc->name);
                sc->failed_ring = 1;
                break;
            }
            else{
                sc->lmc_rxq[i] = skb;
            }
        }
        else
        {
            skb = sc->lmc_rxq[i];
        }

        skb->dev = sc->lmc_device;

        
        sc->lmc_rxring[i].status = 0x80000000;

        
        sc->lmc_rxring[i].length = skb_tailroom(skb);

        sc->lmc_rxring[i].buffer1 = virt_to_bus (skb->data);

        
        sc->lmc_rxring[i].buffer2 = virt_to_bus (&sc->lmc_rxring[i + 1]);

    }

    if (i != 0) {
        sc->lmc_rxring[i - 1].length |= 0x02000000; 
        sc->lmc_rxring[i - 1].buffer2 = virt_to_bus(&sc->lmc_rxring[0]); 
    }
    LMC_CSR_WRITE (sc, csr_rxlist, virt_to_bus (sc->lmc_rxring)); 

    
    for (i = 0; i < LMC_TXDESCS; i++)
    {
        if (sc->lmc_txq[i] != NULL){		
            dev_kfree_skb(sc->lmc_txq[i]);	
	    sc->lmc_device->stats.tx_dropped++;	
        }
        sc->lmc_txq[i] = NULL;
        sc->lmc_txring[i].status = 0x00000000;
        sc->lmc_txring[i].buffer2 = virt_to_bus (&sc->lmc_txring[i + 1]);
    }
    sc->lmc_txring[i - 1].buffer2 = virt_to_bus (&sc->lmc_txring[0]);
    LMC_CSR_WRITE (sc, csr_txlist, virt_to_bus (sc->lmc_txring));

    lmc_trace(sc->lmc_device, "lmc_softreset out");
}

void lmc_gpio_mkinput(lmc_softc_t * const sc, u32 bits) 
{
    lmc_trace(sc->lmc_device, "lmc_gpio_mkinput in");
    sc->lmc_gpio_io &= ~bits;
    LMC_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET | (sc->lmc_gpio_io));
    lmc_trace(sc->lmc_device, "lmc_gpio_mkinput out");
}

void lmc_gpio_mkoutput(lmc_softc_t * const sc, u32 bits) 
{
    lmc_trace(sc->lmc_device, "lmc_gpio_mkoutput in");
    sc->lmc_gpio_io |= bits;
    LMC_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET | (sc->lmc_gpio_io));
    lmc_trace(sc->lmc_device, "lmc_gpio_mkoutput out");
}

void lmc_led_on(lmc_softc_t * const sc, u32 led) 
{
    lmc_trace(sc->lmc_device, "lmc_led_on in");
    if((~sc->lmc_miireg16) & led){ 
        lmc_trace(sc->lmc_device, "lmc_led_on aon out");
        return;
    }
    
    sc->lmc_miireg16 &= ~led;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
    lmc_trace(sc->lmc_device, "lmc_led_on out");
}

void lmc_led_off(lmc_softc_t * const sc, u32 led) 
{
    lmc_trace(sc->lmc_device, "lmc_led_off in");
    if(sc->lmc_miireg16 & led){ 
        lmc_trace(sc->lmc_device, "lmc_led_off aoff out");
        return;
    }
    
    sc->lmc_miireg16 |= led;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);
    lmc_trace(sc->lmc_device, "lmc_led_off out");
}

static void lmc_reset(lmc_softc_t * const sc) 
{
    lmc_trace(sc->lmc_device, "lmc_reset in");
    sc->lmc_miireg16 |= LMC_MII16_FIFO_RESET;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

    sc->lmc_miireg16 &= ~LMC_MII16_FIFO_RESET;
    lmc_mii_writereg(sc, 0, 16, sc->lmc_miireg16);

    lmc_gpio_mkoutput(sc, LMC_GEP_RESET);

    sc->lmc_gpio &= ~(LMC_GEP_RESET);
    LMC_CSR_WRITE(sc, csr_gp, sc->lmc_gpio);

    udelay(50);

    lmc_gpio_mkinput(sc, LMC_GEP_RESET);

    sc->lmc_media->init(sc);

    sc->extra_stats.resetCount++;
    lmc_trace(sc->lmc_device, "lmc_reset out");
}

static void lmc_dec_reset(lmc_softc_t * const sc) 
{
    u32 val;
    lmc_trace(sc->lmc_device, "lmc_dec_reset in");

    sc->lmc_intrmask = 0;
    LMC_CSR_WRITE(sc, csr_intr, sc->lmc_intrmask);

    LMC_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    udelay(25);
#ifdef __sparc__
    sc->lmc_busmode = LMC_CSR_READ(sc, csr_busmode);
    sc->lmc_busmode = 0x00100000;
    sc->lmc_busmode &= ~TULIP_BUSMODE_SWRESET;
    LMC_CSR_WRITE(sc, csr_busmode, sc->lmc_busmode);
#endif
    sc->lmc_cmdmode = LMC_CSR_READ(sc, csr_command);


    sc->lmc_cmdmode |= ( TULIP_CMD_PROMISCUOUS
                         | TULIP_CMD_FULLDUPLEX
                         | TULIP_CMD_PASSBADPKT
                         | TULIP_CMD_NOHEARTBEAT
                         | TULIP_CMD_PORTSELECT
                         | TULIP_CMD_RECEIVEALL
                         | TULIP_CMD_MUSTBEONE
                       );
    sc->lmc_cmdmode &= ~( TULIP_CMD_OPERMODE
                          | TULIP_CMD_THRESHOLDCTL
                          | TULIP_CMD_STOREFWD
                          | TULIP_CMD_TXTHRSHLDCTL
                        );

    LMC_CSR_WRITE(sc, csr_command, sc->lmc_cmdmode);

    val = LMC_CSR_READ(sc, csr_sia_general);
    val |= (TULIP_WATCHDOG_TXDISABLE | TULIP_WATCHDOG_RXDISABLE);
    LMC_CSR_WRITE(sc, csr_sia_general, val);

    lmc_trace(sc->lmc_device, "lmc_dec_reset out");
}

static void lmc_initcsrs(lmc_softc_t * const sc, lmc_csrptr_t csr_base, 
                         size_t csr_size)
{
    lmc_trace(sc->lmc_device, "lmc_initcsrs in");
    sc->lmc_csrs.csr_busmode	        = csr_base +  0 * csr_size;
    sc->lmc_csrs.csr_txpoll		= csr_base +  1 * csr_size;
    sc->lmc_csrs.csr_rxpoll		= csr_base +  2 * csr_size;
    sc->lmc_csrs.csr_rxlist		= csr_base +  3 * csr_size;
    sc->lmc_csrs.csr_txlist		= csr_base +  4 * csr_size;
    sc->lmc_csrs.csr_status		= csr_base +  5 * csr_size;
    sc->lmc_csrs.csr_command	        = csr_base +  6 * csr_size;
    sc->lmc_csrs.csr_intr		= csr_base +  7 * csr_size;
    sc->lmc_csrs.csr_missed_frames	= csr_base +  8 * csr_size;
    sc->lmc_csrs.csr_9		        = csr_base +  9 * csr_size;
    sc->lmc_csrs.csr_10		        = csr_base + 10 * csr_size;
    sc->lmc_csrs.csr_11		        = csr_base + 11 * csr_size;
    sc->lmc_csrs.csr_12		        = csr_base + 12 * csr_size;
    sc->lmc_csrs.csr_13		        = csr_base + 13 * csr_size;
    sc->lmc_csrs.csr_14		        = csr_base + 14 * csr_size;
    sc->lmc_csrs.csr_15		        = csr_base + 15 * csr_size;
    lmc_trace(sc->lmc_device, "lmc_initcsrs out");
}

static void lmc_driver_timeout(struct net_device *dev)
{
    lmc_softc_t *sc = dev_to_sc(dev);
    u32 csr6;
    unsigned long flags;

    lmc_trace(dev, "lmc_driver_timeout in");

    spin_lock_irqsave(&sc->lmc_lock, flags);

    printk("%s: Xmitter busy|\n", dev->name);

    sc->extra_stats.tx_tbusy_calls++;
    if (jiffies - dev_trans_start(dev) < TX_TIMEOUT)
	    goto bug_out;


    LMC_EVENT_LOG(LMC_EVENT_XMTPRCTMO,
                  LMC_CSR_READ (sc, csr_status),
		  sc->extra_stats.tx_ProcTimeout);

    lmc_running_reset (dev);

    LMC_EVENT_LOG(LMC_EVENT_RESET1, LMC_CSR_READ (sc, csr_status), 0);
    LMC_EVENT_LOG(LMC_EVENT_RESET2,
                  lmc_mii_readreg (sc, 0, 16),
                  lmc_mii_readreg (sc, 0, 17));

    
    csr6 = LMC_CSR_READ (sc, csr_command);
    LMC_CSR_WRITE (sc, csr_command, csr6 | 0x0002);
    LMC_CSR_WRITE (sc, csr_command, csr6 | 0x2002);

    
    LMC_CSR_WRITE (sc, csr_txpoll, 0);

    sc->lmc_device->stats.tx_errors++;
    sc->extra_stats.tx_ProcTimeout++; 

    dev->trans_start = jiffies; 

bug_out:

    spin_unlock_irqrestore(&sc->lmc_lock, flags);

    lmc_trace(dev, "lmc_driver_timout out");


}
