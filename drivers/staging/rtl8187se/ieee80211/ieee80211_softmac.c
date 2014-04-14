/* IEEE 802.11 SoftMAC layer
 * Copyright (c) 2005 Andrea Merello <andreamrl@tiscali.it>
 *
 * Mostly extracted from the rtl8180-sa2400 driver for the
 * in-kernel generic ieee802.11 stack.
 *
 * Few lines might be stolen from other part of the ieee80211
 * stack. Copyright who own it's copyright
 *
 * WPA code stolen from the ipw2200 driver.
 * Copyright who own it's copyright.
 *
 * released under the GPL
 */


#include "ieee80211.h"

#include <linux/random.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>

#include "dot11d.h"
u8 rsn_authen_cipher_suite[16][4] = {
	{0x00,0x0F,0xAC,0x00}, 
	{0x00,0x0F,0xAC,0x01}, 
	{0x00,0x0F,0xAC,0x02}, 
	{0x00,0x0F,0xAC,0x03}, 
	{0x00,0x0F,0xAC,0x04}, 
	{0x00,0x0F,0xAC,0x05}, 
};

short ieee80211_is_54g(const struct ieee80211_network *net)
{
	return (net->rates_ex_len > 0) || (net->rates_len > 4);
}

short ieee80211_is_shortslot(const struct ieee80211_network *net)
{
	return net->capability & WLAN_CAPABILITY_SHORT_SLOT;
}

unsigned int ieee80211_MFIE_rate_len(struct ieee80211_device *ieee)
{
	unsigned int rate_len = 0;

	if (ieee->modulation & IEEE80211_CCK_MODULATION)
		rate_len = IEEE80211_CCK_RATE_LEN + 2;

	if (ieee->modulation & IEEE80211_OFDM_MODULATION)

		rate_len += IEEE80211_OFDM_RATE_LEN + 2;

	return rate_len;
}

void ieee80211_MFIE_Brate(struct ieee80211_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	if (ieee->modulation & IEEE80211_CCK_MODULATION){
		*tag++ = MFIE_TYPE_RATES;
		*tag++ = 4;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_1MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_2MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_5MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_11MB;
	}

	
	*tag_p = tag;
}

void ieee80211_MFIE_Grate(struct ieee80211_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

		if (ieee->modulation & IEEE80211_OFDM_MODULATION){

		*tag++ = MFIE_TYPE_RATES_EX;
		*tag++ = 8;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_6MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_9MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_12MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_18MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_24MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_36MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_48MB;
		*tag++ = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_54MB;

	}

	
	*tag_p = tag;
}


void ieee80211_WMM_Info(struct ieee80211_device *ieee, u8 **tag_p) {
	u8 *tag = *tag_p;

	*tag++ = MFIE_TYPE_GENERIC; 
	*tag++ = 7;
	*tag++ = 0x00;
	*tag++ = 0x50;
	*tag++ = 0xf2;
	*tag++ = 0x02;
	*tag++ = 0x00;
	*tag++ = 0x01;
#ifdef SUPPORT_USPD
	if(ieee->current_network.wmm_info & 0x80) {
		*tag++ = 0x0f|MAX_SP_Len;
	} else {
		*tag++ = MAX_SP_Len;
	}
#else
	*tag++ = MAX_SP_Len;
#endif
	*tag_p = tag;
}

void ieee80211_TURBO_Info(struct ieee80211_device *ieee, u8 **tag_p) {
	u8 *tag = *tag_p;

        *tag++ = MFIE_TYPE_GENERIC; 
        *tag++ = 7;
        *tag++ = 0x00;
        *tag++ = 0xe0;
        *tag++ = 0x4c;
        *tag++ = 0x01;
        *tag++ = 0x02;
        *tag++ = 0x11;
	*tag++ = 0x00;

	*tag_p = tag;
	printk(KERN_ALERT "This is enable turbo mode IE process\n");
}

void enqueue_mgmt(struct ieee80211_device *ieee, struct sk_buff *skb)
{
	int nh;
	nh = (ieee->mgmt_queue_head +1) % MGMT_QUEUE_NUM;

	ieee->mgmt_queue_head = nh;
	ieee->mgmt_queue_ring[nh] = skb;

	
}

struct sk_buff *dequeue_mgmt(struct ieee80211_device *ieee)
{
	struct sk_buff *ret;

	if(ieee->mgmt_queue_tail == ieee->mgmt_queue_head)
		return NULL;

	ret = ieee->mgmt_queue_ring[ieee->mgmt_queue_tail];

	ieee->mgmt_queue_tail =
		(ieee->mgmt_queue_tail+1) % MGMT_QUEUE_NUM;

	return ret;
}

void init_mgmt_queue(struct ieee80211_device *ieee)
{
	ieee->mgmt_queue_tail = ieee->mgmt_queue_head = 0;
}


void ieee80211_sta_wakeup(struct ieee80211_device *ieee, short nl);

inline void softmac_mgmt_xmit(struct sk_buff *skb, struct ieee80211_device *ieee)
{
	unsigned long flags;
	short single = ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE;
	struct ieee80211_hdr_3addr  *header=
		(struct ieee80211_hdr_3addr  *) skb->data;


	spin_lock_irqsave(&ieee->lock, flags);

	
	ieee80211_sta_wakeup(ieee,0);

	if(single){
		if(ieee->queue_stop){

			enqueue_mgmt(ieee,skb);
		}else{
			header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0]<<4);

			if (ieee->seq_ctrl[0] == 0xFFF)
				ieee->seq_ctrl[0] = 0;
			else
				ieee->seq_ctrl[0]++;

			
			ieee->dev->trans_start = jiffies;
			ieee->softmac_data_hard_start_xmit(skb,ieee->dev,ieee->basic_rate);
		}

		spin_unlock_irqrestore(&ieee->lock, flags);
	}else{
		spin_unlock_irqrestore(&ieee->lock, flags);
		spin_lock_irqsave(&ieee->mgmt_tx_lock, flags);

		header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

		if (ieee->seq_ctrl[0] == 0xFFF)
			ieee->seq_ctrl[0] = 0;
		else
			ieee->seq_ctrl[0]++;

		
		ieee->dev->trans_start = jiffies;
		ieee->softmac_hard_start_xmit(skb,ieee->dev);

		spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags);
	}
}


