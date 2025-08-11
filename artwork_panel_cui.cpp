// CUI Artwork Panel Implementation - Full Featured
// This file implements complete artwork display functionality for Columns UI

// Prevent socket conflicts before including windows.h
#define _WINSOCKAPI_
#define NOMINMAX

#include <windows.h>
#include <shlobj.h>

// Define CUI support for this file since we're not using precompiled headers
#define COLUMNS_UI_AVAILABLE

// Only compile CUI support if CUI SDK is available
#ifdef COLUMNS_UI_AVAILABLE

// Standard Windows headers
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <exception>
#include <wincodec.h>

// Link WIC library
#pragma comment(lib, "windowscodecs.lib")

// Link GDI library for AlphaBlend function
#pragma comment(lib, "msimg32.lib")

#include <algorithm>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include <cmath>
#include <regex>

// Include CUI SDK headers directly - these contain their own foobar2000 SDK
#include "columns_ui/columns_ui-sdk/ui_extension.h"
#include "columns_ui/columns_ui-sdk/window.h"
#include "columns_ui/columns_ui-sdk/container_uie_window_v3.h"

// Also include base UI extension headers
#include "columns_ui/columns_ui-sdk/base.h"

// Include the unified artwork viewer popup and metadata cleaner
#include "artwork_viewer_popup.h"
#include "metadata_cleaner.h"

// Include necessary foobar2000 SDK headers for artwork and playback callbacks
#include "columns_ui/foobar2000/SDK/album_art.h"
#include "columns_ui/foobar2000/SDK/playback_control.h"
#include "columns_ui/foobar2000/SDK/play_callback.h"
#include "columns_ui/foobar2000/SDK/cfg_var.h"

// Include CUI color API
#include "columns_ui/columns_ui-sdk/colours.h"

// Use the Gdiplus namespace
using namespace Gdiplus;

//=============================================================================
// Forward declarations and external interfaces
//=============================================================================

// Forward declare classes from main component
class artwork_manager;

// External instances from main component
extern std::unique_ptr<artwork_manager> g_artwork_manager;

// Configuration variables that we can safely access
extern bool g_artwork_loading;
extern std::wstring g_current_artwork_path;

// Global preference settings
extern cfg_bool cfg_show_osd;
extern cfg_bool cfg_enable_custom_logos;
extern cfg_string cfg_logos_folder;
extern cfg_bool cfg_clear_panel_when_not_playing;

// Access to main component's artwork bitmap
extern HBITMAP get_main_component_artwork_bitmap();

// Shared bitmap from standalone search
extern HBITMAP g_shared_artwork_bitmap;

// Logo loading functions (declared in sdk_main.cpp)

extern pfc::string8 extract_domain_from_stream_url(metadb_handle_ptr track);
extern pfc::string8 extract_full_path_from_stream_url(metadb_handle_ptr track);
extern pfc::string8 extract_station_name_from_metadata(metadb_handle_ptr track);
extern HBITMAP load_station_logo(const pfc::string8& domain);
extern HBITMAP load_station_logo(metadb_handle_ptr track);
extern Gdiplus::Bitmap* load_station_logo_gdiplus(metadb_handle_ptr track);
extern HBITMAP load_noart_logo(metadb_handle_ptr track);
extern std::unique_ptr<Gdiplus::Bitmap> load_noart_logo_gdiplus(metadb_handle_ptr track);
extern HBITMAP load_noart_logo(const pfc::string8& domain);
extern HBITMAP load_generic_noart_logo(metadb_handle_ptr track);
extern HBITMAP load_generic_noart_logo();
extern std::unique_ptr<Gdiplus::Bitmap> load_generic_noart_logo_gdiplus();

// External functions for triggering main component search
extern void trigger_main_component_search(metadb_handle_ptr track);
extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);
extern void trigger_main_component_local_search(metadb_handle_ptr track);

//=============================================================================
// Event-Driven Artwork System (Forward Declarations)
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

// Forward declare the event manager (implemented in main component)
class ArtworkEventManager {
public:
    static ArtworkEventManager& get();
    void subscribe(IArtworkEventListener* listener);
    void unsubscribe(IArtworkEventListener* listener);
    void notify(const ArtworkEvent& event);
};

// External references to event manager methods (defined in sdk_main.cpp)
extern ArtworkEventManager& get_artwork_event_manager();
extern void subscribe_to_artwork_events(IArtworkEventListener* listener);
extern void unsubscribe_from_artwork_events(IArtworkEventListener* listener);

//=============================================================================
// CUI Artwork Panel Class Definition - Full Implementation
//=============================================================================

