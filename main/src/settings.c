#include "settings.h"

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#if CONFIG_APP_SERVER

static char sleepinterval_key[sizeof(SETSRV_SLEEP_INTERVAL)+2];
bool sleepinterval_loaded = false;
struct SleepInterval sleepintervals[CONFIG_APP_SERVER_SLPITVL_MAX_NUM];
static struct SleepInterval old_sleepintervals[CONFIG_APP_SERVER_SLPITVL_MAX_NUM];
static nvs_handle_t ns_h = {0};

void set_nvs_sleepinterval_index(uint8_t index){
    snprintf(((char*)sleepinterval_key)+sizeof(sleepinterval_key)-3, 3, "%02hhX", index);
}

void reset_sleepintervals(){
    ESP_LOGI(TAG, "Reset sleep intervals");
    for (uint8_t i=0;i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM;i++){
        set_nvs_sleepinterval_index(i);
        ESP_LOGI(TAG, "Write: %s", sleepinterval_key);
        ESP_ERROR_CHECK(nvs_set_u64(ns_h, sleepinterval_key, FFFF_U64));
    }
}

void init_settings(){
    ESP_LOGI(TAG, "init");
    memset(sleepintervals, 0xff, sizeof(struct SleepInterval)*CONFIG_APP_SERVER_SLPITVL_MAX_NUM);
    memset(old_sleepintervals, 0xff, sizeof(struct SleepInterval)*CONFIG_APP_SERVER_SLPITVL_MAX_NUM);
    memcpy(sleepinterval_key, SETSRV_SLEEP_INTERVAL "\0\0", sizeof(SETSRV_SLEEP_INTERVAL)+2);
    esp_err_t ret = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &ns_h);
    if (ret&&ret!=ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "open nvs failed");
        ESP_ERROR_CHECK(ret);
    }
    uint8_t cur_len = 0;
    ret = nvs_get_u8(ns_h, SETSRV_SITVL_LENGTH, &cur_len);
    if (ret&&ret!=ESP_ERR_NVS_NOT_FOUND){
        ESP_LOGE(TAG, "get sitvls len failed");
        ESP_ERROR_CHECK(ret);
    }
    if (!cur_len){
        ESP_LOGI(TAG, "write sitvls len");
        ESP_ERROR_CHECK(nvs_set_u8(ns_h, SETSRV_SITVL_LENGTH, CONFIG_APP_SERVER_SLPITVL_MAX_NUM));
        ESP_LOGI(TAG, "commit sitvls len");
        ESP_ERROR_CHECK(nvs_commit(ns_h));
    } else if (cur_len!=CONFIG_APP_SERVER_SLPITVL_MAX_NUM) {
        ESP_LOGE(TAG, "check sitvls len error, overwrite it now");
        ESP_ERROR_CHECK(nvs_set_u8(ns_h, SETSRV_SITVL_LENGTH, CONFIG_APP_SERVER_SLPITVL_MAX_NUM));
        reset_sleepintervals();
        ESP_LOGI(TAG, "commit reset");
        ESP_ERROR_CHECK(nvs_commit(ns_h));
    }
}

void load_settings(){
    ESP_LOGI(TAG, "loading settings");
    for (uint8_t i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct SleepInterval *si = sleepintervals + i;
        uint64_t u64 = FFFF_U64;
        set_nvs_sleepinterval_index(i);
        esp_err_t ret = nvs_get_u64(ns_h, sleepinterval_key, &u64);
        if (ret==ESP_ERR_NVS_NOT_FOUND){
            ESP_LOGW(TAG, "not found: %s", sleepinterval_key);
            u64 = FFFF_U64;
        } else if (ret){
            ESP_LOGE(TAG, "load %s failed", sleepinterval_key);
            ESP_ERROR_CHECK(ret);
        }
        memcpy(si, &u64, 8);
    }
    memcpy(old_sleepintervals, sleepintervals, sizeof(sleepintervals));
}

void store_settings(){
    ESP_LOGI(TAG, "storing settings");
    for (uint8_t i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct SleepInterval *si = sleepintervals + i;
        if (memcmp(si, old_sleepintervals+i, 8)) continue;
        uint64_t u64;
        memcpy(&u64, si, 8);
        set_nvs_sleepinterval_index(i);
        ESP_LOGI(TAG, "storing %s", sleepinterval_key);
        ESP_ERROR_CHECK(nvs_set_u64(ns_h, sleepinterval_key, u64));
    }
    ESP_LOGI(TAG, "commiting");
    ESP_ERROR_CHECK(nvs_commit(ns_h));
    memcpy(old_sleepintervals, sleepintervals, sizeof(sleepintervals));
}

#elif CONFIG_APP_CLIENT

#endif