/*
 *
 * Copyright (c) 2011, Microsoft Corporation.
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
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */

#ifndef _HYPERV_VMBUS_H
#define _HYPERV_VMBUS_H

#include <linux/list.h>
#include <asm/sync_bitops.h>
#include <linux/atomic.h>
#include <linux/hyperv.h>

enum hv_cpuid_function {
	HVCPUID_VERSION_FEATURES		= 0x00000001,
	HVCPUID_VENDOR_MAXFUNCTION		= 0x40000000,
	HVCPUID_INTERFACE			= 0x40000001,

	HVCPUID_VERSION			= 0x40000002,
	HVCPUID_FEATURES			= 0x40000003,
	HVCPUID_ENLIGHTENMENT_INFO	= 0x40000004,
	HVCPUID_IMPLEMENTATION_LIMITS		= 0x40000005,
};

#define HV_SYNIC_VERSION		(1)

#define HV_SYNIC_VERSION_1		(0x1)

#define HV_MESSAGE_SIZE			(256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT	(240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT	(30)
#define HV_ANY_VP			(0xFFFFFFFF)

#define HV_EVENT_FLAGS_COUNT		(256 * 8)
#define HV_EVENT_FLAGS_BYTE_COUNT	(256)
#define HV_EVENT_FLAGS_DWORD_COUNT	(256 / sizeof(u32))

enum hv_message_type {
	HVMSG_NONE			= 0x00000000,

	
	HVMSG_UNMAPPED_GPA		= 0x80000000,
	HVMSG_GPA_INTERCEPT		= 0x80000001,

	
	HVMSG_TIMER_EXPIRED			= 0x80000010,

	
	HVMSG_INVALID_VP_REGISTER_VALUE	= 0x80000020,
	HVMSG_UNRECOVERABLE_EXCEPTION	= 0x80000021,
	HVMSG_UNSUPPORTED_FEATURE		= 0x80000022,

	
	HVMSG_EVENTLOG_BUFFERCOMPLETE	= 0x80000040,

	
	HVMSG_X64_IOPORT_INTERCEPT		= 0x80010000,
	HVMSG_X64_MSR_INTERCEPT		= 0x80010001,
	HVMSG_X64_CPUID_INTERCEPT		= 0x80010002,
	HVMSG_X64_EXCEPTION_INTERCEPT	= 0x80010003,
	HVMSG_X64_APIC_EOI			= 0x80010004,
	HVMSG_X64_LEGACY_FP_ERROR		= 0x80010005
};

#define HV_SYNIC_SINT_COUNT		(16)
#define HV_SYNIC_STIMER_COUNT		(4)

#define HV_PARTITION_ID_INVALID		((u64)0x0)

union hv_connection_id {
	u32 asu32;
	struct {
		u32 id:24;
		u32 reserved:8;
	} u;
};

union hv_port_id {
	u32 asu32;
	struct {
		u32 id:24;
		u32 reserved:8;
	} u ;
};

enum hv_port_type {
	HVPORT_MSG	= 1,
	HVPORT_EVENT		= 2,
	HVPORT_MONITOR	= 3
};

struct hv_port_info {
	enum hv_port_type port_type;
	u32 padding;
	union {
		struct {
			u32 target_sint;
			u32 target_vp;
			u64 rsvdz;
		} message_port_info;
		struct {
			u32 target_sint;
			u32 target_vp;
			u16 base_flag_bumber;
			u16 flag_count;
			u32 rsvdz;
		} event_port_info;
		struct {
			u64 monitor_address;
			u64 rsvdz;
		} monitor_port_info;
	};
};

struct hv_connection_info {
	enum hv_port_type port_type;
	u32 padding;
	union {
		struct {
			u64 rsvdz;
		} message_connection_info;
		struct {
			u64 rsvdz;
		} event_connection_info;
		struct {
			u64 monitor_address;
		} monitor_connection_info;
	};
};

union hv_message_flags {
	u8 asu8;
	struct {
		u8 msg_pending:1;
		u8 reserved:7;
	};
};

struct hv_message_header {
	enum hv_message_type message_type;
	u8 payload_size;
	union hv_message_flags message_flags;
	u8 reserved[2];
	union {
		u64 sender;
		union hv_port_id port;
	};
};

struct hv_timer_message_payload {
	u32 timer_index;
	u32 reserved;
	u64 expiration_time;	
	u64 delivery_time;	
};

struct hv_message {
	struct hv_message_header header;
	union {
		u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
	} u ;
};

#define HV_PORT_MESSAGE_BUFFER_COUNT	(16)

struct hv_message_page {
	struct hv_message sint_message[HV_SYNIC_SINT_COUNT];
};

