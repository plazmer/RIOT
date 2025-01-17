/*
 * Copyright (C) 2016-2018 Unwired Devices LLC <info@unwds.com>

 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @defgroup    
 * @ingroup     
 * @brief       
 * @{
 * @file		umdk-inclinometer.c
 * @brief       umdk-inclinometer module implementation
 * @author      Oleg Artamonov <info@unwds.com>
 */

#ifdef __cplusplus
extern "C" {
#endif

/* define is autogenerated, do not change */
#undef _UMDK_MID_
#define _UMDK_MID_ UNWDS_INCLINOMETER_MODULE_ID

/* define is autogenerated, do not change */
#undef _UMDK_NAME_
#define _UMDK_NAME_ "inclinometer"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "byteorder.h"

#include "periph/gpio.h"
#include "periph/i2c.h"

#include "board.h"

#include "lis2hh12.h"
#include "lis2hh12_params.h"

#include "lis2dh12.h"
#include "lis2dh12_params.h"

#include "lis3dh.h"
#include "lis3dh_params.h"

#include "adxl345.h"
#include "adxl345_params.h"

#include "lsm6ds3.h"
#include "lsm6ds3_regs.h"

#include "umdk-ids.h"
#include "unwds-common.h"
#include "umdk-inclinometer.h"

#include "thread.h"
#include "lptimer.h"

#define RADIAN_TO_DEGREE_MILLIS 57296

#define ENABLE_DEBUG (0)
#include "debug.h"

static lis2hh12_t   dev_lis2hh12;
static adxl345_t    dev_adxl345;
static lsm6ds3_t    dev_lsm6ds3;
static lis2dh12_t   dev_lis2dh12;
static lis3dh_t     dev_lis3dh;

static uwnds_cb_t *callback;

static kernel_pid_t measure_pid;
static kernel_pid_t timer_pid;

typedef enum {
    INCLINOMETER_NORMAL_MESSAGE,
    INCLINOMETER_ALARM_MESSAGE
} inclinometer_msg_t;

static msg_t timer_msg = { .type = INCLINOMETER_NORMAL_MESSAGE };
static msg_t alarm_msg = { .type = INCLINOMETER_ALARM_MESSAGE };
static msg_t measure_msg = {};
static lptimer_t timer;
static lptimer_t measure_timer;

static bool is_polled = false;

static struct {
	uint16_t publish_period_sec;
    uint16_t rate;
	uint8_t i2c_dev;
    uint16_t threshold_xz;
    uint16_t threshold_yz;
} inclinometer_config;

typedef struct angles {
    int32_t previous;
    int32_t current;
    int32_t max;
    int32_t min;
} inclinometer_angle_t;

static inclinometer_angle_t phi;
static inclinometer_angle_t theta;

typedef enum {
    UMDK_INCLINOMETER_LSM6DS3  = 1,
    UMDK_INCLINOMETER_LIS2HH12 = 1 << 1,
    UMDK_INCLINOMETER_ADXL345  = 1 << 2,
    UMDK_INCLINOMETER_LIS2DH12 = 1 << 3,
    UMDK_INCLINOMETER_LIS3DH   = 1 << 4,
} umdk_inclinometer_active_sensors_t;

static uint8_t active_sensors = 0;

static bool init_sensor(void) {
    printf("[umdk-" _UMDK_NAME_ "] Initializing INCLINOMETER on I2C #%d\n", I2C_DEV(UMDK_INCLINOMETER_I2C));
    
	lis2hh12_params_t lis2hh12_params;
    
    lis2hh12_params.i2c = I2C_DEV(UMDK_INCLINOMETER_I2C);        /**< I2C device */
    lis2hh12_params.i2c_addr = 0x1E;                    /**< Accelerometer I2C address */
    lis2hh12_params.odr = LIS2HH12_ODR_50HZ;            /**< Output data rate */
    lis2hh12_params.scale = LIS2HH12_SCALE_2G;          /**< Scale factor */
    lis2hh12_params.resolution = LIS2HH12_RES_HR;       /**< Resolution */

    if (lis2hh12_init(&dev_lis2hh12, &lis2hh12_params) == 0) {
        puts("[umdk-" _UMDK_NAME_ "] STMicro LIS2HH12 sensor found");
        active_sensors |= UMDK_INCLINOMETER_LIS2HH12;
        lis2hh12_poweroff(&dev_lis2hh12);
        return true;
    }
    
    lis2dh12_params_t lis2dh12_params;
    
    lis2dh12_params.i2c_dev = I2C_DEV(UMDK_INCLINOMETER_I2C);        /**< I2C device */
    lis2dh12_params.i2c_addr = LIS2DH12_I2C_SAD_L;      /**< Accelerometer I2C address */
    lis2dh12_params.rate = LIS2DH12_RATE_50HZ;          /**< Output data rate */
    lis2dh12_params.scale = LIS2DH12_SCALE_2G;          /**< Scale factor */
    lis2dh12_params.res = LIS2DH12_HR_12BIT;            /**< Resolution */
    
    if (lis2dh12_init(&dev_lis2dh12, &lis2dh12_params) == LIS2DH12_OK) {
        puts("[umdk-" _UMDK_NAME_ "] STMicro LIS2DH12 sensor found");
        active_sensors |= UMDK_INCLINOMETER_LIS2DH12;
        lis2dh12_power_off(&dev_lis2dh12);
        return true;
    }
    
    lis3dh_params_t lis3dh_params;
    
    lis3dh_params.i2c = I2C_DEV(UMDK_INCLINOMETER_I2C); /**< I2C device */
    lis3dh_params.addr = LIS3DH_I2C_SAD_L;              /**< Accelerometer I2C address */
    lis3dh_params.odr = LIS3DH_ODR_50Hz;                /**< Output data rate */
    lis3dh_params.scale = LIS3DH_2g;                    /**< Scale factor */
    lis3dh_params.op_mode = LIS3DH_HR_12bit;            /**< Resolution */
    
    if (lis3dh_init(&dev_lis3dh, &lis3dh_params, NULL, NULL) == 0) {
        puts("[umdk-" _UMDK_NAME_ "] STMicro LIS3DH sensor found");
        active_sensors |= UMDK_INCLINOMETER_LIS3DH;
        lis3dh_power_off(&dev_lis3dh);
        return true;
    }

    adxl345_params_t adxl_params = ADXL345_PARAMS;
    adxl_params.i2c = I2C_DEV(UMDK_INCLINOMETER_I2C);
    adxl_params.addr = ADXL345_PARAM_ADDR;
    adxl_params.range = ADXL345_RANGE_2G;
    
    if (adxl345_init(&dev_adxl345, &adxl_params) == ADXL345_OK) {
        puts("[umdk-" _UMDK_NAME_ "] Analog Devices ADXL345 sensor found");
        active_sensors |= UMDK_INCLINOMETER_ADXL345;
        adxl345_set_standby(&dev_adxl345);
        return true;
    }
    
    lsm6ds3_param_t lsm_params;
    lsm_params.i2c_addr = 0x6A;
    lsm_params.i2c = I2C_DEV(UMDK_INCLINOMETER_I2C);

    /* Configure the default settings */
    lsm_params.gyro_enabled = true;
    lsm_params.gyro_range = LSM6DS3_ACC_GYRO_FS_G_500dps;
    lsm_params.gyro_sample_rate = LSM6DS3_ACC_GYRO_ODR_XL_1660Hz;
    lsm_params.gyro_bandwidth = LSM6DS3_ACC_GYRO_BW_XL_400Hz;
    lsm_params.gyro_fifo_enabled = true;
    lsm_params.gyro_fifo_decimation = true;

    lsm_params.accel_enabled = true;
    lsm_params.accel_odr_off = true;
    lsm_params.accel_range = LSM6DS3_ACC_GYRO_FS_XL_16g;
    lsm_params.accel_sample_rate = LSM6DS3_ACC_GYRO_ODR_XL_1660Hz;
    lsm_params.accel_bandwidth = LSM6DS3_ACC_GYRO_BW_XL_400Hz;
    lsm_params.accel_fifo_enabled = true;
    lsm_params.accel_fifo_decimation = true;

    lsm_params.temp_enabled = true;

    lsm_params.comm_mode = 1;

    lsm_params.fifo_threshold = 3000;
    lsm_params.fifo_sample_rate = LSM6DS3_ACC_GYRO_ODR_FIFO_1600Hz;
    lsm_params.fifo_mode_word = 0;

    dev_lsm6ds3.params = lsm_params;
        
    if (lsm6ds3_init(&dev_lsm6ds3) == 0) {
        puts("[umdk-" _UMDK_NAME_ "] ST LSM6DS3 sensor found");
        active_sensors |= UMDK_INCLINOMETER_LSM6DS3;
        return true;
    }

	return false;
}

static void *measure_thread(void *arg) {
    (void)arg;
    
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);
    
    puts("[umdk-" _UMDK_NAME_ "] Periodic measurement thread started");

    while (1) {
        msg_receive(&msg);
        double x = 0, y = 0, z = 0;
        
        /* only one of available sensors is enabled */
        if (active_sensors & UMDK_INCLINOMETER_LIS2HH12) {
            lis2hh12_data_t lis2hh12_data;
        
            lis2hh12_poweron(&dev_lis2hh12);
            lis2hh12_read_xyz(&dev_lis2hh12, &lis2hh12_data);

            int16_t temp_value;
            lis2hh12_read_temp(&dev_lis2hh12, &temp_value);
            lis2hh12_poweroff(&dev_lis2hh12);

            /* Copy measurements into response */
            x = lis2hh12_data.x_axis;
            y = lis2hh12_data.y_axis;
            z = lis2hh12_data.z_axis;
        }
        
        if (active_sensors & UMDK_INCLINOMETER_ADXL345) {
            adxl345_set_measure(&dev_adxl345);
            lptimer_sleep(12);
            adxl345_data_t adxl345_data;
            adxl345_read(&dev_adxl345, &adxl345_data);
            adxl345_set_standby(&dev_adxl345);
        
            x = adxl345_data.x;
            y = adxl345_data.y;
            z = adxl345_data.z;
        }
        
        if (active_sensors & UMDK_INCLINOMETER_LSM6DS3) {
            lsm6ds3_data_t lsm6ds3_data;
            lsm6ds3_poweron(&dev_lsm6ds3);
            lsm6ds3_read_acc(&dev_lsm6ds3, &lsm6ds3_data);
            lsm6ds3_poweroff(&dev_lsm6ds3);
            
            x = lsm6ds3_data.acc_x;
            y = lsm6ds3_data.acc_y;
            z = lsm6ds3_data.acc_z;
        }
        
        if (active_sensors & UMDK_INCLINOMETER_LIS2DH12) {
            lis2dh12_acc_t lis2dh12_data;
            lis2dh12_power_on(&dev_lis2dh12);
            lis2dh12_read_xyz(&dev_lis2dh12, &lis2dh12_data);
            lis2dh12_power_off(&dev_lis2dh12);
            
            x = lis2dh12_data.axis_x;
            y = lis2dh12_data.axis_y;
            z = lis2dh12_data.axis_z;
        }
        
        if (active_sensors & UMDK_INCLINOMETER_LIS3DH) {
            lis3dh_acceleration_t lis3dh_data;
            lis3dh_power_on(&dev_lis3dh);
            lis3dh_read_xyz(&dev_lis3dh, &lis3dh_data);
            lis3dh_power_off(&dev_lis3dh);
            
            x = lis3dh_data.axis_x;
            y = lis3dh_data.axis_y;
            z = lis3dh_data.axis_z;
        }

        char acc[3][10];
        
#if ENABLE_DEBUG
        /* printf with native float support costs too much */
        int_to_float_str(acc[0], (int)x, 3);
        int_to_float_str(acc[1], (int)y, 3);
        int_to_float_str(acc[2], (int)z, 3);
        printf("Acceleration: X %s mg, Y %s mg, Z %s mg\n", acc[0], acc[1], acc[2]);
#endif

        theta.previous = theta.current;
        phi.previous = phi.current;

        if (y != 0) {
            phi.current = atan2(z, y) * RADIAN_TO_DEGREE_MILLIS;
            theta.current = atan2((-x) , sqrt(y*y + z*z)) * RADIAN_TO_DEGREE_MILLIS;
        } else {
            theta.current = 90000;
            phi.current = 90000;
        }
        
        if (theta.max < theta.current) {
            theta.max = theta.current;
        }
        if (theta.min > theta.current) {
            theta.min = theta.current;
        }
        
        if (phi.max < phi.current) {
            phi.max = phi.current;
        }
        if (phi.min > phi.current) {
            phi.min = phi.current;
        }
        
        if (abs(theta.current - theta.previous) > inclinometer_config.threshold_xz) {
            msg_send(&alarm_msg, timer_pid);
        }
        
        if (abs(phi.current - phi.previous) > inclinometer_config.threshold_yz) {
            msg_send(&alarm_msg, timer_pid);
        }
        
        int_to_float_str(acc[0], (int)theta.current, 3);
        int_to_float_str(acc[1], (int)theta.max, 3);
        int_to_float_str(acc[2], (int)theta.min, 3);
        printf("Theta: %s [ %s - %s ]\n", acc[0], acc[1], acc[2]);
        
        int_to_float_str(acc[0], (int)phi.current, 3);
        int_to_float_str(acc[1], (int)phi.max, 3);
        int_to_float_str(acc[2], (int)phi.min, 3);
        printf("Phi: %s [ %s - %s ]\n", acc[0], acc[1], acc[2]);
        
        /* Restart after delay */
        lptimer_set_msg(&measure_timer, 1000 * inclinometer_config.rate, &measure_msg, measure_pid);
    }

    return NULL;
}

