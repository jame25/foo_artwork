#include "stdafx.h"
#include "artwork_manager.h"
#include "metadata_cleaner.h"
#include "preferences.h"
#include <algorithm>
#include <thread>
#include <vector>
#include <mutex>
#include <regex>
#include <shlobj.h>

// CUI support is now defined in stdafx.h

static class debug_init {
public:
    debug_init() {
#ifdef _DEBUG
#ifdef COLUMNS_UI_AVAILABLE
#else
#endif
#endif
    }
} g_debug_init;

#ifdef COLUMNS_UI_AVAILABLE
    #pragma message("Compiling with Columns UI support")
    // CUI panel will register itself via static initialization in artwork_panel_cui.cpp
    #include "artwork_panel_cui.h"
    
    static class cui_debug_init {
    public:
        cui_debug_init() {
#ifdef _DEBUG
#endif
        }
    } g_cui_debug;
#else
    #pragma message("Columns UI support NOT available")
#endif

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

// Configuration variable GUIDs
static constexpr GUID guid_cfg_enable_itunes = { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };
static constexpr GUID guid_cfg_enable_discogs = { 0x12345679, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf1 } };
static constexpr GUID guid_cfg_enable_lastfm = { 0x1234567a, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf2 } };
static constexpr GUID guid_cfg_enable_deezer = { 0x1234567b, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf3 } };
static constexpr GUID guid_cfg_enable_musicbrainz = { 0x12345681, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf9 } };
static constexpr GUID guid_cfg_discogs_key = { 0x1234567c, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf4 } };
static constexpr GUID guid_cfg_discogs_consumer_key = { 0x1234567e, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf6 } };
static constexpr GUID guid_cfg_discogs_consumer_secret = { 0x1234567f, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf7 } };
static constexpr GUID guid_cfg_lastfm_key = { 0x1234567d, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf5 } };
static constexpr GUID guid_cfg_fill_mode = { 0x12345680, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };
static constexpr GUID guid_cfg_priority_1 = { 0x12345682, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfa } };
static constexpr GUID guid_cfg_priority_2 = { 0x12345683, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfb } };
static constexpr GUID guid_cfg_priority_3 = { 0x12345684, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfc } };
static constexpr GUID guid_cfg_priority_4 = { 0x12345685, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfd } };
static constexpr GUID guid_cfg_priority_5 = { 0x12345686, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfe } };
static constexpr GUID guid_cfg_show_osd = { 0x12345688, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };
static constexpr GUID guid_cfg_enable_custom_logos = { 0x12345689, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf9 } };
static constexpr GUID guid_cfg_logos_folder = { 0x1234568a, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfa } };
static constexpr GUID guid_cfg_clear_panel_when_not_playing = { 0x1234568b, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfb } };
static constexpr GUID guid_cfg_use_noart_image = { 0x1234568c, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xfc } };
static constexpr GUID guid_cfg_infobar = { 0x59b0b41b, 0x2d12, 0x4965, { 0xaa, 0x4a, 0xb5, 0x80, 0x5, 0x55, 0x2e, 0xf7 } };


// Configuration variables with default values
cfg_bool cfg_enable_itunes(guid_cfg_enable_itunes, false);
cfg_bool cfg_enable_discogs(guid_cfg_enable_discogs, false);
cfg_bool cfg_enable_lastfm(guid_cfg_enable_lastfm, false);
cfg_bool cfg_enable_deezer(guid_cfg_enable_deezer, true);
cfg_bool cfg_enable_musicbrainz(guid_cfg_enable_musicbrainz, false);
cfg_string cfg_discogs_key(guid_cfg_discogs_key, "");
cfg_string cfg_discogs_consumer_key(guid_cfg_discogs_consumer_key, "");
cfg_string cfg_discogs_consumer_secret(guid_cfg_discogs_consumer_secret, "");
cfg_string cfg_lastfm_key(guid_cfg_lastfm_key, "");
cfg_bool cfg_fill_mode(guid_cfg_fill_mode, false);  // true = fill window (crop), false = fit window (letterbox)

// API Priority order (0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs)
// Default order: Deezer > iTunes > Last.fm > MusicBrainz > Discogs
// NEW SIMPLE PRIORITY SYSTEM
// Each variable represents a search position (1st, 2nd, 3rd, etc.)
// Values: 0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs
cfg_int cfg_search_order_1(guid_cfg_priority_1, 1);  // 1st choice: Deezer (default)
cfg_int cfg_search_order_2(guid_cfg_priority_2, 0);  // 2nd choice: iTunes (default)
cfg_int cfg_search_order_3(guid_cfg_priority_3, 2);  // 3rd choice: Last.fm (default)
cfg_int cfg_search_order_4(guid_cfg_priority_4, 3);  // 4th choice: MusicBrainz (default)
cfg_int cfg_search_order_5(guid_cfg_priority_5, 4);  // 5th choice: Discogs (default)


// OSD display setting (default enabled)
cfg_bool cfg_show_osd(guid_cfg_show_osd, true);

// Custom Station Logos settings
cfg_bool cfg_enable_custom_logos(guid_cfg_enable_custom_logos, true);  // Enable custom station logos (default enabled)
cfg_string cfg_logos_folder(guid_cfg_logos_folder, "");  // Custom logos folder path (empty = use default)

// Miscellaneous settings
cfg_bool cfg_clear_panel_when_not_playing(guid_cfg_clear_panel_when_not_playing, false);  // Clear panel when not playing (default disabled)
cfg_bool cfg_use_noart_image(guid_cfg_use_noart_image, false);  // Use noart image when clearing panel (default disabled)

cfg_bool cfg_infobar(guid_cfg_infobar, false);  // DUI infobar (default disabled)


//=============================================================================
// Event-Driven Artwork System
//=============================================================================

// Artwork event types
enum class ArtworkEventType {
    ARTWORK_LOADED,     // New artwork loaded successfully
    ARTWORK_LOADING,    // Search started
    ARTWORK_FAILED,     // Search failed
    ARTWORK_CLEARED     // Artwork cleared
};

// Artwork event data
struct ArtworkEvent {
    ArtworkEventType type;
    HBITMAP bitmap;
    std::string source;
    std::string artist;
    std::string title;
    
    ArtworkEvent(ArtworkEventType t, HBITMAP bmp = nullptr, const std::string& src = "", 
                 const std::string& art = "", const std::string& ttl = "")
        : type(t), bitmap(bmp), source(src), artist(art), title(ttl) {}
};

// Artwork event listener interface
class IArtworkEventListener {
public:
    virtual ~IArtworkEventListener() = default;
    virtual void on_artwork_event(const ArtworkEvent& event) = 0;
};

// Artwork event manager (singleton)
class ArtworkEventManager {
private:
    std::vector<IArtworkEventListener*> m_listeners;
    std::mutex m_listeners_mutex;
    static std::unique_ptr<ArtworkEventManager> s_instance;
    
public:
    static ArtworkEventManager& get() {
        if (!s_instance) {
            s_instance = std::make_unique<ArtworkEventManager>();
        }
        return *s_instance;
    }
    
    void subscribe(IArtworkEventListener* listener) {
        std::lock_guard<std::mutex> lock(m_listeners_mutex);
        m_listeners.push_back(listener);
    }
    
    void unsubscribe(IArtworkEventListener* listener) {
        std::lock_guard<std::mutex> lock(m_listeners_mutex);
        m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
    }
    
    void notify(const ArtworkEvent& event) {
        std::lock_guard<std::mutex> lock(m_listeners_mutex);
        for (auto* listener : m_listeners) {
            try {
                listener->on_artwork_event(event);
            } catch (...) {
                // Continue notifying other listeners even if one fails
            }
        }
    }
};

std::unique_ptr<ArtworkEventManager> ArtworkEventManager::s_instance;

//=============================================================================
// Per-Request HTTP Management System
//=============================================================================

// HTTP request manager for individual APIs
class HttpRequestManager {
private:
    std::string m_api_name;
    std::mutex m_request_mutex;
    std::atomic<bool> m_request_active{false};
    
public:
    HttpRequestManager(const std::string& api_name) : m_api_name(api_name) {}
    
    // RAII lock for HTTP requests
    class RequestLock {
    private:
        HttpRequestManager* m_manager;
        std::unique_lock<std::mutex> m_lock;
        bool m_acquired;
        
    public:
        RequestLock(HttpRequestManager* manager) 
            : m_manager(manager), m_lock(manager->m_request_mutex), m_acquired(true) {
            manager->m_request_active = true;
        }
        
        ~RequestLock() {
            if (m_acquired && m_manager) {
                m_manager->m_request_active = false;
            }
        }
        
        bool is_acquired() const { return m_acquired; }
    };
    
    bool is_busy() const { return m_request_active.load(); }
    
    RequestLock acquire_lock() {
        return RequestLock(this);
    }
};

// API-specific request managers
static HttpRequestManager g_itunes_request_manager("iTunes");
static HttpRequestManager g_deezer_request_manager("Deezer");
static HttpRequestManager g_lastfm_request_manager("Last.fm");
static HttpRequestManager g_musicbrainz_request_manager("MusicBrainz");
static HttpRequestManager g_discogs_request_manager("Discogs");

// Helper function to get request manager by API name
HttpRequestManager* get_request_manager_for_api(const std::string& api_name) {
    if (api_name.find("iTunes") != std::string::npos) return &g_itunes_request_manager;
    if (api_name.find("Deezer") != std::string::npos) return &g_deezer_request_manager;
    if (api_name.find("Last.fm") != std::string::npos || api_name.find("lastfm") != std::string::npos) return &g_lastfm_request_manager;
    if (api_name.find("MusicBrainz") != std::string::npos || api_name.find("musicbrainz") != std::string::npos) return &g_musicbrainz_request_manager;
    if (api_name.find("Discogs") != std::string::npos) return &g_discogs_request_manager;
    return &g_deezer_request_manager; // Default fallback
}

// Forward declarations
class artwork_ui_element;

// Global variables for CUI panel communication
std::wstring g_current_artwork_path;
bool g_artwork_loading = false;
HBITMAP g_shared_artwork_bitmap = NULL;

// Artwork source tracking
pfc::string8 g_current_artwork_source;

// Global reference to main UI element for artwork sharing
static artwork_ui_element* g_main_ui_element = nullptr;

// Component's DLL instance handle
HINSTANCE g_hIns = NULL;

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        // Debug: Component loading notification
#ifdef _DEBUG
#ifdef _DEBUG
#endif
        // Also try creating a file to verify this code runs
        FILE* f = fopen("c:\\temp\\artwork_debug.txt", "a");
        if (f) {
            fprintf(f, "DLL loaded at %s\n", __TIMESTAMP__);
            fclose(f);
        }
#endif
        g_hIns = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    }
    case DLL_PROCESS_DETACH:
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
        break;
    }
    return TRUE;
}

// Component version declaration using the proper SDK macro
#ifdef COLUMNS_UI_AVAILABLE
DECLARE_COMPONENT_VERSION(
    "Artwork Display",
    "1.5.29",
    "Cover artwork display component for foobar2000.\n"
    "Features:\n"
    "- Local artwork search (Cover.jpg, folder.jpg, etc.)\n"
    "- Online API fallback (iTunes, Discogs, Last.fm)\n"
    "- Smart metadata cleaning for better API results\n"
    "- Configurable preferences\n"
    "- Columns UI panel support\n"
    "- On-screen display for artwork source\n\n"
    "Author: jame25\n"
    "Build date: " __DATE__ "\n\n"
    "This component displays cover artwork for the currently playing track.\n"
    "Includes Columns UI panel integration."
);
#else
DECLARE_COMPONENT_VERSION(
    "Artwork Display",
    "1.5.29",
    "Cover artwork display component for foobar2000.\n"
    "Features:\n"
    "- Local artwork search (Cover.jpg, folder.jpg, etc.)\n"
    "- Online API fallback (iTunes, Discogs, Last.fm)\n"
    "- Smart metadata cleaning for better API results\n"
    "- Configurable preferences\n"
    "- On-screen display for artwork source\n\n"
    "Author: jame25\n"
    "Build date: " __DATE__ "\n\n"
    "This component displays cover artwork for the currently playing track."
);
#endif

// Validate component compatibility using the proper SDK macro
VALIDATE_COMPONENT_FILENAME("foo_artwork.dll");

// Old artwork_manager class removed - now using async version from artwork_manager.h

// Global list to track artwork UI elements
static pfc::list_t<artwork_ui_element*> g_artwork_ui_elements;

// Note: g_current_artwork_source already declared above

// Function to create AppData directory structure for logos
void create_appdata_directories() {
    try {
        // Get foobar2000 profile path (returns file:// URL)
        pfc::string8 profile_url = core_api::get_profile_path();
        
        // Convert file:// URL to filesystem path
        pfc::string8 profile_path;
        if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
            // Remove "file://" prefix and convert to Windows path
            profile_path = profile_url.c_str() + 7; // Skip "file://"
            // Replace forward slashes with backslashes for Windows
            for (size_t i = 0; i < profile_path.length(); i++) {
                if (profile_path[i] == '/') {
                    profile_path.set_char(i, '\\');
                }
            }
        } else {
            profile_path = profile_url;
        }
        
        // Create foo_artwork_data directory
        pfc::string8 artwork_data_dir = profile_path + "\\foo_artwork_data\\";
        
        // Create logos subdirectory
        pfc::string8 logos_dir = artwork_data_dir + "logos\\";
        
        // Create directories (SHCreateDirectoryEx creates parent directories if needed)
        HRESULT hr1 = SHCreateDirectoryExA(NULL, artwork_data_dir.get_ptr(), NULL);
        HRESULT hr2 = SHCreateDirectoryExA(NULL, logos_dir.get_ptr(), NULL);
        
        // Directory creation completed silently
        // (Console messages removed to reduce noise)
        
    } catch (const std::exception& e) {
    } catch (...) {
    }
}

// Function to extract domain from stream URL for logo matching
pfc::string8 extract_domain_from_stream_url(metadb_handle_ptr track) {
    // CRASH FIX: Add comprehensive safety checks
    try {
        if (!track.is_valid()) return "";
        
        pfc::string8 path = track->get_path();
        
        // Safety: Check path length to prevent buffer overruns
        if (path.is_empty() || path.length() > 2048) return "";
        
        // Check if it's an internet stream
        if (!(strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"))) {
            return "";
        }
        
        // Extract domain from URL (e.g., "http://somafm.com/stream" -> "somafm.com")
        const char* start = strstr(path.c_str(), "://");
        if (!start) return "";
        start += 3; // Skip "://"
        
        // Safety: Ensure we don't go past end of string
        if (start >= path.c_str() + path.length()) return "";
        
        const char* end = strchr(start, '/');
        if (!end) end = start + strlen(start);
        
        const char* port = strchr(start, ':');
        if (port && port < end) end = port;
        
        // Safety: Validate domain length
        if (end > start && (end - start) > 0 && (end - start) < 256) {
            return pfc::string8(start, end - start);
        }
        
        return "";
        
    } catch (...) {
        // Any exception in domain extraction should not crash the player
        return "";
    }
}

// Function to extract full host+path from stream URL for specific logo matching
// e.g., "https://ice1.somafm.com/indiepop-128-aac" -> "https---ice1.somafm.com-indiepop-128-aac"
pfc::string8 extract_full_path_from_stream_url(metadb_handle_ptr track) {
    if (!track.is_valid()) return "";
    
    pfc::string8 path = track->get_path();
    
            // Check mtag file internet streams
        const double length = track->get_length();
        if (strstr(path.c_str(), "://")) {
            // Has protocol - check if it's a local file protocol and is mtag without duration
            if ((strstr(path.c_str(), "file://") == path.c_str()) && (!strstr(path.c_str(), ".tags")) && (!length <= 0)) {
                return "";
            }
        
        // Any other protocol (http://, https://, etc.) = internet stream - continue processing
    } else {
        // No protocol - check length as fallback for other stream types
        double length = track->get_length();
        if (length > 0) {
            return ""; // Has length but no protocol = local file
        }
    }
    
    // Replace illegal characters for filename compatibility
    //$replace(%path%,/,-,\-,|,-,:,-,*,x,",'',<,_,>_,?,_)
    //$replace(%path%,$char(47),$char(45),$char(92),$char(45),$char(448),$char(45),$char(58),$char(45),$char(42),$char(140),$char(34),$char(39)$char(39),$char(60),$char(95),$char(62),$char(95),$char(63),$char(95))
    //Usable also with other artwork readers defining in artwork sources eg C:\Users\xxx\foobar2000\profile\foo_artwork_data\logos\$replace(%path%,$char(47),$char(45),$char(92),$char(45),$char(448),$char(45),$char(58),$char(45),$char(42),$char(140),$char(34),$char(39)$char(39),$char(60),$char(95),$char(62),$char(95),$char(63),$char(95)).*
    pfc::string8 result = path;
    for (size_t i = 0; i < result.length(); i++) {
        if (result[i] == '/') {result.set_char(i, '-');}
        else if (result[i] == '\\') {result.set_char(i, '-');} 
        else if (result[i] == '|') {result.set_char(i, '-');} 
        else if (result[i] == ':') {result.set_char(i, '-');}
        else if (result[i] == '*') {result.set_char(i, 'x');}
        else if (result[i] == '"') { result.set_char(i, '\'\''); }
        else if (result[i] == '<') { result.set_char(i, '_'); }
        else if (result[i] == '>') { result.set_char(i, '_'); }
        else if (result[i] == '?') { result.set_char(i, '_'); }

    }

    return result;
}


// Function to extract station name from metadata for logo matching
pfc::string8 extract_station_name_from_metadata(metadb_handle_ptr track) {
    if (!track.is_valid()) return "";
    
    // Get metadata
    file_info_impl info;
    if (!track->get_info(info)) return "";
    
    // Get artist and title
    const char* artist = info.meta_get("ARTIST", 0);
    const char* title = info.meta_get("TITLE", 0);
    
    // If no artist (empty or just "?") but we have a title, assume title contains station name
    if ((!artist || strlen(artist) == 0 || strcmp(artist, "?") == 0) && title && strlen(title) > 0) {
        // Look for pattern "? - StationName" or just "StationName"
        const char* dash = strstr(title, " - ");
        if (dash) {
            // Skip "? - " and return the station name part
            return pfc::string8(dash + 3);
        } else {
            // No dash, assume the whole title is the station name
            return pfc::string8(title);
        }
    }
    
    return "";
}

// CRASH FIX: Helper function to safely create GDI+ bitmap from file
HBITMAP safe_load_gdiplus_bitmap(const std::wstring& wide_path) {
    
    Gdiplus::Bitmap* bitmap = nullptr;
    try {
        // First check if file is accessible and has reasonable size
        HANDLE hFile = CreateFileW(wide_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER fileSize;
            if (GetFileSizeEx(hFile, &fileSize)) {
                // Reject files that are too large (>50MB) or too small (<100 bytes) to prevent memory issues
                if (fileSize.QuadPart > 100 && fileSize.QuadPart < 50*1024*1024) {
                    CloseHandle(hFile);
                    
                    // Now safely create bitmap
                    bitmap = new Gdiplus::Bitmap(wide_path.c_str());
                    if (bitmap) {
                        Gdiplus::Status last_status = bitmap->GetLastStatus();
                        if (last_status == Gdiplus::Ok) {
                            // Additional validation - check bitmap dimensions
                            UINT width = bitmap->GetWidth();
                            UINT height = bitmap->GetHeight();
                            if (width > 0 && height > 0 && width <= 4096 && height <= 4096) {
                                // Debug: Check GDI+ bitmap pixel format before conversion
                                Gdiplus::PixelFormat format = bitmap->GetPixelFormat();
                                
                                HBITMAP gdi_bitmap = nullptr;
                                
                                // Use alpha-preserving method for 32-bit ARGB images
                                if (format == PixelFormat32bppARGB) {
                                    // Create DIB section that preserves alpha channel
                                    HDC screen_dc = GetDC(NULL);
                                    if (screen_dc) {
                                        BITMAPINFO bmi = {};
                                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                        bmi.bmiHeader.biWidth = width;
                                        bmi.bmiHeader.biHeight = -(LONG)height; // Top-down DIB
                                        bmi.bmiHeader.biPlanes = 1;
                                        bmi.bmiHeader.biBitCount = 32;
                                        bmi.bmiHeader.biCompression = BI_RGB;
                                        
                                        void* pixel_data = nullptr;
                                        gdi_bitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pixel_data, NULL, 0);
                                        
                                        if (gdi_bitmap && pixel_data) {
                                            // Lock bitmap bits and copy with alpha preservation
                                            Gdiplus::Rect rect(0, 0, width, height);
                                            Gdiplus::BitmapData bitmapData;
                                            if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok) {
                                                // Calculate stride for DIB section
                                                int stride = ((width * 32 + 31) / 32) * 4;
                                                
                                                // Copy pixel data row by row to preserve alpha
                                                BYTE* src = (BYTE*)bitmapData.Scan0;
                                                BYTE* dst = (BYTE*)pixel_data;
                                                for (UINT y = 0; y < height; y++) {
                                                    memcpy(dst + y * stride, src + y * bitmapData.Stride, width * 4);
                                                }
                                                
                                                bitmap->UnlockBits(&bitmapData);
                                                
                                            } else {
                                                DeleteObject(gdi_bitmap);
                                                gdi_bitmap = nullptr;
                                            }
                                        }
                                        ReleaseDC(NULL, screen_dc);
                                    }
                                } else {
                                    // Use standard method for non-alpha images
                                    Gdiplus::Status status = bitmap->GetHBITMAP(NULL, &gdi_bitmap);
                                    if (status != Gdiplus::Ok) {
                                        gdi_bitmap = nullptr;
                                    }
                                }
                                
                                if (gdi_bitmap) {
                                    // Debug: Check resulting HBITMAP
                                    BITMAP bm;
                                    GetObject(gdi_bitmap, sizeof(BITMAP), &bm);
                                    delete bitmap;
                                    return gdi_bitmap;
                                }
                            }
                        }
                    }
                } else {
                    CloseHandle(hFile);
                }
            } else {
                CloseHandle(hFile);
            }
        }
    } catch (...) {
        // Handle any GDI+ or file system exceptions
    }
    
    // Always clean up bitmap pointer on failure
    if (bitmap) {
        delete bitmap;
        bitmap = nullptr;
    }
    
    return NULL;
}

// Helper function to try loading a logo with a specific identifier
HBITMAP try_load_station_logo(const pfc::string8& identifier, const pfc::string8& logos_dir) {
    
    if (identifier.is_empty()) return NULL;
    
    // Simple safe version without __try (to avoid object unwinding issues)
    try {
        // Try common image extensions
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        for (const char* ext : extensions) {
            pfc::string8 logo_path = logos_dir + identifier + ext;
            
            // Additional safety check for path length
            if (logo_path.length() > MAX_PATH - 1) {
                continue; // Skip if path is too long
            }
            
            if (PathFileExistsA(logo_path.get_ptr())) {
                // Use Win32 API to load image instead of GDI+ to avoid crashes
                std::wstring wide_path;
                wide_path.resize(logo_path.length() + 1);
                int result = MultiByteToWideChar(CP_UTF8, 0, logo_path.c_str(), -1, &wide_path[0], (int)wide_path.size());
                if (result == 0) {
                    continue; // Skip if conversion failed
                }
                
                // For BMP files, try Win32 LoadImage first (safest)
                if (_stricmp(ext, ".bmp") == 0) {
                    HBITMAP hBitmap = (HBITMAP)LoadImageW(
                        NULL,
                        wide_path.c_str(),
                        IMAGE_BITMAP,
                        0, 0,
                        LR_LOADFROMFILE | LR_CREATEDIBSECTION
                    );
                    
                    if (hBitmap) {
                        return hBitmap;
                    }
                }
                
                // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                HBITMAP gdi_bitmap = safe_load_gdiplus_bitmap(wide_path);
                if (gdi_bitmap) {
                    return gdi_bitmap;
                }
            }
        }
    } catch (...) {
        // Handle any top-level exceptions
    }
    
    return NULL;
}

// Function to load station logo with full path fallback to domain-only
HBITMAP load_station_logo(metadb_handle_ptr track) {
    
    if (!track.is_valid()) return NULL;
    
    // Check if custom station logos are enabled
    if (!cfg_enable_custom_logos) {
        return NULL;
    }
    
    try {
        
        pfc::string8 logos_dir;
        char appdata_buffer[MAX_PATH];
        HRESULT hr = E_FAIL; // Initialize to prevent compiler warning
        
        // Use custom folder path if specified, otherwise use default
        if (!cfg_logos_folder.is_empty()) {
            logos_dir = cfg_logos_folder.get_ptr();
            // Ensure path ends with backslash
            if (!logos_dir.is_empty() && logos_dir[logos_dir.length() - 1] != '\\') {
                logos_dir += "\\";
            }
        } else {
            // CRASH FIX: Try APPDATA path first (safe in DUI), fall back to profile path if it fails (CUI compatibility)
            try {
                hr = SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_buffer);
                if (SUCCEEDED(hr)) {
                    logos_dir = pfc::string8(appdata_buffer) + "\\foobar2000-v2\\foo_artwork_data\\logos\\";
                } else {
                    throw std::exception(); // Force fallback to profile path
                }
            } catch (...) {
                // Fallback to profile path (CUI-safe)
                try {
                    pfc::string8 profile_url = core_api::get_profile_path();
                    pfc::string8 profile_path;
                    if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
                        profile_path = profile_url.c_str() + 7;
                        for (size_t i = 0; i < profile_path.length(); i++) {
                            if (profile_path[i] == '/') {
                                profile_path.set_char(i, '\\');
                            }
                        }
                    } else {
                        profile_path = profile_url;
                    }
                    logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
                } catch (...) {
                    // Ultimate fallback: use relative path
                    logos_dir = "foo_artwork_data\\logos\\";
                }
            }
        }
        
        // Try 1: Full host+path matching (most specific)
        pfc::string8 full_path = extract_full_path_from_stream_url(track);
        
        if (!full_path.is_empty()) {
            HBITMAP result = try_load_station_logo(full_path, logos_dir);
            if (result) return result;
        }
        
        // Try 2: Domain-only matching (fallback for backward compatibility)
        pfc::string8 domain = extract_domain_from_stream_url(track);
        if (!domain.is_empty()) {
            HBITMAP result = try_load_station_logo(domain, logos_dir);
            if (result) return result;
            
            // Additional check: Try relative path "foo_artwork_data\logos\" in case user put it there
            pfc::string8 relative_logos_dir = "foo_artwork_data\\logos\\";
            result = try_load_station_logo(domain, relative_logos_dir);
            if (result) return result;
        }
        
    } catch (...) {
        // Silently fail - this is just a fallback feature
    }
    
    return NULL;
}

// New function to load station logo directly as GDI+ bitmap (preserves alpha)
Gdiplus::Bitmap* try_load_station_logo_gdiplus(const pfc::string8& identifier, const pfc::string8& logos_dir) {
 
    if (identifier.is_empty()) return nullptr;
    
    try {
        // Try PNG first (most likely to have alpha)
        pfc::string8 png_path = logos_dir + identifier + ".png";
        
        if (PathFileExistsA(png_path.get_ptr())) {
            
            // Convert to wide string for GDI+
            std::wstring wide_path;
            wide_path.resize(png_path.length() + 1);
            int result = MultiByteToWideChar(CP_UTF8, 0, png_path.c_str(), -1, &wide_path[0], (int)wide_path.size());
            if (result > 0) {
                // Load directly with GDI+ to preserve alpha
                Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(wide_path.c_str());
                if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                    return bitmap;
                } else {
                    delete bitmap;
                }
            }
        }
        
        // Try other formats (fallback)
        const char* extensions[] = { ".jpg", ".jpeg", ".gif", ".bmp" };
        for (const char* ext : extensions) {
            pfc::string8 logo_path = logos_dir + identifier + ext;
            
            if (PathFileExistsA(logo_path.get_ptr())) {
                
                // Convert to wide string
                std::wstring wide_path;
                wide_path.resize(logo_path.length() + 1);
                int result = MultiByteToWideChar(CP_UTF8, 0, logo_path.c_str(), -1, &wide_path[0], (int)wide_path.size());
                if (result > 0) {
                    Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(wide_path.c_str());
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        return bitmap;
                    } else {
                        delete bitmap;
                    }
                }
            }
        }
    } catch (...) {
        // Silently handle exceptions
    }
    
    return nullptr;
}

