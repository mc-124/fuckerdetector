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
#define oled (&u8g2)
#define u8x8 ((u8x8_t*)&u8g2)

uint8_t ui_self_mac_address[6];
char ui_self_mac_string[5];
#define UI_MAX_DISPLAY_DEV_NUM 7
static struct ui_alarmdev ui_alarmdev_ringbuf[UI_MAX_DISPLAY_DEV_NUM];
static uint8_t ui_alarmdev_ringbuf_start = 0;
static struct ui_respdev ui_respdev_list[UI_MAX_DISPLAY_DEV_NUM];
static uint8_t ui_respdev_list_len = 0;

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

static button_handle_t btn_h = {0};

static ui_btn_callback_t ui_btn_callbacks[4] = {0};

#define ui_ringbuf_indexadd(__u8Index, __u8Add) ((__u8Index+__u8Add)%UI_MAX_DISPLAY_DEV_NUM)
#define ui_ringbuf_indexdec() ui_alarmdev_ringbuf_start = (ui_alarmdev_ringbuf_start==0 ? UI_MAX_DISPLAY_DEV_NUM-1 : ui_alarmdev_ringbuf_start - 1)

static struct ui_alarmdev *ui_get_alarmdev(uint8_t index){
    uint8_t real_index = ui_ringbuf_indexadd(ui_alarmdev_ringbuf_start, index);
    struct ui_alarmdev *this = &ui_alarmdev_ringbuf[real_index];
    return this->rssi ? this : NULL;
}

void ui_add_alarmdev(const struct ui_alarmdev *dev){
    assert(dev);
    ui_ringbuf_indexdec();
    memcpy(&ui_alarmdev_ringbuf[ui_alarmdev_ringbuf_start], dev, sizeof(struct ui_alarmdev));
}

static void ui_alarmdev_time_to_str(const struct ui_alarmdev *in, char out[6]){
    if (ADV_IS_CLIENT(in->alarm_type)){
        uint32_t diff = ((uint32_t)get_seconds()) - in->time.recv_time_s;
        uint8_t h = diff / 3600;
        if (h==0){
            uint8_t m = diff % 3600 / 60;
            if (m==0){
                snprintf(out, 6, "  >1m");
            } else {
                snprintf(out, 6, " %3hhum", m);
            }
        } else if (h<100){
            uint8_t m2 = (diff % 3600 / 60)/6;
            snprintf(out, 6, "%2hhu.%hhuh", h, m2);
        } else {
            snprintf(out, 6, " 99h+");
        }
    }
}

static void ui_alarmdev_to_line(const struct ui_alarmdev *in, char out[21]){
    snprintf(out, 8, "[%04hX] ", in->short_mac); // 0:7
    ui_alarmdev_time_to_str(in, out+7); // 7:12
    if (ADV_IS_CLIENT(in->alarm_type)){ // 12:21
        snprintf(out+12, 9, " %4hhddBm", in->rssi);
    } else {
        uint8_t rssi_chr = (-in->rssi)/10;
        if (rssi_chr>=16){
            rssi_chr = 15;
        }
        snprintf(out+12, 9, "%X %5.2fV", (char)rssi_chr, in->data.vbat);
    }
}

void ui_clear_resp_list(){
    ESP_LOGI(TAG, "clear resp list");
    ui_respdev_list_len = 0;
}

void ui_add_resp_dev(struct ui_respdev dev){
    ESP_LOGI(TAG, "add resp dev: [%04hX] %hhddBm", dev.short_mac, dev.rssi);
    if (ui_respdev_list_len<=UI_MAX_DISPLAY_DEV_NUM){
        ui_respdev_list[ui_respdev_list_len++] = dev;
    }
}

uint16_t ui_get_short_mac(uint8_t mac[6]){
    return esp_crc32_le(0xCC114514, mac, 6) % 0xffff;
}

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
    u8g2_ClearBuffer(oled);
}

static void ui_update(){
    u8g2_SendBuffer(oled);
}

void ui_init(){
    ESP_LOGI(TAG, "ui");
    ESP_ERROR_CHECK(u8g2_esp32_i2c_set_default_context(&ctx));
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        oled,
        U8G2_R0,
        u8x8_byte_esp32_hw_i2c,
        u8x8_gpio_and_delay_esp32_i2c
    );
    u8x8_SetI2CAddress(u8x8, PERI_SSD1306_ADDR<<1);
    u8g2_InitDisplay(oled);
    u8g2_SetPowerSave(u8x8, 0);
    esp_read_mac(ui_self_mac_address, ESP_MAC_BT);
    snprintf(ui_self_mac_string, 5, "%04hX", ui_get_short_mac(ui_self_mac_address));
    memset(ui_alarmdev_ringbuf, 0, sizeof(ui_alarmdev_ringbuf));
}

// FONT_0

#define ui_setfont(__font) u8g2_SetFont(oled, __font)
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
    u8g2_DrawStr(oled, ui_get_en_center_x(14), y, "FuckerDetector");
    y += 12;
    // version
    u8g2_DrawStr(oled, ui_get_en_center_x(14)+8, y, "V: " FIRMWARE_VERSION);
    y += 12;
    // address
    char buf[10];
    snprintf(buf, sizeof(buf), "A: [%s]", ui_self_mac_string);
    u8g2_DrawStr(oled, ui_get_en_center_x(14)+8, y, buf);

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
    char buf[21];
    uint8_t y = 0;
    snprintf(buf, sizeof(buf), "SCANNING [%s]%4.2fV", ui_self_mac_string, misc_vbat_read());
    u8g2_DrawStr(oled, 0, y, buf);
    for (uint8_t i=0; i<UI_MAX_DISPLAY_DEV_NUM; i++){
        const struct ui_alarmdev *this = ui_get_alarmdev(i);
        y += 8;
        if (this){
            ui_alarmdev_to_line(this, buf);
            u8g2_DrawStr(oled, 0, y, buf);
        }
    }
    ui_update();
}

