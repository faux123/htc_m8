/*++
Copyright-c Realtek Semiconductor Corp. All rights reserved.

Module Name:
	r8192U_dm.c

Abstract:
	HW dynamic mechanism.

Major Change History:
	When      	Who				What
	----------	--------------- -------------------------------
	2008-05-14	amy                     create version 0 porting from windows code.

--*/
#include "r8192U.h"
#include "r8192U_dm.h"
#include "r8192U_hw.h"
#include "r819xU_phy.h"
#include "r819xU_phyreg.h"
#include "r8190_rtl8256.h"
#include "r819xU_cmdpkt.h"
static u32 edca_setting_DL[HT_IOT_PEER_MAX] =
		{ 0x5e4322, 	0x5e4322, 	0x5e4322, 	0x604322, 	0xa44f, 	0x5ea44f};
static u32 edca_setting_UL[HT_IOT_PEER_MAX] =
		{ 0x5e4322, 	0xa44f, 	0x5e4322, 	0x604322, 	0x5ea44f, 	0x5ea44f};


#define RTK_UL_EDCA 0xa44f
#define RTK_DL_EDCA 0x5e4322


dig_t	dm_digtable;
u8		dm_shadow[16][256] = {{0}};
DRxPathSel	DM_RxPathSelTable;




extern	void	init_hal_dm(struct net_device *dev);
extern	void deinit_hal_dm(struct net_device *dev);

extern void hal_dm_watchdog(struct net_device *dev);


extern	void	init_rate_adaptive(struct net_device *dev);
extern	void	dm_txpower_trackingcallback(struct work_struct *work);

extern	void	dm_cck_txpower_adjust(struct net_device *dev,bool  binch14);
extern	void	dm_restore_dynamic_mechanism_state(struct net_device *dev);
extern	void	dm_backup_dynamic_mechanism_state(struct net_device *dev);
extern	void	dm_change_dynamic_initgain_thresh(struct net_device *dev,
								u32		dm_type,
								u32		dm_value);
extern	void	DM_ChangeFsyncSetting(struct net_device *dev,
												s32		DM_Type,
												s32		DM_Value);
extern	void dm_force_tx_fw_info(struct net_device *dev,
										u32		force_type,
										u32		force_value);
extern	void	dm_init_edca_turbo(struct net_device *dev);
extern	void	dm_rf_operation_test_callback(unsigned long data);
extern	void	dm_rf_pathcheck_workitemcallback(struct work_struct *work);
extern	void dm_fsync_timer_callback(unsigned long data);
extern	void dm_check_fsync(struct net_device *dev);
extern	void	dm_shadow_init(struct net_device *dev);




static	void	dm_check_rate_adaptive(struct net_device *dev);

static	void	dm_init_bandwidth_autoswitch(struct net_device *dev);
static	void	dm_bandwidth_autoswitch(	struct net_device *dev);


static	void	dm_check_txpower_tracking(struct net_device *dev);





#ifndef RTL8192U
static	void	dm_bb_initialgain_restore(struct net_device *dev);


static	void	dm_bb_initialgain_backup(struct net_device *dev);
#endif
static	void	dm_dig_init(struct net_device *dev);
static	void	dm_ctrl_initgain_byrssi(struct net_device *dev);
static	void	dm_ctrl_initgain_byrssi_highpwr(struct net_device *dev);
static	void	dm_ctrl_initgain_byrssi_by_driverrssi(	struct net_device *dev);
static	void	dm_ctrl_initgain_byrssi_by_fwfalse_alarm(struct net_device *dev);
static	void	dm_initial_gain(struct net_device *dev);
static	void	dm_pd_th(struct net_device *dev);
static	void	dm_cs_ratio(struct net_device *dev);

static	void dm_init_ctstoself(struct net_device *dev);
static	void	dm_check_edca_turbo(struct net_device *dev);

static	void	dm_check_rfctrl_gpio(struct net_device *dev);

#ifndef RTL8190P
#endif
static	void dm_check_pbc_gpio(struct net_device *dev);


static	void	dm_check_rx_path_selection(struct net_device *dev);
static 	void dm_init_rxpath_selection(struct net_device *dev);
static	void dm_rxpath_sel_byrssi(struct net_device *dev);


static void dm_init_fsync(struct net_device *dev);
static void dm_deInit_fsync(struct net_device *dev);

static	void	dm_check_txrateandretrycount(struct net_device *dev);


   
static	void	dm_init_dynamic_txpower(struct net_device *dev);
static	void	dm_dynamic_txpower(struct net_device *dev);


static	void dm_send_rssi_tofw(struct net_device *dev);
static	void	dm_ctstoself(struct net_device *dev);

extern	void
init_hal_dm(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	
	priv->undecorated_smoothed_pwdb = -1;

	
	dm_init_dynamic_txpower(dev);
	init_rate_adaptive(dev);
	
	dm_dig_init(dev);
	dm_init_edca_turbo(dev);
	dm_init_bandwidth_autoswitch(dev);
	dm_init_fsync(dev);
	dm_init_rxpath_selection(dev);
	dm_init_ctstoself(dev);

}	

extern void deinit_hal_dm(struct net_device *dev)
{

	dm_deInit_fsync(dev);

}


#ifdef USB_RX_AGGREGATION_SUPPORT
void dm_CheckRxAggregation(struct net_device *dev) {
	struct r8192_priv *priv = ieee80211_priv((struct net_device *)dev);
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	static unsigned long	lastTxOkCnt = 0;
	static unsigned long	lastRxOkCnt = 0;
	unsigned long		curTxOkCnt = 0;
	unsigned long		curRxOkCnt = 0;

	curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
	curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;

	if((curTxOkCnt + curRxOkCnt) < 15000000) {
		return;
	}

	if(curTxOkCnt > 4*curRxOkCnt) {
		if (priv->bCurrentRxAggrEnable) {
			write_nic_dword(dev, 0x1a8, 0);
			priv->bCurrentRxAggrEnable = false;
		}
	}else{
		if (!priv->bCurrentRxAggrEnable && !pHTInfo->bCurrentRT2RTAggregation) {
			u32 ulValue;
			ulValue = (pHTInfo->UsbRxFwAggrEn<<24) | (pHTInfo->UsbRxFwAggrPageNum<<16) |
				(pHTInfo->UsbRxFwAggrPacketNum<<8) | (pHTInfo->UsbRxFwAggrTimeout);
			write_nic_dword(dev, 0x1a8, ulValue);
			priv->bCurrentRxAggrEnable = true;
		}
	}

	lastTxOkCnt = priv->stats.txbytesunicast;
	lastRxOkCnt = priv->stats.rxbytesunicast;
}	
#endif



extern  void    hal_dm_watchdog(struct net_device *dev)
{
	

	

	
	dm_check_rate_adaptive(dev);
	dm_dynamic_txpower(dev);
	dm_check_txrateandretrycount(dev);
	dm_check_txpower_tracking(dev);
	dm_ctrl_initgain_byrssi(dev);
	dm_check_edca_turbo(dev);
	dm_bandwidth_autoswitch(dev);
	dm_check_rfctrl_gpio(dev);
	dm_check_rx_path_selection(dev);
	dm_check_fsync(dev);

	
	dm_check_pbc_gpio(dev);
	dm_send_rssi_tofw(dev);
	dm_ctstoself(dev);
#ifdef USB_RX_AGGREGATION_SUPPORT
	dm_CheckRxAggregation(dev);
#endif
}	


extern void init_rate_adaptive(struct net_device * dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);
	prate_adaptive	pra = (prate_adaptive)&priv->rate_adaptive;

	pra->ratr_state = DM_RATR_STA_MAX;
	pra->high2low_rssi_thresh_for_ra = RateAdaptiveTH_High;
	pra->low2high_rssi_thresh_for_ra20M = RateAdaptiveTH_Low_20M+5;
	pra->low2high_rssi_thresh_for_ra40M = RateAdaptiveTH_Low_40M+5;

	pra->high_rssi_thresh_for_ra = RateAdaptiveTH_High+5;
	pra->low_rssi_thresh_for_ra20M = RateAdaptiveTH_Low_20M;
	pra->low_rssi_thresh_for_ra40M = RateAdaptiveTH_Low_40M;

	if(priv->CustomerID == RT_CID_819x_Netcore)
		pra->ping_rssi_enable = 1;
	else
		pra->ping_rssi_enable = 0;
	pra->ping_rssi_thresh_for_ra = 15;


	if (priv->rf_type == RF_2T4R)
	{
		
		
		pra->upper_rssi_threshold_ratr		= 	0x8f0f0000;
		pra->middle_rssi_threshold_ratr		= 	0x8f0ff000;
		pra->low_rssi_threshold_ratr		= 	0x8f0ff001;
		pra->low_rssi_threshold_ratr_40M	= 	0x8f0ff005;
		pra->low_rssi_threshold_ratr_20M	= 	0x8f0ff001;
		pra->ping_rssi_ratr	= 	0x0000000d;
	}
	else if (priv->rf_type == RF_1T2R)
	{
		pra->upper_rssi_threshold_ratr		= 	0x000f0000;
		pra->middle_rssi_threshold_ratr		= 	0x000ff000;
		pra->low_rssi_threshold_ratr		= 	0x000ff001;
		pra->low_rssi_threshold_ratr_40M	= 	0x000ff005;
		pra->low_rssi_threshold_ratr_20M	= 	0x000ff001;
		pra->ping_rssi_ratr	= 	0x0000000d;
	}

}	


static void dm_check_rate_adaptive(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	prate_adaptive			pra = (prate_adaptive)&priv->rate_adaptive;
	u32						currentRATR, targetRATR = 0;
	u32						LowRSSIThreshForRA = 0, HighRSSIThreshForRA = 0;
	bool						bshort_gi_enabled = false;
	static u8					ping_rssi_state=0;


	if(!priv->up)
	{
		RT_TRACE(COMP_RATE, "<---- dm_check_rate_adaptive(): driver is going to unload\n");
		return;
	}

	if(pra->rate_adaptive_disabled)
		return;

	
	if( !(priv->ieee80211->mode == WIRELESS_MODE_N_24G ||
		 priv->ieee80211->mode == WIRELESS_MODE_N_5G))
		 return;

	if( priv->ieee80211->state == IEEE80211_LINKED )
	{
	

		
		
		
		bshort_gi_enabled = (pHTInfo->bCurTxBW40MHz && pHTInfo->bCurShortGI40MHz) ||
			(!pHTInfo->bCurTxBW40MHz && pHTInfo->bCurShortGI20MHz);


		pra->upper_rssi_threshold_ratr =
				(pra->upper_rssi_threshold_ratr & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;

		pra->middle_rssi_threshold_ratr =
				(pra->middle_rssi_threshold_ratr & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;

		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			pra->low_rssi_threshold_ratr =
				(pra->low_rssi_threshold_ratr_40M & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;
		}
		else
		{
			pra->low_rssi_threshold_ratr =
			(pra->low_rssi_threshold_ratr_20M & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;
		}
		
		pra->ping_rssi_ratr =
				(pra->ping_rssi_ratr & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;

		if (pra->ratr_state == DM_RATR_STA_HIGH)
		{
			HighRSSIThreshForRA 	= pra->high2low_rssi_thresh_for_ra;
			LowRSSIThreshForRA	= (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)?
					(pra->low_rssi_thresh_for_ra40M):(pra->low_rssi_thresh_for_ra20M);
		}
		else if (pra->ratr_state == DM_RATR_STA_LOW)
		{
			HighRSSIThreshForRA	= pra->high_rssi_thresh_for_ra;
			LowRSSIThreshForRA 	= (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)?
					(pra->low2high_rssi_thresh_for_ra40M):(pra->low2high_rssi_thresh_for_ra20M);
		}
		else
		{
			HighRSSIThreshForRA	= pra->high_rssi_thresh_for_ra;
			LowRSSIThreshForRA	= (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)?
					(pra->low_rssi_thresh_for_ra40M):(pra->low_rssi_thresh_for_ra20M);
		}

		
		if(priv->undecorated_smoothed_pwdb >= (long)HighRSSIThreshForRA)
		{
			
			pra->ratr_state = DM_RATR_STA_HIGH;
			targetRATR = pra->upper_rssi_threshold_ratr;
		}else if(priv->undecorated_smoothed_pwdb >= (long)LowRSSIThreshForRA)
		{
			
			pra->ratr_state = DM_RATR_STA_MIDDLE;
			targetRATR = pra->middle_rssi_threshold_ratr;
		}else
		{
			
			pra->ratr_state = DM_RATR_STA_LOW;
			targetRATR = pra->low_rssi_threshold_ratr;
		}

			
		if(pra->ping_rssi_enable)
		{
			
			if(priv->undecorated_smoothed_pwdb < (long)(pra->ping_rssi_thresh_for_ra+5))
			{
				if( (priv->undecorated_smoothed_pwdb < (long)pra->ping_rssi_thresh_for_ra) ||
					ping_rssi_state )
				{
					
					pra->ratr_state = DM_RATR_STA_LOW;
					targetRATR = pra->ping_rssi_ratr;
					ping_rssi_state = 1;
				}
				
				
			}
			else
			{
				
				ping_rssi_state = 0;
			}
		}

		
		
		if(priv->ieee80211->GetHalfNmodeSupportByAPsHandler(dev))
			targetRATR &=  0xf00fffff;

		
		
		
		currentRATR = read_nic_dword(dev, RATR0);
		if( targetRATR !=  currentRATR )
		{
			u32 ratr_value;
			ratr_value = targetRATR;
			RT_TRACE(COMP_RATE,"currentRATR = %x, targetRATR = %x\n", currentRATR, targetRATR);
			if(priv->rf_type == RF_1T2R)
			{
				ratr_value &= ~(RATE_ALL_OFDM_2SS);
			}
			write_nic_dword(dev, RATR0, ratr_value);
			write_nic_byte(dev, UFWP, 1);

			pra->last_ratr = targetRATR;
		}

	}
	else
	{
		pra->ratr_state = DM_RATR_STA_MAX;
	}

}	


static void dm_init_bandwidth_autoswitch(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->ieee80211->bandwidth_auto_switch.threshold_20Mhzto40Mhz = BW_AUTO_SWITCH_LOW_HIGH;
	priv->ieee80211->bandwidth_auto_switch.threshold_40Mhzto20Mhz = BW_AUTO_SWITCH_HIGH_LOW;
	priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz = false;
	priv->ieee80211->bandwidth_auto_switch.bautoswitch_enable = false;

}	


static void dm_bandwidth_autoswitch(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	if(priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20 ||!priv->ieee80211->bandwidth_auto_switch.bautoswitch_enable){
		return;
	}else{
		if(priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz == false){
			if(priv->undecorated_smoothed_pwdb <= priv->ieee80211->bandwidth_auto_switch.threshold_40Mhzto20Mhz)
				priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz = true;
		}else{
			if(priv->undecorated_smoothed_pwdb >= priv->ieee80211->bandwidth_auto_switch.threshold_20Mhzto40Mhz)
				priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz = false;

		}
	}
}	

static u32 OFDMSwingTable[OFDM_Table_Length] = {
	0x7f8001fe,	
	0x71c001c7,	
	0x65400195,	
	0x5a400169,	
	0x50800142,	
	0x47c0011f,	
	0x40000100,	
	0x390000e4,	
	0x32c000cb,	
	0x2d4000b5,	
	0x288000a2,	
	0x24000090,	
	0x20000080,	
	0x1c800072,	
	0x19800066,	
	0x26c0005b,	
	0x24400051,	
	0x12000048,	
	0x10000040	
};

static u8	CCKSwingTable_Ch1_Ch13[CCK_Table_length][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},	
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},	
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},	
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},	
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},	
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},	
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},	
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},	
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},	
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},	
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}	
};

