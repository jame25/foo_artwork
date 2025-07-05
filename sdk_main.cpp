#include "stdafx.h"
#include <algorithm>
#include <thread>
#include <vector>

// Configuration variable GUIDs
static constexpr GUID guid_cfg_enable_itunes = { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };
static constexpr GUID guid_cfg_enable_discogs = { 0x12345679, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf1 } };
static constexpr GUID guid_cfg_enable_lastfm = { 0x1234567a, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf2 } };
static constexpr GUID guid_cfg_discogs_key = { 0x1234567c, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf4 } };
static constexpr GUID guid_cfg_discogs_consumer_key = { 0x1234567e, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf6 } };
static constexpr GUID guid_cfg_discogs_consumer_secret = { 0x1234567f, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf7 } };
static constexpr GUID guid_cfg_lastfm_key = { 0x1234567d, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf5 } };
static constexpr GUID guid_cfg_fill_mode = { 0x12345680, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };

// Configuration variables with default values
cfg_bool cfg_enable_itunes(guid_cfg_enable_itunes, true);
cfg_bool cfg_enable_discogs(guid_cfg_enable_discogs, true);
cfg_bool cfg_enable_lastfm(guid_cfg_enable_lastfm, true);
cfg_string cfg_discogs_key(guid_cfg_discogs_key, "");
cfg_string cfg_discogs_consumer_key(guid_cfg_discogs_consumer_key, "");
cfg_string cfg_discogs_consumer_secret(guid_cfg_discogs_consumer_secret, "");
cfg_string cfg_lastfm_key(guid_cfg_lastfm_key, "");
cfg_bool cfg_fill_mode(guid_cfg_fill_mode, true);  // true = fill window (crop), false = fit window (letterbox)

// Forward declaration
class artwork_ui_element;

// Component's DLL instance handle
HINSTANCE g_hIns = NULL;

// DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hIns = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

// Component version declaration using the proper SDK macro
DECLARE_COMPONENT_VERSION(
    "Artwork Display",
    "1.0.5",
    "Cover artwork display component for foobar2000.\n"
    "Features:\n"
    "- Local artwork search (Cover.jpg, folder.jpg, etc.)\n"
    "- Online API fallback (iTunes, Discogs, Last.fm)\n"
    "- Smart metadata cleaning for better API results\n"
    "- Configurable preferences\n\n"
    "Author: jame25\n"
    "Build date: " __DATE__ "\n\n"
    "This component displays cover artwork for the currently playing track."
);

// Validate component compatibility using the proper SDK macro
VALIDATE_COMPONENT_FILENAME("foo_artwork.dll");

// Artwork manager class
class artwork_manager {
private:
    static artwork_manager* instance;
    
public:
    static artwork_manager& get_instance() {
        if (!instance) {
            instance = new artwork_manager();
        }
        return *instance;
    }
    
    void initialize() {
        // Initialize artwork system
    }
    
    void cleanup() {
        // Clean up artwork resources
    }
    
    void update_artwork(metadb_handle_ptr track) {
        if (!track.is_valid()) return;
        
        // Get track information safely
        file_info_impl info;
        if (track->get_info(info)) {
            pfc::string8 artist, album, title;
            
            // Get metadata fields
            if (info.meta_exists("ARTIST")) {
                artist = info.meta_get("ARTIST", 0);
            }
            if (info.meta_exists("ALBUM")) {
                album = info.meta_get("ALBUM", 0);
            }
            if (info.meta_exists("TITLE")) {
                title = info.meta_get("TITLE", 0);
            }
            
            // Build display string
            pfc::string8 display_info;
            display_info << "Artwork component: ";
            if (artist.length() > 0) display_info << artist;
            if (title.length() > 0) {
                if (artist.length() > 0) display_info << " - ";
                display_info << title;
            }
            if (album.length() > 0) {
                display_info << " (" << album << ")";
            }
            
            // Output to debug console
            OutputDebugStringA(display_info.c_str());
        }
    }
};

artwork_manager* artwork_manager::instance = nullptr;

// Global list to track artwork UI elements
static pfc::list_t<artwork_ui_element*> g_artwork_ui_elements;

// Artwork initialization handler
class artwork_init : public initquit {
private:
    ULONG_PTR m_gdiplusToken;
    
public:
    void on_init() override {
        // Initialize GDI+
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
        
        // Initialize artwork component
        artwork_manager::get_instance().initialize();
    }
    
    void on_quit() override {
        // Clean up artwork component
        artwork_manager::get_instance().cleanup();
        
        // Shutdown GDI+
        GdiplusShutdown(m_gdiplusToken);
    }
};

// UI Element for displaying artwork
class artwork_ui_element : public service_impl_single_t<ui_element_instance> {
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
    DWORD m_last_update_timestamp;  // Timestamp for debouncing updates
    metadb_handle_ptr m_last_update_track;  // Track handle for smart debouncing
    pfc::string8 m_last_update_content;  // Content (artist|title) for smart debouncing

public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
private:
    void paint_artwork(HDC hdc);
    void load_artwork_for_track(metadb_handle_ptr track);
    
public:
    void search_itunes_artwork(metadb_handle_ptr track);
    void search_itunes_background(pfc::string8 artist, pfc::string8 title);
    void search_discogs_artwork(metadb_handle_ptr track);
    void search_discogs_background(pfc::string8 artist, pfc::string8 title);
    void search_lastfm_artwork(metadb_handle_ptr track);
    void search_lastfm_background(pfc::string8 artist, pfc::string8 title);
    
