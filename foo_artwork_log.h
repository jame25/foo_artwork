#pragma once
#include "stdafx.h"

extern cfg_int cfg_console_logging_mode;

namespace foo_artwork {
    enum ConsoleLoggingMode {
        MODE_QUIET = 0,
        MODE_TRACK_INFO = 1,
        MODE_DEBUG = 2
    };

    inline void log_track_info(const char* fmt, ...) {
        if (cfg_console_logging_mode == MODE_QUIET) return;
        va_list args;
        va_start(args, fmt);
        ::console::printfv(fmt, args);
        va_end(args);
    }

    inline void log_printf(const char* fmt, ...) {
        if (cfg_console_logging_mode != MODE_DEBUG) return;
        va_list args;
        va_start(args, fmt);
        ::console::printfv(fmt, args);
        va_end(args);
    }

    inline void log_info(const char* msg) {
        if (cfg_console_logging_mode != MODE_DEBUG) return;
        ::console::info(msg);
    }

    inline void log_print(const char* msg) {
        if (cfg_console_logging_mode != MODE_DEBUG) return;
        ::console::print(msg);
    }
}
