#include "stdafx.h"
#include <algorithm>
#include <thread>
#include <vector>
#include <mutex>

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
static constexpr GUID guid_cfg_stream_delay = { 0x12345687, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xff } };
static constexpr GUID guid_cfg_show_osd = { 0x12345688, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };

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

// Stream delay setting (in seconds, default 0 seconds - no delay)
cfg_int cfg_stream_delay(guid_cfg_stream_delay, 0);

// OSD display setting (default enabled)
cfg_bool cfg_show_osd(guid_cfg_show_osd, true);

// Global download throttle to prevent system freeze
static std::mutex g_download_mutex;

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
    "1.2.1",
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
            
        }
    }
};

artwork_manager* artwork_manager::instance = nullptr;

// Global list to track artwork UI elements
static pfc::list_t<artwork_ui_element*> g_artwork_ui_elements;

// Global artwork source for logging (accessible from preferences)
pfc::string8 g_current_artwork_source;

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

// Helper function to get API search order based on priority configuration
enum class ApiType {
    iTunes = 0,
    Deezer = 1,
    LastFm = 2,
    MusicBrainz = 3,
    Discogs = 4
};


static std::vector<ApiType> get_api_search_order() {
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
    pfc::string8 m_last_search_artist;  // Cache artist metadata between API searches
    pfc::string8 m_last_search_title;   // Cache title metadata between API searches
    pfc::string8 m_artwork_source;      // Track source of current artwork (Local, iTunes, Deezer, etc.)
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
    bool m_new_stream_delay_active;  // Track when Timer 9 is active for new stream delay
    bool m_playback_stopped;  // Track when playback is stopped to detect initial connections
    pfc::string8 m_pending_timer_artist;  // Store artist for Timer 9 delayed search
    pfc::string8 m_pending_timer_title;   // Store title for Timer 9 delayed search
    
    // OSD (On-Screen Display) for artwork source
    bool m_osd_visible;           // Whether OSD is currently visible
    pfc::string8 m_osd_text;      // Text to display in OSD
    DWORD m_osd_start_time;       // When OSD was shown (GetTickCount)
    int m_osd_slide_offset;       // Current slide offset for animation (0 = fully visible)
    int m_last_osd_slide_offset;  // Previous slide offset for optimized repainting
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
    
    // Helper functions for APIs
    pfc::string8 url_encode(const pfc::string8& str);
    bool http_get_request(const pfc::string8& url, pfc::string8& response);
    bool http_get_request_binary(const pfc::string8& url, std::vector<BYTE>& data);
    bool http_get_request_with_discogs_auth(const pfc::string8& url, pfc::string8& response);
    bool http_get_request_with_user_agent(const pfc::string8& url, pfc::string8& response, const pfc::string8& user_agent);
    void extract_metadata_for_search(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title);
    void extract_metadata_from_info(const file_info& info, pfc::string8& artist, pfc::string8& title);
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
    
    // OSD (On-Screen Display) methods
    void show_osd(const pfc::string8& source_name);
    void update_osd_animation();
    void paint_osd(HDC hdc, const RECT& client_rect);
    
public:
    // GUID for our element
    static const GUID g_guid;
    artwork_ui_element(HWND parent, ui_element_config::ptr config, ui_element_instance_callback::ptr callback)
        : m_config(config), m_callback(callback), m_hWnd(NULL), m_artwork_bitmap(NULL), m_last_update_timestamp(0), m_last_search_timestamp(0), m_last_search_artist(""), m_last_search_title(""), m_artwork_source(""), m_artwork_found(false), m_current_priority_position(0), m_new_stream_delay_active(false), m_playback_stopped(true), m_osd_visible(false), m_osd_start_time(0), m_osd_slide_offset(0) {
        
        // Remove jarring "No track playing" message
        // m_status_text = "No track playing";
        
        // Register a custom window class for artwork display
        static bool class_registered = false;
        if (!class_registered) {
            WNDCLASS wc = {};
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = g_hIns;
            wc.lpszClassName = L"foo_artwork_window";
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
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
                // Schedule initial artwork load after a longer delay
                // This handles cases where foobar2000 has resumed playback state
                // but stream metadata takes time to populate
                m_status_text = "Detected resumed playback - scheduling artwork load...";
                InvalidateRect(m_hWnd, NULL, TRUE);
                SetTimer(m_hWnd, 3, 3000, NULL);  // Timer ID 3, 3 second delay
            } else {
                // No track playing during initialization
                
                // Set a timer to check periodically if playback starts
                SetTimer(m_hWnd, 6, 1000, NULL);  // Timer ID 6, check every 1 second
            }
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
        
        // Smart debouncing - only update if track or content actually changed
        DWORD current_time = GetTickCount();
        bool track_changed = (track != m_last_update_track);
        
        // Extract current content for comparison
        pfc::string8 current_artist, current_title;
        extract_metadata_for_search(track, current_artist, current_title);
        pfc::string8 current_content = current_artist + "|" + current_title;
        
        bool content_changed = (current_content != m_last_update_content);
        
        bool enough_time_passed = (current_time - m_last_update_timestamp) > 1000; // 1 second
        
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
            
            // For new internet radio streams, wait before checking metadata (regardless of clearing)
            if (is_new_stream) {
                // Schedule delayed metadata check for new streams
                SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);  // Timer ID 9 for new stream delay
                m_new_stream_delay_active = true;  // Set flag to prevent override
                
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
                
                // Use appropriate delay based on stream connection type
                m_current_track = track;
                if (is_new_stream_in_content_changed) {
                    // New stream connection: Use full configurable stream delay
                    SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);
                    m_new_stream_delay_active = true;  // Set flag to prevent override
                } else {
                    // Check if Timer 9 is already running for a new stream delay - don't override it
                    if (!m_new_stream_delay_active) {
                        // Normal track change: Use short delay for responsiveness
                        SetTimer(m_hWnd, 9, 500, NULL);
                    }
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
                
                // For stream resumes (like restarting foobar2000), use full stream delay
                // This handles both ad break recoveries and stream reconnections properly
                m_current_track = track;
                
                // Only set timer if not already active for new stream delay
                if (!m_new_stream_delay_active) {
                    SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);  // Use full configurable delay
                    m_new_stream_delay_active = true;  // Set flag to prevent override
                }
                
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
            
            // Use nuanced delay approach for internet streams
            if (is_internet_stream) {
                
                if (is_new_stream_connection) {
                    // Initial stream connection: Use full configurable stream delay
                    SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);
                    m_new_stream_delay_active = true;  // Set flag to prevent override
                } else {
                    // Normal track changes within stream: Use short delay for responsiveness
                    // Only set if new stream delay is not already active
                    if (!m_new_stream_delay_active) {
                        SetTimer(m_hWnd, 9, 500, NULL);  // 0.5 second delay for track changes
                    }
                }
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Timer 9 will handle the artwork search
            }
        }
        
        // FINAL CHECK: Apply stream delay to ALL internet streams regardless of execution path
        if (track.is_valid()) {
            pfc::string8 path = track->get_path();
            bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
            
            if (is_internet_stream) {
                // This should not happen if our earlier logic is correct, but this is a safety net
                // Check if this is a new stream connection for nuanced delay
                bool is_final_new_stream = false;
                if (m_last_update_track.is_valid()) {
                    pfc::string8 old_path = m_last_update_track->get_path();
                    bool old_is_internet_stream = (strstr(old_path.c_str(), "://") && !strstr(old_path.c_str(), "file://"));
                    
                    if (!old_is_internet_stream && is_internet_stream) {
                        is_final_new_stream = true;  // Local file -> Internet stream
                    } else if (is_internet_stream && (path != old_path)) {
                        is_final_new_stream = true;  // Different internet stream
                    }
                } else if (is_internet_stream) {
                    is_final_new_stream = true;  // First track is internet stream
                }
                
                if (is_final_new_stream) {
                    SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);  // Full delay for new streams
                    m_new_stream_delay_active = true;  // Set flag to prevent override
                } else {
                    // Only set short delay if new stream delay is not already active
                    if (!m_new_stream_delay_active) {
                        SetTimer(m_hWnd, 9, 500, NULL);  // Short delay for track changes
                    }
                }
                
                // Update tracking variables before returning
                m_last_update_track = track;
                m_last_update_content = current_content;
                m_last_update_timestamp = current_time;
                return;  // Never allow immediate searches for internet streams
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
        bool enough_time_passed = (current_time - m_last_update_timestamp) > 3000; // 3 second debounce
        
        
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
                    // Schedule delayed metadata check to wait for real track metadata
                    SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);
                    m_new_stream_delay_active = true;
                    
                    // Update tracking variables
                    m_last_update_track = track;
                    m_last_update_content = current_content;
                    m_last_update_timestamp = current_time;
                    return;
                }
            }
        }
        
        // For internet radio streams, only apply delay on initial connection
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
                    
                    // Use stream delay for initial connection
                    SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);
                    m_new_stream_delay_active = true;
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
    // OLD MESSAGE HANDLERS REMOVED - Now using priority-based search system
    // OLD WM_USER + 7 HANDLER REMOVED - Now using priority-based search system
    case WM_USER + 6: // Create bitmap from downloaded data
        pThis->process_downloaded_image_data();
        return 0;
    case WM_TIMER:
        if (wParam == 2) {  // Timer ID 2 - delayed retry for station metadata
            KillTimer(hwnd, 2);  // Kill the timer
            
            // Retry artwork search with current track
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                pThis->update_track(current_track);
            }
        } else if (wParam == 3) {  // Timer ID 3 - initial load for resume scenarios
            KillTimer(hwnd, 3);  // Kill the timer
            
            // Load artwork for currently playing track
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                // Check if we have valid metadata available
                pfc::string8 test_artist, test_title;
                pThis->extract_metadata_for_search(current_track, test_artist, test_title);
                
                if (!test_artist.is_empty() && !test_title.is_empty()) {
                    // We have valid metadata, proceed with search
                    // Check if this is an internet stream that should respect delay
                    pfc::string8 path = current_track->get_path();
                    bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                    
                    if (is_internet_stream) {
                        pThis->m_status_text = "Waiting for stream delay before searching...";
                        if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                        
                        // DON'T clear artwork - keep existing artwork visible during stream transitions
                        // Use stream delay for internet streams even during UI initialization
                        SetTimer(hwnd, 9, cfg_stream_delay * 1000, NULL);
                        pThis->m_new_stream_delay_active = true;  // Set flag to prevent override
                    } else {
                        pThis->m_status_text = "Searching for artwork...";
                        if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                        
                        // DON'T clear artwork - keep existing artwork visible during transitions
                        pThis->update_track(current_track);
                    }
                } else {
                    // Metadata not ready yet, schedule another retry
                    pThis->m_status_text = "Waiting for track metadata...";
                    if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                    
                    SetTimer(hwnd, 4, 2000, NULL);  // Timer ID 4, retry in 2 seconds
                }
            }
        } else if (wParam == 4) {  // Timer ID 4 - retry for resume scenarios
            KillTimer(hwnd, 4);  // Kill the timer
            
            // Retry artwork search for resumed internet streams
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                // Check if we have valid metadata available now
                pfc::string8 test_artist, test_title;
                pThis->extract_metadata_for_search(current_track, test_artist, test_title);
                
                if (!test_artist.is_empty() && !test_title.is_empty()) {
                    // We have valid metadata, proceed with search
                    // Check if this is an internet stream that should respect delay
                    pfc::string8 path = current_track->get_path();
                    bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                    
                    if (is_internet_stream) {
                        pThis->m_status_text = "Waiting for stream delay before searching...";
                        if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                        
                        // DON'T clear artwork - keep existing artwork visible during stream transitions
                        
                        // Clear search cache to bypass any cached failures
                        {
                            std::lock_guard<std::mutex> lock(pThis->m_artwork_found_mutex);
                            pThis->m_last_search_key = "";
                            pThis->m_current_search_key = "";
                        }
                        pThis->m_last_search_timestamp = 0;
                        
                        // Use stream delay for internet streams even during retry
                        SetTimer(hwnd, 9, cfg_stream_delay * 1000, NULL);
                        pThis->m_new_stream_delay_active = true;  // Set flag to prevent override
                    } else {
                        pThis->m_status_text = "Searching for artwork...";
                        if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                        
                        // DON'T clear artwork - keep existing artwork visible during stream transitions
                        
                        // Clear search cache to bypass any cached failures
                        {
                            std::lock_guard<std::mutex> lock(pThis->m_artwork_found_mutex);
                            pThis->m_last_search_key = "";
                            pThis->m_current_search_key = "";
                        }
                        pThis->m_last_search_timestamp = 0;
                        
                        pThis->update_track(current_track);
                    }
                } else {
                    // Still no metadata, schedule final retry
                    pThis->m_status_text = "Still waiting for metadata...";
                    if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                    
                    SetTimer(hwnd, 5, 3000, NULL);  // Timer ID 5, final retry in 3 seconds
                }
            }
        } else if (wParam == 5) {  // Timer ID 5 - final retry for resume scenarios
            KillTimer(hwnd, 5);  // Kill the timer
            
            // Final retry for artwork search
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                // Check metadata one more time
                pfc::string8 test_artist, test_title;
                pThis->extract_metadata_for_search(current_track, test_artist, test_title);
                
                if (!test_artist.is_empty() && !test_title.is_empty()) {
                    // Finally have metadata, proceed with search
                    // Check if this is an internet stream that should respect delay
                    pfc::string8 path = current_track->get_path();
                    bool is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                    
                    if (is_internet_stream) {
                        pThis->m_status_text = "Waiting for stream delay before final search...";
                        if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                        
                        // Clear ALL cached search state for final retry
                        // DON'T clear artwork - keep existing artwork visible during stream transitions
                        
                        // Force bypass of search cache
                        {
                            std::lock_guard<std::mutex> lock(pThis->m_artwork_found_mutex);
                            pThis->m_last_search_key = "";
                            pThis->m_current_search_key = "";
                        }
                        pThis->m_last_search_timestamp = 0;
                        
                        // Use stream delay for internet streams even during final retry
                        SetTimer(hwnd, 9, cfg_stream_delay * 1000, NULL);
                        pThis->m_new_stream_delay_active = true;  // Set flag to prevent override
                    } else {
                        pThis->m_status_text = "Final artwork search...";
                        if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                        
                        // Clear ALL cached search state for final retry
                        // DON'T clear artwork - keep existing artwork visible during stream transitions
                        
                        // Force bypass of search cache
                        {
                            std::lock_guard<std::mutex> lock(pThis->m_artwork_found_mutex);
                            pThis->m_last_search_key = "";
                            pThis->m_current_search_key = "";
                        }
                        pThis->m_last_search_timestamp = 0;
                        
                        pThis->update_track(current_track);
                    }
                } else {
                    // Give up - no metadata available
                    pThis->m_status_text = "No track metadata available";
                    if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                }
            }
        } else if (wParam == 6) {  // Timer ID 6 - periodic check for playback start
            // Check if playback has started
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                // Playback has started, switch to resume logic
                KillTimer(hwnd, 6);  // Stop the periodic check
                
                pThis->m_status_text = "Playback detected - scheduling artwork load...";
                if (pThis->m_hWnd) InvalidateRect(pThis->m_hWnd, NULL, TRUE);
                
                SetTimer(hwnd, 3, 3000, NULL);  // Timer ID 3, 3 second delay
            } else {
                // Still no playback, but limit the checking to avoid infinite loop
                static int check_count = 0;
                check_count++;
                if (check_count >= 10) {  // Stop after 10 seconds
                    KillTimer(hwnd, 6);
                }
            }
        } else if (wParam == 7) {  // Timer ID 7 - delayed metadata check for track changes
            KillTimer(hwnd, 7);  // Kill the timer
            
            // Retry metadata extraction to check if track has changed
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                
                // Extract fresh metadata and compare with cached content
                pfc::string8 fresh_artist, fresh_title;
                pThis->extract_metadata_for_search(current_track, fresh_artist, fresh_title);
                pfc::string8 fresh_content = fresh_artist + "|" + fresh_title;
                
                // Only proceed if content has actually changed
                if (fresh_content != pThis->m_last_update_content && !fresh_artist.is_empty() && !fresh_title.is_empty()) {
                    
                    // Update content and trigger normal update process
                    pThis->m_last_update_content = fresh_content;
                    pThis->update_track(current_track);
                } else {
                }
            }
        } else if (wParam == 8) {  // Timer ID 8 - aggressive metadata retry for track changes
            KillTimer(hwnd, 8);  // Kill the timer
            
            // More aggressive retry with multiple attempts
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                
                // Extract fresh metadata
                pfc::string8 fresh_artist, fresh_title;
                pThis->extract_metadata_for_search(current_track, fresh_artist, fresh_title);
                pfc::string8 fresh_content = fresh_artist + "|" + fresh_title;
                
                // Compare with last known content
                if (fresh_content != pThis->m_last_update_content && !fresh_artist.is_empty() && !fresh_title.is_empty()) {
                    
                    // DON'T clear artwork - keep it until new artwork loads
                    // Only clear search cache to force new search
                    {
                        std::lock_guard<std::mutex> lock(pThis->m_artwork_found_mutex);
                        pThis->m_last_search_key = "";
                        pThis->m_current_search_key = "";
                        pThis->m_artwork_found = false;  // Reset found flag
                    }
                    pThis->m_last_search_timestamp = 0;
                    
                    // Update with fresh metadata
                    pThis->m_last_update_content = fresh_content;
                    pThis->load_artwork_for_track_with_metadata(current_track, fresh_artist, fresh_title);
                } else {
                }
            }
        } else if (wParam == 9) {  // Timer ID 9 - delayed metadata check for new streams
            KillTimer(hwnd, 9);  // Kill the timer
            pThis->m_new_stream_delay_active = false;  // Clear the flag when timer fires
            
            // Use stored metadata from when timer was set
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track)) {
                // Use the metadata that was stored when the timer was set
                pfc::string8 stream_artist = pThis->m_pending_timer_artist;
                pfc::string8 stream_title = pThis->m_pending_timer_title;
                
                // Proceed if we have at least a title (artist can be empty for some streams)
                if (!stream_title.is_empty()) {
                    // Clear search cache and start fresh search
                    {
                        std::lock_guard<std::mutex> lock(pThis->m_artwork_found_mutex);
                        pThis->m_last_search_key = "";
                        pThis->m_current_search_key = "";
                        pThis->m_artwork_found = false;
                    }
                    pThis->m_last_search_timestamp = 0;
                    
                    // Update tracking and start search
                    pThis->m_current_track = current_track;
                    pThis->m_last_update_content = stream_artist + "|" + stream_title;
                    pThis->load_artwork_for_track_with_metadata(current_track, stream_artist, stream_title);
                    
                    // Clear stored metadata after use
                    pThis->m_pending_timer_artist = "";
                    pThis->m_pending_timer_title = "";
                }
            }
        } else if (wParam == 10) {  // Timer ID 10 - OSD animation
            pThis->update_osd_animation();
        }
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// Paint artwork
void artwork_ui_element::paint_artwork(HDC hdc) {
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    
    // Use foobar2000's proper UI element colors
    COLORREF bg_color, text_color;
    
    // Try to get foobar2000 UI element colors
    try {
        t_ui_color ui_bg_color, ui_text_color;
        
        // Query the UI element background and text colors
        if (m_callback.is_valid() && 
            m_callback->query_color(ui_color_background, ui_bg_color) && 
            m_callback->query_color(ui_color_text, ui_text_color)) {
            // Use foobar2000 UI colors
            bg_color = ui_bg_color;
            text_color = ui_text_color;
        } else {
            // Fallback: get standard colors using helper method
            if (m_callback.is_valid()) {
                bg_color = m_callback->query_std_color(ui_color_background);
                text_color = m_callback->query_std_color(ui_color_text);
            } else {
                // Final fallback to system colors
                bg_color = GetSysColor(COLOR_WINDOW);
                text_color = GetSysColor(COLOR_WINDOWTEXT);
            }
        }
    } catch (...) {
        // Fallback to system colors if any foobar2000 API calls fail
        bg_color = GetSysColor(COLOR_WINDOW);
        text_color = GetSysColor(COLOR_WINDOWTEXT);
    }
    
    // Fill background
    HBRUSH bgBrush = CreateSolidBrush(bg_color);
    FillRect(hdc, &clientRect, bgBrush);
    DeleteObject(bgBrush);
    
    // ALWAYS show artwork if we have any bitmap, regardless of search state
    // This ensures smooth transitions without blank screens
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
        
        // Always use fit mode: Scale to fit window (shows full image, may have letterboxing)
        // Use the smaller scale factor to ensure the entire image is visible
        double scale = (scaleX < scaleY) ? scaleX : scaleY;
        
        // Apply more reasonable scale limits that don't interfere with aspect ratio
        // Only limit extreme scaling to prevent system issues
        if (scale > 50.0) {
            scale = 50.0;
        }
        if (scale < 0.01) {
            scale = 0.01;
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
        
        // In fit mode, keep image centered - no clamping needed since image always fits in window
        // The image should always fit within the window bounds in fit mode
        
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
        // Never show blank screen - always keep the last artwork visible
        // If no artwork exists, show a subtle placeholder instead of blank screen
        if (!m_status_text.is_empty()) {
            // Only show status text if we have something meaningful to display
            SetBkColor(hdc, bg_color);
            SetTextColor(hdc, text_color);
            DrawTextA(hdc, m_status_text.c_str(), -1, &clientRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        // If no status text and no artwork, just show the background color (no blank screen)
    }
    
    // Paint OSD overlay (always on top)
    if (m_osd_visible) {
        paint_osd(hdc, clientRect);
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
    if (search_key == m_current_search_key || 
        (search_key == m_last_search_key && !m_artwork_bitmap && !retry_timeout_passed)) {
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
            
            // Schedule delayed metadata check to wait for real track metadata
            SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);
            m_new_stream_delay_active = true;  // Set flag to prevent override
            return;
        }
    }
    
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
                    complete_artwork_search();
                    m_status_text = "Local artwork loaded";
                    m_artwork_source = "Local file";
                    g_current_artwork_source = "Local file";
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
    
    if (!track.is_valid()) {
        m_status_text = "No track";
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        return;
    }
    
    // Cache metadata for use in sequential API fallback chain
    m_last_search_artist = artist;
    m_last_search_title = title;
    
    // Use the already-extracted and cleaned metadata
    pfc::string8 search_key = artist + "|" + title;
    
    // Reset artwork found flag and clear stale search state for new search
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        m_artwork_found = false;
        // Also clear any stale search state while we have the lock
        if (search_key != m_current_search_key) {
            m_current_search_key = "";
        }
    }
    
    // Determine search priority (higher is better)
    int search_priority = 0;
    if (!artist.is_empty() && !title.is_empty()) {
        search_priority = 3; // Both artist and title = highest priority
    } else if (!title.is_empty()) {
        search_priority = 2; // Title only = medium priority  
    } else {
        search_priority = 1; // Neither artist nor title = lowest priority
    }
    
    
    // Add member variable to track last search priority
    static int last_search_priority = 0;
    
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
    if (search_key == m_current_search_key || 
        (search_key == m_last_search_key && !m_artwork_bitmap && !retry_timeout_passed)) {
        return;
    }
    
    // Don't override higher priority artwork with lower priority
    if (search_priority < last_search_priority && !retry_timeout_passed) {
        return;
    }
    
    
    // Update priority tracking
    last_search_priority = search_priority;
    
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
    
    
    // Try to get local artwork first
    try {
        static_api_ptr_t<album_art_manager_v2> aam;
        auto aaer = aam->open(pfc::list_single_ref_t<metadb_handle_ptr>(track), 
                              pfc::list_single_ref_t<GUID>(album_art_ids::cover_front), 
                              fb2k::noAbort);
        
        auto aar = aaer->query(album_art_ids::cover_front, fb2k::noAbort);
        if (aar.is_valid()) {
            auto data = aar->get_ptr();
            if (data && aar->get_size() > 0) {
                const BYTE* byte_data = static_cast<const BYTE*>(data);
                std::vector<BYTE> image_data(byte_data, byte_data + aar->get_size());
                if (create_bitmap_from_data(image_data)) {
                    // Mark that artwork has been found to stop any online searches
                    {
                        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
                        m_artwork_found = true;
                    }
                    
                    complete_artwork_search();
                    if (m_hWnd) {
                        PostMessage(m_hWnd, WM_USER + 1, 0, 0);
                    }
                    m_current_track = track;
                    return;
                } else {
                }
            } else {
            }
        } else {
        }
    } catch (...) {
        // Local artwork failed, continue to online search
    }
    
    // Check if we should search online
    bool should_search_online = false;
    if (is_internet_stream) {
        // This looks like a stream URL, search online
        should_search_online = true;
    } else {
        // For local files, search online if we have valid metadata but no local artwork
        if (!artist.is_empty() && !title.is_empty()) {
            should_search_online = true;
        } else {
        }
    }
    
    if (should_search_online && track.is_valid()) {
        // Use priority-based search order
        start_priority_search(artist, title);
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
        m_status_text = "iTunes search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        m_status_text = "iTunes search error";
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
        m_status_text = "Discogs search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        complete_artwork_search();  // Mark cache as completed
        m_status_text = "Discogs search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// Background Last.fm API search
void artwork_ui_element::search_lastfm_background(pfc::string8 artist, pfc::string8 title) {
    try {
        if (cfg_lastfm_key.is_empty()) {
            m_status_text = "Last.fm API key not configured";
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
        m_status_text = "Last.fm search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        m_status_text = "Last.fm search error";
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
            pfc::string8 clean_artist = clean_metadata_text(artist);
            pfc::string8 clean_title = clean_metadata_text(title);
            
            // Try artist + track format for better matching
            search_query = clean_artist + " " + clean_title;
        }
        pfc::string8 encoded_search = url_encode(search_query);
        
        pfc::string8 search_url = "https://api.deezer.com/search/track?q=";
        search_url << encoded_search << "&limit=10";
        
        // Update status
        m_status_text = "Searching Deezer...";
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        
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
        m_status_text = "Deezer search failed";
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        m_status_text = "Deezer search error";
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
        m_status_text = "MusicBrainz search failed";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
        
    } catch (...) {
        m_status_text = "MusicBrainz search error";
        search_next_api_in_priority(m_last_search_artist, m_last_search_title, m_current_priority_position + 1);
    }
}

// Extract metadata for search with comprehensive cleaning - ENHANCED VERSION
void artwork_ui_element::extract_metadata_for_search(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title) {
    artist = "";
    title = "";
    
    if (!track.is_valid()) return;
    
    // Use file_info directly to get metadata (no more window title parsing)
    try {
        const file_info& info = track->get_info_ref()->info();
        
        // Extract metadata from track info
        if (info.meta_get("ARTIST", 0)) {
            artist = info.meta_get("ARTIST", 0);
        }
        
        if (info.meta_get("TITLE", 0)) {
            title = info.meta_get("TITLE", 0);
        }
    } catch (...) {
        // If metadata access fails, keep empty values
    }
    
    // Clean up common encoding issues and unwanted text
    artist = clean_metadata_text(artist);
    title = clean_metadata_text(title);
}


// Clean metadata text from encoding issues and unwanted content - ENHANCED VERSION
pfc::string8 artwork_ui_element::clean_metadata_text(const pfc::string8& text) {
    pfc::string8 cleaned = text;
    const char* text_cstr;
    
    
    // Fix common encoding issues (using hex escape sequences)
    // Handle all variants of apostrophes and quotes
    cleaned.replace_string("\xE2\x80\x98", "'");  // Left single quotation mark
    cleaned.replace_string("\xE2\x80\x99", "'");  // Right single quotation mark
    cleaned.replace_string("\xE2\x80\x9A", "'");  // Single low-9 quotation mark
    
    // Remove common subtitle patterns in brackets/parentheses
    // These patterns often prevent artwork APIs from finding matches
    
    // Remove parentheses with common live/acoustic/remix patterns (case insensitive)
    text_cstr = cleaned.c_str();
    
    // Pattern 1: (live...)
    const char* live_patterns[] = {
        "(live)",
        "(live at",
        "(acoustic)",
        "(acoustic version)",
        "(unplugged)",
        "(radio edit)",
        "(extended)",
        "(extended version)", 
        "(single version)",
        "(album version)",
        "(remix)",
        "(remaster)",
        "(remastered)",
        "(demo)",
        "(instrumental)",
        "(explicit)",
        "(clean)",
        "(feat.",
        "(featuring",
        "(ft.",
        "(with"
    };
    
    for (int i = 0; i < 20; i++) {
        // Find pattern (case insensitive)
        const char* found_pos = text_cstr;
        bool pattern_found = false;
        
        while (*found_pos) {
            if (*found_pos == '(' || *found_pos == '[') {
                // Check if this bracket contains our pattern
                bool matches = true;
                const char* pattern = live_patterns[i];
                const char* check_pos = found_pos + 1;
                const char* pattern_pos = pattern + 1; // Skip opening bracket in pattern
                
                while (*pattern_pos && *check_pos) {
                    char p_char = *pattern_pos >= 'A' && *pattern_pos <= 'Z' ? *pattern_pos + 32 : *pattern_pos;
                    char c_char = *check_pos >= 'A' && *check_pos <= 'Z' ? *check_pos + 32 : *check_pos;
                    
                    if (p_char != c_char) {
                        matches = false;
                        break;
                    }
                    pattern_pos++;
                    check_pos++;
                }
                
                if (matches) {
                    // Found the pattern, now find the closing bracket
                    char closing_bracket = (*found_pos == '(') ? ')' : ']';
                    const char* end_pos = check_pos;
                    while (*end_pos && *end_pos != closing_bracket) {
                        end_pos++;
                    }
                    if (*end_pos == closing_bracket) {
                        end_pos++; // Include closing bracket
                        
                        // Remove this entire bracketed section
                        size_t start_offset = found_pos - text_cstr;
                        size_t end_offset = end_pos - text_cstr;
                        pfc::string8 before = cleaned.subString(0, start_offset);
                        pfc::string8 after = cleaned.subString(end_offset);
                        cleaned = before + after;
                        
                        // Update text_cstr for next iteration
                        text_cstr = cleaned.c_str();
                        pattern_found = true;
                        break;
                    }
                }
            }
            found_pos++;
        }
        
        if (pattern_found) {
            // Start over with updated string
            i = -1; // Will be incremented to 0
            continue;
        }
    }
    
    // Remove trailing timestamp patterns like " - 0.00", " - 3:45", " - 12.34"
    // These are commonly added by radio streaming software
    text_cstr = cleaned.c_str();
    const char* hyphen_pos = text_cstr;
    
    // Look for " - " pattern near the end
    while (*hyphen_pos) {
        if (*hyphen_pos == ' ' && *(hyphen_pos + 1) == '-' && *(hyphen_pos + 2) == ' ') {
            // Found " - ", check if followed by digits/time pattern
            const char* after_hyphen = hyphen_pos + 3;
            bool is_timestamp = true;
            int digit_count = 0;
            
            // Check if it's a timestamp pattern (digits, colons, dots)
            const char* check_pos = after_hyphen;
            while (*check_pos && *check_pos != '\0') {
                char c = *check_pos;
                if ((c >= '0' && c <= '9') || c == ':' || c == '.') {
                    if (c >= '0' && c <= '9') digit_count++;
                    check_pos++;
                } else {
                    is_timestamp = false;
                    break;
                }
            }
            
            // If it looks like a timestamp and has at least 1 digit, remove it
            if (is_timestamp && digit_count >= 1) {
                size_t remove_start = hyphen_pos - text_cstr;
                cleaned = cleaned.subString(0, remove_start);
                break; // Only remove the first occurrence
            }
        }
        hyphen_pos++;
    }
    
    // Remove leading and trailing whitespace
    while (cleaned.length() > 0 && cleaned[0] == ' ') {
        cleaned = cleaned.subString(1);
    }
    while (cleaned.length() > 0 && cleaned[cleaned.length()-1] == ' ') {
        cleaned = cleaned.subString(0, cleaned.length()-1);
    }
    
    return cleaned;
}

// Extract metadata directly from file_info (for radio streams)
void artwork_ui_element::extract_metadata_from_info(const file_info& info, pfc::string8& artist, pfc::string8& title) {
    artist = "";
    title = "";
    
    // Get artist from file_info
    if (info.meta_get("ARTIST", 0)) {
        artist = info.meta_get("ARTIST", 0);
    }
    
    // Get title from file_info
    if (info.meta_get("TITLE", 0)) {
        title = info.meta_get("TITLE", 0);
    }
    
    // Clean the metadata
    artist = clean_metadata_text(artist);
    title = clean_metadata_text(title);
    
}

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
    // Use global mutex to prevent multiple simultaneous downloads that can freeze the system
    std::lock_guard<std::mutex> download_lock(g_download_mutex);
    
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
                
                
                // Convert lower resolutions to higher resolution if possible
                if (strstr(artwork_fields[i], "100")) {
                    artwork_url.replace_string("100x100", "600x600");
                } else if (strstr(artwork_fields[i], "60")) {
                    artwork_url.replace_string("60x60", "600x600");
                } else if (strstr(artwork_fields[i], "30")) {
                    artwork_url.replace_string("30x30", "600x600");
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
                
                artwork_url = potential_url;
                
                
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
        m_status_text = "Artwork loaded from ";
        m_status_text << source;
        
        // Show OSD with artwork source
        show_osd(source);
        
        InvalidateRect(m_hWnd, NULL, TRUE);
    } else {
        // Bitmap creation failed
        complete_artwork_search();
        m_status_text = "Failed to create artwork bitmap";
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

// Playback callback for track changes
class artwork_play_callback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_dynamic_info | flag_on_playback_dynamic_info_track | flag_on_playback_starting | flag_on_playback_stop;
    }
    
    void on_playback_new_track(metadb_handle_ptr p_track) override {
        // Update all artwork UI elements
        if (p_track.is_valid()) {
            pfc::string8 track_path = p_track->get_path();
        }
        
        for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
            g_artwork_ui_elements[i]->update_track(p_track);
        }
    }
    
    void on_playback_stop(play_control::t_stop_reason p_reason) override {
        // Clear artwork from all UI elements when playback stops
        for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
            g_artwork_ui_elements[i]->clear_artwork_bitmap();  // Actually clear bitmap on stop
            g_artwork_ui_elements[i]->m_status_text = "";
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
        // This is specifically called for stream track title changes
        // Use the p_info parameter directly to get accurate metadata
        
        static_api_ptr_t<playback_control> pc;
        metadb_handle_ptr current_track;
        if (pc->get_now_playing(current_track)) {
            pfc::string8 track_path = current_track->get_path();
            
            for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                auto* element = g_artwork_ui_elements[i];
                
                // Extract metadata directly from the p_info parameter
                pfc::string8 fresh_artist, fresh_title;
                element->extract_metadata_from_info(p_info, fresh_artist, fresh_title);
                pfc::string8 fresh_content = fresh_artist + "|" + fresh_title;
                
                
                // Only update if content has actually changed and we have valid metadata
                // OR if we don't have artwork yet for valid metadata
                bool content_changed = (fresh_content != element->m_last_update_content);
                bool has_valid_metadata = (!fresh_artist.is_empty() && !fresh_title.is_empty());
                bool needs_artwork = (!element->m_artwork_bitmap || element->m_last_search_key.is_empty());
                
                if ((content_changed || needs_artwork) && has_valid_metadata) {
                    element->update_track_with_metadata(current_track, fresh_artist, fresh_title);
                }
            }
        }
    }
    void on_playback_time(double p_time) override {}
    void on_volume_change(float p_new_val) override {}
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {
        // This is called when playback starts (including new internet radio streams)
        // Get current track and update UI elements
        static_api_ptr_t<playback_control> pc;
        metadb_handle_ptr current_track;
        if (pc->get_now_playing(current_track)) {
            for (t_size i = 0; i < g_artwork_ui_elements.get_count(); i++) {
                // Don't clear stopped flag here - let update_track_with_metadata handle it
                // This ensures stream delay logic works properly
                g_artwork_ui_elements[i]->update_track(current_track);
            }
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
                
                // Schedule delayed metadata check to wait for real track metadata
                SetTimer(m_hWnd, 9, cfg_stream_delay * 1000, NULL);
                return;
            }
        }
    }
    
    search_next_api_in_priority(artist, title, 0);
}

