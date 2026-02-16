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
extern cfg_string cfg_itunes_key;
extern cfg_string cfg_discogs_key;
extern cfg_string cfg_lastfm_key;
extern cfg_uint cfg_cache_size;

// Priority order configuration
extern cfg_int cfg_search_order_1, cfg_search_order_2, cfg_search_order_3, cfg_search_order_4, cfg_search_order_5;

// Skip local artwork setting
extern cfg_bool cfg_skip_local_artwork;

// Function to get API search order based on user preferences
std::vector<ApiType> get_api_search_order();
