#include "stdafx.h"
#include "artwork_manager.h"
#include "metadata_cleaner.h"
#include "preferences.h"
#include "acrcloud_client.h"
#include "titleformat_provider.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Normalize diacritics/accented characters to ASCII equivalents (UTF-8 aware)
static std::string normalize_diacritics(const std::string& s) {
    std::string result;
    result.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);

        // Check for UTF-8 two-byte sequences starting with 0xC3 (Latin-1 Supplement)
        if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char next = static_cast<unsigned char>(s[i + 1]);
            char replacement = 0;

            // Uppercase variants (0xC3 0x80-0x9F)
            if (next >= 0x80 && next <= 0x85) replacement = 'A';       // À Á Â Ã Ä Å
            else if (next == 0x86) { result += "AE"; i++; continue; }  // Æ
            else if (next == 0x87) replacement = 'C';                  // Ç
            else if (next >= 0x88 && next <= 0x8B) replacement = 'E';  // È É Ê Ë
            else if (next >= 0x8C && next <= 0x8F) replacement = 'I';  // Ì Í Î Ï
            else if (next == 0x90) replacement = 'D';                  // Ð
            else if (next == 0x91) replacement = 'N';                  // Ñ
            else if (next >= 0x92 && next <= 0x96) replacement = 'O';  // Ò Ó Ô Õ Ö
            else if (next == 0x98) replacement = 'O';                  // Ø
            else if (next >= 0x99 && next <= 0x9C) replacement = 'U';  // Ù Ú Û Ü
            else if (next == 0x9D) replacement = 'Y';                  // Ý
            // Lowercase variants (0xC3 0xA0-0xBF)
            else if (next >= 0xA0 && next <= 0xA5) replacement = 'a';  // à á â ã ä å
            else if (next == 0xA6) { result += "ae"; i++; continue; }  // æ
            else if (next == 0xA7) replacement = 'c';                  // ç
            else if (next >= 0xA8 && next <= 0xAB) replacement = 'e';  // è é ê ë
            else if (next >= 0xAC && next <= 0xAF) replacement = 'i';  // ì í î ï
            else if (next == 0xB0) replacement = 'd';                  // ð
            else if (next == 0xB1) replacement = 'n';                  // ñ
            else if (next >= 0xB2 && next <= 0xB6) replacement = 'o';  // ò ó ô õ ö
            else if (next == 0xB8) replacement = 'o';                  // ø
            else if (next >= 0xB9 && next <= 0xBC) replacement = 'u';  // ù ú û ü
            else if (next == 0xBD || next == 0xBF) replacement = 'y';  // ý ÿ
            else if (next == 0x9F) { result += "ss"; i++; continue; }  // ß (German eszett)

            if (replacement) {
                result += replacement;
                i++;  // Skip the second byte
                continue;
            }
        }

        // Check for UTF-8 two-byte sequences starting with 0xC5 (Latin Extended-A)
        if (c == 0xC5 && i + 1 < s.size()) {
            unsigned char next = static_cast<unsigned char>(s[i + 1]);
            char replacement = 0;

            if (next == 0x92 || next == 0x93) {  // Œ œ
                result += (next == 0x92) ? "OE" : "oe";
                i++;
                continue;
            }
            else if (next == 0xA0 || next == 0xA1) replacement = (next == 0xA0) ? 'S' : 's';  // Š š
            else if (next == 0xBD || next == 0xBE) replacement = (next == 0xBD) ? 'Z' : 'z';  // Ž ž

            if (replacement) {
                result += replacement;
                i++;
                continue;
            }
        }

        // Pass through other characters unchanged
        result += s[i];
    }

    return result;
}

// Normalize string for fuzzy matching: removes diacritics, punctuation, normalizes "AND"/"&", lowercases
static std::string normalize_for_matching(const std::string& s) {
    // First normalize diacritics (ö→o, é→e, etc.)
    std::string diacritic_normalized = normalize_diacritics(s);

    std::string result;
    result.reserve(diacritic_normalized.size());

    for (size_t i = 0; i < diacritic_normalized.size(); ++i) {
        char c = diacritic_normalized[i];

        // Skip punctuation (periods, commas, apostrophes, etc.)
        if (c == '.' || c == ',' || c == '\'' || c == '!' || c == '?' || c == '-') {
            continue;
        }

        // Treat underscores as spaces (common in stream metadata)
        if (c == '_') {
            result += ' ';
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
    if (strings_match_fuzzy(a_stripped, b_stripped)) return true;

    // Try matching extracted first artists for multi-artist collaborations
    std::string first_a = MetadataCleaner::extract_first_artist(a.c_str());
    std::string first_b = MetadataCleaner::extract_first_artist(b.c_str());

    if (!first_a.empty() && !first_b.empty() && (first_a != a || first_b != b)) {
        if (strings_equal_ignore_case(first_a, first_b)) return true;
        if (strings_match_fuzzy(first_a, first_b)) return true;

        std::string first_a_stripped = strip_the_prefix(first_a);
        std::string first_b_stripped = strip_the_prefix(first_b);
        if (strings_equal_ignore_case(first_a_stripped, first_b_stripped)) return true;
        if (strings_match_fuzzy(first_a_stripped, first_b_stripped)) return true;
    }

    if (!first_a.empty() && first_a != a) {
        if (strings_equal_ignore_case(first_a, b)) return true;
        if (strings_match_fuzzy(first_a, b)) return true;
        std::string first_a_stripped = strip_the_prefix(first_a);
        if (strings_equal_ignore_case(first_a_stripped, b_stripped)) return true;
        if (strings_match_fuzzy(first_a_stripped, b_stripped)) return true;
    }

    if (!first_b.empty() && first_b != b) {
        if (strings_equal_ignore_case(a, first_b)) return true;
        if (strings_match_fuzzy(a, first_b)) return true;
        std::string first_b_stripped = strip_the_prefix(first_b);
        if (strings_equal_ignore_case(a_stripped, first_b_stripped)) return true;
        if (strings_match_fuzzy(a_stripped, first_b_stripped)) return true;
    }

    // Try matching extracted second artists (e.g. Gouryella from Ferry Corsten pres. Gouryella)
    std::string second_a = MetadataCleaner::extract_second_artist(a.c_str());
    std::string second_b = MetadataCleaner::extract_second_artist(b.c_str());

    if (!second_a.empty()) {
        if (strings_equal_ignore_case(second_a, b)) return true;
        if (strings_match_fuzzy(second_a, b)) return true;
        if (!first_b.empty() && (strings_equal_ignore_case(second_a, first_b) || strings_match_fuzzy(second_a, first_b))) return true;
    }

    if (!second_b.empty()) {
        if (strings_equal_ignore_case(a, second_b)) return true;
        if (strings_match_fuzzy(a, second_b)) return true;
        if (!first_a.empty() && (strings_equal_ignore_case(first_a, second_b) || strings_match_fuzzy(first_a, second_b))) return true;
    }

    return false;
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
extern cfg_bool cfg_enable_disk_cache;
extern cfg_bool cfg_single_file_cache;
extern cfg_bool cfg_enable_acrcloud;
extern cfg_string cfg_acrcloud_host;
extern cfg_string cfg_acrcloud_access_key;
extern cfg_string cfg_acrcloud_access_secret;
extern cfg_bool cfg_disable_instream_artwork;

// Static member initialization
std::atomic<bool> artwork_manager::initialized_(false);
std::atomic<bool> g_is_shutting_down{false};
static std::chrono::steady_clock::time_point g_acrcloud_cooldown_until;
static pfc::string8 g_current_stream_url;
static visualisation_stream::ptr g_vis_stream;
static std::atomic<uint64_t> g_acrcloud_task_id{0};
static artwork_manager::artwork_result g_last_recognized_result;
static pfc::string8 g_last_recognized_stream_url;
static pfc::string8 g_last_recognized_artist;
static pfc::string8 g_last_recognized_title;
static std::atomic<uint64_t> g_rms_detector_token{0};
static std::atomic<uint64_t> g_stream_monitor_token{0};
static std::atomic<uint64_t> g_external_api_session_token{0};
static std::atomic<uint64_t> g_coversync_cue_token{0};
static pfc::string8 g_last_stream_artist = "";
static pfc::string8 g_last_stream_title = "";
static pfc::string8 g_pending_external_api_artist = "";
static pfc::string8 g_pending_external_api_title = "";
static pfc::string8 g_last_logged_track_info = "";

static metadb_handle_ptr g_active_playing_track;
static pfc::string8 g_active_source;
static pfc::string8 g_active_resolved_provider;
static pfc::string8 g_active_cache_key;
static std::set<std::string> g_rejected_providers_for_current_track;
static std::atomic<uint64_t> g_search_generation{0};

pfc::string8 artwork_manager::get_active_resolved_provider() {
    return g_active_resolved_provider;
}

pfc::string8 artwork_manager::get_active_source() {
    return g_active_source;
}

static bool contains_case_insensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    pfc::string8 h(haystack);
    pfc::string8 n(needle);
    return strstr(h.toLower().c_str(), n.toLower().c_str()) != nullptr;
}

struct ApiDedupEntry {
    std::vector<artwork_manager::artwork_callback> callbacks;
    bool completed = false;
    artwork_manager::artwork_result result;
    std::chrono::steady_clock::time_point completed_time;
};

static std::mutex g_in_flight_mutex;
static std::map<std::string, std::vector<artwork_manager::artwork_callback>> g_in_flight_queries;
static std::map<std::string, ApiDedupEntry> g_api_dedup_map;
static visualisation_stream::ptr get_persistent_vis_stream();
static void stop_rms_silence_detector(bool force = true);
static void reset_acrcloud_cooldown();
static void log_simplified_track_info(const char* artist, const char* title);

void artwork_manager::initialize() {
    g_is_shutting_down.store(false);
    if (initialized_.exchange(true)) return; // Already initialized
    
    async_io_manager::instance().initialize(4); // 4 thread pool workers
}

void artwork_manager::shutdown() {
    g_is_shutting_down.store(true);
    on_playback_stop();

    if (!initialized_.exchange(false)) return; // Not initialized
    
    async_io_manager::instance().shutdown();
}

void artwork_manager::on_playback_new_track(metadb_handle_ptr track) {
    g_search_generation++;
    stop_external_stream_api_poller();
    stop_rms_silence_detector(true);
    cancel_acrcloud_tasks();
    g_rms_detector_token++;
    g_acrcloud_task_id++;
    g_stream_monitor_token++;
    g_external_api_session_token++;
    g_coversync_cue_token++;
    g_vis_stream.release();
    g_active_playing_track = track;
    g_active_source.reset();
    g_active_resolved_provider.reset();
    g_rejected_providers_for_current_track.clear();
    g_last_stream_artist.reset();
    g_last_stream_title.reset();
    g_pending_external_api_artist.reset();
    g_pending_external_api_title.reset();
    g_last_logged_track_info.reset();
    reset_acrcloud_cooldown();
    titleformat_provider::set_status("Searching artwork...");

    if (track.is_valid()) {
        pfc::string8 path = track->get_path();
        bool is_yt = !extract_youtube_video_id(path.c_str()).is_empty();
        if (is_yt || (strstr(path.c_str(), "://") && !strstr(path.c_str(), "file://")) || strstr(path.c_str(), "youtube.com") || strstr(path.c_str(), "youtu.be")) {
            g_current_stream_url = path;
        } else {
            g_current_stream_url.reset();
        }
    } else {
        g_current_stream_url.reset();
    }

    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        g_in_flight_queries.clear();
        g_api_dedup_map.clear();
    }
}

void artwork_manager::on_playback_stop() {
    stop_external_stream_api_poller();
    stop_rms_silence_detector(true);
    cancel_acrcloud_tasks();
    g_rms_detector_token++;
    g_acrcloud_task_id++;
    g_stream_monitor_token++;
    g_external_api_session_token++;
    g_coversync_cue_token++;
    g_vis_stream.release();
    g_active_playing_track.release();
    g_current_stream_url.reset();
    g_last_stream_artist.reset();
    g_last_stream_title.reset();
    g_pending_external_api_artist.reset();
    g_pending_external_api_title.reset();
    g_last_logged_track_info.reset();
    titleformat_provider::clear_track_artwork_info();
    titleformat_provider::set_status("");

    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        g_in_flight_queries.clear();
        g_api_dedup_map.clear();
    }
}

extern void refresh_all_dui_artwork_panels();
extern void refresh_all_cui_artwork_panels();
extern bool create_bitmap_from_image_data(const std::vector<BYTE>& data);

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
        
        pfc::string8 yt_video_id;
        bool is_stream = !g_current_stream_url.is_empty();
        if (is_stream) {
            yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
        }
        if (yt_video_id.is_empty() && g_active_playing_track.is_valid()) {
            yt_video_id = extract_youtube_video_id(g_active_playing_track->get_path());
        }
        bool is_youtube = !yt_video_id.is_empty();

        pfc::string8 cache_key;
        if (is_youtube) {
            cache_key = pfc::string8("yt_") + yt_video_id;
        } else {
            cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key(artist_str, track_str);
        }

        uint64_t gen = g_search_generation.load();
        auto original_callback = callback;
        auto wrapped_callback = [gen, artist_str, track_str, cache_key, original_callback](const artwork_result& res) {
            if (gen != g_search_generation.load()) {
                // Stale callback for previous track/cue - drop updates
                original_callback(res);
                return;
            }
            if (res.success) {
                if (!res.source.is_empty() && res.source != "Cache") {
                    g_active_resolved_provider = res.source;
                    g_active_source = res.source;
                } else if (!g_active_resolved_provider.is_empty()) {
                    g_active_source = g_active_resolved_provider;
                } else {
                    g_active_source = res.source;
                }
                pfc::string8 effective_source = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache") ? g_active_resolved_provider : res.source;
                pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
                metadb_handle_ptr now_track;
                static_api_ptr_t<playback_control> pc;
                if (pc->get_now_playing(now_track)) {
                    titleformat_provider::set_track_artwork_info(now_track, artist_str.c_str(), track_str.c_str(), cache_file.c_str(), effective_source.c_str());
                }
                titleformat_provider::set_status((pfc::string8("Artwork loaded from ") + effective_source).c_str());
                artwork_result final_res = res;
                final_res.source = effective_source;
                original_callback(final_res);
                return;
            }
            titleformat_provider::set_status("No artwork found");
            original_callback(res);
        };
        callback = wrapped_callback;

        // Extract broadcast artwork if currently playing an internet stream
        pfc::string8 broadcast_art_url;
        if (is_stream) {
            broadcast_art_url = extract_broadcast_artwork_url();
        }
        bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
            (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());
        bool try_youtube_thumbnail = is_youtube && !yt_video_id.is_empty() &&
            (g_rejected_providers_for_current_track.find("YouTube Thumbnail") == g_rejected_providers_for_current_track.end());

        if (try_broadcast_artwork) {
            foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
        }

        // In single-file cache mode, skip cache reads (key is always "current" so it would
        // return the previous track's artwork). Go directly to broadcast -> API search, still write to cache.
        if (cfg_single_file_cache) {
            if (try_broadcast_artwork) {
                search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist_str, track_str, cache_key, callback, try_youtube_thumbnail, yt_video_id](const artwork_result& res) {
                    if (res.success) {
                        callback(res);
                    } else if (try_youtube_thumbnail) {
                        search_youtube_thumbnail_async(yt_video_id, cache_key, [artist_str, track_str, cache_key, callback](const artwork_result& yt_res) {
                            if (yt_res.success) {
                                callback(yt_res);
                            } else {
                                search_apis_async_metadata(artist_str, track_str, cache_key, callback);
                            }
                        });
                    } else {
                        search_apis_async_metadata(artist_str, track_str, cache_key, callback);
                    }
                });
            } else if (try_youtube_thumbnail) {
                search_youtube_thumbnail_async(yt_video_id, cache_key, [artist_str, track_str, cache_key, callback](const artwork_result& yt_res) {
                    if (yt_res.success) {
                        callback(yt_res);
                    } else {
                        search_apis_async_metadata(artist_str, track_str, cache_key, callback);
                    }
                });
            } else {
                search_apis_async_metadata(artist_str, track_str, cache_key, callback);
            }
        } else {
            // Start async pipeline: Cache -> Broadcast Artwork -> APIs (skip local files since we don't have a track)
            check_cache_async_metadata(cache_key, artist_str, track_str, callback);
        }
    } catch (const std::exception& e) {
        artwork_result result;
        result.success = false;
        result.error_message = e.what();
        callback(result);
    }
}

pfc::string8 artwork_manager::extract_youtube_video_id(const char* path_or_url) {
    if (!path_or_url || strlen(path_or_url) == 0) return "";

    std::string s(path_or_url);

    // 1. Check "v=" query parameter (e.g. youtube.com/watch?v=dQw4w9WgXcQ, fy+https://www.youtube.com/watch?v=dQw4w9WgXcQ)
    size_t v_pos = s.find("v=");
    if (v_pos != std::string::npos && v_pos + 2 < s.length()) {
        std::string vid = s.substr(v_pos + 2, 11);
        if (vid.length() == 11) return vid.c_str();
    }

    // 2. Check "youtu.be/" (e.g. youtu.be/dQw4w9WgXcQ)
    size_t be_pos = s.find("youtu.be/");
    if (be_pos != std::string::npos && be_pos + 9 < s.length()) {
        std::string vid = s.substr(be_pos + 9, 11);
        if (vid.length() == 11) return vid.c_str();
    }

    // 3. Check subpath patterns "embed/", "/v/", "shorts/", "/vi/", "3dyd://", "fy://", "fy+https://", "fy+http://"
    const char* subpaths[] = { "embed/", "/v/", "shorts/", "/vi/", "3dyd://", "fy://", "fy+https://", "fy+http://" };
    for (const char* sp : subpaths) {
        size_t sp_pos = s.find(sp);
        if (sp_pos != std::string::npos) {
            size_t start = sp_pos + strlen(sp);
            if (s.compare(start, 8, "youtube/") == 0) start += 8;
            if (s.compare(start, 6, "video/") == 0) start += 6;
            if (start + 11 <= s.length()) {
                std::string vid = s.substr(start, 11);
                if (vid.length() == 11) return vid.c_str();
            }
        }
    }

    // 4. Regex for standard 11-char YouTube ID across schemes
    std::smatch match;
    static const std::regex yt_regex("(?:(?:fy\\+)?https?:\\/\\/)?(?:www\\.)?(?:youtube\\.com\\/(?:[^\\/\\n\\s]+\\/\\S+\\/|(?:v|e(?:mbed)?|shorts)\\/|(?:.*?[?&])?v=)|youtu\\.be\\/|3dyd:\\/\\/(?:video\\/)?|fy:\\/\\/(?:video\\/)?)([a-zA-Z0-9_-]{11})");
    if (std::regex_search(s, match, yt_regex)) {
        return match[1].str().c_str();
    }

    return "";
}

void artwork_manager::search_youtube_thumbnail_async(const pfc::string8& video_id, const pfc::string8& cache_key, artwork_callback callback) {
    if (video_id.is_empty()) {
        artwork_result fail;
        fail.success = false;
        fail.error_message = "Invalid YouTube video ID";
        callback(fail);
        return;
    }

    foo_artwork::log_printf("foo_artwork: Fetching YouTube video thumbnail for ID '%s'...", video_id.c_str());
    titleformat_provider::set_status("Fetching YouTube thumbnail...");

    pfc::string8 maxres_url = "https://img.youtube.com/vi/";
    maxres_url << video_id << "/maxresdefault.jpg";

    download_image_async(maxres_url.c_str(), [video_id, cache_key, callback](const artwork_result& res) {
        if (res.success && res.data.get_size() > 1024) {
            artwork_result final_res = res;
            final_res.source = "YouTube Thumbnail";
            g_active_resolved_provider = "YouTube Thumbnail";
            g_active_source = "YouTube Thumbnail";
            titleformat_provider::set_status("Artwork loaded from YouTube Thumbnail");
            if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                async_io_manager::instance().cache_set_async(cache_key, final_res.data);
            }
            if (cfg_single_file_cache) {
                async_io_manager::instance().cache_set_async("current", final_res.data);
            }
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            metadb_handle_ptr now_track = g_active_playing_track;
            titleformat_provider::set_track_artwork_info(now_track, g_last_stream_artist.c_str(), g_last_stream_title.c_str(), cache_file.c_str(), "YouTube Thumbnail");
            std::vector<uint8_t> vec(final_res.data.get_ptr(), final_res.data.get_ptr() + final_res.data.get_size());
            create_bitmap_from_image_data(vec);
            refresh_all_dui_artwork_panels();
            refresh_all_cui_artwork_panels();
            callback(final_res);
            return;
        }

        // Fall back to hqdefault
        pfc::string8 hq_url = "https://img.youtube.com/vi/";
        hq_url << video_id << "/hqdefault.jpg";

        download_image_async(hq_url.c_str(), [cache_key, callback](const artwork_result& hq_res) {
            if (hq_res.success && hq_res.data.get_size() > 0) {
                artwork_result final_res = hq_res;
                final_res.source = "YouTube Thumbnail";
                g_active_resolved_provider = "YouTube Thumbnail";
                g_active_source = "YouTube Thumbnail";
                titleformat_provider::set_status("Artwork loaded from YouTube Thumbnail");
                if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                    async_io_manager::instance().cache_set_async(cache_key, final_res.data);
                }
                if (cfg_single_file_cache) {
                    async_io_manager::instance().cache_set_async("current", final_res.data);
                }
                pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
                metadb_handle_ptr now_track = g_active_playing_track;
                titleformat_provider::set_track_artwork_info(now_track, g_last_stream_artist.c_str(), g_last_stream_title.c_str(), cache_file.c_str(), "YouTube Thumbnail");
                std::vector<uint8_t> vec(final_res.data.get_ptr(), final_res.data.get_ptr() + final_res.data.get_size());
                create_bitmap_from_image_data(vec);
                refresh_all_dui_artwork_panels();
                refresh_all_cui_artwork_panels();
                callback(final_res);
            } else {
                callback(hq_res);
            }
        });
    });
}