inline void softmac_ps_mgmt_xmit(struct sk_buff *skb, struct ieee80211_device *ieee)
{

	short single = ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE;
	struct ieee80211_hdr_3addr  *header =
		(struct ieee80211_hdr_3addr  *) skb->data;


	if(single){

		header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

		if (ieee->seq_ctrl[0] == 0xFFF)
			ieee->seq_ctrl[0] = 0;
		else
			ieee->seq_ctrl[0]++;

		
		ieee->dev->trans_start = jiffies;
		ieee->softmac_data_hard_start_xmit(skb,ieee->dev,ieee->basic_rate);

	}else{

		header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

		if (ieee->seq_ctrl[0] == 0xFFF)
			ieee->seq_ctrl[0] = 0;
		else
			ieee->seq_ctrl[0]++;

		
		ieee->dev->trans_start = jiffies;
		ieee->softmac_hard_start_xmit(skb,ieee->dev);

	}
}
inline struct sk_buff *ieee80211_disassociate_skb(
							struct ieee80211_network *beacon,
							struct ieee80211_device *ieee,
							u8	asRsn)
{
	struct sk_buff *skb;
	struct ieee80211_disassoc_frame *disass;

	skb = dev_alloc_skb(sizeof(struct ieee80211_disassoc_frame));
	if (!skb)
		return NULL;

	disass = (struct ieee80211_disassoc_frame *) skb_put(skb,sizeof(struct ieee80211_disassoc_frame));
	disass->header.frame_control = cpu_to_le16(IEEE80211_STYPE_DISASSOC);
	disass->header.duration_id = 0;

	memcpy(disass->header.addr1, beacon->bssid, ETH_ALEN);
	memcpy(disass->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(disass->header.addr3, beacon->bssid, ETH_ALEN);

	disass->reasoncode = asRsn;
	return skb;
}
void
SendDisassociation(
        struct ieee80211_device *ieee,
        u8*                     asSta,
        u8                      asRsn
)
{
        struct ieee80211_network *beacon = &ieee->current_network;
        struct sk_buff *skb;
        skb = ieee80211_disassociate_skb(beacon,ieee,asRsn);
        if (skb){
                softmac_mgmt_xmit(skb, ieee);
                
        }
}

inline struct sk_buff *ieee80211_probe_req(struct ieee80211_device *ieee)
{
	unsigned int len,rate_len;
	u8 *tag;
	struct sk_buff *skb;
	struct ieee80211_probe_request *req;

	len = ieee->current_network.ssid_len;

	rate_len = ieee80211_MFIE_rate_len(ieee);

	skb = dev_alloc_skb(sizeof(struct ieee80211_probe_request) +
			    2 + len + rate_len);
	if (!skb)
		return NULL;

	req = (struct ieee80211_probe_request *) skb_put(skb,sizeof(struct ieee80211_probe_request));
	req->header.frame_ctl = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	req->header.duration_id = 0; 

	memset(req->header.addr1, 0xff, ETH_ALEN);
	memcpy(req->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	memset(req->header.addr3, 0xff, ETH_ALEN);

	tag = (u8 *) skb_put(skb,len+2+rate_len);

	*tag++ = MFIE_TYPE_SSID;
	*tag++ = len;
	memcpy(tag, ieee->current_network.ssid, len);
	tag += len;
	ieee80211_MFIE_Brate(ieee,&tag);
	ieee80211_MFIE_Grate(ieee,&tag);

	return skb;
}

struct sk_buff *ieee80211_get_beacon_(struct ieee80211_device *ieee);

void ext_ieee80211_send_beacon_wq(struct ieee80211_device *ieee)
{
	struct sk_buff *skb;

	

	skb = ieee80211_get_beacon_(ieee);

	if (skb){
		softmac_mgmt_xmit(skb, ieee);
		ieee->softmac_stats.tx_beacons++;
		dev_kfree_skb_any(skb);
	}


	
	ieee->beacon_timer.expires = jiffies +
		(MSECS( ieee->current_network.beacon_interval -5));

	
	if(ieee->beacon_txing)
		add_timer(&ieee->beacon_timer);
	
}

void ieee80211_send_beacon(struct ieee80211_device *ieee)
{
	struct sk_buff *skb;

	

	skb = ieee80211_get_beacon_(ieee);

	if (skb){
		softmac_mgmt_xmit(skb, ieee);
		ieee->softmac_stats.tx_beacons++;
		dev_kfree_skb_any(skb);
	}

	
	ieee->beacon_timer.expires = jiffies +
		(MSECS( ieee->current_network.beacon_interval -5));

	
	if(ieee->beacon_txing)
		add_timer(&ieee->beacon_timer);
	
}


void ieee80211_send_beacon_cb(unsigned long _ieee)
{
	struct ieee80211_device *ieee =
		(struct ieee80211_device *) _ieee;
	unsigned long flags;

	spin_lock_irqsave(&ieee->beacon_lock, flags);
	ieee80211_send_beacon(ieee);
	spin_unlock_irqrestore(&ieee->beacon_lock, flags);
}

void ieee80211_send_probe(struct ieee80211_device *ieee)
{
	struct sk_buff *skb;

	skb = ieee80211_probe_req(ieee);
	if (skb){
		softmac_mgmt_xmit(skb, ieee);
		ieee->softmac_stats.tx_probe_rq++;
		
	}
}

void ieee80211_send_probe_requests(struct ieee80211_device *ieee)
{
	if (ieee->active_scan && (ieee->softmac_features & IEEE_SOFTMAC_PROBERQ)){
		ieee80211_send_probe(ieee);
		ieee80211_send_probe(ieee);
	}
}

void ieee80211_softmac_scan_syncro(struct ieee80211_device *ieee)
{
	short ch = 0;
	u8 channel_map[MAX_CHANNEL_NUMBER+1];
	memcpy(channel_map, GET_DOT11D_INFO(ieee)->channel_map, MAX_CHANNEL_NUMBER+1);
	down(&ieee->scan_sem);

	while(1)
	{

		do{
			ch++;
			if (ch > MAX_CHANNEL_NUMBER)
				goto out; 

		}while(!channel_map[ch]);

		if (ieee->state == IEEE80211_LINKED)
			goto out;

		ieee->set_chan(ieee->dev, ch);
		if(channel_map[ch] == 1)
		{
			ieee80211_send_probe_requests(ieee);
		}
		if (ieee->sync_scan_hurryup)
			goto out;


		msleep_interruptible_rtl(IEEE80211_SOFTMAC_SCAN_TIME);

	}
out:
	ieee->sync_scan_hurryup = 0;
	up(&ieee->scan_sem);
	if(IS_DOT11D_ENABLE(ieee))
		DOT11D_ScanComplete(ieee);
}

void ieee80211_softmac_ips_scan_syncro(struct ieee80211_device *ieee)
{
	int ch;
        unsigned int watch_dog = 0;
	u8 channel_map[MAX_CHANNEL_NUMBER+1];
	memcpy(channel_map, GET_DOT11D_INFO(ieee)->channel_map, MAX_CHANNEL_NUMBER+1);
        down(&ieee->scan_sem);
	ch = ieee->current_network.channel;

     	while(1)
        {
		if (ieee->state == IEEE80211_LINKED)
		{
			goto out;
		}
		if(channel_map[ieee->current_network.channel] > 0)
		{
			ieee->set_chan(ieee->dev, ieee->current_network.channel);
		}
		if(channel_map[ieee->current_network.channel] == 1)
		{
			ieee80211_send_probe_requests(ieee);
                }

		msleep_interruptible_rtl(IEEE80211_SOFTMAC_SCAN_TIME);

		do{
			if (watch_dog++ >= MAX_CHANNEL_NUMBER)
		
				goto out; 

			ieee->current_network.channel = (ieee->current_network.channel + 1)%MAX_CHANNEL_NUMBER;
		}while(!channel_map[ieee->current_network.channel]);
        }
out:
	
   	
   	
	ieee->actscanning = false;
	up(&ieee->scan_sem);
	if(IS_DOT11D_ENABLE(ieee))
		DOT11D_ScanComplete(ieee);
}

void ieee80211_softmac_scan_wq(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ieee80211_device *ieee = container_of(dwork, struct ieee80211_device, softmac_scan_wq);
	static short watchdog = 0;
	u8 channel_map[MAX_CHANNEL_NUMBER+1];
	memcpy(channel_map, GET_DOT11D_INFO(ieee)->channel_map, MAX_CHANNEL_NUMBER+1);
	down(&ieee->scan_sem);

	do{
		ieee->current_network.channel =
			(ieee->current_network.channel + 1) % MAX_CHANNEL_NUMBER;
		if (watchdog++ > MAX_CHANNEL_NUMBER)
				goto out; 

 	}while(!channel_map[ieee->current_network.channel]);

	
	if (ieee->scanning == 0 )
	{
		printk("error out, scanning = 0\n");
		goto out;
	}
	ieee->set_chan(ieee->dev, ieee->current_network.channel);
	if(channel_map[ieee->current_network.channel] == 1)
		ieee80211_send_probe_requests(ieee);

	queue_delayed_work(ieee->wq, &ieee->softmac_scan_wq, IEEE80211_SOFTMAC_SCAN_TIME);
	up(&ieee->scan_sem);
	return;
out:
	ieee->actscanning = false;
	watchdog = 0;
	ieee->scanning = 0;
	up(&ieee->scan_sem);

	if(IS_DOT11D_ENABLE(ieee))
		DOT11D_ScanComplete(ieee);
	return;
}

void ieee80211_beacons_start(struct ieee80211_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->beacon_lock,flags);

	ieee->beacon_txing = 1;
	ieee80211_send_beacon(ieee);

	spin_unlock_irqrestore(&ieee->beacon_lock,flags);
}

void ieee80211_beacons_stop(struct ieee80211_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->beacon_lock,flags);

	ieee->beacon_txing = 0;
 	del_timer_sync(&ieee->beacon_timer);

	spin_unlock_irqrestore(&ieee->beacon_lock,flags);

}


void ieee80211_stop_send_beacons(struct ieee80211_device *ieee)
{
	if(ieee->stop_send_beacons)
		ieee->stop_send_beacons(ieee->dev);
	if (ieee->softmac_features & IEEE_SOFTMAC_BEACONS)
		ieee80211_beacons_stop(ieee);
}


void ieee80211_start_send_beacons(struct ieee80211_device *ieee)
{
	if(ieee->start_send_beacons)
		ieee->start_send_beacons(ieee->dev);
	if(ieee->softmac_features & IEEE_SOFTMAC_BEACONS)
		ieee80211_beacons_start(ieee);
}


void ieee80211_softmac_stop_scan(struct ieee80211_device *ieee)
{

	

	down(&ieee->scan_sem);

	if (ieee->scanning == 1){
		ieee->scanning = 0;
		
		cancel_delayed_work(&ieee->softmac_scan_wq);
	}

	up(&ieee->scan_sem);
}

void ieee80211_stop_scan(struct ieee80211_device *ieee)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN)
		ieee80211_softmac_stop_scan(ieee);
	else
		ieee->stop_scan(ieee->dev);
}

void ieee80211_rtl_start_scan(struct ieee80211_device *ieee)
{
	if(IS_DOT11D_ENABLE(ieee) )
	{
		if(IS_COUNTRY_IE_VALID(ieee))
		{
			RESET_CIE_WATCHDOG(ieee);
		}
	}
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN){
		if (ieee->scanning == 0)
		{
			ieee->scanning = 1;
			
		
		
#if 1
			queue_delayed_work(ieee->wq, &ieee->softmac_scan_wq,0);
#endif
		}
	}else
		ieee->start_scan(ieee->dev);

}

void ieee80211_start_scan_syncro(struct ieee80211_device *ieee)
{
	if(IS_DOT11D_ENABLE(ieee) )
	{
		if(IS_COUNTRY_IE_VALID(ieee))
		{
			RESET_CIE_WATCHDOG(ieee);
		}
	}
	ieee->sync_scan_hurryup = 0;

	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN)
		ieee80211_softmac_scan_syncro(ieee);
	else
		ieee->scan_syncro(ieee->dev);

}

inline struct sk_buff *ieee80211_authentication_req(struct ieee80211_network *beacon,
	struct ieee80211_device *ieee, int challengelen)
{
	struct sk_buff *skb;
	struct ieee80211_authentication *auth;

	skb = dev_alloc_skb(sizeof(struct ieee80211_authentication) + challengelen);

	if (!skb) return NULL;

	auth = (struct ieee80211_authentication *)
		skb_put(skb, sizeof(struct ieee80211_authentication));

	auth->header.frame_ctl = IEEE80211_STYPE_AUTH;
	if (challengelen) auth->header.frame_ctl |= IEEE80211_FCTL_WEP;

	auth->header.duration_id = 0x013a; 

	memcpy(auth->header.addr1, beacon->bssid, ETH_ALEN);
	memcpy(auth->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(auth->header.addr3, beacon->bssid, ETH_ALEN);

	auth->algorithm = ieee->open_wep ? WLAN_AUTH_OPEN : WLAN_AUTH_SHARED_KEY;

	auth->transaction = cpu_to_le16(ieee->associate_seq);
	ieee->associate_seq++;

	auth->status = cpu_to_le16(WLAN_STATUS_SUCCESS);

	return skb;

}

static struct sk_buff* ieee80211_probe_resp(struct ieee80211_device *ieee, u8 *dest)
{
	u8 *tag;
	int beacon_size;
	struct ieee80211_probe_response *beacon_buf;
	struct sk_buff *skb;
	int encrypt;
	int atim_len,erp_len;
	struct ieee80211_crypt_data* crypt;

	char *ssid = ieee->current_network.ssid;
	int ssid_len = ieee->current_network.ssid_len;
	int rate_len = ieee->current_network.rates_len+2;
	int rate_ex_len = ieee->current_network.rates_ex_len;
	int wpa_ie_len = ieee->wpa_ie_len;
	if(rate_ex_len > 0) rate_ex_len+=2;

	if(ieee->current_network.capability & WLAN_CAPABILITY_IBSS)
		atim_len = 4;
	else
		atim_len = 0;

	if(ieee80211_is_54g(&ieee->current_network))
		erp_len = 3;
	else
		erp_len = 0;

	beacon_size = sizeof(struct ieee80211_probe_response)+
		ssid_len
		+3 
		+rate_len
		+rate_ex_len
		+atim_len
		+wpa_ie_len
		+erp_len;

	skb = dev_alloc_skb(beacon_size);

	if (!skb)
		return NULL;

	beacon_buf = (struct ieee80211_probe_response*) skb_put(skb, beacon_size);

