#pragma once

#include "config.h"

typedef void(*repl_cmd_callback_func_t)(uint8_t, const char**);

/// @brief 初始化 REPL
void repl_init();

// 不要用这个 去用 repl_addcmd
void __repl_addcmd(const char *name, const char *prompt, repl_cmd_callback_func_t func);

/// @brief 添加命令 需要先初始化后才能调用
/// @param[in] strName 命令名字符串字面量
/// @param[in] strPrompt 命令提示文本字符串字面量
/// @param[in] pFunc 命令函数指针
#define repl_addcmd(strName, strPrompt, pFunc) __repl_addcmd(strName "", strPrompt "", pFunc)

/// @brief 进入 REPL
[[noreturn]] void repl_begin();