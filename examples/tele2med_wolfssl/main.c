/*
 * Copyright (C) 2006-2017 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       wolfSSL client example
 *
 * @author      Oleg Manchenko
 *
 * @}
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* socket includes */
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <unistd.h>

/* wolfSSL */
#include <wolfssl/ssl.h>
#include <wolfssl/certs_test.h>

#include "periph/gpio.h"
#include "rtctimers-millis.h"
#include "sim5300.h"

#include "od.h"

#define SIM5300_TIME_ON         (500)           /* The time of active low level impulse of PWRKEY pin to power on module. Min: 50ms, typ: 100ms */
#define SIM5300_UART            (T2M_UART_GSM)  /* UART number for modem */
#define SIM5300_BAUDRATE        (9600)          /* UART baudrate for modem*/
#define SIM5300_TIME_ON_UART    (3000)          /* The time from power-on issue to UART port ready. Min: 3s, max: 5s */
#define AT_DEV_BUF_SIZE         (2048)          /* The size of the buffer for all incoming data from modem */
#define AT_DEV_RESP_SIZE        (2048)          /* The size of the buffer to answer the command from the modem */

static sim5300_dev_t sim5300_dev;               /* Struct for SIM5300 */

static int sockfd;                              /* Socket */

static char at_dev_buf[AT_DEV_BUF_SIZE];        /* Buffer for incoming data from modem SIM5300 */
static char at_dev_resp[AT_DEV_RESP_SIZE];      /* Buffer for parse incoming data from modem SIM5300 */

// #define DEFAULT_PORT 11111