	memcpy (beacon_buf->header.addr1, dest,ETH_ALEN);
	memcpy (beacon_buf->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy (beacon_buf->header.addr3, ieee->current_network.bssid, ETH_ALEN);

	beacon_buf->header.duration_id = 0; 
	beacon_buf->beacon_interval =
		cpu_to_le16(ieee->current_network.beacon_interval);
	beacon_buf->capability =
		cpu_to_le16(ieee->current_network.capability & WLAN_CAPABILITY_IBSS);

	if(ieee->short_slot && (ieee->current_network.capability & WLAN_CAPABILITY_SHORT_SLOT))
		beacon_buf->capability |= cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT);

	crypt = ieee->crypt[ieee->tx_keyidx];

	encrypt = ieee->host_encrypt && crypt && crypt->ops &&
		((0 == strcmp(crypt->ops->name, "WEP")) || wpa_ie_len);

	if (encrypt)
		beacon_buf->capability |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);


	beacon_buf->header.frame_ctl = cpu_to_le16(IEEE80211_STYPE_PROBE_RESP);

	beacon_buf->info_element.id = MFIE_TYPE_SSID;
	beacon_buf->info_element.len = ssid_len;

	tag = (u8*) beacon_buf->info_element.data;

	memcpy(tag, ssid, ssid_len);

	tag += ssid_len;

	*(tag++) = MFIE_TYPE_RATES;
	*(tag++) = rate_len-2;
	memcpy(tag,ieee->current_network.rates,rate_len-2);
	tag+=rate_len-2;

	*(tag++) = MFIE_TYPE_DS_SET;
	*(tag++) = 1;
	*(tag++) = ieee->current_network.channel;

	if(atim_len){
		*(tag++) = MFIE_TYPE_IBSS_SET;
		*(tag++) = 2;
		*((u16*)(tag)) = cpu_to_le16(ieee->current_network.atim_window);
		tag+=2;
	}

	if(erp_len){
		*(tag++) = MFIE_TYPE_ERP;
		*(tag++) = 1;
		*(tag++) = 0;
	}

	if(rate_ex_len){
		*(tag++) = MFIE_TYPE_RATES_EX;
		*(tag++) = rate_ex_len-2;
		memcpy(tag,ieee->current_network.rates_ex,rate_ex_len-2);
		tag+=rate_ex_len-2;
	}

	if (wpa_ie_len)
	{
		if (ieee->iw_mode == IW_MODE_ADHOC)
		{
			memcpy(&ieee->wpa_ie[14], &ieee->wpa_ie[8], 4);
		}

		memcpy(tag, ieee->wpa_ie, ieee->wpa_ie_len);
	}

	skb->dev = ieee->dev;
	return skb;
}

struct sk_buff* ieee80211_assoc_resp(struct ieee80211_device *ieee, u8 *dest)
{
	struct sk_buff *skb;
	u8* tag;

	struct ieee80211_crypt_data* crypt;
	struct ieee80211_assoc_response_frame *assoc;
	short encrypt;

	unsigned int rate_len = ieee80211_MFIE_rate_len(ieee);
	int len = sizeof(struct ieee80211_assoc_response_frame) + rate_len;

	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	assoc = (struct ieee80211_assoc_response_frame *)
		skb_put(skb,sizeof(struct ieee80211_assoc_response_frame));

	assoc->header.frame_control = cpu_to_le16(IEEE80211_STYPE_ASSOC_RESP);
	memcpy(assoc->header.addr1, dest,ETH_ALEN);
	memcpy(assoc->header.addr3, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(assoc->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	assoc->capability = cpu_to_le16(ieee->iw_mode == IW_MODE_MASTER ?
		WLAN_CAPABILITY_BSS : WLAN_CAPABILITY_IBSS);


	if(ieee->short_slot)
		assoc->capability |= cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT);

	if (ieee->host_encrypt)
		crypt = ieee->crypt[ieee->tx_keyidx];
	else crypt = NULL;

	encrypt = ( crypt && crypt->ops);

	if (encrypt)
		assoc->capability |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);

	assoc->status = 0;
	assoc->aid = cpu_to_le16(ieee->assoc_id);
	if (ieee->assoc_id == 0x2007) ieee->assoc_id=0;
	else ieee->assoc_id++;

	tag = (u8*) skb_put(skb, rate_len);

	ieee80211_MFIE_Brate(ieee, &tag);
	ieee80211_MFIE_Grate(ieee, &tag);

	return skb;
}

struct sk_buff* ieee80211_auth_resp(struct ieee80211_device *ieee,int status, u8 *dest)
{
	struct sk_buff *skb;
	struct ieee80211_authentication *auth;

	skb = dev_alloc_skb(sizeof(struct ieee80211_authentication)+1);

	if (!skb)
		return NULL;

	skb->len = sizeof(struct ieee80211_authentication);

	auth = (struct ieee80211_authentication *)skb->data;

	auth->status = cpu_to_le16(status);
	auth->transaction = cpu_to_le16(2);
	auth->algorithm = cpu_to_le16(WLAN_AUTH_OPEN);

	memcpy(auth->header.addr3, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(auth->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(auth->header.addr1, dest, ETH_ALEN);
	auth->header.frame_ctl = cpu_to_le16(IEEE80211_STYPE_AUTH);
	return skb;


}

struct sk_buff* ieee80211_null_func(struct ieee80211_device *ieee,short pwr)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr* hdr;

	skb = dev_alloc_skb(sizeof(struct ieee80211_hdr_3addr));

	if (!skb)
		return NULL;

	hdr = (struct ieee80211_hdr_3addr*)skb_put(skb,sizeof(struct ieee80211_hdr_3addr));

	memcpy(hdr->addr1, ieee->current_network.bssid, ETH_ALEN);
	memcpy(hdr->addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(hdr->addr3, ieee->current_network.bssid, ETH_ALEN);

	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
		IEEE80211_STYPE_NULLFUNC | IEEE80211_FCTL_TODS |
		(pwr ? IEEE80211_FCTL_PM:0));

	return skb;


}


void ieee80211_resp_to_assoc_rq(struct ieee80211_device *ieee, u8* dest)
{
	struct sk_buff *buf = ieee80211_assoc_resp(ieee, dest);

	if (buf){
		softmac_mgmt_xmit(buf, ieee);
		dev_kfree_skb_any(buf);
	}
}


void ieee80211_resp_to_auth(struct ieee80211_device *ieee, int s, u8* dest)
{
	struct sk_buff *buf = ieee80211_auth_resp(ieee, s, dest);

	if (buf){
		softmac_mgmt_xmit(buf, ieee);
		dev_kfree_skb_any(buf);
	}
}


void ieee80211_resp_to_probe(struct ieee80211_device *ieee, u8 *dest)
{

	struct sk_buff *buf = ieee80211_probe_resp(ieee, dest);

	if (buf) {
		softmac_mgmt_xmit(buf, ieee);
		dev_kfree_skb_any(buf);
	}
}


inline struct sk_buff *ieee80211_association_req(struct ieee80211_network *beacon,struct ieee80211_device *ieee)
{
	struct sk_buff *skb;
	

	struct ieee80211_assoc_request_frame *hdr;
	u8 *tag;
	
	
	
	
	unsigned int wpa_len = beacon->wpa_ie_len;
	
	
	
	
#if 1
	
	unsigned int rsn_len = beacon->rsn_ie_len;
#else
	unsigned int rsn_len = beacon->rsn_ie_len - 4;
#endif
	unsigned int rate_len = ieee80211_MFIE_rate_len(ieee);
	unsigned int wmm_info_len = beacon->QoS_Enable?9:0;
	unsigned int turbo_info_len = beacon->Turbo_Enable?9:0;

	u8  encry_proto = ieee->wpax_type_notify & 0xff;
	
	

	int len = 0;

	
	
	
	if(ieee->wpax_type_set) {
		if (IEEE_PROTO_WPA == encry_proto) {
			rsn_len = 0;
		} else if (IEEE_PROTO_RSN == encry_proto) {
			wpa_len = 0;
		}
	}
	len = sizeof(struct ieee80211_assoc_request_frame)+
		+ beacon->ssid_len
		+ rate_len
		+ wpa_len
		+ rsn_len
		+ wmm_info_len
		+ turbo_info_len;

	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	hdr = (struct ieee80211_assoc_request_frame *)
		skb_put(skb, sizeof(struct ieee80211_assoc_request_frame));


	hdr->header.frame_control = IEEE80211_STYPE_ASSOC_REQ;
	hdr->header.duration_id= 37; 
	memcpy(hdr->header.addr1, beacon->bssid, ETH_ALEN);
	memcpy(hdr->header.addr2, ieee->dev->dev_addr, ETH_ALEN);
	memcpy(hdr->header.addr3, beacon->bssid, ETH_ALEN);
	memcpy(ieee->ap_mac_addr, beacon->bssid, ETH_ALEN);

	hdr->capability = cpu_to_le16(WLAN_CAPABILITY_BSS);
	if (beacon->capability & WLAN_CAPABILITY_PRIVACY )
		hdr->capability |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);
	if (beacon->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		hdr->capability |= cpu_to_le16(WLAN_CAPABILITY_SHORT_PREAMBLE);

	if(ieee->short_slot)
		hdr->capability |= cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT);

	hdr->listen_interval = 0xa; 

	hdr->info_element.id = MFIE_TYPE_SSID;

	hdr->info_element.len = beacon->ssid_len;
	tag = skb_put(skb, beacon->ssid_len);
	memcpy(tag, beacon->ssid, beacon->ssid_len);

	tag = skb_put(skb, rate_len);

	ieee80211_MFIE_Brate(ieee, &tag);
	ieee80211_MFIE_Grate(ieee, &tag);

	
	

	tag = skb_put(skb,ieee->wpa_ie_len);
	memcpy(tag,ieee->wpa_ie,ieee->wpa_ie_len);

	tag = skb_put(skb,wmm_info_len);
	if(wmm_info_len) {
	  ieee80211_WMM_Info(ieee, &tag);
	}
	tag = skb_put(skb,turbo_info_len);
        if(turbo_info_len) {
                ieee80211_TURBO_Info(ieee, &tag);
        }

	return skb;
}

