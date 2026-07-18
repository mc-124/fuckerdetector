#pragma once

#include "config.h"

#if CONFIG_APP_GENERAL_CLI_ENABLED

#define CLICMD_FREE     0
#define CLICMD_USED     1
#define CLICMD_PROXY    2

#pragma pack(1)
struct CliArgument {
    uint8_t len;
    char buf[CONFIG_APP_GENERAL_CLI_MAX_ARG_LEN+1];
};
#pragma pack()

#pragma pack(1)
struct CliContext {
    uint8_t seek;
    uint8_t cmd_len;
    char cmd_buf[CONFIG_APP_GENERAL_CLI_MAX_CMD_LEN+1];
    struct CliArgument args[CONFIG_APP_GENERAL_CLI_MAX_ARG_COUNT];
};
#pragma pack()
struct CliCommands;
typedef void(*CliCommandFuncPtr)(struct CliContext*, const struct CliCommands*);

struct __CliCommandSlot {
    uint8_t len;    // 不包含字符串的\0
    uint32_t crc32; // 不包含字符串的\0
    const char* cmd; // 不包含\0
    CliCommandFuncPtr func;
    const char* prompt;
};

struct CliCommands {
    uint8_t len;
    struct __CliCommandSlot cmds[CONFIG_APP_GENERAL_CLI_MAX_CMD_COUNT];
};
// 没必要使用哈希表

/// @brief 初始化CLI
/// @param pctx 返回CLI上下文结构体指针
/// @param pcmds 返回CLI命令表结构体指针
void cli_init(struct CliContext **pctx, struct CliCommands **pcmds);

/// @brief 启动CLI主循环
/// @param ctx CLI上下文结构体指针
/// @param cmds CLI命令表结构体指针
/// @note 此函数不会返回
[[noreturn]] void cli_loop(struct CliContext *ctx, struct CliCommands *cmds);

/// @brief 添加命令
/// @param cmds CLI命令表
/// @param command 命令名称字符串 必须始终有效
/// @param func 命令回调函数指针
/// @param prompt? 提示文本（给`/help`用） 可以是NULL，必须始终有效
void cli_add_cmd(struct CliCommands *cmds, const char *command, CliCommandFuncPtr func, const char *prompt);

#endif // CONFIG_APP_GENERAL_CLI_ENABLED