static uint32_t last_publish_time = 0;
static bool alarm_was_sent = false;

static void *publish_thread(void *arg) {
    (void)arg;
    
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);
    
    puts("[umdk-" _UMDK_NAME_ "] Periodic publisher thread started");

    while (1) {
        msg_receive(&msg);
        
        if ((msg.type == INCLINOMETER_ALARM_MESSAGE) && alarm_was_sent) {
            puts("[umdk-" _UMDK_NAME_ "] Ignore repeated alarm message");
            continue;
        }
        
        module_data_t data = {};
        data.as_ack = is_polled;
        is_polled = false;
        
        data.data[0] = _UMDK_MID_;
        switch (msg.type) {
            case INCLINOMETER_NORMAL_MESSAGE:
                data.data[1] = UMDK_INCLINOMETER_DATA;
                alarm_was_sent = false;
                break;
            case INCLINOMETER_ALARM_MESSAGE:
                data.data[1] = UMDK_INCLINOMETER_ALARM;
                alarm_was_sent = true;
                break;
            default:
                data.data[1] = UMDK_INCLINOMETER_DATA;
                alarm_was_sent = false;
                break;
        }        
        data.length = 2;
        
        int16_t th = (theta.current + 5)/10;
        convert_to_be_sam((void *)&th, sizeof(th));
        memcpy((void *)&data.data[data.length], (uint8_t *)&th, sizeof(th));
        data.length += sizeof(th);
        
        th = (theta.min + 5)/10;
        convert_to_be_sam((void *)&th, sizeof(th));
        memcpy((void *)&data.data[data.length], (uint8_t *)&th, sizeof(th));
        data.length += sizeof(th);
        
        th = (theta.max + 5)/10;
        convert_to_be_sam((void *)&th, sizeof(th));
        memcpy((void *)&data.data[data.length], (uint8_t *)&th, sizeof(th));
        data.length += sizeof(th);
        
        int16_t ph = (phi.current + 5)/10;
        convert_to_be_sam((void *)&ph, sizeof(ph));
        memcpy((void *)&data.data[data.length], (uint8_t *)&ph, sizeof(ph));
        data.length += sizeof(ph);
        
        ph = (phi.min + 5)/10;
        convert_to_be_sam((void *)&ph, sizeof(ph));
        memcpy((void *)&data.data[data.length], (uint8_t *)&ph, sizeof(ph));
        data.length += sizeof(ph);
        
        ph = (phi.max + 5)/10;
        convert_to_be_sam((void *)&ph, sizeof(ph));
        memcpy((void *)&data.data[data.length], (uint8_t *)&ph, sizeof(ph));
        data.length += sizeof(ph);
        
        /* Notify the application */
        callback(&data);
        
        /* reset measurement data */
        theta.max = INT_MIN;
        theta.min = INT_MAX;
        
        phi.max = INT_MIN;
        phi.min = INT_MAX;
        
        last_publish_time = lptimer_now_msec();
        
        /* Restart after delay */
        lptimer_set_msg(&timer, 1000 * inclinometer_config.publish_period_sec, &timer_msg, timer_pid);
    }

    return NULL;
}

