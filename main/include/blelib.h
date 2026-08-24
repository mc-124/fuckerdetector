#pragma once
#include "config.h"

#if FIRMWARE_BLE_ADV || FIRMWARE_BLE_SCAN

#include "host/ble_gap.h"

#if !CONFIG_BT_NIMBLE_ENABLED
#error need nimble
#endif /* !CONFIG_BT_NIMBLE_ENABLED */

#define ADVTYPE_SERVER_ALARM    ((uint8_t)0x00)

#define ADVTYPE_CLIENT_ALARM    ((uint8_t)0x80)
#define ADVTYPE_CLIENT_LOUD ((uint8_t)0x81)
#define ADVTYPE_CLIENT_RESPONSE ((uint8_t)0x82)

#define BLELIB_ADV_FLAGS (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP)

#define ADV_IS_CLIENT(__u8Type) (__u8Type & 0x80)

extern uint8_t blelib_addr_val[6];

// encode vbat value range: [2.2, 4.75]

static inline uint8_t blelib_encode_vbat(float vbat) {
    return (
        (uint8_t)(
            (
                (int)(vbat*100.0f)
            )-220
        )
    );
}
static inline float blelib_decode_vbat(uint8_t encoded_vbat) {
    return(
        (float)(
            encoded_vbat+220
        )/100.0f
    );
}

#if CONFIG_APP_SERVER
#define APP_BLE_NAME CONFIG_APP_SERVER_BLE_NAME
#elif CONFIG_APP_CLIENT
#define APP_BLE_NAME CONFIG_APP_CLIENT_BLE_NAME
#endif

#pragma pack(1)
struct blelib_adv_manfacturer_data {
    uint16_t company_id;
    uint8_t type;
    uint8_t encoded_vbat;   // client: 0
    union {
        int day_sec;        // server_only [0, 86400)
        uint32_t adv_id;    // client_only
    } data;
    uint16_t protocol_ver;
};
#pragma pack()

/// @brief 扫描回调函数指针
typedef void(*blelib_scan_disc_callback_t)(const struct ble_gap_ext_disc_desc*);;

static_assert(
    (0
        +(sizeof(APP_BLE_NAME)+2)
        +(sizeof(struct blelib_adv_manfacturer_data)+2)
    ) <= 31,
    "advertising data too big"
);

struct blelib_payload_field {
    uint8_t type;
    uint8_t len;
    const uint8_t *data;
};

/// @brief 初始化蓝牙控制器 BLE协议栈 GAP服务
void blelib_init();

/// @brief 反初始化蓝牙控制器 BLE协议栈
void blelib_deinit();

#endif // FIRMWARE_BLE_ADV || FIRMWARE_BLE_SCAN

#if FIRMWARE_BLE_ADV

/// @brief 等待广告完毕
/// @return 广告是否完毕
bool blelib_adv_wait_for_complete(uint32_t max_wait_ms);

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

/// @brief 等待扫描完毕
/// @return 扫描是否完毕
bool blelib_scan_wait_for_complete(uint32_t max_wait_ms);

/// @brief 设置扫描回调
/// @param func 扫描回调函数指针
void blelib_scan_set_callback(const blelib_scan_disc_callback_t func);

/// @brief 开始扫描
/// @param scan_time 扫描时间（ms）
void blelib_scan_start(uint32_t scan_time);

/// @brief 停止扫描
void blelib_scan_stop();

/// @brief 迭代 ADStructure 里的每个字段
/// @param payload_buf payload缓冲区指针
/// @param size payload缓冲区大小
/// @param cur_ptr 当前指针
/// @param result 迭代结果
/// @return 是否成功
/// @note 结束迭代时
bool blelib_iter_payload_fields(const uint8_t *payload_buf, uint16_t size, const uint8_t **cur_ptr, struct blelib_payload_field *result);

#endif // FIRMWARE_BLE_SCAN