// Function to load station logo directly as GDI+ bitmap (preserves alpha)
Gdiplus::Bitmap* load_station_logo_gdiplus(metadb_handle_ptr track) {

    // Check if custom station logos are enabled
    if (!cfg_enable_custom_logos) {
        return nullptr;
    }
    
    if (!track.is_valid()) return nullptr;
    
    // Get the directory path (using same logic as existing functions)
    pfc::string8 profile_url = core_api::get_profile_path();
    pfc::string8 profile_path;
    if (profile_url.startsWith("file://")) {
        profile_path = profile_url.c_str() + 7; // Skip "file://"
        for (size_t i = 0; i < profile_path.length(); i++) {
            if (profile_path[i] == '/') {
                profile_path.set_char(i, '\\');
            }
        }
    } else {
        profile_path = profile_url;
    }
    
    pfc::string8 logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
    
    try {
        // Get stream URL or path
        const char* url = track->get_path();
      
        if (!url || strlen(url) == 0) return nullptr;
        
        // Extract domain and full path using existing functions
        pfc::string8 full_path = extract_full_path_from_stream_url(track);
        pfc::string8 domain = extract_domain_from_stream_url(track);

        // Try full path first
        if (!full_path.is_empty()) {
            Gdiplus::Bitmap* result = try_load_station_logo_gdiplus(full_path, logos_dir);
            if (result) {
                return result;
            }
        }
        
        // Try domain fallback
        if (!domain.is_empty()) {
            Gdiplus::Bitmap* result = try_load_station_logo_gdiplus(domain, logos_dir);
            if (result) {
                return result;
            }
        }
    } catch (...) {
        // Silently handle exceptions
    }
    
    return nullptr;
}

// New function to load noart logo directly as GDI+ bitmap (preserves alpha)
std::unique_ptr<Gdiplus::Bitmap> try_load_noart_logo_gdiplus(const pfc::string8& identifier, const pfc::string8& logos_dir) {
    if (identifier.is_empty()) return nullptr;
    
    try {
        // Try PNG first (most likely to have alpha)
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        for (const char* ext : extensions) {
            pfc::string8 noart_path = logos_dir + identifier + "-noart" + ext;
            
            if (PathFileExistsA(noart_path.get_ptr())) {
                // Convert to wide string for GDI+
                std::wstring wide_path;
                wide_path.resize(noart_path.length() + 1);
                int result = MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], (int)wide_path.size());
                if (result > 0) {
                    // Load directly with GDI+ to preserve alpha
                    auto bitmap = std::make_unique<Gdiplus::Bitmap>(wide_path.c_str());
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        return bitmap;
                    }
                }
            }
        }
    } catch (...) {
        // Silently handle exceptions
    }
    
    return nullptr;
}

// Function to load noart logo directly as GDI+ bitmap (preserves alpha)
std::unique_ptr<Gdiplus::Bitmap> load_noart_logo_gdiplus(metadb_handle_ptr track) {
    // Check if custom station logos are enabled
    if (!cfg_enable_custom_logos) {
        return nullptr;
    }
    
    if (!track.is_valid()) return nullptr;
    
    // Get the directory path (using same logic as existing functions)
    pfc::string8 profile_url = core_api::get_profile_path();
    pfc::string8 profile_path;
    if (profile_url.startsWith("file://")) {
        profile_path = profile_url.c_str() + 7; // Skip "file://"
        for (size_t i = 0; i < profile_path.length(); i++) {
            if (profile_path[i] == '/') {
                profile_path.set_char(i, '\\');
            }
        }
    } else {
        profile_path = profile_url;
    }
    
    pfc::string8 logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
    
    try {
        // Extract domain and full path using existing functions
        pfc::string8 full_path = extract_full_path_from_stream_url(track);
        pfc::string8 domain = extract_domain_from_stream_url(track);
        
        // Try full path first
        if (!full_path.is_empty()) {
            auto result = try_load_noart_logo_gdiplus(full_path, logos_dir);
            if (result) {
                return result;
            }
        }
        
        // Try domain fallback
        if (!domain.is_empty()) {
            auto result = try_load_noart_logo_gdiplus(domain, logos_dir);
            if (result) {
                return result;
            }
        }
    } catch (...) {
        // Silently handle exceptions
    }
    
    return nullptr;
}

// Function to load generic noart logo directly as GDI+ bitmap (preserves alpha)
std::unique_ptr<Gdiplus::Bitmap> load_generic_noart_logo_gdiplus() {
    // Check if custom station logos are enabled
    if (!cfg_enable_custom_logos) {
        return nullptr;
    }
    
    // Get the logos directory path (using same logic as other logo functions)
    pfc::string8 logos_dir;
    
    // Use custom logos folder if configured
    if (!cfg_logos_folder.is_empty()) {
        logos_dir = cfg_logos_folder.get_ptr();
        if (!logos_dir.is_empty() && logos_dir[logos_dir.length() - 1] != '\\') {
            logos_dir += "\\";
        }
    } else {
        // Use default path
        pfc::string8 profile_url = core_api::get_profile_path();
        pfc::string8 profile_path;
        if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
            profile_path = profile_url.c_str() + 7;
            for (size_t i = 0; i < profile_path.length(); i++) {
                if (profile_path[i] == '/') {
                    profile_path.set_char(i, '\\');
                }
            }
        } else {
            profile_path = profile_url;
        }
        logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
    }
    
    try {
        // Try common image extensions for generic noart.png
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        for (const char* ext : extensions) {
            pfc::string8 noart_path = logos_dir + "noart" + ext;
            
            if (PathFileExistsA(noart_path.get_ptr())) {
                // Convert to wide string for GDI+
                std::wstring wide_path;
                wide_path.resize(noart_path.length() + 1);
                int result = MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], (int)wide_path.size());
                if (result > 0) {
                    // Load directly with GDI+ to preserve alpha
                    auto bitmap = std::make_unique<Gdiplus::Bitmap>(wide_path.c_str());
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        return bitmap;
                    }
                }
            }
        }
    } catch (...) {
        // Silently handle exceptions
    }
    
    return nullptr;
}

// Legacy function to load station logo from domain string (for backward compatibility)
HBITMAP load_station_logo(const pfc::string8& domain) {
    if (domain.is_empty()) return NULL;
    
    // Check if custom station logos are enabled
    if (!cfg_enable_custom_logos) {
        return NULL;
    }
    
    try {
        pfc::string8 logos_dir;
        char appdata_buffer[MAX_PATH];
        HRESULT hr = E_FAIL; // Initialize to prevent compiler warning
        
        // Use custom folder path if specified, otherwise use default
        if (!cfg_logos_folder.is_empty()) {
            logos_dir = cfg_logos_folder.get_ptr();
            // Ensure path ends with backslash
            if (!logos_dir.is_empty() && logos_dir[logos_dir.length() - 1] != '\\') {
                logos_dir += "\\";
            }
        } else {
            // Use default APPDATA path: %APPDATA%\foobar2000-v2\foo_artwork_data\logos\
            hr = SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_buffer);
            if (SUCCEEDED(hr)) {
                logos_dir = pfc::string8(appdata_buffer) + "\\foobar2000-v2\\foo_artwork_data\\logos\\";
            } else {
                // Fallback to old behavior if APPDATA fails
                pfc::string8 profile_url = core_api::get_profile_path();
                pfc::string8 profile_path;
                if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
                    profile_path = profile_url.c_str() + 7;
                    for (size_t i = 0; i < profile_path.length(); i++) {
                        if (profile_path[i] == '/') {
                            profile_path.set_char(i, '\\');
                        }
                    }
                } else {
                    profile_path = profile_url;
                }
                logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
            }
        }
        
        return try_load_station_logo(domain, logos_dir);
        
    } catch (...) {
        // Silently fail - this is just a fallback feature
    }
    
    return NULL;
}

// Function to load "no artwork" fallback logo with full URL path support
HBITMAP load_noart_logo(metadb_handle_ptr track) {
    if (!track.is_valid()) return NULL;
    
    try {
        // Get logos directory path
        pfc::string8 profile_url = core_api::get_profile_path();
        pfc::string8 profile_path;
        if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
            profile_path = profile_url.c_str() + 7;
            for (size_t i = 0; i < profile_path.length(); i++) {
                if (profile_path[i] == '/') {
                    profile_path.set_char(i, '\\');
                }
            }
        } else {
            profile_path = profile_url;
        }
        
        pfc::string8 logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        // Try full URL path matching first (most specific)
        pfc::string8 full_path = extract_full_path_from_stream_url(track);
        if (!full_path.is_empty()) {
            for (const char* ext : extensions) {
                pfc::string8 noart_path = logos_dir + full_path + "-noart" + ext;
                
                if (PathFileExistsA(noart_path.get_ptr())) {
                    std::wstring wide_path;
                    wide_path.resize(noart_path.length() + 1);
                    MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                    
                    // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                    HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                    if (hBitmap) {
                        return hBitmap;
                    }
                }
            }
        }
        
        // Fallback to domain-only matching (for backward compatibility)
        pfc::string8 domain = extract_domain_from_stream_url(track);
        if (!domain.is_empty()) {
            for (const char* ext : extensions) {
                pfc::string8 noart_path = logos_dir + domain + "-noart" + ext;
                
                if (PathFileExistsA(noart_path.get_ptr())) {
                    std::wstring wide_path;
                    wide_path.resize(noart_path.length() + 1);
                    MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                    
                    // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                    HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                    if (hBitmap) {
                        return hBitmap;
                    }
                }
            }
        }
    } catch (...) {
        // Silently fail - this is just a fallback feature
    }
    
    return NULL;
}

// Function to load "no artwork" fallback logo for a specific domain when artwork search fails (legacy version)
HBITMAP load_noart_logo(const pfc::string8& domain) {
    if (domain.is_empty()) return NULL;
    
    try {
        // Get foobar2000 profile path (returns file:// URL)
        pfc::string8 profile_url = core_api::get_profile_path();
        
        // Convert file:// URL to filesystem path
        pfc::string8 profile_path;
        if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
            // Remove "file://" prefix and convert to Windows path
            profile_path = profile_url.c_str() + 7; // Skip "file://"
            // Replace forward slashes with backslashes for Windows
            for (size_t i = 0; i < profile_path.length(); i++) {
                if (profile_path[i] == '/') {
                    profile_path.set_char(i, '\\');
                }
            }
        } else {
            profile_path = profile_url;
        }
        
        pfc::string8 logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
        
        // Try common image extensions with -noart suffix
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        for (const char* ext : extensions) {
            pfc::string8 noart_path = logos_dir + domain + "-noart" + ext;
            
            if (PathFileExistsA(noart_path.get_ptr())) {
                // Load the image file
                std::wstring wide_path;
                wide_path.resize(noart_path.length() + 1);
                MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                
                // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                if (hBitmap) {
                    return hBitmap;
                }
            }
        }
    } catch (...) {
        // Silently fail - this is just a fallback feature
    }
    
    return NULL;
}

// Function to load generic "no artwork" fallback image with full URL path support
HBITMAP load_generic_noart_logo(metadb_handle_ptr track) {
    try {
        // Get logos directory path
        pfc::string8 profile_url = core_api::get_profile_path();
        pfc::string8 profile_path;
        if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
            profile_path = profile_url.c_str() + 7;
            for (size_t i = 0; i < profile_path.length(); i++) {
                if (profile_path[i] == '/') {
                    profile_path.set_char(i, '\\');
                }
            }
        } else {
            profile_path = profile_url;
        }
        
        pfc::string8 logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        // Try full URL path matching first (most specific)
        if (track.is_valid()) {
            pfc::string8 full_path = extract_full_path_from_stream_url(track);
            if (!full_path.is_empty()) {
                for (const char* ext : extensions) {
                    pfc::string8 noart_path = logos_dir + full_path + "-noart" + ext;
                    
                    if (PathFileExistsA(noart_path.get_ptr())) {
                        std::wstring wide_path;
                        wide_path.resize(noart_path.length() + 1);
                        MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                        
                        // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                        HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                        if (hBitmap) {
                            return hBitmap;
                        }
                    }
                }
            }
            
            // Try domain-specific fallback
            pfc::string8 domain = extract_domain_from_stream_url(track);
            if (!domain.is_empty()) {
                for (const char* ext : extensions) {
                    pfc::string8 noart_path = logos_dir + domain + "-noart" + ext;
                    
                    if (PathFileExistsA(noart_path.get_ptr())) {
                        std::wstring wide_path;
                        wide_path.resize(noart_path.length() + 1);
                        MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                        
                        // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                        HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                        if (hBitmap) {
                            return hBitmap;
                        }
                    }
                }
            }
        }
        
        // Finally try generic noart.png (universal fallback)
        for (const char* ext : extensions) {
            pfc::string8 noart_path = logos_dir + "noart" + ext;
            
            if (PathFileExistsA(noart_path.get_ptr())) {
                std::wstring wide_path;
                wide_path.resize(noart_path.length() + 1);
                MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                
                // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                if (hBitmap) {
                    return hBitmap;
                }
            }
        }
    } catch (...) {
        // Silently fail - this is just a fallback feature
    }
    
    return NULL;
}

// Function to load generic "no artwork" fallback image for all streams (legacy version)
HBITMAP load_generic_noart_logo() {
    try {
        // Get foobar2000 profile path (returns file:// URL)
        pfc::string8 profile_url = core_api::get_profile_path();
        
        // Convert file:// URL to filesystem path
        pfc::string8 profile_path;
        if (strstr(profile_url.c_str(), "file://") == profile_url.c_str()) {
            // Remove "file://" prefix and convert to Windows path
            profile_path = profile_url.c_str() + 7; // Skip "file://"
            // Replace forward slashes with backslashes for Windows
            for (size_t i = 0; i < profile_path.length(); i++) {
                if (profile_path[i] == '/') {
                    profile_path.set_char(i, '\\');
                }
            }
        } else {
            profile_path = profile_url;
        }
        
        pfc::string8 logos_dir = profile_path + "\\foo_artwork_data\\logos\\";
        
        // Try common image extensions for generic noart file
        const char* extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp" };
        
        for (const char* ext : extensions) {
            pfc::string8 noart_path = logos_dir + "noart" + ext;
            
            if (PathFileExistsA(noart_path.get_ptr())) {
                // Load the image file
                std::wstring wide_path;
                wide_path.resize(noart_path.length() + 1);
                MultiByteToWideChar(CP_UTF8, 0, noart_path.c_str(), -1, &wide_path[0], wide_path.size());
                
                // CRASH FIX: Use safe helper function for GDI+ bitmap loading
                HBITMAP hBitmap = safe_load_gdiplus_bitmap(wide_path);
                if (hBitmap) {
                    return hBitmap;
                }
            }
        }
    } catch (...) {
        // Silently fail - this is just a fallback feature
    }
    
    return NULL;
}

// Artwork initialization handler
class artwork_init : public initquit {
private:
    ULONG_PTR m_gdiplusToken;
    
public:
    void on_init() override {
        // Debug: Initialization notification
        
        // Initialize GDI+
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
        
        // Create AppData directory structure for logos
        create_appdata_directories();
        
        // Initialize artwork component
        artwork_manager::initialize();
    }
    
    void on_quit() override {
        // Clean up artwork component
        artwork_manager::shutdown();
        
        // Shutdown GDI+
        GdiplusShutdown(m_gdiplusToken);
    }
};

// Helper function to get API search order based on priority configuration
std::vector<ApiType> get_api_search_order() {
    // NEW SIMPLE SYSTEM: Direct mapping from search position to API
    // cfg_search_order_X contains the API index for each position
    // 0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs
    std::vector<ApiType> ordered_apis(5);
    
    // Convert API indices to ApiType enum values
    auto index_to_api = [](int index) -> ApiType {
        switch (index) {
            case 0: return ApiType::iTunes;
            case 1: return ApiType::Deezer;
            case 2: return ApiType::LastFm;
            case 3: return ApiType::MusicBrainz;
            case 4: return ApiType::Discogs;
            default: return ApiType::Deezer; // Fallback
        }
    };
    
    ordered_apis[0] = index_to_api(cfg_search_order_1);  // 1st choice
    ordered_apis[1] = index_to_api(cfg_search_order_2);  // 2nd choice
    ordered_apis[2] = index_to_api(cfg_search_order_3);  // 3rd choice
    ordered_apis[3] = index_to_api(cfg_search_order_4);  // 4th choice
    ordered_apis[4] = index_to_api(cfg_search_order_5);  // 5th choice
    
    return ordered_apis;
}