class CUIArtworkPanel : public uie::container_uie_window_v3
                      , public now_playing_album_art_notify
                      , public play_callback
                      , public IArtworkEventListener
{
public:
    CUIArtworkPanel();
    ~CUIArtworkPanel();
    
    // Timer management for clear panel functionality
    void update_clear_panel_timer();  // Start/stop clear panel monitoring timer based on setting
    void force_clear_artwork_bitmap();  // Force clear bitmap for "clear panel when not playing" option
    
    // Required uie::window interface
    const GUID& get_extension_guid() const override;
    void get_name(pfc::string_base& out) const override;
    void get_category(pfc::string_base& out) const override;
    unsigned get_type() const override;
    
    // Window management  
    bool is_available(const uie::window_host_ptr& p_host) const override;
    
    // Container window configuration (creates borderless panel like JScript Panel)
    uie::container_window_v3_config get_window_config() override {
        uie::container_window_v3_config config(L"foo_artwork_cui_panel_borderless", false);
        
        // KEY: Keep default extended_window_styles = WS_EX_CONTROLPARENT
        // Do NOT add WS_EX_CLIENTEDGE or WS_EX_STATICEDGE (which cause borders)
        // This matches how JScript Panel creates borderless panels
        
        // Add flicker-reduction window styles and enable double-clicks
        config.window_styles |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        config.class_styles = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        config.class_cursor = IDC_ARROW;
        config.class_background = nullptr;  // No background brush (we handle it in WM_PAINT)
        
        return config;
    }
    
    // Container window message handler (replaces manual window creation)
    LRESULT on_message(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
    
    // Configuration
    void set_config(stream_reader* p_reader, size_t p_size, abort_callback& p_abort) override {}
    void get_config(stream_writer* p_writer, abort_callback& p_abort) const override {}
    bool have_config_popup() const override { return false; }
    
    // foobar2000 callbacks
    void on_album_art(album_art_data::ptr data) noexcept override;
    void on_playback_new_track(metadb_handle_ptr p_track) override;
    void on_playback_stop(play_control::t_stop_reason p_reason) override;
    
    // Required play_callback methods
    void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {}
    void on_playback_seek(double p_time) override {}
    void on_playback_pause(bool p_state) override {}
    void on_playback_edited(metadb_handle_ptr p_track) override {}
    void on_playback_dynamic_info(const file_info& p_info) override {}
    void on_playback_dynamic_info_track(const file_info& p_info) override;
    void on_playback_time(double p_time) override {}
    void on_volume_change(float p_new_val) override {}
    
    // IArtworkEventListener implementation
    void on_artwork_event(const ArtworkEvent& event) override;

private:
    // Window state (managed by container_uie_window_v3)
    HWND m_hWnd;
    
    // GDI+ objects for artwork rendering
    std::unique_ptr<Gdiplus::Graphics> m_graphics;
    std::unique_ptr<Gdiplus::Bitmap> m_artwork_bitmap;
    HBITMAP m_scaled_gdi_bitmap; // GDI bitmap for rendering (like Default UI)
    
    // Artwork state
    std::wstring m_current_artwork_path;
    std::string m_current_artwork_source;
    bool m_artwork_loaded;
    bool m_fit_to_window;
    bool m_was_playing;  // Track previous playback state for clear panel detection
    
    // OSD (On-Screen Display) system
    bool m_show_osd;
    std::string m_osd_text;
    std::string m_artwork_source; // Track the source of current artwork
    std::string m_delayed_search_artist; // Store artist for delayed search
    std::string m_delayed_search_title; // Store title for delayed search
    DWORD m_osd_start_time;
    int m_osd_slide_offset;
    UINT_PTR m_osd_timer_id;
    bool m_osd_visible;
    
    // Event-driven artwork system (replaces polling)
    HBITMAP m_last_event_bitmap;
    
    // Container window message handling (used by container_uie_window_v3)
    // Note: LRESULT on_message() is already declared in public section
    
    // Artwork loading and management
    void load_artwork_from_data(album_art_data::ptr data);
    void load_artwork_from_file(const std::wstring& file_path);
    void clear_artwork();
    
    // Rendering functions
    void paint_artwork(HDC hdc);
    void paint_no_artwork(HDC hdc);
    void paint_loading(HDC hdc);
    
    // OSD functions
    void show_osd(const std::string& text);
    void hide_osd();
    void update_osd_animation();
    void paint_osd(HDC hdc);
    
    // Utility functions
    void resize_artwork_to_fit();
    std::wstring get_formatted_text(const std::string& text);
    void initialize_gdiplus();
    
    // Safe track information access
    bool get_safe_track_path(metadb_handle_ptr track, pfc::string8& path);
    bool is_safe_internet_stream(metadb_handle_ptr track);
    void cleanup_gdiplus();
    bool copy_bitmap_from_main_component(HBITMAP source_bitmap);
    bool load_custom_logo_with_wic(HBITMAP logo_bitmap);  // WIC-based safe loading for CUI
    void search_artwork_for_track(metadb_handle_ptr track);
    void search_artwork_with_metadata(const std::string& artist, const std::string& title);
    bool is_station_name(const std::string& artist, const std::string& title);  // Station name detection helper
    bool is_metadata_valid_for_search(const char* artist, const char* title);  // Metadata validation
	bool is_inverted_internet_stream(metadb_handle_ptr track, const file_info& p_info); //Inverted stream detection helper																													  

public:
    // Static color change handler (public so color client can access it)
    static void g_on_colours_change();
    
    // Constants
    static const int OSD_DELAY_DURATION = 1000;   // 1 second delay before animation starts
    static const int OSD_DURATION_MS = 5000;      // 5 seconds visible duration
    static const int OSD_ANIMATION_SPEED = 8;     // 120 FPS: 1000ms / 120fps ≈ 8ms
    static const int OSD_SLIDE_DISTANCE = 200;
    static const int OSD_SLIDE_IN_DURATION = 300;  // 300ms smooth slide in
    static const int OSD_SLIDE_OUT_DURATION = 300; // 300ms smooth slide out
};

//=============================================================================
// Static variables and registration
//=============================================================================

// Panel GUID - unique identifier for this CUI panel 
static const GUID g_cui_artwork_panel_guid = 
{ 0xB1C2D3E4, 0xF5F6, 0x7890, { 0xAB, 0xCD, 0xEF, 0x13, 0x24, 0x57, 0x68, 0x9B } };

// Factory registration
static uie::window_factory<CUIArtworkPanel> g_cui_artwork_panel_factory;

//=============================================================================
// Global functions for managing CUI clear panel timers
//=============================================================================

// Global list of CUI artwork panels for preference updates
static pfc::list_t<CUIArtworkPanel*> g_cui_artwork_panels;

// Static color change handler implementation
void CUIArtworkPanel::g_on_colours_change() {
    for (t_size i = 0; i < g_cui_artwork_panels.get_count(); i++) {
        CUIArtworkPanel* panel = g_cui_artwork_panels[i];
        if (panel && panel->get_wnd()) {
            InvalidateRect(panel->get_wnd(), NULL, TRUE);
        }
    }
}

// Global function to update CUI timers (called from sdk_main.cpp)
void update_all_cui_clear_panel_timers() {
    for (t_size i = 0; i < g_cui_artwork_panels.get_count(); i++) {
        g_cui_artwork_panels[i]->update_clear_panel_timer();
    }
}

// Registration verification function
static class cui_registration_helper {
public:
    cui_registration_helper() {
    }
} g_cui_registration_helper;

//=============================================================================
// Constructor and Destructor
//=============================================================================

CUIArtworkPanel::CUIArtworkPanel() 
    : m_hWnd(NULL)
    , m_artwork_loaded(false)
    , m_fit_to_window(false)
    , m_was_playing(false)
    , m_show_osd(true)
    , m_osd_start_time(0)
    , m_osd_slide_offset(OSD_SLIDE_DISTANCE)
    , m_osd_timer_id(0)
    , m_osd_visible(false)
    , m_last_event_bitmap(nullptr)
    , m_scaled_gdi_bitmap(NULL)
{
    // Register for artwork events (replaces polling)
    subscribe_to_artwork_events(this);
    
    // Add to global list for preference updates
    g_cui_artwork_panels.add_item(this);
}

CUIArtworkPanel::~CUIArtworkPanel() {
    // Remove from global list
    g_cui_artwork_panels.remove_item(this);
    
    // Unregister from artwork events
    unsubscribe_from_artwork_events(this);
    
    if (m_scaled_gdi_bitmap) {
        DeleteObject(m_scaled_gdi_bitmap);
        m_scaled_gdi_bitmap = NULL;
    }
    cleanup_gdiplus();
}

//=============================================================================
// CUI Extension interface implementation
//=============================================================================

const GUID& CUIArtworkPanel::get_extension_guid() const {
    return g_cui_artwork_panel_guid;
}

void CUIArtworkPanel::get_name(pfc::string_base& out) const {
    out = "Artwork Display";
}

void CUIArtworkPanel::get_category(pfc::string_base& out) const {
    out = "Panels";
}

unsigned CUIArtworkPanel::get_type() const {
    return uie::type_panel;
}

//=============================================================================
// Window management
//=============================================================================

bool CUIArtworkPanel::is_available(const uie::window_host_ptr& p_host) const {
    return true; // Always available
}

// Window creation is now handled by container_uie_window_v3

//=============================================================================
// Window procedure and message handling
//=============================================================================

LRESULT CUIArtworkPanel::on_message(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Store window handle for compatibility
    if (!m_hWnd) {
        m_hWnd = wnd;
    }
    switch (msg) {
    case WM_CREATE:
        // Initialize GDI+
        initialize_gdiplus();
        
        // Register for foobar2000 callbacks
        try {
            now_playing_album_art_notify_manager::get()->add(this);
            
            play_callback_manager::get()->register_callback(this, 
                play_callback::flag_on_playback_new_track | 
                play_callback::flag_on_playback_stop | 
                play_callback::flag_on_playback_dynamic_info_track, false);
            
            // Request artwork for current track if playing
            auto pc = playback_control::get();
            if (pc->is_playing()) {
                metadb_handle_ptr track;
                if (pc->get_now_playing(track)) {
                    on_playback_new_track(track);
                }
                m_was_playing = true;  // Initialize state if currently playing
            }
            
            // Start timer to monitor playback state for clear panel functionality
            if (cfg_clear_panel_when_not_playing) {
                SetTimer(m_hWnd, 102, 500, NULL);  // Timer ID 102, check every 0.5 seconds
                m_current_artwork_source = "CUI Timer auto-started on creation";
                InvalidateRect(m_hWnd, NULL, TRUE);
            }
        } catch (const std::exception& e) {
        }
        
        // Note: No need for polling timer - using event-driven artwork updates
        break;
        
    case WM_DESTROY:
        // Stop timers
        if (m_osd_timer_id) {
            KillTimer(m_hWnd, m_osd_timer_id);
            m_osd_timer_id = 0;
        }
        KillTimer(m_hWnd, 102);  // Stop playback monitoring timer
        // Note: No artwork polling timer to stop - using event-driven system
        
        // Unregister callbacks
        now_playing_album_art_notify_manager::get()->remove(this);
        play_callback_manager::get()->unregister_callback(this);
        
        // Cleanup GDI+
        cleanup_gdiplus();
        break;
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        
        // Use double buffering to eliminate flicker during resizing
        RECT client_rect;
        GetClientRect(m_hWnd, &client_rect);
        
        // Create memory DC and bitmap for double buffering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, client_rect.right, client_rect.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        // Paint to memory DC first (off-screen)
        paint_artwork(memDC);
        
        // Paint OSD if visible
        if (m_osd_visible) {
            paint_osd(memDC);
        }
        
        // Copy the entire off-screen buffer to screen in one operation (flicker-free)
        BitBlt(hdc, 0, 0, client_rect.right, client_rect.bottom, memDC, 0, 0, SRCCOPY);
        
        // Cleanup
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        EndPaint(m_hWnd, &ps);
        return 0;
    }
    
    case WM_SIZE:
        // Resize artwork to fit new window size
        resize_artwork_to_fit();
        // Use RedrawWindow for flicker-free resizing
        RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
        return 0;  // Prevent default processing
        
    case WM_ERASEBKGND:
        // Always return 1 to prevent background erasing (causes flicker)
        return 1;
        
    case WM_TIMER:
        if (wParam == m_osd_timer_id) {
            update_osd_animation();
        } else if (wParam == 100) {
            // Fallback timer - no Default UI panel detected, handle station logos ourselves
            KillTimer(m_hWnd, 100);
            
            // Try to load station logo for internet stream
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            if (pc->get_now_playing(current_track) && current_track.is_valid()) {
                
                // SAFE PATH ACCESS: Use safer helper function
                if (is_safe_internet_stream(current_track)) {
                    // Manually trigger the fallback mechanism
                    PostMessage(m_hWnd, WM_USER + 11, 0, 0);
                }
            }
        } else if (wParam == 101) {
            // Stream delay timer fired - now do the delayed search
            KillTimer(m_hWnd, 101);
            
            // Use stored metadata for delayed search
            if (!m_delayed_search_title.empty()) {
                
                // Apply unified metadata cleaning for consistency with DUI mode
                std::string final_artist = MetadataCleaner::clean_for_search(m_delayed_search_artist.c_str(), true);
                std::string final_title = MetadataCleaner::clean_for_search(m_delayed_search_title.c_str(), true);
                
                extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);
                trigger_main_component_search_with_metadata(final_artist, final_title);
                
                // Clear stored metadata
                m_delayed_search_artist.clear();
                m_delayed_search_title.clear();
            } else {
            }
        } else if (wParam == 102) {
            // Timer ID 102 - playback state monitoring for clear panel
            static_api_ptr_t<playback_control> pc;
            bool is_playing = pc->is_playing();
            
            // If we were playing but now we're not, clear the panel
            if (m_was_playing && !is_playing && cfg_clear_panel_when_not_playing) {
                force_clear_artwork_bitmap();
            }
            
            // Update the previous state
            m_was_playing = is_playing;
            
            // If option is disabled, stop the timer
            if (!cfg_clear_panel_when_not_playing) {
                KillTimer(m_hWnd, 102);
            }
        }
        break;
        
    case WM_USER + 10: // Artwork event update (from background thread)
        {
            HBITMAP bitmap = (HBITMAP)wParam;
            std::string* source_ptr = (std::string*)lParam;
            
            // Extract the source string from the message
            std::string artwork_source = source_ptr ? *source_ptr : "";
            
            // Clean up the allocated source string
            if (source_ptr) {
                delete source_ptr;
            }
            
            // Now it's safe to call UI functions since we're on the main thread
            if (bitmap && copy_bitmap_from_main_component(bitmap)) {
                // Update the member variable with the correct source
                m_artwork_source = artwork_source;
                
                // Only show OSD for online sources, not local files
                if (artwork_source != "Local file" && !artwork_source.empty()) {
                    show_osd("Artwork from " + artwork_source);
                }
                
                InvalidateRect(m_hWnd, NULL, FALSE);
                UpdateWindow(m_hWnd); // Force immediate repaint
            } else {
                // Clean up even if bitmap processing failed
            }
        }
        break;
        
    case WM_USER + 11: // Handle -noart fallback on main thread
        {
            
            // Now it's safe to access foobar2000 APIs and UI functions
            try {
                static_api_ptr_t<playback_control> pc;
                metadb_handle_ptr current_track;
                if (pc->get_now_playing(current_track) && current_track.is_valid()) {
                    // SAFE PATH ACCESS: Add try-catch to prevent crashes
                    pfc::string8 path;
                    bool is_internet_stream = false;
                    // SAFE PATH ACCESS: Use safer helper function
                    if (get_safe_track_path(current_track, path)) {
                        is_internet_stream = (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://"));
                    } else {
                        is_internet_stream = false;
                    }
                    
                    if (is_internet_stream && cfg_enable_custom_logos) {
                        // CRASH FIX: Add safe guards before complex file operations
                        try {
                            // First check if track is still valid
                            if (!current_track.is_valid()) return DefWindowProc(wnd, msg, wParam, lParam);
                            
                            // Try to extract domain from URL - do this safely
                            pfc::string8 domain = extract_domain_from_stream_url(current_track);
                            if (!domain.is_empty() && domain.length() < 256) { // Prevent overly long domains
                            
                                // CRASH FIX: Use safer logo loading without direct file operations in CUI
                                // Instead of complex file path building, use the SDK functions which are already crash-protected
                                bool fallback_loaded = false;
                                
                                // Priority 1: Station logo (with full path + domain fallback)
                                if (!fallback_loaded) {
                                    // First try direct GDI+ loading to preserve transparency
                                    Gdiplus::Bitmap* gdi_logo = load_station_logo_gdiplus(current_track);
                                    if (gdi_logo && gdi_logo->GetLastStatus() == Gdiplus::Ok) {
                                        
                                        // Set the artwork directly from GDI+ bitmap
                                        m_artwork_bitmap = std::unique_ptr<Gdiplus::Bitmap>(gdi_logo);
                                        m_artwork_loaded = true;
                                        m_artwork_source = "Station logo";
                                        fallback_loaded = true;
                                        InvalidateRect(get_wnd(), NULL, TRUE);
                                    } else {
                                        // Clean up failed GDI+ bitmap
                                        delete gdi_logo;
                                        
                                        // Fallback to HBITMAP method
                                        HBITMAP logo_bitmap = load_station_logo(current_track);
                                        if (logo_bitmap) {
                                            // CRASH FIX: Use WIC-based loading for CUI compatibility
                                            if (load_custom_logo_with_wic(logo_bitmap)) {
                                                m_artwork_loaded = true;
                                                m_artwork_source = "Station logo";
                                                fallback_loaded = true;
                                            }
                                            DeleteObject(logo_bitmap); // Always clean up the source bitmap
                                        }
                                    }
                                }
                                
                                // Priority 2: Station-specific noart (with full URL path support)
                                if (!fallback_loaded) {
                                    auto noart_bitmap = load_noart_logo_gdiplus(current_track);
                                    if (noart_bitmap && noart_bitmap->GetLastStatus() == Gdiplus::Ok) {
                                        // Set the artwork directly from GDI+ bitmap
                                        m_artwork_bitmap = std::move(noart_bitmap);
                                        m_artwork_loaded = true;
                                        m_artwork_source = "Station fallback (no artwork)";
                                        fallback_loaded = true;
                                    }
                                }
                                
                                // Priority 3: Generic noart (with full URL path support)
                                if (!fallback_loaded) {
                                    auto generic_bitmap = load_generic_noart_logo_gdiplus();
                                    if (generic_bitmap && generic_bitmap->GetLastStatus() == Gdiplus::Ok) {
                                        // Set the artwork directly from GDI+ bitmap
                                        m_artwork_bitmap = std::move(generic_bitmap);
                                        m_artwork_loaded = true;
                                        m_artwork_source = "Generic fallback (no artwork)";
                                        fallback_loaded = true;
                                    }
                                }
                                
                                if (fallback_loaded) {
                                    resize_artwork_to_fit();
                                    InvalidateRect(m_hWnd, NULL, FALSE);
                                    UpdateWindow(m_hWnd);
                                }
                            } // End domain check
                        } catch (...) {
                            // Silently handle any exceptions in custom logo loading
                        }
                    }
                }
            } catch (...) {
                // Silently handle any exceptions in track processing
            }
        }
        break;
        
    case WM_RBUTTONDOWN:
        // Show/hide OSD on right click
        m_show_osd = !m_show_osd;
        break;
        
    case WM_LBUTTONDBLCLK:
        // Open artwork viewer popup on double-click
        if (m_artwork_loaded && m_artwork_bitmap) {
            try {
                // Create source info string from current source
                std::string source_info = m_artwork_source;
                if (source_info.empty()) {
                    source_info = "Local file"; // Default assumption for unknown source
                }
                
                // Create and show the popup viewer
                ArtworkViewerPopup* popup = new ArtworkViewerPopup(m_artwork_bitmap.get(), source_info);
                if (popup) {
                    popup->ShowPopup(wnd);
                    // Note: The popup will delete itself when closed
                }
            } catch (...) {
                // Handle any errors silently
            }
        }
        break;
    }
    
    return DefWindowProc(wnd, msg, wParam, lParam);
}

