#pragma once
#include "config.h"

#if FIRMWARE_BLE_ADV || FIRMWARE_BLE_SCAN

#include "host/ble_gap.h"

#if !CONFIG_BT_NIMBLE_ENABLED
#error need nimble
#endif /* !CONFIG_BT_NIMBLE_ENABLED */

#define ADVTYPE_SERVER_ALARM    ((uint8_t)0x00)

#define ADVTYPE_CLIENT_ALARM    ((uint8_t)0x80)
#define ADVTYPE_CLIENT_VIOLANCE ((uint8_t)0x81)
#define ADVTYPE_CLIENT_RESPONSE ((uint8_t)0x82)

#define ADV_IS_CLIENT(__u8Type) (__u8Type & 0x80)

// encode vbat value range: [2.2, 4.75]

inline uint8_t blelib_encode_vbat(float vbat) {return ((uint8_t)(((int)(vbat*100.0))-220));}
inline uint8_t blelib_decode_vbat(float vbat) {return((float)(((int)vbat)+220)/100.0);}

#if CONFIG_APP_SERVER
#define APP_BLE_NAME CONFIG_APP_SERVER_BLE_NAME
#elif CONFIG_APP_CLIENT
#define APP_BLE_NAME CONFIG_APP_CLIENT_BLE_NAME
#endif

#pragma pack(1)
struct blelib_adv_manfacturer_data {
    uint16_t company_id;
    uint8_t type;
    uint8_t encoded_vbat;
    union {
        int day_sec;        // server_only [0, 86400)
        uint32_t adv_id;    // client_only
    } data;
    uint16_t protocol_ver;
};
#pragma pack()

/// @brief 扫描回调函数指针
typedef void(*blelib_scan_disc_callback_t)(struct ble_gap_ext_disc_desc*);;

static_assert(
    (0
        +(sizeof(APP_BLE_NAME)+2)               // Adv name
        +(sizeof(struct blelib_adv_manfacturer_data)+2)  // ManfacturesData
    ) <= 31,
    "advertising data too big"
);

struct PayloadField {
    uint8_t type;
    uint8_t len;
    uint8_t *data;
};

/// @brief 初始化蓝牙控制器 BLE协议栈 GAP服务
void blelib_init();

/// @brief 反初始化蓝牙控制器 BLE协议栈
void blelib_deinit();

#endif // FIRMWARE_BLE_ADV || FIRMWARE_BLE_SCAN

#if FIRMWARE_BLE_ADV

/// @brief 初始化广告
void blelib_adv_init();

/// @brief 开始发送广告
/// @param data 广告 ManfacturerData
/// @param adv_time 持续时间（ms）
void blelib_adv_start(struct blelib_adv_manfacturer_data *data, uint32_t adv_time);

/// @brief 停止发送广告
void blelib_adv_stop();

#endif // FIRMWARE_BLE_ADV

#if FIRMWARE_BLE_SCAN

/// @brief 设置扫描回调
/// @param func 扫描回调函数指针
void blelib_scan_set_callback(const blelib_scan_disc_callback_t func);

/// @brief 开始扫描
/// @param scan_time 扫描时间（ms）
void blelib_scan_start(uint32_t scan_time);

/// @brief 停止扫描
void blelib_scan_stop();

/// @brief 迭代 ADStructure 里的每个字段
/// @param start_ptr 缓冲区起始指针
/// @param size 缓冲区大小
/// @param cur_ptr 当前指针
/// @param result 迭代结果
/// @return 是否成功
bool blelib_iter_payload_fields(uint8_t *start_ptr, uint16_t size, uint8_t **cur_ptr, struct PayloadField *result);

#endif // FIRMWARE_BLE_SCAN