#include "stdafx.h"
#include <windowsx.h>
#include "artwork_manager.h"
#include "artwork_viewer_popup.h"
#include "metadata_cleaner.h"
#include "webp_decoder.h"
#include <gdiplus.h>
#include <atlbase.h>
#include <atlwin.h>
#include <algorithm>
#include <regex>
#include <vector>

// Include CUI color system for proper foobar2000 theme colors
#include "columns_ui/columns_ui-sdk/colours.h"

// infobar
  
#pragma comment(lib, "gdiplus.lib")

// External configuration variables
extern cfg_bool cfg_enable_custom_logos;
extern cfg_string cfg_logos_folder;
extern cfg_bool cfg_clear_panel_when_not_playing;
extern cfg_bool cfg_use_noart_image;
extern cfg_bool cfg_show_osd;
extern cfg_bool cfg_infobar;

// External custom logo loading functions
extern HBITMAP load_station_logo(metadb_handle_ptr track);
extern Gdiplus::Bitmap* load_station_logo_gdiplus(metadb_handle_ptr track);
extern HBITMAP load_noart_logo(metadb_handle_ptr track);
extern std::unique_ptr<Gdiplus::Bitmap> load_noart_logo_gdiplus(metadb_handle_ptr track);
extern HBITMAP load_generic_noart_logo(metadb_handle_ptr track);
extern std::unique_ptr<Gdiplus::Bitmap> load_generic_noart_logo_gdiplus();

// External functions for triggering main component search (same as CUI)
extern void trigger_main_component_search(metadb_handle_ptr track);
extern void trigger_main_component_search_with_metadata(const std::string& artist, const std::string& title);
extern void trigger_main_component_local_search(metadb_handle_ptr track);

// External function to get main component artwork bitmap (for priority checking)
extern HBITMAP get_main_component_artwork_bitmap();

// Global list to track DUI artwork element instances for external refresh
static pfc::list_t<class artwork_ui_element*> g_dui_artwork_panels;

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

// Custom message for thread-safe artwork event dispatching
#define WM_USER_ARTWORK_EVENT (WM_USER + 101)

// Heap-allocated struct for passing artwork event data via PostMessage
struct ArtworkEventData {
    ArtworkEventType type;
    HBITMAP bitmap;
    std::string source;
    std::string artist;
    std::string title;
};

// For now, we'll create a simplified version without ATL dependencies
// This creates a basic component that can be extended later
class artwork_ui_element : public ui_element_instance, public CWindowImpl<artwork_ui_element>, public IArtworkEventListener {
public:
    artwork_ui_element(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback);
    virtual ~artwork_ui_element();

    DECLARE_WND_CLASS_EX(L"foo_artwork_ui_element", CS_DBLCLKS, NULL);

    BEGIN_MSG_MAP(artwork_ui_element)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
        MESSAGE_HANDLER(WM_USER_ARTWORK_LOADED, OnArtworkLoaded)
        MESSAGE_HANDLER(WM_USER_ARTWORK_EVENT, OnArtworkEvent)
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
    
    // Getter for artwork source (for main component access)
    std::string get_artwork_source() const { return m_artwork_source; }
    
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

