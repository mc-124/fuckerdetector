#include "blelib.h"

#if FIRMWARE_BLE_ADV || FIRMWARE_BLE_SCAN

#include "misc.h"

#include "esp_log.h"
#include "esp_bt.h"
#include "freertos/queue.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"

// generic tag
#define BLELIB_APPEARANCE 0x0200

#define BLELIB_ADV_INSTANCE 0

static const char *TAG = "blelib";
//static QueueHandle_t ble_op_queue = NULL;
static uint8_t blelib_own_addr_type = 0;
static uint8_t blelib_addr_val[6] = {0};

static_assert(sizeof(blelib_addr_val)==6, "MAC buffer error");

#if FIRMWARE_BLE_SCAN
static blelib_scan_disc_callback_tFuncPtr scan_callback_func = NULL;
#endif // FIRMWARE_BLE_SCAN

/// @brief NimBLE 事件回调函数
/// @param event 事件
/// @param arg 参数
/// @return 状态
static int blelib_event_callback(struct ble_gap_event *event, void *arg){
    switch (event->type){
#if FIRMWARE_BLE_ADV
        case BLE_GAP_EVENT_ADV_COMPLETE: // adv
            ESP_LOGI(TAG, "adv completed. reason=%d", event->adv_complete);
            break;
#endif // FIRMWARE_BLE_ADV
#if FIRMWARE_BLE_SCAN
        case BLE_GAP_EVENT_EXT_DISC: // scan
            if (scan_callback_func){
                scan_callback_func(&event->ext_disc);
            }
            break;
        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Scan completed. reason=%d", event->disc_complete);
            break;
#endif // FIRMWARE_BLE_SCAN
        default:
            break;
    }
    return 0;
}

/// @brief 添加数据到ADStructure
/// @param payload ADStructure 缓冲区
/// @param payload_len 缓冲区长度
/// @param type AD_Type
/// @param data_len 数据长度
/// @param data 数据指针
static void blelib_add_data_to_payload(uint8_t *payload, uint8_t *payload_len, uint8_t type, uint8_t data_len, void *data){
    uint8_t len = *payload_len;
    payload[len++] = data_len+1;
    payload[len++] = type;
    memcpy(payload+len, data, data_len);
    len += data_len;
    ESP_LOGI(TAG, "Add fiedl: payload=%x payload_len=%hhu type=%hhu data_len=%hhu", (uint32_t)payload, len, type, data_len);
    *payload_len = len;
}

/// @brief BLE 主机任务
/// @param p 空参数
static void blelib_host_task(void *p){
    ESP_LOGI(TAG, "ble host running");
    nimble_port_run();
    ESP_LOGI(TAG, "ble host stopped");
    vTaskDelete(NULL);
}

/// @brief BLE 同步回调
static void blelib_sync_callback(){
    ESP_LOGI(TAG, "sync");
}

/// @brief BLE 重置时回调
/// @param rsn 重置原因
static void blelib_reset_callback(int rsn){
    ESP_LOGE(TAG, "BLE host reset. reason=%d", rsn);
}

void blelib_init(){
    ESP_LOGI(TAG, "init bt");
    /// @note nimble_port_init会调用这些函数，二次执行将导致PANIC
    //esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    //ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    //ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = blelib_sync_callback;
    ble_hs_cfg.reset_cb = blelib_reset_callback;
    ble_svc_gap_init();
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(APP_BLE_NAME));
    ESP_ERROR_CHECK(ble_svc_gap_device_appearance_set(BLELIB_APPEARANCE));
    nimble_port_freertos_init(blelib_host_task);
}

