#include "stdafx.h"
#include "resource.h"
#include "async_io_manager.h"
#include <commdlg.h>  // For file save dialog
#include <shlobj.h>   // For folder browser dialog (still needed for directory extraction)

// Forward declarations no longer needed - using simpler logging approach

// Reference to configuration variables defined in sdk_main.cpp
extern cfg_bool cfg_enable_itunes, cfg_enable_discogs, cfg_enable_lastfm, cfg_enable_deezer, cfg_enable_musicbrainz;
extern cfg_string cfg_discogs_key, cfg_discogs_consumer_key, cfg_discogs_consumer_secret, cfg_lastfm_key;
extern cfg_int cfg_search_order_1, cfg_search_order_2, cfg_search_order_3, cfg_search_order_4, cfg_search_order_5;
extern cfg_bool cfg_show_osd;
extern cfg_bool cfg_enable_custom_logos;
extern cfg_string cfg_logos_folder;
extern cfg_bool cfg_clear_panel_when_not_playing;
extern cfg_bool cfg_infobar;
extern cfg_bool cfg_use_noart_image;
extern cfg_int cfg_http_timeout;
extern cfg_int cfg_retry_count;
extern cfg_bool cfg_enable_disk_cache;
extern cfg_string cfg_cache_folder;
extern cfg_bool cfg_skip_local_artwork;

// Reference to current artwork source for logging
extern pfc::string8 g_current_artwork_source;

// External declarations from sdk_main.cpp
extern HINSTANCE g_hIns;
extern void update_all_clear_panel_timers();

// GUID for our preferences pages
static const GUID guid_preferences_page_artwork =
{ 0x12345680, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf7 } };