unsigned char t2m_cert[846] = {
	0x30, 0x82, 0x03, 0x4A, 0x30, 0x82, 0x02, 0x32, 0xA0, 0x03, 0x02, 0x01,
	0x02, 0x02, 0x10, 0x44, 0xAF, 0xB0, 0x80, 0xD6, 0xA3, 0x27, 0xBA, 0x89,
	0x30, 0x39, 0x86, 0x2E, 0xF8, 0x40, 0x6B, 0x30, 0x0D, 0x06, 0x09, 0x2A,
	0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x3F,
	0x31, 0x24, 0x30, 0x22, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x1B, 0x44,
	0x69, 0x67, 0x69, 0x74, 0x61, 0x6C, 0x20, 0x53, 0x69, 0x67, 0x6E, 0x61,
	0x74, 0x75, 0x72, 0x65, 0x20, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20, 0x43,
	0x6F, 0x2E, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
	0x0E, 0x44, 0x53, 0x54, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x43, 0x41,
	0x20, 0x58, 0x33, 0x30, 0x1E, 0x17, 0x0D, 0x30, 0x30, 0x30, 0x39, 0x33,
	0x30, 0x32, 0x31, 0x31, 0x32, 0x31, 0x39, 0x5A, 0x17, 0x0D, 0x32, 0x31,
	0x30, 0x39, 0x33, 0x30, 0x31, 0x34, 0x30, 0x31, 0x31, 0x35, 0x5A, 0x30,
	0x3F, 0x31, 0x24, 0x30, 0x22, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x1B,
	0x44, 0x69, 0x67, 0x69, 0x74, 0x61, 0x6C, 0x20, 0x53, 0x69, 0x67, 0x6E,
	0x61, 0x74, 0x75, 0x72, 0x65, 0x20, 0x54, 0x72, 0x75, 0x73, 0x74, 0x20,
	0x43, 0x6F, 0x2E, 0x31, 0x17, 0x30, 0x15, 0x06, 0x03, 0x55, 0x04, 0x03,
	0x13, 0x0E, 0x44, 0x53, 0x54, 0x20, 0x52, 0x6F, 0x6F, 0x74, 0x20, 0x43,
	0x41, 0x20, 0x58, 0x33, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0D, 0x06, 0x09,
	0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03,
	0x82, 0x01, 0x0F, 0x00, 0x30, 0x82, 0x01, 0x0A, 0x02, 0x82, 0x01, 0x01,
	0x00, 0xDF, 0xAF, 0xE9, 0x97, 0x50, 0x08, 0x83, 0x57, 0xB4, 0xCC, 0x62,
	0x65, 0xF6, 0x90, 0x82, 0xEC, 0xC7, 0xD3, 0x2C, 0x6B, 0x30, 0xCA, 0x5B,
	0xEC, 0xD9, 0xC3, 0x7D, 0xC7, 0x40, 0xC1, 0x18, 0x14, 0x8B, 0xE0, 0xE8,
	0x33, 0x76, 0x49, 0x2A, 0xE3, 0x3F, 0x21, 0x49, 0x93, 0xAC, 0x4E, 0x0E,
	0xAF, 0x3E, 0x48, 0xCB, 0x65, 0xEE, 0xFC, 0xD3, 0x21, 0x0F, 0x65, 0xD2,
	0x2A, 0xD9, 0x32, 0x8F, 0x8C, 0xE5, 0xF7, 0x77, 0xB0, 0x12, 0x7B, 0xB5,
	0x95, 0xC0, 0x89, 0xA3, 0xA9, 0xBA, 0xED, 0x73, 0x2E, 0x7A, 0x0C, 0x06,
	0x32, 0x83, 0xA2, 0x7E, 0x8A, 0x14, 0x30, 0xCD, 0x11, 0xA0, 0xE1, 0x2A,
	0x38, 0xB9, 0x79, 0x0A, 0x31, 0xFD, 0x50, 0xBD, 0x80, 0x65, 0xDF, 0xB7,
	0x51, 0x63, 0x83, 0xC8, 0xE2, 0x88, 0x61, 0xEA, 0x4B, 0x61, 0x81, 0xEC,
	0x52, 0x6B, 0xB9, 0xA2, 0xE2, 0x4B, 0x1A, 0x28, 0x9F, 0x48, 0xA3, 0x9E,
	0x0C, 0xDA, 0x09, 0x8E, 0x3E, 0x17, 0x2E, 0x1E, 0xDD, 0x20, 0xDF, 0x5B,
	0xC6, 0x2A, 0x8A, 0xAB, 0x2E, 0xBD, 0x70, 0xAD, 0xC5, 0x0B, 0x1A, 0x25,
	0x90, 0x74, 0x72, 0xC5, 0x7B, 0x6A, 0xAB, 0x34, 0xD6, 0x30, 0x89, 0xFF,
	0xE5, 0x68, 0x13, 0x7B, 0x54, 0x0B, 0xC8, 0xD6, 0xAE, 0xEC, 0x5A, 0x9C,
	0x92, 0x1E, 0x3D, 0x64, 0xB3, 0x8C, 0xC6, 0xDF, 0xBF, 0xC9, 0x41, 0x70,
	0xEC, 0x16, 0x72, 0xD5, 0x26, 0xEC, 0x38, 0x55, 0x39, 0x43, 0xD0, 0xFC,
	0xFD, 0x18, 0x5C, 0x40, 0xF1, 0x97, 0xEB, 0xD5, 0x9A, 0x9B, 0x8D, 0x1D,
	0xBA, 0xDA, 0x25, 0xB9, 0xC6, 0xD8, 0xDF, 0xC1, 0x15, 0x02, 0x3A, 0xAB,
	0xDA, 0x6E, 0xF1, 0x3E, 0x2E, 0xF5, 0x5C, 0x08, 0x9C, 0x3C, 0xD6, 0x83,
	0x69, 0xE4, 0x10, 0x9B, 0x19, 0x2A, 0xB6, 0x29, 0x57, 0xE3, 0xE5, 0x3D,
	0x9B, 0x9F, 0xF0, 0x02, 0x5D, 0x02, 0x03, 0x01, 0x00, 0x01, 0xA3, 0x42,
	0x30, 0x40, 0x30, 0x0F, 0x06, 0x03, 0x55, 0x1D, 0x13, 0x01, 0x01, 0xFF,
	0x04, 0x05, 0x30, 0x03, 0x01, 0x01, 0xFF, 0x30, 0x0E, 0x06, 0x03, 0x55,
	0x1D, 0x0F, 0x01, 0x01, 0xFF, 0x04, 0x04, 0x03, 0x02, 0x01, 0x06, 0x30,
	0x1D, 0x06, 0x03, 0x55, 0x1D, 0x0E, 0x04, 0x16, 0x04, 0x14, 0xC4, 0xA7,
	0xB1, 0xA4, 0x7B, 0x2C, 0x71, 0xFA, 0xDB, 0xE1, 0x4B, 0x90, 0x75, 0xFF,
	0xC4, 0x15, 0x60, 0x85, 0x89, 0x10, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86,
	0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01,
	0x01, 0x00, 0xA3, 0x1A, 0x2C, 0x9B, 0x17, 0x00, 0x5C, 0xA9, 0x1E, 0xEE,
	0x28, 0x66, 0x37, 0x3A, 0xBF, 0x83, 0xC7, 0x3F, 0x4B, 0xC3, 0x09, 0xA0,
	0x95, 0x20, 0x5D, 0xE3, 0xD9, 0x59, 0x44, 0xD2, 0x3E, 0x0D, 0x3E, 0xBD,
	0x8A, 0x4B, 0xA0, 0x74, 0x1F, 0xCE, 0x10, 0x82, 0x9C, 0x74, 0x1A, 0x1D,
	0x7E, 0x98, 0x1A, 0xDD, 0xCB, 0x13, 0x4B, 0xB3, 0x20, 0x44, 0xE4, 0x91,
	0xE9, 0xCC, 0xFC, 0x7D, 0xA5, 0xDB, 0x6A, 0xE5, 0xFE, 0xE6, 0xFD, 0xE0,
	0x4E, 0xDD, 0xB7, 0x00, 0x3A, 0xB5, 0x70, 0x49, 0xAF, 0xF2, 0xE5, 0xEB,
	0x02, 0xF1, 0xD1, 0x02, 0x8B, 0x19, 0xCB, 0x94, 0x3A, 0x5E, 0x48, 0xC4,
	0x18, 0x1E, 0x58, 0x19, 0x5F, 0x1E, 0x02, 0x5A, 0xF0, 0x0C, 0xF1, 0xB1,
	0xAD, 0xA9, 0xDC, 0x59, 0x86, 0x8B, 0x6E, 0xE9, 0x91, 0xF5, 0x86, 0xCA,
	0xFA, 0xB9, 0x66, 0x33, 0xAA, 0x59, 0x5B, 0xCE, 0xE2, 0xA7, 0x16, 0x73,
	0x47, 0xCB, 0x2B, 0xCC, 0x99, 0xB0, 0x37, 0x48, 0xCF, 0xE3, 0x56, 0x4B,
	0xF5, 0xCF, 0x0F, 0x0C, 0x72, 0x32, 0x87, 0xC6, 0xF0, 0x44, 0xBB, 0x53,
	0x72, 0x6D, 0x43, 0xF5, 0x26, 0x48, 0x9A, 0x52, 0x67, 0xB7, 0x58, 0xAB,
	0xFE, 0x67, 0x76, 0x71, 0x78, 0xDB, 0x0D, 0xA2, 0x56, 0x14, 0x13, 0x39,
	0x24, 0x31, 0x85, 0xA2, 0xA8, 0x02, 0x5A, 0x30, 0x47, 0xE1, 0xDD, 0x50,
	0x07, 0xBC, 0x02, 0x09, 0x90, 0x00, 0xEB, 0x64, 0x63, 0x60, 0x9B, 0x16,
	0xBC, 0x88, 0xC9, 0x12, 0xE6, 0xD2, 0x7D, 0x91, 0x8B, 0xF9, 0x3D, 0x32,
	0x8D, 0x65, 0xB4, 0xE9, 0x7C, 0xB1, 0x57, 0x76, 0xEA, 0xC5, 0xB6, 0x28,
	0x39, 0xBF, 0x15, 0x65, 0x1C, 0xC8, 0xF6, 0x77, 0x96, 0x6A, 0x0A, 0x8D,
	0x77, 0x0B, 0xD8, 0x91, 0x0B, 0x04, 0x8E, 0x07, 0xDB, 0x29, 0xB6, 0x0A,
	0xEE, 0x9D, 0x82, 0x35, 0x35, 0x10
};

