#pragma once

#include "config.h"

#if CONFIG_APP_CLI_ENABLED

#define CLICMD_FREE     0
#define CLICMD_USED     1
#define CLICMD_PROXY    2

#pragma pack(1)
struct CliArgument {
    uint8_t len;
    char buf[CONFIG_APP_CLI_MAX_ARG_LEN+1];
};
#pragma pack()

#pragma pack(1)
struct CliContext {
    uint8_t seek;
    uint8_t cmd_len;
    char cmd_buf[CONFIG_APP_CLI_MAX_CMD_LEN+1];
    struct CliArgument args[CONFIG_APP_CLI_MAX_ARG_NUM];
};
#pragma pack()
struct CliCommands;
typedef void(*CliCommandFuncPtr)(struct CliContext*, const struct CliCommands*);

struct __CliCommandSlot {
    uint8_t len;    // 不包含字符串的\0
    uint32_t crc32; // 不包含字符串的\0
    const char* cmd;
    CliCommandFuncPtr func;
    const char* prompt;
};

struct CliCommands {
    uint8_t len;
    struct __CliCommandSlot cmds[CONFIG_APP_CLI_MAX_CMD_NUM];
};
// 没必要使用哈希表

const struct __CliCommandSlot *cli_find_cmd(const struct CliCommands *cmds, const char *name, uint8_t name_len);

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

/// @brief 检查命令得到的参数数量
/// @param ctx CLI上下文
/// @param len 期望得到的参数数量
/// @return 实际得到的参数数量与期望是否相等
inline bool cli_cmd_chk_arg_len(const struct CliContext *ctx, uint8_t len){
    return ctx->seek-1==len;
}

/// @brief 获取命令得到的参数的字符串指针
/// @param ctx CLI上下文
/// @param index 参数索引
/// @return 字符串指针
/// @note 字符串以 \0 结尾
/// @note 遇到不存在的参数时返回 NULL
inline const char* cli_cmd_get_arg_str(const struct CliContext *ctx, uint8_t index){
    if (!cli_cmd_chk_arg_len(ctx, index+1)) return NULL;
    return ctx->args[index].buf;
}

/// @brief 获取命令得到的参数的字符串长度
/// @param ctx CLI上下文
/// @param index 参数索引
/// @return 字符串长度
/// @note 长度不包含 \0
/// @note 遇到不存在的参数时返回 0
inline uint8_t cli_cmd_get_arg_strlen(const struct CliContext *ctx, uint8_t index){
    if (!cli_cmd_chk_arg_len(ctx, index+1)) return 0;
    return ctx->args[index].len;
}

#define cli_cmd_invalid_arg() do {println("error: invalid arguments");return;} while(0)

#endif // CONFIG_APP_CLI_ENABLED