//=============================================================================
// foobar2000 callback implementations
//=============================================================================

void CUIArtworkPanel::on_album_art(album_art_data::ptr data) noexcept {
    if (!m_hWnd) return;
    
    
    try {
        if (data.is_valid() && data->get_size() > 0) {
            
            // CRITICAL: Block embedded artwork completely for internet streams
            // User requirement: never check embedded artwork on internet streams
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            
            if (pc->get_now_playing(current_track) && current_track.is_valid()) {
                // Check if this is an internet stream
                pfc::string8 file_path;
                if (get_safe_track_path(current_track, file_path)) {
                    bool is_internet_stream = (strstr(file_path.c_str(), "://") && !strstr(file_path.c_str(), "file://"));
                    
                    if (is_internet_stream) {
                        return; // Never use embedded artwork for internet streams
                    }
                }
            }
            
            // PRIORITY CHECK: For local files, prefer local artwork files over embedded artwork
            bool should_prefer_local = false;
            
            if (current_track.is_valid()) {
                // SAFE PATH ACCESS: Add try-catch to prevent crashes
                // SAFE PATH ACCESS: Use safer helper function
                pfc::string8 file_path;
                if (get_safe_track_path(current_track, file_path)) {
                    bool is_local_file = !(strstr(file_path.c_str(), "://") && !strstr(file_path.c_str(), "file://"));
                    
                    if (is_local_file) {
                        // Check if main component already found local artwork
                        HBITMAP main_bitmap = get_main_component_artwork_bitmap();
                        if (main_bitmap) {
                            should_prefer_local = true;
                            
                            if (copy_bitmap_from_main_component(main_bitmap)) {
                                m_artwork_source = "Local file";
                                // No OSD for local files - they should load silently
                                InvalidateRect(m_hWnd, NULL, FALSE);
                                UpdateWindow(m_hWnd); // Force immediate repaint
                                return;
                            }
                        }
                    }
                }
            }
            
            // If no local artwork found or this is a stream, use embedded artwork
            if (!should_prefer_local) {
                load_artwork_from_data(data);
                m_artwork_source = "Local file"; // Use consistent label for all local sources
            }
        } else {
            // Keep previous artwork visible - don't clear source info
            // m_artwork_source remains unchanged to preserve last known source
        }
        
        InvalidateRect(m_hWnd, NULL, FALSE);
        UpdateWindow(m_hWnd); // Force immediate repaint
    } catch (...) {
        // Handle any exceptions gracefully
        // Keep previous artwork visible - don't clear source info
        // m_artwork_source remains unchanged to preserve last known source
        InvalidateRect(m_hWnd, NULL, FALSE);
        UpdateWindow(m_hWnd); // Force immediate repaint
    }
}