static u8	CCKSwingTable_Ch14[CCK_Table_length][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},	
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},	
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},	
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},	
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},	
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},	
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},	
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},	
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},	
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},	
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}	
};

static void dm_TXPowerTrackingCallback_TSSI(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	bool						bHighpowerstate, viviflag = FALSE;
	DCMD_TXCMD_T			tx_cmd;
	u8						powerlevelOFDM24G;
	int						i =0, j = 0, k = 0;
	u8						RF_Type, tmp_report[5]={0, 0, 0, 0, 0};
	u32						Value;
	u8						Pwr_Flag;
	u16						Avg_TSSI_Meas, TSSI_13dBm, Avg_TSSI_Meas_from_driver=0;
	
	bool rtStatus = true;
	u32						delta=0;

	write_nic_byte(dev, 0x1ba, 0);

	priv->ieee80211->bdynamic_txpower_enable = false;
	bHighpowerstate = priv->bDynamicTxHighPower;

	powerlevelOFDM24G = (u8)(priv->Pwr_Track>>24);
	RF_Type = priv->rf_type;
	Value = (RF_Type<<8) | powerlevelOFDM24G;

	RT_TRACE(COMP_POWER_TRACKING, "powerlevelOFDM24G = %x\n", powerlevelOFDM24G);

	for(j = 0; j<=30; j++)
{	

	tx_cmd.Op		= TXCMD_SET_TX_PWR_TRACKING;
	tx_cmd.Length	= 4;
	tx_cmd.Value		= Value;
#ifdef RTL8192U
	rtStatus = SendTxCommandPacket(dev, &tx_cmd, 12);
	if (rtStatus == RT_STATUS_FAILURE)
	{
		RT_TRACE(COMP_POWER_TRACKING, "Set configuration with tx cmd queue fail!\n");
	}
#else
	cmpk_message_handle_tx(dev, (u8*)&tx_cmd,
								DESC_PACKET_TYPE_INIT, sizeof(DCMD_TXCMD_T));
#endif
	mdelay(1);
	
	for(i = 0;i <= 30; i++)
	{
		Pwr_Flag = read_nic_byte(dev, 0x1ba);

		if (Pwr_Flag == 0)
		{
			mdelay(1);
			continue;
		}
#ifdef RTL8190P
		Avg_TSSI_Meas = read_nic_word(dev, 0x1bc);
#else
		Avg_TSSI_Meas = read_nic_word(dev, 0x13c);
#endif
		if(Avg_TSSI_Meas == 0)
		{
			write_nic_byte(dev, 0x1ba, 0);
			break;
		}

		for(k = 0;k < 5; k++)
		{
#ifdef RTL8190P
			tmp_report[k] = read_nic_byte(dev, 0x1d8+k);
#else
			if(k !=4)
				tmp_report[k] = read_nic_byte(dev, 0x134+k);
			else
				tmp_report[k] = read_nic_byte(dev, 0x13e);
#endif
			RT_TRACE(COMP_POWER_TRACKING, "TSSI_report_value = %d\n", tmp_report[k]);
		}

		
		for(k = 0;k < 5; k++)
		{
			if(tmp_report[k] <= 20)
			{
				viviflag =TRUE;
				break;
			}
		}
		if(viviflag ==TRUE)
		{
			write_nic_byte(dev, 0x1ba, 0);
			viviflag = FALSE;
			RT_TRACE(COMP_POWER_TRACKING, "we filted this data\n");
			for(k = 0;k < 5; k++)
				tmp_report[k] = 0;
			break;
		}

		for(k = 0;k < 5; k++)
		{
			Avg_TSSI_Meas_from_driver += tmp_report[k];
		}

		Avg_TSSI_Meas_from_driver = Avg_TSSI_Meas_from_driver*100/5;
		RT_TRACE(COMP_POWER_TRACKING, "Avg_TSSI_Meas_from_driver = %d\n", Avg_TSSI_Meas_from_driver);
		TSSI_13dBm = priv->TSSI_13dBm;
		RT_TRACE(COMP_POWER_TRACKING, "TSSI_13dBm = %d\n", TSSI_13dBm);

		
		
		if(Avg_TSSI_Meas_from_driver > TSSI_13dBm)
			delta = Avg_TSSI_Meas_from_driver - TSSI_13dBm;
		else
			delta = TSSI_13dBm - Avg_TSSI_Meas_from_driver;

		if(delta <= E_FOR_TX_POWER_TRACK)
		{
			priv->ieee80211->bdynamic_txpower_enable = TRUE;
			write_nic_byte(dev, 0x1ba, 0);
			RT_TRACE(COMP_POWER_TRACKING, "tx power track is done\n");
			RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex = %d\n", priv->rfa_txpowertrackingindex);
			RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex_real = %d\n", priv->rfa_txpowertrackingindex_real);
#ifdef RTL8190P
			RT_TRACE(COMP_POWER_TRACKING, "priv->rfc_txpowertrackingindex = %d\n", priv->rfc_txpowertrackingindex);
			RT_TRACE(COMP_POWER_TRACKING, "priv->rfc_txpowertrackingindex_real = %d\n", priv->rfc_txpowertrackingindex_real);
#endif
			RT_TRACE(COMP_POWER_TRACKING, "priv->cck_present_attentuation_difference = %d\n", priv->cck_present_attentuation_difference);
			RT_TRACE(COMP_POWER_TRACKING, "priv->cck_present_attentuation = %d\n", priv->cck_present_attentuation);
			return;
		}
		else
		{
			if(Avg_TSSI_Meas_from_driver < TSSI_13dBm - E_FOR_TX_POWER_TRACK)
			{
				if((priv->rfa_txpowertrackingindex > 0)
#ifdef RTL8190P
					&&(priv->rfc_txpowertrackingindex > 0)
#endif
				)
				{
					priv->rfa_txpowertrackingindex--;
					if(priv->rfa_txpowertrackingindex_real > 4)
					{
						priv->rfa_txpowertrackingindex_real--;
						rtl8192_setBBreg(dev, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfa_txpowertrackingindex_real].txbbgain_value);
					}
#ifdef RTL8190P
					priv->rfc_txpowertrackingindex--;
					if(priv->rfc_txpowertrackingindex_real > 4)
					{
						priv->rfc_txpowertrackingindex_real--;
						rtl8192_setBBreg(dev, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex_real].txbbgain_value);
					}
#endif
				}
			}
			else
			{
				if((priv->rfa_txpowertrackingindex < 36)
#ifdef RTL8190P
					&&(priv->rfc_txpowertrackingindex < 36)
#endif
					)
				{
					priv->rfa_txpowertrackingindex++;
					priv->rfa_txpowertrackingindex_real++;
					rtl8192_setBBreg(dev, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfa_txpowertrackingindex_real].txbbgain_value);

#ifdef RTL8190P
					priv->rfc_txpowertrackingindex++;
					priv->rfc_txpowertrackingindex_real++;
					rtl8192_setBBreg(dev, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex_real].txbbgain_value);
#endif
				}
			}
			priv->cck_present_attentuation_difference
				= priv->rfa_txpowertrackingindex - priv->rfa_txpowertracking_default;

			if(priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
				priv->cck_present_attentuation
				= priv->cck_present_attentuation_20Mdefault + priv->cck_present_attentuation_difference;
			else
				priv->cck_present_attentuation
				= priv->cck_present_attentuation_40Mdefault + priv->cck_present_attentuation_difference;

			if(priv->cck_present_attentuation > -1&&priv->cck_present_attentuation <23)
			{
				if(priv->ieee80211->current_network.channel == 14 && !priv->bcck_in_ch14)
				{
					priv->bcck_in_ch14 = TRUE;
					dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
				}
				else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
				{
					priv->bcck_in_ch14 = FALSE;
					dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
				}
				else
					dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);
			}
		RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex = %d\n", priv->rfa_txpowertrackingindex);
		RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex_real = %d\n", priv->rfa_txpowertrackingindex_real);
#ifdef RTL8190P
		RT_TRACE(COMP_POWER_TRACKING, "priv->rfc_txpowertrackingindex = %d\n", priv->rfc_txpowertrackingindex);
		RT_TRACE(COMP_POWER_TRACKING, "priv->rfc_txpowertrackingindex_real = %d\n", priv->rfc_txpowertrackingindex_real);
#endif
		RT_TRACE(COMP_POWER_TRACKING, "priv->cck_present_attentuation_difference = %d\n", priv->cck_present_attentuation_difference);
		RT_TRACE(COMP_POWER_TRACKING, "priv->cck_present_attentuation = %d\n", priv->cck_present_attentuation);

		if (priv->cck_present_attentuation_difference <= -12||priv->cck_present_attentuation_difference >= 24)
		{
			priv->ieee80211->bdynamic_txpower_enable = TRUE;
			write_nic_byte(dev, 0x1ba, 0);
			RT_TRACE(COMP_POWER_TRACKING, "tx power track--->limited\n");
			return;
		}


	}
		write_nic_byte(dev, 0x1ba, 0);
		Avg_TSSI_Meas_from_driver = 0;
		for(k = 0;k < 5; k++)
			tmp_report[k] = 0;
		break;
	}
}
		priv->ieee80211->bdynamic_txpower_enable = TRUE;
		write_nic_byte(dev, 0x1ba, 0);
}

static void dm_TXPowerTrackingCallback_ThermalMeter(struct net_device * dev)
{
#define ThermalMeterVal	9
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 tmpRegA, TempCCk;
	u8 tmpOFDMindex, tmpCCKindex, tmpCCK20Mindex, tmpCCK40Mindex, tmpval;
	int i =0, CCKSwingNeedUpdate=0;

	if(!priv->btxpower_trackingInit)
	{
		
		tmpRegA= rtl8192_QueryBBReg(dev, rOFDM0_XATxIQImbalance, bMaskDWord);
		for(i=0; i<OFDM_Table_Length; i++)	
		{
			if(tmpRegA == OFDMSwingTable[i])
			{
				priv->OFDM_index= (u8)i;
				RT_TRACE(COMP_POWER_TRACKING, "Initial reg0x%x = 0x%x, OFDM_index=0x%x\n",
					rOFDM0_XATxIQImbalance, tmpRegA, priv->OFDM_index);
			}
		}

		
		TempCCk = rtl8192_QueryBBReg(dev, rCCK0_TxFilter1, bMaskByte2);
		for(i=0 ; i<CCK_Table_length ; i++)
		{
			if(TempCCk == (u32)CCKSwingTable_Ch1_Ch13[i][0])
			{
				priv->CCK_index =(u8) i;
				RT_TRACE(COMP_POWER_TRACKING, "Initial reg0x%x = 0x%x, CCK_index=0x%x\n",
					rCCK0_TxFilter1, TempCCk, priv->CCK_index);
				break;
			}
		}
		priv->btxpower_trackingInit = TRUE;
		
		return;
	}

	
	
	

	
	tmpRegA = rtl8192_phy_QueryRFReg(dev, RF90_PATH_A, 0x12, 0x078);	
	RT_TRACE(COMP_POWER_TRACKING, "Readback ThermalMeterA = %d \n", tmpRegA);
	if(tmpRegA < 3 || tmpRegA > 13)
		return;
	if(tmpRegA >= 12)	
		tmpRegA = 12;
	RT_TRACE(COMP_POWER_TRACKING, "Valid ThermalMeterA = %d \n", tmpRegA);
	priv->ThermalMeter[0] = ThermalMeterVal;	
	priv->ThermalMeter[1] = ThermalMeterVal;	

	
	if(priv->ThermalMeter[0] >= (u8)tmpRegA)	
	{
		tmpOFDMindex = tmpCCK20Mindex = 6+(priv->ThermalMeter[0]-(u8)tmpRegA);
		tmpCCK40Mindex = tmpCCK20Mindex - 6;
		if(tmpOFDMindex >= OFDM_Table_Length)
			tmpOFDMindex = OFDM_Table_Length-1;
		if(tmpCCK20Mindex >= CCK_Table_length)
			tmpCCK20Mindex = CCK_Table_length-1;
		if(tmpCCK40Mindex >= CCK_Table_length)
			tmpCCK40Mindex = CCK_Table_length-1;
	}
	else
	{
		tmpval = ((u8)tmpRegA - priv->ThermalMeter[0]);
		if(tmpval >= 6)								
			tmpOFDMindex = tmpCCK20Mindex = 0;		
		else
			tmpOFDMindex = tmpCCK20Mindex = 6 - tmpval;
		tmpCCK40Mindex = 0;
	}
	
		
		
	if(priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)	
		tmpCCKindex = tmpCCK40Mindex;
	else
		tmpCCKindex = tmpCCK20Mindex;

	if(priv->ieee80211->current_network.channel == 14 && !priv->bcck_in_ch14)
	{
		priv->bcck_in_ch14 = TRUE;
		CCKSwingNeedUpdate = 1;
	}
	else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
	{
		priv->bcck_in_ch14 = FALSE;
		CCKSwingNeedUpdate = 1;
	}

	if(priv->CCK_index != tmpCCKindex)
	{
		priv->CCK_index = tmpCCKindex;
		CCKSwingNeedUpdate = 1;
	}

	if(CCKSwingNeedUpdate)
	{
		
		dm_cck_txpower_adjust(dev, priv->bcck_in_ch14);
	}
	if(priv->OFDM_index != tmpOFDMindex)
	{
		priv->OFDM_index = tmpOFDMindex;
		rtl8192_setBBreg(dev, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[priv->OFDM_index]);
		RT_TRACE(COMP_POWER_TRACKING, "Update OFDMSwing[%d] = 0x%x\n",
			priv->OFDM_index, OFDMSwingTable[priv->OFDM_index]);
	}
	priv->txpower_count = 0;
}

