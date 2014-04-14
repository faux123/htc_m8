/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL12XX_H__
#define __WL12XX_H__

#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <net/mac80211.h>

#include "conf.h"
#include "ini.h"

#define WL127X_FW_NAME_MULTI "ti-connectivity/wl127x-fw-4-mr.bin"
#define WL127X_FW_NAME_SINGLE "ti-connectivity/wl127x-fw-4-sr.bin"

#define WL128X_FW_NAME_MULTI "ti-connectivity/wl128x-fw-4-mr.bin"
#define WL128X_FW_NAME_SINGLE "ti-connectivity/wl128x-fw-4-sr.bin"

#define WL127X_PLT_FW_NAME "ti-connectivity/wl127x-fw-4-plt.bin"
#define WL128X_PLT_FW_NAME "ti-connectivity/wl128x-fw-4-plt.bin"

#define WL12XX_NVS_NAME "ti-connectivity/wl1271-nvs.bin"

#define WL1271_TX_SECURITY_LO16(s) ((u16)((s) & 0xffff))
#define WL1271_TX_SECURITY_HI32(s) ((u32)(((s) >> 16) & 0xffffffff))
#define WL1271_TX_SQN_POST_RECOVERY_PADDING 0xff

#define WL1271_CIPHER_SUITE_GEM 0x00147201

#define WL1271_BUSY_WORD_CNT 1
#define WL1271_BUSY_WORD_LEN (WL1271_BUSY_WORD_CNT * sizeof(u32))

#define WL1271_ELP_HW_STATE_ASLEEP 0
#define WL1271_ELP_HW_STATE_IRQ    1

#define WL1271_DEFAULT_BEACON_INT  100
#define WL1271_DEFAULT_DTIM_PERIOD 1

#define WL12XX_MAX_ROLES           4
#define WL12XX_MAX_LINKS           12
#define WL12XX_INVALID_ROLE_ID     0xff
#define WL12XX_INVALID_LINK_ID     0xff

#define WL12XX_MAX_RATE_POLICIES 16

#define WL12XX_SYSTEM_HLID         0

#define WL1271_PS_STA_MAX_PACKETS  2

#define WL1271_AP_BSS_INDEX        0
#define WL1271_AP_DEF_BEACON_EXP   20

#define ACX_TX_DESCRIPTORS         16

#define WL1271_AGGR_BUFFER_SIZE (4 * PAGE_SIZE)

enum wl1271_state {
	WL1271_STATE_OFF,
	WL1271_STATE_ON,
};

enum wl12xx_fw_type {
	WL12XX_FW_TYPE_NONE,
	WL12XX_FW_TYPE_NORMAL,
	WL12XX_FW_TYPE_MULTI,
	WL12XX_FW_TYPE_PLT,
};

enum wl1271_partition_type {
	PART_DOWN,
	PART_WORK,
	PART_DRPW,

	PART_TABLE_LEN
};

struct wl1271_partition {
	u32 size;
	u32 start;
};

struct wl1271_partition_set {
	struct wl1271_partition mem;
	struct wl1271_partition reg;
	struct wl1271_partition mem2;
	struct wl1271_partition mem3;
};

struct wl1271;

enum {
	FW_VER_CHIP,
	FW_VER_IF_TYPE,
	FW_VER_MAJOR,
	FW_VER_SUBTYPE,
	FW_VER_MINOR,

	NUM_FW_VER
};

#define FW_VER_CHIP_WL127X 6
#define FW_VER_CHIP_WL128X 7

#define FW_VER_IF_TYPE_STA 1
#define FW_VER_IF_TYPE_AP  2

#define FW_VER_MINOR_1_SPARE_STA_MIN 58
#define FW_VER_MINOR_1_SPARE_AP_MIN  47

#define FW_VER_MINOR_FWLOG_STA_MIN 70

struct wl1271_chip {
	u32 id;
	char fw_ver_str[ETHTOOL_BUSINFO_LEN];
	unsigned int fw_ver[NUM_FW_VER];
};

struct wl1271_stats {
	struct acx_statistics *fw_stats;
	unsigned long fw_stats_update;

	unsigned int retry_count;
	unsigned int excessive_retries;
};

#define NUM_TX_QUEUES              4
#define NUM_RX_PKT_DESC            8

