#include "stdafx.h"
#include "resource.h"

// Reference to configuration variables defined in sdk_main.cpp
extern cfg_bool cfg_enable_itunes, cfg_enable_discogs, cfg_enable_lastfm, cfg_enable_deezer, cfg_enable_musicbrainz;
extern cfg_string cfg_discogs_key, cfg_discogs_consumer_key, cfg_discogs_consumer_secret, cfg_lastfm_key;
extern cfg_int cfg_priority_1, cfg_priority_2, cfg_priority_3, cfg_priority_4, cfg_priority_5;
extern cfg_int cfg_stream_delay;

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
        
        // Set current priority selections
        // Convert from "What position is API X in?" to "Which API is at position Y?"
        int apis_at_position[5] = {-1, -1, -1, -1, -1};
        
        // Map each API to its position
        if (cfg_priority_2 >= 0 && cfg_priority_2 < 5) apis_at_position[cfg_priority_2] = 0; // iTunes
        if (cfg_priority_1 >= 0 && cfg_priority_1 < 5) apis_at_position[cfg_priority_1] = 1; // Deezer
        if (cfg_priority_3 >= 0 && cfg_priority_3 < 5) apis_at_position[cfg_priority_3] = 2; // LastFm
        if (cfg_priority_4 >= 0 && cfg_priority_4 < 5) apis_at_position[cfg_priority_4] = 3; // MusicBrainz
        if (cfg_priority_5 >= 0 && cfg_priority_5 < 5) apis_at_position[cfg_priority_5] = 4; // Discogs
        
        // Set dropdown selections
        if (apis_at_position[0] >= 0) SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_1), CB_SETCURSEL, api_value_to_dropdown_index(apis_at_position[0]), 0);
        if (apis_at_position[1] >= 0) SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_2), CB_SETCURSEL, api_value_to_dropdown_index(apis_at_position[1]), 0);
        if (apis_at_position[2] >= 0) SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_3), CB_SETCURSEL, api_value_to_dropdown_index(apis_at_position[2]), 0);
        if (apis_at_position[3] >= 0) SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_4), CB_SETCURSEL, api_value_to_dropdown_index(apis_at_position[3]), 0);
        if (apis_at_position[4] >= 0) SendMessage(GetDlgItem(hwnd, IDC_PRIORITY_5), CB_SETCURSEL, api_value_to_dropdown_index(apis_at_position[4]), 0);
        
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
    
    // Check priority comboboxes
    // Convert current UI state to config format and compare
    int current_positions[5] = {-1, -1, -1, -1, -1}; // iTunes, Deezer, LastFm, MusicBrainz, Discogs
    
    int current_api_at_pos0 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_GETCURSEL, 0, 0));
    int current_api_at_pos1 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_GETCURSEL, 0, 0));
    int current_api_at_pos2 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_GETCURSEL, 0, 0));
    int current_api_at_pos3 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_GETCURSEL, 0, 0));
    int current_api_at_pos4 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_GETCURSEL, 0, 0));
    
    if (current_api_at_pos0 >= 0 && current_api_at_pos0 < 5) current_positions[current_api_at_pos0] = 0;
    if (current_api_at_pos1 >= 0 && current_api_at_pos1 < 5) current_positions[current_api_at_pos1] = 1;
    if (current_api_at_pos2 >= 0 && current_api_at_pos2 < 5) current_positions[current_api_at_pos2] = 2;
    if (current_api_at_pos3 >= 0 && current_api_at_pos3 < 5) current_positions[current_api_at_pos3] = 3;
    if (current_api_at_pos4 >= 0 && current_api_at_pos4 < 5) current_positions[current_api_at_pos4] = 4;
    
    bool priority1_changed = current_positions[1] != cfg_priority_1; // Deezer
    bool priority2_changed = current_positions[0] != cfg_priority_2; // iTunes
    bool priority3_changed = current_positions[2] != cfg_priority_3; // LastFm
    bool priority4_changed = current_positions[3] != cfg_priority_4; // MusicBrainz
    bool priority5_changed = current_positions[4] != cfg_priority_5; // Discogs
    
    // Check stream delay
    BOOL success;
    int stream_delay_value = GetDlgItemInt(m_hwnd, IDC_STREAM_DELAY, &success, FALSE);
    bool stream_delay_changed = success && (stream_delay_value != cfg_stream_delay);
    
    return itunes_changed || discogs_changed || lastfm_changed || deezer_changed || musicbrainz_changed ||
           discogs_key_changed || discogs_consumer_key_changed || 
           discogs_consumer_secret_changed || lastfm_key_changed ||
           priority1_changed || priority2_changed || priority3_changed || priority4_changed || priority5_changed ||
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
        
        // Save priority combobox selections 
        // The UI shows: Priority 1 = "Which API is first?", Priority 2 = "Which API is second?", etc.
        // We need to convert this to: cfg_priority_X = "What position is API X in?"
        
        int priority_positions[5] = {-1, -1, -1, -1, -1}; // iTunes, Deezer, LastFm, MusicBrainz, Discogs
        
        // Get which API is selected for each priority position
        int api_at_pos0 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_GETCURSEL, 0, 0));
        int api_at_pos1 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_GETCURSEL, 0, 0));
        int api_at_pos2 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_GETCURSEL, 0, 0));
        int api_at_pos3 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_GETCURSEL, 0, 0));
        int api_at_pos4 = dropdown_index_to_api_value(SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_GETCURSEL, 0, 0));
        
        // Set the position for each API
        if (api_at_pos0 >= 0 && api_at_pos0 < 5) priority_positions[api_at_pos0] = 0;
        if (api_at_pos1 >= 0 && api_at_pos1 < 5) priority_positions[api_at_pos1] = 1;
        if (api_at_pos2 >= 0 && api_at_pos2 < 5) priority_positions[api_at_pos2] = 2;
        if (api_at_pos3 >= 0 && api_at_pos3 < 5) priority_positions[api_at_pos3] = 3;
        if (api_at_pos4 >= 0 && api_at_pos4 < 5) priority_positions[api_at_pos4] = 4;
        
        // Save to config variables
        cfg_priority_2 = priority_positions[0]; // iTunes position
        cfg_priority_1 = priority_positions[1]; // Deezer position  
        cfg_priority_3 = priority_positions[2]; // LastFm position
        cfg_priority_4 = priority_positions[3]; // MusicBrainz position
        cfg_priority_5 = priority_positions[4]; // Discogs position
        
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
        
        // Reset priority comboboxes to default order
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_1), CB_SETCURSEL, 1, 0);  // Deezer
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_2), CB_SETCURSEL, 0, 0);  // iTunes
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_3), CB_SETCURSEL, 2, 0);  // Last.fm
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_4), CB_SETCURSEL, 3, 0);  // MusicBrainz
        SendMessage(GetDlgItem(m_hwnd, IDC_PRIORITY_5), CB_SETCURSEL, 4, 0);  // Discogs
        
        // Reset stream delay to default (1 second)
        SetDlgItemInt(m_hwnd, IDC_STREAM_DELAY, 1, FALSE);
        SendMessage(GetDlgItem(m_hwnd, IDC_STREAM_DELAY_SPIN), UDM_SETPOS, 0, 1);
        
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
