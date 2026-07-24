#pragma once

#include "config.h"
#include <time.h>

#if CONFIG_APP_SERVER

#if !CONFIG_APP_SERVER_RTC_DISABLED

struct timelib_slpitvl {
    int start;
    int end;
};

extern struct timelib_slpitvl timelib_slpitvl_array[CONFIG_APP_SERVER_SLPITVL_MAX_NUM];

#endif

#if CONFIG_APP_SERVER_RTC_DS3231

/// @brief 初始化 不会从 NVS 加载也不会清空`timelib_slpitvl_array`
void timelib_init();

/// @brief 检查 RTC 是否丢失时间
bool timelib_is_lost_time();

/// @brief 清理 RTC 丢失时间标志位
void timelib_clear_lost_time();

/// @brief 设置 RTC 时间
void timelib_set_time(struct tm *time);

/// @brief 获取 RTC 时间
void timelib_get_time(struct tm *time);

/// @brief 获取日秒
/// @return 0~86439 日秒
int timelib_get_day_sec();

/// @brief 尝试查找正在进行中的睡眠间隔
/// @param now 当前的日秒
/// @return 找到的睡眠间隔的指针 找不到返回 NULL
const struct timelib_slpitvl *timelib_find_inprog_slpitvl(int now);

/// @brief 尝试查找下一个可用的睡眠间隔
/// @param now 当前的日秒
/// @return 找到的睡眠间隔的指针 找不到返回 NULL
const struct timelib_slpitvl *timelib_find_next_slpitvl(int now);

/// @brief 打印睡眠间隔
void timelib_print_slpitvl(const struct timelib_slpitvl *slpitvl);

/// @brief 打印所有睡眠间隔
void timelib_print_all_slpitvl();

/// @brief 检查所有睡眠间隔 检查到非法时把它设置为无效
void timelib_check_slpitvl();

#if CONFIG_LOG_DEFAULT_LEVEL_INFO
#   define timelib_logprint_slpitvl(pSlpItvl) timelib_print_slpitvl(pSlpItvl)
#else
#   define timelib_logprint_slpitvl(pSlpItvl) do{}while(0)
#endif

#else // CONFIG_APP_SERVER_RTC_DISABLED

#endif 

#endif // CONFIG_APP_SERVER