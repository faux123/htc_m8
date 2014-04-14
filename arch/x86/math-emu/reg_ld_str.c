/*---------------------------------------------------------------------------+
 |  reg_ld_str.c                                                             |
 |                                                                           |
 | All of the functions which transfer data between user memory and FPU_REGs.|
 |                                                                           |
 | Copyright (C) 1992,1993,1994,1996,1997                                    |
 |                  W. Metzenthen, 22 Parker St, Ormond, Vic 3163, Australia |
 |                  E-mail   billm@suburbia.net                              |
 |                                                                           |
 |                                                                           |
 +---------------------------------------------------------------------------*/


#include "fpu_emu.h"

#include <asm/uaccess.h>

#include "fpu_system.h"
#include "exception.h"
#include "reg_constant.h"
#include "control_w.h"
#include "status_w.h"

#define DOUBLE_Emax 1023	
#define DOUBLE_Ebias 1023
#define DOUBLE_Emin (-1022)	

#define SINGLE_Emax 127		
#define SINGLE_Ebias 127
#define SINGLE_Emin (-126)	

static u_char normalize_no_excep(FPU_REG *r, int exp, int sign)
{
	u_char tag;

	setexponent16(r, exp);

	tag = FPU_normalize_nuo(r);
	stdexp(r);
	if (sign)
		setnegative(r);

	return tag;
}

int FPU_tagof(FPU_REG *ptr)
{
	int exp;

	exp = exponent16(ptr) & 0x7fff;
	if (exp == 0) {
		if (!(ptr->sigh | ptr->sigl)) {
			return TAG_Zero;
		}
		
		return TAG_Special;
	}

	if (exp == 0x7fff) {
		
		return TAG_Special;
	}

	if (!(ptr->sigh & 0x80000000)) {
		
		
		
		return TAG_Special;
	}

	return TAG_Valid;
}

int FPU_load_extended(long double __user *s, int stnr)
{
	FPU_REG *sti_ptr = &st(stnr);

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, s, 10);
	__copy_from_user(sti_ptr, s, 10);
	RE_ENTRANT_CHECK_ON;

	return FPU_tagof(sti_ptr);
}

int FPU_load_double(double __user *dfloat, FPU_REG *loaded_data)
{
	int exp, tag, negative;
	unsigned m64, l64;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, dfloat, 8);
	FPU_get_user(m64, 1 + (unsigned long __user *)dfloat);
	FPU_get_user(l64, (unsigned long __user *)dfloat);
	RE_ENTRANT_CHECK_ON;

	negative = (m64 & 0x80000000) ? SIGN_Negative : SIGN_Positive;
	exp = ((m64 & 0x7ff00000) >> 20) - DOUBLE_Ebias + EXTENDED_Ebias;
	m64 &= 0xfffff;
	if (exp > DOUBLE_Emax + EXTENDED_Ebias) {
		
		if ((m64 == 0) && (l64 == 0)) {
			
			loaded_data->sigh = 0x80000000;
			loaded_data->sigl = 0x00000000;
			exp = EXP_Infinity + EXTENDED_Ebias;
			tag = TAG_Special;
		} else {
			
			exp = EXP_NaN + EXTENDED_Ebias;
			loaded_data->sigh = (m64 << 11) | 0x80000000;
			loaded_data->sigh |= l64 >> 21;
			loaded_data->sigl = l64 << 11;
			tag = TAG_Special;	
		}
	} else if (exp < DOUBLE_Emin + EXTENDED_Ebias) {
		
		if ((m64 == 0) && (l64 == 0)) {
			
			reg_copy(&CONST_Z, loaded_data);
			exp = 0;
			tag = TAG_Zero;
		} else {
			
			loaded_data->sigh = m64 << 11;
			loaded_data->sigh |= l64 >> 21;
			loaded_data->sigl = l64 << 11;

			return normalize_no_excep(loaded_data, DOUBLE_Emin,
						  negative)
			    | (denormal_operand() < 0 ? FPU_Exception : 0);
		}
	} else {
		loaded_data->sigh = (m64 << 11) | 0x80000000;
		loaded_data->sigh |= l64 >> 21;
		loaded_data->sigl = l64 << 11;

		tag = TAG_Valid;
	}

	setexponent16(loaded_data, exp | negative);

	return tag;
}