static void reset_config(void) {
	inclinometer_config.publish_period_sec = UMDK_INCLINOMETER_PUBLISH_PERIOD_SEC;
	inclinometer_config.i2c_dev = UMDK_INCLINOMETER_I2C;
    inclinometer_config.threshold_xz = 4500;
    inclinometer_config.threshold_yz = 4500;
    inclinometer_config.rate = UMDK_INCLINOMETER_RATE_SEC;
}

static void init_config(void) {
	reset_config();

	if (!unwds_read_nvram_config(_UMDK_MID_, (uint8_t *) &inclinometer_config, sizeof(inclinometer_config)))
		reset_config();

	if (inclinometer_config.i2c_dev >= I2C_NUMOF) {
		reset_config();
		return;
	}
}

static inline void save_config(void) {
	unwds_write_nvram_config(_UMDK_MID_, (uint8_t *) &inclinometer_config, sizeof(inclinometer_config));
}

static void set_period (int period) {
    lptimer_remove(&timer);

    inclinometer_config.publish_period_sec = period;
	save_config();

	/* Don't restart timer if new period is zero */
	if (inclinometer_config.publish_period_sec) {
        lptimer_set_msg(&timer, 1000 * inclinometer_config.publish_period_sec, &timer_msg, timer_pid);
		printf("[umdk-" _UMDK_NAME_ "] Period set to %d sec\n", inclinometer_config.publish_period_sec);
    } else {
        lptimer_remove(&timer);
        puts("[umdk-" _UMDK_NAME_ "] Timer stopped");
    }
}

