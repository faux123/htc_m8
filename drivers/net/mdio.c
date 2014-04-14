/*
 * mdio.c: Generic support for MDIO-compatible transceivers
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/mdio.h>
#include <linux/module.h>

MODULE_DESCRIPTION("Generic support for MDIO-compatible transceivers");
MODULE_AUTHOR("Copyright 2006-2009 Solarflare Communications Inc.");
MODULE_LICENSE("GPL");

int mdio45_probe(struct mdio_if_info *mdio, int prtad)
{
	int mmd, stat2, devs1, devs2;

	for (mmd = 1; mmd <= 5; mmd++) {
		
		stat2 = mdio->mdio_read(mdio->dev, prtad, mmd, MDIO_STAT2);
		if (stat2 < 0 ||
		    (stat2 & MDIO_STAT2_DEVPRST) != MDIO_STAT2_DEVPRST_VAL)
			continue;

		
		devs1 = mdio->mdio_read(mdio->dev, prtad, mmd, MDIO_DEVS1);
		devs2 = mdio->mdio_read(mdio->dev, prtad, mmd, MDIO_DEVS2);
		if (devs1 < 0 || devs2 < 0)
			continue;

		mdio->prtad = prtad;
		mdio->mmds = devs1 | (devs2 << 16);
		return 0;
	}

	return -ENODEV;
}
EXPORT_SYMBOL(mdio45_probe);

int mdio_set_flag(const struct mdio_if_info *mdio,
		  int prtad, int devad, u16 addr, int mask,
		  bool sense)
{
	int old_val = mdio->mdio_read(mdio->dev, prtad, devad, addr);
	int new_val;

	if (old_val < 0)
		return old_val;
	if (sense)
		new_val = old_val | mask;
	else
		new_val = old_val & ~mask;
	if (old_val == new_val)
		return 0;
	return mdio->mdio_write(mdio->dev, prtad, devad, addr, new_val);
}
EXPORT_SYMBOL(mdio_set_flag);

int mdio45_links_ok(const struct mdio_if_info *mdio, u32 mmd_mask)
{
	int devad, reg;

	if (!mmd_mask) {
		
		reg = mdio->mdio_read(mdio->dev, mdio->prtad,
				      MDIO_MMD_PHYXS, MDIO_STAT2);
		return reg >= 0 && !(reg & MDIO_STAT2_RXFAULT);
	}

	for (devad = 0; mmd_mask; devad++) {
		if (mmd_mask & (1 << devad)) {
			mmd_mask &= ~(1 << devad);

			
			mdio->mdio_read(mdio->dev, mdio->prtad,
					devad, MDIO_STAT1);
			if (devad == MDIO_MMD_PMAPMD || devad == MDIO_MMD_PCS ||
			    devad == MDIO_MMD_PHYXS || devad == MDIO_MMD_DTEXS)
				mdio->mdio_read(mdio->dev, mdio->prtad,
						devad, MDIO_STAT2);

			
			reg = mdio->mdio_read(mdio->dev, mdio->prtad,
					      devad, MDIO_STAT1);
			if (reg < 0 ||
			    (reg & (MDIO_STAT1_FAULT | MDIO_STAT1_LSTATUS)) !=
			    MDIO_STAT1_LSTATUS)
				return false;
		}
	}

	return true;
}
EXPORT_SYMBOL(mdio45_links_ok);

int mdio45_nway_restart(const struct mdio_if_info *mdio)
{
	if (!(mdio->mmds & MDIO_DEVS_AN))
		return -EOPNOTSUPP;

	mdio_set_flag(mdio, mdio->prtad, MDIO_MMD_AN, MDIO_CTRL1,
		      MDIO_AN_CTRL1_RESTART, true);
	return 0;
}
EXPORT_SYMBOL(mdio45_nway_restart);

static u32 mdio45_get_an(const struct mdio_if_info *mdio, u16 addr)
{
	u32 result = 0;
	int reg;

	reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_AN, addr);
	if (reg & ADVERTISE_10HALF)
		result |= ADVERTISED_10baseT_Half;
	if (reg & ADVERTISE_10FULL)
		result |= ADVERTISED_10baseT_Full;
	if (reg & ADVERTISE_100HALF)
		result |= ADVERTISED_100baseT_Half;
	if (reg & ADVERTISE_100FULL)
		result |= ADVERTISED_100baseT_Full;
	if (reg & ADVERTISE_PAUSE_CAP)
		result |= ADVERTISED_Pause;
	if (reg & ADVERTISE_PAUSE_ASYM)
		result |= ADVERTISED_Asym_Pause;
	return result;
}

void mdio45_ethtool_gset_npage(const struct mdio_if_info *mdio,
			       struct ethtool_cmd *ecmd,
			       u32 npage_adv, u32 npage_lpa)
{
	int reg;
	u32 speed;

	BUILD_BUG_ON(MDIO_SUPPORTS_C22 != ETH_MDIO_SUPPORTS_C22);
	BUILD_BUG_ON(MDIO_SUPPORTS_C45 != ETH_MDIO_SUPPORTS_C45);

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->phy_address = mdio->prtad;
	ecmd->mdio_support =
		mdio->mode_support & (MDIO_SUPPORTS_C45 | MDIO_SUPPORTS_C22);

	reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_PMAPMD,
			      MDIO_CTRL2);
	switch (reg & MDIO_PMA_CTRL2_TYPE) {
	case MDIO_PMA_CTRL2_10GBT:
	case MDIO_PMA_CTRL2_1000BT:
	case MDIO_PMA_CTRL2_100BTX:
	case MDIO_PMA_CTRL2_10BT:
		ecmd->port = PORT_TP;
		ecmd->supported = SUPPORTED_TP;
		reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_PMAPMD,
				      MDIO_SPEED);
		if (reg & MDIO_SPEED_10G)
			ecmd->supported |= SUPPORTED_10000baseT_Full;
		if (reg & MDIO_PMA_SPEED_1000)
			ecmd->supported |= (SUPPORTED_1000baseT_Full |
					    SUPPORTED_1000baseT_Half);
		if (reg & MDIO_PMA_SPEED_100)
			ecmd->supported |= (SUPPORTED_100baseT_Full |
					    SUPPORTED_100baseT_Half);
		if (reg & MDIO_PMA_SPEED_10)
			ecmd->supported |= (SUPPORTED_10baseT_Full |
					    SUPPORTED_10baseT_Half);
		ecmd->advertising = ADVERTISED_TP;
		break;

	case MDIO_PMA_CTRL2_10GBCX4:
		ecmd->port = PORT_OTHER;
		ecmd->supported = 0;
		ecmd->advertising = 0;
		break;

	case MDIO_PMA_CTRL2_10GBKX4:
	case MDIO_PMA_CTRL2_10GBKR:
	case MDIO_PMA_CTRL2_1000BKX:
		ecmd->port = PORT_OTHER;
		ecmd->supported = SUPPORTED_Backplane;
		reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_PMAPMD,
				      MDIO_PMA_EXTABLE);
		if (reg & MDIO_PMA_EXTABLE_10GBKX4)
			ecmd->supported |= SUPPORTED_10000baseKX4_Full;
		if (reg & MDIO_PMA_EXTABLE_10GBKR)
			ecmd->supported |= SUPPORTED_10000baseKR_Full;
		if (reg & MDIO_PMA_EXTABLE_1000BKX)
			ecmd->supported |= SUPPORTED_1000baseKX_Full;
		reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_PMAPMD,
				      MDIO_PMA_10GBR_FECABLE);
		if (reg & MDIO_PMA_10GBR_FECABLE_ABLE)
			ecmd->supported |= SUPPORTED_10000baseR_FEC;
		ecmd->advertising = ADVERTISED_Backplane;
		break;

	
	default:
		ecmd->port = PORT_FIBRE;
		ecmd->supported = SUPPORTED_FIBRE;
		ecmd->advertising = ADVERTISED_FIBRE;
		break;
	}

	if (mdio->mmds & MDIO_DEVS_AN) {
		ecmd->supported |= SUPPORTED_Autoneg;
		reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_AN,
				      MDIO_CTRL1);
		if (reg & MDIO_AN_CTRL1_ENABLE) {
			ecmd->autoneg = AUTONEG_ENABLE;
			ecmd->advertising |=
				ADVERTISED_Autoneg |
				mdio45_get_an(mdio, MDIO_AN_ADVERTISE) |
				npage_adv;
		} else {
			ecmd->autoneg = AUTONEG_DISABLE;
		}
	} else {
		ecmd->autoneg = AUTONEG_DISABLE;
	}

	if (ecmd->autoneg) {
		u32 modes = 0;
		int an_stat = mdio->mdio_read(mdio->dev, mdio->prtad,
					      MDIO_MMD_AN, MDIO_STAT1);

		if (an_stat & MDIO_AN_STAT1_COMPLETE) {
			ecmd->lp_advertising =
				mdio45_get_an(mdio, MDIO_AN_LPA) | npage_lpa;
			if (an_stat & MDIO_AN_STAT1_LPABLE)
				ecmd->lp_advertising |= ADVERTISED_Autoneg;
			modes = ecmd->advertising & ecmd->lp_advertising;
		}
		if ((modes & ~ADVERTISED_Autoneg) == 0)
			modes = ecmd->advertising;

		if (modes & (ADVERTISED_10000baseT_Full |
			     ADVERTISED_10000baseKX4_Full |
			     ADVERTISED_10000baseKR_Full)) {
			speed = SPEED_10000;
			ecmd->duplex = DUPLEX_FULL;
		} else if (modes & (ADVERTISED_1000baseT_Full |
				    ADVERTISED_1000baseT_Half |
				    ADVERTISED_1000baseKX_Full)) {
			speed = SPEED_1000;
			ecmd->duplex = !(modes & ADVERTISED_1000baseT_Half);
		} else if (modes & (ADVERTISED_100baseT_Full |
				    ADVERTISED_100baseT_Half)) {
			speed = SPEED_100;
			ecmd->duplex = !!(modes & ADVERTISED_100baseT_Full);
		} else {
			speed = SPEED_10;
			ecmd->duplex = !!(modes & ADVERTISED_10baseT_Full);
		}
	} else {
		
		reg = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_PMAPMD,
				      MDIO_CTRL1);
		speed = (((reg & MDIO_PMA_CTRL1_SPEED1000) ? 100 : 1)
			 * ((reg & MDIO_PMA_CTRL1_SPEED100) ? 100 : 10));
		ecmd->duplex = (reg & MDIO_CTRL1_FULLDPLX ||
				speed == SPEED_10000);
	}

	ethtool_cmd_speed_set(ecmd, speed);

	
	if (ecmd->port == PORT_TP
	    && (ethtool_cmd_speed(ecmd) == SPEED_10000)) {
		switch (mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_PMAPMD,
					MDIO_PMA_10GBT_SWAPPOL)) {
		case MDIO_PMA_10GBT_SWAPPOL_ABNX | MDIO_PMA_10GBT_SWAPPOL_CDNX:
			ecmd->eth_tp_mdix = ETH_TP_MDI;
			break;
		case 0:
			ecmd->eth_tp_mdix = ETH_TP_MDI_X;
			break;
		default:
			
			ecmd->eth_tp_mdix = ETH_TP_MDI_INVALID;
			break;
		}
	}
}
EXPORT_SYMBOL(mdio45_ethtool_gset_npage);

void mdio45_ethtool_spauseparam_an(const struct mdio_if_info *mdio,
				   const struct ethtool_pauseparam *ecmd)
{
	int adv, old_adv;

	WARN_ON(!(mdio->mmds & MDIO_DEVS_AN));

	old_adv = mdio->mdio_read(mdio->dev, mdio->prtad, MDIO_MMD_AN,
				  MDIO_AN_ADVERTISE);
	adv = ((old_adv & ~(ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM)) |
	       mii_advertise_flowctrl((ecmd->rx_pause ? FLOW_CTRL_RX : 0) |
				      (ecmd->tx_pause ? FLOW_CTRL_TX : 0)));
	if (adv != old_adv) {
		mdio->mdio_write(mdio->dev, mdio->prtad, MDIO_MMD_AN,
				 MDIO_AN_ADVERTISE, adv);
		mdio45_nway_restart(mdio);
	}
}
EXPORT_SYMBOL(mdio45_ethtool_spauseparam_an);

int mdio_mii_ioctl(const struct mdio_if_info *mdio,
		   struct mii_ioctl_data *mii_data, int cmd)
{
	int prtad, devad;
	u16 addr = mii_data->reg_num;

	
	switch (cmd) {
	case SIOCGMIIPHY:
		if (mdio->prtad == MDIO_PRTAD_NONE)
			return -EOPNOTSUPP;
		mii_data->phy_id = mdio->prtad;
		cmd = SIOCGMIIREG;
		break;
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		break;
	default:
		return -EOPNOTSUPP;
	}

	
	if ((mdio->mode_support & MDIO_SUPPORTS_C45) &&
	    mdio_phy_id_is_c45(mii_data->phy_id)) {
		prtad = mdio_phy_id_prtad(mii_data->phy_id);
		devad = mdio_phy_id_devad(mii_data->phy_id);
	} else if ((mdio->mode_support & MDIO_SUPPORTS_C22) &&
		   mii_data->phy_id < 0x20) {
		prtad = mii_data->phy_id;
		devad = MDIO_DEVAD_NONE;
		addr &= 0x1f;
	} else if ((mdio->mode_support & MDIO_EMULATE_C22) &&
		   mdio->prtad != MDIO_PRTAD_NONE &&
		   mii_data->phy_id == mdio->prtad) {
		
		prtad = mdio->prtad;
		switch (addr) {
		case MII_BMCR:
		case MII_BMSR:
		case MII_PHYSID1:
		case MII_PHYSID2:
			devad = __ffs(mdio->mmds);
			break;
		case MII_ADVERTISE:
		case MII_LPA:
			if (!(mdio->mmds & MDIO_DEVS_AN))
				return -EINVAL;
			devad = MDIO_MMD_AN;
			if (addr == MII_ADVERTISE)
				addr = MDIO_AN_ADVERTISE;
			else
				addr = MDIO_AN_LPA;
			break;
		default:
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	if (cmd == SIOCGMIIREG) {
		int rc = mdio->mdio_read(mdio->dev, prtad, devad, addr);
		if (rc < 0)
			return rc;
		mii_data->val_out = rc;
		return 0;
	} else {
		return mdio->mdio_write(mdio->dev, prtad, devad, addr,
					mii_data->val_in);
	}
}
EXPORT_SYMBOL(mdio_mii_ioctl);