int FPU_load_single(float __user *single, FPU_REG *loaded_data)
{
	unsigned m32;
	int exp, tag, negative;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, single, 4);
	FPU_get_user(m32, (unsigned long __user *)single);
	RE_ENTRANT_CHECK_ON;

	negative = (m32 & 0x80000000) ? SIGN_Negative : SIGN_Positive;

	if (!(m32 & 0x7fffffff)) {
		
		reg_copy(&CONST_Z, loaded_data);
		addexponent(loaded_data, negative);
		return TAG_Zero;
	}
	exp = ((m32 & 0x7f800000) >> 23) - SINGLE_Ebias + EXTENDED_Ebias;
	m32 = (m32 & 0x7fffff) << 8;
	if (exp < SINGLE_Emin + EXTENDED_Ebias) {
		
		loaded_data->sigh = m32;
		loaded_data->sigl = 0;

		return normalize_no_excep(loaded_data, SINGLE_Emin, negative)
		    | (denormal_operand() < 0 ? FPU_Exception : 0);
	} else if (exp > SINGLE_Emax + EXTENDED_Ebias) {
		
		if (m32 == 0) {
			
			loaded_data->sigh = 0x80000000;
			loaded_data->sigl = 0x00000000;
			exp = EXP_Infinity + EXTENDED_Ebias;
			tag = TAG_Special;
		} else {
			
			exp = EXP_NaN + EXTENDED_Ebias;
			loaded_data->sigh = m32 | 0x80000000;
			loaded_data->sigl = 0;
			tag = TAG_Special;	
		}
	} else {
		loaded_data->sigh = m32 | 0x80000000;
		loaded_data->sigl = 0;
		tag = TAG_Valid;
	}

	setexponent16(loaded_data, exp | negative);	

	return tag;
}

int FPU_load_int64(long long __user *_s)
{
	long long s;
	int sign;
	FPU_REG *st0_ptr = &st(0);

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, _s, 8);
	if (copy_from_user(&s, _s, 8))
		FPU_abort;
	RE_ENTRANT_CHECK_ON;

	if (s == 0) {
		reg_copy(&CONST_Z, st0_ptr);
		return TAG_Zero;
	}

	if (s > 0)
		sign = SIGN_Positive;
	else {
		s = -s;
		sign = SIGN_Negative;
	}

	significand(st0_ptr) = s;

	return normalize_no_excep(st0_ptr, 63, sign);
}

int FPU_load_int32(long __user *_s, FPU_REG *loaded_data)
{
	long s;
	int negative;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, _s, 4);
	FPU_get_user(s, _s);
	RE_ENTRANT_CHECK_ON;

	if (s == 0) {
		reg_copy(&CONST_Z, loaded_data);
		return TAG_Zero;
	}

	if (s > 0)
		negative = SIGN_Positive;
	else {
		s = -s;
		negative = SIGN_Negative;
	}

	loaded_data->sigh = s;
	loaded_data->sigl = 0;

	return normalize_no_excep(loaded_data, 31, negative);
}

int FPU_load_int16(short __user *_s, FPU_REG *loaded_data)
{
	int s, negative;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, _s, 2);
	
	FPU_get_user(s, _s);
	RE_ENTRANT_CHECK_ON;

	if (s == 0) {
		reg_copy(&CONST_Z, loaded_data);
		return TAG_Zero;
	}

	if (s > 0)
		negative = SIGN_Positive;
	else {
		s = -s;
		negative = SIGN_Negative;
	}

	loaded_data->sigh = s << 16;
	loaded_data->sigl = 0;

	return normalize_no_excep(loaded_data, 15, negative);
}

int FPU_load_bcd(u_char __user *s)
{
	FPU_REG *st0_ptr = &st(0);
	int pos;
	u_char bcd;
	long long l = 0;
	int sign;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, s, 10);
	RE_ENTRANT_CHECK_ON;
	for (pos = 8; pos >= 0; pos--) {
		l *= 10;
		RE_ENTRANT_CHECK_OFF;
		FPU_get_user(bcd, s + pos);
		RE_ENTRANT_CHECK_ON;
		l += bcd >> 4;
		l *= 10;
		l += bcd & 0x0f;
	}

	RE_ENTRANT_CHECK_OFF;
	FPU_get_user(sign, s + 9);
	sign = sign & 0x80 ? SIGN_Negative : SIGN_Positive;
	RE_ENTRANT_CHECK_ON;

	if (l == 0) {
		reg_copy(&CONST_Z, st0_ptr);
		addexponent(st0_ptr, sign);	
		return TAG_Zero;
	} else {
		significand(st0_ptr) = l;
		return normalize_no_excep(st0_ptr, 63, sign);
	}
}


