#ifndef __LIS3LV02D_H_
#define __LIS3LV02D_H_


struct lis3lv02d_platform_data {
#define LIS3_CLICK_SINGLE_X	(1 << 0)
#define LIS3_CLICK_DOUBLE_X	(1 << 1)
#define LIS3_CLICK_SINGLE_Y	(1 << 2)
#define LIS3_CLICK_DOUBLE_Y	(1 << 3)
#define LIS3_CLICK_SINGLE_Z	(1 << 4)
#define LIS3_CLICK_DOUBLE_Z	(1 << 5)
	unsigned char click_flags;
	unsigned char click_thresh_x;
	unsigned char click_thresh_y;
	unsigned char click_thresh_z;
	unsigned char click_time_limit;
	unsigned char click_latency;
	unsigned char click_window;

#define LIS3_IRQ1_DISABLE	(0 << 0)
#define LIS3_IRQ1_FF_WU_1	(1 << 0)
#define LIS3_IRQ1_FF_WU_2	(2 << 0)
#define LIS3_IRQ1_FF_WU_12	(3 << 0)
#define LIS3_IRQ1_DATA_READY	(4 << 0)
#define LIS3_IRQ1_CLICK		(7 << 0)
#define LIS3_IRQ1_MASK		(7 << 0)
#define LIS3_IRQ2_DISABLE	(0 << 3)
#define LIS3_IRQ2_FF_WU_1	(1 << 3)
#define LIS3_IRQ2_FF_WU_2	(2 << 3)
#define LIS3_IRQ2_FF_WU_12	(3 << 3)
#define LIS3_IRQ2_DATA_READY	(4 << 3)
#define LIS3_IRQ2_CLICK		(7 << 3)
#define LIS3_IRQ2_MASK		(7 << 3)
#define LIS3_IRQ_OPEN_DRAIN	(1 << 6)
#define LIS3_IRQ_ACTIVE_LOW	(1 << 7)
	unsigned char irq_cfg;
	unsigned char irq_flags1; 
	unsigned char irq_flags2; 
	unsigned char duration1;
	unsigned char duration2;
#define LIS3_WAKEUP_X_LO	(1 << 0)
#define LIS3_WAKEUP_X_HI	(1 << 1)
#define LIS3_WAKEUP_Y_LO	(1 << 2)
#define LIS3_WAKEUP_Y_HI	(1 << 3)
#define LIS3_WAKEUP_Z_LO	(1 << 4)
#define LIS3_WAKEUP_Z_HI	(1 << 5)
	unsigned char wakeup_flags;
	unsigned char wakeup_thresh;
	unsigned char wakeup_flags2;
	unsigned char wakeup_thresh2;
#define LIS3_HIPASS_CUTFF_8HZ   0
#define LIS3_HIPASS_CUTFF_4HZ   1
#define LIS3_HIPASS_CUTFF_2HZ   2
#define LIS3_HIPASS_CUTFF_1HZ   3
#define LIS3_HIPASS1_DISABLE    (1 << 2)
#define LIS3_HIPASS2_DISABLE    (1 << 3)
	unsigned char hipass_ctrl;
#define LIS3_NO_MAP		0
#define LIS3_DEV_X		1
#define LIS3_DEV_Y		2
#define LIS3_DEV_Z		3
#define LIS3_INV_DEV_X	       -1
#define LIS3_INV_DEV_Y	       -2
#define LIS3_INV_DEV_Z	       -3
	s8 axis_x;
	s8 axis_y;
	s8 axis_z;
#define LIS3_USE_BLOCK_READ	0x02
	u16 driver_features;
	int default_rate;
	int (*setup_resources)(void);
	int (*release_resources)(void);
	
	s16 st_min_limits[3]; 
	s16 st_max_limits[3]; 
	int irq2;
};

#endif 