void ieee80211_associate_abort(struct ieee80211_device *ieee)
{

	unsigned long flags;
	spin_lock_irqsave(&ieee->lock, flags);

	ieee->associate_seq++;

	if (ieee->state == IEEE80211_ASSOCIATING_AUTHENTICATING){
		IEEE80211_DEBUG_MGMT("Authentication failed\n");
		ieee->softmac_stats.no_auth_rs++;
	}else{
		IEEE80211_DEBUG_MGMT("Association failed\n");
		ieee->softmac_stats.no_ass_rs++;
	}

	ieee->state = IEEE80211_ASSOCIATING_RETRY;

	queue_delayed_work(ieee->wq, &ieee->associate_retry_wq,IEEE80211_SOFTMAC_ASSOC_RETRY_TIME);

	spin_unlock_irqrestore(&ieee->lock, flags);
}

void ieee80211_associate_abort_cb(unsigned long dev)
{
	ieee80211_associate_abort((struct ieee80211_device *) dev);
}


void ieee80211_associate_step1(struct ieee80211_device *ieee)
{
	struct ieee80211_network *beacon = &ieee->current_network;
	struct sk_buff *skb;

	IEEE80211_DEBUG_MGMT("Stopping scan\n");
	ieee->softmac_stats.tx_auth_rq++;
	skb=ieee80211_authentication_req(beacon, ieee, 0);
	if (!skb){

		ieee80211_associate_abort(ieee);
	}
	else{
		ieee->state = IEEE80211_ASSOCIATING_AUTHENTICATING ;
		IEEE80211_DEBUG_MGMT("Sending authentication request\n");
		
		softmac_mgmt_xmit(skb, ieee);
		
		if(!timer_pending(&ieee->associate_timer)){
			ieee->associate_timer.expires = jiffies + (HZ / 2);
			add_timer(&ieee->associate_timer);
		}
		
		
		
		
		
	}
}

void ieee80211_rtl_auth_challenge(struct ieee80211_device *ieee, u8 *challenge, int chlen)
{
	u8 *c;
	struct sk_buff *skb;
	struct ieee80211_network *beacon = &ieee->current_network;
	del_timer_sync(&ieee->associate_timer);
	ieee->associate_seq++;
	ieee->softmac_stats.tx_auth_rq++;

	skb = ieee80211_authentication_req(beacon, ieee, chlen+2);
	if (!skb)
		ieee80211_associate_abort(ieee);
	else{
		c = skb_put(skb, chlen+2);
		*(c++) = MFIE_TYPE_CHALLENGE;
		*(c++) = chlen;
		memcpy(c, challenge, chlen);

		IEEE80211_DEBUG_MGMT("Sending authentication challenge response\n");

		ieee80211_encrypt_fragment(ieee, skb, sizeof(struct ieee80211_hdr_3addr  ));

		softmac_mgmt_xmit(skb, ieee);
		if (!timer_pending(&ieee->associate_timer)){
		
		ieee->associate_timer.expires = jiffies + (HZ / 2);
		add_timer(&ieee->associate_timer);
		}
		dev_kfree_skb_any(skb);
	}
	kfree(challenge);
}

void ieee80211_associate_step2(struct ieee80211_device *ieee)
{
	struct sk_buff* skb;
	struct ieee80211_network *beacon = &ieee->current_network;

	del_timer_sync(&ieee->associate_timer);

	IEEE80211_DEBUG_MGMT("Sending association request\n");
	ieee->softmac_stats.tx_ass_rq++;
	skb=ieee80211_association_req(beacon, ieee);
	if (!skb)
		ieee80211_associate_abort(ieee);
	else{
		softmac_mgmt_xmit(skb, ieee);
		if (!timer_pending(&ieee->associate_timer)){
		ieee->associate_timer.expires = jiffies + (HZ / 2);
		add_timer(&ieee->associate_timer);
		}
		
	}
}

void ieee80211_associate_complete_wq(struct work_struct *work)
{
	struct ieee80211_device *ieee = container_of(work, struct ieee80211_device, associate_complete_wq);

	printk(KERN_INFO "Associated successfully\n");
	if(ieee80211_is_54g(&ieee->current_network) &&
		(ieee->modulation & IEEE80211_OFDM_MODULATION)){

		ieee->rate = 540;
		printk(KERN_INFO"Using G rates\n");
	}else{
		ieee->rate = 110;
		printk(KERN_INFO"Using B rates\n");
	}
	ieee->link_change(ieee->dev);
	notify_wx_assoc_event(ieee);
	if (ieee->data_hard_resume)
		ieee->data_hard_resume(ieee->dev);
	netif_carrier_on(ieee->dev);
}

void ieee80211_associate_complete(struct ieee80211_device *ieee)
{
	int i;
	del_timer_sync(&ieee->associate_timer);

	for(i = 0; i < 6; i++) {
	  
	}
	ieee->state = IEEE80211_LINKED;
	IEEE80211_DEBUG_MGMT("Successfully associated\n");

	queue_work(ieee->wq, &ieee->associate_complete_wq);
}

void ieee80211_associate_procedure_wq(struct work_struct *work)
{
	struct ieee80211_device *ieee = container_of(work, struct ieee80211_device, associate_procedure_wq);

	ieee->sync_scan_hurryup = 1;
	down(&ieee->wx_sem);

	if (ieee->data_hard_stop)
		ieee->data_hard_stop(ieee->dev);

	ieee80211_stop_scan(ieee);
	ieee->set_chan(ieee->dev, ieee->current_network.channel);

	ieee->associate_seq = 1;
	ieee80211_associate_step1(ieee);

	up(&ieee->wx_sem);
}

inline void ieee80211_softmac_new_net(struct ieee80211_device *ieee, struct ieee80211_network *net)
{
	u8 tmp_ssid[IW_ESSID_MAX_SIZE+1];
	int tmp_ssid_len = 0;

	short apset,ssidset,ssidbroad,apmatch,ssidmatch;

	if (ieee->state != IEEE80211_NOLINK)
		return;

	if ((ieee->iw_mode == IW_MODE_INFRA) && !(net->capability & WLAN_CAPABILITY_BSS))
		return;

	if ((ieee->iw_mode == IW_MODE_ADHOC) && !(net->capability & WLAN_CAPABILITY_IBSS))
		return;


	if (ieee->iw_mode == IW_MODE_INFRA || ieee->iw_mode == IW_MODE_ADHOC){
		apset = ieee->wap_set;
		ssidset = ieee->ssid_set;
		ssidbroad =  !(net->ssid_len == 0 || net->ssid[0]== '\0');
		apmatch = (memcmp(ieee->current_network.bssid, net->bssid, ETH_ALEN)==0);

		if(ieee->current_network.ssid_len != net->ssid_len)
			ssidmatch = 0;
		else
			ssidmatch = (0==strncmp(ieee->current_network.ssid, net->ssid, net->ssid_len));

		
		

		if (	
				( apset && apmatch &&
				  ((ssidset && ssidbroad && ssidmatch) || (ssidbroad && !ssidset) || (!ssidbroad && ssidset)) ) ||
				(!apset && ssidset && ssidbroad && ssidmatch)
		   ){


			if (!ssidbroad){
				strncpy(tmp_ssid, ieee->current_network.ssid, IW_ESSID_MAX_SIZE);
				tmp_ssid_len = ieee->current_network.ssid_len;
			}
			memcpy(&ieee->current_network, net, sizeof(struct ieee80211_network));

			if (!ssidbroad){
				strncpy(ieee->current_network.ssid, tmp_ssid, IW_ESSID_MAX_SIZE);
				ieee->current_network.ssid_len = tmp_ssid_len;
			}
			printk(KERN_INFO"Linking with %s: channel is %d\n",ieee->current_network.ssid,ieee->current_network.channel);

			if (ieee->iw_mode == IW_MODE_INFRA){
				ieee->state = IEEE80211_ASSOCIATING;
				ieee->beinretry = false;
				queue_work(ieee->wq, &ieee->associate_procedure_wq);
			}else{
				if(ieee80211_is_54g(&ieee->current_network) &&
						(ieee->modulation & IEEE80211_OFDM_MODULATION)){
					ieee->rate = 540;
					printk(KERN_INFO"Using G rates\n");
				}else{
					ieee->rate = 110;
					printk(KERN_INFO"Using B rates\n");
				}
				ieee->state = IEEE80211_LINKED;
				ieee->beinretry = false;
			}

		}
	}

}

void ieee80211_softmac_check_all_nets(struct ieee80211_device *ieee)
{
	unsigned long flags;
	struct ieee80211_network *target;

	spin_lock_irqsave(&ieee->lock, flags);
	list_for_each_entry(target, &ieee->network_list, list) {


		if (ieee->state != IEEE80211_NOLINK)
			break;

		if (ieee->scan_age == 0 || time_after(target->last_scanned + ieee->scan_age, jiffies))
			ieee80211_softmac_new_net(ieee, target);
	}

	spin_unlock_irqrestore(&ieee->lock, flags);

}


static inline u16 auth_parse(struct sk_buff *skb, u8** challenge, int *chlen)
{
	struct ieee80211_authentication *a;
	u8 *t;
	if (skb->len <  (sizeof(struct ieee80211_authentication)-sizeof(struct ieee80211_info_element))){
		IEEE80211_DEBUG_MGMT("invalid len in auth resp: %d\n",skb->len);
		return 0xcafe;
	}
	*challenge = NULL;
	a = (struct ieee80211_authentication*) skb->data;
	if(skb->len > (sizeof(struct ieee80211_authentication) +3)){
		t = skb->data + sizeof(struct ieee80211_authentication);

		if(*(t++) == MFIE_TYPE_CHALLENGE){
			*chlen = *(t++);
			*challenge = kmemdup(t, *chlen, GFP_ATOMIC);
			if (!*challenge)
				return -ENOMEM;
		}
	}

	return cpu_to_le16(a->status);

}


int auth_rq_parse(struct sk_buff *skb,u8* dest)
{
	struct ieee80211_authentication *a;

	if (skb->len <  (sizeof(struct ieee80211_authentication)-sizeof(struct ieee80211_info_element))){
		IEEE80211_DEBUG_MGMT("invalid len in auth request: %d\n",skb->len);
		return -1;
	}
	a = (struct ieee80211_authentication*) skb->data;

	memcpy(dest,a->header.addr2, ETH_ALEN);

	if (le16_to_cpu(a->algorithm) != WLAN_AUTH_OPEN)
		return  WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;

	return WLAN_STATUS_SUCCESS;
}

