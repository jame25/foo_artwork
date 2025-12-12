#include "stdafx.h"
#include "artwork_manager.h"
#include "preferences.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Normalize string for fuzzy matching: removes punctuation, normalizes "AND"/"&", lowercases
static std::string normalize_for_matching(const std::string& s) {
    std::string result;
    result.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];

        // Skip punctuation (periods, commas, apostrophes, etc.)
        if (c == '.' || c == ',' || c == '\'' || c == '!' || c == '?' || c == '-') {
            continue;
        }

        // Convert to lowercase
        result += std::tolower(static_cast<unsigned char>(c));
    }

    // Normalize " and " to " & " for consistent comparison
    // Process the result to handle "and" vs "&"
    std::string normalized;
    normalized.reserve(result.size());

    for (size_t i = 0; i < result.size(); ++i) {
        // Check for " and " pattern (with spaces)
        if (i + 4 < result.size() &&
            result[i] == ' ' &&
            result[i+1] == 'a' &&
            result[i+2] == 'n' &&
            result[i+3] == 'd' &&
            result[i+4] == ' ') {
            normalized += ' ';  // Replace " and " with single space (remove the word entirely)
            i += 4;  // Skip past " and " (loop will add 1 more)
            continue;
        }

        // Check for " & " pattern
        if (i + 2 < result.size() &&
            result[i] == ' ' &&
            result[i+1] == '&' &&
            result[i+2] == ' ') {
            normalized += ' ';  // Replace " & " with single space
            i += 2;  // Skip past " & "
            continue;
        }

        normalized += result[i];
    }

    // Collapse multiple spaces into one
    result.clear();
    bool last_was_space = false;
    for (char c : normalized) {
        if (c == ' ') {
            if (!last_was_space) {
                result += c;
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space = false;
        }
    }

    // Trim leading/trailing spaces
    size_t start = result.find_first_not_of(' ');
    if (start == std::string::npos) return "";
    size_t end = result.find_last_not_of(' ');

    return result.substr(start, end - start + 1);
}

