#include "stdafx.h"
#include "resource.h"
#include <commdlg.h>  // For file save dialog
#include <shlobj.h>   // For folder browser dialog (still needed for directory extraction)

// Forward declarations no longer needed - using simpler logging approach

// Reference to configuration variables defined in sdk_main.cpp
extern cfg_bool cfg_enable_itunes, cfg_enable_discogs, cfg_enable_lastfm, cfg_enable_deezer, cfg_enable_musicbrainz;
extern cfg_string cfg_discogs_key, cfg_discogs_consumer_key, cfg_discogs_consumer_secret, cfg_lastfm_key;
extern cfg_int cfg_search_order_1, cfg_search_order_2, cfg_search_order_3, cfg_search_order_4, cfg_search_order_5;
extern cfg_int cfg_stream_delay;

// Reference to current artwork source for logging
extern pfc::string8 g_current_artwork_source;

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
    static pfc::string8 s_log_directory;  // Static to persist across instances
    
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
    void write_log();
};

// Initialize static variable
pfc::string8 artwork_preferences::s_log_directory;

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

// Helper functions for API comboboxes
static const char* get_api_name(int api_index) {
    switch (api_index) {
        case 0: return "iTunes";
        case 1: return "Deezer";
        case 2: return "Last.fm";
        case 3: return "MusicBrainz";
        case 4: return "Discogs";
        default: return "Unknown";
    }
}

// Helper to get search order position name
static const char* get_position_name(int position) {
    switch (position) {
        case 0: return "1st";
        case 1: return "2nd"; 
        case 2: return "3rd";
        case 3: return "4th";
        case 4: return "5th";
        default: return "Unknown";
    }
}

// Convert dropdown index to API enum value
static int dropdown_index_to_api_value(int dropdown_index) {
    // Dropdown index:  0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs
    // API enum value:  0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs
    return dropdown_index; // They match, so direct mapping
}

// Convert API enum value to dropdown index
static int api_value_to_dropdown_index(int api_value) {
    // API enum value:  0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs
    // Dropdown index:  0=iTunes, 1=Deezer, 2=Last.fm, 3=MusicBrainz, 4=Discogs
    return api_value; // They match, so direct mapping
}