static const GUID guid_preferences_page_advanced =
{ 0x12345690, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf8 } };

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
    void toggle_osd();
    void browse_for_cache_folder();
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

        // Initialize disk cache combobox
        HWND hDiskCache = GetDlgItem(hwnd, IDC_ENABLE_DISK_CACHE);
        SendMessage(hDiskCache, CB_RESETCONTENT, 0, 0);
        SendMessageA(hDiskCache, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Enabled"));
        SendMessageA(hDiskCache, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Disabled"));
        SendMessageA(hDiskCache, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Clear"));
        SendMessage(hDiskCache, CB_SETCURSEL, cfg_enable_disk_cache ? 0 : 1, 0);
        
        // Enable/disable browse cache folder button based on disk cache enabled state
        EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_CACHE_FOLDER), cfg_enable_disk_cache ? TRUE : FALSE);

        // Initialize skip local artwork checkbox
        CheckDlgButton(hwnd, IDC_SKIP_LOCAL_ARTWORK, cfg_skip_local_artwork ? BST_CHECKED : BST_UNCHECKED);

        p_this->update_controls();
        p_this->m_has_changes = false;
    }
    else {
        p_this = reinterpret_cast<artwork_preferences*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (p_this == nullptr) return FALSE;

    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED && (LOWORD(wp) == IDC_ENABLE_ITUNES ||
            LOWORD(wp) == IDC_ENABLE_DISCOGS ||
            LOWORD(wp) == IDC_ENABLE_LASTFM ||
            LOWORD(wp) == IDC_ENABLE_DEEZER ||
            LOWORD(wp) == IDC_ENABLE_MUSICBRAINZ ||
            LOWORD(wp) == IDC_SKIP_LOCAL_ARTWORK)) {
            p_this->update_controls();
            p_this->on_changed();
        }
        else if (HIWORD(wp) == EN_CHANGE && (LOWORD(wp) == IDC_DISCOGS_KEY ||
            LOWORD(wp) == IDC_DISCOGS_CONSUMER_KEY ||
            LOWORD(wp) == IDC_DISCOGS_CONSUMER_SECRET ||
            LOWORD(wp) == IDC_LASTFM_KEY)) {
            p_this->on_changed();
        }
        else if (HIWORD(wp) == CBN_SELCHANGE && (LOWORD(wp) == IDC_PRIORITY_1 ||
            LOWORD(wp) == IDC_PRIORITY_2 ||
            LOWORD(wp) == IDC_PRIORITY_3 ||
            LOWORD(wp) == IDC_PRIORITY_4 ||
            LOWORD(wp) == IDC_PRIORITY_5)) {
            p_this->on_changed();
        }
        else if (HIWORD(wp) == CBN_SELCHANGE && LOWORD(wp) == IDC_ENABLE_DISK_CACHE) {
            p_this->on_changed();
            // Enable/disable browse button based on disk cache enabled state
            int sel = SendMessage(GetDlgItem(hwnd, IDC_ENABLE_DISK_CACHE), CB_GETCURSEL, 0, 0);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE_CACHE_FOLDER), sel == 0);
        }
        else if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == IDC_SHOW_SOURCE) {
            p_this->toggle_osd();
        }
        else if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == IDC_BROWSE_CACHE_FOLDER) {
            p_this->browse_for_cache_folder();
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

    // Update Show Source button text based on OSD state
    SetDlgItemTextA(m_hwnd, IDC_SHOW_SOURCE, cfg_show_osd ? "Hide Artwork Source" : "Show Artwork Source");
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

    // Check disk cache combobox (0 = Enabled, 1 = Disabled, 2 = Clear)
    int disk_cache_selection = SendMessage(GetDlgItem(m_hwnd, IDC_ENABLE_DISK_CACHE), CB_GETCURSEL, 0, 0);
    bool disk_cache_changed = (disk_cache_selection == 2) || ((disk_cache_selection == 0) != cfg_enable_disk_cache);

    // Check skip local artwork checkbox
    bool skip_local_changed = (IsDlgButtonChecked(m_hwnd, IDC_SKIP_LOCAL_ARTWORK) == BST_CHECKED) != cfg_skip_local_artwork;

    return itunes_changed || discogs_changed || lastfm_changed || deezer_changed || musicbrainz_changed ||
        discogs_key_changed || discogs_consumer_key_changed ||
        discogs_consumer_secret_changed || lastfm_key_changed || disk_cache_changed ||
        order1_changed || order2_changed || order3_changed || order4_changed || order5_changed ||
        skip_local_changed;
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

        // Apply disk cache setting (0 = Enabled, 1 = Disabled, 2 = Clear)
        int disk_cache_selection = SendMessage(GetDlgItem(m_hwnd, IDC_ENABLE_DISK_CACHE), CB_GETCURSEL, 0, 0);
        if (disk_cache_selection == 2) {
            // Clear all cached artwork files, then revert to previous state
            async_io_manager::instance().cache_clear_all();
            SendMessage(GetDlgItem(m_hwnd, IDC_ENABLE_DISK_CACHE), CB_SETCURSEL, cfg_enable_disk_cache ? 0 : 1, 0);
            EnableWindow(GetDlgItem(m_hwnd, IDC_BROWSE_CACHE_FOLDER), cfg_enable_disk_cache ? TRUE : FALSE);
        } else {
            cfg_enable_disk_cache = (disk_cache_selection == 0);
        }

        // Apply skip local artwork setting
        cfg_skip_local_artwork = (IsDlgButtonChecked(m_hwnd, IDC_SKIP_LOCAL_ARTWORK) == BST_CHECKED);
    }
}

