#include "stdafx.h"
#include "artwork_manager.h"
#include <gdiplus.h>
#include <atlbase.h>
#include <atlwin.h>
#include <algorithm>
#include <regex>

// Include CUI color system for proper foobar2000 theme colors
#include "columns_ui/columns_ui-sdk/colours.h"

#pragma comment(lib, "gdiplus.lib")

// External configuration variables
extern cfg_int cfg_stream_delay;
extern cfg_bool cfg_enable_custom_logos;
extern cfg_bool cfg_clear_panel_when_not_playing;

// External custom logo loading functions
extern HBITMAP load_station_logo(metadb_handle_ptr track);
extern HBITMAP load_noart_logo(metadb_handle_ptr track);
extern HBITMAP load_generic_noart_logo(metadb_handle_ptr track);

// External functions for triggering main component search (same as CUI)
extern void trigger_main_component_search(metadb_handle_ptr track);
extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);

// Forward declarations for the event system (matching sdk_main.cpp)
enum class ArtworkEventType {
    ARTWORK_LOADED,     // New artwork loaded successfully
    ARTWORK_LOADING,    // Search started
    ARTWORK_FAILED,     // Search failed
    ARTWORK_CLEARED     // Artwork cleared
};

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

// Forward declare the event manager class
class ArtworkEventManager {
public:
    static ArtworkEventManager& get();
    void subscribe(IArtworkEventListener* listener);
    void unsubscribe(IArtworkEventListener* listener);
    void notify(const ArtworkEvent& event);
};

// External references to event manager methods
extern void subscribe_to_artwork_events(IArtworkEventListener* listener);
extern void unsubscribe_from_artwork_events(IArtworkEventListener* listener);

// Custom message for artwork loading completion
#define WM_USER_ARTWORK_LOADED (WM_USER + 100)

// For now, we'll create a simplified version without ATL dependencies
// This creates a basic component that can be extended later
class artwork_ui_element : public ui_element_instance, public CWindowImpl<artwork_ui_element>, public IArtworkEventListener {
public:
    artwork_ui_element(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback);
    virtual ~artwork_ui_element();

    DECLARE_WND_CLASS_EX(L"foo_artwork_ui_element", 0, NULL);

    BEGIN_MSG_MAP(artwork_ui_element)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_USER_ARTWORK_LOADED, OnArtworkLoaded)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
    END_MSG_MAP()

    // ui_element_instance methods
    HWND get_wnd() override { return m_hWnd; }
    void set_configuration(ui_element_config::ptr cfg) override {}
    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(get_guid());
    }
    GUID get_guid() override {
        return GUID { 0x12345690, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };
    }
    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }
    void initialize_window(HWND parent);
    void shutdown();
    void notify(const GUID& p_what, t_size p_param1, const void* p_param2, t_size p_param2size) override;
    
    // IArtworkEventListener implementation
    void on_artwork_event(const ArtworkEvent& event) override;
    
    // service_base implementation
    int service_add_ref() throw() { return 1; }
    int service_release() throw() { return 1; }
    bool service_query(service_ptr & p_out, const GUID & p_guid) override {
        if (p_guid == ui_element_instance::class_guid) {
            p_out = this;
            return true;
        }
        return false;
    }