int FPU_store_extended(FPU_REG *st0_ptr, u_char st0_tag,
		       long double __user * d)
{

	if (st0_tag != TAG_Empty) {
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(VERIFY_WRITE, d, 10);

		FPU_put_user(st0_ptr->sigl, (unsigned long __user *)d);
		FPU_put_user(st0_ptr->sigh,
			     (unsigned long __user *)((u_char __user *) d + 4));
		FPU_put_user(exponent16(st0_ptr),
			     (unsigned short __user *)((u_char __user *) d +
						       8));
		RE_ENTRANT_CHECK_ON;

		return 1;
	}

	
	EXCEPTION(EX_StackUnder);
	if (control_word & CW_Invalid) {
		
		
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(VERIFY_WRITE, d, 10);
		FPU_put_user(0, (unsigned long __user *)d);
		FPU_put_user(0xc0000000, 1 + (unsigned long __user *)d);
		FPU_put_user(0xffff, 4 + (short __user *)d);
		RE_ENTRANT_CHECK_ON;
		return 1;
	} else
		return 0;

}

int FPU_store_double(FPU_REG *st0_ptr, u_char st0_tag, double __user *dfloat)
{
	unsigned long l[2];
	unsigned long increment = 0;	
	int precision_loss;
	int exp;
	FPU_REG tmp;

	l[0] = 0;
	l[1] = 0;
	if (st0_tag == TAG_Valid) {
		reg_copy(st0_ptr, &tmp);
		exp = exponent(&tmp);

		if (exp < DOUBLE_Emin) {	
			addexponent(&tmp, -DOUBLE_Emin + 52);	
denormal_arg:
			if ((precision_loss = FPU_round_to_int(&tmp, st0_tag))) {
#ifdef PECULIAR_486
				
				if (!
				    ((tmp.sigh == 0x00100000) && (tmp.sigl == 0)
				     && (st0_ptr->sigl & 0x000007ff)))
#endif 
				{
					EXCEPTION(EX_Underflow);
					if (!(control_word & CW_Underflow))
						return 0;
				}
				EXCEPTION(precision_loss);
				if (!(control_word & CW_Precision))
					return 0;
			}
			l[0] = tmp.sigl;
			l[1] = tmp.sigh;
		} else {
			if (tmp.sigl & 0x000007ff) {
				precision_loss = 1;
				switch (control_word & CW_RC) {
				case RC_RND:
					
					increment = ((tmp.sigl & 0x7ff) > 0x400) |	
					    ((tmp.sigl & 0xc00) == 0xc00);	
					break;
				case RC_DOWN:	
					increment =
					    signpositive(&tmp) ? 0 : tmp.
					    sigl & 0x7ff;
					break;
				case RC_UP:	
					increment =
					    signpositive(&tmp) ? tmp.
					    sigl & 0x7ff : 0;
					break;
				case RC_CHOP:
					increment = 0;
					break;
				}

				
				tmp.sigl &= 0xfffff800;

				if (increment) {
					if (tmp.sigl >= 0xfffff800) {
						
						if (tmp.sigh == 0xffffffff) {
							
							tmp.sigh = 0x80000000;
							exp++;
							if (exp >= EXP_OVER)
								goto overflow;
						} else {
							tmp.sigh++;
						}
						tmp.sigl = 0x00000000;
					} else {
						
						tmp.sigl += 0x00000800;
					}
				}
			} else
				precision_loss = 0;

			l[0] = (tmp.sigl >> 11) | (tmp.sigh << 21);
			l[1] = ((tmp.sigh >> 11) & 0xfffff);

			if (exp > DOUBLE_Emax) {
			      overflow:
				EXCEPTION(EX_Overflow);
				if (!(control_word & CW_Overflow))
					return 0;
				set_precision_flag_up();
				if (!(control_word & CW_Precision))
					return 0;

				
				
				l[1] = 0x7ff00000;	
			} else {
				if (precision_loss) {
					if (increment)
						set_precision_flag_up();
					else
						set_precision_flag_down();
				}
				
				l[1] |= (((exp + DOUBLE_Ebias) & 0x7ff) << 20);
			}
		}
	} else if (st0_tag == TAG_Zero) {
		
	} else if (st0_tag == TAG_Special) {
		st0_tag = FPU_Special(st0_ptr);
		if (st0_tag == TW_Denormal) {
			
#ifndef PECULIAR_486
			
			if (control_word & CW_Underflow)
				denormal_operand();
#endif 
			reg_copy(st0_ptr, &tmp);
			goto denormal_arg;
		} else if (st0_tag == TW_Infinity) {
			l[1] = 0x7ff00000;
		} else if (st0_tag == TW_NaN) {
			
			if ((exponent(st0_ptr) == EXP_OVER)
			    && (st0_ptr->sigh & 0x80000000)) {
				
				l[0] =
				    (st0_ptr->sigl >> 11) | (st0_ptr->
							     sigh << 21);
				l[1] = ((st0_ptr->sigh >> 11) & 0xfffff);
				if (!(st0_ptr->sigh & 0x40000000)) {
					
					EXCEPTION(EX_Invalid);
					if (!(control_word & CW_Invalid))
						return 0;
					l[1] |= (0x40000000 >> 11);
				}
				l[1] |= 0x7ff00000;
			} else {
				
				EXCEPTION(EX_Invalid);
				if (!(control_word & CW_Invalid))
					return 0;
				l[1] = 0xfff80000;
			}
		}
	} else if (st0_tag == TAG_Empty) {
		
		EXCEPTION(EX_StackUnder);
		if (control_word & CW_Invalid) {
			
			
			RE_ENTRANT_CHECK_OFF;
			FPU_access_ok(VERIFY_WRITE, dfloat, 8);
			FPU_put_user(0, (unsigned long __user *)dfloat);
			FPU_put_user(0xfff80000,
				     1 + (unsigned long __user *)dfloat);
			RE_ENTRANT_CHECK_ON;
			return 1;
		} else
			return 0;
	}
	if (getsign(st0_ptr))
		l[1] |= 0x80000000;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, dfloat, 8);
	FPU_put_user(l[0], (unsigned long __user *)dfloat);
	FPU_put_user(l[1], 1 + (unsigned long __user *)dfloat);
	RE_ENTRANT_CHECK_ON;

	return 1;
}

