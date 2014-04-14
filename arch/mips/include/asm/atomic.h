/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * But use these as seldom as possible since they are much more slower
 * than regular operations.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 99, 2000, 03, 04, 06 by Ralf Baechle
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/irqflags.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/cpu-features.h>
#include <asm/cmpxchg.h>
#include <asm/war.h>

#define ATOMIC_INIT(i)    { (i) }

#define atomic_read(v)		(*(volatile int *)&(v)->counter)

#define atomic_set(v, i)		((v)->counter = (i))

static __inline__ void atomic_add(int i, atomic_t * v)
{
	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		int temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %1		# atomic_add		\n"
		"	addu	%0, %2					\n"
		"	sc	%0, %1					\n"
		"	beqzl	%0, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter));
	} else if (kernel_uses_llsc) {
		int temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	ll	%0, %1		# atomic_add	\n"
			"	addu	%0, %2				\n"
			"	sc	%0, %1				\n"
			"	.set	mips0				\n"
			: "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter));
		} while (unlikely(!temp));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		v->counter += i;
		raw_local_irq_restore(flags);
	}
}

static __inline__ void atomic_sub(int i, atomic_t * v)
{
	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		int temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %1		# atomic_sub		\n"
		"	subu	%0, %2					\n"
		"	sc	%0, %1					\n"
		"	beqzl	%0, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter));
	} else if (kernel_uses_llsc) {
		int temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	ll	%0, %1		# atomic_sub	\n"
			"	subu	%0, %2				\n"
			"	sc	%0, %1				\n"
			"	.set	mips0				\n"
			: "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter));
		} while (unlikely(!temp));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		v->counter -= i;
		raw_local_irq_restore(flags);
	}
}

static __inline__ int atomic_add_return(int i, atomic_t * v)
{
	int result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		int temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%1, %2		# atomic_add_return	\n"
		"	addu	%0, %1, %3				\n"
		"	sc	%0, %2					\n"
		"	beqzl	%0, 1b					\n"
		"	addu	%0, %1, %3				\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else if (kernel_uses_llsc) {
		int temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	ll	%1, %2	# atomic_add_return	\n"
			"	addu	%0, %1, %3			\n"
			"	sc	%0, %2				\n"
			"	.set	mips0				\n"
			: "=&r" (result), "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter)
			: "memory");
		} while (unlikely(!result));

		result = temp + i;
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result += i;
		v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

static __inline__ int atomic_sub_return(int i, atomic_t * v)
{
	int result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		int temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%1, %2		# atomic_sub_return	\n"
		"	subu	%0, %1, %3				\n"
		"	sc	%0, %2					\n"
		"	beqzl	%0, 1b					\n"
		"	subu	%0, %1, %3				\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");

		result = temp - i;
	} else if (kernel_uses_llsc) {
		int temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	ll	%1, %2	# atomic_sub_return	\n"
			"	subu	%0, %1, %3			\n"
			"	sc	%0, %2				\n"
			"	.set	mips0				\n"
			: "=&r" (result), "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter)
			: "memory");
		} while (unlikely(!result));

		result = temp - i;
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result -= i;
		v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

static __inline__ int atomic_sub_if_positive(int i, atomic_t * v)
{
	int result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		int temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%1, %2		# atomic_sub_if_positive\n"
		"	subu	%0, %1, %3				\n"
		"	bltz	%0, 1f					\n"
		"	sc	%0, %2					\n"
		"	.set	noreorder				\n"
		"	beqzl	%0, 1b					\n"
		"	 subu	%0, %1, %3				\n"
		"	.set	reorder					\n"
		"1:							\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else if (kernel_uses_llsc) {
		int temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%1, %2		# atomic_sub_if_positive\n"
		"	subu	%0, %1, %3				\n"
		"	bltz	%0, 1f					\n"
		"	sc	%0, %2					\n"
		"	.set	noreorder				\n"
		"	beqz	%0, 1b					\n"
		"	 subu	%0, %1, %3				\n"
		"	.set	reorder					\n"
		"1:							\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result -= i;
		if (result >= 0)
			v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), (new)))

static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c;
}

#define atomic_dec_return(v) atomic_sub_return(1, (v))
#define atomic_inc_return(v) atomic_add_return(1, (v))

#define atomic_sub_and_test(i, v) (atomic_sub_return((i), (v)) == 0)

#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)

#define atomic_dec_if_positive(v)	atomic_sub_if_positive(1, v)

#define atomic_inc(v) atomic_add(1, (v))

#define atomic_dec(v) atomic_sub(1, (v))

#define atomic_add_negative(i, v) (atomic_add_return(i, (v)) < 0)

#ifdef CONFIG_64BIT

#define ATOMIC64_INIT(i)    { (i) }

#define atomic64_read(v)	(*(volatile long *)&(v)->counter)

#define atomic64_set(v, i)	((v)->counter = (i))

