#include "stdafx.h"
#include "artwork_viewer_popup.h"
#include <commdlg.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

// GUIDs for the configuration variables
static const GUID guid_artwork_viewer_window_x = { 0x12345601, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf1 } };
static const GUID guid_artwork_viewer_window_y = { 0x12345602, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf2 } };
static const GUID guid_artwork_viewer_window_width = { 0x12345603, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf3 } };
static const GUID guid_artwork_viewer_window_height = { 0x12345604, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf4 } };
static const GUID guid_artwork_viewer_window_maximized = { 0x12345605, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf5 } };

// foobar2000 configuration variables for window state persistence
static cfg_int cfg_viewer_window_x(guid_artwork_viewer_window_x, -1);
static cfg_int cfg_viewer_window_y(guid_artwork_viewer_window_y, -1);
static cfg_int cfg_viewer_window_width(guid_artwork_viewer_window_width, 800);
static cfg_int cfg_viewer_window_height(guid_artwork_viewer_window_height, 600);
static cfg_bool cfg_viewer_window_maximized(guid_artwork_viewer_window_maximized, false);

using namespace Gdiplus;

ArtworkViewerPopup::ArtworkViewerPopup(Gdiplus::Image* artwork_image, const std::string& source_info)
    : m_artwork_image(nullptr)
    , m_source_info(source_info)
    , m_fit_to_window(false)  // Default to original size when popup opens
    , m_fit_button(NULL)
    , m_save_button(NULL)
    , m_info_label(NULL)
    , m_parent_hwnd(NULL)
{
    SetRect(&m_client_rect, 0, 0, 0, 0);
    SetRect(&m_image_rect, 0, 0, 0, 0);
    
    // Clone the artwork image so we own it
    if (artwork_image) {
        m_artwork_image = std::unique_ptr<Gdiplus::Image>(artwork_image->Clone());
    }
    
    // Generate image info string
    m_image_info = GetImageInfo();
}

ArtworkViewerPopup::~ArtworkViewerPopup() {
    // Smart pointer will automatically clean up the image
}

void ArtworkViewerPopup::ShowPopup(HWND parent_hwnd) {
    if (!m_artwork_image) {
        return; // No image to show
    }
    
    // Store parent window for theme color retrieval
    m_parent_hwnd = parent_hwnd;
    
    // Calculate initial window size based on image
    UINT img_width = m_artwork_image->GetWidth();
    UINT img_height = m_artwork_image->GetHeight();
    
    // Limit maximum initial size to 80% of screen
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int max_width = (int)(screen_width * 0.8);
    int max_height = (int)(screen_height * 0.8);
    
    // Calculate window size maintaining aspect ratio
    int window_width = img_width;
    int window_height = img_height + CONTROL_HEIGHT + (CONTROL_MARGIN * 3); // Space for controls
    
    if (window_width > max_width) {
        double scale = (double)max_width / window_width;
        window_width = max_width;
        window_height = (int)(window_height * scale);
    }
    
    if (window_height > max_height) {
        double scale = (double)max_height / window_height;
        window_height = max_height;
        window_width = (int)(window_width * scale);
    }
    
    // Ensure minimum size
    if (window_width < 400) window_width = 400;
    if (window_height < 300) window_height = 300;
    
    // Create the window
    HWND hwnd = Create(parent_hwnd, CWindow::rcDefault, L"Artwork Viewer", 
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0);
    
    if (hwnd) {
        // Try to restore saved window state, otherwise use calculated size and center
        RECT saved_rect;
        bool was_maximized = false;
        if (GetSavedWindowRect(saved_rect, was_maximized)) {
            // Restore saved position and size first
            ::SetWindowPos(hwnd, HWND_TOP, saved_rect.left, saved_rect.top, 
                          saved_rect.right - saved_rect.left, 
                          saved_rect.bottom - saved_rect.top, 0);
            
            // Then maximize if it was maximized before
            if (was_maximized) {
                ShowWindow(SW_MAXIMIZE);
            }
        } else {
            // Use calculated size and center the window
            ::SetWindowPos(hwnd, HWND_TOP, 0, 0, window_width, window_height, SWP_NOMOVE);
            CenterWindow(parent_hwnd);
        }
        
        // Set focus
        SetFocus();
    }
}