int FPU_store_single(FPU_REG *st0_ptr, u_char st0_tag, float __user *single)
{
	long templ = 0;
	unsigned long increment = 0;	
	int precision_loss;
	int exp;
	FPU_REG tmp;

	if (st0_tag == TAG_Valid) {

		reg_copy(st0_ptr, &tmp);
		exp = exponent(&tmp);

		if (exp < SINGLE_Emin) {
			addexponent(&tmp, -SINGLE_Emin + 23);	

		      denormal_arg:

			if ((precision_loss = FPU_round_to_int(&tmp, st0_tag))) {
#ifdef PECULIAR_486
				
				if (!((tmp.sigl == 0x00800000) &&
				      ((st0_ptr->sigh & 0x000000ff)
				       || st0_ptr->sigl)))
#endif 
				{
					EXCEPTION(EX_Underflow);
					if (!(control_word & CW_Underflow))
						return 0;
				}
				EXCEPTION(precision_loss);
				if (!(control_word & CW_Precision))
					return 0;
			}
			templ = tmp.sigl;
		} else {
			if (tmp.sigl | (tmp.sigh & 0x000000ff)) {
				unsigned long sigh = tmp.sigh;
				unsigned long sigl = tmp.sigl;

				precision_loss = 1;
				switch (control_word & CW_RC) {
				case RC_RND:
					increment = ((sigh & 0xff) > 0x80)	
					    ||(((sigh & 0xff) == 0x80) && sigl)	
					    ||((sigh & 0x180) == 0x180);	
					break;
				case RC_DOWN:	
					increment = signpositive(&tmp)
					    ? 0 : (sigl | (sigh & 0xff));
					break;
				case RC_UP:	
					increment = signpositive(&tmp)
					    ? (sigl | (sigh & 0xff)) : 0;
					break;
				case RC_CHOP:
					increment = 0;
					break;
				}

				
				tmp.sigl = 0;

				if (increment) {
					if (sigh >= 0xffffff00) {
						
						tmp.sigh = 0x80000000;
						exp++;
						if (exp >= EXP_OVER)
							goto overflow;
					} else {
						tmp.sigh &= 0xffffff00;
						tmp.sigh += 0x100;
					}
				} else {
					tmp.sigh &= 0xffffff00;	
				}
			} else
				precision_loss = 0;

			templ = (tmp.sigh >> 8) & 0x007fffff;

			if (exp > SINGLE_Emax) {
			      overflow:
				EXCEPTION(EX_Overflow);
				if (!(control_word & CW_Overflow))
					return 0;
				set_precision_flag_up();
				if (!(control_word & CW_Precision))
					return 0;

				
				
				templ = 0x7f800000;
			} else {
				if (precision_loss) {
					if (increment)
						set_precision_flag_up();
					else
						set_precision_flag_down();
				}
				
				templ |= ((exp + SINGLE_Ebias) & 0xff) << 23;
			}
		}
	} else if (st0_tag == TAG_Zero) {
		templ = 0;
	} else if (st0_tag == TAG_Special) {
		st0_tag = FPU_Special(st0_ptr);
		if (st0_tag == TW_Denormal) {
			reg_copy(st0_ptr, &tmp);

			
#ifndef PECULIAR_486
			
			if (control_word & CW_Underflow)
				denormal_operand();
#endif 
			goto denormal_arg;
		} else if (st0_tag == TW_Infinity) {
			templ = 0x7f800000;
		} else if (st0_tag == TW_NaN) {
			
			if ((exponent(st0_ptr) == EXP_OVER)
			    && (st0_ptr->sigh & 0x80000000)) {
				
				templ = st0_ptr->sigh >> 8;
				if (!(st0_ptr->sigh & 0x40000000)) {
					
					EXCEPTION(EX_Invalid);
					if (!(control_word & CW_Invalid))
						return 0;
					templ |= (0x40000000 >> 8);
				}
				templ |= 0x7f800000;
			} else {
				
				EXCEPTION(EX_Invalid);
				if (!(control_word & CW_Invalid))
					return 0;
				templ = 0xffc00000;
			}
		}
#ifdef PARANOID
		else {
			EXCEPTION(EX_INTERNAL | 0x164);
			return 0;
		}
#endif
	} else if (st0_tag == TAG_Empty) {
		
		EXCEPTION(EX_StackUnder);
		if (control_word & EX_Invalid) {
			
			
			RE_ENTRANT_CHECK_OFF;
			FPU_access_ok(VERIFY_WRITE, single, 4);
			FPU_put_user(0xffc00000,
				     (unsigned long __user *)single);
			RE_ENTRANT_CHECK_ON;
			return 1;
		} else
			return 0;
	}
#ifdef PARANOID
	else {
		EXCEPTION(EX_INTERNAL | 0x163);
		return 0;
	}
#endif
	if (getsign(st0_ptr))
		templ |= 0x80000000;

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, single, 4);
	FPU_put_user(templ, (unsigned long __user *)single);
	RE_ENTRANT_CHECK_ON;

	return 1;
}

