// CUI Artwork Panel Implementation - Full Featured
// This file implements complete artwork display functionality for Columns UI

// Prevent socket conflicts before including windows.h
#define _WINSOCKAPI_
#define NOMINMAX

// Need windows.h for OutputDebugStringA
#include <windows.h>

// Define CUI support for this file since we're not using precompiled headers
#define COLUMNS_UI_AVAILABLE

// Debug: Always output this regardless of defines
static class cui_file_debug {
public:
    cui_file_debug() {
        OutputDebugStringA("ARTWORK: CUI artwork_panel_cui.cpp is being compiled and linked\n");
    }
} g_cui_file_debug;

// Only compile CUI support if CUI SDK is available
#ifdef COLUMNS_UI_AVAILABLE

// Debug: Check if we get inside the ifdef
static class cui_ifdef_debug {
public:
    cui_ifdef_debug() {
        OutputDebugStringA("ARTWORK: Inside COLUMNS_UI_AVAILABLE ifdef in CUI file\n");
    }
} g_cui_ifdef_debug;

// Standard Windows headers
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <algorithm>
#include <thread>
#include <vector>
#include <mutex>
#include <memory>
#include <cmath>

// Include CUI SDK headers directly - these contain their own foobar2000 SDK
#include "columns_ui/columns_ui-sdk/ui_extension.h"
#include "columns_ui/columns_ui-sdk/window.h"

// Also include base UI extension headers
#include "columns_ui/columns_ui-sdk/base.h"

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

// Access to main component's artwork bitmap
extern HBITMAP get_main_component_artwork_bitmap();

// Shared bitmap from standalone search
extern HBITMAP g_shared_artwork_bitmap;

// External functions for triggering main component search
extern void trigger_main_component_search(metadb_handle_ptr track);
extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);

//=============================================================================
// CUI Artwork Panel Class Definition - Full Implementation
//=============================================================================