void blelib_deinit(){
    ESP_LOGI(TAG, "deinit bt");
    esp_err_t ret = nimble_port_stop();
    if (ret){
        ESP_LOGE(TAG, "stop nimble failed: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    ESP_ERROR_CHECK(nimble_port_deinit());
    nimble_port_freertos_deinit();
    ESP_ERROR_CHECK(esp_bt_controller_deinit());
}

#endif // FIRMWARE_BLE_ADV || FIRMWARE_BLE_SCAN

#if FIRMWARE_BLE_ADV

void blelib_adv_init(){
    esp_err_t ret = ble_hs_util_ensure_addr(0); // 使用公共地址
    if (ret){
        ESP_LOGE(TAG, "ensure addr failed: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    ret = ble_hs_id_infer_auto(0, &blelib_own_addr_type);
    if (ret){
        ESP_LOGE(TAG, "ble_hs_id_infer_auto -> %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    ESP_LOGI(TAG, "own addr type = %d", blelib_own_addr_type);
    ret = ble_hs_id_copy_addr(blelib_own_addr_type, blelib_addr_val, NULL);
    if (ret){
        ESP_LOGE(TAG, "ble_hs_id_copy_addr -> %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
}

void blelib_adv_start(struct blelib_adv_manfacturer_data *data, uint32_t adv_time){
    if (!data){
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    ESP_LOGI(TAG, "start adv. duration=%u", adv_time);
    struct os_mbuf *mbuf = os_msys_get_pkthdr(sizeof(struct blelib_adv_manfacturer_data), 0);
    if (!mbuf){
        ESP_LOGE(TAG, "failed to allocate mbuf");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    struct ble_gap_ext_adv_params params = {0};
    params.connectable = false;
    params.scannable = false;
    params.legacy_pdu = false;
    params.own_addr_type = blelib_own_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_CODED;
    params.secondary_phy = BLE_HCI_LE_PHY_CODED;
    params.sid = 2;
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(CONFIG_APP_ADV_ITVL_MAX);
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(CONFIG_APP_ADV_ITVL_MIN);
    params.tx_power = ESP_PWR_LVL_P20; // +20dbm
    esp_err_t ret = ble_gap_ext_adv_configure(BLELIB_ADV_INSTANCE, &params, NULL, blelib_event_callback, NULL);
    if (ret){
        ESP_LOGE(TAG, "set adv failed: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }

    uint8_t payload[31] = {0};
    uint8_t payload_len = 0;
    uint8_t flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    blelib_add_data_to_payload(payload, &payload_len, BLE_HS_ADV_TYPE_FLAGS, 1, &flags); // Flags
    blelib_add_data_to_payload(payload, &payload_len, BLE_HS_ADV_TYPE_COMP_NAME, sizeof(APP_BLE_NAME)-1, (void*)APP_BLE_NAME); // Name
    blelib_add_data_to_payload(payload, &payload_len, BLE_HS_ADV_TYPE_MFG_DATA, sizeof(struct blelib_adv_manfacturer_data), data); // ManfacturerData

    printfln("payload_len = %d", payload_len);

    ret = os_mbuf_append(mbuf, payload, payload_len);
    if (ret){
        ESP_LOGE(TAG, "write mbuf failed: %d",ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }

    ret = ble_gap_ext_adv_set_data(0, mbuf);
    if (ret){
        ESP_LOGE(TAG, "set adv data failed: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }

    ret = ble_gap_ext_adv_start(BLELIB_ADV_INSTANCE, adv_time/10, 0);
    if (ret){
        ESP_LOGE(TAG, "start adv failed: %d", ret);
    }
    println("advertising started");
}

void blelib_adv_stop(){
    ESP_LOGI(TAG, "stop adv");
    esp_err_t ret = ble_gap_ext_adv_stop(BLELIB_ADV_INSTANCE);
    if (ret==BLE_HS_EALREADY){
        ESP_LOGW(TAG, "stop adv failed: BLE_HS_EALREADY");
    } else if (ret){
        ESP_LOGE(TAG, "stop adv failed: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
}

#endif // FIRMWARE_BLE_ADV

#if FIRMWARE_BLE_SCAN

void blelib_scan_set_callback(const blelib_scan_disc_callback_tFuncPtr func){
    if (!func){
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    scan_callback_func = func;
}

void blelib_scan_start(uint32_t scan_time){
    if (scan_time>=0xffff*10){
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    struct ble_gap_ext_disc_params params = {0};
    params.passive = true; // 被动扫描
    params.itvl = CONFIG_APP_SCAN_ITVL;
    params.window = CONFIG_APP_SCAN_WINDOW;
    esp_err_t ret = ble_gap_ext_disc(
        blelib_own_addr_type,
        scan_time/10,
        0,
        false, // 过滤重复
        0,
        0,
        NULL,   // 不扫描 1M PHY
        &params,    // 扫描 Coded PHY
        blelib_event_callback,
        NULL
    );
    if (ret){
        ESP_LOGE(TAG, "cannot start scan: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    ESP_LOGI(TAG, "scanning");
}

void blelib_scan_stop(){
    esp_err_t ret = ble_gap_disc_cancel();
    if (ret){
        ESP_LOGE(TAG, "cannot stop scan: %d", ret);
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
    ESP_LOGI(TAG, "scan stopped");
}

bool blelib_iter_payload_fields(uint8_t *start_ptr, uint16_t size, uint8_t **cur_ptr, struct PayloadField *result){
    uint8_t *end_ptr = start_ptr + size;
    if (*cur_ptr<start_ptr||*cur_ptr>=end_ptr){
        return false;
    }
    uint8_t field_len = **cur_ptr;
    if (!field_len){
        result->type = 0;
        result->len = 0;
        result->data = NULL;
        (*cur_ptr)++;
        return true;
    }
    uint8_t ad_type = *(*cur_ptr+1);
    if (field_len==1){
        result->type = ad_type;
        result->len = 0;
        result->data = NULL;
        (*cur_ptr) += 2;
        return true;
    }
    uint8_t data_len = field_len - 1;
    uint8_t diff_size = end_ptr - *cur_ptr;
    if (diff_size<field_len+1){
        return false;
    }
    result->type = ad_type;
    result->len = data_len;
    result->data = (*cur_ptr)+2;
    (*cur_ptr) += field_len+1;
    return true;
}

#endif // FIRMWARE_BLE_SCAN