#define AP_MAX_STATIONS            8

struct wl12xx_fw_status {
	__le32 intr;
	u8  fw_rx_counter;
	u8  drv_rx_counter;
	u8  reserved;
	u8  tx_results_counter;
	__le32 rx_pkt_descs[NUM_RX_PKT_DESC];
	__le32 fw_localtime;

	__le32 link_ps_bitmap;

	__le32 link_fast_bitmap;

	
	__le32 total_released_blks;

	
	__le32 tx_total;

	
	u8 tx_released_pkts[NUM_TX_QUEUES];

	
	u8 tx_lnk_free_pkts[WL12XX_MAX_LINKS];

	
	u8 tx_voice_released_blks;
	u8 padding_1[3];
	__le32 log_start_addr;
} __packed;

struct wl1271_rx_mem_pool_addr {
	u32 addr;
	u32 addr_extra;
};

#define WL1271_MAX_CHANNELS 64
struct wl1271_scan {
	struct cfg80211_scan_request *req;
	unsigned long scanned_ch[BITS_TO_LONGS(WL1271_MAX_CHANNELS)];
	bool failed;
	u8 state;
	u8 ssid[IEEE80211_MAX_SSID_LEN+1];
	size_t ssid_len;
};

struct wl1271_if_operations {
	void (*read)(struct device *child, int addr, void *buf, size_t len,
		     bool fixed);
	void (*write)(struct device *child, int addr, void *buf, size_t len,
		     bool fixed);
	void (*reset)(struct device *child);
	void (*init)(struct device *child);
	int (*power)(struct device *child, bool enable);
	void (*set_block_size) (struct device *child, unsigned int blksz);
};

#define MAX_NUM_KEYS 14
#define MAX_KEY_SIZE 32

struct wl1271_ap_key {
	u8 id;
	u8 key_type;
	u8 key_size;
	u8 key[MAX_KEY_SIZE];
	u8 hlid;
	u32 tx_seq_32;
	u16 tx_seq_16;
};

enum wl12xx_flags {
	WL1271_FLAG_GPIO_POWER,
	WL1271_FLAG_TX_QUEUE_STOPPED,
	WL1271_FLAG_TX_PENDING,
	WL1271_FLAG_IN_ELP,
	WL1271_FLAG_ELP_REQUESTED,
	WL1271_FLAG_IRQ_RUNNING,
	WL1271_FLAG_FW_TX_BUSY,
	WL1271_FLAG_DUMMY_PACKET_PENDING,
	WL1271_FLAG_SUSPENDED,
	WL1271_FLAG_PENDING_WORK,
	WL1271_FLAG_SOFT_GEMINI,
	WL1271_FLAG_RECOVERY_IN_PROGRESS,
	WL1271_FLAG_VIF_CHANGE_IN_PROGRESS,
	WL1271_FLAG_INTENDED_FW_RECOVERY,
};

enum wl12xx_vif_flags {
	WLVIF_FLAG_INITIALIZED,
	WLVIF_FLAG_STA_ASSOCIATED,
	WLVIF_FLAG_STA_AUTHORIZED,
	WLVIF_FLAG_IBSS_JOINED,
	WLVIF_FLAG_AP_STARTED,
	WLVIF_FLAG_IN_PS,
	WLVIF_FLAG_STA_STATE_SENT,
	WLVIF_FLAG_RX_STREAMING_STARTED,
	WLVIF_FLAG_PSPOLL_FAILURE,
	WLVIF_FLAG_CS_PROGRESS,
	WLVIF_FLAG_AP_PROBE_RESP_SET,
	WLVIF_FLAG_IN_USE,
};

struct wl1271_link {
	
	struct sk_buff_head tx_queue[NUM_TX_QUEUES];

	
	u8 allocated_pkts;
	u8 prev_freed_pkts;

	u8 addr[ETH_ALEN];

	
	u8 ba_bitmap;
};

struct wl1271 {
	struct ieee80211_hw *hw;
	bool mac80211_registered;

	struct device *dev;

	void *if_priv;

	struct wl1271_if_operations *if_ops;

	void (*set_power)(bool enable);
	int irq;
	int ref_clock;

	spinlock_t wl_lock;

	enum wl1271_state state;
	enum wl12xx_fw_type fw_type;
	bool plt;
	u8 last_vif_count;
	struct mutex mutex;

