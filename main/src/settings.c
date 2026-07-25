#include "settings.h"

#include "misc.h"
#include "timelib.h"
#include "repl.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#if CONFIG_APP_SERVER

static char settings_nvs_key[sizeof(SETTINGS_SERVER_SLEEP_INTERVAL)+2];
static struct timelib_slpitvl settings_old_slpitvl_array[CONFIG_APP_SERVER_SLPITVL_MAX_NUM];
static nvs_handle_t ns_h = {0};

static void set_nvs_sleepinterval_index(uint8_t index){
    snprintf(((char*)settings_nvs_key)+sizeof(settings_nvs_key)-3, 3, "%02hhX", index);
}

void settings_reset_all_timelib_slpitvl_array(){
    ESP_LOGI(TAG, "Reset sleep intervals");
    for (uint8_t i=0;i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM;i++){
        set_nvs_sleepinterval_index(i);
        ESP_LOGI(TAG, "Write: %s", settings_nvs_key);
        ESP_ERROR_CHECK(nvs_set_u64(ns_h, settings_nvs_key, FFFF_U64));
    }
    ESP_ERROR_CHECK(nvs_commit(ns_h));
}

void settings_init(){
    ESP_LOGI(TAG, "init");
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
    timelib_check_slpitvl();
    for (uint8_t i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *si = timelib_slpitvl_array + i;
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
    memcpy(settings_old_slpitvl_array, timelib_slpitvl_array, sizeof(timelib_slpitvl_array));
}

void settings_store(){
    ESP_LOGI(TAG, "storing settings");
    timelib_check_slpitvl();
    bool changed = false;
    for (uint8_t i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *si = timelib_slpitvl_array + i;
        if (!memcmp(si, settings_old_slpitvl_array+i, 8)) 
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

    if (sscanf(args[0], "%hhu:%hhu:%hhu", &start_hour, &start_minute, &start_second)<2
        ||start_hour>=24||start_minute>=60||start_second>=60
    ){
        println("error: invalid start time");
        return;
    }

    if (sscanf(args[1], "%hhu:%hhu:%hhu", &end_hour, &end_minute, &end_second)<2
        ||end_hour>=24||end_minute>=60||end_second>=60
    ){
        println("error: invalid end time");
        return;
    }

    int start_time = start_hour*3600 + start_minute*60 + start_second;
    int end_time = end_hour*3600 + end_minute*60 + end_second;
    
    struct timelib_slpitvl *freeslot = NULL;
    for (uint8_t i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *this = timelib_slpitvl_array+i;
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
    int index;
    if (!misc_str_to_int(&index, args[0])){
        printfln("error: invalid number: %s", args[0]);
    }
    if (0<=index&&index<CONFIG_APP_SERVER_SLPITVL_MAX_NUM){
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

#endif