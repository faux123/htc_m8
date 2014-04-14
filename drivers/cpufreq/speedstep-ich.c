/*
 * (C) 2001  Dave Jones, Arjan van de ven.
 * (C) 2002 - 2003  Dominik Brodowski <linux@brodo.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *  Based upon reverse engineered information, and on Intel documentation
 *  for chipsets ICH2-M and ICH3-M.
 *
 *  Many thanks to Ducrot Bruno for finding and fixing the last
 *  "missing link" for ICH2-M/ICH3-M support, and to Thomas Winkler
 *  for extensive testing.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */



#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/cpu_device_id.h>

#include "speedstep-lib.h"


static struct pci_dev *speedstep_chipset_dev;


static enum speedstep_processor speedstep_processor;

static u32 pmbase;

static struct cpufreq_frequency_table speedstep_freqs[] = {
	{SPEEDSTEP_HIGH,	0},
	{SPEEDSTEP_LOW,		0},
	{0,			CPUFREQ_TABLE_END},
};


static int speedstep_find_register(void)
{
	if (!speedstep_chipset_dev)
		return -ENODEV;

	
	pci_read_config_dword(speedstep_chipset_dev, 0x40, &pmbase);
	if (!(pmbase & 0x01)) {
		printk(KERN_ERR "speedstep-ich: could not find speedstep register\n");
		return -ENODEV;
	}

	pmbase &= 0xFFFFFFFE;
	if (!pmbase) {
		printk(KERN_ERR "speedstep-ich: could not find speedstep register\n");
		return -ENODEV;
	}

	pr_debug("pmbase is 0x%x\n", pmbase);
	return 0;
}

static void speedstep_set_state(unsigned int state)
{
	u8 pm2_blk;
	u8 value;
	unsigned long flags;

	if (state > 0x1)
		return;

	
	local_irq_save(flags);

	
	value = inb(pmbase + 0x50);

	pr_debug("read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

	
	value &= 0xFE;
	value |= state;

	pr_debug("writing 0x%x to pmbase 0x%x + 0x50\n", value, pmbase);

	
	pm2_blk = inb(pmbase + 0x20);
	pm2_blk |= 0x01;
	outb(pm2_blk, (pmbase + 0x20));

	
	outb(value, (pmbase + 0x50));

	
	pm2_blk &= 0xfe;
	outb(pm2_blk, (pmbase + 0x20));

	
	value = inb(pmbase + 0x50);

	
	local_irq_restore(flags);

	pr_debug("read at pmbase 0x%x + 0x50 returned 0x%x\n", pmbase, value);

	if (state == (value & 0x1))
		pr_debug("change to %u MHz succeeded\n",
			speedstep_get_frequency(speedstep_processor) / 1000);
	else
		printk(KERN_ERR "cpufreq: change failed - I/O error\n");

	return;
}

static void _speedstep_set_state(void *_state)
{
	speedstep_set_state(*(unsigned int *)_state);
}

static int speedstep_activate(void)
{
	u16 value = 0;

	if (!speedstep_chipset_dev)
		return -EINVAL;

	pci_read_config_word(speedstep_chipset_dev, 0x00A0, &value);
	if (!(value & 0x08)) {
		value |= 0x08;
		pr_debug("activating SpeedStep (TM) registers\n");
		pci_write_config_word(speedstep_chipset_dev, 0x00A0, value);
	}

	return 0;
}


static unsigned int speedstep_detect_chipset(void)
{
	speedstep_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801DB_12,
			      PCI_ANY_ID, PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return 4; 

	speedstep_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801CA_12,
			      PCI_ANY_ID, PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev)
		return 3; 


	speedstep_chipset_dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82801BA_10,
			      PCI_ANY_ID, PCI_ANY_ID,
			      NULL);
	if (speedstep_chipset_dev) {
		static struct pci_dev *hostbridge;

		hostbridge  = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			      PCI_DEVICE_ID_INTEL_82815_MC,
			      PCI_ANY_ID, PCI_ANY_ID,
			      NULL);

		if (!hostbridge)
			return 2; 

		if (hostbridge->revision < 5) {
			pr_debug("hostbridge does not support speedstep\n");
			speedstep_chipset_dev = NULL;
			pci_dev_put(hostbridge);
			return 0;
		}

		pci_dev_put(hostbridge);
		return 2; 
	}

	return 0;
}

static void get_freq_data(void *_speed)
{
	unsigned int *speed = _speed;

	*speed = speedstep_get_frequency(speedstep_processor);
}

static unsigned int speedstep_get(unsigned int cpu)
{
	unsigned int speed;

	
	if (smp_call_function_single(cpu, get_freq_data, &speed, 1) != 0)
		BUG();

	pr_debug("detected %u kHz as current frequency\n", speed);
	return speed;
}

