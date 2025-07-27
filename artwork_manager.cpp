#include "stdafx.h"
#include "artwork_manager.h"
#include "preferences.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <thread>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")

// External configuration variables
extern cfg_bool cfg_enable_itunes;
extern cfg_bool cfg_enable_discogs;
extern cfg_bool cfg_enable_lastfm;
extern cfg_bool cfg_enable_deezer;
extern cfg_bool cfg_enable_musicbrainz;
extern cfg_string cfg_itunes_key;
extern cfg_string cfg_discogs_key;
extern cfg_string cfg_lastfm_key;

// Static member initialization
std::atomic<bool> artwork_manager::initialized_(false);

void artwork_manager::initialize() {
    if (initialized_.exchange(true)) return; // Already initialized
    
    async_io_manager::instance().initialize(4); // 4 thread pool workers
}

void artwork_manager::shutdown() {
    if (!initialized_.exchange(false)) return; // Not initialized
    
    async_io_manager::instance().shutdown();
}

void artwork_manager::get_artwork_async(metadb_handle_ptr track, artwork_callback callback) {
    ASSERT_MAIN_THREAD();
    
    if (!initialized_) {
        console::print("foo_artwork: Initializing artwork manager");
        initialize();
    }
    
    console::print("foo_artwork: Starting artwork search pipeline");
    
    try {
        // Start the fully asynchronous pipeline
        search_artwork_pipeline(track, callback);
    } catch (const std::exception& e) {
        artwork_result result;
        result.success = false;
        result.error_message = e.what();
        callback(result);
    }
}

void artwork_manager::get_artwork_async_with_metadata(const char* artist, const char* track, artwork_callback callback) {
    ASSERT_MAIN_THREAD();
    
    if (!initialized_) {
        console::print("foo_artwork: Initializing artwork manager");
        initialize();
    }
    
    console::print("foo_artwork: Starting artwork search pipeline with explicit metadata");
    
    try {
        // Use explicit metadata instead of track metadata
        pfc::string8 artist_str = artist ? artist : "Unknown Artist";
        pfc::string8 track_str = track ? track : "Unknown Track";
        
        pfc::string8 search_msg = "foo_artwork: Searching APIs for artist='";
        search_msg << artist_str << "', track='" << track_str << "'";
        console::print(search_msg);
        
        pfc::string8 cache_key = generate_cache_key(artist_str, track_str);
        
        // Start async pipeline: Cache -> APIs (skip local files since we don't have a track)
        check_cache_async_metadata(cache_key, artist_str, track_str, callback);
    } catch (const std::exception& e) {
        artwork_result result;
        result.success = false;
        result.error_message = e.what();
        callback(result);
    }
}

void artwork_manager::search_artwork_pipeline(metadb_handle_ptr track, artwork_callback callback) {
    ASSERT_MAIN_THREAD();
    
    // Extract metadata on main thread (this is fast)
    metadb_info_container::ptr info_container = track->get_info_ref();
    const file_info* info = &info_container->info();
    
    pfc::string8 artist = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "Unknown Artist";
    pfc::string8 track_name = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "Unknown Track";
    pfc::string8 file_path = track->get_path();
    
    pfc::string8 cache_key = generate_cache_key(artist, track_name);
    
    // Start async pipeline: Cache -> Local -> APIs
    check_cache_async(cache_key, track, callback);
}

void artwork_manager::check_cache_async(const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback) {
    async_io_manager::instance().cache_get_async(cache_key, 
        [cache_key, track, callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0) {
                // Cache hit - validate and return
                validate_and_complete_result(data, callback);
            } else {
                // Cache miss - continue to local search
                pfc::string8 file_path = track->get_path();
                search_local_async(file_path, cache_key, track, callback);
            }
        });
}

void artwork_manager::check_cache_async_metadata(const pfc::string8& cache_key, const pfc::string8& artist, const pfc::string8& track, artwork_callback callback) {
    async_io_manager::instance().cache_get_async(cache_key, 
        [cache_key, artist, track, callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0) {
                // Cache hit - validate and return
                validate_and_complete_result(data, callback);
            } else {
                // Cache miss - skip local search and go directly to APIs
                search_apis_async_metadata(artist, track, cache_key, callback);
            }
        });
}

