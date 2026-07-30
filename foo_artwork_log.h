#pragma once
#include "stdafx.h"

extern cfg_bool cfg_quiet_console;

namespace foo_artwork {
    inline void log_printf(const char* fmt, ...) {
        if (cfg_quiet_console) return;
        va_list args;
        va_start(args, fmt);
        ::console::printfv(fmt, args);
        va_end(args);
    }

    inline void log_info(const char* msg) {
        if (cfg_quiet_console) return;
        ::console::info(msg);
    }

    inline void log_print(const char* msg) {
        if (cfg_quiet_console) return;
        ::console::print(msg);
    }
}