static void set_rate (int rate) {
    inclinometer_config.rate = rate;
	save_config();
    
    if (inclinometer_config.rate) {
        lptimer_set_msg(&measure_timer, 1000 * inclinometer_config.rate, &measure_msg, measure_pid);
		printf("[umdk-" _UMDK_NAME_ "] Rate set to %d sec\n", inclinometer_config.rate);
    } else {
        lptimer_remove(&measure_timer);
        puts("[umdk-" _UMDK_NAME_ "] Timer stopped");
    }
}

static void set_threshold (int xz, int yz) {
    if (xz != 0) {
        inclinometer_config.threshold_xz = xz;
    }
    
    if (yz != 0) {
        inclinometer_config.threshold_yz = yz;
    }

    printf("[umdk-" _UMDK_NAME_ "] Threshold set to %d / %d degrees\n",
                inclinometer_config.threshold_xz/100, inclinometer_config.threshold_yz/100);
	save_config();
}

int umdk_inclinometer_shell_cmd(int argc, char **argv) {
    if (argc == 1) {
        puts (_UMDK_NAME_ " get - get results now");
        puts (_UMDK_NAME_ " send - get and send results now");
        puts (_UMDK_NAME_ " period <N> - set publish period to N seconds");
        puts (_UMDK_NAME_ " rate <N> - set measurement period to N seconds");
        puts (_UMDK_NAME_ " reset - reset settings to default");
        return 0;
    }
    
    char *cmd = argv[1];
	
    if (strcmp(cmd, "get") == 0) {
        msg_send(&timer_msg, measure_pid);
    }
    
    if (strcmp(cmd, "send") == 0) {
		/* Send signal to publisher thread */
		msg_send(&timer_msg, timer_pid);
    }
    
    if (strcmp(cmd, "period") == 0) {
        char *val = argv[2];
        set_period(atoi(val));
    }
    
    if (strcmp(cmd, "rate") == 0) {
        char *val = argv[2];
        set_rate(atoi(val));
    }
    
    if (strcmp(cmd, "reset") == 0) {
        reset_config();
        save_config();
    }
    
    return 1;
}

