/*
 * Copyright (C) 2016 Unwired Devices [info@unwds.com]
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup
 * @ingroup
 * @brief
 * @{
 * @file		umdk-mhz19.c
 * @brief       umdk-mhz19 module implementation
 * @author      EP
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "periph/gpio.h"
#include "periph/uart.h"

#include "board.h"

#include "unwds-common.h"
#include "include/umdk-mhz19.h"

#include "thread.h"
#include "xtimer.h"
#include "rtctimers.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

static uwnds_cb_t *callback;
static uint8_t rxbuf[UMDK_MHZ19_RXBUF_SIZE] = {};

static volatile uint8_t num_bytes_received;

static kernel_pid_t writer_pid;

static msg_t send_msg;
static msg_t send_msg_ovf;
// static msg_t timer_msg1;
static xtimer_t send_timer;

typedef struct {
	uint8_t is_valid;
    uint8_t publish_period_sec;
} umdk_mhz19_config_t;

static umdk_mhz19_config_t umdk_mhz19_config = { .is_valid = 0, .publish_period_sec = 5};

static bool is_polled = false;
static rtctimer_t timer;
static msg_t timer_msg = {};
static kernel_pid_t timer_pid;


void umdk_mhz19_ask(void){
        /*
        uint8_t data[8] = {0x01, 0x03, 0x01, 0x05, 0x00, 0x04, 0x55, 0xf4}; // for modbus
        uint8_t count = 8; // for modbus
        */

        uint8_t data[9] = {0xFF,0x01,0x86,0x00,0x00,0x00,0x00,0x00,0x79}; // for original mh-z19 protocol
        
        uart_write(UART_DEV(UMDK_UART_DEV), (uint8_t *)data, sizeof(data));
}

static void *timer_thread(void *arg) {
    msg_t msg;
    msg_t msg_queue[4];
    msg_init_queue(msg_queue, 4);
    
    puts("[umdk-mhz19] Periodic publisher thread started");

    while (1) {
        msg_receive(&msg);

        puts ("[umdk-mhz19] Periodic publisher thread received a message!");

        umdk_mhz19_ask();

        /* Restart after delay */
        rtctimers_set_msg(&timer, umdk_mhz19_config.publish_period_sec, &timer_msg, timer_pid);
    }
    puts ("[umdk-mhz19] Periodic publisher thread ended!");

    return NULL;
}

void *writer(void *arg) {
    msg_t msg;
    msg_t msg_queue[128];
    msg_init_queue(msg_queue, 128);

    while (1) {
        msg_receive(&msg);

        module_data_t data;
        data.data[0] = UNWDS_MHZ19_MODULE_ID;
        data.length = 1;

        char buf[200];
        char *pos = buf;
        int k = 0;
        for (k = 0; k < num_bytes_received; k++) {
            snprintf(pos, 3, " %02x", rxbuf[k]);
            pos += 3;
        }

        DEBUG("[umdk-mhz19] received 0x%s\n", buf);

        /*
        // for modbus
        int co2 = data.data[2+3] * 256 + data.data[2+4];
        int raw = data.data[2+9] * 256 + data.data[2+10];

        printf("[umdk-mhz19] CO2: %d, %d\n", co2, raw);
        // for modbus
        */

        // for original mh-z19 protocol
        int16_t co2 = rxbuf[2] * 256 + rxbuf[3];
        int16_t temperature = rxbuf[4] - 40;
        uint8_t confidence = rxbuf[5];

        num_bytes_received = 0;

        printf("[umdk-mhz19] CO2: %d, temperature: %d, confidence: %" PRIu8 " \n", (int)co2, (int)temperature, confidence);
        // for original mh-z19 protocol

        memcpy((void *)&data.data[data.length], &co2, sizeof(co2));
        data.length += sizeof(co2);

        temperature *= 10;
        memcpy((void *)&data.data[data.length], &temperature, sizeof(temperature));
        data.length += sizeof(temperature);

        data.data[data.length] = confidence;
        data.length++;

        data.as_ack = is_polled;
        is_polled = false;

        callback(&data);
    }

    return NULL;
}

void rx_cb(void *arg, uint8_t data)
{
	/* Buffer overflow */
	if (num_bytes_received == UMDK_MHZ19_RXBUF_SIZE) {
		num_bytes_received = 0;
		return;
	}

	rxbuf[num_bytes_received++] = data;

	/* Schedule sending after timeout */
	xtimer_set_msg(&send_timer, 1e3 * UMDK_MHZ19_SYMBOL_TIMEOUT_MS, &send_msg, writer_pid);
}

static void reset_config(void) {
	umdk_mhz19_config.is_valid = 0;
    umdk_mhz19_config.publish_period_sec = 60;
}

static void init_config(void) {
	reset_config();

	if (!unwds_read_nvram_config(UNWDS_MHZ19_MODULE_ID, (uint8_t *) &umdk_mhz19_config, sizeof(umdk_mhz19_config)))
		return;

	if ((umdk_mhz19_config.is_valid == 0xFF) || (umdk_mhz19_config.is_valid == 0))  {
		reset_config();
		return;
	}
}

