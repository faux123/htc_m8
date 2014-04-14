/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: rf.c
 *
 * Purpose: rf function code
 *
 * Author: Jerry Chen
 *
 * Date: Feb. 19, 2004
 *
 * Functions:
 *      IFRFbWriteEmbeded      - Embeded write RF register via MAC
 *
 * Revision History:
 *
 */

#include "mac.h"
#include "srom.h"
#include "rf.h"
#include "baseband.h"



#define BY_RF2959_REG_LEN     23 
#define CB_RF2959_INIT_SEQ    15
#define SWITCH_CHANNEL_DELAY_RF2959 200 
#define RF2959_PWR_IDX_LEN    32

#define BY_MA2825_REG_LEN     23 
#define CB_MA2825_INIT_SEQ    13
#define SWITCH_CHANNEL_DELAY_MA2825 200 
#define MA2825_PWR_IDX_LEN    31

#define BY_AL2230_REG_LEN     23 
#define CB_AL2230_INIT_SEQ    15
#define SWITCH_CHANNEL_DELAY_AL2230 200 
#define AL2230_PWR_IDX_LEN    64


#define BY_UW2451_REG_LEN     23
#define CB_UW2451_INIT_SEQ    6
#define SWITCH_CHANNEL_DELAY_UW2451 200 
#define UW2451_PWR_IDX_LEN    25

#define BY_MA2829_REG_LEN     23 
#define CB_MA2829_INIT_SEQ    13
#define SWITCH_CHANNEL_DELAY_MA2829 200 
#define MA2829_PWR_IDX_LEN    64

#define BY_AL7230_REG_LEN     23 
#define CB_AL7230_INIT_SEQ    16
#define SWITCH_CHANNEL_DELAY_AL7230 200 
#define AL7230_PWR_IDX_LEN    64

#define BY_UW2452_REG_LEN     23
#define CB_UW2452_INIT_SEQ    5 
#define SWITCH_CHANNEL_DELAY_UW2452 100 
#define UW2452_PWR_IDX_LEN    64

#define BY_VT3226_REG_LEN     23
#define CB_VT3226_INIT_SEQ    12
#define SWITCH_CHANNEL_DELAY_VT3226 200 
#define VT3226_PWR_IDX_LEN    16





const unsigned long dwAL2230InitTable[CB_AL2230_INIT_SEQ] = {
    0x03F79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x01A00200+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00FFF300+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0F4DC500+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0805B600+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0146C700+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00068800+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0403B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00DBBA00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00099B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0BDFFC00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x00000D00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x00580F00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW
    };

const unsigned long dwAL2230ChannelTable0[CB_MAX_CHANNEL] = {
    0x03F79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03F79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E79000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03F7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03F7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E7A000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03F7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03F7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E7B000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03F7C000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03E7C000+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW  
    };

const unsigned long dwAL2230ChannelTable1[CB_MAX_CHANNEL] = {
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x06666100+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW  
    };

unsigned long dwAL2230PowerTable[AL2230_PWR_IDX_LEN] = {
    0x04040900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04041900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04042900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04043900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04044900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04045900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04046900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04047900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04048900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04049900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0404A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0404B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0404C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0404D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0404E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0404F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04050900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04051900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04052900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04053900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04054900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04055900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04056900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04057900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04058900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04059900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0405A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0405B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0405C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0405D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0405E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0405F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04060900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04061900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04062900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04063900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04064900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04065900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04066900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04067900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04068900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04069900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0406A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0406B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0406C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0406D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0406E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0406F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04070900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04071900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04072900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04073900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04074900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04075900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04076900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04077900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04078900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x04079900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0407A900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0407B900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0407C900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0407D900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0407E900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW,
    0x0407F900+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW
    };

const unsigned long dwAL7230InitTable[CB_AL7230_INIT_SEQ] = {
    0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x841FF200+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x3FDFA300+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    
    
    0x802B5500+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x56AF3600+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0xCE020700+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x6EBC0800+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0x221BB900+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0xE0000A00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x08031B00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    
    
    0x000A3C00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0xFFFFFD00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0x00000E00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0x1ABA8F00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  
    };