	unsigned long flags;

	struct wl1271_partition_set part;

	struct wl1271_chip chip;

	int cmd_box_addr;
	int event_box_addr;

	u8 *fw;
	size_t fw_len;
	void *nvs;
	size_t nvs_len;

	s8 hw_pg_ver;

	
	u32 fuse_oui_addr;
	u32 fuse_nic_addr;

	
	struct mac_address addresses[2];
	int channel;
	u8 system_hlid;

	unsigned long links_map[BITS_TO_LONGS(WL12XX_MAX_LINKS)];
	unsigned long roles_map[BITS_TO_LONGS(WL12XX_MAX_ROLES)];
	unsigned long roc_map[BITS_TO_LONGS(WL12XX_MAX_ROLES)];
	unsigned long rate_policies_map[
			BITS_TO_LONGS(WL12XX_MAX_RATE_POLICIES)];

	struct list_head wlvif_list;

	u8 sta_count;
	u8 ap_count;

	struct wl1271_acx_mem_map *target_mem_map;

	
	u32 tx_blocks_freed;
	u32 tx_blocks_available;
	u32 tx_allocated_blocks;
	u32 tx_results_count;

	
	u32 tx_spare_blocks;

	
	u32 tx_pkts_freed[NUM_TX_QUEUES];
	u32 tx_allocated_pkts[NUM_TX_QUEUES];

	
	u32 tx_packets_count;

	
	s64 time_offset;

	
	int tx_queue_count[NUM_TX_QUEUES];
	long stopped_queues_map;

	
	struct sk_buff_head deferred_rx_queue;

	
	struct sk_buff_head deferred_tx_queue;

	struct work_struct tx_work;
	struct workqueue_struct *freezable_wq;

	
	unsigned long tx_frames_map[BITS_TO_LONGS(ACX_TX_DESCRIPTORS)];
	struct sk_buff *tx_frames[ACX_TX_DESCRIPTORS];
	int tx_frames_cnt;

	
	u32 rx_counter;

	
	struct wl1271_rx_mem_pool_addr rx_mem_pool_addr;

	
	u8 *aggr_buf;

	
	struct sk_buff *dummy_packet;

	
	struct work_struct netstack_work;

	
	u8 *fwlog;

	
	ssize_t fwlog_size;

	
	wait_queue_head_t fwlog_waitq;

	
	struct work_struct recovery_work;

	
	u32 event_mask;

	
	u32 mbox_ptr[2];

	
	struct ieee80211_vif *scan_vif;
	struct wl1271_scan scan;
	struct delayed_work scan_complete_work;

	bool sched_scanning;

	
	enum ieee80211_band band;

	struct completion *elp_compl;
	struct delayed_work elp_work;

	
	int power_level;

	struct wl1271_stats stats;

	__le32 buffer_32;
	u32 buffer_cmd;
	u32 buffer_busyword[WL1271_BUSY_WORD_CNT];

	struct wl12xx_fw_status *fw_status;
	struct wl1271_tx_hw_res_if *tx_res_if;

	
	struct conf_drv_settings conf;

	bool sg_enabled;

	bool enable_11a;

	
	s8 noise;

	
	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];

	int tcxo_clock;

	bool wow_enabled;
	bool irq_wake_enabled;

	struct wl1271_link links[WL12XX_MAX_LINKS];

	
	u32 ap_fw_ps_map;

	
	unsigned long ap_ps_map;

	
	unsigned int quirks;

	
	unsigned int platform_quirks;

	
	int ba_rx_session_count;

	
	int active_sta_count;

	
	struct wl12xx_vif *last_wlvif;

	
	struct delayed_work tx_watchdog_work;
};

struct wl1271_station {
	u8 hlid;
};

struct wl12xx_vif {
	struct wl1271 *wl;
	struct list_head list;
	unsigned long flags;
	u8 bss_type;
	u8 p2p; 
	u8 role_id;

	
	u8 dev_role_id;
	u8 dev_hlid;

	union {
		struct {
			u8 hlid;
			u8 ba_rx_bitmap;

			u8 basic_rate_idx;
			u8 ap_rate_idx;
			u8 p2p_rate_idx;

