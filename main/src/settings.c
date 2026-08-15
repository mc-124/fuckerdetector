// 这个文件里可能会有隐藏 BUG，因为它们有较多都是我在深夜里神志不清时写的
// 力竭了，懒得修

#include "settings.h"

#include "misc.h"
#include "timelib.h"
#include "repl.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include <assert.h>

static const char *TAG = "settings";

static nvs_handle_t ns_h = {0};

#if CONFIG_APP_SERVER

static char settings_nvs_key[sizeof(SETTINGS_SERVER_SLEEP_INTERVAL)+2];
static struct timelib_slpitvl settings_old_slpitvl_array[CONFIG_APP_SERVER_SLPITVL_MAX_NUM];

static void set_nvs_sleepinterval_index(uint8_t index){
    snprintf(((char*)settings_nvs_key)+sizeof(settings_nvs_key)-3, 3, "%02hhX", index);
}

void settings_reset_all_timelib_slpitvl_array(){
    ESP_LOGI(TAG, "Reset sleep intervals");
    for (int i=0;i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM;i++){
        set_nvs_sleepinterval_index(i);
        ESP_LOGI(TAG, "Write: %s", settings_nvs_key);
        ESP_ERROR_CHECK(nvs_set_u64(ns_h, settings_nvs_key, FFFF_U64));
    }
    ESP_ERROR_CHECK(nvs_commit(ns_h));
}

void settings_init(){
    ESP_LOGI(TAG, "init");
    misc_init_nvs();
    memset(timelib_slpitvl_array, 0xff, sizeof(struct timelib_slpitvl)*CONFIG_APP_SERVER_SLPITVL_MAX_NUM);
    memset(settings_old_slpitvl_array, 0xff, sizeof(struct timelib_slpitvl)*CONFIG_APP_SERVER_SLPITVL_MAX_NUM);
    memcpy(settings_nvs_key, SETTINGS_SERVER_SLEEP_INTERVAL "\0\0", sizeof(SETTINGS_SERVER_SLEEP_INTERVAL)+2);
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &ns_h);
    if (ret&&ret!=ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "open nvs failed");
        ESP_ERROR_CHECK(ret);
    }
    uint8_t cur_len = 0;
    ret = nvs_get_u8(ns_h, SETTINGS_SERVER_SITVL_LENGTH, &cur_len);
    if (ret&&ret!=ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "get sitvls len failed");
        ESP_ERROR_CHECK(ret);
    }
    if (!cur_len){
        ESP_LOGI(TAG, "write sitvls len");
        ESP_ERROR_CHECK(nvs_set_u8(ns_h, SETTINGS_SERVER_SITVL_LENGTH, CONFIG_APP_SERVER_SLPITVL_MAX_NUM));
        ESP_LOGI(TAG, "commit sitvls len");
        ESP_ERROR_CHECK(nvs_commit(ns_h));
    } else if (cur_len!=CONFIG_APP_SERVER_SLPITVL_MAX_NUM) {
        ESP_LOGE(TAG, "check sitvls len error, overwrite it now");
        ESP_ERROR_CHECK(nvs_set_u8(ns_h, SETTINGS_SERVER_SITVL_LENGTH, CONFIG_APP_SERVER_SLPITVL_MAX_NUM));
        settings_reset_all_timelib_slpitvl_array();
    }
}

void settings_load(){
    ESP_LOGI(TAG, "loading settings");
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *si = &timelib_slpitvl_array[i];
        uint64_t u64 = FFFF_U64;
        set_nvs_sleepinterval_index(i);
        esp_err_t ret = nvs_get_u64(ns_h, settings_nvs_key, &u64);
        if (ret==ESP_ERR_NVS_NOT_FOUND){
            ESP_LOGW(TAG, "not found: %s", settings_nvs_key);
            u64 = FFFF_U64;
        } else if (ret){
            ESP_LOGE(TAG, "load %s failed", settings_nvs_key);
            ESP_ERROR_CHECK(ret);
        }
        memcpy(si, &u64, 8);
    }
    timelib_check_slpitvl();
    memcpy(settings_old_slpitvl_array, timelib_slpitvl_array, sizeof(timelib_slpitvl_array));
}