// UI Element for displaying artwork - MOVED TO ui_element.cpp
/*
class artwork_ui_element : public service_impl_single_t<ui_element_instance>, public IArtworkEventListener {
public:
    void initialize_window(HWND parent) {
        if (!m_hWnd && parent) {
            // Re-initialize with parent if needed
            // Window is already created in constructor, this is just for compliance
        }
    }

private:
public:
    HWND m_hWnd;
    ui_element_config::ptr m_config;
    ui_element_instance_callback::ptr m_callback;
    HBITMAP m_artwork_bitmap;
    pfc::string8 m_status_text;
    metadb_handle_ptr m_current_track;
    pfc::string8 m_last_search_key;  // Cache key to prevent repeated searches
    pfc::string8 m_current_search_key;  // Current search in progress
    pfc::string8 m_last_search_artist;  // Cache artist metadata between API searches
    pfc::string8 m_last_search_title;   // Cache title metadata between API searches
    pfc::string8 m_artwork_source;      // Track source of current artwork (Local, iTunes, Deezer, etc.)
    pfc::string8 m_current_artwork_path; // Path to currently loaded artwork file (for flicker prevention)
    DWORD m_last_update_timestamp;  // Timestamp for debouncing updates
    DWORD m_last_search_timestamp;  // Timestamp for search cooldown
    metadb_handle_ptr m_last_update_track;  // Track handle for smart debouncing
    pfc::string8 m_last_update_content;  // Content (artist|title) for smart debouncing
    int m_current_priority_position;  // Current position in priority search chain
    
    // Thread-safe image data storage
    std::mutex m_image_data_mutex;
    std::vector<BYTE> m_pending_image_data;
    pfc::string8 m_pending_artwork_source;
    std::mutex m_artwork_found_mutex;  // Protect artwork found flag
    bool m_artwork_found;  // Track whether artwork has been found
    bool m_playback_stopped;  // Track when playback is stopped to detect initial connections
    bool m_was_playing;  // Track previous playback state for clear panel detection
    pfc::string8 m_pending_timer_artist;  // Store artist for Timer 9 delayed search
    pfc::string8 m_pending_timer_title;   // Store title for Timer 9 delayed search
    
    // Safe metadata storage from dynamic info callback
    pfc::string8 m_safe_artist;  // Artist from on_playback_dynamic_info_track
    pfc::string8 m_safe_title;   // Title from on_playback_dynamic_info_track
    std::mutex m_safe_metadata_mutex;  // Protect safe metadata access
    
    // OSD (On-Screen Display) for artwork source
    bool m_osd_visible;           // Whether OSD is currently visible
    pfc::string8 m_osd_text;      // Text to display in OSD
    DWORD m_osd_start_time;       // When OSD was shown (GetTickCount)
    int m_osd_slide_offset;       // Current slide offset for animation (0 = fully visible)
    int m_last_osd_slide_offset;  // Previous slide offset for optimized repainting
    static const int OSD_DELAY_DURATION = 1000;    // 1 second delay before animation starts
    static const int OSD_DISPLAY_DURATION = 5000;  // 5 seconds in milliseconds
    static const int OSD_SLIDE_IN_DURATION = 300;  // 0.3 seconds slide-in animation
    static const int OSD_SLIDE_OUT_DURATION = 500; // 0.5 seconds slide-out animation

public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
private:
    void paint_artwork(HDC hdc);
    void load_artwork_for_track(metadb_handle_ptr track);
    void load_artwork_for_track_with_metadata(metadb_handle_ptr track, const pfc::string8& artist, const pfc::string8& title);
    void process_downloaded_image_data();  // Process image data on main thread
    void queue_image_for_processing(const std::vector<BYTE>& image_data, const char* source = "Unknown");  // Thread-safe image queuing
    
public:
    void clear_artwork();  // Clear search state only (keep bitmap visible)
    void clear_artwork_bitmap();  // Actually clear bitmap (only for playback stop)
    void force_clear_artwork_bitmap();  // Force clear bitmap for "clear panel when not playing" option
    void load_noart_image();  // Load and display noart image when both clear panel and use noart image are enabled
    
    // Start/stop clear panel monitoring timer based on setting
    void update_clear_panel_timer() {
        if (!m_hWnd) return;
        
        if (cfg_clear_panel_when_not_playing) {
            // Start the timer if option is enabled
            SetTimer(m_hWnd, 11, 500, NULL);  // Timer ID 11, check every 0.5 seconds
        } else {
            // Stop the timer if option is disabled
            KillTimer(m_hWnd, 11);
        }
    }
    void try_fallback_images_for_stream(metadb_handle_ptr track);  // Try fallback images when no metadata
    pfc::string8 get_artwork_source() const { return m_artwork_source; }  // Get current artwork source
    void search_itunes_artwork(metadb_handle_ptr track);
    void search_itunes_background(pfc::string8 artist, pfc::string8 title);
    void search_discogs_artwork(metadb_handle_ptr track);
    void search_discogs_background(pfc::string8 artist, pfc::string8 title);
    void search_lastfm_artwork(metadb_handle_ptr track);
    void search_lastfm_background(pfc::string8 artist, pfc::string8 title);
    void search_deezer_artwork(metadb_handle_ptr track);
    void search_deezer_background(pfc::string8 artist, pfc::string8 title);
    void search_musicbrainz_artwork(metadb_handle_ptr track);
    void search_musicbrainz_background(pfc::string8 artist, pfc::string8 title);
    
    // Priority-based search
    void start_priority_search(const pfc::string8& artist, const pfc::string8& title);
    void search_next_api_in_priority(const pfc::string8& artist, const pfc::string8& title, int current_position = 0);
    
    // Local artwork search
    bool search_local_artwork();
    bool load_local_artwork_file(const pfc::string8& file_path);
    
    // Helper functions for APIs
    pfc::string8 url_encode(const pfc::string8& str);
    bool http_get_request(const pfc::string8& url, pfc::string8& response);
    bool http_get_request_binary(const pfc::string8& url, std::vector<BYTE>& data);
    bool http_get_request_with_discogs_auth(const pfc::string8& url, pfc::string8& response);
    bool http_get_request_with_user_agent(const pfc::string8& url, pfc::string8& response, const pfc::string8& user_agent);
    void extract_metadata_for_search(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title);
    void extract_metadata_from_info(const file_info& info, pfc::string8& artist, pfc::string8& title);
    void store_safe_metadata(const pfc::string8& artist, const pfc::string8& title);
    bool parse_itunes_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_discogs_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_discogs_release_details(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_lastfm_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_deezer_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_musicbrainz_response(const pfc::string8& json, std::vector<pfc::string8>& release_ids);
    bool get_coverart_from_release_id(const pfc::string8& release_id, pfc::string8& artwork_url);
    bool download_image(const pfc::string8& url, std::vector<BYTE>& data);
    bool create_bitmap_from_data(const std::vector<BYTE>& data);
    pfc::string8 clean_metadata_text(const pfc::string8& text);
    void complete_artwork_search();  // Helper to complete cache management
    bool is_station_name(const pfc::string8& artist, const pfc::string8& title);  // Station name detection helper
    
    // OSD (On-Screen Display) methods
    void show_osd(const pfc::string8& source_name);
    void update_osd_animation();
    void paint_osd(HDC hdc, const RECT& client_rect);
    
    // Safe internet stream detection to prevent crashes
    bool is_safe_internet_stream(metadb_handle_ptr track);
    
public:
    // GUID for our element
    static const GUID g_guid;
    artwork_ui_element(HWND parent, ui_element_config::ptr config, ui_element_instance_callback::ptr callback)
        : m_config(config), m_callback(callback), m_hWnd(NULL), m_artwork_bitmap(NULL), m_last_update_timestamp(0), m_last_search_timestamp(0), m_last_search_artist(""), m_last_search_title(""), m_artwork_source(""), m_artwork_found(false), m_current_priority_position(0), m_playback_stopped(true), m_was_playing(false), m_osd_visible(false), m_osd_start_time(0), m_osd_slide_offset(0) {
        
        
        // Register as main UI element for artwork sharing with CUI panels
        if (!g_main_ui_element) {
            g_main_ui_element = this;
        }
        
        // Remove jarring "No track playing" message
        // m_status_text = "No track playing";
        
        // Register a custom window class for artwork display
        static bool class_registered = false;
        if (!class_registered) {
            WNDCLASS wc = {};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = g_hIns;
            wc.lpszClassName = L"foo_artwork_window";
            wc.hbrBackground = NULL;  // No automatic background - we'll handle it in paint
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClass(&wc);
            class_registered = true;
        }
        
        // Create the window
        m_hWnd = CreateWindow(
            L"foo_artwork_window",
            L"Artwork Display",
            WS_CHILD | WS_VISIBLE,
            0, 0, 300, 300,
            parent,
            NULL,
            g_hIns,
            this);
        
        if (m_hWnd) {
            // Add to global list for tracking
            g_artwork_ui_elements.add_item(this);
            
            // Check if there's already a track playing (for resume scenarios)
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                // Schedule initial artwork load with minimal delay
                // This handles cases where foobar2000 has resumed playback state
                // but stream metadata takes time to populate
                // m_status_text = "Detected resumed playback - scheduling artwork load...";
                // InvalidateRect(m_hWnd, NULL, TRUE);
                
                // Use minimal delay for responsiveness
                SetTimer(m_hWnd, 3, 100, NULL);  // Timer ID 3, minimal delay
            } else {
                // No track playing during initialization
                
                // Set a timer to check periodically if playback starts
                SetTimer(m_hWnd, 6, 1000, NULL);  // Timer ID 6, check every 1 second
            }
            
            // Start timer to monitor playback state for clear panel functionality
            if (cfg_clear_panel_when_not_playing) {
                SetTimer(m_hWnd, 11, 500, NULL);  // Timer ID 11, check every 0.5 seconds
            }
        }
        
        // Subscribe to artwork events for OSD notifications
        ArtworkEventManager::get().subscribe(this);
    }
    
    ~artwork_ui_element() {
        // Unregister from global reference if this is the main element
        if (g_main_ui_element == this) {
            g_main_ui_element = nullptr;
        }
        
        // Remove from global list
        g_artwork_ui_elements.remove_item(this);
        
        // Clean up bitmap
        if (m_artwork_bitmap) {
            DeleteObject(m_artwork_bitmap);
            m_artwork_bitmap = NULL;
            
            // Notify event system that artwork was cleared
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_CLEARED, 
                nullptr, 
                "Component shutdown", 
                "", 
                ""
            ));
        }
        
        // Unsubscribe from artwork events
        ArtworkEventManager::get().unsubscribe(this);
        
        // Destroy window
        if (m_hWnd) {
            KillTimer(m_hWnd, 11);  // Stop playback monitoring timer
            DestroyWindow(m_hWnd);
            m_hWnd = NULL;
        }
    }
    
    // IArtworkEventListener interface implementation
    void on_artwork_event(const ArtworkEvent& event) override {
        if (!m_hWnd) return;
        
        // Only show OSD for artwork loaded events with source information
        if (event.type == ArtworkEventType::ARTWORK_LOADED && !event.source.empty()) {
            pfc::string8 osd_text = "Artwork from ";
            osd_text << event.source.c_str();
            show_osd(osd_text);
        }
    }
    
    // ui_element_instance interface implementation
    HWND get_wnd() override { return m_hWnd; }
    
    void set_configuration(ui_element_config::ptr config) override {
        m_config = config;
    }
    
    ui_element_config::ptr get_configuration() override {
        return m_config;
    }
    
    // Required virtual methods from ui_element_instance
    GUID get_guid() override {
        return g_get_guid();
    }
    
    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }
    
    void ui_colors_changed() {
        // Repaint when UI colors change (theme switching, etc.)
        if (m_hWnd) {
            InvalidateRect(m_hWnd, NULL, TRUE);
        }
    }
    
    static GUID g_get_guid() {
        // {E8F5A5A0-1234-5678-9ABC-DEF012345678}
        return { 0xe8f5a5a0, 0x1234, 0x5678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 } };
    }
    
    static void g_get_name(pfc::string_base& out) {
        out = "Artwork Display";
    }
    
    static ui_element_config::ptr g_get_default_configuration() {
        return ui_element_config::g_create_empty(g_get_guid());
    }
    
    static const char* g_get_description() {
        return "Displays cover artwork for the currently playing track with online API fallback.";
    }
    
    void notify(const GUID& p_what, t_size p_param1, const void* p_param2, t_size p_param2size) override {
        if (p_what == ui_element_notify_colors_changed || p_what == ui_element_notify_font_changed) {
            if (m_hWnd) {
                RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
            }
        }
    }
    
    // Track update method
    void update_track(metadb_handle_ptr track) {
        if (!m_hWnd) return;
        
        // TESTING: Re-enable track updates with safety checks
        try {
            if (!track.is_valid()) {
                // m_status_text = "Invalid track";
                // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
                return;
            }
            
            // FULL FUNCTIONALITY RESTORED - All systems enabled
            // m_status_text = "Full functionality restored - testing complete system";
            // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
            
            // Continue with full track update logic (remove early return)
            // Now test the complete system with all functionality
            
        } catch (...) {
            // m_status_text = "Crash caught in update_track";
            // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
            return;
        }
        
        // Smart debouncing - only update if track or content actually changed
        DWORD current_time = GetTickCount();
        bool track_changed = (track != m_last_update_track);
        
        // Extract current content for comparison
        pfc::string8 current_artist, current_title;
        extract_metadata_for_search(track, current_artist, current_title);
        pfc::string8 current_content = current_artist + "|" + current_title;
        
        bool content_changed = (current_content != m_last_update_content);
        
        // Use minimal debounce time for responsiveness  
        int debounce_time = 50; // Minimal debounce for responsiveness
        bool enough_time_passed = (current_time - m_last_update_timestamp) > debounce_time;
        
        // Detect transition from empty metadata (ads) to populated metadata (music resumes)
        bool had_empty_metadata = (m_last_update_content == "|" || m_last_update_content.is_empty());
        bool now_has_metadata = (!current_artist.is_empty() && !current_title.is_empty());
        bool metadata_resumed = (had_empty_metadata && now_has_metadata);
        
        
        // Capture old path BEFORE updating tracking variables (for stream detection later)
        pfc::string8 old_track_path;
        bool had_previous_track = false;
        if (m_last_update_track.is_valid()) {
            old_track_path = m_last_update_track->get_path();
            had_previous_track = true;
        }
        
        
        if (!track_changed && !content_changed && !enough_time_passed && !metadata_resumed) {
            // For internet radio streams, schedule a delayed retry in case metadata is delayed
            if (track.is_valid()) {
                pfc::string8 path = track->get_path();
                bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                
                if (is_internet_stream) {
                    // Schedule a delayed check for metadata updates (short delay for responsiveness)
                    SetTimer(m_hWnd, 7, 1000, NULL);  // Timer ID 7 for delayed metadata check (1 second)
                }
            }
            return;
        } else if (!track_changed && !content_changed && enough_time_passed) {
            // Also schedule delayed retry when enough time has passed but no content change
            // This handles cases where callbacks fire but metadata isn't updated yet
            if (track.is_valid()) {
                pfc::string8 path = track->get_path();
                bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                
                if (is_internet_stream) {
                    // Schedule retry with short delay for responsiveness
                    SetTimer(m_hWnd, 8, 500, NULL);  // Timer ID 8 for retry (0.5 seconds)
                }
            }
        }
        
        // DON'T update tracking variables yet - do it after stream detection logic
        // This ensures stream detection can compare old vs new correctly
        // m_last_update_track = track;
        // m_last_update_content = current_content;
        // m_last_update_timestamp = current_time;
        
        // Handle ad breaks (when metadata becomes empty) - only clear during actual ad breaks
        if (track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            
            // Only clear artwork if this is actually an ad break (empty metadata from non-empty)
            if (is_internet_stream && current_artist.is_empty() && current_title.is_empty()) {
                // Check if we transitioned from having metadata to empty (actual ad break)
                bool was_not_empty = !m_last_update_content.is_empty() && m_last_update_content != "|";
                
                // CRITICAL FIX: Don't trigger ad break logic during local-to-stream transitions
                // Check if this is a local-to-stream transition (previous track was local file)
                bool is_local_to_stream_transition = false;
                if (m_last_update_track.is_valid()) {
                    pfc::string8 old_path = m_last_update_track->get_path();
                    bool old_was_internet_stream = (strstr(old_path.c_str(), "://") && !strstr(old_path.c_str(), "file://"));
                    if (!old_was_internet_stream && is_internet_stream) {
                        is_local_to_stream_transition = true;
                    }
                }
                
                // Only process as ad break if this is NOT a local-to-stream transition
                if (!is_local_to_stream_transition) {
                    // Add additional check: only clear if we don't have artwork already loaded
                    // This prevents clearing artwork during normal metadata updates after successful loading
                    bool has_artwork_loaded = (m_artwork_bitmap != NULL);
                    
                    if (was_not_empty && !has_artwork_loaded) {
                        // DON'T clear artwork - keep existing artwork visible during transitions
                        m_status_text = "";  // Clear any status text
                        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
                    }
                    
                    // Update tracking variables before returning
                    m_last_update_track = track;
                    m_last_update_content = current_content;
                    m_last_update_timestamp = current_time;
                    return;  // Don't search during ad breaks
                }
                
            }
        }
        
        // Clear previous artwork when switching between different source types or streams
        // OR when content changes within the same internet radio stream
        if (track_changed && track.is_valid()) {
            pfc::string8 new_path = track->get_path();
            bool new_is_internet_stream = (strstr(new_path.c_str(), "://") && !strstr(new_path.c_str(), "file://"));
            
            // Station logos and noart fallbacks are now handled after metadata search fails
            // This allows metadata-based artwork to have priority
            
            bool should_clear_artwork = false;
            bool is_new_stream = false;
            
            if (m_last_update_track.is_valid()) {
                pfc::string8 old_path = m_last_update_track->get_path();
                bool old_is_internet_stream = (strstr(old_path.c_str(), "://") && !strstr(old_path.c_str(), "file://"));
                
                // NEVER clear artwork during any transitions - only when playback stops
                // Keep existing artwork for smooth transitions in all cases:
                // 1. Local file to local file transitions
                // 2. Local file to internet stream transitions  
                // 3. Internet stream to local file transitions
                // 4. Internet stream to different internet stream transitions
                should_clear_artwork = false; // Never clear during track transitions
                
                if (new_is_internet_stream && !old_is_internet_stream) {
                    // Transitioning TO internet stream from local file
                    is_new_stream = true;
                } else if (new_is_internet_stream && old_is_internet_stream && (new_path != old_path)) {
                    // Different internet stream URL - treat as new stream but don't clear
                    is_new_stream = true;
                }
            } else {
                // First track load - never clear artwork, even for first stream
                should_clear_artwork = false;
                if (new_is_internet_stream) {
                    is_new_stream = true;
                }
            }
            
            if (should_clear_artwork) {
                clear_artwork();
            }
            
            // For new internet radio streams, use minimal delay before checking metadata
            if (is_new_stream) {
                // Schedule minimal delay for new streams
                SetTimer(m_hWnd, 9, 100, NULL);  // Timer ID 9 with minimal delay
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Don't search immediately for new streams
            } else if (!new_is_internet_stream) {
                // For local files, proceed with immediate artwork search
                m_current_track = track;
                pfc::string8 fresh_artist, fresh_title;
                extract_metadata_for_search(track, fresh_artist, fresh_title);
                load_artwork_for_track_with_metadata(track, fresh_artist, fresh_title);
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Skip the normal load process since we just did it
            }
        }
        
        // Handle track changes within the same internet radio stream
        // Clear artwork when content changes within the same stream
        if (content_changed && track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            if (is_internet_stream) {
                // Check if this is a new stream connection vs track change within stream
                bool is_new_stream_in_content_changed = false;
                if (had_previous_track) {
                    bool old_is_internet_stream = (strstr(old_track_path.c_str(), "://") && !strstr(old_track_path.c_str(), "file://"));
                    
                    // New stream if transitioning TO internet stream or different stream URL
                    if (!old_is_internet_stream && is_internet_stream) {
                        is_new_stream_in_content_changed = true;  // Local file -> Internet stream
                    } else if (is_internet_stream && (path != old_track_path)) {
                        is_new_stream_in_content_changed = true;  // Different internet stream
                    }
                } else if (is_internet_stream) {
                    is_new_stream_in_content_changed = true;  // First track is internet stream
                }
                
                
                // For internet radio streams, DON'T clear artwork yet - keep old artwork until new one loads
                // This prevents flashing and provides smoother transitions
                
                // Clear search cache to force new search
                {
                    std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                    m_last_search_key = "";
                    m_current_search_key = "";
                    m_artwork_found = false;  // Reset found flag to allow new search
                }
                m_last_search_timestamp = 0;
                
                // Use minimal delay for all stream types
                m_current_track = track;
                if (is_new_stream_in_content_changed) {
                    // New stream connection: Use short delay
                    SetTimer(m_hWnd, 9, 500, NULL);
                } else {
                    // Normal track change: Use minimal delay for responsiveness
                    SetTimer(m_hWnd, 9, 100, NULL);
                }
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Skip the normal load process since Timer 9 will handle it
            }
        }
        
        // Handle metadata resume after ad breaks
        if (metadata_resumed && track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            if (is_internet_stream) {
                
                // Clear any "No artwork found" status from ad period
                m_status_text = "";
                
                // DON'T clear artwork - keep it until new artwork loads to prevent flashing
                
                // Clear search cache to bypass any cached "no artwork found" from ad period
                {
                    std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                    m_last_search_key = "";
                    m_current_search_key = "";
                    m_artwork_found = false;  // Reset found flag to allow new search
                }
                m_last_search_timestamp = 0;
                
                // For stream resumes (like restarting foobar2000), use short delay
                // This handles both ad break recoveries and stream reconnections properly
                m_current_track = track;
                
                // Set timer with minimal delay
                SetTimer(m_hWnd, 9, 500, NULL);  // Use short delay for stream resumes
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Skip the normal load process since Timer 9 will handle it
            }
        }
        
        m_current_track = track;
        
        // Check if this is a new internet radio stream that should use delay
        if (track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            // Check if this is a new stream (different from last track)
            bool is_new_stream_connection = false;
            if (m_last_update_track.is_valid()) {
                pfc::string8 old_path = m_last_update_track->get_path();
                bool old_is_internet_stream = (strstr(old_path.c_str(), "://") && !strstr(old_path.c_str(), "file://"));
                
                // New stream if transitioning TO internet stream or different stream URL
                if (!old_is_internet_stream && is_internet_stream) {
                    is_new_stream_connection = true;  // Local file -> Internet stream
                } else if (is_internet_stream && (path != old_path)) {
                    is_new_stream_connection = true;  // Different internet stream
                }
            } else if (is_internet_stream) {
                is_new_stream_connection = true;  // First track is internet stream
            }
            
            // Use minimal delay for internet streams
            if (is_internet_stream) {
                
                if (is_new_stream_connection) {
                    // Initial stream connection: Use short delay
                    SetTimer(m_hWnd, 9, 500, NULL);
                } else {
                    // Normal track changes within stream: Use minimal delay
                    SetTimer(m_hWnd, 9, 100, NULL);
                }
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Timer 9 will handle the artwork search
            }
        }
        
        
        // Pass the already-extracted metadata to avoid duplicate extraction (LOCAL FILES ONLY)
        load_artwork_for_track_with_metadata(track, current_artist, current_title);
        
        // Update tracking variables at the end
        m_last_update_track = track;
        m_last_update_content = current_content;
        m_last_update_timestamp = current_time;
    }
    
    // Track update method with direct metadata (for radio streams)
    void update_track_with_metadata(metadb_handle_ptr track, const pfc::string8& artist, const pfc::string8& title) {
        
        if (!m_hWnd) {
            return;
        }
        
        // Create content string for comparison
        pfc::string8 current_content = artist + "|" + title;
        
        // Smart debouncing - only update if content actually changed
        DWORD current_time = GetTickCount();
        bool content_changed = (current_content != m_last_update_content);
        // Use minimal debounce time for responsiveness
        int debounce_time = 100; // Minimal debounce for responsiveness
        bool enough_time_passed = (current_time - m_last_update_timestamp) > debounce_time;
        
        
        // Skip if no changes and not enough time passed
        if (!content_changed && !enough_time_passed) {
            return;
        }
        
        
        // Check if this looks like a stream name rather than actual track metadata
        if (track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            if (is_internet_stream && artist.is_empty() && !title.is_empty()) {
                // Single title with no artist on internet stream - likely a station name
                // Check for common station name patterns
                bool looks_like_station_name = false;
                
                // Pattern 1: Ends with exclamation mark
                if (title.get_ptr()[title.get_length() - 1] == '!') {
                    looks_like_station_name = true;
                }
                
                // Pattern 2: Contains radio/stream keywords
                const char* station_keywords[] = {"Radio", "FM", "Stream", "Station", "Music", "Rocks", "Hits", "Channel"};
                for (int i = 0; i < 8; i++) {
                    if (strstr(title.c_str(), station_keywords[i])) {
                        looks_like_station_name = true;
                        break;
                    }
                }
                
                // Pattern 3: All caps or mostly caps
                if (title.get_length() > 3) {
                    int caps_count = 0;
                    int letter_count = 0;
                    for (t_size i = 0; i < title.get_length(); i++) {
                        char c = title.get_ptr()[i];
                        if (isalpha(c)) {
                            letter_count++;
                            if (isupper(c)) caps_count++;
                        }
                    }
                    if (letter_count > 0 && (caps_count * 100 / letter_count) > 70) {
                        looks_like_station_name = true;
                    }
                }
                
                if (looks_like_station_name) {
                    // Schedule brief delay to wait for real track metadata
                    SetTimer(m_hWnd, 9, 1000, NULL);
                    
                    // Update tracking variables
                    m_last_update_track = track;
                    m_last_update_content = current_content;
                    m_last_update_timestamp = current_time;
                    return;
                }
            }
        }
        
        // For internet radio streams, use minimal delay
        if (track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            if (is_internet_stream && !artist.is_empty() && !title.is_empty()) {
                // Check if this is initial connection from stopped state
                bool is_initial_connection = m_playback_stopped || 
                                           !m_last_update_track.is_valid() ||
                                           m_last_update_content.is_empty();
                
                if (is_initial_connection) {
                    // Store metadata for Timer 9 to use later
                    m_pending_timer_artist = artist;
                    m_pending_timer_title = title;
                    
                    // Use minimal delay for initial connection
                    SetTimer(m_hWnd, 9, 500, NULL);
                    m_playback_stopped = false;  // Clear the stopped flag
                } else {
                    // For track changes within the same stream, search immediately
                    m_current_track = track;
                    load_artwork_for_track_with_metadata(track, artist, title);
                }
                
                // Update tracking variables
                m_last_update_track = track;
                m_last_update_content = current_content; 
                m_last_update_timestamp = current_time;
                return;
            }
        }
        
        // For local files, search immediately
        m_current_track = track;
        m_playback_stopped = false;  // Clear stopped flag for local files too
        load_artwork_for_track_with_metadata(track, artist, title);
        
        // Update tracking variables
        m_last_update_track = track;
        m_last_update_content = current_content;
        m_last_update_timestamp = current_time;
    }
};
*/

// const GUID artwork_ui_element::g_guid = artwork_ui_element::g_get_guid();

// Function to get artwork bitmap from main component (for CUI panels) - STUB for new implementation
HBITMAP get_main_component_artwork_bitmap() {
    // Note: New DUI implementation handles this differently
    // CUI panels will use their own custom logo loading system
    return nullptr;
}

// Function to get artwork source from main component (for DUI panels)
std::string get_main_component_artwork_source() {
    // This will be implemented directly in ui_element.cpp to avoid circular dependencies
    return "";
}

// Forward declarations for standalone search (legacy)
void standalone_deezer_search(metadb_handle_ptr track);
void standalone_deezer_search_with_metadata(const std::string& artist, const std::string& title);

// Bridge functions that call main component API methods safely
bool bridge_search_deezer(const std::string& artist, const std::string& title);
bool bridge_search_discogs(const std::string& artist, const std::string& title);
bool bridge_search_itunes(const std::string& artist, const std::string& title);
bool bridge_search_lastfm(const std::string& artist, const std::string& title);
bool bridge_search_musicbrainz(const std::string& artist, const std::string& title);

// Bridge helper functions
bool load_local_artwork_into_shared_bitmap(const pfc::string8& file_path);
bool parse_deezer_json_response(const std::string& json, std::string& artwork_url);
bool parse_itunes_json_response(const std::string& json, std::string& artwork_url);
bool parse_lastfm_json_response(const std::string& json, std::string& artwork_url);
bool parse_musicbrainz_json_response(const std::string& json, std::string& release_mbid);
bool parse_discogs_json_response(const std::string& json, std::string& artwork_url);
bool create_bitmap_from_image_data(const std::vector<BYTE>& data);
bool bridge_http_get_request(const std::string& url, std::string& response);
bool bridge_http_get_request_with_useragent(const std::string& url, std::string& response, const std::string& user_agent);
bool bridge_download_image(const std::string& url, std::vector<BYTE>& data);

// External wrapper functions for event manager (for use by CUI panels)
ArtworkEventManager& get_artwork_event_manager() {
    return ArtworkEventManager::get();
}

void subscribe_to_artwork_events(IArtworkEventListener* listener) {
    ArtworkEventManager::get().subscribe(listener);
}

void unsubscribe_from_artwork_events(IArtworkEventListener* listener) {
    ArtworkEventManager::get().unsubscribe(listener);
}

// Function to trigger main component search from CUI panels with metadata
void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title) {
	
    // Use artwork manager directly instead of bridge functions
    g_artwork_loading = true;
    
    // Call artwork manager from main thread
    auto callback = [](const artwork_manager::artwork_result& result) {
        g_artwork_loading = false;
        
        if (result.success && result.data.get_size() > 0) {
            
            // Store the artwork source
            g_current_artwork_source = result.source.c_str();
            
            // Convert pfc::array_t<t_uint8> to std::vector<BYTE> for existing bitmap creation function
            std::vector<BYTE> image_data;
            image_data.resize(result.data.get_size());
            memcpy(image_data.data(), result.data.get_ptr(), result.data.get_size());
            
            // Use the existing bitmap creation function that handles all UI updates
            if (create_bitmap_from_image_data(image_data)) {
                // Clear the path since this is online artwork
                g_current_artwork_path.clear();
            }
        } else {
            // Notify event system that artwork search failed (for DUI/CUI panels)
            std::string error_source = result.source.is_empty() ? "API search failed" : result.source.c_str();
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_FAILED, 
                nullptr, 
                error_source, 
                "", 
                ""
            ));
        }
    };
    
    // Convert to pfc::string8 and call artwork manager
    pfc::string8 pfc_artist = artist.c_str();
    pfc::string8 pfc_title = title.c_str();
    
    artwork_manager::get_artwork_async_with_metadata(pfc_artist, pfc_title, callback);
}

// Legacy function (kept for compatibility)
void trigger_main_component_search(metadb_handle_ptr track) {
    // Always use consistent bridge-based search for API preference respect
    if (track.is_valid()) {
        g_artwork_loading = true;
        
        // Extract metadata and use priority search instead of hardcoded Deezer
        std::thread([track]() {
            try {
                // Extract metadata (replicate the main component's logic)
                const file_info& info = track->get_info_ref()->info();
                std::string artist, title;
                
                if (info.meta_get("ARTIST", 0)) {
                    artist = info.meta_get("ARTIST", 0);
                }
                if (info.meta_get("TITLE", 0)) {
                    title = info.meta_get("TITLE", 0);
                }

                // Use priority search with extracted metadata
                trigger_main_component_search_with_metadata(artist, title);
                
            } catch (...) {
                g_artwork_loading = false;
            }
        }).detach();
    }
}

// Function to trigger LOCAL artwork search specifically (for CUI panel priority)
void trigger_main_component_local_search(metadb_handle_ptr track) {
    
    if (!track.is_valid()) {
        return;
    }
    
    // Note: This function is called by Default UI element
    // The actual artwork search is now handled directly in ui_element.cpp
    // This function remains for compatibility with CUI panel
}

// Helper function commented out due to corruption
/*
bool load_local_artwork_into_shared_bitmap(const pfc::string8& file_path) {
        int file_count = 0;
        do {
            file_count++;
            
            // Skip directories
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            
            // Check if file has image extension
            const char* filename = findData.cFileName;
            const char* ext = strrchr(filename, '.');
            if (ext) {
                
                if (_stricmp(ext, ".jpg") == 0 || 
                    _stricmp(ext, ".jpeg") == 0 || 
                    _stricmp(ext, ".png") == 0) {
                    
                    pfc::string8 full_path = directory;
                    full_path << "\\" << filename;
                    
                    // Try to load the file directly into shared bitmap
                    if (load_local_artwork_into_shared_bitmap(full_path)) {
                        FindClose(hFind);
                        return;
                    }
                }
            }
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
    }

}

// Helper function to load local artwork into shared bitmap for CUI panel
bool load_local_artwork_into_shared_bitmap(const pfc::string8& file_path) {
    try {
        // Open file
        HANDLE hFile = CreateFileA(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        DWORD file_size = GetFileSize(hFile, NULL);
        if (file_size == 0 || file_size > 10 * 1024 * 1024) {  // Max 10MB
            CloseHandle(hFile);
            return false;
        }
        
        // Read file data
        std::vector<BYTE> file_data(file_size);
        DWORD bytes_read = 0;
        if (!ReadFile(hFile, file_data.data(), file_size, &bytes_read, NULL) || bytes_read != file_size) {
            CloseHandle(hFile);
            return false;
        }
        CloseHandle(hFile);
        
        // Create GDI+ bitmap from file data
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, file_size);
        if (!hGlobal) return false;
        
        void* pBuffer = GlobalLock(hGlobal);
        if (!pBuffer) {
            GlobalFree(hGlobal);
            return false;
        }
        
        memcpy(pBuffer, file_data.data(), file_size);
        GlobalUnlock(hGlobal);
        
        IStream* pStream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hGlobal, TRUE, &pStream))) {
            GlobalFree(hGlobal);
            return false;
        }
        
        Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(pStream);
        pStream->Release();
        
        if (!pBitmap || pBitmap->GetLastStatus() != Gdiplus::Ok) {
            if (pBitmap) delete pBitmap;
            return false;
        }
        
        // Convert to HBITMAP
        HBITMAP hBitmap = NULL;
        if (pBitmap->GetHBITMAP(NULL, &hBitmap) == Gdiplus::Ok) {
            // Clean up old shared bitmap
            if (g_shared_artwork_bitmap) {
                DeleteObject(g_shared_artwork_bitmap);
            }
            
            // Set new shared bitmap
            g_shared_artwork_bitmap = hBitmap;
            
            // Notify event system that local artwork was loaded
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_LOADED, 
                hBitmap, 
                "Local file", 
                "", 
                ""
            ));
#ifdef _DEBUG
#endif
            
            delete pBitmap;
            return true;
        }
        
        delete pBitmap;
        return false;
        
    } catch (...) {
        return false;
    }
}
*/

#ifdef COLUMNS_UI_AVAILABLE
extern void update_all_cui_clear_panel_timers();
#endif

// Global function to update clear panel timers for all UI elements (stub for new implementation)
void update_all_clear_panel_timers() {
    // Update CUI elements only (DUI elements handle this internally now)
#ifdef COLUMNS_UI_AVAILABLE
    update_all_cui_clear_panel_timers();
#endif
}