time_t get_epoch_time(struct tm *time) {
    (void) time;
    return 1567096992;
}

int unwired_recv(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
    (void) ssl;
    (void) ctx;

    int res;

    printf("[Socket] Recv\n");

    if (sz <= RECEIVE_MAX_LEN) {
        /* Receive data up to RECEIVE_MAX_LEN bytes in size */
        return sim5300_receive(&sim5300_dev,
                               sockfd, 
                               (uint8_t*)buf,
                               sz);
    } else {
        /* Receiving data larger than RECEIVE_MAX_LEN bytes */
        uint16_t recv_sz = 0;
        printf("[DEBUG] RECV NEED %i BYTE\n", sz);

        do {
            if ((sz - recv_sz) >= RECEIVE_MAX_LEN) {
                res = sim5300_receive(&sim5300_dev,
                                      sockfd, 
                                      (uint8_t*)buf + recv_sz,
                                      RECEIVE_MAX_LEN);
                
                if (res < 0) {
                    printf("[DEBUG] RECV ERROR: %i\n", res);

                    return res;
                }

                recv_sz += res;
            } else {
                res = sim5300_receive(&sim5300_dev,
                                      sockfd, 
                                      (uint8_t*)buf + recv_sz,
                                      sz - recv_sz);
                
                if (res < 0) {
                    printf("[DEBUG] RECV ERROR: %i\n", res);

                    return res;
                }

                recv_sz += res;
                printf("[DEBUG] RECV %i BYTE\n", recv_sz);

                break;
            }
        } while (recv_sz < sz);

        return recv_sz;
    }
}

