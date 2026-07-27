#include "ui.h"

#if CONFIG_APP_CLIENT

#include "misc.h"
#include "settings.h"

#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_crc.h"
#include "esp_mac.h"
#include "esp_log.h"

#include "iot_button.h"
#include "button_gpio.h"
#include "esp32_hw_i2c.h"
#include "u8g2.h"
#include "u8x8.h"

#define FONT_0 u8g2_font_wqy12_t_chinese1
#define FONT_1 u8g2_font_5x7_tf

static const char *TAG = "ui";

static uint8_t self_mac_address[6];
static uint8_t self_mac_string[6];

static u8g2_esp32_i2c_ctx_t ctx = {
    .cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_pin = PIN_IIC_SDA,
        .scl_pin = PIN_IIC_SCL,
        .clk_hz = 400000,
        .dev_addr_7bit = PERI_SSD1306_ADDR,
    }
};

static u8g2_t u8g2 = {};

static const button_config_t btn_cfg = {
    .short_press_time = 10,
    .long_press_time = 640,
};

static const button_gpio_config_t btn_gpio_cfg = {
    .gpio_num = PIN_FUNCT,
    .active_level = 0,
    .disable_pull = true,
    .enable_power_save = false
};

static void ui_get_mac_str(uint8_t mac[6]){
    uint32_t u16addr = esp_crc32_le(0xCC114514, mac, 6) % 0xffff;
    snprintf((char*)mac, 5, "%04X", u16addr);
}

static button_handle_t btn_h = {0};

static ui_btn_callback_t ui_btn_callbacks[4] = {0};

static void ui_btn_callback(void *a, void *usr_data){
    uint32_t i = (uint32_t)usr_data;
    ui_btn_callback_t cb = ui_btn_callbacks[i];
    if (cb) cb();
}

void ui_init_buttons(
    ui_btn_callback_t single_click,
    ui_btn_callback_t double_click,
    ui_btn_callback_t lpress_start,
    ui_btn_callback_t lpress_stop
){
    ESP_LOGI(TAG, "init buttons");
    ui_btn_callbacks[0] = single_click;
    ui_btn_callbacks[1] = double_click;
    ui_btn_callbacks[2] = lpress_start;
    ui_btn_callbacks[3] = lpress_stop;
    ESP_ERROR_CHECK(iot_button_new_gpio_device(&btn_cfg, NULL, &btn_h));
    ESP_ERROR_CHECK(iot_button_register_cb(btn_h, BUTTON_SINGLE_CLICK, NULL, ui_btn_callback, (void*)0));
    ESP_ERROR_CHECK(iot_button_register_cb(btn_h, BUTTON_DOUBLE_CLICK, NULL, ui_btn_callback, (void*)1));
    ESP_ERROR_CHECK(iot_button_register_cb(btn_h, BUTTON_LONG_PRESS_START, NULL, ui_btn_callback, (void*)2));
    ESP_ERROR_CHECK(iot_button_register_cb(btn_h, BUTTON_LONG_PRESS_UP, NULL, ui_btn_callback, (void*)3));
    
}

static void ui_oled_invert(bool en){
    uint8_t cmd = en?0x06:0x07;
    //ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, PERI_SSD1306_ADDR, &cmd, 1, pdMS_TO_TICKS(100)));
    ESP_ERROR_CHECK(i2c_master_transmit(ctx.dev_handle, &cmd, 1, 100));
}

static void ui_clear(){
    u8g2_ClearBuffer(&u8g2);
}

static void ui_update(){
    u8g2_SendBuffer(&u8g2);
}

void ui_init(){
    ESP_LOGI(TAG, "ui");
    ESP_ERROR_CHECK(u8g2_esp32_i2c_set_default_context(&ctx));
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_esp32_hw_i2c, u8x8_gpio_and_delay_esp32_i2c);
    u8x8_SetI2CAddress(&u8g2.u8x8, PERI_SSD1306_ADDR<<1);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2.u8x8, 0);
    esp_read_mac(self_mac_address, ESP_MAC_BT);
    memcpy(self_mac_address, self_mac_string, 6);
    ui_get_mac_str(self_mac_string);
}

// FONT_0

#define ui_setfont(__font) u8g2_SetFont(&u8g2, __font)
#define ui_get_zh_center_x(__u8Length) ((uint8_t)(64-((__u8Length*12)/2)))
#define ui_get_en_center_x(__u8Length) ((uint8_t)(64-((__u8Length*6)/2)))
#define ui_get_txt_center_y(__u8Length) ((uint8_t)(32-((__u8Length*12)/2)))

void ui_showpage_launch(){
    ESP_LOGI(TAG, "showpage: launch");
    ui_clear();
    ui_setfont(FONT_0);
    ui_oled_invert(true);
    uint8_t y = ui_get_txt_center_y(3);
    
    // title
    u8g2_DrawStr(&u8g2, ui_get_en_center_x(14), y, "FuckerDetector");
    y += 12;
    // version
    u8g2_DrawStr(&u8g2, ui_get_en_center_x(14)+8, y, "V: " FIRMWARE_VERSION);
    y += 12;
    // address
    char buf[10];
    snprintf(buf, sizeof(buf), "A: [%s]", self_mac_string);
    u8g2_DrawStr(&u8g2, ui_get_en_center_x(14)+8, y, buf);

    ui_update();
    misc_vibration_set(50);
    misc_delay_ms(1000);
    misc_vibration_set(0);
    ui_oled_invert(false);
}

void ui_showpage_main(){
    ESP_LOGI(TAG, "showpage: main");
    ui_clear();
    ui_setfont(FONT_1);
    char buf_a[7];
    snprintf(buf_a, sizeof(buf_a), "[%s]", self_mac_string);
    char buf_v[6];
    snprintf(buf_v, sizeof(buf_v), "%04.2fV", misc_vbat_read());
    

}

void ui_showpage_settings(uint8_t subpage_index){
    ESP_LOGI(TAG, "showpage: settings[%hhu]", subpage_index);
    ui_clear();
}

#endif // CONFIG_APP_CLIENT