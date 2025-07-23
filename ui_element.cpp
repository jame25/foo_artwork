#include "stdafx.h"
#include "artwork_manager.h"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

// For now, we'll create a simplified version without ATL dependencies
// This creates a basic component that can be extended later
/*
class artwork_ui_element : public ui_element_instance, public CWindowImpl<artwork_ui_element> {
public:
    artwork_ui_element(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback);
    ~artwork_ui_element();

    DECLARE_WND_CLASS_EX(L"foo_artwork_ui_element", 0, COLOR_BTNFACE);

    BEGIN_MSG_MAP_EX(artwork_ui_element)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_SIZE(OnSize)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_CONTEXTMENU(OnContextMenu)
    END_MSG_MAP()

    // ui_element_instance methods
    HWND get_wnd() override { return *this; }
    void set_configuration(stream_reader* p_reader, t_size p_size, abort_callback& p_abort) override {}
    void get_configuration(stream_writer* p_writer, abort_callback& p_abort) const override {}
    void initialize_window(HWND parent) override;
    void shutdown() override;
    void notify(const GUID& p_what, t_size p_param1, const void* p_param2, t_size p_param2size) override;

private:
    // Message handlers
    int OnCreate(LPCREATESTRUCT lpCreateStruct);
    void OnDestroy();
    void OnPaint(CDCHandle dc);
    void OnSize(UINT nType, CSize size);
    BOOL OnEraseBkgnd(CDCHandle dc);
    void OnContextMenu(CWindow wnd, CPoint point);

    // Artwork handling
    void on_playback_new_track(metadb_handle_ptr track);
    void on_artwork_loaded(const artwork_manager::artwork_result& result);
    void update_artwork_display();
    void clear_artwork();

    // Drawing
    void draw_artwork(HDC hdc, const RECT& rect);
    void draw_placeholder(HDC hdc, const RECT& rect);
    
    // GDI+ helpers
    bool load_image_from_memory(const t_uint8* data, size_t size);
    void cleanup_gdiplus_image();

    ui_element_instance_callback::ptr m_callback;
    
    // Artwork data
    Gdiplus::Image* m_artwork_image;
    metadb_handle_ptr m_current_track;
    bool m_artwork_loading;
    
    // UI state
    RECT m_client_rect;
    
    // GDI+ token
    ULONG_PTR m_gdiplus_token;
    
    // Playback callback
    class playback_callback_impl : public play_callback_static {
    public:
        playback_callback_impl(artwork_ui_element* parent) : m_parent(parent) {}
        
        void on_playback_new_track(metadb_handle_ptr p_track) override {
            if (m_parent) m_parent->on_playback_new_track(p_track);
        }
        
        void on_playback_stop(play_control::t_stop_reason p_reason) override {
            if (m_parent) m_parent->clear_artwork();
        }
        
        unsigned get_flags() override {
            return flag_on_playback_new_track | flag_on_playback_stop;
        }
        
        void set_parent(artwork_ui_element* parent) { m_parent = parent; }
        
    private:
        artwork_ui_element* m_parent;
    };
    
    playback_callback_impl m_playback_callback;
};

artwork_ui_element::artwork_ui_element(ui_element_config::ptr cfg, ui_element_instance_callback::ptr callback)
    : m_callback(callback), m_artwork_image(nullptr), m_artwork_loading(false), 
      m_gdiplus_token(0), m_playback_callback(this) {
    
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplus_token, &gdiplusStartupInput, NULL);
    
    SetRect(&m_client_rect, 0, 0, 0, 0);
}

artwork_ui_element::~artwork_ui_element() {
    cleanup_gdiplus_image();
    
    if (m_gdiplus_token) {
        Gdiplus::GdiplusShutdown(m_gdiplus_token);
    }
}

void artwork_ui_element::initialize_window(HWND parent) {
    Create(parent, NULL, NULL, WS_CHILD | WS_VISIBLE);
    
    // Register for playback callbacks
    m_playback_callback.set_parent(this);
    static_api_ptr_t<play_callback_manager>()->register_callback(&m_playback_callback, 
                                                                 play_callback::flag_on_playback_new_track | 
                                                                 play_callback::flag_on_playback_stop, 
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
    // Handle notifications if needed
}

int artwork_ui_element::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    return 0;
}

void artwork_ui_element::OnDestroy() {
    cleanup_gdiplus_image();
}

void artwork_ui_element::OnPaint(CDCHandle dc) {
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
    draw_artwork(memDC, &client_rect);
    
    // Copy the entire off-screen buffer to screen in one operation (flicker-free)
    BitBlt(hdc, 0, 0, client_rect.right, client_rect.bottom, memDC, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    
    EndPaint(&ps);
}

void artwork_ui_element::OnSize(UINT nType, CSize size) {
    GetClientRect(&m_client_rect);
    
    // Use RedrawWindow for flicker-free resizing instead of Invalidate()
    RedrawWindow(RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN);
}

BOOL artwork_ui_element::OnEraseBkgnd(CDCHandle dc) {
    return TRUE; // We handle all drawing in OnPaint
}

void artwork_ui_element::OnContextMenu(CWindow wnd, CPoint point) {
    // Could add context menu for artwork options
}

void artwork_ui_element::on_playback_new_track(metadb_handle_ptr track) {
    m_current_track = track;
    m_artwork_loading = true;
    
    // Clear current artwork
    cleanup_gdiplus_image();
    Invalidate();
    
    // Load new artwork asynchronously
    artwork_manager::get_artwork_async(track, [this](const artwork_manager::artwork_result& result) {
        // This callback runs in background thread, so we need to marshal to UI thread
        // For simplicity, we'll call the handler directly (in a real implementation, 
        // you'd want proper thread marshaling)
        on_artwork_loaded(result);
    });
}

void artwork_ui_element::on_artwork_loaded(const artwork_manager::artwork_result& result) {
    m_artwork_loading = false;
    
    if (result.success && result.data.get_size() > 0) {
        if (load_image_from_memory(result.data.get_ptr(), result.data.get_size())) {
            Invalidate();
        }
    } else {
        // No artwork found, show placeholder
        cleanup_gdiplus_image();
        Invalidate();
    }
}

void artwork_ui_element::clear_artwork() {
    m_current_track.release();
    cleanup_gdiplus_image();
    m_artwork_loading = false;
    Invalidate();
}

void artwork_ui_element::draw_artwork(HDC hdc, const RECT& rect) {
    // Fill background (hdc is already the memory DC from OnPaint)
    HBRUSH bg_brush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
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
        // Show loading indicator
        draw_placeholder(hdc, m_client_rect);
        
        HFONT font = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT old_font = (HFONT)SelectObject(hdc, font);
        
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        
        const char* loading_text = "Loading artwork...";
        RECT text_rect = m_client_rect;
        DrawTextA(hdc, loading_text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        SelectObject(hdc, old_font);
        DeleteObject(font);
    } else {
        // Show placeholder
        draw_placeholder(hdc, m_client_rect);
    }
}

void artwork_ui_element::draw_placeholder(HDC hdc, const RECT& rect) {
    // Draw a simple placeholder icon
    int center_x = (rect.left + rect.right) / 2;
    int center_y = (rect.top + rect.bottom) / 2;
    int size = min(rect.right - rect.left, rect.bottom - rect.top) / 4;
    
    HPEN pen = CreatePen(PS_SOLID, 2, GetSysColor(COLOR_BTNSHADOW));
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    
    // Draw musical note placeholder
    Rectangle(hdc, center_x - size/2, center_y - size/2, center_x + size/2, center_y + size/2);
    
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

bool artwork_ui_element::load_image_from_memory(const t_uint8* data, size_t size) {
    cleanup_gdiplus_image();
    
    // Create IStream from memory
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hGlobal) return false;
    
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

// UI Element factory
class ui_element_artwork : public ui_element_impl_withguid<artwork_ui_element> {
public:
    const char* get_description() { return "Artwork Display"; }
    
    GUID get_guid() {
        return GUID { 0x12345681, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };
    }
    
    GUID get_subclass() {
        return ui_element_subclass_media_display;
    }
};

static ui_element_factory<ui_element_artwork> g_ui_element_artwork_factory;
*/

// Placeholder implementation - component will load but without UI element for now
// This allows me to test the basic component loading without ATL dependencies