private:
    // Message handlers
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnArtworkLoaded(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // Artwork handling
    void on_playback_new_track(metadb_handle_ptr track);
    void on_dynamic_info_track(const file_info& p_info);
    void on_artwork_loaded(const artwork_manager::artwork_result& result);
    void start_artwork_search();
    void update_artwork_display();
    void clear_artwork();

    // Drawing
    void draw_artwork(HDC hdc, const RECT& rect);
    void draw_placeholder(HDC hdc, const RECT& rect);
    
    // GDI+ helpers
    bool load_image_from_memory(const t_uint8* data, size_t size);
    void cleanup_gdiplus_image();
    
    // OSD functions
    void show_osd(const std::string& text);
    void hide_osd();
    void update_osd_animation();
    void paint_osd(HDC hdc);
    
    // Stream delay functions
    bool is_internet_stream(metadb_handle_ptr track);
    void start_delayed_search();
    
    // Metadata validation and cleaning
    bool is_metadata_valid_for_search(const char* artist, const char* title);
    std::string clean_metadata_for_search(const char* metadata);

    ui_element_instance_callback::ptr m_callback;
    
    // Artwork data
    Gdiplus::Image* m_artwork_image;
    metadb_handle_ptr m_current_track;
    bool m_artwork_loading;
    
    // Stream delay metadata storage
    std::string m_delayed_artist;
    std::string m_delayed_title;
    bool m_has_delayed_metadata;
    
    // UI state
    RECT m_client_rect;
    
    // GDI+ token
    ULONG_PTR m_gdiplus_token;
    
    // OSD (On-Screen Display) system
    bool m_show_osd;
    std::string m_osd_text;
    std::string m_artwork_source;
    DWORD m_osd_start_time;
    int m_osd_slide_offset;
    UINT_PTR m_osd_timer_id;
    bool m_osd_visible;
    
    // OSD constants
    static const int OSD_DELAY_DURATION = 1000;   // 1 second delay before animation starts
    static const int OSD_DURATION_MS = 5000;      // 5 seconds visible duration
    static const int OSD_ANIMATION_SPEED = 8;     // 120 FPS: 1000ms / 120fps ≈ 8ms
    static const int OSD_SLIDE_DISTANCE = 200;
    static const int OSD_SLIDE_IN_DURATION = 300;  // 300ms smooth slide in
    static const int OSD_SLIDE_OUT_DURATION = 300; // 300ms smooth slide out
    
    // Playback callback
    class playback_callback_impl : public play_callback {
    public:
        playback_callback_impl(artwork_ui_element* parent) : m_parent(parent) {}
        
        void on_playback_new_track(metadb_handle_ptr p_track) override {
            if (m_parent) {
                m_parent->on_playback_new_track(p_track);
            }
        }
        
        void on_playback_stop(play_control::t_stop_reason p_reason) override {
            // Only clear artwork if the preference is enabled (like CUI)
            if (m_parent && cfg_clear_panel_when_not_playing) {
                m_parent->clear_artwork();
            }
        }
        
        // Required by play_callback base class
        void on_playback_starting(play_control::t_track_command p_command, bool p_paused) override {}
        void on_playback_seek(double p_time) override {}
        void on_playback_pause(bool p_state) override {}
        void on_playback_edited(metadb_handle_ptr p_track) override {}
        void on_playback_dynamic_info(const file_info& p_info) override {
        }
        void on_playback_dynamic_info_track(const file_info& p_info) override {
            if (m_parent) m_parent->on_dynamic_info_track(p_info);
        }
        void on_playback_time(double p_time) override {}
        void on_volume_change(float p_new_val) override {}
        
        void set_parent(artwork_ui_element* parent) { m_parent = parent; }
        
    private:
        artwork_ui_element* m_parent;
    };
    
    playback_callback_impl m_playback_callback;
};

artwork_ui_element::artwork_ui_element(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback)
    : m_callback(callback), m_artwork_image(nullptr), m_artwork_loading(false), 
      m_gdiplus_token(0), m_playback_callback(this),
      m_show_osd(true), m_osd_start_time(0), m_osd_slide_offset(OSD_SLIDE_DISTANCE), 
      m_osd_timer_id(0), m_osd_visible(false), m_has_delayed_metadata(false) {
    
    
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdiplusStartupInput, NULL);
    
    // Subscribe to artwork events for proper API fallback
    subscribe_to_artwork_events(this);
    
    SetRect(&m_client_rect, 0, 0, 0, 0);
}

artwork_ui_element::~artwork_ui_element() {
    // Unsubscribe from artwork events
    unsubscribe_from_artwork_events(this);
    
    cleanup_gdiplus_image();
    
    if (m_gdiplus_token) {
        Gdiplus::GdiplusShutdown(m_gdiplus_token);
    }
}

void artwork_ui_element::initialize_window(HWND parent) {
    Create(parent, NULL, NULL, WS_CHILD | WS_VISIBLE);
    
    // Register for playback callbacks including dynamic info
    m_playback_callback.set_parent(this);
    static_api_ptr_t<play_callback_manager>()->register_callback(&m_playback_callback, 
                                                                 play_callback::flag_on_playback_new_track | 
                                                                 play_callback::flag_on_playback_stop |
                                                                 play_callback::flag_on_playback_dynamic_info_track, 
                                                                 false);
    
    // Load artwork for currently playing track
    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr track;
    if (pc->get_now_playing(track)) {
        on_playback_new_track(track);
    }
}

void artwork_ui_element::shutdown() {
    // Unregister callbacks
    static_api_ptr_t<play_callback_manager>()->unregister_callback(&m_playback_callback);
    m_playback_callback.set_parent(nullptr);
    
    if (IsWindow()) {
        DestroyWindow();
    }
}

void artwork_ui_element::notify(const GUID& p_what, t_size p_param1, const void* p_param2, t_size p_param2size) {
    // Handle color/theme change notifications
    if (p_what == ui_color_background || p_what == ui_color_darkmode) {
        if (IsWindow()) {
            Invalidate();
        }
    }
}

LRESULT artwork_ui_element::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    return 0;
}

LRESULT artwork_ui_element::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    cleanup_gdiplus_image();
    bHandled = TRUE;
    return 0;
}

LRESULT artwork_ui_element::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(&ps);
    
    // Use double buffering to eliminate flicker during resizing
    RECT client_rect;
    GetClientRect(&client_rect);
    
    // Create memory DC and bitmap for double buffering
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, client_rect.right, client_rect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // Paint to memory DC first (off-screen)
    draw_artwork(memDC, client_rect);
    
    // Paint OSD overlay on memory DC before copying to screen
    if (m_osd_visible) {
        paint_osd(memDC);
    }
    
    // Copy the entire off-screen buffer to screen in one operation (flicker-free)
    BitBlt(hdc, 0, 0, client_rect.right, client_rect.bottom, memDC, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    
    EndPaint(&ps);
    bHandled = TRUE;
    return 0;
}