static inline void save_config(void) {
	umdk_mhz19_config.is_valid = 1;
	unwds_write_nvram_config(UNWDS_MHZ19_MODULE_ID, (uint8_t *) &umdk_mhz19_config, sizeof(umdk_mhz19_config));
}

int umdk_mhz19_shell_cmd(int argc, char **argv) {
    if (argc == 1) {
        puts ("mhz19 send - ask MH-Z19 for CO2 concentration (equivalent to mhz19 send 01030105000455f4 )");
        puts ("mhz19 raw <hex> - send raw data to MH-Z19, without leading 0x");
        puts ("mhz19 period <period> - set publishing period");
        puts ("mhz19 reset - reset settings to default");
        return 0;
    }
    
    char *cmd = argv[1];
    
    if (strcmp(cmd, "send") == 0) {
        is_polled = true;
        // umdk_mhz19_ask();
        msg_send(&timer_msg, timer_pid);
    }
    
    if (strcmp(cmd, "raw") == 0) {
        is_polled = true;
        char *pos = argv[2];
        
        if ((strlen(pos) % 2) != 0 ) {
            puts("[umdk-mhz19] Error: hex number length must be even");
            return 0;
        }
        
        if ((strlen(pos)) > 400 ) {
            puts("[umdk-mhz19] Error: over 200 bytes of data");
            return 0;
        }

        uint8_t data[200];
        uint8_t count = 0;
        uint8_t i = 0;
        char buf[3] = { 0 };
        for(i = 0; i < strlen(argv[2])/2; i++) {
            /* copy 2 hex symbols to a new array */
            memcpy(buf, pos, 2);
            pos += 2;

            if (strcmp(buf, "0x") && strcmp(buf, "0X")) {
                data[count] = strtol(buf, NULL, 16);
                count++;
            }
        }
        
        /* Send data */
        uart_write(UART_DEV(UMDK_UART_DEV), (uint8_t *) data, count);
    }
    
    if (strcmp(cmd, "period") == 0) {
        char *val = argv[2];
        umdk_mhz19_config.publish_period_sec = atoi(val);
        save_config();
    }
    
    if (strcmp(cmd, "reset") == 0) {
        reset_config();
        save_config();
    }
    
    return 1;
}

void umdk_mhz19_init(uint32_t *non_gpio_pin_map, uwnds_cb_t *event_callback)
{
    (void) non_gpio_pin_map;
    callback = event_callback;

    init_config();

    uart_params_t uart_params;
    uart_params.baudrate = 9600;
    uart_params.parity = UART_PARITY_NOPARITY;
    uart_params.stopbits = UART_STOPBITS_10;
    uart_params.databits = UART_DATABITS_8;

    /* Initialize UART */
    if (uart_init_ext(UART_DEV(UMDK_UART_DEV), &uart_params, rx_cb, NULL)) {
        return;
    }

    send_msg.content.value = 0;
    send_msg_ovf.content.value = 1;

    char *stack = (char *) allocate_stack();
    if (!stack) {
    	puts("umdk-mhz19: unable to allocate memory. Is too many modules enabled?");
    	return;
    }
    /* Create handler thread */
    writer_pid = thread_create(stack, UNWDS_STACK_SIZE_BYTES, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, writer, NULL, "umdk-mhz19 listening thread");

    char *timer_stack = (char *) allocate_stack();
    if (!timer_stack) {
        puts("umdk-mhz19: unable to allocate memory. Is too many modules enabled?");
        return;
    }
    timer_pid = thread_create(timer_stack, UNWDS_STACK_SIZE_BYTES, THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST, timer_thread, NULL, "umdk-mhz19 timer thread");
    /* Start publishing timer */
    rtctimers_set_msg(&timer, umdk_mhz19_config.publish_period_sec, &timer_msg, timer_pid);
    // msg_send(&timer_msg, timer_pid);

    unwds_add_shell_command("mhz19", "type 'mhz19' for commands list", umdk_mhz19_shell_cmd);

}

static void do_reply(module_data_t *reply, umdk_mhz19_reply_t r)
{
    reply->length = 2;
    reply->data[0] = UNWDS_MHZ19_MODULE_ID;
    reply->data[1] = r;
}

bool umdk_mhz19_cmd(module_data_t *data, module_data_t *reply)
{
    if (data->length < 1) {
        do_reply(reply, UMDK_MHZ19_REPLY_ERR_FMT);
        return true;
    }

    umdk_mhz19_prefix_t prefix = data->data[0];
    switch (prefix) {
        case UMDK_MHZ19_ASK:
            is_polled = true;
            // umdk_mhz19_ask();
            msg_send(&timer_msg, timer_pid);
        case UMDK_MHZ19_SET_PERIOD:
            if (data->length != 2) {
                do_reply(reply, UMDK_MHZ19_REPLY_ERR_FMT);
                break;
            }

            umdk_mhz19_config.publish_period_sec = 60*(data->data[1]);
            do_reply(reply, UMDK_MHZ19_REPLY_OK);
            break;

        default:
        	do_reply(reply, UMDK_MHZ19_REPLY_ERR_FMT);
        	break;
    }

    return true;
}

#ifdef __cplusplus
}
#endif
