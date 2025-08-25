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
    
    // Debug: Track artwork loading
    
    if (!initialized_) {
        initialize();
    }
    
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
        initialize();
    }
    
    try {
        // Use explicit metadata instead of track metadata
        pfc::string8 artist_str = artist ? artist : "Unknown Artist";
        pfc::string8 track_str = track ? track : "Unknown Track";
        
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
    
    // Extract metadata and path on main thread (this is fast)
    metadb_info_container::ptr info_container = track->get_info_ref();
    const file_info* info = &info_container->info();
    
    pfc::string8 artist = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "Unknown Artist";
    pfc::string8 track_name = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "Unknown Track";
    pfc::string8 file_path = track->get_path();
    
    pfc::string8 cache_key = generate_cache_key(artist, track_name);
    
    // CACHE SKIP FOR INTERNET STREAMS: For internet streams, metadata is often wrong initially
    // (belongs to previous track), causing wrong cached artwork to be returned.
    // Skip cache and go directly to local (tagged) artwork search.
    bool is_internet_stream = (strstr(file_path.c_str(), "://") && 
                              !(strstr(file_path.c_str(), "file://") == file_path.c_str()));
    
    if (is_internet_stream) {
        // Skip cache for internet streams - go directly to local artwork search
        search_local_async(file_path, cache_key, track, callback);
    } else {
        // For local files, use normal cache -> local -> APIs pipeline
        check_cache_async(cache_key, track, callback);
    }
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
    // TEMPORARY FIX: Skip cache entirely and go directly to APIs
    search_apis_async_metadata(artist, track, cache_key, callback);
    
    /* ORIGINAL CACHE CODE - TEMPORARILY DISABLED
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
    */
}

void artwork_manager::search_apis_async_metadata(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback) {
    // Use the existing search_apis_async function
    search_apis_async(artist, track, cache_key, callback);
}