void CUIArtworkPanel::on_playback_new_track(metadb_handle_ptr p_track) {
    if (!m_hWnd) return;
    
	 // Check if it's an internet stream and custom logos enabled
    double length = p_track->get_length();
    if (length <= 0 && cfg_enable_custom_logos) {
        pfc::string8 path = p_track->get_path();
        pfc::string8 result = path;
        for (size_t i = 0; i < result.length(); i++) {
            if (result[i] == '/') { result.set_char(i, '-'); }
            else if (result[i] == '\\') { result.set_char(i, '-'); }
            else if (result[i] == '|') { result.set_char(i, '-'); }
            else if (result[i] == ':') { result.set_char(i, '-'); }
            else if (result[i] == '*') { result.set_char(i, 'x'); }
            else if (result[i] == '"') { result.set_char(i, '\'\''); }
            else if (result[i] == '<') { result.set_char(i, '_'); }
            else if (result[i] == '>') { result.set_char(i, '_'); }
            else if (result[i] == '?') { result.set_char(i, '_'); }

        }

        std::string str = "foo_artwork - Filename for Full URL Path Matching LOGO: ";
        str.append(result);
        const char* cstr = str.c_str();

        //console log it for the user to know what filename to use
        console::info(cstr);
    }
    // Keep previous artwork visible - don't clear source info
    // m_artwork_source remains unchanged to preserve last known source
    // Don't invalidate here - let new artwork trigger the redraw
    
    if (p_track.is_valid()) {
        
        // PRIORITY FIX: For local files, immediately trigger main component local artwork search
        // This ensures local files are found before embedded artwork loads
        
        // SAFE PATH HANDLING: Use safer helper function
        pfc::string8 file_path;
        bool is_local_file = false;
        if (get_safe_track_path(p_track, file_path)) {
            is_local_file = (strstr(file_path.c_str(), "file://") == file_path.c_str()) || 
                           !(strstr(file_path.c_str(), "://"));
        }
        
        if (is_local_file) {
            // Force main component to search for local artwork immediately
            trigger_main_component_local_search(p_track);
        } else {
            // For internet streams, let the main component handle the stream delay for metadata search
            // API search should always be the primary priority, station logos are fallbacks only
        }
        
        // Check if main component's artwork manager has artwork for this track
        HBITMAP main_bitmap = get_main_component_artwork_bitmap();
        
        if (main_bitmap) {
            if (copy_bitmap_from_main_component(main_bitmap)) {
                m_last_event_bitmap = main_bitmap;
                m_artwork_source = "Local file";
                // No OSD for local files - they should load silently
                InvalidateRect(m_hWnd, NULL, FALSE);
                UpdateWindow(m_hWnd); // Force immediate repaint
                return;
            }
        }
        
        // If main component is loading artwork, wait for it
        if (g_artwork_loading) {
            return;
        }
        
        // Station logos and fallback images are now handled after metadata search fails
        // This allows metadata-based artwork to have priority
        
        // If no Default UI element is active, wait for dynamic metadata
        
        // Set a timeout to handle station logos if no Default UI panel is active
        // This timer will fire if no artwork events are received within a reasonable time
        if (m_hWnd) {
            SetTimer(m_hWnd, 100, 3000, NULL); // Timer ID 100, 3 second timeout
        }
    }
}

void CUIArtworkPanel::on_playback_stop(play_control::t_stop_reason p_reason) {
    if (!m_hWnd) return;
    
    // Clear artwork if option is enabled
    if (cfg_clear_panel_when_not_playing) {
        m_artwork_bitmap.reset();  // Properly release GDI+ bitmap
        m_current_artwork_source = "Panel cleared - not playing";
        m_osd_text = "";
        InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

void CUIArtworkPanel::on_playback_dynamic_info_track(const file_info& p_info) {
    if (!m_hWnd) return;
    
    // Extract metadata from dynamic info
    std::string artist, title;
    
    if (p_info.meta_get("ARTIST", 0)) {
        artist = p_info.meta_get("ARTIST", 0);
    }
    if (p_info.meta_get("TITLE", 0)) {
        title = p_info.meta_get("TITLE", 0);
    }
    
    
    // Clean up metadata using the unified UTF-8 safe cleaner
    std::string original_title = title;
    std::string original_artist = artist;
    
    // Use the unified metadata cleaner with Cyrillic preservation
    std::string cleaned_artist = MetadataCleaner::clean_for_search(artist.c_str(), true);
    std::string cleaned_title = MetadataCleaner::clean_for_search(title.c_str(), true);
    
    // 2. Remove featuring patterns in parentheses/brackets (including unclosed ones)
    auto remove_featuring_in_brackets = [](std::string& str) {
        // Remove featuring patterns in parentheses - handles both complete and unclosed parentheses
        // Patterns: (ft. Artist), (feat. Artist), (featuring Artist), (Ft. Artist), etc.
        std::vector<std::string> feat_patterns = {
            "ft\\.", "feat\\.", "featuring", "Ft\\.", "Feat\\.", "Featuring", 
            "FT\\.", "FEAT\\.", "FEATURING"
        };
        
        for (const auto& pattern : feat_patterns) {
            // Match parentheses with featuring pattern: "(ft. Artist)" or "(ft. Artist" (unclosed)
            std::string paren_regex = "\\s*\\(\\s*" + pattern + "\\s+[^)]*\\)?\\s*";
            str = std::regex_replace(str, std::regex(paren_regex, std::regex_constants::icase), " ");
            
            // Match square brackets with featuring pattern: "[ft. Artist]" or "[ft. Artist" (unclosed)
            std::string bracket_regex = "\\s*\\[\\s*" + pattern + "\\s+[^\\]]*\\]?\\s*";
            str = std::regex_replace(str, std::regex(bracket_regex, std::regex_constants::icase), " ");
        }
        
        // Clean up any remaining unmatched opening brackets/parentheses at the end
        // This handles cases like "Song Title (Ft. Artist" where the closing ) is missing
        std::regex trailing_open("\\s*[\\(\\[]\\s*$");
        str = std::regex_replace(str, trailing_open, "");
        
        // Remove multiple consecutive spaces
        std::regex multi_space("\\s{2,}");
        str = std::regex_replace(str, multi_space, " ");
        
        // Trim leading and trailing spaces
        str = std::regex_replace(str, std::regex("^\\s+|\\s+$"), "");
    };
    
    // 3. Remove bracketed information and quality indicators
    auto remove_bracketed_info = [](std::string& str) {
        std::vector<std::string> patterns = {
            "[Live]", "[Explicit]", "[Remastered]", "[Radio Edit]", "[Extended]", "[Remix]",
            "[HD]", "[HQ]", "[320kbps]", "[256kbps]", "[192kbps]", "[128kbps]",
            "(Live)", "(Explicit)", "(Remastered)", "(Radio Edit)", "(Extended)", "(Remix)",
            "(HD)", "(HQ)", "(320kbps)", "(256kbps)", "(192kbps)", "(128kbps)"
        };
        
        for (const auto& pattern : patterns) {
            size_t pos = 0;
            while ((pos = str.find(pattern, pos)) != std::string::npos) {
                str.erase(pos, pattern.length());
                // Clean up any double spaces
                while ((pos = str.find("  ", pos)) != std::string::npos) {
                    str.replace(pos, 2, " ");
                }
            }
        }
    };
    
    // 3. Remove time/duration indicators
    auto remove_duration_info = [](std::string& str) {
        // Remove (MM:SS) or (M:SS) patterns
        std::regex duration_regex("\\s*\\(\\d{1,2}:\\d{2}\\)\\s*");
        str = std::regex_replace(str, duration_regex, " ");
        
        // Remove " - MM:SS" or " - M:SS" timestamp patterns at the end (like " - 0:00")
        std::regex dash_time_regex("\\s+-\\s+\\d{1,2}:\\d{2}\\s*$");
        str = std::regex_replace(str, dash_time_regex, "");
        
        // Remove " - MM.SS" or " - M.SS" timestamp patterns with decimal point (like " - 0.00")
        std::regex dash_decimal_regex("\\s+-\\s+\\d{1,2}\\.\\d{2}\\s*$");
        str = std::regex_replace(str, dash_decimal_regex, "");
        
        // Remove " - X.XX" numeric timestamp patterns at the end
        size_t dash_pos = str.rfind(" - ");
        if (dash_pos != std::string::npos) {
            std::string suffix = str.substr(dash_pos + 3);
            // Check if suffix is a number (with optional decimal point)
            bool is_numeric = true;
            bool has_dot = false;
            for (char c : suffix) {
                if (c == '.' && !has_dot) {
                    has_dot = true;
                } else if (!isdigit(c)) {
                    is_numeric = false;
                    break;
                }
            }
            if (is_numeric && !suffix.empty()) {
                str = str.substr(0, dash_pos);
            }
        }
    };
    
    // 4. Normalize artist collaborations
    auto normalize_collaborations = [](std::string& str) {
        // Normalize featuring patterns
        std::vector<std::pair<std::string, std::string>> feat_patterns = {
            {" ft. ", " feat. "}, {" ft ", " feat. "}, {" featuring ", " feat. "},
            {" Ft. ", " feat. "}, {" Ft ", " feat. "}, {" Featuring ", " feat. "},
            {" FT. ", " feat. "}, {" FT ", " feat. "}, {" FEATURING ", " feat. "}
        };
        
        for (const auto& pattern : feat_patterns) {
            size_t pos = 0;
            while ((pos = str.find(pattern.first, pos)) != std::string::npos) {
                str.replace(pos, pattern.first.length(), pattern.second);
                pos += pattern.second.length();
            }
        }
        
        // Normalize vs/versus patterns
        std::vector<std::pair<std::string, std::string>> vs_patterns = {
            {" vs. ", " vs "}, {" vs ", " vs "}, {" versus ", " vs "},
            {" Vs. ", " vs "}, {" Vs ", " vs "}, {" Versus ", " vs "},
            {" VS. ", " vs "}, {" VS ", " vs "}, {" VERSUS ", " vs "}
        };
        
        for (const auto& pattern : vs_patterns) {
            size_t pos = 0;
            while ((pos = str.find(pattern.first, pos)) != std::string::npos) {
                str.replace(pos, pattern.first.length(), pattern.second);
                pos += pattern.second.length();
            }
        }
        
        // Normalize & patterns (be careful not to break band names)
        str = std::regex_replace(str, std::regex("\\s+&\\s+"), " & ");
    };
    
    // 5. Clean up encoding artifacts and normalize spacing
    auto clean_encoding_artifacts = [](std::string& str) {
        // Normalize quotes and apostrophes
        std::vector<std::pair<std::string, std::string>> quote_patterns = {
            {"\u201C", "\""}, {"\u201D", "\""}, {"\u2018", "'"}, {"\u2019", "'"},
            {"\u201A", "'"}, {"\u201E", "\""}, {"\u2039", "<"}, {"\u203A", ">"}
        };
        
        for (const auto& pattern : quote_patterns) {
            size_t pos = 0;
            while ((pos = str.find(pattern.first, pos)) != std::string::npos) {
                str.replace(pos, pattern.first.length(), pattern.second);
                pos += pattern.second.length();
            }
        }
        
        // Remove multiple consecutive spaces
        std::regex multi_space("\\s{2,}");
        str = std::regex_replace(str, multi_space, " ");
        
        // Trim leading and trailing spaces
        str = std::regex_replace(str, std::regex("^\\s+|\\s+$"), "");
    };
    
    // Remove all parenthetical content (like "(Vocal Version)", "(Remix)", etc.)
    auto remove_all_parentheses = [](std::string& str) {
        std::regex paren_content_regex("\\s*\\([^)]*\\)\\s*");
        str = std::regex_replace(str, paren_content_regex, " ");
        
        // Clean up multiple spaces
        std::regex multi_space("\\s{2,}");
        str = std::regex_replace(str, multi_space, " ");
        
        // Trim leading and trailing spaces
        str = std::regex_replace(str, std::regex("^\\s+|\\s+$"), "");
    };
    
    // Apply the unified cleaning to both artist and title  
    // This replaces all the complex lambda functions with UTF-8 safe processing
    artist = cleaned_artist;
    title = cleaned_title;
    
    // Apply comprehensive metadata validation using the unified cleaner (same as DUI)
    if (!MetadataCleaner::is_valid_for_search(artist.c_str(), title.c_str())) {
        return;
    }
    
    // If we have valid metadata and aren't already loading, trigger main component search
    if (!g_artwork_loading) {
        // Get current track to check if it's a stream
        auto pc = playback_control::get();
        metadb_handle_ptr current_track;
        if (pc->get_now_playing(current_track) && current_track.is_valid()) {
            // Check if this is an internet stream
            // SAFE PATH ACCESS: Use safer helper function
            bool is_internet_stream = is_safe_internet_stream(current_track);
            
            if (is_internet_stream) {
                // For internet streams, respect the stream delay configuration
                extern cfg_int cfg_stream_delay;
                int delay_seconds = (int)cfg_stream_delay;
                
                // If inverted swap artist title
                bool is_inverted_stream = is_inverted_internet_stream(current_track, p_info);												
                
                if (delay_seconds > 0) {
                    // Clear any previous artwork to respect stream delay
                    extern HBITMAP g_shared_artwork_bitmap;
                    if (g_shared_artwork_bitmap) {
                        DeleteObject(g_shared_artwork_bitmap);
                        g_shared_artwork_bitmap = NULL;
                    }
                    
                    // Store metadata for delayed search
                    if (is_inverted_stream) {
                        m_delayed_search_artist = title;
                        m_delayed_search_title = artist;
                    }
                    else
                    {
                        m_delayed_search_artist = artist;
                        m_delayed_search_title = title;
                    }
                    
                    // Set a timer to delay the search
                    if (m_hWnd) {
                        // Use timer ID 101 for stream delay
                        SetTimer(m_hWnd, 101, delay_seconds * 1000, NULL);
                        
                        // Note: No polling timer needed - using event-driven updates
                    }
                } else {
                    // No delay configured, search immediately
                    if (is_inverted_stream) {
                        std::string artist_old = artist;
                        std::string title_old = title;
                        artist = title_old;
                        title = artist_old;
                    }											 
                    extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);
                    trigger_main_component_search_with_metadata(artist, title);
                    
                    // Note: No polling timer needed - using event-driven updates
                }
                return;
            } else {
                // For local files, do NOT trigger API searches - local artwork will be handled elsewhere
                // API searches should only occur for internet streams
            }
        }
    }
}

//=============================================================================
// Artwork loading and management
//=============================================================================

void CUIArtworkPanel::load_artwork_from_data(album_art_data::ptr data) {
    if (!data.is_valid() || data->get_size() == 0) {
        // Keep previous artwork visible - don't clear
        return;
    }
    
    try {
        // Create memory stream from artwork data
        const void* artwork_data = data->get_ptr();
        size_t artwork_size = data->get_size();
        
        // Create IStream from memory
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, artwork_size);
        if (!hGlobal) {
            // Keep previous artwork visible - don't clear
            return;
        }
        
        void* pBuffer = GlobalLock(hGlobal);
        if (!pBuffer) {
            GlobalFree(hGlobal);
            // Keep previous artwork visible - don't clear
            return;
        }
        
        memcpy(pBuffer, artwork_data, artwork_size);
        GlobalUnlock(hGlobal);
        
        IStream* pStream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hGlobal, TRUE, &pStream))) {
            GlobalFree(hGlobal);
            // Keep previous artwork visible - don't clear
            return;
        }
        
        // Create bitmap from stream
        auto new_bitmap = std::make_unique<Gdiplus::Bitmap>(pStream);
        pStream->Release();
        
        if (new_bitmap && new_bitmap->GetLastStatus() == Gdiplus::Ok) {
            m_artwork_bitmap = std::move(new_bitmap);
            m_artwork_loaded = true;
            m_current_artwork_source = "Local file"; // Use consistent label for all local sources
            
            // Resize to fit window
            resize_artwork_to_fit();
            
        } else {
            // Keep previous artwork visible - don't clear on load failure
        }
    } catch (...) {
        // Keep previous artwork visible - don't clear on exception
    }
}