class CUIArtworkPanel : public uie::window
                      , public now_playing_album_art_notify
                      , public play_callback
{
public:
    CUIArtworkPanel();
    ~CUIArtworkPanel();
    
    // Required uie::window interface
    const GUID& get_extension_guid() const override;
    void get_name(pfc::string_base& out) const override;
    void get_category(pfc::string_base& out) const override;
    unsigned get_type() const override;
    
    // Window management
    bool is_available(const uie::window_host_ptr& p_host) const override;
    HWND create_or_transfer_window(HWND wnd_parent, const uie::window_host_ptr& p_host, const ui_helpers::window_position_t& p_position) override;
    void destroy_window() override;
    HWND get_wnd() const override;
    
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

private:
    // Window state
    HWND m_hWnd;
    uie::window_host_ptr m_host;
    
    // GDI+ objects for artwork rendering
    std::unique_ptr<Gdiplus::Graphics> m_graphics;
    std::unique_ptr<Gdiplus::Bitmap> m_artwork_bitmap;
    std::unique_ptr<Gdiplus::Bitmap> m_scaled_bitmap;
    
    // Artwork state
    std::wstring m_current_artwork_path;
    std::string m_current_artwork_source;
    bool m_artwork_loaded;
    bool m_fit_to_window;
    
    // OSD (On-Screen Display) system
    bool m_show_osd;
    std::string m_osd_text;
    std::string m_artwork_source; // Track the source of current artwork
    DWORD m_osd_start_time;
    int m_osd_slide_offset;
    UINT_PTR m_osd_timer_id;
    bool m_osd_visible;
    
    // Artwork polling system
    UINT_PTR m_artwork_check_timer_id;
    HBITMAP m_last_checked_bitmap;
    
    // Window procedure and message handling
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT on_message(UINT msg, WPARAM wParam, LPARAM lParam);
    
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
    void cleanup_gdiplus();
    void check_for_new_artwork();
    bool copy_bitmap_from_main_component(HBITMAP source_bitmap);
    void search_artwork_for_track(metadb_handle_ptr track);
    void search_artwork_with_metadata(const std::string& artist, const std::string& title);
    
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

// Registration verification function
static class cui_registration_helper {
public:
    cui_registration_helper() {
        OutputDebugStringA("ARTWORK: CUI Full Artwork Panel factory registered during static initialization\n");
        console::print("CUI Full Artwork Panel: Factory registered during static initialization");
    }
} g_cui_registration_helper;

//=============================================================================
// Constructor and Destructor
//=============================================================================

CUIArtworkPanel::CUIArtworkPanel() 
    : m_hWnd(NULL)
    , m_artwork_loaded(false)
    , m_fit_to_window(false)
    , m_show_osd(true)
    , m_osd_start_time(0)
    , m_osd_slide_offset(OSD_SLIDE_DISTANCE)
    , m_osd_timer_id(0)
    , m_osd_visible(false)
    , m_artwork_check_timer_id(0)
    , m_last_checked_bitmap(nullptr)
{
    OutputDebugStringA("ARTWORK: CUI Full Artwork Panel Constructor called\n");
    console::print("CUI Full Artwork Panel: Constructor called");
}

CUIArtworkPanel::~CUIArtworkPanel() {
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

HWND CUIArtworkPanel::create_or_transfer_window(HWND wnd_parent, const uie::window_host_ptr& p_host, const ui_helpers::window_position_t& p_position) {
    if (m_hWnd) {
        // Transfer existing window
        ShowWindow(m_hWnd, SW_HIDE);
        SetParent(m_hWnd, wnd_parent);
        m_host->relinquish_ownership(m_hWnd);
        m_host = p_host;
        SetWindowPos(m_hWnd, nullptr, p_position.x, p_position.y, p_position.cx, p_position.cy, SWP_NOZORDER);
    } else {
        // Create new window
        m_host = p_host;
        
        WNDCLASS wc = {};
        wc.lpfnWndProc = window_proc;
        wc.hInstance = core_api::get_my_instance();
        wc.lpszClassName = L"foo_artwork_cui_panel_full";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        
        RegisterClass(&wc);
        
        m_hWnd = CreateWindow(
            L"foo_artwork_cui_panel_full",
            L"Artwork Display",
            WS_CHILD,
            p_position.x, p_position.y, p_position.cx, p_position.cy,
            wnd_parent,
            NULL,
            core_api::get_my_instance(),
            this
        );
    }
    
    return m_hWnd;
}

void CUIArtworkPanel::destroy_window() {
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = NULL;
    }
    m_host.release();
}

HWND CUIArtworkPanel::get_wnd() const {
    return m_hWnd;
}

//=============================================================================
// Window procedure and message handling
//=============================================================================

LRESULT CALLBACK CUIArtworkPanel::window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CUIArtworkPanel* p_this = nullptr;
    
    if (msg == WM_NCCREATE) {
        LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
        p_this = static_cast<CUIArtworkPanel*>(lpcs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(p_this));
        p_this->m_hWnd = hwnd;
    } else {
        p_this = reinterpret_cast<CUIArtworkPanel*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (p_this) {
        return p_this->on_message(msg, wParam, lParam);
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CUIArtworkPanel::on_message(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        OutputDebugStringA("ARTWORK: CUI Full Panel WM_CREATE\n");
        // Initialize GDI+
        initialize_gdiplus();
        
        // Register for foobar2000 callbacks
        try {
            now_playing_album_art_notify_manager::get()->add(this);
            OutputDebugStringA("ARTWORK: CUI Panel registered for album art notifications\n");
            
            play_callback_manager::get()->register_callback(this, 
                play_callback::flag_on_playback_new_track | 
                play_callback::flag_on_playback_stop | 
                play_callback::flag_on_playback_dynamic_info_track, false);
            OutputDebugStringA("ARTWORK: CUI Panel registered for playback callbacks\n");
            
            // Request artwork for current track if playing
            auto pc = playback_control::get();
            if (pc->is_playing()) {
                metadb_handle_ptr track;
                if (pc->get_now_playing(track)) {
                    OutputDebugStringA("ARTWORK: CUI Panel requesting artwork for current track\n");
                    on_playback_new_track(track);
                }
            }
        } catch (const std::exception& e) {
            OutputDebugStringA("ARTWORK: CUI Panel callback registration failed\n");
        }
        
        // Start artwork polling timer (check every 500ms)
        m_artwork_check_timer_id = SetTimer(m_hWnd, 2, 500, nullptr);
        break;
        
    case WM_DESTROY:
        OutputDebugStringA("ARTWORK: CUI Full Panel WM_DESTROY\n");
        // Stop timers
        if (m_osd_timer_id) {
            KillTimer(m_hWnd, m_osd_timer_id);
            m_osd_timer_id = 0;
        }
        if (m_artwork_check_timer_id) {
            KillTimer(m_hWnd, m_artwork_check_timer_id);
            m_artwork_check_timer_id = 0;
        }
        
        // Unregister callbacks
        now_playing_album_art_notify_manager::get()->remove(this);
        play_callback_manager::get()->unregister_callback(this);
        
        // Cleanup GDI+
        cleanup_gdiplus();
        break;
        
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hWnd, &ps);
        
        // Paint artwork
        paint_artwork(hdc);
        
        // Paint OSD if visible
        if (m_osd_visible) {
            paint_osd(hdc);
        }
        
        EndPaint(m_hWnd, &ps);
        return 0;
    }
    
    case WM_SIZE:
        // Resize artwork to fit new window size
        resize_artwork_to_fit();
        InvalidateRect(m_hWnd, NULL, FALSE);  // FALSE prevents background erase
        break;
        
    case WM_ERASEBKGND:
        // We handle background in WM_PAINT to prevent flickering
        return 1;
        
    case WM_TIMER:
        if (wParam == m_osd_timer_id) {
            update_osd_animation();
        } else if (wParam == m_artwork_check_timer_id) {
            // Check for new artwork from main component
            check_for_new_artwork();
        }
        break;
        
    case WM_RBUTTONDOWN:
        // Show/hide OSD on right click
        m_show_osd = !m_show_osd;
        break;
    }
    
    return DefWindowProc(m_hWnd, msg, wParam, lParam);
}

