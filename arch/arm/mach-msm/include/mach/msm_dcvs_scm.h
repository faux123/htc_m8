/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef _ARCH_ARM_MACH_MSM_MSM_DCVS_SCM_H
#define _ARCH_ARM_MACH_MSM_MSM_DCVS_SCM_H

enum msm_dcvs_core_type {
	MSM_DCVS_CORE_TYPE_CPU = 0,
	MSM_DCVS_CORE_TYPE_GPU = 1,
};

enum msm_dcvs_algo_param_type {
	MSM_DCVS_ALGO_DCVS_PARAM = 0,
	MSM_DCVS_ALGO_MPD_PARAM  = 1,
};

enum msm_dcvs_scm_event {
	MSM_DCVS_SCM_IDLE_ENTER = 0, 
	MSM_DCVS_SCM_IDLE_EXIT = 1, 
	MSM_DCVS_SCM_QOS_TIMER_EXPIRED = 2, 
	MSM_DCVS_SCM_CLOCK_FREQ_UPDATE = 3, 
	MSM_DCVS_SCM_CORE_ONLINE = 4, 
	MSM_DCVS_SCM_CORE_OFFLINE = 5, 
	MSM_DCVS_SCM_CORE_UNAVAILABLE = 6, 
	MSM_DCVS_SCM_DCVS_ENABLE = 7, 
	MSM_DCVS_SCM_MPD_ENABLE = 8, 
	MSM_DCVS_SCM_RUNQ_UPDATE = 9, 
	MSM_DCVS_SCM_MPD_QOS_TIMER_EXPIRED = 10, 
};

struct msm_dcvs_algo_param {
	uint32_t disable_pc_threshold;
	uint32_t em_win_size_min_us;
	uint32_t em_win_size_max_us;
	uint32_t em_max_util_pct;
	uint32_t group_id;
	uint32_t max_freq_chg_time_us;
	uint32_t slack_mode_dynamic;
	uint32_t slack_time_min_us;
	uint32_t slack_time_max_us;
	uint32_t slack_weight_thresh_pct;
	uint32_t ss_no_corr_below_freq;
	uint32_t ss_win_size_min_us;
	uint32_t ss_win_size_max_us;
	uint32_t ss_util_pct;
};

struct msm_dcvs_freq_entry {
	uint32_t freq;
	uint32_t voltage;
	uint32_t is_trans_level;
	uint32_t active_energy_offset;
	uint32_t leakage_energy_offset;
};

struct msm_dcvs_energy_curve_coeffs {
	int32_t active_coeff_a;
	int32_t active_coeff_b;
	int32_t active_coeff_c;

	int32_t leakage_coeff_a;
	int32_t leakage_coeff_b;
	int32_t leakage_coeff_c;
	int32_t leakage_coeff_d;
};

struct msm_dcvs_power_params {
	uint32_t current_temp;
	uint32_t num_freq; 
};

struct msm_dcvs_core_param {
	uint32_t core_type;
	uint32_t core_bitmask_id;
};

struct msm_mpd_algo_param {
	uint32_t em_win_size_min_us;
	uint32_t em_win_size_max_us;
	uint32_t em_max_util_pct;
	uint32_t mp_em_rounding_point_min;
	uint32_t mp_em_rounding_point_max;
	uint32_t online_util_pct_min;
	uint32_t online_util_pct_max;
	uint32_t slack_time_min_us;
	uint32_t slack_time_max_us;
};

#ifdef CONFIG_MSM_DCVS
extern int msm_dcvs_scm_init(size_t size);

extern int msm_dcvs_scm_register_core(uint32_t core_id,
		struct msm_dcvs_core_param *param);

extern int msm_dcvs_scm_set_algo_params(uint32_t core_id,
		struct msm_dcvs_algo_param *param);

extern int msm_mpd_scm_set_algo_params(struct msm_mpd_algo_param *param);

extern int msm_dcvs_scm_set_power_params(uint32_t core_id,
				struct msm_dcvs_power_params *pwr_param,
				struct msm_dcvs_freq_entry *freq_entry,
				struct msm_dcvs_energy_curve_coeffs *coeffs);

extern int msm_dcvs_scm_event(uint32_t core_id,
		enum msm_dcvs_scm_event event_id,
		uint32_t param0, uint32_t param1,
		uint32_t *ret0, uint32_t *ret1);

#else
static inline int msm_dcvs_scm_init(uint32_t phy, size_t bytes)
{ return -ENOSYS; }
static inline int msm_dcvs_scm_register_core(uint32_t core_id,
		struct msm_dcvs_core_param *param,
		struct msm_dcvs_freq_entry *freq)
{ return -ENOSYS; }
static inline int msm_dcvs_scm_set_algo_params(uint32_t core_id,
		struct msm_dcvs_algo_param *param)
{ return -ENOSYS; }
static inline int msm_mpd_scm_set_algo_params(
		struct msm_mpd_algo_param *param)
{ return -ENOSYS; }
static inline int msm_dcvs_set_power_params(uint32_t core_id,
		struct msm_dcvs_power_params *pwr_param,
		struct msm_dcvs_freq_entry *freq_entry,
		struct msm_dcvs_energy_curve_coeffs *coeffs)
{ return -ENOSYS; }
static inline int msm_dcvs_scm_event(uint32_t core_id,
		enum msm_dcvs_scm_event event_id,
		uint32_t param0, uint32_t param1,
		uint32_t *ret0, uint32_t *ret1)
{ return -ENOSYS; }
#endif

#endif