    // Helper functions for APIs
    pfc::string8 url_encode(const pfc::string8& str);
    bool http_get_request(const pfc::string8& url, pfc::string8& response);
    bool http_get_request_binary(const pfc::string8& url, std::vector<BYTE>& data);
    bool http_get_request_with_discogs_auth(const pfc::string8& url, pfc::string8& response);
    void extract_metadata_for_search(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title);
    bool parse_itunes_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_discogs_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_discogs_release_details(const pfc::string8& json, pfc::string8& artwork_url);
    bool parse_lastfm_response(const pfc::string8& json, pfc::string8& artwork_url);
    bool download_image(const pfc::string8& url, std::vector<BYTE>& data);
    bool create_bitmap_from_data(const std::vector<BYTE>& data);
    pfc::string8 clean_metadata_text(const pfc::string8& text);
    void complete_artwork_search();  // Helper to complete cache management
    
public:
    // GUID for our element
    static const GUID g_guid;
    artwork_ui_element(HWND parent, ui_element_config::ptr config, ui_element_instance_callback::ptr callback)
        : m_config(config), m_callback(callback), m_hWnd(NULL), m_artwork_bitmap(NULL), m_last_update_timestamp(0) {
        
        // Remove jarring "No track playing" message
        // m_status_text = "No track playing";
        
        // Register a custom window class for artwork display
        static bool class_registered = false;
        if (!class_registered) {
            WNDCLASS wc = {};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = g_hIns;
            wc.lpszClassName = L"foo_artwork_window";
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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
        }
    }
    
    ~artwork_ui_element() {
        // Remove from global list
        g_artwork_ui_elements.remove_item(this);
        
        // Clean up bitmap
        if (m_artwork_bitmap) {
            DeleteObject(m_artwork_bitmap);
            m_artwork_bitmap = NULL;
        }
        
        // Destroy window
        if (m_hWnd) {
            DestroyWindow(m_hWnd);
            m_hWnd = NULL;
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
        
        pfc::string8 debug;
        debug << "UI Element: update_track called\n";
        OutputDebugStringA(debug);
        
        // Smart debouncing - only update if track or content actually changed
        DWORD current_time = GetTickCount();
        bool track_changed = (track != m_last_update_track);
        
        // Extract current content for comparison
        pfc::string8 current_artist, current_title;
        extract_metadata_for_search(track, current_artist, current_title);
        pfc::string8 current_content = current_artist + "|" + current_title;
        
        bool content_changed = (current_content != m_last_update_content);
        bool enough_time_passed = (current_time - m_last_update_timestamp) > 1000; // 1 second
        
        if (!track_changed && !content_changed && !enough_time_passed) {
            pfc::string8 debounce_debug;
            debounce_debug << "UI Element: update_track debounced (same track+content, too soon)\n";
            OutputDebugStringA(debounce_debug);
            return;
        }
        
        // Update tracking variables
        m_last_update_track = track;
        m_last_update_content = current_content;
        m_last_update_timestamp = current_time;
        
        // Don't clear previous artwork immediately - keep it until new artwork is loaded
        // This prevents white flash during transitions
        
        m_current_track = track;
        load_artwork_for_track(track);
    }
};

const GUID artwork_ui_element::g_guid = artwork_ui_element::g_get_guid();

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
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_USER + 1: // Artwork found
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_USER + 2: // iTunes search failed - try Last.fm
        if (cfg_enable_lastfm) {
            pThis->search_lastfm_artwork(pThis->m_current_track);
        } else if (cfg_enable_discogs) {
            pThis->search_discogs_artwork(pThis->m_current_track);
        } else {
            pThis->m_status_text = "No artwork found";
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_USER + 3: // Last.fm search failed - try Discogs
        if (cfg_enable_discogs) {
            pThis->search_discogs_artwork(pThis->m_current_track);
        } else {
            pThis->m_status_text = "No artwork found";
            InvalidateRect(hwnd, NULL, TRUE);
        }
        return 0;
    case WM_USER + 4: // Discogs search failed
        pThis->m_status_text = "No artwork found";
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// Paint artwork
void artwork_ui_element::paint_artwork(HDC hdc) {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    
    // Use system colors
    COLORREF bg_color = GetSysColor(COLOR_WINDOW);
    COLORREF text_color = GetSysColor(COLOR_WINDOWTEXT);
    
    // Fill background
    HBRUSH bgBrush = CreateSolidBrush(bg_color);
    FillRect(hdc, &clientRect, bgBrush);
    DeleteObject(bgBrush);
    
    if (m_artwork_bitmap) {
        // Get bitmap dimensions
        BITMAP bm;
        GetObject(m_artwork_bitmap, sizeof(bm), &bm);
        
        // Calculate window and image dimensions
        int windowWidth = clientRect.right - clientRect.left;
        int windowHeight = clientRect.bottom - clientRect.top;
        
        // Avoid division by zero
        if (windowWidth <= 0 || windowHeight <= 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
            return;
        }
        
        // Calculate aspect ratios
        double windowAspect = (double)windowWidth / windowHeight;
        double imageAspect = (double)bm.bmWidth / bm.bmHeight;
        
        // Calculate scaling factors
        double scaleX = (double)windowWidth / bm.bmWidth;
        double scaleY = (double)windowHeight / bm.bmHeight;
        
        // Choose scaling mode based on configuration
        double scale;
        if (cfg_fill_mode) {
            // Fill mode: Scale to fill window (may crop image, minimizes white space)
            // Use the larger scale factor to ensure the window is completely filled
            scale = (scaleX > scaleY) ? scaleX : scaleY;
        } else {
            // Fit mode: Scale to fit window (shows full image, may have letterboxing)
            // Use the smaller scale factor to ensure the entire image is visible
            scale = (scaleX < scaleY) ? scaleX : scaleY;
        }
        
        // Limit maximum scale to prevent excessive enlargement of small images
        if (scale > 10.0) {
            scale = 10.0;
        }
        
        // Limit minimum scale to prevent images from becoming too small
        if (scale < 0.1) {
            scale = 0.1;
        }
        
        // Calculate final dimensions with proper rounding
        int scaledWidth = (int)(bm.bmWidth * scale + 0.5);
        int scaledHeight = (int)(bm.bmHeight * scale + 0.5);
        
        // Ensure minimum size to prevent invisible images
        if (scaledWidth < 1) scaledWidth = 1;
        if (scaledHeight < 1) scaledHeight = 1;
        
        // Center the image within the window
        int x = (windowWidth - scaledWidth) / 2;
        int y = (windowHeight - scaledHeight) / 2;
        
        // Clamp position to prevent drawing outside the window
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + scaledWidth > windowWidth) x = windowWidth - scaledWidth;
        if (y + scaledHeight > windowHeight) y = windowHeight - scaledHeight;
        
        // Create compatible DC and select bitmap
        HDC memDC = CreateCompatibleDC(hdc);
        if (memDC) {
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, m_artwork_bitmap);
            
            // Set stretch mode for better quality
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, NULL);
            
            // Draw the scaled bitmap
            StretchBlt(hdc, x, y, scaledWidth, scaledHeight, memDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            
            // Clean up
            SelectObject(memDC, oldBitmap);
            DeleteDC(memDC);
        }
    } else {
        // Draw status text
        SetBkColor(hdc, bg_color);
        SetTextColor(hdc, text_color);
        DrawTextA(hdc, m_status_text.c_str(), -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

// Load artwork for track
void artwork_ui_element::load_artwork_for_track(metadb_handle_ptr track) {
    if (!track.is_valid()) {
        m_status_text = "No track";
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        return;
    }
    
    // Create cache key from track metadata to prevent repeated searches
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    pfc::string8 search_key = artist + "|" + title;
    
    // Check if we already have artwork for this search or search is in progress
    static DWORD last_search_time = 0;
    DWORD current_time = GetTickCount();
    bool enough_time_passed = (current_time - last_search_time) > 30000; // 30 seconds before re-searching
    
    // Don't search if we already completed this search recently
    if ((search_key == m_last_search_key || search_key == m_current_search_key) && !enough_time_passed) {
        pfc::string8 debug;
        if (search_key == m_current_search_key) {
            debug << "Artwork search already in progress for: " << search_key << "\n";
        } else {
            debug << "Artwork search already completed for: " << search_key << "\n";
        }
        OutputDebugStringA(debug);
        return;
    }
    
    // Clear any stale search state
    m_current_search_key = "";
    
    // Update search timestamp
    last_search_time = current_time;
    
    // Mark this search as in progress
    m_current_search_key = search_key;
    
    pfc::string8 debug;
    debug << "Starting artwork search for: " << search_key << "\n";
    OutputDebugStringA(debug);
    
    // Don't clear previous artwork here - keep it until new artwork is successfully loaded
    // This prevents white transition screens
    
    // Remove jarring "Loading artwork..." message
    // m_status_text = "Loading artwork...";
    // if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
    
    // Try to get local artwork first
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
                    m_status_text = "Local artwork loaded";
                    if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
                    return;
                }
            }
        } catch (...) {
            // Local artwork failed, continue to online search
        }
    } catch (...) {
        // Album art manager failed
    }
    
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
        // Try iTunes API first (if enabled)
        if (cfg_enable_itunes) {
            search_itunes_artwork(track);
        }
        // If iTunes failed or disabled, try Last.fm API (if enabled)
        else if (cfg_enable_lastfm) {
            search_lastfm_artwork(track);
        }
        // If both iTunes and Last.fm failed/disabled, try Discogs API (if enabled)
        else if (cfg_enable_discogs) {
            search_discogs_artwork(track);
        }
        else {
            // No APIs enabled - mark search as complete to prevent retries
            complete_artwork_search();
            m_status_text = "No artwork APIs enabled";
            if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        }
    } else {
        // Not searching - mark as complete to prevent cache issues
        complete_artwork_search();
    }
    
    m_current_track = track;
}