LRESULT artwork_ui_element::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    GetClientRect(&m_client_rect);
    
    // Use RedrawWindow for flicker-free resizing instead of Invalidate()
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
    bHandled = TRUE;
    return 0;
}

LRESULT artwork_ui_element::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    return TRUE; // We handle all drawing in OnPaint
}

LRESULT artwork_ui_element::OnContextMenu(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Could add context menu for artwork options
    bHandled = FALSE; // Let default handler process
    return 0;
}

LRESULT artwork_ui_element::OnArtworkLoaded(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Safely handle artwork loading completion on UI thread
    bHandled = TRUE;
    
    std::unique_ptr<artwork_manager::artwork_result> result(reinterpret_cast<artwork_manager::artwork_result*>(lParam));
    if (result) {
        on_artwork_loaded(*result);
    }
    
    return 0;
}

LRESULT artwork_ui_element::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (wParam == m_osd_timer_id) {
        // OSD animation timer
        update_osd_animation();
        bHandled = TRUE;
        return 0;
    } else if (wParam == 100) {
        // Metadata arrival timer - no metadata received within grace period, start fallback search (like CUI)
        KillTimer(100);
        start_artwork_search();
        bHandled = TRUE;
        return 0;
    } else if (wParam == 101) {
        // Timer for delayed artwork search on radio streams (stream delay)
        KillTimer(101);
        start_delayed_search();
        bHandled = TRUE;
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

void artwork_ui_element::on_playback_new_track(metadb_handle_ptr track) {
    m_current_track = track;
    m_artwork_loading = true;
    
    // Clear any pending delayed metadata from previous track
    m_has_delayed_metadata = false;
    m_delayed_artist.clear();
    m_delayed_title.clear();
    
    // Keep previous artwork visible until replaced (like CUI)
    // Don't clear artwork here - let new artwork replace it when found
    
    if (track.is_valid()) {
        if (is_internet_stream(track)) {
            // Kill any existing timers
            KillTimer(100); // Metadata arrival timer
            KillTimer(101); // Stream delay timer
            
            // Set initial metadata arrival timer (like CUI) - 3 second grace period
            // This gives metadata time to arrive via on_dynamic_info_track()
            SetTimer(100, 3000); // Timer ID 100, 3 second timeout
        } else {
            // For local files, start search immediately
            start_artwork_search();
        }
    }
}

void artwork_ui_element::on_dynamic_info_track(const file_info& p_info) {
    try {
        // Get artist and track from the updated info safely
        const char* artist_ptr = p_info.meta_get("ARTIST", 0);
        const char* track_ptr = p_info.meta_get("TITLE", 0);
        
        pfc::string8 artist = artist_ptr ? artist_ptr : "";
        pfc::string8 track = track_ptr ? track_ptr : "";
        
        // Clean metadata before validation
        std::string cleaned_artist = clean_metadata_for_search(artist.c_str());
        std::string cleaned_track = clean_metadata_for_search(track.c_str());
        
        // Apply comprehensive metadata validation rules
        bool is_valid_metadata = is_metadata_valid_for_search(cleaned_artist.c_str(), cleaned_track.c_str());
        
        if (is_valid_metadata) {
            // Cancel metadata arrival timer since we got valid metadata (like CUI)
            KillTimer(100);
            
            // Keep previous artwork visible until replaced (like CUI)
            // Don't clear artwork here - let new artwork replace it when found
            
            // FIRST: Check for custom station logos if enabled (before API search)
            if (m_current_track.is_valid() && is_internet_stream(m_current_track) && cfg_enable_custom_logos) {
                HBITMAP logo_bitmap = load_station_logo(m_current_track);
                if (logo_bitmap) {
                    
                    // Convert HBITMAP to GDI+ Image for DUI display
                    cleanup_gdiplus_image();
                    
                    try {
                        m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(logo_bitmap, NULL);
                        
                        if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                            m_artwork_loading = false;
                            m_artwork_source = "Station logo";
                            
                            // No OSD for local files (station logos)
                            Invalidate(); // Trigger repaint
                            return; // Exit early - don't do API search
                        } else {
                            cleanup_gdiplus_image();
                        }
                    } catch (...) {
                        cleanup_gdiplus_image();
                    }
                }
            }
            
            // If we reach here, no custom logo was found or custom logos are disabled
            // Proceed with API search (with delay if configured)
            if (m_current_track.is_valid() && is_internet_stream(m_current_track) && cfg_stream_delay > 0) {
                // Kill any existing delay timer
                KillTimer(101);
                
                // Store cleaned metadata for delayed search
                m_delayed_artist = cleaned_artist;
                m_delayed_title = cleaned_track;
                m_has_delayed_metadata = true;
                
                // Set timer for stream delay, metadata will be used when timer expires
                SetTimer(101, cfg_stream_delay * 1000);
            } else {
                // Use the main component trigger function for proper API fallback (results come via event system)
                trigger_main_component_search_with_metadata(cleaned_artist, cleaned_track);
            }
        }
    } catch (...) {
        // Ignore metadata processing errors
    }
}