void CUIArtworkPanel::load_artwork_from_file(const std::wstring& file_path) {
    try {
        auto new_bitmap = std::make_unique<Gdiplus::Bitmap>(file_path.c_str());
        
        if (new_bitmap && new_bitmap->GetLastStatus() == Gdiplus::Ok) {
            m_artwork_bitmap = std::move(new_bitmap);
            m_artwork_loaded = true;
            m_current_artwork_path = file_path;
            m_current_artwork_source = "Local file";
            
            // Resize to fit window
            resize_artwork_to_fit();
            
        } else {
            // Keep previous artwork visible - don't clear on load failure
        }
    } catch (...) {
        // Keep previous artwork visible - don't clear on exception
    }
}

void CUIArtworkPanel::clear_artwork() {
    m_artwork_bitmap.reset();
    if (m_scaled_gdi_bitmap) {
        DeleteObject(m_scaled_gdi_bitmap);
        m_scaled_gdi_bitmap = NULL;
    }
    m_artwork_loaded = false;
    m_current_artwork_path.clear();
    m_current_artwork_source.clear();
}

// Start/stop clear panel monitoring timer based on configuration
void CUIArtworkPanel::update_clear_panel_timer() {
    if (!m_hWnd) {
        return;
    }
    
    if (cfg_clear_panel_when_not_playing) {
        // Start the timer if option is enabled
        SetTimer(m_hWnd, 102, 500, NULL);  // Timer ID 102, check every 0.5 seconds
    } else {
        // Stop the timer if option is disabled
        KillTimer(m_hWnd, 102);
    }
}