bool artwork_manager::has_url_flag(const char* url, const char* flag) {
    if (!url || !flag || strlen(url) == 0 || strlen(flag) == 0) return false;
    std::string u(url);
    std::transform(u.begin(), u.end(), u.begin(), ::tolower);
    std::string f(flag);
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);

    std::string p1 = "?" + f;
    std::string p2 = "&" + f;
    std::string p3 = "#" + f;
    return (u.find(p1) != std::string::npos || u.find(p2) != std::string::npos || u.find(p3) != std::string::npos);
}

pfc::string8 artwork_manager::get_url_param_value(const char* url, const char* param_name) {
    if (!url || !param_name || strlen(url) == 0 || strlen(param_name) == 0) return "";
    std::string u(url);
    std::string p(param_name);
    
    std::string u_lower = u;
    std::transform(u_lower.begin(), u_lower.end(), u_lower.begin(), ::tolower);
    std::string p_lower = p;
    std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), ::tolower);

    std::string search1 = "?" + p_lower + "=";
    std::string search2 = "&" + p_lower + "=";
    
    size_t pos = u_lower.find(search1);
    size_t offset = search1.length();
    if (pos == std::string::npos) {
        pos = u_lower.find(search2);
        offset = search2.length();
    }
    
    if (pos == std::string::npos) {
        if (has_url_flag(url, param_name)) {
            return "1";
        }
        return "";
    }
    
    size_t start = pos + offset;
    size_t end = u.find_first_of("&# \t\r\n", start);
    std::string val = (end == std::string::npos) ? u.substr(start) : u.substr(start, end - start);
    return val.c_str();
}

int artwork_manager::extract_coversync_seconds(metadb_handle_ptr track) {
    pfc::string8 sync_val = artwork_manager::get_url_param_value(g_current_stream_url.c_str(), "coversync");
    if (sync_val.is_empty()) {
        if (!track.is_valid()) {
            static_api_ptr_t<playback_control> pc;
            pc->get_now_playing(track);
        }
        if (track.is_valid()) {
            try {
                metadb_info_container::ptr info_container = track->get_info_ref();
                if (info_container.is_valid()) {
                    const file_info& info = info_container->info();
                    const char* cs_meta = info.meta_get("COVERSYNC", 0);
                    if (!cs_meta) cs_meta = info.meta_get("coversync", 0);
                    if (cs_meta && cs_meta[0] != '\0') sync_val = cs_meta;
                    
                    if (sync_val.is_empty()) {
                        const char* at_meta = info.meta_get("@", 0);
                        if (at_meta && at_meta[0] != '\0') {
                            sync_val = artwork_manager::get_url_param_value(at_meta, "coversync");
                        }
                    }
                }
            } catch (...) {}
        }
    }
    if (!sync_val.is_empty()) {
        try {
            return std::atoi(sync_val.c_str());
        } catch (...) {}
    }
    return 0;
}

static pfc::string8 sanitize_broadcast_url(const char* url_str) {
    if (!url_str || strlen(url_str) == 0) return "";
    std::string s(url_str);
    // Trim leading whitespace and quotes
    size_t start = s.find_first_not_of(" \t\r\n\"'");
    if (start == std::string::npos) return "";
    // Trim trailing whitespace and quotes
    size_t end = s.find_last_not_of(" \t\r\n\"'");
    s = s.substr(start, end - start + 1);
    if (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0) {
        if (s.length() >= 10) {
            return s.c_str();
        }
    }
    return "";
}

pfc::string8 artwork_manager::extract_broadcast_artwork_url_from_info(const file_info& info) {
    if (cfg_disable_instream_artwork) return "";

    // 1. Check direct info_get fields (technical/stream info)
    const char* info_field_names[] = {
        "cover_url", "COVER_URL", "artwork_url", "ARTWORK_URL", "art_url", "ART_URL",
        "image_url", "IMAGE_URL", "stream_cover_url", "STREAM_COVER_URL",
        "station_cover_url", "STATION_COVER_URL", "radio_cover_url", "RADIO_COVER_URL",
        "albumart_url", "ALBUMART_URL", "album_art", "ALBUM_ART",
        "album_art_url", "ALBUM_ART_URL", "song_art", "SONG_ART",
        "song_art_url", "SONG_ART_URL", "icy-cover-url", "ICY-COVER-URL",
        "icy-artwork", "ICY-ARTWORK", "icy-logo", "ICY-LOGO",
        "coverart", "COVERART", "cover", "COVER", "song.art"
    };
    for (const char* field : info_field_names) {
        const char* val = info.info_get(field);
        if (val) {
            pfc::string8 clean_url = sanitize_broadcast_url(val);
            if (!clean_url.is_empty()) return clean_url;
        }
    }

    // 2. Check direct meta_get tags (metadata)
    const char* tag_names[] = {
        "COVER_URL", "cover_url", "ARTWORK_URL", "artwork_url", "ART_URL", "art_url",
        "IMAGE_URL", "image_url", "STREAM_COVER_URL", "stream_cover_url",
        "STATION_COVER_URL", "station_cover_url", "RADIO_COVER_URL", "radio_cover_url",
        "ALBUMART_URL", "albumart_url", "ALBUM_ART", "album_art",
        "ALBUM_ART_URL", "album_art_url", "SONG_ART", "song_art",
        "SONG_ART_URL", "song_art_url", "ICY-COVER-URL", "icy-cover-url",
        "ICY-ARTWORK", "icy-artwork", "ICY-LOGO", "icy-logo",
        "COVERART", "coverart", "COVER", "cover"
    };
    for (const char* tag : tag_names) {
        const char* val = info.meta_get(tag, 0);
        if (val) {
            pfc::string8 clean_url = sanitize_broadcast_url(val);
            if (!clean_url.is_empty()) return clean_url;
        }
    }

    // 3. Enumerate all info items (match image/cover/artwork-specific fields only)
    t_size info_cnt = info.info_get_count();
    for (t_size i = 0; i < info_cnt; ++i) {
        const char* name = info.info_enum_name(i);
        const char* val = info.info_enum_value(i);
        if (val && name) {
            std::string n(name);
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (n == "icy-url" || n == "url" || n == "website" || n == "homepage") continue;
            if (n.find("cover") != std::string::npos || n.find("artwork") != std::string::npos ||
                n.find("image") != std::string::npos || n.find("albumart") != std::string::npos ||
                n.find("song_art") != std::string::npos || n.find("pic") != std::string::npos ||
                n.find("thumb") != std::string::npos) {
                pfc::string8 clean_url = sanitize_broadcast_url(val);
                if (!clean_url.is_empty()) return clean_url;
            }
        }
    }

    // 4. Enumerate all meta items (match image/cover/artwork-specific tags only)
    t_size meta_cnt = info.meta_get_count();
    for (t_size i = 0; i < meta_cnt; ++i) {
        const char* name = info.meta_enum_name(i);
        if (name) {
            std::string n(name);
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (n == "icy-url" || n == "url" || n == "website" || n == "homepage") continue;
            if (n.find("cover") != std::string::npos || n.find("artwork") != std::string::npos ||
                n.find("image") != std::string::npos || n.find("albumart") != std::string::npos ||
                n.find("song_art") != std::string::npos || n.find("pic") != std::string::npos ||
                n.find("thumb") != std::string::npos) {
                t_size val_cnt = info.meta_enum_value_count(i);
                for (t_size j = 0; j < val_cnt; ++j) {
                    const char* val = info.meta_enum_value(i, j);
                    if (val) {
                        pfc::string8 clean_url = sanitize_broadcast_url(val);
                        if (!clean_url.is_empty()) return clean_url;
                    }
                }
            }
        }
    }

    return "";
}

pfc::string8 artwork_manager::extract_broadcast_artwork_url(metadb_handle_ptr track) {
    if (cfg_disable_instream_artwork) return "";

    // 1. Extract from track file_info if valid
    if (track.is_valid()) {
        try {
            metadb_info_container::ptr info_container = track->get_info_ref();
            if (info_container.is_valid()) {
                pfc::string8 url = extract_broadcast_artwork_url_from_info(info_container->info());
                if (!url.is_empty()) return url;
            }
        } catch (...) {}
    }

    // 2. Extract from live playback formatting
    try {
        static_api_ptr_t<playback_control> pc;
        if (pc->is_playing() || pc->is_paused()) {
            static_api_ptr_t<titleformat_compiler> compiler;
            const char* tf_patterns[] = {
                "$info(cover_url)", "$info(COVER_URL)",
                "$info(artwork_url)", "$info(ARTWORK_URL)",
                "$info(art_url)", "$info(ART_URL)",
                "$info(image_url)", "$info(IMAGE_URL)",
                "$info(stream_cover_url)", "$info(STREAM_COVER_URL)",
                "$info(station_cover_url)", "$info(STATION_COVER_URL)",
                "$info(radio_cover_url)", "$info(RADIO_COVER_URL)",
                "$info(albumart_url)", "$info(ALBUMART_URL)",
                "$info(album_art)", "$info(album_art_url)",
                "$info(song_art)", "$info(song_art_url)",
                "$info(icy-cover-url)", "$info(ICY-COVER-URL)",
                "$info(icy-artwork)", "$info(ICY-ARTWORK)",
                "$info(icy-logo)", "$info(ICY-LOGO)",
                "$info(cover)", "$info(coverart)",
                "%cover_url%", "%COVER_URL%",
                "%artwork_url%", "%ARTWORK_URL%",
                "%art_url%", "%ART_URL%",
                "%image_url%", "%IMAGE_URL%",
                "%cover%", "%coverart%",
                "%stream_cover_url%", "%station_cover_url%",
                "%radio_cover_url%", "%albumart_url%",
                "%song_art%", "%song_art_url%",
                "%album_art%", "%album_art_url%"
            };
            for (const char* pat : tf_patterns) {
                service_ptr_t<titleformat_object> script;
                compiler->compile_force(script, pat);
                pfc::string8 formatted;
                if (pc->playback_format_title(nullptr, formatted, script, nullptr, playback_control::display_level_all)) {
                    pfc::string8 clean_url = sanitize_broadcast_url(formatted.c_str());
                    if (!clean_url.is_empty()) {
                        return clean_url;
                    }
                }
            }
        }
    } catch (...) {}

    // 3. Fallback: track title formatting if track is valid
    if (track.is_valid()) {
        try {
            static_api_ptr_t<titleformat_compiler> compiler;
            const char* tf_patterns[] = {
                "$info(cover_url)", "$info(artwork_url)", "$info(art_url)",
                "$info(image_url)", "%cover_url%", "%artwork_url%", "%art_url%"
            };
            for (const char* pat : tf_patterns) {
                service_ptr_t<titleformat_object> script;
                compiler->compile_force(script, pat);
                pfc::string8 formatted;
                if (track->format_title(nullptr, formatted, script, nullptr)) {
                    pfc::string8 clean_url = sanitize_broadcast_url(formatted.c_str());
                    if (!clean_url.is_empty()) {
                        return clean_url;
                    }
                }
            }
        } catch (...) {}
    }

    return "";
}

void artwork_manager::search_broadcast_artwork_async(const pfc::string8& cover_url, const pfc::string8& cache_key, artwork_callback callback) {
    pfc::string8 clean_url = sanitize_broadcast_url(cover_url.c_str());
    if (clean_url.is_empty()) {
        artwork_result fail;
        fail.success = false;
        fail.error_message = "Invalid broadcast artwork URL";
        callback(fail);
        return;
    }

    foo_artwork::log_printf("foo_artwork: Fetching in-stream broadcast artwork from '%s'...", clean_url.c_str());

    download_image_async(clean_url.c_str(), [clean_url, cache_key, callback](const artwork_result& res) {
        if (res.success && res.data.get_size() > 0) {
            artwork_result final_res = res;
            final_res.source = "Broadcast Artwork";
            g_active_resolved_provider = "Broadcast Artwork";
            g_active_source = "Broadcast Artwork";
            titleformat_provider::set_status("Artwork loaded from Broadcast Artwork");
            if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                async_io_manager::instance().cache_set_async(cache_key, final_res.data);
            }
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            metadb_handle_ptr now_track = g_active_playing_track;
            titleformat_provider::set_track_artwork_info(now_track, g_last_stream_artist.c_str(), g_last_stream_title.c_str(), cache_file.c_str(), "Broadcast Artwork");
            std::vector<uint8_t> vec(final_res.data.get_ptr(), final_res.data.get_ptr() + final_res.data.get_size());
            create_bitmap_from_image_data(vec);
            refresh_all_dui_artwork_panels();
            refresh_all_cui_artwork_panels();
            foo_artwork::log_printf("foo_artwork: SUCCESS - In-stream broadcast artwork retrieved (%u bytes)", (unsigned)final_res.data.get_size());
            callback(final_res);
        } else {
            foo_artwork::log_printf("foo_artwork: Failed to download broadcast artwork from in-stream URL '%s'", clean_url.c_str());
            callback(res);
        }
    });
}

enum class StreamProbeStatus {
    UNKNOWN,
    FAILED,
    SUCCESS
};

static std::mutex g_probe_cache_mutex;
static std::map<std::string, std::pair<StreamProbeStatus, pfc::string8>> g_probed_stream_endpoints_cache;

pfc::string8 artwork_manager::extract_station_slug_from_url(const char* url) {
    if (!url || url[0] == '\0') return "";
    std::string s = url;
    size_t q = s.find('?');
    if (q != std::string::npos) s = s.substr(0, q);
    size_t h = s.find('#');
    if (h != std::string::npos) s = s.substr(0, h);

    size_t proto = s.find("://");
    if (proto == std::string::npos) return "";

    size_t path_start = s.find('/', proto + 3);
    if (path_start == std::string::npos) return "";
    std::string path = s.substr(path_start);

    std::vector<std::string> segments;
    size_t start = 0;
    while (start < path.length()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.length();
        if (end > start) {
            std::string seg = path.substr(start, end - start);
            if (!seg.empty()) segments.push_back(seg);
        }
        start = end + 1;
    }

    if (segments.empty()) return "";

    for (size_t i = 0; i < segments.size(); ++i) {
        std::string seg_lower = segments[i];
        std::transform(seg_lower.begin(), seg_lower.end(), seg_lower.begin(), ::tolower);
        if ((seg_lower == "hls" || seg_lower == "listen" || seg_lower == "radio" || seg_lower == "stream") && i + 1 < segments.size()) {
            return segments[i + 1].c_str();
        }
    }

    if (!segments.empty()) {
        const std::string& first = segments[0];
        if (first.find(".m3u") == std::string::npos && first.find(".mp3") == std::string::npos && 
            first.find(".aac") == std::string::npos && first.find(".ogg") == std::string::npos &&
            first.find(".flac") == std::string::npos) {
            return first.c_str();
        }
    }

    return "";
}

static const json* find_matching_station_in_array(const json& j_array, const std::string& stream_url) {
    if (!j_array.is_array() || j_array.empty()) return nullptr;

    // Stage 1: Single-Station Check
    // If response array has only 1 station, select it immediately
    if (j_array.size() == 1) {
        return &j_array[0];
    }

    std::string stream_lower = stream_url;
    size_t q_pos = stream_lower.find('?');
    if (q_pos != std::string::npos) stream_lower = stream_lower.substr(0, q_pos);
    std::transform(stream_lower.begin(), stream_lower.end(), stream_lower.begin(), ::tolower);

    // Extract path of stream_url for mount/path comparison
    std::string stream_path = stream_lower;
    size_t proto_pos = stream_path.find("://");
    if (proto_pos != std::string::npos) {
        size_t path_pos = stream_path.find('/', proto_pos + 3);
        if (path_pos != std::string::npos) {
            stream_path = stream_path.substr(path_pos);
        }
    }

    // Stage 2: Mount & Path Comparison
    // Compare stream URL path against all mount paths in station.mounts[].url, station.mounts[].path, and station.hls_url
    for (const auto& item : j_array) {
        if (!item.is_object()) continue;

        if (item.contains("station") && item["station"].is_object()) {
            const auto& st = item["station"];

            // Compare against mounts[].path and mounts[].url
            if (st.contains("mounts") && st["mounts"].is_array()) {
                for (const auto& m : st["mounts"]) {
                    if (m.is_object()) {
                        if (m.contains("path") && m["path"].is_string()) {
                            std::string path = m["path"].get<std::string>();
                            std::transform(path.begin(), path.end(), path.begin(), ::tolower);
                            if (!path.empty()) {
                                if (stream_path == path || stream_path.find(path) != std::string::npos || path.find(stream_path) != std::string::npos) {
                                    return &item;
                                }
                            }
                        }
                        if (m.contains("url") && m["url"].is_string()) {
                            std::string m_url = m["url"].get<std::string>();
                            std::transform(m_url.begin(), m_url.end(), m_url.begin(), ::tolower);
                            if (!m_url.empty()) {
                                if (stream_lower == m_url || stream_lower.find(m_url) != std::string::npos || m_url.find(stream_lower) != std::string::npos) {
                                    return &item;
                                }
                            }
                        }
                    }
                }
            }

            // Compare against station.hls_url
            if (st.contains("hls_url") && st["hls_url"].is_string()) {
                std::string hls = st["hls_url"].get<std::string>();
                std::transform(hls.begin(), hls.end(), hls.begin(), ::tolower);
                if (!hls.empty()) {
                    if (stream_lower == hls || stream_lower.find(hls) != std::string::npos || hls.find(stream_lower) != std::string::npos) {
                        return &item;
                    }
                }
            }
        }
    }

    // Stage 3: URL Slug / Shortcode Matching
    // Match URL path segments (e.g. /powerhouse/) against station shortcode or mount slugs
    pfc::string8 slug_pfc = artwork_manager::extract_station_slug_from_url(stream_url.c_str());
    std::string slug = slug_pfc.c_str();
    std::transform(slug.begin(), slug.end(), slug.begin(), ::tolower);

    for (const auto& item : j_array) {
        if (!item.is_object()) continue;

        if (item.contains("station") && item["station"].is_object()) {
            const auto& st = item["station"];

            if (st.contains("shortcode") && st["shortcode"].is_string()) {
                std::string sc = st["shortcode"].get<std::string>();
                std::transform(sc.begin(), sc.end(), sc.begin(), ::tolower);
                if (!sc.empty()) {
                    std::string sc_slash = "/" + sc + "/";
                    std::string sc_end = "/" + sc;
                    if (sc == slug || stream_lower.find(sc_slash) != std::string::npos || 
                        (stream_lower.size() >= sc_end.size() && stream_lower.rfind(sc_end) == stream_lower.size() - sc_end.size()) ||
                        stream_lower.find(sc) != std::string::npos) {
                        return &item;
                    }
                }
            }

            // Also check station ID matching slug or path segment
            if (st.contains("id")) {
                std::string id_str;
                if (st["id"].is_string()) id_str = st["id"].get<std::string>();
                else if (st["id"].is_number()) id_str = std::to_string(st["id"].get<int>());
                if (!id_str.empty()) {
                    std::string id_slash = "/" + id_str + "/";
                    std::string id_end = "/" + id_str;
                    if (id_str == slug || stream_lower.find(id_slash) != std::string::npos || 
                        (stream_lower.size() >= id_end.size() && stream_lower.rfind(id_end) == stream_lower.size() - id_end.size())) {
                        return &item;
                    }
                }
            }
        }
    }

    // Stage 4: Default Fallback
    // If zero matches found in a multi-station JSON payload, default to Station 1 (or index 0)
    // and log a concise notice to the console.
    foo_artwork::log_printf("foo_artwork: Multi-station stream detected without exact mount match. Defaulted to Station 1. Specify '?azuracast_api={id}' in stream URL for targeted station.");

    for (const auto& item : j_array) {
        if (item.is_object() && item.contains("station") && item["station"].is_object()) {
            const auto& st = item["station"];
            if (st.contains("id")) {
                if ((st["id"].is_number() && st["id"].get<int>() == 1) ||
                    (st["id"].is_string() && st["id"].get<std::string>() == "1")) {
                    return &item;
                }
            }
        }
    }

    return &j_array[0];
}

