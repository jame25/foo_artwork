#pragma once
#include "stdafx.h"
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
extern cfg_string cfg_lastfm_key;
extern cfg_string cfg_acrcloud_host;
extern cfg_string cfg_acrcloud_access_key;
extern cfg_string cfg_acrcloud_access_secret;
extern cfg_uint cfg_cache_size;

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

// Function to get API search order based on user preferences
std::vector<ApiType> get_api_search_order();