LRESULT ArtworkViewerPopup::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Detect if we should use dark mode for title bar
    bool should_use_dark_titlebar = false;
    
    // Check Windows theme setting
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExA(hkey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            should_use_dark_titlebar = (value == 0); // 0 = dark mode, 1 = light mode
        }
        RegCloseKey(hkey);
    }
    
    // Also check foobar2000 background color as additional hint
    COLORREF bg_color = GetSysColor(COLOR_WINDOW);
    try {
        ui_config_manager::ptr ui_config = ui_config_manager::tryGet();
        if (ui_config.is_valid()) {
            t_ui_color fb2k_bg_color;
            if (ui_config->query_color(ui_color_background, fb2k_bg_color)) {
                bg_color = fb2k_bg_color;
            }
        }
    } catch (...) {
        // Ignore
    }
    
    // If foobar2000 background is dark, prefer dark title bar
    BYTE r = GetRValue(bg_color);
    BYTE g = GetGValue(bg_color);
    BYTE b = GetBValue(bg_color);
    int brightness = (r * 299 + g * 587 + b * 114) / 1000;
    if (brightness < 128) {
        should_use_dark_titlebar = true;
    }
    
    // Apply dark mode title bar if needed
    if (should_use_dark_titlebar) {
        BOOL dark_mode = TRUE;
        
        // Try Windows 11 method first
        DwmSetWindowAttribute(m_hWnd, 20, &dark_mode, sizeof(dark_mode)); // DWMWA_USE_IMMERSIVE_DARK_MODE
        
        // Fallback for Windows 10 build 18985+
        if (FAILED(DwmSetWindowAttribute(m_hWnd, 20, &dark_mode, sizeof(dark_mode)))) {
            DwmSetWindowAttribute(m_hWnd, 19, &dark_mode, sizeof(dark_mode)); // DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
        }
    }
    
    CreateControls();
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(&ps);
    
    // Use double buffering for smooth rendering
    RECT client_rect;
    GetClientRect(&client_rect);
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, client_rect.right, client_rect.bottom);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // Get proper foobar2000 background color using official API
    COLORREF bg_color = GetSysColor(COLOR_WINDOW); // Fallback
    
    // Try to get the proper foobar2000 UI background color
    try {
        ui_config_manager::ptr ui_config = ui_config_manager::tryGet();
        if (ui_config.is_valid()) {
            // Get the official foobar2000 background color
            t_ui_color fb2k_bg_color;
            if (ui_config->query_color(ui_color_background, fb2k_bg_color)) {
                bg_color = fb2k_bg_color;
            }
        }
    } catch (...) {
        // Fallback to system color if foobar2000 API unavailable
    }
    
    // Simple dark mode detection based on background color brightness
    BYTE r = GetRValue(bg_color);
    BYTE g = GetGValue(bg_color);
    BYTE b = GetBValue(bg_color);
    int brightness = (r * 299 + g * 587 + b * 114) / 1000;
    bool is_dark_mode = brightness < 128;
    
    // Adjust background slightly for better artwork contrast
    
    if (is_dark_mode) {
        // For dark mode, make it slightly lighter for contrast
        r = (BYTE)(r * 1.1 > 255 ? 255 : r * 1.1);
        g = (BYTE)(g * 1.1 > 255 ? 255 : g * 1.1);
        b = (BYTE)(b * 1.1 > 255 ? 255 : b * 1.1);
    } else {
        // For light mode, make it slightly darker for contrast
        r = (BYTE)(r * 0.95);
        g = (BYTE)(g * 0.95);
        b = (BYTE)(b * 0.95);
    }
    
    bg_color = RGB(r, g, b);
    
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    FillRect(memDC, &client_rect, bg_brush);
    DeleteObject(bg_brush);
    
    // Paint artwork and UI
    PaintArtwork(memDC);
    PaintUI(memDC);
    
    // Copy to screen
    BitBlt(hdc, 0, 0, client_rect.right, client_rect.bottom, memDC, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    
    EndPaint(&ps);
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    GetClientRect(&m_client_rect);
    UpdateLayout();
    CalculateImageRect();
    Invalidate();
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    bHandled = TRUE;
    return TRUE; // We handle all drawing in OnPaint
}

LRESULT ArtworkViewerPopup::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    switch (wParam) {
        case VK_ESCAPE:
            PostMessage(WM_CLOSE);
            break;
        case VK_F11:
        case VK_SPACE:
            ToggleFitMode();
            break;
        case 'S':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                SaveArtwork();
            }
            break;
    }
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // This is handled by WM_LBUTTONDBLCLK for double-clicks
    // Single clicks don't need special handling for now
    bHandled = FALSE; // Let default processing handle it
    return 0;
}