static short probe_rq_parse(struct ieee80211_device *ieee, struct sk_buff *skb, u8 *src)
{
	u8 *tag;
	u8 *skbend;
	u8 *ssid=NULL;
	u8 ssidlen = 0;

	struct ieee80211_hdr_3addr   *header =
		(struct ieee80211_hdr_3addr   *) skb->data;

	if (skb->len < sizeof (struct ieee80211_hdr_3addr  ))
		return -1; 

	memcpy(src,header->addr2, ETH_ALEN);

	skbend = (u8*)skb->data + skb->len;

	tag = skb->data + sizeof (struct ieee80211_hdr_3addr  );

	while (tag+1 < skbend){
		if (*tag == 0){
			ssid = tag+2;
			ssidlen = *(tag+1);
			break;
		}
		tag++; 
		tag = tag + *(tag); 
		tag++; 
	}

	
	if (ssidlen == 0) return 1;

	if (!ssid) return 1; 
	return (!strncmp(ssid, ieee->current_network.ssid, ssidlen));

}

int assoc_rq_parse(struct sk_buff *skb,u8* dest)
{
	struct ieee80211_assoc_request_frame *a;

	if (skb->len < (sizeof(struct ieee80211_assoc_request_frame) -
		sizeof(struct ieee80211_info_element))) {

		IEEE80211_DEBUG_MGMT("invalid len in auth request:%d \n", skb->len);
		return -1;
	}

	a = (struct ieee80211_assoc_request_frame*) skb->data;

	memcpy(dest,a->header.addr2,ETH_ALEN);

	return 0;
}

static inline u16 assoc_parse(struct sk_buff *skb, int *aid)
{
	struct ieee80211_assoc_response_frame *a;
	if (skb->len <  sizeof(struct ieee80211_assoc_response_frame)){
		IEEE80211_DEBUG_MGMT("invalid len in auth resp: %d\n", skb->len);
		return 0xcafe;
	}

	a = (struct ieee80211_assoc_response_frame*) skb->data;
	*aid = le16_to_cpu(a->aid) & 0x3fff;
	return le16_to_cpu(a->status);
}

static inline void
ieee80211_rx_probe_rq(struct ieee80211_device *ieee, struct sk_buff *skb)
{
	u8 dest[ETH_ALEN];

	
	ieee->softmac_stats.rx_probe_rq++;
	
	if (probe_rq_parse(ieee, skb, dest)){
		
		ieee->softmac_stats.tx_probe_rs++;
		ieee80211_resp_to_probe(ieee, dest);
	}
}

inline void
ieee80211_rx_auth_rq(struct ieee80211_device *ieee, struct sk_buff *skb)
{
	u8 dest[ETH_ALEN];
	int status;
	
	ieee->softmac_stats.rx_auth_rq++;

	status = auth_rq_parse(skb, dest);
	if (status != -1) {
		ieee80211_resp_to_auth(ieee, status, dest);
	}
	

}

 inline void
ieee80211_rx_assoc_rq(struct ieee80211_device *ieee, struct sk_buff *skb)
{

	u8 dest[ETH_ALEN];
	

	ieee->softmac_stats.rx_ass_rq++;
	if (assoc_rq_parse(skb,dest) != -1){
		ieee80211_resp_to_assoc_rq(ieee, dest);
	}

	printk(KERN_INFO"New client associated: %pM\n", dest);
}



void ieee80211_sta_ps_send_null_frame(struct ieee80211_device *ieee, short pwr)
{

	struct sk_buff *buf = ieee80211_null_func(ieee, pwr);

	if (buf)
		softmac_ps_mgmt_xmit(buf, ieee);

}


short ieee80211_sta_ps_sleep(struct ieee80211_device *ieee, u32 *time_h, u32 *time_l)
{
        int timeout = 0;

	u8 dtim;
	dtim = ieee->current_network.dtim_data;
	

	if(!(dtim & IEEE80211_DTIM_VALID))
		return 0;
        else
                timeout = ieee->current_network.beacon_interval;

	
	ieee->current_network.dtim_data = IEEE80211_DTIM_INVALID;

	if(dtim & ((IEEE80211_DTIM_UCAST | IEEE80211_DTIM_MBCAST)& ieee->ps))
		return 2;

	if(!time_after(jiffies, ieee->dev->trans_start + MSECS(timeout)))
		return 0;

	if(!time_after(jiffies, ieee->last_rx_ps_time + MSECS(timeout)))
		return 0;

	if((ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE ) &&
		(ieee->mgmt_queue_tail != ieee->mgmt_queue_head))
		return 0;

	if(time_l){
		*time_l = ieee->current_network.last_dtim_sta_time[0]
			+ MSECS((ieee->current_network.beacon_interval));
			
			
	}

	if(time_h){
		*time_h = ieee->current_network.last_dtim_sta_time[1];
		if(time_l && *time_l < ieee->current_network.last_dtim_sta_time[0])
			*time_h += 1;
	}

	return 1;


}

inline void ieee80211_sta_ps(struct ieee80211_device *ieee)
{

	u32 th,tl;
	short sleep;

	unsigned long flags,flags2;

	spin_lock_irqsave(&ieee->lock, flags);

	if((ieee->ps == IEEE80211_PS_DISABLED ||

		ieee->iw_mode != IW_MODE_INFRA ||
		ieee->state != IEEE80211_LINKED)){

		
		spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);

		ieee80211_sta_wakeup(ieee, 1);

		spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
	}

	sleep = ieee80211_sta_ps_sleep(ieee,&th, &tl);
	
	if(sleep == 0)
		goto out;

	if(sleep == 1){

		if(ieee->sta_sleep == 1)
			ieee->enter_sleep_state(ieee->dev,th,tl);

		else if(ieee->sta_sleep == 0){
	
			spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);

			if(ieee->ps_is_queue_empty(ieee->dev)){


				ieee->sta_sleep = 2;

				ieee->ps_request_tx_ack(ieee->dev);

				ieee80211_sta_ps_send_null_frame(ieee,1);

				ieee->ps_th = th;
				ieee->ps_tl = tl;
			}
			spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);

		}


	}else if(sleep == 2){
		spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);

	
		ieee80211_sta_wakeup(ieee,1);

		spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
	}

out:
	spin_unlock_irqrestore(&ieee->lock, flags);

}

void ieee80211_sta_wakeup(struct ieee80211_device *ieee, short nl)
{
	if(ieee->sta_sleep == 0){
		if(nl){
		
			ieee->ps_request_tx_ack(ieee->dev);
			ieee80211_sta_ps_send_null_frame(ieee, 0);
		}
		return;

	}

	if(ieee->sta_sleep == 1)
		ieee->sta_wake_up(ieee->dev);

	ieee->sta_sleep = 0;

	if(nl){
		ieee->ps_request_tx_ack(ieee->dev);
		ieee80211_sta_ps_send_null_frame(ieee, 0);
	}
}

void ieee80211_ps_tx_ack(struct ieee80211_device *ieee, short success)
{
	unsigned long flags,flags2;

	spin_lock_irqsave(&ieee->lock, flags);
	if(ieee->sta_sleep == 2){
		
		if(success){

		
			ieee->sta_sleep = 1;
			ieee->enter_sleep_state(ieee->dev,ieee->ps_th,ieee->ps_tl);
		}
	}
	
	else {

		if((ieee->sta_sleep == 0) && !success){
			spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);
			ieee80211_sta_ps_send_null_frame(ieee, 0);
			spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
		}
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

inline int
ieee80211_rx_frame_softmac(struct ieee80211_device *ieee, struct sk_buff *skb,
			struct ieee80211_rx_stats *rx_stats, u16 type,
			u16 stype)
{
	struct ieee80211_hdr_3addr *header = (struct ieee80211_hdr_3addr *) skb->data;
	u16 errcode;
	u8* challenge=NULL;
	int chlen=0;
	int aid=0;
	struct ieee80211_assoc_response_frame *assoc_resp;
	struct ieee80211_info_element *info_element;

	if(!ieee->proto_started)
		return 0;

	if(ieee->sta_sleep || (ieee->ps != IEEE80211_PS_DISABLED &&
		ieee->iw_mode == IW_MODE_INFRA &&
		ieee->state == IEEE80211_LINKED))

		tasklet_schedule(&ieee->ps_task);

	if (WLAN_FC_GET_STYPE(header->frame_control) != IEEE80211_STYPE_PROBE_RESP &&
		WLAN_FC_GET_STYPE(header->frame_control) != IEEE80211_STYPE_BEACON)
		ieee->last_rx_ps_time = jiffies;

	switch (WLAN_FC_GET_STYPE(header->frame_control)) {

		case IEEE80211_STYPE_ASSOC_RESP:
		case IEEE80211_STYPE_REASSOC_RESP:

			IEEE80211_DEBUG_MGMT("received [RE]ASSOCIATION RESPONSE (%d)\n",
					WLAN_FC_GET_STYPE(header->frame_ctl));
			if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
				ieee->state == IEEE80211_ASSOCIATING_AUTHENTICATED &&
				ieee->iw_mode == IW_MODE_INFRA){
				if (0 == (errcode=assoc_parse(skb, &aid))){
					u16 left;

					ieee->state=IEEE80211_LINKED;
					ieee->assoc_id = aid;
					ieee->softmac_stats.rx_ass_ok++;

					
					if(1 == rx_stats->nic_type) 
					{
						goto associate_complete;
					}
					assoc_resp = (struct ieee80211_assoc_response_frame*)skb->data;
					info_element = 	&assoc_resp->info_element;
					left = skb->len - ((void*)info_element - (void*)assoc_resp);

					while (left >= sizeof(struct ieee80211_info_element_hdr)) {
						if (sizeof(struct ieee80211_info_element_hdr) + info_element->len > left) {
							printk(KERN_WARNING "[re]associate reeponse error!");
							return 1;
						}
						switch (info_element->id) {
						  case MFIE_TYPE_GENERIC:
						         IEEE80211_DEBUG_SCAN("MFIE_TYPE_GENERIC: %d bytes\n", info_element->len);
							if (info_element->len >= 8  &&
							    info_element->data[0] == 0x00 &&
							    info_element->data[1] == 0x50 &&
							    info_element->data[2] == 0xf2 &&
							    info_element->data[3] == 0x02 &&
							    info_element->data[4] == 0x01) {
							    
							    
							    memcpy(ieee->current_network.wmm_param,(u8*)(info_element->data\
										    + 8),(info_element->len - 8));

					 	            if (((ieee->current_network.wmm_info^info_element->data[6])& \
										    0x0f)||(!ieee->init_wmmparam_flag)) {
						   	      
							      
							      ieee->init_wmmparam_flag = 1;
							      queue_work(ieee->wq, &ieee->wmm_param_update_wq);

						            }
						            
						            ieee->current_network.wmm_info  = info_element->data[6];
							}
							break;
						  default:
							
							break;
						}

						left -= sizeof(struct ieee80211_info_element_hdr) +
							info_element->len;
						info_element = (struct ieee80211_info_element *)
							&info_element->data[info_element->len];
					}
					if(!ieee->init_wmmparam_flag) 
					{
						queue_work(ieee->wq,&ieee->wmm_param_update_wq);
						ieee->init_wmmparam_flag = 1;
					}
associate_complete:
					ieee80211_associate_complete(ieee);
				}else{
					ieee->softmac_stats.rx_ass_err++;
					IEEE80211_DEBUG_MGMT(
						"Association response status code 0x%x\n",
						errcode);
					ieee80211_associate_abort(ieee);
				}
			}
			break;

		case IEEE80211_STYPE_ASSOC_REQ:
		case IEEE80211_STYPE_REASSOC_REQ:

			if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
				ieee->iw_mode == IW_MODE_MASTER)

				ieee80211_rx_assoc_rq(ieee, skb);
			break;

		case IEEE80211_STYPE_AUTH:

			if (ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE){
				if (ieee->state == IEEE80211_ASSOCIATING_AUTHENTICATING &&
				ieee->iw_mode == IW_MODE_INFRA){

						IEEE80211_DEBUG_MGMT("Received authentication response");

						if (0 == (errcode=auth_parse(skb, &challenge, &chlen))){
							if(ieee->open_wep || !challenge){
								ieee->state = IEEE80211_ASSOCIATING_AUTHENTICATED;
								ieee->softmac_stats.rx_auth_rs_ok++;

								ieee80211_associate_step2(ieee);
							}else{
								ieee80211_rtl_auth_challenge(ieee, challenge, chlen);
							}
						}else{
							ieee->softmac_stats.rx_auth_rs_err++;
							IEEE80211_DEBUG_MGMT("Authentication respose status code 0x%x",errcode);
							ieee80211_associate_abort(ieee);
						}

					}else if (ieee->iw_mode == IW_MODE_MASTER){
						ieee80211_rx_auth_rq(ieee, skb);
					}
				}
			break;

		case IEEE80211_STYPE_PROBE_REQ:

			if ((ieee->softmac_features & IEEE_SOFTMAC_PROBERS) &&
				((ieee->iw_mode == IW_MODE_ADHOC ||
				ieee->iw_mode == IW_MODE_MASTER) &&
				ieee->state == IEEE80211_LINKED))

				ieee80211_rx_probe_rq(ieee, skb);
			break;

		case IEEE80211_STYPE_DISASSOC:
		case IEEE80211_STYPE_DEAUTH:
			if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
				(ieee->state == IEEE80211_LINKED) &&
				(ieee->iw_mode == IW_MODE_INFRA) &&
				(!memcmp(header->addr2,ieee->current_network.bssid,ETH_ALEN))){
				ieee->state = IEEE80211_ASSOCIATING;
				ieee->softmac_stats.reassoc++;

				
				queue_work(ieee->wq, &ieee->associate_procedure_wq);
			}

			break;

		default:
			return -1;
			break;
	}

	
	return 0;
}




