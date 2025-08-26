#pragma once

// foobar2000 v1.6 compatibility - define moved to project settings

// Threading compatibility fixes for Visual Studio 2022
#define _ALLOW_RTCc_IN_STL
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING

// Prevent socket conflicts
#define _WINSOCKAPI_
#define NOMINMAX

// Standard C++ headers first
#include <memory>

// Windows headers - include COM definitions
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <shellapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <gdiplus.h>
#include <atlbase.h>
#include <shlwapi.h>
#include <ole2.h>
#include <winhttp.h>

// Library linking
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")

// GDI+ namespace usage
using namespace Gdiplus;

// Define CUI support - enables Columns UI integration
#define COLUMNS_UI_AVAILABLE

// Include the foobar2000 SDK from CUI (which includes the same SDK)
#include "columns_ui/foobar2000/SDK/foobar2000.h"
#include "columns_ui/foobar2000/SDK/playback_control.h"
#include "columns_ui/foobar2000/SDK/play_callback.h"
#include "columns_ui/foobar2000/SDK/metadb_handle.h"
#include "columns_ui/foobar2000/SDK/file_info.h"
#include "columns_ui/foobar2000/SDK/file_info_impl.h"
#include "columns_ui/foobar2000/SDK/album_art.h"
#include "columns_ui/foobar2000/SDK/coreDarkMode.h"
#include "columns_ui/foobar2000/SDK/titleformat.h"
#include "columns_ui/foobar2000/SDK/ui_element.h"

#include "resource.h"