extern	void	dm_txpower_trackingcallback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
       struct r8192_priv *priv = container_of(dwork,struct r8192_priv,txpower_tracking_wq);
       struct net_device *dev = priv->ieee80211->dev;

#ifdef RTL8190P
	dm_TXPowerTrackingCallback_TSSI(dev);
#else
	if(priv->bDcut == TRUE)
		dm_TXPowerTrackingCallback_TSSI(dev);
	else
		dm_TXPowerTrackingCallback_ThermalMeter(dev);
#endif
}


static void dm_InitializeTXPowerTracking_TSSI(struct net_device *dev)
{

	struct r8192_priv *priv = ieee80211_priv(dev);

	
	priv->txbbgain_table[0].txbb_iq_amplifygain = 			12;
	priv->txbbgain_table[0].txbbgain_value=0x7f8001fe;
	priv->txbbgain_table[1].txbb_iq_amplifygain = 			11;
	priv->txbbgain_table[1].txbbgain_value=0x788001e2;
	priv->txbbgain_table[2].txbb_iq_amplifygain = 			10;
	priv->txbbgain_table[2].txbbgain_value=0x71c001c7;
	priv->txbbgain_table[3].txbb_iq_amplifygain = 			9;
	priv->txbbgain_table[3].txbbgain_value=0x6b8001ae;
	priv->txbbgain_table[4].txbb_iq_amplifygain = 		       8;
	priv->txbbgain_table[4].txbbgain_value=0x65400195;
	priv->txbbgain_table[5].txbb_iq_amplifygain = 		       7;
	priv->txbbgain_table[5].txbbgain_value=0x5fc0017f;
	priv->txbbgain_table[6].txbb_iq_amplifygain = 		       6;
	priv->txbbgain_table[6].txbbgain_value=0x5a400169;
	priv->txbbgain_table[7].txbb_iq_amplifygain = 		       5;
	priv->txbbgain_table[7].txbbgain_value=0x55400155;
	priv->txbbgain_table[8].txbb_iq_amplifygain = 		       4;
	priv->txbbgain_table[8].txbbgain_value=0x50800142;
	priv->txbbgain_table[9].txbb_iq_amplifygain = 		       3;
	priv->txbbgain_table[9].txbbgain_value=0x4c000130;
	priv->txbbgain_table[10].txbb_iq_amplifygain = 		       2;
	priv->txbbgain_table[10].txbbgain_value=0x47c0011f;
	priv->txbbgain_table[11].txbb_iq_amplifygain = 		       1;
	priv->txbbgain_table[11].txbbgain_value=0x43c0010f;
	priv->txbbgain_table[12].txbb_iq_amplifygain = 		       0;
	priv->txbbgain_table[12].txbbgain_value=0x40000100;
	priv->txbbgain_table[13].txbb_iq_amplifygain = 		       -1;
	priv->txbbgain_table[13].txbbgain_value=0x3c8000f2;
	priv->txbbgain_table[14].txbb_iq_amplifygain = 		     -2;
	priv->txbbgain_table[14].txbbgain_value=0x390000e4;
	priv->txbbgain_table[15].txbb_iq_amplifygain = 		     -3;
	priv->txbbgain_table[15].txbbgain_value=0x35c000d7;
	priv->txbbgain_table[16].txbb_iq_amplifygain = 		     -4;
	priv->txbbgain_table[16].txbbgain_value=0x32c000cb;
	priv->txbbgain_table[17].txbb_iq_amplifygain = 		     -5;
	priv->txbbgain_table[17].txbbgain_value=0x300000c0;
	priv->txbbgain_table[18].txbb_iq_amplifygain = 			    -6;
	priv->txbbgain_table[18].txbbgain_value=0x2d4000b5;
	priv->txbbgain_table[19].txbb_iq_amplifygain = 		     -7;
	priv->txbbgain_table[19].txbbgain_value=0x2ac000ab;
	priv->txbbgain_table[20].txbb_iq_amplifygain = 		     -8;
	priv->txbbgain_table[20].txbbgain_value=0x288000a2;
	priv->txbbgain_table[21].txbb_iq_amplifygain = 		     -9;
	priv->txbbgain_table[21].txbbgain_value=0x26000098;
	priv->txbbgain_table[22].txbb_iq_amplifygain = 		     -10;
	priv->txbbgain_table[22].txbbgain_value=0x24000090;
	priv->txbbgain_table[23].txbb_iq_amplifygain = 		     -11;
	priv->txbbgain_table[23].txbbgain_value=0x22000088;
	priv->txbbgain_table[24].txbb_iq_amplifygain = 		     -12;
	priv->txbbgain_table[24].txbbgain_value=0x20000080;
	priv->txbbgain_table[25].txbb_iq_amplifygain = 		     -13;
	priv->txbbgain_table[25].txbbgain_value=0x1a00006c;
	priv->txbbgain_table[26].txbb_iq_amplifygain = 		     -14;
	priv->txbbgain_table[26].txbbgain_value=0x1c800072;
	priv->txbbgain_table[27].txbb_iq_amplifygain = 		     -15;
	priv->txbbgain_table[27].txbbgain_value=0x18000060;
	priv->txbbgain_table[28].txbb_iq_amplifygain = 		     -16;
	priv->txbbgain_table[28].txbbgain_value=0x19800066;
	priv->txbbgain_table[29].txbb_iq_amplifygain = 		     -17;
	priv->txbbgain_table[29].txbbgain_value=0x15800056;
	priv->txbbgain_table[30].txbb_iq_amplifygain = 		     -18;
	priv->txbbgain_table[30].txbbgain_value=0x26c0005b;
	priv->txbbgain_table[31].txbb_iq_amplifygain = 		     -19;
	priv->txbbgain_table[31].txbbgain_value=0x14400051;
	priv->txbbgain_table[32].txbb_iq_amplifygain = 		     -20;
	priv->txbbgain_table[32].txbbgain_value=0x24400051;
	priv->txbbgain_table[33].txbb_iq_amplifygain = 		     -21;
	priv->txbbgain_table[33].txbbgain_value=0x1300004c;
	priv->txbbgain_table[34].txbb_iq_amplifygain = 		     -22;
	priv->txbbgain_table[34].txbbgain_value=0x12000048;
	priv->txbbgain_table[35].txbb_iq_amplifygain = 		     -23;
	priv->txbbgain_table[35].txbbgain_value=0x11000044;
	priv->txbbgain_table[36].txbb_iq_amplifygain = 		     -24;
	priv->txbbgain_table[36].txbbgain_value=0x10000040;

	
	
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[0] = 0x36;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[1] = 0x35;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[2] = 0x2e;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[3] = 0x25;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[4] = 0x1c;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[5] = 0x12;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[6] = 0x09;
	priv->cck_txbbgain_table[0].ccktxbb_valuearray[7] = 0x04;

	priv->cck_txbbgain_table[1].ccktxbb_valuearray[0] = 0x33;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[1] = 0x32;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[2] = 0x2b;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[3] = 0x23;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[4] = 0x1a;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[5] = 0x11;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[6] = 0x08;
	priv->cck_txbbgain_table[1].ccktxbb_valuearray[7] = 0x04;

	priv->cck_txbbgain_table[2].ccktxbb_valuearray[0] = 0x30;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[1] = 0x2f;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[2] = 0x29;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[3] = 0x21;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[4] = 0x19;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[5] = 0x10;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[6] = 0x08;
	priv->cck_txbbgain_table[2].ccktxbb_valuearray[7] = 0x03;

	priv->cck_txbbgain_table[3].ccktxbb_valuearray[0] = 0x2d;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[1] = 0x2d;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[2] = 0x27;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[3] = 0x1f;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[4] = 0x18;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[5] = 0x0f;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[6] = 0x08;
	priv->cck_txbbgain_table[3].ccktxbb_valuearray[7] = 0x03;

	priv->cck_txbbgain_table[4].ccktxbb_valuearray[0] = 0x2b;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[1] = 0x2a;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[2] = 0x25;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[3] = 0x1e;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[4] = 0x16;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[5] = 0x0e;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[6] = 0x07;
	priv->cck_txbbgain_table[4].ccktxbb_valuearray[7] = 0x03;

	priv->cck_txbbgain_table[5].ccktxbb_valuearray[0] = 0x28;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[1] = 0x28;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[2] = 0x22;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[3] = 0x1c;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[4] = 0x15;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[5] = 0x0d;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[6] = 0x07;
	priv->cck_txbbgain_table[5].ccktxbb_valuearray[7] = 0x03;

	priv->cck_txbbgain_table[6].ccktxbb_valuearray[0] = 0x26;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[1] = 0x25;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[2] = 0x21;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[3] = 0x1b;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[4] = 0x14;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[5] = 0x0d;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[6] = 0x06;
	priv->cck_txbbgain_table[6].ccktxbb_valuearray[7] = 0x03;

	priv->cck_txbbgain_table[7].ccktxbb_valuearray[0] = 0x24;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[1] = 0x23;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[2] = 0x1f;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[3] = 0x19;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[4] = 0x13;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[5] = 0x0c;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[6] = 0x06;
	priv->cck_txbbgain_table[7].ccktxbb_valuearray[7] = 0x03;

	priv->cck_txbbgain_table[8].ccktxbb_valuearray[0] = 0x22;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[1] = 0x21;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[2] = 0x1d;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[3] = 0x18;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[4] = 0x11;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[5] = 0x0b;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[6] = 0x06;
	priv->cck_txbbgain_table[8].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[9].ccktxbb_valuearray[0] = 0x20;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[1] = 0x20;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[2] = 0x1b;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[3] = 0x16;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[4] = 0x11;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[5] = 0x08;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[6] = 0x05;
	priv->cck_txbbgain_table[9].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[10].ccktxbb_valuearray[0] = 0x1f;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[1] = 0x1e;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[2] = 0x1a;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[3] = 0x15;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[4] = 0x10;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[5] = 0x0a;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[6] = 0x05;
	priv->cck_txbbgain_table[10].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[11].ccktxbb_valuearray[0] = 0x1d;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[1] = 0x1c;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[2] = 0x18;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[3] = 0x14;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[4] = 0x0f;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[5] = 0x0a;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[6] = 0x05;
	priv->cck_txbbgain_table[11].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[12].ccktxbb_valuearray[0] = 0x1b;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[1] = 0x1a;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[2] = 0x17;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[3] = 0x13;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[4] = 0x0e;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[5] = 0x09;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[6] = 0x04;
	priv->cck_txbbgain_table[12].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[13].ccktxbb_valuearray[0] = 0x1a;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[1] = 0x19;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[2] = 0x16;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[3] = 0x12;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[4] = 0x0d;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[5] = 0x09;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[6] = 0x04;
	priv->cck_txbbgain_table[13].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[14].ccktxbb_valuearray[0] = 0x18;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[1] = 0x17;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[2] = 0x15;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[3] = 0x11;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[4] = 0x0c;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[5] = 0x08;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[6] = 0x04;
	priv->cck_txbbgain_table[14].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[15].ccktxbb_valuearray[0] = 0x17;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[1] = 0x16;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[2] = 0x13;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[3] = 0x10;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[4] = 0x0c;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[5] = 0x08;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[6] = 0x04;
	priv->cck_txbbgain_table[15].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[16].ccktxbb_valuearray[0] = 0x16;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[1] = 0x15;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[2] = 0x12;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[3] = 0x0f;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[4] = 0x0b;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[5] = 0x07;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[6] = 0x04;
	priv->cck_txbbgain_table[16].ccktxbb_valuearray[7] = 0x01;

	priv->cck_txbbgain_table[17].ccktxbb_valuearray[0] = 0x14;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[1] = 0x14;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[2] = 0x11;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[3] = 0x0e;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[4] = 0x0b;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[5] = 0x07;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[6] = 0x03;
	priv->cck_txbbgain_table[17].ccktxbb_valuearray[7] = 0x02;

	priv->cck_txbbgain_table[18].ccktxbb_valuearray[0] = 0x13;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[1] = 0x13;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[2] = 0x10;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[3] = 0x0d;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[4] = 0x0a;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[5] = 0x06;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[6] = 0x03;
	priv->cck_txbbgain_table[18].ccktxbb_valuearray[7] = 0x01;

	priv->cck_txbbgain_table[19].ccktxbb_valuearray[0] = 0x12;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[1] = 0x12;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[2] = 0x0f;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[3] = 0x0c;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[4] = 0x09;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[5] = 0x06;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[6] = 0x03;
	priv->cck_txbbgain_table[19].ccktxbb_valuearray[7] = 0x01;

	priv->cck_txbbgain_table[20].ccktxbb_valuearray[0] = 0x11;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[1] = 0x11;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[2] = 0x0f;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[3] = 0x0c;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[4] = 0x09;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[5] = 0x06;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[6] = 0x03;
	priv->cck_txbbgain_table[20].ccktxbb_valuearray[7] = 0x01;

	priv->cck_txbbgain_table[21].ccktxbb_valuearray[0] = 0x10;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[1] = 0x10;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[2] = 0x0e;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[3] = 0x0b;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[4] = 0x08;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[5] = 0x05;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[6] = 0x03;
	priv->cck_txbbgain_table[21].ccktxbb_valuearray[7] = 0x01;

	priv->cck_txbbgain_table[22].ccktxbb_valuearray[0] = 0x0f;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[1] = 0x0f;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[2] = 0x0d;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[3] = 0x0b;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[4] = 0x08;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[5] = 0x05;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[6] = 0x03;
	priv->cck_txbbgain_table[22].ccktxbb_valuearray[7] = 0x01;

	
	
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[0] = 0x36;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[1] = 0x35;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[2] = 0x2e;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[3] = 0x1b;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[0].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[0] = 0x33;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[1] = 0x32;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[2] = 0x2b;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[3] = 0x19;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[1].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[0] = 0x30;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[1] = 0x2f;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[2] = 0x29;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[3] = 0x18;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[2].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[0] = 0x2d;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[1] = 0x2d;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[2] = 0x27;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[3] = 0x17;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[3].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[0] = 0x2b;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[1] = 0x2a;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[2] = 0x25;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[3] = 0x15;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[4].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[0] = 0x28;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[1] = 0x28;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[2] = 0x22;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[3] = 0x14;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[5].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[0] = 0x26;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[1] = 0x25;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[2] = 0x21;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[3] = 0x13;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[6].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[0] = 0x24;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[1] = 0x23;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[2] = 0x1f;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[3] = 0x12;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[7].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[0] = 0x22;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[1] = 0x21;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[2] = 0x1d;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[3] = 0x11;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[8].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[0] = 0x20;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[1] = 0x20;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[2] = 0x1b;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[3] = 0x10;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[9].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[0] = 0x1f;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[1] = 0x1e;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[2] = 0x1a;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[3] = 0x0f;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[10].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[0] = 0x1d;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[1] = 0x1c;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[2] = 0x18;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[3] = 0x0e;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[11].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[0] = 0x1b;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[1] = 0x1a;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[2] = 0x17;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[3] = 0x0e;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[12].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[0] = 0x1a;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[1] = 0x19;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[2] = 0x16;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[3] = 0x0d;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[13].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[0] = 0x18;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[1] = 0x17;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[2] = 0x15;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[3] = 0x0c;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[14].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[0] = 0x17;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[1] = 0x16;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[2] = 0x13;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[3] = 0x0b;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[15].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[0] = 0x16;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[1] = 0x15;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[2] = 0x12;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[3] = 0x0b;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[16].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[0] = 0x14;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[1] = 0x14;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[2] = 0x11;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[3] = 0x0a;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[17].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[0] = 0x13;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[1] = 0x13;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[2] = 0x10;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[3] = 0x0a;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[18].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[0] = 0x12;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[1] = 0x12;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[2] = 0x0f;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[3] = 0x09;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[19].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[0] = 0x11;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[1] = 0x11;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[2] = 0x0f;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[3] = 0x09;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[20].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[0] = 0x10;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[1] = 0x10;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[2] = 0x0e;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[3] = 0x08;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[21].ccktxbb_valuearray[7] = 0x00;

	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[0] = 0x0f;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[1] = 0x0f;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[2] = 0x0d;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[3] = 0x08;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[4] = 0x00;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[5] = 0x00;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[6] = 0x00;
	priv->cck_txbbgain_ch14_table[22].ccktxbb_valuearray[7] = 0x00;

	priv->btxpower_tracking = TRUE;
	priv->txpower_count       = 0;
	priv->btxpower_trackingInit = FALSE;

}