void ieee80211_softmac_xmit(struct ieee80211_txb *txb, struct ieee80211_device *ieee)
{


	unsigned long flags;
	int  i;

	spin_lock_irqsave(&ieee->lock,flags);

	
	ieee80211_sta_wakeup(ieee,0);

	for(i = 0; i < txb->nr_frags; i++) {

		if (ieee->queue_stop){
			ieee->tx_pending.txb = txb;
			ieee->tx_pending.frag = i;
			goto exit;
		}else{
			ieee->softmac_data_hard_start_xmit(
				txb->fragments[i],
				ieee->dev,ieee->rate);
				
			ieee->stats.tx_packets++;
			ieee->stats.tx_bytes += txb->fragments[i]->len;
			ieee->dev->trans_start = jiffies;
		}
	}

	ieee80211_txb_free(txb);

	exit:
	spin_unlock_irqrestore(&ieee->lock,flags);

}

void ieee80211_resume_tx(struct ieee80211_device *ieee)
{
	int i;
	for(i = ieee->tx_pending.frag; i < ieee->tx_pending.txb->nr_frags; i++) {

		if (ieee->queue_stop){
			ieee->tx_pending.frag = i;
			return;
		}else{

			ieee->softmac_data_hard_start_xmit(
				ieee->tx_pending.txb->fragments[i],
				ieee->dev,ieee->rate);
				
			ieee->stats.tx_packets++;
			ieee->dev->trans_start = jiffies;
		}
	}


	ieee80211_txb_free(ieee->tx_pending.txb);
	ieee->tx_pending.txb = NULL;
}


void ieee80211_reset_queue(struct ieee80211_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock,flags);
	init_mgmt_queue(ieee);
	if (ieee->tx_pending.txb){
		ieee80211_txb_free(ieee->tx_pending.txb);
		ieee->tx_pending.txb = NULL;
	}
	ieee->queue_stop = 0;
	spin_unlock_irqrestore(&ieee->lock,flags);

}

void ieee80211_rtl_wake_queue(struct ieee80211_device *ieee)
{

	unsigned long flags;
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr  *header;

	spin_lock_irqsave(&ieee->lock,flags);
	if (! ieee->queue_stop) goto exit;

	ieee->queue_stop = 0;

	if(ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE){
		while (!ieee->queue_stop && (skb = dequeue_mgmt(ieee))){

			header = (struct ieee80211_hdr_3addr  *) skb->data;

			header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

			if (ieee->seq_ctrl[0] == 0xFFF)
				ieee->seq_ctrl[0] = 0;
			else
				ieee->seq_ctrl[0]++;

			
			ieee->softmac_data_hard_start_xmit(skb,ieee->dev,ieee->basic_rate);
			dev_kfree_skb_any(skb);
		}
	}
	if (!ieee->queue_stop && ieee->tx_pending.txb)
		ieee80211_resume_tx(ieee);

	if (!ieee->queue_stop && netif_queue_stopped(ieee->dev)){
		ieee->softmac_stats.swtxawake++;
		netif_wake_queue(ieee->dev);
	}

exit :
	spin_unlock_irqrestore(&ieee->lock,flags);
}


void ieee80211_rtl_stop_queue(struct ieee80211_device *ieee)
{
	
	

	if (! netif_queue_stopped(ieee->dev)){
		netif_stop_queue(ieee->dev);
		ieee->softmac_stats.swtxstop++;
	}
	ieee->queue_stop = 1;
	

}


inline void ieee80211_randomize_cell(struct ieee80211_device *ieee)
{

	get_random_bytes(ieee->current_network.bssid, ETH_ALEN);

	ieee->current_network.bssid[0] &= ~0x01;
	ieee->current_network.bssid[0] |= 0x02;
}

void ieee80211_start_master_bss(struct ieee80211_device *ieee)
{
	ieee->assoc_id = 1;

	if (ieee->current_network.ssid_len == 0){
		strncpy(ieee->current_network.ssid,
			IEEE80211_DEFAULT_TX_ESSID,
			IW_ESSID_MAX_SIZE);

		ieee->current_network.ssid_len = strlen(IEEE80211_DEFAULT_TX_ESSID);
		ieee->ssid_set = 1;
	}

	memcpy(ieee->current_network.bssid, ieee->dev->dev_addr, ETH_ALEN);

	ieee->set_chan(ieee->dev, ieee->current_network.channel);
	ieee->state = IEEE80211_LINKED;
	ieee->link_change(ieee->dev);
	notify_wx_assoc_event(ieee);

	if (ieee->data_hard_resume)
		ieee->data_hard_resume(ieee->dev);

	netif_carrier_on(ieee->dev);
}

void ieee80211_start_monitor_mode(struct ieee80211_device *ieee)
{
	if(ieee->raw_tx){

		if (ieee->data_hard_resume)
			ieee->data_hard_resume(ieee->dev);

		netif_carrier_on(ieee->dev);
	}
}

void ieee80211_start_ibss_wq(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ieee80211_device *ieee = container_of(dwork, struct ieee80211_device, start_ibss_wq);


	down(&ieee->wx_sem);


	if (ieee->current_network.ssid_len == 0){
		strcpy(ieee->current_network.ssid,IEEE80211_DEFAULT_TX_ESSID);
		ieee->current_network.ssid_len = strlen(IEEE80211_DEFAULT_TX_ESSID);
		ieee->ssid_set = 1;
	}

	
	ieee80211_softmac_check_all_nets(ieee);

	if(ieee->state == IEEE80211_NOLINK)
		ieee->current_network.channel = 10;
	if (ieee->state == IEEE80211_NOLINK)
		ieee80211_start_scan_syncro(ieee);

	
	if (ieee->state == IEEE80211_NOLINK){
		printk("creating new IBSS cell\n");
		if(!ieee->wap_set)
			ieee80211_randomize_cell(ieee);

		if(ieee->modulation & IEEE80211_CCK_MODULATION){

			ieee->current_network.rates_len = 4;

			ieee->current_network.rates[0] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_1MB;
			ieee->current_network.rates[1] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_2MB;
			ieee->current_network.rates[2] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_5MB;
			ieee->current_network.rates[3] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_11MB;

		}else
			ieee->current_network.rates_len = 0;

		if(ieee->modulation & IEEE80211_OFDM_MODULATION){
			ieee->current_network.rates_ex_len = 8;

			ieee->current_network.rates_ex[0] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_6MB;
			ieee->current_network.rates_ex[1] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_9MB;
			ieee->current_network.rates_ex[2] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_12MB;
			ieee->current_network.rates_ex[3] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_18MB;
			ieee->current_network.rates_ex[4] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_24MB;
			ieee->current_network.rates_ex[5] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_36MB;
			ieee->current_network.rates_ex[6] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_48MB;
			ieee->current_network.rates_ex[7] = IEEE80211_BASIC_RATE_MASK | IEEE80211_OFDM_RATE_54MB;

			ieee->rate = 540;
		}else{
			ieee->current_network.rates_ex_len = 0;
			ieee->rate = 110;
		}

		
		ieee->current_network.QoS_Enable = 0;

		ieee->current_network.atim_window = 0;
		ieee->current_network.capability = WLAN_CAPABILITY_IBSS;
		if(ieee->short_slot)
			ieee->current_network.capability |= WLAN_CAPABILITY_SHORT_SLOT;

	}

	ieee->state = IEEE80211_LINKED;
	ieee->set_chan(ieee->dev, ieee->current_network.channel);
	ieee->link_change(ieee->dev);

	notify_wx_assoc_event(ieee);

	ieee80211_start_send_beacons(ieee);
	printk(KERN_WARNING "after sending beacon packet!\n");

	if (ieee->data_hard_resume)
		ieee->data_hard_resume(ieee->dev);

	netif_carrier_on(ieee->dev);

	up(&ieee->wx_sem);
}
inline void ieee80211_start_ibss(struct ieee80211_device *ieee)
{
	queue_delayed_work(ieee->wq, &ieee->start_ibss_wq, 100);
}