int unwired_send(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
    (void) ssl;
    (void) ctx;

    printf("[Socket] Send\n");

    return sim5300_send(&sim5300_dev,
                        sockfd, 
                        (uint8_t*)buf,
                        sz);
}

/*---------------------------------------------------------------------------*/
/* Power on for SIM5300 */
void sim5300_power_on(void) {
    puts("[SIM5300] Power on");

    /* MODEM_POWER_ENABLE to Hi */
    gpio_init(T2M_GSMPOWER, GPIO_OUT);
    gpio_set(T2M_GSMPOWER);

    /* 3G_PWR to Hi on 100 ms*/
    gpio_init(T2M_GSMENABLE, GPIO_OUT);
    gpio_set(T2M_GSMENABLE);

    /* 500ms sleep and clear */
    rtctimers_millis_sleep(SIM5300_TIME_ON);
    gpio_clear(T2M_GSMENABLE);
}

/*---------------------------------------------------------------------------*/
/* Power off for SIM5300 */
// void sim5300_power_off(void) {
//     /* Power off UART for modem */
//     uart_poweroff(at_dev.uart);

//     /* T2M_GSMPOWER to Low */
//     gpio_init(T2M_GSMPOWER, GPIO_OUT);
//     gpio_clear(T2M_GSMPOWER);

//     /* Modem is not initialized */
//     sim_status.modem = MODEM_NOT_INITIALIZED;
//     puts("[SIM5300] Power off");
// }

