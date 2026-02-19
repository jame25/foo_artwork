// CUI Artwork Panel Implementation - Full Featured
// This file implements complete artwork display functionality for Columns UI

// Prevent socket conflicts before including windows.h
#define _WINSOCKAPI_
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
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
#include "webp_decoder.h"

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
#include "artwork_manager.h"

// Include necessary foobar2000 SDK headers for artwork and playback callbacks
#include "columns_ui/foobar2000/SDK/album_art.h"
#include "columns_ui/foobar2000/SDK/playback_control.h"
#include "columns_ui/foobar2000/SDK/play_callback.h"
#include "columns_ui/foobar2000/SDK/cfg_var.h"
#include "columns_ui/foobar2000/SDK/console.h"

// Include CUI color API
#include "columns_ui/columns_ui-sdk/colours.h"

// Use the Gdiplus namespace
using namespace Gdiplus;

//=============================================================================
// Forward declarations and external interfaces
//=============================================================================

// Forward declare classes from main component
// artwork_manager is now included via artwork_manager.h

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
extern cfg_bool cfg_use_noart_image;

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
    void load_noart_image();  // Load noart image for "use noart image" option
    
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

    const char* bool_cast(const bool b) {
        return b ? "true" : "false";
    }
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
    
    // Download icon hover state
    bool m_mouse_hovering;
    bool m_hover_over_download;
    RECT m_download_icon_rect;

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
    bool is_stream_with_possible_artwork(metadb_handle_ptr track);
    bool is_youtube_stream(metadb_handle_ptr track);
    void cleanup_gdiplus();
    bool copy_bitmap_from_main_component(HBITMAP source_bitmap);
    bool load_custom_logo_with_wic(HBITMAP logo_bitmap);  // WIC-based safe loading for CUI
    void search_artwork_for_track(metadb_handle_ptr track);
    void search_artwork_with_metadata(const std::string& artist, const std::string& title);
    bool is_station_name(const std::string& artist, const std::string& title);  // Station name detection helper
    bool is_metadata_valid_for_search(const char* artist, const char* title);  // Metadata validation
	bool is_inverted_internet_stream(metadb_handle_ptr track, const file_info& p_info); //Inverted stream detection helper																													  

    // Stream dynamic info metadata storage
    void clear_dinfo();
    std::string m_dinfo_artist;
    std::string m_dinfo_title;

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
// Download icon overlay helper
//=============================================================================