void artwork_manager::search_local_async(const pfc::string8& file_path, const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback) {
    
    // ALWAYS try to find tagged artwork first, regardless of local/internet file type
    // Internet streams (like YouTube videos) can have embedded artwork too
    
    find_local_artwork_async(track, [cache_key, track, callback](const artwork_result& result) {
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
    
    if (cfg_enable_itunes) {
        // iTunes API implementation would go here
        artwork_result result;
        result.success = false;
        result.error_message = "iTunes API not implemented";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
    } else if (cfg_enable_deezer) {
        search_deezer_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
            if (result.success) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
                callback(result);
            } else if (cfg_enable_lastfm && !cfg_lastfm_key.is_empty()) {
                search_lastfm_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
                    if (result.success) {
                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                        callback(result);
                    } else if (cfg_enable_musicbrainz) {
                        search_musicbrainz_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
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
            } else if (cfg_enable_musicbrainz) {
                search_musicbrainz_api_async(artist, track, [cache_key, artist, track, callback](const artwork_result& result) {
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
        return;
    }
    
    if (cfg_enable_itunes) {
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

void artwork_manager::find_local_artwork_async(metadb_handle_ptr track, artwork_callback callback) {
    // Use album_art_manager_v2 from SDK exclusively - no custom logic
    
    async_io_manager::instance().submit_task([track, callback]() {
        artwork_result result;
        result.success = false;
        
        try {
            if (!track.is_valid()) {
                result.error_message = "Invalid metadb handle";
                async_io_manager::instance().post_to_main_thread([callback, result]() {
                    callback(result);
                });
                return;
            }
            
            // Try multiple artwork IDs in priority order to find any available tagged artwork
            const GUID artwork_ids[] = {
                album_art_ids::cover_front,  // Front cover (most common)
                album_art_ids::disc,         // Disc/media artwork
                album_art_ids::artist,       // Artist image
                album_art_ids::icon,         // Icon artwork
                album_art_ids::cover_back    // Back cover (least preferred)
            };
            
            const char* artwork_names[] = {
                "Front Cover",
                "Disc/Media",
                "Artist Image", 
                "Icon",
                "Back Cover"
            };
            
            static_api_ptr_t<album_art_manager_v2> aam;
            
            // Try each artwork ID until we find one
            for (int i = 0; i < 5; i++) {
                try {
                    auto extractor = aam->open(pfc::list_single_ref_t<metadb_handle_ptr>(track),
                                             pfc::list_single_ref_t<GUID>(artwork_ids[i]),
                                             fb2k::noAbort);
                    
                    auto art_data = extractor->query(artwork_ids[i], fb2k::noAbort);
                    if (art_data.is_valid() && art_data->get_size() > 0) {
                        result.success = true;
                        result.data.set_size(art_data->get_size());
                        memcpy(result.data.get_ptr(), art_data->get_ptr(), art_data->get_size());
                        result.mime_type = detect_mime_type(result.data.get_ptr(), result.data.get_size());
                        result.source = "Local artwork";
                        
                        async_io_manager::instance().post_to_main_thread([callback, result]() {
                            callback(result);
                        });
                        return;
                    }
                } catch (...) {
                    // Continue to next artwork ID if this one fails
                    continue;
                }
            }
        } catch (const std::exception& e) {
            result.error_message = "SDK artwork search exception";
        } catch (...) {
            result.error_message = "SDK artwork search failed with unknown exception";
        }
        
        // No artwork found via SDK
        result.error_message = "No artwork found via SDK";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
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

void artwork_manager::search_lastfm_api_async(const char* artist, const char* title, artwork_callback callback) {
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
    pfc::string8 url = "http://ws.audioscrobbler.com/2.0/?method=track.getinfo&api_key=";
    url << url_encode(cfg_lastfm_key.get_ptr());
    url << "&artist=" << url_encode(artist);
    url << "&track=" << url_encode(title);
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

void artwork_manager::perform_deezer_fallback_search(const char* artist, const char* track, artwork_callback callback) {
    
    // Copy parameters to ensure they remain valid throughout async operations
    pfc::string8 artist_copy = artist ? artist : "";
    pfc::string8 track_copy = track ? track : "";
    
    // Strategy 1: Try artist only
    if (!artist_copy.is_empty()) {
        pfc::string8 artist_only_url = "https://api.deezer.com/search/track?q=";
        artist_only_url << artwork_manager::url_encode(artist_copy.get_ptr()) << "&limit=5";
        
        async_io_manager::instance().http_get_async(artist_only_url, [artist_copy, track_copy, callback](bool success, const pfc::string8& response, const pfc::string8& error) {
            if (success) {
                pfc::string8 artwork_url;
                if (artwork_manager::parse_deezer_json(response, artwork_url)) {
                    // Download artwork
                    async_io_manager::instance().http_get_binary_async(artwork_url, [callback](bool dl_success, const pfc::array_t<t_uint8>& data, const pfc::string8& dl_error) {
                        artwork_result result;
                        if (dl_success && data.get_size() > 0) {
                            result.success = true;
                            result.data = data;
                            result.mime_type = artwork_manager::detect_mime_type(data.get_ptr(), data.get_size());
                            result.source = "Deezer";
                        } else {
                            result.success = false;
                            result.error_message = "Failed to download Deezer artwork";
                        }
                        callback(result);
                    });
                    return;
                }
            }
            
            // Skip track-only search as requested - only use artist fallback
            artwork_result final_result;
            final_result.success = false;
            final_result.error_message = "No artwork found in Deezer (artist search failed)";
            callback(final_result);
        });
    } else {
        // No artist available - skip track-only search as requested
        artwork_result result;
        result.success = false;
        result.error_message = "No artist available for Deezer search";
        callback(result);
    }
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
    
    // Make async HTTP request
    try {
        async_io_manager::instance().http_get_async(url, [artist, track, callback](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            artwork_result result;
            result.success = false;
            result.error_message = "Deezer API request failed: ";
            result.error_message << error;
            callback(result);
            return;
        }
        
        // Parse JSON response to extract artwork URL
        pfc::string8 artwork_url;
        if (!artwork_manager::parse_deezer_json(response, artwork_url)) {
            // Try fallback search strategies
            artwork_manager::perform_deezer_fallback_search(artist, track, callback);
            return;
        }
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.mime_type = artwork_manager::detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "Deezer";  // Set source for OSD display
            } else {
                result.success = false;
                result.error_message = "Failed to download Deezer artwork: ";
                result.error_message << error;
            }
            callback(result);
        });
        });
    } catch (const std::exception& e) {
        artwork_result result;
        result.success = false;
        result.error_message = "Exception in Deezer HTTP request: ";
        result.error_message += e.what();
        callback(result);
    } catch (...) {
        artwork_result result;
        result.success = false;
        result.error_message = "Unknown exception in Deezer HTTP request";
        callback(result);
    }
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
    
    // WebP (RIFF....WEBP) - detect but don't support loading
    if (size >= 12 && 
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') return "image/webp";
    
    // GIF
    if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) return "image/gif";
    
    // BMP
    if (data[0] == 'B' && data[1] == 'M') return "image/bmp";
    
    return "application/octet-stream";
}

pfc::string8 artwork_manager::get_file_directory(const char* file_path) {
    pfc::string8 directory = file_path;
    
    // Remove file:// prefix if present
    if (directory.find_first("file://") == 0) {
        directory = directory.get_ptr() + 7; // Remove "file://" by getting substring from position 7
    }
    
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
    // Look for artwork URLs and upgrade to highest available resolution
    const char* json_str = json.get_ptr();
    
    // Try multiple artwork URL fields in order of preference
    const char* artwork_fields[] = {
        "\"artworkUrl600\":",    // 600x600 (if available)
        "\"artworkUrl512\":",    // 512x512 (if available) 
        "\"artworkUrl100\":",    // 100x100 (most common)
        "\"artworkUrl60\":",     // 60x60 (fallback)
        "\"artworkUrl30\":"      // 30x30 (smallest)
    };
    
    for (int i = 0; i < 5; i++) {
        const char* url_pos = strstr(json_str, artwork_fields[i]);
        if (url_pos) {
            const char* url_start = strchr(url_pos, '"');
            if (url_start) {
                url_start++; // Skip opening quote
                const char* url_end = strchr(url_start, '"');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start, url_end - url_start);
                    
                    // Upgrade any resolution to 1200x1200 using iTunes URL manipulation with quality settings
                    if (artwork_url.find_first("100x100") != pfc_infinite) {
                        artwork_url.replace_string("100x100", "1200x1200");
                    } else if (artwork_url.find_first("60x60") != pfc_infinite) {
                        artwork_url.replace_string("60x60", "1200x1200");
                    } else if (artwork_url.find_first("30x30") != pfc_infinite) {
                        artwork_url.replace_string("30x30", "1200x1200");
                    } else if (artwork_url.find_first("600x600") != pfc_infinite) {
                        artwork_url.replace_string("600x600", "1200x1200");
                    } else if (artwork_url.find_first("512x512") != pfc_infinite) {
                        artwork_url.replace_string("512x512", "1200x1200");
                    }
                    
                    // Set compression quality: 80 for PNG files, 90 for JPEG files
                    if (artwork_url.find_first(".png") != pfc_infinite) {
                        // For PNG files: add bb-80 quality parameter  
                        if (artwork_url.find_first("bb.png") != pfc_infinite) {
                            artwork_url.replace_string("bb.png", "bb-80.png");
                        } else if (artwork_url.find_first("bf.png") != pfc_infinite) {
                            artwork_url.replace_string("bf.png", "bb-80.png");
                        } else if (artwork_url.find_first("1200x1200.png") != pfc_infinite) {
                            artwork_url.replace_string("1200x1200.png", "1200x1200bb-80.png");
                        }
                    } else if (artwork_url.find_first(".jpg") != pfc_infinite || artwork_url.find_first(".jpeg") != pfc_infinite) {
                        // For JPEG files: add bb-90 quality parameter for better quality
                        if (artwork_url.find_first("bb.jpg") != pfc_infinite) {
                            artwork_url.replace_string("bb.jpg", "bb-90.jpg");
                        } else if (artwork_url.find_first("bf.jpg") != pfc_infinite) {
                            artwork_url.replace_string("bf.jpg", "bb-90.jpg");
                        } else if (artwork_url.find_first("1200x1200.jpg") != pfc_infinite) {
                            artwork_url.replace_string("1200x1200.jpg", "1200x1200bb-90.jpg");
                        }
                    }
                    
                    return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
                }
            }
        }
    }
    
    return false;
}

bool artwork_manager::parse_deezer_json(const pfc::string8& json, pfc::string8& artwork_url) {
    // Simple JSON parsing for Deezer response
    // Look for "cover_xl" field in album data
    const char* json_str = json.get_ptr();
    
    
    // Look for data array first, then find album within the first result
    const char* data_pos = strstr(json_str, "\"data\":[");
    if (!data_pos) {
        return false;
    }
    
    // Find first album object within data array
    const char* album_pos = strstr(data_pos, "\"album\":");
    if (!album_pos) {
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
                    
                    // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
                    const char* size_pos = strstr(artwork_url.get_ptr(), "1000x1000");
                    if (size_pos) {
                        pfc::string8 upgraded_url;
                        upgraded_url << pfc::string8(artwork_url.get_ptr(), size_pos - artwork_url.get_ptr());
                        upgraded_url << "1200x1200";
                        upgraded_url << (size_pos + 9); // Skip past "1000x1000"
                        artwork_url = upgraded_url;
                    }
                    
                    return !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();
                }
            }
        }
    } else {
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
                const char* url_end = strchr(url_start, '",');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start,  url_end - url_start - 1);
                    if (!artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http")) {
                        artwork_url = strstr(artwork_url.get_ptr(), "http");
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
                const char* url_end = strchr(url_start, '",');
                if (url_end && url_end > url_start) {
                    artwork_url = pfc::string8(url_start, url_end - url_start - 1);
                    if (!artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http")) {
                        artwork_url = strstr(artwork_url.get_ptr(), "http");
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