// iTunes API artwork search - FIXED to use cleaned metadata
void artwork_ui_element::search_itunes_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return;
    
    // Use the unified metadata extraction function that includes cleaning
    pfc::string8 artist, title;
    extract_metadata_for_search(track, artist, title);
    
    // Check if we have valid metadata for search
    if (artist.is_empty() || title.is_empty()) {
        return; // No point searching without both artist and title
    }
    
    // Remove jarring "Searching iTunes API..." message
    // m_status_text = "Searching iTunes API...";
    // if (m_hWnd) {
    //     InvalidateRect(m_hWnd, NULL, TRUE);
    // }
    
    // Debug the values being passed to background thread
    pfc::string8 thread_debug;
    thread_debug << "Passing to background thread - Artist: '" << artist << "', Title: '" << title << "'\n";
    OutputDebugStringA(thread_debug);
    
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
    if (artist.is_empty() || title.is_empty()) {
        return; // No point searching without both artist and title
    }
    
    // Remove jarring "Searching Discogs API..." message
    // m_status_text = "Searching Discogs API...";
    // if (m_hWnd) {
    //     InvalidateRect(m_hWnd, NULL, TRUE);
    // }
    
    // Debug the values being passed to background thread
    pfc::string8 thread_debug;
    thread_debug << "Discogs search - Artist: '" << artist << "', Title: '" << title << "'\n";
    OutputDebugStringA(thread_debug);
    
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
    if (artist.is_empty() || title.is_empty()) {
        return; // No point searching without both artist and title
    }
    
    // Remove jarring "Searching Last.fm API..." message
    // m_status_text = "Searching Last.fm API...";
    // if (m_hWnd) {
    //     InvalidateRect(m_hWnd, NULL, TRUE);
    // }
    
    // Debug the values being passed to background thread
    pfc::string8 thread_debug;
    thread_debug << "Last.fm search - Artist: '" << artist << "', Title: '" << title << "'\n";
    OutputDebugStringA(thread_debug);
    
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
        pfc::string8 encode_debug;
        encode_debug << "Before encoding - Artist: '" << artist << "', Title: '" << title << "'\n";
        encode_debug << "Artist length: " << artist.length() << ", Title length: " << title.length() << "\n";
        OutputDebugStringA(encode_debug);
        
        pfc::string8 encoded_artist = url_encode(artist);
        pfc::string8 encoded_title = url_encode(title);
        
        pfc::string8 encoded_debug;
        encoded_debug << "After encoding - Artist: '" << encoded_artist << "', Title: '" << encoded_title << "'\n";
        OutputDebugStringA(encoded_debug);
        
        // Build iTunes search URL
        pfc::string8 search_url;
        search_url << "https://itunes.apple.com/search?term=" << encoded_artist << "%20" << encoded_title << "&media=music&entity=song&limit=1";
        
        pfc::string8 search_debug;
        search_debug << "iTunes search URL: " << search_url << "\n";
        OutputDebugStringA(search_debug);
        
        // Make HTTP request
        pfc::string8 response;
        if (http_get_request(search_url, response)) {
            pfc::string8 response_debug;
            size_t response_preview_len = response.length() < 200 ? response.length() : 200;
            response_debug << "iTunes API response (first 200 chars): " << pfc::string8(response.c_str(), response_preview_len) << "\n";
            OutputDebugStringA(response_debug);
            
            // Parse JSON response and extract artwork URL
            pfc::string8 artwork_url;
            if (parse_itunes_response(response, artwork_url)) {
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (download_image(artwork_url, image_data)) {
                    // Create bitmap from downloaded data
                    if (create_bitmap_from_data(image_data)) {
                        complete_artwork_search();  // Mark cache as completed
                        // Update UI on main thread
                        if (m_hWnd) {
                            PostMessage(m_hWnd, WM_USER + 1, 0, 0);
                        }
                        return;
                    }
                }
            } else {
                pfc::string8 no_results_debug;
                no_results_debug << "iTunes search failed - trying next API in chain\n";
                OutputDebugStringA(no_results_debug);
            }
        } else {
            pfc::string8 http_debug;
            http_debug << "iTunes HTTP request failed for URL: " << search_url << "\n";
            OutputDebugStringA(http_debug);
        }
        
        // Failed to get artwork
        m_status_text = "iTunes search failed";
        if (m_hWnd) {
            PostMessage(m_hWnd, WM_USER + 2, 0, 0);
        }
        
    } catch (...) {
        m_status_text = "iTunes search error";
        if (m_hWnd) {
            PostMessage(m_hWnd, WM_USER + 2, 0, 0);
        }
    }
}

