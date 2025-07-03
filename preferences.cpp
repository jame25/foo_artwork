#include "stdafx.h"
#include "resource.h"

// Reference to configuration variables defined in sdk_main.cpp
extern cfg_bool cfg_enable_itunes, cfg_enable_discogs, cfg_enable_lastfm;
extern cfg_string cfg_discogs_key, cfg_discogs_consumer_key, cfg_discogs_consumer_secret, cfg_lastfm_key;

// External declaration from sdk_main.cpp
extern HINSTANCE g_hIns;

// GUID for our preferences page
static const GUID guid_preferences_page_artwork = 
{ 0x12345680, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf7 } };

//=============================================================================
// artwork_preferences - preferences page instance implementation
//=============================================================================

class artwork_preferences : public preferences_page_instance {
private:
    HWND m_hwnd;
    preferences_page_callback::ptr m_callback;
    bool m_has_changes;
    fb2k::CCoreDarkModeHooks m_darkMode;
    
public:
    artwork_preferences(preferences_page_callback::ptr callback);
    
    // preferences_page_instance implementation
    HWND get_wnd() override;
    t_uint32 get_state() override;
    void apply() override;
    void reset() override;
    
    // Dialog procedure
    static INT_PTR CALLBACK ConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    
private:
    void on_changed();
    bool has_changed();
    void apply_settings();
    void reset_settings();
    void update_controls();
};

artwork_preferences::artwork_preferences(preferences_page_callback::ptr callback) 
    : m_hwnd(nullptr), m_callback(callback), m_has_changes(false) {
}

HWND artwork_preferences::get_wnd() {
    return m_hwnd;
}

t_uint32 artwork_preferences::get_state() {
    t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
    if (m_has_changes) {
        state |= preferences_state::changed;
    }
    return state;
}

void artwork_preferences::apply() {
    apply_settings();
    m_has_changes = false;
    m_callback->on_state_changed();
}

void artwork_preferences::reset() {
    reset_settings();
    m_has_changes = false;
    m_callback->on_state_changed();
}

INT_PTR CALLBACK artwork_preferences::ConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    artwork_preferences* p_this = nullptr;
    
    if (msg == WM_INITDIALOG) {
        p_this = reinterpret_cast<artwork_preferences*>(lp);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, lp);
        p_this->m_hwnd = hwnd;
        
        // Initialize dark mode hooks
        p_this->m_darkMode.AddDialogWithControls(hwnd);
        
        // Initialize checkbox states
        CheckDlgButton(hwnd, IDC_ENABLE_ITUNES, cfg_enable_itunes ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ENABLE_DISCOGS, cfg_enable_discogs ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ENABLE_LASTFM, cfg_enable_lastfm ? BST_CHECKED : BST_UNCHECKED);
        
        // Initialize text fields
        SetDlgItemTextA(hwnd, IDC_DISCOGS_KEY, cfg_discogs_key);
        SetDlgItemTextA(hwnd, IDC_DISCOGS_CONSUMER_KEY, cfg_discogs_consumer_key);
        SetDlgItemTextA(hwnd, IDC_DISCOGS_CONSUMER_SECRET, cfg_discogs_consumer_secret);
        SetDlgItemTextA(hwnd, IDC_LASTFM_KEY, cfg_lastfm_key);
        
        p_this->update_controls();
        p_this->m_has_changes = false;
    } else {
        p_this = reinterpret_cast<artwork_preferences*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    
    if (p_this == nullptr) return FALSE;
    
    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED && (LOWORD(wp) == IDC_ENABLE_ITUNES || 
                                         LOWORD(wp) == IDC_ENABLE_DISCOGS || 
                                         LOWORD(wp) == IDC_ENABLE_LASTFM)) {
            p_this->update_controls();
            p_this->on_changed();
        } else if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == IDC_DISCOGS_KEY ||
                                              LOWORD(wp) == IDC_DISCOGS_CONSUMER_KEY ||
                                              LOWORD(wp) == IDC_DISCOGS_CONSUMER_SECRET ||
                                              LOWORD(wp) == IDC_LASTFM_KEY)) {
            p_this->on_changed();
        }
        break;
        
    case WM_DESTROY:
        p_this->m_hwnd = nullptr;
        break;
    }
    
    return FALSE;
}

void artwork_preferences::on_changed() {
    m_has_changes = true;
    m_callback->on_state_changed();
}