void artwork_ui_element::start_artwork_search() {
    if (!m_current_track.is_valid()) {
        return;
    }
    
    try {
        // FIRST: Check for custom station logos if enabled (for fallback case when no metadata)
        if (is_internet_stream(m_current_track) && cfg_enable_custom_logos) {
            HBITMAP logo_bitmap = load_station_logo(m_current_track);
            if (logo_bitmap) {
                // Convert HBITMAP to GDI+ Image for DUI display
                cleanup_gdiplus_image();
                
                try {
                    m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(logo_bitmap, NULL);
                    
                    if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                        m_artwork_loading = false;
                        m_artwork_source = "Station logo";
                        
                        // No OSD for local files (station logos)
                        Invalidate(); // Trigger repaint
                        return; // Exit early - don't do API search
                    } else {
                        cleanup_gdiplus_image();
                    }
                } catch (...) {
                    cleanup_gdiplus_image();
                }
            }
        }
        
        // Notify event system that artwork loading started
        ArtworkEventManager::get().notify(ArtworkEvent(
            ArtworkEventType::ARTWORK_LOADING, 
            nullptr, 
            "Artwork search started", 
            "", 
            ""
        ));
        
        // Use the main component trigger function for proper API fallback (results come via event system)
        trigger_main_component_search(m_current_track);
    } catch (const std::exception& e) {
        // Handle any initialization errors silently
        m_artwork_loading = false;
    }
}

void artwork_ui_element::on_artwork_loaded(const artwork_manager::artwork_result& result) {
    m_artwork_loading = false;
    
    if (result.success && result.data.get_size() > 0) {
        if (load_image_from_memory(result.data.get_ptr(), result.data.get_size())) {
            // Store artwork source and show OSD for Default UI
            std::string source = result.source.is_empty() ? "Unknown" : result.source.c_str();
            m_artwork_source = source;
            
            // Show OSD animation for Default UI (only for online sources, not local files)
            if (source != "Local file" && !source.empty() && source != "Cache") {
                show_osd("Artwork from " + source);
            }
            
            // Also notify event system for CUI panels
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_LOADED, 
                nullptr,  // UI element doesn't have HBITMAP, CUI panel will get its own copy
                source, 
                "",  // Artist info not available in UI element context
                ""   // Title info not available in UI element context  
            ));
            
            Invalidate();
        } else {
            // Notify event system that artwork loading failed
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_FAILED, 
                nullptr, 
                "Image decode failed", 
                "", 
                ""
            ));
        }
    } else {
        // API search failed - try fallback images (like Columns UI)
        // This implements the same fallback hierarchy as Columns UI
        
        bool fallback_loaded = false;
        
        // Only try fallback images for internet streams
        if (m_current_track.is_valid() && is_internet_stream(m_current_track) && cfg_enable_custom_logos) {
            // Priority 1: Station-specific fallback with full path (e.g., ice1.somafm.com_indiepop-128-aac-noart.png)
            if (!fallback_loaded) {
                HBITMAP noart_bitmap = load_noart_logo(m_current_track);
                if (noart_bitmap) {
                    cleanup_gdiplus_image();
                    try {
                        m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(noart_bitmap, NULL);
                        if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                            m_artwork_loading = false;
                            m_artwork_source = "Station fallback (no artwork)";
                            fallback_loaded = true;
                            Invalidate();
                        } else {
                            cleanup_gdiplus_image();
                        }
                    } catch (...) {
                        cleanup_gdiplus_image();
                    }
                    DeleteObject(noart_bitmap); // Always clean up the source bitmap
                }
            }
            
            // Priority 2: Generic fallback with URL support (e.g., somafm.com-noart.png or noart.png)
            if (!fallback_loaded) {
                HBITMAP generic_bitmap = load_generic_noart_logo(m_current_track);
                if (generic_bitmap) {
                    cleanup_gdiplus_image();
                    try {
                        m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(generic_bitmap, NULL);
                        if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                            m_artwork_loading = false;
                            m_artwork_source = "Generic fallback (no artwork)";
                            fallback_loaded = true;
                            Invalidate();
                        } else {
                            cleanup_gdiplus_image();
                        }
                    } catch (...) {
                        cleanup_gdiplus_image();
                    }
                    DeleteObject(generic_bitmap); // Always clean up the source bitmap
                }
            }
        }
        
        if (!fallback_loaded) {
            // No fallback found - notify event system that artwork loading failed
            std::string error_source = result.source.is_empty() ? "Unknown source" : result.source.c_str();
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_FAILED, 
                nullptr, 
                error_source, 
                "", 
                ""
            ));
            
            Invalidate();
        }
    }
}

void artwork_ui_element::clear_artwork() {
    m_current_track.release();
    cleanup_gdiplus_image();
    m_artwork_loading = false;
    Invalidate();
}