union hv_synic_event_flags {
	u8 flags8[HV_EVENT_FLAGS_BYTE_COUNT];
	u32 flags32[HV_EVENT_FLAGS_DWORD_COUNT];
};

struct hv_synic_event_flags_page {
	union hv_synic_event_flags sintevent_flags[HV_SYNIC_SINT_COUNT];
};

union hv_synic_scontrol {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:63;
	};
};

union hv_synic_sint {
	u64 as_uint64;
	struct {
		u64 vector:8;
		u64 reserved1:8;
		u64 masked:1;
		u64 auto_eoi:1;
		u64 reserved2:46;
	};
};

union hv_synic_simp {
	u64 as_uint64;
	struct {
		u64 simp_enabled:1;
		u64 preserved:11;
		u64 base_simp_gpa:52;
	};
};

union hv_synic_siefp {
	u64 as_uint64;
	struct {
		u64 siefp_enabled:1;
		u64 preserved:11;
		u64 base_siefp_gpa:52;
	};
};

union hv_monitor_trigger_group {
	u64 as_uint64;
	struct {
		u32 pending;
		u32 armed;
	};
};

struct hv_monitor_parameter {
	union hv_connection_id connectionid;
	u16 flagnumber;
	u16 rsvdz;
};

union hv_monitor_trigger_state {
	u32 asu32;

	struct {
		u32 group_enable:4;
		u32 rsvdz:28;
	};
};

struct hv_monitor_page {
	union hv_monitor_trigger_state trigger_state;
	u32 rsvdz1;

	union hv_monitor_trigger_group trigger_group[4];
	u64 rsvdz2[3];

	s32 next_checktime[4][32];

	u16 latency[4][32];
	u64 rsvdz3[32];

	struct hv_monitor_parameter parameter[4][32];

	u8 rsvdz4[1984];
};

enum hv_call_code {
	HVCALL_POST_MESSAGE	= 0x005c,
	HVCALL_SIGNAL_EVENT	= 0x005d,
};

struct hv_input_post_message {
	union hv_connection_id connectionid;
	u32 reserved;
	enum hv_message_type message_type;
	u32 payload_size;
	u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
};

struct hv_input_signal_event {
	union hv_connection_id connectionid;
	u16 flag_number;
	u16 rsvdz;
};


enum hv_guest_os_vendor {
	HVGUESTOS_VENDOR_MICROSOFT	= 0x0001
};

enum hv_guest_os_microsoft_ids {
	HVGUESTOS_MICROSOFT_UNDEFINED	= 0x00,
	HVGUESTOS_MICROSOFT_MSDOS		= 0x01,
	HVGUESTOS_MICROSOFT_WINDOWS3X	= 0x02,
	HVGUESTOS_MICROSOFT_WINDOWS9X	= 0x03,
	HVGUESTOS_MICROSOFT_WINDOWSNT	= 0x04,
	HVGUESTOS_MICROSOFT_WINDOWSCE	= 0x05
};

#define HV_X64_MSR_GUEST_OS_ID	0x40000000

union hv_x64_msr_guest_os_id_contents {
	u64 as_uint64;
	struct {
		u64 build_number:16;
		u64 service_version:8; 
		u64 minor_version:8;
		u64 major_version:8;
		u64 os_id:8; 
		u64 vendor_id:16; 
	};
};

#define HV_X64_MSR_HYPERCALL	0x40000001

union hv_x64_msr_hypercall_contents {
	u64 as_uint64;
	struct {
		u64 enable:1;
		u64 reserved:11;
		u64 guest_physical_address:52;
	};
};


enum {
	VMBUS_MESSAGE_CONNECTION_ID	= 1,
	VMBUS_MESSAGE_PORT_ID		= 1,
	VMBUS_EVENT_CONNECTION_ID	= 2,
	VMBUS_EVENT_PORT_ID		= 2,
	VMBUS_MONITOR_CONNECTION_ID	= 3,
	VMBUS_MONITOR_PORT_ID		= 3,
	VMBUS_MESSAGE_SINT		= 2,
};


#define HV_PRESENT_BIT			0x80000000

#define HV_LINUX_GUEST_ID_LO		0x00000000
#define HV_LINUX_GUEST_ID_HI		0xB16B00B5
#define HV_LINUX_GUEST_ID		(((u64)HV_LINUX_GUEST_ID_HI << 32) | \
					   HV_LINUX_GUEST_ID_LO)

#define HV_CPU_POWER_MANAGEMENT		(1 << 0)
#define HV_RECOMMENDATIONS_MAX		4

#define HV_X64_MAX			5
#define HV_CAPS_MAX			8


#define HV_HYPERCALL_PARAM_ALIGN	sizeof(u64)