void artwork_manager::stop_external_stream_api_poller() {
    g_external_api_session_token++;
}

static void extract_track_metadata_dynamic(metadb_handle_ptr track, pfc::string8& out_artist, pfc::string8& out_title);

static void add_unique_candidate(std::vector<pfc::string8>& list, const pfc::string8& url) {
    if (url.is_empty()) return;
    for (const auto& existing : list) {
        if (existing == url) return;
    }
    list.push_back(url);
}

static void probe_candidates_async(const pfc::string8& stream_url, const std::vector<pfc::string8>& candidates, size_t index, uint64_t session_token) {
    ASSERT_MAIN_THREAD();
    if (g_is_shutting_down.load() || g_external_api_session_token != session_token) return;

    if (index >= candidates.size()) {
        std::lock_guard<std::mutex> lock(g_probe_cache_mutex);
        g_probed_stream_endpoints_cache[stream_url.c_str()] = { StreamProbeStatus::FAILED, "" };
        return;
    }

    pfc::string8 target = candidates[index];
    foo_artwork::log_printf("foo_artwork: Auto-probing stream for now-playing API at: %s", target.c_str());

    async_io_manager::instance().http_get_async(target.c_str(), [stream_url, candidates, index, target, session_token](bool success, const pfc::string8& response, const pfc::string8& error) {
        ASSERT_MAIN_THREAD();
        if (g_is_shutting_down.load() || g_external_api_session_token != session_token) return;

        bool validated = false;
        if (success && !response.is_empty()) {
            try {
                json j = json::parse(response.c_str(), nullptr, false);
                if (!j.is_discarded()) {
                    if (j.is_object()) {
                        if (j.contains("now_playing") || 
                            (j.contains("station") && j.contains("song")) || 
                            j.contains("song") || 
                            (j.contains("artist") && j.contains("title"))) {
                            validated = true;
                        }
                    } else if (j.is_array() && !j.empty() && j[0].is_object()) {
                        if (j[0].contains("now_playing") || 
                            j[0].contains("station") || 
                            (j[0].contains("artist") && j[0].contains("title"))) {
                            validated = true;
                        }
                    }
                }
            } catch (...) {}
        }

        if (validated) {
            // Guard: Check if native ICY metadata has already arrived on the stream
            metadb_handle_ptr track;
            if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
                pfc::string8 art, tit;
                extract_track_metadata_dynamic(track, art, tit);
                StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
                if (meta.is_valid_search && !meta.is_station_or_url) {
                    // Native ICY metadata is active on the stream - do not launch auto-probed poller
                    return;
                }
            }

            {
                std::lock_guard<std::mutex> lock(g_probe_cache_mutex);
                g_probed_stream_endpoints_cache[stream_url.c_str()] = { StreamProbeStatus::SUCCESS, target };
            }
            foo_artwork::log_printf("foo_artwork: Server auto-probing successfully discovered external API: %s", target.c_str());
            artwork_manager::poll_external_stream_api(target, session_token);
        } else {
            probe_candidates_async(stream_url, candidates, index + 1, session_token);
        }
    });
}

void artwork_manager::probe_external_stream_api(const pfc::string8& stream_url, uint64_t session_token) {
    ASSERT_MAIN_THREAD();
    if (g_is_shutting_down.load() || g_external_api_session_token != session_token) return;

    std::string s = stream_url.c_str();
    size_t proto = s.find("://");
    if (proto == std::string::npos || s.find("file://") == 0) return;

    size_t host_end = s.find('/', proto + 3);
    std::string base = (host_end != std::string::npos) ? s.substr(0, host_end) : s;
    if (base.empty()) return;

    // Check probe cache
    {
        std::lock_guard<std::mutex> lock(g_probe_cache_mutex);
        auto it = g_probed_stream_endpoints_cache.find(stream_url.c_str());
        if (it != g_probed_stream_endpoints_cache.end()) {
            if (it->second.first == StreamProbeStatus::SUCCESS) {
                // If stream already has metadata, do not poll cached endpoint
                metadb_handle_ptr track;
                if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
                    pfc::string8 art, tit;
                    extract_track_metadata_dynamic(track, art, tit);
                    StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
                    if (meta.is_valid_search && !meta.is_station_or_url) {
                        return;
                    }
                }
                pfc::string8 endpoint = it->second.second;
                foo_artwork::log_printf("foo_artwork: Using cached external now-playing endpoint: %s", endpoint.c_str());
                poll_external_stream_api(endpoint, session_token);
                return;
            } else if (it->second.first == StreamProbeStatus::FAILED) {
                return; // Probed previously and not an external API endpoint
            }
        }
    }

    pfc::string8 slug = extract_station_slug_from_url(stream_url.c_str());

    // Extract numeric segment / sub-id if present
    std::string sub_id;
    size_t q = s.find('?');
    std::string path_only = (q != std::string::npos) ? s.substr(0, q) : s;
    size_t path_start = path_only.find('/', proto + 3);
    if (path_start != std::string::npos) {
        std::string p = path_only.substr(path_start);
        size_t start = 0;
        while (start < p.length()) {
            size_t end = p.find('/', start);
            if (end == std::string::npos) end = p.length();
            if (end > start) {
                std::string seg = p.substr(start, end - start);
                if (!seg.empty() && seg.find_first_not_of("0123456789") == std::string::npos) {
                    sub_id = seg;
                    break;
                }
            }
            start = end + 1;
        }
    }

    std::vector<pfc::string8> candidates;
    bool is_known_radioreg_network = contains_case_insensitive(s.c_str(), "atomicradio") || contains_case_insensitive(s.c_str(), "radioreg");

    if (is_known_radioreg_network) {
        // Prioritize RadioReg candidates for atomicradio / radioreg networks
        if (!slug.is_empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/stream/" + std::string(slug.c_str())).c_str());
        if (!sub_id.empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/stream/" + sub_id).c_str());
        if (!slug.is_empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/nowplaying/" + std::string(slug.c_str())).c_str());
        if (!sub_id.empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/nowplaying/" + sub_id).c_str());
        add_unique_candidate(candidates, "https://api.radioreg.net/stream/1");
        add_unique_candidate(candidates, "https://api.radioreg.net/nowplaying/1");
        if (!slug.is_empty()) add_unique_candidate(candidates, (base + "/api/nowplaying/" + slug.c_str()).c_str());
        add_unique_candidate(candidates, (base + "/api/nowplaying").c_str());
    } else {
        // Prioritize same-origin AzuraCast candidates for generic streams
        if (!slug.is_empty()) add_unique_candidate(candidates, (base + "/api/nowplaying/" + slug.c_str()).c_str());
        add_unique_candidate(candidates, (base + "/api/nowplaying").c_str());
        if (!slug.is_empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/stream/" + std::string(slug.c_str())).c_str());
        if (!sub_id.empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/stream/" + sub_id).c_str());
        if (!slug.is_empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/nowplaying/" + std::string(slug.c_str())).c_str());
        if (!sub_id.empty()) add_unique_candidate(candidates, ("https://api.radioreg.net/nowplaying/" + sub_id).c_str());
    }

    if (candidates.empty()) return;

    probe_candidates_async(stream_url, candidates, 0, session_token);
}

void artwork_manager::start_external_stream_api_poller(const pfc::string8& stream_url) {
    stop_external_stream_api_poller();

    pfc::string8 azuracast_val = get_url_param_value(stream_url.c_str(), "azuracast_api");
    pfc::string8 radioreg_val = get_url_param_value(stream_url.c_str(), "radioreg_api");

    // Also check metadb tags (e.g. from foo_external_tags)
    if (azuracast_val.is_empty() && radioreg_val.is_empty()) {
        metadb_handle_ptr now_playing;
        if (playback_control::get()->get_now_playing(now_playing) && now_playing.is_valid()) {
            metadb_info_container::ptr info_container = now_playing->get_info_ref();
            if (info_container.is_valid()) {
                const file_info& info = info_container->info();
                const char* az_meta = info.meta_get("AZURACAST_API", 0);
                if (!az_meta) az_meta = info.meta_get("azuracast_api", 0);
                if (az_meta && az_meta[0] != '\0') azuracast_val = az_meta;

                const char* rr_meta = info.meta_get("RADIOREG_API", 0);
                if (!rr_meta) rr_meta = info.meta_get("radioreg_api", 0);
                if (rr_meta && rr_meta[0] != '\0') radioreg_val = rr_meta;
            }
        }
    }

    bool is_azura = !azuracast_val.is_empty() || contains_case_insensitive(stream_url.c_str(), "/api/nowplaying");
    bool is_radioreg = !radioreg_val.is_empty() || contains_case_insensitive(stream_url.c_str(), "radioreg.net");

    uint64_t current_token = ++g_external_api_session_token;

    if (!is_azura && !is_radioreg) {
        // Check if stream metadata is already valid and not a placeholder/station URL
        metadb_handle_ptr track;
        if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
            pfc::string8 art, tit;
            extract_track_metadata_dynamic(track, art, tit);
            StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
            if (meta.is_valid_search && !meta.is_station_or_url) {
                // Native ICY stream metadata exists - do not auto-probe external server endpoints
                return;
            }
        }
        // Automatic server probing for AzuraCast / RadioReg now-playing endpoints on untagged streams
        probe_external_stream_api(stream_url, current_token);
        return;
    }

    pfc::string8 endpoint_url;
    if (is_azura) {
        if (azuracast_val.find_first("http://") == 0 || azuracast_val.find_first("https://") == 0) {
            endpoint_url = azuracast_val;
        } else {
            std::string s = stream_url.c_str();
            size_t proto = s.find("://");
            if (proto != std::string::npos) {
                size_t host_end = s.find('/', proto + 3);
                std::string base = (host_end != std::string::npos) ? s.substr(0, host_end) : s;
                bool has_explicit_val = contains_case_insensitive(stream_url.c_str(), "azuracast_api=") || 
                                        (!azuracast_val.is_empty() && azuracast_val != "1");
                if (has_explicit_val && !azuracast_val.is_empty()) {
                    endpoint_url = (base + "/api/nowplaying/" + azuracast_val.c_str()).c_str();
                } else {
                    endpoint_url = (base + "/api/nowplaying").c_str();
                }
            }
        }
    } else if (is_radioreg) {
        if (radioreg_val.find_first("http://") == 0 || radioreg_val.find_first("https://") == 0) {
            endpoint_url = radioreg_val;
        } else {
            bool has_explicit_val = contains_case_insensitive(stream_url.c_str(), "radioreg_api=");
            std::string station;
            if (has_explicit_val && !radioreg_val.is_empty()) {
                station = radioreg_val.c_str();
            } else {
                pfc::string8 slug = extract_station_slug_from_url(stream_url.c_str());
                station = !slug.is_empty() ? slug.c_str() : "1";
            }
            endpoint_url = ("https://api.radioreg.net/stream/" + station).c_str();
        }
    }

    if (endpoint_url.is_empty()) return;

    foo_artwork::log_printf("foo_artwork: Starting external now-playing API poller for endpoint: %s", endpoint_url.c_str());

    // Schedule initial poll after 500ms delay via async_io_manager worker and post back to main thread
    async_io_manager::instance().submit_task([endpoint_url, current_token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (g_is_shutting_down.load() || g_external_api_session_token != current_token) return;

        async_io_manager::instance().post_to_main_thread([endpoint_url, current_token]() {
            poll_external_stream_api(endpoint_url, current_token);
        });
    });
}

void artwork_manager::poll_external_stream_api(const pfc::string8& endpoint_url, uint64_t session_token) {
    ASSERT_MAIN_THREAD();
    if (g_is_shutting_down.load() || g_external_api_session_token != session_token) return;

    static_api_ptr_t<playback_control> pc;
    if (!pc->is_playing() && !pc->is_paused()) return;

    async_io_manager::instance().http_get_async(endpoint_url.c_str(), [endpoint_url, session_token](bool success, const pfc::string8& json_response, const pfc::string8& error) {
        ASSERT_MAIN_THREAD();
        if (g_is_shutting_down.load() || g_external_api_session_token != session_token) {
            return;
        }

        static_api_ptr_t<playback_control> pc_resp;
        if (!pc_resp->is_playing() && !pc_resp->is_paused()) {
            return;
        }

        if (success && !json_response.is_empty()) {
            try {
                json j = json::parse(json_response.c_str(), nullptr, false);
                if (!j.is_discarded()) {
                    std::string artist, title, art_url, album, listeners;

                    if (j.is_object() && j.contains("now_playing") && j["now_playing"].is_object()) {
                        auto& np = j["now_playing"];
                        if (np.contains("song") && np["song"].is_object()) {
                            auto& song = np["song"];
                            if (song.contains("artist") && song["artist"].is_string()) artist = song["artist"].get<std::string>();
                            if (song.contains("title") && song["title"].is_string()) title = song["title"].get<std::string>();
                            if (song.contains("album") && song["album"].is_string()) album = song["album"].get<std::string>();
                            if (song.contains("art") && song["art"].is_string()) art_url = song["art"].get<std::string>();
                            if (song.contains("text") && song["text"].is_string() && (artist.empty() || title.empty())) {
                                std::string text = song["text"].get<std::string>();
                                size_t dash = text.find(" - ");
                                if (dash != std::string::npos) {
                                    artist = text.substr(0, dash);
                                    title = text.substr(dash + 3);
                                }
                            }
                        }
                        if (j.contains("listeners") && j["listeners"].is_object()) {
                            auto& l = j["listeners"];
                            if (l.contains("current")) {
                                if (l["current"].is_number()) listeners = std::to_string(l["current"].get<int>());
                                else if (l["current"].is_string()) listeners = l["current"].get<std::string>();
                            } else if (l.contains("total")) {
                                if (l["total"].is_number()) listeners = std::to_string(l["total"].get<int>());
                                else if (l["total"].is_string()) listeners = l["total"].get<std::string>();
                            }
                        }
                    } else if (j.is_array() && !j.empty()) {
                        const json* matched_station = find_matching_station_in_array(j, g_current_stream_url.c_str());
                        if (matched_station && matched_station->is_object()) {
                            if (matched_station->contains("now_playing") && (*matched_station)["now_playing"].is_object()) {
                                auto& np = (*matched_station)["now_playing"];
                                if (np.contains("song") && np["song"].is_object()) {
                                    auto& song = np["song"];
                                    if (song.contains("artist") && song["artist"].is_string()) artist = song["artist"].get<std::string>();
                                    if (song.contains("title") && song["title"].is_string()) title = song["title"].get<std::string>();
                                    if (song.contains("album") && song["album"].is_string()) album = song["album"].get<std::string>();
                                    if (song.contains("art") && song["art"].is_string()) art_url = song["art"].get<std::string>();
                                    if (song.contains("text") && song["text"].is_string() && (artist.empty() || title.empty())) {
                                        std::string text = song["text"].get<std::string>();
                                        size_t dash = text.find(" - ");
                                        if (dash != std::string::npos) {
                                            artist = text.substr(0, dash);
                                            title = text.substr(dash + 3);
                                        }
                                    }
                                }
                            } else if (matched_station->contains("song") && (*matched_station)["song"].is_object()) {
                                auto& song = (*matched_station)["song"];
                                if (song.contains("artist") && song["artist"].is_string()) artist = song["artist"].get<std::string>();
                                if (song.contains("title") && song["title"].is_string()) title = song["title"].get<std::string>();
                                if (song.contains("album") && song["album"].is_string()) album = song["album"].get<std::string>();
                                if (song.contains("art") && song["art"].is_string()) art_url = song["art"].get<std::string>();
                            }
                            if (matched_station->contains("listeners") && (*matched_station)["listeners"].is_object()) {
                                auto& l = (*matched_station)["listeners"];
                                if (l.contains("current")) {
                                    if (l["current"].is_number()) listeners = std::to_string(l["current"].get<int>());
                                    else if (l["current"].is_string()) listeners = l["current"].get<std::string>();
                                }
                            }
                        }
                    } else if (j.is_object()) {
                        if (j.contains("artist") && j["artist"].is_string()) artist = j["artist"].get<std::string>();
                        if (j.contains("title") && j["title"].is_string()) title = j["title"].get<std::string>();
                        if (j.contains("album") && j["album"].is_string()) album = j["album"].get<std::string>();
                        if (j.contains("cover_url") && j["cover_url"].is_string()) art_url = j["cover_url"].get<std::string>();
                        else if (j.contains("art") && j["art"].is_string()) art_url = j["art"].get<std::string>();
                        else if (j.contains("image") && j["image"].is_string()) art_url = j["image"].get<std::string>();
                        else if (j.contains("cover") && j["cover"].is_string()) art_url = j["cover"].get<std::string>();
                        else if (j.contains("artwork") && j["artwork"].is_string()) art_url = j["artwork"].get<std::string>();
                        
                        if (j.contains("song") && j["song"].is_object()) {
                            auto& song = j["song"];
                            if (artist.empty() && song.contains("artist") && song["artist"].is_string()) artist = song["artist"].get<std::string>();
                            if (title.empty() && song.contains("title") && song["title"].is_string()) title = song["title"].get<std::string>();
                            if (album.empty() && song.contains("album") && song["album"].is_string()) album = song["album"].get<std::string>();
                            if (art_url.empty() && song.contains("art") && song["art"].is_string()) art_url = song["art"].get<std::string>();
                            if (art_url.empty() && song.contains("cover_url") && song["cover_url"].is_string()) art_url = song["cover_url"].get<std::string>();
                            if (art_url.empty() && song.contains("image") && song["image"].is_string()) art_url = song["image"].get<std::string>();
                            if (art_url.empty() && song.contains("cover") && song["cover"].is_string()) art_url = song["cover"].get<std::string>();
                            if (art_url.empty() && song.contains("artwork") && song["artwork"].is_string()) art_url = song["artwork"].get<std::string>();
                        }
                        if (j.contains("listeners")) {
                            if (j["listeners"].is_number()) listeners = std::to_string(j["listeners"].get<int>());
                            else if (j["listeners"].is_string()) listeners = j["listeners"].get<std::string>();
                            else if (j["listeners"].is_object() && j["listeners"].contains("current")) {
                                if (j["listeners"]["current"].is_number()) listeners = std::to_string(j["listeners"]["current"].get<int>());
                                else if (j["listeners"]["current"].is_string()) listeners = j["listeners"]["current"].get<std::string>();
                            }
                        }
                    }

                    if (!artist.empty() || !title.empty()) {
                        if (artwork_manager::has_url_flag(g_current_stream_url.c_str(), "inverted")) {
                            std::swap(artist, title);
                        }

                        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), title.c_str());
                        pfc::string8 clean_art = meta.first_artist.c_str();
                        pfc::string8 clean_tit = meta.clean_title.c_str();
                        pfc::string8 clean_art_full = meta.clean_artist.c_str();

                        // Case-insensitive comparison against last stream track and pending cue
                        bool is_different_from_last = (pfc::stricmp_ascii(clean_art.c_str(), g_last_stream_artist.c_str()) != 0 ||
                                                       pfc::stricmp_ascii(clean_tit.c_str(), g_last_stream_title.c_str()) != 0);
                        bool is_different_from_pending = (pfc::stricmp_ascii(clean_art.c_str(), g_pending_external_api_artist.c_str()) != 0 ||
                                                          pfc::stricmp_ascii(clean_tit.c_str(), g_pending_external_api_title.c_str()) != 0);

                        if (is_different_from_last && is_different_from_pending) {
                            auto apply_cue = [session_token](std::string artist_c, std::string title_c, std::string art_url_c, std::string album_c, std::string listeners_c) {
                                ASSERT_MAIN_THREAD();
                                if (g_is_shutting_down.load() || g_external_api_session_token != session_token) return;
                                static_api_ptr_t<playback_control> pc_cue;
                                if (!pc_cue->is_playing() && !pc_cue->is_paused()) return;

                                g_pending_external_api_artist.reset();
                                g_pending_external_api_title.reset();

                                foo_artwork::log_printf("foo_artwork: External API cue - Track changed: '%s - %s'", artist_c.c_str(), title_c.c_str());
                                g_rejected_providers_for_current_track.clear();
                                g_active_resolved_provider.reset();

                                StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist_c.c_str(), title_c.c_str());
                                pfc::string8 clean_art = meta.first_artist.c_str();
                                pfc::string8 clean_tit = meta.clean_title.c_str();
                                pfc::string8 clean_art_full = meta.clean_artist.c_str();

                                metadb_handle_ptr track;
                                if (pc_cue->get_now_playing(track) && track.is_valid()) {
                                    titleformat_provider::set_track_artwork_info(track, clean_art.c_str(), clean_tit.c_str(), "", "", clean_art_full.c_str(), album_c.c_str(), listeners_c.c_str());
                                }

                                if (!cfg_disable_instream_artwork && !art_url_c.empty() && (art_url_c.find("http://") == 0 || art_url_c.find("https://") == 0) &&
                                    (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end())) {
                                    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key(clean_art.c_str(), clean_tit.c_str());
                                    search_broadcast_artwork_async(art_url_c.c_str(), cache_key, [clean_art, clean_tit, clean_art_full, album_c, listeners_c, cache_key](const artwork_result& res) {
                                        if (res.success) {
                                            g_last_stream_artist = clean_art;
                                            g_last_stream_title = clean_tit;
                                            g_stream_monitor_token++;
                                            stop_rms_silence_detector();
                                            reset_acrcloud_cooldown();
                                            log_simplified_track_info(clean_art.c_str(), clean_tit.c_str());

                                            g_active_source = res.source;
                                            g_active_resolved_provider = res.source;

                                            if (res.data.get_size() > 0) {
                                                std::vector<BYTE> vec(res.data.get_ptr(), res.data.get_ptr() + res.data.get_size());
                                                create_bitmap_from_image_data(vec);
                                            }

                                            metadb_handle_ptr track;
                                            if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
                                                pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
                                                titleformat_provider::set_track_artwork_info(track, clean_art.c_str(), clean_tit.c_str(), cache_file.c_str(), res.source.c_str(), clean_art_full.c_str(), album_c.c_str(), listeners_c.c_str());
                                            }

                                            pfc::string8 status_msg = pfc::string8("Artwork loaded from ") + res.source;
                                            titleformat_provider::set_status(status_msg.c_str());

                                            refresh_all_dui_artwork_panels();
                                            refresh_all_cui_artwork_panels();
                                        } else {
                                            artwork_manager::on_stream_metadata_changed(clean_art.c_str(), clean_tit.c_str(), clean_art_full.c_str(), album_c.c_str(), listeners_c.c_str());
                                        }
                                    });
                                } else {
                                    artwork_manager::on_stream_metadata_changed(clean_art.c_str(), clean_tit.c_str(), clean_art_full.c_str(), album_c.c_str(), listeners_c.c_str());
                                }
                            };

                            metadb_handle_ptr cur_track;
                            pc_resp->get_now_playing(cur_track);
                            int sync_delay_sec = extract_coversync_seconds(cur_track);
                            if (sync_delay_sec > 0) {
                                g_pending_external_api_artist = clean_art.c_str();
                                g_pending_external_api_title = clean_tit.c_str();
                                uint64_t cue_token = ++g_coversync_cue_token;
                                foo_artwork::log_printf("foo_artwork: Applying coversync delay of %d seconds for external API track cue '%s - %s'", 
                                                       sync_delay_sec, clean_art.c_str(), clean_tit.c_str());
                                char status_buf[128];
                                snprintf(status_buf, sizeof(status_buf), "Next cue in %ds: %s - %s", sync_delay_sec, clean_art.c_str(), clean_tit.c_str());
                                titleformat_provider::set_status(status_buf);

                                async_io_manager::instance().submit_task([clean_art, clean_tit, art_url, album, listeners, session_token, cue_token, sync_delay_sec, apply_cue]() {
                                    for (int s = sync_delay_sec; s > 0; --s) {
                                        if (g_is_shutting_down.load() || g_external_api_session_token != session_token || g_coversync_cue_token != cue_token) {
                                            return;
                                        }
                                        char status_buf[128];
                                        snprintf(status_buf, sizeof(status_buf), "Next cue in %ds: %s - %s", s, clean_art.c_str(), clean_tit.c_str());
                                        titleformat_provider::set_status(status_buf);
                                        std::this_thread::sleep_for(std::chrono::seconds(1));
                                    }
                                    if (g_is_shutting_down.load() || g_external_api_session_token != session_token || g_coversync_cue_token != cue_token) {
                                        return;
                                    }

                                    async_io_manager::instance().post_to_main_thread([clean_art, clean_tit, art_url, album, listeners, session_token, cue_token, apply_cue]() {
                                        if (g_is_shutting_down.load() || g_external_api_session_token != session_token || g_coversync_cue_token != cue_token) {
                                            return;
                                        }
                                        apply_cue(clean_art.c_str(), clean_tit.c_str(), art_url, album, listeners);
                                    });
                                });
                            } else {
                                apply_cue(clean_art.c_str(), clean_tit.c_str(), art_url, album, listeners);
                            }
                        } else if (!listeners.empty()) {
                            metadb_handle_ptr cur_track;
                            if (pc_resp->get_now_playing(cur_track) && cur_track.is_valid()) {
                                pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(generate_cache_key(g_last_stream_artist.c_str(), g_last_stream_title.c_str()));
                                titleformat_provider::set_track_artwork_info(cur_track, g_last_stream_artist.c_str(), g_last_stream_title.c_str(), cache_file.c_str(), g_active_source.c_str(), "", album.c_str(), listeners.c_str());
                            }
                        }
                    }
                }
            } catch (...) {}
        }

        // Schedule next poll iteration after 5 seconds on background thread, then post back to main thread
        if (g_external_api_session_token == session_token && !g_is_shutting_down.load()) {
            async_io_manager::instance().submit_task([endpoint_url, session_token]() {
                for (int i = 0; i < 10; ++i) {
                    if (g_external_api_session_token != session_token || g_is_shutting_down.load()) return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                if (g_external_api_session_token != session_token || g_is_shutting_down.load()) return;

                async_io_manager::instance().post_to_main_thread([endpoint_url, session_token]() {
                    poll_external_stream_api(endpoint_url, session_token);
                });
            });
        }
    });
}

void artwork_manager::reject_current_artwork() {
    ASSERT_MAIN_THREAD();

    metadb_handle_ptr track;
    if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
        g_active_playing_track = track;
    } else {
        track = g_active_playing_track;
    }

    if (!track.is_valid()) {
        foo_artwork::log_printf("foo_artwork: Reject Artwork: No track currently playing.");
        return;
    }

    // Build list of all enabled providers for this track in search order
    std::vector<pfc::string8> available_providers;
    auto api_order = get_api_search_order();
    for (auto api : api_order) {
        switch (api) {
            case ApiType::iTunes: if (cfg_enable_itunes) available_providers.push_back("iTunes"); break;
            case ApiType::Deezer: if (cfg_enable_deezer) available_providers.push_back("Deezer"); break;
            case ApiType::LastFm: if (cfg_enable_lastfm && !cfg_lastfm_key.is_empty()) available_providers.push_back("Last.fm"); break;
            case ApiType::MusicBrainz: if (cfg_enable_musicbrainz) available_providers.push_back("MusicBrainz"); break;
            case ApiType::Discogs: if (cfg_enable_discogs && (!cfg_discogs_key.is_empty() || (!cfg_discogs_consumer_key.is_empty() && !cfg_discogs_consumer_secret.is_empty()))) available_providers.push_back("Discogs"); break;
        }
    }
    if (!extract_broadcast_artwork_url(track).is_empty()) {
        available_providers.push_back("Broadcast Artwork");
    }
    if (track.is_valid() && (!extract_youtube_video_id(track->get_path()).is_empty() || !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty())) {
        available_providers.push_back("YouTube Thumbnail");
    }
    if (cfg_enable_acrcloud && !cfg_acrcloud_host.is_empty() && !cfg_acrcloud_access_key.is_empty()) {
        available_providers.push_back("ACRCloud");
    }

    pfc::string8 source_to_reject = g_active_resolved_provider;
    if (source_to_reject.is_empty()) {
        if (!g_active_source.is_empty() && g_active_source != "Cache") {
            source_to_reject = g_active_source;
        } else {
            // Find the first enabled provider in the priority chain not yet rejected
            for (const auto& prov : available_providers) {
                if (g_rejected_providers_for_current_track.find(prov.c_str()) == g_rejected_providers_for_current_track.end()) {
                    source_to_reject = prov;
                    break;
                }
            }
        }
    }

    if (source_to_reject.is_empty()) {
        source_to_reject = "Unknown";
    }

    foo_artwork::log_printf("foo_artwork: Rejecting artwork from '%s' for current track. Searching next provider in chain...", source_to_reject.c_str());
    g_rejected_providers_for_current_track.insert(source_to_reject.c_str());
    g_active_resolved_provider.reset();
    g_active_source.reset();

    // Check if all available providers for this track have now been rejected
    bool all_rejected = true;
    for (const auto& prov : available_providers) {
        if (g_rejected_providers_for_current_track.find(prov.c_str()) == g_rejected_providers_for_current_track.end()) {
            all_rejected = false;
            break;
        }
    }

    if (all_rejected && !available_providers.empty()) {
        foo_artwork::log_printf("foo_artwork: All artwork providers rejected for current track. Looping back to first provider in priority chain...");
        g_rejected_providers_for_current_track.clear();
    }

    // Invalidate current cache on disk
    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key_for_track(track);
    async_io_manager::instance().cache_remove(cache_key);
    if (cfg_single_file_cache) {
        async_io_manager::instance().cache_remove("current");
    }

    // Clear in-memory deduplication and in-flight records
    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        g_api_dedup_map.clear();
        g_in_flight_queries.clear();
    }

    // Start fresh search to load artwork from the next provider
    get_artwork_async(track, [](const artwork_result& res) {
        refresh_all_dui_artwork_panels();
        refresh_all_cui_artwork_panels();
    });
}