void artwork_preferences::reset_settings() {
    if (m_hwnd) {
        // Reset to default values (only Deezer enabled)
        CheckDlgButton(m_hwnd, IDC_ENABLE_ITUNES, BST_UNCHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_DISCOGS, BST_UNCHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_LASTFM, BST_UNCHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_DEEZER, BST_CHECKED);
        CheckDlgButton(m_hwnd, IDC_ENABLE_MUSICBRAINZ, BST_UNCHECKED);

        // Actually apply the reset values to the configuration variables
        cfg_enable_itunes = false;
        cfg_enable_discogs = false;
        cfg_enable_lastfm = false;
        cfg_enable_deezer = true;
        cfg_enable_musicbrainz = false;

        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_KEY, "");
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_KEY, "");
        SetDlgItemTextA(m_hwnd, IDC_DISCOGS_CONSUMER_SECRET, "");
        SetDlgItemTextA(m_hwnd, IDC_LASTFM_KEY, "");

        // Actually apply the reset values to configuration variables
        cfg_discogs_key = "";
        cfg_discogs_consumer_key = "";
        cfg_discogs_consumer_secret = "";
        cfg_lastfm_key = "";

        // Reset search order to default order (NEW SIMPLE SYSTEM)
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_SETCURSEL, 1, 0);  // 1st: Deezer
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_SETCURSEL, 0, 0);  // 2nd: iTunes
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_SETCURSEL, 2, 0);  // 3rd: Last.fm
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_SETCURSEL, 3, 0);  // 4th: MusicBrainz
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_SETCURSEL, 4, 0);  // 5th: Discogs

        // Actually apply the reset search order values
        cfg_search_order_1 = 1;  // 1st choice: Deezer
        cfg_search_order_2 = 0;  // 2nd choice: iTunes
        cfg_search_order_3 = 2;  // 3rd choice: Last.fm
        cfg_search_order_4 = 3;  // 4th choice: MusicBrainz
        cfg_search_order_5 = 4;  // 5th choice: Discogs

        // Reset disk cache to enabled (select index 0)
        SendMessage(GetDlgItem(m_hwnd, IDC_ENABLE_DISK_CACHE), CB_SETCURSEL, 0, 0);
        cfg_enable_disk_cache = true;

        // Reset skip local artwork to disabled
        CheckDlgButton(m_hwnd, IDC_SKIP_LOCAL_ARTWORK, BST_UNCHECKED);
        cfg_skip_local_artwork = false;

        update_controls();
    }
}

void artwork_preferences::toggle_osd() {
    if (!m_hwnd) return;

    // Toggle the OSD setting
    cfg_show_osd = !cfg_show_osd;

    // Update button text immediately
    update_controls();
}

