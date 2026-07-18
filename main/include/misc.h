#pragma once

#include "config.h"

#include "esp_timer.h"
#include "driver/gpio.h"

#define __PreProcessor_MacroToString(__x) #__x
#define STRINGIFY(__x) __PreProcessor_MacroToString(__x)

#define get_millis() ((int64_t)(esp_timer_get_time()/1000))
#define get_seconds() ((int64_t)(esp_timer_get_time()/1000000))

#define println(msg) printf(msg "\r\n")
#define printfln(msg, ...) printf(msg "\r\n", __VA_ARGS__)

#define led(en) gpio_set_level(PIN_LED, !en)

void init_gpio(gpio_num_t pin, gpio_mode_t mode, gpio_pull_mode_t pull, gpio_int_type_t intr);
void init_vbat_adc();
float read_vbat();
void init_peri_uart();
void init_i2c();
void init_nvs();
void delay_ms(uint32_t ms);