// Force clear artwork bitmap for "clear panel when not playing" option
void CUIArtworkPanel::force_clear_artwork_bitmap() {
    // Clear the GDI+ bitmap
    if (m_artwork_bitmap) {
        m_artwork_bitmap.reset();
    }
    
    // Clear the GDI bitmap
    if (m_scaled_gdi_bitmap) {
        DeleteObject(m_scaled_gdi_bitmap);
        m_scaled_gdi_bitmap = NULL;
    }
    
    // Clear artwork state
    m_artwork_loaded = false;
    m_current_artwork_path.clear();
    m_current_artwork_source.clear();
    
    // Invalidate window to redraw
    if (m_hWnd) {
        InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

//=============================================================================
// Rendering functions
//=============================================================================

void CUIArtworkPanel::paint_artwork(HDC hdc) {
    RECT client_rect;
    GetClientRect(m_hWnd, &client_rect);
    
    // Use pure GDI painting like Default UI to prevent flickering during OSD animation
    // Clear background using Columns UI color scheme
    cui::colours::helper colors;
    COLORREF bg_color = colors.get_colour(cui::colours::colour_background);
    
    // Fill background with solid brush (GDI approach like Default UI)
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    FillRect(hdc, &client_rect, bg_brush);
    DeleteObject(bg_brush);
    
    // Use our own stored artwork bitmap instead of asking main component each time
    if (m_artwork_loaded && m_scaled_gdi_bitmap) {
        // Use pure GDI rendering (like Default UI) to prevent OSD flickering
        BITMAP bm;
        GetObject(m_scaled_gdi_bitmap, sizeof(bm), &bm);
        
        // Center the artwork
        int x = (client_rect.right - bm.bmWidth) / 2;
        int y = (client_rect.bottom - bm.bmHeight) / 2;
        
        // Create memory DC for the bitmap
        HDC memDC = CreateCompatibleDC(hdc);
        if (memDC) {
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, m_scaled_gdi_bitmap);
            
            // PNG transparency fix: Check if bitmap has alpha channel (32-bit) and use AlphaBlend for transparency
            // This fixes the issue where PNG transparency works in DUI mode but not CUI mode
            if (bm.bmBitsPixel == 32) {
                // Use AlphaBlend for PNG transparency support (preserves alpha channel)
                BLENDFUNCTION blend = {};
                blend.BlendOp = AC_SRC_OVER;
                blend.BlendFlags = 0;
                blend.SourceConstantAlpha = 255; // Fully opaque (use per-pixel alpha)
                blend.AlphaFormat = AC_SRC_ALPHA; // Use source alpha channel
                
                AlphaBlend(hdc, x, y, bm.bmWidth, bm.bmHeight, 
                          memDC, 0, 0, bm.bmWidth, bm.bmHeight, blend);
            } else {
                // Fall back to BitBlt for non-alpha bitmaps
                BitBlt(hdc, x, y, bm.bmWidth, bm.bmHeight, memDC, 0, 0, SRCCOPY);
            }
            
            // Cleanup
            SelectObject(memDC, oldBitmap);
            DeleteDC(memDC);
        }
        return; // Early return when artwork is drawn
    } else {
        // Only show "no artwork" message if panel wasn't deliberately cleared
        if (!m_current_artwork_source.empty()) {
            // Draw "no artwork" message using GDI (like Default UI)
            SetBkMode(hdc, TRANSPARENT);
            
            // Get text color from CUI color scheme
            COLORREF text_color = colors.get_colour(cui::colours::colour_text);
            SetTextColor(hdc, text_color);
            
            const char* text = "No artwork available";
            DrawTextA(hdc, text, -1, &client_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        // If m_current_artwork_source is empty, panel was cleared - show nothing
    }
}

void CUIArtworkPanel::paint_no_artwork(HDC hdc) {
    // Only show message if panel wasn't deliberately cleared
    if (!m_current_artwork_source.empty()) {
        RECT client_rect;
        GetClientRect(m_hWnd, &client_rect);
        
        // Draw text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(100, 100, 100));
        
        const char* text = "No artwork available";
        DrawTextA(hdc, text, -1, &client_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // If m_current_artwork_source is empty, panel was cleared - show nothing
}

void CUIArtworkPanel::paint_loading(HDC hdc) {
    // Loading indicator without text - just show background
    RECT client_rect;
    GetClientRect(m_hWnd, &client_rect);
}

//=============================================================================
// OSD functions
//=============================================================================

void CUIArtworkPanel::show_osd(const std::string& text) {
    
    // Check if this is a local file OSD call that should be blocked
    if (text.find("Local file") != std::string::npos || 
        text.find("local") != std::string::npos) {
        return;
    }
    
    // Also check if current track is a local file - if so, block all OSD
    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr current_track;
    if (pc->get_now_playing(current_track) && current_track.is_valid()) {
        // SAFE PATH ACCESS: Use safer helper function
        pfc::string8 current_path;
        if (get_safe_track_path(current_track, current_path)) {
            bool is_current_local = (strstr(current_path.c_str(), "file://") == current_path.c_str()) || 
                                   !(strstr(current_path.c_str(), "://"));
            if (is_current_local) {
                return;
            }
        }
    }
    
    // Check global preference setting first
    if (!cfg_show_osd) return;
    
    // Then check local panel setting
    if (!m_show_osd) return;
    
    m_osd_text = text;
    m_osd_start_time = GetTickCount();
    m_osd_slide_offset = OSD_SLIDE_DISTANCE;
    m_osd_visible = true;
    
    // Start animation timer for 120 FPS
    if (m_osd_timer_id) {
        KillTimer(m_hWnd, m_osd_timer_id);
    }
    m_osd_timer_id = SetTimer(m_hWnd, 1, OSD_ANIMATION_SPEED, nullptr);
    
    InvalidateRect(m_hWnd, NULL, FALSE);
}

void CUIArtworkPanel::hide_osd() {
    m_osd_visible = false;
    if (m_osd_timer_id) {
        KillTimer(m_hWnd, m_osd_timer_id);
        m_osd_timer_id = 0;
    }
    InvalidateRect(m_hWnd, NULL, FALSE);
}

void CUIArtworkPanel::update_osd_animation() {
    if (!m_osd_visible) return;
    
    // Store previous offset for optimized invalidation
    int previous_offset = m_osd_slide_offset;
    
    DWORD current_time = GetTickCount();
    DWORD elapsed = current_time - m_osd_start_time;
    
    // Animation phases:
    // 1. Delay: 0-1000ms (invisible, off-screen)
    // 2. Slide in: 1000ms-1300ms
    // 3. Fully visible: 1300ms-6300ms 
    // 4. Slide out: 6300ms-6600ms
    // 5. Hidden: >6600ms
    
    if (elapsed <= OSD_DELAY_DURATION) {
        // Delay phase: keep OSD invisible (fully off-screen)
        m_osd_slide_offset = OSD_SLIDE_DISTANCE;  // Stay off-screen
        
    } else if (elapsed <= (OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION)) {
        // Slide in phase: smooth easing from right
        DWORD slide_in_elapsed = elapsed - OSD_DELAY_DURATION;
        double progress = (double)slide_in_elapsed / OSD_SLIDE_IN_DURATION;
        // Use easing function for smooth animation (ease-out cubic)
        double eased_progress = 1.0 - pow(1.0 - progress, 3.0);
        m_osd_slide_offset = (int)(OSD_SLIDE_DISTANCE * (1.0 - eased_progress));
        
    } else if (elapsed <= (OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DURATION_MS)) {
        // Fully visible phase
        m_osd_slide_offset = 0;
        
    } else if (elapsed <= (OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DURATION_MS + OSD_SLIDE_OUT_DURATION)) {
        // Slide out phase: smooth easing to right
        DWORD slide_out_elapsed = elapsed - (OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DURATION_MS);
        double progress = (double)slide_out_elapsed / OSD_SLIDE_OUT_DURATION;
        // Use easing function for smooth animation (ease-in cubic)
        double eased_progress = pow(progress, 3.0);
        m_osd_slide_offset = (int)(OSD_SLIDE_DISTANCE * eased_progress);
        
    } else {
        // Animation complete - hide OSD
        hide_osd();
        return;
    }
    
    // Only invalidate OSD regions if offset has changed (prevents artwork flickering)
    if (m_hWnd && previous_offset != m_osd_slide_offset) {
        RECT client_rect;
        GetClientRect(m_hWnd, &client_rect);
        
        // Calculate actual OSD dimensions by measuring text
        HDC hdc = GetDC(m_hWnd);
        HFONT font = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT old_font = (HFONT)SelectObject(hdc, font);
        
        SIZE text_size;
        GetTextExtentPoint32A(hdc, m_osd_text.c_str(), (int)m_osd_text.length(), &text_size);
        
        SelectObject(hdc, old_font);
        DeleteObject(font);
        ReleaseDC(m_hWnd, hdc);
        
        // Use actual measured dimensions with padding
        int osd_width = text_size.cx + 40;  // Match paint_osd padding
        int osd_height = text_size.cy + 20; // Match paint_osd padding
        int osd_y = client_rect.bottom - osd_height - 10;
        
        // Calculate both old and new OSD positions
        int old_osd_x = client_rect.right - osd_width - 10 + previous_offset;
        int new_osd_x = client_rect.right - osd_width - 10 + m_osd_slide_offset;
        
        // Only invalidate if the OSD is actually visible on screen
        if (new_osd_x < client_rect.right && osd_y >= 0) {
            // Create minimal invalidation rectangle - just the exact area that changed
            int min_x = (old_osd_x < new_osd_x ? old_osd_x : new_osd_x);
            int max_x = (old_osd_x > new_osd_x ? old_osd_x : new_osd_x) + osd_width;
            
            // Clamp to client area bounds
            min_x = min_x > 0 ? min_x : 0;
            max_x = max_x < client_rect.right ? max_x : client_rect.right;
            
            // Only invalidate if there's actually an area to update
            if (min_x < max_x) {
                RECT invalidate_rect = { 
                    min_x, 
                    osd_y, 
                    max_x, 
                    osd_y + osd_height 
                };
                
                // Simple solution: Just invalidate the minimal exact area
                InvalidateRect(m_hWnd, &invalidate_rect, FALSE);
            }
        }
    }
}

void CUIArtworkPanel::paint_osd(HDC hdc) {
    if (!m_osd_visible || m_osd_text.empty()) return;
    
    // Save the current GDI state (exactly like Default UI)
    int saved_dc = SaveDC(hdc);
    
    RECT client_rect;
    GetClientRect(m_hWnd, &client_rect);
    
    // Calculate OSD dimensions using the same approach as Default UI
    SIZE text_size;
    HFONT old_font = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32A(hdc, m_osd_text.c_str(), (int)m_osd_text.length(), &text_size);
    
    const int padding = 8;  // Match Default UI padding
    const int osd_width = text_size.cx + (padding * 2);
    const int osd_height = text_size.cy + (padding * 2);
    
    // Position in bottom-right corner with slide offset (exactly like Default UI)
    int osd_x = client_rect.right - osd_width - 10 + m_osd_slide_offset;
    int osd_y = client_rect.bottom - osd_height - 10;
    
    // Don't draw if completely slid off screen (exactly like Default UI)
    if (osd_x >= client_rect.right) {
        RestoreDC(hdc, saved_dc);
        return;
    }
    
    // Create OSD rectangle
    RECT osd_rect = { osd_x, osd_y, osd_x + osd_width, osd_y + osd_height };
    
    // Get CUI colors for OSD background (adapted from Default UI approach)
    COLORREF osd_bg_color, osd_border_color, text_color;
    
    try {
        cui::colours::helper colors;
        COLORREF base_bg_color = colors.get_colour(cui::colours::colour_background);
        
        // Create darker background for better contrast (like Default UI)
        BYTE r = GetRValue(base_bg_color);
        BYTE g = GetGValue(base_bg_color);
        BYTE b = GetBValue(base_bg_color);
        
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
        
        // Get contrasting text color
        text_color = colors.get_colour(cui::colours::colour_text);
        
    } catch (...) {
        // Fallback to Default UI colors if CUI API fails
        osd_bg_color = RGB(30, 50, 100);
        osd_border_color = RGB(60, 80, 140);
        text_color = RGB(255, 255, 255);
    }
    
    // Create background brush and border pen using CUI colors (like Default UI)
    HBRUSH bg_brush = CreateSolidBrush(osd_bg_color);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, bg_brush);
    
    // Set pen for border
    HPEN border_pen = CreatePen(PS_SOLID, 1, osd_border_color);
    HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
    
    // Draw filled rectangle with border (exactly like Default UI)
    Rectangle(hdc, osd_rect.left, osd_rect.top, osd_rect.right, osd_rect.bottom);
    
    // Set text properties (exactly like Default UI)
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    
    // Calculate centered text position (exactly like Default UI)
    RECT text_rect = { 
        osd_rect.left + padding, 
        osd_rect.top + padding, 
        osd_rect.right - padding, 
        osd_rect.bottom - padding 
    };
    
    // Draw the text (exactly like Default UI)
    DrawTextA(hdc, m_osd_text.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    
    // Clean up GDI objects (exactly like Default UI)
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_font);
    DeleteObject(bg_brush);
    DeleteObject(border_pen);
    
    // Restore original DC state (exactly like Default UI)
    RestoreDC(hdc, saved_dc);
}

//=============================================================================
// Utility functions
//=============================================================================

void CUIArtworkPanel::resize_artwork_to_fit() {
    if (!m_artwork_loaded || !m_artwork_bitmap) return;
    
    RECT client_rect;
    GetClientRect(m_hWnd, &client_rect);
    
    int client_width = client_rect.right;
    int client_height = client_rect.bottom;
    int img_width = m_artwork_bitmap->GetWidth();
    int img_height = m_artwork_bitmap->GetHeight();
    
    if (client_width <= 0 || client_height <= 0) return;
    
    int new_width, new_height;
    
    if (m_fit_to_window) {
        // Fill window (crop if necessary)
        double scale_x = (double)client_width / img_width;
        double scale_y = (double)client_height / img_height;
        double scale = (scale_x > scale_y) ? scale_x : scale_y;
        
        new_width = (int)(img_width * scale);
        new_height = (int)(img_height * scale);
    } else {
        // Fit to window (letterbox/pillarbox)
        double scale_x = (double)client_width / img_width;
        double scale_y = (double)client_height / img_height;
        double scale = (scale_x < scale_y) ? scale_x : scale_y;
        
        new_width = (int)(img_width * scale);
        new_height = (int)(img_height * scale);
    }
    
    // Ensure minimum size
    if (new_width < 1) new_width = 1;
    if (new_height < 1) new_height = 1;
    
    // Create scaled GDI bitmap (like Default UI)
    try {
        // Clean up old bitmap
        if (m_scaled_gdi_bitmap) {
            DeleteObject(m_scaled_gdi_bitmap);
            m_scaled_gdi_bitmap = NULL;
        }
        
        // Create temporary GDI+ scaled bitmap for conversion
        auto temp_scaled = std::make_unique<Gdiplus::Bitmap>(new_width, new_height);
        if (temp_scaled && temp_scaled->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(temp_scaled.get());
            g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            
            // Create ImageAttributes to fix edge artifacts
            Gdiplus::ImageAttributes img_attr;
            img_attr.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
            
            // Use DrawImage with ImageAttributes to prevent edge artifacts
            Gdiplus::Rect dest_rect(0, 0, new_width, new_height);
            g.DrawImage(m_artwork_bitmap.get(), dest_rect, 0, 0, img_width, img_height, 
                       Gdiplus::UnitPixel, &img_attr);
            
            // Convert to GDI HBITMAP - preserve alpha channel for transparency
            // Don't pass background color to preserve 32-bit ARGB format with alpha channel
            if (temp_scaled->GetHBITMAP(NULL, &m_scaled_gdi_bitmap) != Gdiplus::Ok) {
                m_scaled_gdi_bitmap = NULL;
            }
        }
    } catch (...) {
        if (m_scaled_gdi_bitmap) {
            DeleteObject(m_scaled_gdi_bitmap);
            m_scaled_gdi_bitmap = NULL;
        }
    }
}

void CUIArtworkPanel::initialize_gdiplus() {
    // GDI+ is already initialized by the main component
    // We just need to create our graphics object when needed
}

void CUIArtworkPanel::cleanup_gdiplus() {
    m_graphics.reset();
    m_artwork_bitmap.reset();
    if (m_scaled_gdi_bitmap) {
        DeleteObject(m_scaled_gdi_bitmap);
        m_scaled_gdi_bitmap = NULL;
    }
}

//=============================================================================
// Event-driven artwork update handler (replaces polling)
//=============================================================================

void CUIArtworkPanel::on_artwork_event(const ArtworkEvent& event) {
    if (!m_hWnd) return;
    
    
    // THREAD SAFETY: This event handler can be called from background threads
    // We must NOT directly call UI functions like InvalidateRect or copy_bitmap_from_main_component
    // Instead, post a message to the main thread to handle the update
    
    switch (event.type) {
        case ArtworkEventType::ARTWORK_LOADED:
            if (event.bitmap && event.bitmap != m_last_event_bitmap) {
                
                // Cancel fallback timer since artwork was found
                if (m_hWnd) {
                    KillTimer(m_hWnd, 100);
                }
                
                // Store bitmap handle for comparison (this is thread-safe)
                m_last_event_bitmap = event.bitmap;
                
                // Pass the source string through the message to avoid race conditions
                // Allocate a copy of the source string that the main thread will free
                std::string* source_copy = new std::string(event.source);
                
                
                // Post message to main thread to handle the UI update safely
                // Use WM_USER + 10 for artwork event updates
                // wParam = bitmap, lParam = source string pointer
                PostMessage(m_hWnd, WM_USER + 10, (WPARAM)event.bitmap, (LPARAM)source_copy);
            }
            break;
            
        case ArtworkEventType::ARTWORK_LOADING:
            // Could show a loading indicator here if desired
            break;
            
        case ArtworkEventType::ARTWORK_FAILED:
            {
                
                // THREAD SAFETY: Post message to main thread to handle -noart fallback
                // Cannot access foobar2000 APIs or UI functions from background thread
                if (m_hWnd) {
                    PostMessage(m_hWnd, WM_USER + 11, 0, 0); // WM_USER + 11 for -noart fallback
                }
                
                // Keep previous artwork visible - don't clear on failure
                break;
            }
            
        case ArtworkEventType::ARTWORK_CLEARED:
            m_last_event_bitmap = nullptr;
            // Keep previous artwork visible - don't clear unless explicitly requested
            break;
    }
}

// WIC-based helper for loading custom logos safely in CUI mode
bool CUIArtworkPanel::load_custom_logo_with_wic(HBITMAP logo_bitmap) {
    if (!logo_bitmap) return false;
    
    IWICImagingFactory* imaging_factory = nullptr;
    IWICBitmap* wic_bitmap = nullptr;
    
    try {
        // Create WIC factory
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, 
                                      IID_IWICImagingFactory, (void**)&imaging_factory);
        if (FAILED(hr) || !imaging_factory) return false;
        
        // Convert HBITMAP to WIC bitmap (WIC handles this more safely than GDI+)
        hr = imaging_factory->CreateBitmapFromHBITMAP(logo_bitmap, nullptr, WICBitmapUseAlpha, &wic_bitmap);
        if (FAILED(hr) || !wic_bitmap) {
            if (imaging_factory) imaging_factory->Release();
            return false;
        }
        
        // Get bitmap dimensions for validation
        UINT width, height;
        hr = wic_bitmap->GetSize(&width, &height);
        if (FAILED(hr) || width == 0 || height == 0 || width > 4096 || height > 4096) {
            wic_bitmap->Release();
            imaging_factory->Release();
            return false;
        }
        
        // Create a new compatible HBITMAP from WIC bitmap
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
            HBITMAP safe_bitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pixel_data, NULL, 0);
            
            if (safe_bitmap && pixel_data) {
                // Copy WIC bitmap data to our safe bitmap
                WICRect rect = {0, 0, (INT)width, (INT)height};
                UINT stride = width * 4; // 32-bit BGRA
                hr = wic_bitmap->CopyPixels(&rect, stride, stride * height, (BYTE*)pixel_data);
                
                if (SUCCEEDED(hr)) {
                    // Use standard bitmap conversion (now safe because WIC processed it)
                    bool result = copy_bitmap_from_main_component(safe_bitmap);
                    
                    DeleteObject(safe_bitmap);
                    ReleaseDC(NULL, screen_dc);
                    wic_bitmap->Release();
                    imaging_factory->Release();
                    return result;
                }
                DeleteObject(safe_bitmap);
            }
            ReleaseDC(NULL, screen_dc);
        }
        
        wic_bitmap->Release();
        imaging_factory->Release();
        return false;
        
    } catch (...) {
        // Clean up COM objects on exception
        if (wic_bitmap) wic_bitmap->Release();
        if (imaging_factory) imaging_factory->Release();
        return false;
    }
}

bool CUIArtworkPanel::copy_bitmap_from_main_component(HBITMAP source_bitmap) {
    if (!source_bitmap) return false;
    
    try {
        // CRASH FIX: Safer HBITMAP to GDI+ Bitmap conversion
        // Get bitmap info first to validate format
        BITMAP bmp_info;
        if (!GetObject(source_bitmap, sizeof(BITMAP), &bmp_info)) {
            return false;
        }
        
        // Only proceed if bitmap has valid dimensions and depth
        if (bmp_info.bmWidth > 0 && bmp_info.bmHeight > 0 && 
            bmp_info.bmBitsPixel >= 1 && bmp_info.bmBitsPixel <= 32) {
            
            // Create compatible DC and copy bitmap data safely
            HDC screen_dc = GetDC(NULL);
            if (screen_dc) {
                HDC mem_dc = CreateCompatibleDC(screen_dc);
                if (mem_dc) {
                    HBITMAP old_bmp = (HBITMAP)SelectObject(mem_dc, source_bitmap);
                    
                    // Create 32-bit ARGB bitmap to preserve transparency
                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = bmp_info.bmWidth;
                    bmi.bmiHeader.biHeight = -bmp_info.bmHeight; // Top-down DIB
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32; // Use 32-bit to preserve alpha channel
                    bmi.bmiHeader.biCompression = BI_RGB;
                    
                    void* pixel_data = nullptr;
                    HBITMAP compatible_bitmap = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pixel_data, NULL, 0);
                    if (compatible_bitmap && pixel_data) {
                        HDC compatible_dc = CreateCompatibleDC(screen_dc);
                        if (compatible_dc) {
                            HBITMAP old_compatible_bmp = (HBITMAP)SelectObject(compatible_dc, compatible_bitmap);
                            
                            // Copy original bitmap to compatible format, preserving alpha if present
                            bool copy_success = false;
                            if (bmp_info.bmBitsPixel == 32) {
                                // Use AlphaBlend for 32-bit bitmaps to preserve alpha channel
                                BLENDFUNCTION blend = {};
                                blend.BlendOp = AC_SRC_OVER;
                                blend.BlendFlags = 0;
                                blend.SourceConstantAlpha = 255; // Use per-pixel alpha
                                blend.AlphaFormat = AC_SRC_ALPHA;
                                
                                copy_success = AlphaBlend(compatible_dc, 0, 0, bmp_info.bmWidth, bmp_info.bmHeight,
                                                        mem_dc, 0, 0, bmp_info.bmWidth, bmp_info.bmHeight, blend);
                            } else {
                                // Use BitBlt for non-alpha bitmaps
                                copy_success = BitBlt(compatible_dc, 0, 0, bmp_info.bmWidth, bmp_info.bmHeight, mem_dc, 0, 0, SRCCOPY);
                            }
                            
                            if (copy_success) {
                                
                                // Create GDI+ bitmap using safer method
                                std::unique_ptr<Gdiplus::Bitmap> new_bitmap = std::make_unique<Gdiplus::Bitmap>(compatible_bitmap, nullptr);
                                
                                if (new_bitmap && new_bitmap->GetLastStatus() == Gdiplus::Ok) {
                                    m_artwork_bitmap = std::move(new_bitmap);
                                    m_artwork_loaded = true;
                                    m_current_artwork_source = "Main component";
                                    
                                    // Resize to fit window
                                    resize_artwork_to_fit();
                                    
                                    // Clean up and return success
                                    SelectObject(compatible_dc, old_compatible_bmp);
                                    DeleteDC(compatible_dc);
                                    DeleteObject(compatible_bitmap);
                                    SelectObject(mem_dc, old_bmp);
                                    DeleteDC(mem_dc);
                                    ReleaseDC(NULL, screen_dc);
                                    
                                    return true;
                                }
                            }
                            
                            SelectObject(compatible_dc, old_compatible_bmp);
                            DeleteDC(compatible_dc);
                        }
                        DeleteObject(compatible_bitmap);
                    }
                    
                    SelectObject(mem_dc, old_bmp);
                    DeleteDC(mem_dc);
                }
                ReleaseDC(NULL, screen_dc);
            }
        }
    } catch (...) {
        // Silently handle any GDI+/bitmap conversion errors
    }
    
    return false;
}

