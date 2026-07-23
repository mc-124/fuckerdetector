#pragma once

#include "config.h"

typedef void(*ReplFuncPtr)(uint8_t, const char**);

void init_repl();
void __add_command(const char *name, const char *prompt, ReplFuncPtr func);
#define add_command(strName, strPrompt, pFunc) \
    __add_command(strName "", strPrompt "", pFunc)
void begin_repl();