static void dm_InitializeTXPowerTracking_ThermalMeter(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	
	
	
	if(priv->ieee80211->FwRWRF)
		priv->btxpower_tracking = TRUE;
	else
		priv->btxpower_tracking = FALSE;
	priv->txpower_count       = 0;
	priv->btxpower_trackingInit = FALSE;
}


void dm_initialize_txpower_tracking(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef RTL8190P
	dm_InitializeTXPowerTracking_TSSI(dev);
#else
	if(priv->bDcut == TRUE)
		dm_InitializeTXPowerTracking_TSSI(dev);
	else
		dm_InitializeTXPowerTracking_ThermalMeter(dev);
#endif
}


static void dm_CheckTXPowerTracking_TSSI(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	static u32 tx_power_track_counter = 0;

	if(!priv->btxpower_tracking)
		return;
	else
	{
		if((tx_power_track_counter % 30 == 0)&&(tx_power_track_counter != 0))
		{
				queue_delayed_work(priv->priv_wq,&priv->txpower_tracking_wq,0);
		}
		tx_power_track_counter++;
	}

}


static void dm_CheckTXPowerTracking_ThermalMeter(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	static u8 	TM_Trigger=0;
	
	if(!priv->btxpower_tracking)
		return;
	else
	{
		if(priv->txpower_count  <= 2)
		{
			priv->txpower_count++;
			return;
		}
	}

	if(!TM_Trigger)
	{
		
		
		
		rtl8192_phy_SetRFReg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4d);
		rtl8192_phy_SetRFReg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4f);
		rtl8192_phy_SetRFReg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4d);
		rtl8192_phy_SetRFReg(dev, RF90_PATH_A, 0x02, bMask12Bits, 0x4f);
		TM_Trigger = 1;
		return;
	}
	else
	{
		
			queue_delayed_work(priv->priv_wq,&priv->txpower_tracking_wq,0);
		TM_Trigger = 0;
	}
}


static void dm_check_txpower_tracking(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	

#ifdef  RTL8190P
	dm_CheckTXPowerTracking_TSSI(dev);
#else
	if(priv->bDcut == TRUE)
		dm_CheckTXPowerTracking_TSSI(dev);
	else
		dm_CheckTXPowerTracking_ThermalMeter(dev);
#endif

}	


static void dm_CCKTxPowerAdjust_TSSI(struct net_device *dev, bool  bInCH14)
{
	u32 TempVal;
	struct r8192_priv *priv = ieee80211_priv(dev);
	
	TempVal = 0;
	if(!bInCH14){
		
		TempVal = 	priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[0] +
					(priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[1]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_TxFilter1,bMaskHWord, TempVal);
		
		TempVal = 0;
		TempVal = 	priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[2] +
					(priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[3]<<8) +
					(priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[4]<<16 )+
					(priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[5]<<24);
		rtl8192_setBBreg(dev, rCCK0_TxFilter2,bMaskDWord, TempVal);
		
		TempVal = 0;
		TempVal = 	priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[6] +
					(priv->cck_txbbgain_table[priv->cck_present_attentuation].ccktxbb_valuearray[7]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_DebugPort,bMaskLWord, TempVal);
	}
	else
	{
		TempVal = 	priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[0] +
					(priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[1]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_TxFilter1,bMaskHWord, TempVal);
		
		TempVal = 0;
		TempVal = 	priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[2] +
					(priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[3]<<8) +
					(priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[4]<<16 )+
					(priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[5]<<24);
		rtl8192_setBBreg(dev, rCCK0_TxFilter2,bMaskDWord, TempVal);
		
		TempVal = 0;
		TempVal = 	priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[6] +
					(priv->cck_txbbgain_ch14_table[priv->cck_present_attentuation].ccktxbb_valuearray[7]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_DebugPort,bMaskLWord, TempVal);
	}


}

static void dm_CCKTxPowerAdjust_ThermalMeter(struct net_device *dev,	bool  bInCH14)
{
	u32 TempVal;
	struct r8192_priv *priv = ieee80211_priv(dev);

	TempVal = 0;
	if(!bInCH14)
	{
		
		TempVal = 	CCKSwingTable_Ch1_Ch13[priv->CCK_index][0] +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][1]<<8) ;
		rtl8192_setBBreg(dev, rCCK0_TxFilter1, bMaskHWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK not chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter1, TempVal);
		
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch1_Ch13[priv->CCK_index][2] +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][3]<<8) +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][4]<<16 )+
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][5]<<24);
		rtl8192_setBBreg(dev, rCCK0_TxFilter2, bMaskDWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK not chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter2, TempVal);
		
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch1_Ch13[priv->CCK_index][6] +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][7]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_DebugPort, bMaskLWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK not chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_DebugPort, TempVal);
	}
	else
	{
		
		TempVal = 	CCKSwingTable_Ch14[priv->CCK_index][0] +
					(CCKSwingTable_Ch14[priv->CCK_index][1]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_TxFilter1, bMaskHWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter1, TempVal);
		
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch14[priv->CCK_index][2] +
					(CCKSwingTable_Ch14[priv->CCK_index][3]<<8) +
					(CCKSwingTable_Ch14[priv->CCK_index][4]<<16 )+
					(CCKSwingTable_Ch14[priv->CCK_index][5]<<24);
		rtl8192_setBBreg(dev, rCCK0_TxFilter2, bMaskDWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter2, TempVal);
		
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch14[priv->CCK_index][6] +
					(CCKSwingTable_Ch14[priv->CCK_index][7]<<8) ;

		rtl8192_setBBreg(dev, rCCK0_DebugPort, bMaskLWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING,"CCK chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_DebugPort, TempVal);
	}
}



extern void dm_cck_txpower_adjust(
	struct net_device *dev,
	bool  binch14
)
{	

	struct r8192_priv *priv = ieee80211_priv(dev);
#ifdef RTL8190P
	dm_CCKTxPowerAdjust_TSSI(dev, binch14);
#else
	if(priv->bDcut == TRUE)
		dm_CCKTxPowerAdjust_TSSI(dev, binch14);
	else
		dm_CCKTxPowerAdjust_ThermalMeter(dev, binch14);
#endif
}


#ifndef  RTL8192U
static void dm_txpower_reset_recovery(
	struct net_device *dev
)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_POWER_TRACKING, "Start Reset Recovery ==>\n");
	rtl8192_setBBreg(dev, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfa_txpowertrackingindex].txbbgain_value);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery: Fill in 0xc80 is %08x\n",priv->txbbgain_table[priv->rfa_txpowertrackingindex].txbbgain_value);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery: Fill in RFA_txPowerTrackingIndex is %x\n",priv->rfa_txpowertrackingindex);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery : RF A I/Q Amplify Gain is %ld\n",priv->txbbgain_table[priv->rfa_txpowertrackingindex].txbb_iq_amplifygain);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery: CCK Attenuation is %d dB\n",priv->cck_present_attentuation);
	dm_cck_txpower_adjust(dev,priv->bcck_in_ch14);

	rtl8192_setBBreg(dev, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex].txbbgain_value);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery: Fill in 0xc90 is %08x\n",priv->txbbgain_table[priv->rfc_txpowertrackingindex].txbbgain_value);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery: Fill in RFC_txPowerTrackingIndex is %x\n",priv->rfc_txpowertrackingindex);
	RT_TRACE(COMP_POWER_TRACKING, "Reset Recovery : RF C I/Q Amplify Gain is %ld\n",priv->txbbgain_table[priv->rfc_txpowertrackingindex].txbb_iq_amplifygain);

}	

extern void dm_restore_dynamic_mechanism_state(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 	reg_ratr = priv->rate_adaptive.last_ratr;

	if(!priv->up)
	{
		RT_TRACE(COMP_RATE, "<---- dm_restore_dynamic_mechanism_state(): driver is going to unload\n");
		return;
	}

	
	
	
	if(priv->rate_adaptive.rate_adaptive_disabled)
		return;
	
	if( !(priv->ieee80211->mode==WIRELESS_MODE_N_24G ||
		 priv->ieee80211->mode==WIRELESS_MODE_N_5G))
		 return;
	{
			
			u32 ratr_value;
			ratr_value = reg_ratr;
			if(priv->rf_type == RF_1T2R)	
			{
				ratr_value &=~ (RATE_ALL_OFDM_2SS);
				
			}
			
			
			write_nic_dword(dev, RATR0, ratr_value);
			write_nic_byte(dev, UFWP, 1);
	}
	
	if(priv->btxpower_trackingInit && priv->btxpower_tracking){
		dm_txpower_reset_recovery(dev);
	}

	
	
	
	dm_bb_initialgain_restore(dev);

}	

static void dm_bb_initialgain_restore(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 bit_mask = 0x7f; 

	if(dm_digtable.dig_algorithm == DIG_ALGO_BY_RSSI)
		return;

	
	
	rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	
	rtl8192_setBBreg(dev, rOFDM0_XAAGCCore1, bit_mask, (u32)priv->initgain_backup.xaagccore1);
	rtl8192_setBBreg(dev, rOFDM0_XBAGCCore1, bit_mask, (u32)priv->initgain_backup.xbagccore1);
	rtl8192_setBBreg(dev, rOFDM0_XCAGCCore1, bit_mask, (u32)priv->initgain_backup.xcagccore1);
	rtl8192_setBBreg(dev, rOFDM0_XDAGCCore1, bit_mask, (u32)priv->initgain_backup.xdagccore1);
	bit_mask  = bMaskByte2;
	rtl8192_setBBreg(dev, rCCK0_CCA, bit_mask, (u32)priv->initgain_backup.cca);

	RT_TRACE(COMP_DIG, "dm_BBInitialGainRestore 0xc50 is %x\n",priv->initgain_backup.xaagccore1);
	RT_TRACE(COMP_DIG, "dm_BBInitialGainRestore 0xc58 is %x\n",priv->initgain_backup.xbagccore1);
	RT_TRACE(COMP_DIG, "dm_BBInitialGainRestore 0xc60 is %x\n",priv->initgain_backup.xcagccore1);
	RT_TRACE(COMP_DIG, "dm_BBInitialGainRestore 0xc68 is %x\n",priv->initgain_backup.xdagccore1);
	RT_TRACE(COMP_DIG, "dm_BBInitialGainRestore 0xa0a is %x\n",priv->initgain_backup.cca);
	
	
	rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x1);	

}	


extern void dm_backup_dynamic_mechanism_state(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	
	priv->bswitch_fsync  = false;
	priv->bfsync_processing = false;
	
	dm_bb_initialgain_backup(dev);

}	


static void dm_bb_initialgain_backup(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 bit_mask = bMaskByte0; 

	if(dm_digtable.dig_algorithm == DIG_ALGO_BY_RSSI)
		return;

	
	rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	
	priv->initgain_backup.xaagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XAAGCCore1, bit_mask);
	priv->initgain_backup.xbagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XBAGCCore1, bit_mask);
	priv->initgain_backup.xcagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XCAGCCore1, bit_mask);
	priv->initgain_backup.xdagccore1 = (u8)rtl8192_QueryBBReg(dev, rOFDM0_XDAGCCore1, bit_mask);
	bit_mask  = bMaskByte2;
	priv->initgain_backup.cca = (u8)rtl8192_QueryBBReg(dev, rCCK0_CCA, bit_mask);

	RT_TRACE(COMP_DIG, "BBInitialGainBackup 0xc50 is %x\n",priv->initgain_backup.xaagccore1);
	RT_TRACE(COMP_DIG, "BBInitialGainBackup 0xc58 is %x\n",priv->initgain_backup.xbagccore1);
	RT_TRACE(COMP_DIG, "BBInitialGainBackup 0xc60 is %x\n",priv->initgain_backup.xcagccore1);
	RT_TRACE(COMP_DIG, "BBInitialGainBackup 0xc68 is %x\n",priv->initgain_backup.xdagccore1);
	RT_TRACE(COMP_DIG, "BBInitialGainBackup 0xa0a is %x\n",priv->initgain_backup.cca);

}   