void umdk_inclinometer_init(uwnds_cb_t *event_callback) {

	callback = event_callback;
    
    theta.max = INT_MIN;
    theta.min = INT_MAX;
    
    phi.max = INT_MIN;
    phi.min = INT_MAX;
    
	init_config();
	printf("[umdk-" _UMDK_NAME_ "] Publish period: %d sec\n", inclinometer_config.publish_period_sec);
    printf("[umdk-" _UMDK_NAME_ "] Measurement period: %d sec\n", inclinometer_config.rate);

	if (!init_sensor()) {
		puts("[umdk-" _UMDK_NAME_ "] No sensors found");
        return;
	}

    /* Create handler thread */
	char *stack_measure = (char *) allocate_stack(UMDK_INCLINOMETER_STACK_SIZE);
	if (!stack_measure) {
		return;
	}
    
    measure_pid = thread_create(stack_measure, UMDK_INCLINOMETER_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, measure_thread, NULL, "inc meas");

    /* Start measuring timer */
    lptimer_set_msg(&measure_timer, 1000 * inclinometer_config.rate, &measure_msg, measure_pid);

    /* Create handler thread */
	char *stack = (char *) allocate_stack(UMDK_INCLINOMETER_STACK_SIZE);
	if (!stack) {
		return;
	}
  
    timer_pid = thread_create(stack, UMDK_INCLINOMETER_STACK_SIZE, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, publish_thread, NULL, "inc pub");
  
    /* Start publishing timer */
	lptimer_set_msg(&timer, 1000 * inclinometer_config.publish_period_sec, &timer_msg, timer_pid);
    
    unwds_add_shell_command( _UMDK_NAME_, "type '" _UMDK_NAME_ "' for commands list", umdk_inclinometer_shell_cmd);
}