void artwork_manager::search_apis_async_metadata(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback) {
    // Use the existing search_apis_async function
    search_apis_async(artist, track, cache_key, callback);
}

void artwork_manager::search_local_async(const pfc::string8& file_path, const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback) {
    // Skip local search for non-local files
    if (file_path.is_empty() || pfc::string_find_first(file_path, "://", 1) != pfc_infinite) {
        // Not a local file - skip to API search
        metadb_info_container::ptr info_container = track->get_info_ref();
        const file_info* info = &info_container->info();
        pfc::string8 artist = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "Unknown Artist";
        pfc::string8 track_name = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "Unknown Track";
        search_apis_async(artist, track_name, cache_key, callback);
        return;
    }
    
    find_local_artwork_async(file_path, [cache_key, track, callback](const artwork_result& result) {
        if (result.success) {
            // Local artwork found - cache it and return
            async_io_manager::instance().cache_set_async(cache_key, result.data);
            callback(result);
        } else {
            // Local search failed - continue to API search
            metadb_info_container::ptr info_container = track->get_info_ref();
            const file_info* info = &info_container->info();
            pfc::string8 artist = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "Unknown Artist";
            pfc::string8 track_name = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "Unknown Track";
            search_apis_async(artist, track_name, cache_key, callback);
        }
    });
}

