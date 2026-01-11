#pragma once
#include <stdint.h>

typedef struct { // 8 bytes on the wire
    uint16_t top_value1;
    uint16_t top_value2;
    uint16_t bottom_value1;
    uint16_t bottom_value2;
} response_data_t;

typedef struct { // 74 bytes on the wire
    int8_t index;
    uint8_t flags; // bit 0 top 1 is gradient, bit 1 top 2 is gradient, bit 2 bottom 1 is gradient, bit 3 bottom 2 is gradient
    char top_label[12];
    uint8_t top_label_color[4];
    uint8_t top_color1[4];
    char top_suffix1[4];
    uint16_t top_max1;
    uint8_t top_color2[4];
    char top_suffix2[4];
    uint16_t top_max2;
    char bottom_label[12];
    uint8_t bottom_label_color[4];
    uint8_t bottom_color1[4];
    char bottom_suffix1[4];
    uint16_t bottom_max1;
    uint8_t bottom_color2[4];
    char bottom_suffix2[4];
    uint16_t bottom_max2;
} response_screen_t;

typedef union {
    response_data_t data;
    response_screen_t screen;
} response_t;

bool serial_init();
void serial_write(int8_t cmd,uint8_t screen_index);
int8_t serial_read_packet(response_t* out_resp);