void settings_store(){
    ESP_LOGI(TAG, "storing settings");
    timelib_check_slpitvl();
    bool changed = false;
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *si = &timelib_slpitvl_array[i];
        if (!memcmp(si, &settings_old_slpitvl_array[i], 8)) 
            continue;
        uint64_t u64;
        memcpy(&u64, si, 8);
        set_nvs_sleepinterval_index(i);
        ESP_LOGI(TAG, "storing %s", settings_nvs_key);
        ESP_ERROR_CHECK(nvs_set_u64(ns_h, settings_nvs_key, u64));
        changed = true;
    }
    if (changed){
        ESP_LOGI(TAG, "commiting");
        ESP_ERROR_CHECK(nvs_commit(ns_h));
        memcpy(settings_old_slpitvl_array, timelib_slpitvl_array, sizeof(timelib_slpitvl_array));
    } else {
        ESP_LOGI(TAG, "No change");
    }
}

// addsleep <str start HH:MM:SS/HH:MM> <str end HH:MM:SS/HH:MM>
static void cmd_addsleep(uint8_t argc, const char **args){
    if (argc==0){
        println("Usage: addsleep <start_time: HH:MM|HH:MM:SS> <end_time: HH:MM|HH:MM:SS>");
        return;
    } else if (argc!=2){
        println("error: invalid arguments");
        return;
    }

    // 这里不是 bug 是故意的
    uint8_t start_hour, start_minute, start_second = 0;
    uint8_t end_hour, end_minute, end_second = 0;

    uint32_t endi = 0;
    if (sscanf(args[0], "%hhu:%hhu:%hhu%n", &start_hour, &start_minute, &start_second, &endi)<2
        ||start_hour>=24||start_minute>=60||start_second>=60
        ||args[0][endi]!=0
    ){
        println("error: invalid start time");
        return;
    }

    if (sscanf(args[1], "%hhu:%hhu:%hhu%n", &end_hour, &end_minute, &end_second, &endi)<2
        ||end_hour>=24||end_minute>=60||end_second>=60
        ||args[0][endi]!=0
    ){
        println("error: invalid end time");
        return;
    }

    int start_time = start_hour*3600 + start_minute*60 + start_second;
    int end_time = end_hour*3600 + end_minute*60 + end_second;
    
    struct timelib_slpitvl *freeslot = NULL;
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *this = &timelib_slpitvl_array[i];
        if (this->start==FFFF_U32||this->end==FFFF_U32){
            freeslot = this;
            break;
        }
    }
    if (!freeslot){
        println("error: array is full");
        return;
    }

    freeslot->start = start_time;
    freeslot->end = end_time;

    printf("success: ");
    timelib_print_slpitvl(freeslot);
}

// delsleep <int slot>
static void cmd_delsleep(uint8_t argc, const char **args){
    if (argc==0){
        println("Usage: delsleep <index>");
        return;
    } else if (argc!=1){
        println("error: invalid arguments");
        return;
    }
    uint32_t index;
    if (!misc_str_to_uint(&index, args[0])){
        printfln("error: invalid number: %s", args[0]);
        return;
    }
    if (index<CONFIG_APP_SERVER_SLPITVL_MAX_NUM){
        memset(timelib_slpitvl_array+index, 0xff, sizeof(struct timelib_slpitvl));
        println("success");
    } else {
        println("error: index out of range");
    }
}

static void cmd_lssleep(uint8_t argc, const char **args){
    if (argc){
        println("error: invalid arguments");
        return;
    }
    timelib_print_all_slpitvl();
}

static void cmd_savesleep(uint8_t argc, const char **args){
    if (argc){
        println("error: invalid arguments");
        return;
    }
    settings_store();
    println("success");
}

void settings_addcmds(){
    repl_addcmd("addsleep", "Add sleep interval", cmd_addsleep); // #5
    repl_addcmd("delsleep", "Delete a sleep interval", cmd_delsleep); // #6
    repl_addcmd("lssleep", "Print all sleep interval", cmd_lssleep); // #7
    repl_addcmd("savesleep", "Save all sleep interval", cmd_savesleep); // #8
}

#elif CONFIG_APP_CLIENT

struct settings settings;

#define MS_TO_VIB(ms) ((uint8_t)(ms/20-1))

const static struct settings settings_default = {
    .enable_recv_server_alarm = true,
    .vib_normal_num = 3-1,
    .vib_normal_dur = MS_TO_VIB(1000),
    .vib_normal_itv = MS_TO_VIB(500),
    .enable_recv_client_alarm = true,
    .vib_normal_pwr = 20-1,
    .enable_recv_client_loud_alarm = true,
    .vib_loud_num = 2,
    .vib_loud_dur = MS_TO_VIB(1500),
    .vib_loud_itv = MS_TO_VIB(800),
    .server_alarm_as_power = true,
    .vib_loud_pwr = 70-1,
};
static_assert(sizeof(struct settings)==8);