LRESULT ArtworkViewerPopup::OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    // Double-click on image area toggles fit mode
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (PtInRect(&m_image_rect, pt)) {
        ToggleFitMode();
    }
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    WORD cmd_id = LOWORD(wParam);
    
    switch (cmd_id) {
        case ID_FIT_BUTTON:
            ToggleFitMode();
            break;
        case ID_SAVE_BUTTON:
            SaveArtwork();
            break;
    }
    
    bHandled = TRUE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    HDC hdc = (HDC)wParam;
    HWND hwndControl = (HWND)lParam;
    
    // Only handle our info label
    if (hwndControl == m_info_label) {
        // Get foobar2000 background color to determine text color
        COLORREF bg_color = GetSysColor(COLOR_WINDOW);
        try {
            ui_config_manager::ptr ui_config = ui_config_manager::tryGet();
            if (ui_config.is_valid()) {
                t_ui_color fb2k_bg_color;
                if (ui_config->query_color(ui_color_background, fb2k_bg_color)) {
                    bg_color = fb2k_bg_color;
                }
            }
        } catch (...) {
            // Ignore
        }
        
        // Determine if we're in dark mode
        BYTE r = GetRValue(bg_color);
        BYTE g = GetGValue(bg_color);
        BYTE b = GetBValue(bg_color);
        int brightness = (r * 299 + g * 587 + b * 114) / 1000;
        bool is_dark_mode = brightness < 128;
        
        // Set text color based on theme
        if (is_dark_mode) {
            SetTextColor(hdc, RGB(255, 255, 255)); // White text for dark mode
        } else {
            SetTextColor(hdc, RGB(0, 0, 0)); // Black text for light mode
        }
        
        // Set transparent background
        SetBkMode(hdc, TRANSPARENT);
        
        bHandled = TRUE;
        return (LRESULT)GetStockObject(NULL_BRUSH); // Transparent background
    }
    
    bHandled = FALSE;
    return 0;
}

LRESULT ArtworkViewerPopup::OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
    SaveWindowState();
    DestroyWindow();
    bHandled = TRUE;
    return 0;
}

void ArtworkViewerPopup::PaintArtwork(HDC hdc) {
    if (!m_artwork_image) return;
    
    Graphics graphics(hdc);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    // Draw the image in the calculated rectangle
    Rect dest_rect(m_image_rect.left, m_image_rect.top, 
                  m_image_rect.right - m_image_rect.left, 
                  m_image_rect.bottom - m_image_rect.top);
    
    graphics.DrawImage(m_artwork_image.get(), dest_rect);
}

void ArtworkViewerPopup::PaintUI(HDC hdc) {
    // The controls will paint themselves, but we can add any additional UI here
    // such as borders or overlays if needed
}

void ArtworkViewerPopup::CalculateImageRect() {
    if (!m_artwork_image) return;
    
    UINT img_width = m_artwork_image->GetWidth();
    UINT img_height = m_artwork_image->GetHeight();
    
    // Calculate available space (excluding control area at bottom)
    int available_width = m_client_rect.right - m_client_rect.left;
    int available_height = m_client_rect.bottom - m_client_rect.top - CONTROL_HEIGHT - (CONTROL_MARGIN * 2);
    
    if (available_width <= 0 || available_height <= 0) return;
    
    int draw_width, draw_height, draw_x, draw_y;
    
    if (m_fit_to_window) {
        // Fit to window (maintain aspect ratio)
        double img_aspect = (double)img_width / img_height;
        double available_aspect = (double)available_width / available_height;
        
        if (img_aspect > available_aspect) {
            // Image is wider than available space
            draw_width = available_width;
            draw_height = (int)(available_width / img_aspect);
        } else {
            // Image is taller than available space
            draw_width = (int)(available_height * img_aspect);
            draw_height = available_height;
        }
        
        draw_x = (available_width - draw_width) / 2;
        draw_y = (available_height - draw_height) / 2;
    } else {
        // Show in original size (centered)
        draw_width = img_width;
        draw_height = img_height;
        draw_x = (available_width - draw_width) / 2;
        draw_y = (available_height - draw_height) / 2;
        
        // Clamp to available space
        if (draw_width > available_width) {
            draw_x = 0;
            draw_width = available_width;
        }
        if (draw_height > available_height) {
            draw_y = 0;
            draw_height = available_height;
        }
    }
    
    SetRect(&m_image_rect, draw_x, draw_y, draw_x + draw_width, draw_y + draw_height);
}