    // Artwork handling (public for external refresh)
    void on_playback_new_track(metadb_handle_ptr track);

private:
    // Message handlers
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnArtworkLoaded(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnArtworkEvent(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
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
    void update_clear_panel_timer();  // Start/stop clear panel monitoring timer
    void load_noart_image();  // Load noart image for "use noart image" option
    void paint_osd(HDC hdc);
    
    // Timer functions
    bool is_internet_stream(metadb_handle_ptr track);
    bool is_stream_with_possible_artwork(metadb_handle_ptr track);
    bool is_youtube_stream(metadb_handle_ptr track);
    void start_delayed_search();
    
    // Metadata validation and cleaning
    bool is_metadata_valid_for_search(const char* artist, const char* title);
    std::string clean_metadata_for_search(const char* metadata);
    
    // Local artwork priority checking
    bool should_prefer_local_artwork();
    bool load_local_artwork_from_main_component();

	// Inverted stream detection
    bool is_inverted_internet_stream(metadb_handle_ptr track, const file_info& p_info);							
    ui_element_instance_callback::ptr m_callback;
    
    // Artwork data
    Gdiplus::Image* m_artwork_image;
    IStream* m_artwork_stream = nullptr;
    IStream* m_infobar_stream = nullptr;
    metadb_handle_ptr m_current_track;
    bool m_artwork_loading;
    
    // Delayed search metadata storage
    std::string m_delayed_artist;
    std::string m_delayed_title;
    bool m_has_delayed_metadata;
    
    //Metadata infobar

    void clear_infobar();
    void cleanup_gdiplus_infobar_image();

    std::wstring stringToWstring(const std::string& str) {
        if (str.empty()) {
            return std::wstring();
        }
        
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
        if (size_needed == 0) {
            return std::wstring();
        }
        
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    std::wstring m_infobar_artist;
    std::wstring m_infobar_title;
    std::wstring m_infobar_album;
    std::wstring m_infobar_station;	
    std::wstring m_infobar_result;
    Gdiplus::Bitmap* m_infobar_image = nullptr;
    Gdiplus::Bitmap* m_infobar_bitmap = nullptr;

    // Stream dynamic info metadata storage
    void clear_dinfo();
    std::string m_dinfo_artist;
    std::string m_dinfo_title;
    
    //First run counter
    void reset_m_counter();
    int m_counter = 0;

    // Download icon hover state
    bool m_mouse_hovering;
    bool m_hover_over_download;
    RECT m_download_icon_rect;
    BYTE m_download_fade_alpha;
    UINT_PTR m_download_fade_timer_id;

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
    bool m_was_playing;  // Track previous playback state for clear panel functionality
    
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
            m_parent->clear_infobar();
            m_parent->clear_dinfo();
            m_parent->reset_m_counter();
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

//=============================================================================
// Download icon overlay helper
//=============================================================================

static void draw_download_icon(HDC hdc, const RECT& client_rect, bool hovered, RECT& out_icon_rect, BYTE fade_alpha = 255)
{
    HMODULE hGrab = GetModuleHandle(L"foo_artgrab.dll");
    if (!hGrab || fade_alpha == 0) {
        SetRectEmpty(&out_icon_rect);
        return;
    }

    const int icon_size = 24;
    const int padding = 10;
    const int bg_pad = 6;

    int ix = client_rect.left + padding;
    int iy = client_rect.bottom - padding - icon_size;

    RECT bg = { ix - bg_pad, iy - bg_pad, ix + icon_size + bg_pad, iy + icon_size + bg_pad };

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    BYTE base_alpha = hovered ? (BYTE)200 : (BYTE)120;
    BYTE alpha = (BYTE)((int)base_alpha * fade_alpha / 255);
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

    float scale = (float)icon_size / 960.0f;
    float ox = (float)ix;
    float oy = (float)iy + (float)icon_size;

    BYTE icon_alpha = (BYTE)((int)230 * fade_alpha / 255);
    Gdiplus::SolidBrush iconBrush(Gdiplus::Color(icon_alpha, 255, 255, 255));

    // Arrow portion
    {
        Gdiplus::GraphicsPath arrow;
        Gdiplus::PointF pts[] = {
            { ox + 480*scale, oy + (-320)*scale },
            { ox + 280*scale, oy + (-520)*scale },
            { ox + 336*scale, oy + (-578)*scale },
            { ox + 440*scale, oy + (-474)*scale },
            { ox + 440*scale, oy + (-800)*scale },
            { ox + 520*scale, oy + (-800)*scale },
            { ox + 520*scale, oy + (-474)*scale },
            { ox + 624*scale, oy + (-578)*scale },
            { ox + 680*scale, oy + (-520)*scale },
        };
        arrow.AddPolygon(pts, 9);
        g.FillPath(&iconBrush, &arrow);
    }

    // Tray portion
    {
        Gdiplus::GraphicsPath tray;
        Gdiplus::PointF pts[] = {
            { ox + 160*scale, oy + (-240)*scale },
            { ox + 160*scale, oy + (-360)*scale },
            { ox + 240*scale, oy + (-360)*scale },
            { ox + 240*scale, oy + (-280)*scale },
            { ox + 720*scale, oy + (-280)*scale },
            { ox + 720*scale, oy + (-360)*scale },
            { ox + 800*scale, oy + (-360)*scale },
            { ox + 800*scale, oy + (-240)*scale },
            { ox + 720*scale, oy + (-160)*scale },
            { ox + 240*scale, oy + (-160)*scale },
        };
        tray.AddPolygon(pts, 10);
        g.FillPath(&iconBrush, &tray);
    }

    out_icon_rect = bg;
}

artwork_ui_element::artwork_ui_element(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback)
    : m_callback(callback), m_artwork_image(nullptr), m_artwork_stream(nullptr), m_infobar_stream(nullptr),
      m_artwork_loading(false), m_gdiplus_token(0), m_playback_callback(this),
      m_show_osd(true), m_osd_start_time(0), m_osd_slide_offset(OSD_SLIDE_DISTANCE),
      m_osd_timer_id(0), m_osd_visible(false), m_has_delayed_metadata(false), m_was_playing(false),
      m_mouse_hovering(false), m_hover_over_download(false), m_download_icon_rect{},
      m_download_fade_alpha(0), m_download_fade_timer_id(0) {
    
    
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdiplusStartupInput, NULL);
    
    // Subscribe to artwork events for proper API fallback
    subscribe_to_artwork_events(this);
    g_dui_artwork_panels.add_item(this);
    
    // Start playback monitoring timer if clear panel option is enabled
    // Note: We'll start the timer after window creation
    
    SetRect(&m_client_rect, 0, 0, 0, 0);
    
    // Initialize current playback state
    static_api_ptr_t<playback_control> pc;
    m_was_playing = pc->is_playing();
}

artwork_ui_element::~artwork_ui_element() {
    g_dui_artwork_panels.remove_item(this);
    // Unsubscribe from artwork events
    unsubscribe_from_artwork_events(this);

    cleanup_gdiplus_image();
    cleanup_gdiplus_infobar_image();

    if (m_gdiplus_token) {
        Gdiplus::GdiplusShutdown(m_gdiplus_token);
    }
}

// Re-trigger artwork lookup on all DUI panels using the now-playing track
void refresh_all_dui_artwork_panels() {
    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr track;
    if (!pc->get_now_playing(track) || !track.is_valid()) return;

    for (t_size i = 0; i < g_dui_artwork_panels.get_count(); i++) {
        artwork_ui_element* panel = g_dui_artwork_panels[i];
        if (panel && panel->m_hWnd) {
            panel->on_playback_new_track(track);
        }
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
    if (p_what == ui_element_notify_colors_changed || p_what == ui_element_notify_font_changed) {
        // We use global colors and fonts - trigger a repaint whenever these change
        if (IsWindow()) {
            Invalidate();
        }
    }
}


LRESULT artwork_ui_element::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (m_download_fade_timer_id) {
        KillTimer(1002);
        m_download_fade_timer_id = 0;
    }
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
    
    // Draw download icon overlay when hovering (only when a track is playing, skip streams)
    if (m_mouse_hovering || m_download_fade_alpha > 0) {
        bool should_show = false;
        if (m_mouse_hovering) {
            static_api_ptr_t<playback_control> pc;
            if (pc->is_playing()) {
                bool is_stream = false;
                if (m_current_track.is_valid()) {
                    pfc::string8 path = m_current_track->get_path();
                    is_stream = strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://");
                }
                should_show = !is_stream;
            }
        }
        if (should_show) {
            // Fade in: jump to full opacity and stop any fade timer
            if (m_download_fade_alpha < 255) {
                m_download_fade_alpha = 255;
                if (m_download_fade_timer_id) {
                    KillTimer(m_download_fade_timer_id);
                    m_download_fade_timer_id = 0;
                }
            }
            draw_download_icon(memDC, client_rect, m_hover_over_download, m_download_icon_rect, m_download_fade_alpha);
        } else if (m_download_fade_alpha > 0) {
            // Fading out - draw at current fade alpha
            draw_download_icon(memDC, client_rect, false, m_download_icon_rect, m_download_fade_alpha);
        } else {
            SetRectEmpty(&m_download_icon_rect);
        }
    }

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

LRESULT artwork_ui_element::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Start the clear panel monitoring timer if enabled
    update_clear_panel_timer();
    
    bHandled = FALSE;  // Let default processing continue
    return 0;
}

LRESULT artwork_ui_element::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    return TRUE; // We handle all drawing in OnPaint
}

LRESULT artwork_ui_element::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (!m_mouse_hovering) {
        m_mouse_hovering = true;
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&tme);
        Invalidate(FALSE);
    }
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    bool over = !IsRectEmpty(&m_download_icon_rect) &&
                PtInRect(&m_download_icon_rect, pt);
    if (over != m_hover_over_download) {
        m_hover_over_download = over;
        InvalidateRect(&m_download_icon_rect, FALSE);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT artwork_ui_element::OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    m_mouse_hovering = false;
    m_hover_over_download = false;
    // Start fade-out animation if icon was visible
    if (m_download_fade_alpha > 0 && !m_download_fade_timer_id) {
        m_download_fade_timer_id = SetTimer(1002, 16);  // ~60 FPS
    }
    if (IsRectEmpty(&m_download_icon_rect)) {
        Invalidate(FALSE);
    }
    bHandled = TRUE;
    return 0;
}

LRESULT artwork_ui_element::OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    if (LOWORD(lParam) == HTCLIENT && m_hover_over_download) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        bHandled = TRUE;
        return TRUE;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT artwork_ui_element::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (!IsRectEmpty(&m_download_icon_rect) && PtInRect(&m_download_icon_rect, pt)) {
        typedef void (*pfn_open)(const char*, const char*, const char*);
        HMODULE hGrab = GetModuleHandle(L"foo_artgrab.dll");
        pfn_open pOpen = hGrab ? (pfn_open)GetProcAddress(hGrab, "foo_artgrab_open") : nullptr;
        if (pOpen && m_current_track.is_valid()) {
            std::string artist, album;
            int len = WideCharToMultiByte(CP_UTF8, 0, m_infobar_artist.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) { artist.resize(len - 1); WideCharToMultiByte(CP_UTF8, 0, m_infobar_artist.c_str(), -1, artist.data(), len, nullptr, nullptr); }
            len = WideCharToMultiByte(CP_UTF8, 0, m_infobar_album.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) { album.resize(len - 1); WideCharToMultiByte(CP_UTF8, 0, m_infobar_album.c_str(), -1, album.data(), len, nullptr, nullptr); }
            pfc::string8 file_path = m_current_track->get_path();
            pOpen(artist.c_str(), album.c_str(), file_path.get_ptr());
        }
        bHandled = TRUE;
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT artwork_ui_element::OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Open artwork viewer popup on double-click
    if (m_artwork_image) {
        try {
            // Create source info string
            std::string source_info = m_artwork_source;
            if (source_info.empty()) {
                source_info = "Local file"; // Default assumption for unknown source
            }
            
            // Create and show the popup viewer
            ArtworkViewerPopup* popup = new ArtworkViewerPopup(m_artwork_image, source_info);
            if (popup) {
                popup->ShowPopup(m_hWnd);
                // Note: The popup will delete itself when closed
            }
        } catch (...) {
            // Handle any errors silently
        }
    }
    
    bHandled = TRUE;
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
    if (wParam == 1002) {
        // Download icon fade-out animation
        const BYTE fade_step = 20;  // ~200ms total fade at 60fps
        if (m_download_fade_alpha <= fade_step) {
            m_download_fade_alpha = 0;
            KillTimer(1002);
            m_download_fade_timer_id = 0;
            SetRectEmpty(&m_download_icon_rect);
        } else {
            m_download_fade_alpha -= fade_step;
        }
        Invalidate(FALSE);
        bHandled = TRUE;
        return 0;
    } else if (wParam == m_osd_timer_id) {
        // OSD animation timer
        update_osd_animation();
        bHandled = TRUE;
        return 0;
    } else if (wParam == 100) {
        // Metadata arrival timer - no metadata received within grace period
        KillTimer(100);
        
        // CHECK: Only trigger fallback if we don't already have tagged artwork
        if (m_artwork_image && !m_artwork_source.empty() && m_artwork_source == "Local artwork") {
            bHandled = TRUE;
            return 0;
        }
        
        // Skip API search for streams with no metadata - go directly to station logo fallback
        if (m_current_track.is_valid() && is_internet_stream(m_current_track)) {
            // Simulate ARTWORK_FAILED to trigger station logo fallback without API search
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_FAILED, 
                nullptr, 
                "No metadata - skipped API search", 
                "", 
                ""
            ));
        }
        