// ALL ARTWORK_UI_ELEMENT METHODS MOVED TO ui_element.cpp
/*
// Window procedure
LRESULT CALLBACK artwork_ui_element::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    artwork_ui_element* pThis = nullptr;
    
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (artwork_ui_element*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (artwork_ui_element*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    
    if (!pThis) {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        pThis->paint_artwork(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // We handle background in WM_PAINT
    case WM_SIZE:
        // Use RedrawWindow for flicker-free resizing instead of InvalidateRect
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        return 0;
    case WM_USER + 1: // Artwork found
        // Use RedrawWindow for smoother artwork updates
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        return 0;
    // OLD MESSAGE HANDLERS REMOVED - Now using priority-based search system
    // OLD WM_USER + 7 HANDLER REMOVED - Now using priority-based search system
    case WM_USER + 6: // Create bitmap from downloaded data
        pThis->process_downloaded_image_data();
        return 0;
    case WM_TIMER:
        // ESSENTIAL TIMER SYSTEM - Only timers needed for artwork functionality
        if (wParam == 9) {  // Timer ID 9 - Essential for radio stream artwork
            KillTimer(hwnd, 9);  // Kill the timer
            
            try {
                if (pThis) {
                    
                    static_api_ptr_t<playback_control> pc;
                    metadb_handle_ptr current_track;
                    if (pc->get_now_playing(current_track)) {
                        // Use stored metadata from when timer was set
                        pfc::string8 stream_artist = pThis->m_pending_timer_artist;
                        pfc::string8 stream_title = pThis->m_pending_timer_title;
                        
                        if (!stream_title.is_empty()) {
                            // Essential: Call API search directly with stored metadata (bypass broken redirect)
                            pThis->m_current_track = current_track;
                            pThis->m_last_search_artist = stream_artist;
                            pThis->m_last_search_title = stream_title;
                            pThis->search_next_api_in_priority(stream_artist, stream_title, 0);
                        } else {
                            // Try fallback for streams without metadata
                            pThis->try_fallback_images_for_stream(current_track);
                        }
                    }
                }
            } catch (...) {
                // Silently handle any issues
            }
        } else if (wParam == 10) {  // Timer ID 10 - OSD animation
            try {
                if (pThis) {
                    pThis->update_osd_animation();
                }
            } catch (...) {
                // Silently handle any issues
            }
        } else {
            // ALL OTHER TIMERS DISABLED - Kill any timer that fires
            KillTimer(hwnd, wParam);
        }
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// Paint artwork
void artwork_ui_element::paint_artwork(HDC hdc) {
    // TESTING COMPLEX PAINTING - Re-enabling bitmap operations
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    
    try {
        // Create memory DC and bitmap for double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        if (!memDC) {
            // Fallback to direct painting if memory DC fails
            COLORREF bg_color = GetSysColor(COLOR_WINDOW);  // Use proper window background
            
            // Check if system is in dark mode by examining window background brightness
            BYTE r = GetRValue(bg_color);
            BYTE g = GetGValue(bg_color);
            BYTE b = GetBValue(bg_color);
            int brightness = (r + g + b) / 3;
            
            // If system colors are light but we're in foobar2000 dark mode, use darker fallback
            if (brightness > 200) {
                bg_color = RGB(32, 32, 32);  // Dark gray fallback
            }
            
            // Try to get foobar2000's actual background color
            if (m_callback.is_valid()) {
                t_ui_color temp_color;
                
                // Try dark mode color first
                if (m_callback->query_color(ui_color_darkmode, temp_color)) {
                    bg_color = temp_color;
                } else if (m_callback->query_color(ui_color_background, temp_color)) {
                    bg_color = temp_color;
                } else {
                    bg_color = m_callback->query_std_color(ui_color_background);
                }
            }
            
            HBRUSH bgBrush = CreateSolidBrush(bg_color);
            if (bgBrush) {
                FillRect(hdc, &clientRect, bgBrush);
                DeleteObject(bgBrush);
            }
            return;
        }
        
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        if (!memBitmap) {
            DeleteDC(memDC);
            return;
        }
        
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        // Use actual foobar2000 UI colors instead of system colors
        COLORREF bg_color = GetSysColor(COLOR_WINDOW);  // Use proper window background
        COLORREF text_color = GetSysColor(COLOR_WINDOWTEXT);  // Use proper window text
        
        // Check if system is in dark mode by examining window background brightness
        BYTE r = GetRValue(bg_color);
        BYTE g = GetGValue(bg_color);
        BYTE b = GetBValue(bg_color);
        int brightness = (r + g + b) / 3;
        
        // If system colors are light but we're in foobar2000 dark mode, use darker fallback
        if (brightness > 200) {
            bg_color = RGB(32, 32, 32);  // Dark gray fallback
            text_color = RGB(220, 220, 220);  // Light text
        }
        
        // Try to get foobar2000's actual background color
        if (m_callback.is_valid()) {
            t_ui_color temp_color;
            
            // Try dark mode color first
            if (m_callback->query_color(ui_color_darkmode, temp_color)) {
                bg_color = temp_color;
            } else if (m_callback->query_color(ui_color_background, temp_color)) {
                bg_color = temp_color;
            } else {
                bg_color = m_callback->query_std_color(ui_color_background);
            }
            
            text_color = m_callback->query_std_color(ui_color_text);
        }
        
        // Fill background
        HBRUSH bgBrush = CreateSolidBrush(bg_color);
        if (bgBrush) {
            FillRect(memDC, &clientRect, bgBrush);
            DeleteObject(bgBrush);
        }
        
        // TEST: Show artwork if we have any bitmap
        if (m_artwork_bitmap) {
            // Get bitmap dimensions
            BITMAP bm;
            if (GetObject(m_artwork_bitmap, sizeof(bm), &bm)) {
                // Calculate window dimensions
                int windowWidth = clientRect.right - clientRect.left;
                int windowHeight = clientRect.bottom - clientRect.top;
                
                // Avoid division by zero
                if (windowWidth > 0 && windowHeight > 0 && bm.bmWidth > 0 && bm.bmHeight > 0) {
                    // Auto-resize with aspect ratio preservation - fit to panel
                    double scaleX = (double)windowWidth / bm.bmWidth;
                    double scaleY = (double)windowHeight / bm.bmHeight;
                    double scale = (scaleX < scaleY) ? scaleX : scaleY;  // Use smaller scale to maintain aspect ratio
                    
                    // Apply reasonable scale limits to prevent extreme scaling
                    if (scale > 10.0) scale = 10.0;  // Max 10x zoom
                    if (scale < 0.05) scale = 0.05;  // Min scale for very small panels
                    
                    int scaledWidth = (int)(bm.bmWidth * scale);
                    int scaledHeight = (int)(bm.bmHeight * scale);
                    
                    // Center the image
                    int x = (windowWidth - scaledWidth) / 2;
                    int y = (windowHeight - scaledHeight) / 2;
                    
                    // Create compatible DC for artwork
                    HDC artworkDC = CreateCompatibleDC(hdc);
                    if (artworkDC) {
                        HBITMAP oldArtworkBitmap = (HBITMAP)SelectObject(artworkDC, m_artwork_bitmap);
                        
                        // Set stretch mode for better quality
                        SetStretchBltMode(memDC, HALFTONE);
                        SetBrushOrgEx(memDC, 0, 0, NULL);
                        
                        // Draw the scaled bitmap
                        StretchBlt(memDC, x, y, scaledWidth, scaledHeight, 
                                 artworkDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                        
                        // Clean up
                        SelectObject(artworkDC, oldArtworkBitmap);
                        DeleteDC(artworkDC);
                    }
                }
            }
        } else {
            // Show status text
            SetBkColor(memDC, bg_color);
            SetTextColor(memDC, text_color);
            
            // Status text drawing disabled to prevent "Artwork loading" white box
            // if (!m_status_text.is_empty()) {
            //     DrawTextA(memDC, m_status_text.c_str(), -1, &clientRect, 
            //              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            // }
            // Don't show fallback status text - causes white screen when stopping playback
            // else {
            //     const char* status = "Complex painting enabled - testing bitmap operations";
            //     DrawTextA(memDC, status, -1, &clientRect, 
            //              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            // }
        }
        
        // Paint OSD overlay on the memory DC before copying to screen
        paint_osd(memDC, clientRect);
        
        // Copy the off-screen buffer to screen
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
        
        // Cleanup
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
    } catch (...) {
        // If complex painting fails, do nothing
    }
}

// Load artwork for track
void artwork_ui_element::load_artwork_for_track(metadb_handle_ptr track) {
    if (!track.is_valid()) {
        // m_status_text = "No track";
        // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        return;
    }
    
    // Create cache key from track metadata to prevent repeated searches
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    pfc::string8 search_key = artist + "|" + title;
    
    
    // Check if we already have artwork displayed for this track
    if (m_artwork_bitmap && search_key == m_last_search_key) {
        // For internet radio streams, use shorter cache timeout since artwork might change
        // For regular files, use longer timeout since artwork is static
        pfc::string8 path = track->get_path();
        bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
        
        DWORD current_time = GetTickCount();
        DWORD cache_timeout = is_internet_stream ? (5 * 60 * 1000) : (24 * 60 * 60 * 1000); // 5 minutes vs 24 hours
        bool artwork_timeout_passed = (current_time - m_last_search_timestamp) > cache_timeout;
        
        if (!artwork_timeout_passed) {
            return;
        }
    }
    
    // Check if search is in progress or recently failed
    DWORD current_time = GetTickCount();
    
    // For internet radio streams, use shorter retry timeout since tracks change frequently
    pfc::string8 path = track->get_path();
    bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
    DWORD retry_timeout = is_internet_stream ? 10000 : 30000; // 10 seconds vs 30 seconds
    bool retry_timeout_passed = (current_time - m_last_search_timestamp) > retry_timeout;
    
    // Don't search if already in progress or recently attempted without success
    // EXCEPTION: Always force search when switching from stream to local file
    bool force_local_search = false;
    if (!is_internet_stream && m_current_track.is_valid()) {
        pfc::string8 old_path = m_current_track->get_path();
        bool old_was_stream = (strstr(old_path.c_str(), "://") && !strstr(old_path.c_str(), "file://"));
        if (old_was_stream) {
            force_local_search = true;
        }
    }
    
    if (!force_local_search && 
        (search_key == m_current_search_key || 
         (search_key == m_last_search_key && !m_artwork_bitmap && !retry_timeout_passed))) {
        return;
    }
    
    // Update search state atomically with artwork found flag
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        // Clear any stale search state
        m_current_search_key = "";
        // Mark this search as in progress
        m_current_search_key = search_key;
    }
    
    // Update search timestamp
    m_last_search_timestamp = current_time;
    
    
    // Check if this looks like a stream name rather than actual track metadata
    // This prevents searching for artwork based on station names like "Indie Pop Rocks!"
    // (path and is_internet_stream already defined above)
    
    
    if (is_internet_stream && artist.is_empty() && !title.is_empty()) {
        // Single title with no artist on internet stream - likely a station name
        // Check for common station name patterns
        bool looks_like_station_name = false;
        
        // Pattern 1: Ends with exclamation mark (e.g., "Indie Pop Rocks!")
        if (title.get_ptr()[title.get_length() - 1] == '!') {
            looks_like_station_name = true;
        }
        
        // Pattern 2: Contains radio/stream keywords
        const char* station_keywords[] = {"Radio", "FM", "Stream", "Station", "Music", "Rocks", "Hits", "Channel"};
        for (int i = 0; i < 8; i++) {
            if (strstr(title.c_str(), station_keywords[i])) {
                looks_like_station_name = true;
                break;
            }
        }
        
        // Pattern 3: All caps or mostly caps (common for station names)
        if (title.get_length() > 3) {
            int caps_count = 0;
            int letter_count = 0;
            for (t_size i = 0; i < title.get_length(); i++) {
                char c = title.get_ptr()[i];
                if (isalpha(c)) {
                    letter_count++;
                    if (isupper(c)) caps_count++;
                }
            }
            if (letter_count > 0 && (caps_count * 100 / letter_count) > 70) {
                looks_like_station_name = true;
            }
        }
        
        if (looks_like_station_name) {
            
            // Schedule brief delay to wait for real track metadata
            SetTimer(m_hWnd, 9, 1000, NULL);
            return;
        }
    }
    
    // Don't clear previous artwork here - keep it until new artwork is successfully loaded
    // This prevents white transition screens
    
    // Remove jarring "Loading artwork..." message
    // m_status_text = "Loading artwork...";
    // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
    
    // Check for embedded artwork (LOCAL FILES ONLY - never for internet streams) 
    // is_internet_stream already declared above, reuse it
    
    // Only check embedded artwork for local files, never for internet streams
    if (!is_internet_stream) {
        try {
            static_api_ptr_t<album_art_manager_v2> aam;
            auto aaer = aam->open(pfc::list_single_ref_t<metadb_handle_ptr>(track), 
                                  pfc::list_single_ref_t<GUID>(album_art_ids::cover_front), 
                                  fb2k::noAbort);
            
            try {
                auto result = aaer->query(album_art_ids::cover_front, fb2k::noAbort);
                if (result.is_valid()) {
                
                // Convert album art data to bitmap using GDI+
                auto data = result->get_ptr();
                auto size = result->get_size();
                
                std::vector<BYTE> image_data;
                image_data.resize(size);
                memcpy(image_data.data(), data, size);
                if (create_bitmap_from_data(image_data)) {
                    complete_artwork_search();
                    // Removed status text to prevent white screen when local artwork loads
                    m_artwork_source = "Local file";
                    g_current_artwork_source = "Local file";
                    return;
                }
            }
            } catch (...) {
                // Local artwork failed, continue to online search
            }
        } catch (...) {
            // Album art manager failed
        }
    }
    // For internet streams, skip embedded artwork completely and proceed to online search
    
    // Try online APIs if no local artwork found or if this is a new track
    bool should_search_online = false;
    
    if (!m_artwork_bitmap) {
        // No artwork at all - definitely search
        should_search_online = true;
    } else if (search_key != m_last_search_key) {
        // Different track - search for new artwork
        should_search_online = true;
    }
    
    if (should_search_online && track.is_valid()) {
        // This function is deprecated - use the optimized version instead
        // Extract metadata and call the optimized version
        pfc::string8 artist, title;
        extract_metadata_for_search(track, artist, title);
        load_artwork_for_track_with_metadata(track, artist, title);
        return;
    } else {
        // Not searching - mark as complete to prevent cache issues
        complete_artwork_search();
    }
    
    m_current_track = track;
}

// Optimized version that reuses already-extracted metadata
void artwork_ui_element::load_artwork_for_track_with_metadata(metadb_handle_ptr track, const pfc::string8& artist, const pfc::string8& title) {
    
    // CRASH PREVENTION: Check for bad metadata that causes crashes
    if (artist.is_empty() || title.is_empty() || artist.get_length() == 0 || title.get_length() == 0) {
        // m_status_text = "Skipping artwork - bad metadata (artist: '";
        // m_status_text += artist.c_str();
        // m_status_text += "', title: '";
        // m_status_text += title.c_str();
        // m_status_text += "')";
        // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        return; // Don't proceed with empty metadata
    }
    
    // CRASH-SAFE: This function causes crashes - use basic load_artwork_for_track instead
    // m_status_text = "Redirect: ";
    // m_status_text += artist.c_str();
    // m_status_text += " - ";
    // m_status_text += title.c_str();
    // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
    
    // Store the metadata for the basic loading function to use
    {
        std::lock_guard<std::mutex> lock(m_safe_metadata_mutex);
        m_safe_artist = artist;
        m_safe_title = title;
    }
    
    // Redirect to safer artwork loading method
    load_artwork_for_track(track);
    return;
}


// REMOVED: Broken duplicate search_itunes_artwork function

// REMOVED: Broken duplicate search_discogs_artwork function - see correct implementation below

// REMOVED: Broken duplicate search_lastfm_artwork function - see correct implementation below

// REMOVED: Broken duplicate search_itunes_background function - see correct implementation below

// REMOVED: All broken duplicate functions - see correct implementations below

// iTunes API artwork search - FIXED to use cleaned metadata
void artwork_ui_element::search_itunes_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    // Use the unified metadata extraction function that includes cleaning
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    
    // Check if we have valid metadata for search
    // For internet radio streams, allow search with just title
    if (title.is_empty()) {
        return; // Need at least a title to search
    }
    
    // Try to parse artist from title if artist is empty (common for internet radio)
    if (artist.is_empty() && !title.is_empty()) {
        const char* separator = strstr(title.c_str(), " - ");
        if (separator && separator != title.c_str()) {
            // Extract artist from "Artist - Title" format
            size_t artist_len = separator - title.c_str();
            artist = pfc::string8(title.c_str(), artist_len);
            title = pfc::string8(separator + 3);
            
            // Clean up extracted values
            // Extract only the first artist for better artwork search results
            std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
            artist = first_artist.c_str();
            artist = clean_metadata_text(artist);
            title = clean_metadata_text(title);
        }
    }
    
    // Remove jarring "Searching iTunes API..." message
    // m_status_text = "Searching iTunes API...";
    // if (m_hWnd) {
    //     InvalidateRect(m_hWnd, NULL, TRUE);
    // }
    
    
    // Perform iTunes search in background thread
    // Copy strings to avoid potential threading issues
    pfc::string8 artist_copy = artist;
    pfc::string8 title_copy = title;
    
    std::thread([this, artist_copy, title_copy]() {
        search_itunes_background(artist_copy, title_copy);
    }).detach();
}

// Discogs API artwork search - FIXED to use cleaned metadata
void artwork_ui_element::search_discogs_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    // Use the unified metadata extraction function that includes cleaning
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    
    // Check if we have valid metadata for search
    // For internet radio streams, allow search with just title
    if (title.is_empty()) {
        return; // Need at least a title to search
    }
    
    // Try to parse artist from title if artist is empty (common for internet radio)
    if (artist.is_empty() && !title.is_empty()) {
        const char* separator = strstr(title.c_str(), " - ");
        if (separator && separator != title.c_str()) {
            // Extract artist from "Artist - Title" format
            size_t artist_len = separator - title.c_str();
            artist = pfc::string8(title.c_str(), artist_len);
            title = pfc::string8(separator + 3);
            
            // Clean up extracted values
            // Extract only the first artist for better artwork search results
            std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
            artist = first_artist.c_str();
            artist = clean_metadata_text(artist);
            title = clean_metadata_text(title);
        }
    }
    
    // Remove jarring "Searching Discogs API..." message
    // m_status_text = "Searching Discogs API...";
    // if (m_hWnd) {
    //     InvalidateRect(m_hWnd, NULL, TRUE);
    // }
    
    
    // Perform Discogs search in background thread
    // Copy strings to avoid potential threading issues
    pfc::string8 artist_copy = artist;
    pfc::string8 title_copy = title;
    
    std::thread([this, artist_copy, title_copy]() {
        search_discogs_background(artist_copy, title_copy);
    }).detach();
}

// Last.fm API artwork search - FIXED to use cleaned metadata
void artwork_ui_element::search_lastfm_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    // Use the unified metadata extraction function that includes cleaning
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    
    // Check if we have valid metadata for search
    // For internet radio streams, allow search with just title
    if (title.is_empty()) {
        return; // Need at least a title to search
    }
    
    // Try to parse artist from title if artist is empty (common for internet radio)
    if (artist.is_empty() && !title.is_empty()) {
        const char* separator = strstr(title.c_str(), " - ");
        if (separator && separator != title.c_str()) {
            // Extract artist from "Artist - Title" format
            size_t artist_len = separator - title.c_str();
            artist = pfc::string8(title.c_str(), artist_len);
            title = pfc::string8(separator + 3);
            
            // Clean up extracted values
            // Extract only the first artist for better artwork search results
            std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
            artist = first_artist.c_str();
            artist = clean_metadata_text(artist);
            title = clean_metadata_text(title);
        }
    }
    
    // Remove jarring "Searching Last.fm API..." message
    // m_status_text = "Searching Last.fm API...";
    // if (m_hWnd) {
    //     InvalidateRect(m_hWnd, NULL, TRUE);
    // }
    
    
    // Perform Last.fm search in background thread
    // Copy strings to avoid potential threading issues
    pfc::string8 artist_copy = artist;
    pfc::string8 title_copy = title;
    
    std::thread([this, artist_copy, title_copy]() {
        search_lastfm_background(artist_copy, title_copy);
    }).detach();
}

// Background iTunes API search
void artwork_ui_element::search_itunes_background(pfc::string8 artist, pfc::string8 title) {
    try {
        // URL encode the search terms
        
        pfc::string8 encoded_artist = url_encode(artist);
        pfc::string8 encoded_title = url_encode(title);
        
        
        // Build iTunes search URL
        pfc::string8 search_url;
        if (artist.is_empty()) {
            search_url << "https://itunes.apple.com/search?term=" << encoded_title << "&media=music&entity=song&limit=1";
        } else {
            search_url << "https://itunes.apple.com/search?term=" << encoded_artist << "%20" << encoded_title << "&media=music&entity=song&limit=1";
        }
        
        
        // Make HTTP request
        pfc::string8 response;
        if (http_get_request(search_url, response)) {
            
            // Parse JSON response and extract artwork URL
            pfc::string8 artwork_url;
            if (parse_itunes_response(response, artwork_url)) {
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (download_image(artwork_url, image_data)) {
                    // Check if artwork was already found by another search
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        if (m_artwork_found) {
                            return; // Another search already found artwork, stop this one
                        }
                        // Mark that artwork has been found to stop other searches
                        m_artwork_found = true;
                    }
                    
                    // Queue image data for processing on main thread (safer)
                    queue_image_for_processing(image_data, "iTunes");
                    return;
                }
            } else {
            }
        } else {
        }
        
        // Failed to get artwork
        // Don't show failure message - causes white screen
        // m_status_text = "iTunes search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        // Don't show error message - causes white screen
        // m_status_text = "iTunes search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// Background Discogs API search
void artwork_ui_element::search_discogs_background(pfc::string8 artist, pfc::string8 title) {
    try {
        pfc::string8 search_terms;
        if (artist.is_empty()) {
            search_terms = title;
        } else {
            search_terms = artist + " - " + title;
        }
        pfc::string8 encoded_search = url_encode(search_terms);
        
        
        // Build Discogs search URL
        pfc::string8 search_url = "https://api.discogs.com/database/search?q=";
        search_url << encoded_search << "&type=release";
        
        // Add authentication parameters (Discogs API requires token authentication)
        if (!cfg_discogs_key.is_empty()) {
            search_url << "&token=" << url_encode(cfg_discogs_key.get_ptr());
        } else if (!cfg_discogs_consumer_key.is_empty() && !cfg_discogs_consumer_secret.is_empty()) {
            search_url << "&key=" << url_encode(cfg_discogs_consumer_key.get_ptr());
            search_url << "&secret=" << url_encode(cfg_discogs_consumer_secret.get_ptr());
        } else {
            complete_artwork_search();
            return;
        }
        
        
        // Make HTTP request
        pfc::string8 response;
        if (http_get_request(search_url, response)) {
            
            // Parse JSON response and extract artwork URL
            pfc::string8 artwork_url;
            if (parse_discogs_response(response, artwork_url)) {
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (download_image(artwork_url, image_data)) {
                    // Check if artwork was already found by another search
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        if (m_artwork_found) {
                            return; // Another search already found artwork, stop this one
                        }
                        // Mark that artwork has been found to stop other searches
                        m_artwork_found = true;
                    }
                    
                    // Queue image data for processing on main thread (safer)
                    queue_image_for_processing(image_data, "Discogs");
                    return;
                }
            } else {
            }
        } else {
        }
        
        // Failed to get artwork - mark search as completed
        complete_artwork_search();
        // Don't show failure message - causes white screen
        // m_status_text = "Discogs search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        complete_artwork_search();  // Mark cache as completed
        // Don't show error message - causes white screen
        // m_status_text = "Discogs search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// Background Last.fm API search
void artwork_ui_element::search_lastfm_background(pfc::string8 artist, pfc::string8 title) {
    try {
        if (cfg_lastfm_key.is_empty()) {
            // Don't show config message - causes white screen
            // m_status_text = "Last.fm API key not configured";
            search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
            return;
        }
        
        // Build Last.fm API URL
        pfc::string8 search_url = "https://ws.audioscrobbler.com/2.0/?method=track.getInfo";
        if (artist.is_empty()) {
            // For empty artist, use track.search method instead
            search_url = "https://ws.audioscrobbler.com/2.0/?method=track.search";
            search_url << "&track=" << url_encode(title);
        } else {
            search_url << "&artist=" << url_encode(artist);
            search_url << "&track=" << url_encode(title);
        }
        search_url << "&api_key=" << url_encode(cfg_lastfm_key.get_ptr());
        search_url << "&format=json";
        
        // Make HTTP request
        pfc::string8 response;
        if (http_get_request(search_url, response)) {
            // Parse JSON response and extract artwork URL
            pfc::string8 artwork_url;
            if (parse_lastfm_response(response, artwork_url)) {
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (download_image(artwork_url, image_data)) {
                    // Check if artwork was already found by another search
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        if (m_artwork_found) {
                            return; // Another search already found artwork, stop this one
                        }
                        // Mark that artwork has been found to stop other searches
                        m_artwork_found = true;
                    }
                    
                    // Queue image data for processing on main thread (safer)
                    queue_image_for_processing(image_data, "Last.fm");
                    return;
                }
            }
        }
        
        // Failed to get artwork
        // Don't show failure message - causes white screen
        // m_status_text = "Last.fm search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        // Don't show error message - causes white screen
        // m_status_text = "Last.fm search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// Deezer API artwork search
void artwork_ui_element::search_deezer_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    // Use the unified metadata extraction function
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    
    // Start background search
    std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
        search_deezer_background(artist, title);
    }).detach();
}

// Background Deezer API search
void artwork_ui_element::search_deezer_background(pfc::string8 artist, pfc::string8 title) {
    // Check if artwork has already been found by another search
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        if (m_artwork_found) {
            return;
        }
    }
    
    try {
        // Build Deezer search URL (search for tracks, which include album info)
        pfc::string8 search_query;
        if (artist.is_empty()) {
            search_query = title;
        } else {
            // Clean metadata for better search results
            // Extract only the first artist for better artwork search results
            std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
            pfc::string8 clean_artist = clean_metadata_text(first_artist.c_str());
            pfc::string8 clean_title = clean_metadata_text(title);
            
            // Try artist + track format for better matching
            search_query = clean_artist + " " + clean_title;
        }
        pfc::string8 encoded_search = url_encode(search_query);
        
        pfc::string8 search_url = "https://api.deezer.com/search/track?q=";
        search_url << encoded_search << "&limit=10";
        
        // Don't show search status - causes white screen
        // m_status_text = "Searching Deezer...";
        // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        
        // Make HTTP request
        pfc::string8 response;
        if (http_get_request(search_url, response)) {
            
            // Parse JSON response and extract artwork URL
            pfc::string8 artwork_url;
            if (parse_deezer_response(response, artwork_url)) {
                
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (download_image(artwork_url, image_data)) {
                    // Check if artwork was already found by another search
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        if (m_artwork_found) {
                            return; // Another search already found artwork, stop this one
                        }
                        // Mark that artwork has been found to stop other searches
                        m_artwork_found = true;
                    }
                    
                    // Queue image data for processing on main thread (safer)
                    queue_image_for_processing(image_data, "Deezer");
                    return;
                }
            }
        }
        
        // Failed to get artwork - continue fallback chain
        // Don't show failure message - causes white screen
        // m_status_text = "Deezer search failed";
        // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        // Don't show error message - causes white screen
        // m_status_text = "Deezer search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// MusicBrainz API artwork search
void artwork_ui_element::search_musicbrainz_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    // Use the unified metadata extraction function
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    
    // Start background search
    std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
        search_musicbrainz_background(artist, title);
    }).detach();
}

// Background MusicBrainz API search
void artwork_ui_element::search_musicbrainz_background(pfc::string8 artist, pfc::string8 title) {
    try {
        // MusicBrainz rate limiting: 1 request per second minimum
        static DWORD last_musicbrainz_request = 0;
        DWORD current_time = GetTickCount();
        DWORD time_since_last = current_time - last_musicbrainz_request;
        
        if (last_musicbrainz_request > 0 && time_since_last < 1000) {
            // Sleep to enforce 1-second minimum between requests
            Sleep(1000 - time_since_last);
        }
        last_musicbrainz_request = GetTickCount();
        
        // Build MusicBrainz search URL for releases
        // Format: https://musicbrainz.org/ws/2/release?query=artist:ARTIST%20AND%20recording:TITLE&fmt=json&limit=5
        pfc::string8 encoded_artist = url_encode(artist);
        pfc::string8 encoded_title = url_encode(title);
        
        pfc::string8 search_url = "https://musicbrainz.org/ws/2/release?query=";
        if (artist.is_empty()) {
            search_url << "recording:" << encoded_title;
        } else {
            search_url << "artist:" << encoded_artist << "%20AND%20recording:" << encoded_title;
        }
        search_url << "&fmt=json&limit=5";
        
        
        // Set User-Agent header as required by MusicBrainz API
        pfc::string8 response;
        if (http_get_request_with_user_agent(search_url, response, "foo_artwork/1.0.5 (https://example.com/contact)")) {
            
            // Parse JSON response and try multiple release IDs
            std::vector<pfc::string8> release_ids;
            if (parse_musicbrainz_response(response, release_ids)) {
                // Try each release ID until we find artwork
                pfc::string8 artwork_url;
                bool found_artwork = false;
                
                for (const auto& release_id : release_ids) {
                    if (get_coverart_from_release_id(release_id, artwork_url)) {
                        found_artwork = true;
                        break;
                    }
                }
                
                if (found_artwork) {
                    // Download the artwork image
                    std::vector<BYTE> image_data;
                    if (download_image(artwork_url, image_data)) {
                        // Check if artwork was already found by another search
                        {
                            std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                            if (m_artwork_found) {
                                return; // Another search already found artwork, stop this one
                            }
                            // Mark that artwork has been found to stop other searches
                            m_artwork_found = true;
                        }
                        
                        // Queue image data for processing on main thread (safer)
                        queue_image_for_processing(image_data, "MusicBrainz");
                        return;
                    }
                }
            }
        }
        
        // Failed to get artwork - continue to next API in priority
        // Don't show failure message - causes white screen
        // m_status_text = "MusicBrainz search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        // Don't show error message - causes white screen
        // m_status_text = "MusicBrainz search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// Extract metadata for search with comprehensive cleaning - ENHANCED VERSION
void artwork_ui_element::extract_metadata_for_search(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title) {
    // CALLBACK-BASED SAFE EXTRACTION: Use metadata from on_playback_dynamic_info_track callback
    // This completely avoids risky direct track access that causes crashes
    
    artist = "";
    title = "";
    
    if (!track.is_valid()) return;
    
    // Try to get safely cached metadata from callback first
    try {
        std::lock_guard<std::mutex> lock(m_safe_metadata_mutex);
        
        // Use the safely extracted metadata from the callback
        if (!m_safe_artist.is_empty() || !m_safe_title.is_empty()) {
            artist = m_safe_artist;
            title = m_safe_title;
            return; // Success with safe metadata
        }
    } catch (...) {
        // Mutex failed, continue with fallback
    }
    
    // FALLBACK: Only for local files or when callback metadata isn't available
    // Use minimal safe extraction for local files only
    bool is_likely_local = false;
    try {
        pfc::string8 path = track->get_path();
        if (!path.is_empty() && path.length() < 300) {
            const char* path_str = path.c_str();
            // Simple local file detection
            if ((path_str[1] == ':' && path_str[2] == '\\') ||  // C:\path
                (path_str[0] == 'f' && path_str[1] == 'i' && path_str[2] == 'l' && path_str[3] == 'e')) { // file://
                is_likely_local = true;
            }
        }
    } catch (...) {
        is_likely_local = false;
    }
    
    // Only try direct extraction for local files (safer)
    if (is_likely_local) {
        try {
            service_ptr_t<titleformat_object> script_artist, script_title;
            static_api_ptr_t<titleformat_compiler> compiler;
            
            compiler->compile_safe(script_artist, "[%artist%]");
            compiler->compile_safe(script_title, "[%title%]");
            
            if (script_artist.is_valid()) {
                track->format_title(NULL, artist, script_artist, NULL);
                if (artist.length() > 300) artist = ""; // Sanity check
            }
            
            if (script_title.is_valid()) {
                track->format_title(NULL, title, script_title, NULL);
                if (title.length() > 300) title = ""; // Sanity check
            }
            
            // Clean local file metadata
            if (!artist.is_empty()) {
                // Extract only the first artist for better artwork search results
                std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
                artist = first_artist.c_str();
                artist = clean_metadata_text(artist);
            }
            if (!title.is_empty()) {
                title = clean_metadata_text(title);
            }
            
        } catch (...) {
            // Even local file extraction failed, return empty
            artist = "";
            title = "";
        }
    }
    
    // For internet streams without callback metadata, return empty
    // This prevents crashes while preserving station logos and fallback functionality
}

// SAFE: Extract metadata from file_info parameter (from callback) - CRASH-FREE
void artwork_ui_element::extract_metadata_from_info(const file_info& info, pfc::string8& artist, pfc::string8& title) {
    artist = "";
    title = "";
    
    try {
        // Direct access to file_info is safe since it's provided by callback
        const char* artist_meta = info.meta_get("ARTIST", 0);
        if (artist_meta && strlen(artist_meta) > 0 && strlen(artist_meta) < 500) {
            artist = artist_meta;
        }
        
        const char* title_meta = info.meta_get("TITLE", 0);
        if (title_meta && strlen(title_meta) > 0 && strlen(title_meta) < 500) {
            title = title_meta;
        }
        
        // Clean metadata
        if (!artist.is_empty()) {
            // Extract only the first artist for better artwork search results
            std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
            artist = first_artist.c_str();
            artist = clean_metadata_text(artist);
        }
        if (!title.is_empty()) {
            title = clean_metadata_text(title);
        }
        
        // Store safely for later use
        store_safe_metadata(artist, title);
        
    } catch (...) {
        // If any operation fails, continue safely
        artist = "";
        title = "";
    }
}

// Store metadata safely for use by extract_metadata_for_search
void artwork_ui_element::store_safe_metadata(const pfc::string8& artist, const pfc::string8& title) {
    try {
        std::lock_guard<std::mutex> lock(m_safe_metadata_mutex);
        m_safe_artist = artist;
        m_safe_title = title;
    } catch (...) {
        // If mutex fails, continue without storing
    }
}


// Clean metadata text from encoding issues and unwanted content - UTF-8 SAFE VERSION
// Fixed: Replaced problematic manual byte-level parsing with regex-based approach
// This ensures proper handling of Cyrillic and other multi-byte UTF-8 characters
// Addresses CUI mode having fewer artwork hits for non-Latin scripts
pfc::string8 artwork_ui_element::clean_metadata_text(const pfc::string8& text) {
    if (!text || text.length() == 0) return "";
    
    // Convert to std::string for UTF-8 safe regex processing
    std::string str(text.c_str());
    
    // Fix common encoding issues first
    // Handle all variants of apostrophes and quotes (UTF-8 safe)
    str = std::regex_replace(str, std::regex("\xE2\x80\x98"), "'");  // Left single quotation mark
    str = std::regex_replace(str, std::regex("\xE2\x80\x99"), "'");  // Right single quotation mark
    str = std::regex_replace(str, std::regex("\xE2\x80\x9A"), "'");  // Single low-9 quotation mark
    
    // Remove timestamp patterns at the end (UTF-8 safe)
    // Pattern 1: " - MM:SS" or " - M:SS" (like " - 0:00")
    str = std::regex_replace(str, std::regex("\\s+-\\s+\\d{1,2}:\\d{2}\\s*$"), "");
    
    // Pattern 2: " - MM.SS" or " - M.SS" (like " - 0.00") - handle decimal point
    str = std::regex_replace(str, std::regex("\\s+-\\s+\\d{1,2}\\.\\d{2}\\s*$"), "");
    
    // Remove parenthetical timestamps (MM:SS) or (M:SS)
    str = std::regex_replace(str, std::regex("\\s*\\(\\d{1,2}:\\d{2}\\)\\s*"), " ");
    
    // Remove common remix/version patterns in parentheses (case insensitive, UTF-8 safe)
    str = std::regex_replace(str, std::regex("\\s*\\((?:live|acoustic|unplugged|remix|remaster|demo|instrumental|explicit|clean|radio edit|extended|single version|album version)(?:\\s+[^)]*)?\\)\\s*", std::regex_constants::icase), " ");
    
    // Remove featuring patterns in parentheses (case insensitive, UTF-8 safe)
    str = std::regex_replace(str, std::regex("\\s*\\((?:feat\\.|featuring|ft\\.|with)\\s+[^)]*\\)\\s*", std::regex_constants::icase), " ");
    
    // Remove all remaining parenthetical content (like "(Vocal Version)", etc.)
    str = std::regex_replace(str, std::regex("\\s*\\([^)]*\\)\\s*"), " ");

    // Remove all square bracket content (like "[Vocal Version]", "[Remix]", etc.)
    str = std::regex_replace(str, std::regex("\\s*\\[[^\\]]*\\]\\s*"), " ");
    
    // Clean up multiple spaces
    str = std::regex_replace(str, std::regex("\\s{2,}"), " ");
    
    // Trim leading and trailing spaces
    str = std::regex_replace(str, std::regex("^\\s+|\\s+$"), "");
    
    return pfc::string8(str.c_str());
}

// Extract metadata directly from file_info (for radio streams)

// URL encoding helper function
pfc::string8 artwork_ui_element::url_encode(const pfc::string8& str) {
    pfc::string8 encoded;
    
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.add_char(c);
        } else {
            char hex[4];
            sprintf_s(hex, "%%%02X", (unsigned char)c);
            encoded += hex;
        }
    }
    
    return encoded;
}

// HTTP GET request using WinHTTP
bool artwork_ui_element::http_get_request(const pfc::string8& url, pfc::string8& response) {
    bool success = false;
    
    // Convert URL to wide string
    pfc::stringcvt::string_wide_from_utf8 wide_url(url);
    
    // Parse URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;
    
    if (!WinHttpCrackUrl(wide_url, 0, 0, &urlComp)) {
        return false;
    }
    
    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    // Set timeouts to prevent application freezing
    // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
    
    // Connect to the server
    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Create the request
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    
    DWORD dwFlags = 0;
    if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
        dwFlags = WINHTTP_FLAG_SECURE;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           dwFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Send the request
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        
        // Receive the response
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            
            do {
                // Check for available data
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                
                if (dwSize == 0) break;
                
                // Allocate space for the buffer
                std::vector<char> buffer(dwSize + 1);
                
                // Read the data
                if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                
                // Null-terminate and append to response
                buffer[dwDownloaded] = '\0';
                response << buffer.data();
                
            } while (dwSize > 0);
            
            success = true;
        }
    }
    
    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return success;
}

// HTTP GET request with custom User-Agent header using WinHTTP
bool artwork_ui_element::http_get_request_with_user_agent(const pfc::string8& url, pfc::string8& response, const pfc::string8& user_agent) {
    response.reset();
    
    // Parse URL into components
    pfc::string8 hostname, path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool is_https = true;
    
    if (strstr(url.c_str(), "https://")) {
        const char* host_start = url.c_str() + 8; // Skip "https://"
        is_https = true;
        port = INTERNET_DEFAULT_HTTPS_PORT;
        
        const char* path_start = strchr(host_start, '/');
        if (path_start) {
            hostname = pfc::string8(host_start, path_start - host_start);
            path = path_start;
        } else {
            hostname = host_start;
            path = "/";
        }
    } else if (strstr(url.c_str(), "http://")) {
        const char* host_start = url.c_str() + 7; // Skip "http://"
        is_https = false;
        port = INTERNET_DEFAULT_HTTP_PORT;
        
        const char* path_start = strchr(host_start, '/');
        if (path_start) {
            hostname = pfc::string8(host_start, path_start - host_start);
            path = path_start;
        } else {
            hostname = host_start;
            path = "/";
        }
    } else {
        return false; // Invalid URL
    }
    
    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(pfc::stringcvt::string_wide_from_utf8(user_agent.c_str()),
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) return false;
    
    // Set timeouts to prevent application freezing
    // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
    
    // Connect to server
    HINTERNET hConnect = WinHttpConnect(hSession,
                                        pfc::stringcvt::string_wide_from_utf8(hostname.c_str()),
                                        port,
                                        0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Create request
    DWORD flags = 0;
    if (is_https) {
        flags = WINHTTP_FLAG_SECURE;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
                                            L"GET",
                                            pfc::stringcvt::string_wide_from_utf8(path.c_str()),
                                            NULL,
                                            WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Send request
    BOOL result = WinHttpSendRequest(hRequest,
                                     WINHTTP_NO_ADDITIONAL_HEADERS,
                                     0,
                                     WINHTTP_NO_REQUEST_DATA,
                                     0,
                                     0,
                                     0);
    
    bool success = false;
    if (result && WinHttpReceiveResponse(hRequest, NULL)) {
        // Read response data
        DWORD bytes_available;
        while (WinHttpQueryDataAvailable(hRequest, &bytes_available) && bytes_available > 0) {
            pfc::array_t<char> buffer;
            buffer.set_size(bytes_available);
            
            DWORD bytes_read;
            if (WinHttpReadData(hRequest, buffer.get_ptr(), bytes_available, &bytes_read)) {
                response.add_string(buffer.get_ptr(), bytes_read);
            } else {
                break;
            }
        }
        success = true;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return success;
}

// HTTP GET request for binary data (images) using WinHTTP
bool artwork_ui_element::http_get_request_binary(const pfc::string8& url, std::vector<BYTE>& data) {
    // Use per-request manager to prevent API interference without blocking other APIs
    HttpRequestManager* request_manager = get_request_manager_for_api(url.c_str());
    auto request_lock = request_manager->acquire_lock();
    
    bool success = false;
    data.clear();
    
    
    // Convert URL to wide string
    pfc::stringcvt::string_wide_from_utf8 wide_url(url);
    
    // Parse URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;
    
    if (!WinHttpCrackUrl(wide_url, 0, 0, &urlComp)) {
        return false;
    }
    
    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return false;
    }
    
    // Set shorter timeouts for image downloads to prevent hangs
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000); // 10s connect, 30s receive
    
    // Connect to the server
    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Create the request
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    
    DWORD dwFlags = 0;
    if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
        dwFlags = WINHTTP_FLAG_SECURE;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           dwFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Add headers for image requests
    const wchar_t* headers = L"Accept: image/jpeg, image/png, image/gif, image/webp, image/*\r\n"
                            L"User-Agent: foobar2000-artwork/1.0\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    // Send the request
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        
        // Receive the response
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            // Check HTTP status code
            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
            
            if (dwStatusCode == 200) {
                // Get content length if available
                DWORD dwContentLength = 0;
                dwSize = sizeof(dwContentLength);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                   WINHTTP_HEADER_NAME_BY_INDEX, &dwContentLength, &dwSize, WINHTTP_NO_HEADER_INDEX);
                
                // Limit artwork size to prevent memory issues (max 10MB)
                const DWORD MAX_ARTWORK_SIZE = 10 * 1024 * 1024;
                if (dwContentLength > MAX_ARTWORK_SIZE) {
                    WinHttpCloseHandle(hRequest);
                    WinHttpCloseHandle(hConnect);
                    WinHttpCloseHandle(hSession);
                    return false;
                }
                
                if (dwContentLength > 0) {
                    data.reserve(dwContentLength);
                } else {
                    // No content length header, reserve reasonable amount
                    data.reserve(1024 * 1024); // 1MB default
                }
                
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                
                const DWORD CHUNK_SIZE = 8192; // 8KB chunks
                BYTE buffer[CHUNK_SIZE];
                
                do {
                    // Check for available data
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    
                    if (dwSize == 0) break;
                    
                    // Limit chunk size to prevent excessive memory operations
                    DWORD chunkSize = min(dwSize, CHUNK_SIZE);
                    
                    // Read the binary data
                    if (!WinHttpReadData(hRequest, buffer, chunkSize, &dwDownloaded)) break;
                    
                    // Check size limit to prevent runaway downloads
                    if (data.size() + dwDownloaded > MAX_ARTWORK_SIZE) {
                        break;
                    }
                    
                    // Efficiently append to data vector
                    size_t old_size = data.size();
                    data.resize(old_size + dwDownloaded);
                    memcpy(data.data() + old_size, buffer, dwDownloaded);
                    
                    // Add small delay every few chunks to prevent system freeze
                    static int chunk_count = 0;
                    chunk_count++;
                    if (chunk_count % 10 == 0) {
                        Sleep(1); // 1ms yield to system every 10 chunks (~80KB)
                    }
                    
                } while (dwSize > 0);
                
                success = (data.size() > 0);
                
                
                // Basic validation - check for common image file signatures
                if (success && data.size() >= 4) {
                    bool valid_image = false;
                    
                    // Check for JPEG signature (FF D8 FF)
                    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
                        valid_image = true;
                    }
                    // Check for PNG signature (89 50 4E 47)
                    else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
                        valid_image = true;
                    }
                    // Check for GIF signature (47 49 46 38)
                    else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x38) {
                        valid_image = true;
                    }
                    // Check for WebP signature (52 49 46 46)
                    else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46) {
                        valid_image = true;
                    }
                    
                    if (!valid_image) {
                        // Don't fail here - let GDI+ handle the validation
                    }
                }
                
            } else {
            }
        } else {
        }
    } else {
    }
    
    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return success;
}

// Simple JSON parser for iTunes response
bool artwork_ui_element::parse_itunes_response(const pfc::string8& json, pfc::string8& artwork_url) {
    
    // First check if we have any results
    const char* result_count_key = "\"resultCount\":";
    const char* result_count_start = strstr(json.c_str(), result_count_key);
    if (result_count_start) {
        result_count_start += strlen(result_count_key);
        int result_count = atoi(result_count_start);
        
        
        if (result_count == 0) {
            return false;
        }
    }
    
    // Try multiple artwork URL fields in order of preference
    const char* artwork_fields[] = {
        "\"artworkUrl600\":\"",   // 600x600 (if available)
        "\"artworkUrl512\":\"",   // 512x512 (if available)
        "\"artworkUrl100\":\"",   // 100x100 (most common)
        "\"artworkUrl60\":\"",    // 60x60 (fallback)
        "\"artworkUrl30\":\""     // 30x30 (smallest)
    };
    
    for (int i = 0; i < 5; i++) {
        const char* start = strstr(json.c_str(), artwork_fields[i]);
        if (start) {
            start += strlen(artwork_fields[i]);
            const char* end = strchr(start, '"');
            if (end) {
                artwork_url = pfc::string8(start, end - start);
                
                // Upgrade any resolution to 1200x1200 using iTunes URL manipulation
                if (strstr(artwork_fields[i], "100")) {
                    artwork_url.replace_string("100x100", "1200x1200");
                } else if (strstr(artwork_fields[i], "60")) {
                    artwork_url.replace_string("60x60", "1200x1200");
                } else if (strstr(artwork_fields[i], "30")) {
                    artwork_url.replace_string("30x30", "1200x1200");
                } else if (strstr(artwork_fields[i], "600")) {
                    artwork_url.replace_string("600x600", "1200x1200");
                } else if (strstr(artwork_fields[i], "512")) {
                    artwork_url.replace_string("512x512", "1200x1200");
                }
                
                // Set compression quality: 80 for PNG files, 90 for JPEG files
                if (artwork_url.find_first(".png") != pfc_infinite) {
                    // For PNG files: add bb-80 quality parameter  
                    if (artwork_url.find_first("bb.png") != pfc_infinite) {
                        artwork_url.replace_string("bb.png", "bb-80.png");
                    } else if (artwork_url.find_first("bf.png") != pfc_infinite) {
                        artwork_url.replace_string("bf.png", "bb-80.png");
                    } else if (artwork_url.find_first("1200x1200.png") != pfc_infinite) {
                        artwork_url.replace_string("1200x1200.png", "1200x1200bb-80.png");
                    }
                } else if (artwork_url.find_first(".jpg") != pfc_infinite || artwork_url.find_first(".jpeg") != pfc_infinite) {
                    // For JPEG files: add bb-90 quality parameter for better quality
                    if (artwork_url.find_first("bb.jpg") != pfc_infinite) {
                        artwork_url.replace_string("bb.jpg", "bb-90.jpg");
                    } else if (artwork_url.find_first("bf.jpg") != pfc_infinite) {
                        artwork_url.replace_string("bf.jpg", "bb-90.jpg");
                    } else if (artwork_url.find_first("1200x1200.jpg") != pfc_infinite) {
                        artwork_url.replace_string("1200x1200.jpg", "1200x1200bb-90.jpg");
                    }
                }
                
                if (artwork_url.length() > 0) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

// Simple JSON parser for Discogs response
bool artwork_ui_element::parse_discogs_response(const pfc::string8& json, pfc::string8& artwork_url) {
    
    // Look for results array with flexible whitespace handling
    const char* results_start = strstr(json.c_str(), "\"results\"");
    if (!results_start) {
        return false;
    }
    
    // Find the opening bracket for the results array
    const char* bracket_pos = strchr(results_start, '[');
    if (!bracket_pos) {
        return false;
    }
    
    results_start = bracket_pos;
    
    
    // Loop through multiple results like the JavaScript version
    // Look for cover_image field with flexible spacing
    const char* cover_image_key = "\"cover_image\"";
    const char* current_pos = results_start;
    
    for (int result_index = 0; result_index < 5; result_index++) {
        const char* key_pos = strstr(current_pos, cover_image_key);
        if (!key_pos) {
            break; // No more cover_image fields found
        }
        
        // Find the colon and opening quote
        const char* colon_pos = strchr(key_pos, ':');
        if (!colon_pos) {
            current_pos = key_pos + strlen(cover_image_key);
            continue;
        }
        
        // Skip whitespace and find opening quote
        const char* quote_pos = strchr(colon_pos, '"');
        if (!quote_pos) {
            current_pos = colon_pos + 1;
            continue;
        }
        
        const char* start = quote_pos + 1;
        
        
        const char* end = strchr(start, '"');
        if (!end) {
            current_pos = start;
            continue; // Malformed, try next one
        }
        
        pfc::string8 potential_url = pfc::string8(start, end - start);
        
        
        // Check if this URL is valid - filter out placeholder images
        if (potential_url.length() > 0 && 
            potential_url != "" && 
            strstr(potential_url.c_str(), "http") &&
            !strstr(potential_url.c_str(), "spacer.gif") &&
            !strstr(potential_url.c_str(), "placeholder")) {
            
            artwork_url = potential_url;
            
            
            return true;
        }
        
        // Move to next potential result
        current_pos = end + 1;
    }
    
    return false;
}

// Enhanced JSON parser for Last.fm response
bool artwork_ui_element::parse_lastfm_response(const pfc::string8& json, pfc::string8& artwork_url) {
    
    // First check for API errors
    if (strstr(json.c_str(), "\"error\":")) {
        return false;
    }
    
    // Check if track exists
    if (!strstr(json.c_str(), "\"track\":")) {
        return false;
    }
    
    // Look for album images in order of preference (largest to smallest)
    const char* size_preferences[] = {
        "\"size\":\"extralarge\"",
        "\"size\":\"large\"",
        "\"size\":\"medium\"",
        "\"size\":\"small\""
    };
    
    for (int i = 0; i < 4; i++) {
        const char* size_pos = strstr(json.c_str(), size_preferences[i]);
        if (size_pos) {
            // Look backwards for the #text URL in this image object
            const char* search_start = json.c_str();
            const char* image_start = size_pos;
            
            // Find the start of this image object
            while (image_start > search_start && *image_start != '{') {
                image_start--;
            }
            
            // Look for #text URL within this image object
            const char* text_key = "\"#text\":\"";
            const char* url_start = strstr(image_start, text_key);
            if (url_start && url_start < size_pos + 50) { // Make sure it's in the same object
                url_start += strlen(text_key);
                const char* url_end = strchr(url_start, '"');
                if (url_end) {
                    pfc::string8 potential_url = pfc::string8(url_start, url_end - url_start);
                    
                    // Validate it's actually an image URL
                    if (potential_url.length() > 10 && 
                        (strstr(potential_url.c_str(), ".jpg") || 
                         strstr(potential_url.c_str(), ".png") ||
                         strstr(potential_url.c_str(), ".jpeg"))) {
                        
                        artwork_url = potential_url;
                        
                        
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

// Enhanced JSON parser for Deezer response
bool artwork_ui_element::parse_deezer_response(const pfc::string8& json, pfc::string8& artwork_url) {
    
    // Look for data array containing track results
    const char* data_start = strstr(json.c_str(), "\"data\"");
    if (!data_start) {
        return false;
    }
    
    // Find the opening bracket for the data array
    const char* bracket_pos = strchr(data_start, '[');
    if (!bracket_pos) {
        return false;
    }
    
    data_start = bracket_pos;
    
    // Loop through multiple results to find the best artwork
    const char* current_pos = data_start;
    
    for (int result_index = 0; result_index < 10; result_index++) {
        // Look for album object within each track result
        const char* album_key = strstr(current_pos, "\"album\"");
        if (!album_key) {
            break;
        }
        
        // Find the opening brace for the album object
        const char* album_brace = strchr(album_key, '{');
        if (!album_brace) {
            current_pos = album_key + 7; // Skip "album"
            continue;
        }
        
        
        // Look for cover_xl, cover_big, cover_medium, or cover fields (in order of preference)
        const char* cover_fields[] = {"\"cover_xl\"", "\"cover_big\"", "\"cover_medium\"", "\"cover\""};
        
        for (const char* cover_field : cover_fields) {
            const char* cover_pos = strstr(album_brace, cover_field);
            if (!cover_pos) continue;
            
            // Make sure this cover field is within the same album object
            const char* next_album = strstr(album_brace + 1, "\"album\"");
            if (next_album && cover_pos > next_album) continue;
            
            // Find the colon and opening quote
            const char* colon_pos = strchr(cover_pos, ':');
            if (!colon_pos) continue;
            
            // Skip whitespace and find opening quote
            const char* quote_pos = strchr(colon_pos, '"');
            if (!quote_pos) continue;
            
            const char* url_start = quote_pos + 1;
            const char* url_end = strchr(url_start, '"');
            
            if (!url_end) continue;
            
            pfc::string8 potential_url = pfc::string8(url_start, url_end - url_start);
            
            // Unescape JSON forward slashes (\/ -> /)
            potential_url.replace_string("\\/", "/");
            
            
            // Check if this URL is valid
            if (potential_url.length() > 0 && 
                potential_url != "" && 
                strstr(potential_url.c_str(), "http") &&
                !strstr(potential_url.c_str(), "placeholder") &&
                (strstr(potential_url.c_str(), ".jpg") ||
                 strstr(potential_url.c_str(), ".jpeg") ||
                 strstr(potential_url.c_str(), ".png"))) {
                
                // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
                const char* size_pos = strstr(potential_url.c_str(), "1000x1000");
                if (size_pos) {
                    pfc::string8 upgraded_url;
                    upgraded_url << pfc::string8(potential_url.c_str(), size_pos - potential_url.c_str());
                    upgraded_url << "1200x1200";
                    upgraded_url << (size_pos + 9); // Skip past "1000x1000"
                    artwork_url = upgraded_url;
                } else {
                    artwork_url = potential_url;
                }
                
                
                return true;
            }
        }
        
        // Move to next track result
        current_pos = album_brace + 1;
    }
    
    return false;
}

// JSON parser for MusicBrainz release search response
bool artwork_ui_element::parse_musicbrainz_response(const pfc::string8& json, std::vector<pfc::string8>& release_ids) {
    
    release_ids.clear();
    
    // Look for releases array
    const char* releases_start = strstr(json.c_str(), "\"releases\"");
    if (!releases_start) {
        return false;
    }
    
    // Find the opening bracket for the releases array
    const char* bracket_pos = strchr(releases_start, '[');
    if (!bracket_pos) {
        return false;
    }
    
    releases_start = bracket_pos;
    
    // Look for multiple release IDs
    const char* current_pos = releases_start;
    
    for (int release_index = 0; release_index < 5; release_index++) {
        const char* id_key = strstr(current_pos, "\"id\"");
        if (!id_key) {
            break; // No more release IDs found
        }
        
        // Find the colon and opening quote
        const char* colon_pos = strchr(id_key, ':');
        if (!colon_pos) {
            current_pos = id_key + 4;
            continue;
        }
        
        // Skip whitespace and find opening quote
        const char* quote_pos = strchr(colon_pos, '"');
        if (!quote_pos) {
            current_pos = colon_pos + 1;
            continue;
        }
        
        const char* id_start = quote_pos + 1;
        const char* id_end = strchr(id_start, '"');
        
        if (!id_end) {
            current_pos = id_start;
            continue;
        }
        
        pfc::string8 release_id = pfc::string8(id_start, id_end - id_start);
        release_ids.push_back(release_id);
        
        
        // Move to next potential release
        current_pos = id_end + 1;
    }
    
    
    return !release_ids.empty();
}

// Get cover art URL from Cover Art Archive using release ID
bool artwork_ui_element::get_coverart_from_release_id(const pfc::string8& release_id, pfc::string8& artwork_url) {
    
    // Try different artwork endpoints in order of preference
    const char* cover_types[] = { "front", "", "back" }; // "" means all available artwork
    
    for (const char* cover_type : cover_types) {
        // Build Cover Art Archive URL
        pfc::string8 coverart_url = "https://coverartarchive.org/release/";
        coverart_url << release_id;
        if (strlen(cover_type) > 0) {
            coverart_url << "/" << cover_type;
        }
        
        
        // Make HTTP request to Cover Art Archive
        pfc::string8 response;
        if (http_get_request(coverart_url, response)) {
            // Check if this is an HTML error page (404)
            if (strstr(response.c_str(), "<!doctype html>") || strstr(response.c_str(), "<html")) {
                continue; // Try next cover type
            }
            
            
            // Parse Cover Art Archive JSON response
            const char* image_key = strstr(response.c_str(), "\"image\"");
            if (!image_key) {
                continue; // Try next cover type
            }
            
            // Find the colon and opening quote
            const char* colon_pos = strchr(image_key, ':');
            if (!colon_pos) continue;
            
            // Skip whitespace and find opening quote
            const char* quote_pos = strchr(colon_pos, '"');
            if (!quote_pos) continue;
            
            const char* url_start = quote_pos + 1;
            const char* url_end = strchr(url_start, '"');
            
            if (!url_end) continue;
            
            artwork_url = pfc::string8(url_start, url_end - url_start);
            
            // Unescape JSON forward slashes if needed
            artwork_url.replace_string("\\/", "/");
            
            
            return true;
        }
        
    }
    
    return false;
}

// Download image data
bool artwork_ui_element::download_image(const pfc::string8& url, std::vector<BYTE>& data) {
    
    // Use the new binary HTTP request function for proper image download
    bool result = http_get_request_binary(url, data);
    
    
    return result;
}

// Create bitmap from image data
bool artwork_ui_element::create_bitmap_from_data(const std::vector<BYTE>& data) {
    if (data.empty()) return false;
    
    // Keep reference to old bitmap to avoid white flash
    HBITMAP old_bitmap = m_artwork_bitmap;
    // DON'T clear m_artwork_bitmap yet - keep old artwork visible until new one is ready
    
    // Create memory stream from data using fixed memory (more efficient)
    IStream* pStream = NULL;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_FIXED, data.size());
    if (!hGlobal) return false;
    
    void* pData = GlobalLock(hGlobal);
    if (!pData) {
        GlobalFree(hGlobal);
        return false;
    }
    
    // Use faster memory copy for large data
    if (data.size() > 64 * 1024) {
        // Copy in smaller chunks to prevent freeze
        const size_t COPY_CHUNK = 64 * 1024;
        const BYTE* src = data.data();
        BYTE* dst = static_cast<BYTE*>(pData);
        size_t remaining = data.size();
        
        while (remaining > 0) {
            size_t chunk_size = min(remaining, COPY_CHUNK);
            memcpy(dst, src, chunk_size);
            src += chunk_size;
            dst += chunk_size;
            remaining -= chunk_size;
            
            // Yield every 64KB to prevent system freeze
            if (remaining > 0) {
                Sleep(0);
            }
        }
    } else {
        memcpy(pData, data.data(), data.size());
    }
    
    GlobalUnlock(hGlobal);
    
    if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) != S_OK) {
        GlobalFree(hGlobal);
        return false;
    }
    
    // Create GDI+ bitmap from stream
    Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();
    
    if (!pBitmap || pBitmap->GetLastStatus() != Gdiplus::Ok) {
        if (pBitmap) delete pBitmap;
        return false;
    }
    
    // Debug: Check pixel format to see if alpha channel is present
    Gdiplus::PixelFormat format = pBitmap->GetPixelFormat();
    char debug_msg[256];
    sprintf_s(debug_msg, "SDK_MAIN DEBUG: Loaded bitmap pixel format: 0x%08X, Has Alpha: %s", 
             format, (format & PixelFormatAlpha) ? "YES" : "NO");
    console::info(debug_msg);
    
    if (format == PixelFormat32bppARGB) {
    } else if (format == PixelFormat32bppRGB) {
    } else if (format == PixelFormat24bppRGB) {
    }
    
    // Use faster DIB creation instead of GetHBITMAP to prevent freeze
    
    UINT width = pBitmap->GetWidth();
    UINT height = pBitmap->GetHeight();
    
    // Create DIB section (much faster than GetHBITMAP)
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -(int)height; // Negative for top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    
    void* pBits = NULL;
    HDC hdc = GetDC(NULL);
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    ReleaseDC(NULL, hdc);
    
    if (!hBitmap || !pBits) {
        delete pBitmap;
        m_artwork_bitmap = old_bitmap;
        return false;
    }
    
    // Lock bitmap data for direct access (faster than GetHBITMAP)
    Gdiplus::BitmapData bitmapData;
    Gdiplus::Rect rect(0, 0, width, height);
    
    if (pBitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok) {
        // Copy pixel data directly (much faster)
        memcpy(pBits, bitmapData.Scan0, width * height * 4);
        pBitmap->UnlockBits(&bitmapData);
        m_artwork_bitmap = hBitmap;
        
        // Update global variables for CUI panel communication
        // For now, we don't have a file path for online images, so we clear it
        // The CUI panel will need to get the bitmap data directly
        g_current_artwork_path.clear();
        g_artwork_loading = false;
        
        // Clear instance path since this is not local artwork (prevents flicker prevention from interfering)
        m_current_artwork_path.clear();
        
    } else {
        DeleteObject(hBitmap);
        delete pBitmap;
        m_artwork_bitmap = old_bitmap;
        return false;
    }
    
    // Only now clean up the old bitmap since new one was successfully created
    if (old_bitmap) {
        DeleteObject(old_bitmap);
    }
    
    delete pBitmap;
    
    
    // Mark artwork as found to stop any competing searches
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_artwork_found = true;
    }
    
    return true;
}

// Process downloaded image data on main thread (thread-safe)
void artwork_ui_element::process_downloaded_image_data() {
    std::vector<BYTE> image_data;
    pfc::string8 source;
    
    // Thread-safe retrieval of image data
    {
        std::lock_guard<std::mutex> lock(m_image_data_mutex);
        if (m_pending_image_data.empty()) {
            return; // No data to process
        }
        image_data = std::move(m_pending_image_data);
        source = m_pending_artwork_source;
        m_pending_image_data.clear();
        m_pending_artwork_source = "";
    }
    
    // Create bitmap from data on main thread (safe for GDI+)
    if (create_bitmap_from_data(image_data)) {
        complete_artwork_search();
        m_artwork_source = source;  // Store the source of successful artwork
        g_current_artwork_source = source;  // Update global source for logging
        // Removed status text to prevent white screen when artwork loads
        
        // Notify event system that artwork was loaded successfully
        ArtworkEventManager::get().notify(ArtworkEvent(
            ArtworkEventType::ARTWORK_LOADED, 
            m_artwork_bitmap, 
            source.c_str(), 
            m_last_search_artist.c_str(), 
            m_last_search_title.c_str()
        ));
        
        // Show OSD with artwork source
        show_osd(source);
        
        InvalidateRect(m_hWnd, NULL, TRUE);
    } else {
        // Bitmap creation failed
        complete_artwork_search();
        // m_status_text = "Failed to create artwork bitmap";
        
        // Notify event system that artwork loading failed
        ArtworkEventManager::get().notify(ArtworkEvent(
            ArtworkEventType::ARTWORK_FAILED, 
            nullptr, 
            source.c_str(), 
            m_last_search_artist.c_str(), 
            m_last_search_title.c_str()
        ));
        
        InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

// Queue image data for processing on main thread (thread-safe)
void artwork_ui_element::queue_image_for_processing(const std::vector<BYTE>& image_data, const char* source) {
    if (image_data.empty()) return;
    
    // Thread-safe storage of image data
    {
        std::lock_guard<std::mutex> lock(m_image_data_mutex);
        m_pending_image_data = image_data;
        m_pending_artwork_source = source;
    }
    
    // Signal main thread to process the data
    if (m_hWnd) {
        PostMessage(m_hWnd, WM_USER + 6, 0, 0);
    }
    
    // Notify event system that artwork is loading
    ArtworkEventManager::get().notify(ArtworkEvent(
        ArtworkEventType::ARTWORK_LOADING, 
        nullptr, 
        source, 
        m_last_search_artist.c_str(), 
        m_last_search_title.c_str()
    ));
}

// Clear artwork and reset search state
void artwork_ui_element::clear_artwork() {
    
    // DON'T clear artwork bitmap during transitions - only clear search state
    // The bitmap will be replaced when new artwork loads
    // This prevents blank screens during stream transitions
    
    // Only clear bitmap when playback actually stops (not during transitions)
    // if (m_artwork_bitmap) {
    //     DeleteObject(m_artwork_bitmap);
    //     m_artwork_bitmap = NULL;
    // }
    
    // Reset artwork found flag and search state
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_artwork_found = false;
        m_current_search_key = "";
        m_last_search_key = "";
    }
    
    // Clear pending image data
    {
        std::lock_guard<std::mutex> lock(m_image_data_mutex);
        m_pending_image_data.clear();
    }
    
    // Clear search metadata cache
    m_last_search_artist = "";
    m_last_search_title = "";
    m_current_priority_position = 0;
    
    // Clear current artwork path to reset flicker prevention
    m_current_artwork_path.clear();
    
    // Reset search timestamp to allow immediate new search
    m_last_search_timestamp = 0;
    
    // Update status text
    m_status_text = "";
    
    // Invalidate window to redraw
    if (m_hWnd) {
        InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

// Actually clear the bitmap - only used when playback stops
void artwork_ui_element::clear_artwork_bitmap() {
    // NEVER clear artwork bitmap - keep last artwork visible always
    // This prevents blank screens in ALL scenarios including playback stop
    // if (m_artwork_bitmap) {
    //     DeleteObject(m_artwork_bitmap);
    //     m_artwork_bitmap = NULL;
    // }
    
    // Reset artwork found flag and search state
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_artwork_found = false;
        m_current_search_key = "";
        m_last_search_key = "";
    }
    
    // Clear pending image data
    {
        std::lock_guard<std::mutex> lock(m_image_data_mutex);
        m_pending_image_data.clear();
    }
    
    // Clear search metadata cache
    m_last_search_artist = "";
    m_last_search_title = "";
    m_current_priority_position = 0;
    
    // Reset search timestamp to allow immediate new search
    m_last_search_timestamp = 0;
    
    // Update status text
    m_status_text = "";
    
    // Invalidate window to redraw
    if (m_hWnd) {
        InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

// Force clear artwork bitmap for "clear panel when not playing" option
void artwork_ui_element::force_clear_artwork_bitmap() {
    // Actually clear the bitmap (unlike clear_artwork_bitmap which keeps it)
    if (m_artwork_bitmap) {
        DeleteObject(m_artwork_bitmap);
        m_artwork_bitmap = NULL;
    }
    
    // Clear search state like the regular function
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_artwork_found = false;
        m_current_search_key = "";
        m_last_search_key = "";
    }
    
    // Clear pending image data
    {
        std::lock_guard<std::mutex> lock(m_image_data_mutex);
        m_pending_image_data.clear();
    }
    
    // Update status text
    m_status_text = "";
    
    // Invalidate window to redraw
    if (m_hWnd) {
        InvalidateRect(m_hWnd, NULL, FALSE);  // Don't erase background - we'll handle it in paint
    }
}

// Load noart image for "use noart image" option
void artwork_ui_element::load_noart_image() {
    
    // First clear any existing artwork
    if (m_artwork_bitmap) {
        DeleteObject(m_artwork_bitmap);
        m_artwork_bitmap = NULL;
    }
    
    // Clear search state
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_artwork_found = false;
        m_current_search_key = "";
        m_last_search_key = "";
    }
    
    // Clear pending image data
    {
        std::lock_guard<std::mutex> lock(m_image_data_mutex);
        m_pending_image_data.clear();
    }
    
    // Try to load noart image from configured logos directory
    pfc::string8 data_path;
    
    // Use custom logos folder if configured, otherwise use default
    if (!cfg_logos_folder.is_empty()) {
        data_path = cfg_logos_folder.get_ptr();
        if (!data_path.is_empty() && data_path[data_path.length() - 1] != '\\') {
            data_path += "\\";
        }
            } else {
        // Use default path
        pfc::string8 profile_path = core_api::get_profile_path();
        
        // Convert file:// URL to regular file path
        pfc::string8 file_path = profile_path;
        if (file_path.startsWith("file://")) {
            file_path = file_path.subString(7); // Remove "file://" prefix
        }
        
        data_path = file_path;
        data_path.add_string("\\foo_artwork_data\\logos\\");
    }
    
    // Try different noart image formats
    const char* noart_filenames[] = {
        "noart.png",
        "noart.jpg", 
        "noart.jpeg",
        "noart.gif",
        "noart.bmp",
        nullptr
    };
    
    pfc::string8 noart_file_path;
    bool noart_loaded = false;
    
    for (int i = 0; noart_filenames[i] != nullptr && !noart_loaded; i++) {
        noart_file_path = data_path;
        noart_file_path.add_string(noart_filenames[i]);
        
        // Check if file exists
        DWORD file_attrs = GetFileAttributesA(noart_file_path);
        if (file_attrs != INVALID_FILE_ATTRIBUTES && !(file_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            // File exists, try to load it
            
            // Read file data and use create_bitmap_from_data for proper scaling
            std::vector<BYTE> file_data;
            if (read_file_data(noart_file_path, file_data)) {
                if (create_bitmap_from_data(file_data)) {
                    m_artwork_source = "Noart image";
                    g_current_artwork_source = "Noart image";
                    m_status_text = "No artwork image";
                    
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        m_artwork_found = true;
                    }
                    
                    noart_loaded = true;
                } else {
                }
            } else {
            }
        }
    }
    
    if (!noart_loaded) {
        // No noart image found, just clear the panel (fallback behavior)
        m_status_text = "";
        g_current_artwork_source = "";
    }
    
    // Invalidate window to redraw with noart image or clear panel
    if (m_hWnd) {
        InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

// Try fallback images when no metadata is available
void artwork_ui_element::try_fallback_images_for_stream(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    
    pfc::string8 domain = extract_domain_from_stream_url(track);
    HBITMAP fallback_bitmap = nullptr;
    std::string fallback_source;
    
    if (!domain.is_empty()) {
        
        // Try station logo first (with full path + domain fallback)
        fallback_bitmap = load_station_logo(track);
        if (fallback_bitmap) {
            fallback_source = "Station logo";
        } else {
            // Try station-specific noart with full URL path support
            fallback_bitmap = load_noart_logo(track);
            if (fallback_bitmap) {
                fallback_source = "Station fallback (no artwork)";
            }
        }
    }
    
    // Try generic noart if nothing else found (now includes full URL path support)
    if (!fallback_bitmap) {
        fallback_bitmap = load_generic_noart_logo(track);
        if (fallback_bitmap) {
            fallback_source = "Generic fallback (no artwork)";
        }
    }
    
    if (fallback_bitmap) {
        // Clean up any existing bitmap
        if (m_artwork_bitmap) {
            DeleteObject(m_artwork_bitmap);
        }
        
        m_artwork_bitmap = fallback_bitmap;
        // Removed status text to prevent white screen when fallback image loads
        
        // Mark that artwork has been found
        {
            std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
            m_artwork_found = true;
        }
        
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
    } else {
        // m_status_text = "No artwork found";
        // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

// Playback callback for track changes
class artwork_play_callback : public play_callback_static {
public:
    artwork_play_callback() {
    }
    
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_dynamic_info | flag_on_playback_dynamic_info_track | flag_on_playback_starting | flag_on_playback_stop;
    }
    
    void on_playback_new_track(metadb_handle_ptr p_track) override {
        // RE-ENABLED SAFELY - Handle both local artwork and station logos
        try {
            // Handle both local files and internet streams
            if (p_track.is_valid()) {
                pfc::string8 track_path = p_track->get_path();
                // Reset artwork state for ALL UI elements before processing new track
                for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                    auto* element = g_artwork_ui_elements[i];
                    if (element && element->m_hWnd) {
                        // Clear existing artwork
                        if (element->m_artwork_bitmap) {
                            DeleteObject(element->m_artwork_bitmap);
                            element->m_artwork_bitmap = NULL;
                        }
                        
                        // Reset all search state
                        {
                            std::lock_guard<std::mutex> lock(element->m_artwork_found_mutex);
                            element->m_artwork_found = false;
                            element->m_last_search_key = "";
                            element->m_current_search_key = "";
                        }
                        element->m_last_search_timestamp = 0;
                        element->m_current_track = p_track;
                        // Don't show loading text - causes white screen
                        // element->m_status_text = "Loading...";
                        // InvalidateRect(element->m_hWnd, NULL, TRUE);
                    }
                }
                
                pfc::string8 path = p_track->get_path();
                bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                
                if (is_internet_stream) {
                    // Handle internet streams with minimal delay
                    for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                        auto* element = g_artwork_ui_elements[i];
                        if (element && element->m_hWnd) {
                            // Kill any existing timer and start new timer with minimal delay
                            KillTimer(element->m_hWnd, 9);  // Use timer ID 9 like CUI
                            SetTimer(element->m_hWnd, 9, 500, NULL);
                            
                            // Store track for delayed processing
                            element->m_current_track = p_track;
                            
                            // Clear pending metadata since this is a new track (metadata will come later via dynamic info)
                            element->m_pending_timer_artist = "";
                            element->m_pending_timer_title = "";
                        }
                    }
                } else {
                    // For local files, start API search immediately
                    for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                        auto* element = g_artwork_ui_elements[i];
                            if (element && element->m_hWnd) {
                                // Start API search immediately for internet streams with no delay
                                pfc::string8 artist, title;
                                element->extract_metadata_for_search(p_track, artist, title);
                                
                                if (!artist.is_empty() && !title.is_empty()) {
                                    element->m_last_search_artist = artist;
                                    element->m_last_search_title = title;
                                    element->search_next_api_in_priority(artist, title, 0);
                                }
                                
                                InvalidateRect(element->m_hWnd, NULL, TRUE);
                            }
                        }
                    }
                } else {
                    // Handle local files - load local artwork
                    for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                        auto* element = g_artwork_ui_elements[i];
                        if (element && element->m_hWnd) {
                            // Don't show loading text - causes white screen
                            // element->m_status_text = "Loading local artwork...";
                            
                            // Try local artwork search first
                            if (element->search_local_artwork()) {
                                // Local artwork found, no need for API search - no status text needed
                                // element->m_status_text = "Local artwork loaded";
                            } else {
                                // No local artwork, extract metadata and search APIs if needed
                                pfc::string8 artist, title;
                                element->extract_metadata_for_search(p_track, artist, title);
                                
                                if (!artist.is_empty() && !title.is_empty()) {
                                    element->m_last_search_artist = artist;
                                    element->m_last_search_title = title;
                                    element->search_next_api_in_priority(artist, title, 0);
                                } else {
                                    // Don't show status text - causes white screen
                                    // element->m_status_text = "No local artwork found";
                                }
                            }
                            
                            InvalidateRect(element->m_hWnd, NULL, TRUE);
                        }
                    }
                }
            }
        } catch (...) {
            // Silently handle any exceptions
        }
    }
    
    void on_playback_stop(play_control::t_stop_reason p_reason) override {
        
        // Clear artwork from all UI elements when playback stops (if option is enabled)
        for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
            if (cfg_clear_panel_when_not_playing) {
                if (cfg_use_noart_image) {
                    // Load and display noart image instead of clearing
                    g_artwork_ui_elements[i]->load_noart_image();
                } else {
                    // Just clear the panel
                    g_artwork_ui_elements[i]->force_clear_artwork_bitmap();
                }
                // Don't show status text - keep the panel clean
            } else {
            }
            g_artwork_ui_elements[i]->m_current_track.release();
            g_artwork_ui_elements[i]->m_playback_stopped = true;  // Mark playback as stopped
            if (g_artwork_ui_elements[i]->m_hWnd) {
                InvalidateRect(g_artwork_ui_elements[i]->m_hWnd, NULL, TRUE);
            }
        }
    }
    void on_playback_seek(double p_time) override {}
    void on_playback_pause(bool p_state) override {}
    void on_playback_edited(metadb_handle_ptr p_track) override {}
    void on_playback_dynamic_info(const file_info& p_info) override {
        // Empty implementation - not needed for artwork component
        // This callback is for bitrate changes and other non-essential info
    }
    void on_playback_dynamic_info_track(const file_info& p_info) override {
        // CALLBACK TESTING - Re-enabled with safety checks
        try {
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                    auto* element = g_artwork_ui_elements[i];
                    if (element && element->m_hWnd) {
                        // Just extract metadata safely and update status - no artwork loading yet
                        pfc::string8 artist, title;
                        element->extract_metadata_from_info(p_info, artist, title);
                        
                        if (!artist.is_empty() || !title.is_empty()) {
                            // Don't show dynamic metadata - causes white screen
                            // element->m_status_text = "Dynamic metadata: ";
                            // element->m_status_text += artist.c_str();
                            // element->m_status_text += " - ";
                            // element->m_status_text += title.c_str();
                            
                            // CRASH-SAFE: Use basic update_track instead of complex metadata version
                            pfc::string8 fresh_content = artist.c_str();
                            fresh_content += "|";
                            fresh_content += title.c_str();
                            
                            // SIMPLIFIED: Always search for valid metadata to ensure updates work
                            // Only require both artist and title to be present
                            if (!artist.is_empty() && !title.is_empty() && 
                                artist.get_length() > 0 && title.get_length() > 0) {
                                
                                // Check if content actually changed
                                bool content_changed = (fresh_content != element->m_last_update_content);
                                
                                // Removed status text to prevent white screen during metadata detection
                                
                                // Store the safe metadata for use by extract_metadata_for_search
                                {
                                    std::lock_guard<std::mutex> lock(element->m_safe_metadata_mutex);
                                    element->m_safe_artist = artist;
                                    element->m_safe_title = title;
                                }
                                element->m_last_update_content = fresh_content;
                                
                                // BYPASS update_track - Call start_priority_search directly
                                // Don't show search message - causes white screen
                                // element->m_status_text = "Starting priority search directly";
                                // InvalidateRect(element->m_hWnd, NULL, TRUE);
                                
                                // Update tracking variables that update_track would normally set
                                element->m_current_track = current_track;
                                
                                // BYPASS start_priority_search - Call search_next_api_in_priority directly
                                try {
                                    // Clear any existing search state to force new search
                                    {
                                        std::lock_guard<std::mutex> lock(element->m_artwork_found_mutex);
                                        element->m_last_search_key = "";
                                        element->m_current_search_key = "";
                                        element->m_artwork_found = false;
                                    }
                                    element->m_last_search_timestamp = 0;
                                    
                                    // Set search metadata for the API calls
                                    element->m_last_search_artist = artist;
                                    element->m_last_search_title = title;
                                    
                                    // Start search immediately since delay has been removed
                                    element->search_next_api_in_priority(artist, title, 0);
                                    
                                    // Update content AFTER successful search trigger
                                    element->m_last_update_content = fresh_content;
                                } catch (...) {
                                    // Don't show error message - causes white screen
                                    // element->m_status_text = "CRASH CAUGHT in search_next_api_in_priority!";
                                    // InvalidateRect(element->m_hWnd, NULL, TRUE);
                                }
                            } else {
                                // BAD METADATA: Don't trigger artwork search for stations with empty artist
                                // Don't show metadata message - causes white screen
                                // element->m_status_text = "Skipping search - incomplete metadata (artist: '";
                                // element->m_status_text += artist.c_str();
                                // element->m_status_text += "', title: '";
                                // element->m_status_text += title.c_str();
                                // element->m_status_text += "')";
                            }
                        } else {
                            // Don't show metadata message - causes white screen
                            // element->m_status_text = "Dynamic metadata received - no artist/title";
                            
                            // For stations with no/invalid metadata, load station logo as fallback
                            if (current_track.is_valid()) {
                                pfc::string8 path = current_track->get_path();
                                bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                                
                                if (is_internet_stream) {
                                    // Load station logo as immediate fallback for internet streams
                                    HBITMAP logo_bitmap = load_station_logo(current_track);
                                    if (logo_bitmap) {
                                    // Clean up any existing bitmap
                                    if (element->m_artwork_bitmap) {
                                        DeleteObject(element->m_artwork_bitmap);
                                    }
                                    
                                    element->m_artwork_bitmap = logo_bitmap;
                                    // Don't show station logo status - not needed
                                    // element->m_status_text = "Station logo loaded (no metadata)";
                                    
                                    // Mark that artwork has been found
                                    {
                                        std::lock_guard<std::mutex> lock(element->m_artwork_found_mutex);
                                        element->m_artwork_found = true;
                                    }
                                    
                                    // Don't notify event system for station logos (no OSD needed)
                                    }
                                }
                            }
                        }
                        InvalidateRect(element->m_hWnd, NULL, TRUE);
                    }
                }
            }
        } catch (...) {
            // Silently catch any crashes in callback
        }
    }
    void on_playback_time(double p_time) override {}
    void on_volume_change(float p_new_val) override {}
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {
        // CRASH ISOLATION: Completely disable playback starting processing
        try {
            for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                auto* element = g_artwork_ui_elements[i];
                if (element && element->m_hWnd) {
                    // Don't show status message - causes white screen
                    // element->m_status_text = "Playback starting - processing disabled to prevent crashes";
                    // InvalidateRect(element->m_hWnd, NULL, TRUE);
                }
            }
        } catch (...) {
            // Silently catch any crashes in callback
        }
    }
};

// Helper method to complete artwork search cache management
void artwork_ui_element::complete_artwork_search() {
    
    // Mark search as completed atomically
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_last_search_key = m_current_search_key;
        m_current_search_key = "";
    }
    
    // Update timestamp to prevent immediate re-search
    m_last_search_timestamp = GetTickCount();
    
}

// Priority-based search implementation
void artwork_ui_element::start_priority_search(const pfc::string8& artist, const pfc::string8& title) {
    
    // API SEARCHES RESTORED - Safe operation confirmed
    // Don't show search status - causes white screen
    // m_status_text = "Starting API artwork search...";
    // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
    
    // Check if this looks like a stream name rather than actual track metadata
    // This prevents searching for artwork based on station names like "Indie Pop Rocks!"
    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr current_track;
    if (pc->get_now_playing(current_track) && current_track.is_valid()) {
        pfc::string8 path = current_track->get_path();
        bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
        
        if (is_internet_stream && artist.is_empty() && !title.is_empty()) {
            // Single title with no artist on internet stream - likely a station name
            // Check for common station name patterns
            bool looks_like_station_name = false;
            
            // Pattern 1: Ends with exclamation mark (e.g., "Indie Pop Rocks!")
            if (title.get_length() > 0 && title.get_ptr()[title.get_length() - 1] == '!') {
                looks_like_station_name = true;
            }
            
            // Pattern 2: Contains radio/stream keywords
            const char* station_keywords[] = {"Radio", "FM", "Stream", "Station", "Music", "Rocks", "Hits", "Channel", "Chill", "Out"};
            for (int i = 0; i < 10; i++) {
                if (strstr(title.c_str(), station_keywords[i])) {
                    looks_like_station_name = true;
                    break;
                }
            }
            
            // Pattern 4: Contains website/domain patterns (e.g., "101.ru: Chill Out")
            if (strstr(title.c_str(), ".ru") || strstr(title.c_str(), ".com") || 
                strstr(title.c_str(), ".net") || strstr(title.c_str(), ".org") ||
                strstr(title.c_str(), ".fm") || strstr(title.c_str(), ".tv")) {
                looks_like_station_name = true;
            }
            
            // Pattern 5: Contains colon separator (common in station names like "101.ru: Chill Out")
            if (strstr(title.c_str(), ": ")) {
                looks_like_station_name = true;
            }
            
            // Pattern 3: All caps or mostly caps (common for station names)
            if (title.get_length() > 3) {
                int caps_count = 0;
                int letter_count = 0;
                for (t_size i = 0; i < title.get_length(); i++) {
                    char c = title.get_ptr()[i];
                    if (isalpha(c)) {
                        letter_count++;
                        if (isupper(c)) caps_count++;
                    }
                }
                if (letter_count > 0 && (caps_count * 100 / letter_count) > 70) {
                    looks_like_station_name = true;
                }
            }
            
            if (looks_like_station_name) {
                
                // Schedule brief delay to wait for real track metadata
                SetTimer(m_hWnd, 9, 1000, NULL);
                return;
            }
        }
    }
    
    // First, try to find local artwork files before searching online
    if (search_local_artwork()) {
        return;  // Found local artwork, no need to search online
    }
    
    // No local artwork found, proceed with online API search
    search_next_api_in_priority(artist, title, 0);
}

// Local artwork search implementation
bool artwork_ui_element::search_local_artwork() {
    // Get current track to find file path
    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr current_track;
    if (!pc->get_now_playing(current_track) || !current_track.is_valid()) {
        return false;
    }
    
    pfc::string8 file_path = current_track->get_path();
    
    // Skip if this is an internet stream
    if (strstr(file_path.c_str(), "://") && !strstr(file_path.c_str(), "file://")) {
        return false;
    }
    
    // Remove file:// prefix if present
    if (strstr(file_path.c_str(), "file://") == file_path.c_str()) {
        file_path = pfc::string8(file_path.c_str() + 7); // Skip "file://"
    }
    
    // Get directory of the music file
    pfc::string8 directory;
    t_size last_slash = file_path.find_last('\\');
    if (last_slash == pfc_infinite) {
        last_slash = file_path.find_last('/');
    }
    
    if (last_slash != pfc_infinite) {
        directory = pfc::string8(file_path.c_str(), last_slash);
    } else {
        return false;
    }
    
    // Search for ANY .jpg, .jpeg, or .png file in the directory
    pfc::string8 search_pattern = directory;
    search_pattern << "\\*";
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // Skip directories
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            
            // Check if file has image extension
            const char* filename = findData.cFileName;
            const char* ext = strrchr(filename, '.');
            if (ext) {
                if (_stricmp(ext, ".jpg") == 0 || 
                    _stricmp(ext, ".jpeg") == 0 || 
                    _stricmp(ext, ".png") == 0) {
                    
                    pfc::string8 full_path = directory;
                    full_path << "\\" << filename;
                    
                    if (load_local_artwork_file(full_path)) {
                        m_artwork_source = "Local file";
                        // No OSD for local files - they should load silently
                        complete_artwork_search();  // Mark search as complete
                        if (m_hWnd) InvalidateRect(m_hWnd, NULL, FALSE);
                        FindClose(hFind);
                        return true;
                    }
                }
            }
        } while (FindNextFileA(hFind, &findData));
        
        FindClose(hFind);
    }
    
    return false;
}

// Helper function to load local artwork file
bool artwork_ui_element::load_local_artwork_file(const pfc::string8& file_path) {
    // Check if this is the same file we already have loaded to prevent flicker
    if (m_artwork_bitmap && !m_current_artwork_path.is_empty() && 
        m_current_artwork_path == file_path) {
        return true;
    }
    
    try {
        // Open file
        HANDLE hFile = CreateFileA(file_path.c_str(), GENERIC_READ, FILE_SHARE_READ, 
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        DWORD file_size = GetFileSize(hFile, NULL);
        if (file_size == 0 || file_size > 10 * 1024 * 1024) {  // Max 10MB
            CloseHandle(hFile);
            return false;
        }
        
        // Read file data
        std::vector<BYTE> file_data(file_size);
        DWORD bytes_read = 0;
        if (!ReadFile(hFile, file_data.data(), file_size, &bytes_read, NULL) || bytes_read != file_size) {
            CloseHandle(hFile);
            return false;
        }
        CloseHandle(hFile);
        
        // Validate image data
        if (file_data.size() < 4) {
            return false;
        }
        
        // Check image format signatures
        const BYTE* data = file_data.data();
        bool valid_format = false;
        
        // JPEG
        if (data[0] == 0xFF && data[1] == 0xD8) valid_format = true;
        // PNG  
        else if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') valid_format = true;
        // GIF
        else if (file_data.size() >= 6 && 
                (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) valid_format = true;
        // BMP
        else if (data[0] == 'B' && data[1] == 'M') valid_format = true;
        
        if (!valid_format) {
            return false;
        }
        
        // Create bitmap from the image data
        if (create_bitmap_from_data(file_data)) {
            m_current_artwork_path = file_path; // Store path to prevent flicker on reload
            return true;
        } else {
            return false;
        }
        
    } catch (...) {
        return false;
    }
}

void artwork_ui_element::search_next_api_in_priority(const pfc::string8& artist, const pfc::string8& title, int current_position) {
    
    // Check if artwork has already been found by another search
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        if (m_artwork_found) {
            return;
        }
    }
    
    // Station logos are now fallbacks only - API searches happen first for internet streams
    
    if (current_position >= 5) {
        // No more APIs to try - try fallback images for internet streams
        if (m_current_track.is_valid()) {
            pfc::string8 path = m_current_track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            if (is_internet_stream) {
                pfc::string8 domain = extract_domain_from_stream_url(m_current_track);
                HBITMAP fallback_bitmap = nullptr;
                std::string fallback_source;
                
                // Priority order for fallbacks:
                // 1. Station-specific logo (domain.com.png)
                // 2. Station-specific noart (domain.com-noart.png) 
                // 3. Generic noart (noart.png)
                
                if (!domain.is_empty()) {
                    // Try station logo first (with full path + domain fallback)
                    fallback_bitmap = load_station_logo(m_current_track);
                    if (fallback_bitmap) {
                        fallback_source = "Station logo";
                    } else {
                        // Try station-specific noart with full URL path support
                        fallback_bitmap = load_noart_logo(m_current_track);
                        if (fallback_bitmap) {
                            fallback_source = "Station fallback (no artwork)";
                        }
                    }
                }
                
                // If no station-specific fallback, try generic noart (now includes full URL path support)
                if (!fallback_bitmap) {
                    fallback_bitmap = load_generic_noart_logo(m_current_track);
                    if (fallback_bitmap) {
                        fallback_source = "Generic fallback (no artwork)";
                    }
                }
                
                if (fallback_bitmap) {
                    // Clean up any existing bitmap
                    if (m_artwork_bitmap) {
                        DeleteObject(m_artwork_bitmap);
                    }
                    
                    m_artwork_bitmap = fallback_bitmap;
                    // Removed status text to prevent white screen when fallback image loads
                    
                    // Mark that artwork has been found (fallback counts as found)
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        m_artwork_found = true;
                    }
                    
                    // Notify event system that fallback artwork was loaded (for CUI panel)
                    ArtworkEventManager::get().notify(ArtworkEvent(
                        ArtworkEventType::ARTWORK_LOADED, 
                        fallback_bitmap, 
                        fallback_source.c_str(), 
                        m_last_search_artist.c_str(), 
                        m_last_search_title.c_str()
                    ));
                    
                    complete_artwork_search();
                    if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
                    return;
                }
            }
        }
        
        // No fallback available - mark search as complete
        complete_artwork_search();
        // Don't show "no artwork" message - causes white screen
        // m_status_text = "No artwork found";
        
        // Notify event system that artwork search failed (for CUI panel)
        ArtworkEventManager::get().notify(ArtworkEvent(
            ArtworkEventType::ARTWORK_FAILED, 
            nullptr, 
            "All APIs exhausted", 
            m_last_search_artist.c_str(), 
            m_last_search_title.c_str()
        ));
        
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        return;
    }
    
    auto api_order = get_api_search_order();
    ApiType current_api = api_order[current_position];
    
    // Check if the current API is enabled and try it
    bool api_enabled = false;
    switch (current_api) {
        case ApiType::iTunes:
            api_enabled = cfg_enable_itunes;
            break;
        case ApiType::Deezer:
            api_enabled = cfg_enable_deezer;
            break;
        case ApiType::LastFm:
            api_enabled = cfg_enable_lastfm;
            break;
        case ApiType::MusicBrainz:
            api_enabled = cfg_enable_musicbrainz;
            break;
        case ApiType::Discogs:
            api_enabled = cfg_enable_discogs;
            break;
    }
    
    pfc::string8 api_name;
    switch (current_api) {
        case ApiType::iTunes: api_name = "iTunes"; break;
        case ApiType::Deezer: api_name = "Deezer"; break;
        case ApiType::LastFm: api_name = "LastFm"; break;
        case ApiType::MusicBrainz: api_name = "MusicBrainz"; break;
        case ApiType::Discogs: api_name = "Discogs"; break;
    }
    
    
    if (api_enabled) {
        // Store current position for fallback
        m_current_priority_position = current_position;
        
        pfc::string8 debug_status = "Searching ";
        // Removed status text to prevent white screen during API searches
        
        // Start the API search (all in background threads to prevent UI freezing)
        switch (current_api) {
            case ApiType::iTunes:
                std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
                    search_itunes_background(artist, title);
                }).detach();
                break;
            case ApiType::Deezer:
                std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
                    search_deezer_background(artist, title);
                }).detach();
                break;
            case ApiType::LastFm:
                std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
                    search_lastfm_background(artist, title);
                }).detach();
                break;
            case ApiType::MusicBrainz:
                std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
                    search_musicbrainz_background(artist, title);
                }).detach();
                break;
            case ApiType::Discogs:
                std::thread([this, artist = pfc::string8(artist), title = pfc::string8(title)]() {
                    search_discogs_background(artist, title);
                }).detach();
                break;
        }
    } else {
        // API is disabled, try the next one
        search_next_api_in_priority(artist, title, current_position + 1);
    }
}

//=============================================================================
// Global functions for managing clear panel timers
//=============================================================================

// External function from CUI implementation
#ifdef COLUMNS_UI_AVAILABLE
extern void update_all_cui_clear_panel_timers();
#endif

// Global function commented out - duplicate definition
/*
void update_all_clear_panel_timers() {
    // Update DUI elements
    for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
        g_artwork_ui_elements[i]->update_clear_panel_timer();
    }
    
    // Update CUI elements
#ifdef COLUMNS_UI_AVAILABLE
    update_all_cui_clear_panel_timers();
#endif
}
*/

