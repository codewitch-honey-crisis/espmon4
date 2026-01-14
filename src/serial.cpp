#if __has_include(<Arduino.h>)
#include <Arduino.h>
#include "serial_config.h"
#else
#include <driver/uart.h>
#include <driver/gpio.h>
#endif
#include <memory.h>
#include <esp_idf_version.h>
#include <esp_err.h>
#include <esp_log.h>
#include "serial.hpp"
#define SERIAL_QUEUE_SIZE 64
#define SERIAL_BUF_SIZE (2*SERIAL_QUEUE_SIZE)
const char* TAG = "Serial";

#ifdef TEST_NO_SERIAL
#include "esp_random.h"
static int8_t waiting = -1;
static int index_requested = -1;
#endif

int8_t serial_read_packet(response_t* out_resp) {
#ifndef TEST_NO_SERIAL
    uint8_t tmp;
    if(1==uart_read_bytes(UART_NUM_0,&tmp,1,0)) {
        if(tmp==0) {
            if(0!=uart_read_bytes(UART_NUM_0,out_resp,sizeof(response_screen_t),portMAX_DELAY)) {
                return tmp;
            }
        }
        if(tmp==1) {
            if(0!=uart_read_bytes(UART_NUM_0,out_resp,sizeof(response_data_t),portMAX_DELAY)) {
                return tmp;
            }
        } else {
            while(uart_read_bytes(UART_NUM_0,&tmp,1,0)>0) vTaskDelay(5);
        }
    }
    return -1;
#else
    if(waiting==0) {
        response_screen_t* pr = (response_screen_t*)out_resp;
        pr->index = 0;
        pr->flags = (1<<1)|(1<<3);
        strcpy(pr->top_label,"CPU");
        strcpy(pr->top_suffix1,"%");
        strcpy(pr->top_suffix2,"\xC2\xB0");
        memcpy(pr->top_label_color,(uint8_t[]){ (uint8_t)(0.67843137254902*0xFF), (uint8_t)(0.847058823529412*0xFF), (uint8_t)(0.901960784313726*0xFF),0xFF},4); // light blue
        memcpy(pr->top_color1,(uint8_t[]){0x00,0xFF,0x00,0xFF},4); // green
        memcpy(pr->top_color2,(uint8_t[]){0xFF,0x7F,0x00,0xFF},4); // orange
        pr->top_max1=100;
        pr->top_max2=90;
        strcpy(pr->bottom_label,"GPU");
        strcpy(pr->bottom_suffix1,"%");
        strcpy(pr->bottom_suffix2,"\xC2\xB0");
        memcpy(pr->bottom_label_color,(uint8_t[]){0xFF, (uint8_t)(0.627450980392157*0xFF), (uint8_t)(0.47843137254902*0xFF),0xFF},4); // light_salmon
        memcpy(pr->bottom_color1,(uint8_t[]){0xFF,0xFF,0xFF,0xFF},4); // white
        memcpy(pr->bottom_color2,(uint8_t[]){0xFF,0x00,0xFF,0xFF},4); // purple
        pr->bottom_max1=100;
        pr->bottom_max2=85;
    }
    if(waiting==1) {
        response_data_t* pr = (response_data_t*)out_resp;
        pr->top_value1 = 50;
        pr->top_value2 = 30;
        pr->bottom_value1 = 15;
        pr->bottom_value2 = 35;
        int variance = (esp_random()%30)-15;
        pr->top_value1 += variance;
        variance = (esp_random()%30)-15;
        pr->top_value2 += variance;
        variance = (esp_random()%30)-15;
        pr->bottom_value1 += variance;
        variance = (esp_random()%30)-15;
        pr->bottom_value2 += variance;
    }
    int8_t ret = waiting;
    waiting = -1;
    return ret;
#endif
}
void serial_write(int8_t cmd, uint8_t screen_index) {
#ifndef TEST_NO_SERIAL
    uint8_t ba[] = {(uint8_t)cmd,screen_index};
    if(0>uart_write_bytes(UART_NUM_0,ba,2)) {
        int i=1000;
        while(i-->0) {
            vTaskDelay(5);
            if(-1<uart_write_bytes(UART_NUM_0,ba,2)) {
                break;
            }
        }
        if(i==0) { return; }
    }
    
    uart_wait_tx_done(UART_NUM_0,portMAX_DELAY);
#else
    waiting = cmd;
    index_requested = screen_index;
#endif
}
bool serial_init() {
#ifndef TEST_NO_SERIAL
    esp_log_level_set(TAG, ESP_LOG_INFO);
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config;
    memset(&uart_config,0,sizeof(uart_config));
    uart_config.baud_rate = 115200;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    //Install UART driver, and get the queue.
    if(ESP_OK!=uart_driver_install(UART_NUM_0, SERIAL_BUF_SIZE * 2, 0, 20, nullptr, 0)) {
        ESP_LOGE(TAG,"Unable to install uart driver");
        goto error;
    }
    uart_param_config(UART_NUM_0, &uart_config);
    //Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    //Create a task to handler UART event from ISR
#else
    waiting = 0;
#endif
    return true;
#ifndef TEST_NO_SERIAL
error:
    return false;
#endif
}