        bHandled = TRUE;
        return 0;
    } else if (wParam == 101) {
        // Timer for delayed artwork search on radio streams
        KillTimer(101);
        start_delayed_search();
        bHandled = TRUE;
        return 0;
    } else if (wParam == 102) {
        // Timer ID 102 - playback state monitoring for clear panel functionality
        static_api_ptr_t<playback_control> pc;
        bool is_playing = pc->is_playing();
        
        // If we were playing but now we're not, handle clear panel functionality
        if (m_was_playing && !is_playing && cfg_clear_panel_when_not_playing) {
            if (cfg_use_noart_image) {
                // Load and display noart image instead of clearing
                load_noart_image();
            } else {
                // Just clear the panel
                clear_artwork();
            }
        }
        
        // Update the previous state
        m_was_playing = is_playing;
        
        // If option is disabled, stop the timer
        if (!cfg_clear_panel_when_not_playing) {
            KillTimer(102);
        }
        
        bHandled = TRUE;
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

void artwork_ui_element::on_playback_new_track(metadb_handle_ptr track) {

    //Set counter to 1 to define first run
    m_counter++;

    // Check if it's an internet stream and custom logos enabled
    if (is_internet_stream(track) && cfg_enable_custom_logos) {
        pfc::string8 path = track->get_path();
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

    //try to get infobar values
    try {
        const file_info& info = track->get_info_ref()->info();
        std::string artist, title, album, station;

        if (info.meta_get("ARTIST", 0)) {
            artist = info.meta_get("ARTIST", 0);
        }
        if (info.meta_get("TITLE", 0)) {
            title = info.meta_get("TITLE", 0);
        }

        if (info.meta_get("ALBUM", 0)) {
            album = info.meta_get("ALBUM", 0);
        }

        if (info.meta_get("STREAM_NAME", 0)) {
            station = info.meta_get("STREAM_NAME", 0);
        }


        //store metadata for infobar

        m_infobar_artist = stringToWstring(artist);
        m_infobar_title = stringToWstring(title);
        m_infobar_album = stringToWstring(album);
        m_infobar_station = stringToWstring(station);

        //clear infobar logo
        cleanup_gdiplus_infobar_image();

    }
    catch (...) {
        // Handle any errors silently
    }


    m_current_track = track;
    m_artwork_loading = true;
    
    // Clear any pending delayed metadata from previous track
    m_has_delayed_metadata = false;
    m_delayed_artist.clear();
    m_delayed_title.clear();
    
    // Keep previous artwork visible until replaced (like CUI)
    // Don't clear artwork here - let new artwork replace it when found
    
    if (track.is_valid()) {
        
        bool get_all_album_art_manager = true;

        if (get_all_album_art_manager) {
            // Get any art for all cases via album_art_manager_v2 , for radio display logo on startup
            // Clear any existing artwork first to avoid conflicts
            cleanup_gdiplus_image();
            m_artwork_loading = true;
            
            artwork_manager::get_artwork_async(track, [this, track](const artwork_manager::artwork_result& result) {
                auto* heap_result = new artwork_manager::artwork_result(result);
                ::PostMessage(m_hWnd, WM_USER_ARTWORK_LOADED, 0, reinterpret_cast<LPARAM>(heap_result));
            });
        } else {
            
        //Don't search yet,wait for the first on_dynamic_info_track to get called
        //Fixes wrong first image with radio
        // Use minimal delay for internet streams
        
        }
    }
}

// HELPER Inverted internet stream detection
bool artwork_ui_element::is_inverted_internet_stream(metadb_handle_ptr track, const file_info& p_info) {
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
void artwork_ui_element::on_dynamic_info_track(const file_info& p_info) {
    try {
        // Get artist and track from the updated info safely
        const char* artist_ptr = p_info.meta_get("ARTIST", 0);
        const char* track_ptr = p_info.meta_get("TITLE", 0);
        const char* album_ptr = p_info.meta_get("ALBUM", 0);
        const char* station_ptr = p_info.meta_get("STREAM_NAME", 0);															
        
        pfc::string8 artist = artist_ptr ? artist_ptr : "";
        pfc::string8 track = track_ptr ? track_ptr : "";
        pfc::string8 album = album_ptr ? album_ptr : "";
        pfc::string8 station = station_ptr ? station_ptr : "";		

                
        // Extract only the first artist for better artwork search results
        std::string first_artist = MetadataCleaner::extract_first_artist(artist.c_str());
        
        // Clean metadata using the unified UTF-8 safe cleaner
        std::string cleaned_artist = MetadataCleaner::clean_for_search(first_artist.c_str(), true);
        std::string cleaned_track = MetadataCleaner::clean_for_search(track.c_str(), true);
        
		//If inverted swap artist title
        bool is_inverted_stream = is_inverted_internet_stream(m_current_track,p_info);

        if (is_inverted_stream) {
            std::string clean_artist_old = cleaned_artist;
            std::string clean_title_old = cleaned_track;
            cleaned_artist = clean_title_old;
            cleaned_track = clean_artist_old;
        }  

        //If no artist and title is "artist - title" (eg https://stream.radioclub80.cl:8022/retro80.opus)  split 
        if (cleaned_artist.empty()) {
            std::string delimiter = " - ";
            size_t pos = cleaned_track.find(delimiter);
            if (pos != std::string::npos) {
                std::string lvalue = cleaned_track.substr(0, pos);
                std::string rvalue = cleaned_track.substr(pos + delimiter.length());
                cleaned_artist = lvalue;
                cleaned_track = rvalue;
            }

            //or "artist ˗ title" (eg https ://energybasel.ice.infomaniak.ch/energybasel-high.mp3)

            std::string delimiter2 = " ˗ ";
            size_t pos2 = cleaned_track.find(delimiter2);
            if (pos2 != std::string::npos) {
                std::string lvalue = cleaned_track.substr(0, pos2);
                std::string rvalue = cleaned_track.substr(pos2 + delimiter2.length());
                cleaned_artist = lvalue;
                cleaned_track = rvalue;
            }

            //or "artist / title" (eg https://radiostream.pl/tuba8-1.mp3?cache=1650763965 )

            std::string delimiter3 = " / ";
            size_t pos3 = cleaned_track.find(delimiter3);
            if (pos3 != std::string::npos) {
                std::string lvalue = cleaned_track.substr(0, pos3);
                std::string rvalue = cleaned_track.substr(pos3 + delimiter3.length());
                cleaned_artist = lvalue;
                cleaned_track = rvalue;
            }

        }
            //with inverted (eg https://icy.unitedradio.it/um049.mp3?inverted )
            else if (cleaned_track.empty() && is_inverted_stream) {
                std::string delimiter = " - ";
                size_t pos = cleaned_artist.find(delimiter);
                if (pos != std::string::npos) {
                    std::string lvalue = cleaned_artist.substr(0, pos);
                    std::string rvalue = cleaned_artist.substr(pos + delimiter.length());
                    cleaned_artist = rvalue;
                    cleaned_track =  lvalue;
                }
            else {
                //do nothing
            }
        }  

		        //WalmRadio
        //If no title and artist is "title by artist" (eg https://icecast.walmradio.com:8443/classic)  split 
        if (cleaned_track.empty()) {

            //not use extract_first_artist , clean again
            cleaned_artist = MetadataCleaner::clean_for_search(artist.c_str(), true);
           
            std::string delimiter = " by ";
            size_t pos = cleaned_artist.find(delimiter);
            if (pos != std::string::npos) {
                std::string lvalue = cleaned_artist.substr(0, pos);
                std::string rvalue = cleaned_artist.substr(pos + delimiter.length());
                cleaned_artist = rvalue;
                cleaned_track = lvalue;
            }
        }	   
        //Don't search if received same artist title
        //same info - don't search - stop
        if (m_dinfo_artist == cleaned_artist && m_dinfo_title == cleaned_track) {
            return;
        }
        else {
            //differnet info - save new info and continue
            m_dinfo_artist = cleaned_artist;
            m_dinfo_title = cleaned_track;
        }
        
        // Apply comprehensive metadata validation rules
        bool is_valid_metadata = MetadataCleaner::is_valid_for_search(cleaned_artist.c_str(), cleaned_track.c_str());


        //Infobar
        //store metadata for infobar
        m_infobar_artist = stringToWstring(cleaned_artist);
        m_infobar_title = stringToWstring(cleaned_track);
        m_infobar_album = stringToWstring(album.c_str());
        m_infobar_station = stringToWstring(station.c_str());
        // Invalidate to trigger repaint with new metadata
        Invalidate();

				   
        if (is_valid_metadata) {

            // Cancel metadata arrival timer since we got valid metadata (like CUI)
            KillTimer(100);
     
            // Keep previous artwork visible until replaced (like CUI)
            // Don't clear artwork here - let new artwork replace it when found
            
            // PRIORITY CHECK: Disabled for local files to allow tagged artwork display
            // Local artwork will be loaded via the artwork_manager in on_playback_new_track()
            // if (should_prefer_local_artwork()) {
            //     if (load_local_artwork_from_main_component()) {
            //         return; // Exit early - local artwork found and loaded, don't do API search
            //     }
            // }
            
            // FIXED: Removed premature station logo check - station logos should be fallbacks only
            // API searches have priority according to README.md fallback chain
            
            // For internet streams, start search with minimal delay
            if (m_current_track.is_valid() && is_internet_stream(m_current_track)) {
                // Store metadata for delayed search - use minimal delay
                m_has_delayed_metadata = true;
                m_delayed_artist = cleaned_artist;
                m_delayed_title = cleaned_track;
                // Set Timer 101 to fire after minimal delay
                SetTimer(101, 500);
            }
            // For local files, do nothing - local artwork will be handled elsewhere
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
        // PRIORITY CHECK: Disabled for local files to allow tagged artwork display  
        // Local artwork will be loaded via the artwork_manager in on_playback_new_track()
        // if (should_prefer_local_artwork()) {
        //     if (load_local_artwork_from_main_component()) {
        //         return; // Exit early - local artwork found and loaded, don't do API search
        //     }
        // }
        
        // FIXED: Removed second premature station logo check
        // Station logos should only be used as fallback after API search fails
        
        // Only trigger API searches for internet streams, never for local files
        if (is_internet_stream(m_current_track)) {
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
        } else {
            // Not an internet stream - no API search triggered
        }
        // For local files, do nothing - local artwork will be handled elsewhere
    } catch (...) {
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
            
            // IMPORTANT: Kill all fallback timers since we found tagged artwork
            KillTimer(100); // Metadata arrival timer
            // Don't kill timer on_playback_new_track or first delayed search gets killed.
            // After that kill all late searches.
            if (m_counter > 1 ) KillTimer(101); // Delay timer
            
            // Show OSD animation for Default UI (only for online sources, not local files)
            if (source != "Local file" && !source.empty() && source != "Cache") {
                show_osd("Artwork from " + source);
            } else if (source == "Local artwork") {
                show_osd("Tagged artwork");
            }
            
            // Also notify event system for CUI panels
            ArtworkEventManager::get().notify(ArtworkEvent(
                ArtworkEventType::ARTWORK_LOADED, 
                nullptr,  // UI element doesn't have HBITMAP, CUI panel will get its own copy
                source, 
                "",  // Artist info not available in UI element context
                ""   // Title info not available in UI element context  
            ));
            
            //Add result metadata to infobar
            m_infobar_result = L"Artwork Source: " + stringToWstring(m_artwork_source) + L" [ " + m_infobar_artist + L" / " + m_infobar_title  + L" ] ";
            
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
        // Tagged artwork search failed - implement proper fallback chain
        
        // For YouTube streams with metadata, try API search first before station logos
        if (m_current_track.is_valid() && is_youtube_stream(m_current_track)) {
            // Check if we have metadata for API search
            metadb_info_container::ptr info_container = m_current_track->get_info_ref();
            if (info_container.is_valid()) {
                const file_info& info = info_container->info();
                
                const char* artist_ptr = info.meta_get("ARTIST", 0);
                const char* title_ptr = info.meta_get("TITLE", 0);
                
                if (artist_ptr && title_ptr && strlen(artist_ptr) > 0 && strlen(title_ptr) > 0) {
                    // We have valid metadata - trigger API search before station logos
                    pfc::string8 artist = artist_ptr;
                    pfc::string8 title = title_ptr;
                    
                    // Clean metadata for search
                    std::string cleaned_artist = MetadataCleaner::clean_for_search(artist.c_str(), true);
                    std::string cleaned_title = MetadataCleaner::clean_for_search(title.c_str(), true);
                    
                    // Validate metadata
                    if (MetadataCleaner::is_valid_for_search(cleaned_artist.c_str(), cleaned_title.c_str())) {
                        // Clear any existing artwork before API search
                        cleanup_gdiplus_image();
                        m_artwork_source = "Loading from API...";
                        m_artwork_loading = true;
                        
                        // Trigger API search for YouTube stream with failed embedded artwork
                        trigger_main_component_search_with_metadata(cleaned_artist, cleaned_title);
                        
                        // Exit early - API search will handle fallbacks if it fails
                        return;
                    }
                }
            }
        }
        
        // If we get here, either:
        // 1. Not a YouTube stream, OR
        // 2. YouTube stream with no valid metadata, OR  
        // 3. Regular radio stream with failed local search
        // Try fallback images (station logos, then no-art)
        
        bool fallback_loaded = false;
        
        // Only try fallback images for internet streams
        if (m_current_track.is_valid() && is_internet_stream(m_current_track) && cfg_enable_custom_logos) {
            // Priority 1: Station logo (e.g., ice1.somafm.com_indiepop-128-aac.png, somafm.com.png)
            if (!fallback_loaded) {
                cleanup_gdiplus_image();
                
                // Try loading directly as GDI+ bitmap to preserve alpha
                m_artwork_image = load_station_logo_gdiplus(m_current_track);
                
                if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                    m_artwork_loading = false;
                    m_artwork_source = "Station logo";
                    fallback_loaded = true;
                    Invalidate();
                } else {
                    cleanup_gdiplus_image();
                    
                    // Fallback to HBITMAP method
                    HBITMAP logo_bitmap = load_station_logo(m_current_track);
                    if (logo_bitmap) {
                        try {
                            m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(logo_bitmap, NULL);
                            if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                                m_artwork_loading = false;
                                m_artwork_source = "Station logo";
                                fallback_loaded = true;
                                Invalidate();
                            } else {
                                cleanup_gdiplus_image();
                            }
                        } catch (...) {
                            cleanup_gdiplus_image();
                        }
                        DeleteObject(logo_bitmap);
                    }
                }
            }
            
            // Priority 2: Station-specific fallback with full path (e.g., ice1.somafm.com_indiepop-128-aac-noart.png)
            if (!fallback_loaded) {
                auto noart_bitmap = load_noart_logo_gdiplus(m_current_track);
                if (noart_bitmap) {
                    cleanup_gdiplus_image();
                    m_artwork_image = noart_bitmap.release();
                    if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                        m_artwork_loading = false;
                        m_artwork_source = "Station fallback (no artwork)";
                        fallback_loaded = true;
                        Invalidate();
                    } else {
                        cleanup_gdiplus_image();
                    }
                }
            }
            
            // Priority 3: Generic fallback with URL support (e.g., somafm.com-noart.png or noart.png)
            if (!fallback_loaded) {
                auto generic_bitmap = load_generic_noart_logo_gdiplus();
                if (generic_bitmap) {
                    cleanup_gdiplus_image();
                    m_artwork_image = generic_bitmap.release();
                    if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                        m_artwork_loading = false;
                        m_artwork_source = "Generic fallback (no artwork)";
                        fallback_loaded = true;
                        Invalidate();
                    } else {
                        cleanup_gdiplus_image();
                    }
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

void artwork_ui_element::clear_infobar() {
    m_infobar_artist.clear();
    m_infobar_title.clear();
    m_infobar_album.clear();
    m_infobar_station.clear();
    m_infobar_result.clear();
    cleanup_gdiplus_infobar_image();
}

void artwork_ui_element::clear_dinfo() {
    m_dinfo_artist.clear();
    m_dinfo_title.clear();
}

void artwork_ui_element::reset_m_counter() {
    m_counter = 0;

}

void artwork_ui_element::draw_artwork(HDC hdc, const RECT& rect) {
    // Create memory DC for double buffering (like v1.3.1)
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bitmap = CreateCompatibleBitmap(hdc, m_client_rect.right, m_client_rect.bottom);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, mem_bitmap);
    
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
    
    // Check if image has alpha channel before filling background
    bool has_alpha = false;
    if (m_artwork_image) {
        Gdiplus::PixelFormat format = m_artwork_image->GetPixelFormat();
        has_alpha = (format == PixelFormat32bppARGB);
    }
    
    // Fill background (simpler approach)
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    FillRect(mem_dc, &m_client_rect, bg_brush);
    DeleteObject(bg_brush);
    
    if (m_artwork_image) {
        // Check if we have alpha channel
        Gdiplus::PixelFormat format = m_artwork_image->GetPixelFormat();
        bool has_alpha = (format == PixelFormat32bppARGB);
        
        if (has_alpha) {
            // First, fill the background with the proper theme color
            HBRUSH bg_brush = CreateSolidBrush(bg_color);
            FillRect(hdc, &rect, bg_brush);
            DeleteObject(bg_brush);
            
            // For transparent images, draw directly to the window DC to preserve alpha
            Gdiplus::Graphics direct_graphics(hdc);
            direct_graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            direct_graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            direct_graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            direct_graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            
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
                    draw_x = rect.left;
                    draw_y = rect.top + (client_height - draw_height) / 2;
                } else {
                    // Image is taller than client area
                    draw_width = (int)(client_height * img_aspect);
                    draw_height = client_height;
                    draw_x = rect.left + (client_width - draw_width) / 2;
                    draw_y = rect.top;
                }
                
                Gdiplus::Rect dest_rect(draw_x, draw_y, draw_width, draw_height);
                direct_graphics.DrawImage(m_artwork_image, dest_rect);
            }
            
            // Skip the memory DC approach for transparent images
            SelectObject(mem_dc, old_bitmap);
            DeleteObject(mem_bitmap);
            DeleteDC(mem_dc);
            return;
        } else {
            // For non-transparent images, use memory DC as before
            Gdiplus::Graphics graphics(mem_dc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

            
            // Calculate aspect ratio preserving rectangle
            UINT img_width = m_artwork_image->GetWidth();
            UINT img_height = m_artwork_image->GetHeight();
            
            int client_width;
            int client_height;
            int infobar_height;
            int infobar_text_height;
            

            if (cfg_infobar) {
                 client_width = m_client_rect.right - m_client_rect.left;
                 client_height = ((m_client_rect.bottom - m_client_rect.top) / 5 ) * 4;
                 infobar_height = (m_client_rect.bottom - m_client_rect.top) / 5;
                 infobar_text_height = infobar_height / 6;
            }
            else {
                 client_width = m_client_rect.right - m_client_rect.left;
                 client_height = m_client_rect.bottom - m_client_rect.top;

            }
            
            
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
			         
                
                
                if (cfg_infobar) {
                    
                    SolidBrush solidBrush(Color(20, 80, 80, 80));
                    Gdiplus::Rect dest_rect3(0, client_height, client_width, infobar_height);
                    graphics.FillRectangle(&solidBrush, dest_rect3);
                
                   
                    auto m_infobar_image_fallback = load_noart_logo_gdiplus(m_current_track);
                    auto m_infobar_image_fallback2 = load_generic_noart_logo_gdiplus();

                    if (load_station_logo_gdiplus(m_current_track)) {
                        m_infobar_image = load_station_logo_gdiplus(m_current_track);
                    }
                    

                    if (m_infobar_bitmap) {
                        Gdiplus::Rect dest_rect2(10, client_height + 10, infobar_height - 20, infobar_height - 20);
                        graphics.DrawImage(m_infobar_bitmap, dest_rect2);
                    }
                    else if (m_infobar_image) {
                        Gdiplus::Rect dest_rect2(10, client_height + 10, infobar_height - 20, infobar_height - 20);
                        graphics.DrawImage(m_infobar_image, dest_rect2);
                        delete m_infobar_image;
                        m_infobar_image = nullptr;
                    }
                    else if (m_infobar_image_fallback) {
                        m_infobar_image = m_infobar_image_fallback.release();
                        Gdiplus::Rect dest_rect2(10, client_height + 10, infobar_height - 20, infobar_height - 20);
                        graphics.DrawImage(m_infobar_image, dest_rect2);
                        delete m_infobar_image;
                        m_infobar_image = nullptr;
                    }
                    else if (m_infobar_image_fallback2) {
                        m_infobar_image = m_infobar_image_fallback2.release();
                        Gdiplus::Rect dest_rect2(10, client_height + 10, infobar_height - 20, infobar_height - 20);
                        graphics.DrawImage(m_infobar_image, dest_rect2);
                        delete m_infobar_image;
                        m_infobar_image = nullptr;
                    }

                    
                    // Get contrasting text color using DUI callback
                    COLORREF text_color = GetSysColor(COLOR_WINDOWTEXT); // Default fallback


                    if (m_callback.is_valid()) {
                        t_ui_color callback_text_color;
                        if (m_callback->query_color(ui_color_text, callback_text_color)) {
                            text_color = callback_text_color;
                        }
                        else {
                            text_color = m_callback->query_std_color(ui_color_text);
                        }
                    }

                    
                    FontFamily fontFamily(L"Segoe UI");
                    Font font(&fontFamily, 14, FontStyleRegular, UnitPixel);
                    SolidBrush brush(Color(255, GetRValue(text_color), GetGValue(text_color), GetBValue(text_color)));

                    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

                    graphics.DrawString(m_infobar_artist.c_str(), -1, &font, PointF(static_cast<float>(infobar_height), static_cast<float>(client_height + infobar_text_height/2)), &brush);
                    graphics.DrawString(m_infobar_title.c_str(), -1, &font, PointF(static_cast<float>(infobar_height), static_cast<float>(client_height + infobar_text_height * 2 - infobar_text_height / 2)), &brush);
                    graphics.DrawString(m_infobar_album.c_str(), -1, &font, PointF(static_cast<float>(infobar_height), static_cast<float>(client_height + infobar_text_height *3 - infobar_text_height/2)), &brush);
                    graphics.DrawString(m_infobar_station.c_str(), -1, &font, PointF(static_cast<float>(infobar_height), static_cast<float>(client_height + infobar_text_height *4)), &brush);
                    graphics.DrawString(m_infobar_result.c_str(), -1, &font, PointF(static_cast<float>(infobar_height), static_cast<float>(client_height + infobar_text_height *5)), &brush);
                }

                
              																							 
            }
        }
    } else if (m_artwork_loading) {
        // Show loading indicator (without text)
        draw_placeholder(mem_dc, m_client_rect);
    } else {
        // Show placeholder
        draw_placeholder(mem_dc, m_client_rect);
    }
    
    // Copy to main DC
    BitBlt(hdc, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
           mem_dc, rect.left, rect.top, SRCCOPY);
    
    // Cleanup
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(mem_bitmap);
    DeleteDC(mem_dc);
}

void artwork_ui_element::draw_placeholder(HDC hdc, const RECT& rect) {
    // Placeholder drawing disabled - just show clean background
    // No placeholder rectangle or icon needed
}

bool artwork_ui_element::load_image_from_memory(const t_uint8* data, size_t size) {
    
    cleanup_gdiplus_image();
    cleanup_gdiplus_infobar_image();

    // Try WIC-based WebP decoding first
    if (is_webp_signature(data, size)) {
        Gdiplus::Bitmap* webp_bitmap = decode_webp_via_wic(data, size);
        if (webp_bitmap) {
            m_artwork_image = webp_bitmap;

            // Create infobar bitmap clone for WebP (same as GDI+ path below)
            static_api_ptr_t<playback_control> pc;
            m_was_playing = pc->is_playing();
            if (!m_infobar_bitmap && cfg_infobar && m_was_playing && is_internet_stream(m_current_track)) {
                Gdiplus::Bitmap* infobar_bitmap = decode_webp_via_wic(data, size);
                if (infobar_bitmap && infobar_bitmap->GetLastStatus() == Gdiplus::Ok) {
                    m_infobar_bitmap = infobar_bitmap;
                } else {
                    delete infobar_bitmap;
                }
            }

            return true;
        }
        return false; // WebP detected but decoding failed (old Windows etc.)
    }

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
    // NOTE: GDI+ requires the stream to remain valid for the lifetime of the Image.
    // Store the stream and release it only when the image is cleaned up.
    m_artwork_image = Gdiplus::Image::FromStream(stream);

    if (!m_artwork_image || m_artwork_image->GetLastStatus() != Gdiplus::Ok) {
        stream->Release();
        cleanup_gdiplus_image();
        return false;
    }

    // Release old stream if any, then keep the new one alive
    if (m_artwork_stream) {
        m_artwork_stream->Release();
    }
    m_artwork_stream = stream;

    // Debug: Check pixel format to see if alpha channel is present
    if (m_artwork_image) {
        Gdiplus::Bitmap* bitmap = dynamic_cast<Gdiplus::Bitmap*>(m_artwork_image);
        if (bitmap) {
            Gdiplus::PixelFormat format = bitmap->GetPixelFormat();
            
            // Also check if it's specifically 32-bit ARGB
            if (format == PixelFormat32bppARGB) {
                // 32-bit ARGB with alpha channel
            } else if (format == PixelFormat32bppRGB) {
                // 32-bit RGB without alpha channel
            } else if (format == PixelFormat24bppRGB) {
                // 24-bit RGB
            }
        }
    }

    
    // infobar bitmap from local logo
    // clone it once on playback start
    static_api_ptr_t<playback_control> pc;
    m_was_playing = pc->is_playing();

    if (!m_infobar_bitmap && cfg_infobar && m_was_playing && is_internet_stream(m_current_track)) {
        
        // Create IStream from memory2
        
        HGLOBAL hGlobal2 = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hGlobal2) {
            return false;
        }

        void* pData2 = GlobalLock(hGlobal2);
        if (!pData2) {
            GlobalFree(hGlobal2);
            return false;
        }

        memcpy(pData2, data, size);
        GlobalUnlock(hGlobal2);

        IStream* stream2 = nullptr;
        if (CreateStreamOnHGlobal(hGlobal2, TRUE, &stream2) != S_OK) {
            GlobalFree(hGlobal2);
            return false;
        }
        //infobar
        // NOTE: GDI+ requires the stream to remain valid for the lifetime of the Bitmap.
        m_infobar_bitmap = Gdiplus::Bitmap::FromStream(stream2);

        if (!m_infobar_bitmap || m_infobar_bitmap->GetLastStatus() != Gdiplus::Ok) {
            stream2->Release();
            delete m_infobar_bitmap;
            m_infobar_bitmap = nullptr;
        } else {
            // Release old infobar stream if any, then keep the new one alive
            if (m_infobar_stream) {
                m_infobar_stream->Release();
            }
            m_infobar_stream = stream2;
        }
    }
    
    return true;
}

void artwork_ui_element::cleanup_gdiplus_infobar_image() {
    if (m_infobar_bitmap) {
        delete m_infobar_bitmap;
        m_infobar_bitmap = nullptr;
    }
    if (m_infobar_stream) {
        m_infobar_stream->Release();
        m_infobar_stream = nullptr;
    }
}

void artwork_ui_element::cleanup_gdiplus_image() {
    if (m_artwork_image) {
        delete m_artwork_image;
        m_artwork_image = nullptr;
    }
    if (m_artwork_stream) {
        m_artwork_stream->Release();
        m_artwork_stream = nullptr;
    }
}

void artwork_ui_element::show_osd(const std::string& text) {
    // Check global preference setting first (like CUI)
    if (!cfg_show_osd) return;
    
    // Then check local panel setting (like CUI)
    if (!m_show_osd) return;
    
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

void artwork_ui_element::update_clear_panel_timer() {
    if (!m_hWnd) return;
    
    if (cfg_clear_panel_when_not_playing) {
        // Start the timer if option is enabled
        SetTimer(102, 500);  // Timer ID 102, check every 0.5 seconds
    } else {
        // Stop the timer if option is disabled
        KillTimer(102);
    }
}

void artwork_ui_element::load_noart_image() {
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
            
            // Read file data and use load_image_from_memory for proper scaling
            HANDLE hFile = CreateFileA(noart_file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD file_size = GetFileSize(hFile, NULL);
                if (file_size > 0) {
                    std::vector<BYTE> file_data(file_size);
                    DWORD bytes_read;
                    if (ReadFile(hFile, file_data.data(), file_size, &bytes_read, NULL) && bytes_read == file_size) {
                        if (load_image_from_memory(file_data.data(), file_data.size())) {
                            m_artwork_source = "Noart image";
                            noart_loaded = true;
                        }
                    }
                }
                CloseHandle(hFile);
            }
        }
    }
    
    // Invalidate window to redraw with noart image or clear panel
    if (m_hWnd) {
        Invalidate();
    }
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

        const double length = track->get_length();

        // Check mtag file internet streams
        if (strstr(path.c_str(), "://")) {
            // Has protocol - check if it's a local file protocol and is mtag without duration
            if ((strstr(path.c_str(), "file://") == path.c_str()) && (strstr(path.c_str(), ".tags")) && (length <= 0)) {
                return true; // mtag internet stream
            }
        }

        // Check URL protocol first - this covers online playlists with duration
        if (strstr(path.c_str(), "://")) {
            // Has protocol - check if it's a local file protocol
            if (strstr(path.c_str(), "file://") == path.c_str()) {
                return false; // file:// protocol = local file
            }
            // Any other protocol (http://, https://, etc.) = internet stream
            return true;
        }
        
        // No protocol - check length as fallback for other stream types
        if (length > 0) {
            return false; // Has length but no protocol = local file
        }     
        
        return true; // No length, no protocol = likely internet stream
    } catch (...) {
        return false; // Error accessing path, assume local file
    }
}