const unsigned long dwAL7230InitTableAMode[CB_AL7230_INIT_SEQ] = {
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x451FE200+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x5FDFA300+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67F78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x853F5500+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x56AF3600+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0xCE020700+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x6EBC0800+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0x221BB900+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0xE0600A00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x08031B00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00147C00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0xFFFFFD00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0x00000E00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW,
    0x12BACF00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  
    };


const unsigned long dwAL7230ChannelTable0[CB_MAX_CHANNEL] = {
    0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00379000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037A000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037B000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0037C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF52000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF53000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF53000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    
    

    0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF54000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF55000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF56000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF56000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF57000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF58000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF58000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF58000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF59000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    0x0FF5C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5C000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5D000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5D000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5D000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5E000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5E000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5E000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5F000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF5F000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF60000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF60000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF60000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF61000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0FF61000+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  
    };

const unsigned long dwAL7230ChannelTable1[CB_MAX_CHANNEL] = {
    0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x1B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x1B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x1B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x03333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0B333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x13333100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x06666100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    
    0x1D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x08000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    
    
    0x1D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x08000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x05555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x10000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x1AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x05555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0AAAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x15555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x00000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x18000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x0D555100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x18000100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x02AAA100+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  
    };

const unsigned long dwAL7230ChannelTable2[CB_MAX_CHANNEL] = {
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x7FD78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 

    
    
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x67D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW, 
    0x77D78400+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW  
    };








bool s_bAL7230Init (unsigned long dwIoBase)
{
    int     ii;
    bool bResult;

    bResult = true;

    
    VNSvOutPortB(dwIoBase + MAC_REG_SOFTPWRCTL, 0);

    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPECTI  |
                                                     SOFTPWRCTL_TXPEINV));
    BBvPowerSaveModeOFF(dwIoBase); 

    for (ii = 0; ii < CB_AL7230_INIT_SEQ; ii++)
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[ii]);

    
    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

    
    MACvTimer0MicroSDelay(dwIoBase, 150);
    bResult &= IFRFbWriteEmbeded(dwIoBase, (0x9ABA8F00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW)); 
    MACvTimer0MicroSDelay(dwIoBase, 30);
    bResult &= IFRFbWriteEmbeded(dwIoBase, (0x3ABA8F00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW)); 
    MACvTimer0MicroSDelay(dwIoBase, 30);
    bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[CB_AL7230_INIT_SEQ-1]); 

    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPE3    |
                                                     SOFTPWRCTL_SWPE2    |
                                                     SOFTPWRCTL_SWPECTI  |
                                                     SOFTPWRCTL_TXPEINV));

    BBvPowerSaveModeON(dwIoBase); 

    
    
    VNSvOutPortB(dwIoBase + MAC_REG_PSPWRSIG, (PSSIG_WPE3 | PSSIG_WPE2)); 

    return bResult;
}

bool s_bAL7230SelectChannel (unsigned long dwIoBase, unsigned char byChannel)
{
    bool bResult;

    bResult = true;

    
    MACvWordRegBitsOff(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

    bResult &= IFRFbWriteEmbeded (dwIoBase, dwAL7230ChannelTable0[byChannel-1]); 
    bResult &= IFRFbWriteEmbeded (dwIoBase, dwAL7230ChannelTable1[byChannel-1]); 
    bResult &= IFRFbWriteEmbeded (dwIoBase, dwAL7230ChannelTable2[byChannel-1]); 

    
    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

    
    VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel & 0x7F));
    MACvTimer0MicroSDelay(dwIoBase, SWITCH_CHANNEL_DELAY_AL7230);
    
    VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel | 0x80));

    return bResult;
}













bool IFRFbWriteEmbeded (unsigned long dwIoBase, unsigned long dwData)
{
    unsigned short ww;
    unsigned long dwValue;

    VNSvOutPortD(dwIoBase + MAC_REG_IFREGCTL, dwData);

    
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortD(dwIoBase + MAC_REG_IFREGCTL, &dwValue);
        if (dwValue & IFREGCTL_DONE)
            break;
    }

    if (ww == W_MAX_TIMEOUT) {
        return false;
    }
    return true;
}





