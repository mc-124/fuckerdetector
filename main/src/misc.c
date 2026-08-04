#include "misc.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "nvs_flash.h"
#include "esp_sleep.h"

#if CONFIG_APP_CLIENT
#include "driver/ledc.h"
#endif

static const char* TAG = "misc";

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

bool misc_str_to_int(int *out, const char *str){
    assert(str);
    uint8_t len = strlen(str);
    char *p_end;
    int result = strtol(str, &p_end, 10);
    if (((uint32_t)p_end)==((uint32_t)str)+len){
        *out = result;
        return true;
    } else {
        return false;
    }
}

bool misc_str_to_uint(uint32_t *out, const char *str){
    assert(str);
    uint8_t len = strlen(str);
    char *p_end;
    int result = strtoul(str, &p_end, 10);
    if (((uint32_t)p_end)==((uint32_t)str)+len){
        *out = result;
        return true;
    } else {
        return false;
    }
}

#if CONFIG_APP_CLIENT

void misc_vibration_init(){
    ESP_LOGI(TAG, "init ledc");
    ledc_timer_config_t cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = CONFIG_APP_CLIENT_VIBRATION_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&cfg));
    ledc_channel_config_t chan = {
        .gpio_num = PIN_OUTPUT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan));
}

void misc_vibration_set(uint8_t pwr){
    ESP_LOGI(TAG, "vibration: pwr=%hhu", pwr);
    if (!pwr){
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        return;
    } else if (pwr>100){
        pwr = 100;
    }
    float fpwr = ((float)pwr) * 0.01;
    uint32_t duty = fpwr*(1<<13);
    ledc_timer_resume(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

#endif