static void reply_fail(module_data_t *reply) {
	reply->data[0] = _UMDK_MID_;
    reply->data[1] = UMDK_INCLINOMETER_FAIL;
    reply->length = 2;
}

static void reply_ok(module_data_t *reply) {
	reply->data[0] = _UMDK_MID_;
	reply->data[1] = UMDK_INCLINOMETER_CONFIG;
    reply->length = 2;
    
    uint16_t period = inclinometer_config.publish_period_sec;
    uint16_t rate = inclinometer_config.rate;
    uint16_t xz = inclinometer_config.threshold_xz/10;
    uint16_t yz = inclinometer_config.threshold_yz/10;
    
    convert_to_be_sam((void *)&period, sizeof(period));
    convert_to_be_sam((void *)&rate, sizeof(rate));
    convert_to_be_sam((void *)&xz, sizeof(xz));
    convert_to_be_sam((void *)&yz, sizeof(yz));
    
    memcpy(&reply->data[reply->length], (void *)&rate, sizeof(rate));
    reply->length += sizeof(rate);
    
    memcpy(&reply->data[reply->length], (void *)&period, sizeof(period));
    reply->length += sizeof(period);
    
    memcpy(&reply->data[reply->length], (void *)&xz, sizeof(xz));
    reply->length += sizeof(xz);
    
    memcpy(&reply->data[reply->length], (void *)&yz, sizeof(yz));
    reply->length += sizeof(yz);
}

bool umdk_inclinometer_cmd(module_data_t *cmd, module_data_t *reply) {
	if (cmd->length < 1) {
		reply_fail(reply);
		return true;
	}
    
    if ((cmd->data[0] == UMDK_INCLINOMETER_CONFIG) && (cmd->length == 9)) {
        int16_t rate = cmd->data[1] | cmd->data[2] << 8;
        convert_from_be_sam((void *)&rate, sizeof(rate));
        if (rate > 0) {
            set_rate(rate);
        } else {
            puts("[umdk-" _UMDK_NAME_ "] rate: do not change");
        }

        int16_t period = cmd->data[3] | cmd->data[4] << 8;
        convert_from_be_sam((void *)&period, sizeof(period));
        if (period > 0) {
            set_period(period);
        } else {
            puts("[umdk-" _UMDK_NAME_ "] period: do not change");
        }
        
        uint16_t threshold_xz = cmd->data[5] | cmd->data[6] << 8;
        uint16_t threshold_yz = cmd->data[7] | cmd->data[8] << 8;
        
        convert_from_be_sam((void *)&threshold_xz, sizeof(threshold_xz));
        convert_from_be_sam((void *)&threshold_yz, sizeof(threshold_yz));
        
        set_threshold(threshold_xz*10, threshold_yz*10);

        /* reply with configuration */
        reply_ok(reply);
    } else {
        puts("[umdk-" _UMDK_NAME_ "] Incorrect command");
        reply_fail(reply);
    }

	return true;
}

#ifdef __cplusplus
}
#endif