void artwork_manager::search_apis_async(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback) {
    // Try APIs in sequence: iTunes -> Deezer -> Last.fm -> MusicBrainz -> Discogs
    pfc::string8 debug_msg = "foo_artwork: Searching APIs for artist='";
    debug_msg << artist << "', track='" << track << "'";
    console::print(debug_msg);
    
    if (cfg_enable_itunes) {
        console::print("foo_artwork: Trying iTunes API");
    } else if (cfg_enable_deezer) {
        console::print("foo_artwork: Skipping iTunes, trying Deezer API");
        search_deezer_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
            if (result.success) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
                callback(result);
            } else if (cfg_enable_lastfm && !cfg_lastfm_key.is_empty()) {
                console::print("foo_artwork: Deezer failed, trying Last.fm API");
                search_lastfm_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                        callback(result);
                    } else if (cfg_enable_musicbrainz) {
                        console::print("foo_artwork: Last.fm failed, trying MusicBrainz API");
                        search_musicbrainz_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                            if (result.success) {
                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                                callback(result);
                            } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                                console::print("foo_artwork: MusicBrainz failed, trying Discogs API");
                                search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                                    if (result.success) {
                                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                                    }
                                    callback(result);
                                });
                            } else {
                                console::print("foo_artwork: No more APIs to try");
                                artwork_result final_result;
                                final_result.success = false;
                                final_result.error_message = "No artwork found";
                                callback(final_result);
                            }
                        });
                    } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                        console::print("foo_artwork: Last.fm failed, MusicBrainz disabled, trying Discogs API");
                        search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                            if (result.success) {
                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                            }
                            callback(result);
                        });
                    } else {
                        console::print("foo_artwork: No more APIs available");
                        artwork_result final_result;
                        final_result.success = false;
                        final_result.error_message = "No artwork found";
                        callback(final_result);
                    }
                });
            } else if (cfg_enable_musicbrainz) {
                console::print("foo_artwork: Deezer failed, Last.fm disabled, trying MusicBrainz API");
                search_musicbrainz_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                        callback(result);
                    } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                        console::print("foo_artwork: MusicBrainz failed, trying Discogs API");
                        search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                            if (result.success) {
                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                            }
                            callback(result);
                        });
                    } else {
                        console::print("foo_artwork: No more APIs to try");
                        artwork_result final_result;
                        final_result.success = false;
                        final_result.error_message = "No artwork found";
                        callback(final_result);
                    }
                });
            } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                console::print("foo_artwork: Deezer failed, other APIs disabled, trying Discogs API");
                search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                    }
                    callback(result);
                });
            } else {
                console::print("foo_artwork: All APIs disabled or failed");
                artwork_result final_result;
                final_result.success = false;
                final_result.error_message = "No artwork found";
                callback(final_result);
            }
        });
        return;
    }
    
    if (cfg_enable_itunes) {
        console::print("foo_artwork: Trying iTunes API");
        search_itunes_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
            if (result.success) {
                // iTunes success - cache and return
                async_io_manager::instance().cache_set_async(cache_key, result.data);
                callback(result);
            } else if (cfg_enable_deezer) {
                // iTunes failed - try Deezer
                search_deezer_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                        callback(result);
                    } else if (cfg_enable_lastfm && !cfg_lastfm_key.is_empty()) {
                        // Deezer failed - try Last.fm
                        search_lastfm_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                            if (result.success) {
                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                                callback(result);
                            } else if (cfg_enable_musicbrainz) {
                                // Last.fm failed - try MusicBrainz
                                search_musicbrainz_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                                    if (result.success) {
                                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                                        callback(result);
                                    } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                                        // MusicBrainz failed - try Discogs
                                        search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                                            if (result.success) {
                                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                                            }
                                            callback(result); // Final result regardless of success
                                        });
                                    } else {
                                        // No more APIs to try
                                        artwork_result final_result;
                                        final_result.success = false;
                                        final_result.error_message = "No artwork found";
                                        callback(final_result);
                                    }
                                });
                            } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                                // Last.fm failed, MusicBrainz disabled - try Discogs
                                search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                                    if (result.success) {
                                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                                    }
                                    callback(result); // Final result regardless of success
                                });
                            } else {
                                // No more APIs to try
                                artwork_result final_result;
                                final_result.success = false;
                                final_result.error_message = "No artwork found";
                                callback(final_result);
                            }
                        });
                    } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                        // Deezer failed, Last.fm disabled - try Discogs
                        search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                            if (result.success) {
                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                            }
                            callback(result);
                        });
                    } else {
                        // No more APIs available
                        artwork_result final_result;
                        final_result.success = false;
                        final_result.error_message = "No artwork found";
                        callback(final_result);
                    }
                });
            } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                // iTunes failed, Last.fm disabled - try Discogs
                search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                    }
                    callback(result);
                });
            } else {
                // No APIs available
                artwork_result final_result;
                final_result.success = false;
                final_result.error_message = "No artwork found";
                callback(final_result);
            }
        });
    } else if (cfg_enable_lastfm && !cfg_lastfm_key.is_empty()) {
        // iTunes disabled - start with Last.fm
        search_lastfm_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
            if (result.success) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
                callback(result);
            } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
                search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                    }
                    callback(result);
                });
            } else {
                artwork_result final_result;
                final_result.success = false;
                final_result.error_message = "No artwork found";
                callback(final_result);
            }
        });
    } else if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
        // Only Discogs available
        search_discogs_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
            if (result.success) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
            }
            callback(result);
        });
    } else {
        // No APIs enabled
        artwork_result final_result;
        final_result.success = false;
        final_result.error_message = "No online artwork sources enabled";
        callback(final_result);
    }
}

void artwork_manager::find_local_artwork_async(const char* file_path, artwork_callback callback) {
    pfc::string8 directory = get_file_directory(file_path);
    if (directory.is_empty()) {
        artwork_result result;
        result.success = false;
        result.error_message = "Invalid file path";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
        return;
    }
    
    // Scan directory for image files asynchronously
    async_io_manager::instance().scan_directory_async(directory, "*.jpg",
        [directory, callback](bool success, const std::vector<pfc::string8>& jpg_files, const pfc::string8& error) {
            if (!success) {
                artwork_result result;
                result.success = false;
                result.error_message = "Failed to scan directory: ";
        result.error_message << error;
                callback(result);
                return;
            }
            
            // Also scan for PNG files
            async_io_manager::instance().scan_directory_async(directory, "*.png",
                [directory, jpg_files, callback](bool success, const std::vector<pfc::string8>& png_files, const pfc::string8& error) {
                    std::vector<pfc::string8> all_files = jpg_files;
                    if (success) {
                        all_files.insert(all_files.end(), png_files.begin(), png_files.end());
                    }
                    
                    // Also scan for JPEG files
                    async_io_manager::instance().scan_directory_async(directory, "*.jpeg",
                        [directory, all_files, callback](bool success, const std::vector<pfc::string8>& jpeg_files, const pfc::string8& error) {
                            std::vector<pfc::string8> final_files = all_files;
                            if (success) {
                                final_files.insert(final_files.end(), jpeg_files.begin(), jpeg_files.end());
                            }
                            
                            if (final_files.empty()) {
                                artwork_result result;
                                result.success = false;
                                result.error_message = "No image files found";
                                callback(result);
                            } else {
                                // Process files asynchronously
                                process_local_files(final_files, callback, 0);
                            }
                        });
                });
        });
}

