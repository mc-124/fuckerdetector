#pragma once

#include "config.h"

typedef void(*ReplFuncPtr)(uint8_t, const char**);

/// @brief 初始化 REPL
void init_repl();

void __add_command(const char *name, const char *prompt, ReplFuncPtr func);

/// @brief 添加命令 需要先初始化后才能调用
/// @param[in] strName 命令名字符串字面量
/// @param[in] strPrompt 命令提示文本字符串字面量
/// @param[in] pFunc 命令函数指针
#define add_command(strName, strPrompt, pFunc) __add_command(strName "", strPrompt "", pFunc)

/// @brief 进入 REPL
[[noreturn]] void begin_repl();