void ArtworkViewerPopup::UpdateLayout() {
    if (!m_fit_button || !m_save_button || !m_info_label) return;
    
    int client_width = m_client_rect.right - m_client_rect.left;
    int client_height = m_client_rect.bottom - m_client_rect.top;
    
    // Position controls at the bottom
    int control_y = client_height - CONTROL_HEIGHT - CONTROL_MARGIN;
    
    // Fit/Original button on the left
    ::SetWindowPos(m_fit_button, NULL, CONTROL_MARGIN, control_y, 
                  BUTTON_WIDTH, CONTROL_HEIGHT - 5, SWP_NOZORDER);
    
    // Save button on the right
    ::SetWindowPos(m_save_button, NULL, client_width - BUTTON_WIDTH - CONTROL_MARGIN, control_y,
                  BUTTON_WIDTH, CONTROL_HEIGHT - 5, SWP_NOZORDER);
    
    // Info label in the center
    int info_x = BUTTON_WIDTH + (CONTROL_MARGIN * 2);
    int info_width = client_width - (BUTTON_WIDTH * 2) - (CONTROL_MARGIN * 4);
    ::SetWindowPos(m_info_label, NULL, info_x, control_y + 5,
                  info_width, CONTROL_HEIGHT - 10, SWP_NOZORDER);
}

void ArtworkViewerPopup::ToggleFitMode() {
    m_fit_to_window = !m_fit_to_window;
    
    // Update button text
    if (m_fit_button) {
        SetWindowTextA(m_fit_button, m_fit_to_window ? "Show original size" : "Fit to window size");
    }
    
    CalculateImageRect();
    Invalidate();
}

void ArtworkViewerPopup::SaveArtwork() {
    if (!m_artwork_image) return;
    
    // Show save file dialog
    OPENFILENAMEA ofn = {};
    char file_path[MAX_PATH] = "artwork.png";
    
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "PNG Files (*.png)\0*.png\0JPEG Files (*.jpg)\0*.jpg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Save Artwork As";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    
    if (GetSaveFileNameA(&ofn)) {
        // Determine encoder based on file extension
        CLSID encoder_clsid;
        std::string ext = file_path;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext.find(".jpg") != std::string::npos || ext.find(".jpeg") != std::string::npos) {
            // JPEG encoder
            GetEncoderClsid(L"image/jpeg", &encoder_clsid);
        } else {
            // Default to PNG
            GetEncoderClsid(L"image/png", &encoder_clsid);
            // Ensure .png extension
            if (ext.find(".png") == std::string::npos) {
                strcat_s(file_path, ".png");
            }
        }
        
        // Convert to wide string for GDI+
        int wide_len = MultiByteToWideChar(CP_ACP, 0, file_path, -1, NULL, 0);
        wchar_t* wide_path = new wchar_t[wide_len];
        MultiByteToWideChar(CP_ACP, 0, file_path, -1, wide_path, wide_len);
        
        // Save the image
        Status result = m_artwork_image->Save(wide_path, &encoder_clsid);
        
        delete[] wide_path;
        
        if (result != Ok) {
            MessageBoxA(m_hWnd, "Failed to save artwork file.", "Error", MB_OK | MB_ICONERROR);
        }
        // No confirmation dialog - save silently on success
    }
}