void artwork_manager::process_local_files(const std::vector<pfc::string8>& files, artwork_callback callback, size_t index) {
    if (index >= files.size()) {
        // No more files to process
        artwork_result result;
        result.success = false;
        result.error_message = "No valid image files found";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
        return;
    }
    
    // Try to read the current file
    async_io_manager::instance().read_file_async(files[index],
        [files, callback, index](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0 && is_valid_image_data(data.get_ptr(), data.get_size())) {
                // Valid image found
                artwork_result result;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.success = true;
                result.source = "Local file";  // Set source for OSD display
                callback(result);
            } else {
                // Try next file
                process_local_files(files, callback, index + 1);
            }
        });
}

void artwork_manager::search_itunes_api_async(const char* artist, const char* album, artwork_callback callback) {
    // iTunes Search API doesn't require an API key
    pfc::string8 url = "https://itunes.apple.com/search?term=";
    url << url_encode(artist) << "+" << url_encode(album);
    url << "&entity=album&limit=1";
    
    // Make async HTTP request
    async_io_manager::instance().http_get_async(url, [callback](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            artwork_result result;
            result.success = false;
            result.error_message = "iTunes API request failed: ";
            result.error_message << error;
            callback(result);
            return;
        }
        
        // Parse JSON response to extract artwork URL
        pfc::string8 artwork_url;
        if (!parse_itunes_json(response, artwork_url)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in iTunes response";
            callback(result);
            return;
        }
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "iTunes";  // Set source for OSD display
            } else {
                result.success = false;
                result.error_message = "Failed to download iTunes artwork: ";
                result.error_message << error;
            }
            callback(result);
        });
    });
}

void artwork_manager::search_discogs_api_async(const char* artist, const char* album, artwork_callback callback) {
    if (cfg_discogs_key.is_empty()) {
        async_io_manager::instance().post_to_main_thread([callback]() {
            artwork_result result;
            result.success = false;
            result.error_message = "Discogs API key not configured";
            callback(result);
        });
        return;
    }
    
    // Build Discogs API URL
    pfc::string8 search_query = artist;
    search_query << " " << album;
    
    pfc::string8 url = "https://api.discogs.com/database/search?q=";
    url << url_encode(search_query);
    url << "&type=release&token=" << url_encode(cfg_discogs_key.get_ptr());
    
    // Make async HTTP request
    async_io_manager::instance().http_get_async(url, [callback](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            artwork_result result;
            result.success = false;
            result.error_message = "Discogs API request failed: ";
            result.error_message << error;
            callback(result);
            return;
        }
        
        // Parse JSON response to extract artwork URL
        pfc::string8 artwork_url;
        if (!parse_discogs_json(response, artwork_url)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in Discogs response";
            callback(result);
            return;
        }
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "Discogs";  // Set source for OSD display
            } else {
                result.success = false;
                result.error_message = "Failed to download Discogs artwork: ";
                result.error_message << error;
            }
            callback(result);
        });
    });
}

