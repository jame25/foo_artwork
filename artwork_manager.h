#pragma once
#include "stdafx.h"
#include "async_io_manager.h"
#include "preferences.h"
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
        pfc::string8 source;  // Source of the artwork (e.g., "iTunes", "Deezer", "Local file")
        
        artwork_result() : success(false) {}
    };

    // Callback for async artwork retrieval
    typedef std::function<void(const artwork_result&)> artwork_callback;

    // Main entry point for artwork retrieval - fully asynchronous operation
    static void get_artwork_async(metadb_handle_ptr track, artwork_callback callback);
    static void get_artwork_async_with_metadata(const char* artist, const char* track, artwork_callback callback);
    
    // Initialize/shutdown async I/O system
    static void initialize();
    static void shutdown();
    
    // Utility functions
    static pfc::string8 detect_mime_type(const t_uint8* data, size_t size);
    
private:
    // Internal async pipeline methods
    static void check_cache_async_metadata(const pfc::string8& cache_key, const pfc::string8& artist, const pfc::string8& track, artwork_callback callback);
    static void search_apis_async_metadata(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback);

private:
    // Async search pipeline
    static void search_artwork_pipeline(metadb_handle_ptr track, artwork_callback callback);
    static void check_cache_async(const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback);
    static void search_local_async(const pfc::string8& file_path, const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback);
    static void search_apis_async(const pfc::string8& artist, const pfc::string8& album, const pfc::string8& cache_key, artwork_callback callback);
    static void search_apis_by_priority(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback, const std::vector<ApiType>& api_order, size_t index);
    
    // Async local artwork search (uses SDK only)
    static void find_local_artwork_async(metadb_handle_ptr track, artwork_callback callback);
    
    // Async API artwork search  
    static void search_itunes_api_async(const char* artist, const char* track, artwork_callback callback);
    static void search_deezer_api_async(const char* artist, const char* track, artwork_callback callback);
    static void perform_deezer_fallback_search(const char* artist, const char* track, artwork_callback callback);
    static void search_discogs_api_async(const char* artist, const char* track, artwork_callback callback);
    static void search_lastfm_api_async(const char* artist, const char* track, artwork_callback callback);
    static void search_musicbrainz_api_async(const char* artist, const char* track, artwork_callback callback);
    
    // Async HTTP utilities
    static void download_image_async(const char* url, artwork_callback callback);
    
    // Helper functions for async operations
    static void validate_and_complete_result(const pfc::array_t<t_uint8>& data, artwork_callback callback);
    
    // Utility functions
    static bool is_valid_image_data(const t_uint8* data, size_t size);
    static bool is_supported_image_format(const pfc::string8& mime_type);
    static pfc::string8 get_file_directory(const char* file_path);
    static pfc::string8 url_encode(const char* str);
    static pfc::string8 generate_cache_key(const char* artist, const char* track);
    
    // JSON parsing functions
    static bool parse_itunes_json(const char* artist, const char* track, const pfc::string8& json, pfc::string8& artwork_url);
    static bool parse_deezer_json(const char* artist, const char* track, const pfc::string8& json, pfc::string8& artwork_url);
    static bool parse_lastfm_json(const pfc::string8& json, pfc::string8& artwork_url);
    static bool parse_discogs_json(const pfc::string8& json, pfc::string8& artwork_url);
    static bool parse_musicbrainz_json(const pfc::string8& json, pfc::string8& release_id);
    
    // Initialization flag
    static std::atomic<bool> initialized_;
};