void artwork_preferences::browse_for_cache_folder() {
    if (!m_hwnd) return;
    
    pfc::string8 folder_path;
    if (uBrowseForFolder(m_hwnd, "Select Artwork Cache Folder", folder_path)) {
        cfg_cache_folder = folder_path;
        on_changed();
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

//=============================================================================
// artwork_advanced_preferences - Advanced preferences page instance implementation  
//=============================================================================

class artwork_advanced_preferences : public preferences_page_instance {
private:
    HWND m_hwnd;
    preferences_page_callback::ptr m_callback;
    bool m_has_changes;
    fb2k::CCoreDarkModeHooks m_darkMode;

public:
    artwork_advanced_preferences(preferences_page_callback::ptr callback);

    // preferences_page_instance implementation
    HWND get_wnd() override;
    t_uint32 get_state() override;
    void apply() override;
    void reset() override;

    // Dialog procedure
    static INT_PTR CALLBACK AdvancedConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void on_changed();
    bool has_changed();
    void apply_settings();
    void reset_settings();
    void update_controls();
    void update_control_states();
    void browse_for_folder();
};

artwork_advanced_preferences::artwork_advanced_preferences(preferences_page_callback::ptr callback)
    : m_hwnd(nullptr), m_callback(callback), m_has_changes(false) {
}

HWND artwork_advanced_preferences::get_wnd() {
    return m_hwnd;
}

t_uint32 artwork_advanced_preferences::get_state() {
    t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
    if (m_has_changes) {
        state |= preferences_state::changed;
    }
    return state;
}

void artwork_advanced_preferences::apply() {
    apply_settings();
    m_has_changes = false;
    m_callback->on_state_changed();
}

void artwork_advanced_preferences::reset() {
    reset_settings();
    m_has_changes = false;
    m_callback->on_state_changed();
}

void artwork_advanced_preferences::on_changed() {
    m_has_changes = true;
    m_callback->on_state_changed();
}

bool artwork_advanced_preferences::has_changed() {
    if (!m_hwnd) return false;

    // Check if Enable Custom Station Logos checkbox changed
    bool enable_logos_changed = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_CUSTOM_LOGOS) == BST_CHECKED) != cfg_enable_custom_logos;

    // Check if custom folder path changed
    char current_folder[MAX_PATH];
    GetDlgItemTextA(m_hwnd, IDC_LOGOS_FOLDER_PATH, current_folder, MAX_PATH);
    bool folder_changed = strcmp(current_folder, cfg_logos_folder.get_ptr()) != 0;

    // Check if Clear panel when not playing checkbox changed
    bool clear_panel_changed = (IsDlgButtonChecked(m_hwnd, IDC_CLEAR_PANEL_WHEN_NOT_PLAYING) == BST_CHECKED) != cfg_clear_panel_when_not_playing;

    // Check if infobar checkbox changed
    bool infobar_changed = (IsDlgButtonChecked(m_hwnd, IDC_INFOBAR) == BST_CHECKED) != cfg_infobar;

    // Check if Use noart image checkbox changed
    bool use_noart_changed = (IsDlgButtonChecked(m_hwnd, IDC_USE_NOART_IMAGE) == BST_CHECKED) != cfg_use_noart_image;

    // Check if HTTP timeout changed
    int current_timeout = GetDlgItemInt(m_hwnd, IDC_HTTP_TIMEOUT, NULL, FALSE);
    bool timeout_changed = current_timeout != cfg_http_timeout;

    // Check if retry count changed
    int current_retry = GetDlgItemInt(m_hwnd, IDC_RETRY_COUNT, NULL, FALSE);
    bool retry_changed = current_retry != cfg_retry_count;

    return enable_logos_changed || folder_changed || clear_panel_changed || use_noart_changed ||
           infobar_changed || timeout_changed || retry_changed;
}

void artwork_advanced_preferences::apply_settings() {
    if (!m_hwnd) return;

    // Apply Enable Custom Station Logos setting
    cfg_enable_custom_logos = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_CUSTOM_LOGOS) == BST_CHECKED);

    // Apply custom folder path
    char folder_path[MAX_PATH];
    GetDlgItemTextA(m_hwnd, IDC_LOGOS_FOLDER_PATH, folder_path, MAX_PATH);
    cfg_logos_folder = folder_path;

    // Apply Clear panel when not playing setting
    cfg_clear_panel_when_not_playing = (IsDlgButtonChecked(m_hwnd, IDC_CLEAR_PANEL_WHEN_NOT_PLAYING) == BST_CHECKED);

    // Apply infobar setting
    cfg_infobar = (IsDlgButtonChecked(m_hwnd, IDC_INFOBAR) == BST_CHECKED);

    // Apply Use noart image setting
    cfg_use_noart_image = (IsDlgButtonChecked(m_hwnd, IDC_USE_NOART_IMAGE) == BST_CHECKED);

    // Apply HTTP timeout setting (clamp to valid range 5-120 seconds)
    int timeout = GetDlgItemInt(m_hwnd, IDC_HTTP_TIMEOUT, NULL, FALSE);
    if (timeout < 5) timeout = 5;
    if (timeout > 120) timeout = 120;
    cfg_http_timeout = timeout;

    // Apply retry count setting (clamp to valid range 0-5)
    int retry = GetDlgItemInt(m_hwnd, IDC_RETRY_COUNT, NULL, FALSE);
    if (retry < 0) retry = 0;
    if (retry > 5) retry = 5;
    cfg_retry_count = retry;

    // Update timers for all UI elements when setting changes
    update_all_clear_panel_timers();
}