void ArtworkViewerPopup::CreateControls() {
    // Create fit/original size button
    m_fit_button = CreateWindowA("BUTTON", "Fit to window size",  // Button shows opposite of current mode
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd, (HMENU)ID_FIT_BUTTON, GetModuleHandle(NULL), NULL);
    
    // Create save button
    m_save_button = CreateWindowA("BUTTON", "Save...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd, (HMENU)ID_SAVE_BUTTON, GetModuleHandle(NULL), NULL);
    
    // Create info label (no border, transparent background)
    m_info_label = CreateWindowA("STATIC", m_image_info.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 0, 0, m_hWnd, NULL, GetModuleHandle(NULL), NULL);
    
    // Set transparent background for the label
    ::SetWindowLongPtr(m_info_label, GWL_EXSTYLE, ::GetWindowLongPtr(m_info_label, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    
    // Set font for all controls
    HFONT font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    
    if (font) {
        SendMessage(m_fit_button, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(m_save_button, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessage(m_info_label, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

std::string ArtworkViewerPopup::GetImageInfo() const {
    if (!m_artwork_image) return "No image information";
    
    UINT width = m_artwork_image->GetWidth();
    UINT height = m_artwork_image->GetHeight();
    
    std::ostringstream info;
    
    // Calculate approximate file size based on image dimensions
    // Use a reasonable estimate for typical compressed images
    size_t estimated_size = (width * height * 3) / 8; // Assume moderate compression
    
    // Format file size
    if (estimated_size < 1024) {
        info << estimated_size << " B";
    } else if (estimated_size < 1024 * 1024) {
        info << std::fixed << std::setprecision(0) << (estimated_size / 1024.0) << " KB";
    } else {
        info << std::fixed << std::setprecision(1) << (estimated_size / (1024.0 * 1024.0)) << " MB";
    }
    
    // Add dimensions
    info << "  |  " << width << "x" << height;
    
    // Add source (Local, Deezer, etc.)
    std::string source = m_source_info;
    if (source.empty()) {
        source = "Unknown";
    }
    info << "  |  " << source;
    
    return info.str();
}

void ArtworkViewerPopup::CenterWindow(HWND parent_hwnd) {
    RECT parent_rect, window_rect;
    
    if (parent_hwnd && ::GetWindowRect(parent_hwnd, &parent_rect)) {
        // Center on parent window
        ::GetWindowRect(m_hWnd, &window_rect);
        
        int parent_width = parent_rect.right - parent_rect.left;
        int parent_height = parent_rect.bottom - parent_rect.top;
        int window_width = window_rect.right - window_rect.left;
        int window_height = window_rect.bottom - window_rect.top;
        
        int x = parent_rect.left + (parent_width - window_width) / 2;
        int y = parent_rect.top + (parent_height - window_height) / 2;
        
        ::SetWindowPos(m_hWnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    } else {
        // Center on screen
        int screen_width = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);
        
        ::GetWindowRect(m_hWnd, &window_rect);
        int window_width = window_rect.right - window_rect.left;
        int window_height = window_rect.bottom - window_rect.top;
        
        int x = (screen_width - window_width) / 2;
        int y = (screen_height - window_height) / 2;
        
        ::SetWindowPos(m_hWnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    }
}

// Helper function to get encoder CLSID for saving images
int ArtworkViewerPopup::GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;          // number of image encoders
    UINT size = 0;         // size of the image encoder array in bytes

    ImageCodecInfo* pImageCodecInfo = NULL;

    GetImageEncodersSize(&num, &size);
    if (size == 0)
        return -1;  // Failure

    pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL)
        return -1;  // Failure

    GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;  // Success
        }
    }

    free(pImageCodecInfo);
    return -1;  // Failure
}

void ArtworkViewerPopup::SaveWindowState() {
    if (!IsWindow()) return;
    
    // Check if window is maximized
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    
    if (::GetWindowPlacement(m_hWnd, &wp)) {
        bool is_maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
        
        // For maximized windows, save the restored position/size, not the maximized one
        RECT rect_to_save;
        if (is_maximized) {
            rect_to_save = wp.rcNormalPosition;
        } else {
            ::GetWindowRect(m_hWnd, &rect_to_save);
        }
        
        // Save to foobar2000 configuration
        cfg_viewer_window_x = rect_to_save.left;
        cfg_viewer_window_y = rect_to_save.top;
        cfg_viewer_window_width = rect_to_save.right - rect_to_save.left;
        cfg_viewer_window_height = rect_to_save.bottom - rect_to_save.top;
        cfg_viewer_window_maximized = is_maximized;
    }
}

bool ArtworkViewerPopup::GetSavedWindowRect(RECT& rect, bool& was_maximized) {
    // Check if we have saved window state (-1 means never saved)
    if (cfg_viewer_window_x == -1 || cfg_viewer_window_y == -1) {
        return false;
    }
    
    int x = cfg_viewer_window_x;
    int y = cfg_viewer_window_y;
    int width = cfg_viewer_window_width;
    int height = cfg_viewer_window_height;
    was_maximized = cfg_viewer_window_maximized;
    
    // Validate the saved position is still on screen (multi-monitor aware)
    // Get virtual desktop dimensions (covers all monitors)
    int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int virtual_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int virtual_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int virtual_right = virtual_left + virtual_width;
    int virtual_bottom = virtual_top + virtual_height;
    
    // Check if window would be completely off the virtual desktop
    if (x >= virtual_right || y >= virtual_bottom || 
        x + width <= virtual_left || y + height <= virtual_top) {
        return false; // Position is invalid
    }
    
    // Ensure minimum size
    if (width < 400) width = 400;
    if (height < 300) height = 300;
    
    // For multi-monitor setups, we don't need to force the window onto the primary monitor
    // Just ensure it's not completely off the virtual desktop (which we already checked above)
    // Windows will handle moving the window to a visible area if needed
    
    rect.left = x;
    rect.top = y;
    rect.right = x + width;
    rect.bottom = y + height;
    
    return true;
}

void ArtworkViewerPopup::RestoreWindowState(HWND parent_hwnd) {
    // This method is kept for potential future use
    // Currently the restore logic is integrated into ShowPopup()
}
