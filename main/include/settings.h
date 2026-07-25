#include "config.h"

#if CONFIG_APP_SERVER

/// @brief 重置所有睡眠间隔 并commit
void settings_reset_all_sleepintervals();
/// @brief 初始化
void settings_init();
/// @brief 加载设置
void settings_load();
/// @brief 保存设置
void settings_store();
/// @brief 为 REPL 添加命令
void settings_addcmds();

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

// 启用暴力震动模式
// b
#define SETTINGS_CLIENT__VIOLANCE_ENABLED "v1e"
// 探测器使用暴力震动模式
// b
#define SETTINGS_CLIENT__VIOLANCE_SERVER "v1s"
// 正常震动 震动次数
// u8
#define SETTINGS_CLIENT__VIB_NORMAL_NUM "v0n"
// 正常震动 震动持续时间
// u16 (ms)
#define SETTINGS_CLIENT__VIB_NORMAL_DUR "v0d"
// 正常震动 震动间隔时间
// u16 (ms)
#define SETTINGS_CLIENT__VIB_NORMAL_INT "v0i"
// 正常震动 震动功率百分比
// u8 (1-100)
#define SETTINGS_CLIENT__VIB_NORMAL_PWR "v0p"
// 暴力震动 震动次数
// u8
#define SETTINGS_CLIENT__VIB_VIOLANCE_NUM "v1n"
// 暴力震动 震动持续时间
// u16 (ms)
#define SETTINGS_CLIENT__VIB_VIOLANCE_DUR "v1d"
// 暴力震动 震动间隔时间
// u16 (ms)
#define SETTINGS_CLIENT__VIB_VIOLANCE_INT "v1i"
// 暴力震动 震动功率百分比
// u8 (1-100)
#define SETTINGS_CLIENT__VIB_VIOLANCE_PWR "v1p"

#endif
