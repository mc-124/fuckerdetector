#pragma once

#include "config.h"

#include "esp_timer.h"
#include "driver/gpio.h"

#define FFFF_U64 0xffff'ffff'ffff'ffffULL
#define FFFF_U32 0xffff'ffffU

#define __PreProcessor_MacroToString(__x) #__x
#define STRINGIFY(__x) __PreProcessor_MacroToString(__x)

#define get_millis() ((int64_t)(esp_timer_get_time()/1000))
#define get_seconds() ((int64_t)(esp_timer_get_time()/1000000))

#define println(msg) printf(msg "\r\n")
#define printfln(msg, ...) printf(msg "\r\n", __VA_ARGS__)

void misc_gpio_init(gpio_num_t pin, gpio_mode_t mode, gpio_pull_mode_t pull, gpio_int_type_t intr);
void misc_vbat_init();
float misc_vbat_read();
void misc_init_peri_uaer();
void misc_init_iic();
void misc_init_nvs();

void misc_delay_ms(uint32_t ms);
int sec_add(int a, int b);
int sec_sub(int a, int b);
