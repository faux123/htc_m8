/*
 *  i8042 keyboard and mouse controller driver for Linux
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/err.h>
#include <linux/rcupdate.h>
#include <linux/platform_device.h>
#include <linux/i8042.h>
#include <linux/slab.h>

#include <asm/io.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("i8042 keyboard and mouse controller driver");
MODULE_LICENSE("GPL");

static bool i8042_nokbd;
module_param_named(nokbd, i8042_nokbd, bool, 0);
MODULE_PARM_DESC(nokbd, "Do not probe or use KBD port.");

static bool i8042_noaux;
module_param_named(noaux, i8042_noaux, bool, 0);
MODULE_PARM_DESC(noaux, "Do not probe or use AUX (mouse) port.");

static bool i8042_nomux;
module_param_named(nomux, i8042_nomux, bool, 0);
MODULE_PARM_DESC(nomux, "Do not check whether an active multiplexing controller is present.");

static bool i8042_unlock;
module_param_named(unlock, i8042_unlock, bool, 0);
MODULE_PARM_DESC(unlock, "Ignore keyboard lock.");

static bool i8042_reset;
module_param_named(reset, i8042_reset, bool, 0);
MODULE_PARM_DESC(reset, "Reset controller during init and cleanup.");

static bool i8042_direct;
module_param_named(direct, i8042_direct, bool, 0);
MODULE_PARM_DESC(direct, "Put keyboard port into non-translated mode.");

static bool i8042_dumbkbd;
module_param_named(dumbkbd, i8042_dumbkbd, bool, 0);
MODULE_PARM_DESC(dumbkbd, "Pretend that controller can only read data from keyboard");

static bool i8042_noloop;
module_param_named(noloop, i8042_noloop, bool, 0);
MODULE_PARM_DESC(noloop, "Disable the AUX Loopback command while probing for the AUX port");

static bool i8042_notimeout;
module_param_named(notimeout, i8042_notimeout, bool, 0);
MODULE_PARM_DESC(notimeout, "Ignore timeouts signalled by i8042");

#ifdef CONFIG_X86
static bool i8042_dritek;
module_param_named(dritek, i8042_dritek, bool, 0);
MODULE_PARM_DESC(dritek, "Force enable the Dritek keyboard extension");
#endif

#ifdef CONFIG_PNP
static bool i8042_nopnp;
module_param_named(nopnp, i8042_nopnp, bool, 0);
MODULE_PARM_DESC(nopnp, "Do not use PNP to detect controller settings");
#endif

#define DEBUG
#ifdef DEBUG
static bool i8042_debug;
module_param_named(debug, i8042_debug, bool, 0600);
MODULE_PARM_DESC(debug, "Turn i8042 debugging mode on and off");
#endif

static bool i8042_bypass_aux_irq_test;

#include "i8042.h"

static DEFINE_SPINLOCK(i8042_lock);

static DEFINE_MUTEX(i8042_mutex);

struct i8042_port {
	struct serio *serio;
	int irq;
	bool exists;
	signed char mux;
};

#define I8042_KBD_PORT_NO	0
#define I8042_AUX_PORT_NO	1
#define I8042_MUX_PORT_NO	2
#define I8042_NUM_PORTS		(I8042_NUM_MUX_PORTS + 2)

static struct i8042_port i8042_ports[I8042_NUM_PORTS];

static unsigned char i8042_initial_ctr;
static unsigned char i8042_ctr;
static bool i8042_mux_present;
static bool i8042_kbd_irq_registered;
static bool i8042_aux_irq_registered;
static unsigned char i8042_suppress_kbd_ack;
static struct platform_device *i8042_platform_device;

static irqreturn_t i8042_interrupt(int irq, void *dev_id);
static bool (*i8042_platform_filter)(unsigned char data, unsigned char str,
				     struct serio *serio);

void i8042_lock_chip(void)
{
	mutex_lock(&i8042_mutex);
}
EXPORT_SYMBOL(i8042_lock_chip);

void i8042_unlock_chip(void)
{
	mutex_unlock(&i8042_mutex);
}
EXPORT_SYMBOL(i8042_unlock_chip);

int i8042_install_filter(bool (*filter)(unsigned char data, unsigned char str,
					struct serio *serio))
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	if (i8042_platform_filter) {
		ret = -EBUSY;
		goto out;
	}

	i8042_platform_filter = filter;

out:
	spin_unlock_irqrestore(&i8042_lock, flags);
	return ret;
}
EXPORT_SYMBOL(i8042_install_filter);

int i8042_remove_filter(bool (*filter)(unsigned char data, unsigned char str,
				       struct serio *port))
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	if (i8042_platform_filter != filter) {
		ret = -EINVAL;
		goto out;
	}

	i8042_platform_filter = NULL;

out:
	spin_unlock_irqrestore(&i8042_lock, flags);
	return ret;
}
EXPORT_SYMBOL(i8042_remove_filter);


static int i8042_wait_read(void)
{
	int i = 0;

	while ((~i8042_read_status() & I8042_STR_OBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_wait_write(void)
{
	int i = 0;

	while ((i8042_read_status() & I8042_STR_IBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}


static int i8042_flush(void)
{
	unsigned long flags;
	unsigned char data, str;
	int i = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	while (((str = i8042_read_status()) & I8042_STR_OBF) && (i < I8042_BUFFER_SIZE)) {
		udelay(50);
		data = i8042_read_data();
		i++;
		dbg("%02x <- i8042 (flush, %s)\n",
		    data, str & I8042_STR_AUXDATA ? "aux" : "kbd");
	}

	spin_unlock_irqrestore(&i8042_lock, flags);

	return i;
}


static int __i8042_command(unsigned char *param, int command)
{
	int i, error;

	if (i8042_noloop && command == I8042_CMD_AUX_LOOP)
		return -1;

	error = i8042_wait_write();
	if (error)
		return error;

	dbg("%02x -> i8042 (command)\n", command & 0xff);
	i8042_write_command(command & 0xff);

	for (i = 0; i < ((command >> 12) & 0xf); i++) {
		error = i8042_wait_write();
		if (error)
			return error;
		dbg("%02x -> i8042 (parameter)\n", param[i]);
		i8042_write_data(param[i]);
	}

	for (i = 0; i < ((command >> 8) & 0xf); i++) {
		error = i8042_wait_read();
		if (error) {
			dbg("     -- i8042 (timeout)\n");
			return error;
		}

		if (command == I8042_CMD_AUX_LOOP &&
		    !(i8042_read_status() & I8042_STR_AUXDATA)) {
			dbg("     -- i8042 (auxerr)\n");
			return -1;
		}

		param[i] = i8042_read_data();
		dbg("%02x <- i8042 (return)\n", param[i]);
	}

	return 0;
}

int i8042_command(unsigned char *param, int command)
{
	unsigned long flags;
	int retval;

	spin_lock_irqsave(&i8042_lock, flags);
	retval = __i8042_command(param, command);
	spin_unlock_irqrestore(&i8042_lock, flags);

	return retval;
}
EXPORT_SYMBOL(i8042_command);


static int i8042_kbd_write(struct serio *port, unsigned char c)
{
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	if (!(retval = i8042_wait_write())) {
		dbg("%02x -> i8042 (kbd-data)\n", c);
		i8042_write_data(c);
	}

	spin_unlock_irqrestore(&i8042_lock, flags);

	return retval;
}


static int i8042_aux_write(struct serio *serio, unsigned char c)
{
	struct i8042_port *port = serio->port_data;

	return i8042_command(&c, port->mux == -1 ?
					I8042_CMD_AUX_SEND :
					I8042_CMD_MUX_SEND + port->mux);
}



static void i8042_port_close(struct serio *serio)
{
	int irq_bit;
	int disable_bit;
	const char *port_name;

	if (serio == i8042_ports[I8042_AUX_PORT_NO].serio) {
		irq_bit = I8042_CTR_AUXINT;
		disable_bit = I8042_CTR_AUXDIS;
		port_name = "AUX";
	} else {
		irq_bit = I8042_CTR_KBDINT;
		disable_bit = I8042_CTR_KBDDIS;
		port_name = "KBD";
	}

	i8042_ctr &= ~irq_bit;
	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		pr_warn("Can't write CTR while closing %s port\n", port_name);

	udelay(50);

	i8042_ctr &= ~disable_bit;
	i8042_ctr |= irq_bit;
	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		pr_err("Can't reactivate %s port\n", port_name);

	i8042_interrupt(0, NULL);
}

static int i8042_start(struct serio *serio)
{
	struct i8042_port *port = serio->port_data;

	port->exists = true;
	mb();
	return 0;
}

static void i8042_stop(struct serio *serio)
{
	struct i8042_port *port = serio->port_data;

	port->exists = false;

	synchronize_irq(I8042_AUX_IRQ);
	synchronize_irq(I8042_KBD_IRQ);
	port->serio = NULL;
}

static bool i8042_filter(unsigned char data, unsigned char str,
			 struct serio *serio)
{
	if (unlikely(i8042_suppress_kbd_ack)) {
		if ((~str & I8042_STR_AUXDATA) &&
		    (data == 0xfa || data == 0xfe)) {
			i8042_suppress_kbd_ack--;
			dbg("Extra keyboard ACK - filtered out\n");
			return true;
		}
	}

	if (i8042_platform_filter && i8042_platform_filter(data, str, serio)) {
		dbg("Filtered out by platform filter\n");
		return true;
	}

	return false;
}


static irqreturn_t i8042_interrupt(int irq, void *dev_id)
{
	struct i8042_port *port;
	struct serio *serio;
	unsigned long flags;
	unsigned char str, data;
	unsigned int dfl;
	unsigned int port_no;
	bool filtered;
	int ret = 1;

	spin_lock_irqsave(&i8042_lock, flags);

	str = i8042_read_status();
	if (unlikely(~str & I8042_STR_OBF)) {
		spin_unlock_irqrestore(&i8042_lock, flags);
		if (irq)
			dbg("Interrupt %d, without any data\n", irq);
		ret = 0;
		goto out;
	}

	data = i8042_read_data();

	if (i8042_mux_present && (str & I8042_STR_AUXDATA)) {
		static unsigned long last_transmit;
		static unsigned char last_str;

		dfl = 0;
		if (str & I8042_STR_MUXERR) {
			dbg("MUX error, status is %02x, data is %02x\n",
			    str, data);

			switch (data) {
				default:
					if (time_before(jiffies, last_transmit + HZ/10)) {
						str = last_str;
						break;
					}
					
				case 0xfc:
				case 0xfd:
				case 0xfe: dfl = SERIO_TIMEOUT; data = 0xfe; break;
				case 0xff: dfl = SERIO_PARITY;  data = 0xfe; break;
			}
		}

		port_no = I8042_MUX_PORT_NO + ((str >> 6) & 3);
		last_str = str;
		last_transmit = jiffies;
	} else {

		dfl = ((str & I8042_STR_PARITY) ? SERIO_PARITY : 0) |
		      ((str & I8042_STR_TIMEOUT && !i8042_notimeout) ? SERIO_TIMEOUT : 0);

		port_no = (str & I8042_STR_AUXDATA) ?
				I8042_AUX_PORT_NO : I8042_KBD_PORT_NO;
	}

	port = &i8042_ports[port_no];
	serio = port->exists ? port->serio : NULL;

	dbg("%02x <- i8042 (interrupt, %d, %d%s%s)\n",
	    data, port_no, irq,
	    dfl & SERIO_PARITY ? ", bad parity" : "",
	    dfl & SERIO_TIMEOUT ? ", timeout" : "");

	filtered = i8042_filter(data, str, serio);

	spin_unlock_irqrestore(&i8042_lock, flags);

	if (likely(port->exists && !filtered))
		serio_interrupt(serio, data, dfl);

 out:
	return IRQ_RETVAL(ret);
}


static int i8042_enable_kbd_port(void)
{
	i8042_ctr &= ~I8042_CTR_KBDDIS;
	i8042_ctr |= I8042_CTR_KBDINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		i8042_ctr &= ~I8042_CTR_KBDINT;
		i8042_ctr |= I8042_CTR_KBDDIS;
		pr_err("Failed to enable KBD port\n");
		return -EIO;
	}

	return 0;
}


static int i8042_enable_aux_port(void)
{
	i8042_ctr &= ~I8042_CTR_AUXDIS;
	i8042_ctr |= I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		i8042_ctr &= ~I8042_CTR_AUXINT;
		i8042_ctr |= I8042_CTR_AUXDIS;
		pr_err("Failed to enable AUX port\n");
		return -EIO;
	}

	return 0;
}


static int i8042_enable_mux_ports(void)
{
	unsigned char param;
	int i;

	for (i = 0; i < I8042_NUM_MUX_PORTS; i++) {
		i8042_command(&param, I8042_CMD_MUX_PFX + i);
		i8042_command(&param, I8042_CMD_AUX_ENABLE);
	}

	return i8042_enable_aux_port();
}


static int i8042_set_mux_mode(bool multiplex, unsigned char *mux_version)
{

	unsigned char param, val;

	i8042_flush();


	param = val = 0xf0;
	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param != val)
		return -1;
	param = val = multiplex ? 0x56 : 0xf6;
	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param != val)
		return -1;
	param = val = multiplex ? 0xa4 : 0xa5;
	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param == val)
		return -1;

	if (param == 0xac)
		return -1;

	if (mux_version)
		*mux_version = param;

	return 0;
}


static int __init i8042_check_mux(void)
{
	unsigned char mux_version;

	if (i8042_set_mux_mode(true, &mux_version))
		return -1;

	pr_info("Detected active multiplexing controller, rev %d.%d\n",
		(mux_version >> 4) & 0xf, mux_version & 0xf);

	i8042_ctr |= I8042_CTR_AUXDIS;
	i8042_ctr &= ~I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		pr_err("Failed to disable AUX port, can't use MUX\n");
		return -EIO;
	}

	i8042_mux_present = true;

	return 0;
}

static struct completion i8042_aux_irq_delivered __initdata;
static bool i8042_irq_being_tested __initdata;

static irqreturn_t __init i8042_aux_test_irq(int irq, void *dev_id)
{
	unsigned long flags;
	unsigned char str, data;
	int ret = 0;

	spin_lock_irqsave(&i8042_lock, flags);
	str = i8042_read_status();
	if (str & I8042_STR_OBF) {
		data = i8042_read_data();
		dbg("%02x <- i8042 (aux_test_irq, %s)\n",
		    data, str & I8042_STR_AUXDATA ? "aux" : "kbd");
		if (i8042_irq_being_tested &&
		    data == 0xa5 && (str & I8042_STR_AUXDATA))
			complete(&i8042_aux_irq_delivered);
		ret = 1;
	}
	spin_unlock_irqrestore(&i8042_lock, flags);

	return IRQ_RETVAL(ret);
}

static int __init i8042_toggle_aux(bool on)
{
	unsigned char param;
	int i;

	if (i8042_command(&param,
			on ? I8042_CMD_AUX_ENABLE : I8042_CMD_AUX_DISABLE))
		return -1;

	
	for (i = 0; i < 100; i++) {
		udelay(50);

		if (i8042_command(&param, I8042_CMD_CTL_RCTR))
			return -1;

		if (!(param & I8042_CTR_AUXDIS) == on)
			return 0;
	}

	return -1;
}


static int __init i8042_check_aux(void)
{
	int retval = -1;
	bool irq_registered = false;
	bool aux_loop_broken = false;
	unsigned long flags;
	unsigned char param;


	i8042_flush();


	param = 0x5a;
	retval = i8042_command(&param, I8042_CMD_AUX_LOOP);
	if (retval || param != 0x5a) {


		if (i8042_command(&param, I8042_CMD_AUX_TEST) ||
		    (param && param != 0xfa && param != 0xff))
			return -1;

		if (!retval)
			aux_loop_broken = true;
	}


	if (i8042_toggle_aux(false)) {
		pr_warn("Failed to disable AUX port, but continuing anyway... Is this a SiS?\n");
		pr_warn("If AUX port is really absent please use the 'i8042.noaux' option\n");
	}

	if (i8042_toggle_aux(true))
		return -1;


	if (i8042_noloop || i8042_bypass_aux_irq_test || aux_loop_broken) {
		retval = 0;
		goto out;
	}

	if (request_irq(I8042_AUX_IRQ, i8042_aux_test_irq, IRQF_SHARED,
			"i8042", i8042_platform_device))
		goto out;

	irq_registered = true;

	if (i8042_enable_aux_port())
		goto out;

	spin_lock_irqsave(&i8042_lock, flags);

	init_completion(&i8042_aux_irq_delivered);
	i8042_irq_being_tested = true;

	param = 0xa5;
	retval = __i8042_command(&param, I8042_CMD_AUX_LOOP & 0xf0ff);

	spin_unlock_irqrestore(&i8042_lock, flags);

	if (retval)
		goto out;

	if (wait_for_completion_timeout(&i8042_aux_irq_delivered,
					msecs_to_jiffies(250)) == 0) {
		dbg("     -- i8042 (aux irq test timeout)\n");
		i8042_flush();
		retval = -1;
	}

 out:


	i8042_ctr |= I8042_CTR_AUXDIS;
	i8042_ctr &= ~I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		retval = -1;

	if (irq_registered)
		free_irq(I8042_AUX_IRQ, i8042_platform_device);

	return retval;
}

static int i8042_controller_check(void)
{
	if (i8042_flush() == I8042_BUFFER_SIZE) {
		pr_err("No controller found\n");
		return -ENODEV;
	}

	return 0;
}

static int i8042_controller_selftest(void)
{
	unsigned char param;
	int i = 0;

	do {

		if (i8042_command(&param, I8042_CMD_CTL_TEST)) {
			pr_err("i8042 controller selftest timeout\n");
			return -ENODEV;
		}

		if (param == I8042_RET_CTL_TEST)
			return 0;

		dbg("i8042 controller selftest: %#x != %#x\n",
		    param, I8042_RET_CTL_TEST);
		msleep(50);
	} while (i++ < 5);

#ifdef CONFIG_X86
	pr_info("giving up on controller selftest, continuing anyway...\n");
	return 0;
#else
	pr_err("i8042 controller selftest failed\n");
	return -EIO;
#endif
}


static int i8042_controller_init(void)
{
	unsigned long flags;
	int n = 0;
	unsigned char ctr[2];


	do {
		if (n >= 10) {
			pr_err("Unable to get stable CTR read\n");
			return -EIO;
		}

		if (n != 0)
			udelay(50);

		if (i8042_command(&ctr[n++ % 2], I8042_CMD_CTL_RCTR)) {
			pr_err("Can't read CTR while initializing i8042\n");
			return -EIO;
		}

	} while (n < 2 || ctr[0] != ctr[1]);

	i8042_initial_ctr = i8042_ctr = ctr[0];


	i8042_ctr |= I8042_CTR_KBDDIS;
	i8042_ctr &= ~I8042_CTR_KBDINT;


	spin_lock_irqsave(&i8042_lock, flags);
	if (~i8042_read_status() & I8042_STR_KEYLOCK) {
		if (i8042_unlock)
			i8042_ctr |= I8042_CTR_IGNKEYLOCK;
		else
			pr_warn("Warning: Keylock active\n");
	}
	spin_unlock_irqrestore(&i8042_lock, flags);


	if (~i8042_ctr & I8042_CTR_XLATE)
		i8042_direct = true;


	if (i8042_direct)
		i8042_ctr &= ~I8042_CTR_XLATE;


	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		pr_err("Can't write CTR while initializing i8042\n");
		return -EIO;
	}


	i8042_flush();

	return 0;
}



static void i8042_controller_reset(bool force_reset)
{
	i8042_flush();


	i8042_ctr |= I8042_CTR_KBDDIS | I8042_CTR_AUXDIS;
	i8042_ctr &= ~(I8042_CTR_KBDINT | I8042_CTR_AUXINT);

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		pr_warn("Can't write CTR while resetting\n");


	if (i8042_mux_present)
		i8042_set_mux_mode(false, NULL);


	if (i8042_reset || force_reset)
		i8042_controller_selftest();


	if (i8042_command(&i8042_initial_ctr, I8042_CMD_CTL_WCTR))
		pr_warn("Can't restore CTR\n");
}



#define DELAY do { mdelay(1); if (++delay > 10) return delay; } while(0)

static long i8042_panic_blink(int state)
{
	long delay = 0;
	char led;

	led = (state) ? 0x01 | 0x04 : 0;
	while (i8042_read_status() & I8042_STR_IBF)
		DELAY;
	dbg("%02x -> i8042 (panic blink)\n", 0xed);
	i8042_suppress_kbd_ack = 2;
	i8042_write_data(0xed); 
	DELAY;
	while (i8042_read_status() & I8042_STR_IBF)
		DELAY;
	DELAY;
	dbg("%02x -> i8042 (panic blink)\n", led);
	i8042_write_data(led);
	DELAY;
	return delay;
}

#undef DELAY

#ifdef CONFIG_X86
static void i8042_dritek_enable(void)
{
	unsigned char param = 0x90;
	int error;

	error = i8042_command(&param, 0x1059);
	if (error)
		pr_warn("Failed to enable DRITEK extension: %d\n", error);
}
#endif

#ifdef CONFIG_PM


static int i8042_controller_resume(bool force_reset)
{
	int error;

	error = i8042_controller_check();
	if (error)
		return error;

	if (i8042_reset || force_reset) {
		error = i8042_controller_selftest();
		if (error)
			return error;
	}


	i8042_ctr = i8042_initial_ctr;
	if (i8042_direct)
		i8042_ctr &= ~I8042_CTR_XLATE;
	i8042_ctr |= I8042_CTR_AUXDIS | I8042_CTR_KBDDIS;
	i8042_ctr &= ~(I8042_CTR_AUXINT | I8042_CTR_KBDINT);
	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		pr_warn("Can't write CTR to resume, retrying...\n");
		msleep(50);
		if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
			pr_err("CTR write retry failed\n");
			return -EIO;
		}
	}


#ifdef CONFIG_X86
	if (i8042_dritek)
		i8042_dritek_enable();
#endif

	if (i8042_mux_present) {
		if (i8042_set_mux_mode(true, NULL) || i8042_enable_mux_ports())
			pr_warn("failed to resume active multiplexor, mouse won't work\n");
	} else if (i8042_ports[I8042_AUX_PORT_NO].serio)
		i8042_enable_aux_port();

	if (i8042_ports[I8042_KBD_PORT_NO].serio)
		i8042_enable_kbd_port();

	i8042_interrupt(0, NULL);

	return 0;
}


static int i8042_pm_suspend(struct device *dev)
{
	i8042_controller_reset(true);

	return 0;
}

static int i8042_pm_resume(struct device *dev)
{
	return i8042_controller_resume(true);
}

static int i8042_pm_thaw(struct device *dev)
{
	i8042_interrupt(0, NULL);

	return 0;
}

static int i8042_pm_reset(struct device *dev)
{
	i8042_controller_reset(false);

	return 0;
}

static int i8042_pm_restore(struct device *dev)
{
	return i8042_controller_resume(false);
}

static const struct dev_pm_ops i8042_pm_ops = {
	.suspend	= i8042_pm_suspend,
	.resume		= i8042_pm_resume,
	.thaw		= i8042_pm_thaw,
	.poweroff	= i8042_pm_reset,
	.restore	= i8042_pm_restore,
};

#endif 


static void i8042_shutdown(struct platform_device *dev)
{
	i8042_controller_reset(false);
}

static int __init i8042_create_kbd_port(void)
{
	struct serio *serio;
	struct i8042_port *port = &i8042_ports[I8042_KBD_PORT_NO];

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio)
		return -ENOMEM;

	serio->id.type		= i8042_direct ? SERIO_8042 : SERIO_8042_XL;
	serio->write		= i8042_dumbkbd ? NULL : i8042_kbd_write;
	serio->start		= i8042_start;
	serio->stop		= i8042_stop;
	serio->close		= i8042_port_close;
	serio->port_data	= port;
	serio->dev.parent	= &i8042_platform_device->dev;
	strlcpy(serio->name, "i8042 KBD port", sizeof(serio->name));
	strlcpy(serio->phys, I8042_KBD_PHYS_DESC, sizeof(serio->phys));

	port->serio = serio;
	port->irq = I8042_KBD_IRQ;

	return 0;
}

static int __init i8042_create_aux_port(int idx)
{
	struct serio *serio;
	int port_no = idx < 0 ? I8042_AUX_PORT_NO : I8042_MUX_PORT_NO + idx;
	struct i8042_port *port = &i8042_ports[port_no];

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio)
		return -ENOMEM;

	serio->id.type		= SERIO_8042;
	serio->write		= i8042_aux_write;
	serio->start		= i8042_start;
	serio->stop		= i8042_stop;
	serio->port_data	= port;
	serio->dev.parent	= &i8042_platform_device->dev;
	if (idx < 0) {
		strlcpy(serio->name, "i8042 AUX port", sizeof(serio->name));
		strlcpy(serio->phys, I8042_AUX_PHYS_DESC, sizeof(serio->phys));
		serio->close = i8042_port_close;
	} else {
		snprintf(serio->name, sizeof(serio->name), "i8042 AUX%d port", idx);
		snprintf(serio->phys, sizeof(serio->phys), I8042_MUX_PHYS_DESC, idx + 1);
	}

	port->serio = serio;
	port->mux = idx;
	port->irq = I8042_AUX_IRQ;

	return 0;
}

static void __init i8042_free_kbd_port(void)
{
	kfree(i8042_ports[I8042_KBD_PORT_NO].serio);
	i8042_ports[I8042_KBD_PORT_NO].serio = NULL;
}

static void __init i8042_free_aux_ports(void)
{
	int i;

	for (i = I8042_AUX_PORT_NO; i < I8042_NUM_PORTS; i++) {
		kfree(i8042_ports[i].serio);
		i8042_ports[i].serio = NULL;
	}
}

static void __init i8042_register_ports(void)
{
	int i;

	for (i = 0; i < I8042_NUM_PORTS; i++) {
		if (i8042_ports[i].serio) {
			printk(KERN_INFO "serio: %s at %#lx,%#lx irq %d\n",
				i8042_ports[i].serio->name,
				(unsigned long) I8042_DATA_REG,
				(unsigned long) I8042_COMMAND_REG,
				i8042_ports[i].irq);
			serio_register_port(i8042_ports[i].serio);
		}
	}
}

static void __devexit i8042_unregister_ports(void)
{
	int i;

	for (i = 0; i < I8042_NUM_PORTS; i++) {
		if (i8042_ports[i].serio) {
			serio_unregister_port(i8042_ports[i].serio);
			i8042_ports[i].serio = NULL;
		}
	}
}

bool i8042_check_port_owner(const struct serio *port)
{
	int i;

	for (i = 0; i < I8042_NUM_PORTS; i++)
		if (i8042_ports[i].serio == port)
			return true;

	return false;
}
EXPORT_SYMBOL(i8042_check_port_owner);

static void i8042_free_irqs(void)
{
	if (i8042_aux_irq_registered)
		free_irq(I8042_AUX_IRQ, i8042_platform_device);
	if (i8042_kbd_irq_registered)
		free_irq(I8042_KBD_IRQ, i8042_platform_device);

	i8042_aux_irq_registered = i8042_kbd_irq_registered = false;
}

static int __init i8042_setup_aux(void)
{
	int (*aux_enable)(void);
	int error;
	int i;

	if (i8042_check_aux())
		return -ENODEV;

	if (i8042_nomux || i8042_check_mux()) {
		error = i8042_create_aux_port(-1);
		if (error)
			goto err_free_ports;
		aux_enable = i8042_enable_aux_port;
	} else {
		for (i = 0; i < I8042_NUM_MUX_PORTS; i++) {
			error = i8042_create_aux_port(i);
			if (error)
				goto err_free_ports;
		}
		aux_enable = i8042_enable_mux_ports;
	}

	error = request_irq(I8042_AUX_IRQ, i8042_interrupt, IRQF_SHARED,
			    "i8042", i8042_platform_device);
	if (error)
		goto err_free_ports;

	if (aux_enable())
		goto err_free_irq;

	i8042_aux_irq_registered = true;
	return 0;

 err_free_irq:
	free_irq(I8042_AUX_IRQ, i8042_platform_device);
 err_free_ports:
	i8042_free_aux_ports();
	return error;
}

static int __init i8042_setup_kbd(void)
{
	int error;

	error = i8042_create_kbd_port();
	if (error)
		return error;

	error = request_irq(I8042_KBD_IRQ, i8042_interrupt, IRQF_SHARED,
			    "i8042", i8042_platform_device);
	if (error)
		goto err_free_port;

	error = i8042_enable_kbd_port();
	if (error)
		goto err_free_irq;

	i8042_kbd_irq_registered = true;
	return 0;

 err_free_irq:
	free_irq(I8042_KBD_IRQ, i8042_platform_device);
 err_free_port:
	i8042_free_kbd_port();
	return error;
}

static int __init i8042_probe(struct platform_device *dev)
{
	int error;

	i8042_platform_device = dev;

	if (i8042_reset) {
		error = i8042_controller_selftest();
		if (error)
			return error;
	}

	error = i8042_controller_init();
	if (error)
		return error;

#ifdef CONFIG_X86
	if (i8042_dritek)
		i8042_dritek_enable();
#endif

	if (!i8042_noaux) {
		error = i8042_setup_aux();
		if (error && error != -ENODEV && error != -EBUSY)
			goto out_fail;
	}

	if (!i8042_nokbd) {
		error = i8042_setup_kbd();
		if (error)
			goto out_fail;
	}
	i8042_register_ports();

	return 0;

 out_fail:
	i8042_free_aux_ports();	
	i8042_free_irqs();
	i8042_controller_reset(false);
	i8042_platform_device = NULL;

	return error;
}

static int __devexit i8042_remove(struct platform_device *dev)
{
	i8042_unregister_ports();
	i8042_free_irqs();
	i8042_controller_reset(false);
	i8042_platform_device = NULL;

	return 0;
}

static struct platform_driver i8042_driver = {
	.driver		= {
		.name	= "i8042",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &i8042_pm_ops,
#endif
	},
	.remove		= __devexit_p(i8042_remove),
	.shutdown	= i8042_shutdown,
};

static int __init i8042_init(void)
{
	struct platform_device *pdev;
	int err;

	dbg_init();

	err = i8042_platform_init();
	if (err)
		return err;

	err = i8042_controller_check();
	if (err)
		goto err_platform_exit;

	pdev = platform_create_bundle(&i8042_driver, i8042_probe, NULL, 0, NULL, 0);
	if (IS_ERR(pdev)) {
		err = PTR_ERR(pdev);
		goto err_platform_exit;
	}

	panic_blink = i8042_panic_blink;

	return 0;

 err_platform_exit:
	i8042_platform_exit();
	return err;
}

static void __exit i8042_exit(void)
{
	platform_device_unregister(i8042_platform_device);
	platform_driver_unregister(&i8042_driver);
	i8042_platform_exit();

	panic_blink = NULL;
}

module_init(i8042_init);
module_exit(i8042_exit);