// Background Discogs API search
void artwork_ui_element::search_discogs_background(pfc::string8 artist, pfc::string8 title) {
    try {
        pfc::string8 search_terms = artist + " - " + title;
        pfc::string8 encoded_search = url_encode(search_terms);
        
        pfc::string8 search_debug;
        search_debug << "Discogs search for: " << search_terms << "\n";
        search_debug << "Discogs search terms - Artist: '" << artist << "' Title: '" << title << "'\n";
        OutputDebugStringA(search_debug);
        
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
            OutputDebugStringA("Discogs: No API credentials configured\n");
            complete_artwork_search();
            return;
        }
        
        pfc::string8 request_debug;
        request_debug << "Discogs request URL: " << search_url << "\n";
        OutputDebugStringA(request_debug);
        
        // Make HTTP request
        pfc::string8 response;
        if (http_get_request(search_url, response)) {
            pfc::string8 response_debug;
            size_t response_preview_len = response.length() < 300 ? response.length() : 300;
            response_debug << "Discogs API response (first 300 chars): " << pfc::string8(response.c_str(), response_preview_len) << "\n";
            OutputDebugStringA(response_debug);
            
            // Parse JSON response and extract artwork URL
            pfc::string8 artwork_url;
            if (parse_discogs_response(response, artwork_url)) {
                // Download the artwork image
                std::vector<BYTE> image_data;
                if (download_image(artwork_url, image_data)) {
                    // Create bitmap from downloaded data
                    if (create_bitmap_from_data(image_data)) {
                        complete_artwork_search();  // Mark cache as completed
                        // Update UI on main thread
                        if (m_hWnd) {
                            PostMessage(m_hWnd, WM_USER + 1, 0, 0);
                        }
                        return;
                    }
                }
            } else {
                OutputDebugStringA("Discogs: Failed to parse response or no artwork found\n");
            }
        } else {
            OutputDebugStringA("Discogs: HTTP request failed\n");
        }
        
        // Failed to get artwork - mark search as completed
        complete_artwork_search();
        m_status_text = "Discogs search failed";
        if (m_hWnd) {
            PostMessage(m_hWnd, WM_USER + 4, 0, 0);
        }
        
    } catch (...) {
        complete_artwork_search();  // Mark cache as completed
        m_status_text = "Discogs search error";
        if (m_hWnd) {
            PostMessage(m_hWnd, WM_USER + 4, 0, 0);
        }
    }
}

