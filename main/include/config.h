#pragma once

#include "sdkconfig.h"
#include <stdint.h>
#include "hal/adc_types.h"

#if !defined(CONFIG_IDF_TARGET_ESP32C3)||!CONFIG_IDF_TARGET_ESP32C3
#error not supported chip
#endif

#ifndef CONFIG_APP_SERVER
#define CONFIG_APP_SERVER 0
#endif

#ifndef CONFIG_APP_CLIENT
#define CONFIG_APP_CLIENT 0
#endif

#ifndef CONFIG_APP_CLIENT_RESPONSE_ENABLED
#define CONFIG_APP_CLIENT_RESPONSE_ENABLED 0
#endif /* CONFIG_APP_CLIENT_RESPONSE_ENABLED */

#if CONFIG_APP_SERVER == CONFIG_APP_CLIENT
#error repeat firmware type
#elif CONFIG_APP_SERVER
#define FIRMWARE_TYPE_STRING "Server"
#elif CONFIG_APP_CLIENT
#define FIRMWARE_TYPE_STRING "Client"
#else
#error unknown error
#endif /* CONFIG_APP_SERVER == CONFIG_APP_CLIENT */

#if defined(CONFIG_APP_VER_TYPE_RELEASE)&&CONFIG_APP_VER_TYPE_RELEASE
#define FIRMWARE_VER_TYPE "Release"
#elif defined(CONFIG_APP_VER_TYPE_SNAPSHOT)&&CONFIG_APP_VER_TYPE_SNAPSHOT
#define FIRMWARE_VER_TYPE "Snapshot"
#elif defined(CONFIG_APP_VER_TYPE_DEV)&&CONFIG_APP_VER_TYPE_DEV
#define FIRMWARE_VER_TYPE "Dev"
#else
#error unknown error
#endif

static_assert(
    0<=CONFIG_APP_GENERAL_BLE_COMPANY_ID&&CONFIG_APP_GENERAL_BLE_COMPANY_ID<=0xffff
    &&0<=CONFIG_APP_GENERAL_BLE_PROTOCOL_VER&&CONFIG_APP_GENERAL_BLE_PROTOCOL_VER<=0xffff
    &&10<=CONFIG_APP_CLIENT_I2C_SPEED&&CONFIG_APP_CLIENT_I2C_SPEED<=400
    &&10<=CONFIG_APP_SERVER_I2C_SPEED&&CONFIG_APP_SERVER_I2C_SPEED<=400,
    "config out of range"
);

static_assert(
    (
        CONFIG_APP_GENERAL_ADV_ITVL_MAX>=CONFIG_APP_GENERAL_ADV_ITVL_MIN
        &&20<=CONFIG_APP_GENERAL_ADV_ITVL_MIN
        &&CONFIG_APP_GENERAL_ADV_ITVL_MAX<=10240
    ),
    "invalid advertising interval"
);

static_assert(
    CONFIG_APP_GENERAL_SCAN_WINDOW<=CONFIG_APP_GENERAL_SCAN_ITVL,
    "invalid scan window (must be less than or equal interval)"
);

#if CONFIG_APP_GENERAL_CLI_ENABLED
    static_assert(
        8<=CONFIG_APP_GENERAL_CLI_MAX_CMD_LEN&&CONFIG_APP_GENERAL_CLI_MAX_CMD_LEN<=255,
        "invalid cli command length"
    );
    static_assert(
        1<=CONFIG_APP_GENERAL_CLI_MAX_ARG_COUNT&&CONFIG_APP_GENERAL_CLI_MAX_ARG_COUNT<=63,
        "invalid cli arguments count"
    );
    static_assert(
        8<=CONFIG_APP_GENERAL_CLI_MAX_ARG_LEN&&CONFIG_APP_GENERAL_CLI_MAX_ARG_LEN<=255,
        "invalid cli argument length"
    );
#endif // CONFIG_APP_GENERAL_CLI_ENABLED

#define FIRMWARE_VERSION CONFIG_APP_PROJECT_VER

#ifndef CONFIG_APP_SERVER_RTC_DISABLED
#define CONFIG_APP_SERVER_RTC_DISABLED 0
#endif /* CONFIG_APP_SERVER_RTC_DISABLED */

#ifndef CONFIG_APP_SERVER_RTC_DS3231
#define CONFIG_APP_SERVER_RTC_DS3231 0
#endif /* CONFIG_APP_SERVER_RTC_DS3231 */

// ADC 读取电池 1/2 分压后电压
#define CHAN_VBAT    ADC_CHANNEL_1

#define PIN_FUNCT   0   /* 唤醒/按钮 */
#define PIN_PERI_RX 3
#define PIN_PERI_TX 4
#define PIN_OUTPUT  5   /* 输出 */
#define PIN_IIC_SDA 6
#define PIN_IIC_SCL 7
#define PIN_LED     8   /* 板载 LED */
#define PIN_BOOT    9   /* 板载 BOOT 键 */
#define PIN_RUNCLI  10  /* 进入命令行按键 */
#define PIN_UART_RX 20
#define PIN_UART_TX 21

// 外设 UART1
#define PERI_UART_BAUD 9600
#define PERI_UART_BUFSIZE 256
#define PERI_SSD1306_ADDR 0x3C
#define PERI_AT24C32_ADDR 0b1010111
#define PERI_IIC_FREQ 1000000


