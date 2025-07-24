#pragma once
#include "stdafx.h"
#include <functional>
#include <memory>
#include <thread>
#include <atomic>

class artwork_manager {
public:
    struct artwork_result {
        pfc::array_t<t_uint8> data;
        pfc::string8 mime_type;
        bool success;
        pfc::string8 error_message;
        
        artwork_result() : success(false) {}
    };

    // Callback for async artwork retrieval
    typedef std::function<void(const artwork_result&)> artwork_callback;

    // Main entry point for artwork retrieval - silent background operation
    static void get_artwork_async(metadb_handle_ptr track, artwork_callback callback);

private:
    // Background search function
    static void search_artwork_background(metadb_handle_ptr track, artwork_callback callback);
    
    // Local artwork search
    static bool find_local_artwork(const char* file_path, artwork_result& result);
    
    // API artwork search
    static bool search_itunes_api(const char* artist, const char* album, artwork_result& result);
    static bool search_discogs_api(const char* artist, const char* album, artwork_result& result);
    static bool search_lastfm_api(const char* artist, const char* album, artwork_result& result);
    
    // HTTP utilities with WinHTTP
    static bool download_image_winhttp(const char* url, artwork_result& result);
    
    // Cache management
    static bool get_from_cache(const char* cache_key, artwork_result& result);
    static void save_to_cache(const char* cache_key, const artwork_result& result);
    static pfc::string8 generate_cache_key(const char* artist, const char* album);
    
    // Utility functions
    static bool is_valid_image_data(const t_uint8* data, size_t size);
    static pfc::string8 detect_mime_type(const t_uint8* data, size_t size);
    static pfc::string8 get_file_directory(const char* file_path);
    static pfc::string8 url_encode(const char* str);
};