struct PerceptualVector {
    double db1, db2, db3, db4, db5; // 5 sub-band log-energies in dB
    double total_rms;
    bool valid;

    PerceptualVector() : db1(-100), db2(-100), db3(-100), db4(-100), db5(-100), total_rms(0), valid(false) {}
};

static PerceptualVector extract_5band_vector(audio_chunk_impl& chunk) {
    PerceptualVector vec;
    const audio_sample* data = chunk.get_data();
    size_t sample_cnt = chunk.get_sample_count();
    unsigned chans = chunk.get_channels();
    size_t total_samples = sample_cnt * chans;

    if (!data || total_samples == 0) return vec;

    double sum_total = 0.0;
    double sum_b1 = 0.0, sum_b2 = 0.0, sum_b3 = 0.0, sum_b4 = 0.0, sum_b5 = 0.0;

    // 5-band recursive IIR filter states
    double lp1 = 0.0, lp2 = 0.0, lp3 = 0.0, lp4 = 0.0;
    double a1 = 0.015; // ~150 Hz (sub-bass)
    double a2 = 0.060; // ~600 Hz (bass/low-mid)
    double a3 = 0.250; // ~2.5 kHz (mid/vocals)
    double a4 = 0.550; // ~7 kHz (treble)

    for (size_t i = 0; i < total_samples; ++i) {
        double s = (double)data[i];
        sum_total += s * s;

        lp1 += a1 * (s - lp1);
        lp2 += a2 * (s - lp2);
        lp3 += a3 * (s - lp3);
        lp4 += a4 * (s - lp4);

        double s_sub = lp1;
        double s_bass = lp2 - lp1;
        double s_vocal = lp3 - lp2;
        double s_treble = lp4 - lp3;
        double s_high = s - lp4;

        sum_b1 += s_sub * s_sub;
        sum_b2 += s_bass * s_bass;
        sum_b3 += s_vocal * s_vocal;
        sum_b4 += s_treble * s_treble;
        sum_b5 += s_high * s_high;
    }

    vec.total_rms = std::sqrt(sum_total / (double)total_samples);
    double e1 = std::sqrt(sum_b1 / (double)total_samples);
    double e2 = std::sqrt(sum_b2 / (double)total_samples);
    double e3 = std::sqrt(sum_b3 / (double)total_samples);
    double e4 = std::sqrt(sum_b4 / (double)total_samples);
    double e5 = std::sqrt(sum_b5 / (double)total_samples);

    // Convert sub-band energies to log-dB values for scale-invariant distance comparison
    vec.db1 = 10.0 * std::log10(e1 * e1 + 1e-8);
    vec.db2 = 10.0 * std::log10(e2 * e2 + 1e-8);
    vec.db3 = 10.0 * std::log10(e3 * e3 + 1e-8);
    vec.db4 = 10.0 * std::log10(e4 * e4 + 1e-8);
    vec.db5 = 10.0 * std::log10(e5 * e5 + 1e-8);
    vec.valid = true;

    return vec;
}

static void stop_rms_silence_detector(bool force) {
    if (force || !contains_case_insensitive(g_current_stream_url.c_str(), "forceacr")) {
        g_rms_detector_token++;
    }
}

static void start_rms_silence_detector(const pfc::string8& stream_url) {
    bool is_stream = strstr(stream_url.c_str(), "://") && !strstr(stream_url.c_str(), "file://");
    if (!is_stream || stream_url.is_empty()) {
        return; // Never run acoustic shift detector for local music files
    }

    uint64_t current_token = ++g_rms_detector_token;

    async_io_manager::instance().submit_task([stream_url, current_token]() {
        PerceptualVector prev_vec;
        auto last_trigger_time = std::chrono::steady_clock::now();
        bool is_force_acr_stream = contains_case_insensitive(stream_url.c_str(), "forceacr");

        while (true) {
            // Stable 3-second background poll sleep
            std::this_thread::sleep_for(std::chrono::seconds(3));

            if (g_is_shutting_down.load() || current_token != g_rms_detector_token.load() || g_current_stream_url != stream_url || g_current_stream_url.is_empty()) {
                return; // Stream changed, stopped, or app exiting -> exit worker thread cleanly
            }

            auto now = std::chrono::steady_clock::now();

            // Lock out background scans while playing a recognized track (unless forceacr stream)
            if (!is_force_acr_stream && now < g_acrcloud_cooldown_until) {
                prev_vec = PerceptualVector(); // Reset previous vector while cooldown is active
                continue;
            }

            if (!g_vis_stream.is_valid()) {
                async_io_manager::instance().post_to_main_thread([]() {
                    get_persistent_vis_stream();
                });
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            PerceptualVector curr_vec;
            auto vec_promise = std::make_shared<std::promise<PerceptualVector>>();
            auto vec_future = vec_promise->get_future();

            async_io_manager::instance().post_to_main_thread([vec_promise]() {
                PerceptualVector vec;
                try {
                    auto stream = get_persistent_vis_stream();
                    if (stream.is_valid()) {
                        double abs_time = 0;
                        if (stream->get_absolute_time(abs_time) && abs_time >= 0.6) {
                            audio_chunk_impl chunk;
                            if (stream->get_chunk_absolute(chunk, abs_time - 0.5, 0.5)) {
                                vec = extract_5band_vector(chunk);
                            }
                        }
                    }
                } catch (...) {}
                try {
                    vec_promise->set_value(vec);
                } catch (...) {}
            });

            if (vec_future.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready) {
                try {
                    curr_vec = vec_future.get();
                } catch (...) {}
            }

            if (!curr_vec.valid || curr_vec.total_rms < 0.003) {
                continue; // Do not reset prev_vec on transient misses
            }

            auto time_since_last_trigger = std::chrono::duration_cast<std::chrono::seconds>(now - last_trigger_time).count();

            bool trigger_needed = false;
            const char* trigger_reason = nullptr;

            if (prev_vec.valid) {
                // Compute Log-Spectral Distance across 5 perceptual sub-bands in dB
                double d1 = curr_vec.db1 - prev_vec.db1;
                double d2 = curr_vec.db2 - prev_vec.db2;
                double d3 = curr_vec.db3 - prev_vec.db3;
                double d4 = curr_vec.db4 - prev_vec.db4;
                double d5 = curr_vec.db5 - prev_vec.db5;

                double log_distance = std::sqrt(d1*d1 + d2*d2 + d3*d3 + d4*d4 + d5*d5);

                // Stable trigger threshold: Log-Spectral Distance >= 7.5 dB, 45s hold-off
                if (log_distance >= 7.5 && time_since_last_trigger >= 45) {
                    trigger_needed = true;
                    trigger_reason = "Log-Spectral Acoustic Shift detected";
                }
            }

            // Fallback Safety Rescan: Force ACRCloud rescan every 90 seconds only for ?forceacr tagged streams
            bool is_force_acr_stream = contains_case_insensitive(stream_url.c_str(), "forceacr");
            if (!trigger_needed && is_force_acr_stream && time_since_last_trigger >= 90) {
                trigger_needed = true;
                trigger_reason = "90-second safety periodic rescan timer elapsed";
            }

            if (trigger_needed && trigger_reason != nullptr) {
                last_trigger_time = now;

                async_io_manager::instance().post_to_main_thread([stream_url, current_token, trigger_reason]() {
                    if (current_token == g_rms_detector_token.load() && g_current_stream_url == stream_url) {
                        foo_artwork::log_printf("foo_artwork: %s. Waiting 2s for new song to settle before sampling...", trigger_reason);

                        // Schedule 2-second post-transition settling delay on background thread
                        async_io_manager::instance().submit_task([stream_url, current_token]() {
                            std::this_thread::sleep_for(std::chrono::seconds(2));

                            async_io_manager::instance().post_to_main_thread([stream_url, current_token]() {
                                if (current_token == g_rms_detector_token.load() && g_current_stream_url == stream_url) {
                                    foo_artwork::log_printf("foo_artwork: Settling period complete. Initiating ACRCloud audio recognition...");
                                    g_acrcloud_cooldown_until = std::chrono::steady_clock::time_point{};
                                    refresh_all_dui_artwork_panels();
                                    refresh_all_cui_artwork_panels();
                                }
                            });
                        });
                    }
                });
            }

            prev_vec = curr_vec;
        }
    });
}

static pfc::string8 get_formatted_timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%d/%m/%Y %H:%M", &tm_buf);
    return time_str;
}

static void log_simplified_track_info(const char* artist, const char* title) {
    if (!artist || !title || strlen(artist) == 0 || strlen(title) == 0) return;
    if (MetadataCleaner::is_station_name_or_url(artist) || MetadataCleaner::is_station_name_or_url(title)) {
        return;
    }
    pfc::string8 key = artist;
    key += " - ";
    key += title;

    if (key == g_last_logged_track_info) {
        return; // Avoid logging duplicate consecutive lines for the same track
    }
    g_last_logged_track_info = key;

    pfc::string8 ts = get_formatted_timestamp();
    foo_artwork::log_track_info("foo_artwork: '%s - %s' %s", artist, title, ts.c_str());
}

