#pragma once

#include "config.h"

#if CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040

#ifdef CONFIG_APP_SERVER_RADER_PWRON_DURATION
#undef CONFIG_APP_SERVER_RADER_PWRON_DURATION
#endif
#define CONFIG_APP_SERVER_RADER_PWRON_DURATION 7000

void __raderctl_init();
#define raderctl_init() __raderctl_init()

#endif // CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040

#ifndef raderctl_init
#define raderctl_init() do {} while(0)
#endif // raderctl_init