void artwork_ui_element::draw_artwork(HDC hdc, const RECT& rect) {
    // Use proper DUI callback system for background color
    COLORREF bg_color = GetSysColor(COLOR_WINDOW); // Default fallback
    
    
    if (m_callback.is_valid()) {
        // Use the DUI callback to get the proper background color
        t_ui_color callback_color;
        if (m_callback->query_color(ui_color_background, callback_color)) {
            bg_color = callback_color;
        } else {
            // Try the standard color query
            bg_color = m_callback->query_std_color(ui_color_background);
        }
    }
    
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    FillRect(hdc, &m_client_rect, bg_brush);
    DeleteObject(bg_brush);
    
    if (m_artwork_image) {
        // Draw artwork
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        
        // Calculate aspect ratio preserving rectangle
        UINT img_width = m_artwork_image->GetWidth();
        UINT img_height = m_artwork_image->GetHeight();
        
        int client_width = m_client_rect.right - m_client_rect.left;
        int client_height = m_client_rect.bottom - m_client_rect.top;
        
        if (img_width > 0 && img_height > 0 && client_width > 0 && client_height > 0) {
            double img_aspect = (double)img_width / img_height;
            double client_aspect = (double)client_width / client_height;
            
            int draw_width, draw_height, draw_x, draw_y;
            
            if (img_aspect > client_aspect) {
                // Image is wider than client area
                draw_width = client_width;
                draw_height = (int)(client_width / img_aspect);
                draw_x = 0;
                draw_y = (client_height - draw_height) / 2;
            } else {
                // Image is taller than client area
                draw_width = (int)(client_height * img_aspect);
                draw_height = client_height;
                draw_x = (client_width - draw_width) / 2;
                draw_y = 0;
            }
            
            Gdiplus::Rect dest_rect(draw_x, draw_y, draw_width, draw_height);
            graphics.DrawImage(m_artwork_image, dest_rect);
        }
    } else if (m_artwork_loading) {
        // Show loading indicator (without text)
        draw_placeholder(hdc, m_client_rect);
    } else {
        // Show placeholder
        draw_placeholder(hdc, m_client_rect);
    }
}

void artwork_ui_element::draw_placeholder(HDC hdc, const RECT& rect) {
    // Placeholder drawing disabled - just show clean background
    // No placeholder rectangle or icon needed
}

bool artwork_ui_element::load_image_from_memory(const t_uint8* data, size_t size) {
    cleanup_gdiplus_image();
    
    // Create IStream from memory
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hGlobal) {
        return false;
    }
    
    void* pData = GlobalLock(hGlobal);
    if (!pData) {
        GlobalFree(hGlobal);
        return false;
    }
    
    memcpy(pData, data, size);
    GlobalUnlock(hGlobal);
    
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hGlobal, TRUE, &stream) != S_OK) {
        GlobalFree(hGlobal);
        return false;
    }
    
    // Create GDI+ Image from stream
    m_artwork_image = Gdiplus::Image::FromStream(stream);
    stream->Release();
    
    if (!m_artwork_image || m_artwork_image->GetLastStatus() != Gdiplus::Ok) {
        cleanup_gdiplus_image();
        return false;
    }
    
    return true;
}

void artwork_ui_element::cleanup_gdiplus_image() {
    if (m_artwork_image) {
        delete m_artwork_image;
        m_artwork_image = nullptr;
    }
}

void artwork_ui_element::show_osd(const std::string& text) {
    // Check if this is a local file OSD call that should be blocked (like CUI)
    if (text.find("Local file") != std::string::npos || 
        text.find("local") != std::string::npos) {
        return;
    }
    
    // Also check if current track is a local file - if so, block all OSD (like CUI)
    if (m_current_track.is_valid()) {
        try {
            pfc::string8 current_path = m_current_track->get_path();
            if (!current_path.is_empty()) {
                bool is_current_local = (strstr(current_path.c_str(), "file://") == current_path.c_str()) || 
                                       !(strstr(current_path.c_str(), "://"));
                if (is_current_local) {
                    return;
                }
            }
        } catch (...) {
            // If path access fails, err on the side of caution and block OSD
            return;
        }
    }
    
    m_osd_text = text;
    m_osd_start_time = GetTickCount();
    m_osd_slide_offset = OSD_SLIDE_DISTANCE;
    m_osd_visible = true;
    
    // Start animation timer
    m_osd_timer_id = SetTimer(1001, OSD_ANIMATION_SPEED);
    
    // Invalidate to trigger repaint
    Invalidate();
}

void artwork_ui_element::hide_osd() {
    m_osd_visible = false;
    if (m_osd_timer_id) {
        KillTimer(m_osd_timer_id);
        m_osd_timer_id = 0;
    }
    
    // Invalidate to trigger repaint
    Invalidate();
}