// Background Last.fm API search
void artwork_ui_element::search_lastfm_background(pfc::string8 artist, pfc::string8 title) {
    try {
        if (cfg_lastfm_key.is_empty()) {
            m_status_text = "Last.fm API key not configured";
            if (m_hWnd) {
                PostMessage(m_hWnd, WM_USER + 4, 0, 0);
            }
            return;
        }
        
        // Build Last.fm API URL
        pfc::string8 search_url = "https://ws.audioscrobbler.com/2.0/?method=track.getInfo";
        search_url << "&artist=" << url_encode(artist);
        search_url << "&track=" << url_encode(title);
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
                    // Create bitmap from downloaded data
                    if (create_bitmap_from_data(image_data)) {
                        complete_artwork_search();  // Mark cache as completed
                        // Update UI on main thread
                        if (m_hWnd) {
                            PostMessage(m_hWnd, WM_USER + 1, 0, 0);
                        }
                        return;
                    }
                }
            }
        }
        
        // Failed to get artwork
        m_status_text = "Last.fm search failed";
        if (m_hWnd) {
            PostMessage(m_hWnd, WM_USER + 3, 0, 0);
        }
        
    } catch (...) {
        m_status_text = "Last.fm search error";
        if (m_hWnd) {
            PostMessage(m_hWnd, WM_USER + 3, 0, 0);
        }
    }
}

// Extract metadata for search with comprehensive cleaning - ENHANCED VERSION
void artwork_ui_element::extract_metadata_for_search(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title) {
    artist = "";
    title = "";
    
    if (!track.is_valid()) return;
    
    // Use the same comprehensive extraction logic as track change detection
    // First try window title parsing (for internet radio and dynamic content)
    static auto find_fb2k_window_with_track_info = [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* result = reinterpret_cast<pfc::string8*>(lParam);
        
        wchar_t class_name[256];
        wchar_t window_text[512];
        
        if (GetClassNameW(hwnd, class_name, 256) && GetWindowTextW(hwnd, window_text, 512)) {
            if (wcsstr(window_text, L" - ") && wcsstr(window_text, L"[foobar2000]")) {
                pfc::stringcvt::string_utf8_from_wide text_utf8(window_text);
                *result = text_utf8;
                return FALSE;
            }
        }
        return TRUE;
    };
    
    pfc::string8 track_info_from_window;
    EnumWindows(find_fb2k_window_with_track_info, reinterpret_cast<LPARAM>(&track_info_from_window));
    
    bool extracted_from_window = false;
    
    if (!track_info_from_window.is_empty()) {
        pfc::string8 clean_track_info = track_info_from_window;
        const char* fb2k_suffix = strstr(clean_track_info.c_str(), " [foobar2000]");
        if (fb2k_suffix) {
            clean_track_info = pfc::string8(clean_track_info.c_str(), fb2k_suffix - clean_track_info.c_str());
        }
        
        const char* separator = strstr(clean_track_info.c_str(), " - ");
        if (separator) {
            size_t artist_len = separator - clean_track_info.c_str();
            if (artist_len > 0) {
                artist = pfc::string8(clean_track_info.c_str(), artist_len);
                while (artist.length() > 0 && artist[0] == ' ') artist = artist.subString(1);
                while (artist.length() > 0 && artist[artist.length()-1] == ' ') artist = artist.subString(0, artist.length()-1);
            }
            
            const char* title_start = separator + 3;
            if (strlen(title_start) > 0) {
                title = pfc::string8(title_start);
                while (title.length() > 0 && title[0] == ' ') title = title.subString(1);
                while (title.length() > 0 && title[title.length()-1] == ' ') title = title.subString(0, title.length()-1);
                extracted_from_window = true;
            }
        }
    }
    
    // Fallback to basic metadata if window parsing failed or returned empty values
    if (!extracted_from_window || artist.is_empty() || title.is_empty()) {
        try {
            file_info_impl info;
            track->get_info(info);
            
            if (info.meta_exists("ARTIST")) {
                artist = info.meta_get("ARTIST", 0);
            } else if (info.meta_exists("artist")) {
                artist = info.meta_get("artist", 0);
            }
            
            if (info.meta_exists("TITLE")) {
                title = info.meta_get("TITLE", 0);
            } else if (info.meta_exists("title")) {
                title = info.meta_get("title", 0);
            }
        } catch (...) {
            // If metadata access fails, keep empty values
        }
    }
    
    // Clean up common encoding issues and unwanted text - ENHANCED CLEANING
    artist = clean_metadata_text(artist);
    title = clean_metadata_text(title);
}

