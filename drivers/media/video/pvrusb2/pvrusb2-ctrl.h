/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef __PVRUSB2_CTRL_H
#define __PVRUSB2_CTRL_H

struct pvr2_ctrl;

enum pvr2_ctl_type {
	pvr2_ctl_int = 0,
	pvr2_ctl_enum = 1,
	pvr2_ctl_bitmask = 2,
	pvr2_ctl_bool = 3,
};


int pvr2_ctrl_set_value(struct pvr2_ctrl *,int val);

int pvr2_ctrl_set_mask_value(struct pvr2_ctrl *,int mask,int val);

int pvr2_ctrl_get_value(struct pvr2_ctrl *,int *valptr);

enum pvr2_ctl_type pvr2_ctrl_get_type(struct pvr2_ctrl *);

int pvr2_ctrl_get_max(struct pvr2_ctrl *);

int pvr2_ctrl_get_min(struct pvr2_ctrl *);

int pvr2_ctrl_get_def(struct pvr2_ctrl *, int *valptr);

int pvr2_ctrl_get_cnt(struct pvr2_ctrl *);

int pvr2_ctrl_get_mask(struct pvr2_ctrl *);

const char *pvr2_ctrl_get_name(struct pvr2_ctrl *);

const char *pvr2_ctrl_get_desc(struct pvr2_ctrl *);

int pvr2_ctrl_get_valname(struct pvr2_ctrl *,int,char *,unsigned int,
			  unsigned int *);

int pvr2_ctrl_is_writable(struct pvr2_ctrl *);

unsigned int pvr2_ctrl_get_v4lflags(struct pvr2_ctrl *);

int pvr2_ctrl_get_v4lid(struct pvr2_ctrl *);

int pvr2_ctrl_has_custom_symbols(struct pvr2_ctrl *);

int pvr2_ctrl_custom_value_to_sym(struct pvr2_ctrl *,
				  int mask,int val,
				  char *buf,unsigned int maxlen,
				  unsigned int *len);

int pvr2_ctrl_custom_sym_to_value(struct pvr2_ctrl *,
				  const char *buf,unsigned int len,
				  int *maskptr,int *valptr);

int pvr2_ctrl_value_to_sym(struct pvr2_ctrl *,
			   int mask,int val,
			   char *buf,unsigned int maxlen,
			   unsigned int *len);

int pvr2_ctrl_sym_to_value(struct pvr2_ctrl *,
			   const char *buf,unsigned int len,
			   int *maskptr,int *valptr);

int pvr2_ctrl_value_to_sym_internal(struct pvr2_ctrl *,
			   int mask,int val,
			   char *buf,unsigned int maxlen,
			   unsigned int *len);

#endif 