bool RFbAL2230Init (unsigned long dwIoBase)
{
    int     ii;
    bool bResult;

    bResult = true;

    
    VNSvOutPortB(dwIoBase + MAC_REG_SOFTPWRCTL, 0);

    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPECTI  |
                                                     SOFTPWRCTL_TXPEINV));
    

    MACvWordRegBitsOff(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);



    
    IFRFbWriteEmbeded(dwIoBase, (0x07168700+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW));


    for (ii = 0; ii < CB_AL2230_INIT_SEQ; ii++)
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL2230InitTable[ii]);
MACvTimer0MicroSDelay(dwIoBase, 30); 

    
    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, SOFTPWRCTL_SWPE3);

    MACvTimer0MicroSDelay(dwIoBase, 150);
    bResult &= IFRFbWriteEmbeded(dwIoBase, (0x00d80f00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW));
    MACvTimer0MicroSDelay(dwIoBase, 30);
    bResult &= IFRFbWriteEmbeded(dwIoBase, (0x00780f00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW));
    MACvTimer0MicroSDelay(dwIoBase, 30);
    bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL2230InitTable[CB_AL2230_INIT_SEQ-1]);

    MACvWordRegBitsOn(dwIoBase, MAC_REG_SOFTPWRCTL, (SOFTPWRCTL_SWPE3    |
                                                     SOFTPWRCTL_SWPE2    |
                                                     SOFTPWRCTL_SWPECTI  |
                                                     SOFTPWRCTL_TXPEINV));

    
    VNSvOutPortB(dwIoBase + MAC_REG_PSPWRSIG, (PSSIG_WPE3 | PSSIG_WPE2)); 

    return bResult;
}

bool RFbAL2230SelectChannel (unsigned long dwIoBase, unsigned char byChannel)
{
    bool bResult;

    bResult = true;

    bResult &= IFRFbWriteEmbeded (dwIoBase, dwAL2230ChannelTable0[byChannel-1]);
    bResult &= IFRFbWriteEmbeded (dwIoBase, dwAL2230ChannelTable1[byChannel-1]);

    
    VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel & 0x7F));
    MACvTimer0MicroSDelay(dwIoBase, SWITCH_CHANNEL_DELAY_AL2230);
    
    VNSvOutPortB(dwIoBase + MAC_REG_CHANNEL, (byChannel | 0x80));

    return bResult;
}





bool RFbInit (
    PSDevice  pDevice
    )
{
bool bResult = true;
    switch (pDevice->byRFType) {
        case RF_AIROHA :
        case RF_AL2230S:
            pDevice->byMaxPwrLevel = AL2230_PWR_IDX_LEN;
            bResult = RFbAL2230Init(pDevice->PortOffset);
            break;
        case RF_AIROHA7230 :
            pDevice->byMaxPwrLevel = AL7230_PWR_IDX_LEN;
            bResult = s_bAL7230Init(pDevice->PortOffset);
            break;
        case RF_NOTHING :
            bResult = true;
            break;
        default :
            bResult = false;
            break;
    }
    return bResult;
}

bool RFbShutDown (
    PSDevice  pDevice
    )
{
bool bResult = true;

    switch (pDevice->byRFType) {
        case RF_AIROHA7230 :
            bResult = IFRFbWriteEmbeded (pDevice->PortOffset, 0x1ABAEF00+(BY_AL7230_REG_LEN<<3)+IFREGCTL_REGW);
            break;
        default :
            bResult = true;
            break;
    }
    return bResult;
}

bool RFbSelectChannel (unsigned long dwIoBase, unsigned char byRFType, unsigned char byChannel)
{
bool bResult = true;
    switch (byRFType) {

        case RF_AIROHA :
        case RF_AL2230S:
            bResult = RFbAL2230SelectChannel(dwIoBase, byChannel);
            break;
        
        case RF_AIROHA7230 :
            bResult = s_bAL7230SelectChannel(dwIoBase, byChannel);
            break;
        
        case RF_NOTHING :
            bResult = true;
            break;
        default:
            bResult = false;
            break;
    }
    return bResult;
}

