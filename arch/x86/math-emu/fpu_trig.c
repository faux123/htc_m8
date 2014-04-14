/*---------------------------------------------------------------------------+
 |  fpu_trig.c                                                               |
 |                                                                           |
 | Implementation of the FPU "transcendental" functions.                     |
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1997,1999                                    |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail   billm@melbpc.org.au            |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#include "fpu_system.h"
#include "exception.h"
#include "fpu_emu.h"
#include "status_w.h"
#include "control_w.h"
#include "reg_constant.h"

static void rem_kernel(unsigned long long st0, unsigned long long *y,
		       unsigned long long st1, unsigned long long q, int n);

#define BETTER_THAN_486

#define FCOS  4

static int trig_arg(FPU_REG *st0_ptr, int even)
{
	FPU_REG tmp;
	u_char tmptag;
	unsigned long long q;
	int old_cw = control_word, saved_status = partial_status;
	int tag, st0_tag = TAG_Valid;

	if (exponent(st0_ptr) >= 63) {
		partial_status |= SW_C2;	
		return -1;
	}

	control_word &= ~CW_RC;
	control_word |= RC_CHOP;

	setpositive(st0_ptr);
	tag = FPU_u_div(st0_ptr, &CONST_PI2, &tmp, PR_64_BITS | RC_CHOP | 0x3f,
			SIGN_POS);

	FPU_round_to_int(&tmp, tag);	
	q = significand(&tmp);
	if (q) {
		rem_kernel(significand(st0_ptr),
			   &significand(&tmp),
			   significand(&CONST_PI2),
			   q, exponent(st0_ptr) - exponent(&CONST_PI2));
		setexponent16(&tmp, exponent(&CONST_PI2));
		st0_tag = FPU_normalize(&tmp);
		FPU_copy_to_reg0(&tmp, st0_tag);
	}

	if ((even && !(q & 1)) || (!even && (q & 1))) {
		st0_tag =
		    FPU_sub(REV | LOADED | TAG_Valid, (int)&CONST_PI2,
			    FULL_PRECISION);

#ifdef BETTER_THAN_486
		if ((exponent(st0_ptr) <= exponent(&CONST_PI2extra) + 64)
		    || (q > 1)) {

			significand(&tmp) = q + 1;
			setexponent16(&tmp, 63);
			FPU_normalize(&tmp);
			tmptag =
			    FPU_u_mul(&CONST_PI2extra, &tmp, &tmp,
				      FULL_PRECISION, SIGN_POS,
				      exponent(&CONST_PI2extra) +
				      exponent(&tmp));
			setsign(&tmp, getsign(&CONST_PI2extra));
			st0_tag = FPU_add(&tmp, tmptag, 0, FULL_PRECISION);
			if (signnegative(st0_ptr)) {
				setpositive(st0_ptr);
				q++;
			}
		}
#endif 
	}
#ifdef BETTER_THAN_486
	else {
		if (((q > 0)
		     && (exponent(st0_ptr) <= exponent(&CONST_PI2extra) + 64))
		    || (q > 1)) {

			significand(&tmp) = q;
			setexponent16(&tmp, 63);
			FPU_normalize(&tmp);	
			tmptag =
			    FPU_u_mul(&CONST_PI2extra, &tmp, &tmp,
				      FULL_PRECISION, SIGN_POS,
				      exponent(&CONST_PI2extra) +
				      exponent(&tmp));
			setsign(&tmp, getsign(&CONST_PI2extra));
			st0_tag = FPU_sub(LOADED | (tmptag & 0x0f), (int)&tmp,
					  FULL_PRECISION);
			if ((exponent(st0_ptr) == exponent(&CONST_PI2)) &&
			    ((st0_ptr->sigh > CONST_PI2.sigh)
			     || ((st0_ptr->sigh == CONST_PI2.sigh)
				 && (st0_ptr->sigl > CONST_PI2.sigl)))) {
				st0_tag =
				    FPU_sub(REV | LOADED | TAG_Valid,
					    (int)&CONST_PI2, FULL_PRECISION);
				q++;
			}
		}
	}
#endif 

	FPU_settag0(st0_tag);
	control_word = old_cw;
	partial_status = saved_status & ~SW_C2;	

	return (q & 3) | even;
}

static void convert_l2reg(long const *arg, int deststnr)
{
	int tag;
	long num = *arg;
	u_char sign;
	FPU_REG *dest = &st(deststnr);

	if (num == 0) {
		FPU_copy_to_regi(&CONST_Z, TAG_Zero, deststnr);
		return;
	}

	if (num > 0) {
		sign = SIGN_POS;
	} else {
		num = -num;
		sign = SIGN_NEG;
	}

	dest->sigh = num;
	dest->sigl = 0;
	setexponent16(dest, 31);
	tag = FPU_normalize(dest);
	FPU_settagi(deststnr, tag);
	setsign(dest, sign);
	return;
}

static void single_arg_error(FPU_REG *st0_ptr, u_char st0_tag)
{
	if (st0_tag == TAG_Empty)
		FPU_stack_underflow();	
	else if (st0_tag == TW_NaN)
		real_1op_NaN(st0_ptr);	
#ifdef PARANOID
	else
		EXCEPTION(EX_INTERNAL | 0x0112);
#endif 
}

static void single_arg_2_error(FPU_REG *st0_ptr, u_char st0_tag)
{
	int isNaN;

	switch (st0_tag) {
	case TW_NaN:
		isNaN = (exponent(st0_ptr) == EXP_OVER)
		    && (st0_ptr->sigh & 0x80000000);
		if (isNaN && !(st0_ptr->sigh & 0x40000000)) {	
			EXCEPTION(EX_Invalid);
			if (control_word & CW_Invalid) {
				
				
				st0_ptr->sigh |= 0x40000000;
				push();
				FPU_copy_to_reg0(st0_ptr, TAG_Special);
			}
		} else if (isNaN) {
			
			push();
			FPU_copy_to_reg0(st0_ptr, TAG_Special);
		} else {
			
			EXCEPTION(EX_Invalid);
			if (control_word & CW_Invalid) {
				
				FPU_copy_to_reg0(&CONST_QNaN, TAG_Special);
				push();
				FPU_copy_to_reg0(&CONST_QNaN, TAG_Special);
			}
		}
		break;		
#ifdef PARANOID
	default:
		EXCEPTION(EX_INTERNAL | 0x0112);
#endif 
	}
}


static void f2xm1(FPU_REG *st0_ptr, u_char tag)
{
	FPU_REG a;

	clear_C1();

	if (tag == TAG_Valid) {
		
		if (exponent(st0_ptr) < 0) {
		      denormal_arg:

			FPU_to_exp16(st0_ptr, &a);

			
			poly_2xm1(getsign(st0_ptr), &a, st0_ptr);
		}
		set_precision_flag_up();	
		return;
	}

	if (tag == TAG_Zero)
		return;

	if (tag == TAG_Special)
		tag = FPU_Special(st0_ptr);

	switch (tag) {
	case TW_Denormal:
		if (denormal_operand() < 0)
			return;
		goto denormal_arg;
	case TW_Infinity:
		if (signnegative(st0_ptr)) {
			
			FPU_copy_to_reg0(&CONST_1, TAG_Valid);
			setnegative(st0_ptr);
		}
		return;
	default:
		single_arg_error(st0_ptr, tag);
	}
}

static void fptan(FPU_REG *st0_ptr, u_char st0_tag)
{
	FPU_REG *st_new_ptr;
	int q;
	u_char arg_sign = getsign(st0_ptr);

	
	if (st0_tag == TAG_Empty) {
		FPU_stack_underflow();	
		if (control_word & CW_Invalid) {
			st_new_ptr = &st(-1);
			push();
			FPU_stack_underflow();	
		}
		return;
	}

	if (STACK_OVERFLOW) {
		FPU_stack_overflow();
		return;
	}

	if (st0_tag == TAG_Valid) {
		if (exponent(st0_ptr) > -40) {
			if ((q = trig_arg(st0_ptr, 0)) == -1) {
				
				return;
			}

			poly_tan(st0_ptr);
			setsign(st0_ptr, (q & 1) ^ (arg_sign != 0));
			set_precision_flag_up();	
		} else {
			
			

		      denormal_arg:

			FPU_to_exp16(st0_ptr, st0_ptr);

			st0_tag =
			    FPU_round(st0_ptr, 1, 0, FULL_PRECISION, arg_sign);
			FPU_settag0(st0_tag);
		}
		push();
		FPU_copy_to_reg0(&CONST_1, TAG_Valid);
		return;
	}

	if (st0_tag == TAG_Zero) {
		push();
		FPU_copy_to_reg0(&CONST_1, TAG_Valid);
		setcc(0);
		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);

	if (st0_tag == TW_Denormal) {
		if (denormal_operand() < 0)
			return;

		goto denormal_arg;
	}

	if (st0_tag == TW_Infinity) {
		
		if (arith_invalid(0) >= 0) {
			st_new_ptr = &st(-1);
			push();
			arith_invalid(0);
		}
		return;
	}

	single_arg_2_error(st0_ptr, st0_tag);
}

static void fxtract(FPU_REG *st0_ptr, u_char st0_tag)
{
	FPU_REG *st_new_ptr;
	u_char sign;
	register FPU_REG *st1_ptr = st0_ptr;	

	if (STACK_OVERFLOW) {
		FPU_stack_overflow();
		return;
	}

	clear_C1();

	if (st0_tag == TAG_Valid) {
		long e;

		push();
		sign = getsign(st1_ptr);
		reg_copy(st1_ptr, st_new_ptr);
		setexponent16(st_new_ptr, exponent(st_new_ptr));

	      denormal_arg:

		e = exponent16(st_new_ptr);
		convert_l2reg(&e, 1);
		setexponentpos(st_new_ptr, 0);
		setsign(st_new_ptr, sign);
		FPU_settag0(TAG_Valid);	
		return;
	} else if (st0_tag == TAG_Zero) {
		sign = getsign(st0_ptr);

		if (FPU_divide_by_zero(0, SIGN_NEG) < 0)
			return;

		push();
		FPU_copy_to_reg0(&CONST_Z, TAG_Zero);
		setsign(st_new_ptr, sign);
		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);

	if (st0_tag == TW_Denormal) {
		if (denormal_operand() < 0)
			return;

		push();
		sign = getsign(st1_ptr);
		FPU_to_exp16(st1_ptr, st_new_ptr);
		goto denormal_arg;
	} else if (st0_tag == TW_Infinity) {
		sign = getsign(st0_ptr);
		setpositive(st0_ptr);
		push();
		FPU_copy_to_reg0(&CONST_INF, TAG_Special);
		setsign(st_new_ptr, sign);
		return;
	} else if (st0_tag == TW_NaN) {
		if (real_1op_NaN(st0_ptr) < 0)
			return;

		push();
		FPU_copy_to_reg0(st0_ptr, TAG_Special);
		return;
	} else if (st0_tag == TAG_Empty) {
		
		if (control_word & EX_Invalid) {
			FPU_stack_underflow();
			push();
			FPU_stack_underflow();
		} else
			EXCEPTION(EX_StackUnder);
	}
#ifdef PARANOID
	else
		EXCEPTION(EX_INTERNAL | 0x119);
#endif 
}

static void fdecstp(void)
{
	clear_C1();
	top--;
}

static void fincstp(void)
{
	clear_C1();
	top++;
}

static void fsqrt_(FPU_REG *st0_ptr, u_char st0_tag)
{
	int expon;

	clear_C1();

	if (st0_tag == TAG_Valid) {
		u_char tag;

		if (signnegative(st0_ptr)) {
			arith_invalid(0);	
			return;
		}

		
		expon = exponent(st0_ptr);

	      denormal_arg:

		setexponent16(st0_ptr, (expon & 1));

		
		tag = wm_sqrt(st0_ptr, 0, 0, control_word, SIGN_POS);
		addexponent(st0_ptr, expon >> 1);
		FPU_settag0(tag);
		return;
	}

	if (st0_tag == TAG_Zero)
		return;

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);

	if (st0_tag == TW_Infinity) {
		if (signnegative(st0_ptr))
			arith_invalid(0);	
		return;
	} else if (st0_tag == TW_Denormal) {
		if (signnegative(st0_ptr)) {
			arith_invalid(0);	
			return;
		}

		if (denormal_operand() < 0)
			return;

		FPU_to_exp16(st0_ptr, st0_ptr);

		expon = exponent16(st0_ptr);

		goto denormal_arg;
	}

	single_arg_error(st0_ptr, st0_tag);

}

static void frndint_(FPU_REG *st0_ptr, u_char st0_tag)
{
	int flags, tag;

	if (st0_tag == TAG_Valid) {
		u_char sign;

	      denormal_arg:

		sign = getsign(st0_ptr);

		if (exponent(st0_ptr) > 63)
			return;

		if (st0_tag == TW_Denormal) {
			if (denormal_operand() < 0)
				return;
		}

		
		if ((flags = FPU_round_to_int(st0_ptr, st0_tag)))
			set_precision_flag(flags);

		setexponent16(st0_ptr, 63);
		tag = FPU_normalize(st0_ptr);
		setsign(st0_ptr, sign);
		FPU_settag0(tag);
		return;
	}

	if (st0_tag == TAG_Zero)
		return;

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);

	if (st0_tag == TW_Denormal)
		goto denormal_arg;
	else if (st0_tag == TW_Infinity)
		return;
	else
		single_arg_error(st0_ptr, st0_tag);
}

static int fsin(FPU_REG *st0_ptr, u_char tag)
{
	u_char arg_sign = getsign(st0_ptr);

	if (tag == TAG_Valid) {
		int q;

		if (exponent(st0_ptr) > -40) {
			if ((q = trig_arg(st0_ptr, 0)) == -1) {
				
				return 1;
			}

			poly_sine(st0_ptr);

			if (q & 2)
				changesign(st0_ptr);

			setsign(st0_ptr, getsign(st0_ptr) ^ arg_sign);

			
			set_precision_flag_up();
			return 0;
		} else {
			
			set_precision_flag_up();	
			return 0;
		}
	}

	if (tag == TAG_Zero) {
		setcc(0);
		return 0;
	}

	if (tag == TAG_Special)
		tag = FPU_Special(st0_ptr);

	if (tag == TW_Denormal) {
		if (denormal_operand() < 0)
			return 1;

		
		
		FPU_to_exp16(st0_ptr, st0_ptr);

		tag = FPU_round(st0_ptr, 1, 0, FULL_PRECISION, arg_sign);

		FPU_settag0(tag);

		return 0;
	} else if (tag == TW_Infinity) {
		
		arith_invalid(0);
		return 1;
	} else {
		single_arg_error(st0_ptr, tag);
		return 1;
	}
}

static int f_cos(FPU_REG *st0_ptr, u_char tag)
{
	u_char st0_sign;

	st0_sign = getsign(st0_ptr);

	if (tag == TAG_Valid) {
		int q;

		if (exponent(st0_ptr) > -40) {
			if ((exponent(st0_ptr) < 0)
			    || ((exponent(st0_ptr) == 0)
				&& (significand(st0_ptr) <=
				    0xc90fdaa22168c234LL))) {
				poly_cos(st0_ptr);

				
				set_precision_flag_down();

				return 0;
			} else if ((q = trig_arg(st0_ptr, FCOS)) != -1) {
				poly_sine(st0_ptr);

				if ((q + 1) & 2)
					changesign(st0_ptr);

				
				set_precision_flag_down();

				return 0;
			} else {
				
				return 1;
			}
		} else {
		      denormal_arg:

			setcc(0);
			FPU_copy_to_reg0(&CONST_1, TAG_Valid);
#ifdef PECULIAR_486
			set_precision_flag_down();	
#else
			set_precision_flag_up();	
#endif 
			return 0;
		}
	} else if (tag == TAG_Zero) {
		FPU_copy_to_reg0(&CONST_1, TAG_Valid);
		setcc(0);
		return 0;
	}

	if (tag == TAG_Special)
		tag = FPU_Special(st0_ptr);

	if (tag == TW_Denormal) {
		if (denormal_operand() < 0)
			return 1;

		goto denormal_arg;
	} else if (tag == TW_Infinity) {
		
		arith_invalid(0);
		return 1;
	} else {
		single_arg_error(st0_ptr, tag);	
		return 1;
	}
}

static void fcos(FPU_REG *st0_ptr, u_char st0_tag)
{
	f_cos(st0_ptr, st0_tag);
}

static void fsincos(FPU_REG *st0_ptr, u_char st0_tag)
{
	FPU_REG *st_new_ptr;
	FPU_REG arg;
	u_char tag;

	
	if (st0_tag == TAG_Empty) {
		FPU_stack_underflow();	
		if (control_word & CW_Invalid) {
			st_new_ptr = &st(-1);
			push();
			FPU_stack_underflow();	
		}
		return;
	}

	if (STACK_OVERFLOW) {
		FPU_stack_overflow();
		return;
	}

	if (st0_tag == TAG_Special)
		tag = FPU_Special(st0_ptr);
	else
		tag = st0_tag;

	if (tag == TW_NaN) {
		single_arg_2_error(st0_ptr, TW_NaN);
		return;
	} else if (tag == TW_Infinity) {
		
		if (arith_invalid(0) >= 0) {
			
			push();
			arith_invalid(0);
		}
		return;
	}

	reg_copy(st0_ptr, &arg);
	if (!fsin(st0_ptr, st0_tag)) {
		push();
		FPU_copy_to_reg0(&arg, st0_tag);
		f_cos(&st(0), st0_tag);
	} else {
		
		FPU_copy_to_reg0(&arg, st0_tag);
	}
}


static void rem_kernel(unsigned long long st0, unsigned long long *y,
		       unsigned long long st1, unsigned long long q, int n)
{
	int dummy;
	unsigned long long x;

	x = st0 << n;

	

	
	asm volatile ("mull %4; subl %%eax,%0; sbbl %%edx,%1":"=m"
		      (((unsigned *)&x)[0]), "=m"(((unsigned *)&x)[1]),
		      "=a"(dummy)
		      :"2"(((unsigned *)&st1)[0]), "m"(((unsigned *)&q)[0])
		      :"%dx");
	
	asm volatile ("mull %3; subl %%eax,%0":"=m" (((unsigned *)&x)[1]),
		      "=a"(dummy)
		      :"1"(((unsigned *)&st1)[1]), "m"(((unsigned *)&q)[0])
		      :"%dx");
	
	asm volatile ("mull %3; subl %%eax,%0":"=m" (((unsigned *)&x)[1]),
		      "=a"(dummy)
		      :"1"(((unsigned *)&st1)[0]), "m"(((unsigned *)&q)[1])
		      :"%dx");

	*y = x;
}

static void do_fprem(FPU_REG *st0_ptr, u_char st0_tag, int round)
{
	FPU_REG *st1_ptr = &st(1);
	u_char st1_tag = FPU_gettagi(1);

	if (!((st0_tag ^ TAG_Valid) | (st1_tag ^ TAG_Valid))) {
		FPU_REG tmp, st0, st1;
		u_char st0_sign, st1_sign;
		u_char tmptag;
		int tag;
		int old_cw;
		int expdif;
		long long q;
		unsigned short saved_status;
		int cc;

	      fprem_valid:
		
		st0_sign = FPU_to_exp16(st0_ptr, &st0);
		st1_sign = FPU_to_exp16(st1_ptr, &st1);
		expdif = exponent16(&st0) - exponent16(&st1);

		old_cw = control_word;
		cc = 0;

		saved_status = partial_status;
		control_word &= ~CW_RC;
		control_word |= RC_CHOP;

		if (expdif < 64) {
			

			if (expdif > -2) {
				u_char sign = st0_sign ^ st1_sign;
				tag = FPU_u_div(&st0, &st1, &tmp,
						PR_64_BITS | RC_CHOP | 0x3f,
						sign);
				setsign(&tmp, sign);

				if (exponent(&tmp) >= 0) {
					FPU_round_to_int(&tmp, tag);	
					q = significand(&tmp);

					rem_kernel(significand(&st0),
						   &significand(&tmp),
						   significand(&st1),
						   q, expdif);

					setexponent16(&tmp, exponent16(&st1));
				} else {
					reg_copy(&st0, &tmp);
					q = 0;
				}

				if ((round == RC_RND)
				    && (tmp.sigh & 0xc0000000)) {
					unsigned long long x;
					expdif =
					    exponent16(&st1) - exponent16(&tmp);
					if (expdif <= 1) {
						if (expdif == 0)
							x = significand(&st1) -
							    significand(&tmp);
						else	
							x = (significand(&st1)
							     << 1) -
							    significand(&tmp);
						if ((x < significand(&tmp)) ||
						    
						    ((x == significand(&tmp))
						     && (q & 1))) {
							st0_sign = !st0_sign;
							significand(&tmp) = x;
							q++;
						}
					}
				}

				if (q & 4)
					cc |= SW_C0;
				if (q & 2)
					cc |= SW_C3;
				if (q & 1)
					cc |= SW_C1;
			} else {
				control_word = old_cw;
				setcc(0);
				return;
			}
		} else {
			
			int exp_1, N;
			u_char sign;

			
			
			reg_copy(&st0, &tmp);
			tmptag = st0_tag;
			N = (expdif & 0x0000001f) + 32;	
			setexponent16(&tmp, N);
			exp_1 = exponent16(&st1);
			setexponent16(&st1, 0);
			expdif -= N;

			sign = getsign(&tmp) ^ st1_sign;
			tag =
			    FPU_u_div(&tmp, &st1, &tmp,
				      PR_64_BITS | RC_CHOP | 0x3f, sign);
			setsign(&tmp, sign);

			FPU_round_to_int(&tmp, tag);	

			rem_kernel(significand(&st0),
				   &significand(&tmp),
				   significand(&st1),
				   significand(&tmp), exponent(&tmp)
			    );
			setexponent16(&tmp, exp_1 + expdif);

			if (!(tmp.sigh | tmp.sigl)) {
				
				control_word = old_cw;
				partial_status = saved_status;
				FPU_copy_to_reg0(&CONST_Z, TAG_Zero);
				setsign(&st0, st0_sign);
#ifdef PECULIAR_486
				setcc(SW_C2);
#else
				setcc(0);
#endif 
				return;
			}
			cc = SW_C2;
		}

		control_word = old_cw;
		partial_status = saved_status;
		tag = FPU_normalize_nuo(&tmp);
		reg_copy(&tmp, st0_ptr);

		if ((exponent16(&tmp) <= EXP_UNDER) && (tag != TAG_Zero)
		    && !(control_word & CW_Underflow)) {
			setcc(cc);
			tag = arith_underflow(st0_ptr);
			setsign(st0_ptr, st0_sign);
			FPU_settag0(tag);
			return;
		} else if ((exponent16(&tmp) > EXP_UNDER) || (tag == TAG_Zero)) {
			stdexp(st0_ptr);
			setsign(st0_ptr, st0_sign);
		} else {
			tag =
			    FPU_round(st0_ptr, 0, 0, FULL_PRECISION, st0_sign);
		}
		FPU_settag0(tag);
		setcc(cc);

		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);
	if (st1_tag == TAG_Special)
		st1_tag = FPU_Special(st1_ptr);

	if (((st0_tag == TAG_Valid) && (st1_tag == TW_Denormal))
	    || ((st0_tag == TW_Denormal) && (st1_tag == TAG_Valid))
	    || ((st0_tag == TW_Denormal) && (st1_tag == TW_Denormal))) {
		if (denormal_operand() < 0)
			return;
		goto fprem_valid;
	} else if ((st0_tag == TAG_Empty) || (st1_tag == TAG_Empty)) {
		FPU_stack_underflow();
		return;
	} else if (st0_tag == TAG_Zero) {
		if (st1_tag == TAG_Valid) {
			setcc(0);
			return;
		} else if (st1_tag == TW_Denormal) {
			if (denormal_operand() < 0)
				return;
			setcc(0);
			return;
		} else if (st1_tag == TAG_Zero) {
			arith_invalid(0);
			return;
		} 
		else if (st1_tag == TW_Infinity) {
			setcc(0);
			return;
		}
	} else if ((st0_tag == TAG_Valid) || (st0_tag == TW_Denormal)) {
		if (st1_tag == TAG_Zero) {
			arith_invalid(0);	
			return;
		} else if (st1_tag != TW_NaN) {
			if (((st0_tag == TW_Denormal)
			     || (st1_tag == TW_Denormal))
			    && (denormal_operand() < 0))
				return;

			if (st1_tag == TW_Infinity) {
				
				setcc(0);
				return;
			}
		}
	} else if (st0_tag == TW_Infinity) {
		if (st1_tag != TW_NaN) {
			arith_invalid(0);	
			return;
		}
	}

	

#ifdef PARANOID
	if ((st0_tag != TW_NaN) && (st1_tag != TW_NaN))
		EXCEPTION(EX_INTERNAL | 0x118);
#endif 

	real_2op_NaN(st1_ptr, st1_tag, 0, st1_ptr);

}

static void fyl2x(FPU_REG *st0_ptr, u_char st0_tag)
{
	FPU_REG *st1_ptr = &st(1), exponent;
	u_char st1_tag = FPU_gettagi(1);
	u_char sign;
	int e, tag;

	clear_C1();

	if ((st0_tag == TAG_Valid) && (st1_tag == TAG_Valid)) {
	      both_valid:
		
		if (signpositive(st0_ptr)) {
			if (st0_tag == TW_Denormal)
				FPU_to_exp16(st0_ptr, st0_ptr);
			else
				
				setexponent16(st0_ptr, exponent(st0_ptr));

			if ((st0_ptr->sigh == 0x80000000)
			    && (st0_ptr->sigl == 0)) {
				
				u_char esign;
				e = exponent16(st0_ptr);
				if (e >= 0) {
					exponent.sigh = e;
					esign = SIGN_POS;
				} else {
					exponent.sigh = -e;
					esign = SIGN_NEG;
				}
				exponent.sigl = 0;
				setexponent16(&exponent, 31);
				tag = FPU_normalize_nuo(&exponent);
				stdexp(&exponent);
				setsign(&exponent, esign);
				tag =
				    FPU_mul(&exponent, tag, 1, FULL_PRECISION);
				if (tag >= 0)
					FPU_settagi(1, tag);
			} else {
				
				sign = getsign(st1_ptr);
				if (st1_tag == TW_Denormal)
					FPU_to_exp16(st1_ptr, st1_ptr);
				else
					
					setexponent16(st1_ptr,
						      exponent(st1_ptr));
				poly_l2(st0_ptr, st1_ptr, sign);
			}
		} else {
			
			if (arith_invalid(1) < 0)
				return;
		}

		FPU_pop();

		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);
	if (st1_tag == TAG_Special)
		st1_tag = FPU_Special(st1_ptr);

	if ((st0_tag == TAG_Empty) || (st1_tag == TAG_Empty)) {
		FPU_stack_underflow_pop(1);
		return;
	} else if ((st0_tag <= TW_Denormal) && (st1_tag <= TW_Denormal)) {
		if (st0_tag == TAG_Zero) {
			if (st1_tag == TAG_Zero) {
				
				if (arith_invalid(1) < 0)
					return;
			} else {
				u_char sign;
				sign = getsign(st1_ptr) ^ SIGN_NEG;
				if (FPU_divide_by_zero(1, sign) < 0)
					return;

				setsign(st1_ptr, sign);
			}
		} else if (st1_tag == TAG_Zero) {
			
			
			sign = getsign(st1_ptr);

			if (signnegative(st0_ptr)) {
				
				if (arith_invalid(1) < 0)
					return;
			} else if ((st0_tag == TW_Denormal)
				   && (denormal_operand() < 0))
				return;
			else {
				if (exponent(st0_ptr) < 0)
					sign ^= SIGN_NEG;

				FPU_copy_to_reg1(&CONST_Z, TAG_Zero);
				setsign(st1_ptr, sign);
			}
		} else {
			
			if (denormal_operand() < 0)
				return;
			goto both_valid;
		}
	} else if ((st0_tag == TW_NaN) || (st1_tag == TW_NaN)) {
		if (real_2op_NaN(st0_ptr, st0_tag, 1, st0_ptr) < 0)
			return;
	}
	
	else if (st0_tag == TW_Infinity) {
		if ((signnegative(st0_ptr)) || (st1_tag == TAG_Zero)) {
			
			if (arith_invalid(1) < 0)
				return;
		} else {
			u_char sign = getsign(st1_ptr);

			if ((st1_tag == TW_Denormal)
			    && (denormal_operand() < 0))
				return;

			FPU_copy_to_reg1(&CONST_INF, TAG_Special);
			setsign(st1_ptr, sign);
		}
	}
	
	else if (((st0_tag == TAG_Valid) || (st0_tag == TW_Denormal))
		 && (signpositive(st0_ptr))) {
		if (exponent(st0_ptr) >= 0) {
			if ((exponent(st0_ptr) == 0) &&
			    (st0_ptr->sigh == 0x80000000) &&
			    (st0_ptr->sigl == 0)) {
				
				
				if (arith_invalid(1) < 0)
					return;
			}
			
		} else {
			

			if ((st0_tag == TW_Denormal)
			    && (denormal_operand() < 0))
				return;

			changesign(st1_ptr);
		}
	} else {
		
		if (st0_tag == TAG_Zero) {
			

#ifndef PECULIAR_486
			sign = getsign(st1_ptr);
			if (FPU_divide_by_zero(1, sign) < 0)
				return;
#endif 

			changesign(st1_ptr);
		} else if (arith_invalid(1) < 0)	
			return;
	}

	FPU_pop();
}

static void fpatan(FPU_REG *st0_ptr, u_char st0_tag)
{
	FPU_REG *st1_ptr = &st(1);
	u_char st1_tag = FPU_gettagi(1);
	int tag;

	clear_C1();
	if (!((st0_tag ^ TAG_Valid) | (st1_tag ^ TAG_Valid))) {
	      valid_atan:

		poly_atan(st0_ptr, st0_tag, st1_ptr, st1_tag);

		FPU_pop();

		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);
	if (st1_tag == TAG_Special)
		st1_tag = FPU_Special(st1_ptr);

	if (((st0_tag == TAG_Valid) && (st1_tag == TW_Denormal))
	    || ((st0_tag == TW_Denormal) && (st1_tag == TAG_Valid))
	    || ((st0_tag == TW_Denormal) && (st1_tag == TW_Denormal))) {
		if (denormal_operand() < 0)
			return;

		goto valid_atan;
	} else if ((st0_tag == TAG_Empty) || (st1_tag == TAG_Empty)) {
		FPU_stack_underflow_pop(1);
		return;
	} else if ((st0_tag == TW_NaN) || (st1_tag == TW_NaN)) {
		if (real_2op_NaN(st0_ptr, st0_tag, 1, st0_ptr) >= 0)
			FPU_pop();
		return;
	} else if ((st0_tag == TW_Infinity) || (st1_tag == TW_Infinity)) {
		u_char sign = getsign(st1_ptr);
		if (st0_tag == TW_Infinity) {
			if (st1_tag == TW_Infinity) {
				if (signpositive(st0_ptr)) {
					FPU_copy_to_reg1(&CONST_PI4, TAG_Valid);
				} else {
					setpositive(st1_ptr);
					tag =
					    FPU_u_add(&CONST_PI4, &CONST_PI2,
						      st1_ptr, FULL_PRECISION,
						      SIGN_POS,
						      exponent(&CONST_PI4),
						      exponent(&CONST_PI2));
					if (tag >= 0)
						FPU_settagi(1, tag);
				}
			} else {
				if ((st1_tag == TW_Denormal)
				    && (denormal_operand() < 0))
					return;

				if (signpositive(st0_ptr)) {
					FPU_copy_to_reg1(&CONST_Z, TAG_Zero);
					setsign(st1_ptr, sign);	
					FPU_pop();
					return;
				} else {
					FPU_copy_to_reg1(&CONST_PI, TAG_Valid);
				}
			}
		} else {
			
			if ((st0_tag == TW_Denormal)
			    && (denormal_operand() < 0))
				return;

			FPU_copy_to_reg1(&CONST_PI2, TAG_Valid);
		}
		setsign(st1_ptr, sign);
	} else if (st1_tag == TAG_Zero) {
		
		u_char sign = getsign(st1_ptr);

		if ((st0_tag == TW_Denormal) && (denormal_operand() < 0))
			return;

		if (signpositive(st0_ptr)) {
			
			FPU_pop();
			return;
		}

		FPU_copy_to_reg1(&CONST_PI, TAG_Valid);
		setsign(st1_ptr, sign);
	} else if (st0_tag == TAG_Zero) {
		
		u_char sign = getsign(st1_ptr);

		if ((st1_tag == TW_Denormal) && (denormal_operand() < 0))
			return;

		FPU_copy_to_reg1(&CONST_PI2, TAG_Valid);
		setsign(st1_ptr, sign);
	}
#ifdef PARANOID
	else
		EXCEPTION(EX_INTERNAL | 0x125);
#endif 

	FPU_pop();
	set_precision_flag_up();	
}

static void fprem(FPU_REG *st0_ptr, u_char st0_tag)
{
	do_fprem(st0_ptr, st0_tag, RC_CHOP);
}

static void fprem1(FPU_REG *st0_ptr, u_char st0_tag)
{
	do_fprem(st0_ptr, st0_tag, RC_RND);
}

static void fyl2xp1(FPU_REG *st0_ptr, u_char st0_tag)
{
	u_char sign, sign1;
	FPU_REG *st1_ptr = &st(1), a, b;
	u_char st1_tag = FPU_gettagi(1);

	clear_C1();
	if (!((st0_tag ^ TAG_Valid) | (st1_tag ^ TAG_Valid))) {
	      valid_yl2xp1:

		sign = getsign(st0_ptr);
		sign1 = getsign(st1_ptr);

		FPU_to_exp16(st0_ptr, &a);
		FPU_to_exp16(st1_ptr, &b);

		if (poly_l2p1(sign, sign1, &a, &b, st1_ptr))
			return;

		FPU_pop();
		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);
	if (st1_tag == TAG_Special)
		st1_tag = FPU_Special(st1_ptr);

	if (((st0_tag == TAG_Valid) && (st1_tag == TW_Denormal))
	    || ((st0_tag == TW_Denormal) && (st1_tag == TAG_Valid))
	    || ((st0_tag == TW_Denormal) && (st1_tag == TW_Denormal))) {
		if (denormal_operand() < 0)
			return;

		goto valid_yl2xp1;
	} else if ((st0_tag == TAG_Empty) | (st1_tag == TAG_Empty)) {
		FPU_stack_underflow_pop(1);
		return;
	} else if (st0_tag == TAG_Zero) {
		switch (st1_tag) {
		case TW_Denormal:
			if (denormal_operand() < 0)
				return;

		case TAG_Zero:
		case TAG_Valid:
			setsign(st0_ptr, getsign(st0_ptr) ^ getsign(st1_ptr));
			FPU_copy_to_reg1(st0_ptr, st0_tag);
			break;

		case TW_Infinity:
			
			if (arith_invalid(1) < 0)
				return;
			break;

		case TW_NaN:
			if (real_2op_NaN(st0_ptr, st0_tag, 1, st0_ptr) < 0)
				return;
			break;

		default:
#ifdef PARANOID
			EXCEPTION(EX_INTERNAL | 0x116);
			return;
#endif 
			break;
		}
	} else if ((st0_tag == TAG_Valid) || (st0_tag == TW_Denormal)) {
		switch (st1_tag) {
		case TAG_Zero:
			if (signnegative(st0_ptr)) {
				if (exponent(st0_ptr) >= 0) {
					
#ifdef PECULIAR_486		
					changesign(st1_ptr);
#else
					if (arith_invalid(1) < 0)
						return;
#endif 
				} else if ((st0_tag == TW_Denormal)
					   && (denormal_operand() < 0))
					return;
				else
					changesign(st1_ptr);
			} else if ((st0_tag == TW_Denormal)
				   && (denormal_operand() < 0))
				return;
			break;

		case TW_Infinity:
			if (signnegative(st0_ptr)) {
				if ((exponent(st0_ptr) >= 0) &&
				    !((st0_ptr->sigh == 0x80000000) &&
				      (st0_ptr->sigl == 0))) {
					
#ifdef PECULIAR_486		
					changesign(st1_ptr);
#else
					if (arith_invalid(1) < 0)
						return;
#endif 
				} else if ((st0_tag == TW_Denormal)
					   && (denormal_operand() < 0))
					return;
				else
					changesign(st1_ptr);
			} else if ((st0_tag == TW_Denormal)
				   && (denormal_operand() < 0))
				return;
			break;

		case TW_NaN:
			if (real_2op_NaN(st0_ptr, st0_tag, 1, st0_ptr) < 0)
				return;
		}

	} else if (st0_tag == TW_NaN) {
		if (real_2op_NaN(st0_ptr, st0_tag, 1, st0_ptr) < 0)
			return;
	} else if (st0_tag == TW_Infinity) {
		if (st1_tag == TW_NaN) {
			if (real_2op_NaN(st0_ptr, st0_tag, 1, st0_ptr) < 0)
				return;
		} else if (signnegative(st0_ptr)) {
#ifndef PECULIAR_486
			
			if (arith_invalid(1) < 0)	
				return;
#endif 
			if ((st1_tag == TW_Denormal)
			    && (denormal_operand() < 0))
				return;
#ifdef PECULIAR_486
			
			if (arith_invalid(1) < 0)	
				return;
#endif 
		} else if (st1_tag == TAG_Zero) {
			
			if (arith_invalid(1) < 0)
				return;
		}

		

		else if ((st1_tag == TW_Denormal) && (denormal_operand() < 0))
			return;

		else {
			u_char sign = getsign(st1_ptr);
			FPU_copy_to_reg1(&CONST_INF, TAG_Special);
			setsign(st1_ptr, sign);
		}
	}
#ifdef PARANOID
	else {
		EXCEPTION(EX_INTERNAL | 0x117);
		return;
	}
#endif 

	FPU_pop();
	return;

}

static void fscale(FPU_REG *st0_ptr, u_char st0_tag)
{
	FPU_REG *st1_ptr = &st(1);
	u_char st1_tag = FPU_gettagi(1);
	int old_cw = control_word;
	u_char sign = getsign(st0_ptr);

	clear_C1();
	if (!((st0_tag ^ TAG_Valid) | (st1_tag ^ TAG_Valid))) {
		long scale;
		FPU_REG tmp;

		
		setexponent16(st0_ptr, exponent(st0_ptr));

	      valid_scale:

		if (exponent(st1_ptr) > 30) {
			

			if (signpositive(st1_ptr)) {
				EXCEPTION(EX_Overflow);
				FPU_copy_to_reg0(&CONST_INF, TAG_Special);
			} else {
				EXCEPTION(EX_Underflow);
				FPU_copy_to_reg0(&CONST_Z, TAG_Zero);
			}
			setsign(st0_ptr, sign);
			return;
		}

		control_word &= ~CW_RC;
		control_word |= RC_CHOP;
		reg_copy(st1_ptr, &tmp);
		FPU_round_to_int(&tmp, st1_tag);	
		control_word = old_cw;
		scale = signnegative(st1_ptr) ? -tmp.sigl : tmp.sigl;
		scale += exponent16(st0_ptr);

		setexponent16(st0_ptr, scale);

		
		FPU_round(st0_ptr, 0, 0, control_word, sign);

		return;
	}

	if (st0_tag == TAG_Special)
		st0_tag = FPU_Special(st0_ptr);
	if (st1_tag == TAG_Special)
		st1_tag = FPU_Special(st1_ptr);

	if ((st0_tag == TAG_Valid) || (st0_tag == TW_Denormal)) {
		switch (st1_tag) {
		case TAG_Valid:
			
			if ((st0_tag == TW_Denormal)
			    && (denormal_operand() < 0))
				return;

			FPU_to_exp16(st0_ptr, st0_ptr);	
			goto valid_scale;

		case TAG_Zero:
			if (st0_tag == TW_Denormal)
				denormal_operand();
			return;

		case TW_Denormal:
			denormal_operand();
			return;

		case TW_Infinity:
			if ((st0_tag == TW_Denormal)
			    && (denormal_operand() < 0))
				return;

			if (signpositive(st1_ptr))
				FPU_copy_to_reg0(&CONST_INF, TAG_Special);
			else
				FPU_copy_to_reg0(&CONST_Z, TAG_Zero);
			setsign(st0_ptr, sign);
			return;

		case TW_NaN:
			real_2op_NaN(st1_ptr, st1_tag, 0, st0_ptr);
			return;
		}
	} else if (st0_tag == TAG_Zero) {
		switch (st1_tag) {
		case TAG_Valid:
		case TAG_Zero:
			return;

		case TW_Denormal:
			denormal_operand();
			return;

		case TW_Infinity:
			if (signpositive(st1_ptr))
				arith_invalid(0);	
			return;

		case TW_NaN:
			real_2op_NaN(st1_ptr, st1_tag, 0, st0_ptr);
			return;
		}
	} else if (st0_tag == TW_Infinity) {
		switch (st1_tag) {
		case TAG_Valid:
		case TAG_Zero:
			return;

		case TW_Denormal:
			denormal_operand();
			return;

		case TW_Infinity:
			if (signnegative(st1_ptr))
				arith_invalid(0);	
			return;

		case TW_NaN:
			real_2op_NaN(st1_ptr, st1_tag, 0, st0_ptr);
			return;
		}
	} else if (st0_tag == TW_NaN) {
		if (st1_tag != TAG_Empty) {
			real_2op_NaN(st1_ptr, st1_tag, 0, st0_ptr);
			return;
		}
	}
#ifdef PARANOID
	if (!((st0_tag == TAG_Empty) || (st1_tag == TAG_Empty))) {
		EXCEPTION(EX_INTERNAL | 0x115);
		return;
	}
#endif

	
	FPU_stack_underflow();

}


static FUNC_ST0 const trig_table_a[] = {
	f2xm1, fyl2x, fptan, fpatan,
	fxtract, fprem1, (FUNC_ST0) fdecstp, (FUNC_ST0) fincstp
};

void FPU_triga(void)
{
	(trig_table_a[FPU_rm]) (&st(0), FPU_gettag0());
}

static FUNC_ST0 const trig_table_b[] = {
	fprem, fyl2xp1, fsqrt_, fsincos, frndint_, fscale, (FUNC_ST0) fsin, fcos
};

void FPU_trigb(void)
{
	(trig_table_b[FPU_rm]) (&st(0), FPU_gettag0());
}