void artwork_advanced_preferences::reset_settings() {
    if (!m_hwnd) return;

    // Reset to default values
    cfg_enable_custom_logos = false;  // Default disabled
    cfg_logos_folder = "";  // Default empty (use default path)
    cfg_clear_panel_when_not_playing = false;  // Default disabled
    cfg_infobar = false;  // Default disabled
    cfg_use_noart_image = false;  // Default disabled
    cfg_http_timeout = 15;  // Default 15 seconds
    cfg_retry_count = 2;  // Default 2 retries

    update_controls();
}

void artwork_advanced_preferences::update_controls() {
    if (!m_hwnd) return;

    // Update Enable Custom Station Logos checkbox
    CheckDlgButton(m_hwnd, IDC_ENABLE_CUSTOM_LOGOS, cfg_enable_custom_logos ? BST_CHECKED : BST_UNCHECKED);

    // Update custom folder path
    SetDlgItemTextA(m_hwnd, IDC_LOGOS_FOLDER_PATH, cfg_logos_folder.get_ptr());

    // Update Clear panel when not playing checkbox
    CheckDlgButton(m_hwnd, IDC_CLEAR_PANEL_WHEN_NOT_PLAYING, cfg_clear_panel_when_not_playing ? BST_CHECKED : BST_UNCHECKED);

    // Update infobar checkbox
    CheckDlgButton(m_hwnd, IDC_INFOBAR, cfg_infobar ? BST_CHECKED : BST_UNCHECKED);

    // Update Use noart image checkbox
    CheckDlgButton(m_hwnd, IDC_USE_NOART_IMAGE, cfg_use_noart_image ? BST_CHECKED : BST_UNCHECKED);

    // Update HTTP timeout field
    SetDlgItemInt(m_hwnd, IDC_HTTP_TIMEOUT, cfg_http_timeout, FALSE);

    // Update retry count field
    SetDlgItemInt(m_hwnd, IDC_RETRY_COUNT, cfg_retry_count, FALSE);

    // Enable/disable noart image checkbox based on clear panel checkbox state
    EnableWindow(GetDlgItem(m_hwnd, IDC_USE_NOART_IMAGE), cfg_clear_panel_when_not_playing ? TRUE : FALSE);

    // Enable/disable folder controls based on checkbox state
    BOOL enable_folder_controls = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_CUSTOM_LOGOS) == BST_CHECKED);
    EnableWindow(GetDlgItem(m_hwnd, IDC_LOGOS_FOLDER_PATH), enable_folder_controls);
    EnableWindow(GetDlgItem(m_hwnd, IDC_BROWSE_LOGOS_FOLDER), enable_folder_controls);
}

void artwork_advanced_preferences::update_control_states() {
    if (!m_hwnd) return;

    // Enable/disable noart image checkbox based on clear panel checkbox state
    BOOL clear_panel_enabled = (IsDlgButtonChecked(m_hwnd, IDC_CLEAR_PANEL_WHEN_NOT_PLAYING) == BST_CHECKED);
    EnableWindow(GetDlgItem(m_hwnd, IDC_USE_NOART_IMAGE), clear_panel_enabled);

    // Enable/disable folder controls based on current checkbox state (don't change checkbox)
    BOOL enable_folder_controls = (IsDlgButtonChecked(m_hwnd, IDC_ENABLE_CUSTOM_LOGOS) == BST_CHECKED);
    EnableWindow(GetDlgItem(m_hwnd, IDC_LOGOS_FOLDER_PATH), enable_folder_controls);
    EnableWindow(GetDlgItem(m_hwnd, IDC_BROWSE_LOGOS_FOLDER), enable_folder_controls);
}

void artwork_advanced_preferences::browse_for_folder() {
    pfc::string8 folder_path;
    if (uBrowseForFolder(m_hwnd, "Select Custom Station Logos Folder", folder_path)) {
        SetDlgItemTextA(m_hwnd, IDC_LOGOS_FOLDER_PATH, folder_path);
        on_changed();
    }
}