int FPU_store_int64(FPU_REG *st0_ptr, u_char st0_tag, long long __user *d)
{
	FPU_REG t;
	long long tll;
	int precision_loss;

	if (st0_tag == TAG_Empty) {
		
		EXCEPTION(EX_StackUnder);
		goto invalid_operand;
	} else if (st0_tag == TAG_Special) {
		st0_tag = FPU_Special(st0_ptr);
		if ((st0_tag == TW_Infinity) || (st0_tag == TW_NaN)) {
			EXCEPTION(EX_Invalid);
			goto invalid_operand;
		}
	}

	reg_copy(st0_ptr, &t);
	precision_loss = FPU_round_to_int(&t, st0_tag);
	((long *)&tll)[0] = t.sigl;
	((long *)&tll)[1] = t.sigh;
	if ((precision_loss == 1) ||
	    ((t.sigh & 0x80000000) &&
	     !((t.sigh == 0x80000000) && (t.sigl == 0) && signnegative(&t)))) {
		EXCEPTION(EX_Invalid);
		
	      invalid_operand:
		if (control_word & EX_Invalid) {
			
			tll = 0x8000000000000000LL;
		} else
			return 0;
	} else {
		if (precision_loss)
			set_precision_flag(precision_loss);
		if (signnegative(&t))
			tll = -tll;
	}

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, d, 8);
	if (copy_to_user(d, &tll, 8))
		FPU_abort;
	RE_ENTRANT_CHECK_ON;

	return 1;
}