bool artwork_ui_element::is_stream_with_possible_artwork(metadb_handle_ptr track) {
    if (!track.is_valid()) return false;
    
    try {
        pfc::string8 path = track->get_path();
        if (path.is_empty()) return false;
        
        // Check if this is a stream that can have embedded artwork
        // YouTube videos and similar services can have embedded thumbnails
        const char* path_str = path.c_str();
        
        // YouTube videos (can have embedded thumbnails)
        if (strstr(path_str, "youtube.com") || strstr(path_str, "youtu.be") || 
            strstr(path_str, "ytimg.com") || strstr(path_str, "googlevideo.com")) {
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
        if (strstr(path_str, ".m4a") || strstr(path_str, ".aac") || strstr(path_str, ".mp3")) {
            return true;
        }
        
        // For all other streams (like pure internet radio), assume no embedded artwork
        return false;
        
    } catch (...) {
        // On error, assume no embedded artwork to be safe
        return false;
    }
}

bool artwork_ui_element::is_youtube_stream(metadb_handle_ptr track) {
    if (!track.is_valid()) return false;
    
    try {
        pfc::string8 path = track->get_path();
        if (path.is_empty()) return false;
        
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

void artwork_ui_element::start_delayed_search() {
    // Only search for internet streams, never for local files
    if (!m_current_track.is_valid() || !is_internet_stream(m_current_track)) {
        return;
    }
    
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
    // Use the unified metadata validation (same as CUI mode)
    return MetadataCleaner::is_valid_for_search(artist, title);
}

std::string artwork_ui_element::clean_metadata_for_search(const char* metadata) {
    // Use the unified metadata cleaner (same as CUI mode)
    return MetadataCleaner::clean_for_search(metadata, true);
}

void artwork_ui_element::on_artwork_event(const ArtworkEvent& event) {
    if (!IsWindow()) return;

    // Marshal to the main thread via PostMessage to avoid modifying GDI+ state
    // from background threads. Heap-allocate a copy of the event data.
    auto* data = new ArtworkEventData();
    data->type = event.type;
    data->bitmap = event.bitmap;
    data->source = event.source;
    data->artist = event.artist;
    data->title = event.title;
    if (!PostMessage(WM_USER_ARTWORK_EVENT, 0, reinterpret_cast<LPARAM>(data))) {
        delete data; // PostMessage failed (window destroyed, etc.)
    }
}

LRESULT artwork_ui_element::OnArtworkEvent(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    auto* event = reinterpret_cast<ArtworkEventData*>(lParam);
    if (!event) {
        bHandled = TRUE;
        return 0;
    }

    // Take ownership — ensure we delete the heap-allocated data when done
    std::unique_ptr<ArtworkEventData> event_guard(event);

    switch (event->type) {
        case ArtworkEventType::ARTWORK_LOADED:
            {
            if (event->bitmap) {
                m_artwork_loading = false;

                // Convert HBITMAP directly to GDI+ Image (like the existing custom logo loading)
                cleanup_gdiplus_image();

                // Convert HBITMAP info
                BITMAP bm;
                GetObject(event->bitmap, sizeof(BITMAP), &bm);

                try {
                    m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(event->bitmap, NULL);

                    if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                        m_artwork_source = event->source;

                        // Show OSD for online sources (not local files)
                        if (!event->source.empty() && event->source != "Local file" && event->source != "Cache") {
                            show_osd("Artwork from " + event->source);
                        }

                        //Add result metadata to infobar
                        m_infobar_result = L"Artwork Source: " + stringToWstring(m_artwork_source) + L" [ " + m_infobar_artist + L" / " + m_infobar_title + +L" ] ";

                        Invalidate(); // Trigger repaint
                    } else {
                        cleanup_gdiplus_image();
                    }
                } catch (...) {
                    cleanup_gdiplus_image();
                }
            }
            }
            break;
            
        case ArtworkEventType::ARTWORK_LOADING:
            m_artwork_loading = true;
            break;
            
        case ArtworkEventType::ARTWORK_FAILED:
            {
                m_artwork_loading = false;
                
                // CHECK: Don't override existing tagged artwork with fallback images
                if (m_artwork_image && !m_artwork_source.empty() && m_artwork_source == "Local artwork") {
                    break;
                }
            
                // Try fallback images when API search fails (same logic as on_artwork_loaded)
                bool fallback_loaded = false;
                
                // Only try fallback images for internet streams
                if (m_current_track.is_valid() && is_internet_stream(m_current_track) && cfg_enable_custom_logos) {
                    // Priority 1: Station logo (e.g., ice1.somafm.com_indiepop-128-aac.png, somafm.com.png)
                    if (!fallback_loaded) {
                                        cleanup_gdiplus_image();
                        
                        // Try loading directly as GDI+ bitmap to preserve alpha
                        m_artwork_image = load_station_logo_gdiplus(m_current_track);
                        if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                            m_artwork_loading = false;
                            m_artwork_source = "Station logo";
                            fallback_loaded = true;
                            Invalidate();
                        } else {
                            cleanup_gdiplus_image();
                            
                            // Fallback to HBITMAP method
                            HBITMAP logo_bitmap = load_station_logo(m_current_track);
                            if (logo_bitmap) {
                                try {
                                    m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(logo_bitmap, NULL);
                                    if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                                        m_artwork_loading = false;
                                        m_artwork_source = "Station logo";
                                        fallback_loaded = true;
                                        Invalidate();
                                    } else {
                                        cleanup_gdiplus_image();
                                    }
                                } catch (...) {
                                    cleanup_gdiplus_image();
                                }
                                DeleteObject(logo_bitmap);
                            }
                        }
                    }
                    
                    // Priority 2: Station-specific fallback with full path (e.g., ice1.somafm.com_indiepop-128-aac-noart.png)
                    if (!fallback_loaded) {
                        auto noart_bitmap = load_noart_logo_gdiplus(m_current_track);
                        if (noart_bitmap) {
                            cleanup_gdiplus_image();
                            m_artwork_image = noart_bitmap.release();
                            if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                                m_artwork_loading = false;
                                m_artwork_source = "Station fallback (no artwork)";
                                fallback_loaded = true;
                                Invalidate();
                            } else {
                                cleanup_gdiplus_image();
                            }
                        }
                    }
                    
                    // Priority 3: Generic fallback with URL support (e.g., somafm.com-noart.png or noart.png)
                    if (!fallback_loaded) {
                        auto generic_bitmap = load_generic_noart_logo_gdiplus();
                        if (generic_bitmap) {
                            cleanup_gdiplus_image();
                            m_artwork_image = generic_bitmap.release();
                            if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
                                m_artwork_loading = false;
                                m_artwork_source = "Generic fallback (no artwork)";
                                fallback_loaded = true;
                                Invalidate();
                            } else {
                                cleanup_gdiplus_image();
                            }
                        }
                    }
                }
                break;
            }
            
        case ArtworkEventType::ARTWORK_CLEARED:
            break;
    }

    bHandled = TRUE;
    return 0;
}

