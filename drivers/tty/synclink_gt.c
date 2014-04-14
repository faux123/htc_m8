/*
 * Device driver for Microgate SyncLink GT serial adapters.
 *
 * written by Paul Fulghum for Microgate Corporation
 * paulkf@microgate.com
 *
 * Microgate and SyncLink are trademarks of Microgate Corporation
 *
 * This code is released under the GNU General Public License (GPL)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#define DBGINFO(fmt) if (debug_level >= DEBUG_LEVEL_INFO) printk fmt
#define DBGERR(fmt) if (debug_level >= DEBUG_LEVEL_ERROR) printk fmt
#define DBGBH(fmt) if (debug_level >= DEBUG_LEVEL_BH) printk fmt
#define DBGISR(fmt) if (debug_level >= DEBUG_LEVEL_ISR) printk fmt
#define DBGDATA(info, buf, size, label) if (debug_level >= DEBUG_LEVEL_DATA) trace_block((info), (buf), (size), (label))


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioctl.h>
#include <linux/termios.h>
#include <linux/bitops.h>
#include <linux/workqueue.h>
#include <linux/hdlc.h>
#include <linux/synclink.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/types.h>
#include <asm/uaccess.h>

#if defined(CONFIG_HDLC) || (defined(CONFIG_HDLC_MODULE) && defined(CONFIG_SYNCLINK_GT_MODULE))
#define SYNCLINK_GENERIC_HDLC 1
#else
#define SYNCLINK_GENERIC_HDLC 0
#endif

static char *driver_name     = "SyncLink GT";
static char *tty_driver_name = "synclink_gt";
static char *tty_dev_prefix  = "ttySLG";
MODULE_LICENSE("GPL");
#define MGSL_MAGIC 0x5401
#define MAX_DEVICES 32

static struct pci_device_id pci_table[] = {
	{PCI_VENDOR_ID_MICROGATE, SYNCLINK_GT_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_MICROGATE, SYNCLINK_GT2_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_MICROGATE, SYNCLINK_GT4_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_MICROGATE, SYNCLINK_AC_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{0,}, 
};
MODULE_DEVICE_TABLE(pci, pci_table);

static int  init_one(struct pci_dev *dev,const struct pci_device_id *ent);
static void remove_one(struct pci_dev *dev);
static struct pci_driver pci_driver = {
	.name		= "synclink_gt",
	.id_table	= pci_table,
	.probe		= init_one,
	.remove		= __devexit_p(remove_one),
};

static bool pci_registered;

static struct slgt_info *slgt_device_list;
static int slgt_device_count;

static int ttymajor;
static int debug_level;
static int maxframe[MAX_DEVICES];

module_param(ttymajor, int, 0);
module_param(debug_level, int, 0);
module_param_array(maxframe, int, NULL, 0);

MODULE_PARM_DESC(ttymajor, "TTY major device number override: 0=auto assigned");
MODULE_PARM_DESC(debug_level, "Debug syslog output: 0=disabled, 1 to 5=increasing detail");
MODULE_PARM_DESC(maxframe, "Maximum frame size used by device (4096 to 65535)");

static struct tty_driver *serial_driver;

static int  open(struct tty_struct *tty, struct file * filp);
static void close(struct tty_struct *tty, struct file * filp);
static void hangup(struct tty_struct *tty);
static void set_termios(struct tty_struct *tty, struct ktermios *old_termios);

static int  write(struct tty_struct *tty, const unsigned char *buf, int count);
static int put_char(struct tty_struct *tty, unsigned char ch);
static void send_xchar(struct tty_struct *tty, char ch);
static void wait_until_sent(struct tty_struct *tty, int timeout);
static int  write_room(struct tty_struct *tty);
static void flush_chars(struct tty_struct *tty);
static void flush_buffer(struct tty_struct *tty);
static void tx_hold(struct tty_struct *tty);
static void tx_release(struct tty_struct *tty);

static int  ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
static int  chars_in_buffer(struct tty_struct *tty);
static void throttle(struct tty_struct * tty);
static void unthrottle(struct tty_struct * tty);
static int set_break(struct tty_struct *tty, int break_state);

#if SYNCLINK_GENERIC_HDLC
#define dev_to_port(D) (dev_to_hdlc(D)->priv)
static void hdlcdev_tx_done(struct slgt_info *info);
static void hdlcdev_rx(struct slgt_info *info, char *buf, int size);
static int  hdlcdev_init(struct slgt_info *info);
static void hdlcdev_exit(struct slgt_info *info);
#endif



#define SLGT_MAX_PORTS 4
#define SLGT_REG_SIZE  256

struct cond_wait {
	struct cond_wait *next;
	wait_queue_head_t q;
	wait_queue_t wait;
	unsigned int data;
};
static void init_cond_wait(struct cond_wait *w, unsigned int data);
static void add_cond_wait(struct cond_wait **head, struct cond_wait *w);
static void remove_cond_wait(struct cond_wait **head, struct cond_wait *w);
static void flush_cond_wait(struct cond_wait **head);

struct slgt_desc
{
	__le16 count;
	__le16 status;
	__le32 pbuf;  
	__le32 next;  

	
	char *buf;          
    	unsigned int pdesc; 
	dma_addr_t buf_dma_addr;
	unsigned short buf_count;
};

#define set_desc_buffer(a,b) (a).pbuf = cpu_to_le32((unsigned int)(b))
#define set_desc_next(a,b) (a).next   = cpu_to_le32((unsigned int)(b))
#define set_desc_count(a,b)(a).count  = cpu_to_le16((unsigned short)(b))
#define set_desc_eof(a,b)  (a).status = cpu_to_le16((b) ? (le16_to_cpu((a).status) | BIT0) : (le16_to_cpu((a).status) & ~BIT0))
#define set_desc_status(a, b) (a).status = cpu_to_le16((unsigned short)(b))
#define desc_count(a)      (le16_to_cpu((a).count))
#define desc_status(a)     (le16_to_cpu((a).status))
#define desc_complete(a)   (le16_to_cpu((a).status) & BIT15)
#define desc_eof(a)        (le16_to_cpu((a).status) & BIT2)
#define desc_crc_error(a)  (le16_to_cpu((a).status) & BIT1)
#define desc_abort(a)      (le16_to_cpu((a).status) & BIT0)
#define desc_residue(a)    ((le16_to_cpu((a).status) & 0x38) >> 3)

struct _input_signal_events {
	int ri_up;
	int ri_down;
	int dsr_up;
	int dsr_down;
	int dcd_up;
	int dcd_down;
	int cts_up;
	int cts_down;
};

struct slgt_info {
	void *if_ptr;		
	struct tty_port port;

	struct slgt_info *next_device;	

	int magic;

	char device_name[25];
	struct pci_dev *pdev;

	int port_count;  
	int adapter_num; 
	int port_num;    

	
	struct slgt_info *port_array[SLGT_MAX_PORTS];

	int			line;		

	struct mgsl_icount	icount;

	int			timeout;
	int			x_char;		
	unsigned int		read_status_mask;
	unsigned int 		ignore_status_mask;

	wait_queue_head_t	status_event_wait_q;
	wait_queue_head_t	event_wait_q;
	struct timer_list	tx_timer;
	struct timer_list	rx_timer;

	unsigned int            gpio_present;
	struct cond_wait        *gpio_wait_q;

	spinlock_t lock;	

	struct work_struct task;
	u32 pending_bh;
	bool bh_requested;
	bool bh_running;

	int isr_overflow;
	bool irq_requested;	
	bool irq_occurred;	

	

	unsigned int bus_type;
	unsigned int irq_level;
	unsigned long irq_flags;

	unsigned char __iomem * reg_addr;  
	u32 phys_reg_addr;
	bool reg_addr_requested;

	MGSL_PARAMS params;       
	u32 idle_mode;
	u32 max_frame_size;       

	unsigned int rbuf_fill_level;
	unsigned int rx_pio;
	unsigned int if_mode;
	unsigned int base_clock;
	unsigned int xsync;
	unsigned int xctrl;

	

	bool rx_enabled;
	bool rx_restart;

	bool tx_enabled;
	bool tx_active;

	unsigned char signals;    
	int init_error;  

	unsigned char *tx_buf;
	int tx_count;

	char flag_buf[MAX_ASYNC_BUFFER_SIZE];
	char char_buf[MAX_ASYNC_BUFFER_SIZE];
	bool drop_rts_on_tx_done;
	struct	_input_signal_events	input_signal_events;

	int dcd_chkcount;	
	int cts_chkcount;	
	int dsr_chkcount;	
	int ri_chkcount;

	char *bufs;		
	dma_addr_t bufs_dma_addr; 

	unsigned int rbuf_count;
	struct slgt_desc *rbufs;
	unsigned int rbuf_current;
	unsigned int rbuf_index;
	unsigned int rbuf_fill_index;
	unsigned short rbuf_fill_count;

	unsigned int tbuf_count;
	struct slgt_desc *tbufs;
	unsigned int tbuf_current;
	unsigned int tbuf_start;

	unsigned char *tmp_rbuf;
	unsigned int tmp_rbuf_count;

	

	int netcount;
	spinlock_t netlock;
#if SYNCLINK_GENERIC_HDLC
	struct net_device *netdev;
#endif

};

static MGSL_PARAMS default_params = {
	.mode            = MGSL_MODE_HDLC,
	.loopback        = 0,
	.flags           = HDLC_FLAG_UNDERRUN_ABORT15,
	.encoding        = HDLC_ENCODING_NRZI_SPACE,
	.clock_speed     = 0,
	.addr_filter     = 0xff,
	.crc_type        = HDLC_CRC_16_CCITT,
	.preamble_length = HDLC_PREAMBLE_LENGTH_8BITS,
	.preamble        = HDLC_PREAMBLE_PATTERN_NONE,
	.data_rate       = 9600,
	.data_bits       = 8,
	.stop_bits       = 1,
	.parity          = ASYNC_PARITY_NONE
};


#define BH_RECEIVE  1
#define BH_TRANSMIT 2
#define BH_STATUS   4
#define IO_PIN_SHUTDOWN_LIMIT 100

#define DMABUFSIZE 256
#define DESC_LIST_SIZE 4096

#define MASK_PARITY  BIT1
#define MASK_FRAMING BIT0
#define MASK_BREAK   BIT14
#define MASK_OVERRUN BIT4

#define GSR   0x00 
#define JCR   0x04 
#define IODR  0x08 
#define IOER  0x0c 
#define IOVR  0x10 
#define IOSR  0x14 
#define TDR   0x80 
#define RDR   0x80 
#define TCR   0x82 
#define TIR   0x84 
#define TPR   0x85 
#define RCR   0x86 
#define VCR   0x88 
#define CCR   0x89 
#define BDR   0x8a 
#define SCR   0x8c 
#define SSR   0x8e 
#define RDCSR 0x90 
#define TDCSR 0x94 
#define RDDAR 0x98 
#define TDDAR 0x9c 
#define XSR   0x40 
#define XCR   0x44 

#define RXIDLE      BIT14
#define RXBREAK     BIT14
#define IRQ_TXDATA  BIT13
#define IRQ_TXIDLE  BIT12
#define IRQ_TXUNDER BIT11 
#define IRQ_RXDATA  BIT10
#define IRQ_RXIDLE  BIT9  
#define IRQ_RXBREAK BIT9  
#define IRQ_RXOVER  BIT8
#define IRQ_DSR     BIT7
#define IRQ_CTS     BIT6
#define IRQ_DCD     BIT5
#define IRQ_RI      BIT4
#define IRQ_ALL     0x3ff0
#define IRQ_MASTER  BIT0

#define slgt_irq_on(info, mask) \
	wr_reg16((info), SCR, (unsigned short)(rd_reg16((info), SCR) | (mask)))
#define slgt_irq_off(info, mask) \
	wr_reg16((info), SCR, (unsigned short)(rd_reg16((info), SCR) & ~(mask)))

static __u8  rd_reg8(struct slgt_info *info, unsigned int addr);
static void  wr_reg8(struct slgt_info *info, unsigned int addr, __u8 value);
static __u16 rd_reg16(struct slgt_info *info, unsigned int addr);
static void  wr_reg16(struct slgt_info *info, unsigned int addr, __u16 value);
static __u32 rd_reg32(struct slgt_info *info, unsigned int addr);
static void  wr_reg32(struct slgt_info *info, unsigned int addr, __u32 value);

static void  msc_set_vcr(struct slgt_info *info);

static int  startup(struct slgt_info *info);
static int  block_til_ready(struct tty_struct *tty, struct file * filp,struct slgt_info *info);
static void shutdown(struct slgt_info *info);
static void program_hw(struct slgt_info *info);
static void change_params(struct slgt_info *info);

static int  register_test(struct slgt_info *info);
static int  irq_test(struct slgt_info *info);
static int  loopback_test(struct slgt_info *info);
static int  adapter_test(struct slgt_info *info);

static void reset_adapter(struct slgt_info *info);
static void reset_port(struct slgt_info *info);
static void async_mode(struct slgt_info *info);
static void sync_mode(struct slgt_info *info);

static void rx_stop(struct slgt_info *info);
static void rx_start(struct slgt_info *info);
static void reset_rbufs(struct slgt_info *info);
static void free_rbufs(struct slgt_info *info, unsigned int first, unsigned int last);
static void rdma_reset(struct slgt_info *info);
static bool rx_get_frame(struct slgt_info *info);
static bool rx_get_buf(struct slgt_info *info);

static void tx_start(struct slgt_info *info);
static void tx_stop(struct slgt_info *info);
static void tx_set_idle(struct slgt_info *info);
static unsigned int free_tbuf_count(struct slgt_info *info);
static unsigned int tbuf_bytes(struct slgt_info *info);
static void reset_tbufs(struct slgt_info *info);
static void tdma_reset(struct slgt_info *info);
static bool tx_load(struct slgt_info *info, const char *buf, unsigned int count);

static void get_signals(struct slgt_info *info);
static void set_signals(struct slgt_info *info);
static void enable_loopback(struct slgt_info *info);
static void set_rate(struct slgt_info *info, u32 data_rate);

static int  bh_action(struct slgt_info *info);
static void bh_handler(struct work_struct *work);
static void bh_transmit(struct slgt_info *info);
static void isr_serial(struct slgt_info *info);
static void isr_rdma(struct slgt_info *info);
static void isr_txeom(struct slgt_info *info, unsigned short status);
static void isr_tdma(struct slgt_info *info);

static int  alloc_dma_bufs(struct slgt_info *info);
static void free_dma_bufs(struct slgt_info *info);
static int  alloc_desc(struct slgt_info *info);
static void free_desc(struct slgt_info *info);
static int  alloc_bufs(struct slgt_info *info, struct slgt_desc *bufs, int count);
static void free_bufs(struct slgt_info *info, struct slgt_desc *bufs, int count);

static int  alloc_tmp_rbuf(struct slgt_info *info);
static void free_tmp_rbuf(struct slgt_info *info);

static void tx_timeout(unsigned long context);
static void rx_timeout(unsigned long context);

static int  get_stats(struct slgt_info *info, struct mgsl_icount __user *user_icount);
static int  get_params(struct slgt_info *info, MGSL_PARAMS __user *params);
static int  set_params(struct slgt_info *info, MGSL_PARAMS __user *params);
static int  get_txidle(struct slgt_info *info, int __user *idle_mode);
static int  set_txidle(struct slgt_info *info, int idle_mode);
static int  tx_enable(struct slgt_info *info, int enable);
static int  tx_abort(struct slgt_info *info);
static int  rx_enable(struct slgt_info *info, int enable);
static int  modem_input_wait(struct slgt_info *info,int arg);
static int  wait_mgsl_event(struct slgt_info *info, int __user *mask_ptr);
static int  tiocmget(struct tty_struct *tty);
static int  tiocmset(struct tty_struct *tty,
				unsigned int set, unsigned int clear);
static int set_break(struct tty_struct *tty, int break_state);
static int  get_interface(struct slgt_info *info, int __user *if_mode);
static int  set_interface(struct slgt_info *info, int if_mode);
static int  set_gpio(struct slgt_info *info, struct gpio_desc __user *gpio);
static int  get_gpio(struct slgt_info *info, struct gpio_desc __user *gpio);
static int  wait_gpio(struct slgt_info *info, struct gpio_desc __user *gpio);
static int  get_xsync(struct slgt_info *info, int __user *if_mode);
static int  set_xsync(struct slgt_info *info, int if_mode);
static int  get_xctrl(struct slgt_info *info, int __user *if_mode);
static int  set_xctrl(struct slgt_info *info, int if_mode);

static void add_device(struct slgt_info *info);
static void device_init(int adapter_num, struct pci_dev *pdev);
static int  claim_resources(struct slgt_info *info);
static void release_resources(struct slgt_info *info);

#ifndef DBGINFO
#define DBGINFO(fmt)
#endif
#ifndef DBGERR
#define DBGERR(fmt)
#endif
#ifndef DBGBH
#define DBGBH(fmt)
#endif
#ifndef DBGISR
#define DBGISR(fmt)
#endif

#ifdef DBGDATA
static void trace_block(struct slgt_info *info, const char *data, int count, const char *label)
{
	int i;
	int linecount;
	printk("%s %s data:\n",info->device_name, label);
	while(count) {
		linecount = (count > 16) ? 16 : count;
		for(i=0; i < linecount; i++)
			printk("%02X ",(unsigned char)data[i]);
		for(;i<17;i++)
			printk("   ");
		for(i=0;i<linecount;i++) {
			if (data[i]>=040 && data[i]<=0176)
				printk("%c",data[i]);
			else
				printk(".");
		}
		printk("\n");
		data  += linecount;
		count -= linecount;
	}
}
#else
#define DBGDATA(info, buf, size, label)
#endif

#ifdef DBGTBUF
static void dump_tbufs(struct slgt_info *info)
{
	int i;
	printk("tbuf_current=%d\n", info->tbuf_current);
	for (i=0 ; i < info->tbuf_count ; i++) {
		printk("%d: count=%04X status=%04X\n",
			i, le16_to_cpu(info->tbufs[i].count), le16_to_cpu(info->tbufs[i].status));
	}
}
#else
#define DBGTBUF(info)
#endif

#ifdef DBGRBUF
static void dump_rbufs(struct slgt_info *info)
{
	int i;
	printk("rbuf_current=%d\n", info->rbuf_current);
	for (i=0 ; i < info->rbuf_count ; i++) {
		printk("%d: count=%04X status=%04X\n",
			i, le16_to_cpu(info->rbufs[i].count), le16_to_cpu(info->rbufs[i].status));
	}
}
#else
#define DBGRBUF(info)
#endif

static inline int sanity_check(struct slgt_info *info, char *devname, const char *name)
{
#ifdef SANITY_CHECK
	if (!info) {
		printk("null struct slgt_info for (%s) in %s\n", devname, name);
		return 1;
	}
	if (info->magic != MGSL_MAGIC) {
		printk("bad magic number struct slgt_info (%s) in %s\n", devname, name);
		return 1;
	}
#else
	if (!info)
		return 1;
#endif
	return 0;
}

static void ldisc_receive_buf(struct tty_struct *tty,
			      const __u8 *data, char *flags, int count)
{
	struct tty_ldisc *ld;
	if (!tty)
		return;
	ld = tty_ldisc_ref(tty);
	if (ld) {
		if (ld->ops->receive_buf)
			ld->ops->receive_buf(tty, data, flags, count);
		tty_ldisc_deref(ld);
	}
}


static int open(struct tty_struct *tty, struct file *filp)
{
	struct slgt_info *info;
	int retval, line;
	unsigned long flags;

	line = tty->index;
	if (line >= slgt_device_count) {
		DBGERR(("%s: open with invalid line #%d.\n", driver_name, line));
		return -ENODEV;
	}

	info = slgt_device_list;
	while(info && info->line != line)
		info = info->next_device;
	if (sanity_check(info, tty->name, "open"))
		return -ENODEV;
	if (info->init_error) {
		DBGERR(("%s init error=%d\n", info->device_name, info->init_error));
		return -ENODEV;
	}

	tty->driver_data = info;
	info->port.tty = tty;

	DBGINFO(("%s open, old ref count = %d\n", info->device_name, info->port.count));

	
	if (tty_hung_up_p(filp) || info->port.flags & ASYNC_CLOSING){
		if (info->port.flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->port.close_wait);
		retval = ((info->port.flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
		goto cleanup;
	}

	mutex_lock(&info->port.mutex);
	info->port.tty->low_latency = (info->port.flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	spin_lock_irqsave(&info->netlock, flags);
	if (info->netcount) {
		retval = -EBUSY;
		spin_unlock_irqrestore(&info->netlock, flags);
		mutex_unlock(&info->port.mutex);
		goto cleanup;
	}
	info->port.count++;
	spin_unlock_irqrestore(&info->netlock, flags);

	if (info->port.count == 1) {
		
		retval = startup(info);
		if (retval < 0) {
			mutex_unlock(&info->port.mutex);
			goto cleanup;
		}
	}
	mutex_unlock(&info->port.mutex);
	retval = block_til_ready(tty, filp, info);
	if (retval) {
		DBGINFO(("%s block_til_ready rc=%d\n", info->device_name, retval));
		goto cleanup;
	}

	retval = 0;

cleanup:
	if (retval) {
		if (tty->count == 1)
			info->port.tty = NULL; 
		if(info->port.count)
			info->port.count--;
	}

	DBGINFO(("%s open rc=%d\n", info->device_name, retval));
	return retval;
}

static void close(struct tty_struct *tty, struct file *filp)
{
	struct slgt_info *info = tty->driver_data;

	if (sanity_check(info, tty->name, "close"))
		return;
	DBGINFO(("%s close entry, count=%d\n", info->device_name, info->port.count));

	if (tty_port_close_start(&info->port, tty, filp) == 0)
		goto cleanup;

	mutex_lock(&info->port.mutex);
 	if (info->port.flags & ASYNC_INITIALIZED)
 		wait_until_sent(tty, info->timeout);
	flush_buffer(tty);
	tty_ldisc_flush(tty);

	shutdown(info);
	mutex_unlock(&info->port.mutex);

	tty_port_close_end(&info->port, tty);
	info->port.tty = NULL;
cleanup:
	DBGINFO(("%s close exit, count=%d\n", tty->driver->name, info->port.count));
}

static void hangup(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "hangup"))
		return;
	DBGINFO(("%s hangup\n", info->device_name));

	flush_buffer(tty);

	mutex_lock(&info->port.mutex);
	shutdown(info);

	spin_lock_irqsave(&info->port.lock, flags);
	info->port.count = 0;
	info->port.flags &= ~ASYNC_NORMAL_ACTIVE;
	info->port.tty = NULL;
	spin_unlock_irqrestore(&info->port.lock, flags);
	mutex_unlock(&info->port.mutex);

	wake_up_interruptible(&info->port.open_wait);
}

static void set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	DBGINFO(("%s set_termios\n", tty->driver->name));

	change_params(info);

	
	if (old_termios->c_cflag & CBAUD &&
	    !(tty->termios->c_cflag & CBAUD)) {
		info->signals &= ~(SerialSignal_RTS + SerialSignal_DTR);
		spin_lock_irqsave(&info->lock,flags);
		set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}

	
	if (!(old_termios->c_cflag & CBAUD) &&
	    tty->termios->c_cflag & CBAUD) {
		info->signals |= SerialSignal_DTR;
 		if (!(tty->termios->c_cflag & CRTSCTS) ||
 		    !test_bit(TTY_THROTTLED, &tty->flags)) {
			info->signals |= SerialSignal_RTS;
 		}
		spin_lock_irqsave(&info->lock,flags);
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}

	
	if (old_termios->c_cflag & CRTSCTS &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		tx_release(tty);
	}
}

static void update_tx_timer(struct slgt_info *info)
{
	if (info->params.mode == MGSL_MODE_HDLC) {
		int timeout  = (tbuf_bytes(info) * 7) + 1000;
		mod_timer(&info->tx_timer, jiffies + msecs_to_jiffies(timeout));
	}
}

static int write(struct tty_struct *tty,
		 const unsigned char *buf, int count)
{
	int ret = 0;
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "write"))
		return -EIO;

	DBGINFO(("%s write count=%d\n", info->device_name, count));

	if (!info->tx_buf || (count > info->max_frame_size))
		return -EIO;

	if (!count || tty->stopped || tty->hw_stopped)
		return 0;

	spin_lock_irqsave(&info->lock, flags);

	if (info->tx_count) {
		
		if (!tx_load(info, info->tx_buf, info->tx_count))
			goto cleanup;
		info->tx_count = 0;
	}

	if (tx_load(info, buf, count))
		ret = count;

cleanup:
	spin_unlock_irqrestore(&info->lock, flags);
	DBGINFO(("%s write rc=%d\n", info->device_name, ret));
	return ret;
}

static int put_char(struct tty_struct *tty, unsigned char ch)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;
	int ret = 0;

	if (sanity_check(info, tty->name, "put_char"))
		return 0;
	DBGINFO(("%s put_char(%d)\n", info->device_name, ch));
	if (!info->tx_buf)
		return 0;
	spin_lock_irqsave(&info->lock,flags);
	if (info->tx_count < info->max_frame_size) {
		info->tx_buf[info->tx_count++] = ch;
		ret = 1;
	}
	spin_unlock_irqrestore(&info->lock,flags);
	return ret;
}

static void send_xchar(struct tty_struct *tty, char ch)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "send_xchar"))
		return;
	DBGINFO(("%s send_xchar(%d)\n", info->device_name, ch));
	info->x_char = ch;
	if (ch) {
		spin_lock_irqsave(&info->lock,flags);
		if (!info->tx_enabled)
		 	tx_start(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}
}

static void wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long orig_jiffies, char_time;

	if (!info )
		return;
	if (sanity_check(info, tty->name, "wait_until_sent"))
		return;
	DBGINFO(("%s wait_until_sent entry\n", info->device_name));
	if (!(info->port.flags & ASYNC_INITIALIZED))
		goto exit;

	orig_jiffies = jiffies;


	if (info->params.data_rate) {
	       	char_time = info->timeout/(32 * 5);
		if (!char_time)
			char_time++;
	} else
		char_time = 1;

	if (timeout)
		char_time = min_t(unsigned long, char_time, timeout);

	while (info->tx_active) {
		msleep_interruptible(jiffies_to_msecs(char_time));
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
exit:
	DBGINFO(("%s wait_until_sent exit\n", info->device_name));
}

static int write_room(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	int ret;

	if (sanity_check(info, tty->name, "write_room"))
		return 0;
	ret = (info->tx_active) ? 0 : HDLC_MAX_FRAME_SIZE;
	DBGINFO(("%s write_room=%d\n", info->device_name, ret));
	return ret;
}

static void flush_chars(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "flush_chars"))
		return;
	DBGINFO(("%s flush_chars entry tx_count=%d\n", info->device_name, info->tx_count));

	if (info->tx_count <= 0 || tty->stopped ||
	    tty->hw_stopped || !info->tx_buf)
		return;

	DBGINFO(("%s flush_chars start transmit\n", info->device_name));

	spin_lock_irqsave(&info->lock,flags);
	if (info->tx_count && tx_load(info, info->tx_buf, info->tx_count))
		info->tx_count = 0;
	spin_unlock_irqrestore(&info->lock,flags);
}

static void flush_buffer(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "flush_buffer"))
		return;
	DBGINFO(("%s flush_buffer\n", info->device_name));

	spin_lock_irqsave(&info->lock, flags);
	info->tx_count = 0;
	spin_unlock_irqrestore(&info->lock, flags);

	tty_wakeup(tty);
}

static void tx_hold(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "tx_hold"))
		return;
	DBGINFO(("%s tx_hold\n", info->device_name));
	spin_lock_irqsave(&info->lock,flags);
	if (info->tx_enabled && info->params.mode == MGSL_MODE_ASYNC)
	 	tx_stop(info);
	spin_unlock_irqrestore(&info->lock,flags);
}

static void tx_release(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "tx_release"))
		return;
	DBGINFO(("%s tx_release\n", info->device_name));
	spin_lock_irqsave(&info->lock, flags);
	if (info->tx_count && tx_load(info, info->tx_buf, info->tx_count))
		info->tx_count = 0;
	spin_unlock_irqrestore(&info->lock, flags);
}

static int ioctl(struct tty_struct *tty,
		 unsigned int cmd, unsigned long arg)
{
	struct slgt_info *info = tty->driver_data;
	void __user *argp = (void __user *)arg;
	int ret;

	if (sanity_check(info, tty->name, "ioctl"))
		return -ENODEV;
	DBGINFO(("%s ioctl() cmd=%08X\n", info->device_name, cmd));

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCMIWAIT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
	case MGSL_IOCWAITEVENT:
		return wait_mgsl_event(info, argp);
	case TIOCMIWAIT:
		return modem_input_wait(info,(int)arg);
	case MGSL_IOCSGPIO:
		return set_gpio(info, argp);
	case MGSL_IOCGGPIO:
		return get_gpio(info, argp);
	case MGSL_IOCWAITGPIO:
		return wait_gpio(info, argp);
	case MGSL_IOCGXSYNC:
		return get_xsync(info, argp);
	case MGSL_IOCSXSYNC:
		return set_xsync(info, (int)arg);
	case MGSL_IOCGXCTRL:
		return get_xctrl(info, argp);
	case MGSL_IOCSXCTRL:
		return set_xctrl(info, (int)arg);
	}
	mutex_lock(&info->port.mutex);
	switch (cmd) {
	case MGSL_IOCGPARAMS:
		ret = get_params(info, argp);
		break;
	case MGSL_IOCSPARAMS:
		ret = set_params(info, argp);
		break;
	case MGSL_IOCGTXIDLE:
		ret = get_txidle(info, argp);
		break;
	case MGSL_IOCSTXIDLE:
		ret = set_txidle(info, (int)arg);
		break;
	case MGSL_IOCTXENABLE:
		ret = tx_enable(info, (int)arg);
		break;
	case MGSL_IOCRXENABLE:
		ret = rx_enable(info, (int)arg);
		break;
	case MGSL_IOCTXABORT:
		ret = tx_abort(info);
		break;
	case MGSL_IOCGSTATS:
		ret = get_stats(info, argp);
		break;
	case MGSL_IOCGIF:
		ret = get_interface(info, argp);
		break;
	case MGSL_IOCSIF:
		ret = set_interface(info,(int)arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}
	mutex_unlock(&info->port.mutex);
	return ret;
}

static int get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)

{
	struct slgt_info *info = tty->driver_data;
	struct mgsl_icount cnow;	
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
	cnow = info->icount;
	spin_unlock_irqrestore(&info->lock,flags);

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}

#ifdef CONFIG_COMPAT
static long get_params32(struct slgt_info *info, struct MGSL_PARAMS32 __user *user_params)
{
	struct MGSL_PARAMS32 tmp_params;

	DBGINFO(("%s get_params32\n", info->device_name));
	memset(&tmp_params, 0, sizeof(tmp_params));
	tmp_params.mode            = (compat_ulong_t)info->params.mode;
	tmp_params.loopback        = info->params.loopback;
	tmp_params.flags           = info->params.flags;
	tmp_params.encoding        = info->params.encoding;
	tmp_params.clock_speed     = (compat_ulong_t)info->params.clock_speed;
	tmp_params.addr_filter     = info->params.addr_filter;
	tmp_params.crc_type        = info->params.crc_type;
	tmp_params.preamble_length = info->params.preamble_length;
	tmp_params.preamble        = info->params.preamble;
	tmp_params.data_rate       = (compat_ulong_t)info->params.data_rate;
	tmp_params.data_bits       = info->params.data_bits;
	tmp_params.stop_bits       = info->params.stop_bits;
	tmp_params.parity          = info->params.parity;
	if (copy_to_user(user_params, &tmp_params, sizeof(struct MGSL_PARAMS32)))
		return -EFAULT;
	return 0;
}

static long set_params32(struct slgt_info *info, struct MGSL_PARAMS32 __user *new_params)
{
	struct MGSL_PARAMS32 tmp_params;

	DBGINFO(("%s set_params32\n", info->device_name));
	if (copy_from_user(&tmp_params, new_params, sizeof(struct MGSL_PARAMS32)))
		return -EFAULT;

	spin_lock(&info->lock);
	if (tmp_params.mode == MGSL_MODE_BASE_CLOCK) {
		info->base_clock = tmp_params.clock_speed;
	} else {
		info->params.mode            = tmp_params.mode;
		info->params.loopback        = tmp_params.loopback;
		info->params.flags           = tmp_params.flags;
		info->params.encoding        = tmp_params.encoding;
		info->params.clock_speed     = tmp_params.clock_speed;
		info->params.addr_filter     = tmp_params.addr_filter;
		info->params.crc_type        = tmp_params.crc_type;
		info->params.preamble_length = tmp_params.preamble_length;
		info->params.preamble        = tmp_params.preamble;
		info->params.data_rate       = tmp_params.data_rate;
		info->params.data_bits       = tmp_params.data_bits;
		info->params.stop_bits       = tmp_params.stop_bits;
		info->params.parity          = tmp_params.parity;
	}
	spin_unlock(&info->lock);

	program_hw(info);

	return 0;
}

static long slgt_compat_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	struct slgt_info *info = tty->driver_data;
	int rc = -ENOIOCTLCMD;

	if (sanity_check(info, tty->name, "compat_ioctl"))
		return -ENODEV;
	DBGINFO(("%s compat_ioctl() cmd=%08X\n", info->device_name, cmd));

	switch (cmd) {

	case MGSL_IOCSPARAMS32:
		rc = set_params32(info, compat_ptr(arg));
		break;

	case MGSL_IOCGPARAMS32:
		rc = get_params32(info, compat_ptr(arg));
		break;

	case MGSL_IOCGPARAMS:
	case MGSL_IOCSPARAMS:
	case MGSL_IOCGTXIDLE:
	case MGSL_IOCGSTATS:
	case MGSL_IOCWAITEVENT:
	case MGSL_IOCGIF:
	case MGSL_IOCSGPIO:
	case MGSL_IOCGGPIO:
	case MGSL_IOCWAITGPIO:
	case MGSL_IOCGXSYNC:
	case MGSL_IOCGXCTRL:
	case MGSL_IOCSTXIDLE:
	case MGSL_IOCTXENABLE:
	case MGSL_IOCRXENABLE:
	case MGSL_IOCTXABORT:
	case TIOCMIWAIT:
	case MGSL_IOCSIF:
	case MGSL_IOCSXSYNC:
	case MGSL_IOCSXCTRL:
		rc = ioctl(tty, cmd, arg);
		break;
	}

	DBGINFO(("%s compat_ioctl() cmd=%08X rc=%d\n", info->device_name, cmd, rc));
	return rc;
}
#else
#define slgt_compat_ioctl NULL
#endif 

static inline void line_info(struct seq_file *m, struct slgt_info *info)
{
	char stat_buf[30];
	unsigned long flags;

	seq_printf(m, "%s: IO=%08X IRQ=%d MaxFrameSize=%u\n",
		      info->device_name, info->phys_reg_addr,
		      info->irq_level, info->max_frame_size);

	
	spin_lock_irqsave(&info->lock,flags);
	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	stat_buf[0] = 0;
	stat_buf[1] = 0;
	if (info->signals & SerialSignal_RTS)
		strcat(stat_buf, "|RTS");
	if (info->signals & SerialSignal_CTS)
		strcat(stat_buf, "|CTS");
	if (info->signals & SerialSignal_DTR)
		strcat(stat_buf, "|DTR");
	if (info->signals & SerialSignal_DSR)
		strcat(stat_buf, "|DSR");
	if (info->signals & SerialSignal_DCD)
		strcat(stat_buf, "|CD");
	if (info->signals & SerialSignal_RI)
		strcat(stat_buf, "|RI");

	if (info->params.mode != MGSL_MODE_ASYNC) {
		seq_printf(m, "\tHDLC txok:%d rxok:%d",
			       info->icount.txok, info->icount.rxok);
		if (info->icount.txunder)
			seq_printf(m, " txunder:%d", info->icount.txunder);
		if (info->icount.txabort)
			seq_printf(m, " txabort:%d", info->icount.txabort);
		if (info->icount.rxshort)
			seq_printf(m, " rxshort:%d", info->icount.rxshort);
		if (info->icount.rxlong)
			seq_printf(m, " rxlong:%d", info->icount.rxlong);
		if (info->icount.rxover)
			seq_printf(m, " rxover:%d", info->icount.rxover);
		if (info->icount.rxcrc)
			seq_printf(m, " rxcrc:%d", info->icount.rxcrc);
	} else {
		seq_printf(m, "\tASYNC tx:%d rx:%d",
			       info->icount.tx, info->icount.rx);
		if (info->icount.frame)
			seq_printf(m, " fe:%d", info->icount.frame);
		if (info->icount.parity)
			seq_printf(m, " pe:%d", info->icount.parity);
		if (info->icount.brk)
			seq_printf(m, " brk:%d", info->icount.brk);
		if (info->icount.overrun)
			seq_printf(m, " oe:%d", info->icount.overrun);
	}

	
	seq_printf(m, " %s\n", stat_buf+1);

	seq_printf(m, "\ttxactive=%d bh_req=%d bh_run=%d pending_bh=%x\n",
		       info->tx_active,info->bh_requested,info->bh_running,
		       info->pending_bh);
}

static int synclink_gt_proc_show(struct seq_file *m, void *v)
{
	struct slgt_info *info;

	seq_puts(m, "synclink_gt driver\n");

	info = slgt_device_list;
	while( info ) {
		line_info(m, info);
		info = info->next_device;
	}
	return 0;
}

static int synclink_gt_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, synclink_gt_proc_show, NULL);
}

static const struct file_operations synclink_gt_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= synclink_gt_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int chars_in_buffer(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	int count;
	if (sanity_check(info, tty->name, "chars_in_buffer"))
		return 0;
	count = tbuf_bytes(info);
	DBGINFO(("%s chars_in_buffer()=%d\n", info->device_name, count));
	return count;
}

static void throttle(struct tty_struct * tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "throttle"))
		return;
	DBGINFO(("%s throttle\n", info->device_name));
	if (I_IXOFF(tty))
		send_xchar(tty, STOP_CHAR(tty));
 	if (tty->termios->c_cflag & CRTSCTS) {
		spin_lock_irqsave(&info->lock,flags);
		info->signals &= ~SerialSignal_RTS;
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}
}

static void unthrottle(struct tty_struct * tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned long flags;

	if (sanity_check(info, tty->name, "unthrottle"))
		return;
	DBGINFO(("%s unthrottle\n", info->device_name));
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			send_xchar(tty, START_CHAR(tty));
	}
 	if (tty->termios->c_cflag & CRTSCTS) {
		spin_lock_irqsave(&info->lock,flags);
		info->signals |= SerialSignal_RTS;
	 	set_signals(info);
		spin_unlock_irqrestore(&info->lock,flags);
	}
}

static int set_break(struct tty_struct *tty, int break_state)
{
	struct slgt_info *info = tty->driver_data;
	unsigned short value;
	unsigned long flags;

	if (sanity_check(info, tty->name, "set_break"))
		return -EINVAL;
	DBGINFO(("%s set_break(%d)\n", info->device_name, break_state));

	spin_lock_irqsave(&info->lock,flags);
	value = rd_reg16(info, TCR);
 	if (break_state == -1)
		value |= BIT6;
	else
		value &= ~BIT6;
	wr_reg16(info, TCR, value);
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

#if SYNCLINK_GENERIC_HDLC

static int hdlcdev_attach(struct net_device *dev, unsigned short encoding,
			  unsigned short parity)
{
	struct slgt_info *info = dev_to_port(dev);
	unsigned char  new_encoding;
	unsigned short new_crctype;

	
	if (info->port.count)
		return -EBUSY;

	DBGINFO(("%s hdlcdev_attach\n", info->device_name));

	switch (encoding)
	{
	case ENCODING_NRZ:        new_encoding = HDLC_ENCODING_NRZ; break;
	case ENCODING_NRZI:       new_encoding = HDLC_ENCODING_NRZI_SPACE; break;
	case ENCODING_FM_MARK:    new_encoding = HDLC_ENCODING_BIPHASE_MARK; break;
	case ENCODING_FM_SPACE:   new_encoding = HDLC_ENCODING_BIPHASE_SPACE; break;
	case ENCODING_MANCHESTER: new_encoding = HDLC_ENCODING_BIPHASE_LEVEL; break;
	default: return -EINVAL;
	}

	switch (parity)
	{
	case PARITY_NONE:            new_crctype = HDLC_CRC_NONE; break;
	case PARITY_CRC16_PR1_CCITT: new_crctype = HDLC_CRC_16_CCITT; break;
	case PARITY_CRC32_PR1_CCITT: new_crctype = HDLC_CRC_32_CCITT; break;
	default: return -EINVAL;
	}

	info->params.encoding = new_encoding;
	info->params.crc_type = new_crctype;

	
	if (info->netcount)
		program_hw(info);

	return 0;
}

static netdev_tx_t hdlcdev_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct slgt_info *info = dev_to_port(dev);
	unsigned long flags;

	DBGINFO(("%s hdlc_xmit\n", dev->name));

	if (!skb->len)
		return NETDEV_TX_OK;

	
	netif_stop_queue(dev);

	
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	
	dev->trans_start = jiffies;

	spin_lock_irqsave(&info->lock, flags);
	tx_load(info, skb->data, skb->len);
	spin_unlock_irqrestore(&info->lock, flags);

	
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int hdlcdev_open(struct net_device *dev)
{
	struct slgt_info *info = dev_to_port(dev);
	int rc;
	unsigned long flags;

	if (!try_module_get(THIS_MODULE))
		return -EBUSY;

	DBGINFO(("%s hdlcdev_open\n", dev->name));

	
	if ((rc = hdlc_open(dev)))
		return rc;

	
	spin_lock_irqsave(&info->netlock, flags);
	if (info->port.count != 0 || info->netcount != 0) {
		DBGINFO(("%s hdlc_open busy\n", dev->name));
		spin_unlock_irqrestore(&info->netlock, flags);
		return -EBUSY;
	}
	info->netcount=1;
	spin_unlock_irqrestore(&info->netlock, flags);

	
	if ((rc = startup(info)) != 0) {
		spin_lock_irqsave(&info->netlock, flags);
		info->netcount=0;
		spin_unlock_irqrestore(&info->netlock, flags);
		return rc;
	}

	
	info->signals |= SerialSignal_RTS + SerialSignal_DTR;
	program_hw(info);

	
	dev->trans_start = jiffies;
	netif_start_queue(dev);

	
	spin_lock_irqsave(&info->lock, flags);
	get_signals(info);
	spin_unlock_irqrestore(&info->lock, flags);
	if (info->signals & SerialSignal_DCD)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	return 0;
}

static int hdlcdev_close(struct net_device *dev)
{
	struct slgt_info *info = dev_to_port(dev);
	unsigned long flags;

	DBGINFO(("%s hdlcdev_close\n", dev->name));

	netif_stop_queue(dev);

	
	shutdown(info);

	hdlc_close(dev);

	spin_lock_irqsave(&info->netlock, flags);
	info->netcount=0;
	spin_unlock_irqrestore(&info->netlock, flags);

	module_put(THIS_MODULE);
	return 0;
}

static int hdlcdev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	const size_t size = sizeof(sync_serial_settings);
	sync_serial_settings new_line;
	sync_serial_settings __user *line = ifr->ifr_settings.ifs_ifsu.sync;
	struct slgt_info *info = dev_to_port(dev);
	unsigned int flags;

	DBGINFO(("%s hdlcdev_ioctl\n", dev->name));

	
	if (info->port.count)
		return -EBUSY;

	if (cmd != SIOCWANDEV)
		return hdlc_ioctl(dev, ifr, cmd);

	memset(&new_line, 0, sizeof(new_line));

	switch(ifr->ifr_settings.type) {
	case IF_GET_IFACE: 

		ifr->ifr_settings.type = IF_IFACE_SYNC_SERIAL;
		if (ifr->ifr_settings.size < size) {
			ifr->ifr_settings.size = size; 
			return -ENOBUFS;
		}

		flags = info->params.flags & (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_RXC_DPLL |
					      HDLC_FLAG_RXC_BRG    | HDLC_FLAG_RXC_TXCPIN |
					      HDLC_FLAG_TXC_TXCPIN | HDLC_FLAG_TXC_DPLL |
					      HDLC_FLAG_TXC_BRG    | HDLC_FLAG_TXC_RXCPIN);

		switch (flags){
		case (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_TXCPIN): new_line.clock_type = CLOCK_EXT; break;
		case (HDLC_FLAG_RXC_BRG    | HDLC_FLAG_TXC_BRG):    new_line.clock_type = CLOCK_INT; break;
		case (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_BRG):    new_line.clock_type = CLOCK_TXINT; break;
		case (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_RXCPIN): new_line.clock_type = CLOCK_TXFROMRX; break;
		default: new_line.clock_type = CLOCK_DEFAULT;
		}

		new_line.clock_rate = info->params.clock_speed;
		new_line.loopback   = info->params.loopback ? 1:0;

		if (copy_to_user(line, &new_line, size))
			return -EFAULT;
		return 0;

	case IF_IFACE_SYNC_SERIAL: 

		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&new_line, line, size))
			return -EFAULT;

		switch (new_line.clock_type)
		{
		case CLOCK_EXT:      flags = HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_TXCPIN; break;
		case CLOCK_TXFROMRX: flags = HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_RXCPIN; break;
		case CLOCK_INT:      flags = HDLC_FLAG_RXC_BRG    | HDLC_FLAG_TXC_BRG;    break;
		case CLOCK_TXINT:    flags = HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_TXC_BRG;    break;
		case CLOCK_DEFAULT:  flags = info->params.flags &
					     (HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_RXC_DPLL |
					      HDLC_FLAG_RXC_BRG    | HDLC_FLAG_RXC_TXCPIN |
					      HDLC_FLAG_TXC_TXCPIN | HDLC_FLAG_TXC_DPLL |
					      HDLC_FLAG_TXC_BRG    | HDLC_FLAG_TXC_RXCPIN); break;
		default: return -EINVAL;
		}

		if (new_line.loopback != 0 && new_line.loopback != 1)
			return -EINVAL;

		info->params.flags &= ~(HDLC_FLAG_RXC_RXCPIN | HDLC_FLAG_RXC_DPLL |
					HDLC_FLAG_RXC_BRG    | HDLC_FLAG_RXC_TXCPIN |
					HDLC_FLAG_TXC_TXCPIN | HDLC_FLAG_TXC_DPLL |
					HDLC_FLAG_TXC_BRG    | HDLC_FLAG_TXC_RXCPIN);
		info->params.flags |= flags;

		info->params.loopback = new_line.loopback;

		if (flags & (HDLC_FLAG_RXC_BRG | HDLC_FLAG_TXC_BRG))
			info->params.clock_speed = new_line.clock_rate;
		else
			info->params.clock_speed = 0;

		
		if (info->netcount)
			program_hw(info);
		return 0;

	default:
		return hdlc_ioctl(dev, ifr, cmd);
	}
}

static void hdlcdev_tx_timeout(struct net_device *dev)
{
	struct slgt_info *info = dev_to_port(dev);
	unsigned long flags;

	DBGINFO(("%s hdlcdev_tx_timeout\n", dev->name));

	dev->stats.tx_errors++;
	dev->stats.tx_aborted_errors++;

	spin_lock_irqsave(&info->lock,flags);
	tx_stop(info);
	spin_unlock_irqrestore(&info->lock,flags);

	netif_wake_queue(dev);
}

static void hdlcdev_tx_done(struct slgt_info *info)
{
	if (netif_queue_stopped(info->netdev))
		netif_wake_queue(info->netdev);
}

static void hdlcdev_rx(struct slgt_info *info, char *buf, int size)
{
	struct sk_buff *skb = dev_alloc_skb(size);
	struct net_device *dev = info->netdev;

	DBGINFO(("%s hdlcdev_rx\n", dev->name));

	if (skb == NULL) {
		DBGERR(("%s: can't alloc skb, drop packet\n", dev->name));
		dev->stats.rx_dropped++;
		return;
	}

	memcpy(skb_put(skb, size), buf, size);

	skb->protocol = hdlc_type_trans(skb, dev);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += size;

	netif_rx(skb);
}

static const struct net_device_ops hdlcdev_ops = {
	.ndo_open       = hdlcdev_open,
	.ndo_stop       = hdlcdev_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = hdlcdev_ioctl,
	.ndo_tx_timeout = hdlcdev_tx_timeout,
};

static int hdlcdev_init(struct slgt_info *info)
{
	int rc;
	struct net_device *dev;
	hdlc_device *hdlc;

	

	if (!(dev = alloc_hdlcdev(info))) {
		printk(KERN_ERR "%s hdlc device alloc failure\n", info->device_name);
		return -ENOMEM;
	}

	
	dev->mem_start = info->phys_reg_addr;
	dev->mem_end   = info->phys_reg_addr + SLGT_REG_SIZE - 1;
	dev->irq       = info->irq_level;

	
	dev->netdev_ops	    = &hdlcdev_ops;
	dev->watchdog_timeo = 10 * HZ;
	dev->tx_queue_len   = 50;

	
	hdlc         = dev_to_hdlc(dev);
	hdlc->attach = hdlcdev_attach;
	hdlc->xmit   = hdlcdev_xmit;

	
	if ((rc = register_hdlc_device(dev))) {
		printk(KERN_WARNING "%s:unable to register hdlc device\n",__FILE__);
		free_netdev(dev);
		return rc;
	}

	info->netdev = dev;
	return 0;
}

static void hdlcdev_exit(struct slgt_info *info)
{
	unregister_hdlc_device(info->netdev);
	free_netdev(info->netdev);
	info->netdev = NULL;
}

#endif 

static void rx_async(struct slgt_info *info)
{
 	struct tty_struct *tty = info->port.tty;
 	struct mgsl_icount *icount = &info->icount;
	unsigned int start, end;
	unsigned char *p;
	unsigned char status;
	struct slgt_desc *bufs = info->rbufs;
	int i, count;
	int chars = 0;
	int stat;
	unsigned char ch;

	start = end = info->rbuf_current;

	while(desc_complete(bufs[end])) {
		count = desc_count(bufs[end]) - info->rbuf_index;
		p     = bufs[end].buf + info->rbuf_index;

		DBGISR(("%s rx_async count=%d\n", info->device_name, count));
		DBGDATA(info, p, count, "rx");

		for(i=0 ; i < count; i+=2, p+=2) {
			ch = *p;
			icount->rx++;

			stat = 0;

			if ((status = *(p+1) & (BIT1 + BIT0))) {
				if (status & BIT1)
					icount->parity++;
				else if (status & BIT0)
					icount->frame++;
				
				if (status & info->ignore_status_mask)
					continue;
				if (status & BIT1)
					stat = TTY_PARITY;
				else if (status & BIT0)
					stat = TTY_FRAME;
			}
			if (tty) {
				tty_insert_flip_char(tty, ch, stat);
				chars++;
			}
		}

		if (i < count) {
			
			info->rbuf_index += i;
			mod_timer(&info->rx_timer, jiffies + 1);
			break;
		}

		info->rbuf_index = 0;
		free_rbufs(info, end, end);

		if (++end == info->rbuf_count)
			end = 0;

		
		if (end == start)
			break;
	}

	if (tty && chars)
		tty_flip_buffer_push(tty);
}

static int bh_action(struct slgt_info *info)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&info->lock,flags);

	if (info->pending_bh & BH_RECEIVE) {
		info->pending_bh &= ~BH_RECEIVE;
		rc = BH_RECEIVE;
	} else if (info->pending_bh & BH_TRANSMIT) {
		info->pending_bh &= ~BH_TRANSMIT;
		rc = BH_TRANSMIT;
	} else if (info->pending_bh & BH_STATUS) {
		info->pending_bh &= ~BH_STATUS;
		rc = BH_STATUS;
	} else {
		
		info->bh_running = false;
		info->bh_requested = false;
		rc = 0;
	}

	spin_unlock_irqrestore(&info->lock,flags);

	return rc;
}

static void bh_handler(struct work_struct *work)
{
	struct slgt_info *info = container_of(work, struct slgt_info, task);
	int action;

	if (!info)
		return;
	info->bh_running = true;

	while((action = bh_action(info))) {
		switch (action) {
		case BH_RECEIVE:
			DBGBH(("%s bh receive\n", info->device_name));
			switch(info->params.mode) {
			case MGSL_MODE_ASYNC:
				rx_async(info);
				break;
			case MGSL_MODE_HDLC:
				while(rx_get_frame(info));
				break;
			case MGSL_MODE_RAW:
			case MGSL_MODE_MONOSYNC:
			case MGSL_MODE_BISYNC:
			case MGSL_MODE_XSYNC:
				while(rx_get_buf(info));
				break;
			}
			
			if (info->rx_restart)
				rx_start(info);
			break;
		case BH_TRANSMIT:
			bh_transmit(info);
			break;
		case BH_STATUS:
			DBGBH(("%s bh status\n", info->device_name));
			info->ri_chkcount = 0;
			info->dsr_chkcount = 0;
			info->dcd_chkcount = 0;
			info->cts_chkcount = 0;
			break;
		default:
			DBGBH(("%s unknown action\n", info->device_name));
			break;
		}
	}
	DBGBH(("%s bh_handler exit\n", info->device_name));
}

static void bh_transmit(struct slgt_info *info)
{
	struct tty_struct *tty = info->port.tty;

	DBGBH(("%s bh_transmit\n", info->device_name));
	if (tty)
		tty_wakeup(tty);
}

static void dsr_change(struct slgt_info *info, unsigned short status)
{
	if (status & BIT3) {
		info->signals |= SerialSignal_DSR;
		info->input_signal_events.dsr_up++;
	} else {
		info->signals &= ~SerialSignal_DSR;
		info->input_signal_events.dsr_down++;
	}
	DBGISR(("dsr_change %s signals=%04X\n", info->device_name, info->signals));
	if ((info->dsr_chkcount)++ == IO_PIN_SHUTDOWN_LIMIT) {
		slgt_irq_off(info, IRQ_DSR);
		return;
	}
	info->icount.dsr++;
	wake_up_interruptible(&info->status_event_wait_q);
	wake_up_interruptible(&info->event_wait_q);
	info->pending_bh |= BH_STATUS;
}

static void cts_change(struct slgt_info *info, unsigned short status)
{
	if (status & BIT2) {
		info->signals |= SerialSignal_CTS;
		info->input_signal_events.cts_up++;
	} else {
		info->signals &= ~SerialSignal_CTS;
		info->input_signal_events.cts_down++;
	}
	DBGISR(("cts_change %s signals=%04X\n", info->device_name, info->signals));
	if ((info->cts_chkcount)++ == IO_PIN_SHUTDOWN_LIMIT) {
		slgt_irq_off(info, IRQ_CTS);
		return;
	}
	info->icount.cts++;
	wake_up_interruptible(&info->status_event_wait_q);
	wake_up_interruptible(&info->event_wait_q);
	info->pending_bh |= BH_STATUS;

	if (info->port.flags & ASYNC_CTS_FLOW) {
		if (info->port.tty) {
			if (info->port.tty->hw_stopped) {
				if (info->signals & SerialSignal_CTS) {
		 			info->port.tty->hw_stopped = 0;
					info->pending_bh |= BH_TRANSMIT;
					return;
				}
			} else {
				if (!(info->signals & SerialSignal_CTS))
		 			info->port.tty->hw_stopped = 1;
			}
		}
	}
}

static void dcd_change(struct slgt_info *info, unsigned short status)
{
	if (status & BIT1) {
		info->signals |= SerialSignal_DCD;
		info->input_signal_events.dcd_up++;
	} else {
		info->signals &= ~SerialSignal_DCD;
		info->input_signal_events.dcd_down++;
	}
	DBGISR(("dcd_change %s signals=%04X\n", info->device_name, info->signals));
	if ((info->dcd_chkcount)++ == IO_PIN_SHUTDOWN_LIMIT) {
		slgt_irq_off(info, IRQ_DCD);
		return;
	}
	info->icount.dcd++;
#if SYNCLINK_GENERIC_HDLC
	if (info->netcount) {
		if (info->signals & SerialSignal_DCD)
			netif_carrier_on(info->netdev);
		else
			netif_carrier_off(info->netdev);
	}
#endif
	wake_up_interruptible(&info->status_event_wait_q);
	wake_up_interruptible(&info->event_wait_q);
	info->pending_bh |= BH_STATUS;

	if (info->port.flags & ASYNC_CHECK_CD) {
		if (info->signals & SerialSignal_DCD)
			wake_up_interruptible(&info->port.open_wait);
		else {
			if (info->port.tty)
				tty_hangup(info->port.tty);
		}
	}
}

static void ri_change(struct slgt_info *info, unsigned short status)
{
	if (status & BIT0) {
		info->signals |= SerialSignal_RI;
		info->input_signal_events.ri_up++;
	} else {
		info->signals &= ~SerialSignal_RI;
		info->input_signal_events.ri_down++;
	}
	DBGISR(("ri_change %s signals=%04X\n", info->device_name, info->signals));
	if ((info->ri_chkcount)++ == IO_PIN_SHUTDOWN_LIMIT) {
		slgt_irq_off(info, IRQ_RI);
		return;
	}
	info->icount.rng++;
	wake_up_interruptible(&info->status_event_wait_q);
	wake_up_interruptible(&info->event_wait_q);
	info->pending_bh |= BH_STATUS;
}

static void isr_rxdata(struct slgt_info *info)
{
	unsigned int count = info->rbuf_fill_count;
	unsigned int i = info->rbuf_fill_index;
	unsigned short reg;

	while (rd_reg16(info, SSR) & IRQ_RXDATA) {
		reg = rd_reg16(info, RDR);
		DBGISR(("isr_rxdata %s RDR=%04X\n", info->device_name, reg));
		if (desc_complete(info->rbufs[i])) {
			
			rx_stop(info);
			info->rx_restart = 1;
			continue;
		}
		info->rbufs[i].buf[count++] = (unsigned char)reg;
		
		if (info->params.mode == MGSL_MODE_ASYNC)
			info->rbufs[i].buf[count++] = (unsigned char)(reg >> 8);
		if (count == info->rbuf_fill_level || (reg & BIT10)) {
			
			set_desc_count(info->rbufs[i], count);
			set_desc_status(info->rbufs[i], BIT15 | (reg >> 8));
			info->rbuf_fill_count = count = 0;
			if (++i == info->rbuf_count)
				i = 0;
			info->pending_bh |= BH_RECEIVE;
		}
	}

	info->rbuf_fill_index = i;
	info->rbuf_fill_count = count;
}

static void isr_serial(struct slgt_info *info)
{
	unsigned short status = rd_reg16(info, SSR);

	DBGISR(("%s isr_serial status=%04X\n", info->device_name, status));

	wr_reg16(info, SSR, status); 

	info->irq_occurred = true;

	if (info->params.mode == MGSL_MODE_ASYNC) {
		if (status & IRQ_TXIDLE) {
			if (info->tx_active)
				isr_txeom(info, status);
		}
		if (info->rx_pio && (status & IRQ_RXDATA))
			isr_rxdata(info);
		if ((status & IRQ_RXBREAK) && (status & RXBREAK)) {
			info->icount.brk++;
			
			if (info->port.tty) {
				if (!(status & info->ignore_status_mask)) {
					if (info->read_status_mask & MASK_BREAK) {
						tty_insert_flip_char(info->port.tty, 0, TTY_BREAK);
						if (info->port.flags & ASYNC_SAK)
							do_SAK(info->port.tty);
					}
				}
			}
		}
	} else {
		if (status & (IRQ_TXIDLE + IRQ_TXUNDER))
			isr_txeom(info, status);
		if (info->rx_pio && (status & IRQ_RXDATA))
			isr_rxdata(info);
		if (status & IRQ_RXIDLE) {
			if (status & RXIDLE)
				info->icount.rxidle++;
			else
				info->icount.exithunt++;
			wake_up_interruptible(&info->event_wait_q);
		}

		if (status & IRQ_RXOVER)
			rx_start(info);
	}

	if (status & IRQ_DSR)
		dsr_change(info, status);
	if (status & IRQ_CTS)
		cts_change(info, status);
	if (status & IRQ_DCD)
		dcd_change(info, status);
	if (status & IRQ_RI)
		ri_change(info, status);
}

static void isr_rdma(struct slgt_info *info)
{
	unsigned int status = rd_reg32(info, RDCSR);

	DBGISR(("%s isr_rdma status=%08x\n", info->device_name, status));

	wr_reg32(info, RDCSR, status);	

	if (status & (BIT5 + BIT4)) {
		DBGISR(("%s isr_rdma rx_restart=1\n", info->device_name));
		info->rx_restart = true;
	}
	info->pending_bh |= BH_RECEIVE;
}

static void isr_tdma(struct slgt_info *info)
{
	unsigned int status = rd_reg32(info, TDCSR);

	DBGISR(("%s isr_tdma status=%08x\n", info->device_name, status));

	wr_reg32(info, TDCSR, status);	

	if (status & (BIT5 + BIT4 + BIT3)) {
		
		
		info->pending_bh |= BH_TRANSMIT;
	}
}

static bool unsent_tbufs(struct slgt_info *info)
{
	unsigned int i = info->tbuf_current;
	bool rc = false;


	do {
		if (i)
			i--;
		else
			i = info->tbuf_count - 1;
		if (!desc_count(info->tbufs[i]))
			break;
		info->tbuf_start = i;
		rc = true;
	} while (i != info->tbuf_current);

	return rc;
}

static void isr_txeom(struct slgt_info *info, unsigned short status)
{
	DBGISR(("%s txeom status=%04x\n", info->device_name, status));

	slgt_irq_off(info, IRQ_TXDATA + IRQ_TXIDLE + IRQ_TXUNDER);
	tdma_reset(info);
	if (status & IRQ_TXUNDER) {
		unsigned short val = rd_reg16(info, TCR);
		wr_reg16(info, TCR, (unsigned short)(val | BIT2)); 
		wr_reg16(info, TCR, val); 
	}

	if (info->tx_active) {
		if (info->params.mode != MGSL_MODE_ASYNC) {
			if (status & IRQ_TXUNDER)
				info->icount.txunder++;
			else if (status & IRQ_TXIDLE)
				info->icount.txok++;
		}

		if (unsent_tbufs(info)) {
			tx_start(info);
			update_tx_timer(info);
			return;
		}
		info->tx_active = false;

		del_timer(&info->tx_timer);

		if (info->params.mode != MGSL_MODE_ASYNC && info->drop_rts_on_tx_done) {
			info->signals &= ~SerialSignal_RTS;
			info->drop_rts_on_tx_done = false;
			set_signals(info);
		}

#if SYNCLINK_GENERIC_HDLC
		if (info->netcount)
			hdlcdev_tx_done(info);
		else
#endif
		{
			if (info->port.tty && (info->port.tty->stopped || info->port.tty->hw_stopped)) {
				tx_stop(info);
				return;
			}
			info->pending_bh |= BH_TRANSMIT;
		}
	}
}

static void isr_gpio(struct slgt_info *info, unsigned int changed, unsigned int state)
{
	struct cond_wait *w, *prev;

	
	for (w = info->gpio_wait_q, prev = NULL ; w != NULL ; w = w->next) {
		if (w->data & changed) {
			w->data = state;
			wake_up_interruptible(&w->q);
			if (prev != NULL)
				prev->next = w->next;
			else
				info->gpio_wait_q = w->next;
		} else
			prev = w;
	}
}

static irqreturn_t slgt_interrupt(int dummy, void *dev_id)
{
	struct slgt_info *info = dev_id;
	unsigned int gsr;
	unsigned int i;

	DBGISR(("slgt_interrupt irq=%d entry\n", info->irq_level));

	while((gsr = rd_reg32(info, GSR) & 0xffffff00)) {
		DBGISR(("%s gsr=%08x\n", info->device_name, gsr));
		info->irq_occurred = true;
		for(i=0; i < info->port_count ; i++) {
			if (info->port_array[i] == NULL)
				continue;
			spin_lock(&info->port_array[i]->lock);
			if (gsr & (BIT8 << i))
				isr_serial(info->port_array[i]);
			if (gsr & (BIT16 << (i*2)))
				isr_rdma(info->port_array[i]);
			if (gsr & (BIT17 << (i*2)))
				isr_tdma(info->port_array[i]);
			spin_unlock(&info->port_array[i]->lock);
		}
	}

	if (info->gpio_present) {
		unsigned int state;
		unsigned int changed;
		spin_lock(&info->lock);
		while ((changed = rd_reg32(info, IOSR)) != 0) {
			DBGISR(("%s iosr=%08x\n", info->device_name, changed));
			
			state = rd_reg32(info, IOVR);
			
			wr_reg32(info, IOSR, changed);
			for (i=0 ; i < info->port_count ; i++) {
				if (info->port_array[i] != NULL)
					isr_gpio(info->port_array[i], changed, state);
			}
		}
		spin_unlock(&info->lock);
	}

	for(i=0; i < info->port_count ; i++) {
		struct slgt_info *port = info->port_array[i];
		if (port == NULL)
			continue;
		spin_lock(&port->lock);
		if ((port->port.count || port->netcount) &&
		    port->pending_bh && !port->bh_running &&
		    !port->bh_requested) {
			DBGISR(("%s bh queued\n", port->device_name));
			schedule_work(&port->task);
			port->bh_requested = true;
		}
		spin_unlock(&port->lock);
	}

	DBGISR(("slgt_interrupt irq=%d exit\n", info->irq_level));
	return IRQ_HANDLED;
}

static int startup(struct slgt_info *info)
{
	DBGINFO(("%s startup\n", info->device_name));

	if (info->port.flags & ASYNC_INITIALIZED)
		return 0;

	if (!info->tx_buf) {
		info->tx_buf = kmalloc(info->max_frame_size, GFP_KERNEL);
		if (!info->tx_buf) {
			DBGERR(("%s can't allocate tx buffer\n", info->device_name));
			return -ENOMEM;
		}
	}

	info->pending_bh = 0;

	memset(&info->icount, 0, sizeof(info->icount));

	
	change_params(info);

	if (info->port.tty)
		clear_bit(TTY_IO_ERROR, &info->port.tty->flags);

	info->port.flags |= ASYNC_INITIALIZED;

	return 0;
}

static void shutdown(struct slgt_info *info)
{
	unsigned long flags;

	if (!(info->port.flags & ASYNC_INITIALIZED))
		return;

	DBGINFO(("%s shutdown\n", info->device_name));

	
	
	wake_up_interruptible(&info->status_event_wait_q);
	wake_up_interruptible(&info->event_wait_q);

	del_timer_sync(&info->tx_timer);
	del_timer_sync(&info->rx_timer);

	kfree(info->tx_buf);
	info->tx_buf = NULL;

	spin_lock_irqsave(&info->lock,flags);

	tx_stop(info);
	rx_stop(info);

	slgt_irq_off(info, IRQ_ALL | IRQ_MASTER);

 	if (!info->port.tty || info->port.tty->termios->c_cflag & HUPCL) {
 		info->signals &= ~(SerialSignal_DTR + SerialSignal_RTS);
		set_signals(info);
	}

	flush_cond_wait(&info->gpio_wait_q);

	spin_unlock_irqrestore(&info->lock,flags);

	if (info->port.tty)
		set_bit(TTY_IO_ERROR, &info->port.tty->flags);

	info->port.flags &= ~ASYNC_INITIALIZED;
}

static void program_hw(struct slgt_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);

	rx_stop(info);
	tx_stop(info);

	if (info->params.mode != MGSL_MODE_ASYNC ||
	    info->netcount)
		sync_mode(info);
	else
		async_mode(info);

	set_signals(info);

	info->dcd_chkcount = 0;
	info->cts_chkcount = 0;
	info->ri_chkcount = 0;
	info->dsr_chkcount = 0;

	slgt_irq_on(info, IRQ_DCD | IRQ_CTS | IRQ_DSR | IRQ_RI);
	get_signals(info);

	if (info->netcount ||
	    (info->port.tty && info->port.tty->termios->c_cflag & CREAD))
		rx_start(info);

	spin_unlock_irqrestore(&info->lock,flags);
}

static void change_params(struct slgt_info *info)
{
	unsigned cflag;
	int bits_per_char;

	if (!info->port.tty || !info->port.tty->termios)
		return;
	DBGINFO(("%s change_params\n", info->device_name));

	cflag = info->port.tty->termios->c_cflag;

	
	
 	if (cflag & CBAUD)
		info->signals |= SerialSignal_RTS + SerialSignal_DTR;
	else
		info->signals &= ~(SerialSignal_RTS + SerialSignal_DTR);

	

	switch (cflag & CSIZE) {
	case CS5: info->params.data_bits = 5; break;
	case CS6: info->params.data_bits = 6; break;
	case CS7: info->params.data_bits = 7; break;
	case CS8: info->params.data_bits = 8; break;
	default:  info->params.data_bits = 7; break;
	}

	info->params.stop_bits = (cflag & CSTOPB) ? 2 : 1;

	if (cflag & PARENB)
		info->params.parity = (cflag & PARODD) ? ASYNC_PARITY_ODD : ASYNC_PARITY_EVEN;
	else
		info->params.parity = ASYNC_PARITY_NONE;

	bits_per_char = info->params.data_bits +
			info->params.stop_bits + 1;

	info->params.data_rate = tty_get_baud_rate(info->port.tty);

	if (info->params.data_rate) {
		info->timeout = (32*HZ*bits_per_char) /
				info->params.data_rate;
	}
	info->timeout += HZ/50;		

	if (cflag & CRTSCTS)
		info->port.flags |= ASYNC_CTS_FLOW;
	else
		info->port.flags &= ~ASYNC_CTS_FLOW;

	if (cflag & CLOCAL)
		info->port.flags &= ~ASYNC_CHECK_CD;
	else
		info->port.flags |= ASYNC_CHECK_CD;

	

	info->read_status_mask = IRQ_RXOVER;
	if (I_INPCK(info->port.tty))
		info->read_status_mask |= MASK_PARITY | MASK_FRAMING;
 	if (I_BRKINT(info->port.tty) || I_PARMRK(info->port.tty))
 		info->read_status_mask |= MASK_BREAK;
	if (I_IGNPAR(info->port.tty))
		info->ignore_status_mask |= MASK_PARITY | MASK_FRAMING;
	if (I_IGNBRK(info->port.tty)) {
		info->ignore_status_mask |= MASK_BREAK;
		if (I_IGNPAR(info->port.tty))
			info->ignore_status_mask |= MASK_OVERRUN;
	}

	program_hw(info);
}

static int get_stats(struct slgt_info *info, struct mgsl_icount __user *user_icount)
{
	DBGINFO(("%s get_stats\n",  info->device_name));
	if (!user_icount) {
		memset(&info->icount, 0, sizeof(info->icount));
	} else {
		if (copy_to_user(user_icount, &info->icount, sizeof(struct mgsl_icount)))
			return -EFAULT;
	}
	return 0;
}

static int get_params(struct slgt_info *info, MGSL_PARAMS __user *user_params)
{
	DBGINFO(("%s get_params\n", info->device_name));
	if (copy_to_user(user_params, &info->params, sizeof(MGSL_PARAMS)))
		return -EFAULT;
	return 0;
}

static int set_params(struct slgt_info *info, MGSL_PARAMS __user *new_params)
{
 	unsigned long flags;
	MGSL_PARAMS tmp_params;

	DBGINFO(("%s set_params\n", info->device_name));
	if (copy_from_user(&tmp_params, new_params, sizeof(MGSL_PARAMS)))
		return -EFAULT;

	spin_lock_irqsave(&info->lock, flags);
	if (tmp_params.mode == MGSL_MODE_BASE_CLOCK)
		info->base_clock = tmp_params.clock_speed;
	else
		memcpy(&info->params, &tmp_params, sizeof(MGSL_PARAMS));
	spin_unlock_irqrestore(&info->lock, flags);

	program_hw(info);

	return 0;
}

static int get_txidle(struct slgt_info *info, int __user *idle_mode)
{
	DBGINFO(("%s get_txidle=%d\n", info->device_name, info->idle_mode));
	if (put_user(info->idle_mode, idle_mode))
		return -EFAULT;
	return 0;
}

static int set_txidle(struct slgt_info *info, int idle_mode)
{
 	unsigned long flags;
	DBGINFO(("%s set_txidle(%d)\n", info->device_name, idle_mode));
	spin_lock_irqsave(&info->lock,flags);
	info->idle_mode = idle_mode;
	if (info->params.mode != MGSL_MODE_ASYNC)
		tx_set_idle(info);
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int tx_enable(struct slgt_info *info, int enable)
{
 	unsigned long flags;
	DBGINFO(("%s tx_enable(%d)\n", info->device_name, enable));
	spin_lock_irqsave(&info->lock,flags);
	if (enable) {
		if (!info->tx_enabled)
			tx_start(info);
	} else {
		if (info->tx_enabled)
			tx_stop(info);
	}
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int tx_abort(struct slgt_info *info)
{
 	unsigned long flags;
	DBGINFO(("%s tx_abort\n", info->device_name));
	spin_lock_irqsave(&info->lock,flags);
	tdma_reset(info);
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int rx_enable(struct slgt_info *info, int enable)
{
 	unsigned long flags;
	unsigned int rbuf_fill_level;
	DBGINFO(("%s rx_enable(%08x)\n", info->device_name, enable));
	spin_lock_irqsave(&info->lock,flags);
	rbuf_fill_level = ((unsigned int)enable) >> 16;
	if (rbuf_fill_level) {
		if ((rbuf_fill_level > DMABUFSIZE) || (rbuf_fill_level % 4)) {
			spin_unlock_irqrestore(&info->lock, flags);
			return -EINVAL;
		}
		info->rbuf_fill_level = rbuf_fill_level;
		if (rbuf_fill_level < 128)
			info->rx_pio = 1; 
		else
			info->rx_pio = 0; 
		rx_stop(info); 
	}

	enable &= 3;
	if (enable) {
		if (!info->rx_enabled)
			rx_start(info);
		else if (enable == 2) {
			
			wr_reg16(info, RCR, rd_reg16(info, RCR) | BIT3);
		}
	} else {
		if (info->rx_enabled)
			rx_stop(info);
	}
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int wait_mgsl_event(struct slgt_info *info, int __user *mask_ptr)
{
 	unsigned long flags;
	int s;
	int rc=0;
	struct mgsl_icount cprev, cnow;
	int events;
	int mask;
	struct	_input_signal_events oldsigs, newsigs;
	DECLARE_WAITQUEUE(wait, current);

	if (get_user(mask, mask_ptr))
		return -EFAULT;

	DBGINFO(("%s wait_mgsl_event(%d)\n", info->device_name, mask));

	spin_lock_irqsave(&info->lock,flags);

	
	get_signals(info);
	s = info->signals;

	events = mask &
		( ((s & SerialSignal_DSR) ? MgslEvent_DsrActive:MgslEvent_DsrInactive) +
 		  ((s & SerialSignal_DCD) ? MgslEvent_DcdActive:MgslEvent_DcdInactive) +
		  ((s & SerialSignal_CTS) ? MgslEvent_CtsActive:MgslEvent_CtsInactive) +
		  ((s & SerialSignal_RI)  ? MgslEvent_RiActive :MgslEvent_RiInactive) );
	if (events) {
		spin_unlock_irqrestore(&info->lock,flags);
		goto exit;
	}

	
	cprev = info->icount;
	oldsigs = info->input_signal_events;

	
	if (mask & (MgslEvent_ExitHuntMode+MgslEvent_IdleReceived)) {
		unsigned short val = rd_reg16(info, SCR);
		if (!(val & IRQ_RXIDLE))
			wr_reg16(info, SCR, (unsigned short)(val | IRQ_RXIDLE));
	}

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&info->event_wait_q, &wait);

	spin_unlock_irqrestore(&info->lock,flags);

	for(;;) {
		schedule();
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}

		
		spin_lock_irqsave(&info->lock,flags);
		cnow = info->icount;
		newsigs = info->input_signal_events;
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&info->lock,flags);

		
		if (newsigs.dsr_up   == oldsigs.dsr_up   &&
		    newsigs.dsr_down == oldsigs.dsr_down &&
		    newsigs.dcd_up   == oldsigs.dcd_up   &&
		    newsigs.dcd_down == oldsigs.dcd_down &&
		    newsigs.cts_up   == oldsigs.cts_up   &&
		    newsigs.cts_down == oldsigs.cts_down &&
		    newsigs.ri_up    == oldsigs.ri_up    &&
		    newsigs.ri_down  == oldsigs.ri_down  &&
		    cnow.exithunt    == cprev.exithunt   &&
		    cnow.rxidle      == cprev.rxidle) {
			rc = -EIO;
			break;
		}

		events = mask &
			( (newsigs.dsr_up   != oldsigs.dsr_up   ? MgslEvent_DsrActive:0)   +
			  (newsigs.dsr_down != oldsigs.dsr_down ? MgslEvent_DsrInactive:0) +
			  (newsigs.dcd_up   != oldsigs.dcd_up   ? MgslEvent_DcdActive:0)   +
			  (newsigs.dcd_down != oldsigs.dcd_down ? MgslEvent_DcdInactive:0) +
			  (newsigs.cts_up   != oldsigs.cts_up   ? MgslEvent_CtsActive:0)   +
			  (newsigs.cts_down != oldsigs.cts_down ? MgslEvent_CtsInactive:0) +
			  (newsigs.ri_up    != oldsigs.ri_up    ? MgslEvent_RiActive:0)    +
			  (newsigs.ri_down  != oldsigs.ri_down  ? MgslEvent_RiInactive:0)  +
			  (cnow.exithunt    != cprev.exithunt   ? MgslEvent_ExitHuntMode:0) +
			  (cnow.rxidle      != cprev.rxidle     ? MgslEvent_IdleReceived:0) );
		if (events)
			break;

		cprev = cnow;
		oldsigs = newsigs;
	}

	remove_wait_queue(&info->event_wait_q, &wait);
	set_current_state(TASK_RUNNING);


	if (mask & (MgslEvent_ExitHuntMode + MgslEvent_IdleReceived)) {
		spin_lock_irqsave(&info->lock,flags);
		if (!waitqueue_active(&info->event_wait_q)) {
			
			wr_reg16(info, SCR,
				(unsigned short)(rd_reg16(info, SCR) & ~IRQ_RXIDLE));
		}
		spin_unlock_irqrestore(&info->lock,flags);
	}
exit:
	if (rc == 0)
		rc = put_user(events, mask_ptr);
	return rc;
}

static int get_interface(struct slgt_info *info, int __user *if_mode)
{
	DBGINFO(("%s get_interface=%x\n", info->device_name, info->if_mode));
	if (put_user(info->if_mode, if_mode))
		return -EFAULT;
	return 0;
}

static int set_interface(struct slgt_info *info, int if_mode)
{
 	unsigned long flags;
	unsigned short val;

	DBGINFO(("%s set_interface=%x)\n", info->device_name, if_mode));
	spin_lock_irqsave(&info->lock,flags);
	info->if_mode = if_mode;

	msc_set_vcr(info);

	
	val = rd_reg16(info, TCR);
	if (info->if_mode & MGSL_INTERFACE_RTS_EN)
		val |= BIT7;
	else
		val &= ~BIT7;
	wr_reg16(info, TCR, val);

	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int get_xsync(struct slgt_info *info, int __user *xsync)
{
	DBGINFO(("%s get_xsync=%x\n", info->device_name, info->xsync));
	if (put_user(info->xsync, xsync))
		return -EFAULT;
	return 0;
}

static int set_xsync(struct slgt_info *info, int xsync)
{
	unsigned long flags;

	DBGINFO(("%s set_xsync=%x)\n", info->device_name, xsync));
	spin_lock_irqsave(&info->lock, flags);
	info->xsync = xsync;
	wr_reg32(info, XSR, xsync);
	spin_unlock_irqrestore(&info->lock, flags);
	return 0;
}

static int get_xctrl(struct slgt_info *info, int __user *xctrl)
{
	DBGINFO(("%s get_xctrl=%x\n", info->device_name, info->xctrl));
	if (put_user(info->xctrl, xctrl))
		return -EFAULT;
	return 0;
}

static int set_xctrl(struct slgt_info *info, int xctrl)
{
	unsigned long flags;

	DBGINFO(("%s set_xctrl=%x)\n", info->device_name, xctrl));
	spin_lock_irqsave(&info->lock, flags);
	info->xctrl = xctrl;
	wr_reg32(info, XCR, xctrl);
	spin_unlock_irqrestore(&info->lock, flags);
	return 0;
}

static int set_gpio(struct slgt_info *info, struct gpio_desc __user *user_gpio)
{
 	unsigned long flags;
	struct gpio_desc gpio;
	__u32 data;

	if (!info->gpio_present)
		return -EINVAL;
	if (copy_from_user(&gpio, user_gpio, sizeof(gpio)))
		return -EFAULT;
	DBGINFO(("%s set_gpio state=%08x smask=%08x dir=%08x dmask=%08x\n",
		 info->device_name, gpio.state, gpio.smask,
		 gpio.dir, gpio.dmask));

	spin_lock_irqsave(&info->port_array[0]->lock, flags);
	if (gpio.dmask) {
		data = rd_reg32(info, IODR);
		data |= gpio.dmask & gpio.dir;
		data &= ~(gpio.dmask & ~gpio.dir);
		wr_reg32(info, IODR, data);
	}
	if (gpio.smask) {
		data = rd_reg32(info, IOVR);
		data |= gpio.smask & gpio.state;
		data &= ~(gpio.smask & ~gpio.state);
		wr_reg32(info, IOVR, data);
	}
	spin_unlock_irqrestore(&info->port_array[0]->lock, flags);

	return 0;
}

static int get_gpio(struct slgt_info *info, struct gpio_desc __user *user_gpio)
{
	struct gpio_desc gpio;
	if (!info->gpio_present)
		return -EINVAL;
	gpio.state = rd_reg32(info, IOVR);
	gpio.smask = 0xffffffff;
	gpio.dir   = rd_reg32(info, IODR);
	gpio.dmask = 0xffffffff;
	if (copy_to_user(user_gpio, &gpio, sizeof(gpio)))
		return -EFAULT;
	DBGINFO(("%s get_gpio state=%08x dir=%08x\n",
		 info->device_name, gpio.state, gpio.dir));
	return 0;
}

static void init_cond_wait(struct cond_wait *w, unsigned int data)
{
	init_waitqueue_head(&w->q);
	init_waitqueue_entry(&w->wait, current);
	w->data = data;
}

static void add_cond_wait(struct cond_wait **head, struct cond_wait *w)
{
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&w->q, &w->wait);
	w->next = *head;
	*head = w;
}

static void remove_cond_wait(struct cond_wait **head, struct cond_wait *cw)
{
	struct cond_wait *w, *prev;
	remove_wait_queue(&cw->q, &cw->wait);
	set_current_state(TASK_RUNNING);
	for (w = *head, prev = NULL ; w != NULL ; prev = w, w = w->next) {
		if (w == cw) {
			if (prev != NULL)
				prev->next = w->next;
			else
				*head = w->next;
			break;
		}
	}
}

static void flush_cond_wait(struct cond_wait **head)
{
	while (*head != NULL) {
		wake_up_interruptible(&(*head)->q);
		*head = (*head)->next;
	}
}

static int wait_gpio(struct slgt_info *info, struct gpio_desc __user *user_gpio)
{
 	unsigned long flags;
	int rc = 0;
	struct gpio_desc gpio;
	struct cond_wait wait;
	u32 state;

	if (!info->gpio_present)
		return -EINVAL;
	if (copy_from_user(&gpio, user_gpio, sizeof(gpio)))
		return -EFAULT;
	DBGINFO(("%s wait_gpio() state=%08x smask=%08x\n",
		 info->device_name, gpio.state, gpio.smask));
	
	if ((gpio.smask &= ~rd_reg32(info, IODR)) == 0)
		return -EINVAL;
	init_cond_wait(&wait, gpio.smask);

	spin_lock_irqsave(&info->port_array[0]->lock, flags);
	
	wr_reg32(info, IOER, rd_reg32(info, IOER) | gpio.smask);
	
	state = rd_reg32(info, IOVR);

	if (gpio.smask & ~(state ^ gpio.state)) {
		
		gpio.state = state;
	} else {
		
		add_cond_wait(&info->gpio_wait_q, &wait);
		spin_unlock_irqrestore(&info->port_array[0]->lock, flags);
		schedule();
		if (signal_pending(current))
			rc = -ERESTARTSYS;
		else
			gpio.state = wait.data;
		spin_lock_irqsave(&info->port_array[0]->lock, flags);
		remove_cond_wait(&info->gpio_wait_q, &wait);
	}

	
	if (info->gpio_wait_q == NULL)
		wr_reg32(info, IOER, 0);
	spin_unlock_irqrestore(&info->port_array[0]->lock, flags);

	if ((rc == 0) && copy_to_user(user_gpio, &gpio, sizeof(gpio)))
		rc = -EFAULT;
	return rc;
}

static int modem_input_wait(struct slgt_info *info,int arg)
{
 	unsigned long flags;
	int rc;
	struct mgsl_icount cprev, cnow;
	DECLARE_WAITQUEUE(wait, current);

	
	spin_lock_irqsave(&info->lock,flags);
	cprev = info->icount;
	add_wait_queue(&info->status_event_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	spin_unlock_irqrestore(&info->lock,flags);

	for(;;) {
		schedule();
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}

		
		spin_lock_irqsave(&info->lock,flags);
		cnow = info->icount;
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&info->lock,flags);

		
		if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
		    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts) {
			rc = -EIO;
			break;
		}

		
		if ((arg & TIOCM_RNG && cnow.rng != cprev.rng) ||
		    (arg & TIOCM_DSR && cnow.dsr != cprev.dsr) ||
		    (arg & TIOCM_CD  && cnow.dcd != cprev.dcd) ||
		    (arg & TIOCM_CTS && cnow.cts != cprev.cts)) {
			rc = 0;
			break;
		}

		cprev = cnow;
	}
	remove_wait_queue(&info->status_event_wait_q, &wait);
	set_current_state(TASK_RUNNING);
	return rc;
}

static int tiocmget(struct tty_struct *tty)
{
	struct slgt_info *info = tty->driver_data;
	unsigned int result;
 	unsigned long flags;

	spin_lock_irqsave(&info->lock,flags);
 	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);

	result = ((info->signals & SerialSignal_RTS) ? TIOCM_RTS:0) +
		((info->signals & SerialSignal_DTR) ? TIOCM_DTR:0) +
		((info->signals & SerialSignal_DCD) ? TIOCM_CAR:0) +
		((info->signals & SerialSignal_RI)  ? TIOCM_RNG:0) +
		((info->signals & SerialSignal_DSR) ? TIOCM_DSR:0) +
		((info->signals & SerialSignal_CTS) ? TIOCM_CTS:0);

	DBGINFO(("%s tiocmget value=%08X\n", info->device_name, result));
	return result;
}

static int tiocmset(struct tty_struct *tty,
		    unsigned int set, unsigned int clear)
{
	struct slgt_info *info = tty->driver_data;
 	unsigned long flags;

	DBGINFO(("%s tiocmset(%x,%x)\n", info->device_name, set, clear));

	if (set & TIOCM_RTS)
		info->signals |= SerialSignal_RTS;
	if (set & TIOCM_DTR)
		info->signals |= SerialSignal_DTR;
	if (clear & TIOCM_RTS)
		info->signals &= ~SerialSignal_RTS;
	if (clear & TIOCM_DTR)
		info->signals &= ~SerialSignal_DTR;

	spin_lock_irqsave(&info->lock,flags);
 	set_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);
	return 0;
}

static int carrier_raised(struct tty_port *port)
{
	unsigned long flags;
	struct slgt_info *info = container_of(port, struct slgt_info, port);

	spin_lock_irqsave(&info->lock,flags);
 	get_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);
	return (info->signals & SerialSignal_DCD) ? 1 : 0;
}

static void dtr_rts(struct tty_port *port, int on)
{
	unsigned long flags;
	struct slgt_info *info = container_of(port, struct slgt_info, port);

	spin_lock_irqsave(&info->lock,flags);
	if (on)
		info->signals |= SerialSignal_RTS + SerialSignal_DTR;
	else
		info->signals &= ~(SerialSignal_RTS + SerialSignal_DTR);
 	set_signals(info);
	spin_unlock_irqrestore(&info->lock,flags);
}


static int block_til_ready(struct tty_struct *tty, struct file *filp,
			   struct slgt_info *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int		retval;
	bool		do_clocal = false;
	bool		extra_count = false;
	unsigned long	flags;
	int		cd;
	struct tty_port *port = &info->port;

	DBGINFO(("%s block_til_ready\n", tty->driver->name));

	if (filp->f_flags & O_NONBLOCK || tty->flags & (1 << TTY_IO_ERROR)){
		
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = true;


	retval = 0;
	add_wait_queue(&port->open_wait, &wait);

	spin_lock_irqsave(&info->lock, flags);
	if (!tty_hung_up_p(filp)) {
		extra_count = true;
		port->count--;
	}
	spin_unlock_irqrestore(&info->lock, flags);
	port->blocked_open++;

	while (1) {
		if ((tty->termios->c_cflag & CBAUD))
			tty_port_raise_dtr_rts(port);

		set_current_state(TASK_INTERRUPTIBLE);

		if (tty_hung_up_p(filp) || !(port->flags & ASYNC_INITIALIZED)){
			retval = (port->flags & ASYNC_HUP_NOTIFY) ?
					-EAGAIN : -ERESTARTSYS;
			break;
		}

		cd = tty_port_carrier_raised(port);

 		if (!(port->flags & ASYNC_CLOSING) && (do_clocal || cd ))
 			break;

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}

		DBGINFO(("%s block_til_ready wait\n", tty->driver->name));
		tty_unlock();
		schedule();
		tty_lock();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->open_wait, &wait);

	if (extra_count)
		port->count++;
	port->blocked_open--;

	if (!retval)
		port->flags |= ASYNC_NORMAL_ACTIVE;

	DBGINFO(("%s block_til_ready ready, rc=%d\n", tty->driver->name, retval));
	return retval;
}

static int alloc_tmp_rbuf(struct slgt_info *info)
{
	info->tmp_rbuf = kmalloc(info->max_frame_size + 5, GFP_KERNEL);
	if (info->tmp_rbuf == NULL)
		return -ENOMEM;
	return 0;
}

static void free_tmp_rbuf(struct slgt_info *info)
{
	kfree(info->tmp_rbuf);
	info->tmp_rbuf = NULL;
}

static int alloc_desc(struct slgt_info *info)
{
	unsigned int i;
	unsigned int pbufs;

	
	info->bufs = pci_alloc_consistent(info->pdev, DESC_LIST_SIZE, &info->bufs_dma_addr);
	if (info->bufs == NULL)
		return -ENOMEM;

	memset(info->bufs, 0, DESC_LIST_SIZE);

	info->rbufs = (struct slgt_desc*)info->bufs;
	info->tbufs = ((struct slgt_desc*)info->bufs) + info->rbuf_count;

	pbufs = (unsigned int)info->bufs_dma_addr;


	for (i=0; i < info->rbuf_count; i++) {
		
		info->rbufs[i].pdesc = pbufs + (i * sizeof(struct slgt_desc));

		
		if (i == info->rbuf_count - 1)
			info->rbufs[i].next = cpu_to_le32(pbufs);
		else
			info->rbufs[i].next = cpu_to_le32(pbufs + ((i+1) * sizeof(struct slgt_desc)));
		set_desc_count(info->rbufs[i], DMABUFSIZE);
	}

	for (i=0; i < info->tbuf_count; i++) {
		
		info->tbufs[i].pdesc = pbufs + ((info->rbuf_count + i) * sizeof(struct slgt_desc));

		
		if (i == info->tbuf_count - 1)
			info->tbufs[i].next = cpu_to_le32(pbufs + info->rbuf_count * sizeof(struct slgt_desc));
		else
			info->tbufs[i].next = cpu_to_le32(pbufs + ((info->rbuf_count + i + 1) * sizeof(struct slgt_desc)));
	}

	return 0;
}

static void free_desc(struct slgt_info *info)
{
	if (info->bufs != NULL) {
		pci_free_consistent(info->pdev, DESC_LIST_SIZE, info->bufs, info->bufs_dma_addr);
		info->bufs  = NULL;
		info->rbufs = NULL;
		info->tbufs = NULL;
	}
}

static int alloc_bufs(struct slgt_info *info, struct slgt_desc *bufs, int count)
{
	int i;
	for (i=0; i < count; i++) {
		if ((bufs[i].buf = pci_alloc_consistent(info->pdev, DMABUFSIZE, &bufs[i].buf_dma_addr)) == NULL)
			return -ENOMEM;
		bufs[i].pbuf  = cpu_to_le32((unsigned int)bufs[i].buf_dma_addr);
	}
	return 0;
}

static void free_bufs(struct slgt_info *info, struct slgt_desc *bufs, int count)
{
	int i;
	for (i=0; i < count; i++) {
		if (bufs[i].buf == NULL)
			continue;
		pci_free_consistent(info->pdev, DMABUFSIZE, bufs[i].buf, bufs[i].buf_dma_addr);
		bufs[i].buf = NULL;
	}
}

static int alloc_dma_bufs(struct slgt_info *info)
{
	info->rbuf_count = 32;
	info->tbuf_count = 32;

	if (alloc_desc(info) < 0 ||
	    alloc_bufs(info, info->rbufs, info->rbuf_count) < 0 ||
	    alloc_bufs(info, info->tbufs, info->tbuf_count) < 0 ||
	    alloc_tmp_rbuf(info) < 0) {
		DBGERR(("%s DMA buffer alloc fail\n", info->device_name));
		return -ENOMEM;
	}
	reset_rbufs(info);
	return 0;
}

static void free_dma_bufs(struct slgt_info *info)
{
	if (info->bufs) {
		free_bufs(info, info->rbufs, info->rbuf_count);
		free_bufs(info, info->tbufs, info->tbuf_count);
		free_desc(info);
	}
	free_tmp_rbuf(info);
}

static int claim_resources(struct slgt_info *info)
{
	if (request_mem_region(info->phys_reg_addr, SLGT_REG_SIZE, "synclink_gt") == NULL) {
		DBGERR(("%s reg addr conflict, addr=%08X\n",
			info->device_name, info->phys_reg_addr));
		info->init_error = DiagStatus_AddressConflict;
		goto errout;
	}
	else
		info->reg_addr_requested = true;

	info->reg_addr = ioremap_nocache(info->phys_reg_addr, SLGT_REG_SIZE);
	if (!info->reg_addr) {
		DBGERR(("%s can't map device registers, addr=%08X\n",
			info->device_name, info->phys_reg_addr));
		info->init_error = DiagStatus_CantAssignPciResources;
		goto errout;
	}
	return 0;

errout:
	release_resources(info);
	return -ENODEV;
}

static void release_resources(struct slgt_info *info)
{
	if (info->irq_requested) {
		free_irq(info->irq_level, info);
		info->irq_requested = false;
	}

	if (info->reg_addr_requested) {
		release_mem_region(info->phys_reg_addr, SLGT_REG_SIZE);
		info->reg_addr_requested = false;
	}

	if (info->reg_addr) {
		iounmap(info->reg_addr);
		info->reg_addr = NULL;
	}
}

static void add_device(struct slgt_info *info)
{
	char *devstr;

	info->next_device = NULL;
	info->line = slgt_device_count;
	sprintf(info->device_name, "%s%d", tty_dev_prefix, info->line);

	if (info->line < MAX_DEVICES) {
		if (maxframe[info->line])
			info->max_frame_size = maxframe[info->line];
	}

	slgt_device_count++;

	if (!slgt_device_list)
		slgt_device_list = info;
	else {
		struct slgt_info *current_dev = slgt_device_list;
		while(current_dev->next_device)
			current_dev = current_dev->next_device;
		current_dev->next_device = info;
	}

	if (info->max_frame_size < 4096)
		info->max_frame_size = 4096;
	else if (info->max_frame_size > 65535)
		info->max_frame_size = 65535;

	switch(info->pdev->device) {
	case SYNCLINK_GT_DEVICE_ID:
		devstr = "GT";
		break;
	case SYNCLINK_GT2_DEVICE_ID:
		devstr = "GT2";
		break;
	case SYNCLINK_GT4_DEVICE_ID:
		devstr = "GT4";
		break;
	case SYNCLINK_AC_DEVICE_ID:
		devstr = "AC";
		info->params.mode = MGSL_MODE_ASYNC;
		break;
	default:
		devstr = "(unknown model)";
	}
	printk("SyncLink %s %s IO=%08x IRQ=%d MaxFrameSize=%u\n",
		devstr, info->device_name, info->phys_reg_addr,
		info->irq_level, info->max_frame_size);

#if SYNCLINK_GENERIC_HDLC
	hdlcdev_init(info);
#endif
}

static const struct tty_port_operations slgt_port_ops = {
	.carrier_raised = carrier_raised,
	.dtr_rts = dtr_rts,
};

static struct slgt_info *alloc_dev(int adapter_num, int port_num, struct pci_dev *pdev)
{
	struct slgt_info *info;

	info = kzalloc(sizeof(struct slgt_info), GFP_KERNEL);

	if (!info) {
		DBGERR(("%s device alloc failed adapter=%d port=%d\n",
			driver_name, adapter_num, port_num));
	} else {
		tty_port_init(&info->port);
		info->port.ops = &slgt_port_ops;
		info->magic = MGSL_MAGIC;
		INIT_WORK(&info->task, bh_handler);
		info->max_frame_size = 4096;
		info->base_clock = 14745600;
		info->rbuf_fill_level = DMABUFSIZE;
		info->port.close_delay = 5*HZ/10;
		info->port.closing_wait = 30*HZ;
		init_waitqueue_head(&info->status_event_wait_q);
		init_waitqueue_head(&info->event_wait_q);
		spin_lock_init(&info->netlock);
		memcpy(&info->params,&default_params,sizeof(MGSL_PARAMS));
		info->idle_mode = HDLC_TXIDLE_FLAGS;
		info->adapter_num = adapter_num;
		info->port_num = port_num;

		setup_timer(&info->tx_timer, tx_timeout, (unsigned long)info);
		setup_timer(&info->rx_timer, rx_timeout, (unsigned long)info);

		
		info->pdev = pdev;
		info->irq_level = pdev->irq;
		info->phys_reg_addr = pci_resource_start(pdev,0);

		info->bus_type = MGSL_BUS_TYPE_PCI;
		info->irq_flags = IRQF_SHARED;

		info->init_error = -1; 
	}

	return info;
}

static void device_init(int adapter_num, struct pci_dev *pdev)
{
	struct slgt_info *port_array[SLGT_MAX_PORTS];
	int i;
	int port_count = 1;

	if (pdev->device == SYNCLINK_GT2_DEVICE_ID)
		port_count = 2;
	else if (pdev->device == SYNCLINK_GT4_DEVICE_ID)
		port_count = 4;

	
	for (i=0; i < port_count; ++i) {
		port_array[i] = alloc_dev(adapter_num, i, pdev);
		if (port_array[i] == NULL) {
			for (--i; i >= 0; --i)
				kfree(port_array[i]);
			return;
		}
	}

	
	for (i=0; i < port_count; ++i) {
		memcpy(port_array[i]->port_array, port_array, sizeof(port_array));
		add_device(port_array[i]);
		port_array[i]->port_count = port_count;
		spin_lock_init(&port_array[i]->lock);
	}

	
	if (!claim_resources(port_array[0])) {

		alloc_dma_bufs(port_array[0]);

		
		for (i = 1; i < port_count; ++i) {
			port_array[i]->irq_level = port_array[0]->irq_level;
			port_array[i]->reg_addr  = port_array[0]->reg_addr;
			alloc_dma_bufs(port_array[i]);
		}

		if (request_irq(port_array[0]->irq_level,
					slgt_interrupt,
					port_array[0]->irq_flags,
					port_array[0]->device_name,
					port_array[0]) < 0) {
			DBGERR(("%s request_irq failed IRQ=%d\n",
				port_array[0]->device_name,
				port_array[0]->irq_level));
		} else {
			port_array[0]->irq_requested = true;
			adapter_test(port_array[0]);
			for (i=1 ; i < port_count ; i++) {
				port_array[i]->init_error = port_array[0]->init_error;
				port_array[i]->gpio_present = port_array[0]->gpio_present;
			}
		}
	}

	for (i=0; i < port_count; ++i)
		tty_register_device(serial_driver, port_array[i]->line, &(port_array[i]->pdev->dev));
}

static int __devinit init_one(struct pci_dev *dev,
			      const struct pci_device_id *ent)
{
	if (pci_enable_device(dev)) {
		printk("error enabling pci device %p\n", dev);
		return -EIO;
	}
	pci_set_master(dev);
	device_init(slgt_device_count, dev);
	return 0;
}

static void __devexit remove_one(struct pci_dev *dev)
{
}

static const struct tty_operations ops = {
	.open = open,
	.close = close,
	.write = write,
	.put_char = put_char,
	.flush_chars = flush_chars,
	.write_room = write_room,
	.chars_in_buffer = chars_in_buffer,
	.flush_buffer = flush_buffer,
	.ioctl = ioctl,
	.compat_ioctl = slgt_compat_ioctl,
	.throttle = throttle,
	.unthrottle = unthrottle,
	.send_xchar = send_xchar,
	.break_ctl = set_break,
	.wait_until_sent = wait_until_sent,
	.set_termios = set_termios,
	.stop = tx_hold,
	.start = tx_release,
	.hangup = hangup,
	.tiocmget = tiocmget,
	.tiocmset = tiocmset,
	.get_icount = get_icount,
	.proc_fops = &synclink_gt_proc_fops,
};

static void slgt_cleanup(void)
{
	int rc;
	struct slgt_info *info;
	struct slgt_info *tmp;

	printk(KERN_INFO "unload %s\n", driver_name);

	if (serial_driver) {
		for (info=slgt_device_list ; info != NULL ; info=info->next_device)
			tty_unregister_device(serial_driver, info->line);
		if ((rc = tty_unregister_driver(serial_driver)))
			DBGERR(("tty_unregister_driver error=%d\n", rc));
		put_tty_driver(serial_driver);
	}

	
	info = slgt_device_list;
	while(info) {
		reset_port(info);
		info = info->next_device;
	}

	
	info = slgt_device_list;
	while(info) {
#if SYNCLINK_GENERIC_HDLC
		hdlcdev_exit(info);
#endif
		free_dma_bufs(info);
		free_tmp_rbuf(info);
		if (info->port_num == 0)
			release_resources(info);
		tmp = info;
		info = info->next_device;
		kfree(tmp);
	}

	if (pci_registered)
		pci_unregister_driver(&pci_driver);
}

static int __init slgt_init(void)
{
	int rc;

	printk(KERN_INFO "%s\n", driver_name);

	serial_driver = alloc_tty_driver(MAX_DEVICES);
	if (!serial_driver) {
		printk("%s can't allocate tty driver\n", driver_name);
		return -ENOMEM;
	}

	

	serial_driver->driver_name = tty_driver_name;
	serial_driver->name = tty_dev_prefix;
	serial_driver->major = ttymajor;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->init_termios.c_ispeed = 9600;
	serial_driver->init_termios.c_ospeed = 9600;
	serial_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_set_operations(serial_driver, &ops);
	if ((rc = tty_register_driver(serial_driver)) < 0) {
		DBGERR(("%s can't register serial driver\n", driver_name));
		put_tty_driver(serial_driver);
		serial_driver = NULL;
		goto error;
	}

	printk(KERN_INFO "%s, tty major#%d\n",
	       driver_name, serial_driver->major);

	slgt_device_count = 0;
	if ((rc = pci_register_driver(&pci_driver)) < 0) {
		printk("%s pci_register_driver error=%d\n", driver_name, rc);
		goto error;
	}
	pci_registered = true;

	if (!slgt_device_list)
		printk("%s no devices found\n",driver_name);

	return 0;

error:
	slgt_cleanup();
	return rc;
}

static void __exit slgt_exit(void)
{
	slgt_cleanup();
}

module_init(slgt_init);
module_exit(slgt_exit);


#define CALC_REGADDR() \
	unsigned long reg_addr = ((unsigned long)info->reg_addr) + addr; \
	if (addr >= 0x80) \
		reg_addr += (info->port_num) * 32; \
	else if (addr >= 0x40)	\
		reg_addr += (info->port_num) * 16;

static __u8 rd_reg8(struct slgt_info *info, unsigned int addr)
{
	CALC_REGADDR();
	return readb((void __iomem *)reg_addr);
}

static void wr_reg8(struct slgt_info *info, unsigned int addr, __u8 value)
{
	CALC_REGADDR();
	writeb(value, (void __iomem *)reg_addr);
}

static __u16 rd_reg16(struct slgt_info *info, unsigned int addr)
{
	CALC_REGADDR();
	return readw((void __iomem *)reg_addr);
}

static void wr_reg16(struct slgt_info *info, unsigned int addr, __u16 value)
{
	CALC_REGADDR();
	writew(value, (void __iomem *)reg_addr);
}

static __u32 rd_reg32(struct slgt_info *info, unsigned int addr)
{
	CALC_REGADDR();
	return readl((void __iomem *)reg_addr);
}

static void wr_reg32(struct slgt_info *info, unsigned int addr, __u32 value)
{
	CALC_REGADDR();
	writel(value, (void __iomem *)reg_addr);
}

static void rdma_reset(struct slgt_info *info)
{
	unsigned int i;

	
	wr_reg32(info, RDCSR, BIT1);

	
	for(i=0 ; i < 1000 ; i++)
		if (!(rd_reg32(info, RDCSR) & BIT0))
			break;
}

static void tdma_reset(struct slgt_info *info)
{
	unsigned int i;

	
	wr_reg32(info, TDCSR, BIT1);

	
	for(i=0 ; i < 1000 ; i++)
		if (!(rd_reg32(info, TDCSR) & BIT0))
			break;
}

static void enable_loopback(struct slgt_info *info)
{
	
	wr_reg16(info, SCR, (unsigned short)(rd_reg16(info, SCR) | BIT2));

	if (info->params.mode != MGSL_MODE_ASYNC) {
		wr_reg8(info, CCR, 0x49);

		
		if (info->params.clock_speed)
			set_rate(info, info->params.clock_speed);
		else
			set_rate(info, 3686400);
	}
}

static void set_rate(struct slgt_info *info, u32 rate)
{
	unsigned int div;
	unsigned int osc = info->base_clock;


	if (rate) {
		div = osc/rate;
		if (!(osc % rate) && div)
			div--;
		wr_reg16(info, BDR, (unsigned short)div);
	}
}

static void rx_stop(struct slgt_info *info)
{
	unsigned short val;

	
	val = rd_reg16(info, RCR) & ~BIT1;          
	wr_reg16(info, RCR, (unsigned short)(val | BIT2)); 
	wr_reg16(info, RCR, val);                  

	slgt_irq_off(info, IRQ_RXOVER + IRQ_RXDATA + IRQ_RXIDLE);

	
	wr_reg16(info, SSR, IRQ_RXIDLE + IRQ_RXOVER);

	rdma_reset(info);

	info->rx_enabled = false;
	info->rx_restart = false;
}

static void rx_start(struct slgt_info *info)
{
	unsigned short val;

	slgt_irq_off(info, IRQ_RXOVER + IRQ_RXDATA);

	
	wr_reg16(info, SSR, IRQ_RXOVER);

	
	val = rd_reg16(info, RCR) & ~BIT1; 
	wr_reg16(info, RCR, (unsigned short)(val | BIT2)); 
	wr_reg16(info, RCR, val);                  

	rdma_reset(info);
	reset_rbufs(info);

	if (info->rx_pio) {
		
		wr_reg16(info, SCR, (unsigned short)(rd_reg16(info, SCR) & ~BIT14));
		slgt_irq_on(info, IRQ_RXDATA);
		if (info->params.mode == MGSL_MODE_ASYNC) {
			
			wr_reg32(info, RDCSR, BIT6);
		}
	} else {
		
		wr_reg16(info, SCR, (unsigned short)(rd_reg16(info, SCR) | BIT14));
		
		wr_reg32(info, RDDAR, info->rbufs[0].pdesc);

		if (info->params.mode != MGSL_MODE_ASYNC) {
			
			wr_reg32(info, RDCSR, (BIT2 + BIT0));
		} else {
			
			wr_reg32(info, RDCSR, (BIT6 + BIT2 + BIT0));
		}
	}

	slgt_irq_on(info, IRQ_RXOVER);

	
	wr_reg16(info, RCR, (unsigned short)(rd_reg16(info, RCR) | BIT1));

	info->rx_restart = false;
	info->rx_enabled = true;
}

static void tx_start(struct slgt_info *info)
{
	if (!info->tx_enabled) {
		wr_reg16(info, TCR,
			 (unsigned short)((rd_reg16(info, TCR) | BIT1) & ~BIT2));
		info->tx_enabled = true;
	}

	if (desc_count(info->tbufs[info->tbuf_start])) {
		info->drop_rts_on_tx_done = false;

		if (info->params.mode != MGSL_MODE_ASYNC) {
			if (info->params.flags & HDLC_FLAG_AUTO_RTS) {
				get_signals(info);
				if (!(info->signals & SerialSignal_RTS)) {
					info->signals |= SerialSignal_RTS;
					set_signals(info);
					info->drop_rts_on_tx_done = true;
				}
			}

			slgt_irq_off(info, IRQ_TXDATA);
			slgt_irq_on(info, IRQ_TXUNDER + IRQ_TXIDLE);
			
			wr_reg16(info, SSR, (unsigned short)(IRQ_TXIDLE + IRQ_TXUNDER));
		} else {
			slgt_irq_off(info, IRQ_TXDATA);
			slgt_irq_on(info, IRQ_TXIDLE);
			
			wr_reg16(info, SSR, IRQ_TXIDLE);
		}
		
		wr_reg32(info, TDDAR, info->tbufs[info->tbuf_start].pdesc);
		wr_reg32(info, TDCSR, BIT2 + BIT0);
		info->tx_active = true;
	}
}

static void tx_stop(struct slgt_info *info)
{
	unsigned short val;

	del_timer(&info->tx_timer);

	tdma_reset(info);

	
	val = rd_reg16(info, TCR) & ~BIT1;          
	wr_reg16(info, TCR, (unsigned short)(val | BIT2)); 

	slgt_irq_off(info, IRQ_TXDATA + IRQ_TXIDLE + IRQ_TXUNDER);

	
	wr_reg16(info, SSR, (unsigned short)(IRQ_TXIDLE + IRQ_TXUNDER));

	reset_tbufs(info);

	info->tx_enabled = false;
	info->tx_active = false;
}

static void reset_port(struct slgt_info *info)
{
	if (!info->reg_addr)
		return;

	tx_stop(info);
	rx_stop(info);

	info->signals &= ~(SerialSignal_DTR + SerialSignal_RTS);
	set_signals(info);

	slgt_irq_off(info, IRQ_ALL | IRQ_MASTER);
}

static void reset_adapter(struct slgt_info *info)
{
	int i;
	for (i=0; i < info->port_count; ++i) {
		if (info->port_array[i])
			reset_port(info->port_array[i]);
	}
}

static void async_mode(struct slgt_info *info)
{
  	unsigned short val;

	slgt_irq_off(info, IRQ_ALL | IRQ_MASTER);
	tx_stop(info);
	rx_stop(info);

	val = 0x4000;

	if (info->if_mode & MGSL_INTERFACE_RTS_EN)
		val |= BIT7;

	if (info->params.parity != ASYNC_PARITY_NONE) {
		val |= BIT9;
		if (info->params.parity == ASYNC_PARITY_ODD)
			val |= BIT8;
	}

	switch (info->params.data_bits)
	{
	case 6: val |= BIT4; break;
	case 7: val |= BIT5; break;
	case 8: val |= BIT5 + BIT4; break;
	}

	if (info->params.stop_bits != 1)
		val |= BIT3;

	if (info->params.flags & HDLC_FLAG_AUTO_CTS)
		val |= BIT0;

	wr_reg16(info, TCR, val);

	val = 0x4000;

	if (info->params.parity != ASYNC_PARITY_NONE) {
		val |= BIT9;
		if (info->params.parity == ASYNC_PARITY_ODD)
			val |= BIT8;
	}

	switch (info->params.data_bits)
	{
	case 6: val |= BIT4; break;
	case 7: val |= BIT5; break;
	case 8: val |= BIT5 + BIT4; break;
	}

	if (info->params.flags & HDLC_FLAG_AUTO_DCD)
		val |= BIT0;

	wr_reg16(info, RCR, val);

	wr_reg8(info, CCR, 0x69);

	msc_set_vcr(info);

	val = BIT15 + BIT14 + BIT0;
	
	if ((rd_reg32(info, JCR) & BIT8) && info->params.data_rate &&
	    ((info->base_clock < (info->params.data_rate * 16)) ||
	     (info->base_clock % (info->params.data_rate * 16)))) {
		
		val |= BIT3;
		set_rate(info, info->params.data_rate * 8);
	} else {
		
		set_rate(info, info->params.data_rate * 16);
	}
	wr_reg16(info, SCR, val);

	slgt_irq_on(info, IRQ_RXBREAK | IRQ_RXOVER);

	if (info->params.loopback)
		enable_loopback(info);
}

static void sync_mode(struct slgt_info *info)
{
	unsigned short val;

	slgt_irq_off(info, IRQ_ALL | IRQ_MASTER);
	tx_stop(info);
	rx_stop(info);

	val = BIT2;

	switch(info->params.mode) {
	case MGSL_MODE_XSYNC:
		val |= BIT15 + BIT13;
		break;
	case MGSL_MODE_MONOSYNC: val |= BIT14 + BIT13; break;
	case MGSL_MODE_BISYNC:   val |= BIT15; break;
	case MGSL_MODE_RAW:      val |= BIT13; break;
	}
	if (info->if_mode & MGSL_INTERFACE_RTS_EN)
		val |= BIT7;

	switch(info->params.encoding)
	{
	case HDLC_ENCODING_NRZB:          val |= BIT10; break;
	case HDLC_ENCODING_NRZI_MARK:     val |= BIT11; break;
	case HDLC_ENCODING_NRZI:          val |= BIT11 + BIT10; break;
	case HDLC_ENCODING_BIPHASE_MARK:  val |= BIT12; break;
	case HDLC_ENCODING_BIPHASE_SPACE: val |= BIT12 + BIT10; break;
	case HDLC_ENCODING_BIPHASE_LEVEL: val |= BIT12 + BIT11; break;
	case HDLC_ENCODING_DIFF_BIPHASE_LEVEL: val |= BIT12 + BIT11 + BIT10; break;
	}

	switch (info->params.crc_type & HDLC_CRC_MASK)
	{
	case HDLC_CRC_16_CCITT: val |= BIT9; break;
	case HDLC_CRC_32_CCITT: val |= BIT9 + BIT8; break;
	}

	if (info->params.preamble != HDLC_PREAMBLE_PATTERN_NONE)
		val |= BIT6;

	switch (info->params.preamble_length)
	{
	case HDLC_PREAMBLE_LENGTH_16BITS: val |= BIT5; break;
	case HDLC_PREAMBLE_LENGTH_32BITS: val |= BIT4; break;
	case HDLC_PREAMBLE_LENGTH_64BITS: val |= BIT5 + BIT4; break;
	}

	if (info->params.flags & HDLC_FLAG_AUTO_CTS)
		val |= BIT0;

	wr_reg16(info, TCR, val);

	

	switch (info->params.preamble)
	{
	case HDLC_PREAMBLE_PATTERN_FLAGS: val = 0x7e; break;
	case HDLC_PREAMBLE_PATTERN_ONES:  val = 0xff; break;
	case HDLC_PREAMBLE_PATTERN_ZEROS: val = 0x00; break;
	case HDLC_PREAMBLE_PATTERN_10:    val = 0x55; break;
	case HDLC_PREAMBLE_PATTERN_01:    val = 0xaa; break;
	default:                          val = 0x7e; break;
	}
	wr_reg8(info, TPR, (unsigned char)val);

	val = 0;

	switch(info->params.mode) {
	case MGSL_MODE_XSYNC:
		val |= BIT15 + BIT13;
		break;
	case MGSL_MODE_MONOSYNC: val |= BIT14 + BIT13; break;
	case MGSL_MODE_BISYNC:   val |= BIT15; break;
	case MGSL_MODE_RAW:      val |= BIT13; break;
	}

	switch(info->params.encoding)
	{
	case HDLC_ENCODING_NRZB:          val |= BIT10; break;
	case HDLC_ENCODING_NRZI_MARK:     val |= BIT11; break;
	case HDLC_ENCODING_NRZI:          val |= BIT11 + BIT10; break;
	case HDLC_ENCODING_BIPHASE_MARK:  val |= BIT12; break;
	case HDLC_ENCODING_BIPHASE_SPACE: val |= BIT12 + BIT10; break;
	case HDLC_ENCODING_BIPHASE_LEVEL: val |= BIT12 + BIT11; break;
	case HDLC_ENCODING_DIFF_BIPHASE_LEVEL: val |= BIT12 + BIT11 + BIT10; break;
	}

	switch (info->params.crc_type & HDLC_CRC_MASK)
	{
	case HDLC_CRC_16_CCITT: val |= BIT9; break;
	case HDLC_CRC_32_CCITT: val |= BIT9 + BIT8; break;
	}

	if (info->params.flags & HDLC_FLAG_AUTO_DCD)
		val |= BIT0;

	wr_reg16(info, RCR, val);

	val = 0;

	if (info->params.flags & HDLC_FLAG_TXC_BRG)
	{
		
		
		
		if (info->params.flags & HDLC_FLAG_RXC_DPLL)
			val |= BIT6 + BIT5;	
		else
			val |= BIT6;	
	}
	else if (info->params.flags & HDLC_FLAG_TXC_DPLL)
		val |= BIT7;	
	else if (info->params.flags & HDLC_FLAG_TXC_RXCPIN)
		val |= BIT5;	

	if (info->params.flags & HDLC_FLAG_RXC_BRG)
		val |= BIT3;	
	else if (info->params.flags & HDLC_FLAG_RXC_DPLL)
		val |= BIT4;	
	else if (info->params.flags & HDLC_FLAG_RXC_TXCPIN)
		val |= BIT2;	

	if (info->params.clock_speed)
		val |= BIT1 + BIT0;

	wr_reg8(info, CCR, (unsigned char)val);

	if (info->params.flags & (HDLC_FLAG_TXC_DPLL + HDLC_FLAG_RXC_DPLL))
	{
		
		switch(info->params.encoding)
		{
		case HDLC_ENCODING_BIPHASE_MARK:
		case HDLC_ENCODING_BIPHASE_SPACE:
			val = BIT7; break;
		case HDLC_ENCODING_BIPHASE_LEVEL:
		case HDLC_ENCODING_DIFF_BIPHASE_LEVEL:
			val = BIT7 + BIT6; break;
		default: val = BIT6;	
		}
		wr_reg16(info, RCR, (unsigned short)(rd_reg16(info, RCR) | val));

		
		set_rate(info, info->params.clock_speed * 16);
	}
	else
		set_rate(info, info->params.clock_speed);

	tx_set_idle(info);

	msc_set_vcr(info);

	wr_reg16(info, SCR, BIT15 + BIT14 + BIT0);

	if (info->params.loopback)
		enable_loopback(info);
}

static void tx_set_idle(struct slgt_info *info)
{
	unsigned char val;
	unsigned short tcr;

	tcr = rd_reg16(info, TCR);
	if (info->idle_mode & HDLC_TXIDLE_CUSTOM_16) {
		
		tcr = (tcr & ~(BIT6 + BIT5)) | BIT4;
		
		wr_reg8(info, TPR, (unsigned char)((info->idle_mode >> 8) & 0xff));
	} else if (!(tcr & BIT6)) {
		
		tcr &= ~(BIT5 + BIT4);
	}
	wr_reg16(info, TCR, tcr);

	if (info->idle_mode & (HDLC_TXIDLE_CUSTOM_8 | HDLC_TXIDLE_CUSTOM_16)) {
		
		val = (unsigned char)(info->idle_mode & 0xff);
	} else {
		
		switch(info->idle_mode)
		{
		case HDLC_TXIDLE_FLAGS:          val = 0x7e; break;
		case HDLC_TXIDLE_ALT_ZEROS_ONES:
		case HDLC_TXIDLE_ALT_MARK_SPACE: val = 0xaa; break;
		case HDLC_TXIDLE_ZEROS:
		case HDLC_TXIDLE_SPACE:          val = 0x00; break;
		default:                         val = 0xff;
		}
	}

	wr_reg8(info, TIR, val);
}

static void get_signals(struct slgt_info *info)
{
	unsigned short status = rd_reg16(info, SSR);

	
	info->signals &= SerialSignal_DTR + SerialSignal_RTS;

	if (status & BIT3)
		info->signals |= SerialSignal_DSR;
	if (status & BIT2)
		info->signals |= SerialSignal_CTS;
	if (status & BIT1)
		info->signals |= SerialSignal_DCD;
	if (status & BIT0)
		info->signals |= SerialSignal_RI;
}

static void msc_set_vcr(struct slgt_info *info)
{
	unsigned char val = 0;


	switch(info->if_mode & MGSL_INTERFACE_MASK)
	{
	case MGSL_INTERFACE_RS232:
		val |= BIT5; 
		break;
	case MGSL_INTERFACE_V35:
		val |= BIT7 + BIT6 + BIT5; 
		break;
	case MGSL_INTERFACE_RS422:
		val |= BIT6; 
		break;
	}

	if (info->if_mode & MGSL_INTERFACE_MSB_FIRST)
		val |= BIT4;
	if (info->signals & SerialSignal_DTR)
		val |= BIT3;
	if (info->signals & SerialSignal_RTS)
		val |= BIT2;
	if (info->if_mode & MGSL_INTERFACE_LL)
		val |= BIT1;
	if (info->if_mode & MGSL_INTERFACE_RL)
		val |= BIT0;
	wr_reg8(info, VCR, val);
}

static void set_signals(struct slgt_info *info)
{
	unsigned char val = rd_reg8(info, VCR);
	if (info->signals & SerialSignal_DTR)
		val |= BIT3;
	else
		val &= ~BIT3;
	if (info->signals & SerialSignal_RTS)
		val |= BIT2;
	else
		val &= ~BIT2;
	wr_reg8(info, VCR, val);
}

static void free_rbufs(struct slgt_info *info, unsigned int i, unsigned int last)
{
	int done = 0;

	while(!done) {
		
		info->rbufs[i].status = 0;
		set_desc_count(info->rbufs[i], info->rbuf_fill_level);
		if (i == last)
			done = 1;
		if (++i == info->rbuf_count)
			i = 0;
	}
	info->rbuf_current = i;
}

static void reset_rbufs(struct slgt_info *info)
{
	free_rbufs(info, 0, info->rbuf_count - 1);
	info->rbuf_fill_index = 0;
	info->rbuf_fill_count = 0;
}

static bool rx_get_frame(struct slgt_info *info)
{
	unsigned int start, end;
	unsigned short status;
	unsigned int framesize = 0;
	unsigned long flags;
	struct tty_struct *tty = info->port.tty;
	unsigned char addr_field = 0xff;
	unsigned int crc_size = 0;

	switch (info->params.crc_type & HDLC_CRC_MASK) {
	case HDLC_CRC_16_CCITT: crc_size = 2; break;
	case HDLC_CRC_32_CCITT: crc_size = 4; break;
	}

check_again:

	framesize = 0;
	addr_field = 0xff;
	start = end = info->rbuf_current;

	for (;;) {
		if (!desc_complete(info->rbufs[end]))
			goto cleanup;

		if (framesize == 0 && info->params.addr_filter != 0xff)
			addr_field = info->rbufs[end].buf[0];

		framesize += desc_count(info->rbufs[end]);

		if (desc_eof(info->rbufs[end]))
			break;

		if (++end == info->rbuf_count)
			end = 0;

		if (end == info->rbuf_current) {
			if (info->rx_enabled){
				spin_lock_irqsave(&info->lock,flags);
				rx_start(info);
				spin_unlock_irqrestore(&info->lock,flags);
			}
			goto cleanup;
		}
	}

	status = desc_status(info->rbufs[end]);

	
	if ((info->params.crc_type & HDLC_CRC_MASK) == HDLC_CRC_NONE)
		status &= ~BIT1;

	if (framesize == 0 ||
		 (addr_field != 0xff && addr_field != info->params.addr_filter)) {
		free_rbufs(info, start, end);
		goto check_again;
	}

	if (framesize < (2 + crc_size) || status & BIT0) {
		info->icount.rxshort++;
		framesize = 0;
	} else if (status & BIT1) {
		info->icount.rxcrc++;
		if (!(info->params.crc_type & HDLC_CRC_RETURN_EX))
			framesize = 0;
	}

#if SYNCLINK_GENERIC_HDLC
	if (framesize == 0) {
		info->netdev->stats.rx_errors++;
		info->netdev->stats.rx_frame_errors++;
	}
#endif

	DBGBH(("%s rx frame status=%04X size=%d\n",
		info->device_name, status, framesize));
	DBGDATA(info, info->rbufs[start].buf, min_t(int, framesize, info->rbuf_fill_level), "rx");

	if (framesize) {
		if (!(info->params.crc_type & HDLC_CRC_RETURN_EX)) {
			framesize -= crc_size;
			crc_size = 0;
		}

		if (framesize > info->max_frame_size + crc_size)
			info->icount.rxlong++;
		else {
			
			int copy_count = framesize;
			int i = start;
			unsigned char *p = info->tmp_rbuf;
			info->tmp_rbuf_count = framesize;

			info->icount.rxok++;

			while(copy_count) {
				int partial_count = min_t(int, copy_count, info->rbuf_fill_level);
				memcpy(p, info->rbufs[i].buf, partial_count);
				p += partial_count;
				copy_count -= partial_count;
				if (++i == info->rbuf_count)
					i = 0;
			}

			if (info->params.crc_type & HDLC_CRC_RETURN_EX) {
				*p = (status & BIT1) ? RX_CRC_ERROR : RX_OK;
				framesize++;
			}

#if SYNCLINK_GENERIC_HDLC
			if (info->netcount)
				hdlcdev_rx(info,info->tmp_rbuf, framesize);
			else
#endif
				ldisc_receive_buf(tty, info->tmp_rbuf, info->flag_buf, framesize);
		}
	}
	free_rbufs(info, start, end);
	return true;

cleanup:
	return false;
}

static bool rx_get_buf(struct slgt_info *info)
{
	unsigned int i = info->rbuf_current;
	unsigned int count;

	if (!desc_complete(info->rbufs[i]))
		return false;
	count = desc_count(info->rbufs[i]);
	switch(info->params.mode) {
	case MGSL_MODE_MONOSYNC:
	case MGSL_MODE_BISYNC:
	case MGSL_MODE_XSYNC:
		
		if (desc_residue(info->rbufs[i]))
			count--;
		break;
	}
	DBGDATA(info, info->rbufs[i].buf, count, "rx");
	DBGINFO(("rx_get_buf size=%d\n", count));
	if (count)
		ldisc_receive_buf(info->port.tty, info->rbufs[i].buf,
				  info->flag_buf, count);
	free_rbufs(info, i, i);
	return true;
}

static void reset_tbufs(struct slgt_info *info)
{
	unsigned int i;
	info->tbuf_current = 0;
	for (i=0 ; i < info->tbuf_count ; i++) {
		info->tbufs[i].status = 0;
		info->tbufs[i].count  = 0;
	}
}

static unsigned int free_tbuf_count(struct slgt_info *info)
{
	unsigned int count = 0;
	unsigned int i = info->tbuf_current;

	do
	{
		if (desc_count(info->tbufs[i]))
			break; 
		++count;
		if (++i == info->tbuf_count)
			i=0;
	} while (i != info->tbuf_current);

	
	if (count && (rd_reg32(info, TDCSR) & BIT0))
		--count;

	return count;
}

static unsigned int tbuf_bytes(struct slgt_info *info)
{
	unsigned int total_count = 0;
	unsigned int i = info->tbuf_current;
	unsigned int reg_value;
	unsigned int count;
	unsigned int active_buf_count = 0;

	do {
		count = desc_count(info->tbufs[i]);
		if (count)
			total_count += count;
		else if (!total_count)
			active_buf_count = info->tbufs[i].buf_count;
		if (++i == info->tbuf_count)
			i = 0;
	} while (i != info->tbuf_current);

	
	reg_value = rd_reg32(info, TDCSR);

	
	if (reg_value & BIT0)
		total_count += active_buf_count;

	
	total_count += (reg_value >> 8) & 0xff;

	
	if (info->tx_active)
		total_count++;

	return total_count;
}

static bool tx_load(struct slgt_info *info, const char *buf, unsigned int size)
{
	unsigned short count;
	unsigned int i;
	struct slgt_desc *d;

	
	if (DIV_ROUND_UP(size, DMABUFSIZE) > free_tbuf_count(info))
		return false;

	DBGDATA(info, buf, size, "tx");


	info->tbuf_start = i = info->tbuf_current;

	while (size) {
		d = &info->tbufs[i];

		count = (unsigned short)((size > DMABUFSIZE) ? DMABUFSIZE : size);
		memcpy(d->buf, buf, count);

		size -= count;
		buf  += count;

		if ((!size && info->params.mode == MGSL_MODE_HDLC) ||
		    info->params.mode == MGSL_MODE_RAW)
			set_desc_eof(*d, 1);
		else
			set_desc_eof(*d, 0);

		
		if (i != info->tbuf_start)
			set_desc_count(*d, count);
		d->buf_count = count;

		if (++i == info->tbuf_count)
			i = 0;
	}

	info->tbuf_current = i;

	
	d = &info->tbufs[info->tbuf_start];
	set_desc_count(*d, d->buf_count);

	
	if (!info->tx_active)
		tx_start(info);
	update_tx_timer(info);

	return true;
}

static int register_test(struct slgt_info *info)
{
	static unsigned short patterns[] =
		{0x0000, 0xffff, 0xaaaa, 0x5555, 0x6969, 0x9696};
	static unsigned int count = ARRAY_SIZE(patterns);
	unsigned int i;
	int rc = 0;

	for (i=0 ; i < count ; i++) {
		wr_reg16(info, TIR, patterns[i]);
		wr_reg16(info, BDR, patterns[(i+1)%count]);
		if ((rd_reg16(info, TIR) != patterns[i]) ||
		    (rd_reg16(info, BDR) != patterns[(i+1)%count])) {
			rc = -ENODEV;
			break;
		}
	}
	info->gpio_present = (rd_reg32(info, JCR) & BIT5) ? 1 : 0;
	info->init_error = rc ? 0 : DiagStatus_AddressFailure;
	return rc;
}

static int irq_test(struct slgt_info *info)
{
	unsigned long timeout;
	unsigned long flags;
	struct tty_struct *oldtty = info->port.tty;
	u32 speed = info->params.data_rate;

	info->params.data_rate = 921600;
	info->port.tty = NULL;

	spin_lock_irqsave(&info->lock, flags);
	async_mode(info);
	slgt_irq_on(info, IRQ_TXIDLE);

	
	wr_reg16(info, TCR,
		(unsigned short)(rd_reg16(info, TCR) | BIT1));

	
	wr_reg16(info, TDR, 0);

	
	info->init_error = DiagStatus_IrqFailure;
	info->irq_occurred = false;

	spin_unlock_irqrestore(&info->lock, flags);

	timeout=100;
	while(timeout-- && !info->irq_occurred)
		msleep_interruptible(10);

	spin_lock_irqsave(&info->lock,flags);
	reset_port(info);
	spin_unlock_irqrestore(&info->lock,flags);

	info->params.data_rate = speed;
	info->port.tty = oldtty;

	info->init_error = info->irq_occurred ? 0 : DiagStatus_IrqFailure;
	return info->irq_occurred ? 0 : -ENODEV;
}

static int loopback_test_rx(struct slgt_info *info)
{
	unsigned char *src, *dest;
	int count;

	if (desc_complete(info->rbufs[0])) {
		count = desc_count(info->rbufs[0]);
		src   = info->rbufs[0].buf;
		dest  = info->tmp_rbuf;

		for( ; count ; count-=2, src+=2) {
			
			if (!(*(src+1) & (BIT9 + BIT8))) {
				*dest = *src;
				dest++;
				info->tmp_rbuf_count++;
			}
		}
		DBGDATA(info, info->tmp_rbuf, info->tmp_rbuf_count, "rx");
		return 1;
	}
	return 0;
}

static int loopback_test(struct slgt_info *info)
{
#define TESTFRAMESIZE 20

	unsigned long timeout;
	u16 count = TESTFRAMESIZE;
	unsigned char buf[TESTFRAMESIZE];
	int rc = -ENODEV;
	unsigned long flags;

	struct tty_struct *oldtty = info->port.tty;
	MGSL_PARAMS params;

	memcpy(&params, &info->params, sizeof(params));

	info->params.mode = MGSL_MODE_ASYNC;
	info->params.data_rate = 921600;
	info->params.loopback = 1;
	info->port.tty = NULL;

	
	for (count = 0; count < TESTFRAMESIZE; ++count)
		buf[count] = (unsigned char)count;

	info->tmp_rbuf_count = 0;
	memset(info->tmp_rbuf, 0, TESTFRAMESIZE);

	
	spin_lock_irqsave(&info->lock,flags);
	async_mode(info);
	rx_start(info);
	tx_load(info, buf, count);
	spin_unlock_irqrestore(&info->lock, flags);

	
	for (timeout = 100; timeout; --timeout) {
		msleep_interruptible(10);
		if (loopback_test_rx(info)) {
			rc = 0;
			break;
		}
	}

	
	if (!rc && (info->tmp_rbuf_count != count ||
		  memcmp(buf, info->tmp_rbuf, count))) {
		rc = -ENODEV;
	}

	spin_lock_irqsave(&info->lock,flags);
	reset_adapter(info);
	spin_unlock_irqrestore(&info->lock,flags);

	memcpy(&info->params, &params, sizeof(info->params));
	info->port.tty = oldtty;

	info->init_error = rc ? DiagStatus_DmaFailure : 0;
	return rc;
}

static int adapter_test(struct slgt_info *info)
{
	DBGINFO(("testing %s\n", info->device_name));
	if (register_test(info) < 0) {
		printk("register test failure %s addr=%08X\n",
			info->device_name, info->phys_reg_addr);
	} else if (irq_test(info) < 0) {
		printk("IRQ test failure %s IRQ=%d\n",
			info->device_name, info->irq_level);
	} else if (loopback_test(info) < 0) {
		printk("loopback test failure %s\n", info->device_name);
	}
	return info->init_error;
}

static void tx_timeout(unsigned long context)
{
	struct slgt_info *info = (struct slgt_info*)context;
	unsigned long flags;

	DBGINFO(("%s tx_timeout\n", info->device_name));
	if(info->tx_active && info->params.mode == MGSL_MODE_HDLC) {
		info->icount.txtimeout++;
	}
	spin_lock_irqsave(&info->lock,flags);
	tx_stop(info);
	spin_unlock_irqrestore(&info->lock,flags);

#if SYNCLINK_GENERIC_HDLC
	if (info->netcount)
		hdlcdev_tx_done(info);
	else
#endif
		bh_transmit(info);
}

static void rx_timeout(unsigned long context)
{
	struct slgt_info *info = (struct slgt_info*)context;
	unsigned long flags;

	DBGINFO(("%s rx_timeout\n", info->device_name));
	spin_lock_irqsave(&info->lock, flags);
	info->pending_bh |= BH_RECEIVE;
	spin_unlock_irqrestore(&info->lock, flags);
	bh_handler(&info->task);
}