void artwork_manager::on_stream_metadata_changed(const char* raw_artist, const char* raw_title, const char* raw_artist_full, const char* raw_album, const char* raw_listeners) {
    ASSERT_MAIN_THREAD();
    if (!raw_artist || !raw_title) return;

    static_api_ptr_t<playback_control> pc;
    if (!pc->is_playing() && !pc->is_paused()) return;

    StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(raw_artist, raw_title);
    if (!meta.is_valid_search || meta.is_station_or_url) return;

    pfc::string8 clean_art = meta.first_artist.c_str();
    pfc::string8 clean_tit = meta.clean_title.c_str();
    pfc::string8 clean_art_full = (raw_artist_full && raw_artist_full[0] != '\0') ? raw_artist_full : meta.clean_artist.c_str();
    pfc::string8 clean_album = raw_album ? raw_album : "";
    pfc::string8 clean_listeners = raw_listeners ? raw_listeners : "";

    if (clean_art == g_last_stream_artist && clean_tit == g_last_stream_title) {
        return; // Avoid duplicate searches for identical stream metadata when already in-flight or resolved
    }
    g_last_stream_artist = clean_art;
    g_last_stream_title = clean_tit;

    // Reset rejected providers and active provider for the new stream song
    g_rejected_providers_for_current_track.clear();
    g_active_resolved_provider.reset();

    // Increment monitor token to cancel any pending 10s initial metadata fallback monitor
    g_stream_monitor_token++;

    // Stop acoustic shift detector since valid song metadata is now available
    stop_rms_silence_detector();

    // If external stream API was auto-probed (not explicitly configured via ?azuracast_api or ?radioreg_api),
    // stop the background poller since valid native ICY stream metadata is actively arriving
    pfc::string8 az_param = get_url_param_value(g_current_stream_url.c_str(), "azuracast_api");
    pfc::string8 rr_param = get_url_param_value(g_current_stream_url.c_str(), "radioreg_api");
    if (az_param.is_empty() && rr_param.is_empty()) {
        stop_external_stream_api_poller();
    }

    // Reset ACRCloud cooldown on fresh dynamic track update
    reset_acrcloud_cooldown();

    uint64_t gen = ++g_search_generation;

    log_simplified_track_info(clean_art.c_str(), clean_tit.c_str());

    metadb_handle_ptr track;
    if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
        titleformat_provider::set_track_artwork_info(track, clean_art.c_str(), clean_tit.c_str(), "", "", clean_art_full.c_str(), clean_album.c_str(), clean_listeners.c_str());
    }

    pfc::string8 yt_video_id;
    if (track.is_valid()) {
        yt_video_id = extract_youtube_video_id(track->get_path());
    }
    if (yt_video_id.is_empty()) {
        yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
    }
    bool is_youtube = !yt_video_id.is_empty();

    pfc::string8 cache_key;
    if (is_youtube) {
        cache_key = pfc::string8("yt_") + yt_video_id;
    } else {
        cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key(clean_art, clean_tit);
    }

    pfc::string8 broadcast_art_url = extract_broadcast_artwork_url(track);
    bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
        (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());
    bool try_youtube_thumbnail = is_youtube && !yt_video_id.is_empty() &&
        (g_rejected_providers_for_current_track.find("YouTube Thumbnail") == g_rejected_providers_for_current_track.end());

    auto apply_success_result = [gen, clean_art, clean_tit, clean_art_full, clean_album, clean_listeners, cache_key](const artwork_result& res) {
        if (gen != g_search_generation.load() || clean_art != g_last_stream_artist || clean_tit != g_last_stream_title) {
            // A newer metadata cue or track has arrived - discard this stale result
            return;
        }
        if (!res.source.is_empty() && res.source != "Cache") {
            g_active_resolved_provider = res.source;
            g_active_source = res.source;
        } else if (!g_active_resolved_provider.is_empty()) {
            g_active_source = g_active_resolved_provider;
        } else {
            g_active_source = res.source;
        }
        pfc::string8 effective_source = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache") ? g_active_resolved_provider : res.source;
        if (cfg_enable_disk_cache || cfg_single_file_cache) {
            pfc::string8 key = cfg_single_file_cache ? pfc::string8("current") : cache_key;
            async_io_manager::instance().cache_set_async(key, res.data);
        }
        if (res.data.get_size() > 0) {
            std::vector<BYTE> vec(res.data.get_ptr(), res.data.get_ptr() + res.data.get_size());
            create_bitmap_from_image_data(vec);
        }

        metadb_handle_ptr now_track;
        if (playback_control::get()->get_now_playing(now_track) && now_track.is_valid()) {
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            titleformat_provider::set_track_artwork_info(now_track, clean_art.c_str(), clean_tit.c_str(), cache_file.c_str(), effective_source.c_str(), clean_art_full.c_str(), clean_album.c_str(), clean_listeners.c_str());
        }

        pfc::string8 status_msg = pfc::string8("Artwork loaded from ") + effective_source;
        titleformat_provider::set_status(status_msg.c_str());

        refresh_all_dui_artwork_panels();
        refresh_all_cui_artwork_panels();
    };

    if (try_broadcast_artwork) {
        foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
    } else if (g_rejected_providers_for_current_track.find("Broadcast Artwork") != g_rejected_providers_for_current_track.end()) {
        foo_artwork::log_printf("foo_artwork: Skipping rejected provider 'Broadcast Artwork' for current track.");
    }

    if (cfg_single_file_cache) {
        if (try_broadcast_artwork) {
            search_broadcast_artwork_async(broadcast_art_url, cache_key, [clean_art, clean_tit, cache_key, apply_success_result, try_youtube_thumbnail, yt_video_id](const artwork_result& res) {
                if (res.success && res.data.get_size() > 0) {
                    apply_success_result(res);
                } else if (try_youtube_thumbnail) {
                    search_youtube_thumbnail_async(yt_video_id, cache_key, [clean_art, clean_tit, cache_key, apply_success_result](const artwork_result& yt_res) {
                        if (yt_res.success && yt_res.data.get_size() > 0) {
                            apply_success_result(yt_res);
                        } else {
                            search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& api_res) {
                                if (api_res.success && api_res.data.get_size() > 0) {
                                    apply_success_result(api_res);
                                }
                            });
                        }
                    });
                } else {
                    foo_artwork::log_printf("foo_artwork: Broadcast artwork download failed. Falling back to online APIs...");
                    search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& api_res) {
                        if (api_res.success && api_res.data.get_size() > 0) {
                            apply_success_result(api_res);
                        }
                    });
                }
            });
        } else if (try_youtube_thumbnail) {
            search_youtube_thumbnail_async(yt_video_id, cache_key, [clean_art, clean_tit, cache_key, apply_success_result](const artwork_result& yt_res) {
                if (yt_res.success && yt_res.data.get_size() > 0) {
                    apply_success_result(yt_res);
                } else {
                    search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& api_res) {
                        if (api_res.success && api_res.data.get_size() > 0) {
                            apply_success_result(api_res);
                        }
                    });
                }
            });
        } else {
            search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& res) {
                if (res.success && res.data.get_size() > 0) {
                    apply_success_result(res);
                }
            });
        }
    } else {
        // Multi-file cache mode: check disk cache first for this specific song
        async_io_manager::instance().cache_get_async(cache_key, [broadcast_art_url, try_broadcast_artwork, try_youtube_thumbnail, yt_video_id, clean_art, clean_tit, cache_key, apply_success_result](bool cache_hit, const pfc::array_t<t_uint8>& data, const pfc::string8& err) {
            if (cache_hit && data.get_size() > 0) {
                pfc::string8 effective_source = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache") ? g_active_resolved_provider : pfc::string8("Cache");
                if (effective_source == "Cache") {
                    foo_artwork::log_printf("foo_artwork: SUCCESS - Cached artwork displayed for '%s - %s'", clean_art.c_str(), clean_tit.c_str());
                }
                artwork_result cache_res;
                cache_res.success = true;
                cache_res.data = data;
                cache_res.source = effective_source;
                cache_res.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                apply_success_result(cache_res);
            } else if (try_broadcast_artwork) {
                search_broadcast_artwork_async(broadcast_art_url, cache_key, [clean_art, clean_tit, cache_key, apply_success_result, try_youtube_thumbnail, yt_video_id](const artwork_result& res) {
                    if (res.success && res.data.get_size() > 0) {
                        apply_success_result(res);
                    } else if (try_youtube_thumbnail) {
                        search_youtube_thumbnail_async(yt_video_id, cache_key, [clean_art, clean_tit, cache_key, apply_success_result](const artwork_result& yt_res) {
                            if (yt_res.success && yt_res.data.get_size() > 0) {
                                apply_success_result(yt_res);
                            } else {
                                search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& api_res) {
                                    if (api_res.success && api_res.data.get_size() > 0) {
                                        apply_success_result(api_res);
                                    }
                                });
                            }
                        });
                    } else {
                        foo_artwork::log_printf("foo_artwork: Broadcast artwork download failed. Falling back to online APIs...");
                        search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& api_res) {
                            if (api_res.success && api_res.data.get_size() > 0) {
                                apply_success_result(api_res);
                            }
                        });
                    }
                });
            } else if (try_youtube_thumbnail) {
                search_youtube_thumbnail_async(yt_video_id, cache_key, [clean_art, clean_tit, cache_key, apply_success_result](const artwork_result& yt_res) {
                    if (yt_res.success && yt_res.data.get_size() > 0) {
                        apply_success_result(yt_res);
                    } else {
                        search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& api_res) {
                            if (api_res.success && api_res.data.get_size() > 0) {
                                apply_success_result(api_res);
                            }
                        });
                    }
                });
            } else {
                search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& res) {
                    if (res.success && res.data.get_size() > 0) {
                        apply_success_result(res);
                    }
                });
            }
        });
    }
}

void artwork_manager::start_initial_stream_metadata_monitor(const pfc::string8& stream_url) {
    uint64_t current_token = ++g_stream_monitor_token;
    auto last_artist = std::make_shared<pfc::string8>("");
    auto last_title = std::make_shared<pfc::string8>("");
    auto valid_meta_found = std::make_shared<std::atomic<bool>>(false);

    async_io_manager::instance().submit_task([stream_url, current_token, last_artist, last_title, valid_meta_found]() {
        // Poll every 500ms for up to 10 seconds (20 iterations) during initial stream connection
        for (int iteration = 0; iteration < 20; ++iteration) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            if (g_is_shutting_down.load() || valid_meta_found->load() || current_token != g_stream_monitor_token.load() || g_current_stream_url != stream_url || g_current_stream_url.is_empty()) {
                return; // Stream changed, stopped, valid meta found, or app exiting
            }

            async_io_manager::instance().post_to_main_thread([stream_url, current_token, last_artist, last_title, valid_meta_found]() {
                if (g_is_shutting_down.load() || valid_meta_found->load() || current_token != g_stream_monitor_token.load() || g_current_stream_url != stream_url || g_current_stream_url.is_empty()) {
                    return;
                }

                static_api_ptr_t<playback_control> pc;
                if (!pc->is_playing() && !pc->is_paused()) return;

                metadb_handle_ptr track;
                if (pc->get_now_playing(track) && track.is_valid() && track->get_path() == stream_url) {
                    pfc::string8 artist, title;
                    service_ptr_t<titleformat_object> script_art, script_tit;
                    static_api_ptr_t<titleformat_compiler>()->compile_safe(script_art, "%artist%");
                    static_api_ptr_t<titleformat_compiler>()->compile_safe(script_tit, "%title%");
                    pc->playback_format_title(nullptr, artist, script_art, nullptr, playback_control::display_level_titles);
                    pc->playback_format_title(nullptr, title, script_tit, nullptr, playback_control::display_level_titles);

                    if (artist.is_empty() && !title.is_empty()) {
                        std::string t_str = title.c_str();
                        std::string delimiters[] = { " - ", " ˗ ", " / ", " by " };
                        for (const auto& delim : delimiters) {
                            size_t pos = t_str.find(delim);
                            if (pos != std::string::npos) {
                                artist = t_str.substr(0, pos).c_str();
                                title = t_str.substr(pos + delim.length()).c_str();
                                break;
                            }
                        }
                    }

                    if (!artist.is_empty() && !title.is_empty()) {
                        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), title.c_str());
                        if (meta.is_valid_search && !meta.is_station_or_url) {
                            valid_meta_found->store(true);
                            on_stream_metadata_changed(artist.c_str(), title.c_str());
                        }
                    }
                }
            });
        }

        // After 10 seconds of polling (20 iterations), check if no valid song metadata was detected
        if (!g_is_shutting_down.load() && !valid_meta_found->load() && current_token == g_stream_monitor_token.load() && g_current_stream_url == stream_url) {
            async_io_manager::instance().post_to_main_thread([stream_url, current_token, valid_meta_found]() {
                if (!g_is_shutting_down.load() && !valid_meta_found->load() && current_token == g_stream_monitor_token.load() && g_current_stream_url == stream_url) {
                    if (cfg_enable_acrcloud) {
                        foo_artwork::log_printf("foo_artwork: Initial 10s stream metadata monitor completed without song metadata update. Enabling Log-Spectral Acoustic Shift detection...");
                        start_rms_silence_detector(stream_url);
                        if (!cfg_acrcloud_host.is_empty() && !cfg_acrcloud_access_key.is_empty() && !cfg_acrcloud_access_secret.is_empty()) {
                            auto now = std::chrono::steady_clock::now();
                            if (now >= g_acrcloud_cooldown_until) {
                                foo_artwork::log_printf("foo_artwork: Triggering ACRCloud audio recognition fallback for stream without song metadata...");
                                pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : pfc::string8("stream_fallback");
                                search_acrcloud_fallback_async(cache_key, [](const artwork_result& res) {
                                    if (res.success && res.data.get_size() > 0) {
                                        refresh_all_dui_artwork_panels();
                                        refresh_all_cui_artwork_panels();
                                    }
                                });
                            }
                        }
                    }
                }
            });
        }
    });
}

void artwork_manager::cancel_acrcloud_tasks() {
    g_acrcloud_task_id++;
    g_stream_monitor_token++;
    stop_rms_silence_detector();
}

void artwork_manager::reset_acrcloud_cooldown() {
    g_acrcloud_cooldown_until = std::chrono::steady_clock::time_point{};
    g_vis_stream.release();
    cancel_acrcloud_tasks();
    g_last_recognized_result = artwork_result();
    g_last_recognized_stream_url.reset();
    g_last_recognized_artist.reset();
    g_last_recognized_title.reset();
}

void artwork_manager::force_acrcloud_lookup() {
    ASSERT_MAIN_THREAD();

    metadb_handle_ptr track;
    if (!playback_control::get()->get_now_playing(track) || !track.is_valid()) {
        foo_artwork::log_printf("foo_artwork: Manual trigger failed: No track currently playing.");
        return;
    }

    foo_artwork::log_printf("foo_artwork: Manual trigger: Forcing ACRCloud audio recognition lookup on demand...");

    // Release old visualization stream to capture fresh live audio
    g_vis_stream.release();
    g_acrcloud_cooldown_until = std::chrono::steady_clock::time_point{};

    metadb_info_container::ptr info_container = track->get_info_ref();
    const file_info* info = &info_container->info();
    pfc::string8 artist = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "Unknown Artist";
    pfc::string8 track_name = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "Unknown Track";
    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key(artist, track_name);
    pfc::string8 current_url = track->get_path();

    search_acrcloud_fallback_async(cache_key, [cache_key, current_url](const artwork_result& result) {
        if (result.success && result.data.get_size() > 0) {
            foo_artwork::log_printf("foo_artwork: Manual ACRCloud lookup SUCCESS - Artwork retrieved (%u bytes)", (unsigned int)result.data.get_size());

            g_last_recognized_result = result;
            g_last_recognized_stream_url = current_url;

            // Always write retrieved artwork to cache so panels can display it
            if (cfg_enable_disk_cache || cfg_single_file_cache) {
                pfc::string8 key_to_use = cfg_single_file_cache ? pfc::string8("current") : cache_key;
                async_io_manager::instance().cache_set_async(key_to_use, result.data);
            }

            // Post UI refresh onto main thread to update all active panels
            async_io_manager::instance().post_to_main_thread([]() {
                refresh_all_dui_artwork_panels();
                refresh_all_cui_artwork_panels();
            });
        } else {
            foo_artwork::log_printf("foo_artwork: Manual ACRCloud lookup FAILED: %s", result.error_message.c_str());
        }
    }, true /* is_manual_trigger */);
}

static void schedule_periodic_acrcloud_rescan(uint32_t delay_ms) {
    pfc::string8 current_url = g_current_stream_url;
    uint64_t current_task_id = g_acrcloud_task_id.load();

    async_io_manager::instance().submit_task([current_url, current_task_id, delay_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms + 1000));

        async_io_manager::instance().post_to_main_thread([current_url, current_task_id]() {
            if (current_task_id == g_acrcloud_task_id.load() && g_current_stream_url == current_url) {
                foo_artwork::log_printf("foo_artwork: Stream circuit-breaker expired. Triggering automatic periodic ACRCloud recognition...");
                g_last_recognized_result = artwork_manager::artwork_result();
                g_acrcloud_cooldown_until = std::chrono::steady_clock::time_point{};
                g_vis_stream.release();
                refresh_all_dui_artwork_panels();
                refresh_all_cui_artwork_panels();
            }
        });
    });
}

static void extract_track_metadata_dynamic(metadb_handle_ptr track, pfc::string8& out_artist, pfc::string8& out_title) {
    out_artist.reset();
    out_title.reset();

    if (!track.is_valid()) return;

    pfc::string8 file_path = track->get_path();
    bool is_youtube = !artwork_manager::extract_youtube_video_id(file_path.c_str()).is_empty();
    bool is_internet_stream = is_youtube ||
                              (strstr(file_path.c_str(), "://") && !(strstr(file_path.c_str(), "file://") == file_path.c_str()));

    static_api_ptr_t<playback_control> pc;
    metadb_handle_ptr now_playing;
    if (pc->get_now_playing(now_playing) && now_playing == track) {
        static_api_ptr_t<titleformat_compiler> compiler;
        service_ptr_t<titleformat_object> script_artist, script_title;
        compiler->compile_force(script_artist, "%artist%");
        compiler->compile_force(script_title, "%title%");

        pfc::string8 dyn_art, dyn_tit;
        if (pc->playback_format_title(nullptr, dyn_art, script_artist, nullptr, playback_control::display_level_titles)) {
            if (!dyn_art.is_empty() && dyn_art != "?") out_artist = dyn_art;
        }
        if (pc->playback_format_title(nullptr, dyn_tit, script_title, nullptr, playback_control::display_level_titles)) {
            if (!dyn_tit.is_empty() && dyn_tit != "?") out_title = dyn_tit;
        }
    }

    if (out_artist.is_empty() || out_title.is_empty()) {
        try {
            metadb_info_container::ptr info_container = track->get_info_ref();
            if (info_container.is_valid()) {
                const file_info& info = info_container->info();
                if (out_artist.is_empty() && info.meta_get("ARTIST", 0)) out_artist = info.meta_get("ARTIST", 0);
                if (out_title.is_empty() && info.meta_get("TITLE", 0)) out_title = info.meta_get("TITLE", 0);
            }
        } catch (...) {}
    }

    if (is_internet_stream && (out_artist.is_empty() || out_artist == "Unknown Artist" || out_title.is_empty() || out_title == "Unknown Track")) {
        if (!g_last_stream_artist.is_empty() && !g_last_stream_title.is_empty()) {
            out_artist = g_last_stream_artist;
            out_title = g_last_stream_title;
        }
    }

    if (is_internet_stream) {
        if (artwork_manager::has_url_flag(file_path.c_str(), "inverted") ||
            artwork_manager::has_url_flag(g_current_stream_url.c_str(), "inverted")) {
            std::swap(out_artist, out_title);
        }
    }

    if (out_artist.is_empty()) out_artist = "Unknown Artist";
    if (out_title.is_empty()) out_title = "Unknown Track";
}

