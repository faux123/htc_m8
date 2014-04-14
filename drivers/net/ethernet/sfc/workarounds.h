/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_WORKAROUNDS_H
#define EFX_WORKAROUNDS_H


#define EFX_WORKAROUND_ALWAYS(efx) 1
#define EFX_WORKAROUND_FALCON_A(efx) (efx_nic_rev(efx) <= EFX_REV_FALCON_A1)
#define EFX_WORKAROUND_FALCON_AB(efx) (efx_nic_rev(efx) <= EFX_REV_FALCON_B0)
#define EFX_WORKAROUND_SIENA(efx) (efx_nic_rev(efx) == EFX_REV_SIENA_A0)
#define EFX_WORKAROUND_10G(efx) 1

#define EFX_WORKAROUND_5147 EFX_WORKAROUND_ALWAYS
#define EFX_WORKAROUND_7575 EFX_WORKAROUND_ALWAYS
#define EFX_WORKAROUND_7884 EFX_WORKAROUND_10G
#define EFX_WORKAROUND_10727 EFX_WORKAROUND_ALWAYS
#define EFX_WORKAROUND_11482 EFX_WORKAROUND_FALCON_AB
#define EFX_WORKAROUND_15592 EFX_WORKAROUND_FALCON_AB
#define EFX_WORKAROUND_15783 EFX_WORKAROUND_ALWAYS
#define EFX_WORKAROUND_17213 EFX_WORKAROUND_SIENA

#define EFX_WORKAROUND_5129 EFX_WORKAROUND_FALCON_A
#define EFX_WORKAROUND_5391 EFX_WORKAROUND_FALCON_A
#define EFX_WORKAROUND_5583 EFX_WORKAROUND_FALCON_A
#define EFX_WORKAROUND_5676 EFX_WORKAROUND_FALCON_A
#define EFX_WORKAROUND_6555 EFX_WORKAROUND_FALCON_A
#define EFX_WORKAROUND_7244 EFX_WORKAROUND_FALCON_A
#define EFX_WORKAROUND_7803 EFX_WORKAROUND_FALCON_AB
#define EFX_WORKAROUND_8071 EFX_WORKAROUND_FALCON_A

#endif 
