/*
 * f75375s.h - platform data structure for f75375s sensor
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007, Riku Voipio <riku.voipio@iki.fi>
 */

#ifndef __LINUX_F75375S_H
#define __LINUX_F75375S_H

struct f75375s_platform_data {
	u8 pwm[2];
	u8 pwm_enable[2];
};

#endif 
