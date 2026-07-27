#include "config.h"

/// @brief 初始化
void settings_init();
/// @brief 加载设置
void settings_load();
/// @brief 保存设置
void settings_store();
/// @brief 为 REPL 添加命令
void settings_addcmds();

#if CONFIG_APP_SERVER

/// @brief 重置所有睡眠间隔 并commit
void settings_reset_all_sleepintervals();

#define SETTINGS_NAMESPACE "servercfg"
//////////////////// Server Settings

// 睡眠区间数组长度
// u8
#define SETTINGS_SERVER_SITVL_LENGTH "sil"
// 睡眠区间数组
// string
#define SETTINGS_SERVER_SLEEP_INTERVAL "sia"

#elif CONFIG_APP_CLIENT
#define SETTINGS_NAMESPACE "clientcfg"
//////////////////// Client Settings

#define SETTINGS_VERSION 1

#define SETTINGS_CLIENT_SETVERSION "ver"

// 配置结构体
// u64
#define SETTINGS_CLIENT_CONFIG "cfg"

#define SETTINGS_SET_NUM 12

struct settings {
    uint8_t enable_recv_server_alarm:1; // 启用接收探测端警告
    uint8_t vib_normal_num:7; // 普通震动 震动次数 (0-127)+1
    uint8_t vib_normal_dur; // 普通震动 震动时间（20ms）(0-255)+1 10ms-5120ms
    uint8_t vib_normal_itv; // 普通震动 震动间隔（20ms）(0-255)+1 10ms-5120ms
    uint8_t enable_recv_client_alarm:1; // 启用接收客户端警告
    uint8_t vib_normal_pwr:7; // 普通震动 震动功率 1-100
    uint8_t enable_recv_client_violance_alarm:1; // 启用接收客来自户端的暴力型警告
    uint8_t vib_violance_num:7; // 暴力震动 震动次数 (0-127)+1
    uint8_t vib_violance_dur; // 暴力震动 震动时间（20ms）(0-255)+1 10ms-5120ms 
    uint8_t vib_violance_itv; // 暴力震动 震动间隔（20ms）(0-255)+1 10ms-5120ms
    uint8_t server_alarm_as_violance:1; // 把探测端警告视为暴力型警告
    uint8_t vib_violance_pwr:7; // 暴力震动 震动功率 1-100
};

struct settings_config_desc {
    const char *name;
    int8_t display_offset;
    uint8_t display_mul;
    uint8_t min_value;
    uint8_t max_value;
    // is bool: display_offset=0 display_mul=1 min_value=0 nax_value=1
};

extern struct settings settings;
extern const struct settings_config_desc settings_config_list[SETTINGS_SET_NUM];

// IDE 补全不注意会变成 settings_field_is_bool，写成宏更容易发现问题
#define settings_field_is_valid(__Index) (0<=__Index&&__Index<SETTINGS_SET_NUM)

bool settings_field_is_bool(const struct settings_config_desc *desc);
uint32_t settings_get_field_display_value(const struct settings_config_desc *desc, uint8_t value);
uint8_t settings_displayvalue_to_rawvalue(const struct settings_config_desc *desc, uint32_t display_value);
void settings_print_field(const struct settings_config_desc *desc, uint8_t value);
bool settings_human_rw(uint8_t index, bool is_w, uint8_t *value);

#endif