void artwork_manager::search_artwork_pipeline(metadb_handle_ptr track, artwork_callback callback) {
    ASSERT_MAIN_THREAD();
    
    if (!track.is_valid()) {
        artwork_result fail;
        fail.success = false;
        callback(fail);
        return;
    }

    pfc::string8 file_path = track->get_path();

    // URL Flag: ?bypass - Completely skip artwork lookup
    if (has_url_flag(file_path.c_str(), "bypass") || has_url_flag(g_current_stream_url.c_str(), "bypass")) {
        foo_artwork::log_printf("foo_artwork: Stream URL contains '?bypass' tag. Skipping artwork lookup.");
        artwork_result res;
        res.success = false;
        res.source = "Bypassed";
        res.error_message = "Bypassed by stream URL flag (?bypass)";
        callback(res);
        return;
    }

    bool is_youtube = !extract_youtube_video_id(file_path.c_str()).is_empty() || 
                      !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty();
    bool is_internet_stream = is_youtube ||
                              (strstr(file_path.c_str(), "://") && !(strstr(file_path.c_str(), "file://") == file_path.c_str()));

    pfc::string8 artist, track_name;
    extract_track_metadata_dynamic(track, artist, track_name);

    if (!is_internet_stream) {
        log_simplified_track_info(artist.c_str(), track_name.c_str());
    } else {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track_name.c_str());
        if (meta.is_valid_search && !meta.is_station_or_url) {
            log_simplified_track_info(meta.first_artist.c_str(), meta.clean_title.c_str());
        }
    }
    
    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key_for_track(track);

    bool is_different_track = false;
    if (!g_active_playing_track.is_valid() || !track.is_valid()) {
        is_different_track = true;
    } else if (strcmp(g_active_playing_track->get_path(), track->get_path()) != 0) {
        is_different_track = true;
    }

    if (is_different_track) {
        g_active_playing_track = track;
        g_rejected_providers_for_current_track.clear();
        g_active_resolved_provider.reset();
    }
    g_active_cache_key = cache_key;

    uint64_t gen = ++g_search_generation;
    auto original_callback = callback;
    auto wrapped_callback = [gen, track, artist, track_name, cache_key, original_callback](const artwork_result& res) {
        if (gen != g_search_generation.load()) {
            original_callback(res);
            return;
        }
        if (res.success) {
            if (!res.source.is_empty() && res.source != "Cache") {
                g_active_resolved_provider = res.source;
                g_active_source = res.source;
            } else if (!g_active_resolved_provider.is_empty()) {
                g_active_source = g_active_resolved_provider;
            } else {
                g_active_source = res.source;
            }
            pfc::string8 effective_source = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache") ? g_active_resolved_provider : res.source;
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            titleformat_provider::set_track_artwork_info(track, artist.c_str(), track_name.c_str(), cache_file.c_str(), effective_source.c_str());
            titleformat_provider::set_status((pfc::string8("Artwork loaded from ") + effective_source).c_str());
            artwork_result final_res = res;
            final_res.source = effective_source;
            original_callback(final_res);
            return;
        }
        titleformat_provider::set_status("No artwork found");
        original_callback(res);
    };
    callback = wrapped_callback;

    if (is_internet_stream) {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track_name.c_str());
        bool force_acrcloud = has_url_flag(file_path.c_str(), "forceacr") || has_url_flag(g_current_stream_url.c_str(), "forceacr");
        bool is_reject_station_covers = has_url_flag(file_path.c_str(), "rejectstationcovers") || has_url_flag(g_current_stream_url.c_str(), "rejectstationcovers");

        bool new_stream_connect = (g_current_stream_url != file_path);
        if (new_stream_connect) {
            g_current_stream_url = file_path;
            g_last_stream_artist = "";
            g_last_stream_title = "";
            g_last_logged_track_info = ""; // Reset logged track info history for fresh stream
            reset_acrcloud_cooldown(); // Reset cooldown on new radio stream connection
            stop_rms_silence_detector(); // Disabled by default until Stage 3 ends with no artwork
            if (force_acrcloud) {
                start_rms_silence_detector(file_path); // Active immediately for ?forceacr streams
            }
            if (meta.is_station_or_url || !meta.is_valid_search) {
                start_initial_stream_metadata_monitor(file_path);
            }
            start_external_stream_api_poller(file_path);
        }

        // RECOGNIZED STREAM ARTWORK GUARD:
        // If ACRCloud (or manual trigger) recently recognized this stream and cached the result,
        // and we are within the track length circuit-breaker cooldown, return the recognized artwork!
        auto now = std::chrono::steady_clock::now();
        if (g_last_recognized_stream_url == file_path && g_last_recognized_result.success && now < g_acrcloud_cooldown_until) {
            callback(g_last_recognized_result);
            return;
        }

        // Check in-stream broadcast artwork
        pfc::string8 broadcast_art_url = extract_broadcast_artwork_url(track);
        bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
            (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());

        if (try_broadcast_artwork) {
            foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
        } else if (g_rejected_providers_for_current_track.find("Broadcast Artwork") != g_rejected_providers_for_current_track.end()) {
            foo_artwork::log_printf("foo_artwork: Skipping rejected provider 'Broadcast Artwork' for current track.");
        }

        // UNTAGGED STREAMS, PLACEHOLDER METADATA, STATION DOMAINS/SLOGANS OR ?FORCEACR TAG:
        // On untagged streams, station names/URLs, or ?forceacr streams, do NOT query online commercial APIs.
        if (force_acrcloud) {
            if (cfg_enable_acrcloud && !cfg_acrcloud_host.is_empty() && !cfg_acrcloud_access_key.is_empty() && !cfg_acrcloud_access_secret.is_empty()) {
                foo_artwork::log_printf("foo_artwork: Stream URL contains 'forceacr' tag. Bypassing text search to ACRCloud fallback.");
                search_acrcloud_fallback_async(cache_key, callback);
            } else {
                artwork_result fail_res;
                fail_res.success = false;
                fail_res.error_message = "Stream URL has 'forceacr' tag but ACRCloud is disabled or unconfigured";
                callback(fail_res);
            }
            return;
        }

        if (meta.is_station_or_url || !meta.is_valid_search) {
            if (cfg_skip_local_artwork || is_youtube || is_reject_station_covers) {
                if (try_broadcast_artwork) {
                    search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track_name, callback](const artwork_result& res) {
                        if (res.success) {
                            cancel_acrcloud_tasks();
                            callback(res);
                        } else {
                            foo_artwork::log_printf("foo_artwork: Metadata '%s - %s' flagged as station/URL or invalid. Skipping text search (allowing 10s stream monitor for metadata updates).",
                                                   artist.c_str(), track_name.c_str());
                            artwork_result fail_res;
                            fail_res.success = false;
                            fail_res.error_message = "Metadata is station URL or invalid for text search";
                            callback(fail_res);
                        }
                    });
                } else {
                    foo_artwork::log_printf("foo_artwork: Metadata '%s - %s' flagged as station/URL or invalid. Skipping text search (allowing 10s stream monitor for metadata updates).",
                                           artist.c_str(), track_name.c_str());
                    artwork_result fail_res;
                    fail_res.success = false;
                    fail_res.error_message = "Metadata is station URL or invalid for text search";
                    callback(fail_res);
                }
            } else {
                find_local_artwork_async(track, [artist, track_name, cache_key, callback, try_broadcast_artwork, broadcast_art_url](const artwork_result& result) {
                    if (result.success) {
                        cancel_acrcloud_tasks(); // Cancel pending 10s initial stream monitor & acoustic shift detector on station logo / local artwork hit!
                        callback(result);
                    } else if (try_broadcast_artwork) {
                        search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track_name, callback](const artwork_result& res) {
                            if (res.success) {
                                cancel_acrcloud_tasks();
                                callback(res);
                            } else {
                                foo_artwork::log_printf("foo_artwork: Metadata '%s - %s' flagged as station/URL or invalid. Skipping text search (allowing 10s stream monitor for metadata updates).",
                                                       artist.c_str(), track_name.c_str());
                                artwork_result fail_res;
                                fail_res.success = false;
                                fail_res.error_message = "Metadata is station URL or invalid for text search";
                                callback(fail_res);
                            }
                        });
                    } else {
                        foo_artwork::log_printf("foo_artwork: Metadata '%s - %s' flagged as station/URL or invalid. Skipping text search (allowing 10s stream monitor for metadata updates).",
                                               artist.c_str(), track_name.c_str());
                        artwork_result fail_res;
                        fail_res.success = false;
                        fail_res.error_message = "Metadata is station URL or invalid for text search";
                        callback(fail_res);
                    }
                });
            }
            return;
        }

        // VALID STREAM METADATA (e.g., "The Beatles - Let It Be"):
        // In single-file cache mode, skip cache reads (key is always "current" so it would
        // return the previous track's artwork). Go directly to broadcast -> API search, still write to cache.
        if (cfg_single_file_cache) {
            pfc::string8 yt_video_id;
            if (is_youtube) {
                yt_video_id = extract_youtube_video_id(track->get_path());
                if (yt_video_id.is_empty()) {
                    yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
                }
            }
            bool try_youtube_thumbnail = is_youtube && !yt_video_id.is_empty() &&
                (g_rejected_providers_for_current_track.find("YouTube Thumbnail") == g_rejected_providers_for_current_track.end());

            if (try_broadcast_artwork) {
                search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track_name, cache_key, callback, try_youtube_thumbnail, yt_video_id](const artwork_result& res) {
                    if (res.success) {
                        callback(res);
                    } else if (try_youtube_thumbnail) {
                        search_youtube_thumbnail_async(yt_video_id, cache_key, [artist, track_name, cache_key, callback](const artwork_result& yt_res) {
                            if (yt_res.success) {
                                callback(yt_res);
                            } else {
                                search_apis_async(artist, track_name, cache_key, callback);
                            }
                        });
                    } else {
                        search_apis_async(artist, track_name, cache_key, callback);
                    }
                });
            } else if (try_youtube_thumbnail) {
                search_youtube_thumbnail_async(yt_video_id, cache_key, [artist, track_name, cache_key, callback](const artwork_result& yt_res) {
                    if (yt_res.success) {
                        callback(yt_res);
                    } else {
                        search_apis_async(artist, track_name, cache_key, callback);
                    }
                });
            } else {
                search_apis_async(artist, track_name, cache_key, callback);
            }
            return;
        }

        // VALID STREAM METADATA (e.g., "The Beatles - Let It Be"):
        // Check disk cache first for this specific song
        check_cache_async(cache_key, track, [artist, track_name, cache_key, track, callback, is_youtube, is_reject_station_covers, try_broadcast_artwork, broadcast_art_url](const artwork_result& cache_res) {
            if (cache_res.success) {
                foo_artwork::log_printf("foo_artwork: SUCCESS - Cached artwork displayed for initial stream metadata '%s - %s'", artist.c_str(), track_name.c_str());
                foo_artwork::log_printf("foo_artwork: Initial 10s stream metadata monitor cancelled (cached artwork loaded).");
                cancel_acrcloud_tasks(); // Cancel pending 10s initial stream monitor & acoustic shift detector on cache hit!
                callback(cache_res);
            } else {
                pfc::string8 yt_video_id;
                if (is_youtube) {
                    yt_video_id = extract_youtube_video_id(track->get_path());
                    if (yt_video_id.is_empty()) {
                        yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
                    }
                }
                bool try_youtube_thumbnail = is_youtube && !yt_video_id.is_empty() &&
                    (g_rejected_providers_for_current_track.find("YouTube Thumbnail") == g_rejected_providers_for_current_track.end());

                if (try_broadcast_artwork) {
                    search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track_name, cache_key, track, callback, is_youtube, is_reject_station_covers, try_youtube_thumbnail, yt_video_id](const artwork_result& bcast_res) {
                        if (bcast_res.success) {
                            cancel_acrcloud_tasks();
                            callback(bcast_res);
                        } else if (try_youtube_thumbnail) {
                            search_youtube_thumbnail_async(yt_video_id, cache_key, [artist, track_name, cache_key, callback](const artwork_result& yt_res) {
                                if (yt_res.success) {
                                    cancel_acrcloud_tasks();
                                    callback(yt_res);
                                } else {
                                    search_apis_async(artist, track_name, cache_key, callback);
                                }
                            });
                        } else if (cfg_skip_local_artwork || is_youtube || is_reject_station_covers) {
                            search_apis_async(artist, track_name, cache_key, callback);
                        } else {
                            find_local_artwork_async(track, [artist, track_name, cache_key, callback](const artwork_result& result) {
                                if (result.success) {
                                    cancel_acrcloud_tasks();
                                    if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                                        async_io_manager::instance().cache_set_async(cache_key, result.data);
                                    }
                                    callback(result);
                                } else {
                                    search_apis_async(artist, track_name, cache_key, callback);
                                }
                            });
                        }
                    });
                } else if (try_youtube_thumbnail) {
                    search_youtube_thumbnail_async(yt_video_id, cache_key, [artist, track_name, cache_key, callback](const artwork_result& yt_res) {
                        if (yt_res.success) {
                            cancel_acrcloud_tasks();
                            callback(yt_res);
                        } else {
                            search_apis_async(artist, track_name, cache_key, callback);
                        }
                    });
                } else if (cfg_skip_local_artwork || is_youtube || is_reject_station_covers) {
                    search_apis_async(artist, track_name, cache_key, callback);
                } else {
                    find_local_artwork_async(track, [artist, track_name, cache_key, callback](const artwork_result& result) {
                        if (result.success) {
                            cancel_acrcloud_tasks(); // Cancel pending 10s initial stream monitor on local artwork hit!
                            if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                                async_io_manager::instance().cache_set_async(cache_key, result.data);
                            }
                            callback(result);
                        } else {
                            search_apis_async(artist, track_name, cache_key, callback);
                        }
                    });
                }
            }
        });
        return;
    } else {
        // Local file playback: completely stop acoustic shift detector & clear stream URL & external API poller
        stop_rms_silence_detector();
        stop_external_stream_api_poller();
        g_current_stream_url = "";
        cancel_acrcloud_tasks();

        if (cfg_single_file_cache) {
            // In single-file cache mode, skip cache reads (key is always "current" so it would
            // return the previous track's artwork). Go directly to local -> APIs, still write to cache.
            search_local_async(file_path, cache_key, track, callback);
        } else {
            // For local files, check disk cache first with local artwork invalidation verification
            check_cache_async(cache_key, track, callback);
        }
    }
}

void artwork_manager::check_cache_async(const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback) {
    async_io_manager::instance().cache_get_async(cache_key, 
        [cache_key, track, callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0) {
                bool is_already_resolved = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache");
                if (!is_already_resolved) {
                    if (track.is_valid()) {
                        metadb_info_container::ptr info_container = track->get_info_ref();
                        const file_info* info = &info_container->info();
                        pfc::string8 art = info->meta_get("ARTIST", 0) ? info->meta_get("ARTIST", 0) : "";
                        pfc::string8 tit = info->meta_get("TITLE", 0) ? info->meta_get("TITLE", 0) : "";
                        if (!art.is_empty() && !tit.is_empty()) {
                            StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
                            if (meta.is_valid_search && !meta.is_station_or_url) {
                                log_simplified_track_info(meta.first_artist.c_str(), meta.clean_title.c_str());
                            } else {
                                log_simplified_track_info(art.c_str(), tit.c_str());
                            }
                        }
                    }
                }

                pfc::string8 file_path = track.is_valid() ? track->get_path() : "";
                bool is_stream = strstr(file_path.c_str(), "://") && !(strstr(file_path.c_str(), "file://") == file_path.c_str());

                // CACHE PRIORITY & INVALIDATION CHECK:
                // For local tracks, check if local artwork (embedded or in folder) was added or updated after cache was generated
                if (track.is_valid() && !is_stream && !cfg_skip_local_artwork) {
                    async_io_manager::instance().submit_task([cache_key, track, file_path, data, callback, is_already_resolved]() {
                        bool local_art_newer = is_local_artwork_newer_than_cache(file_path, cache_key);
                        if (local_art_newer) {
                            find_local_artwork_async(track, [cache_key, track, data, callback](const artwork_result& local_result) {
                                if (local_result.success && local_result.data.get_size() > 0) {
                                    foo_artwork::log_printf("foo_artwork: Cache invalidation - Local artwork was added or updated after cache generation. Updating cache.");
                                    if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                                        async_io_manager::instance().cache_set_async(cache_key, local_result.data);
                                    }
                                    if (cfg_single_file_cache) {
                                        async_io_manager::instance().cache_set_async("current", local_result.data);
                                    }
                                    callback(local_result);
                                } else {
                                    validate_and_complete_result(data, callback);
                                }
                            });
                        } else {
                            async_io_manager::instance().post_to_main_thread([data, callback, is_already_resolved]() {
                                if (!is_already_resolved) {
                                    foo_artwork::log_printf("foo_artwork: SUCCESS - Artwork displayed from disk cache");
                                }
                                validate_and_complete_result(data, callback);
                            });
                        }
                    });
                } else {
                    if (!is_already_resolved) {
                        foo_artwork::log_printf("foo_artwork: SUCCESS - Artwork displayed from disk cache");
                    }
                    // Cache hit - validate and return
                    validate_and_complete_result(data, callback);
                }
            } else {
                // Cache miss - continue to local search
                pfc::string8 file_path = track->get_path();
                search_local_async(file_path, cache_key, track, callback);
            }
        });
}

void artwork_manager::check_cache_async_metadata(const pfc::string8& cache_key, const pfc::string8& artist, const pfc::string8& track, artwork_callback callback) {
    // Check cache first, then fall back to Broadcast Artwork / API search on miss
    async_io_manager::instance().cache_get_async(cache_key,
        [cache_key, artist, track, callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0) {
                bool is_already_resolved = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache");
                if (!is_already_resolved) {
                    if (!artist.is_empty() && !track.is_empty()) {
                        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track.c_str());
                        if (meta.is_valid_search && !meta.is_station_or_url) {
                            log_simplified_track_info(meta.first_artist.c_str(), meta.clean_title.c_str());
                        } else {
                            log_simplified_track_info(artist.c_str(), track.c_str());
                        }
                    }
                    foo_artwork::log_printf("foo_artwork: SUCCESS - Artwork displayed from disk cache");
                }
                // Cache hit - validate and return
                validate_and_complete_result(data, callback);
            } else {
                // Cache miss - check broadcast artwork and YouTube thumbnail before online APIs
                pfc::string8 yt_video_id;
                if (!g_current_stream_url.is_empty()) {
                    yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
                }
                if (yt_video_id.is_empty() && g_active_playing_track.is_valid()) {
                    yt_video_id = extract_youtube_video_id(g_active_playing_track->get_path());
                }
                bool is_youtube = !yt_video_id.is_empty();

                pfc::string8 broadcast_art_url = extract_broadcast_artwork_url();
                bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
                    (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());
                bool try_youtube_thumbnail = is_youtube && !yt_video_id.is_empty() &&
                    (g_rejected_providers_for_current_track.find("YouTube Thumbnail") == g_rejected_providers_for_current_track.end());

                if (try_broadcast_artwork) {
                    foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
                    search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track, cache_key, callback, try_youtube_thumbnail, yt_video_id](const artwork_result& res) {
                        if (res.success) {
                            callback(res);
                        } else if (try_youtube_thumbnail) {
                            search_youtube_thumbnail_async(yt_video_id, cache_key, [artist, track, cache_key, callback](const artwork_result& yt_res) {
                                if (yt_res.success) {
                                    callback(yt_res);
                                } else {
                                    search_apis_async_metadata(artist, track, cache_key, callback);
                                }
                            });
                        } else {
                            foo_artwork::log_printf("foo_artwork: Broadcast artwork download failed. Falling back to online APIs...");
                            search_apis_async_metadata(artist, track, cache_key, callback);
                        }
                    });
                } else if (try_youtube_thumbnail) {
                    search_youtube_thumbnail_async(yt_video_id, cache_key, [artist, track, cache_key, callback](const artwork_result& yt_res) {
                        if (yt_res.success) {
                            callback(yt_res);
                        } else {
                            search_apis_async_metadata(artist, track, cache_key, callback);
                        }
                    });
                } else {
                    search_apis_async_metadata(artist, track, cache_key, callback);
                }
            }
        });
}

void artwork_manager::search_apis_async_metadata(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback) {
    // Use the existing search_apis_async function
    search_apis_async(artist, track, cache_key, callback);
}

void artwork_manager::search_local_async(const pfc::string8& file_path, const pfc::string8& cache_key, metadb_handle_ptr track, artwork_callback callback) {

    bool is_youtube = !extract_youtube_video_id(file_path.c_str()).is_empty() || 
                      !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty();

    pfc::string8 artist, track_name;
    extract_track_metadata_dynamic(track, artist, track_name);

    // If user wants to skip local artwork or if this is a YouTube stream, go directly to API search
    if (cfg_skip_local_artwork || is_youtube) {
        search_apis_async(artist, track_name, cache_key, callback);
        return;
    }

    // ALWAYS try to find tagged artwork first for local audio files
    find_local_artwork_async(track, [artist, track_name, cache_key, track, callback](const artwork_result& result) {
        if (result.success) {
            // Local artwork found - in single-file cache mode, write to current.cache
            // so external consumers (e.g., JScript Panel 3 Thumbs) see the correct artwork.
            // In normal cache mode, write to disk cache for fast offline retrieval.
            if (cfg_single_file_cache) {
                async_io_manager::instance().cache_set_async("current", result.data);
            }
            if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
            }
            callback(result);
        } else {
            // Local search failed - continue to API search
            search_apis_async(artist, track_name, cache_key, callback);
        }
    });
}