bool RFvWriteWakeProgSyn (unsigned long dwIoBase, unsigned char byRFType, unsigned int uChannel)
{
    int   ii;
    unsigned char byInitCount = 0;
    unsigned char bySleepCount = 0;

    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, 0);
    switch (byRFType) {
        case RF_AIROHA:
        case RF_AL2230S:

            if (uChannel > CB_MAX_CHANNEL_24G)
                return false;

            byInitCount = CB_AL2230_INIT_SEQ + 2; 
            bySleepCount = 0;
            if (byInitCount > (MISCFIFO_SYNDATASIZE - bySleepCount)) {
                return false;
            }

            for (ii = 0; ii < CB_AL2230_INIT_SEQ; ii++ ) {
                MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL2230InitTable[ii]);
            }
            MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL2230ChannelTable0[uChannel-1]);
            ii ++;
            MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL2230ChannelTable1[uChannel-1]);
            break;

        
        
        case RF_AIROHA7230:
            byInitCount = CB_AL7230_INIT_SEQ + 3; 
            bySleepCount = 0;
            if (byInitCount > (MISCFIFO_SYNDATASIZE - bySleepCount)) {
                return false;
            }

            if (uChannel <= CB_MAX_CHANNEL_24G)
            {
                for (ii = 0; ii < CB_AL7230_INIT_SEQ; ii++ ) {
                    MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230InitTable[ii]);
                }
            }
            else
            {
                for (ii = 0; ii < CB_AL7230_INIT_SEQ; ii++ ) {
                    MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230InitTableAMode[ii]);
                }
            }

            MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230ChannelTable0[uChannel-1]);
            ii ++;
            MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230ChannelTable1[uChannel-1]);
            ii ++;
            MACvSetMISCFifo(dwIoBase, (unsigned short)(MISCFIFO_SYNDATA_IDX + ii), dwAL7230ChannelTable2[uChannel-1]);
            break;
        

        case RF_NOTHING :
            return true;
            break;

        default:
            return false;
            break;
    }

    MACvSetMISCFifo(dwIoBase, MISCFIFO_SYNINFO_IDX, (unsigned long )MAKEWORD(bySleepCount, byInitCount));

    return true;
}

bool RFbSetPower (
    PSDevice  pDevice,
    unsigned int uRATE,
    unsigned int uCH
    )
{
bool bResult = true;
unsigned char byPwr = 0;
unsigned char byDec = 0;
unsigned char byPwrdBm = 0;

    if (pDevice->dwDiagRefCount != 0) {
        return true;
    }
    if ((uCH < 1) || (uCH > CB_MAX_CHANNEL)) {
        return false;
    }

    switch (uRATE) {
    case RATE_1M:
    case RATE_2M:
    case RATE_5M:
    case RATE_11M:
        byPwr = pDevice->abyCCKPwrTbl[uCH];
        byPwrdBm = pDevice->abyCCKDefaultPwr[uCH];
	

		break;
    case RATE_6M:
    case RATE_9M:
    case RATE_18M:
        byPwr = pDevice->abyOFDMPwrTbl[uCH];
        if (pDevice->byRFType == RF_UW2452) {
            byDec = byPwr + 14;
        } else {
            byDec = byPwr + 10;
        }
        if (byDec >= pDevice->byMaxPwrLevel) {
            byDec = pDevice->byMaxPwrLevel-1;
        }
        if (pDevice->byRFType == RF_UW2452) {
            byPwrdBm = byDec - byPwr;
            byPwrdBm /= 3;
        } else {
            byPwrdBm = byDec - byPwr;
            byPwrdBm >>= 1;
        }
        byPwrdBm += pDevice->abyOFDMDefaultPwr[uCH];
        byPwr = byDec;
	

		break;
    case RATE_24M:
    case RATE_36M:
    case RATE_48M:
    case RATE_54M:
        byPwr = pDevice->abyOFDMPwrTbl[uCH];
        byPwrdBm = pDevice->abyOFDMDefaultPwr[uCH];
	
		break;
    }

#if 0

    
    if (pDevice->bLinkPass == true) {
        
        if (byPwrdBm > pDevice->abyLocalPwr[uCH]) {
            pDevice->byCurPwrdBm = pDevice->abyLocalPwr[uCH];
            byDec = byPwrdBm - pDevice->abyLocalPwr[uCH];
            if (pDevice->byRFType == RF_UW2452) {
                byDec *= 3;
            } else {
                byDec <<= 1;
            }
            if (byPwr > byDec) {
                byPwr -= byDec;
            } else {
                byPwr = 0;
            }
        } else {
            pDevice->byCurPwrdBm = byPwrdBm;
        }
    } else {
        
        if (byPwrdBm > pDevice->abyRegPwr[uCH]) {
            pDevice->byCurPwrdBm = pDevice->abyRegPwr[uCH];
            byDec = byPwrdBm - pDevice->abyRegPwr[uCH];
            if (pDevice->byRFType == RF_UW2452) {
                byDec *= 3;
            } else {
                byDec <<= 1;
            }
            if (byPwr > byDec) {
                byPwr -= byDec;
            } else {
                byPwr = 0;
            }
        } else {
            pDevice->byCurPwrdBm = byPwrdBm;
        }
    }
#endif

    if (pDevice->byCurPwr == byPwr) {
        return true;
    }
    bResult = RFbRawSetPower(pDevice, byPwr, uRATE);
    if (bResult == true) {
       pDevice->byCurPwr = byPwr;
    }
    return bResult;
}