void artwork_ui_element::update_osd_animation() {
    if (!m_osd_visible) return;
    
    DWORD current_time = GetTickCount();
    DWORD elapsed = current_time - m_osd_start_time;
    
    // 5-phase animation: delay → slide-in → visible → slide-out → hidden
    if (elapsed < OSD_DELAY_DURATION) {
        // Phase 1: Delay phase - keep off-screen
        m_osd_slide_offset = OSD_SLIDE_DISTANCE;
    } else if (elapsed < OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION) {
        // Phase 2: Slide-in animation with cubic ease-out
        double progress = (double)(elapsed - OSD_DELAY_DURATION) / OSD_SLIDE_IN_DURATION;
        progress = 1.0 - pow(1.0 - progress, 3.0); // Cubic ease-out
        m_osd_slide_offset = (int)(OSD_SLIDE_DISTANCE * (1.0 - progress));
    } else if (elapsed < OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DURATION_MS) {
        // Phase 3: Fully visible
        m_osd_slide_offset = 0;
    } else if (elapsed < OSD_DELAY_DURATION + OSD_SLIDE_IN_DURATION + OSD_DURATION_MS + OSD_SLIDE_OUT_DURATION) {
        // Phase 4: Slide-out animation with cubic ease-in
        double progress = (double)(elapsed - OSD_DELAY_DURATION - OSD_SLIDE_IN_DURATION - OSD_DURATION_MS) / OSD_SLIDE_OUT_DURATION;
        progress = pow(progress, 3.0); // Cubic ease-in
        m_osd_slide_offset = (int)(OSD_SLIDE_DISTANCE * progress);
    } else {
        // Phase 5: Animation complete - hide OSD
        hide_osd();
        return;
    }
    
    // Trigger repaint for smooth animation
    Invalidate();
}

void artwork_ui_element::paint_osd(HDC hdc) {
    if (!m_osd_visible || m_osd_text.empty()) return;
    
    // Save the current GDI state (exactly like CUI)
    int saved_dc = SaveDC(hdc);
    
    RECT client_rect;
    GetClientRect(&client_rect);
    
    // Calculate OSD dimensions using actual text measurement (like CUI)
    SIZE text_size;
    HFONT old_font = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32A(hdc, m_osd_text.c_str(), (int)m_osd_text.length(), &text_size);
    
    const int padding = 8;  // Match CUI padding
    const int osd_width = text_size.cx + (padding * 2);
    const int osd_height = text_size.cy + (padding * 2);
    
    // Position in bottom-right corner with slide offset (exactly like CUI)
    int osd_x = client_rect.right - osd_width - 10 + m_osd_slide_offset;
    int osd_y = client_rect.bottom - osd_height - 10;
    
    // Don't draw if completely slid off screen (exactly like CUI)
    if (osd_x >= client_rect.right) {
        RestoreDC(hdc, saved_dc);
        return;
    }
    
    // Create OSD rectangle
    RECT osd_rect = { osd_x, osd_y, osd_x + osd_width, osd_y + osd_height };
    
    // Use same DUI color system as main background
    COLORREF base_bg_color = GetSysColor(COLOR_WINDOW); // Default fallback
    
    if (m_callback.is_valid()) {
        // Use the DUI callback to get the proper background color
        t_ui_color callback_color;
        if (m_callback->query_color(ui_color_background, callback_color)) {
            base_bg_color = callback_color;
        } else {
            base_bg_color = m_callback->query_std_color(ui_color_background);
        }
    }
    
    // Declare color variables
    COLORREF osd_bg_color, osd_border_color;
    
    // Create darker background for better contrast (like CUI)
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
    
    // Get contrasting text color using DUI callback
    COLORREF text_color = GetSysColor(COLOR_WINDOWTEXT); // Default fallback
    
    if (m_callback.is_valid()) {
        t_ui_color callback_text_color;
        if (m_callback->query_color(ui_color_text, callback_text_color)) {
            text_color = callback_text_color;
        } else {
            text_color = m_callback->query_std_color(ui_color_text);
        }
    }
    
    // Create background brush and border pen using system colors (like CUI)
    HBRUSH bg_brush = CreateSolidBrush(osd_bg_color);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, bg_brush);
    
    // Set pen for border
    HPEN border_pen = CreatePen(PS_SOLID, 1, osd_border_color);
    HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
    
    // Draw filled rectangle with border (exactly like CUI)
    Rectangle(hdc, osd_rect.left, osd_rect.top, osd_rect.right, osd_rect.bottom);
    
    // Set text properties (exactly like CUI)
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_color);
    
    // Calculate centered text position (exactly like CUI)
    RECT text_rect = { 
        osd_rect.left + padding, 
        osd_rect.top + padding, 
        osd_rect.right - padding, 
        osd_rect.bottom - padding 
    };
    
    // Draw the text (exactly like CUI)
    DrawTextA(hdc, m_osd_text.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    
    // Clean up GDI objects (exactly like CUI)
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_font);
    DeleteObject(bg_brush);
    DeleteObject(border_pen);
    
    // Restore original DC state (exactly like CUI)
    RestoreDC(hdc, saved_dc);
}