// UI Element factory - DISABLED DUE TO DEPENDENCY ISSUES
// The CUI panel handles artwork display for both DUI and CUI modes
/*
class artwork_ui_element_factory : public ui_element {
public:
    GUID get_guid() override {
        return artwork_ui_element::g_get_guid();
    }
    
    void get_name(pfc::string_base& out) override {
        artwork_ui_element::g_get_name(out);
    }
    
    ui_element_config::ptr get_default_configuration() override {
        return artwork_ui_element::g_get_default_configuration();
    }
    
    ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr config, ui_element_instance_callback::ptr callback) override {
        // Create instance directly since service_impl_t can't handle constructor args
        return new artwork_ui_element(parent, config, callback);
    }
    
    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }
    
    ui_element_children_enumerator::ptr enumerate_children(ui_element_config::ptr cfg) override {
        return NULL; // Not a container element
    }
    
    bool get_description(pfc::string_base& out) override {
        out = artwork_ui_element::g_get_description();
        return true;
    }
};
*/

//=============================================================================
// OSD (On-Screen Display) Implementation - MOVED TO ui_element.cpp
//=============================================================================

/*
// Show OSD with artwork source information
void artwork_ui_element::show_osd(const pfc::string8& source_name) {
    // Check if OSD is enabled in preferences
    if (!cfg_show_osd) {
        return;
    }
    
    m_osd_visible = true;
    m_osd_text = "Artwork from ";
    m_osd_text << source_name;
    m_osd_start_time = GetTickCount();
    m_osd_slide_offset = 200;  // Start fully off-screen to the right
    m_last_osd_slide_offset = 200;  // Initialize tracking variable
    
    // Start OSD animation timer (120 FPS = ~8ms intervals)
    SetTimer(m_hWnd, 10, 8, NULL);
    
    // Force immediate repaint - only invalidate OSD region
    if (m_hWnd) {
        RECT client_rect;
        GetClientRect(m_hWnd, &client_rect);
        
        // Calculate initial OSD position for invalidation
        SIZE text_size;
        HDC hdc = GetDC(m_hWnd);
        HFONT old_font = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        GetTextExtentPoint32A(hdc, m_osd_text.c_str(), m_osd_text.length(), &text_size);
        SelectObject(hdc, old_font);
        ReleaseDC(m_hWnd, hdc);
        
        const int padding = 8;
        const int osd_width = text_size.cx + (padding * 2);
        const int osd_height = text_size.cy + (padding * 2);
        
        // Calculate potential OSD region (include slide area)
        int osd_x_start = client_rect.right - osd_width - 10;  // Final position
        int osd_x_end = osd_x_start + 200;  // Starting position (off-screen)
        int osd_y = client_rect.bottom - osd_height - 10;
        
        RECT invalidate_rect = { 
            (osd_x_start - 5) > 0 ? (osd_x_start - 5) : 0, 
            (osd_y - 5) > 0 ? (osd_y - 5) : 0, 
            (osd_x_end + osd_width + 5) < client_rect.right ? (osd_x_end + osd_width + 5) : client_rect.right, 
            (osd_y + osd_height + 5) < client_rect.bottom ? (osd_y + osd_height + 5) : client_rect.bottom 
        };
        
        InvalidateRect(m_hWnd, &invalidate_rect, TRUE);
    }
}

// Update OSD animation state
void artwork_ui_element::update_osd_animation() {
    if (!m_osd_visible) {
        KillTimer(m_hWnd, 10);
        return;
    }
    
    DWORD current_time = GetTickCount();
    DWORD elapsed = current_time - m_osd_start_time;
    
    // Store previous offset for optimized repainting
    int previous_offset = m_osd_slide_offset;
    
    if (elapsed < OSD_DELAY_DURATION) {
        // Delay phase: keep OSD invisible (fully off-screen)
        m_osd_slide_offset = 200;  // Stay off-screen
        
    } else if (elapsed < (OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION)) {
        // Slide-in animation: from 200 pixels offset to 0 (fully visible)
        DWORD slide_progress = elapsed - OSD_DELAY_DURATION;
        m_osd_slide_offset = 200 - (int)((slide_progress * 200) / OSD_SLIDE_IN_DURATION);
        
    } else if (elapsed < (OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DISPLAY_DURATION)) {
        // Display phase: fully visible (offset = 0)
        m_osd_slide_offset = 0;
        
    } else {
        // Slide-out animation
        DWORD slide_out_start = OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DISPLAY_DURATION;
        DWORD slide_elapsed = elapsed - slide_out_start;
        
        if (slide_elapsed >= OSD_SLIDE_OUT_DURATION) {
            // Animation complete - hide OSD
            m_osd_visible = false;
            KillTimer(m_hWnd, 10);
            
            // Only invalidate the final OSD region to clear it
            if (m_hWnd) {
                RECT client_rect;
                GetClientRect(m_hWnd, &client_rect);
                
                // Calculate final OSD position to invalidate
                SIZE text_size;
                HDC hdc = GetDC(m_hWnd);
                HFONT old_font = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
                GetTextExtentPoint32A(hdc, m_osd_text.c_str(), m_osd_text.length(), &text_size);
                SelectObject(hdc, old_font);
                ReleaseDC(m_hWnd, hdc);
                
                const int padding = 8;
                const int osd_width = text_size.cx + (padding * 2);
                const int osd_height = text_size.cy + (padding * 2);
                
                int osd_x = client_rect.right - osd_width - 10 + previous_offset;
                int osd_y = client_rect.bottom - osd_height - 10;
                
                RECT invalidate_rect = { 
                    (osd_x - 5) > 0 ? (osd_x - 5) : 0, 
                    (osd_y - 5) > 0 ? (osd_y - 5) : 0, 
                    (osd_x + osd_width + 5) < client_rect.right ? (osd_x + osd_width + 5) : client_rect.right, 
                    (osd_y + osd_height + 5) < client_rect.bottom ? (osd_y + osd_height + 5) : client_rect.bottom 
                };
                
                InvalidateRect(m_hWnd, &invalidate_rect, TRUE);
            }
            return;
        } else {
            // Calculate slide-out offset (0 to 200 pixels)
            m_osd_slide_offset = (int)((slide_elapsed * 200) / OSD_SLIDE_OUT_DURATION);
        }
    }
    
    // Only invalidate specific regions if offset has changed
    if (m_hWnd && previous_offset != m_osd_slide_offset) {
        RECT client_rect;
        GetClientRect(m_hWnd, &client_rect);
        
        // Calculate text size for positioning
        SIZE text_size;
        HDC hdc = GetDC(m_hWnd);
        HFONT old_font = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        GetTextExtentPoint32A(hdc, m_osd_text.c_str(), m_osd_text.length(), &text_size);
        SelectObject(hdc, old_font);
        ReleaseDC(m_hWnd, hdc);
        
        const int padding = 8;
        const int osd_width = text_size.cx + (padding * 2);
        const int osd_height = text_size.cy + (padding * 2);
        
        // Calculate both old and new OSD positions
        int old_osd_x = client_rect.right - osd_width - 10 + previous_offset;
        int new_osd_x = client_rect.right - osd_width - 10 + m_osd_slide_offset;
        int osd_y = client_rect.bottom - osd_height - 10;
        
        // Create a combined invalidation rectangle that covers both positions
        int min_x = (old_osd_x < new_osd_x ? old_osd_x : new_osd_x) - 5;  // Extra padding for smooth animation
        int max_x = ((old_osd_x + osd_width) > (new_osd_x + osd_width) ? (old_osd_x + osd_width) : (new_osd_x + osd_width)) + 5;
        
        RECT invalidate_rect = { 
            min_x > 0 ? min_x : 0, 
            (osd_y - 5) > 0 ? (osd_y - 5) : 0, 
            max_x < client_rect.right ? max_x : client_rect.right, 
            (osd_y + osd_height + 5) < client_rect.bottom ? (osd_y + osd_height + 5) : client_rect.bottom 
        };
        
        // Only invalidate the specific region containing the OSD
        InvalidateRect(m_hWnd, &invalidate_rect, TRUE);
    }
    
    // Update last offset tracking
    m_last_osd_slide_offset = m_osd_slide_offset;
}

// Paint OSD overlay
void artwork_ui_element::paint_osd(HDC hdc, const RECT& client_rect) {
    if (!m_osd_visible || m_osd_text.is_empty()) return;
    
    // Save the current GDI state
    int saved_dc = SaveDC(hdc);
    
    // Calculate OSD dimensions
    SIZE text_size;
    HFONT old_font = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32A(hdc, m_osd_text.c_str(), m_osd_text.length(), &text_size);
    
    const int padding = 8;
    const int osd_width = text_size.cx + (padding * 2);
    const int osd_height = text_size.cy + (padding * 2);
    
    // Position in bottom-right corner with slide offset
    int osd_x = client_rect.right - osd_width - 10 + m_osd_slide_offset;
    int osd_y = client_rect.bottom - osd_height - 10;
    
    // Don't draw if completely slid off screen
    if (osd_x >= client_rect.right) {
        RestoreDC(hdc, saved_dc);
        return;
    }
    
    // Create OSD rectangle
    RECT osd_rect = { osd_x, osd_y, osd_x + osd_width, osd_y + osd_height };
    
    // Get foobar2000 UI colors for OSD background
    COLORREF osd_bg_color, osd_border_color;
    
    try {
        t_ui_color ui_bg_color;
        
        // Try to get foobar2000 UI element background color
        if (m_callback.is_valid() && m_callback->query_color(ui_color_background, ui_bg_color)) {
            // Use foobar2000 background color but make it slightly darker for OSD
            BYTE r = GetRValue(ui_bg_color);
            BYTE g = GetGValue(ui_bg_color);
            BYTE b = GetBValue(ui_bg_color);
            
            // Darken the color by reducing RGB values by 30% for better contrast
            r = (BYTE)(r * 0.7);
            g = (BYTE)(g * 0.7);
            b = (BYTE)(b * 0.7);
            
            osd_bg_color = RGB(r, g, b);
            
            // Create border color by making it slightly lighter than background
            r = (BYTE)(r * 1.3 > 255 ? 255 : r * 1.3);
            g = (BYTE)(g * 1.3 > 255 ? 255 : g * 1.3);
            b = (BYTE)(b * 1.3 > 255 ? 255 : b * 1.3);
            
            osd_border_color = RGB(r, g, b);
        } else {
            // Fallback: get standard colors using helper method
            if (m_callback.is_valid()) {
                COLORREF base_color = m_callback->query_std_color(ui_color_background);
                
                BYTE r = GetRValue(base_color);
                BYTE g = GetGValue(base_color);
                BYTE b = GetBValue(base_color);
                
                // Darken for background
                r = (BYTE)(r * 0.7);
                g = (BYTE)(g * 0.7);
                b = (BYTE)(b * 0.7);
                osd_bg_color = RGB(r, g, b);
                
                // Lighten for border
                r = (BYTE)(r * 1.3 > 255 ? 255 : r * 1.3);
                g = (BYTE)(g * 1.3 > 255 ? 255 : g * 1.3);
                b = (BYTE)(b * 1.3 > 255 ? 255 : b * 1.3);
                osd_border_color = RGB(r, g, b);
            } else {
                // Final fallback to dark blue
                osd_bg_color = RGB(30, 50, 100);
                osd_border_color = RGB(60, 80, 140);
            }
        }
    } catch (...) {
        // Fallback to dark blue if any foobar2000 API calls fail
        osd_bg_color = RGB(30, 50, 100);
        osd_border_color = RGB(60, 80, 140);
    }
    
    // Create background brush and border pen using foobar2000 colors
    HBRUSH bg_brush = CreateSolidBrush(osd_bg_color);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, bg_brush);
    
    // Set pen for border
    HPEN border_pen = CreatePen(PS_SOLID, 1, osd_border_color);
    HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
    
    // Draw filled rectangle with border
    Rectangle(hdc, osd_rect.left, osd_rect.top, osd_rect.right, osd_rect.bottom);
    
    // Set text properties
    SetBkMode(hdc, TRANSPARENT);
    
    // Get text color that contrasts well with the background
    COLORREF text_color;
    try {
        t_ui_color ui_text_color;
        
        // Try to get foobar2000 UI element text color
        if (m_callback.is_valid() && m_callback->query_color(ui_color_text, ui_text_color)) {
            text_color = ui_text_color;
        } else {
            // Fallback: get standard text color
            if (m_callback.is_valid()) {
                text_color = m_callback->query_std_color(ui_color_text);
            } else {
                // Final fallback to white text
                text_color = RGB(255, 255, 255);
            }
        }
    } catch (...) {
        // Fallback to white text if any foobar2000 API calls fail
        text_color = RGB(255, 255, 255);
    }
    
    SetTextColor(hdc, text_color);
    
    // Calculate centered text position
    RECT text_rect = { 
        osd_rect.left + padding, 
        osd_rect.top + padding, 
        osd_rect.right - padding, 
        osd_rect.bottom - padding 
    };
    
    // Draw the text
    DrawTextA(hdc, m_osd_text.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    
    // Clean up GDI objects
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_font);
    DeleteObject(bg_brush);
    DeleteObject(border_pen);
    
    // Restore original DC state
    RestoreDC(hdc, saved_dc);
}

static class ui_element_debug {
public:
    ui_element_debug() {
#ifdef _DEBUG
#endif
    }
} g_ui_element_debug;

// Service factory registrations
static initquit_factory_t<artwork_init> g_artwork_init_factory;
static play_callback_static_factory_t<artwork_play_callback> g_play_callback_factory;
// DISABLED: DUI element has dependency issues - CUI panel handles both DUI and CUI
// static service_factory_single_t<artwork_ui_element_factory> g_ui_element_factory;

static class ui_element_post_debug {
public:
    ui_element_post_debug() {
    }
} g_ui_element_post_debug;

//=============================================================================
// Standalone Artwork Search System for CUI panels
//=============================================================================

// Standalone versions of essential methods (copied from main component)
namespace standalone {
    
    // URL encode function (copied from artwork_ui_element)
    std::string url_encode(const std::string& str) {
        std::string encoded;
        
        for (size_t i = 0; i < str.length(); i++) {
            char c = str[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char hex[4];
                sprintf_s(hex, "%%%02X", (unsigned char)c);
                encoded += hex;
            }
        }
        
        return encoded;
    }
    
    // HTTP GET request (copied from artwork_ui_element)
    bool http_get_request(const std::string& url, std::string& response) {
        response.clear();
        bool success = false;
        
        // Use per-request manager to prevent API interference without blocking other APIs
        HttpRequestManager* request_manager = get_request_manager_for_api(url);
        auto request_lock = request_manager->acquire_lock();
        
        // Convert URL to wide string
        std::wstring wide_url(url.begin(), url.end());
        
        // Parse URL components
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
            HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                           WINHTTP_NO_PROXY_NAME,
                                           WINHTTP_NO_PROXY_BYPASS, 0);
            
            if (hSession) {
                // Set timeouts to prevent application freezing
                // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
                WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
                std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
                
                HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), 
                                                  urlComp.nPort, 0);
                
                if (hConnect) {
                    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
                    if (urlComp.lpszExtraInfo) {
                        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
                    }
                    
                    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
                    
                    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                                          NULL, WINHTTP_NO_REFERER,
                                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                    
                    if (hRequest) {
                        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                            
                            if (WinHttpReceiveResponse(hRequest, NULL)) {
                                DWORD dwSize = 0;
                                do {
                                    DWORD dwDownloaded = 0;
                                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                                    
                                    if (dwSize > 0) {
                                        char* pszOutBuffer = new char[dwSize + 1];
                                        ZeroMemory(pszOutBuffer, dwSize + 1);
                                        
                                        if (WinHttpReadData(hRequest, pszOutBuffer, dwSize, &dwDownloaded)) {
                                            response.append(pszOutBuffer, dwDownloaded);
                                            success = true;
                                        }
                                        delete[] pszOutBuffer;
                                    }
                                } while (dwSize > 0);
                            }
                        }
                        WinHttpCloseHandle(hRequest);
                    }
                    WinHttpCloseHandle(hConnect);
                }
                WinHttpCloseHandle(hSession);
            }
        }
        
        return success && !response.empty();
    }
    
    // Parse Deezer JSON response (copied from artwork_ui_element)
    bool parse_deezer_response(const std::string& json, std::string& artwork_url) {
        artwork_url.clear();
        
        // Look for data array containing track results
        const char* data_start = strstr(json.c_str(), "\"data\"");
        if (!data_start) {
            return false;
        }
        
        // Find the opening bracket for the data array
        const char* bracket_pos = strchr(data_start, '[');
        if (!bracket_pos) {
            return false;
        }
        
        // Look for the first track object in the array
        const char* track_start = strchr(bracket_pos, '{');
        if (!track_start) {
            return false;
        }
        
        // Find album object within the track
        const char* album_start = strstr(track_start, "\"album\"");
        if (!album_start) {
            return false;
        }
        
        // Look for cover_xl (highest quality) first
        const char* cover_xl = strstr(album_start, "\"cover_xl\"");
        if (cover_xl) {
            const char* url_start = strchr(cover_xl, ':');
            if (url_start) {
                url_start = strchr(url_start, '"');
                if (url_start) {
                    url_start++; // Skip opening quote
                    const char* url_end = strchr(url_start, '"');
                    if (url_end) {
                        artwork_url = std::string(url_start, url_end - url_start);
                        
                        // Unescape JSON forward slashes
                        size_t pos = 0;
                        while ((pos = artwork_url.find("\\/", pos)) != std::string::npos) {
                            artwork_url.replace(pos, 2, "/");
                            pos += 1;
                        }
                        
                        // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
                        size_t size_pos = artwork_url.find("1000x1000");
                        if (size_pos != std::string::npos) {
                            artwork_url.replace(size_pos, 9, "1200x1200");
                        }
                        
                        return true;
                    }
                }
            }
        }
        
        // Fallback to cover_big
        const char* cover_big = strstr(album_start, "\"cover_big\"");
        if (cover_big) {
            const char* url_start = strchr(cover_big, ':');
            if (url_start) {
                url_start = strchr(url_start, '"');
                if (url_start) {
                    url_start++; // Skip opening quote
                    const char* url_end = strchr(url_start, '"');
                    if (url_end) {
                        artwork_url = std::string(url_start, url_end - url_start);
                        
                        // Unescape JSON forward slashes
                        size_t pos = 0;
                        while ((pos = artwork_url.find("\\/", pos)) != std::string::npos) {
                            artwork_url.replace(pos, 2, "/");
                            pos += 1;
                        }
                        
                        // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
                        size_t size_pos = artwork_url.find("1000x1000");
                        if (size_pos != std::string::npos) {
                            artwork_url.replace(size_pos, 9, "1200x1200");
                        }
                        
                        return true;
                    }
                }
            }
        }
        
        return false;
    }
    
    // Download image data (copied from artwork_ui_element)
    bool download_image(const std::string& url, std::vector<BYTE>& data) {
        data.clear();
        
        // Use per-request manager to prevent API interference without blocking other APIs
        HttpRequestManager* request_manager = get_request_manager_for_api(url);
        auto request_lock = request_manager->acquire_lock();
        
        // Convert URL to wide string
        std::wstring wide_url(url.begin(), url.end());
        
        // Parse URL components
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
            return false;
        }
        
        HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            return false;
        }
        
        // Set timeouts to prevent application freezing
        // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
        WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
        
        std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
        
        HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), 
                                          urlComp.nPort, 0);
        
        if (hConnect) {
            std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
            if (urlComp.lpszExtraInfo) {
                path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
            }
            
            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                                  NULL, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            
            if (hRequest) {
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    
                    if (WinHttpReceiveResponse(hRequest, NULL)) {
                        DWORD dwSize = 0;
                        do {
                            DWORD dwDownloaded = 0;
                            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                            
                            if (dwSize > 0) {
                                size_t oldSize = data.size();
                                data.resize(oldSize + dwSize);
                                
                                if (!WinHttpReadData(hRequest, &data[oldSize], dwSize, &dwDownloaded)) {
                                    data.resize(oldSize); // Revert on error
                                    break;
                                }
                                
                                if (dwDownloaded != dwSize) {
                                    data.resize(oldSize + dwDownloaded);
                                }
                            }
                        } while (dwSize > 0);
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
        
        return !data.empty();
    }
    
    // Create bitmap from image data and make it available globally
    bool create_bitmap_from_data(const std::vector<BYTE>& data) {
        if (data.empty()) {
            return false;
        }
        
        // Create memory stream from data
        IStream* pStream = NULL;
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!hGlobal) {
            return false;
        }
        
        void* pData = GlobalLock(hGlobal);
        if (!pData) {
            GlobalFree(hGlobal);
            return false;
        }
        
        memcpy(pData, data.data(), data.size());
        GlobalUnlock(hGlobal);
        
        if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) != S_OK) {
            GlobalFree(hGlobal);
            return false;
        }
        
        // Create GDI+ bitmap from stream
        Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromStream(pStream);
        pStream->Release();
        
        if (!pBitmap || pBitmap->GetLastStatus() != Gdiplus::Ok) {
            if (pBitmap) delete pBitmap;
            return false;
        }
        
        // Debug: Check pixel format to see if alpha channel is present
        Gdiplus::PixelFormat format = pBitmap->GetPixelFormat();
        char debug_msg[256];
        sprintf_s(debug_msg, "SDK_MAIN DEBUG 2: Loaded bitmap pixel format: 0x%08X, Has Alpha: %s", 
                 format, (format & PixelFormatAlpha) ? "YES" : "NO");
        console::info(debug_msg);
        
        if (format == PixelFormat32bppARGB) {
        } else if (format == PixelFormat32bppRGB) {
        } else if (format == PixelFormat24bppRGB) {
        }
        
        // Convert to HBITMAP
        HBITMAP hBitmap = NULL;
        if (pBitmap->GetHBITMAP(NULL, &hBitmap) != Gdiplus::Ok) {
            delete pBitmap;
            return false;
        }
        
        delete pBitmap;
        
        // Store bitmap globally for CUI panel access
        // First, clean up any existing bitmap
        if (::g_shared_artwork_bitmap) {
            DeleteObject(::g_shared_artwork_bitmap);
        }
        ::g_shared_artwork_bitmap = hBitmap;
        
        // Notify event system that artwork was loaded successfully
        ::ArtworkEventManager::get().notify(::ArtworkEvent(
            ::ArtworkEventType::ARTWORK_LOADED, 
            hBitmap, 
            ::g_current_artwork_source.c_str(), 
            "", 
            ""
        ));
        
        return true;
    }
    
} // namespace standalone

void standalone_deezer_search(metadb_handle_ptr track) {
    if (!track.is_valid()) {
        g_artwork_loading = false;
        return;
    }
    
    try {
        // Extract metadata (same as main component)
        const file_info& info = track->get_info_ref()->info();
        std::string artist, title;
        
        if (info.meta_get("ARTIST", 0)) {
            artist = info.meta_get("ARTIST", 0);
        }
        if (info.meta_get("TITLE", 0)) {
            title = info.meta_get("TITLE", 0);
        }
        
        
        if (title.empty()) {
            g_artwork_loading = false;
            return;
        }
        
        // TODO: Implement full Deezer API search here
        Sleep(2000); // Temporary simulation
        
        g_artwork_loading = false;
        
    } catch (...) {
        g_artwork_loading = false;
    }
}

void standalone_deezer_search_with_metadata(const std::string& artist, const std::string& title) {
    try {
        
        if (title.empty()) {
            g_artwork_loading = false;
            return;
        }
        
        // Build Deezer search URL (same as main component)
        std::string search_query;
        if (artist.empty()) {
            search_query = title;
        } else {
            search_query = artist + " " + title;
        }
        
        // URL encode the search query using standalone method
        std::string encoded_search = standalone::url_encode(search_query);
        
        std::string search_url = "https://api.deezer.com/search/track?q=" + encoded_search + "&limit=10";
        
        
        // Make HTTP request using standalone method
        std::string response;
        if (standalone::http_get_request(search_url, response)) {
            
            // Parse Deezer response using standalone method
            std::string artwork_url;
            if (standalone::parse_deezer_response(response, artwork_url)) {
                
                // Download image using standalone method
                
                std::vector<BYTE> image_data;
                if (standalone::download_image(artwork_url, image_data)) {
                    
                    // Create bitmap from downloaded image data
                    if (standalone::create_bitmap_from_data(image_data)) {
                    } else {
                    }
                } else {
                }
            } else {
            }
        } else {
        }
        g_artwork_loading = false;
        
    } catch (...) {
        g_artwork_loading = false;
    }
}
*/