// Clean metadata text from encoding issues and unwanted content - ENHANCED VERSION
pfc::string8 artwork_ui_element::clean_metadata_text(const pfc::string8& text) {
    pfc::string8 cleaned = text;
    const char* text_cstr;
    
    pfc::string8 debug_before;
    debug_before << "clean_metadata_text - Input: '" << text << "'\n";
    OutputDebugStringA(debug_before);
    
    // Fix common encoding issues (using hex escape sequences)
    cleaned.replace_string("\xE2\x80\x99", "'");  // Right single quotation mark
    cleaned.replace_string("\xE2\x80\x9C", "\""); // Left double quotation mark
    cleaned.replace_string("\xE2\x80\x9D", "\""); // Right double quotation mark
    cleaned.replace_string("\xE2\x80\x93", "-");  // En dash
    cleaned.replace_string("\xE2\x80\x94", "-");  // Em dash
    cleaned.replace_string("\xE2\x80\xA6", "..."); // Horizontal ellipsis
    
    // Remove featuring artists (ft., feat., featuring, etc.) - ENHANCED PATTERNS
    // Patterns: various forms of featuring with different cases and spacing
    const char* featuring_patterns[] = {
        " ft.", " ft ", " feat.", " feat ", " featuring ", " Ft.", " Ft ", " Feat.", " Feat ", " Featuring ",
        " featuring.", " Featured ", " featured ", " feat. ", " ft. ", " Feat. ", " Ft. "
    };
    
    for (int i = 0; i < 17; i++) {
        const char* ft_pos = strstr(cleaned.c_str(), featuring_patterns[i]);
        if (ft_pos) {
            // Remove everything from the featuring pattern onwards
            cleaned = pfc::string8(cleaned.c_str(), ft_pos - cleaned.c_str());
            break;
        }
    }
    
    // Remove content in brackets/parentheses (remixes, versions, etc.)
    // Patterns: "(text)", "[text]"
    text_cstr = cleaned.c_str();
    const char* open_paren = strchr(text_cstr, '(');
    const char* open_bracket = strchr(text_cstr, '[');
    
    // Find the earliest bracket/parenthesis
    const char* earliest_bracket = nullptr;
    if (open_paren && open_bracket) {
        earliest_bracket = (open_paren < open_bracket) ? open_paren : open_bracket;
    } else if (open_paren) {
        earliest_bracket = open_paren;
    } else if (open_bracket) {
        earliest_bracket = open_bracket;
    }
    
    if (earliest_bracket) {
        cleaned = pfc::string8(text_cstr, earliest_bracket - text_cstr);
    }
    
    // Simplify multiple artists - keep only the first main artist
    // Pattern: "Artist1, Artist2" -> "Artist1"
    const char* comma_pos = strchr(cleaned.c_str(), ',');
    if (comma_pos) {
        cleaned = pfc::string8(cleaned.c_str(), comma_pos - cleaned.c_str());
    }
    
    // Remove timestamp patterns (like "- 0:00", "- 3:45", etc.) - ENHANCED VERSION
    text_cstr = cleaned.c_str();
    const char* timestamp_pattern = strstr(text_cstr, " - ");
    
    if (timestamp_pattern) {
        const char* after_dash = timestamp_pattern + 3;
        
        // Skip any whitespace after the dash
        while (*after_dash == ' ') {
            after_dash++;
        }
        
        // Check if it looks like a timestamp: digits:digits (potentially followed by whitespace)
        bool looks_like_timestamp = false;
        const char* colon_pos = strchr(after_dash, ':');
        
        if (colon_pos && colon_pos > after_dash) {
            // Check before colon: should be digits (1-3 digits for minutes/hours)
            bool digits_before = true;
            int digit_count_before = 0;
            for (const char* p = after_dash; p < colon_pos; p++) {
                if (*p < '0' || *p > '9') {
                    digits_before = false;
                    break;
                }
                digit_count_before++;
            }
            
            // Check after colon: should be 1-2 digits followed by end of string or whitespace
            bool digits_after = true;
            int digit_count_after = 0;
            const char* p = colon_pos + 1;
            while (*p >= '0' && *p <= '9') {
                digit_count_after++;
                p++;
            }
            
            // After digits, should only be whitespace or end of string
            while (*p == ' ') {
                p++;
            }
            
            if (*p != '\0') {
                digits_after = false;
            }
            
            // Valid timestamp: 1-3 digits before colon, 1-2 digits after colon
            if (digits_before && digits_after && 
                digit_count_before >= 1 && digit_count_before <= 3 &&
                digit_count_after >= 1 && digit_count_after <= 2) {
                looks_like_timestamp = true;
            }
        }
        
        // If it looks like a timestamp, remove it
        if (looks_like_timestamp) {
            cleaned = pfc::string8(text_cstr, timestamp_pattern - text_cstr);
        }
    }
    
    // Trim whitespace
    while (cleaned.length() > 0 && cleaned[0] == ' ') {
        cleaned = cleaned.subString(1);
    }
    while (cleaned.length() > 0 && cleaned[cleaned.length()-1] == ' ') {
        cleaned = cleaned.subString(0, cleaned.length()-1);
    }
    
    pfc::string8 debug_after;
    debug_after << "clean_metadata_text - Output: '" << cleaned << "'\n";
    OutputDebugStringA(debug_after);
    
    return cleaned;
}