bool artwork_ui_element::is_internet_stream(metadb_handle_ptr track) {
    if (!track.is_valid()) return false;
    
    try {
        pfc::string8 path = track->get_path();
        if (path.is_empty()) return false;
        
        // Check for protocol indicators (anything with :// except file://)
        const char* protocol_pos = strstr(path.c_str(), "://");
        if (!protocol_pos) {
            return false; // No protocol found, likely local file
        }
        
        // Exclude file:// protocol
        if (strstr(path.c_str(), "file://") == path.c_str()) {
            return false; // This is a file:// URL, not an internet stream
        }
        
        return true; // This appears to be an internet stream
    } catch (...) {
        return false; // Error accessing path, assume local file
    }
}

void artwork_ui_element::start_delayed_search() {
    // Check if we have delayed metadata to use
    if (m_has_delayed_metadata && !m_delayed_title.empty()) {
        // Use the main component trigger function for proper API fallback (results come via event system)
        trigger_main_component_search_with_metadata(m_delayed_artist, m_delayed_title);
        
        // Clear the delayed metadata
        m_has_delayed_metadata = false;
        m_delayed_artist.clear();
        m_delayed_title.clear();
    } else {
        // No delayed metadata, use regular track-based search
        start_artwork_search();
    }
}

bool artwork_ui_element::is_metadata_valid_for_search(const char* artist, const char* title) {
    // Convert to strings for easier manipulation
    std::string artist_str = artist ? artist : "";
    std::string title_str = title ? title : "";
    
    // Rule 1: Must have a title - no search without title
    if (title_str.empty()) {
        return false;
    }
    
    // Rule 2: Block common invalid patterns
    if (title_str == "?" || artist_str == "?") {
        return false;
    }
    
    // Rule 3: Block "? - ?" pattern
    if ((artist_str == "?" && title_str == "?") || 
        title_str == "? - ?" || artist_str == "? - ?") {
        return false;
    }
    
    // Rule 4: Block "adbreak" (advertisement breaks)
    if (title_str.find("adbreak") != std::string::npos || 
        artist_str.find("adbreak") != std::string::npos) {
        return false;
    }
    
    // Rule 5: Block "Unknown" patterns
    if (title_str == "Unknown Track" || artist_str == "Unknown Artist" ||
        title_str == "Unknown" || artist_str == "Unknown") {
        return false;
    }
    
    // Rule 6: Block very short or suspicious titles
    if (title_str.length() < 2) {
        return false;
    }
    return true;
}

std::string artwork_ui_element::clean_metadata_for_search(const char* metadata) {
    if (!metadata) return "";
    
    std::string str(metadata);
    
    // Remove timestamp patterns at the end
    // Pattern 1: " - MM:SS" or " - M:SS" (like " - 0:00")
    std::regex dash_time_regex("\\s+-\\s+\\d{1,2}:\\d{2}\\s*$");
    str = std::regex_replace(str, dash_time_regex, "");
    
    // Pattern 2: " - MM.SS" or " - M.SS" (like " - 0.00") - handle decimal point
    std::regex dash_decimal_regex("\\s+-\\s+\\d{1,2}\\.\\d{2}\\s*$");
    str = std::regex_replace(str, dash_decimal_regex, "");
    
    // Remove parenthetical timestamps (MM:SS) or (M:SS)
    std::regex paren_time_regex("\\s*\\(\\d{1,2}:\\d{2}\\)\\s*");
    str = std::regex_replace(str, paren_time_regex, " ");
    
    // Remove all parenthetical content (like "(Vocal Version)", "(Remix)", etc.)
    std::regex paren_content_regex("\\s*\\([^)]*\\)\\s*");
    str = std::regex_replace(str, paren_content_regex, " ");
    
    // Clean up multiple spaces
    std::regex multi_space("\\s{2,}");
    str = std::regex_replace(str, multi_space, " ");
    
    // Trim leading and trailing spaces
    str = std::regex_replace(str, std::regex("^\\s+|\\s+$"), "");
    
    return str;
}