void artwork_preferences::update_controls() {
    if (!m_hwnd) return;
    
    // Enable/disable API key fields based on checkbox state
    bool discogs_enabled = IsDlgButtonChecked(m_hwnd, IDC_ENABLE_DISCOGS) == BST_CHECKED;
    EnableWindow(GetDlgItem(m_hwnd, IDC_DISCOGS_KEY), discogs_enabled);
    EnableWindow(GetDlgItem(m_hwnd, IDC_DISCOGS_CONSUMER_KEY), discogs_enabled);
    EnableWindow(GetDlgItem(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET), discogs_enabled);
    EnableWindow(GetDlgItem(m_hwnd, IDC_LASTFM_KEY), IsDlgButtonChecked(m_hwnd, IDC_ENABLE_LASTFM) == BST_CHECKED);
}

bool artwork_preferences::has_changed() {
    if (!m_hwnd) return false;
    
    bool itunes_changed = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_ITUNES) == BST_CHECKED) != cfg_enable_itunes;
    bool discogs_changed = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_DISCOGS) == BST_CHECKED) != cfg_enable_discogs;
    bool lastfm_changed = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_LASTFM) == BST_CHECKED) != cfg_enable_lastfm;
    
    // Check text fields
    char buffer[256];
    
    GetDlgItemTextA(m_hwnd, IDC_DISCOGS_KEY, buffer, sizeof(buffer));
    bool discogs_key_changed = strcmp(buffer, cfg_discogs_key) != 0;
    
    GetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_KEY, buffer, sizeof(buffer));
    bool discogs_consumer_key_changed = strcmp(buffer, cfg_discogs_consumer_key) != 0;
    
    GetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET, buffer, sizeof(buffer));
    bool discogs_consumer_secret_changed = strcmp(buffer, cfg_discogs_consumer_secret) != 0;
    
    GetDlgItemTextA(m_hwnd, IDC_LASTFM_KEY, buffer, sizeof(buffer));
    bool lastfm_key_changed = strcmp(buffer, cfg_lastfm_key) != 0;
    
    return itunes_changed || discogs_changed || lastfm_changed || 
           discogs_key_changed || discogs_consumer_key_changed || 
           discogs_consumer_secret_changed || lastfm_key_changed;
}

void artwork_preferences::apply_settings() {
    if (m_hwnd) {
        cfg_enable_itunes = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_ITUNES) == BST_CHECKED);
        cfg_enable_discogs = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_DISCOGS) == BST_CHECKED);
        cfg_enable_lastfm = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_LASTFM) == BST_CHECKED);
        
        char buffer[256];
        
        GetDlgItemTextA(m_hwnd, IDC_DISCOGS_KEY, buffer, sizeof(buffer));
        cfg_discogs_key = buffer;
        
        GetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_KEY, buffer, sizeof(buffer));
        cfg_discogs_consumer_key = buffer;
        
        GetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET, buffer, sizeof(buffer));
        cfg_discogs_consumer_secret = buffer;
        
        GetDlgItemTextA(m_hwnd, IDC_LASTFM_KEY, buffer, sizeof(buffer));
        cfg_lastfm_key = buffer;
    }
}

void artwork_preferences::reset_settings() {
    if (m_hwnd) {
        // Reset to default values
        CheckDlgButton(m_hwnd, IDC_ENABLE_ITUNES, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_DISCOGS, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_LASTFM, BST_CHECKED);
        
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_KEY, "");
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_KEY, "");
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET, "");
        SetDlgItemTextA(m_hwnd, IDC_LASTFM_KEY, "");
        
        update_controls();
    }
}

//=============================================================================
// artwork_preferences_page - preferences page factory implementation
//=============================================================================

class artwork_preferences_page : public preferences_page_v3 {
public:
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

const char* artwork_preferences_page::get_name() {
    return "Artwork Display";
}

GUID artwork_preferences_page::get_guid() {
    return guid_preferences_page_artwork;
}

GUID artwork_preferences_page::get_parent_guid() {
    return preferences_page::guid_tools;
}

preferences_page_instance::ptr artwork_preferences_page::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    auto instance = fb2k::service_new<artwork_preferences>(callback);
    
    HWND hwnd = CreateDialogParam(
        g_hIns, 
        MAKEINTRESOURCE(IDD_PREFERENCES), 
        parent, 
        artwork_preferences::ConfigProc, 
        reinterpret_cast<LPARAM>(instance.get_ptr())
    );
    
    if (hwnd == nullptr) {
        throw exception_win32(GetLastError());
    }
    
    return instance;
}

// Service registration
static preferences_page_factory_t<artwork_preferences_page> g_artwork_preferences_page_factory;