//=============================================================================
// foobar2000 callback implementations
//=============================================================================

void CUIArtworkPanel::on_album_art(album_art_data::ptr data) noexcept {
    if (!m_hWnd) return;
    
    OutputDebugStringA("ARTWORK: CUI Panel - on_album_art callback received\n");
    
    try {
        if (data.is_valid() && data->get_size() > 0) {
            OutputDebugStringA("ARTWORK: CUI Panel - Valid artwork data received\n");
            load_artwork_from_data(data);
            m_artwork_source = "Album data";
        } else {
            OutputDebugStringA("ARTWORK: CUI Panel - No artwork data received\n");
            // Keep previous artwork visible - don't clear
            m_artwork_source = "";
        }
        
        InvalidateRect(m_hWnd, NULL, FALSE);
    } catch (...) {
        // Handle any exceptions gracefully
        OutputDebugStringA("ARTWORK: CUI Panel - Exception in on_album_art\n");
        // Keep previous artwork visible - don't clear
        m_artwork_source = "";
        InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CUIArtworkPanel::on_playback_new_track(metadb_handle_ptr p_track) {
    if (!m_hWnd) return;
    
    // Keep previous artwork visible - don't clear
    m_artwork_source = "";
    // Don't invalidate here - let new artwork trigger the redraw
    
    if (p_track.is_valid()) {
        OutputDebugStringA("ARTWORK: CUI Panel - New track, checking main component artwork\n");
        
        // Check if main component's artwork manager has artwork for this track
        HBITMAP main_bitmap = get_main_component_artwork_bitmap();
        
        if (main_bitmap) {
            OutputDebugStringA("ARTWORK: CUI Panel - Found artwork bitmap from main component\n");
            if (copy_bitmap_from_main_component(main_bitmap)) {
                m_last_checked_bitmap = main_bitmap;
                m_artwork_source = "Local file";
                show_osd("Artwork from " + m_artwork_source);
                InvalidateRect(m_hWnd, NULL, FALSE);
                return;
            }
        }
        
        // If main component is loading artwork, wait for it
        if (g_artwork_loading) {
            OutputDebugStringA("ARTWORK: CUI Panel - Main component is loading artwork, waiting...\n");
            return;
        }
        
        // If no Default UI element is active, wait for dynamic metadata
        OutputDebugStringA("ARTWORK: CUI Panel - Waiting for dynamic metadata updates\n");
    }
}

void CUIArtworkPanel::on_playback_stop(play_control::t_stop_reason p_reason) {
    if (!m_hWnd) return;
    
    // Keep artwork visible when playback stops
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
    
    char debug_msg[512];
    sprintf_s(debug_msg, "ARTWORK: CUI Panel - Dynamic info received: artist='%s', title='%s'\n", 
             artist.c_str(), title.c_str());
    OutputDebugStringA(debug_msg);
    
    // Check for "adbreak" in title (radio advertisement breaks - no search needed)
    if (!title.empty() && strstr(title.c_str(), "adbreak")) {
        OutputDebugStringA("ARTWORK: CUI Panel - Detected 'adbreak' in title, skipping artwork search\n");
        return;
    }
    
    // If we have valid metadata and aren't already loading, trigger main component search
    if (!title.empty() && !g_artwork_loading) {
        OutputDebugStringA("ARTWORK: CUI Panel - Starting search with dynamic metadata\n");
        
        // Get current track and call main component search directly
        auto pc = playback_control::get();
        metadb_handle_ptr current_track;
        if (pc->get_now_playing(current_track) && current_track.is_valid()) {
            // Call the main component's search function with the metadata
            extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);
            trigger_main_component_search_with_metadata(artist, title);
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
            m_current_artwork_source = "Album data";
            
            // Resize to fit window
            resize_artwork_to_fit();
            
            OutputDebugStringA("ARTWORK: CUI Panel - Successfully loaded artwork from album data\n");
        } else {
            // Keep previous artwork visible - don't clear on load failure
            OutputDebugStringA("ARTWORK: CUI Panel - Failed to load artwork from album data\n");
        }
    } catch (...) {
        // Keep previous artwork visible - don't clear on exception
        OutputDebugStringA("ARTWORK: CUI Panel - Exception loading artwork from album data\n");
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
            
            OutputDebugStringA("ARTWORK: CUI Panel - Successfully loaded artwork from file\n");
        } else {
            // Keep previous artwork visible - don't clear on load failure
            OutputDebugStringA("ARTWORK: CUI Panel - Failed to load artwork from file\n");
        }
    } catch (...) {
        // Keep previous artwork visible - don't clear on exception
        OutputDebugStringA("ARTWORK: CUI Panel - Exception loading artwork from file\n");
    }
}

