#pragma once

#include "sdkconfig.h"
#include <stdint.h>
#include "hal/adc_types.h"

#if !defined(CONFIG_IDF_TARGET_ESP32C3)||!CONFIG_IDF_TARGET_ESP32C3
#   error not supported chip
#endif

#if CONFIG_APP_SERVER
#   define CONFIG_APP_CLIENT 0
#elif CONFIG_APP_CLIENT
#   define CONFIG_APP_SERVER 0
#else
#   error invalid firmware type
#endif

#if CONFIG_APP_SERVER
#   define FIRMWARE_TYPE_STRING "Server"
#elif CONFIG_APP_CLIENT
#   define FIRMWARE_TYPE_STRING "Client"
#else
#   error unknown error
#endif /* CONFIG_APP_SERVER == CONFIG_APP_CLIENT */

#ifdef CONFIG_COMPILER_OPTIMIZATION_LEVEL_DEBUG
#   define FIRMWARE_VER_TYPE "Debug"
#   define FIRMWARE_VER_TYPE_SHORT "D"
#else
#   define FIRMWARE_VER_TYPE "Release"
#   define FIRMWARE_VER_TYPE_SHORT "R"
#endif

static_assert(
    0<=CONFIG_APP_BLE_COMPANY_ID&&CONFIG_APP_BLE_COMPANY_ID<=0xffff
    &&0<=CONFIG_APP_BLE_PROTOCOL_VER&&CONFIG_APP_BLE_PROTOCOL_VER<=0xffff
    &&10<=CONFIG_APP_CLIENT_I2C_SPEED&&CONFIG_APP_CLIENT_I2C_SPEED<=400,
    "config out of range"
);

#define U32 (unsigned int)
#define DISABLE_TYPELIMIT_START \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wtype-limits\"")
#define DISABLE_TYPELIMIT_END \
    _Pragma("GCC diagnostic pop")

#if CONFIG_APP_SERVER
#   define CONFIG_APP_I2C_SPEED CONFIG_APP_SERVER_I2C_SPEED
#   define CONFIG_APP_ADV_ITVL_MAX CONFIG_APP_SERVER_ADV_ITVL_MAX
#   define CONFIG_APP_ADV_ITVL_MIN CONFIG_APP_SERVER_ADV_ITVL_MIN
#elif CONFIG_APP_CLIENT
#   define CONFIG_APP_I2C_SPEED CONFIG_APP_CLIENT_I2C_SPEED
#   define CONFIG_APP_ADV_ITVL_MAX CONFIG_APP_CLIENT_ADV_ITVL_MAX
#   define CONFIG_APP_ADV_ITVL_MIN CONFIG_APP_CLIENT_ADV_ITVL_MIN 
#   define CONFIG_APP_SCAN_ITVL CONFIG_APP_CLIENT_SCAN_ITVL
#   define CONFIG_APP_SCAN_WINDOW CONFIG_APP_CLIENT_SCAN_WINDOW

static_assert(
    CONFIG_APP_SCAN_WINDOW<=CONFIG_APP_SCAN_ITVL,
    "invalid scan window (must be less than or equal interval)"
);

#endif

static_assert(
    (
        CONFIG_APP_ADV_ITVL_MAX>=CONFIG_APP_ADV_ITVL_MIN
        &&20<=CONFIG_APP_ADV_ITVL_MIN
        &&CONFIG_APP_ADV_ITVL_MAX<=10240
    ),
    "invalid advertising interval"
);

#if CONFIG_APP_SERVER
#   define FIRMWARE_BLE_SCAN 0
#   define FIRMWARE_BLE_ADV 1
#elif CONFIG_APP_CLIENT
#   if CONFIG_APP_CLIENT_RX_SERVERALARM || CONFIG_APP_CLIENT_RX_CLIENTALARM
#       define FIRMWARE_BLE_SCAN 1
#   else
#       define FIRMWARE_BLE_SCAN 0
#   endif
#   if CONFIG_APP_CLIENT_TX_CLIENTALARM
#       define FIRMWARE_BLE_ADV 1
#   else
#       define FIRMWARE_BLE_ADV 0
#   endif
#endif

static_assert(500<=CONFIG_APP_SERVER_ADV_DURATION&&CONFIG_APP_SERVER_ADV_DURATION<=60000, "invalid server adv duration");
static_assert(500<=CONFIG_APP_CLIENT_ADV_DURATION&&CONFIG_APP_CLIENT_ADV_DURATION<=60000, "invalid client adv duration");
static_assert(2<=CONFIG_APP_SERVER_SLPITVL_MAX_NUM&&CONFIG_APP_SERVER_SLPITVL_MAX_NUM<=255, "invalid server sleep interval number");

#define FIRMWARE_VERSION CONFIG_APP_PROJECT_VER

#ifndef CONFIG_APP_SERVER_RTC_DISABLED
#define CONFIG_APP_SERVER_RTC_DISABLED 0
#endif /* CONFIG_APP_SERVER_RTC_DISABLED */

#ifndef CONFIG_APP_SERVER_RTC_DS3231
#define CONFIG_APP_SERVER_RTC_DS3231 0
#endif /* CONFIG_APP_SERVER_RTC_DS3231 */

#ifdef __clang__
#define COMPILER_VERSION "Clang" \
    STRINGIFY(__clang_major__) "." \
    STRINGIFY(__clang_minor__) "." \
    STRINGIFY(__clang_patchlevel__)
#elif __GNUC__
#define COMPILER_VERSION "GCC " \
    STRINGIFY(__GNUC__) "." \
    STRINGIFY(__GNUC_MINOR__) "." \
    STRINGIFY(__GNUC_PATCHLEVEL__)
#endif


// ADC 读取电池 1/2 分压后电压
#define CHAN_VBAT   ADC_CHANNEL_1

#define PIN_FUNCT   0   /* 唤醒/按钮 */
#define PIN_PERI_RX 3
#define PIN_PERI_TX 4
#define PIN_OUTPUT  5   /* 输出 */
#define PIN_IIC_SDA 6
#define PIN_IIC_SCL 7
#define PIN_LED     8   /* 板载 LED */
#define PIN_BOOT    9   /* 板载 BOOT 键 */
#define PIN_CMDLINE 10  /* 进入命令行按键 */
#define PIN_UART_RX 20
#define PIN_UART_TX 21

// 外设 UART1
#define PERI_UART_BAUD 9600
#define PERI_UART_BUFSIZE 256

// 8bit 地址
#define PERI_SSD1306_ADDR 0x3C
//#define PERI_AT24C32_ADDR 0b1010111
//#define PERI_IIC_FREQ 1000000

#define REPL_MAX_CMDS 12
#define REPL_MAX_ARGS 8
#define REPL_MAX_CMD_LEN 12
#define REPL_MAX_ARG_LEN 12
#define REPL_CRC32_DEFAULT 0xCC114514

#define APP_CLIENT_BLETASK_STACKSIZE 4096