void artwork_manager::search_apis_async(const pfc::string8& raw_artist, const pfc::string8& raw_track, const pfc::string8& cache_key, artwork_callback callback) {
    StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(raw_artist.c_str(), raw_track.c_str());

    bool force_acrcloud = contains_case_insensitive(g_current_stream_url.c_str(), "forceacr");

    // Direct ACRCloud Tier 4 fallback if URL explicitly contains 'forceacr' tag
    if (force_acrcloud) {
        if (cfg_enable_acrcloud && !cfg_acrcloud_host.is_empty() && !cfg_acrcloud_access_key.is_empty() && !cfg_acrcloud_access_secret.is_empty()) {
            foo_artwork::log_printf("foo_artwork: Stream URL contains 'forceacr' tag. Bypassing text search to ACRCloud fallback.");
            search_acrcloud_fallback_async(cache_key, callback);
        } else {
            artwork_result fail_res;
            fail_res.success = false;
            fail_res.error_message = "Stream URL has 'forceacr' tag but ACRCloud is disabled or unconfigured";
            callback(fail_res);
        }
        return;
    }

    // If metadata is station name/URL or invalid, skip text search to allow the 10-second initial stream metadata monitor time to receive ICY track updates.
    if (meta.is_station_or_url || !meta.is_valid_search) {
        foo_artwork::log_printf("foo_artwork: Metadata '%s - %s' flagged as station/URL or invalid. Skipping text search (allowing 10s stream monitor for metadata updates).",
                       raw_artist.c_str(), raw_track.c_str());
        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = "Metadata is station URL or invalid for text search";
        callback(fail_res);
        return;
    }

    // Deduplicate in-flight search requests for identical metadata
    pfc::string8 dedup_key_pfc = generate_cache_key(meta.clean_artist.c_str(), meta.clean_title.c_str());
    std::string dedup_key = dedup_key_pfc.c_str();

    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        auto it = g_in_flight_queries.find(dedup_key);
        if (it != g_in_flight_queries.end()) {
            // Already in-flight: queue callback and exit without triggering duplicate network queries
            it->second.push_back(callback);
            foo_artwork::log_printf("foo_artwork: Search for '%s - %s' is already in-flight. Merging request.", meta.clean_artist.c_str(), meta.clean_title.c_str());
            return;
        }
        // Register new in-flight query
        g_in_flight_queries[dedup_key].push_back(callback);
    }

    // Callback wrapper to dispatch result to all merged in-flight listeners when query completes
    auto final_callback = [dedup_key](const artwork_result& result) {
        if (g_is_shutting_down.load() || core_api::is_shutting_down()) return;
        std::vector<artwork_callback> callbacks_to_call;
        {
            std::lock_guard<std::mutex> lock(g_in_flight_mutex);
            auto it = g_in_flight_queries.find(dedup_key);
            if (it != g_in_flight_queries.end()) {
                callbacks_to_call = std::move(it->second);
                g_in_flight_queries.erase(it);
            }
        }
        for (const auto& cb : callbacks_to_call) {
            if (cb && !g_is_shutting_down.load() && !core_api::is_shutting_down()) {
                try {
                    cb(result);
                } catch (...) {}
            }
        }
    };

    auto api_order = get_api_search_order();

    pfc::string8 first_art = meta.first_artist.c_str();
    pfc::string8 second_art = meta.second_artist.c_str();
    pfc::string8 full_art = meta.clean_artist.c_str();
    pfc::string8 clean_title = meta.clean_title.c_str();
    pfc::string8 primary_title = meta.primary_title.c_str();

    auto notify_text_search_failed = [=]() {
        foo_artwork::log_printf("foo_artwork: Text search failed for '%s - %s'. No artwork found from any online API.",
                                 meta.clean_artist.c_str(), meta.clean_title.c_str());

        // YouTube thumbnail direct fallback check
        pfc::string8 yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
        if (yt_video_id.is_empty() && g_active_playing_track.is_valid()) {
            yt_video_id = extract_youtube_video_id(g_active_playing_track->get_path());
        }
        if (!yt_video_id.is_empty()) {
            if (g_rejected_providers_for_current_track.find("YouTube Thumbnail") != g_rejected_providers_for_current_track.end()) {
                foo_artwork::log_printf("foo_artwork: Skipping rejected provider 'YouTube Thumbnail' for current track.");
            } else {
                foo_artwork::log_printf("foo_artwork: Querying YouTube video thumbnail for ID '%s' as fallback...", yt_video_id.c_str());
                search_youtube_thumbnail_async(yt_video_id, cache_key, [final_callback, cache_key](const artwork_result& yt_res) {
                    if (yt_res.success) {
                        final_callback(yt_res);
                    } else {
                        if (cfg_enable_acrcloud && !cfg_acrcloud_host.is_empty() && !cfg_acrcloud_access_key.is_empty() && !cfg_acrcloud_access_secret.is_empty()) {
                            auto now = std::chrono::steady_clock::now();
                            if (now >= g_acrcloud_cooldown_until) {
                                foo_artwork::log_printf("foo_artwork: Triggering ACRCloud audio recognition fallback after YouTube thumbnail failure...");
                                search_acrcloud_fallback_async(cache_key, final_callback);
                                return;
                            }
                        }
                        artwork_result fail_res;
                        fail_res.success = false;
                        fail_res.error_message = "No artwork found in text search or YouTube thumbnail";
                        final_callback(fail_res);
                    }
                });
                return;
            }
        }

        if (cfg_enable_acrcloud && !cfg_acrcloud_host.is_empty() && !cfg_acrcloud_access_key.is_empty() && !cfg_acrcloud_access_secret.is_empty()) {
            auto now = std::chrono::steady_clock::now();
            if (now >= g_acrcloud_cooldown_until) {
                foo_artwork::log_printf("foo_artwork: Triggering ACRCloud audio recognition fallback after text search failure...");
                search_acrcloud_fallback_async(cache_key, final_callback);
                return;
            }
        }
        if (cfg_enable_acrcloud && !g_current_stream_url.is_empty()) {
            foo_artwork::log_printf("foo_artwork: Stage 3 text search ended with no artwork found. Enabling Log-Spectral Acoustic Shift detection...");
            start_rms_silence_detector(g_current_stream_url);
        }
        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = "No artwork found in text search";
        titleformat_provider::set_status("No artwork found");
        final_callback(fail_res);
    };

    // 3-Tier Text Search Query Pipeline:
    // Tier 1: Full Track Title (First Artist -> Second Artist -> Full Clean Artist)
    // Tier 2: Primary Track Title (First Artist -> Second Artist -> Full Clean Artist)
    // Tier 3: Swapped Fallback (Title as Artist, Artist as Track)
    // (Note: ACRCloud audio fingerprinting is dedicated to untagged streams/station URLs and does not trigger on text search failures)

    // Tier 1 Execution
    search_apis_by_priority(first_art, clean_title, cache_key, [=](const artwork_result& r1) {
        if (r1.success) {
            final_callback(r1);
            return;
        }

        auto try_tier1_second = [=]() {
            if (!second_art.is_empty() && second_art != first_art) {
                search_apis_by_priority(second_art, clean_title, cache_key, [=](const artwork_result& r1_2) {
                    if (r1_2.success) {
                        final_callback(r1_2);
                        return;
                    }
                    if (!full_art.is_empty() && full_art != first_art && full_art != second_art) {
                        search_apis_by_priority(full_art, clean_title, cache_key, [=](const artwork_result& r1_3) {
                            if (r1_3.success) final_callback(r1_3);
                            else notify_text_search_failed();
                        }, api_order, 0);
                    } else {
                        notify_text_search_failed();
                    }
                }, api_order, 0);
            } else if (!full_art.is_empty() && full_art != first_art) {
                search_apis_by_priority(full_art, clean_title, cache_key, [=](const artwork_result& r1_3) {
                    if (r1_3.success) final_callback(r1_3);
                    else notify_text_search_failed();
                }, api_order, 0);
            } else {
                // Tier 1 failed. Try Tier 2 (Primary Title) if available
                if (!primary_title.is_empty() && primary_title != clean_title) {
                    search_apis_by_priority(first_art, primary_title, cache_key, [=](const artwork_result& r2) {
                        if (r2.success) final_callback(r2);
                        else {
                            // Try Tier 3 (Swapped Fallback)
                            search_apis_by_priority(clean_title, full_art, cache_key, [=](const artwork_result& r3) {
                                if (r3.success) final_callback(r3);
                                else notify_text_search_failed();
                            }, api_order, 0);
                        }
                    }, api_order, 0);
                } else {
                    // Try Tier 3 (Swapped Fallback)
                    search_apis_by_priority(clean_title, full_art, cache_key, [=](const artwork_result& r3) {
                        if (r3.success) final_callback(r3);
                        else notify_text_search_failed();
                    }, api_order, 0);
                }
            }
        };

        try_tier1_second();
    }, api_order, 0);
}