// URL encoding helper function
pfc::string8 artwork_ui_element::url_encode(const pfc::string8& str) {
    pfc::string8 encoded;
    
    // Use pfc::string8's own length instead of strlen
    for (t_size i = 0; i < str.length(); i++) {
        char c = str[i];
        
        if (c == ' ') {
            encoded << "%20";
        } else {
            // Append character properly - create a single-character string
            char single_char[2] = {c, '\0'};
            encoded << single_char;
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

// HTTP GET request for binary data (images) using WinHTTP
bool artwork_ui_element::http_get_request_binary(const pfc::string8& url, std::vector<BYTE>& data) {
    bool success = false;
    data.clear();
    
    pfc::string8 debug;
    debug << "Binary HTTP request for: " << url << "\n";
    OutputDebugStringA(debug.c_str());
    
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
        OutputDebugStringA("Binary HTTP: Failed to crack URL\n");
        return false;
    }
    
    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        OutputDebugStringA("Binary HTTP: Failed to open session\n");
        return false;
    }
    
    // Set timeouts for image downloads (longer than text requests)
    WinHttpSetTimeouts(hSession, 30000, 30000, 30000, 60000); // 30s connect, 60s receive
    
    // Connect to the server
    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        OutputDebugStringA("Binary HTTP: Failed to connect\n");
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
        OutputDebugStringA("Binary HTTP: Failed to open request\n");
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
                
                if (dwContentLength > 0) {
                    data.reserve(dwContentLength);
                    pfc::string8 size_debug;
                    size_debug << "Binary HTTP: Expected content length: " << dwContentLength << " bytes\n";
                    OutputDebugStringA(size_debug.c_str());
                }
                
                DWORD dwSize = 0;
                DWORD dwDownloaded = 0;
                
                do {
                    // Check for available data
                    if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                    
                    if (dwSize == 0) break;
                    
                    // Allocate buffer for binary data
                    std::vector<BYTE> buffer(dwSize);
                    
                    // Read the binary data
                    if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
                    
                    // Append to data vector (no null termination needed for binary data)
                    data.insert(data.end(), buffer.begin(), buffer.begin() + dwDownloaded);
                    
                } while (dwSize > 0);
                
                success = (data.size() > 0);
                
                pfc::string8 result_debug;
                result_debug << "Binary HTTP: Downloaded " << data.size() << " bytes\n";
                OutputDebugStringA(result_debug.c_str());
                
                // Basic validation - check for common image file signatures
                if (success && data.size() >= 4) {
                    bool valid_image = false;
                    
                    // Check for JPEG signature (FF D8 FF)
                    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
                        valid_image = true;
                        OutputDebugStringA("Binary HTTP: Detected JPEG image\n");
                    }
                    // Check for PNG signature (89 50 4E 47)
                    else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
                        valid_image = true;
                        OutputDebugStringA("Binary HTTP: Detected PNG image\n");
                    }
                    // Check for GIF signature (47 49 46 38)
                    else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x38) {
                        valid_image = true;
                        OutputDebugStringA("Binary HTTP: Detected GIF image\n");
                    }
                    // Check for WebP signature (52 49 46 46)
                    else if (data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46) {
                        valid_image = true;
                        OutputDebugStringA("Binary HTTP: Detected WebP image\n");
                    }
                    
                    if (!valid_image) {
                        OutputDebugStringA("Binary HTTP: Warning - Downloaded data may not be a valid image\n");
                        // Don't fail here - let GDI+ handle the validation
                    }
                }
                
            } else {
                pfc::string8 status_debug;
                status_debug << "Binary HTTP: HTTP status code: " << dwStatusCode << "\n";
                OutputDebugStringA(status_debug.c_str());
            }
        } else {
            OutputDebugStringA("Binary HTTP: Failed to receive response\n");
        }
    } else {
        OutputDebugStringA("Binary HTTP: Failed to send request\n");
    }
    
    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return success;
}

// Simple JSON parser for iTunes response
bool artwork_ui_element::parse_itunes_response(const pfc::string8& json, pfc::string8& artwork_url) {
    pfc::string8 debug;
    debug << "iTunes JSON response parsing - Full JSON length: " << json.length() << "\n";
    OutputDebugStringA(debug);
    
    // First check if we have any results
    const char* result_count_key = "\"resultCount\":";
    const char* result_count_start = strstr(json.c_str(), result_count_key);
    if (result_count_start) {
        result_count_start += strlen(result_count_key);
        int result_count = atoi(result_count_start);
        
        pfc::string8 count_debug;
        count_debug << "iTunes result count: " << result_count << "\n";
        OutputDebugStringA(count_debug);
        
        if (result_count == 0) {
            OutputDebugStringA("iTunes: No results found\n");
            return false;
        }
    }
    
    // Try multiple artwork URL fields in order of preference (highest resolution first)
    const char* artwork_fields[] = {
        "\"artworkUrl600\":\"",
        "\"artworkUrl100\":\"",
        "\"artworkUrl60\":\"",
        "\"artworkUrl30\":\""
    };
    
    for (int i = 0; i < 4; i++) {
        const char* start = strstr(json.c_str(), artwork_fields[i]);
        if (start) {
            start += strlen(artwork_fields[i]);
            const char* end = strchr(start, '"');
            if (end) {
                artwork_url = pfc::string8(start, end - start);
                
                pfc::string8 found_debug;
                found_debug << "Found iTunes artwork field: " << artwork_fields[i] << " -> " << artwork_url << "\n";
                OutputDebugStringA(found_debug);
                
                // Convert lower resolutions to higher resolution if possible
                if (strstr(artwork_fields[i], "100")) {
                    artwork_url.replace_string("100x100", "600x600");
                } else if (strstr(artwork_fields[i], "60")) {
                    artwork_url.replace_string("60x60", "600x600");
                } else if (strstr(artwork_fields[i], "30")) {
                    artwork_url.replace_string("30x30", "600x600");
                }
                
                if (artwork_url.length() > 0) {
                    pfc::string8 final_debug;
                    final_debug << "Final iTunes artwork URL: " << artwork_url << "\n";
                    OutputDebugStringA(final_debug);
                    return true;
                }
            }
        }
    }
    
    OutputDebugStringA("iTunes: No artwork URL found in response\n");
    return false;
}

