#include "misc.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_sleep.h"

static const char* TAG = "Misc";

static adc_oneshot_unit_handle_t misc_adc_h;
static adc_cali_handle_t misc_cali_h;
static i2c_master_bus_handle_t misc_iic_h;

void misc_gpio_init(gpio_num_t pin, gpio_mode_t mode, gpio_pull_mode_t pull, gpio_int_type_t intr){
    gpio_config_t config = {
        .pin_bit_mask = 1ULL<<pin,
        .mode = mode,
        .pull_up_en = (pull==GPIO_PULLUP_ONLY||pull==GPIO_PULLUP_PULLDOWN),
        .pull_down_en = (pull==GPIO_PULLDOWN_ONLY||pull==GPIO_PULLUP_PULLDOWN),
        .intr_type = intr
    };
    ESP_ERROR_CHECK(gpio_config(&config));
}

void misc_vbat_init(){
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &misc_adc_h));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(misc_adc_h, CHAN_VBAT, &chan_cfg));
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &misc_cali_h));
}

float misc_vbat_read(){
    int raw,mv = 0;
    adc_oneshot_read(misc_adc_h, CHAN_VBAT, &raw);
    adc_cali_raw_to_voltage(misc_cali_h, raw, &mv);
    mv *= 2;
    return mv / 1000.0;
}

void misc_init_peri_uaer(){
    uart_config_t cfg = {
        .baud_rate = PERI_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, PIN_PERI_TX, PIN_PERI_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, PERI_UART_BUFSIZE, 0, 0, NULL, 0));
}

void misc_init_iic(){
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_IIC_SDA,
        .scl_io_num = PIN_IIC_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &misc_iic_h));
}

void misc_init_nvs(){
    esp_err_t ret = nvs_flash_init();
    if (ret==ESP_ERR_NVS_NEW_VERSION_FOUND||ret==ESP_ERR_NVS_NO_FREE_PAGES){
        ESP_LOGW(TAG, "init nvs failed, earse it now. %d", ret);
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void misc_delay_ms(uint32_t ms){
    vTaskDelay(pdMS_TO_TICKS(ms));
}

int sec_add(int a, int b){
    int r = a + b;
    if (r>=86400){
        return r - 86400;
    }
    return r;
}

int sec_sub(int a, int b){
    int r = a - b;
    if (r<0){
        return r + 86400;
    }
    return r;
}