static void draw_download_icon(HDC hdc, const RECT& client_rect, bool hovered, RECT& out_icon_rect)
{
    // Only show when foo_artgrab is loaded
    HMODULE hGrab = GetModuleHandle(L"foo_artgrab.dll");
    if (!hGrab) {
        SetRectEmpty(&out_icon_rect);
        return;
    }

    const int icon_size = 24;
    const int padding = 10;
    const int bg_pad = 6;

    // Position at bottom-left
    int ix = client_rect.left + padding;
    int iy = client_rect.bottom - padding - icon_size;

    // Background pill rect
    RECT bg = { ix - bg_pad, iy - bg_pad, ix + icon_size + bg_pad, iy + icon_size + bg_pad };

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Semi-transparent rounded-rect background
    BYTE alpha = hovered ? (BYTE)200 : (BYTE)120;
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(alpha, 0, 0, 0));
    int radius = 6;
    {
        Gdiplus::GraphicsPath path;
        Gdiplus::RectF rf((Gdiplus::REAL)bg.left, (Gdiplus::REAL)bg.top,
                          (Gdiplus::REAL)(bg.right - bg.left), (Gdiplus::REAL)(bg.bottom - bg.top));
        float d = (float)radius * 2.0f;
        path.AddArc(rf.X, rf.Y, d, d, 180, 90);
        path.AddArc(rf.X + rf.Width - d, rf.Y, d, d, 270, 90);
        path.AddArc(rf.X + rf.Width - d, rf.Y + rf.Height - d, d, d, 0, 90);
        path.AddArc(rf.X, rf.Y + rf.Height - d, d, d, 90, 90);
        path.CloseFigure();
        g.FillPath(&bgBrush, &path);
    }

    // Draw the download icon (Material Design download arrow + tray)
    // SVG path translated from 960x960 viewBox to 24x24
    // Original: M480-320 280-520l56-58 104 104v-326h80v326l104-104 56 58-200 200Z
    //           M240-160q-33 0-56.5-23.5T160-240v-120h80v120h480v-120h80v120q0 33-23.5 56.5T720-160H240Z
    float scale = (float)icon_size / 960.0f;
    float ox = (float)ix;
    float oy = (float)iy + (float)icon_size;  // SVG y is negative, so offset from bottom

    Gdiplus::SolidBrush iconBrush(Gdiplus::Color(230, 255, 255, 255));

    // Arrow portion (pointing down)
    {
        Gdiplus::GraphicsPath arrow;
        // Convert SVG coords: SVG uses y-up with range -960..0, we map to 0..24
        // SVG point (sx, sy) -> screen (ox + sx*scale, oy + sy*scale)
        // where sy is negative in SVG space
        Gdiplus::PointF pts[] = {
            { ox + 480*scale, oy + (-320)*scale },  // bottom tip
            { ox + 280*scale, oy + (-520)*scale },  // left of arrow head
            { ox + 336*scale, oy + (-578)*scale },  // left after 56,-58
            { ox + 440*scale, oy + (-474)*scale },  // inner left after 104,104
            { ox + 440*scale, oy + (-800)*scale },  // top left of shaft
            { ox + 520*scale, oy + (-800)*scale },  // top right of shaft
            { ox + 520*scale, oy + (-474)*scale },  // inner right
            { ox + 624*scale, oy + (-578)*scale },  // right after 104,-104
            { ox + 680*scale, oy + (-520)*scale },  // right of arrow head
        };
        arrow.AddPolygon(pts, 9);
        g.FillPath(&iconBrush, &arrow);
    }

    // Tray portion (flat bottom with sides)
    {
        Gdiplus::GraphicsPath tray;
        Gdiplus::PointF pts[] = {
            { ox + 160*scale, oy + (-240)*scale },  // bottom-left outer (after rounding)
            { ox + 160*scale, oy + (-360)*scale },  // top-left
            { ox + 240*scale, oy + (-360)*scale },  // inner top-left
            { ox + 240*scale, oy + (-280)*scale },  // inner bottom-left
            { ox + 720*scale, oy + (-280)*scale },  // inner bottom-right
            { ox + 720*scale, oy + (-360)*scale },  // inner top-right
            { ox + 800*scale, oy + (-360)*scale },  // top-right
            { ox + 800*scale, oy + (-240)*scale },  // bottom-right outer
            { ox + 720*scale, oy + (-160)*scale },  // bottom-right (after curve approx)
            { ox + 240*scale, oy + (-160)*scale },  // bottom-left (after curve approx)
        };
        tray.AddPolygon(pts, 10);
        g.FillPath(&iconBrush, &tray);
    }

    // Output hit-test rect
    out_icon_rect = bg;
}

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
    , m_mouse_hovering(false)
    , m_hover_over_download(false)
    , m_download_icon_rect{}
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
        
        // Draw download icon overlay when hovering (skip for internet streams)
        if (m_mouse_hovering) {
            bool is_stream = false;
            {
                static_api_ptr_t<playback_control> pc;
                metadb_handle_ptr track;
                if (pc->get_now_playing(track) && track.is_valid()) {
                    pfc::string8 path = track->get_path();
                    is_stream = strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://");
                }
            }
            if (!is_stream) {
                draw_download_icon(memDC, client_rect, m_hover_over_download, m_download_icon_rect);
            } else {
                SetRectEmpty(&m_download_icon_rect);
            }
        }

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
            
            // CHECK: Only trigger fallback if we don't already have tagged artwork
            if (m_artwork_loaded && !m_artwork_source.empty() && m_artwork_source == "Local artwork") {
                break;
            }
            
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
            // Delay timer fired - now do the delayed search
            KillTimer(m_hWnd, 101);
            
            // Use stored metadata for delayed search
            if (!m_delayed_search_title.empty()) {
                
                // Apply unified metadata cleaning for consistency with DUI mode
                // Extract only the first artist for better artwork search results
                std::string first_artist = MetadataCleaner::extract_first_artist(m_delayed_search_artist.c_str());
                std::string final_artist = MetadataCleaner::clean_for_search(first_artist.c_str(), true);
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
                if (cfg_use_noart_image) {
                    // Load and display noart image instead of clearing
                    load_noart_image();
                } else {
                    // Just clear the panel
                    force_clear_artwork_bitmap();
                }
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
       
            // PRIORITY CHECK: Don't let API results override tagged artwork, only overide when radio
            static_api_ptr_t<playback_control> pc;
            metadb_handle_ptr current_track;
            
            if (pc->get_now_playing(current_track) && current_track.is_valid()) {
                if (m_artwork_loaded && !m_artwork_source.empty() &&
                    m_artwork_source == "Local artwork" && artwork_source != "Local artwork" && is_stream_with_possible_artwork(current_track)) {
                    break;
                }
            }

            // Now it's safe to call UI functions since we're on the main thread
            if (bitmap && copy_bitmap_from_main_component(bitmap)) {
                // Update the member variable with the correct source
                m_artwork_source = artwork_source;
                
                // IMPORTANT: Kill all fallback timers since we found artwork
                if (artwork_source == "Local artwork") {
                    KillTimer(m_hWnd, 100); // Metadata arrival timer
                    KillTimer(m_hWnd, 101); // Delay timer
                }
                
                // Only show OSD for online sources, not local files
                if (artwork_source != "Local file" && !artwork_source.empty()) {
                    show_osd("Artwork from " + artwork_source);
                } else if (artwork_source == "Local artwork") {
                    show_osd("Tagged artwork");
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
            // CHECK: Don't override existing tagged artwork with fallback images
            if (m_artwork_loaded && !m_artwork_source.empty() && m_artwork_source == "Local artwork") {
                break;
            }
            
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
        
    case WM_MOUSEMOVE:
    {
        if (!m_mouse_hovering) {
            m_mouse_hovering = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(wnd, nullptr, FALSE);
        }
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        bool over = !IsRectEmpty(&m_download_icon_rect) &&
                    PtInRect(&m_download_icon_rect, pt);
        if (over != m_hover_over_download) {
            m_hover_over_download = over;
            InvalidateRect(wnd, &m_download_icon_rect, FALSE);
        }
        if (m_hover_over_download) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
        }
        return 0;
    }

    case WM_MOUSELEAVE:
    {
        RECT old_rect = m_download_icon_rect;
        m_mouse_hovering = false;
        m_hover_over_download = false;
        SetRectEmpty(&m_download_icon_rect);
        if (!IsRectEmpty(&old_rect))
            InvalidateRect(wnd, &old_rect, FALSE);
        else
            InvalidateRect(wnd, nullptr, FALSE);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) == HTCLIENT && m_hover_over_download) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        return DefWindowProc(wnd, msg, wParam, lParam);
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (!IsRectEmpty(&m_download_icon_rect) && PtInRect(&m_download_icon_rect, pt)) {
            typedef void (*pfn_open)(const char*, const char*, const char*);
            HMODULE hGrab = GetModuleHandle(L"foo_artgrab.dll");
            pfn_open pOpen = hGrab ? (pfn_open)GetProcAddress(hGrab, "foo_artgrab_open") : nullptr;
            if (pOpen) {
                metadb_handle_ptr track;
                if (static_api_ptr_t<playback_control>()->get_now_playing(track) && track.is_valid()) {
                    file_info_impl info;
                    track->get_info(info);
                    const char* artist = info.meta_get("artist", 0);
                    const char* album = info.meta_get("album", 0);
                    pfc::string8 path = track->get_path();
                    pOpen(artist ? artist : "", album ? album : "", path.get_ptr());
                }
            }
            return 0;
        }
        break;
    }

    case WM_RBUTTONDOWN:
    {
        POINT pt;
        GetCursorPos(&pt);

        HMENU hMenu = CreatePopupMenu();
        enum { IDM_TOGGLE_OSD = 1 };

        AppendMenuA(hMenu, MF_STRING, IDM_TOGGLE_OSD, m_show_osd ? "Hide OSD" : "Show OSD");

        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, wnd, nullptr);
        DestroyMenu(hMenu);

        if (cmd == IDM_TOGGLE_OSD) {
            m_show_osd = !m_show_osd;
        }
        break;
    }
        
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
    
    // Instead of using the provided data, use album_art_manager_v2 directly
    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr current_track;
    
    if (pc->get_now_playing(current_track) && current_track.is_valid()) {
        try {
            // Use album_art_manager_v2 to get artwork (embedded + external per user preferences)
            static_api_ptr_t<album_art_manager_v2> aam;
            auto extractor = aam->open(pfc::list_single_ref_t<metadb_handle_ptr>(current_track),
                                     pfc::list_single_ref_t<GUID>(album_art_ids::cover_front),
                                     fb2k::noAbort);
            
            auto art_data = extractor->query(album_art_ids::cover_front, fb2k::noAbort);
            if (art_data.is_valid() && art_data->get_size() > 0) {
                load_artwork_from_data(art_data);
                m_artwork_source = "Local artwork";
                InvalidateRect(m_hWnd, NULL, FALSE);
                UpdateWindow(m_hWnd);
                return;
            }
        } catch (...) {
            // SDK artwork search failed, continue with fallback
        }
    }
    
    // Fallback to original behavior if SDK fails
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
    if (is_safe_internet_stream(p_track) && cfg_enable_custom_logos) {
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
        
        // SAFE PATH HANDLING: Use safer helper function for stream detection  
        pfc::string8 file_path;
        bool is_internet_stream = false;
        if (get_safe_track_path(p_track, file_path)) {
            is_internet_stream = (strstr(file_path.c_str(), "://") && 
                                !(strstr(file_path.c_str(), "file://") == file_path.c_str()));
        }
        
        // Check if this stream can have embedded artwork (like YouTube videos)
        bool can_have_embedded_artwork = !is_internet_stream || is_stream_with_possible_artwork(p_track);

        if (can_have_embedded_artwork) {
            // For local files and streams that can have embedded artwork (YouTube videos),
            // try to load tagged artwork first
            // Clear any existing artwork loading state to avoid conflicts
            m_artwork_loaded = false;
            
            // Use the artwork manager directly to try tagged artwork first
            try {
                artwork_manager::get_artwork_async(p_track, [this, p_track](const artwork_manager::artwork_result& result) {
                // Handle the result - load artwork directly instead of using events
                if (result.success && result.data.get_size() > 0) {
                    // Convert pfc::array_t to album_art_data::ptr and load it
                    try {
                        auto art_data = album_art_data_impl::g_create(result.data.get_ptr(), result.data.get_size());
                        if (art_data.is_valid()) {
                            // Update source info and load artwork
                            m_artwork_source = result.source.c_str();
                            load_artwork_from_data(art_data);
                            // Trigger repaint on main thread
                            if (m_hWnd) {
                                InvalidateRect(m_hWnd, NULL, FALSE);
                            }
                        }
                    } catch (...) {
                        // Silently handle conversion errors
                    }
                }
            });
            } catch (...) {
                // Silently handle any exceptions from artwork manager
            }
        } else {
            // For internet radio streams that cannot have embedded artwork,
            // skip tagged artwork search to avoid cached results from previous searches
            // Clear any existing artwork loading state
            m_artwork_loaded = false;
        }
        
        // For internet streams, call the original compatibility function
        if (is_internet_stream) {
            trigger_main_component_local_search(p_track);
        }
        
        // For internet streams, ALSO set up metadata timers for fallback
        if (is_internet_stream) {
            // Kill any existing timers
            KillTimer(m_hWnd, 100); // Metadata arrival timer
            KillTimer(m_hWnd, 101); // Delay timer
            
            // Try to get metadata immediately for online playlists
            file_info_impl info;
            bool has_immediate_metadata = false;
            std::string artist, title;
            
            if (p_track->get_info(info)) {
                if (info.meta_get("ARTIST", 0)) {
                    artist = info.meta_get("ARTIST", 0);
                }
                if (info.meta_get("TITLE", 0)) {
                    title = info.meta_get("TITLE", 0);
                }
                has_immediate_metadata = !artist.empty() && !title.empty();
            }
                
            // If we have valid metadata, search immediately
            if (has_immediate_metadata) {
                // Apply metadata cleaning
                std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
                std::string final_artist = MetadataCleaner::clean_for_search(first_artist.c_str(), true);
                std::string final_title = MetadataCleaner::clean_for_search(title.c_str(), true);
                
                // Trigger API search immediately
                trigger_main_component_search_with_metadata(final_artist, final_title);
            } else {
                // No immediate metadata - use short delay with fallback timer
                m_delayed_search_artist = artist;
                m_delayed_search_title = title;
                SetTimer(m_hWnd, 101, 500, NULL); // Timer ID 101, short delay
            }
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

void CUIArtworkPanel::clear_dinfo() {
    m_dinfo_artist.clear();
    m_dinfo_title.clear();
}


void CUIArtworkPanel::on_playback_stop(play_control::t_stop_reason p_reason) {
    if (!m_hWnd) return;
    
    // Clear artwork if option is enabled
    if (cfg_clear_panel_when_not_playing) {
        if (cfg_use_noart_image) {
            // Load and display noart image instead of clearing
            load_noart_image();
        } else {
            // Just clear the panel
            force_clear_artwork_bitmap();
        }
        m_osd_text = "";
        clear_dinfo();
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
    
    // Extract only the first artist for better artwork search results
    std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
    
    // Use the unified metadata cleaner with Cyrillic preservation
    std::string cleaned_artist = MetadataCleaner::clean_for_search(first_artist.c_str(), true);
    std::string cleaned_title = MetadataCleaner::clean_for_search(title.c_str(), true);
    
   

    // Apply the unified cleaning to both artist and title  
    // This replaces all the complex lambda functions with UTF-8 safe processing
    artist = cleaned_artist;
    title = cleaned_title;
    
    //If no artist and title is "artist - title" (eg https://stream.radioclub80.cl:8022/retro80.opus)  split 
    if (artist.empty()) {
        std::string delimiter = " - ";
        size_t pos = title.find(delimiter);
        if (pos != std::string::npos) {
            std::string lvalue = title.substr(0, pos);
            std::string rvalue = title.substr(pos + delimiter.length());
            artist = lvalue;
            title = rvalue;
        }

        //or "artist ˗ title" (eg https ://energybasel.ice.infomaniak.ch/energybasel-high.mp3)

        std::string delimiter2 = " ˗ ";
        size_t pos2 = title.find(delimiter2);
        if (pos2 != std::string::npos) {
            std::string lvalue = title.substr(0, pos2);
            std::string rvalue = title.substr(pos2 + delimiter2.length());
            artist = lvalue;
            title = rvalue;
        }

        //or "artist / title" (eg https://radiostream.pl/tuba8-1.mp3?cache=1650763965 )

        std::string delimiter3 = " / ";
        size_t pos3 = title.find(delimiter3);
        if (pos3 != std::string::npos) {
            std::string lvalue = title.substr(0, pos3);
            std::string rvalue = title.substr(pos3 + delimiter3.length());
            artist = lvalue;
            title = rvalue;
        }

    
        else {
            //do nothing
        }
    }

    //WalmRadio
    //If no title and artist is "title by artist" (eg https://icecast.walmradio.com:8443/classic)  split 
    if (title.empty()) {
        std::string delimiter = " by ";
        size_t pos = artist.find(delimiter);
        if (pos != std::string::npos) {
            std::string lvalue = artist.substr(0, pos);
            std::string rvalue = artist.substr(pos + delimiter.length());
            artist = rvalue;
            title = lvalue;
        }
    }

    //Don't search if received same artist title
    //same info - don't search - stop
    if (m_dinfo_artist == cleaned_artist && m_dinfo_title == cleaned_title) {
        return;
    }
    else {
        //differnet info - save new info and continue
        m_dinfo_artist = cleaned_artist;
        m_dinfo_title = cleaned_title;
    }

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
                // If inverted swap artist title
                bool is_inverted_stream = is_inverted_internet_stream(current_track, p_info);
                if (is_inverted_stream) {
                    std::string artist_old = artist;
                    std::string title_old = title;
                    artist = title_old;
                    title = artist_old;
                }
                
                // Use minimal delay for internet streams
                KillTimer(m_hWnd, 101);
                // Store metadata for delayed search with minimal delay
                m_delayed_search_artist = artist;
                m_delayed_search_title = title;
                // Set Timer 101 to fire after minimal delay
                SetTimer(m_hWnd, 101, 500, NULL);
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

        // Try WIC-based WebP decoding first
        if (is_webp_signature((const unsigned char*)artwork_data, artwork_size)) {
            Gdiplus::Bitmap* webp_bitmap = decode_webp_via_wic((const unsigned char*)artwork_data, artwork_size);
            if (webp_bitmap) {
                m_artwork_bitmap = std::unique_ptr<Gdiplus::Bitmap>(webp_bitmap);
                m_artwork_loaded = true;
                return;
            }
            return; // WebP detected but decoding failed
        }

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

// Load noart image for "use noart image" option
void CUIArtworkPanel::load_noart_image() {
    // Clear existing artwork
    if (m_artwork_bitmap) {
        m_artwork_bitmap.reset();
    }
    
    if (m_scaled_gdi_bitmap) {
        DeleteObject(m_scaled_gdi_bitmap);
        m_scaled_gdi_bitmap = NULL;
    }
    
    // Clear artwork state
    m_artwork_loaded = false;
    m_current_artwork_path.clear();
    m_current_artwork_source.clear();
    
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
    
    for (int i = 0; noart_filenames[i] != nullptr; i++) {
        noart_file_path = data_path;
        noart_file_path.add_string(noart_filenames[i]);
        
        // Check if file exists
        DWORD file_attrs = GetFileAttributesA(noart_file_path);
        if (file_attrs != INVALID_FILE_ATTRIBUTES && !(file_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            // File exists, try to load it using GDI+
            try {
                // Convert to wide string for GDI+
                std::wstring wide_path;
                wide_path.resize(noart_file_path.length() + 1);
                int result = MultiByteToWideChar(CP_UTF8, 0, noart_file_path.get_ptr(), -1, &wide_path[0], (int)wide_path.size());
                if (result > 0) {
                    wide_path.resize(result - 1); // Remove null terminator from size
                    
                    // Load image using GDI+
                    auto bitmap = std::make_unique<Gdiplus::Bitmap>(wide_path.c_str());
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        m_artwork_bitmap = std::move(bitmap);
                        m_artwork_loaded = true;
                        m_current_artwork_path = wide_path;
                        m_current_artwork_source = "Noart image";
                        
                        // Scale the artwork to fit the panel (same as regular artwork)
                        resize_artwork_to_fit();
                        
                        break;
                    }
                }
            } catch (...) {
                continue;
            }
        }
    }
    
    // Invalidate window to redraw with noart image or clear panel
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
        return false; // This is a file:// URL
    }
    
    return true; // This appears to be an internet stream
}

bool CUIArtworkPanel::is_stream_with_possible_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return false;
    
    try {
        pfc::string8 path;
        if (!get_safe_track_path(track, path) || path.is_empty()) {
            return false;
        }
        
        // Check if this is a stream that can have embedded artwork
        // YouTube videos and similar services can have embedded thumbnails
        const char* path_str = path.c_str();
        
        // YouTube videos (can have embedded thumbnails)
        if (strstr(path_str, "youtube.com") || strstr(path_str, "youtu.be") || 
            strstr(path_str, "ytimg.com") || strstr(path_str, "googlevideo.com") ||
            strstr(path_str, "rutube.ru")) {
            return true;
        }
        
        // Other video platforms that might have embedded artwork
        if (strstr(path_str, "vimeo.com") || strstr(path_str, "dailymotion.com") || 
            strstr(path_str, "twitch.tv") || strstr(path_str, "facebook.com")) {
            return true;
        }
        
        // Streaming services that provide artwork in their streams
        if (strstr(path_str, "spotify.com") || strstr(path_str, "deezer.com") || 
            strstr(path_str, "soundcloud.com") || strstr(path_str, "bandcamp.com")) {
            return true;
        }
        
        // Check for file extensions that suggest video/audio files with possible artwork
        if (strstr(path_str, ".mp4") || strstr(path_str, ".m4v") || strstr(path_str, ".mkv") ||
            strstr(path_str, ".webm") || strstr(path_str, ".mov") || strstr(path_str, ".avi")) {
            return true;
        }
        
        // Audio formats that commonly have embedded artwork
        // For .mp3 and .aac, only consider them to have artwork if they're from known platforms or likely downloadable files
        if (strstr(path_str, ".m4a")) {
            return true;
        }

        // For .aac, be more selective - exclude internet radio streams
        if (strstr(path_str, ".aac")) {
            // Allow .aac from known platforms that provide artwork
            if (strstr(path_str, "youtube.com") || strstr(path_str, "youtu.be") ||
                strstr(path_str, "googlevideo.com") || strstr(path_str, "soundcloud.com") ||
                strstr(path_str, "bandcamp.com") || strstr(path_str, "spotify.com")) {
                return true;
            }
            // Exclude streaming radio URLs (they don't have embedded artwork)
            return false;
        }

        // For .mp3, be more selective - exclude internet radio streams
        if (strstr(path_str, ".mp3")) {
            // Allow .mp3 from known platforms that provide artwork
            if (strstr(path_str, "youtube.com") || strstr(path_str, "youtu.be") ||
                strstr(path_str, "googlevideo.com") || strstr(path_str, "soundcloud.com") ||
                strstr(path_str, "bandcamp.com") || strstr(path_str, "spotify.com")) {
                return true;
            }
            // Exclude streaming radio URLs (they don't have embedded artwork)
            return false;
        }
        
        // For all other streams (like pure internet radio), assume no embedded artwork
        return false;
        
    } catch (...) {
        // On error, assume no embedded artwork to be safe
        return false;
    }
}

bool CUIArtworkPanel::is_youtube_stream(metadb_handle_ptr track) {
    if (!track.is_valid()) return false;
    
    try {
        pfc::string8 path;
        if (!get_safe_track_path(track, path) || path.is_empty()) {
            return false;
        }
        
        const char* path_str = path.c_str();
        
        // YouTube videos
        if (strstr(path_str, "youtube.com") || strstr(path_str, "youtu.be") || 
            strstr(path_str, "ytimg.com") || strstr(path_str, "googlevideo.com")) {
            return true;
        }
        
        return false;
        
    } catch (...) {
        return false;
    }
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
