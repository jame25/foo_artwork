#pragma once
#include "stdafx.h"
#include "crypto_utils.h"
#include <vector>

// API types for priority ordering
enum class ApiType {
    iTunes = 0,
    Deezer = 1,
    LastFm = 2,
    MusicBrainz = 3,
    Discogs = 4
};

// External configuration variables
extern cfg_bool cfg_enable_itunes;
extern cfg_bool cfg_enable_discogs;
extern cfg_bool cfg_enable_lastfm;
extern cfg_bool cfg_enable_acrcloud;
extern cfg_string cfg_itunes_key;
extern cfg_string cfg_discogs_key;
extern cfg_string cfg_discogs_consumer_key;
extern cfg_string cfg_discogs_consumer_secret;
extern cfg_string cfg_lastfm_key;
extern cfg_string cfg_acrcloud_host;
extern cfg_string cfg_acrcloud_access_key;
extern cfg_string cfg_acrcloud_access_secret;
extern cfg_string cfg_acrcloud_host2;
extern cfg_string cfg_acrcloud_access_key2;
extern cfg_string cfg_acrcloud_access_secret2;
extern cfg_uint cfg_cache_size;

// Secure credential accessors (decrypt DPAPI ciphertext into memory)
pfc::string8 get_acrcloud_access_key();
pfc::string8 get_acrcloud_access_secret();
pfc::string8 get_acrcloud_access_key2();
pfc::string8 get_acrcloud_access_secret2();

// Automatic legacy migration helper
void migrate_credentials_to_encrypted();

// Priority order configuration
extern cfg_int cfg_search_order_1, cfg_search_order_2, cfg_search_order_3, cfg_search_order_4, cfg_search_order_5;

// Skip local artwork & Console logging mode settings
extern cfg_bool cfg_skip_local_artwork;
extern cfg_int cfg_console_logging_mode;

// Disk Cache settings
extern cfg_bool cfg_enable_disk_cache;
extern cfg_bool cfg_single_file_cache;
extern cfg_string cfg_cache_folder;

// No-Art Placeholder folder & cycling settings
extern cfg_string cfg_noart_folder;
extern cfg_int cfg_noart_cycle_mode; // 0 = Single / Disabled, 1 = Sequential, 2 = Random

// Miscellaneous settings
extern cfg_bool cfg_disable_instream_artwork;
extern cfg_string cfg_custom_blacklist;
extern cfg_bool cfg_trim_secondary_artists;

// Blacklist file & directory helpers
pfc::string8 get_artwork_data_path();
pfc::string8 get_blacklist_file_path();
pfc::string8 load_custom_blacklist_from_file();
void save_custom_blacklist_to_file(const char* content);
void sync_custom_blacklist_file();
pfc::string8 get_custom_blacklist_active_content();
const char* get_unified_default_blacklist_content();
void reset_custom_blacklist_to_defaults();
void open_blacklist_file(HWND parent = NULL);
void open_artwork_data_folder(HWND parent = NULL);

// Function to get API search order based on user preferences
std::vector<ApiType> get_api_search_order();

