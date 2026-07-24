#include "settings.h"

#include "misc.h"
#include "timelib.h"

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

#elif CONFIG_APP_CLIENT

#endif