#endif
extern void dm_change_dynamic_initgain_thresh(struct net_device *dev,
								u32		dm_type,
								u32		dm_value)
{
	if (dm_type == DIG_TYPE_THRESH_HIGH)
	{
		dm_digtable.rssi_high_thresh = dm_value;
	}
	else if (dm_type == DIG_TYPE_THRESH_LOW)
	{
		dm_digtable.rssi_low_thresh = dm_value;
	}
	else if (dm_type == DIG_TYPE_THRESH_HIGHPWR_HIGH)
	{
		dm_digtable.rssi_high_power_highthresh = dm_value;
	}
	else if (dm_type == DIG_TYPE_THRESH_HIGHPWR_HIGH)
	{
		dm_digtable.rssi_high_power_highthresh = dm_value;
	}
	else if (dm_type == DIG_TYPE_ENABLE)
	{
		dm_digtable.dig_state		= DM_STA_DIG_MAX;
		dm_digtable.dig_enable_flag	= true;
	}
	else if (dm_type == DIG_TYPE_DISABLE)
	{
		dm_digtable.dig_state		= DM_STA_DIG_MAX;
		dm_digtable.dig_enable_flag	= false;
	}
	else if (dm_type == DIG_TYPE_DBG_MODE)
	{
		if(dm_value >= DM_DBG_MAX)
			dm_value = DM_DBG_OFF;
		dm_digtable.dbg_mode		= (u8)dm_value;
	}
	else if (dm_type == DIG_TYPE_RSSI)
	{
		if(dm_value > 100)
			dm_value = 30;
		dm_digtable.rssi_val			= (long)dm_value;
	}
	else if (dm_type == DIG_TYPE_ALGORITHM)
	{
		if (dm_value >= DIG_ALGO_MAX)
			dm_value = DIG_ALGO_BY_FALSE_ALARM;
		if(dm_digtable.dig_algorithm != (u8)dm_value)
			dm_digtable.dig_algorithm_switch = 1;
		dm_digtable.dig_algorithm	= (u8)dm_value;
	}
	else if (dm_type == DIG_TYPE_BACKOFF)
	{
		if(dm_value > 30)
			dm_value = 30;
		dm_digtable.backoff_val		= (u8)dm_value;
	}
	else if(dm_type == DIG_TYPE_RX_GAIN_MIN)
	{
		if(dm_value == 0)
			dm_value = 0x1;
		dm_digtable.rx_gain_range_min = (u8)dm_value;
	}
	else if(dm_type == DIG_TYPE_RX_GAIN_MAX)
	{
		if(dm_value > 0x50)
			dm_value = 0x50;
		dm_digtable.rx_gain_range_max = (u8)dm_value;
	}
}	
extern	void
dm_change_fsync_setting(
	struct net_device *dev,
	s32		DM_Type,
	s32		DM_Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (DM_Type == 0)	
	{
		if(DM_Value > 1)
			DM_Value = 1;
		priv->framesyncMonitor = (u8)DM_Value;
		
	}
}

extern void
dm_change_rxpath_selection_setting(
	struct net_device *dev,
	s32		DM_Type,
	s32		DM_Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	prate_adaptive 	pRA = (prate_adaptive)&(priv->rate_adaptive);


	if(DM_Type == 0)
	{
		if(DM_Value > 1)
			DM_Value = 1;
		DM_RxPathSelTable.Enable = (u8)DM_Value;
	}
	else if(DM_Type == 1)
	{
		if(DM_Value > 1)
			DM_Value = 1;
		DM_RxPathSelTable.DbgMode = (u8)DM_Value;
	}
	else if(DM_Type == 2)
	{
		if(DM_Value > 40)
			DM_Value = 40;
		DM_RxPathSelTable.SS_TH_low = (u8)DM_Value;
	}
	else if(DM_Type == 3)
	{
		if(DM_Value > 25)
			DM_Value = 25;
		DM_RxPathSelTable.diff_TH = (u8)DM_Value;
	}
	else if(DM_Type == 4)
	{
		if(DM_Value >= CCK_Rx_Version_MAX)
			DM_Value = CCK_Rx_Version_1;
		DM_RxPathSelTable.cck_method= (u8)DM_Value;
	}
	else if(DM_Type == 10)
	{
		if(DM_Value > 100)
			DM_Value = 50;
		DM_RxPathSelTable.rf_rssi[0] = (u8)DM_Value;
	}
	else if(DM_Type == 11)
	{
		if(DM_Value > 100)
			DM_Value = 50;
		DM_RxPathSelTable.rf_rssi[1] = (u8)DM_Value;
	}
	else if(DM_Type == 12)
	{
		if(DM_Value > 100)
			DM_Value = 50;
		DM_RxPathSelTable.rf_rssi[2] = (u8)DM_Value;
	}
	else if(DM_Type == 13)
	{
		if(DM_Value > 100)
			DM_Value = 50;
		DM_RxPathSelTable.rf_rssi[3] = (u8)DM_Value;
	}
	else if(DM_Type == 20)
	{
		if(DM_Value > 1)
			DM_Value = 1;
		pRA->ping_rssi_enable = (u8)DM_Value;
	}
	else if(DM_Type == 21)
	{
		if(DM_Value > 30)
			DM_Value = 30;
		pRA->ping_rssi_thresh_for_ra = DM_Value;
	}
}


static void dm_dig_init(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	
	dm_digtable.dig_enable_flag	= true;
	dm_digtable.dig_algorithm = DIG_ALGO_BY_RSSI;
	dm_digtable.dbg_mode = DM_DBG_OFF;	
	dm_digtable.dig_algorithm_switch = 0;

	
	dm_digtable.dig_state		= DM_STA_DIG_MAX;
	dm_digtable.dig_highpwr_state	= DM_STA_DIG_MAX;
	dm_digtable.initialgain_lowerbound_state = false;

	dm_digtable.rssi_low_thresh 	= DM_DIG_THRESH_LOW;
	dm_digtable.rssi_high_thresh 	= DM_DIG_THRESH_HIGH;

	dm_digtable.rssi_high_power_lowthresh = DM_DIG_HIGH_PWR_THRESH_LOW;
	dm_digtable.rssi_high_power_highthresh = DM_DIG_HIGH_PWR_THRESH_HIGH;

	dm_digtable.rssi_val = 50;	
	dm_digtable.backoff_val = DM_DIG_BACKOFF;
	dm_digtable.rx_gain_range_max = DM_DIG_MAX;
	if(priv->CustomerID == RT_CID_819x_Netcore)
		dm_digtable.rx_gain_range_min = DM_DIG_MIN_Netcore;
	else
		dm_digtable.rx_gain_range_min = DM_DIG_MIN;

}	


static void dm_ctrl_initgain_byrssi(struct net_device *dev)
{

	if (dm_digtable.dig_enable_flag == false)
		return;

	if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
		dm_ctrl_initgain_byrssi_by_fwfalse_alarm(dev);
	else if(dm_digtable.dig_algorithm == DIG_ALGO_BY_RSSI)
		dm_ctrl_initgain_byrssi_by_driverrssi(dev);
	else
		return;
}


static void dm_ctrl_initgain_byrssi_by_driverrssi(
	struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 i;
	static u8 	fw_dig=0;

	if (dm_digtable.dig_enable_flag == false)
		return;

	
	if(dm_digtable.dig_algorithm_switch)	
		fw_dig = 0;
	if(fw_dig <= 3)	
	{
		for(i=0; i<3; i++)
			rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	
		fw_dig++;
		dm_digtable.dig_state = DM_STA_DIG_OFF;	
	}

	if(priv->ieee80211->state == IEEE80211_LINKED)
		dm_digtable.cur_connect_state = DIG_CONNECT;
	else
		dm_digtable.cur_connect_state = DIG_DISCONNECT;

	
		

	if(dm_digtable.dbg_mode == DM_DBG_OFF)
		dm_digtable.rssi_val = priv->undecorated_smoothed_pwdb;
	
	dm_initial_gain(dev);
	dm_pd_th(dev);
	dm_cs_ratio(dev);
	if(dm_digtable.dig_algorithm_switch)
		dm_digtable.dig_algorithm_switch = 0;
	dm_digtable.pre_connect_state = dm_digtable.cur_connect_state;

}	

static void dm_ctrl_initgain_byrssi_by_fwfalse_alarm(
	struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	static u32 reset_cnt = 0;
	u8 i;

	if (dm_digtable.dig_enable_flag == false)
		return;

	if(dm_digtable.dig_algorithm_switch)
	{
		dm_digtable.dig_state = DM_STA_DIG_MAX;
		
		for(i=0; i<3; i++)
			rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x1);	
		dm_digtable.dig_algorithm_switch = 0;
	}

	if (priv->ieee80211->state != IEEE80211_LINKED)
		return;

	
	if ((priv->undecorated_smoothed_pwdb > dm_digtable.rssi_low_thresh) &&
		(priv->undecorated_smoothed_pwdb < dm_digtable.rssi_high_thresh))
	{
		return;
	}
	
	
	if ((priv->undecorated_smoothed_pwdb <= dm_digtable.rssi_low_thresh))
	{
		if (dm_digtable.dig_state == DM_STA_DIG_OFF &&
			(priv->reset_count == reset_cnt))
		{
			return;
		}
		else
		{
			reset_cnt = priv->reset_count;
		}

		
		dm_digtable.dig_highpwr_state = DM_STA_DIG_MAX;
		dm_digtable.dig_state = DM_STA_DIG_OFF;

		
		rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x8);	

		
		write_nic_byte(dev, rOFDM0_XAAGCCore1, 0x17);
		write_nic_byte(dev, rOFDM0_XBAGCCore1, 0x17);
		write_nic_byte(dev, rOFDM0_XCAGCCore1, 0x17);
		write_nic_byte(dev, rOFDM0_XDAGCCore1, 0x17);

		
		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			
			
			#ifdef RTL8190P
				write_nic_byte(dev, rOFDM0_RxDetector1, 0x40);
			#else
				write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x00);
			#endif
			


			
				
		}
		else
			write_nic_byte(dev, rOFDM0_RxDetector1, 0x42);

		
		write_nic_byte(dev, 0xa0a, 0x08);

		
		
		return;

	}

	if ((priv->undecorated_smoothed_pwdb >= dm_digtable.rssi_high_thresh) )
	{
		u8 reset_flag = 0;

		if (dm_digtable.dig_state == DM_STA_DIG_ON &&
			(priv->reset_count == reset_cnt))
		{
			dm_ctrl_initgain_byrssi_highpwr(dev);
			return;
		}
		else
		{
			if (priv->reset_count != reset_cnt)
				reset_flag = 1;

			reset_cnt = priv->reset_count;
		}

		dm_digtable.dig_state = DM_STA_DIG_ON;
		

		
		
		if (reset_flag == 1)
		{
			write_nic_byte(dev, rOFDM0_XAAGCCore1, 0x2c);
			write_nic_byte(dev, rOFDM0_XBAGCCore1, 0x2c);
			write_nic_byte(dev, rOFDM0_XCAGCCore1, 0x2c);
			write_nic_byte(dev, rOFDM0_XDAGCCore1, 0x2c);
		}
		else
		{
			write_nic_byte(dev, rOFDM0_XAAGCCore1, 0x20);
			write_nic_byte(dev, rOFDM0_XBAGCCore1, 0x20);
			write_nic_byte(dev, rOFDM0_XCAGCCore1, 0x20);
			write_nic_byte(dev, rOFDM0_XDAGCCore1, 0x20);
		}

		
		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			
			
			#ifdef RTL8190P
				write_nic_byte(dev, rOFDM0_RxDetector1, 0x42);
			#else
				write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x20);
			#endif
			

			
				
		}
		else
			write_nic_byte(dev, rOFDM0_RxDetector1, 0x44);

		
		write_nic_byte(dev, 0xa0a, 0xcd);

		
		
		

		
		rtl8192_setBBreg(dev, UFWP, bMaskByte1, 0x1);	

	}

	dm_ctrl_initgain_byrssi_highpwr(dev);

}	


static void dm_ctrl_initgain_byrssi_highpwr(
	struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	static u32 reset_cnt_highpwr = 0;

	
	if ((priv->undecorated_smoothed_pwdb > dm_digtable.rssi_high_power_lowthresh) &&
		(priv->undecorated_smoothed_pwdb < dm_digtable.rssi_high_power_highthresh))
	{
		return;
	}

	
	if (priv->undecorated_smoothed_pwdb >= dm_digtable.rssi_high_power_highthresh)
	{
		if (dm_digtable.dig_highpwr_state == DM_STA_DIG_ON &&
			(priv->reset_count == reset_cnt_highpwr))
			return;
		else
			dm_digtable.dig_highpwr_state = DM_STA_DIG_ON;

		
		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			#ifdef RTL8190P
				write_nic_byte(dev, rOFDM0_RxDetector1, 0x41);
			#else
				write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x10);
			#endif


		}
		else
			write_nic_byte(dev, rOFDM0_RxDetector1, 0x43);
	}
	else
	{
		if (dm_digtable.dig_highpwr_state == DM_STA_DIG_OFF&&
			(priv->reset_count == reset_cnt_highpwr))
			return;
		else
			dm_digtable.dig_highpwr_state = DM_STA_DIG_OFF;

		if (priv->undecorated_smoothed_pwdb < dm_digtable.rssi_high_power_lowthresh &&
			 priv->undecorated_smoothed_pwdb >= dm_digtable.rssi_high_thresh)
		{
			
			if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
			{
				#ifdef RTL8190P
					write_nic_byte(dev, rOFDM0_RxDetector1, 0x42);
				#else
					write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x20);
				#endif

			}
			else
				write_nic_byte(dev, rOFDM0_RxDetector1, 0x44);
		}
	}

	reset_cnt_highpwr = priv->reset_count;

}	


