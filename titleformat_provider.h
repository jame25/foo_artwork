#pragma once
#include "stdafx.h"

class titleformat_provider {
public:
    static void set_track_artwork_info(metadb_handle_ptr track, 
                                       const char* artist, 
                                       const char* title, 
                                       const char* cover_path, 
                                       const char* source,
                                       const char* artist_full = nullptr,
                                       const char* album = nullptr,
                                       const char* listeners = nullptr);
    static void clear_track_artwork_info();
    static void get_track_artwork_info(pfc::string8& out_artist, pfc::string8& out_title, pfc::string8& out_cover, pfc::string8& out_source);
    static void get_track_artwork_info_extended(pfc::string8& out_artist, pfc::string8& out_artist_full, pfc::string8& out_title, pfc::string8& out_album, pfc::string8& out_listeners, pfc::string8& out_cover, pfc::string8& out_source);
    static void set_status(const char* status);
    static void get_status(pfc::string8& out_status);
    static void set_status_artwork_loaded(const char* source, int width, int height, size_t size_bytes, bool used_acr = false);
    static void set_stream_track_timing(double duration_sec, double elapsed_sec = 0.0, bool has_duration = true, int coversync_sec = 0);
    static void reset_stream_track_timer(int coversync_sec = 0);
};