// Bridge functions - call main component API methods safely without UI element

bool bridge_search_deezer(const std::string& artist, const std::string& title) {
    try {
        
        if (title.empty()) {
            return false;
        }
        
        // Build Deezer search URL (same logic as main component)
        std::string search_query;
        if (artist.empty()) {
            search_query = title;
        } else {
            search_query = artist + " " + title;
        }
        
        // Simple URL encoding for search query
        std::string encoded_query;
        for (char c : search_query) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_query += c;
            } else if (c == ' ') {
                encoded_query += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_query += buf;
            }
        }
        
        // Build Deezer API URL (no authentication required)
        std::string deezer_url = "https://api.deezer.com/search/track?q=" + encoded_query + "&limit=1";
        
        
        // Make HTTP request using bridge function
        std::string response;
        if (bridge_http_get_request(deezer_url, response)) {
            
            // Parse Deezer JSON response for album cover URL (same logic as main component)
            std::string artwork_url;
            if (parse_deezer_json_response(response, artwork_url)) {
                
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (bridge_download_image(artwork_url, image_data)) {
                    
                    // Set source before creating bitmap so event notification has correct source
                    g_current_artwork_source = "Deezer";
                    
                    // Create bitmap from downloaded data and store in shared bitmap
                    if (create_bitmap_from_image_data(image_data)) {
                        g_artwork_loading = false;
                        return true; // Success!
                    } else {
                    }
                } else {
                }
            } else {
            }
        } else {
        }
        
        return false; // No artwork found
        
    } catch (...) {
        return false;
    }
}