void CUIArtworkPanel::clear_artwork() {
    m_artwork_bitmap.reset();
    m_scaled_bitmap.reset();
    m_artwork_loaded = false;
    m_current_artwork_path.clear();
    m_current_artwork_source.clear();
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
    
    // Check if we have artwork from main component
    HBITMAP main_bitmap = get_main_component_artwork_bitmap();
    if (!main_bitmap && g_shared_artwork_bitmap) {
        main_bitmap = g_shared_artwork_bitmap;
    }
    
    if (main_bitmap) {
        // Use GDI for artwork drawing (like Default UI) to prevent OSD flickering
        BITMAP bm;
        GetObject(main_bitmap, sizeof(bm), &bm);
        
        // Calculate window and image dimensions
        int windowWidth = client_rect.right - client_rect.left;
        int windowHeight = client_rect.bottom - client_rect.top;
        
        // Avoid division by zero
        if (windowWidth <= 0 || windowHeight <= 0 || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
            return;
        }
        
        // Calculate scaling factors for fit mode (like Default UI)
        double scaleX = (double)windowWidth / bm.bmWidth;
        double scaleY = (double)windowHeight / bm.bmHeight;
        double scale = (scaleX < scaleY) ? scaleX : scaleY;  // Fit mode
        
        // Apply reasonable scale limits
        if (scale > 50.0) scale = 50.0;
        if (scale < 0.01) scale = 0.01;
        
        // Calculate final dimensions
        int scaledWidth = (int)(bm.bmWidth * scale + 0.5);
        int scaledHeight = (int)(bm.bmHeight * scale + 0.5);
        
        // Ensure minimum size
        if (scaledWidth < 1) scaledWidth = 1;
        if (scaledHeight < 1) scaledHeight = 1;
        
        // Center the image
        int x = (windowWidth - scaledWidth) / 2;
        int y = (windowHeight - scaledHeight) / 2;
        
        // Create compatible DC and draw using GDI (like Default UI)
        HDC memDC = CreateCompatibleDC(hdc);
        if (memDC) {
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, main_bitmap);
            
            // Set stretch mode for better quality
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, NULL);
            
            // Draw the scaled bitmap using GDI
            StretchBlt(hdc, x, y, scaledWidth, scaledHeight, memDC, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            
            // Clean up
            SelectObject(memDC, oldBitmap);
            DeleteDC(memDC);
        }
    } else if (m_artwork_loaded && m_scaled_bitmap) {
        // Fallback: Use GDI+ only if no main component artwork available
        if (!m_graphics) {
            m_graphics = std::make_unique<Gdiplus::Graphics>(hdc);
            m_graphics->SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            m_graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        } else {
            m_graphics.reset(new Gdiplus::Graphics(hdc));
            m_graphics->SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            m_graphics->SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        }
        
        // Draw scaled artwork
        int img_width = m_scaled_bitmap->GetWidth();
        int img_height = m_scaled_bitmap->GetHeight();
        int x = (client_rect.right - img_width) / 2;
        int y = (client_rect.bottom - img_height) / 2;
        
        m_graphics->DrawImage(m_scaled_bitmap.get(), x, y, img_width, img_height);
    } else {
        // Draw "no artwork" message using GDI (like Default UI)
        SetBkMode(hdc, TRANSPARENT);
        
        // Get text color from CUI color scheme
        COLORREF text_color = colors.get_colour(cui::colours::colour_text);
        SetTextColor(hdc, text_color);
        
        const char* text = "No artwork available";
        DrawTextA(hdc, text, -1, &client_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void CUIArtworkPanel::paint_no_artwork(HDC hdc) {
    RECT client_rect;
    GetClientRect(m_hWnd, &client_rect);
    
    // Draw text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(100, 100, 100));
    
    const char* text = "No artwork available";
    DrawTextA(hdc, text, -1, &client_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void CUIArtworkPanel::paint_loading(HDC hdc) {
    RECT client_rect;
    GetClientRect(m_hWnd, &client_rect);
    
    // Draw loading text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(100, 100, 100));
    
    const char* text = "Loading artwork...";
    DrawTextA(hdc, text, -1, &client_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

//=============================================================================
// OSD functions
//=============================================================================

void CUIArtworkPanel::show_osd(const std::string& text) {
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
        
        // Create combined invalidation rectangle covering both positions (Default UI style)
        int min_x = (old_osd_x < new_osd_x ? old_osd_x : new_osd_x) - 5;  // Extra padding for smooth animation
        int max_x = ((old_osd_x + osd_width) > (new_osd_x + osd_width) ? (old_osd_x + osd_width) : (new_osd_x + osd_width)) + 5;
        
        RECT invalidate_rect = { 
            min_x > 0 ? min_x : 0, 
            (osd_y - 5) > 0 ? (osd_y - 5) : 0, 
            max_x < client_rect.right ? max_x : client_rect.right, 
            (osd_y + osd_height + 5) < client_rect.bottom ? (osd_y + osd_height + 5) : client_rect.bottom 
        };
        
        // Only invalidate the specific region containing the OSD (Default UI style)
        InvalidateRect(m_hWnd, &invalidate_rect, TRUE);
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
    
    // Create scaled bitmap
    try {
        m_scaled_bitmap = std::make_unique<Gdiplus::Bitmap>(new_width, new_height);
        if (m_scaled_bitmap && m_scaled_bitmap->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(m_scaled_bitmap.get());
            g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(m_artwork_bitmap.get(), 0, 0, new_width, new_height);
        }
    } catch (...) {
        m_scaled_bitmap.reset();
    }
}

void CUIArtworkPanel::initialize_gdiplus() {
    // GDI+ is already initialized by the main component
    // We just need to create our graphics object when needed
}

void CUIArtworkPanel::cleanup_gdiplus() {
    m_graphics.reset();
    m_artwork_bitmap.reset();
    m_scaled_bitmap.reset();
}

void CUIArtworkPanel::check_for_new_artwork() {
    if (!m_hWnd) return;
    
    // Check if main component has new artwork bitmap
    HBITMAP main_bitmap = get_main_component_artwork_bitmap();
    
    // Also check shared standalone bitmap
    HBITMAP shared_bitmap = g_shared_artwork_bitmap;
    
    // Use whichever bitmap is available (prefer main component)
    HBITMAP active_bitmap = main_bitmap ? main_bitmap : shared_bitmap;
    
    // Debug: Always log the current state  
    char debug_msg[256];
    sprintf_s(debug_msg, "ARTWORK: CUI Panel - Polling check: main=%p, shared=%p, active=%p, loading=%d\n", 
             main_bitmap, shared_bitmap, active_bitmap, g_artwork_loading ? 1 : 0);
    OutputDebugStringA(debug_msg);
    
    if (active_bitmap && active_bitmap != m_last_checked_bitmap) {
        if (main_bitmap) {
            OutputDebugStringA("ARTWORK: CUI Panel - New artwork bitmap detected from main component\n");
            m_artwork_source = "Local file";
        } else {
            OutputDebugStringA("ARTWORK: CUI Panel - New artwork bitmap detected from standalone search\n");
            // Get the actual source from the global variable set by bridge functions
            extern pfc::string8 g_current_artwork_source;
            if (!g_current_artwork_source.is_empty()) {
                m_artwork_source = g_current_artwork_source.c_str();
                char debug_msg[256];
                sprintf_s(debug_msg, "ARTWORK: CUI Panel - Source detected: %s\n", m_artwork_source.c_str());
                OutputDebugStringA(debug_msg);
            } else {
                m_artwork_source = "Online search"; // Fallback if source not set
                OutputDebugStringA("ARTWORK: CUI Panel - No source info available, using fallback\n");
            }
        }
        
        // Store bitmap handle for comparison
        m_last_checked_bitmap = active_bitmap;
        
        // Copy the bitmap from main component or standalone search
        if (copy_bitmap_from_main_component(active_bitmap)) {
            show_osd("Artwork from " + m_artwork_source);
            InvalidateRect(m_hWnd, NULL, FALSE);
        }
    } else if (!active_bitmap && m_last_checked_bitmap) {
        // Main component cleared artwork - but keep previous artwork visible
        OutputDebugStringA("ARTWORK: CUI Panel - Main component cleared artwork (keeping previous visible)\n");
        m_last_checked_bitmap = nullptr;
        // Don't clear artwork or invalidate - keep previous artwork visible
    }
}

bool CUIArtworkPanel::copy_bitmap_from_main_component(HBITMAP source_bitmap) {
    if (!source_bitmap) return false;
    
    try {
        // Get bitmap info
        BITMAP bmp_info;
        if (!GetObject(source_bitmap, sizeof(BITMAP), &bmp_info)) {
            return false;
        }
        
        // Create GDI+ bitmap from HBITMAP
        auto new_bitmap = std::make_unique<Gdiplus::Bitmap>(source_bitmap, nullptr);
        
        if (new_bitmap && new_bitmap->GetLastStatus() == Gdiplus::Ok) {
            m_artwork_bitmap = std::move(new_bitmap);
            m_artwork_loaded = true;
            m_current_artwork_source = "Main component";
            
            // Resize to fit window
            resize_artwork_to_fit();
            
            OutputDebugStringA("ARTWORK: CUI Panel - Successfully copied artwork from main component\n");
            return true;
        }
    } catch (...) {
        OutputDebugStringA("ARTWORK: CUI Panel - Exception copying bitmap from main component\n");
    }
    
    return false;
}

void CUIArtworkPanel::search_artwork_for_track(metadb_handle_ptr track) {
    if (!track.is_valid()) {
        g_artwork_loading = false;
        return;
    }
    
    try {
        OutputDebugStringA("ARTWORK: CUI Panel - Delegating search to main component's priority system\n");
        
        // Use the external trigger function which will be updated to use priority search
        trigger_main_component_search(track);
        
    } catch (...) {
        OutputDebugStringA("ARTWORK: CUI Panel - Exception delegating to main component\n");
        g_artwork_loading = false;
    }
}

void CUIArtworkPanel::search_artwork_with_metadata(const std::string& artist, const std::string& title) {
    try {
        char debug_msg[512];
        sprintf_s(debug_msg, "ARTWORK: CUI Panel - Delegating metadata search to main component: artist='%s', title='%s'\n", 
                 artist.c_str(), title.c_str());
        OutputDebugStringA(debug_msg);
        
        // Use the external trigger function which will be updated to use priority search
        trigger_main_component_search_with_metadata(artist, title);
        
    } catch (...) {
        OutputDebugStringA("ARTWORK: CUI Panel - Exception delegating metadata search\n");
        g_artwork_loading = false;
    }
}

#endif // COLUMNS_UI_AVAILABLE