void artwork_ui_element::on_artwork_event(const ArtworkEvent& event) {
    if (!IsWindow()) return;
    
    switch (event.type) {
        case ArtworkEventType::ARTWORK_LOADED:
            if (event.bitmap) {
                m_artwork_loading = false;
                
                // Convert HBITMAP directly to GDI+ Image (like the existing custom logo loading)
                cleanup_gdiplus_image();
                try {
                    m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(event.bitmap, NULL);
                    if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                        // Show OSD for online sources (not local files)
                        if (!event.source.empty() && event.source != "Local file" && event.source != "Cache") {
                            show_osd("Artwork from " + event.source);
                        }
                        
                        Invalidate(); // Trigger repaint
                    } else {
                        cleanup_gdiplus_image();
                    }
                } catch (...) {
                    cleanup_gdiplus_image();
                }
            }
            break;
            
        case ArtworkEventType::ARTWORK_LOADING:
            m_artwork_loading = true;
            break;
            
        case ArtworkEventType::ARTWORK_FAILED:
            {
                m_artwork_loading = false;
                
                // Try fallback images when API search fails (same logic as on_artwork_loaded)
                bool fallback_loaded = false;
                
                // Only try fallback images for internet streams
                if (m_current_track.is_valid() && is_internet_stream(m_current_track) && cfg_enable_custom_logos) {
                    // Priority 1: Station-specific fallback with full path (e.g., ice1.somafm.com_indiepop-128-aac-noart.png)
                    if (!fallback_loaded) {
                        HBITMAP noart_bitmap = load_noart_logo(m_current_track);
                        if (noart_bitmap) {
                            cleanup_gdiplus_image();
                            try {
                                m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(noart_bitmap, NULL);
                                if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                                    m_artwork_loading = false;
                                    m_artwork_source = "Station fallback (no artwork)";
                                    fallback_loaded = true;
                                    Invalidate();
                                } else {
                                    cleanup_gdiplus_image();
                                }
                            } catch (...) {
                                cleanup_gdiplus_image();
                            }
                            DeleteObject(noart_bitmap); // Always clean up the source bitmap
                        }
                    }
                    
                    // Priority 2: Generic fallback with URL support (e.g., somafm.com-noart.png or noart.png)
                    if (!fallback_loaded) {
                        HBITMAP generic_bitmap = load_generic_noart_logo(m_current_track);
                        if (generic_bitmap) {
                            cleanup_gdiplus_image();
                            try {
                                m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(generic_bitmap, NULL);
                                if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                                    m_artwork_loading = false;
                                    m_artwork_source = "Generic fallback (no artwork)";
                                    fallback_loaded = true;
                                    Invalidate();
                                } else {
                                    cleanup_gdiplus_image();
                                }
                            } catch (...) {
                                cleanup_gdiplus_image();
                            }
                            DeleteObject(generic_bitmap); // Always clean up the source bitmap
                        }
                    }
                }
                break;
            }
            
        case ArtworkEventType::ARTWORK_CLEARED:
            break;
    }
}

// Minimal working UI element implementation
class simple_artwork_element : public ui_element_instance {
public:
    simple_artwork_element(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback) 
        : m_callback(callback), m_parent(parent), m_hwnd(NULL) {
        // Create a simple child window that just shows text
        m_hwnd = CreateWindowEx(0, L"STATIC", L"Async Artwork System Active", 
                               WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
                               0, 0, 200, 100, parent, NULL, 
                               GetModuleHandle(NULL), NULL);
    }
    
    ~simple_artwork_element() {
        if (m_hwnd && IsWindow(m_hwnd)) {
            DestroyWindow(m_hwnd);
        }
    }
    
    // ui_element_instance implementation
    HWND get_wnd() override { return m_hwnd; }
    
    void set_configuration(ui_element_config::ptr cfg) override {
        // No configuration needed
    }
    
    ui_element_config::ptr get_configuration() override {
        return ui_element_config::g_create_empty(get_guid());
    }
    
    GUID get_guid() override {
        return GUID { 0x12345690, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };
    }
    
    GUID get_subclass() override {
        return ui_element_subclass_utility;
    }
    
    void initialize_window(HWND parent) {
        // Already initialized in constructor
    }
    
    void shutdown() {
        if (m_hwnd && IsWindow(m_hwnd)) {
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
    }
    
    void notify(const GUID& what, t_size param1, const void* param2, t_size param2size) override {
        // Handle notifications if needed
    }
    
    // service_base implementation
    int service_add_ref() throw() { return 1; }
    int service_release() throw() { return 1; }

private:
    ui_element_instance_callback::ptr m_callback;
    HWND m_parent;
    HWND m_hwnd;
};

// Simple UI element factory that just returns null to avoid abstract class issues
class ui_element_artwork_factory : public ui_element {
public:
    GUID get_guid() {
        return GUID { 0x12345690, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };
    }
    
    void get_name(pfc::string_base & p_out) { 
        p_out = "Artwork Display"; 
    }
    
    GUID get_subclass() {
        return ui_element_subclass_utility;
    }
    
    ui_element_instance::ptr instantiate(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback) {
        try {
            artwork_ui_element* element = new artwork_ui_element(cfg, callback);
            element->initialize_window(parent);
            return element;
        } catch (...) {
            return nullptr;
        }
    }
    
    ui_element_config::ptr get_default_configuration() {
        return ui_element_config::g_create_empty(get_guid());
    }
    
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr cfg) {
        return nullptr; // Not a container element
    }
    
    // service_base implementation
    int service_add_ref() throw() { return 1; }
    int service_release() throw() { return 1; }
};

static service_factory_single_t<ui_element_artwork_factory> g_ui_element_artwork_factory;

// Simple test of the async artwork system - this can be called programmatically
void test_async_artwork_system() {
    // This would be called by existing UI components or other parts of the system
    // artwork_manager::get_artwork_async(track, callback);
}

static class ui_element_debug {
public:
    ui_element_debug() {
        test_async_artwork_system();
    }
} g_ui_element_debug;

// Async UI element is now active - using truly asynchronous artwork system