static __inline__ void atomic64_add(long i, atomic64_t * v)
{
	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		long temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%0, %1		# atomic64_add		\n"
		"	daddu	%0, %2					\n"
		"	scd	%0, %1					\n"
		"	beqzl	%0, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter));
	} else if (kernel_uses_llsc) {
		long temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	lld	%0, %1		# atomic64_add	\n"
			"	daddu	%0, %2				\n"
			"	scd	%0, %1				\n"
			"	.set	mips0				\n"
			: "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter));
		} while (unlikely(!temp));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		v->counter += i;
		raw_local_irq_restore(flags);
	}
}

static __inline__ void atomic64_sub(long i, atomic64_t * v)
{
	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		long temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%0, %1		# atomic64_sub		\n"
		"	dsubu	%0, %2					\n"
		"	scd	%0, %1					\n"
		"	beqzl	%0, 1b					\n"
		"	.set	mips0					\n"
		: "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter));
	} else if (kernel_uses_llsc) {
		long temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	lld	%0, %1		# atomic64_sub	\n"
			"	dsubu	%0, %2				\n"
			"	scd	%0, %1				\n"
			"	.set	mips0				\n"
			: "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter));
		} while (unlikely(!temp));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		v->counter -= i;
		raw_local_irq_restore(flags);
	}
}

static __inline__ long atomic64_add_return(long i, atomic64_t * v)
{
	long result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		long temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%1, %2		# atomic64_add_return	\n"
		"	daddu	%0, %1, %3				\n"
		"	scd	%0, %2					\n"
		"	beqzl	%0, 1b					\n"
		"	daddu	%0, %1, %3				\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else if (kernel_uses_llsc) {
		long temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	lld	%1, %2	# atomic64_add_return	\n"
			"	daddu	%0, %1, %3			\n"
			"	scd	%0, %2				\n"
			"	.set	mips0				\n"
			: "=&r" (result), "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter)
			: "memory");
		} while (unlikely(!result));

		result = temp + i;
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result += i;
		v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

static __inline__ long atomic64_sub_return(long i, atomic64_t * v)
{
	long result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		long temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%1, %2		# atomic64_sub_return	\n"
		"	dsubu	%0, %1, %3				\n"
		"	scd	%0, %2					\n"
		"	beqzl	%0, 1b					\n"
		"	dsubu	%0, %1, %3				\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else if (kernel_uses_llsc) {
		long temp;

		do {
			__asm__ __volatile__(
			"	.set	mips3				\n"
			"	lld	%1, %2	# atomic64_sub_return	\n"
			"	dsubu	%0, %1, %3			\n"
			"	scd	%0, %2				\n"
			"	.set	mips0				\n"
			: "=&r" (result), "=&r" (temp), "=m" (v->counter)
			: "Ir" (i), "m" (v->counter)
			: "memory");
		} while (unlikely(!result));

		result = temp - i;
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result -= i;
		v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

static __inline__ long atomic64_sub_if_positive(long i, atomic64_t * v)
{
	long result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc && R10000_LLSC_WAR) {
		long temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%1, %2		# atomic64_sub_if_positive\n"
		"	dsubu	%0, %1, %3				\n"
		"	bltz	%0, 1f					\n"
		"	scd	%0, %2					\n"
		"	.set	noreorder				\n"
		"	beqzl	%0, 1b					\n"
		"	 dsubu	%0, %1, %3				\n"
		"	.set	reorder					\n"
		"1:							\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else if (kernel_uses_llsc) {
		long temp;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%1, %2		# atomic64_sub_if_positive\n"
		"	dsubu	%0, %1, %3				\n"
		"	bltz	%0, 1f					\n"
		"	scd	%0, %2					\n"
		"	.set	noreorder				\n"
		"	beqz	%0, 1b					\n"
		"	 dsubu	%0, %1, %3				\n"
		"	.set	reorder					\n"
		"1:							\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp), "=m" (v->counter)
		: "Ir" (i), "m" (v->counter)
		: "memory");
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result -= i;
		if (result >= 0)
			v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), (new)))

static __inline__ int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long c, old;
	c = atomic64_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic64_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define atomic64_inc_not_zero(v) atomic64_add_unless((v), 1, 0)

#define atomic64_dec_return(v) atomic64_sub_return(1, (v))
#define atomic64_inc_return(v) atomic64_add_return(1, (v))

#define atomic64_sub_and_test(i, v) (atomic64_sub_return((i), (v)) == 0)

#define atomic64_inc_and_test(v) (atomic64_inc_return(v) == 0)

#define atomic64_dec_and_test(v) (atomic64_sub_return(1, (v)) == 0)

#define atomic64_dec_if_positive(v)	atomic64_sub_if_positive(1, v)

#define atomic64_inc(v) atomic64_add(1, (v))

#define atomic64_dec(v) atomic64_sub(1, (v))

#define atomic64_add_negative(i, v) (atomic64_add_return(i, (v)) < 0)

#endif 

#define smp_mb__before_atomic_dec()	smp_mb__before_llsc()
#define smp_mb__after_atomic_dec()	smp_llsc_mb()
#define smp_mb__before_atomic_inc()	smp_mb__before_llsc()
#define smp_mb__after_atomic_inc()	smp_llsc_mb()

#endif 