void artwork_manager::search_lastfm_api_async(const char* artist, const char* album, artwork_callback callback) {
    if (cfg_lastfm_key.is_empty()) {
        async_io_manager::instance().post_to_main_thread([callback]() {
            artwork_result result;
            result.success = false;
            result.error_message = "Last.fm API key not configured";
            callback(result);
        });
        return;
    }
    
    // Build Last.fm API URL
    pfc::string8 url = "http://ws.audioscrobbler.com/2.0/?method=album.getinfo&api_key=";
    url << url_encode(cfg_lastfm_key.get_ptr());
    url << "&artist=" << url_encode(artist);
    url << "&album=" << url_encode(album);
    url << "&format=json";
    
    // Make async HTTP request
    async_io_manager::instance().http_get_async(url, [callback](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            artwork_result result;
            result.success = false;
            result.error_message = "Last.fm API request failed: ";
            result.error_message << error;
            callback(result);
            return;
        }
        
        // Parse JSON response to extract artwork URL
        pfc::string8 artwork_url;
        if (!parse_lastfm_json(response, artwork_url)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in Last.fm response";
            callback(result);
            return;
        }
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "Last.fm";  // Set source for OSD display
            } else {
                result.success = false;
                result.error_message = "Failed to download Last.fm artwork: ";
                result.error_message << error;
            }
            callback(result);
        });
    });
}

void artwork_manager::search_deezer_api_async(const char* artist, const char* track, artwork_callback callback) {
    // Deezer API doesn't require authentication
    pfc::string8 search_query;
    if (!artist || strlen(artist) == 0) {
        search_query = track;
    } else {
        // Build search query: "artist track"
        search_query = artist;
        search_query << " " << track;
    }
    
    pfc::string8 url = "https://api.deezer.com/search/track?q=";
    url << url_encode(search_query) << "&limit=10";
    
    pfc::string8 debug_msg = "foo_artwork: Deezer API request: ";
    debug_msg << url;
    console::print(debug_msg);
    
    // Make async HTTP request
    async_io_manager::instance().http_get_async(url, [callback](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            pfc::string8 debug_msg = "foo_artwork: Deezer API request failed: ";
            debug_msg << error;
            console::print(debug_msg);
            
            artwork_result result;
            result.success = false;
            result.error_message = "Deezer API request failed: ";
            result.error_message << error;
            callback(result);
            return;
        }
        
        console::print("foo_artwork: Deezer API request successful, parsing JSON");
        
        // Parse JSON response to extract artwork URL
        pfc::string8 artwork_url;
        if (!parse_deezer_json(response, artwork_url)) {
            console::print("foo_artwork: Failed to parse Deezer JSON response");
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in Deezer response";
            callback(result);
            return;
        }
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "Deezer";  // Set source for OSD display
            } else {
                result.success = false;
                result.error_message = "Failed to download Deezer artwork: ";
                result.error_message << error;
            }
            callback(result);
        });
    });
}

void artwork_manager::download_image_async(const char* url, artwork_callback callback) {
    // This would be called by API implementations after parsing JSON responses
    // For now, placeholder implementation
    async_io_manager::instance().post_to_main_thread([callback]() {
        artwork_result result;
        result.success = false;
        result.error_message = "Image download not implemented";
        callback(result);
    });
}

void artwork_manager::validate_and_complete_result(const pfc::array_t<t_uint8>& data, artwork_callback callback) {
    if (data.get_size() == 0) {
        artwork_result result;
        result.success = false;
        result.error_message = "Empty data";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
        return;
    }
    
    if (!is_valid_image_data(data.get_ptr(), data.get_size())) {
        artwork_result result;
        result.success = false;
        result.error_message = "Invalid image data";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
        return;
    }
    
    artwork_result result;
    result.data = data;
    result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
    result.success = true;
    result.source = "Cache";  // Set source for cached artwork
    
    async_io_manager::instance().post_to_main_thread([callback, result]() {
        callback(result);
    });
}

bool artwork_manager::is_valid_image_data(const t_uint8* data, size_t size) {
    if (size < 4) return false;
    
    // Check for common image format signatures
    // JPEG
    if (data[0] == 0xFF && data[1] == 0xD8) return true;
    
    // PNG
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return true;
    
    // GIF
    if (size >= 6 && memcmp(data, "GIF87a", 6) == 0) return true;
    if (size >= 6 && memcmp(data, "GIF89a", 6) == 0) return true;
    
    // BMP
    if (data[0] == 'B' && data[1] == 'M') return true;
    
    return false;
}