static int speedstep_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int newstate = 0, policy_cpu;
	struct cpufreq_freqs freqs;
	int i;

	if (cpufreq_frequency_table_target(policy, &speedstep_freqs[0],
				target_freq, relation, &newstate))
		return -EINVAL;

	policy_cpu = cpumask_any_and(policy->cpus, cpu_online_mask);
	freqs.old = speedstep_get(policy_cpu);
	freqs.new = speedstep_freqs[newstate].frequency;
	freqs.cpu = policy->cpu;

	pr_debug("transiting from %u to %u kHz\n", freqs.old, freqs.new);

	
	if (freqs.old == freqs.new)
		return 0;

	for_each_cpu(i, policy->cpus) {
		freqs.cpu = i;
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	}

	smp_call_function_single(policy_cpu, _speedstep_set_state, &newstate,
				 true);

	for_each_cpu(i, policy->cpus) {
		freqs.cpu = i;
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

	return 0;
}


static int speedstep_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &speedstep_freqs[0]);
}

struct get_freqs {
	struct cpufreq_policy *policy;
	int ret;
};

static void get_freqs_on_cpu(void *_get_freqs)
{
	struct get_freqs *get_freqs = _get_freqs;

	get_freqs->ret =
		speedstep_get_freqs(speedstep_processor,
			    &speedstep_freqs[SPEEDSTEP_LOW].frequency,
			    &speedstep_freqs[SPEEDSTEP_HIGH].frequency,
			    &get_freqs->policy->cpuinfo.transition_latency,
			    &speedstep_set_state);
}

static int speedstep_cpu_init(struct cpufreq_policy *policy)
{
	int result;
	unsigned int policy_cpu, speed;
	struct get_freqs gf;

	
#ifdef CONFIG_SMP
	cpumask_copy(policy->cpus, cpu_sibling_mask(policy->cpu));
#endif
	policy_cpu = cpumask_any_and(policy->cpus, cpu_online_mask);

	
	gf.policy = policy;
	smp_call_function_single(policy_cpu, get_freqs_on_cpu, &gf, 1);
	if (gf.ret)
		return gf.ret;

	
	speed = speedstep_get(policy_cpu);
	if (!speed)
		return -EIO;

	pr_debug("currently at %s speed setting - %i MHz\n",
		(speed == speedstep_freqs[SPEEDSTEP_LOW].frequency)
		? "low" : "high",
		(speed / 1000));

	
	policy->cur = speed;

	result = cpufreq_frequency_table_cpuinfo(policy, speedstep_freqs);
	if (result)
		return result;

	cpufreq_frequency_table_get_attr(speedstep_freqs, policy->cpu);

	return 0;
}


static int speedstep_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *speedstep_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};


static struct cpufreq_driver speedstep_driver = {
	.name	= "speedstep-ich",
	.verify	= speedstep_verify,
	.target	= speedstep_target,
	.init	= speedstep_cpu_init,
	.exit	= speedstep_cpu_exit,
	.get	= speedstep_get,
	.owner	= THIS_MODULE,
	.attr	= speedstep_attr,
};

static const struct x86_cpu_id ss_smi_ids[] = {
	{ X86_VENDOR_INTEL, 6, 0xb, },
	{ X86_VENDOR_INTEL, 6, 0x8, },
	{ X86_VENDOR_INTEL, 15, 2 },
	{}
};
#if 0
MODULE_DEVICE_TABLE(x86cpu, ss_smi_ids);
#endif

static int __init speedstep_init(void)
{
	if (!x86_match_cpu(ss_smi_ids))
		return -ENODEV;

	
	speedstep_processor = speedstep_detect_processor();
	if (!speedstep_processor) {
		pr_debug("Intel(R) SpeedStep(TM) capable processor "
				"not found\n");
		return -ENODEV;
	}

	
	if (!speedstep_detect_chipset()) {
		pr_debug("Intel(R) SpeedStep(TM) for this chipset not "
				"(yet) available.\n");
		return -ENODEV;
	}

	
	if (speedstep_activate()) {
		pci_dev_put(speedstep_chipset_dev);
		return -EINVAL;
	}

	if (speedstep_find_register())
		return -ENODEV;

	return cpufreq_register_driver(&speedstep_driver);
}


static void __exit speedstep_exit(void)
{
	pci_dev_put(speedstep_chipset_dev);
	cpufreq_unregister_driver(&speedstep_driver);
}


MODULE_AUTHOR("Dave Jones <davej@redhat.com>, "
		"Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("Speedstep driver for Intel mobile processors on chipsets "
		"with ICH-M southbridges.");
MODULE_LICENSE("GPL");

module_init(speedstep_init);
module_exit(speedstep_exit);