static void dm_initial_gain(
	struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8					initial_gain=0;
	static u8				initialized=0, force_write=0;
	static u32			reset_cnt=0;

	if(dm_digtable.dig_algorithm_switch)
	{
		initialized = 0;
		reset_cnt = 0;
	}

	if(dm_digtable.pre_connect_state == dm_digtable.cur_connect_state)
	{
		if(dm_digtable.cur_connect_state == DIG_CONNECT)
		{
			if((dm_digtable.rssi_val+10-dm_digtable.backoff_val) > dm_digtable.rx_gain_range_max)
				dm_digtable.cur_ig_value = dm_digtable.rx_gain_range_max;
			else if((dm_digtable.rssi_val+10-dm_digtable.backoff_val) < dm_digtable.rx_gain_range_min)
				dm_digtable.cur_ig_value = dm_digtable.rx_gain_range_min;
			else
				dm_digtable.cur_ig_value = dm_digtable.rssi_val+10-dm_digtable.backoff_val;
		}
		else		
		{
			if(dm_digtable.cur_ig_value == 0)
				dm_digtable.cur_ig_value = priv->DefaultInitialGain[0];
			else
				dm_digtable.cur_ig_value = dm_digtable.pre_ig_value;
		}
	}
	else	
	{
		dm_digtable.cur_ig_value = priv->DefaultInitialGain[0];
		dm_digtable.pre_ig_value = 0;
	}
	

	
	if(priv->reset_count != reset_cnt)
	{
		force_write = 1;
		reset_cnt = priv->reset_count;
	}

	if(dm_digtable.pre_ig_value != read_nic_byte(dev, rOFDM0_XAAGCCore1))
		force_write = 1;

	{
		if((dm_digtable.pre_ig_value != dm_digtable.cur_ig_value)
			|| !initialized || force_write)
		{
			initial_gain = (u8)dm_digtable.cur_ig_value;
			
			
			write_nic_byte(dev, rOFDM0_XAAGCCore1, initial_gain);
			write_nic_byte(dev, rOFDM0_XBAGCCore1, initial_gain);
			write_nic_byte(dev, rOFDM0_XCAGCCore1, initial_gain);
			write_nic_byte(dev, rOFDM0_XDAGCCore1, initial_gain);
			dm_digtable.pre_ig_value = dm_digtable.cur_ig_value;
			initialized = 1;
			force_write = 0;
		}
	}
}

static void dm_pd_th(
	struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	static u8				initialized=0, force_write=0;
	static u32			reset_cnt = 0;

	if(dm_digtable.dig_algorithm_switch)
	{
		initialized = 0;
		reset_cnt = 0;
	}

	if(dm_digtable.pre_connect_state == dm_digtable.cur_connect_state)
	{
		if(dm_digtable.cur_connect_state == DIG_CONNECT)
		{
			if (dm_digtable.rssi_val >= dm_digtable.rssi_high_power_highthresh)
				dm_digtable.curpd_thstate = DIG_PD_AT_HIGH_POWER;
			else if ((dm_digtable.rssi_val <= dm_digtable.rssi_low_thresh))
				dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
			else if ((dm_digtable.rssi_val >= dm_digtable.rssi_high_thresh) &&
					(dm_digtable.rssi_val < dm_digtable.rssi_high_power_lowthresh))
				dm_digtable.curpd_thstate = DIG_PD_AT_NORMAL_POWER;
			else
				dm_digtable.curpd_thstate = dm_digtable.prepd_thstate;
		}
		else
		{
			dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
		}
	}
	else	
	{
		dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
	}

	
	if(priv->reset_count != reset_cnt)
	{
		force_write = 1;
		reset_cnt = priv->reset_count;
	}

	{
		if((dm_digtable.prepd_thstate != dm_digtable.curpd_thstate) ||
			(initialized<=3) || force_write)
		{
			
			if(dm_digtable.curpd_thstate == DIG_PD_AT_LOW_POWER)
			{
				
				if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
				{
					
					
					#ifdef RTL8190P
						write_nic_byte(dev, rOFDM0_RxDetector1, 0x40);
					#else
						write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x00);
					#endif
				}
				else
					write_nic_byte(dev, rOFDM0_RxDetector1, 0x42);
			}
			else if(dm_digtable.curpd_thstate == DIG_PD_AT_NORMAL_POWER)
			{
				
				if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
				{
					
					
					#ifdef RTL8190P
						write_nic_byte(dev, rOFDM0_RxDetector1, 0x42);
					#else
						write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x20);
					#endif
				}
				else
					write_nic_byte(dev, rOFDM0_RxDetector1, 0x44);
			}
			else if(dm_digtable.curpd_thstate == DIG_PD_AT_HIGH_POWER)
			{
				
				if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
				{
					#ifdef RTL8190P
						write_nic_byte(dev, rOFDM0_RxDetector1, 0x41);
					#else
						write_nic_byte(dev, (rOFDM0_XATxAFE+3), 0x10);
					#endif
				}
				else
					write_nic_byte(dev, rOFDM0_RxDetector1, 0x43);
			}
			dm_digtable.prepd_thstate = dm_digtable.curpd_thstate;
			if(initialized <= 3)
				initialized++;
			force_write = 0;
		}
	}
}

static	void dm_cs_ratio(
	struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	static u8				initialized=0,force_write=0;
	static u32			reset_cnt = 0;

	if(dm_digtable.dig_algorithm_switch)
	{
		initialized = 0;
		reset_cnt = 0;
	}

	if(dm_digtable.pre_connect_state == dm_digtable.cur_connect_state)
	{
		if(dm_digtable.cur_connect_state == DIG_CONNECT)
		{
			if ((dm_digtable.rssi_val <= dm_digtable.rssi_low_thresh))
				dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
			else if ((dm_digtable.rssi_val >= dm_digtable.rssi_high_thresh) )
				dm_digtable.curcs_ratio_state = DIG_CS_RATIO_HIGHER;
			else
				dm_digtable.curcs_ratio_state = dm_digtable.precs_ratio_state;
		}
		else
		{
			dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
		}
	}
	else	
	{
		dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
	}

	
	if(priv->reset_count != reset_cnt)
	{
		force_write = 1;
		reset_cnt = priv->reset_count;
	}


	{
		if((dm_digtable.precs_ratio_state != dm_digtable.curcs_ratio_state) ||
			!initialized || force_write)
		{
			
			if(dm_digtable.curcs_ratio_state == DIG_CS_RATIO_LOWER)
			{
				
				write_nic_byte(dev, 0xa0a, 0x08);
			}
			else if(dm_digtable.curcs_ratio_state == DIG_CS_RATIO_HIGHER)
			{
				
				write_nic_byte(dev, 0xa0a, 0xcd);
			}
			dm_digtable.precs_ratio_state = dm_digtable.curcs_ratio_state;
			initialized = 1;
			force_write = 0;
		}
	}
}

extern void dm_init_edca_turbo(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->bcurrent_turbo_EDCA = false;
	priv->ieee80211->bis_any_nonbepkts = false;
	priv->bis_cur_rdlstate = false;
}	

static void dm_check_edca_turbo(
	struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	

	
	static unsigned long			lastTxOkCnt = 0;
	static unsigned long			lastRxOkCnt = 0;
	unsigned long				curTxOkCnt = 0;
	unsigned long				curRxOkCnt = 0;

	
	
	
	
	if(priv->ieee80211->state != IEEE80211_LINKED)
		goto dm_CheckEdcaTurbo_EXIT;
	
	if(priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_EDCA_TURBO)
		goto dm_CheckEdcaTurbo_EXIT;

	
	if(!priv->ieee80211->bis_any_nonbepkts)
	{
		curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
		curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;
		
		if(curRxOkCnt > 4*curTxOkCnt)
		{
			
			if(!priv->bis_cur_rdlstate || !priv->bcurrent_turbo_EDCA)
			{
				write_nic_dword(dev, EDCAPARA_BE, edca_setting_DL[pHTInfo->IOTPeer]);
				priv->bis_cur_rdlstate = true;
			}
		}
		else
		{

			
			if(priv->bis_cur_rdlstate || !priv->bcurrent_turbo_EDCA)
			{
				write_nic_dword(dev, EDCAPARA_BE, edca_setting_UL[pHTInfo->IOTPeer]);
				priv->bis_cur_rdlstate = false;
			}

		}

		priv->bcurrent_turbo_EDCA = true;
	}
	else
	{
		
		
		
		
		 if(priv->bcurrent_turbo_EDCA)
		{

			{
				u8		u1bAIFS;
				u32		u4bAcParam;
				struct ieee80211_qos_parameters *qos_parameters = &priv->ieee80211->current_network.qos_data.parameters;
				u8 mode = priv->ieee80211->mode;

			
				dm_init_edca_turbo(dev);
				u1bAIFS = qos_parameters->aifs[0] * ((mode&(IEEE_G|IEEE_N_24G)) ?9:20) + aSifsTime;
				u4bAcParam = ((((u32)(qos_parameters->tx_op_limit[0]))<< AC_PARAM_TXOP_LIMIT_OFFSET)|
					(((u32)(qos_parameters->cw_max[0]))<< AC_PARAM_ECW_MAX_OFFSET)|
					(((u32)(qos_parameters->cw_min[0]))<< AC_PARAM_ECW_MIN_OFFSET)|
					((u32)u1bAIFS << AC_PARAM_AIFS_OFFSET));
			
				write_nic_dword(dev, EDCAPARA_BE,  u4bAcParam);

			
			
				{
			

					PACI_AIFSN	pAciAifsn = (PACI_AIFSN)&(qos_parameters->aifs[0]);
					u8		AcmCtrl = read_nic_byte( dev, AcmHwCtrl );
					if( pAciAifsn->f.ACM )
					{ 
						AcmCtrl |= AcmHw_BeqEn;
					}
					else
					{ 
						AcmCtrl &= (~AcmHw_BeqEn);
					}

					RT_TRACE( COMP_QOS,"SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl ) ;
					write_nic_byte(dev, AcmHwCtrl, AcmCtrl );
				}
			}
			priv->bcurrent_turbo_EDCA = false;
		}
	}


dm_CheckEdcaTurbo_EXIT:
	
	priv->ieee80211->bis_any_nonbepkts = false;
	lastTxOkCnt = priv->stats.txbytesunicast;
	lastRxOkCnt = priv->stats.rxbytesunicast;
}	

extern void DM_CTSToSelfSetting(struct net_device * dev,u32 DM_Type, u32 DM_Value)
{
	struct r8192_priv *priv = ieee80211_priv((struct net_device *)dev);

	if (DM_Type == 0)	
	{
		if(DM_Value > 1)
			DM_Value = 1;
		priv->ieee80211->bCTSToSelfEnable = (bool)DM_Value;
		
	}
	else if(DM_Type == 1) 
	{
		if(DM_Value >= 50)
			DM_Value = 50;
		priv->ieee80211->CTSToSelfTH = (u8)DM_Value;
		
	}
}

static void dm_init_ctstoself(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv((struct net_device *)dev);

	priv->ieee80211->bCTSToSelfEnable = TRUE;
	priv->ieee80211->CTSToSelfTH = CTSToSelfTHVal;
}

static void dm_ctstoself(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv((struct net_device *)dev);
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	static unsigned long				lastTxOkCnt = 0;
	static unsigned long				lastRxOkCnt = 0;
	unsigned long						curTxOkCnt = 0;
	unsigned long						curRxOkCnt = 0;

	if(priv->ieee80211->bCTSToSelfEnable != TRUE)
	{
		pHTInfo->IOTAction &= ~HT_IOT_ACT_FORCED_CTS2SELF;
		return;
	}

	if(pHTInfo->IOTPeer == HT_IOT_PEER_BROADCOM)
	{
		curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
		curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;
		if(curRxOkCnt > 4*curTxOkCnt)	
		{
			pHTInfo->IOTAction &= ~HT_IOT_ACT_FORCED_CTS2SELF;
			
		}
		else	
		{
			pHTInfo->IOTAction |= HT_IOT_ACT_FORCED_CTS2SELF;
		}

		lastTxOkCnt = priv->stats.txbytesunicast;
		lastRxOkCnt = priv->stats.rxbytesunicast;
	}
}


static void dm_check_rfctrl_gpio(struct net_device * dev)
{
	

	
	
	

	
#ifdef RTL8190P
	return;
#endif
#ifdef RTL8192U
	return;
#endif
#ifdef RTL8192E
		queue_delayed_work(priv->priv_wq,&priv->gpio_change_rf_wq,0);
#endif

}	

static	void	dm_check_pbc_gpio(struct net_device *dev)
{
#ifdef RTL8192U
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8 tmp1byte;


	tmp1byte = read_nic_byte(dev,GPI);
	if(tmp1byte == 0xff)
		return;

	if (tmp1byte&BIT6 || tmp1byte&BIT0)
	{
		
		
		RT_TRACE(COMP_IO, "CheckPbcGPIO - PBC is pressed\n");
		priv->bpbc_pressed = true;
	}
#endif

}

#ifdef RTL8192E

extern	void	dm_gpio_change_rf_callback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
       struct r8192_priv *priv = container_of(dwork,struct r8192_priv,gpio_change_rf_wq);
       struct net_device *dev = priv->ieee80211->dev;
	u8 tmp1byte;
	RT_RF_POWER_STATE	eRfPowerStateToSet;
	bool bActuallySet = false;

	do{
		bActuallySet=false;

		if(!priv->up)
		{
			RT_TRACE((COMP_INIT | COMP_POWER | COMP_RF),"dm_gpio_change_rf_callback(): Callback function breaks out!!\n");
		}
		else
		{
			
			
			tmp1byte = read_nic_byte(dev,GPI);

			eRfPowerStateToSet = (tmp1byte&BIT1) ?  eRfOn : eRfOff;

			if( (priv->bHwRadioOff == true) && (eRfPowerStateToSet == eRfOn))
			{
				RT_TRACE(COMP_RF, "gpiochangeRF  - HW Radio ON\n");

				priv->bHwRadioOff = false;
				bActuallySet = true;
			}
			else if ( (priv->bHwRadioOff == false) && (eRfPowerStateToSet == eRfOff))
			{
				RT_TRACE(COMP_RF, "gpiochangeRF  - HW Radio OFF\n");
				priv->bHwRadioOff = true;
				bActuallySet = true;
			}

			if(bActuallySet)
			{
				#ifdef TO_DO
				MgntActSet_RF_State(dev, eRfPowerStateToSet, RF_CHANGE_BY_HW);
				
				#endif
			}
			else
			{
				msleep(2000);
			}

		}
	}while(TRUE)

}	