bool RFbRawSetPower (
    PSDevice  pDevice,
    unsigned char byPwr,
    unsigned int uRATE
    )
{
bool bResult = true;
unsigned long dwMax7230Pwr = 0;

    if (byPwr >=  pDevice->byMaxPwrLevel) {
        return (false);
    }
    switch (pDevice->byRFType) {

        case RF_AIROHA :
            bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, dwAL2230PowerTable[byPwr]);
            if (uRATE <= RATE_11M) {
                bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, 0x0001B400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            } else {
                bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, 0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            }
            break;


        case RF_AL2230S :
            bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, dwAL2230PowerTable[byPwr]);
            if (uRATE <= RATE_11M) {
                bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, 0x040C1400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
                bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, 0x00299B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            }else {
                bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, 0x0005A400+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
                bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, 0x00099B00+(BY_AL2230_REG_LEN<<3)+IFREGCTL_REGW);
            }

            break;

        case RF_AIROHA7230:
            
            dwMax7230Pwr = 0x080C0B00 | ( (byPwr) << 12 ) |
                           (BY_AL7230_REG_LEN << 3 )  | IFREGCTL_REGW;

            bResult &= IFRFbWriteEmbeded(pDevice->PortOffset, dwMax7230Pwr);
            break;


        default :
            break;
    }
    return bResult;
}

void
RFvRSSITodBm (
    PSDevice pDevice,
    unsigned char byCurrRSSI,
    long *    pldBm
    )
{
    unsigned char byIdx = (((byCurrRSSI & 0xC0) >> 6) & 0x03);
    long b = (byCurrRSSI & 0x3F);
    long a = 0;
    unsigned char abyAIROHARF[4] = {0, 18, 0, 40};

    switch (pDevice->byRFType) {
        case RF_AIROHA:
        case RF_AL2230S:
        case RF_AIROHA7230: 
            a = abyAIROHARF[byIdx];
            break;
        default:
            break;
    }

    *pldBm = -1 * (a + b * 2);
}



bool RFbAL7230SelectChannelPostProcess (unsigned long dwIoBase, unsigned char byOldChannel, unsigned char byNewChannel)
{
    bool bResult;

    bResult = true;

    
    

    if( (byOldChannel <= CB_MAX_CHANNEL_24G) && (byNewChannel > CB_MAX_CHANNEL_24G) )
    {
        
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[2]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[3]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[5]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[7]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[10]);
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[12]);
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTableAMode[15]);
    }
    else if( (byOldChannel > CB_MAX_CHANNEL_24G) && (byNewChannel <= CB_MAX_CHANNEL_24G) )
    {
        
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[2]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[3]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[5]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[7]); 
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[10]);
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[12]);
        bResult &= IFRFbWriteEmbeded(dwIoBase, dwAL7230InitTable[15]);
    }

    return bResult;
}