bool bridge_search_discogs(const std::string& artist, const std::string& title) {
    try {
        
        // Check credentials like main component does
        if (cfg_discogs_key.is_empty() && 
            (cfg_discogs_consumer_key.is_empty() || cfg_discogs_consumer_secret.is_empty())) {
            return false;
        }
        
        if (artist.empty() || title.empty()) {
            return false;
        }
        
        // URL encoding for search parameters
        std::string encoded_artist, encoded_title;
        for (char c : artist) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_artist += c;
            } else if (c == ' ') {
                encoded_artist += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_artist += buf;
            }
        }
        
        for (char c : title) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_title += c;
            } else if (c == ' ') {
                encoded_title += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_title += buf;
            }
        }
        
        // Build Discogs search URL (match main component format)
        std::string search_terms;
        if (artist.empty()) {
            search_terms = title;
        } else {
            search_terms = artist + " - " + title;  // Main component uses " - " separator
        }
        
        // URL encode the complete search terms
        std::string encoded_search;
        for (char c : search_terms) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_search += c;
            } else if (c == ' ') {
                encoded_search += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_search += buf;
            }
        }
        
        std::string discogs_url = "https://api.discogs.com/database/search?q=" + encoded_search + "&type=release";
        
        // Add authentication token if available
        if (!cfg_discogs_key.is_empty()) {
            discogs_url += "&token=" + std::string(cfg_discogs_key.get_ptr());
        } else if (!cfg_discogs_consumer_key.is_empty() && !cfg_discogs_consumer_secret.is_empty()) {
            discogs_url += "&key=" + std::string(cfg_discogs_consumer_key.get_ptr());
            discogs_url += "&secret=" + std::string(cfg_discogs_consumer_secret.get_ptr());
        }
        
        
        // Make HTTP request with User-Agent header (required by Discogs API)
        std::string response;
        if (bridge_http_get_request_with_useragent(discogs_url, response, "foobar2000-artwork/1.0")) {
            
            
            // Parse Discogs JSON response for artwork URL
            std::string artwork_url;
            if (parse_discogs_json_response(response, artwork_url)) {
                
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (bridge_download_image(artwork_url, image_data)) {
                    
                    // Set source before creating bitmap so event notification has correct source
                    g_current_artwork_source = "Discogs";
                    
                    // Create bitmap from downloaded data and store in shared bitmap
                    if (create_bitmap_from_image_data(image_data)) {
                        g_artwork_loading = false;
                        return true; // Success!
                    } else {
                    }
                } else {
                }
            } else {
            }
        } else {
        }
        
        return false; // No artwork found
        
    } catch (...) {
        return false;
    }
}