			bool qos;
		} sta;
		struct {
			u8 global_hlid;
			u8 bcast_hlid;

			
			unsigned long sta_hlid_map[BITS_TO_LONGS(
							WL12XX_MAX_LINKS)];

			
			struct wl1271_ap_key *recorded_keys[MAX_NUM_KEYS];

			u8 mgmt_rate_idx;
			u8 bcast_rate_idx;
			u8 ucast_rate_idx[CONF_TX_MAX_AC_COUNT];
		} ap;
	};

	
	int last_tx_hlid;

	unsigned long links_map[BITS_TO_LONGS(WL12XX_MAX_LINKS)];

	u8 ssid[IEEE80211_MAX_SSID_LEN + 1];
	u8 ssid_len;

	
	enum ieee80211_band band;
	int channel;

	u32 bitrate_masks[IEEE80211_NUM_BANDS];
	u32 basic_rate_set;

	u32 basic_rate;
	u32 rate_set;

	
	struct sk_buff *probereq;

	
	u32 beacon_int;

	
	u32 default_key;

	
	u16 aid;

	
	int session_counter;

	
	u8 psm_entry_retry;

	
	int power_level;

	int rssi_thold;
	int last_rssi_event;

	
	u8 encryption_type;
	__be32 ip_addr;

	
	bool ba_support;
	bool ba_allowed;

	
	struct work_struct rx_streaming_enable_work;
	struct work_struct rx_streaming_disable_work;
	struct timer_list rx_streaming_timer;

	struct {
		u8 persistent[0];
		u64 tx_security_seq;

		
		u8 tx_security_last_seq_lsb;
	};
};

static inline struct wl12xx_vif *wl12xx_vif_to_data(struct ieee80211_vif *vif)
{
	return (struct wl12xx_vif *)vif->drv_priv;
}

static inline
struct ieee80211_vif *wl12xx_wlvif_to_vif(struct wl12xx_vif *wlvif)
{
	return container_of((void *)wlvif, struct ieee80211_vif, drv_priv);
}

#define wl12xx_for_each_wlvif(wl, wlvif) \
		list_for_each_entry(wlvif, &wl->wlvif_list, list)

#define wl12xx_for_each_wlvif_continue(wl, wlvif) \
		list_for_each_entry_continue(wlvif, &wl->wlvif_list, list)

#define wl12xx_for_each_wlvif_bss_type(wl, wlvif, _bss_type)	\
		wl12xx_for_each_wlvif(wl, wlvif)		\
			if (wlvif->bss_type == _bss_type)

#define wl12xx_for_each_wlvif_sta(wl, wlvif)	\
		wl12xx_for_each_wlvif_bss_type(wl, wlvif, BSS_TYPE_STA_BSS)

#define wl12xx_for_each_wlvif_ap(wl, wlvif)	\
		wl12xx_for_each_wlvif_bss_type(wl, wlvif, BSS_TYPE_AP_BSS)

int wl1271_plt_start(struct wl1271 *wl);
int wl1271_plt_stop(struct wl1271 *wl);
int wl1271_recalc_rx_streaming(struct wl1271 *wl, struct wl12xx_vif *wlvif);
void wl12xx_queue_recovery_work(struct wl1271 *wl);
size_t wl12xx_copy_fwlog(struct wl1271 *wl, u8 *memblock, size_t maxlen);

#define JOIN_TIMEOUT 5000 

#define SESSION_COUNTER_MAX 6 
#define SESSION_COUNTER_INVALID 7 

#define WL1271_DEFAULT_POWER_LEVEL 0

#define WL1271_TX_QUEUE_LOW_WATERMARK  32
#define WL1271_TX_QUEUE_HIGH_WATERMARK 256

#define WL1271_DEFERRED_QUEUE_LIMIT    64

#define WL1271_PRE_POWER_ON_SLEEP 20 
#define WL1271_POWER_ON_SLEEP 200 

#define HW_BG_RATES_MASK	0xffff
#define HW_HT_RATES_OFFSET	16


#define WL12XX_QUIRK_END_OF_TRANSACTION		BIT(0)

#define WL12XX_QUIRK_NO_BLOCKSIZE_ALIGNMENT	BIT(2)

#define WL12XX_QUIRK_FWLOG_NOT_IMPLEMENTED	BIT(4)

#define WL12XX_HW_BLOCK_SIZE	256

#endif