int FPU_store_int32(FPU_REG *st0_ptr, u_char st0_tag, long __user *d)
{
	FPU_REG t;
	int precision_loss;

	if (st0_tag == TAG_Empty) {
		
		EXCEPTION(EX_StackUnder);
		goto invalid_operand;
	} else if (st0_tag == TAG_Special) {
		st0_tag = FPU_Special(st0_ptr);
		if ((st0_tag == TW_Infinity) || (st0_tag == TW_NaN)) {
			EXCEPTION(EX_Invalid);
			goto invalid_operand;
		}
	}

	reg_copy(st0_ptr, &t);
	precision_loss = FPU_round_to_int(&t, st0_tag);
	if (t.sigh ||
	    ((t.sigl & 0x80000000) &&
	     !((t.sigl == 0x80000000) && signnegative(&t)))) {
		EXCEPTION(EX_Invalid);
		
	      invalid_operand:
		if (control_word & EX_Invalid) {
			
			t.sigl = 0x80000000;
		} else
			return 0;
	} else {
		if (precision_loss)
			set_precision_flag(precision_loss);
		if (signnegative(&t))
			t.sigl = -(long)t.sigl;
	}

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, d, 4);
	FPU_put_user(t.sigl, (unsigned long __user *)d);
	RE_ENTRANT_CHECK_ON;

	return 1;
}

int FPU_store_int16(FPU_REG *st0_ptr, u_char st0_tag, short __user *d)
{
	FPU_REG t;
	int precision_loss;

	if (st0_tag == TAG_Empty) {
		
		EXCEPTION(EX_StackUnder);
		goto invalid_operand;
	} else if (st0_tag == TAG_Special) {
		st0_tag = FPU_Special(st0_ptr);
		if ((st0_tag == TW_Infinity) || (st0_tag == TW_NaN)) {
			EXCEPTION(EX_Invalid);
			goto invalid_operand;
		}
	}

	reg_copy(st0_ptr, &t);
	precision_loss = FPU_round_to_int(&t, st0_tag);
	if (t.sigh ||
	    ((t.sigl & 0xffff8000) &&
	     !((t.sigl == 0x8000) && signnegative(&t)))) {
		EXCEPTION(EX_Invalid);
		
	      invalid_operand:
		if (control_word & EX_Invalid) {
			
			t.sigl = 0x8000;
		} else
			return 0;
	} else {
		if (precision_loss)
			set_precision_flag(precision_loss);
		if (signnegative(&t))
			t.sigl = -t.sigl;
	}

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, d, 2);
	FPU_put_user((short)t.sigl, d);
	RE_ENTRANT_CHECK_ON;

	return 1;
}

int FPU_store_bcd(FPU_REG *st0_ptr, u_char st0_tag, u_char __user *d)
{
	FPU_REG t;
	unsigned long long ll;
	u_char b;
	int i, precision_loss;
	u_char sign = (getsign(st0_ptr) == SIGN_NEG) ? 0x80 : 0;

	if (st0_tag == TAG_Empty) {
		
		EXCEPTION(EX_StackUnder);
		goto invalid_operand;
	} else if (st0_tag == TAG_Special) {
		st0_tag = FPU_Special(st0_ptr);
		if ((st0_tag == TW_Infinity) || (st0_tag == TW_NaN)) {
			EXCEPTION(EX_Invalid);
			goto invalid_operand;
		}
	}

	reg_copy(st0_ptr, &t);
	precision_loss = FPU_round_to_int(&t, st0_tag);
	ll = significand(&t);

	
	if ((t.sigh > 0x0de0b6b3) ||
	    ((t.sigh == 0x0de0b6b3) && (t.sigl > 0xa763ffff))) {
		EXCEPTION(EX_Invalid);
		
	      invalid_operand:
		if (control_word & CW_Invalid) {
			
			RE_ENTRANT_CHECK_OFF;
			FPU_access_ok(VERIFY_WRITE, d, 10);
			for (i = 0; i < 7; i++)
				FPU_put_user(0, d + i);	
			FPU_put_user(0xc0, d + 7);	
			FPU_put_user(0xff, d + 8);
			FPU_put_user(0xff, d + 9);
			RE_ENTRANT_CHECK_ON;
			return 1;
		} else
			return 0;
	} else if (precision_loss) {
		
		set_precision_flag(precision_loss);
	}

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, d, 10);
	RE_ENTRANT_CHECK_ON;
	for (i = 0; i < 9; i++) {
		b = FPU_div_small(&ll, 10);
		b |= (FPU_div_small(&ll, 10)) << 4;
		RE_ENTRANT_CHECK_OFF;
		FPU_put_user(b, d + i);
		RE_ENTRANT_CHECK_ON;
	}
	RE_ENTRANT_CHECK_OFF;
	FPU_put_user(sign, d + 9);
	RE_ENTRANT_CHECK_ON;

	return 1;
}


