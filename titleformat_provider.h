#pragma once
#include "stdafx.h"

class titleformat_provider {
public:
    static void set_track_artwork_info(metadb_handle_ptr track, const char* artist, const char* title, const char* cover_path, const char* source);
    static void clear_track_artwork_info();
    static void get_track_artwork_info(pfc::string8& out_artist, pfc::string8& out_title, pfc::string8& out_cover, pfc::string8& out_source);
};