// Simple JSON parser for Discogs response
bool artwork_ui_element::parse_discogs_response(const pfc::string8& json, pfc::string8& artwork_url) {
    OutputDebugStringA("Discogs parse_discogs_response: Starting parse\n");
    
    // Look for results array with flexible whitespace handling
    const char* results_start = strstr(json.c_str(), "\"results\"");
    if (!results_start) {
        OutputDebugStringA("Discogs parse: No results key found\n");
        return false;
    }
    
    // Find the opening bracket for the results array
    const char* bracket_pos = strchr(results_start, '[');
    if (!bracket_pos) {
        OutputDebugStringA("Discogs parse: No results array bracket found\n");
        return false;
    }
    
    results_start = bracket_pos;
    
    OutputDebugStringA("Discogs parse: Found results array, starting search\n");
    
    // Loop through multiple results like the JavaScript version
    // Look for cover_image field with flexible spacing
    const char* cover_image_key = "\"cover_image\"";
    const char* current_pos = results_start;
    
    for (int result_index = 0; result_index < 5; result_index++) {
        const char* key_pos = strstr(current_pos, cover_image_key);
        if (!key_pos) {
            pfc::string8 no_more_debug;
            no_more_debug << "Discogs: No more cover_image fields found after checking " << result_index << " results\n";
            OutputDebugStringA(no_more_debug);
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
        
        pfc::string8 result_debug;
        result_debug << "Discogs: Checking result #" << (result_index + 1) << "\n";
        OutputDebugStringA(result_debug);
        
        const char* end = strchr(start, '"');
        if (!end) {
            current_pos = start;
            continue; // Malformed, try next one
        }
        
        pfc::string8 potential_url = pfc::string8(start, end - start);
        
        pfc::string8 extracted_debug;
        extracted_debug << "Discogs: Found cover_image: '" << potential_url << "' (length: " << potential_url.length() << ")\n";
        OutputDebugStringA(extracted_debug);
        
        // Check if this URL is valid - filter out placeholder images
        if (potential_url.length() > 0 && 
            potential_url != "" && 
            strstr(potential_url.c_str(), "http") &&
            !strstr(potential_url.c_str(), "spacer.gif") &&
            !strstr(potential_url.c_str(), "placeholder")) {
            
            artwork_url = potential_url;
            
            pfc::string8 found_debug;
            found_debug << "Found valid Discogs artwork: " << artwork_url << "\n";
            OutputDebugStringA(found_debug);
            
            return true;
        }
        
        // Move to next potential result
        current_pos = end + 1;
    }
    
    OutputDebugStringA("Discogs: No valid cover_image found in any results\n");
    return false;
}

// Enhanced JSON parser for Last.fm response
bool artwork_ui_element::parse_lastfm_response(const pfc::string8& json, pfc::string8& artwork_url) {
    pfc::string8 debug;
    debug << "Last.fm JSON response parsing - Full JSON length: " << json.length() << "\n";
    OutputDebugStringA(debug);
    
    // First check for API errors
    if (strstr(json.c_str(), "\"error\":")) {
        OutputDebugStringA("Last.fm: API returned error response\n");
        return false;
    }
    
    // Check if track exists
    if (!strstr(json.c_str(), "\"track\":")) {
        OutputDebugStringA("Last.fm: No track found in response\n");
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
                        
                        pfc::string8 found_debug;
                        found_debug << "Found Last.fm artwork (" << size_preferences[i] << "): " << artwork_url << "\n";
                        OutputDebugStringA(found_debug);
                        
                        return true;
                    }
                }
            }
        }
    }
    
    OutputDebugStringA("Last.fm: No artwork URL found in response\n");
    return false;
}

// Download image data
bool artwork_ui_element::download_image(const pfc::string8& url, std::vector<BYTE>& data) {
    pfc::string8 debug;
    debug << "download_image called with URL: " << url << "\n";
    OutputDebugStringA(debug);
    
    // Use the new binary HTTP request function for proper image download
    bool result = http_get_request_binary(url, data);
    
    pfc::string8 result_debug;
    result_debug << "download_image result: " << (result ? "SUCCESS" : "FAILED") << ", data size: " << data.size() << "\n";
    OutputDebugStringA(result_debug);
    
    return result;
}

// Create bitmap from image data
bool artwork_ui_element::create_bitmap_from_data(const std::vector<BYTE>& data) {
    if (data.empty()) return false;
    
    // Keep reference to old bitmap to avoid white flash
    HBITMAP old_bitmap = m_artwork_bitmap;
    m_artwork_bitmap = NULL;  // Clear the member variable
    
    // Create memory stream from data
    IStream* pStream = NULL;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!hGlobal) return false;
    
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
    
    // Convert to HBITMAP
    Gdiplus::Color backgroundColor(255, 255, 255, 255);
    if (pBitmap->GetHBITMAP(backgroundColor, &m_artwork_bitmap) != Gdiplus::Ok) {
        delete pBitmap;
        m_artwork_bitmap = old_bitmap;  // Restore old bitmap on failure
        return false;
    }
    
    // Only now clean up the old bitmap since new one was successfully created
    if (old_bitmap) {
        DeleteObject(old_bitmap);
    }
    
    delete pBitmap;
    return true;
}

// Playback callback for track changes
class artwork_play_callback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_dynamic_info;
    }
    
    void on_playback_new_track(metadb_handle_ptr p_track) override {
        // Update all artwork UI elements
        for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
            g_artwork_ui_elements[i]->update_track(p_track);
        }
    }
    
    void on_playback_dynamic_info(const file_info& p_info) override {
        // Get current track and update UI elements
        static_api_ptr_t<playback_control> pc;
        metadb_handle_ptr current_track;
        if (pc->get_now_playing(current_track)) {
            for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                g_artwork_ui_elements[i]->update_track(current_track);
            }
        }
    }
    
    void on_playback_stop(play_control::t_stop_reason p_reason) override {}
    void on_playback_seek(double p_time) override {}
    void on_playback_pause(bool p_state) override {}
    void on_playback_edited(metadb_handle_ptr p_track) override {}
    void on_playback_dynamic_info_track(const file_info& p_info) override {}
    void on_playback_time(double p_time) override {}
    void on_volume_change(float p_new_val) override {}
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {}
};

// Helper method to complete artwork search cache management
void artwork_ui_element::complete_artwork_search() {
    // Mark search as completed
    m_last_search_key = m_current_search_key;
    m_current_search_key = "";
    
    pfc::string8 debug;
    debug << "Artwork search completed for: " << m_last_search_key << "\n";
    OutputDebugStringA(debug);
}

// UI Element factory - manual implementation
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

// Service factory registrations
static initquit_factory_t<artwork_init> g_artwork_init_factory;
static play_callback_static_factory_t<artwork_play_callback> g_play_callback_factory;
static service_factory_single_t<artwork_ui_element_factory> g_ui_element_factory;