#define TEXT_0 
#define TEXT_1 
#define TEXT_2 
#define TEXT_3 
#define TEXT_4 
#define TEXT_5 
#define TEXT_6 
#define TEXT_7 
#define TEXT_8 
#define TEXT_9 
#define TEXT_A 
#define TEXT_B 

const struct settings_config_desc settings_config_list[SETTINGS_SET_NUM] = {
    {"Receive server alarm", "接收探测器警告", 0,1,0,1},
    {"Receive client alarm", "接收客户端警告", 0,1,0,1},
    {"Receive client loud alarm", "接收客户端强力", 0,1,0,1},
    {"Server alarm as loud alarm", "探测器使用强力", 0,1,0,1},
    {"Normal alarm vibration number", "警告震动次数", 1,1,0,127},
    {"Normal alarm vibration duration (ms)", "警告震动时长", 1,20,0,127},
    {"Normal alarm vibration interval (ms)", "警告震动间隔", 1,20,0,127},
    {"Normal alarm vibration power", "警告震动功率", 0,1,1,100},
    {"Violance alarm vibration number", "强力震动次数", 1,1,0,127},
    {"Violance alarm vibration duration (ms)", "强力震动时长", 1,20,0,127},
    {"Violance alarm vibration interval (ms)", "强力震动间隔", 1,20,0,127},
    {"Violance alarm vibration power", "强力震动功率", 0,1,1,100}    
};

void settings_init(){
    ESP_LOGI(TAG, "init");
    misc_init_nvs();
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &ns_h);
    if (ret&&ret!=ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "open nvs failed");
        ESP_ERROR_CHECK(ret);
    }
    uint8_t ver;
    ret = nvs_get_u8(ns_h, SETTINGS_CLIENT_SETVERSION, &ver);
    if (ret==ESP_ERR_NVS_NOT_FOUND||ver!=SETTINGS_VERSION){
        ESP_LOGE(TAG, "settings version not found or not equal");
        ESP_ERROR_CHECK(nvs_set_u8(ns_h, SETTINGS_CLIENT_SETVERSION, SETTINGS_VERSION));
        memcpy(&settings, &settings_default, 8);
        settings_store();
    } else if (ret){
        ESP_LOGE(TAG, "read settings version failed");
        ESP_ERROR_CHECK(ret);
    }
}

void settings_load(){
    ESP_LOGI(TAG, "loading settings");
    uint64_t tmp;
    esp_err_t ret = nvs_get_u64(ns_h, SETTINGS_CLIENT_CONFIG, &tmp);
    if (ret==ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGW(TAG, "settings not found");
        memcpy(&settings, &settings_default, 8);
    } else if (ret){
        ESP_ERROR_CHECK(ret);
    } else {
        memcpy(&settings, &tmp, 8);
    }
}

void settings_store(){
    ESP_LOGI(TAG, "storing settings");
    uint64_t tmp;
    esp_err_t ret = nvs_get_u64(ns_h, SETTINGS_CLIENT_CONFIG, &tmp);
    if (ret==ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGW(TAG, "settings not found");
    } else if (ret){
        ESP_ERROR_CHECK(ret);
    } else if (!memcmp(&tmp, &settings, 8)){
        ESP_LOGI(TAG, "no changed");
        return;
    }
    memcpy(&tmp, &settings, 8);
    ESP_ERROR_CHECK(nvs_set_u64(ns_h, SETTINGS_CLIENT_CONFIG, tmp));
    ESP_ERROR_CHECK(nvs_commit(ns_h));
}

bool settings_field_is_bool(const struct settings_config_desc *desc){
    assert(desc);
    return desc->display_offset==0&&desc->display_mul==1&&desc->min_value==0&&desc->max_value==1;
}

#define __X_CASE(__index, __fieldname, __min, __max) \
    case __index:       \
    do {                \
        if (is_w){     \
            uint8_t v = *value;                 \
            if (v<__min){                       \
                settings.__fieldname = __min;   \
            } else if (v>__max) {               \
                settings.__fieldname = __max;   \
            } else {                            \
                settings.__fieldname = v;\
            }           \
        } else {        \
            *value = settings.__fieldname;    \
        }               \
    } while(0);         \
    return true