pfc::string8 artwork_manager::detect_mime_type(const t_uint8* data, size_t size) {
    if (size < 4) return "application/octet-stream";
    
    // JPEG
    if (data[0] == 0xFF && data[1] == 0xD8) return "image/jpeg";
    
    // PNG
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return "image/png";
    
    // GIF
    if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) return "image/gif";
    
    // BMP
    if (data[0] == 'B' && data[1] == 'M') return "image/bmp";
    
    return "application/octet-stream";
}

pfc::string8 artwork_manager::get_file_directory(const char* file_path) {
    pfc::string8 directory = file_path;
    
    // Find last backslash or forward slash
    t_size pos = directory.find_last('\\');
    if (pos == pfc_infinite) {
        pos = directory.find_last('/');
    }
    
    if (pos != pfc_infinite) {
        directory.truncate(pos);
        return directory;
    }
    
    return pfc::string8();
}

pfc::string8 artwork_manager::url_encode(const char* str) {
    pfc::string8 result;
    
    for (const char* p = str; *p; ++p) {
        char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result.add_char(c);
        } else if (c == ' ') {
            result << "+";
        } else {
            result << "%" << pfc::format_hex((unsigned char)c, 2);
        }
    }
    
    return result;
}

pfc::string8 artwork_manager::generate_cache_key(const char* artist, const char* track) {
    pfc::string8 key = artist;
    key << "_" << track;
    
    // Replace invalid filename characters
    key.replace_char('\\', '_');
    key.replace_char('/', '_');
    key.replace_char(':', '_');
    key.replace_char('*', '_');
    key.replace_char('?', '_');
    key.replace_char('"', '_');
    key.replace_char('<', '_');
    key.replace_char('>', '_');
    key.replace_char('|', '_');
    
    return key;
}

// JSON parsing implementations
bool artwork_manager::parse_itunes_json(const pfc::string8& json, pfc::string8& artwork_url) {
    // Simple JSON parsing for iTunes response
    // Look for "artworkUrl512" or "artworkUrl100" field
    const char* json_str = json.get_ptr();
    
    // Try artworkUrl512 first (higher quality)
    const char* url512_pos = strstr(json_str, "\"artworkUrl512\":");
    if (url512_pos) {
        const char* url_start = strchr(url512_pos, '"');
        if (url_start) {
            url_start++; // Skip opening quote
            const char* url_end = strchr(url_start, '"');
            if (url_end && url_end > url_start) {
                artwork_url = pfc::string8(url_start, url_end - url_start);
                return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
            }
        }
    }
    
    // Fallback to artworkUrl100
    const char* url100_pos = strstr(json_str, "\"artworkUrl100\":");
    if (url100_pos) {
        const char* url_start = strchr(url100_pos, '"');
        if (url_start) {
            url_start++; // Skip opening quote
            const char* url_end = strchr(url_start, '"');
            if (url_end && url_end > url_start) {
                artwork_url = pfc::string8(url_start, url_end - url_start);
                return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
            }
        }
    }
    
    return false;
}