INT_PTR CALLBACK artwork_advanced_preferences::AdvancedConfigProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    artwork_advanced_preferences* pThis = nullptr;

    if (msg == WM_INITDIALOG) {
        pThis = reinterpret_cast<artwork_advanced_preferences*>(lp);
        SetWindowLongPtr(hwnd, DWLP_USER, lp);
        pThis->m_hwnd = hwnd;
        pThis->m_darkMode.AddDialogWithControls(hwnd);
        pThis->update_controls();
        return TRUE;
    }
    else {
        pThis = reinterpret_cast<artwork_advanced_preferences*>(GetWindowLongPtr(hwnd, DWLP_USER));
    }

    if (pThis) {
        switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_ENABLE_CUSTOM_LOGOS:
                if (HIWORD(wp) == BN_CLICKED) {
                    pThis->on_changed();
                    pThis->update_control_states();
                }
                break;

            case IDC_CLEAR_PANEL_WHEN_NOT_PLAYING:
                if (HIWORD(wp) == BN_CLICKED) {
                    pThis->on_changed();
                    pThis->update_control_states();  // Update control states when clear panel checkbox changes
                }
                break;

            case IDC_INFOBAR:
                if (HIWORD(wp) == BN_CLICKED) {
                    pThis->on_changed();
                    pThis->update_control_states();  // Update control states when clear panel checkbox changes
                }
                break;

            case IDC_USE_NOART_IMAGE:
                if (HIWORD(wp) == BN_CLICKED) {
                    pThis->on_changed();
                }
                break;

            case IDC_LOGOS_FOLDER_PATH:
                if (HIWORD(wp) == EN_CHANGE) {
                    pThis->on_changed();
                }
                break;

            case IDC_BROWSE_LOGOS_FOLDER:
                if (HIWORD(wp) == BN_CLICKED) {
                    pThis->browse_for_folder();
                }
                break;

            case IDC_HTTP_TIMEOUT:
                if (HIWORD(wp) == EN_CHANGE) {
                    pThis->on_changed();
                }
                break;

            case IDC_RETRY_COUNT:
                if (HIWORD(wp) == EN_CHANGE) {
                    pThis->on_changed();
                }
                break;
            }
            break;
        }
    }

    return FALSE;
}

//=============================================================================
// artwork_advanced_preferences_page - Advanced preferences page factory implementation
//=============================================================================

class artwork_advanced_preferences_page : public preferences_page_v3 {
public:
    const char* get_name() override;
    GUID get_guid() override;
    GUID get_parent_guid() override;
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override;
};

const char* artwork_advanced_preferences_page::get_name() {
    return "Advanced";
}

GUID artwork_advanced_preferences_page::get_guid() {
    return guid_preferences_page_advanced;
}

GUID artwork_advanced_preferences_page::get_parent_guid() {
    return guid_preferences_page_artwork;  // Make this a child of the main Artwork Display page
}

preferences_page_instance::ptr artwork_advanced_preferences_page::instantiate(HWND parent, preferences_page_callback::ptr callback) {
    auto instance = fb2k::service_new<artwork_advanced_preferences>(callback);

    HWND hwnd = CreateDialogParam(
        g_hIns,
        MAKEINTRESOURCE(IDD_PREFERENCES_ADVANCED),
        parent,
        artwork_advanced_preferences::AdvancedConfigProc,
        reinterpret_cast<LPARAM>(instance.get_ptr())
    );

    if (hwnd == nullptr) {
        throw exception_win32(GetLastError());
    }

    return instance;
}

// Service registration
static preferences_page_factory_t<artwork_preferences_page> g_artwork_preferences_page_factory;
static preferences_page_factory_t<artwork_advanced_preferences_page> g_artwork_advanced_preferences_page_factory;