// Case-insensitive string comparison helper for matching artist/track names
static bool strings_equal_ignore_case(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Fuzzy string comparison that normalizes before comparing
static bool strings_match_fuzzy(const std::string& a, const std::string& b) {
    // First try exact case-insensitive match (fast path)
    if (a.size() == b.size() && strings_equal_ignore_case(a, b)) {
        return true;
    }

    // Normalize both strings and compare
    std::string norm_a = normalize_for_matching(a);
    std::string norm_b = normalize_for_matching(b);

    return norm_a == norm_b;
}

// Helper to strip "The " prefix from artist names for fuzzy matching
static std::string strip_the_prefix(const std::string& s) {
    if (s.size() > 4) {
        // Check for "The " prefix (case-insensitive)
        if ((s[0] == 'T' || s[0] == 't') &&
            (s[1] == 'H' || s[1] == 'h') &&
            (s[2] == 'E' || s[2] == 'e') &&
            s[3] == ' ') {
            return s.substr(4);
        }
    }
    return s;
}

// Fuzzy artist comparison: handles case, punctuation, "The " prefix, and "AND"/"&" differences
static bool artists_match(const std::string& a, const std::string& b) {
    // First try exact case-insensitive match (fast path)
    if (strings_equal_ignore_case(a, b)) return true;

    // Try fuzzy match (handles punctuation like "T. Rex" vs "T Rex", and "AND" vs "&")
    if (strings_match_fuzzy(a, b)) return true;

    // Try matching after stripping "The " prefix from both
    std::string a_stripped = strip_the_prefix(a);
    std::string b_stripped = strip_the_prefix(b);

    if (strings_equal_ignore_case(a_stripped, b_stripped)) return true;

    // Try fuzzy match on stripped versions too
    return strings_match_fuzzy(a_stripped, b_stripped);
}

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
extern cfg_string cfg_discogs_consumer_key;
extern cfg_string cfg_discogs_consumer_secret;
extern cfg_string cfg_lastfm_key;
extern cfg_int cfg_http_timeout;
extern cfg_int cfg_retry_count;

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
    
    // DEBUG: Track artwork loading request
    {
        metadb_info_container::ptr info_container = track->get_info_ref();
        const file_info* info = &info_container->info();
        
        pfc::string8 artist = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "Unknown Artist";
        pfc::string8 track_name = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "Unknown Track";
        pfc::string8 file_path = track->get_path();
        
    }
    
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
    const double length = track->get_length();
    bool is_internet_stream = (strstr(file_path.c_str(), "://") &&
                              !(strstr(file_path.c_str(), "file://") == file_path.c_str()) || (strstr(file_path.c_str(), "://") && (strstr(file_path.c_str(), ".tags") && (length <= 0))));
    
    if (is_internet_stream) {
        // For internet streams, skip cache but still check for tagged artwork first
        // This prevents stale artwork from being returned when track metadata changes
        find_local_artwork_async(track, [artist, track_name, callback](const artwork_result& result) {
            if (result.success) {
                // Tagged artwork found and supported format - use it (don't cache for streams)
                callback(result);
            } else {
                // No tagged artwork or unsupported format - fall back to API search
                // Don't search for failed metadata
                // Use empty cache_key to signal "don't cache results"
                if (artist != "Unknown Artist" && track_name != "Unknown Track") {
                    search_apis_async(artist, track_name, pfc::string8(""), callback);
                }
            }
        });
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
    // Check cache first, then fall back to API search on miss
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
    // Get the API search order from user preferences
    auto api_order = get_api_search_order();
    
    // Helper function to search with the ordered APIs
    search_apis_by_priority(artist, track, cache_key, callback, api_order, 0);
}

void artwork_manager::search_apis_by_priority(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback, const std::vector<ApiType>& api_order, size_t index) {
    if (index >= api_order.size()) {
        // No more APIs to try
        artwork_result final_result;
        final_result.success = false;
        final_result.error_message = "No artwork found from any source";
        callback(final_result);
        return;
    }
    
    ApiType current_api = api_order[index];
    
    // Check if this API is enabled and has required keys
    bool api_enabled = false;
    switch (current_api) {
        case ApiType::iTunes:
            api_enabled = cfg_enable_itunes;
            break;
        case ApiType::Deezer:
            api_enabled = cfg_enable_deezer;
            break;
        case ApiType::LastFm:
            api_enabled = cfg_enable_lastfm && !cfg_lastfm_key.is_empty();
            break;
        case ApiType::MusicBrainz:
            api_enabled = cfg_enable_musicbrainz;
            break;
        case ApiType::Discogs:
            api_enabled = cfg_enable_discogs && 
                         (!cfg_discogs_key.is_empty() || 
                          (!cfg_discogs_consumer_key.is_empty() && !cfg_discogs_consumer_secret.is_empty()));
            break;
    }
    
    if (!api_enabled) {
        // Skip this API and try the next one
        search_apis_by_priority(artist, track, cache_key, callback, api_order, index + 1);
        return;
    }
    
    // Create a callback that will either return success or try the next API
    auto api_callback = [artist, track, cache_key, callback, api_order, index](const artwork_result& result) {
        pfc::string8 api_name;
        switch (api_order[index]) {
            case ApiType::iTunes: api_name = "iTunes"; break;
            case ApiType::Deezer: api_name = "Deezer"; break;
            case ApiType::LastFm: api_name = "Last.fm"; break;
            case ApiType::MusicBrainz: api_name = "MusicBrainz"; break;
            case ApiType::Discogs: api_name = "Discogs"; break;
        }
        
        if (result.success) {
            // Cache the result if cache_key is not empty (empty means internet stream)
            if (!cache_key.is_empty()) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
            }
            callback(result);
        } else {
            console::printf("foo_artwork: API FAILED - %s failed for '%s - %s' (error: %s)", 
                           api_name.c_str(), artist.c_str(), track.c_str(), result.error_message.c_str());
            
            // This API failed, try the next one
            search_apis_by_priority(artist, track, cache_key, callback, api_order, index + 1);
        }
    };
    
    // Call the appropriate API search function
    switch (current_api) {
        case ApiType::iTunes:
            search_itunes_api_async(artist, track, api_callback);
            break;
        case ApiType::Deezer:
            search_deezer_api_async(artist, track, api_callback);
            break;
        case ApiType::LastFm:
            search_lastfm_api_async(artist, track, api_callback);
            break;
        case ApiType::MusicBrainz:
            search_musicbrainz_api_async(artist, track, api_callback);
            break;
        case ApiType::Discogs:
            search_discogs_api_async(artist, track, api_callback);
            break;
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
                        result.data.set_size(art_data->get_size());
                        memcpy(result.data.get_ptr(), art_data->get_ptr(), art_data->get_size());
                        result.mime_type = detect_mime_type(result.data.get_ptr(), result.data.get_size());
                        
                        // Check if the tagged artwork format is supported
                        if (is_supported_image_format(result.mime_type)) {
                            result.success = true;
                            result.source = "Local artwork";
                            
                            async_io_manager::instance().post_to_main_thread([callback, result]() {
                                callback(result);
                            });
                            return;
                        } else {
                            // Tagged artwork format not supported, continue checking other artwork types
                            continue;
                        }
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



void artwork_manager::search_itunes_api_async(const char* artist, const char* track, artwork_callback callback) {
    // iTunes Search API doesn't require an API key
    // First try searching for the track as a song
    pfc::string8 url = "https://itunes.apple.com/search?term=";
    url << url_encode(artist) << "+" << url_encode(track);
    url << "&entity=song&limit=5";  // Increased limit for better matches
    
   
    // Copy parameters to avoid lambda capture corruption
    pfc::string8 artist_str = artist;
    pfc::string8 track_str = track;
    
    // Make async HTTP request
    async_io_manager::instance().http_get_async(url, [callback, artist_str, track_str](bool success, const pfc::string8& response, const pfc::string8& error) {
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
        if (!parse_itunes_json(artist_str, track_str, response, artwork_url)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in itunes response";
            callback(result);
            return;
        }

        
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback, artwork_url](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
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

void artwork_manager::search_discogs_api_async(const char* artist, const char* track, artwork_callback callback) {
    
    // Check if we have either a personal token OR consumer key+secret
    bool has_token = !cfg_discogs_key.is_empty();
    bool has_consumer_creds = !cfg_discogs_consumer_key.is_empty() && !cfg_discogs_consumer_secret.is_empty();
    
    if (!has_token && !has_consumer_creds) {
        async_io_manager::instance().post_to_main_thread([callback]() {
            artwork_result result;
            result.success = false;
            result.error_message = "Discogs API authentication not configured";
            callback(result);
        });
        return;
    }
    
    // Build Discogs API URL - search for artist + track (not album)
    pfc::string8 search_query = artist;
    search_query << " " << track;
    
    pfc::string8 url = "https://api.discogs.com/database/search?q=";
    url << url_encode(search_query);
    url << "&type=release";

    // Add authentication - prefer personal token over consumer credentials
    if (has_token) {
        url << "&token=" << url_encode(cfg_discogs_key.get_ptr());
    } else {
        url << "&key=" << url_encode(cfg_discogs_consumer_key.get_ptr());
        url << "&secret=" << url_encode(cfg_discogs_consumer_secret.get_ptr());
    }

    // Copy parameters to avoid lambda capture issues
    pfc::string8 artist_str = artist;
    pfc::string8 track_str = track;
    
    // Make async HTTP request
    async_io_manager::instance().http_get_async(url, [callback, artist_str, track_str](bool success, const pfc::string8& response, const pfc::string8& error) {
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
        if (!parse_discogs_json(artist_str, track_str, response, artwork_url)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in Discogs response";
            callback(result);
            return;
        }
       
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback, artwork_url](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
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
    url << "&autocorrect=1&format=json";
    
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
    
    pfc::string8 search_query;
    pfc::string8 search_artist = "artist:\"";

    // Build search query: "artist"
    search_query += search_artist;
    search_query += artist;
    search_query += "\"";


    // Strategy 1: Try artist only
    if (!artist_copy.is_empty()) {
        pfc::string8 artist_only_url = "https://api.deezer.com/search?q=";
        artist_only_url << artwork_manager::url_encode(search_query) << "&limit=5";

        async_io_manager::instance().http_get_async(artist_only_url, [artist_copy, track_copy, callback](bool success, const pfc::string8& response, const pfc::string8& error) {
            if (success) {
                pfc::string8 artwork_url;
                if (artwork_manager::parse_deezer_json(artist_copy, track_copy, response, artwork_url)) {
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
    pfc::string8 search_track = "track:\"";
    pfc::string8 search_artist = "artist:\"";
    
    // Build search query: "track"
    // NOTE: Metadata cleaner (is valid for search) rule 1 stops it from being used
    if (!artist || strlen(artist) == 0) {
        search_query += search_track;
        search_query += track;
        search_query += "\"";
    } else {
        // Build search query: "artist track"
        search_query += search_artist;
        search_query += artist;
        search_query += "\"";
        search_query += " ";
        search_query += search_track;
        search_query += track;
        search_query += "\"";
    }
    
    pfc::string8 url = "https://api.deezer.com/search?q=";
    url << url_encode(search_query) << "&limit=10";

    // Copy parameters to avoid lambda capture corruption
    pfc::string8 artist_str = artist;
    pfc::string8 track_str = track;

    // Make async HTTP request
    try {
        async_io_manager::instance().http_get_async(url, [artist_str, track_str, callback](bool success, const pfc::string8& response, const pfc::string8& error) {
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
        if (!artwork_manager::parse_deezer_json(artist_str, track_str, response, artwork_url)) {
            // Try fallback search strategies
            artwork_manager::perform_deezer_fallback_search(artist_str, track_str, callback);
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

bool artwork_manager::is_supported_image_format(const pfc::string8& mime_type) {
    // Supported formats that can be displayed in foobar2000
    return mime_type == "image/jpeg" || 
           mime_type == "image/png" || 
           mime_type == "image/gif" || 
           mime_type == "image/bmp";
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
bool artwork_manager::parse_itunes_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url) {

    //using nlohmann/json

    std::string json_data;
    json_data += json_in;

    json data = json::parse(json_data);

    //No data return
    if (data["resultCount"] == 0) return false;

    // root
    json s = data["results"];

    // Convert input artist/track to std::string for comparison
    std::string artist_str(artist);
    std::string track_str(track);

    // Try multiple artwork URL fields in order of preference

    //search for exact same artist track values first (case-insensitive)
    for (const auto& item : s)
    {
        std::string result_track = item["trackName"].get<std::string>();
        std::string result_artist = item["artistName"].get<std::string>();

        if (strings_match_fuzzy(result_track, track_str) && artists_match(result_artist, artist_str)) {

            if (item.contains("artworkUrl600")) {
                artwork_url = item["artworkUrl600"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("600x600", "1200x1200");
            }
            else if (item.contains("artworkUrl512")) {
                artwork_url = item["artworkUrl512"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("512x512", "1200x1200");
            }
            else if (item.contains("artworkUrl100")) {
                artwork_url = item["artworkUrl100"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("100x100", "1200x1200");
            }
            else if (item.contains("artworkUrl60")) {
                artwork_url = item["artworkUrl60"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("60x60", "1200x1200");
            }
            else if (item.contains("artworkUrl30")) {
                artwork_url = item["artworkUrl30"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("30x30", "1200x1200");
            }

            // Set compression quality: 80 for PNG files, 90 for JPEG files
            if (artwork_url.find_first(".png") != pfc_infinite) {
                // For PNG files: add bb-80 quality parameter  
                if (artwork_url.find_first("bb.png") != pfc_infinite) {
                    artwork_url.replace_string("bb.png", "bb-80.png");
                }
                else if (artwork_url.find_first("bf.png") != pfc_infinite) {
                    artwork_url.replace_string("bf.png", "bb-80.png");
                }
                else if (artwork_url.find_first("1200x1200.png") != pfc_infinite) {
                    artwork_url.replace_string("1200x1200.png", "1200x1200bb-80.png");
                }
            }
            else if (artwork_url.find_first(".jpg") != pfc_infinite || artwork_url.find_first(".jpeg") != pfc_infinite) {
                // For JPEG files: add bb-90 quality parameter for better quality
                if (artwork_url.find_first("bb.jpg") != pfc_infinite) {
                    artwork_url.replace_string("bb.jpg", "bb-90.jpg");
                }
                else if (artwork_url.find_first("bf.jpg") != pfc_infinite) {
                    artwork_url.replace_string("bf.jpg", "bb-90.jpg");
                }
                else if (artwork_url.find_first("1200x1200.jpg") != pfc_infinite) {
                    artwork_url.replace_string("1200x1200.jpg", "1200x1200bb-90.jpg");
                }
            }

            bool is_valid = !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();

            return is_valid;
        }
    }

    //No exact artist+title match - find first result matching artist (case-insensitive)
    for (const auto& item : s)
    {
            // Only consider results from the same artist to avoid "Best Of" compilations
            std::string result_artist = item["artistName"].get<std::string>();
            if (!artists_match(result_artist, artist_str)) {
                continue; // Skip results from different artists
            }

            if (item.contains("artworkUrl600")) {
                artwork_url = item["artworkUrl600"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("600x600", "1200x1200");
            }
            else if (item.contains("artworkUrl512")) {
                artwork_url = item["artworkUrl512"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("512x512", "1200x1200");
            }
            else if (item.contains("artworkUrl100")) {
                artwork_url = item["artworkUrl100"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("100x100", "1200x1200");
            }
            else if (item.contains("artworkUrl60")) {
                artwork_url = item["artworkUrl60"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("60x60", "1200x1200");
            }
            else if (item.contains("artworkUrl30")) {
                artwork_url = item["artworkUrl30"].get<std::string>().c_str();
                // Upgrade resolution to none for original quality
                artwork_url.replace_string("30x30", "1200x1200");
            }

            // Set compression quality: 80 for PNG files, 90 for JPEG files
            if (artwork_url.find_first(".png") != pfc_infinite) {
                // For PNG files: add bb-80 quality parameter  
                if (artwork_url.find_first("bb.png") != pfc_infinite) {
                    artwork_url.replace_string("bb.png", "bb-80.png");
                }
                else if (artwork_url.find_first("bf.png") != pfc_infinite) {
                    artwork_url.replace_string("bf.png", "bb-80.png");
                }
                else if (artwork_url.find_first("1200x1200.png") != pfc_infinite) {
                    artwork_url.replace_string("1200x1200.png", "1200x1200bb-80.png");
                }
            }
            else if (artwork_url.find_first(".jpg") != pfc_infinite || artwork_url.find_first(".jpeg") != pfc_infinite) {
                // For JPEG files: add bb-90 quality parameter for better quality
                if (artwork_url.find_first("bb.jpg") != pfc_infinite) {
                    artwork_url.replace_string("bb.jpg", "bb-90.jpg");
                }
                else if (artwork_url.find_first("bf.jpg") != pfc_infinite) {
                    artwork_url.replace_string("bf.jpg", "bb-90.jpg");
                }
                else if (artwork_url.find_first("1200x1200.jpg") != pfc_infinite) {
                    artwork_url.replace_string("1200x1200.jpg", "1200x1200bb-90.jpg");
                }
            }

            bool is_valid = !artwork_url.is_empty() && strstr(artwork_url.get_ptr(), "http") == artwork_url.get_ptr();

            return is_valid;
        
    }
    
    return false;
}

bool artwork_manager::parse_deezer_json(const char* artist, const char* track ,const pfc::string8& json_in, pfc::string8& artwork_url) {
    
    //using nlohmann/json

    //convert to std::string
    std::string json_data;
    json_data += json_in;

    //parse
    json data = json::parse(json_data);

    //No data return
    if (data["total"] == 0) return false;

    //sort by rank to get higher ratings values first (api parameter &order= doesn't work) 
    std::sort(data["data"].begin(), data["data"].end(),
        [](const json& a, const json& b) {
            return a["rank"].get<int>() > b["rank"].get<int>();
        });

    //select root
    json s = data["data"];

    // Convert input artist/track to std::string for comparison
    std::string artist_str(artist);
    std::string track_str(track);

   //search for exact same artist track values first (fuzzy matching for punctuation/case/AND-& differences)
   for (const auto& item : s.items())
   {
       std::string result_title = item.value()["title"].get<std::string>();
       std::string result_artist = item.value()["artist"]["name"].get<std::string>();

       if (strings_match_fuzzy(result_title, track_str) && artists_match(result_artist, artist_str)) {

           //search cover_xl
           if (item.value()["album"]["cover_xl"].get<std::string>().c_str()) {
               artwork_url = item.value()["album"]["cover_xl"].get<std::string>().c_str();

               // Unescape JSON slashes (replace \/ with /)
               pfc::string8 unescaped_url;
               const char* src = artwork_url.get_ptr();
               while (*src) {
                   if (*src == '\\' && *(src + 1) == '/') {
                       unescaped_url += "/";
                       src += 2; // Skip both \ and /
                   }
                   else {
                       char single_char[2] = { *src, '\0' };
                       unescaped_url += single_char;
                       src++;
                   }
               }
               artwork_url = unescaped_url;

               // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
               artwork_url = artwork_url.replace("1000x1000", "1200x1200");
               return true;
           }

           //cover_big
           if (item.value()["album"]["cover_big"].get<std::string>().c_str()) {
               artwork_url = item.value()["album"]["cover_big"].get<std::string>().c_str();

               // Unescape JSON slashes (replace \/ with /)
               pfc::string8 unescaped_url;
               const char* src = artwork_url.get_ptr();
               while (*src) {
                   if (*src == '\\' && *(src + 1) == '/') {
                       unescaped_url += "/";
                       src += 2; // Skip both \ and /
                   }
                   else {
                       char single_char[2] = { *src, '\0' };
                       unescaped_url += single_char;
                       src++;
                   }
               }
               artwork_url = unescaped_url;
               return true;
           }          
       }
   }

   //No exact artist+title match - find first result matching artist (case-insensitive)
   for (const auto& item : s.items())
   {
           // Only consider results from the same artist to avoid "Best Of" compilations
           std::string result_artist = item.value()["artist"]["name"].get<std::string>();
           if (!artists_match(result_artist, artist_str)) {
               continue; // Skip results from different artists
           }

           //search cover_xl
           if (item.value()["album"]["cover_xl"].get<std::string>().c_str()) {
               artwork_url = item.value()["album"]["cover_xl"].get<std::string>().c_str();

               // Unescape JSON slashes (replace \/ with /)
               pfc::string8 unescaped_url;
               const char* src = artwork_url.get_ptr();
               while (*src) {
                   if (*src == '\\' && *(src + 1) == '/') {
                       unescaped_url += "/";
                       src += 2; // Skip both \ and /
                   }
                   else {
                       char single_char[2] = { *src, '\0' };
                       unescaped_url += single_char;
                       src++;
                   }
               }
               artwork_url = unescaped_url;

               // Upgrade 1000x1000 resolution to 1200x1200 for higher quality
               artwork_url = artwork_url.replace("1000x1000", "1200x1200");
               return true;
           }

           //cover_big
           if (item.value()["album"]["cover_big"].get<std::string>().c_str()) {
               artwork_url = item.value()["album"]["cover_big"].get<std::string>().c_str();

               // Unescape JSON slashes (replace \/ with /)
               pfc::string8 unescaped_url;
               const char* src = artwork_url.get_ptr();
               while (*src) {
                   if (*src == '\\' && *(src + 1) == '/') {
                       unescaped_url += "/";
                       src += 2; // Skip both \ and /
                   }
                   else {
                       char single_char[2] = { *src, '\0' };
                       unescaped_url += single_char;
                       src++;
                   }
               }
               artwork_url = unescaped_url;
               return true;
           }
   }

   // No results found matching the artist
   return false;
}

bool artwork_manager::parse_lastfm_json(const pfc::string8& json_in, pfc::string8& artwork_url) {
    
    //using nlohmann/json
    
    std::string json_data;
    json_data += json_in;

    json data = json::parse(json_data);

    //No data return
    if (data["message"] == "Track not found") return false;

    json s = data["track"]["album"]["image"];

    //extralarge
    for (const auto& item : s.items())
    {
        if (item.value()["size"].get<std::string>() == "extralarge") {
            artwork_url = item.value()["#text"].get<std::string>().c_str();
            // Upgrade resolution to none for original quality
            artwork_url = artwork_url.replace("u/300x300", "u/");
            return true;
        }
    }

    //large
    for (const auto& item : s.items())
    {
        if (item.value()["size"].get<std::string>() == "large") {
            artwork_url = item.value()["#text"].get<std::string>().c_str();
            // Upgrade resolution to none for original quality
            artwork_url = artwork_url.replace("u/174s", "u/");
            return true;
        }
    }

    return false;
}

bool artwork_manager::parse_discogs_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url) {
    
    //using nlohmann/json

    std::string json_data;
    json_data += json_in;

    json data = json::parse(json_data);

    //No data return
    if (data["pagination"]["items"] == 0) return false;

    // root
    json s = data["results"];

    // Convert input artist to std::string for comparison
    std::string artist_str(artist);

    //search for exact same artist - track value first (case-insensitive)
    std::string artist_title;
    artist_title += artist;
    artist_title += " - ";
    artist_title += track;

    for (const auto& item : s.items())
    {
        std::string result_title = item.value()["title"].get<std::string>();
        if (strings_match_fuzzy(result_title, artist_title)) {
            if (item.value()["cover_image"].get<std::string>().c_str()) {
                artwork_url = item.value()["cover_image"].get<std::string>().c_str();
                return true;
            }
            else if (item.value()["thumb"].get<std::string>().c_str()) {
                artwork_url = item.value()["thumb"].get<std::string>().c_str();
                return true;
            }
        }
    }

    //No exact artist+title match - find first result matching artist (case-insensitive)
    // Discogs format is "Artist - Album/Track", so check if title starts with artist
    for (const auto& item : s.items())
    {
        std::string result_title = item.value()["title"].get<std::string>();

        // Check if title starts with artist name (case-insensitive)
        // Discogs format: "Artist - Album" or "Artist - Track"
        // Also handle "The " prefix differences
        bool artist_matches = false;
        std::string artist_stripped = strip_the_prefix(artist_str);
        std::string title_stripped = strip_the_prefix(result_title);

        // Try matching with original artist
        if (result_title.size() >= artist_str.size()) {
            artist_matches = strings_equal_ignore_case(
                result_title.substr(0, artist_str.size()), artist_str);
        }
        // Try matching after stripping "The " from both
        if (!artist_matches && title_stripped.size() >= artist_stripped.size()) {
            artist_matches = strings_equal_ignore_case(
                title_stripped.substr(0, artist_stripped.size()), artist_stripped);
        }

        if (!artist_matches) {
            continue; // Skip results from different artists
        }

        if (item.value()["cover_image"].get<std::string>().c_str()) {
            artwork_url = item.value()["cover_image"].get<std::string>().c_str();
            return true;
        }
        else if (item.value()["thumb"].get<std::string>().c_str()) {
            artwork_url = item.value()["thumb"].get<std::string>().c_str();
            return true;
        }
    }


    return false;
}

void artwork_manager::search_musicbrainz_api_async(const char* artist, const char* track, artwork_callback callback) {
    // MusicBrainz does not require authentication but uses a two-step process:
    // 1. Search for release ID's
    // 2. Get cover art from Cover Art Archive
    // 
    
    // Build search query
    pfc::string8 search_query;
    search_query << "artist:\"" << artist << "\" AND recording:\"" << track << "\"";

    pfc::string8 url = "http://musicbrainz.org/ws/2/recording/?query=";
    url << url_encode(search_query);
    url << "&fmt=json&limit=5&inc=releases";  // include releases for release IDs

    // Copy parameters to avoid lambda capture issues
    pfc::string8 artist_str = artist;
    pfc::string8 track_str = track;

    async_io_manager::instance().http_get_async(url, [callback, artist_str](bool success, const pfc::string8& response, const pfc::string8& error) {
        if (!success) {
            artwork_result result;
            result.success = false;
            result.error_message = "MusicBrainz API request failed: ";
            callback(result);
            return;
        }

        // Parse JSON response to collect release IDs (filter by artist)
        std::vector<pfc::string8> release_ids;
        if (!parse_musicbrainz_json(response, release_ids, artist_str.c_str()) || release_ids.empty()) {
            artwork_result result;
            result.success = false;
            callback(result);
            result.error_message = "No valid release IDs found in MusicBrainz response";
            callback(result);
            return;
        }

        // Recursive lambda to try each release ID until success
        std::shared_ptr<std::function<void(size_t)>> try_release =
            std::make_shared<std::function<void(size_t)>>();

        *try_release = [release_ids, callback, try_release](size_t index) {
            if (index >= release_ids.size()) {
                // Exhausted all release IDs
                artwork_result result;
                result.success = false;
                result.error_message = "No valid artwork found for any release ID";
                callback(result);
                return;
            }

            pfc::string8 coverart_url = "http://coverartarchive.org/release/";
            coverart_url << release_ids[index] << "/front";

            async_io_manager::instance().http_get_binary_async(coverart_url,
                [callback, try_release, index, release_ids, coverart_url](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
                    if (success && data.get_size() > 0) {
                        bool is_valid_image = is_valid_image_data(data.get_ptr(), data.get_size());
                        pfc::string8 mime_type = detect_mime_type(data.get_ptr(), data.get_size());

                        if (is_valid_image && data.get_size() > 512) {
                            artwork_result result;
                            result.success = true;
                            result.data = data;
                            result.mime_type = mime_type;
                            result.source = "MusicBrainz";
                            callback(result);
                            return;
                        }
                    }
                    // Try next release ID
                    (*try_release)(index + 1);
                });
            };

        // Start with the first release
        (*try_release)(0);
        });
}


bool artwork_manager::parse_musicbrainz_json(const pfc::string8& json_in, std::vector<pfc::string8>& release_ids, const char* artist) {
    try {
        std::string json_data(json_in.c_str());
        json data = json::parse(json_data);

        if (!data.contains("recordings") || data["count"].get<int>() == 0)
            return false;

        std::string artist_str(artist);

        for (const auto& rec : data["recordings"]) {
            // Check if recording's artist-credit matches the requested artist (case-insensitive)
            bool artist_matches = false;
            if (rec.contains("artist-credit")) {
                for (const auto& ac : rec["artist-credit"]) {
                    if (ac.contains("name") && ac["name"].is_string()) {
                        std::string credit_name = ac["name"].get<std::string>();
                        if (artists_match(credit_name, artist_str)) {
                            artist_matches = true;
                            break;
                        }
                    }
                    // Also check nested artist object
                    if (ac.contains("artist") && ac["artist"].contains("name")) {
                        std::string nested_name = ac["artist"]["name"].get<std::string>();
                        if (artists_match(nested_name, artist_str)) {
                            artist_matches = true;
                            break;
                        }
                    }
                }
            }

            // Skip recordings from different artists to avoid "Best Of" compilations
            if (!artist_matches) continue;

            if (!rec.contains("releases")) continue;
            for (const auto& rel : rec["releases"]) {
                if (rel.contains("id") && rel["id"].is_string()) {
                    release_ids.push_back(rel["id"].get<std::string>().c_str());
                }
            }
        }
        return !release_ids.empty();
    }
    catch (const std::exception& e) {
        console::info("MusicBrainz JSON parse error: %s");
        return false;
    }
}