// Placeholder bridge functions for other APIs
bool bridge_search_itunes(const std::string& artist, const std::string& title) {
    try {
        
        if (title.empty()) {
            return false;
        }
        
        // Build iTunes search URL (same logic as main component)
        std::string search_query;
        if (artist.empty()) {
            search_query = title;
        } else {
            search_query = artist + " " + title;
        }
        
        // URL encoding for search query
        std::string encoded_query;
        for (char c : search_query) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_query += c;
            } else if (c == ' ') {
                encoded_query += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_query += buf;
            }
        }
        
        // Build iTunes API URL (no authentication required)
        std::string itunes_url = "https://itunes.apple.com/search?term=" + encoded_query + "&media=music&entity=song&limit=1";
        
        
        // Make HTTP request
        std::string response;
        if (bridge_http_get_request(itunes_url, response)) {
            
            // Parse iTunes JSON response for artwork URL
            std::string artwork_url;
            if (parse_itunes_json_response(response, artwork_url)) {
                
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (bridge_download_image(artwork_url, image_data)) {
                    
                    // Set source before creating bitmap so event notification has correct source
                    g_current_artwork_source = "iTunes";
                    
                    // Create bitmap from downloaded data and store in shared bitmap
                    if (create_bitmap_from_image_data(image_data)) {
                        g_artwork_loading = false;
                        return true; // Success!
                    } else {
                    }
                } else {
                }
            } else {
            }
        } else {
        }
        
        return false; // No artwork found
        
    } catch (...) {
        return false;
    }
}

bool bridge_search_lastfm(const std::string& artist, const std::string& title) {
    try {
        
        // Check if API key is configured
        if (cfg_lastfm_key.is_empty()) {
            return false;
        }
        
        if (artist.empty() || title.empty()) {
            return false;
        }
        
        // URL encoding for search parameters
        std::string encoded_artist, encoded_title;
        for (char c : artist) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_artist += c;
            } else if (c == ' ') {
                encoded_artist += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_artist += buf;
            }
        }
        
        for (char c : title) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_title += c;
            } else if (c == ' ') {
                encoded_title += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_title += buf;
            }
        }
        
        // Build Last.fm API URL
        std::string lastfm_url = "https://ws.audioscrobbler.com/2.0/?method=track.getInfo&api_key=" + 
                                std::string(cfg_lastfm_key.get_ptr()) + 
                                "&artist=" + encoded_artist + 
                                "&track=" + encoded_title + 
                                "&format=json";
        
        
        // Make HTTP request
        std::string response;
        if (bridge_http_get_request(lastfm_url, response)) {
            
            // Parse Last.fm JSON response for artwork URL
            std::string artwork_url;
            if (parse_lastfm_json_response(response, artwork_url)) {
                
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (bridge_download_image(artwork_url, image_data)) {
                    
                    // Set source before creating bitmap so event notification has correct source
                    g_current_artwork_source = "Last.fm";
                    
                    // Create bitmap from downloaded data and store in shared bitmap
                    if (create_bitmap_from_image_data(image_data)) {
                        g_artwork_loading = false;
                        return true; // Success!
                    } else {
                    }
                } else {
                }
            } else {
            }
        } else {
        }
        
        return false; // No artwork found
        
    } catch (...) {
        return false;
    }
}

bool bridge_search_musicbrainz(const std::string& artist, const std::string& title) {
    try {
        // MusicBrainz rate limiting: 1 request per second minimum
        static DWORD last_musicbrainz_request = 0;
        DWORD current_time = GetTickCount();
        DWORD time_since_last = current_time - last_musicbrainz_request;
        
        if (last_musicbrainz_request > 0 && time_since_last < 1000) {
            // Sleep to enforce 1-second minimum between requests
            Sleep(1000 - time_since_last);
        }
        last_musicbrainz_request = GetTickCount();
        
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
        
        if (artist.empty() || title.empty()) {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
            return false;
        }
        
        // URL encoding for search parameters
        std::string encoded_artist, encoded_title;
        for (char c : artist) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_artist += c;
            } else if (c == ' ') {
                encoded_artist += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_artist += buf;
            }
        }
        
        for (char c : title) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                encoded_title += c;
            } else if (c == ' ') {
                encoded_title += "%20";
            } else {
                char buf[4];
                sprintf_s(buf, "%%%02X", (unsigned char)c);
                encoded_title += buf;
            }
        }
        
        // Build MusicBrainz search URL for recordings
        std::string musicbrainz_url = "https://musicbrainz.org/ws/2/recording/?query=artist:" + 
                                     encoded_artist + "%20AND%20recording:" + encoded_title + 
                                     "&fmt=json&limit=1";
        
        
        // Make HTTP request to MusicBrainz (requires User-Agent)
        std::string response;
        if (bridge_http_get_request_with_useragent(musicbrainz_url, response, "foobar2000-artwork/1.0 (https://foobar2000.org/)")) {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
            
            // Parse MusicBrainz JSON response for release MBID
            std::string release_mbid;
            if (parse_musicbrainz_json_response(response, release_mbid)) {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
                
                // Build CoverArtArchive URL
                std::string coverart_url = "https://coverartarchive.org/release/" + release_mbid + "/front";
                
                
                // Download the artwork image directly (CoverArtArchive redirects to actual image)
                std::vector<BYTE> image_data;
                if (bridge_download_image(coverart_url, image_data)) {
                    
                    // Set source before creating bitmap so event notification has correct source
                    g_current_artwork_source = "MusicBrainz";
                    
                    // Create bitmap from downloaded data and store in shared bitmap
                    if (create_bitmap_from_image_data(image_data)) {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
                        g_artwork_loading = false;
                        return true; // Success!
                    } else {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
                    }
                } else {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
                }
            } else {
#ifdef _DEBUG
#endif
            }
        } else {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
        }
        
        return false; // No artwork found
        
    } catch (...) {
#ifdef _DEBUG
#ifdef _DEBUG
#endif
#endif
        return false;
    }
}

// Bridge helper functions implementation

bool parse_deezer_json_response(const std::string& json, std::string& artwork_url) {
    // Simple JSON parsing for Deezer response (same logic as main component)
    // Look for "album" -> "cover_xl" field
    
    size_t album_pos = json.find("\"album\"");
    if (album_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find cover_xl field after album
    size_t cover_pos = json.find("\"cover_xl\"", album_pos);
    if (cover_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find the URL value
    size_t colon_pos = json.find(":", cover_pos);
    if (colon_pos == std::string::npos) return false;
    
    size_t quote_start = json.find("\"", colon_pos);
    if (quote_start == std::string::npos) return false;
    
    size_t url_start = quote_start + 1;
    size_t quote_end = json.find("\"", url_start);
    if (quote_end == std::string::npos) return false;
    
    artwork_url = json.substr(url_start, quote_end - url_start);
    
    // Unescape JSON forward slashes (replace \/ with /)
    size_t pos = 0;
    while ((pos = artwork_url.find("\\/", pos)) != std::string::npos) {
        artwork_url.replace(pos, 2, "/");
        pos += 1;
    }
    
    // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
    size_t size_pos = artwork_url.find("1000x1000");
    if (size_pos != std::string::npos) {
        artwork_url.replace(size_pos, 9, "1200x1200");
    }
    
    // Validate URL
    if (artwork_url.empty() || artwork_url.find("http") != 0) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    
    return true;
}

bool parse_itunes_json_response(const std::string& json, std::string& artwork_url) {
    // Simple JSON parsing for iTunes response
    // Look for "results" array and artwork URL fields
    
    size_t results_pos = json.find("\"results\"");
    if (results_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Try artwork URLs in order of preference
    const char* artwork_fields[] = {
        "\"artworkUrl600\"",   // 600x600 (if available)
        "\"artworkUrl512\"",   // 512x512 (if available)
        "\"artworkUrl100\"",   // 100x100 (most common)
        "\"artworkUrl60\"",    // 60x60 (fallback)
        "\"artworkUrl30\""     // 30x30 (smallest)
    };
    
    for (int i = 0; i < 5; i++) {
        size_t artwork_pos = json.find(artwork_fields[i], results_pos);
        if (artwork_pos != std::string::npos) {
            // Find the URL value
            size_t colon_pos = json.find(":", artwork_pos);
            if (colon_pos == std::string::npos) continue;
            
            size_t quote_start = json.find("\"", colon_pos);
            if (quote_start == std::string::npos) continue;
            
            size_t url_start = quote_start + 1;
            size_t quote_end = json.find("\"", url_start);
            if (quote_end == std::string::npos) continue;
            
            artwork_url = json.substr(url_start, quote_end - url_start);
            
            // Unescape JSON forward slashes (replace \/ with /)
            size_t pos = 0;
            while ((pos = artwork_url.find("\\/", pos)) != std::string::npos) {
                artwork_url.replace(pos, 2, "/");
                pos += 1;
            }
            
            // Upgrade any resolution to 1200x1200 using iTunes URL manipulation
            pos = artwork_url.find("100x100");
            if (pos != std::string::npos) {
                artwork_url.replace(pos, 7, "1200x1200");
            } else {
                pos = artwork_url.find("600x600");
                if (pos != std::string::npos) {
                    artwork_url.replace(pos, 7, "1200x1200");
                } else {
                    pos = artwork_url.find("512x512");
                    if (pos != std::string::npos) {
                        artwork_url.replace(pos, 7, "1200x1200");
                    } else {
                        pos = artwork_url.find("60x60");
                        if (pos != std::string::npos) {
                            artwork_url.replace(pos, 5, "1200x1200");
                        } else {
                            pos = artwork_url.find("30x30");
                            if (pos != std::string::npos) {
                                artwork_url.replace(pos, 5, "1200x1200");
                            }
                        }
                    }
                }
            }
            
            // Set compression quality: 80 for PNG files, 90 for JPEG files
            pos = artwork_url.find(".png");
            if (pos != std::string::npos) {
                // For PNG files: add bb-80 quality parameter  
                size_t bb_pos = artwork_url.find("bb.png");
                if (bb_pos != std::string::npos) {
                    artwork_url.replace(bb_pos, 6, "bb-80.png");
                } else {
                    size_t bf_pos = artwork_url.find("bf.png");
                    if (bf_pos != std::string::npos) {
                        artwork_url.replace(bf_pos, 6, "bb-80.png");
                    } else {
                        size_t res_pos = artwork_url.find("1200x1200.png");
                        if (res_pos != std::string::npos) {
                            artwork_url.replace(res_pos, 13, "1200x1200bb-80.png");
                        }
                    }
                }
            } else {
                pos = artwork_url.find(".jpg");
                if (pos != std::string::npos) {
                    // For JPEG files: add bb-90 quality parameter
                    size_t bb_pos = artwork_url.find("bb.jpg");
                    if (bb_pos != std::string::npos) {
                        artwork_url.replace(bb_pos, 6, "bb-90.jpg");
                    } else {
                        size_t bf_pos = artwork_url.find("bf.jpg");
                        if (bf_pos != std::string::npos) {
                            artwork_url.replace(bf_pos, 6, "bb-90.jpg");
                        } else {
                            size_t res_pos = artwork_url.find("1200x1200.jpg");
                            if (res_pos != std::string::npos) {
                                artwork_url.replace(res_pos, 13, "1200x1200bb-90.jpg");
                            }
                        }
                    }
                }
            }
            
            // Validate URL
            if (!artwork_url.empty() && artwork_url.find("http") == 0) {
                return true;
            }
        }
    }
    
    return false;
}

bool parse_lastfm_json_response(const std::string& json, std::string& artwork_url) {
    // Simple JSON parsing for Last.fm response
    // Look for "track" -> "album" -> "image" array and get the largest image
    
    size_t track_pos = json.find("\"track\"");
    if (track_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find album field after track
    size_t album_pos = json.find("\"album\"", track_pos);
    if (album_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find image array after album
    size_t image_pos = json.find("\"image\"", album_pos);
    if (image_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Look for the largest image size (extralarge or large)
    size_t extralarge_pos = json.find("\"extralarge\"", image_pos);
    size_t search_start = extralarge_pos;
    
    if (extralarge_pos == std::string::npos) {
        // Fall back to large if extralarge not found
        size_t large_pos = json.find("\"large\"", image_pos);
        if (large_pos == std::string::npos) {
#ifdef _DEBUG
#endif
            return false;
        }
        search_start = large_pos;
    }
    
    // Find the "#text" field that contains the URL (search backwards from size field)
    size_t text_pos = json.rfind("\"#text\"", search_start);
    if (text_pos == std::string::npos || text_pos < image_pos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find the URL value
    size_t colon_pos = json.find(":", text_pos);
    if (colon_pos == std::string::npos) return false;
    
    size_t quote_start = json.find("\"", colon_pos);
    if (quote_start == std::string::npos) return false;
    
    size_t url_start = quote_start + 1;
    size_t quote_end = json.find("\"", url_start);
    if (quote_end == std::string::npos) return false;
    
    artwork_url = json.substr(url_start, quote_end - url_start);
    
    // Unescape JSON forward slashes (replace \/ with /)
    size_t pos = 0;
    while ((pos = artwork_url.find("\\/", pos)) != std::string::npos) {
        artwork_url.replace(pos, 2, "/");
        pos += 1;
    }
    
    // Validate URL
    if (artwork_url.empty() || artwork_url.find("http") != 0) {
#ifdef _DEBUG
#endif
        return false;
    }
    
#ifdef _DEBUG
#endif
    
    return true;
}

bool parse_musicbrainz_json_response(const std::string& json, std::string& release_mbid) {
    // Simple JSON parsing for MusicBrainz response
    // Look for "recordings" array and get the first release MBID
    
    size_t recordings_pos = json.find("\"recordings\"");
    if (recordings_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find releases array within the first recording
    size_t releases_pos = json.find("\"releases\"", recordings_pos);
    if (releases_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find the first release MBID
    size_t id_pos = json.find("\"id\"", releases_pos);
    if (id_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find the MBID value
    size_t colon_pos = json.find(":", id_pos);
    if (colon_pos == std::string::npos) return false;
    
    size_t quote_start = json.find("\"", colon_pos);
    if (quote_start == std::string::npos) return false;
    
    size_t mbid_start = quote_start + 1;
    size_t quote_end = json.find("\"", mbid_start);
    if (quote_end == std::string::npos) return false;
    
    release_mbid = json.substr(mbid_start, quote_end - mbid_start);
    
    // Validate MBID format (should be a UUID)
    if (release_mbid.length() != 36) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    
    return true;
}

bool parse_discogs_json_response(const std::string& json, std::string& artwork_url) {
    // Discogs JSON parsing based on working Default UI implementation
    // Look for "results" array and "cover_image" field
    
    size_t results_pos = json.find("\"results\"");
    if (results_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Find the opening bracket for the results array
    size_t bracket_pos = json.find("[", results_pos);
    if (bracket_pos == std::string::npos) {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Check if results array is empty by looking for immediate closing bracket
    size_t check_pos = bracket_pos + 1;
    while (check_pos < json.length() && (json[check_pos] == ' ' || json[check_pos] == '\n' || json[check_pos] == '\t')) {
        check_pos++;
    }
    if (check_pos < json.length() && json[check_pos] == ']') {
#ifdef _DEBUG
#endif
        return false;
    }
    
    // Look for cover_image field (try multiple results like the main component)
    std::string search_start = json.substr(bracket_pos);
    
    for (int result_index = 0; result_index < 5; result_index++) {
        size_t cover_image_pos = search_start.find("\"cover_image\"");
        if (cover_image_pos == std::string::npos) {
            break; // No more cover_image fields found
        }
        
        // Find the colon and opening quote
        size_t colon_pos = search_start.find(":", cover_image_pos);
        if (colon_pos == std::string::npos) {
            search_start = search_start.substr(cover_image_pos + 13); // Move past "cover_image"
            continue;
        }
        
        size_t quote_start = search_start.find("\"", colon_pos);
        if (quote_start == std::string::npos) {
            search_start = search_start.substr(colon_pos + 1);
            continue;
        }
        
        size_t url_start = quote_start + 1;
        size_t quote_end = search_start.find("\"", url_start);
        if (quote_end == std::string::npos) {
            search_start = search_start.substr(url_start);
            continue;
        }
        
        artwork_url = search_start.substr(url_start, quote_end - url_start);
        
        // Unescape JSON forward slashes (replace \/ with /)
        size_t pos = 0;
        while ((pos = artwork_url.find("\\/", pos)) != std::string::npos) {
            artwork_url.replace(pos, 2, "/");
            pos += 1;
        }
        
        // Validate URL - skip empty or null values
        if (!artwork_url.empty() && artwork_url != "null" && artwork_url.find("http") == 0) {
            return true;
        }
        
        // This result didn't have a valid URL, try the next one
        search_start = search_start.substr(quote_end + 1);
    }
    
#ifdef _DEBUG
#endif
    return false;
}

bool create_bitmap_from_image_data(const std::vector<BYTE>& data) {
    try {
#ifdef _DEBUG
#endif        
        if (data.empty()) {
#ifdef _DEBUG
#endif
            return false;
        }
        
        // Create IStream from memory
        IStream* pStream = nullptr;
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, data.size());
        if (!hGlobal) {
#ifdef _DEBUG
#endif
            return false;
        }
#ifdef _DEBUG        
#endif
        
        void* pData = GlobalLock(hGlobal);
        if (!pData) {
            GlobalFree(hGlobal);
#ifdef _DEBUG
#endif
            return false;
        }
        
        memcpy(pData, data.data(), data.size());
        GlobalUnlock(hGlobal);
        
        if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) != S_OK) {
            GlobalFree(hGlobal);
#ifdef _DEBUG
#endif
            return false;
        }
#ifdef _DEBUG        
#endif
        
        // Create GDI+ bitmap from stream
        Gdiplus::Bitmap* bitmap = new Gdiplus::Bitmap(pStream);
        pStream->Release();
        
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
            if (bitmap) {
                delete bitmap;
            } else {
#ifdef _DEBUG
#endif
            }
            return false;
        }
        
#ifdef _DEBUG
#endif
        
        // Convert to HBITMAP and store in shared bitmap
        HBITMAP hBitmap = nullptr;
        if (bitmap->GetHBITMAP(NULL, &hBitmap) == Gdiplus::Ok && hBitmap) {
            // Clean up old shared bitmap
            if (g_shared_artwork_bitmap) {
                DeleteObject(g_shared_artwork_bitmap);
            }
            
            g_shared_artwork_bitmap = hBitmap;
#ifdef _DEBUG
#endif
            
            // Notify event system that artwork was loaded successfully
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_LOADED, 
                hBitmap, 
                g_current_artwork_source.c_str(), 
                "", 
                ""
            ));
#ifdef _DEBUG
#endif
            
            delete bitmap;
            return true;
        }
        
        delete bitmap;
#ifdef _DEBUG
#endif
        return false;
        
    } catch (...) {
#ifdef _DEBUG
#endif
        return false;
    }
}

// Bridge HTTP and download functions (simplified versions for bridge usage)

bool bridge_http_get_request(const std::string& url, std::string& response) {
    try {
        response.clear();
        
        // Determine which API this request is for and use appropriate manager
        HttpRequestManager* request_manager = get_request_manager_for_api(url);
        auto request_lock = request_manager->acquire_lock();
        
        // Convert URL to wide string
        std::wstring wide_url(url.begin(), url.end());
        
        // Parse URL components
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
#ifdef _DEBUG
#endif
            return false;
        }
        
        HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
#ifdef _DEBUG
#endif
            return false;
        }
        
        // Set timeouts to prevent application freezing
        // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
        WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
        
        std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
        HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
        
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.lpszExtraInfo) {
            path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }
        
        DWORD dwFlags = 0;
        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            dwFlags = WINHTTP_FLAG_SECURE;
        }
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                               NULL, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               dwFlags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        bool success = false;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    if (dwSize == 0) break;
                    
                    std::vector<char> buffer(dwSize + 1);
                    if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                    
                    buffer[dwDownloaded] = '\0';
                    response += buffer.data();
                    
                } while (dwSize > 0);
                
                success = true;
            }
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return success;
        
    } catch (...) {
        return false;
    }
}

bool bridge_http_get_request_with_useragent(const std::string& url, std::string& response, const std::string& user_agent) {
    try {
        response.clear();
        
        // Determine which API this request is for and use appropriate manager
        HttpRequestManager* request_manager = get_request_manager_for_api(url);
        auto request_lock = request_manager->acquire_lock();
        
        // Convert URL to wide string
        std::wstring wide_url(url.begin(), url.end());
        
        // Parse URL components
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
#ifdef _DEBUG
#endif
            return false;
        }
        
        HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
#ifdef _DEBUG
#endif
            return false;
        }
        
        // Set timeouts to prevent application freezing
        // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
        WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
        
        std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
        HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
        
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.lpszExtraInfo) {
            path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }
        
        DWORD dwFlags = 0;
        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            dwFlags = WINHTTP_FLAG_SECURE;
        }
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                               NULL, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               dwFlags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        // Add User-Agent header
        std::wstring wide_user_agent(user_agent.begin(), user_agent.end());
        std::wstring headers = L"User-Agent: " + wide_user_agent + L"\r\n";
        
        bool success = false;
        if (WinHttpSendRequest(hRequest, headers.c_str(), headers.length(),
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    if (dwSize == 0) break;
                    
                    std::vector<char> buffer(dwSize + 1);
                    if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                    
                    buffer[dwDownloaded] = '\0';
                    response += buffer.data();
                    
                } while (dwSize > 0);
                
                success = true;
            }
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return success;
        
    } catch (...) {
        return false;
    }
}

bool bridge_download_image(const std::string& url, std::vector<BYTE>& data) {
    try {
        data.clear();
        
        // Determine which API this request is for and use appropriate manager
        HttpRequestManager* request_manager = get_request_manager_for_api(url);
        auto request_lock = request_manager->acquire_lock();
        
        // Convert URL to wide string
        std::wstring wide_url(url.begin(), url.end());
        
        // Parse URL components
        URL_COMPONENTS urlComp = {};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwSchemeLength = -1;
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        urlComp.dwExtraInfoLength = -1;
        
        if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &urlComp)) {
#ifdef _DEBUG
    #endif
            return false;
        }
        
        
        HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                       WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
#ifdef _DEBUG
#endif
            return false;
        }
        
        // Set timeouts to prevent application freezing
        // DNS resolution: 10s, Connect: 10s, Send: 15s, Receive: 30s
        WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);
        
        
        std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
        HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
        
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
        if (urlComp.lpszExtraInfo) {
            path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
        }
        
        DWORD dwFlags = 0;
        if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
            dwFlags = WINHTTP_FLAG_SECURE;
        }
        
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                               NULL, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               dwFlags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        // Add headers for image requests
        const wchar_t* headers = L"Accept: image/jpeg, image/png, image/gif, image/webp, image/*\r\n";
        WinHttpAddRequestHeaders(hRequest, headers, -1, WINHTTP_ADDREQ_FLAG_ADD);
        
        bool success = false;
        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                const DWORD CHUNK_SIZE = 8192;
                BYTE buffer[CHUNK_SIZE];
                
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    if (dwSize == 0) break;
                    
                    DWORD chunkSize = min(dwSize, CHUNK_SIZE);
                    if (!WinHttpReadData(hRequest, buffer, chunkSize, &dwDownloaded)) break;
                    
                    // Limit max download size to prevent memory issues
                    if (data.size() + dwDownloaded > 10 * 1024 * 1024) break; // 10MB limit
                    
                    size_t old_size = data.size();
                    data.resize(old_size + dwDownloaded);
                    memcpy(data.data() + old_size, buffer, dwDownloaded);
                    
                } while (dwSize > 0);
                
                success = !data.empty();
                
            } else {
            }
        } else {
        }
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        return success;
        
    } catch (...) {
        return false;
    }
}

// Station name detection helper function with SomaFM-specific patterns
bool is_station_name(const pfc::string8& artist, const pfc::string8& title) {
    // Only check titles when artist is empty (station name pattern)
    if (!artist.is_empty() || title.is_empty()) {
        return false;
    }
    
    // Pattern 1: Ends with exclamation mark (like "Indie Pop Rocks!")
    if (title.length() > 0 && title[title.length() - 1] == '!') {
        return true;
    }
    
    // Pattern 2: SomaFM-specific station name patterns
    // These are actual SomaFM station names that should be detected
    const char* somafm_stations[] = {
        "Indie Pop Rocks!",
        "Groove Salad",
        "Drone Zone",
        "Lush",
        "Beat Blender",
        "Sonic Universe",
        "Space Station Soma",
        "Suburbs of Goa",
        "Mission Control",
        "Doomed",
        "The Trip",
        "Covers",
        "Folk Forward",
        "Fluid",
        "Poptron",
        "Cliq Hop",
        "Digitalis",
        "Dubstep Beyond",
        "Def Con Radio",
        "Seven Inch Soul",
        "Secret Agent",
        "Bagel Radio",
        "Illinois Street Lounge",
        "Bootliquor",
        "Thistle Radio",
        "Deep Space One",
        "Left Coast 70s",
        "Earwaves",
        "Concrete Jungle",
        "Department Store Christmas",
        "Christmas Lounge",
        "Christmas Rocks!",
        "BAGeL Radio",
        "PopTron",
        "Metal Detector",
        "Heavyweight",
        "Jolly Ol' Soul",
        "Xmas in Frisko",
        "Underground 80s",
        "Smooth Jazz",
        "SF 10-33",
        "Sonic Shivers",
        "Frost",
        "Flipside",
        "Vaporwaves",
        "Tiki Lounge",
        "Dub Step Beyond",
        "Specials",
        "Streaming Soundtracks"
    };
    
    // Check for exact SomaFM station name matches (case-insensitive)
    for (const char* station : somafm_stations) {
        if (_stricmp(title.c_str(), station) == 0) {
            return true;
        }
    }
    
    // Pattern 3: General radio/stream keywords for other stations
    const char* station_keywords[] = {
        "Radio", "FM", "Stream", "Station", "Music", "Rocks", "Hits", "Channel", "Chill", "Out",
        "Lounge", "Jazz", "Classical", "Electronic", "Dance", "House", "Techno", "Ambient",
        "Zone", "Universe", "Space", "Deep", "Underground", "Alternative", "Indie"
    };
    
    // Manual case-insensitive substring search
    std::string title_str = title.c_str();
    std::transform(title_str.begin(), title_str.end(), title_str.begin(), ::toupper);
    
    for (const char* keyword : station_keywords) {
        std::string keyword_str = keyword;
        std::transform(keyword_str.begin(), keyword_str.end(), keyword_str.begin(), ::toupper);
        
        if (title_str.find(keyword_str) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// Safe internet stream detection to prevent crashes
bool is_safe_internet_stream(metadb_handle_ptr track) {
    try {
        if (!track.is_valid()) {
            return false;
        }
        
        pfc::string8 path = track->get_path();
        if (path.is_empty()) {
            return false;
        }
        
        const char* path_cstr = path.c_str();
        if (!path_cstr || strlen(path_cstr) == 0) {
            return false;
        }
        
        // Check for protocol indicators
        const char* protocol_pos = strstr(path_cstr, "://");
        if (!protocol_pos) {
            return false;  // No protocol found
        }
        
        // Check mtag file internet streams
        const double length = track->get_length();
        if (strstr(path.c_str(), "://")) {
            // Has protocol - check if it's a local file protocol and is mtag without duration
            if ((strstr(path.c_str(), "file://") == path.c_str()) && (strstr(path.c_str(), ".tags")) && (length <= 0)) {
                return true; // mtag internet stream
            }
        }

        // Exclude file:// protocol
        const char* file_pos = strstr(path_cstr, "file://");
        if (file_pos == path_cstr) {
            return false;  // This is a file:// URL
        }
        
        return true;  // This appears to be an internet stream
        
    } catch (...) {
        // If any exception occurs during path checking, assume not a stream
        return false;
    }
}

//=============================================================================
// Columns UI Panel Implementation - moved to separate file
//=============================================================================

// CUI implementation is now in artwork_panel_cui.cpp