#define HV_SERVICE_PARENT_PORT				(0)
#define HV_SERVICE_PARENT_CONNECTION			(0)

#define HV_SERVICE_CONNECT_RESPONSE_SUCCESS		(0)
#define HV_SERVICE_CONNECT_RESPONSE_INVALID_PARAMETER	(1)
#define HV_SERVICE_CONNECT_RESPONSE_UNKNOWN_SERVICE	(2)
#define HV_SERVICE_CONNECT_RESPONSE_CONNECTION_REJECTED	(3)

#define HV_SERVICE_CONNECT_REQUEST_MESSAGE_ID		(1)
#define HV_SERVICE_CONNECT_RESPONSE_MESSAGE_ID		(2)
#define HV_SERVICE_DISCONNECT_REQUEST_MESSAGE_ID	(3)
#define HV_SERVICE_DISCONNECT_RESPONSE_MESSAGE_ID	(4)
#define HV_SERVICE_MAX_MESSAGE_ID				(4)

#define HV_SERVICE_PROTOCOL_VERSION (0x0010)
#define HV_CONNECT_PAYLOAD_BYTE_COUNT 64



static const uuid_le VMBUS_SERVICE_ID = {
	.b = {
		0xb8, 0x80, 0x81, 0x62, 0x8d, 0x30, 0x5e, 0x4c,
		0xb7, 0xdb, 0x1b, 0xeb, 0x62, 0xe6, 0x2e, 0xf4
	},
};



struct hv_input_signal_event_buffer {
	u64 align8;
	struct hv_input_signal_event event;
};

struct hv_context {
	u64 guestid;

	void *hypercall_page;

	bool synic_initialized;

	struct hv_input_signal_event_buffer *signal_event_buffer;
	
	struct hv_input_signal_event *signal_event_param;

	void *synic_message_page[NR_CPUS];
	void *synic_event_page[NR_CPUS];
};

extern struct hv_context hv_context;



extern int hv_init(void);

extern void hv_cleanup(void);

extern u16 hv_post_message(union hv_connection_id connection_id,
			 enum hv_message_type message_type,
			 void *payload, size_t payload_size);

extern u16 hv_signal_event(void);

extern void hv_synic_init(void *irqarg);

extern void hv_synic_cleanup(void *arg);




int hv_ringbuffer_init(struct hv_ring_buffer_info *ring_info, void *buffer,
		   u32 buflen);

void hv_ringbuffer_cleanup(struct hv_ring_buffer_info *ring_info);

int hv_ringbuffer_write(struct hv_ring_buffer_info *ring_info,
		    struct scatterlist *sglist,
		    u32 sgcount);

int hv_ringbuffer_peek(struct hv_ring_buffer_info *ring_info, void *buffer,
		   u32 buflen);

int hv_ringbuffer_read(struct hv_ring_buffer_info *ring_info,
		   void *buffer,
		   u32 buflen,
		   u32 offset);

u32 hv_get_ringbuffer_interrupt_mask(struct hv_ring_buffer_info *ring_info);

void hv_ringbuffer_get_debuginfo(struct hv_ring_buffer_info *ring_info,
			    struct hv_ring_buffer_debug_info *debug_info);

#define MAX_NUM_CHANNELS	((PAGE_SIZE >> 1) << 3)	

#define MAX_NUM_CHANNELS_SUPPORTED	256


enum vmbus_connect_state {
	DISCONNECTED,
	CONNECTING,
	CONNECTED,
	DISCONNECTING
};

#define MAX_SIZE_CHANNEL_MESSAGE	HV_MESSAGE_PAYLOAD_BYTE_COUNT

struct vmbus_connection {
	enum vmbus_connect_state conn_state;

	atomic_t next_gpadl_handle;

	void *int_page;
	void *send_int_page;
	void *recv_int_page;

	void *monitor_pages;
	struct list_head chn_msg_list;
	spinlock_t channelmsg_lock;

	
	struct list_head chn_list;
	spinlock_t channel_lock;

	struct workqueue_struct *work_queue;
};


struct vmbus_msginfo {
	
	struct list_head msglist_entry;

	
	unsigned char msg[0];
};


extern struct vmbus_connection vmbus_connection;


struct hv_device *vmbus_device_create(uuid_le *type,
					 uuid_le *instance,
					 struct vmbus_channel *channel);

int vmbus_device_register(struct hv_device *child_device_obj);
void vmbus_device_unregister(struct hv_device *device_obj);


struct vmbus_channel *relid2channel(u32 relid);

void vmbus_free_channels(void);


int vmbus_connect(void);

int vmbus_post_msg(void *buffer, size_t buflen);

int vmbus_set_event(u32 child_relid);

void vmbus_on_event(unsigned long data);


#endif 