void ieee80211_start_bss(struct ieee80211_device *ieee)
{
	unsigned long flags;
	
	
	
	
	if(IS_DOT11D_ENABLE(ieee) && !IS_COUNTRY_IE_VALID(ieee))
	{
		if(! ieee->bGlobalDomain)
		{
			return;
		}
	}
	ieee80211_softmac_check_all_nets(ieee);

	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->state == IEEE80211_NOLINK){
		ieee->actscanning = true;
		ieee80211_rtl_start_scan(ieee);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

void ieee80211_disassociate(struct ieee80211_device *ieee)
{
	netif_carrier_off(ieee->dev);

	if (ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE)
			ieee80211_reset_queue(ieee);

	if (ieee->data_hard_stop)
			ieee->data_hard_stop(ieee->dev);

	if(IS_DOT11D_ENABLE(ieee))
		Dot11d_Reset(ieee);

	ieee->link_change(ieee->dev);
	if (ieee->state == IEEE80211_LINKED)
		notify_wx_assoc_event(ieee);
	ieee->state = IEEE80211_NOLINK;

}
void ieee80211_associate_retry_wq(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ieee80211_device *ieee = container_of(dwork, struct ieee80211_device, associate_retry_wq);
	unsigned long flags;
	down(&ieee->wx_sem);
	if(!ieee->proto_started)
		goto exit;
	if(ieee->state != IEEE80211_ASSOCIATING_RETRY)
		goto exit;
	ieee->state = IEEE80211_NOLINK;
	ieee->beinretry = true;
	ieee80211_softmac_check_all_nets(ieee);

	spin_lock_irqsave(&ieee->lock, flags);

	if(ieee->state == IEEE80211_NOLINK){
		ieee->beinretry = false;
		ieee->actscanning = true;
		ieee80211_rtl_start_scan(ieee);
	}
	
	if(ieee->state == IEEE80211_NOLINK)
	{
		notify_wx_assoc_event(ieee);
	}
	
	spin_unlock_irqrestore(&ieee->lock, flags);

exit:
	up(&ieee->wx_sem);
}

struct sk_buff *ieee80211_get_beacon_(struct ieee80211_device *ieee)
{
	u8 broadcast_addr[] = {0xff,0xff,0xff,0xff,0xff,0xff};

	struct sk_buff *skb = NULL;
	struct ieee80211_probe_response *b;

	skb = ieee80211_probe_resp(ieee, broadcast_addr);
	if (!skb)
		return NULL;

	b = (struct ieee80211_probe_response *) skb->data;
	b->header.frame_ctl = cpu_to_le16(IEEE80211_STYPE_BEACON);

	return skb;

}

struct sk_buff *ieee80211_get_beacon(struct ieee80211_device *ieee)
{
	struct sk_buff *skb;
	struct ieee80211_probe_response *b;

	skb = ieee80211_get_beacon_(ieee);
	if(!skb)
		return NULL;

	b = (struct ieee80211_probe_response *) skb->data;
	b->header.seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

	if (ieee->seq_ctrl[0] == 0xFFF)
		ieee->seq_ctrl[0] = 0;
	else
		ieee->seq_ctrl[0]++;

	return skb;
}

void ieee80211_softmac_stop_protocol(struct ieee80211_device *ieee)
{
	ieee->sync_scan_hurryup = 1;
	down(&ieee->wx_sem);
	ieee80211_stop_protocol(ieee);
	up(&ieee->wx_sem);
}


void ieee80211_stop_protocol(struct ieee80211_device *ieee)
{
	if (!ieee->proto_started)
		return;

	ieee->proto_started = 0;

	ieee80211_stop_send_beacons(ieee);
	if((ieee->iw_mode == IW_MODE_INFRA)&&(ieee->state == IEEE80211_LINKED)) {
		SendDisassociation(ieee,NULL,WLAN_REASON_DISASSOC_STA_HAS_LEFT);
	}
	del_timer_sync(&ieee->associate_timer);
	cancel_delayed_work(&ieee->associate_retry_wq);
	cancel_delayed_work(&ieee->start_ibss_wq);
	ieee80211_stop_scan(ieee);

	ieee80211_disassociate(ieee);
}

void ieee80211_softmac_start_protocol(struct ieee80211_device *ieee)
{
	ieee->sync_scan_hurryup = 0;
	down(&ieee->wx_sem);
	ieee80211_start_protocol(ieee);
	up(&ieee->wx_sem);
}

void ieee80211_start_protocol(struct ieee80211_device *ieee)
{
	short ch = 0;
 	int i = 0;

	if (ieee->proto_started)
		return;

	ieee->proto_started = 1;

	if (ieee->current_network.channel == 0){
		do{
			ch++;
			if (ch > MAX_CHANNEL_NUMBER)
				return; 

		}while(!GET_DOT11D_INFO(ieee)->channel_map[ch]);

		ieee->current_network.channel = ch;
	}

	if (ieee->current_network.beacon_interval == 0)
		ieee->current_network.beacon_interval = 100;
	ieee->set_chan(ieee->dev,ieee->current_network.channel);

       	for(i = 0; i < 17; i++) {
	  ieee->last_rxseq_num[i] = -1;
	  ieee->last_rxfrag_num[i] = -1;
	  ieee->last_packet_time[i] = 0;
	}

	ieee->init_wmmparam_flag = 0;


	switch (ieee->iw_mode) {
		case IW_MODE_AUTO:
			ieee->iw_mode = IW_MODE_INFRA;
			
		case IW_MODE_INFRA:
			ieee80211_start_bss(ieee);
			break;

		case IW_MODE_ADHOC:
			ieee80211_start_ibss(ieee);
			break;

		case IW_MODE_MASTER:
			ieee80211_start_master_bss(ieee);
		break;

		case IW_MODE_MONITOR:
			ieee80211_start_monitor_mode(ieee);
			break;

		default:
			ieee->iw_mode = IW_MODE_INFRA;
			ieee80211_start_bss(ieee);
			break;
	}
}


#define DRV_NAME  "Ieee80211"
void ieee80211_softmac_init(struct ieee80211_device *ieee)
{
	int i;
	memset(&ieee->current_network, 0, sizeof(struct ieee80211_network));

	ieee->state = IEEE80211_NOLINK;
	ieee->sync_scan_hurryup = 0;
	for(i = 0; i < 5; i++) {
	  ieee->seq_ctrl[i] = 0;
	}

	ieee->assoc_id = 0;
	ieee->queue_stop = 0;
	ieee->scanning = 0;
	ieee->softmac_features = 0; 
	ieee->wap_set = 0;
	ieee->ssid_set = 0;
	ieee->proto_started = 0;
	ieee->basic_rate = IEEE80211_DEFAULT_BASIC_RATE;
	ieee->rate = 3;
	ieee->ps = IEEE80211_PS_MBCAST|IEEE80211_PS_UNICAST;
	ieee->sta_sleep = 0;
	ieee->bInactivePs = false;
	ieee->actscanning = false;
	ieee->ListenInterval = 2;
	ieee->NumRxDataInPeriod = 0; 
	ieee->NumRxBcnInPeriod = 0; 
	ieee->NumRxOkTotal = 0;
	ieee->NumRxUnicast = 0;
	ieee->beinretry = false;
	ieee->bHwRadioOff = false;

	init_mgmt_queue(ieee);

	ieee->tx_pending.txb = NULL;

	init_timer(&ieee->associate_timer);
	ieee->associate_timer.data = (unsigned long)ieee;
	ieee->associate_timer.function = ieee80211_associate_abort_cb;

	init_timer(&ieee->beacon_timer);
	ieee->beacon_timer.data = (unsigned long) ieee;
	ieee->beacon_timer.function = ieee80211_send_beacon_cb;

	ieee->wq = create_workqueue(DRV_NAME);

	INIT_DELAYED_WORK(&ieee->start_ibss_wq,(void*) ieee80211_start_ibss_wq);
	INIT_WORK(&ieee->associate_complete_wq,(void*) ieee80211_associate_complete_wq);
	INIT_WORK(&ieee->associate_procedure_wq,(void*) ieee80211_associate_procedure_wq);
	INIT_DELAYED_WORK(&ieee->softmac_scan_wq,(void*) ieee80211_softmac_scan_wq);
	INIT_DELAYED_WORK(&ieee->associate_retry_wq,(void*) ieee80211_associate_retry_wq);
	INIT_WORK(&ieee->wx_sync_scan_wq,(void*) ieee80211_wx_sync_scan_wq);

	sema_init(&ieee->wx_sem, 1);
	sema_init(&ieee->scan_sem, 1);

	spin_lock_init(&ieee->mgmt_tx_lock);
	spin_lock_init(&ieee->beacon_lock);

	tasklet_init(&ieee->ps_task,
	     (void(*)(unsigned long)) ieee80211_sta_ps,
	     (unsigned long)ieee);
	ieee->pDot11dInfo = kmalloc(sizeof(RT_DOT11D_INFO), GFP_ATOMIC);
}

void ieee80211_softmac_free(struct ieee80211_device *ieee)
{
	down(&ieee->wx_sem);

	del_timer_sync(&ieee->associate_timer);
	cancel_delayed_work(&ieee->associate_retry_wq);


	
	cancel_delayed_work(&ieee->GPIOChangeRFWorkItem);

	destroy_workqueue(ieee->wq);
	kfree(ieee->pDot11dInfo);
	up(&ieee->wx_sem);
}



static int ieee80211_wpa_enable(struct ieee80211_device *ieee, int value)
{
	printk("%s WPA\n",value ? "enabling" : "disabling");
	ieee->wpa_enabled = value;
	return 0;
}


void ieee80211_wpa_assoc_frame(struct ieee80211_device *ieee, char *wpa_ie, int wpa_ie_len)
{
	
	ieee80211_wpa_enable(ieee, 1);

	ieee80211_disassociate(ieee);
}


static int ieee80211_wpa_mlme(struct ieee80211_device *ieee, int command, int reason)
{

	int ret = 0;

	switch (command) {
	case IEEE_MLME_STA_DEAUTH:
		
		break;

	case IEEE_MLME_STA_DISASSOC:
		ieee80211_disassociate(ieee);
		break;

	default:
		printk("Unknown MLME request: %d\n", command);
		ret = -EOPNOTSUPP;
	}

	return ret;
}


static int ieee80211_wpa_set_wpa_ie(struct ieee80211_device *ieee,
			      struct ieee_param *param, int plen)
{
	u8 *buf;

	if (param->u.wpa_ie.len > MAX_WPA_IE_LEN ||
	    (param->u.wpa_ie.len && param->u.wpa_ie.data == NULL))
		return -EINVAL;

	if (param->u.wpa_ie.len) {
		buf = kmemdup(param->u.wpa_ie.data, param->u.wpa_ie.len,
			      GFP_KERNEL);
		if (buf == NULL)
			return -ENOMEM;

		kfree(ieee->wpa_ie);
		ieee->wpa_ie = buf;
		ieee->wpa_ie_len = param->u.wpa_ie.len;
	} else {
		kfree(ieee->wpa_ie);
		ieee->wpa_ie = NULL;
		ieee->wpa_ie_len = 0;
	}

	ieee80211_wpa_assoc_frame(ieee, ieee->wpa_ie, ieee->wpa_ie_len);
	return 0;
}

#define AUTH_ALG_OPEN_SYSTEM			0x1
#define AUTH_ALG_SHARED_KEY			0x2

static int ieee80211_wpa_set_auth_algs(struct ieee80211_device *ieee, int value)
{

	struct ieee80211_security sec = {
		.flags = SEC_AUTH_MODE,
	};
	int ret = 0;

	if (value & AUTH_ALG_SHARED_KEY) {
		sec.auth_mode = WLAN_AUTH_SHARED_KEY;
		ieee->open_wep = 0;
	} else {
		sec.auth_mode = WLAN_AUTH_OPEN;
		ieee->open_wep = 1;
	}

	if (ieee->set_security)
		ieee->set_security(ieee->dev, &sec);
	else
		ret = -EOPNOTSUPP;

	return ret;
}

static int ieee80211_wpa_set_param(struct ieee80211_device *ieee, u8 name, u32 value)
{
	int ret=0;
	unsigned long flags;

	switch (name) {
	case IEEE_PARAM_WPA_ENABLED:
		ret = ieee80211_wpa_enable(ieee, value);
		break;

	case IEEE_PARAM_TKIP_COUNTERMEASURES:
		ieee->tkip_countermeasures=value;
		break;

	case IEEE_PARAM_DROP_UNENCRYPTED: {
		struct ieee80211_security sec = {
			.flags = SEC_ENABLED,
			.enabled = value,
		};
 		ieee->drop_unencrypted = value;
		if (!value) {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_0;
		}
		else {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_1;
		}
		if (ieee->set_security)
			ieee->set_security(ieee->dev, &sec);
		break;
	}

	case IEEE_PARAM_PRIVACY_INVOKED:
		ieee->privacy_invoked=value;
		break;

	case IEEE_PARAM_AUTH_ALGS:
		ret = ieee80211_wpa_set_auth_algs(ieee, value);
		break;

	case IEEE_PARAM_IEEE_802_1X:
		ieee->ieee802_1x=value;
		break;
	case IEEE_PARAM_WPAX_SELECT:
		
		
		spin_lock_irqsave(&ieee->wpax_suitlist_lock,flags);
		ieee->wpax_type_set = 1;
		ieee->wpax_type_notify = value;
		spin_unlock_irqrestore(&ieee->wpax_suitlist_lock,flags);
		break;

	default:
		printk("Unknown WPA param: %d\n",name);
		ret = -EOPNOTSUPP;
	}

	return ret;
}


static int ieee80211_wpa_set_encryption(struct ieee80211_device *ieee,
				  struct ieee_param *param, int param_len)
{
	int ret = 0;

	struct ieee80211_crypto_ops *ops;
	struct ieee80211_crypt_data **crypt;

	struct ieee80211_security sec = {
		.flags = 0,
	};

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len !=
	    (int) ((char *) param->u.crypt.key - (char *) param) +
	    param->u.crypt.key_len) {
		printk("Len mismatch %d, %d\n", param_len,
			       param->u.crypt.key_len);
		return -EINVAL;
	}
	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS)
			return -EINVAL;
		crypt = &ieee->crypt[param->u.crypt.idx];
	} else {
		return -EINVAL;
	}

	if (strcmp(param->u.crypt.alg, "none") == 0) {
		if (crypt) {
			sec.enabled = 0;
			
			
			sec.level = SEC_LEVEL_0;
			sec.flags |= SEC_ENABLED | SEC_LEVEL;
			ieee80211_crypt_delayed_deinit(ieee, crypt);
		}
		goto done;
	}
	sec.enabled = 1;
	sec.flags |= SEC_ENABLED;

	
	if (!(ieee->host_encrypt || ieee->host_decrypt) &&
	    strcmp(param->u.crypt.alg, "TKIP"))
		goto skip_host_crypt;

	ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	if (ops == NULL && strcmp(param->u.crypt.alg, "WEP") == 0)
		ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	else if (ops == NULL && strcmp(param->u.crypt.alg, "TKIP") == 0)
		ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	else if (ops == NULL && strcmp(param->u.crypt.alg, "CCMP") == 0)
		ops = ieee80211_get_crypto_ops(param->u.crypt.alg);
	if (ops == NULL) {
		printk("unknown crypto alg '%s'\n", param->u.crypt.alg);
		param->u.crypt.err = IEEE_CRYPT_ERR_UNKNOWN_ALG;
		ret = -EINVAL;
		goto done;
	}

	if (*crypt == NULL || (*crypt)->ops != ops) {
		struct ieee80211_crypt_data *new_crypt;

		ieee80211_crypt_delayed_deinit(ieee, crypt);

		new_crypt = kmalloc(sizeof(*new_crypt), GFP_KERNEL);
		if (new_crypt == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		memset(new_crypt, 0, sizeof(struct ieee80211_crypt_data));
		new_crypt->ops = ops;
		if (new_crypt->ops)
			new_crypt->priv =
				new_crypt->ops->init(param->u.crypt.idx);

		if (new_crypt->priv == NULL) {
			kfree(new_crypt);
			param->u.crypt.err = IEEE_CRYPT_ERR_CRYPT_INIT_FAILED;
			ret = -EINVAL;
			goto done;
		}

		*crypt = new_crypt;
	}

	if (param->u.crypt.key_len > 0 && (*crypt)->ops->set_key &&
	    (*crypt)->ops->set_key(param->u.crypt.key,
				   param->u.crypt.key_len, param->u.crypt.seq,
				   (*crypt)->priv) < 0) {
		printk("key setting failed\n");
		param->u.crypt.err = IEEE_CRYPT_ERR_KEY_SET_FAILED;
		ret = -EINVAL;
		goto done;
	}

 skip_host_crypt:
	if (param->u.crypt.set_tx) {
		ieee->tx_keyidx = param->u.crypt.idx;
		sec.active_key = param->u.crypt.idx;
		sec.flags |= SEC_ACTIVE_KEY;
	} else
		sec.flags &= ~SEC_ACTIVE_KEY;

	if (param->u.crypt.alg != NULL) {
		memcpy(sec.keys[param->u.crypt.idx],
		       param->u.crypt.key,
		       param->u.crypt.key_len);
		sec.key_sizes[param->u.crypt.idx] = param->u.crypt.key_len;
		sec.flags |= (1 << param->u.crypt.idx);

		if (strcmp(param->u.crypt.alg, "WEP") == 0) {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_1;
		} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_2;
		} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
			sec.flags |= SEC_LEVEL;
			sec.level = SEC_LEVEL_3;
		}
	}
 done:
	if (ieee->set_security)
		ieee->set_security(ieee->dev, &sec);

	if (ieee->reset_on_keychange &&
	    ieee->iw_mode != IW_MODE_INFRA &&
	    ieee->reset_port &&
	    ieee->reset_port(ieee->dev)) {
		printk("reset_port failed\n");
		param->u.crypt.err = IEEE_CRYPT_ERR_CARD_CONF_FAILED;
		return -EINVAL;
	}

	return ret;
}