int FPU_round_to_int(FPU_REG *r, u_char tag)
{
	u_char very_big;
	unsigned eax;

	if (tag == TAG_Zero) {
		
		significand(r) = 0;
		return 0;	
	}

	if (exponent(r) > 63) {
		r->sigl = r->sigh = ~0;	
		return 1;	
	}

	eax = FPU_shrxs(&r->sigl, 63 - exponent(r));
	very_big = !(~(r->sigh) | ~(r->sigl));	
#define	half_or_more	(eax & 0x80000000)
#define	frac_part	(eax)
#define more_than_half  ((eax & 0x80000001) == 0x80000001)
	switch (control_word & CW_RC) {
	case RC_RND:
		if (more_than_half	
		    || (half_or_more && (r->sigl & 1))) {	
			if (very_big)
				return 1;	
			significand(r)++;
			return PRECISION_LOST_UP;
		}
		break;
	case RC_DOWN:
		if (frac_part && getsign(r)) {
			if (very_big)
				return 1;	
			significand(r)++;
			return PRECISION_LOST_UP;
		}
		break;
	case RC_UP:
		if (frac_part && !getsign(r)) {
			if (very_big)
				return 1;	
			significand(r)++;
			return PRECISION_LOST_UP;
		}
		break;
	case RC_CHOP:
		break;
	}

	return eax ? PRECISION_LOST_DOWN : 0;

}


u_char __user *fldenv(fpu_addr_modes addr_modes, u_char __user *s)
{
	unsigned short tag_word = 0;
	u_char tag;
	int i;

	if ((addr_modes.default_mode == VM86) ||
	    ((addr_modes.default_mode == PM16)
	     ^ (addr_modes.override.operand_size == OP_SIZE_PREFIX))) {
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(VERIFY_READ, s, 0x0e);
		FPU_get_user(control_word, (unsigned short __user *)s);
		FPU_get_user(partial_status, (unsigned short __user *)(s + 2));
		FPU_get_user(tag_word, (unsigned short __user *)(s + 4));
		FPU_get_user(instruction_address.offset,
			     (unsigned short __user *)(s + 6));
		FPU_get_user(instruction_address.selector,
			     (unsigned short __user *)(s + 8));
		FPU_get_user(operand_address.offset,
			     (unsigned short __user *)(s + 0x0a));
		FPU_get_user(operand_address.selector,
			     (unsigned short __user *)(s + 0x0c));
		RE_ENTRANT_CHECK_ON;
		s += 0x0e;
		if (addr_modes.default_mode == VM86) {
			instruction_address.offset
			    += (instruction_address.selector & 0xf000) << 4;
			operand_address.offset +=
			    (operand_address.selector & 0xf000) << 4;
		}
	} else {
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(VERIFY_READ, s, 0x1c);
		FPU_get_user(control_word, (unsigned short __user *)s);
		FPU_get_user(partial_status, (unsigned short __user *)(s + 4));
		FPU_get_user(tag_word, (unsigned short __user *)(s + 8));
		FPU_get_user(instruction_address.offset,
			     (unsigned long __user *)(s + 0x0c));
		FPU_get_user(instruction_address.selector,
			     (unsigned short __user *)(s + 0x10));
		FPU_get_user(instruction_address.opcode,
			     (unsigned short __user *)(s + 0x12));
		FPU_get_user(operand_address.offset,
			     (unsigned long __user *)(s + 0x14));
		FPU_get_user(operand_address.selector,
			     (unsigned long __user *)(s + 0x18));
		RE_ENTRANT_CHECK_ON;
		s += 0x1c;
	}

#ifdef PECULIAR_486
	control_word &= ~0xe080;
#endif 

	top = (partial_status >> SW_Top_Shift) & 7;

	if (partial_status & ~control_word & CW_Exceptions)
		partial_status |= (SW_Summary | SW_Backward);
	else
		partial_status &= ~(SW_Summary | SW_Backward);

	for (i = 0; i < 8; i++) {
		tag = tag_word & 3;
		tag_word >>= 2;

		if (tag == TAG_Empty)
			
			FPU_settag(i, TAG_Empty);
		else if (FPU_gettag(i) == TAG_Empty) {
			if (exponent(&fpu_register(i)) == -EXTENDED_Ebias) {
				if (!
				    (fpu_register(i).sigl | fpu_register(i).
				     sigh))
					FPU_settag(i, TAG_Zero);
				else
					FPU_settag(i, TAG_Special);
			} else if (exponent(&fpu_register(i)) ==
				   0x7fff - EXTENDED_Ebias) {
				FPU_settag(i, TAG_Special);
			} else if (fpu_register(i).sigh & 0x80000000)
				FPU_settag(i, TAG_Valid);
			else
				FPU_settag(i, TAG_Special);	
		}
	}

	return s;
}