bool artwork_manager::parse_deezer_json(const pfc::string8& json, pfc::string8& artwork_url) {
    // Simple JSON parsing for Deezer response
    // Look for "cover_xl" field in album data
    const char* json_str = json.get_ptr();
    
    // Debug: log the JSON response to understand its structure
    pfc::string8 debug_msg = "foo_artwork: Deezer JSON response: ";
    debug_msg << pfc::string8(json_str, pfc::min_t<size_t>(500, strlen(json_str))); // First 500 chars
    console::print(debug_msg);
    
    // Look for data array first, then find album within the first result
    const char* data_pos = strstr(json_str, "\"data\":[");
    if (!data_pos) {
        console::print("foo_artwork: No 'data' array found in Deezer response");
        return false;
    }
    
    // Find first album object within data array
    const char* album_pos = strstr(data_pos, "\"album\":");
    if (!album_pos) {
        console::print("foo_artwork: No 'album' field found in Deezer response data");
        return false;
    }
    
    // Look for cover_xl within album section
    const char* cover_pos = strstr(album_pos, "\"cover_xl\":");
    if (cover_pos) {
        // Skip past "cover_xl": and find the opening quote of the value
        const char* colon_pos = strchr(cover_pos, ':');
        if (colon_pos) {
            const char* url_start = strchr(colon_pos, '"');
            if (url_start) {
                url_start++; // Skip opening quote
                const char* url_end = strchr(url_start, '"');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start, url_end - url_start);
                    
                    pfc::string8 raw_msg = "foo_artwork: Raw extracted URL: ";
                    raw_msg << artwork_url;
                    console::print(raw_msg);
                    
                    // Unescape JSON slashes (replace \/ with /)
                    pfc::string8 unescaped_url;
                    const char* src = artwork_url.get_ptr();
                    while (*src) {
                        if (*src == '\\' && *(src + 1) == '/') {
                            unescaped_url += "/";
                            src += 2; // Skip both \ and /
                        } else {
                            char single_char[2] = {*src, '\0'};
                            unescaped_url += single_char;
                            src++;
                        }
                    }
                    artwork_url = unescaped_url;
                    
                    pfc::string8 found_msg = "foo_artwork: Found cover_xl URL (unescaped): ";
                    found_msg << artwork_url;
                    console::print(found_msg);
                    return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
                }
            }
        }
    } else {
        console::print("foo_artwork: No 'cover_xl' field found in album section");
    }
    
    // Fallback to cover_big
    const char* cover_big_pos = strstr(album_pos, "\"cover_big\":");
    if (cover_big_pos) {
        // Skip past "cover_big": and find the opening quote of the value
        const char* colon_pos = strchr(cover_big_pos, ':');
        if (colon_pos) {
            const char* url_start = strchr(colon_pos, '"');
            if (url_start) {
                url_start++; // Skip opening quote
                const char* url_end = strchr(url_start, '"');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start, url_end - url_start);
                    
                    // Unescape JSON slashes (replace \/ with /)
                    pfc::string8 unescaped_url;
                    const char* src = artwork_url.get_ptr();
                    while (*src) {
                        if (*src == '\\' && *(src + 1) == '/') {
                            unescaped_url += "/";
                            src += 2; // Skip both \ and /
                        } else {
                            char single_char[2] = {*src, '\0'};
                            unescaped_url += single_char;
                            src++;
                        }
                    }
                    artwork_url = unescaped_url;
                    
                    pfc::string8 cover_big_msg = "foo_artwork: Found cover_big URL (unescaped): ";
                    cover_big_msg << artwork_url;
                    console::print(cover_big_msg);
                    return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
                }
            }
        }
    }
    
    return false;
}

bool artwork_manager::parse_lastfm_json(const pfc::string8& json, pfc::string8& artwork_url) {
    // Simple JSON parsing for Last.fm response
    // Look for image array with size "extralarge" or "large"
    const char* json_str = json.get_ptr();
    
    // Look for image array
    const char* image_pos = strstr(json_str, "\"image\":");
    if (!image_pos) return false;
    
    // Try to find extralarge image first
    const char* search_pos = image_pos;
    while ((search_pos = strstr(search_pos, "\"size\":\"extralarge\"")) != nullptr) {
        // Look backwards for the URL in this image object
        const char* url_search = search_pos;
        while (url_search > image_pos && *url_search != '{') url_search--;
        
        const char* url_pos = strstr(url_search, "\"#text\":");
        if (url_pos && url_pos < search_pos) {
            const char* url_start = strchr(url_pos, '"');
            if (url_start) {
                url_start++; // Skip opening quote
                const char* url_end = strchr(url_start, '"');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start, url_end - url_start);
                    if (!artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr()) {
                        return true;
                    }
                }
            }
        }
        search_pos++;
    }
    
    // Fallback to large image
    search_pos = image_pos;
    while ((search_pos = strstr(search_pos, "\"size\":\"large\"")) != nullptr) {
        const char* url_search = search_pos;
        while (url_search > image_pos && *url_search != '{') url_search--;
        
        const char* url_pos = strstr(url_search, "\"#text\":");
        if (url_pos && url_pos < search_pos) {
            const char* url_start = strchr(url_pos, '"');
            if (url_start) {
                url_start++; // Skip opening quote
                const char* url_end = strchr(url_start, '"');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start, url_end - url_start);
                    if (!artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr()) {
                        return true;
                    }
                }
            }
        }
        search_pos++;
    }
    
    return false;
}

