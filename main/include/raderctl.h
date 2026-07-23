#pragma once

#include "config.h"

#if CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040

void __init_raderctl();
#define init_raderctl() __init_raderctl()

#endif // CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040

#ifndef init_raderctl
#define init_raderctl() do {} while(0)
#endif // init_raderctl