#endif
extern	void	dm_rf_pathcheck_workitemcallback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
       struct r8192_priv *priv = container_of(dwork,struct r8192_priv,rfpath_check_wq);
       struct net_device *dev =priv->ieee80211->dev;
	
	u8 rfpath = 0, i;


	rfpath = read_nic_byte(dev, 0xc04);

	
	for (i = 0; i < RF90_PATH_MAX; i++)
	{
		if (rfpath & (0x01<<i))
			priv->brfpath_rxenable[i] = 1;
		else
			priv->brfpath_rxenable[i] = 0;
	}
	if(!DM_RxPathSelTable.Enable)
		return;

	dm_rxpath_sel_byrssi(dev);
}	

static void dm_init_rxpath_selection(struct net_device * dev)
{
	u8 i;
	struct r8192_priv *priv = ieee80211_priv(dev);
	DM_RxPathSelTable.Enable = 1;	
	DM_RxPathSelTable.SS_TH_low = RxPathSelection_SS_TH_low;
	DM_RxPathSelTable.diff_TH = RxPathSelection_diff_TH;
	if(priv->CustomerID == RT_CID_819x_Netcore)
		DM_RxPathSelTable.cck_method = CCK_Rx_Version_2;
	else
		DM_RxPathSelTable.cck_method = CCK_Rx_Version_1;
	DM_RxPathSelTable.DbgMode = DM_DBG_OFF;
	DM_RxPathSelTable.disabledRF = 0;
	for(i=0; i<4; i++)
	{
		DM_RxPathSelTable.rf_rssi[i] = 50;
		DM_RxPathSelTable.cck_pwdb_sta[i] = -64;
		DM_RxPathSelTable.rf_enable_rssi_th[i] = 100;
	}
}

static void dm_rxpath_sel_byrssi(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u8				i, max_rssi_index=0, min_rssi_index=0, sec_rssi_index=0, rf_num=0;
	u8				tmp_max_rssi=0, tmp_min_rssi=0, tmp_sec_rssi=0;
	u8				cck_default_Rx=0x2;	
	u8				cck_optional_Rx=0x3;
	long				tmp_cck_max_pwdb=0, tmp_cck_min_pwdb=0, tmp_cck_sec_pwdb=0;
	u8				cck_rx_ver2_max_index=0, cck_rx_ver2_min_index=0, cck_rx_ver2_sec_index=0;
	u8				cur_rf_rssi;
	long				cur_cck_pwdb;
	static u8			disabled_rf_cnt=0, cck_Rx_Path_initialized=0;
	u8				update_cck_rx_path;

	if(priv->rf_type != RF_2T4R)
		return;

	if(!cck_Rx_Path_initialized)
	{
		DM_RxPathSelTable.cck_Rx_path = (read_nic_byte(dev, 0xa07)&0xf);
		cck_Rx_Path_initialized = 1;
	}

	DM_RxPathSelTable.disabledRF = 0xf;
	DM_RxPathSelTable.disabledRF &=~ (read_nic_byte(dev, 0xc04));

	if(priv->ieee80211->mode == WIRELESS_MODE_B)
	{
		DM_RxPathSelTable.cck_method = CCK_Rx_Version_2;	
		
	}

	
	for (i=0; i<RF90_PATH_MAX; i++)
	{
		if(!DM_RxPathSelTable.DbgMode)
			DM_RxPathSelTable.rf_rssi[i] = priv->stats.rx_rssi_percentage[i];

		if(priv->brfpath_rxenable[i])
		{
			rf_num++;
			cur_rf_rssi = DM_RxPathSelTable.rf_rssi[i];

			if(rf_num == 1)	
			{	
				max_rssi_index = min_rssi_index = sec_rssi_index = i;
				tmp_max_rssi = tmp_min_rssi = tmp_sec_rssi = cur_rf_rssi;
			}
			else if(rf_num == 2)
			{	
				if(cur_rf_rssi >= tmp_max_rssi)
				{
					tmp_max_rssi = cur_rf_rssi;
					max_rssi_index = i;
				}
				else
				{
					tmp_sec_rssi = tmp_min_rssi = cur_rf_rssi;
					sec_rssi_index = min_rssi_index = i;
				}
			}
			else
			{
				if(cur_rf_rssi > tmp_max_rssi)
				{
					tmp_sec_rssi = tmp_max_rssi;
					sec_rssi_index = max_rssi_index;
					tmp_max_rssi = cur_rf_rssi;
					max_rssi_index = i;
				}
				else if(cur_rf_rssi == tmp_max_rssi)
				{	
					tmp_sec_rssi = cur_rf_rssi;
					sec_rssi_index = i;
				}
				else if((cur_rf_rssi < tmp_max_rssi) &&(cur_rf_rssi > tmp_sec_rssi))
				{
					tmp_sec_rssi = cur_rf_rssi;
					sec_rssi_index = i;
				}
				else if(cur_rf_rssi == tmp_sec_rssi)
				{
					if(tmp_sec_rssi == tmp_min_rssi)
					{	
						tmp_sec_rssi = cur_rf_rssi;
						sec_rssi_index = i;
					}
					else
					{
						
					}
				}
				else if((cur_rf_rssi < tmp_sec_rssi) && (cur_rf_rssi > tmp_min_rssi))
				{
					
				}
				else if(cur_rf_rssi == tmp_min_rssi)
				{
					if(tmp_sec_rssi == tmp_min_rssi)
					{	
						tmp_min_rssi = cur_rf_rssi;
						min_rssi_index = i;
					}
					else
					{
						
					}
				}
				else if(cur_rf_rssi < tmp_min_rssi)
				{
					tmp_min_rssi = cur_rf_rssi;
					min_rssi_index = i;
				}
			}
		}
	}

	rf_num = 0;
	
	if(DM_RxPathSelTable.cck_method == CCK_Rx_Version_2)
	{
		for (i=0; i<RF90_PATH_MAX; i++)
		{
			if(priv->brfpath_rxenable[i])
			{
				rf_num++;
				cur_cck_pwdb =  DM_RxPathSelTable.cck_pwdb_sta[i];

				if(rf_num == 1)	
				{	
					cck_rx_ver2_max_index = cck_rx_ver2_min_index = cck_rx_ver2_sec_index = i;
					tmp_cck_max_pwdb = tmp_cck_min_pwdb = tmp_cck_sec_pwdb = cur_cck_pwdb;
				}
				else if(rf_num == 2)
				{	
					if(cur_cck_pwdb >= tmp_cck_max_pwdb)
					{
						tmp_cck_max_pwdb = cur_cck_pwdb;
						cck_rx_ver2_max_index = i;
					}
					else
					{
						tmp_cck_sec_pwdb = tmp_cck_min_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = cck_rx_ver2_min_index = i;
					}
				}
				else
				{
					if(cur_cck_pwdb > tmp_cck_max_pwdb)
					{
						tmp_cck_sec_pwdb = tmp_cck_max_pwdb;
						cck_rx_ver2_sec_index = cck_rx_ver2_max_index;
						tmp_cck_max_pwdb = cur_cck_pwdb;
						cck_rx_ver2_max_index = i;
					}
					else if(cur_cck_pwdb == tmp_cck_max_pwdb)
					{	
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					}
					else if((cur_cck_pwdb < tmp_cck_max_pwdb) &&(cur_cck_pwdb > tmp_cck_sec_pwdb))
					{
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					}
					else if(cur_cck_pwdb == tmp_cck_sec_pwdb)
					{
						if(tmp_cck_sec_pwdb == tmp_cck_min_pwdb)
						{	
							tmp_cck_sec_pwdb = cur_cck_pwdb;
							cck_rx_ver2_sec_index = i;
						}
						else
						{
							
						}
					}
					else if((cur_cck_pwdb < tmp_cck_sec_pwdb) && (cur_cck_pwdb > tmp_cck_min_pwdb))
					{
						
					}
					else if(cur_cck_pwdb == tmp_cck_min_pwdb)
					{
						if(tmp_cck_sec_pwdb == tmp_cck_min_pwdb)
						{	
							tmp_cck_min_pwdb = cur_cck_pwdb;
							cck_rx_ver2_min_index = i;
						}
						else
						{
							
						}
					}
					else if(cur_cck_pwdb < tmp_cck_min_pwdb)
					{
						tmp_cck_min_pwdb = cur_cck_pwdb;
						cck_rx_ver2_min_index = i;
					}
				}

			}
		}
	}


	
	
	update_cck_rx_path = 0;
	if(DM_RxPathSelTable.cck_method == CCK_Rx_Version_2)
	{
		cck_default_Rx = cck_rx_ver2_max_index;
		cck_optional_Rx = cck_rx_ver2_sec_index;
		if(tmp_cck_max_pwdb != -64)
			update_cck_rx_path = 1;
	}

	if(tmp_min_rssi < DM_RxPathSelTable.SS_TH_low && disabled_rf_cnt < 2)
	{
		if((tmp_max_rssi - tmp_min_rssi) >= DM_RxPathSelTable.diff_TH)
		{
			
			DM_RxPathSelTable.rf_enable_rssi_th[min_rssi_index] = tmp_max_rssi+5;
			
			rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0x1<<min_rssi_index, 0x0);	
			rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0x1<<min_rssi_index, 0x0);	
			disabled_rf_cnt++;
		}
		if(DM_RxPathSelTable.cck_method == CCK_Rx_Version_1)
		{
			cck_default_Rx = max_rssi_index;
			cck_optional_Rx = sec_rssi_index;
			if(tmp_max_rssi)
				update_cck_rx_path = 1;
		}
	}

	if(update_cck_rx_path)
	{
		DM_RxPathSelTable.cck_Rx_path = (cck_default_Rx<<2)|(cck_optional_Rx);
		rtl8192_setBBreg(dev, rCCK0_AFESetting, 0x0f000000, DM_RxPathSelTable.cck_Rx_path);
	}

	if(DM_RxPathSelTable.disabledRF)
	{
		for(i=0; i<4; i++)
		{
			if((DM_RxPathSelTable.disabledRF>>i) & 0x1)	
			{
				if(tmp_max_rssi >= DM_RxPathSelTable.rf_enable_rssi_th[i])
				{
					
					
					rtl8192_setBBreg(dev, rOFDM0_TRxPathEnable, 0x1<<i, 0x1);	
					rtl8192_setBBreg(dev, rOFDM1_TRxPathEnable, 0x1<<i, 0x1);	
					DM_RxPathSelTable.rf_enable_rssi_th[i] = 100;
					disabled_rf_cnt--;
				}
			}
		}
	}
}

static	void	dm_check_rx_path_selection(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	queue_delayed_work(priv->priv_wq,&priv->rfpath_check_wq,0);
}	


static void dm_init_fsync (struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->ieee80211->fsync_time_interval = 500;
	priv->ieee80211->fsync_rate_bitmap = 0x0f000800;
	priv->ieee80211->fsync_rssi_threshold = 30;
#ifdef RTL8190P
	priv->ieee80211->bfsync_enable = true;
#else
	priv->ieee80211->bfsync_enable = false;
#endif
	priv->ieee80211->fsync_multiple_timeinterval = 3;
	priv->ieee80211->fsync_firstdiff_ratethreshold= 100;
	priv->ieee80211->fsync_seconddiff_ratethreshold= 200;
	priv->ieee80211->fsync_state = Default_Fsync;
	priv->framesyncMonitor = 1;	

	init_timer(&priv->fsync_timer);
	priv->fsync_timer.data = (unsigned long)dev;
	priv->fsync_timer.function = dm_fsync_timer_callback;
}


static void dm_deInit_fsync(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	del_timer_sync(&priv->fsync_timer);
}

extern void dm_fsync_timer_callback(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct r8192_priv *priv = ieee80211_priv((struct net_device *)data);
	u32 rate_index, rate_count = 0, rate_count_diff=0;
	bool		bSwitchFromCountDiff = false;
	bool		bDoubleTimeInterval = false;

	if(	priv->ieee80211->state == IEEE80211_LINKED &&
		priv->ieee80211->bfsync_enable &&
		(priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_CDD_FSYNC))
	{
		 
		u32 rate_bitmap;
		for(rate_index = 0; rate_index <= 27; rate_index++)
		{
			rate_bitmap  = 1 << rate_index;
			if(priv->ieee80211->fsync_rate_bitmap &  rate_bitmap)
				rate_count+= priv->stats.received_rate_histogram[1][rate_index];
		}

		if(rate_count < priv->rate_record)
			rate_count_diff = 0xffffffff - rate_count + priv->rate_record;
		else
			rate_count_diff = rate_count - priv->rate_record;
		if(rate_count_diff < priv->rateCountDiffRecord)
		{

			u32 DiffNum = priv->rateCountDiffRecord - rate_count_diff;
			
			if(DiffNum >= priv->ieee80211->fsync_seconddiff_ratethreshold)
				priv->ContiuneDiffCount++;
			else
				priv->ContiuneDiffCount = 0;

			
			if(priv->ContiuneDiffCount >=2)
			{
				bSwitchFromCountDiff = true;
				priv->ContiuneDiffCount = 0;
			}
		}
		else
		{
			
			priv->ContiuneDiffCount = 0;
		}

		
		if(rate_count_diff <= priv->ieee80211->fsync_firstdiff_ratethreshold)
		{
			bSwitchFromCountDiff = true;
			priv->ContiuneDiffCount = 0;
		}
		priv->rate_record = rate_count;
		priv->rateCountDiffRecord = rate_count_diff;
		RT_TRACE(COMP_HALDM, "rateRecord %d rateCount %d, rateCountdiff %d bSwitchFsync %d\n", priv->rate_record, rate_count, rate_count_diff , priv->bswitch_fsync);
		
		if(priv->undecorated_smoothed_pwdb > priv->ieee80211->fsync_rssi_threshold && bSwitchFromCountDiff)
		{
			bDoubleTimeInterval = true;
			priv->bswitch_fsync = !priv->bswitch_fsync;
			if(priv->bswitch_fsync)
			{
			#ifdef RTL8190P
				write_nic_byte(dev, 0xC36, 0x00);
			#else
				write_nic_byte(dev,0xC36, 0x1c);
			#endif
				write_nic_byte(dev, 0xC3e, 0x90);
			}
			else
			{
			#ifdef RTL8190P
				write_nic_byte(dev, 0xC36, 0x40);
			#else
				write_nic_byte(dev, 0xC36, 0x5c);
			#endif
				write_nic_byte(dev, 0xC3e, 0x96);
			}
		}
		else if(priv->undecorated_smoothed_pwdb <= priv->ieee80211->fsync_rssi_threshold)
		{
			if(priv->bswitch_fsync)
			{
				priv->bswitch_fsync  = false;
			#ifdef RTL8190P
				write_nic_byte(dev, 0xC36, 0x40);
			#else
				write_nic_byte(dev, 0xC36, 0x5c);
			#endif
				write_nic_byte(dev, 0xC3e, 0x96);
			}
		}
		if(bDoubleTimeInterval){
			if(timer_pending(&priv->fsync_timer))
				del_timer_sync(&priv->fsync_timer);
			priv->fsync_timer.expires = jiffies + MSECS(priv->ieee80211->fsync_time_interval*priv->ieee80211->fsync_multiple_timeinterval);
			add_timer(&priv->fsync_timer);
		}
		else{
			if(timer_pending(&priv->fsync_timer))
				del_timer_sync(&priv->fsync_timer);
			priv->fsync_timer.expires = jiffies + MSECS(priv->ieee80211->fsync_time_interval);
			add_timer(&priv->fsync_timer);
		}
	}
	else
	{
		
		if(priv->bswitch_fsync)
		{
			priv->bswitch_fsync  = false;
		#ifdef RTL8190P
			write_nic_byte(dev, 0xC36, 0x40);
		#else
			write_nic_byte(dev, 0xC36, 0x5c);
		#endif
			write_nic_byte(dev, 0xC3e, 0x96);
		}
		priv->ContiuneDiffCount = 0;
	#ifdef RTL8190P
		write_nic_dword(dev, rOFDM0_RxDetector2, 0x164052cd);
	#else
		write_nic_dword(dev, rOFDM0_RxDetector2, 0x465c52cd);
	#endif
	}
	RT_TRACE(COMP_HALDM, "ContiuneDiffCount %d\n", priv->ContiuneDiffCount);
	RT_TRACE(COMP_HALDM, "rateRecord %d rateCount %d, rateCountdiff %d bSwitchFsync %d\n", priv->rate_record, rate_count, rate_count_diff , priv->bswitch_fsync);
}

