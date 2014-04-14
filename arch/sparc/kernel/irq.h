#include <linux/platform_device.h>

#include <asm/btfixup.h>
#include <asm/cpu_type.h>

struct irq_bucket {
        struct irq_bucket *next;
        unsigned int real_irq;
        unsigned int irq;
        unsigned int pil;
};

#define SUN4D_MAX_BOARD 10
#define SUN4D_MAX_IRQ ((SUN4D_MAX_BOARD + 2) << 5)

extern struct irq_bucket *irq_map[SUN4D_MAX_IRQ];



struct sun4m_irq_percpu {
	u32	pending;
	u32	clear;
	u32	set;
};

struct sun4m_irq_global {
	u32	pending;
	u32	mask;
	u32	mask_clear;
	u32	mask_set;
	u32	interrupt_target;
};

extern struct sun4m_irq_percpu __iomem *sun4m_irq_percpu[SUN4M_NCPUS];
extern struct sun4m_irq_global __iomem *sun4m_irq_global;

struct sparc_irq_config {
	void (*init_timers)(irq_handler_t);
	unsigned int (*build_device_irq)(struct platform_device *op,
	                                 unsigned int real_irq);
};
extern struct sparc_irq_config sparc_irq_config;

unsigned int irq_alloc(unsigned int real_irq, unsigned int pil);
void irq_link(unsigned int irq);
void irq_unlink(unsigned int irq);
void handler_irq(unsigned int pil, struct pt_regs *regs);


BTFIXUPDEF_CALL(void, clear_clock_irq, void)
BTFIXUPDEF_CALL(void, load_profile_irq, int, unsigned int)

static inline void clear_clock_irq(void)
{
	BTFIXUP_CALL(clear_clock_irq)();
}

static inline void load_profile_irq(int cpu, int limit)
{
	BTFIXUP_CALL(load_profile_irq)(cpu, limit);
}

#ifdef CONFIG_SMP
BTFIXUPDEF_CALL(void, set_cpu_int, int, int)
BTFIXUPDEF_CALL(void, clear_cpu_int, int, int)
BTFIXUPDEF_CALL(void, set_irq_udt, int)

#define set_cpu_int(cpu,level) BTFIXUP_CALL(set_cpu_int)(cpu,level)
#define clear_cpu_int(cpu,level) BTFIXUP_CALL(clear_cpu_int)(cpu,level)
#define set_irq_udt(cpu) BTFIXUP_CALL(set_irq_udt)(cpu)

#define SUN4D_IPI_IRQ 13

extern void sun4d_ipi_interrupt(void);

#endif