bool settings_human_rw(uint8_t index, bool is_w, uint8_t *value){
    assert(value);
    switch (index){
DISABLE_TYPELIMIT_START
        __X_CASE(0, enable_recv_server_alarm, 0, 1); 
        __X_CASE(1, enable_recv_client_alarm, 0, 1);
        __X_CASE(2, enable_recv_client_loud_alarm, 0, 1);
        __X_CASE(3, server_alarm_as_power, 0, 1);
        __X_CASE(4, vib_normal_num, 0, 127);
        __X_CASE(5, vib_normal_dur, 0, 127);
        __X_CASE(6, vib_normal_itv, 0, 127);
        __X_CASE(7, vib_normal_pwr, 1, 100);
        __X_CASE(8, vib_loud_num, 0, 127);
        __X_CASE(9, vib_loud_dur, 0, 127);
        __X_CASE(10, vib_loud_itv, 0, 127);
        __X_CASE(11, vib_loud_pwr, 1, 100);
    default:
        ESP_LOGE(TAG, "invalid index: %d", index);
        return false;
    };
DISABLE_TYPELIMIT_END
}
#undef __X_CASE

uint32_t settings_get_field_display_value(const struct settings_config_desc *desc, uint8_t value){
    assert(desc);
    return (value+desc->display_offset)*desc->display_mul;
}

void settings_print_field(const struct settings_config_desc *desc, uint8_t value){
    assert(desc);
    if (settings_field_is_bool(desc)){
        printfln("[%s]: %s", desc->name, value?"true":"false");
    } else {
        printfln("[%s]: %u", desc->name, U32 settings_get_field_display_value(desc, value));
    }
}

uint8_t settings_displayvalue_to_rawvalue(const struct settings_config_desc *desc, uint32_t display_value){
    assert(desc);
    return (display_value/desc->display_mul)-desc->display_offset;
}

static void cmd_settings(uint8_t argc, const char **args){
    if (argc==0){
        printf("Usage:"
            "- settings get <index> | Read settings value\r\n"
            "- settings set <index> <value> | Write settings value\r\n"
            "- settings list | Print all settings key\r\n"
            "- settings save | Save settings\r\n"
        );
        return;
    }
    const char *mode = args[0];
    if (argc==2&&!strcmp(mode, "get")){
        const char *index_str = args[1];
        uint32_t index;
        if (!misc_str_to_uint(&index, index_str)||index>255){
            println("error: index not a number");
            return;
        }
        uint8_t value;
        if (!settings_human_rw((uint8_t)index, 0, &value)){
            println("error: invalid index");
            return;
        }
        const struct settings_config_desc *desc = &settings_config_list[index];
        settings_print_field(desc, value);
    } else if (argc==3&&!strcmp(mode, "set")){
        const char *index_str = args[1];
        const char *value_str = args[2];
        uint32_t index;
        if (!misc_str_to_uint(&index, index_str)){
            println("error: index not a number");
            return;
        }
        if (!settings_field_is_valid(index)){
            println("error: invalid index");
            return;
        }
        const struct settings_config_desc *desc = &settings_config_list[index];
        if (settings_field_is_bool(desc)){
            uint8_t value;
            if (!strcmp(value_str, "true")){
                value = true;
                settings_human_rw(index, true, &value);
            } else if (!strcmp(value_str, "false")){
                value = false;
                settings_human_rw(index, true, &value);
            } else {
                println("error: value not a boolean");
            }
        } else {
            uint32_t display_value;
            if (!misc_str_to_uint(&display_value, value_str)){
                println("error: value not a number");
                return;
            }
            uint8_t raw_value = settings_displayvalue_to_rawvalue(desc, display_value);
            settings_human_rw(index, true, &raw_value);
        }
    } else if (argc==1&&!strcmp(mode, "list")){
        for (int i=0; i<SETTINGS_SET_NUM; i++){
            const struct settings_config_desc *desc = &settings_config_list[i];
            uint8_t value;
            settings_human_rw(i, false, &value);
            settings_print_field(desc, value);
        }
    } else if (argc==1&&!strcmp(mode, "save")){
        settings_store();
        println("success");
    } else {
        println("error: invalid arguments");
    }   
}

void settings_addcmds(){
    repl_addcmd("settings", "Query or edit device settings", cmd_settings);
}

#endif