static void ui_white_line_start(uint8_t y){
    u8g2_SetDrawColor(oled, 1);
    u8g2_DrawBox(oled, 0, y, 127, y+12);
    u8g2_SetDrawColor(oled, 0);
}
static void ui_white_line_start_font1(uint8_t y){
    u8g2_SetDrawColor(oled, 1);
    u8g2_DrawBox(oled, 0, y, 127, y+8);
    u8g2_SetDrawColor(oled, 0);
}
static void ui_white_line_end(){
    u8g2_SetDrawColor(oled, 1);
}

#define __X_DRAWLINE(__u8LineIndex)         \
    do {                                    \
        uint8_t y = __u8LineIndex*12+16;    \
        if (pagebtn_index==__u8LineIndex)   \
            ui_white_line_start(y);         \
        u8g2_DrawStr(oled, 0, y, desc->name_zh);    \
        if (pagebtn_index==__u8LineIndex)   \
            ui_white_line_end();            \
    } while (0)

void ui_showpage_settings(uint8_t button_index){
    ESP_LOGI(TAG, "showpage: settings[%hhu]", button_index);
    ui_clear();
    ui_setfont(FONT_0);
    uint8_t subpage_index = button_index / 4;
    uint8_t pagebtn_index = button_index % 4;
    const struct settings_config_desc *desc = NULL;
    if (button_index<3){
        desc = &settings_config_list[button_index];
    }
    switch (subpage_index){
    case 0:
        u8g2_DrawStr(oled, ui_get_zh_center_x(4), 0, "设置 1/4");
        __X_DRAWLINE(0);
        __X_DRAWLINE(1);
        __X_DRAWLINE(2);
        __X_DRAWLINE(3);
        break;
    case 1:
        u8g2_DrawStr(oled, ui_get_zh_center_x(4), 0, "设置 2/4");
        __X_DRAWLINE(0);
        __X_DRAWLINE(1);
        __X_DRAWLINE(2);
        __X_DRAWLINE(3);
        break;
    case 2:
        u8g2_DrawStr(oled, ui_get_zh_center_x(4), 0, "设置 3/4");
        __X_DRAWLINE(0);
        __X_DRAWLINE(1);
        __X_DRAWLINE(2);
        __X_DRAWLINE(3);
        break;
    default:
        u8g2_DrawStr(oled, ui_get_zh_center_x(4), 0, "设置 4/4");
        u8g2_DrawStr(oled, 0, 16, "版本: " FIRMWARE_VER_TYPE_SHORT "-" FIRMWARE_VERSION);
    
        ui_white_line_start(0);
        u8g2_DrawStr(oled, ui_get_zh_center_x(2), 52, "退出");
        ui_white_line_end();
        break;
    }
    ui_update();
}
#undef __X_DRAWLINE

void ui_showpage_settingsunit(uint8_t button_index, uint8_t settings_index){
    ESP_LOGI(TAG, "showpage: settingsunit[%hhu]", button_index);
    if (button_index>=SETTINGS_SET_NUM){
        ESP_LOGE(TAG, "invalid button_index");
        return;
    }
    ui_clear();
    ui_setfont(FONT_0);

    const struct settings_config_desc *desc = &settings_config_list[settings_index];

    u8g2_DrawStr(oled, ui_get_zh_center_x(2), 0, "编辑");
    u8g2_DrawStr(oled, 0, 12, settings_config_list[settings_index].name_zh);

    if (settings_field_is_bool(desc)){
        uint8_t cur_val;
        settings_human_rw(settings_index, false, &cur_val);
        ui_white_line_start(28);
        u8g2_DrawStr(oled, ui_get_zh_center_x(1), 28, cur_val ? "开" : "关");
        ui_white_line_end();
    } else {
        uint8_t cur_val;
        settings_human_rw(button_index, false, &cur_val);
        uint32_t ds_val = settings_get_field_display_value(desc, cur_val);
        u8g2_DrawStr(oled, 0, 28, ">");
        char buf[9];
        snprintf(buf, sizeof(buf), "%8u", ds_val);
        u8g2_DrawStr(oled, 127-(8*6), 28, buf);
        if (button_index==0) ui_white_line_start(40);
        u8g2_DrawStr(oled, ui_get_en_center_x(1), 40, "+");
        if (button_index==0) ui_white_line_end();
        if (button_index==1) ui_white_line_start(52);
        u8g2_DrawStr(oled, ui_get_en_center_x(1), 52, "-");
        if (button_index==1) ui_white_line_end();
    }

    ui_update();
}

void ui_showpage_transmitting(){
    ESP_LOGI(TAG, "showpage: transmitting");
    ui_clear();
    ui_setfont(FONT_1);

    ui_white_line_start_font1(0);
    u8g2_DrawStr(oled, ui_get_txt_center_y(15), 0, "TRANSMITTING...");
    ui_white_line_end();

    uint8_t y = 8;
    char buf[21];
    for (uint8_t i=0; i<ui_respdev_list_len; i++){
        struct ui_respdev dev = ui_respdev_list[i];
        if (dev.rssi){
            snprintf(buf, sizeof(buf), "Found [%04hX] %4hhddBm");
            u8g2_DrawStr(oled, 0, y, buf);
        }
        y += 8;
    }
    ui_update();
}

#endif // CONFIG_APP_CLIENT