static void populate_api_combobox(HWND combo) {
    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < 5; i++) {
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(get_api_name(i)));
    }
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
        CheckDlgButton(hwnd, IDC_ENABLE_DEEZER, cfg_enable_deezer ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ENABLE_MUSICBRAINZ, cfg_enable_musicbrainz ? BST_CHECKED : BST_UNCHECKED);
        
        // Initialize text fields
        SetDlgItemTextA(hwnd, IDC_DISCOGS_KEY, cfg_discogs_key);
        SetDlgItemTextA(hwnd, IDC_DISCOGS_CONSUMER_KEY, cfg_discogs_consumer_key);
        SetDlgItemTextA(hwnd, IDC_DISCOGS_CONSUMER_SECRET, cfg_discogs_consumer_secret);
        SetDlgItemTextA(hwnd, IDC_LASTFM_KEY, cfg_lastfm_key);
        
        // Initialize priority comboboxes
        populate_api_combobox(GetDlgItem(hwnd, IDC_PRIORITY_1));
        populate_api_combobox(GetDlgItem(hwnd, IDC_PRIORITY_2));
        populate_api_combobox(GetDlgItem(hwnd, IDC_PRIORITY_3));
        populate_api_combobox(GetDlgItem(hwnd, IDC_PRIORITY_4));
        populate_api_combobox(GetDlgItem(hwnd, IDC_PRIORITY_5));
        
        // Set current search order selections (NEW SIMPLE SYSTEM)
        // Each dropdown shows which API is at that position
        SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_1), CB_SETCURSEL, cfg_search_order_1, 0);  // 1st choice
        SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_2), CB_SETCURSEL, cfg_search_order_2, 0);  // 2nd choice
        SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_3), CB_SETCURSEL, cfg_search_order_3, 0);  // 3rd choice
        SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_4), CB_SETCURSEL, cfg_search_order_4, 0);  // 4th choice
        SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_5), CB_SETCURSEL, cfg_search_order_5, 0);  // 5th choice
        
        // Set stream delay value
        SetDlgItemInt(hwnd, IDC_STREAM_DELAY, cfg_stream_delay, FALSE);
        
        // Initialize spin control for stream delay
        HWND spin_hwnd = GetDlgItem(hwnd, IDC_STREAM_DELAY_SPIN);
        SendMessage(spin_hwnd, UDM_SETRANGE, 0, MAKELPARAM(30, 1));  // Range: 1-30 seconds
        SendMessage(spin_hwnd, UDM_SETPOS, 0, cfg_stream_delay);
        
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
                                         LOWORD(wp) == IDC_ENABLE_LASTFM ||
                                         LOWORD(wp) == IDC_ENABLE_DEEZER ||
                                         LOWORD(wp) == IDC_ENABLE_MUSICBRAINZ)) {
            p_this->update_controls();
            p_this->on_changed();
        } else if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == IDC_DISCOGS_KEY ||
                                              LOWORD(wp) == IDC_DISCOGS_CONSUMER_KEY ||
                                              LOWORD(wp) == IDC_DISCOGS_CONSUMER_SECRET ||
                                              LOWORD(wp) == IDC_LASTFM_KEY ||
                                              LOWORD(wp) == IDC_STREAM_DELAY)) {
            p_this->on_changed();
        } else if (HIWORD(wp) == CBN_SELCHANGE && (LOWORD(wp) == IDC_PRIORITY_1 ||
                                                  LOWORD(wp) == IDC_PRIORITY_2 ||
                                                  LOWORD(wp) == IDC_PRIORITY_3 ||
                                                  LOWORD(wp) == IDC_PRIORITY_4 ||
                                                  LOWORD(wp) == IDC_PRIORITY_5)) {
            p_this->on_changed();
        } else if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == IDC_WRITE_LOG) {
            p_this->write_log();
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
    bool deezer_changed = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_DEEZER) == BST_CHECKED) != cfg_enable_deezer;
    bool musicbrainz_changed = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_MUSICBRAINZ) == BST_CHECKED) != cfg_enable_musicbrainz;
    
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
    
    // Check search order comboboxes (NEW SIMPLE SYSTEM)
    int current_order_1 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_GETCURSEL, 0, 0);
    int current_order_2 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_GETCURSEL, 0, 0);
    int current_order_3 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_GETCURSEL, 0, 0);
    int current_order_4 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_GETCURSEL, 0, 0);
    int current_order_5 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_GETCURSEL, 0, 0);
    
    bool order1_changed = current_order_1 != cfg_search_order_1;
    bool order2_changed = current_order_2 != cfg_search_order_2;
    bool order3_changed = current_order_3 != cfg_search_order_3;
    bool order4_changed = current_order_4 != cfg_search_order_4;
    bool order5_changed = current_order_5 != cfg_search_order_5;
    
    // Check stream delay
    BOOL success;
    int stream_delay_value = GetDlgItemInt(m_hwnd, IDC_STREAM_DELAY, &success, FALSE);
    bool stream_delay_changed = success && (stream_delay_value != cfg_stream_delay);
    
    return itunes_changed || discogs_changed || lastfm_changed || deezer_changed || musicbrainz_changed ||
           discogs_key_changed || discogs_consumer_key_changed || 
           discogs_consumer_secret_changed || lastfm_key_changed ||
           order1_changed || order2_changed || order3_changed || order4_changed || order5_changed ||
           stream_delay_changed;
}