static visualisation_stream::ptr get_persistent_vis_stream() {
    if (!g_vis_stream.is_valid()) {
        try {
            visualisation_manager::get()->create_stream(g_vis_stream, 0);
            visualisation_stream_v2::ptr stream_v2;
            if (g_vis_stream.is_valid() && g_vis_stream->service_query_t(stream_v2)) {
                stream_v2->set_channel_mode(visualisation_stream_v2::channel_mode_mono);
                stream_v2->request_backlog(10.0);
            }
        } catch (...) {
            g_vis_stream.release();
        }
    }
    return g_vis_stream;
}
void artwork_manager::search_acrcloud_fallback_async(const pfc::string8& cache_key, artwork_callback callback, bool is_manual_trigger) {
    if (!cfg_enable_acrcloud || cfg_acrcloud_host.is_empty() || cfg_acrcloud_access_key.is_empty() || cfg_acrcloud_access_secret.is_empty()) {
        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = "All text search tiers failed; ACRCloud fallback is disabled or unconfigured";
        callback(fail_res);
        return;
    }

    // Smart Trigger & Circuit-Breaker Cooldown Check (Bypassed if manual hotkey trigger)
    auto now = std::chrono::steady_clock::now();
    if (!is_manual_trigger && now < g_acrcloud_cooldown_until) {
        auto remaining_sec = std::chrono::duration_cast<std::chrono::seconds>(g_acrcloud_cooldown_until - now).count();
        foo_artwork::log_printf("foo_artwork: ACRCloud recognition on cooldown (%d seconds remaining). Skipping scan to protect API quota.", (int)remaining_sec);

        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = "ACRCloud recognition on cooldown to protect API quota";
        callback(fail_res);
        return;
    }

    uint64_t current_task_id = ++g_acrcloud_task_id;

    if (is_manual_trigger) {
        foo_artwork::log_printf("foo_artwork: Manual Trigger: Bypassing Circuit-Breaker cooldown to force ACRCloud audio recognition...");
    } else {
        foo_artwork::log_printf("foo_artwork: Initiating ACRCloud audio recognition fallback...");
    }
    titleformat_provider::set_status("ACRCloud: Listening for audio stream...");

    // Submit task to background worker thread (NON-BLOCKING FOR FOOBAR2000 UI)
    async_io_manager::instance().submit_task([cache_key, callback, is_manual_trigger, current_task_id]() {
        std::vector<int16_t> pcm_samples;
        int sample_rate = 16000;

        for (int attempt = 0; attempt < 5; ++attempt) {
            if (current_task_id != g_acrcloud_task_id) {
                foo_artwork::log_printf("foo_artwork: ACRCloud background sampling task cancelled (superseded or artwork found).");
                return;
            }

            foo_artwork::log_printf("foo_artwork: Waiting for 5-second PCM stream accumulation (attempt %d/5)...", attempt + 1);

            // Sleep on BACKGROUND WORKER THREAD (UI stays completely fluid and responsive!)
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            }

            std::promise<bool> query_promise;
            auto query_future = query_promise.get_future();

            // Non-blocking sub-millisecond snapshot on main thread
            async_io_manager::instance().post_to_main_thread([&]() {
                bool captured = false;
                try {
                    if (attempt == 0) {
                        g_vis_stream.release();
                    }
                    auto stream = get_persistent_vis_stream();
                    if (stream.is_valid()) {
                        double abs_time = 0;
                        if (stream->get_absolute_time(abs_time) && abs_time >= 3.5) {
                            double req_len = (abs_time > 6.0) ? 6.0 : abs_time;
                            audio_chunk_impl chunk;
                            if (stream->get_chunk_absolute(chunk, abs_time - req_len, req_len)) {
                                const audio_sample* data = chunk.get_data();
                                size_t sample_cnt = chunk.get_sample_count();
                                unsigned chans = chunk.get_channels();
                                sample_rate = chunk.get_sample_rate();

                                if (data && sample_cnt > 0 && chans > 0) {
                                    pcm_samples.clear();
                                    pcm_samples.reserve(sample_cnt);
                                    for (size_t i = 0; i < sample_cnt; ++i) {
                                        float sum = 0.0f;
                                        for (unsigned c = 0; c < chans; ++c) {
                                            sum += (float)data[i * chans + c];
                                        }
                                        float mono = sum / (float)chans;
                                        if (mono > 1.0f) mono = 1.0f;
                                        if (mono < -1.0f) mono = -1.0f;
                                        pcm_samples.push_back((int16_t)(mono * 32767.0f));
                                    }

                                    if (pcm_samples.size() >= (size_t)(sample_rate * 4.5)) {
                                        captured = true;
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {}
                query_promise.set_value(captured);
            });

            if (query_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
                if (query_future.get()) {
                    break; // Captured >= 4.5s of audio!
                }
            }
        }

        foo_artwork::log_printf("foo_artwork: Sampled %u PCM audio samples (%d Hz) for fingerprinting",
                       (unsigned int)pcm_samples.size(), sample_rate);

        if (pcm_samples.empty() || pcm_samples.size() < (size_t)(sample_rate * 3.5)) {
            foo_artwork::log_printf("foo_artwork: Stream audio buffering (%u samples). Setting short 4s grace period for audio playback to settle.", (unsigned int)pcm_samples.size());
            g_acrcloud_cooldown_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            artwork_result fail_res;
            fail_res.success = false;
            fail_res.error_message = "Stream audio buffering";
            callback(fail_res);
            return;
        }

        // Resample to 16 kHz on background thread
        if (sample_rate > 0 && sample_rate != 16000 && !pcm_samples.empty()) {
            double ratio = 16000.0 / (double)sample_rate;
            size_t target_samples = (size_t)(pcm_samples.size() * ratio);
            std::vector<int16_t> resampled_pcm;
            resampled_pcm.reserve(target_samples);
            for (size_t i = 0; i < target_samples; ++i) {
                double src_idx = (double)i / ratio;
                size_t idx0 = (size_t)src_idx;
                size_t idx1 = idx0 + 1;
                if (idx1 >= pcm_samples.size()) idx1 = idx0;
                double frac = src_idx - (double)idx0;
                int16_t s = (int16_t)((1.0 - frac) * pcm_samples[idx0] + frac * pcm_samples[idx1]);
                resampled_pcm.push_back(s);
            }
            pcm_samples = std::move(resampled_pcm);
            sample_rate = 16000;
        }

        std::vector<uint8_t> audio_bytes;
        if (!pcm_samples.empty()) {
            audio_bytes.resize(pcm_samples.size() * sizeof(int16_t));
            memcpy(audio_bytes.data(), pcm_samples.data(), audio_bytes.size());
        }

        ACRCloudClient::RecognitionResult rec = ACRCloudClient::recognize_audio(
            cfg_acrcloud_host.get_ptr(),
            cfg_acrcloud_access_key.get_ptr(),
            cfg_acrcloud_access_secret.get_ptr(),
            audio_bytes.data(),
            audio_bytes.size()
        );

        if (rec.success) {
            log_simplified_track_info(rec.artist.c_str(), rec.title.c_str());

            // Track Cooldown Guard: Compute remaining track duration (minimum 60 seconds)
            uint32_t rem_ms = 90000;
            if (rec.duration_ms > 0) {
                rem_ms = (rec.duration_ms > rec.play_offset_ms) ? (rec.duration_ms - rec.play_offset_ms) : rec.duration_ms;
                if (rem_ms < 60000) rem_ms = 60000;
            }

            g_acrcloud_cooldown_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(rem_ms);

            StreamMetadataResult rec_meta = MetadataCleaner::sanitize_stream_metadata(rec.artist.c_str(), rec.title.c_str());

            pfc::string8 status_msg = pfc::string8("ACRCloud Match: '") + rec.artist.c_str() + " - " + rec.title.c_str() + "'";
            titleformat_provider::set_status(status_msg.c_str());

            async_io_manager::instance().post_to_main_thread([rec_meta, rec]() {
                metadb_handle_ptr track;
                if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
                    titleformat_provider::set_track_artwork_info(track, rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str(), "", "ACRCloud", rec_meta.clean_artist.c_str(), rec.album.c_str(), "");
                }
            });

            if (rec_meta.is_valid_search) {
                if (g_last_recognized_stream_url == g_current_stream_url &&
                    rec_meta.first_artist == g_last_recognized_artist.c_str() &&
                    rec_meta.clean_title == g_last_recognized_title.c_str() &&
                    g_last_recognized_result.success &&
                    g_last_recognized_result.data.get_size() > 0) {

                    foo_artwork::log_printf("foo_artwork: ACRCloud recognized unchanged track '%s - %s'. Skipping online API search.", rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str());
                    callback(g_last_recognized_result);
                    return;
                }

                g_last_recognized_artist = rec_meta.first_artist.c_str();
                g_last_recognized_title = rec_meta.clean_title.c_str();

                foo_artwork::log_printf("foo_artwork: Querying online APIs with ACRCloud recognized track '%s - %s'...", rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str());
                auto api_order = get_api_search_order();
                pfc::string8 current_url = g_current_stream_url;
                search_apis_by_priority(rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str(), cache_key, [callback, current_url](const artwork_result& res) {
                    if (res.success && res.data.get_size() > 0) {
                        g_last_recognized_result = res;
                        g_last_recognized_stream_url = current_url;
                        if (!contains_case_insensitive(current_url.c_str(), "forceacr")) {
                            stop_rms_silence_detector();
                        }
                    } else {
                        if (cfg_enable_acrcloud && !current_url.is_empty()) {
                            foo_artwork::log_printf("foo_artwork: Stage 3 text search for ACRCloud metadata returned no artwork. Enabling Log-Spectral Acoustic Shift detection...");
                            start_rms_silence_detector(current_url);
                        }
                    }
                    callback(res);
                }, api_order, 0, false);
                return;
            }
        } else {
            // Non-Music Content / Talk / Ad Backoff: Apply 75-second cooldown on Status 1001 or no match
            g_acrcloud_cooldown_until = std::chrono::steady_clock::now() + std::chrono::seconds(75);
            foo_artwork::log_printf("foo_artwork: ACRCloud returned No Result.");
            if (rec.error_message.find("limit") != std::string::npos || rec.error_message.find("exceeded") != std::string::npos || rec.error_message.find("3001") != std::string::npos || rec.error_message.find("3014") != std::string::npos || rec.error_message.find("3015") != std::string::npos) {
                titleformat_provider::set_status("ACRCloud Requests Limit Exceeded");
            } else {
                titleformat_provider::set_status("ACRCloud returned No Result");
            }
            if (cfg_enable_acrcloud && !g_current_stream_url.is_empty()) {
                foo_artwork::log_printf("foo_artwork: ACRCloud recognition returned no match. Enabling Log-Spectral Acoustic Shift detection...");
                start_rms_silence_detector(g_current_stream_url);
            }
        }

        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = rec.error_message.empty() ? "ACRCloud audio recognition returned no matching track" : rec.error_message.c_str();
        callback(fail_res);
    });
}

void artwork_manager::search_apis_by_priority(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback, const std::vector<ApiType>& api_order, size_t index, bool force_enable_apis) {
    ASSERT_MAIN_THREAD();
    if (g_is_shutting_down.load() || core_api::is_shutting_down()) return;
    
    if (index == 0) {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track.c_str());
        if (meta.is_valid_search && !meta.is_station_or_url) {
            log_simplified_track_info(meta.first_artist.c_str(), meta.clean_title.c_str());
        }
        foo_artwork::log_printf("foo_artwork: Querying online APIs for '%s - %s'...", artist.c_str(), track.c_str());
    }

    if (index >= api_order.size()) {
        // No more APIs to try
        artwork_result final_result;
        final_result.success = false;
        final_result.error_message = "No artwork found from any source";
        titleformat_provider::set_status("No artwork found");
        callback(final_result);
        return;
    }
    
    ApiType current_api = api_order[index];
    
    // Check if this API is enabled and has required keys (or force enabled for ACRCloud fallback)
    bool api_enabled = false;
    switch (current_api) {
        case ApiType::iTunes:
            api_enabled = force_enable_apis || cfg_enable_itunes;
            break;
        case ApiType::Deezer:
            api_enabled = force_enable_apis || cfg_enable_deezer;
            break;
        case ApiType::LastFm:
            api_enabled = (force_enable_apis || cfg_enable_lastfm) && !cfg_lastfm_key.is_empty();
            break;
        case ApiType::MusicBrainz:
            api_enabled = force_enable_apis || cfg_enable_musicbrainz;
            break;
        case ApiType::Discogs:
            api_enabled = (force_enable_apis || cfg_enable_discogs) && 
                         (!cfg_discogs_key.is_empty() || 
                          (!cfg_discogs_consumer_key.is_empty() && !cfg_discogs_consumer_secret.is_empty()));
            break;
    }
    
    if (!api_enabled) {
        // Skip this API and try the next one
        search_apis_by_priority(artist, track, cache_key, callback, api_order, index + 1, force_enable_apis);
        return;
    }
    
    pfc::string8 current_api_name;
    switch (current_api) {
        case ApiType::iTunes: current_api_name = "iTunes"; break;
        case ApiType::Deezer: current_api_name = "Deezer"; break;
        case ApiType::LastFm: current_api_name = "Last.fm"; break;
        case ApiType::MusicBrainz: current_api_name = "MusicBrainz"; break;
        case ApiType::Discogs: current_api_name = "Discogs"; break;
    }

    pfc::string8 status_str = pfc::string8("Querying ") + current_api_name + "...";
    titleformat_provider::set_status(status_str.c_str());

    // Check if this provider has been rejected by user for current track
    if (g_rejected_providers_for_current_track.find(current_api_name.c_str()) != g_rejected_providers_for_current_track.end()) {
        foo_artwork::log_printf("foo_artwork: Skipping rejected provider '%s' for current track.", current_api_name.c_str());
        search_apis_by_priority(artist, track, cache_key, callback, api_order, index + 1, force_enable_apis);
        return;
    }

    std::string api_dedup_key = current_api_name.c_str();
    api_dedup_key += "|";
    api_dedup_key += artist.c_str();
    api_dedup_key += "|";
    api_dedup_key += track.c_str();

    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        auto now = std::chrono::steady_clock::now();

        // Clean up stale completed entries older than 60 seconds
        for (auto it = g_api_dedup_map.begin(); it != g_api_dedup_map.end(); ) {
            if (it->second.completed && (now - it->second.completed_time > std::chrono::seconds(60))) {
                it = g_api_dedup_map.erase(it);
            } else {
                ++it;
            }
        }

        auto it = g_api_dedup_map.find(api_dedup_key);
        if (it != g_api_dedup_map.end()) {
            if (it->second.completed) {
                // Query recently completed within last 60 seconds
                artwork_result res = it->second.result;
                if (res.success) {
                    async_io_manager::instance().post_to_main_thread([callback, res]() {
                        if (callback) callback(res);
                    });
                    return;
                }
            } else {
                // Query currently in-flight: merge callback
                it->second.callbacks.push_back(callback);
                foo_artwork::log_printf("foo_artwork: %s search for '%s - %s' is already in-flight. Merging request.", current_api_name.c_str(), artist.c_str(), track.c_str());
                return;
            }
        }

        // Register new query entry
        ApiDedupEntry entry;
        entry.completed = false;
        entry.callbacks.push_back(callback);
        g_api_dedup_map[api_dedup_key] = std::move(entry);
    }
    
    // Create a callback that will either return success or try the next API for all pending callbacks
    auto api_callback = [artist, track, cache_key, api_order, index, force_enable_apis, api_dedup_key](const artwork_result& result) {
        pfc::string8 api_name;
        switch (api_order[index]) {
            case ApiType::iTunes: api_name = "iTunes"; break;
            case ApiType::Deezer: api_name = "Deezer"; break;
            case ApiType::LastFm: api_name = "Last.fm"; break;
            case ApiType::MusicBrainz: api_name = "MusicBrainz"; break;
            case ApiType::Discogs: api_name = "Discogs"; break;
        }
        
        std::vector<artwork_callback> callbacks_to_call;
        {
            std::lock_guard<std::mutex> lock(g_in_flight_mutex);
            auto it = g_api_dedup_map.find(api_dedup_key);
            if (it != g_api_dedup_map.end()) {
                it->second.completed = true;
                it->second.result = result;
                it->second.completed_time = std::chrono::steady_clock::now();
                callbacks_to_call = std::move(it->second.callbacks);
            }
        }

        if (result.success) {
            foo_artwork::log_printf("foo_artwork: SUCCESS - Artwork retrieved from %s for '%s - %s' (%u bytes)", api_name.c_str(), artist.c_str(), track.c_str(), (unsigned int)result.data.get_size());
            g_active_resolved_provider = api_name;
            g_active_source = api_name;
            pfc::string8 status_msg = pfc::string8("Artwork loaded from ") + api_name;
            titleformat_provider::set_status(status_msg.c_str());
            cancel_acrcloud_tasks(); // Cancel any pending background ACRCloud sampling tasks
            if (cfg_enable_disk_cache || cfg_single_file_cache) {
                if (!cache_key.is_empty()) {
                    async_io_manager::instance().cache_set_async(cache_key, result.data);
                }
                if (cfg_single_file_cache) {
                    async_io_manager::instance().cache_set_async("current", result.data);
                }
            }
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            metadb_handle_ptr now_track = g_active_playing_track;
            titleformat_provider::set_track_artwork_info(now_track, artist.c_str(), track.c_str(), cache_file.c_str(), api_name.c_str());
            std::vector<uint8_t> vec(result.data.get_ptr(), result.data.get_ptr() + result.data.get_size());
            create_bitmap_from_image_data(vec);
            refresh_all_dui_artwork_panels();
            refresh_all_cui_artwork_panels();
            for (const auto& cb : callbacks_to_call) {
                if (cb) cb(result);
            }
        } else {
            foo_artwork::log_printf("foo_artwork: API FAILED - %s failed for '%s - %s' (error: %s)", 
                           api_name.c_str(), artist.c_str(), track.c_str(), result.error_message.c_str());
            
            // This API failed, try the next API once and broadcast the result to all merged callbacks
            auto combined_cb = [callbacks_to_call](const artwork_result& next_res) {
                for (const auto& cb : callbacks_to_call) {
                    if (cb) cb(next_res);
                }
            };
            search_apis_by_priority(artist, track, cache_key, combined_cb, api_order, index + 1, force_enable_apis);
        }
    };

    foo_artwork::log_printf("foo_artwork: Querying %s for '%s - %s'...", current_api_name.c_str(), artist.c_str(), track.c_str());
    
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
                            g_active_resolved_provider = "Local artwork";
                            g_active_source = "Local artwork";
                            titleformat_provider::set_status("Artwork loaded from Local artwork");
                            
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

        
        
        // Download the artwork image with 600x600 fallback if 1200x1200 fails
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback, artwork_url](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0) {
                artwork_result result;
                result.success = true;
                result.data = data;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "iTunes";  // Set source for OSD display
                callback(result);
            } else {
                pfc::string8 fallback_url = artwork_url;
                fallback_url.replace_string("1200x1200", "600x600");
                if (fallback_url != artwork_url) {
                    async_io_manager::instance().http_get_binary_async(fallback_url, [callback](bool success2, const pfc::array_t<t_uint8>& data2, const pfc::string8& error2) {
                        artwork_result result;
                        if (success2 && data2.get_size() > 0) {
                            result.success = true;
                            result.data = data2;
                            result.mime_type = detect_mime_type(data2.get_ptr(), data2.get_size());
                            result.source = "iTunes";
                        } else {
                            result.success = false;
                            result.error_message = "Failed to download iTunes artwork: ";
                            result.error_message << error2;
                        }
                        callback(result);
                    });
                } else {
                    artwork_result result;
                    result.success = false;
                    result.error_message = "Failed to download iTunes artwork: ";
                    result.error_message << error;
                    callback(result);
                }
            }
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
    if (!url || strlen(url) == 0) {
        artwork_result result;
        result.success = false;
        result.error_message = "Empty image URL";
        async_io_manager::instance().post_to_main_thread([callback, result]() {
            callback(result);
        });
        return;
    }

    pfc::string8 download_url = url;
    async_io_manager::instance().http_get_binary_async(download_url, [callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
        if (success && data.get_size() > 0 && is_valid_image_data(data.get_ptr(), data.get_size())) {
            artwork_result result;
            result.data = data;
            result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
            result.success = true;
            async_io_manager::instance().post_to_main_thread([callback, result]() {
                callback(result);
            });
        } else {
            artwork_result result;
            result.success = false;
            result.error_message = error.is_empty() ? pfc::string8("Invalid or empty image data downloaded") : error;
            async_io_manager::instance().post_to_main_thread([callback, result]() {
                callback(result);
            });
        }
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
    result.source = (!g_active_resolved_provider.is_empty() && g_active_resolved_provider != "Cache") ? g_active_resolved_provider : pfc::string8("Cache");
    
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

    // WebP (RIFF....WEBP)
    if (size >= 12 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') return true;

    return false;
}

pfc::string8 artwork_manager::detect_mime_type(const t_uint8* data, size_t size) {
    if (size < 4) return "application/octet-stream";
    
    // JPEG
    if (data[0] == 0xFF && data[1] == 0xD8) return "image/jpeg";
    
    // PNG
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return "image/png";
    
    // WebP (RIFF....WEBP)
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
           mime_type == "image/bmp" ||
           mime_type == "image/webp";
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
    if (!str) return result;
    
    for (const char* p = str; *p; ++p) {
        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
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

// Helper to convert UTF-8 pfc::string8 to wide string for Unicode Windows APIs
static std::wstring utf8_to_wide(const pfc::string8& utf8_str) {
    if (utf8_str.is_empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, &result[0], len);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

pfc::string8 artwork_manager::generate_cache_key_for_track(metadb_handle_ptr track) {
    if (!track.is_valid()) return pfc::string8("unknown");

    pfc::string8 artist, track_name;
    pfc::string8 file_path = track->get_path();
    pfc::string8 yt_video_id = extract_youtube_video_id(file_path.c_str());
    if (yt_video_id.is_empty()) {
        yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
    }
    if (!yt_video_id.is_empty()) {
        pfc::string8 key = "yt_";
        key << yt_video_id;
        return key;
    }

    bool is_youtube = false;
    bool is_internet_stream = (strstr(file_path.c_str(), "://") && !(strstr(file_path.c_str(), "file://") == file_path.c_str()));

    extract_track_metadata_dynamic(track, artist, track_name);

    if (is_internet_stream) {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track_name.c_str());
        if (meta.is_valid_search && !meta.clean_artist.empty() && !meta.clean_title.empty()) {
            return generate_cache_key(meta.clean_artist.c_str(), meta.clean_title.c_str());
        }
        if (!artist.is_empty() && !track_name.is_empty()) {
            return generate_cache_key(artist.c_str(), track_name.c_str());
        }
        return generate_cache_key(artist.is_empty() ? "Unknown Artist" : artist.c_str(),
                                  track_name.is_empty() ? "Unknown Track" : track_name.c_str());
    } else {
        // Offline / local track
        if (!artist.is_empty() && !track_name.is_empty() &&
            artist != "Unknown Artist" && track_name != "Unknown Track") {
            return generate_cache_key(artist.c_str(), track_name.c_str());
        }

        // Untagged or missing metadata: generate key from local file name
        pfc::string8 clean_path = file_path;
        if (clean_path.find_first("file://") == 0) {
            clean_path = clean_path.get_ptr() + 7;
        }
        t_size last_slash = clean_path.find_last('\\');
        if (last_slash == pfc_infinite) last_slash = clean_path.find_last('/');
        pfc::string8 filename = (last_slash != pfc_infinite) ? pfc::string8(clean_path.get_ptr() + last_slash + 1) : clean_path;

        pfc::string8 key = "_local_";
        key << filename;
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
}

bool artwork_manager::is_local_artwork_newer_than_cache(const pfc::string8& file_path, const pfc::string8& cache_key) {
    if (file_path.is_empty() || cache_key.is_empty()) return false;

    pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
    if (cache_file.is_empty()) return false;

    std::wstring wide_cache_file = utf8_to_wide(cache_file);
    WIN32_FILE_ATTRIBUTE_DATA cache_attr;
    if (!GetFileAttributesExW(wide_cache_file.c_str(), GetFileExInfoStandard, &cache_attr)) {
        // Cache file does not exist on disk yet
        return false;
    }
    FILETIME cache_time = cache_attr.ftLastWriteTime;

    // 1. Check embedded metadata timestamp (the audio file itself)
    pfc::string8 local_path = file_path;
    if (local_path.find_first("file://") == 0) {
        local_path = local_path.get_ptr() + 7;
    }
    for (size_t i = 0; i < local_path.length(); i++) {
        if (local_path[i] == '/') {
            local_path.set_char(i, '\\');
        }
    }
    std::wstring wide_audio_path = utf8_to_wide(local_path);
    WIN32_FILE_ATTRIBUTE_DATA audio_attr;
    if (GetFileAttributesExW(wide_audio_path.c_str(), GetFileExInfoStandard, &audio_attr)) {
        if (CompareFileTime(&audio_attr.ftLastWriteTime, &cache_time) > 0) {
            return true;
        }
    }

    // 2. Check folder artwork files in the audio file's directory
    pfc::string8 dir = get_file_directory(file_path);
    if (!dir.is_empty()) {
        for (size_t i = 0; i < dir.length(); i++) {
            if (dir[i] == '/') {
                dir.set_char(i, '\\');
            }
        }
        if (dir.length() > 0 && dir[dir.length() - 1] != '\\') {
            dir << "\\";
        }

        std::wstring wide_dir = utf8_to_wide(dir);
        std::wstring wide_pattern = wide_dir + L"*.*";

        WIN32_FIND_DATAW find_data;
        HANDLE find_handle = FindFirstFileW(wide_pattern.c_str(), &find_data);
        if (find_handle != INVALID_HANDLE_VALUE) {
            do {
                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    const wchar_t* ext = wcsrchr(find_data.cFileName, L'.');
                    if (ext != nullptr) {
                        if (_wcsicmp(ext, L".jpg") == 0 ||
                            _wcsicmp(ext, L".jpeg") == 0 ||
                            _wcsicmp(ext, L".png") == 0 ||
                            _wcsicmp(ext, L".webp") == 0 ||
                            _wcsicmp(ext, L".bmp") == 0 ||
                            _wcsicmp(ext, L".gif") == 0) {

                            if (CompareFileTime(&find_data.ftLastWriteTime, &cache_time) > 0 ||
                                CompareFileTime(&find_data.ftCreationTime, &cache_time) > 0) {
                                FindClose(find_handle);
                                return true;
                            }
                        }
                    }
                }
            } while (FindNextFileW(find_handle, &find_data));
            FindClose(find_handle);
        }
    }

    return false;
}

// JSON parsing implementations
bool artwork_manager::parse_itunes_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url) {
    try {
        std::string json_data;
        json_data += json_in;

        json data = json::parse(json_data);

        if (!data.contains("resultCount") || data["resultCount"].get<int>() == 0 || !data.contains("results") || !data["results"].is_array()) {
            return false;
        }

        const auto& results = data["results"];
        std::string artist_str(artist ? artist : "");
        std::string track_str(track ? track : "");

        auto extract_url = [](const json& item, pfc::string8& out_url) -> bool {
            std::string url_str;
            if (item.contains("artworkUrl600") && item["artworkUrl600"].is_string()) {
                url_str = item["artworkUrl600"].get<std::string>();
            } else if (item.contains("artworkUrl512") && item["artworkUrl512"].is_string()) {
                url_str = item["artworkUrl512"].get<std::string>();
            } else if (item.contains("artworkUrl100") && item["artworkUrl100"].is_string()) {
                url_str = item["artworkUrl100"].get<std::string>();
            } else if (item.contains("artworkUrl60") && item["artworkUrl60"].is_string()) {
                url_str = item["artworkUrl60"].get<std::string>();
            } else if (item.contains("artworkUrl30") && item["artworkUrl30"].is_string()) {
                url_str = item["artworkUrl30"].get<std::string>();
            }

            if (url_str.empty()) return false;

            out_url = url_str.c_str();
            // Upgrade resolution to 1200x1200 while preserving valid Apple CDN format
            out_url.replace_string("100x100", "1200x1200");
            out_url.replace_string("600x600", "1200x1200");
            out_url.replace_string("512x512", "1200x1200");
            out_url.replace_string("60x60", "1200x1200");
            out_url.replace_string("30x30", "1200x1200");

            return !out_url.is_empty() && strstr(out_url.get_ptr(), "http") == out_url.get_ptr();
        };

        // 1. Search for exact artist + track match
        for (const auto& item : results) {
            std::string result_track;
            if (item.contains("trackName") && item["trackName"].is_string()) {
                result_track = item["trackName"].get<std::string>();
            } else if (item.contains("collectionName") && item["collectionName"].is_string()) {
                result_track = item["collectionName"].get<std::string>();
            }

            std::string result_artist;
            if (item.contains("artistName") && item["artistName"].is_string()) {
                result_artist = item["artistName"].get<std::string>();
            }

            if (!result_track.empty() && !result_artist.empty()) {
                if (strings_match_fuzzy(result_track, track_str) && artists_match(result_artist, artist_str)) {
                    if (extract_url(item, artwork_url)) {
                        return true;
                    }
                }
            }
        }

        // 2. Fallback: match by artist
        for (const auto& item : results) {
            std::string result_artist;
            if (item.contains("artistName") && item["artistName"].is_string()) {
                result_artist = item["artistName"].get<std::string>();
            }

            if (!result_artist.empty() && artists_match(result_artist, artist_str)) {
                if (extract_url(item, artwork_url)) {
                    return true;
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool artwork_manager::parse_deezer_json(const char* artist, const char* track ,const pfc::string8& json_in, pfc::string8& artwork_url) {
    try {
        std::string json_data;
        json_data += json_in;

        json data = json::parse(json_data);

        if (!data.contains("total") || data["total"] == 0 || !data.contains("data") || !data["data"].is_array()) return false;

        //sort by rank to get higher ratings values first
        std::sort(data["data"].begin(), data["data"].end(),
            [](const json& a, const json& b) {
                int rank_a = (a.contains("rank") && a["rank"].is_number()) ? a["rank"].get<int>() : 0;
                int rank_b = (b.contains("rank") && b["rank"].is_number()) ? b["rank"].get<int>() : 0;
                return rank_a > rank_b;
            });

        json s = data["data"];
        std::string artist_str(artist ? artist : "");
        std::string track_str(track ? track : "");

        auto unescape_url = [](const std::string& in_url) -> pfc::string8 {
            pfc::string8 unescaped;
            const char* src = in_url.c_str();
            while (*src) {
                if (*src == '\\' && *(src + 1) == '/') {
                    unescaped += "/";
                    src += 2;
                } else {
                    char single_char[2] = { *src, '\0' };
                    unescaped += single_char;
                    src++;
                }
            }
            return unescaped;
        };

        // 1. Search for exact same artist track values first
        for (const auto& item : s.items()) {
            if (!item.value().contains("title") || !item.value().contains("artist")) continue;
            std::string result_title = item.value()["title"].is_string() ? item.value()["title"].get<std::string>() : "";
            std::string result_artist = (item.value()["artist"].contains("name") && item.value()["artist"]["name"].is_string()) ? item.value()["artist"]["name"].get<std::string>() : "";

            if (strings_match_fuzzy(result_title, track_str) && artists_match(result_artist, artist_str)) {
                if (item.value().contains("album") && item.value()["album"].is_object()) {
                    if (item.value()["album"].contains("cover_xl") && item.value()["album"]["cover_xl"].is_string()) {
                        artwork_url = unescape_url(item.value()["album"]["cover_xl"].get<std::string>());
                        artwork_url = artwork_url.replace("1000x1000", "1200x1200");
                        return true;
                    }
                    if (item.value()["album"].contains("cover_big") && item.value()["album"]["cover_big"].is_string()) {
                        artwork_url = unescape_url(item.value()["album"]["cover_big"].get<std::string>());
                        return true;
                    }
                }
            }
        }

        // 2. Fallback: first result matching artist
        for (const auto& item : s.items()) {
            if (!item.value().contains("artist")) continue;
            std::string result_artist = (item.value()["artist"].contains("name") && item.value()["artist"]["name"].is_string()) ? item.value()["artist"]["name"].get<std::string>() : "";
            if (!artists_match(result_artist, artist_str)) continue;

            if (item.value().contains("album") && item.value()["album"].is_object()) {
                if (item.value()["album"].contains("cover_xl") && item.value()["album"]["cover_xl"].is_string()) {
                    artwork_url = unescape_url(item.value()["album"]["cover_xl"].get<std::string>());
                    artwork_url = artwork_url.replace("1000x1000", "1200x1200");
                    return true;
                }
                if (item.value()["album"].contains("cover_big") && item.value()["album"]["cover_big"].is_string()) {
                    artwork_url = unescape_url(item.value()["album"]["cover_big"].get<std::string>());
                    return true;
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool artwork_manager::parse_lastfm_json(const pfc::string8& json_in, pfc::string8& artwork_url) {
    try {
        std::string json_data;
        json_data += json_in;

        json data = json::parse(json_data);

        if (data.contains("message") && data["message"] == "Track not found") return false;
        if (!data.contains("track") || !data["track"].contains("album") || !data["track"]["album"].contains("image") || !data["track"]["album"]["image"].is_array()) {
            return false;
        }

        json s = data["track"]["album"]["image"];

        for (const auto& item : s.items()) {
            if (item.value().contains("size") && item.value()["size"].is_string() && item.value()["size"].get<std::string>() == "extralarge") {
                if (item.value().contains("#text") && item.value()["#text"].is_string()) {
                    artwork_url = item.value()["#text"].get<std::string>().c_str();
                    artwork_url = artwork_url.replace("u/300x300", "u/");
                    return true;
                }
            }
        }

        for (const auto& item : s.items()) {
            if (item.value().contains("size") && item.value()["size"].is_string() && item.value()["size"].get<std::string>() == "large") {
                if (item.value().contains("#text") && item.value()["#text"].is_string()) {
                    artwork_url = item.value()["#text"].get<std::string>().c_str();
                    artwork_url = artwork_url.replace("u/174s", "u/");
                    return true;
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool artwork_manager::parse_discogs_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url) {
    try {
        std::string json_data;
        json_data += json_in;

        json data = json::parse(json_data);

        if (data.contains("pagination") && data["pagination"].contains("items") && data["pagination"]["items"].get<int>() == 0) return false;
        if (!data.contains("results") || !data["results"].is_array()) return false;

        json s = data["results"];
        std::string artist_str(artist ? artist : "");

        std::string artist_title;
        artist_title += (artist ? artist : "");
        artist_title += " - ";
        artist_title += (track ? track : "");

        for (const auto& item : s.items()) {
            if (!item.value().contains("title") || !item.value()["title"].is_string()) continue;
            std::string result_title = item.value()["title"].get<std::string>();
            if (strings_match_fuzzy(result_title, artist_title)) {
                if (item.value().contains("cover_image") && item.value()["cover_image"].is_string()) {
                    artwork_url = item.value()["cover_image"].get<std::string>().c_str();
                    return true;
                } else if (item.value().contains("thumb") && item.value()["thumb"].is_string()) {
                    artwork_url = item.value()["thumb"].get<std::string>().c_str();
                    return true;
                }
            }
        }

        for (const auto& item : s.items()) {
            if (!item.value().contains("title") || !item.value()["title"].is_string()) continue;
            std::string result_title = item.value()["title"].get<std::string>();

            bool artist_matches = false;
            std::string artist_stripped = strip_the_prefix(artist_str);
            std::string title_stripped = strip_the_prefix(result_title);

            if (result_title.size() >= artist_str.size()) {
                artist_matches = strings_equal_ignore_case(result_title.substr(0, artist_str.size()), artist_str);
            }
            if (!artist_matches && title_stripped.size() >= artist_stripped.size()) {
                artist_matches = strings_equal_ignore_case(title_stripped.substr(0, artist_stripped.size()), artist_stripped);
            }

            if (!artist_matches) continue;

            if (item.value().contains("cover_image") && item.value()["cover_image"].is_string()) {
                artwork_url = item.value()["cover_image"].get<std::string>().c_str();
                return true;
            } else if (item.value().contains("thumb") && item.value()["thumb"].is_string()) {
                artwork_url = item.value()["thumb"].get<std::string>().c_str();
                return true;
            }
        }
    } catch (...) {
        return false;
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
        foo_artwork::log_info("MusicBrainz JSON parse error");
        return false;
    }
}