void artwork_ui_element::search_next_api_in_priority(const pfc::string8& artist, const pfc::string8& title, int current_position) {
    
    // Check if artwork has already been found by another search
    {
        std::lock_guard<std::mutex> lock(m_artwork_found_mutex);
        if (m_artwork_found) {
            return;
        }
    }
    
    if (current_position >= 5) {
        // No more APIs to try - mark search as complete
        complete_artwork_search();
        m_status_text = "No artwork found";
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
        
        // Debug: Update status to show which API is being tried
        pfc::string8 debug_status = "Searching ";
        debug_status << api_name << " (priority " << (current_position + 1) << ")...";
        m_status_text = debug_status;
        if (m_hWnd) InvalidateRect(m_hWnd, NULL, TRUE);
        
        // Start the API search
        switch (current_api) {
            case ApiType::iTunes:
                search_itunes_background(artist, title);
                break;
            case ApiType::Deezer:
                search_deezer_background(artist, title);
                break;
            case ApiType::LastFm:
                search_lastfm_background(artist, title);
                break;
            case ApiType::MusicBrainz:
                search_musicbrainz_background(artist, title);
                break;
            case ApiType::Discogs:
                search_discogs_background(artist, title);
                break;
        }
    } else {
        // API is disabled, try the next one
        search_next_api_in_priority(artist, title, current_position + 1);
    }
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

//=============================================================================
// OSD (On-Screen Display) Implementation
//=============================================================================

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
    
    if (elapsed < OSD_SLIDE_IN_DURATION) {
        // Slide-in animation: from 200 pixels offset to 0 (fully visible)
        DWORD slide_progress = elapsed;
        m_osd_slide_offset = 200 - (int)((slide_progress * 200) / OSD_SLIDE_IN_DURATION);
        
    } else if (elapsed < (OSD_SLIDE_IN_DURATION + OSD_DISPLAY_DURATION)) {
        // Display phase: fully visible (offset = 0)
        m_osd_slide_offset = 0;
        
    } else {
        // Slide-out animation
        DWORD slide_out_start = OSD_SLIDE_IN_DURATION + OSD_DISPLAY_DURATION;
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

// Service factory registrations
static initquit_factory_t<artwork_init> g_artwork_init_factory;
static play_callback_static_factory_t<artwork_play_callback> g_play_callback_factory;
static service_factory_single_t<artwork_ui_element_factory> g_ui_element_factory;