void artwork_preferences::apply_settings() {
    if (m_hwnd) {
        cfg_enable_itunes = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_ITUNES) == BST_CHECKED);
        cfg_enable_discogs = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_DISCOGS) == BST_CHECKED);
        cfg_enable_lastfm = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_LASTFM) == BST_CHECKED);
        cfg_enable_deezer = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_DEEZER) == BST_CHECKED);
        cfg_enable_musicbrainz = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_MUSICBRAINZ) == BST_CHECKED);
        
        char buffer[256];
        
        GetDlgItemTextA(m_hwnd, IDC_DISCOGS_KEY, buffer, sizeof(buffer));
        cfg_discogs_key = buffer;
        
        GetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_KEY, buffer, sizeof(buffer));
        cfg_discogs_consumer_key = buffer;
        
        GetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET, buffer, sizeof(buffer));
        cfg_discogs_consumer_secret = buffer;
        
        GetDlgItemTextA(m_hwnd, IDC_LASTFM_KEY, buffer, sizeof(buffer));
        cfg_lastfm_key = buffer;
        
        // Save search order selections (NEW SIMPLE SYSTEM)
        // Each dropdown directly represents the search position
        cfg_search_order_1 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_GETCURSEL, 0, 0);  // 1st choice
        cfg_search_order_2 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_GETCURSEL, 0, 0);  // 2nd choice
        cfg_search_order_3 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_GETCURSEL, 0, 0);  // 3rd choice
        cfg_search_order_4 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_GETCURSEL, 0, 0);  // 4th choice
        cfg_search_order_5 = SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_GETCURSEL, 0, 0);  // 5th choice
        
        // Save stream delay
        BOOL success;
        int stream_delay_value = GetDlgItemInt(m_hwnd, IDC_STREAM_DELAY, &success, FALSE);
        if (success && stream_delay_value >= 1 && stream_delay_value <= 30) {
            cfg_stream_delay = stream_delay_value;
        }
    }
}

void artwork_preferences::reset_settings() {
    if (m_hwnd) {
        // Reset to default values
        CheckDlgButton(m_hwnd, IDC_ENABLE_ITUNES, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_DISCOGS, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_LASTFM, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_DEEZER, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_MUSICBRAINZ, BST_CHECKED);
        
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_KEY, "");
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_KEY, "");
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET, "");
        SetDlgItemTextA(m_hwnd, IDC_LASTFM_KEY, "");
        
        // Reset search order to default order (NEW SIMPLE SYSTEM)
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_SETCURSEL, 1, 0);  // 1st: Deezer
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_SETCURSEL, 0, 0);  // 2nd: iTunes
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_SETCURSEL, 2, 0);  // 3rd: Last.fm
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_SETCURSEL, 3, 0);  // 4th: MusicBrainz
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_SETCURSEL, 4, 0);  // 5th: Discogs
        
        // Reset stream delay to default (1 second)
        SetDlgItemInt(m_hwnd, IDC_STREAM_DELAY, 1, FALSE);
        SendMessage(GetDlgItem(m_hwnd, IDC_STREAM_DELAY_SPIN), UDM_SETPOS, 0, 1);
        
        update_controls();
    }
}