// Local artwork priority checking functions
bool artwork_ui_element::should_prefer_local_artwork() {
    if (!m_current_track.is_valid()) return false;
    
    // Only check for local artwork if this is a local file
    if (is_internet_stream(m_current_track)) return false;
    
    // Check if main component already found local artwork
    HBITMAP main_bitmap = get_main_component_artwork_bitmap();
    return (main_bitmap != NULL);
}

bool artwork_ui_element::load_local_artwork_from_main_component() {
    HBITMAP main_bitmap = get_main_component_artwork_bitmap();
    if (!main_bitmap) return false;
    
    // If we already have artwork with a valid source (like "Deezer"), don't override it
    if (m_artwork_image && !m_artwork_source.empty() && m_artwork_source != "Unknown") {
        return false; // Keep existing artwork and source
    }
    
    // Convert HBITMAP to GDI+ Image for DUI display
    cleanup_gdiplus_image();
    
    try {
        m_artwork_image = Gdiplus::Bitmap::FromHBITMAP(main_bitmap, NULL);
        
        if (m_artwork_image && m_artwork_image->GetLastStatus() == Gdiplus::Ok) {
            m_artwork_loading = false;
            
            // Only set to "Local file" for truly local artwork
            m_artwork_source = "Local file";
            
            // No OSD for local files - they should load silently
            Invalidate(); // Trigger repaint
            return true;
        } else {
            cleanup_gdiplus_image();
        }
    } catch (...) {
        cleanup_gdiplus_image();
    }
    
    return false;
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