bool artwork_manager::parse_discogs_json(const pfc::string8& json, pfc::string8& artwork_url) {
    // Simple JSON parsing for Discogs response
    // Look for "thumb" or "cover_image" field in results
    const char* json_str = json.get_ptr();
    
    // Look for results array
    const char* results_pos = strstr(json_str, "\"results\":");
    if (!results_pos) return false;
    
    // Try cover_image first (higher quality)
    const char* cover_pos = strstr(results_pos, "\"cover_image\":");
    if (cover_pos) {
        const char* url_start = strchr(cover_pos, '"');
        if (url_start) {
            url_start++; // Skip opening quote
            const char* url_end = strchr(url_start, '"');
            if (url_end && url_end > url_start) {
                artwork_url = pfc::string8(url_start, url_end - url_start);
                if (!artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr()) {
                    return true;
                }
            }
        }
    }
    
    // Fallback to thumb
    const char* thumb_pos = strstr(results_pos, "\"thumb\":");
    if (thumb_pos) {
        const char* url_start = strchr(thumb_pos, '"');
        if (url_start) {
            url_start++; // Skip opening quote
            const char* url_end = strchr(url_start, '"');
            if (url_end && url_end > url_start) {
                artwork_url = pfc::string8(url_start, url_end - url_start);
                return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
            }
        }
    }
    
    return false;
}

void artwork_manager::search_musicbrainz_api_async(const char* artist, const char* album, artwork_callback callback) {
    // MusicBrainz does not require authentication but uses a two-step process:
    // 1. Search for release ID
    // 2. Get cover art from Cover Art Archive
    
    pfc::string8 search_query = "artist:\"";
    search_query << artist << "\" AND release:\"" << album << "\"";
    
    pfc::string8 url = "http://musicbrainz.org/ws/2/release/?query=";
    url << url_encode(search_query);
    url << "&fmt=json&limit=1";
    
    // Make async HTTP request to get release ID
    async_io_manager::instance().http_get_async(url, [callback](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            artwork_result result;
            result.success = false;
            result.error_message = "MusicBrainz API request failed: ";
            result.error_message << error;
            callback(result);
            return;
        }
        
        // Parse JSON response to extract release ID
        pfc::string8 release_id;
        if (!parse_musicbrainz_json(response, release_id)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No release found in MusicBrainz response";
            callback(result);
            return;
        }
        
        // Now get artwork from Cover Art Archive
        pfc::string8 coverart_url = "http://coverartarchive.org/release/";
        coverart_url << release_id << "/front";
        
        // Download the artwork image (Cover Art Archive redirects to actual image)
        async_io_manager::instance().http_get_binary_async(coverart_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "MusicBrainz";  // Set source for OSD display
            } else {
                result.success = false;
                result.error_message = "Failed to download MusicBrainz artwork: ";
                result.error_message << error;
            }
            callback(result);
        });
    });
}

bool artwork_manager::parse_musicbrainz_json(const pfc::string8& json, pfc::string8& release_id) {
    // Simple JSON parsing for MusicBrainz response
    // Look for first release ID in releases array
    const char* json_str = json.get_ptr();
    
    // Look for releases array
    const char* releases_pos = strstr(json_str, "\"releases\":");
    if (!releases_pos) return false;
    
    // Look for first id field after releases
    const char* id_pos = strstr(releases_pos, "\"id\":");
    if (id_pos) {
        const char* id_start = strchr(id_pos, '"');
        if (id_start) {
            id_start++; // Skip opening quote
            const char* id_end = strchr(id_start, '"');
            if (id_end && id_end > id_start) {
                release_id = pfc::string8(id_start, id_end - id_start);
                return !release_id.is_empty();
            }
        }
    }
    
    return false;
}