void artwork_preferences::write_log() {
    if (!m_hwnd) return;
    
    // Generate unique filename with timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    pfc::string8 filename;
    filename << "foo_artwork_debug_" 
             << pfc::format_uint(st.wYear, 4) << "-"
             << pfc::format_uint(st.wMonth, 2) << "-" 
             << pfc::format_uint(st.wDay, 2) << "_"
             << pfc::format_uint(st.wHour, 2) << "-"
             << pfc::format_uint(st.wMinute, 2) << "-"
             << pfc::format_uint(st.wSecond, 2) << ".log";
    
    pfc::string8 log_file_path;
    
    // Show file save dialog
    OPENFILENAMEA ofn = {};
    char file_path[MAX_PATH] = {};
    
    // Set default filename
    strcpy_s(file_path, sizeof(file_path), filename.c_str());
    
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = sizeof(file_path);
    ofn.lpstrFilter = "Log Files (*.log)\0*.log\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = s_log_directory.is_empty() ? nullptr : s_log_directory.c_str();
    ofn.lpstrTitle = "Save Debug Log";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "log";
    
    if (!GetSaveFileNameA(&ofn)) {
        return; // User cancelled
    }
    
    log_file_path = file_path;
    
    // Remember the directory for future saves
    char* last_slash = strrchr(file_path, '\\');
    if (last_slash) {
        *last_slash = '\0';
        s_log_directory = file_path;
    }
    
    // Create log content with current settings
    pfc::string8 log_content;
    log_content << "foo_artwork Debug Log - " << pfc::format_time(filetimestamp_from_system_timer()) << "\n\n";
    
    // API Services status
    log_content << "=== API Services ===\n";
    log_content << "iTunes: " << (cfg_enable_itunes ? "Enabled" : "Disabled") << "\n";
    log_content << "Deezer: " << (cfg_enable_deezer ? "Enabled" : "Disabled") << "\n";
    log_content << "Last.fm: " << (cfg_enable_lastfm ? "Enabled" : "Disabled") << "\n";
    log_content << "MusicBrainz: " << (cfg_enable_musicbrainz ? "Enabled" : "Disabled") << "\n";
    log_content << "Discogs: " << (cfg_enable_discogs ? "Enabled" : "Disabled") << "\n\n";
    
    // API Keys status (don't log actual keys for security)
    log_content << "=== API Keys ===\n";
    log_content << "Discogs API Key: " << (cfg_discogs_key.is_empty() ? "Not set" : "Set") << "\n";
    log_content << "Discogs Consumer Key: " << (cfg_discogs_consumer_key.is_empty() ? "Not set" : "Set") << "\n";
    log_content << "Discogs Consumer Secret: " << (cfg_discogs_consumer_secret.is_empty() ? "Not set" : "Set") << "\n";
    log_content << "Last.fm API Key: " << (cfg_lastfm_key.is_empty() ? "Not set" : "Set") << "\n\n";
    
    // Current artwork source information
    log_content << "=== Current Artwork Source ===\n";
    if (!g_current_artwork_source.is_empty()) {
        log_content << "Current artwork: " << g_current_artwork_source.c_str() << "\n";
    } else {
        log_content << "No artwork currently loaded\n";
    }
    log_content << "Note: This shows the source of the most recently loaded artwork.\n\n";
    
    // Search order settings (NEW CLEAR SYSTEM)
    log_content << "=== Search Order ===\n";
    log_content << "1st choice: " << get_api_name(cfg_search_order_1) << "\n";
    log_content << "2nd choice: " << get_api_name(cfg_search_order_2) << "\n";
    log_content << "3rd choice: " << get_api_name(cfg_search_order_3) << "\n";
    log_content << "4th choice: " << get_api_name(cfg_search_order_4) << "\n";
    log_content << "5th choice: " << get_api_name(cfg_search_order_5) << "\n\n";
    
    // Stream delay
    log_content << "=== Stream Settings ===\n";
    log_content << "Stream Delay: " << cfg_stream_delay << " seconds\n\n";
    
    // System info
    log_content << "=== System Information ===\n";
    log_content << "foobar2000 Version: " << core_version_info::g_get_version_string() << "\n";
    log_content << "Profile Path: " << core_api::get_profile_path() << "\n";
    log_content << "Log Directory: " << s_log_directory << "\n";
    
    // Write to file
    try {
        HANDLE hFile = CreateFileA(log_file_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytes_written;
            WriteFile(hFile, log_content.c_str(), static_cast<DWORD>(log_content.length()), &bytes_written, NULL);
            CloseHandle(hFile);
            
            // Show success message with filename
            pfc::string8 message = "Debug log written to:\n";
            message << log_file_path;
            MessageBoxA(m_hwnd, message.c_str(), "Log Written", MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxA(m_hwnd, "Failed to create log file", "Error", MB_OK | MB_ICONERROR);
        }
    } catch (...) {
        MessageBoxA(m_hwnd, "Error writing log file", "Error", MB_OK | MB_ICONERROR);
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
