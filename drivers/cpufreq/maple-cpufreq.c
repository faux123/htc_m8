/*
 *  Copyright (C) 2011 Dmitry Eremin-Solenikov
 *  Copyright (C) 2002 - 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *  and                       Markus Demleitner <msdemlei@cl.uni-heidelberg.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver adds basic cpufreq support for SMU & 970FX based G5 Macs,
 * that is iMac G5 and latest single CPU desktop.
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/time.h>
#include <linux/of.h>

#define DBG(fmt...) pr_debug(fmt)


#define SCOM_PCR 0x0aa001			

#define PCR_HILO_SELECT		0x80000000U	
#define PCR_SPEED_FULL		0x00000000U	
#define PCR_SPEED_HALF		0x00020000U	
#define PCR_SPEED_QUARTER	0x00040000U	
#define PCR_SPEED_MASK		0x000e0000U	
#define PCR_SPEED_SHIFT		17
#define PCR_FREQ_REQ_VALID	0x00010000U	
#define PCR_VOLT_REQ_VALID	0x00008000U	
#define PCR_TARGET_TIME_MASK	0x00006000U	
#define PCR_STATLAT_MASK	0x00001f00U	
#define PCR_SNOOPLAT_MASK	0x000000f0U	
#define PCR_SNOOPACC_MASK	0x0000000fU	

#define SCOM_PSR 0x408001			
#define PSR_CMD_RECEIVED	0x2000000000000000U   
#define PSR_CMD_COMPLETED	0x1000000000000000U   
#define PSR_CUR_SPEED_MASK	0x0300000000000000U   
#define PSR_CUR_SPEED_SHIFT	(56)

#define CPUFREQ_HIGH                  0
#define CPUFREQ_LOW                   1

static struct cpufreq_frequency_table maple_cpu_freqs[] = {
	{CPUFREQ_HIGH,		0},
	{CPUFREQ_LOW,		0},
	{0,			CPUFREQ_TABLE_END},
};

static struct freq_attr *maple_cpu_freqs_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static int maple_pmode_cur;

static DEFINE_MUTEX(maple_switch_mutex);

static const u32 *maple_pmode_data;
static int maple_pmode_max;

static int maple_scom_switch_freq(int speed_mode)
{
	unsigned long flags;
	int to;

	local_irq_save(flags);

	
	scom970_write(SCOM_PCR, 0);
	
	scom970_write(SCOM_PCR, PCR_HILO_SELECT | 0);
	
	scom970_write(SCOM_PCR, PCR_HILO_SELECT |
		      maple_pmode_data[speed_mode]);

	
	for (to = 0; to < 10; to++) {
		unsigned long psr = scom970_read(SCOM_PSR);

		if ((psr & PSR_CMD_RECEIVED) == 0 &&
		    (((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (maple_pmode_data[speed_mode] >> PCR_SPEED_SHIFT)) & 0x3)
		    == 0)
			break;
		if (psr & PSR_CMD_COMPLETED)
			break;
		udelay(100);
	}

	local_irq_restore(flags);

	maple_pmode_cur = speed_mode;
	ppc_proc_freq = maple_cpu_freqs[speed_mode].frequency * 1000ul;

	return 0;
}

static int maple_scom_query_freq(void)
{
	unsigned long psr = scom970_read(SCOM_PSR);
	int i;

	for (i = 0; i <= maple_pmode_max; i++)
		if ((((psr >> PSR_CUR_SPEED_SHIFT) ^
		      (maple_pmode_data[i] >> PCR_SPEED_SHIFT)) & 0x3) == 0)
			break;
	return i;
}


static int maple_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, maple_cpu_freqs);
}

static int maple_cpufreq_target(struct cpufreq_policy *policy,
	unsigned int target_freq, unsigned int relation)
{
	unsigned int newstate = 0;
	struct cpufreq_freqs freqs;
	int rc;

	if (cpufreq_frequency_table_target(policy, maple_cpu_freqs,
			target_freq, relation, &newstate))
		return -EINVAL;

	if (maple_pmode_cur == newstate)
		return 0;

	mutex_lock(&maple_switch_mutex);

	freqs.old = maple_cpu_freqs[maple_pmode_cur].frequency;
	freqs.new = maple_cpu_freqs[newstate].frequency;
	freqs.cpu = 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	rc = maple_scom_switch_freq(newstate);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	mutex_unlock(&maple_switch_mutex);

	return rc;
}

static unsigned int maple_cpufreq_get_speed(unsigned int cpu)
{
	return maple_cpu_freqs[maple_pmode_cur].frequency;
}

static int maple_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	policy->cpuinfo.transition_latency = 12000;
	policy->cur = maple_cpu_freqs[maple_scom_query_freq()].frequency;
	cpumask_copy(policy->cpus, cpu_online_mask);
	cpufreq_frequency_table_get_attr(maple_cpu_freqs, policy->cpu);

	return cpufreq_frequency_table_cpuinfo(policy,
		maple_cpu_freqs);
}


static struct cpufreq_driver maple_cpufreq_driver = {
	.name		= "maple",
	.owner		= THIS_MODULE,
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= maple_cpufreq_cpu_init,
	.verify		= maple_cpufreq_verify,
	.target		= maple_cpufreq_target,
	.get		= maple_cpufreq_get_speed,
	.attr		= maple_cpu_freqs_attr,
};

static int __init maple_cpufreq_init(void)
{
	struct device_node *cpus;
	struct device_node *cpunode;
	unsigned int psize;
	unsigned long max_freq;
	const u32 *valp;
	u32 pvr_hi;
	int rc = -ENODEV;

	if (!of_machine_is_compatible("Momentum,Maple") &&
	    !of_machine_is_compatible("Momentum,Apache"))
		return 0;

	cpus = of_find_node_by_path("/cpus");
	if (cpus == NULL) {
		DBG("No /cpus node !\n");
		return -ENODEV;
	}

	
	for (cpunode = NULL;
	     (cpunode = of_get_next_child(cpus, cpunode)) != NULL;) {
		const u32 *reg = of_get_property(cpunode, "reg", NULL);
		if (reg == NULL || (*reg) != 0)
			continue;
		if (!strcmp(cpunode->type, "cpu"))
			break;
	}
	if (cpunode == NULL) {
		printk(KERN_ERR "cpufreq: Can't find any CPU 0 node\n");
		goto bail_cpus;
	}

	
	
	pvr_hi = PVR_VER(mfspr(SPRN_PVR));
	if (pvr_hi != 0x3c && pvr_hi != 0x44) {
		printk(KERN_ERR "cpufreq: Unsupported CPU version (%x)\n",
				pvr_hi);
		goto bail_noprops;
	}

	
	maple_pmode_data = of_get_property(cpunode, "power-mode-data", &psize);
	if (!maple_pmode_data) {
		DBG("No power-mode-data !\n");
		goto bail_noprops;
	}
	maple_pmode_max = psize / sizeof(u32) - 1;

	valp = of_get_property(cpunode, "clock-frequency", NULL);
	if (!valp)
		return -ENODEV;
	max_freq = (*valp)/1000;
	maple_cpu_freqs[0].frequency = max_freq;
	maple_cpu_freqs[1].frequency = max_freq/2;

	msleep(10);
	maple_pmode_cur = -1;
	maple_scom_switch_freq(maple_scom_query_freq());

	printk(KERN_INFO "Registering Maple CPU frequency driver\n");
	printk(KERN_INFO "Low: %d Mhz, High: %d Mhz, Cur: %d MHz\n",
		maple_cpu_freqs[1].frequency/1000,
		maple_cpu_freqs[0].frequency/1000,
		maple_cpu_freqs[maple_pmode_cur].frequency/1000);

	rc = cpufreq_register_driver(&maple_cpufreq_driver);

	of_node_put(cpunode);
	of_node_put(cpus);

	return rc;

bail_noprops:
	of_node_put(cpunode);
bail_cpus:
	of_node_put(cpus);

	return rc;
}

module_init(maple_cpufreq_init);


MODULE_LICENSE("GPL");