void CUIArtworkPanel::search_artwork_for_track(metadb_handle_ptr track) {
    if (!track.is_valid()) {
        g_artwork_loading = false;
        return;
    }
    
    // Only search for internet streams, never for local files
    if (!is_safe_internet_stream(track)) {
        g_artwork_loading = false;
        return;
    }
    
    try {
        
        // Use the external trigger function which will be updated to use priority search
        trigger_main_component_search(track);
        
    } catch (...) {
        g_artwork_loading = false;
    }
}

void CUIArtworkPanel::search_artwork_with_metadata(const std::string& artist, const std::string& title) {
    // WARNING: This function should only be called for internet streams, never for local files
    // Local files should never trigger API searches
    try {
        
        // Use the external trigger function which will be updated to use priority search
        trigger_main_component_search_with_metadata(artist, title);
        
    } catch (...) {
        g_artwork_loading = false;
    }
}

// Station name detection helper function with SomaFM-specific patterns
bool CUIArtworkPanel::is_station_name(const std::string& artist, const std::string& title) {
    // Only check titles when artist is empty (station name pattern)
    if (!artist.empty() || title.empty()) {
        return false;
    }
    
    // Pattern 1: Ends with exclamation mark (like "Indie Pop Rocks!")
    if (title.length() > 0 && title[title.length() - 1] == '!') {
        return true;
    }
    
    // Pattern 2: SomaFM-specific station name patterns
    // These are actual SomaFM station names that should be detected
    const std::vector<std::string> somafm_stations = {
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
    std::string title_upper = title;
    std::transform(title_upper.begin(), title_upper.end(), title_upper.begin(), ::toupper);
    
    for (const std::string& station : somafm_stations) {
        std::string station_upper = station;
        std::transform(station_upper.begin(), station_upper.end(), station_upper.begin(), ::toupper);
        
        if (title_upper == station_upper) {
            return true;
        }
    }
    
    // Pattern 3: General radio/stream keywords for other stations
    const std::vector<std::string> station_keywords = {
        "Radio", "FM", "Stream", "Station", "Music", "Rocks", "Hits", "Channel", "Chill", "Out",
        "Lounge", "Jazz", "Classical", "Electronic", "Dance", "House", "Techno", "Ambient",
        "Zone", "Universe", "Space", "Deep", "Underground", "Alternative", "Indie"
    };
    
    for (const std::string& keyword : station_keywords) {
        std::string keyword_upper = keyword;
        std::transform(keyword_upper.begin(), keyword_upper.end(), keyword_upper.begin(), ::toupper);
        
        if (title_upper.find(keyword_upper) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

// Safe track path extraction with multiple fallback methods
bool CUIArtworkPanel::get_safe_track_path(metadb_handle_ptr track, pfc::string8& path) {
    path = "";
    
    if (!track.is_valid()) return false;
    
    try {
        // Method 1: Try direct path access
        path = track->get_path();
        if (!path.is_empty() && path.length() < 2000) { // Sanity check length
            return true;
        }
    } catch (...) {
        // Method 1 failed, try alternative
    }
    
    try {
        // Method 2: Try titleformat approach
        service_ptr_t<titleformat_object> script;
        static_api_ptr_t<titleformat_compiler> compiler;
        
        compiler->compile_safe(script, "[%path%]");
        if (script.is_valid()) {
            track->format_title(NULL, path, script, NULL);
            if (!path.is_empty() && path.length() < 2000) {
                return true;
            }
        }
    } catch (...) {
        // Method 2 failed
    }
    
    // All methods failed
    path = "";
    return false;
}

// Safe internet stream detection
bool CUIArtworkPanel::is_safe_internet_stream(metadb_handle_ptr track) {
    if (!track.is_valid()) return false;
    
    pfc::string8 path;
    if (!get_safe_track_path(track, path)) {
        return false; // Couldn't get path safely
    }
    
    if (path.is_empty()) return false;
    
    // Check for protocol indicators
    const char* path_cstr = path.c_str();
    const char* protocol_pos = strstr(path_cstr, "://");
    if (!protocol_pos) {
        return false; // No protocol found
    }
    
    // Exclude file:// protocol
    const char* file_pos = strstr(path_cstr, "file://");
    if (file_pos == path_cstr) {
        return false; // This is a file:// URL
    }
    
    return true; // This appears to be an internet stream
}
// HELPER Inverted internet stream detection
bool CUIArtworkPanel::is_inverted_internet_stream(metadb_handle_ptr track, const file_info& p_info) {
    if (!track.is_valid()) {
        return false;
    }

    pfc::string8 path = track->get_path();
    if (path.is_empty()) return false;

    //  Search if parameter "?inverted" or "&inverted" in path
    std::string path_str = path.c_str();

    if ((path_str.find("?inverted") != std::string::npos) || (path_str.find("&inverted") != std::string::npos)) {
        return true;
    }
    
    // Search if field %STREAM_INVERTED% exists and equals 1
    const char* stream_inverted_ptr = p_info.meta_get("STREAM_INVERTED", 0);
    pfc::string8 inverted = stream_inverted_ptr ? stream_inverted_ptr : "";

    if (inverted == "1") {
        return true;
    }

    return false;
}

bool CUIArtworkPanel::is_metadata_valid_for_search(const char* artist, const char* title) {
    // Use the unified metadata validation (same as DUI mode)
    return MetadataCleaner::is_valid_for_search(artist, title);
}

//=============================================================================
// CUI Color Change Client
//=============================================================================

class CUIArtworkColoursClient : public cui::colours::client {
public:
    static const GUID g_guid_colour_client;
    
    const GUID& get_client_guid() const override { return g_guid_colour_client; }
    void get_name(pfc::string_base& p_out) const override { p_out = "foo_artwork CUI panel"; }
    
    uint32_t get_supported_colours() const override { return cui::colours::colour_flag_background; }
    uint32_t get_supported_bools() const override { return cui::colours::bool_flag_dark_mode_enabled; }
    bool get_themes_supported() const override { return false; }
    
    void on_colour_changed(uint32_t mask) const override { 
        CUIArtworkPanel::g_on_colours_change(); 
    }
    void on_bool_changed(uint32_t mask) const override {
        if ((mask & cui::colours::bool_flag_dark_mode_enabled)) {
            CUIArtworkPanel::g_on_colours_change();
        }
    }
};

// GUID for the color client
const GUID CUIArtworkColoursClient::g_guid_colour_client = 
{ 0xC1D2E3F4, 0xA5B6, 0x7890, { 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC } };

// Register the color client
namespace {
cui::colours::client::factory<CUIArtworkColoursClient> g_cui_artwork_colour_client;
}

#endif // COLUMNS_UI_AVAILABLE