void frstor(fpu_addr_modes addr_modes, u_char __user *data_address)
{
	int i, regnr;
	u_char __user *s = fldenv(addr_modes, data_address);
	int offset = (top & 7) * 10, other = 80 - offset;

	
	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_READ, s, 80);
	__copy_from_user(register_base + offset, s, other);
	if (offset)
		__copy_from_user(register_base, s + other, offset);
	RE_ENTRANT_CHECK_ON;

	for (i = 0; i < 8; i++) {
		regnr = (i + top) & 7;
		if (FPU_gettag(regnr) != TAG_Empty)
			
			FPU_settag(regnr, FPU_tagof(&st(i)));
	}

}

u_char __user *fstenv(fpu_addr_modes addr_modes, u_char __user *d)
{
	if ((addr_modes.default_mode == VM86) ||
	    ((addr_modes.default_mode == PM16)
	     ^ (addr_modes.override.operand_size == OP_SIZE_PREFIX))) {
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(VERIFY_WRITE, d, 14);
#ifdef PECULIAR_486
		FPU_put_user(control_word & ~0xe080, (unsigned long __user *)d);
#else
		FPU_put_user(control_word, (unsigned short __user *)d);
#endif 
		FPU_put_user(status_word(), (unsigned short __user *)(d + 2));
		FPU_put_user(fpu_tag_word, (unsigned short __user *)(d + 4));
		FPU_put_user(instruction_address.offset,
			     (unsigned short __user *)(d + 6));
		FPU_put_user(operand_address.offset,
			     (unsigned short __user *)(d + 0x0a));
		if (addr_modes.default_mode == VM86) {
			FPU_put_user((instruction_address.
				      offset & 0xf0000) >> 4,
				     (unsigned short __user *)(d + 8));
			FPU_put_user((operand_address.offset & 0xf0000) >> 4,
				     (unsigned short __user *)(d + 0x0c));
		} else {
			FPU_put_user(instruction_address.selector,
				     (unsigned short __user *)(d + 8));
			FPU_put_user(operand_address.selector,
				     (unsigned short __user *)(d + 0x0c));
		}
		RE_ENTRANT_CHECK_ON;
		d += 0x0e;
	} else {
		RE_ENTRANT_CHECK_OFF;
		FPU_access_ok(VERIFY_WRITE, d, 7 * 4);
#ifdef PECULIAR_486
		control_word &= ~0xe080;
		
		control_word |= 0xffff0040;
		partial_status = status_word() | 0xffff0000;
		fpu_tag_word |= 0xffff0000;
		I387->soft.fcs &= ~0xf8000000;
		I387->soft.fos |= 0xffff0000;
#endif 
		if (__copy_to_user(d, &control_word, 7 * 4))
			FPU_abort;
		RE_ENTRANT_CHECK_ON;
		d += 0x1c;
	}

	control_word |= CW_Exceptions;
	partial_status &= ~(SW_Summary | SW_Backward);

	return d;
}

void fsave(fpu_addr_modes addr_modes, u_char __user *data_address)
{
	u_char __user *d;
	int offset = (top & 7) * 10, other = 80 - offset;

	d = fstenv(addr_modes, data_address);

	RE_ENTRANT_CHECK_OFF;
	FPU_access_ok(VERIFY_WRITE, d, 80);

	
	if (__copy_to_user(d, register_base + offset, other))
		FPU_abort;
	if (offset)
		if (__copy_to_user(d + other, register_base, offset))
			FPU_abort;
	RE_ENTRANT_CHECK_ON;

	finit();
}

