#pragma once

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <atlbase.h>
#include <atlwin.h>
#include <string>
#include <memory>

// foobar2000 SDK is already included via stdafx.h

// Unified artwork viewer popup that works the same in both DUI and CUI modes
class ArtworkViewerPopup : public CWindowImpl<ArtworkViewerPopup> {
public:
    // Constructor takes the artwork image and source info
    ArtworkViewerPopup(Gdiplus::Image* artwork_image, const std::string& source_info);
    ~ArtworkViewerPopup();

    DECLARE_WND_CLASS_EX(L"ArtworkViewerPopup", CS_DBLCLKS, COLOR_WINDOW);

    BEGIN_MSG_MAP(ArtworkViewerPopup)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorStatic)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
    END_MSG_MAP()

    // Show the popup window
    void ShowPopup(HWND parent_hwnd);

private:
    // Message handlers
    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCtlColorStatic(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnClose(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // Drawing functions
    void PaintArtwork(HDC hdc);
    void PaintUI(HDC hdc);
    void CalculateImageRect();
    void UpdateLayout();

    // Control functions
    void ToggleFitMode();
    void SaveArtwork();
    void CreateControls();

    // Utility functions
    std::string GetImageInfo() const;
    void CenterWindow(HWND parent_hwnd);
    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
    
    // Window state persistence
    void SaveWindowState();
    void RestoreWindowState(HWND parent_hwnd);
    bool GetSavedWindowRect(RECT& rect, bool& was_maximized);

    // Member variables
    std::unique_ptr<Gdiplus::Image> m_artwork_image;
    std::string m_source_info;
    std::string m_image_info;
    HWND m_parent_hwnd;
    
    // Dark mode support will be added later - for now use basic theme detection
    
    // Display state
    bool m_fit_to_window;
    RECT m_client_rect;
    RECT m_image_rect;
    
    // Controls
    HWND m_fit_button;
    HWND m_save_button;
    HWND m_info_label;
    
    // Control IDs
    static const int ID_FIT_BUTTON = 1001;
    static const int ID_SAVE_BUTTON = 1002;
    
    // Layout constants
    static const int CONTROL_HEIGHT = 30;
    static const int CONTROL_MARGIN = 10;
    static const int BUTTON_WIDTH = 120;
};