int ieee80211_wpa_supplicant_ioctl(struct ieee80211_device *ieee, struct iw_point *p)
{
	struct ieee_param *param;
	int ret=0;

	down(&ieee->wx_sem);
	

	if (p->length < sizeof(struct ieee_param) || !p->pointer){
		ret = -EINVAL;
		goto out;
	}

	param = kmalloc(p->length, GFP_KERNEL);
	if (param == NULL){
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(param, p->pointer, p->length)) {
		kfree(param);
		ret = -EFAULT;
		goto out;
	}

	switch (param->cmd) {

	case IEEE_CMD_SET_WPA_PARAM:
		ret = ieee80211_wpa_set_param(ieee, param->u.wpa_param.name,
					param->u.wpa_param.value);
		break;

	case IEEE_CMD_SET_WPA_IE:
		ret = ieee80211_wpa_set_wpa_ie(ieee, param, p->length);
		break;

	case IEEE_CMD_SET_ENCRYPTION:
		ret = ieee80211_wpa_set_encryption(ieee, param, p->length);
		break;

	case IEEE_CMD_MLME:
		ret = ieee80211_wpa_mlme(ieee, param->u.mlme.command,
				   param->u.mlme.reason_code);
		break;

	default:
		printk("Unknown WPA supplicant request: %d\n",param->cmd);
		ret = -EOPNOTSUPP;
		break;
	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;

	kfree(param);
out:
	up(&ieee->wx_sem);

	return ret;
}

void notify_wx_assoc_event(struct ieee80211_device *ieee)
{
	union iwreq_data wrqu;
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (ieee->state == IEEE80211_LINKED)
		memcpy(wrqu.ap_addr.sa_data, ieee->current_network.bssid, ETH_ALEN);
	else
		memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	wireless_send_event(ieee->dev, SIOCGIWAP, &wrqu, NULL);
}