/*---------------------------------------------------------------------------*/
int main(void)
{
    int res;

    /* SIM5300 power on */
    sim5300_power_on();

    /* We wait while SIM5300 is initialized */
    rtctimers_millis_sleep(SIM5300_TIME_ON_UART);

    /* Init SIM5300 */
    res = sim5300_init(&sim5300_dev, SIM5300_UART, SIM5300_BAUDRATE, at_dev_buf, AT_DEV_RESP_SIZE, at_dev_resp, AT_DEV_RESP_SIZE);
    if (res != SIM5300_OK) {
        puts("sim5300_init ERROR");
    } 

    /* Set internet settings SIM5300 */
    res = sim5300_start_internet(&sim5300_dev, 30, NULL);
    if (res == SIM5300_OK) {
        puts("[SIM5300] Set internet settings OK");
    } else {
        puts("[SIM5300] Set internet settings ERROR");
    }

    
    //////// START SOCKET //////////
    // uint8_t data_for_send[256];
    // uint8_t data_for_recv[256] = {};
    // for (uint16_t i = 0; i < 256; i++) {
    //     data_for_send[i] = i;
    // }

    sockfd = sim5300_socket(&sim5300_dev);
    if (sockfd < SIM5300_OK) {
        printf("Error get socket: %i\n", sockfd);

        return -1;
    }

    res = sim5300_connect(&sim5300_dev, 
                           sockfd, 
                           "31.173.148.98", //"tg.manchenkoos.ru", // "31.173.148.98", // 176.15.5.90
                           "443", //"8080", // "443", // 666
                           "TCP");
    if (res < SIM5300_OK) {
        printf("Error start socket: %i\n", res);

        return -1;
    }

    // /* TEST START */
    // uint8_t data_for_recv[128];

    // while(true) {
    //     res = sim5300_receive(&sim5300_dev,
    //                            sockfd, 
    //                            data_for_recv,
    //                            sizeof(data_for_recv));

    //     if (res < 0) {
    //         printf("Error recv data: %i\n", res);

    //         return false;
    //     }

    //     if (res > 0) {
    //         printf("Recv data: %i\n%s\n", res, data_for_recv);

    //         break;
    //     }

    //     xtimer_usleep(1000000);
    // }

    // res = sim5300_close(&sim5300_dev, sockfd);
    // if (res < 0) {
    //     printf("Error close socket: %i\n", res);

    //     return false;
    // }

    // while (true);
    // /* TEST END*/

    // res = sim5300_send(&sim5300_dev,
    //                     sockfd, 
    //                     data_for_send,
    //                     sizeof(data_for_send));
    // if (res != sizeof(data_for_send)) {
    //     printf("Error send data: %i\n", res);

    //     return false;
    // }

    // while (true) {
    //     res = sim5300_receive(&sim5300_dev,
    //                            sockfd, 
    //                            data_for_recv,
    //                            sizeof(data_for_recv));

    //     if (res < 0) {
    //         printf("Error recv data: %i\n", res);

    //         // return false;
    //     }
    //     printf("Recv data: %i\n%s\n", res, data_for_recv);

    //     xtimer_usleep(1000000);
    // }

    // while (true);
    /////////////////////////////////////////////////////////////////////////////////////////////////

    puts("Start Tele2Med WolfSSL");

    // int                sockfd = 1;
    // int                sockfd;
    // struct sockaddr_in servAddr;
    // char               buff[22] = "Hello wolfSSL Server!\0";
    // char               server_ip[10] = "127.0.0.1\0";
    // size_t             len;

    /* declare wolfSSL objects */
    WOLFSSL_CTX* ctx;
    WOLFSSL*     ssl;

/*----------------------------------------------------------------------------*/
/* TLS Setup:
 * This section will need resolved on a per-device basis depending on the
 * available TCP/IP stack
 */
/*----------------------------------------------------------------------------*/

    // /* Create a socket that uses an internet IPv4 address,
    //  * Sets the socket to be stream based (TCP),
    //  * 0 means choose the default protocol. */
    // if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    //     fprintf(stderr, "ERROR: failed to create the socket\n");
    //     exit(-1);
    // }

    // /* Initialize the server address struct with zeros */
    // memset(&servAddr, 0, sizeof(servAddr));

    // /* Fill in the server address */
    // servAddr.sin_family = AF_INET;             /* using IPv4      */
    // servAddr.sin_port   = htons(DEFAULT_PORT); /* on DEFAULT_PORT */

    // /* Get the server IPv4 address from the command line call */
    // if (inet_pton(AF_INET, server_ip, &servAddr.sin_addr) != 1) {
    //     fprintf(stderr, "ERROR: invalid address\n");
    //     exit(-1);
    // }

    // /* Connect to the server */
    // if (connect(sockfd, (struct sockaddr*) &servAddr, sizeof(servAddr)) == -1) {
    //     fprintf(stderr, "ERROR: failed to connect\n");
    //     exit(-1);
    // }
/*----------------------------------------------------------------------------*/
/* END TCP SETUP, BEGIN TLS */
/*----------------------------------------------------------------------------*/

    wolfSSL_Debugging_ON();

    /* Initialize wolfSSL */
    wolfSSL_Init();

    /* Create and initialize WOLFSSL_CTX */
    if ((ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method())) == NULL) {
        fprintf(stderr, "ERROR: failed to create WOLFSSL_CTX\n");
        exit(-1);
    }

    wolfSSL_SetIORecv(ctx, unwired_recv);
    wolfSSL_SetIOSend(ctx, unwired_send);

    /* Load client certificates into WOLFSSL_CTX */
    if (wolfSSL_CTX_load_verify_buffer(ctx, t2m_cert, //ca_cert_der_2048
                                       sizeof(t2m_cert), //sizeof_ca_cert_der_2048
                                       SSL_FILETYPE_ASN1) != SSL_SUCCESS) {
        fprintf(stderr, "ERROR: failed to load ca buffer\n");
        exit(-1);
    }

    /* Create a WOLFSSL object */
    if ((ssl = wolfSSL_new(ctx)) == NULL) {
        fprintf(stderr, "ERROR: failed to create WOLFSSL object\n");
        exit(-1);
    }

    /* Attach wolfSSL to the socket */
    wolfSSL_set_fd(ssl, sockfd);

    /* Connect to wolfSSL on the server side */
    if (wolfSSL_connect(ssl) != SSL_SUCCESS) {
        fprintf(stderr, "ERROR: failed to connect to wolfSSL\n");
        exit(-1);
    }

    // /* Get a message for the server from stdin */
    // printf("Message for server: %s\n", buff);
    // len = strnlen(buff, sizeof(buff));

    // /* Send the message to the server */
    // if (wolfSSL_write(ssl, buff, len) != (int) len) {
    //     fprintf(stderr, "ERROR: failed to write\n");
    //     exit(-1);
    // }

    // /* Read the server data into our buff array */
    // memset(buff, 0, sizeof(buff));
    // if (wolfSSL_read(ssl, buff, sizeof(buff)-1) == -1) {
    //     fprintf(stderr, "ERROR: failed to read\n");
    //     exit(-1);
    // }

    // /* Print to stdout any data the server sends */
    // printf("Server sent a reply!\n");
    // printf("Server Response was:  %s\n", buff);

    // /* Cleanup and exit */
    // wolfSSL_free(ssl);      /* Free the wolfSSL object                  */
    // wolfSSL_CTX_free(ctx);  /* Free the wolfSSL context object          */
    // wolfSSL_Cleanup();      /* Cleanup the wolfSSL environment          */
    // close(sockfd);          /* Close the connection to the server       */

    // exit(0);               /* Return reporting a success               */
}