static void dm_StartHWFsync(struct net_device *dev)
{
	RT_TRACE(COMP_HALDM, "%s\n", __FUNCTION__);
	write_nic_dword(dev, rOFDM0_RxDetector2, 0x465c12cf);
	write_nic_byte(dev, 0xc3b, 0x41);
}

static void dm_EndSWFsync(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	RT_TRACE(COMP_HALDM, "%s\n", __FUNCTION__);
	del_timer_sync(&(priv->fsync_timer));

	
	if(priv->bswitch_fsync)
	{
		priv->bswitch_fsync  = false;

		#ifdef RTL8190P
			write_nic_byte(dev, 0xC36, 0x40);
		#else
			write_nic_byte(dev, 0xC36, 0x5c);
		#endif

		write_nic_byte(dev, 0xC3e, 0x96);
	}

	priv->ContiuneDiffCount = 0;
#ifndef RTL8190P
	write_nic_dword(dev, rOFDM0_RxDetector2, 0x465c52cd);
#endif

}

static void dm_StartSWFsync(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	u32 			rateIndex;
	u32 			rateBitmap;

	RT_TRACE(COMP_HALDM,"%s\n", __FUNCTION__);
	
	priv->rate_record = 0;
	
	priv->ContiuneDiffCount = 0;
	priv->rateCountDiffRecord = 0;
	priv->bswitch_fsync  = false;

	if(priv->ieee80211->mode == WIRELESS_MODE_N_24G)
	{
		priv->ieee80211->fsync_firstdiff_ratethreshold= 600;
		priv->ieee80211->fsync_seconddiff_ratethreshold = 0xffff;
	}
	else
	{
		priv->ieee80211->fsync_firstdiff_ratethreshold= 200;
		priv->ieee80211->fsync_seconddiff_ratethreshold = 200;
	}
	for(rateIndex = 0; rateIndex <= 27; rateIndex++)
	{
		rateBitmap  = 1 << rateIndex;
		if(priv->ieee80211->fsync_rate_bitmap &  rateBitmap)
			priv->rate_record += priv->stats.received_rate_histogram[1][rateIndex];
	}
	if(timer_pending(&priv->fsync_timer))
		del_timer_sync(&priv->fsync_timer);
	priv->fsync_timer.expires = jiffies + MSECS(priv->ieee80211->fsync_time_interval);
	add_timer(&priv->fsync_timer);

#ifndef RTL8190P
	write_nic_dword(dev, rOFDM0_RxDetector2, 0x465c12cd);
#endif

}

static void dm_EndHWFsync(struct net_device *dev)
{
	RT_TRACE(COMP_HALDM,"%s\n", __FUNCTION__);
	write_nic_dword(dev, rOFDM0_RxDetector2, 0x465c52cd);
	write_nic_byte(dev, 0xc3b, 0x49);

}

void dm_check_fsync(struct net_device *dev)
{
#define	RegC38_Default				0
#define	RegC38_NonFsync_Other_AP	1
#define	RegC38_Fsync_AP_BCM		2
	struct r8192_priv *priv = ieee80211_priv(dev);
	
	static u8		reg_c38_State=RegC38_Default;
	static u32	reset_cnt=0;

	RT_TRACE(COMP_HALDM, "RSSI %d TimeInterval %d MultipleTimeInterval %d\n", priv->ieee80211->fsync_rssi_threshold, priv->ieee80211->fsync_time_interval, priv->ieee80211->fsync_multiple_timeinterval);
	RT_TRACE(COMP_HALDM, "RateBitmap 0x%x FirstDiffRateThreshold %d SecondDiffRateThreshold %d\n", priv->ieee80211->fsync_rate_bitmap, priv->ieee80211->fsync_firstdiff_ratethreshold, priv->ieee80211->fsync_seconddiff_ratethreshold);

	if(	priv->ieee80211->state == IEEE80211_LINKED &&
		(priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_CDD_FSYNC))
	{
		if(priv->ieee80211->bfsync_enable == 0)
		{
			switch(priv->ieee80211->fsync_state)
			{
				case Default_Fsync:
					dm_StartHWFsync(dev);
					priv->ieee80211->fsync_state = HW_Fsync;
					break;
				case SW_Fsync:
					dm_EndSWFsync(dev);
					dm_StartHWFsync(dev);
					priv->ieee80211->fsync_state = HW_Fsync;
					break;
				case HW_Fsync:
				default:
					break;
			}
		}
		else
		{
			switch(priv->ieee80211->fsync_state)
			{
				case Default_Fsync:
					dm_StartSWFsync(dev);
					priv->ieee80211->fsync_state = SW_Fsync;
					break;
				case HW_Fsync:
					dm_EndHWFsync(dev);
					dm_StartSWFsync(dev);
					priv->ieee80211->fsync_state = SW_Fsync;
					break;
				case SW_Fsync:
				default:
					break;

			}
		}
		if(priv->framesyncMonitor)
		{
			if(reg_c38_State != RegC38_Fsync_AP_BCM)
			{	
				#ifdef RTL8190P
					write_nic_byte(dev, rOFDM0_RxDetector3, 0x15);
				#else
					write_nic_byte(dev, rOFDM0_RxDetector3, 0x95);
				#endif

				reg_c38_State = RegC38_Fsync_AP_BCM;
			}
		}
	}
	else
	{
		switch(priv->ieee80211->fsync_state)
		{
			case HW_Fsync:
				dm_EndHWFsync(dev);
				priv->ieee80211->fsync_state = Default_Fsync;
				break;
			case SW_Fsync:
				dm_EndSWFsync(dev);
				priv->ieee80211->fsync_state = Default_Fsync;
				break;
			case Default_Fsync:
			default:
				break;
		}

		if(priv->framesyncMonitor)
		{
			if(priv->ieee80211->state == IEEE80211_LINKED)
			{
				if(priv->undecorated_smoothed_pwdb <= RegC38_TH)
				{
					if(reg_c38_State != RegC38_NonFsync_Other_AP)
					{
						#ifdef RTL8190P
							write_nic_byte(dev, rOFDM0_RxDetector3, 0x10);
						#else
							write_nic_byte(dev, rOFDM0_RxDetector3, 0x90);
						#endif

						reg_c38_State = RegC38_NonFsync_Other_AP;
					}
				}
				else if(priv->undecorated_smoothed_pwdb >= (RegC38_TH+5))
				{
					if(reg_c38_State)
					{
						write_nic_byte(dev, rOFDM0_RxDetector3, priv->framesync);
						reg_c38_State = RegC38_Default;
						
					}
				}
			}
			else
			{
				if(reg_c38_State)
				{
					write_nic_byte(dev, rOFDM0_RxDetector3, priv->framesync);
					reg_c38_State = RegC38_Default;
					
				}
			}
		}
	}
	if(priv->framesyncMonitor)
	{
		if(priv->reset_count != reset_cnt)
		{	
			write_nic_byte(dev, rOFDM0_RxDetector3, priv->framesync);
			reg_c38_State = RegC38_Default;
			reset_cnt = priv->reset_count;
			
		}
	}
	else
	{
		if(reg_c38_State)
		{
			write_nic_byte(dev, rOFDM0_RxDetector3, priv->framesync);
			reg_c38_State = RegC38_Default;
			
		}
	}
}


extern void dm_shadow_init(struct net_device *dev)
{
	u8	page;
	u16	offset;

	for (page = 0; page < 5; page++)
		for (offset = 0; offset < 256; offset++)
		{
			dm_shadow[page][offset] = read_nic_byte(dev, offset+page*256);
			
		}

	for (page = 8; page < 11; page++)
		for (offset = 0; offset < 256; offset++)
			dm_shadow[page][offset] = read_nic_byte(dev, offset+page*256);

	for (page = 12; page < 15; page++)
		for (offset = 0; offset < 256; offset++)
			dm_shadow[page][offset] = read_nic_byte(dev, offset+page*256);

}   

static void dm_init_dynamic_txpower(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	
	priv->ieee80211->bdynamic_txpower_enable = true;    
	priv->bLastDTPFlag_High = false;
	priv->bLastDTPFlag_Low = false;
	priv->bDynamicTxHighPower = false;
	priv->bDynamicTxLowPower = false;
}

static void dm_dynamic_txpower(struct net_device *dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	unsigned int txhipower_threshhold=0;
	unsigned int txlowpower_threshold=0;
	if(priv->ieee80211->bdynamic_txpower_enable != true)
	{
		priv->bDynamicTxHighPower = false;
		priv->bDynamicTxLowPower = false;
		return;
	}
	
	if((priv->ieee80211->current_network.atheros_cap_exist ) && (priv->ieee80211->mode == IEEE_G)){
		txhipower_threshhold = TX_POWER_ATHEROAP_THRESH_HIGH;
		txlowpower_threshold = TX_POWER_ATHEROAP_THRESH_LOW;
	}
	else
	{
		txhipower_threshhold = TX_POWER_NEAR_FIELD_THRESH_HIGH;
		txlowpower_threshold = TX_POWER_NEAR_FIELD_THRESH_LOW;
	}

	RT_TRACE(COMP_TXAGC,"priv->undecorated_smoothed_pwdb = %ld \n" , priv->undecorated_smoothed_pwdb);

	if(priv->ieee80211->state == IEEE80211_LINKED)
	{
		if(priv->undecorated_smoothed_pwdb >= txhipower_threshhold)
		{
			priv->bDynamicTxHighPower = true;
			priv->bDynamicTxLowPower = false;
		}
		else
		{
			
			if(priv->undecorated_smoothed_pwdb < txlowpower_threshold && priv->bDynamicTxHighPower == true)
			{
				priv->bDynamicTxHighPower = false;
			}
			
			if(priv->undecorated_smoothed_pwdb < 35)
			{
				priv->bDynamicTxLowPower = true;
			}
			else if(priv->undecorated_smoothed_pwdb >= 40)
			{
				priv->bDynamicTxLowPower = false;
			}
		}
	}
	else
	{
		
		priv->bDynamicTxHighPower = false;
		priv->bDynamicTxLowPower = false;
	}

	if( (priv->bDynamicTxHighPower != priv->bLastDTPFlag_High ) ||
		(priv->bDynamicTxLowPower != priv->bLastDTPFlag_Low ) )
	{
		RT_TRACE(COMP_TXAGC,"SetTxPowerLevel8190()  channel = %d \n" , priv->ieee80211->current_network.channel);

#if  defined(RTL8190P) || defined(RTL8192E)
		SetTxPowerLevel8190(Adapter,pHalData->CurrentChannel);
#endif

#ifdef RTL8192U
		rtl8192_phy_setTxPower(dev,priv->ieee80211->current_network.channel);
		
#endif
	}
	priv->bLastDTPFlag_High = priv->bDynamicTxHighPower;
	priv->bLastDTPFlag_Low = priv->bDynamicTxLowPower;

}	

static void dm_check_txrateandretrycount(struct net_device * dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device* ieee = priv->ieee80211;
	
	ieee->softmac_stats.CurrentShowTxate = read_nic_byte(dev, Current_Tx_Rate_Reg);
	
	
	ieee->softmac_stats.last_packet_rate = read_nic_byte(dev ,Initial_Tx_Rate_Reg);
	
	ieee->softmac_stats.txretrycount = read_nic_dword(dev, Tx_Retry_Count_Reg);
}

static void dm_send_rssi_tofw(struct net_device *dev)
{
	DCMD_TXCMD_T			tx_cmd;
	struct r8192_priv *priv = ieee80211_priv(dev);

	
	
	
	write_nic_byte(dev, DRIVER_RSSI, (u8)priv->undecorated_smoothed_pwdb);
	return;
	tx_cmd.Op		= TXCMD_SET_RX_RSSI;
	tx_cmd.Length	= 4;
	tx_cmd.Value		= priv->undecorated_smoothed_pwdb;

	cmpk_message_handle_tx(dev, (u8*)&tx_cmd,
								DESC_PACKET_TYPE_INIT, sizeof(DCMD_TXCMD_T));
}


