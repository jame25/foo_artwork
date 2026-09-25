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
#include <regex>
#include <tuple>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

extern HBITMAP g_shared_artwork_bitmap;
extern pfc::string8 g_current_artwork_source;
extern void notify_artwork_cleared(const char* source);

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
extern cfg_bool cfg_normalize_api_metadata_case;
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
extern cfg_string cfg_acrcloud_host2;
extern cfg_string cfg_acrcloud_access_key2;
extern cfg_string cfg_acrcloud_access_secret2;
extern cfg_bool cfg_disable_instream_artwork;
extern cfg_bool cfg_disable_ext_api_autoprobe;

static inline bool is_acrcloud_configured() {
    bool has_primary = !cfg_acrcloud_host.is_empty() && !get_acrcloud_access_key().is_empty() && !get_acrcloud_access_secret().is_empty();
    bool has_secondary = !cfg_acrcloud_host2.is_empty() && !get_acrcloud_access_key2().is_empty() && !get_acrcloud_access_secret2().is_empty();
    return has_primary || has_secondary;
}

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
static std::atomic<bool> g_has_received_first_stream_cue{false};
static std::atomic<int> g_active_acrcloud_account{1}; // 1 = Primary, 2 = Secondary
static pfc::string8 g_last_stream_artist = "";
static pfc::string8 g_last_stream_artist_full = "";
static pfc::string8 g_last_stream_title = "";
static pfc::string8 g_pending_external_api_artist = "";
static pfc::string8 g_pending_external_api_title = "";
static pfc::string8 g_last_logged_track_info = "";

static metadb_handle_ptr g_active_playing_track;
static metadb_handle_ptr g_playback_session_track;
static pfc::string8 g_active_source;
static pfc::string8 g_active_resolved_provider;
static pfc::string8 g_active_cache_key;
static std::set<std::string> g_rejected_providers_for_current_track;
static std::atomic<uint64_t> g_search_generation{0};
static bool g_force_noart = false; // Main-thread state, reset on the next song or manual search.
static bool g_manual_external_probe = false;
// Main-thread subscribers share one recognition task per playback cue. Starting
// a second panel/metadata request must not cancel the first panel's PCM capture.
struct AcrPendingRequest {
    uint64_t generation;
    uint64_t task_id;
    std::vector<artwork_manager::artwork_callback> callbacks;
};
static std::shared_ptr<AcrPendingRequest> g_pending_acrcloud_request;
static uint64_t g_manual_search_generation = 0;
// Keep the query that belongs to this playback cue, independently of raw tags
// and display metadata. Manual provider cycling must reuse the same song.
static pfc::string8 g_search_artist, g_search_title;
static uint64_t g_search_metadata_generation = 0;

static void remember_search_metadata(const pfc::string8& artist, const pfc::string8& title,
    const pfc::string8& cache_key) {
    g_search_artist = artist;
    g_search_title = title;
    g_active_cache_key = cache_key;
    g_search_metadata_generation = g_search_generation.load();
}

uint64_t artwork_manager::get_search_generation() { return g_search_generation.load(); }

bool artwork_manager::is_noart_forced() { return g_force_noart; }
bool artwork_manager::is_manual_artwork_search() {
    return g_manual_search_generation != 0 && g_manual_search_generation == g_search_generation.load();
}

pfc::string8 artwork_manager::get_active_resolved_provider() {
    return g_active_resolved_provider;
}

pfc::string8 artwork_manager::get_active_source() {
    return g_active_source;
}

static bool get_image_dimensions_from_data(const uint8_t* data, size_t size, int& out_w, int& out_h) {
    out_w = 0;
    out_h = 0;
    if (!data || size < 16) return false;
    // 1. PNG: 8-byte magic, IHDR chunk width/height at [16..23]
    if (size >= 24 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        out_w = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
        out_h = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
        return (out_w > 0 && out_h > 0);
    }
    // 2. GIF: 'GIF87a' or 'GIF89a', width at [6..7] LE, height at [8..9] LE
    if (size >= 10 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        out_w = data[6] | (data[7] << 8);
        out_h = data[8] | (data[9] << 8);
        return (out_w > 0 && out_h > 0);
    }
    // 3. BMP: 'BM', width at [18..21] LE, height at [22..25] LE
    if (size >= 26 && data[0] == 'B' && data[1] == 'M') {
        out_w = data[18] | (data[19] << 8) | (data[20] << 16) | (data[21] << 24);
        out_h = data[22] | (data[23] << 8) | (data[24] << 16) | (data[25] << 24);
        if (out_h < 0) out_h = -out_h;
        return (out_w > 0 && out_h > 0);
    }
    // 4. JPEG: Scan markers for SOF0/SOF2 (0xFFC0 .. 0xFFC3)
    if (data[0] == 0xFF && data[1] == 0xD8) {
        size_t idx = 2;
        while (idx + 8 < size) {
            if (data[idx] != 0xFF) { idx++; continue; }
            uint8_t marker = data[idx + 1];
            if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC3) {
                out_h = (data[idx + 5] << 8) | data[idx + 6];
                out_w = (data[idx + 7] << 8) | data[idx + 8];
                return (out_w > 0 && out_h > 0);
            }
            if (marker == 0xDA || marker == 0xD9) break;
            size_t len = (data[idx + 2] << 8) | data[idx + 3];
            idx += 2 + len;
        }
    }
    // Fallback using GDI+ Bitmap from memory stream
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, data, size);
            GlobalUnlock(hMem);
            IStream* pStream = nullptr;
            if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
                Gdiplus::Bitmap bm(pStream);
                if (bm.GetLastStatus() == Gdiplus::Ok) {
                    out_w = (int)bm.GetWidth();
                    out_h = (int)bm.GetHeight();
                    pStream->Release();
                    return (out_w > 0 && out_h > 0);
                }
                pStream->Release();
            }
        } else {
            GlobalFree(hMem);
        }
    }
    return false;
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
static void start_rms_silence_detector(const pfc::string8& stream_url);
static void stop_rms_silence_detector(bool force = false);
static void reset_acrcloud_cooldown();
static void log_simplified_track_info(const char* artist, const char* title);
static bool g_is_youtube_art_track = false;
static bool g_is_youtube_topic_track = false;
static bool g_is_youtube_release_topic = false;
static pfc::string8 g_youtube_extracted_album;

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
    // Panels can request artwork before the static playback callback is dispatched.
    // Initialize once per connection, irrespective of callback ordering.
    if (track.is_valid() && g_playback_session_track == track) return;
    g_playback_session_track = track;
    g_force_noart = false;
    g_search_generation++;
    stop_external_stream_api_poller();
    stop_rms_silence_detector(true);
    cancel_acrcloud_tasks();
    g_rms_detector_token++;
    g_acrcloud_task_id++;
    g_stream_monitor_token++;
    g_external_api_session_token++;
    g_coversync_cue_token++;
    g_has_received_first_stream_cue.store(false);
    g_vis_stream.release();
    g_active_playing_track = track;
    g_active_source.reset();
    g_active_resolved_provider.reset();
    g_rejected_providers_for_current_track.clear();
    g_last_stream_artist.reset();
    g_last_stream_artist_full.reset();
    g_last_stream_title.reset();
    g_pending_external_api_artist.reset();
    g_pending_external_api_title.reset();
    g_last_logged_track_info.reset();
    g_is_youtube_art_track = false;
    g_is_youtube_topic_track = false;
    g_is_youtube_release_topic = false;
    g_youtube_extracted_album.reset();
    reset_acrcloud_cooldown();
    titleformat_provider::set_status("Searching artwork...");

    if (track.is_valid()) {
        pfc::string8 stream_url;
        bool is_stream = is_internet_stream_track(track, &stream_url);
        if (is_stream) {
            g_current_stream_url = stream_url;
            bool force_acrcloud = has_url_flag(stream_url.c_str(), "forceacr", track);
            if (force_acrcloud) {
                titleformat_provider::set_status("ACRCloud: Listening for audio stream...");
                start_rms_silence_detector(stream_url);
            } else {
                auto apis = get_api_search_order();
                pfc::string8 first_api_name = "online APIs";
                if (!apis.empty()) {
                    switch (apis[0]) {
                        case ApiType::iTunes: first_api_name = "iTunes"; break;
                        case ApiType::Deezer: first_api_name = "Deezer"; break;
                        case ApiType::LastFm: first_api_name = "Last.fm"; break;
                        case ApiType::MusicBrainz: first_api_name = "MusicBrainz"; break;
                        case ApiType::Discogs: first_api_name = "Discogs"; break;
                    }
                }
                titleformat_provider::set_status((pfc::string8("Querying ") + first_api_name + "...").c_str());
            }
            start_initial_stream_metadata_monitor(stream_url);
            start_external_stream_api_poller(stream_url);
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
    g_playback_session_track.release();
    g_force_noart = false;
    ++g_search_generation;
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
    g_last_stream_artist_full.reset();
    g_last_stream_title.reset();
    g_pending_external_api_artist.reset();
    g_pending_external_api_title.reset();
    g_last_logged_track_info.reset();
    g_is_youtube_art_track = false;
    g_is_youtube_topic_track = false;
    g_is_youtube_release_topic = false;
    g_youtube_extracted_album.reset();
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

class artwork_manager_playback_callback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop |
            flag_on_playback_dynamic_info_track;
    }
    void on_playback_starting(play_control::t_track_command, bool) override {
        // Also reset when reconnecting to the same URL or replaying the same handle.
        artwork_manager::on_playback_stop();
    }
    void on_playback_new_track(metadb_handle_ptr track) override {
        artwork_manager::on_playback_new_track(track);
    }
    void on_playback_stop(play_control::t_stop_reason) override {
        artwork_manager::on_playback_stop();
    }
    void on_playback_seek(double) override {}
    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info& info) override {
        const char* artist = info.meta_get("ARTIST", 0);
        const char* title = info.meta_get("TITLE", 0);
        if (!title || !*title) title = info.meta_get("STREAMTITLE", 0);
        if (!title || !*title) title = info.meta_get("ICY_TITLE", 0);
        // Keep the cue together; never combine a new artist with the previous title.
        if (title && *title) artwork_manager::on_stream_metadata_changed(artist ? artist : "", title);
    }
    void on_playback_time(double) override {}
    void on_volume_change(float) override {}
};
static play_callback_static_factory_t<artwork_manager_playback_callback> g_artwork_manager_playback_callback;

void artwork_manager::get_artwork_async(metadb_handle_ptr track, artwork_callback callback) {
    ASSERT_MAIN_THREAD();
    if (track.is_valid() && g_playback_session_track != track) on_playback_new_track(track);
    
    // A forced placeholder cancels requests; a failure callback would start logo fallbacks.
    if (g_force_noart) return;
    if (!track.is_valid()) {
        callback(artwork_result());
        return;
    }

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
    metadb_handle_ptr now_playing;
    if (playback_control::get()->get_now_playing(now_playing) && now_playing.is_valid() &&
        g_playback_session_track != now_playing) on_playback_new_track(now_playing);
    if (g_force_noart) on_stream_metadata_changed(artist, track);
    // A forced placeholder cancels requests; a failure callback would start logo fallbacks.
    if (g_force_noart) return;

    // Metadata bridge requests can also come from local-file playback. Keep the
    // track handle so embedded/folder artwork is checked before online providers,
    // including when single-file caching intentionally bypasses cache reads.
    if (now_playing.is_valid() && !is_internet_stream_track(now_playing)) {
        get_artwork_async(now_playing, callback);
        return;
    }
    
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
                    const pfc::string8 display_artist = !res.artist.is_empty() ? res.artist : artist_str;
                    const pfc::string8 display_title = !res.title.is_empty() ? res.title : track_str;
                    titleformat_provider::set_track_artwork_info(now_track, display_artist.c_str(), display_title.c_str(),
                        cache_file.c_str(), effective_source.c_str(), display_artist.c_str(), res.album.c_str());
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

        // YouTube decoder artwork and cached thumbnails must not bypass API policy.
        if (is_youtube) {
            search_apis_async(artist_str, track_str, cache_key, callback);
            return;
        }

        // Explicit acoustic recognition takes precedence over cached tag matches
        // and broadcast logos, just as it does in the track-based pipeline.
        if (has_url_flag(g_current_stream_url.c_str(), "forceacr", now_playing)) {
            search_acrcloud_fallback_async(cache_key, callback);
            return;
        }

        // Extract broadcast artwork if currently playing an internet stream
        pfc::string8 broadcast_art_url;
        if (is_stream) {
            broadcast_art_url = extract_broadcast_artwork_url();
        }
        bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
            (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());
        if (try_broadcast_artwork) {
            foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
        }

        // In single-file cache mode, skip cache reads (key is always "current" so it would
        // return the previous track's artwork). Go directly to broadcast -> API search, still write to cache.
        if (cfg_single_file_cache) {
            if (try_broadcast_artwork) {
                search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist_str, track_str, cache_key, callback](const artwork_result& res) {
                    if (res.success) {
                        callback(res);
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

static int get_encoder_clsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    std::vector<BYTE> memory(size);
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)memory.data();
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return j;
        }
    }
    return -1;
}

bool artwork_manager::crop_image_to_square_jpeg(const t_uint8* in_data, size_t in_size, pfc::array_t<t_uint8>& out_data) {
    if (!in_data || in_size == 0) return false;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, in_size);
    if (!hMem) return false;
    void* pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return false;
    }
    memcpy(pMem, in_data, in_size);
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK || !pStream) {
        GlobalFree(hMem);
        return false;
    }

    CComPtr<IStream> source_stream;
    source_stream.Attach(pStream);
    std::unique_ptr<Gdiplus::Bitmap> src_bmp(Gdiplus::Bitmap::FromStream(source_stream));

    if (!src_bmp || src_bmp->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    UINT w = src_bmp->GetWidth();
    UINT h = src_bmp->GetHeight();
    if (w == 0 || h == 0) return false;

    // If already square (1:1), keep original data
    if (w == h) {
        out_data.set_size(in_size);
        memcpy(out_data.get_ptr(), in_data, in_size);
        return true;
    }

    UINT crop_size = (w > h) ? h : w;
    if (w == 480 && h == 360) crop_size = 270; // YouTube hqdefault letterboxing
    INT crop_x = (INT)((w - crop_size) / 2);
    INT crop_y = (INT)((h - crop_size) / 2);

    Gdiplus::Bitmap dst_bmp(crop_size, crop_size, PixelFormat24bppRGB);
    {
        Gdiplus::Graphics g(&dst_bmp);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        if (g.DrawImage(src_bmp.get(), Gdiplus::Rect(0, 0, crop_size, crop_size),
                        crop_x, crop_y, crop_size, crop_size, Gdiplus::UnitPixel) != Gdiplus::Ok) return false;
    }

    CLSID clsid_jpeg;
    if (get_encoder_clsid(L"image/jpeg", &clsid_jpeg) < 0) {
        return false;
    }

    IStream* pOutStream = nullptr;
    if (CreateStreamOnHGlobal(NULL, TRUE, &pOutStream) != S_OK || !pOutStream) {
        return false;
    }

    ULONG quality = 92;
    Gdiplus::EncoderParameters encoderParameters;
    encoderParameters.Count = 1;
    encoderParameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encoderParameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encoderParameters.Parameter[0].NumberOfValues = 1;
    encoderParameters.Parameter[0].Value = &quality;

    if (dst_bmp.Save(pOutStream, &clsid_jpeg, &encoderParameters) != Gdiplus::Ok) {
        pOutStream->Release();
        return false;
    }

    STATSTG stat;
    if (pOutStream->Stat(&stat, STATFLAG_NONAME) != S_OK) {
        pOutStream->Release();
        return false;
    }

    ULONG out_size = (ULONG)stat.cbSize.QuadPart;
    out_data.set_size(out_size);

    LARGE_INTEGER seek_pos;
    seek_pos.QuadPart = 0;
    pOutStream->Seek(seek_pos, STREAM_SEEK_SET, NULL);

    ULONG bytes_read = 0;
    pOutStream->Read(out_data.get_ptr(), out_size, &bytes_read);
    pOutStream->Release();

    return (bytes_read == out_size && out_size > 0);
}

void artwork_manager::search_youtube_thumbnail_async(const pfc::string8& video_id, const pfc::string8& cache_key, artwork_callback callback, bool crop_to_square) {
    if (video_id.is_empty()) {
        artwork_result fail;
        fail.error_message = "Invalid YouTube video ID";
        callback(fail);
        return;
    }

    const bool should_crop = crop_to_square || g_is_youtube_art_track || g_is_youtube_topic_track;
    const uint64_t generation = g_search_generation.load();
    const metadb_handle_ptr track = g_active_playing_track;
    const pfc::string8 artist = !g_last_stream_artist_full.is_empty() ? g_last_stream_artist_full : g_last_stream_artist;
    const pfc::string8 title = g_last_stream_title;
    const pfc::string8 album = g_youtube_extracted_album;
    foo_artwork::log_printf("foo_artwork: Fetching YouTube thumbnail '%s' (crop_to_square=%s)",
                            video_id.c_str(), should_crop ? "true" : "false");
    titleformat_provider::set_status("Fetching YouTube thumbnail...");

    auto prepare_thumbnail = [should_crop, artist, title, album](const artwork_result& downloaded) {
        artwork_result result = downloaded;
        if (!result.success || result.data.get_size() == 0) {
            result.success = false;
            return result;
        }
        if (should_crop) {
            pfc::array_t<t_uint8> cropped;
            if (!crop_image_to_square_jpeg(result.data.get_ptr(), result.data.get_size(), cropped)) {
                result.success = false;
                result.error_message = "Failed to crop YouTube Topic thumbnail";
                return result;
            }
            result.data = cropped;
        }
        result.mime_type = detect_mime_type(result.data.get_ptr(), result.data.get_size());
        result.source = "YouTube Thumbnail";
        result.artist = artist;
        result.title = title;
        result.album = album;
        return result;
    };
    auto complete = [cache_key, callback, track, generation](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
        if (result.success) {
            g_active_resolved_provider = result.source;
            g_active_source = result.source;
            if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                async_io_manager::instance().cache_set_async(cache_key, result.data);
                async_io_manager::instance().cache_set_metadata(cache_key, result.artist, result.title, result.album, result.source);
            }
            if (cfg_single_file_cache) {
                async_io_manager::instance().cache_set_async("current", result.data);
                async_io_manager::instance().cache_set_metadata("current", result.artist, result.title, result.album, result.source);
            }
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            titleformat_provider::set_track_artwork_info(track, result.artist.c_str(), result.title.c_str(),
                cache_file.c_str(), result.source.c_str(), result.artist.c_str(), result.album.c_str());
        }
        // Let the caller render the result, after ACR metadata has been merged.
        callback(result);
    };

    pfc::string8 maxres_url = pfc::string8("https://img.youtube.com/vi/") + video_id + "/maxresdefault.jpg";
    download_image_async(maxres_url.c_str(), [video_id, prepare_thumbnail, complete, generation](const artwork_result& res) {
        if (generation != g_search_generation.load()) return;
        artwork_result result = prepare_thumbnail(res);
        if (result.success && res.data.get_size() > 1024) {
            complete(result);
            return;
        }
        pfc::string8 hq_url = pfc::string8("https://img.youtube.com/vi/") + video_id + "/hqdefault.jpg";
        download_image_async(hq_url.c_str(), [prepare_thumbnail, complete](const artwork_result& res) {
            complete(prepare_thumbnail(res));
        });
    });
}
static std::string normalize_url_param_delimiters(const char* url) {
    if (!url || url[0] == '\0') return "";
    std::string s(url);

    auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    };

    // Decode XML entities and percent-encoded parameter separators/values
    replace_all(s, "&amp;", "&");
    replace_all(s, "%26", "&");
    replace_all(s, "%3b", ";");
    replace_all(s, "%3B", ";");
    replace_all(s, "%3d", "=");
    replace_all(s, "%3D", "=");
    replace_all(s, "%3f", "?");
    replace_all(s, "%3F", "?");
    replace_all(s, "%23", "#");

    // Standardize query/fragment parameter separators: convert ';' occurring after '?' or '#' to '&'
    size_t q_pos = s.find_first_of("?#");
    if (q_pos != std::string::npos) {
        for (size_t i = q_pos + 1; i < s.length(); ++i) {
            if (s[i] == ';') {
                s[i] = '&';
            }
        }
    } else {
        // If there is no '?' or '#', check if parameters are appended with ';' e.g. "stream;ext_api_autoprobe&rejectstationcovers"
        size_t semi = s.find(';');
        if (semi != std::string::npos) {
            s[semi] = '?';
            for (size_t i = semi + 1; i < s.length(); ++i) {
                if (s[i] == ';') {
                    s[i] = '&';
                }
            }
        }
    }

    return s;
}

static bool check_url_string_has_flag(const char* url, const char* flag) {
    if (!url || !flag || url[0] == '\0' || flag[0] == '\0') return false;
    std::string u = normalize_url_param_delimiters(url);
    std::string f(flag);
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const char* prefixes[] = { "?", "&", "#", ";" };
    for (const char* p : prefixes) {
        std::string pattern = std::string(p) + f;
        size_t pos = 0;
        while ((pos = u.find(pattern, pos)) != std::string::npos) {
            size_t next_char_idx = pos + pattern.length();
            if (next_char_idx >= u.length()) {
                return true;
            }
            char next_c = u[next_char_idx];
            if (next_c == '&' || next_c == '#' || next_c == ';' || next_c == '/' || next_c == '?' ||
                next_c == ' ' || next_c == '\t' || next_c == '\r' || next_c == '\n' ||
                next_c == '"' || next_c == '\'' || next_c == '>' || next_c == ')' || next_c == ']') {
                return true;
            }
            if (next_c == '=') {
                size_t val_end = u.find_first_of("&#;/ \t\r\n\"'>)]", next_char_idx + 1);
                std::string val = (val_end == std::string::npos) ? u.substr(next_char_idx + 1) : u.substr(next_char_idx + 1, val_end - (next_char_idx + 1));
                if (val.empty() || val == "1" || val == "true" || val == "yes" || val == "on") {
                    return true;
                }
            }
            pos += pattern.length();
        }
    }
    return false;
}

static pfc::string8 extract_param_value_from_url_string(const char* url, const char* param_name) {
    if (!url || !param_name || url[0] == '\0' || param_name[0] == '\0') return "";
    std::string u = normalize_url_param_delimiters(url);
    std::string p(param_name);
    std::string u_lower = u;
    std::transform(u_lower.begin(), u_lower.end(), u_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string p_lower = p;
    std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const char* prefixes[] = { "?", "&", "#", ";" };
    size_t pos = std::string::npos;
    size_t offset = 0;
    for (const char* pref : prefixes) {
        std::string search = std::string(pref) + p_lower + "=";
        pos = u_lower.find(search);
        if (pos != std::string::npos) {
            offset = search.length();
            break;
        }
    }

    if (pos == std::string::npos) {
        if (check_url_string_has_flag(url, param_name)) {
            return "1";
        }
        return "";
    }

    size_t start = pos + offset;
    size_t end = u.find_first_of("&#;/ \t\r\n\"'>)]", start);
    std::string val = (end == std::string::npos) ? u.substr(start) : u.substr(start, end - start);
    return val.c_str();
}

// Local filesystem protocols can contain :// too (notably portable file-relative paths).
static bool is_remote_artwork_path(const char* path) {
    if (!path || !*path) return false;
    if (_strnicmp(path, "file://", 7) == 0 ||
        _strnicmp(path, "file-relative://", 16) == 0) return false;
    if (!strstr(path, "://")) return false;
    try {
        return filesystem::g_is_remote(path);
    } catch (...) {
        // Decoder-owned protocols such as fy:// may not have a filesystem handler.
        return true;
    }
}

bool artwork_manager::is_internet_stream_track(metadb_handle_ptr track, pfc::string8* out_stream_url) {
    if (out_stream_url) out_stream_url->reset();
    if (!track.is_valid()) return false;

    try {
        pfc::string8 path = track->get_path();
        if (path.is_empty()) return false;

        // Ask the owning filesystem rather than assuming every non-file:// URI
        // is remote. This includes YouTube decoder protocols via the fallback.
        if (is_remote_artwork_path(path.c_str())) {
            if (out_stream_url) *out_stream_url = path;
            return true;
        }

        // 3. m-TAGS / .tags sidecar file or tracks with @ metadata target URL
        metadb_info_container::ptr info_container = track->get_info_ref();
        if (info_container.is_valid()) {
            const file_info& info = info_container->info();
            const char* at_meta = info.meta_get("@", 0);
            if (at_meta && at_meta[0] != '\0') {
                if (is_remote_artwork_path(at_meta)) {
                    if (out_stream_url) *out_stream_url = at_meta;
                    return true;
                }
                return false;
            }
        }

        // 4. m-TAGS files with length <= 0 (live stream proxy without duration)
        const double length = track->get_length();
        if (strstr(path.c_str(), ".tags") && length <= 0) {
            if (out_stream_url && out_stream_url->is_empty()) {
                *out_stream_url = path;
            }
            return true;
        }
    } catch (...) {}

    return false;
}

bool artwork_manager::has_url_flag(const char* url, const char* flag, metadb_handle_ptr track) {
    if (!flag || flag[0] == '\0') return false;

    // 1. Check raw URL string if provided
    if (url && url[0] != '\0' && check_url_string_has_flag(url, flag)) {
        return true;
    }

    // A proxy path can carry modifiers separately from its underlying @ URL.
    if (track.is_valid() && check_url_string_has_flag(track->get_path(), flag)) return true;

    // Only inherit the active connection's flags for that connection.
    if ((!track.is_valid() || track == g_active_playing_track) &&
        !g_current_stream_url.is_empty() && (!url || strcmp(url, g_current_stream_url.c_str()) != 0)) {
        if (check_url_string_has_flag(g_current_stream_url.c_str(), flag)) {
            return true;
        }
    }

    // 3. Check m-tags and custom tag fields via track or playback control
    metadb_handle_ptr t = track.is_valid() ? track : g_active_playing_track;
    if (!t.is_valid() && core_api::is_main_thread()) {
        playback_control::get()->get_now_playing(t);
    }

    if (t.is_valid()) {
        try {
            metadb_info_container::ptr info_container = t->get_info_ref();
            if (info_container.is_valid()) {
                const file_info& info = info_container->info();

                // Check @ metadata tag (underlying target stream URL in .tags / m-tags files)
                const char* at_meta = info.meta_get("@", 0);
                if (at_meta && at_meta[0] != '\0') {
                    if (check_url_string_has_flag(at_meta, flag)) {
                        return true;
                    }
                }

                // Check custom tag fields directly (case-insensitive)
                std::string f_low(flag);
                std::transform(f_low.begin(), f_low.end(), f_low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::string f_up(flag);
                std::transform(f_up.begin(), f_up.end(), f_up.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

                const char* tag_val = info.meta_get(f_low.c_str(), 0);
                if (!tag_val) tag_val = info.meta_get(f_up.c_str(), 0);
                if (!tag_val) tag_val = info.meta_get(flag, 0);

                if (tag_val && tag_val[0] != '\0') {
                    std::string v(tag_val);
                    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (v != "0" && v != "false" && v != "no" && v != "off" && v != "disabled") {
                        return true;
                    }
                }
            }
        } catch (...) {}
    }

    return false;
}

pfc::string8 artwork_manager::get_url_param_value(const char* url, const char* param_name, metadb_handle_ptr track) {
    if (!param_name || param_name[0] == '\0') return "";

    // 1. Check raw URL string if provided
    if (url && url[0] != '\0') {
        pfc::string8 val = extract_param_value_from_url_string(url, param_name);
        if (!val.is_empty()) return val;
    }

    // 2. Check current stream URL if different from url
    if (!g_current_stream_url.is_empty() && (!url || strcmp(url, g_current_stream_url.c_str()) != 0)) {
        pfc::string8 val = extract_param_value_from_url_string(g_current_stream_url.c_str(), param_name);
        if (!val.is_empty()) return val;
    }

    // 3. Check m-tags and custom tag fields via track or playback control
    metadb_handle_ptr t = track.is_valid() ? track : g_active_playing_track;
    if (!t.is_valid() && core_api::is_main_thread()) {
        playback_control::get()->get_now_playing(t);
    }

    if (t.is_valid()) {
        try {
            metadb_info_container::ptr info_container = t->get_info_ref();
            if (info_container.is_valid()) {
                const file_info& info = info_container->info();

                // Check custom tag fields directly (case-insensitive)
                std::string p_low(param_name);
                std::transform(p_low.begin(), p_low.end(), p_low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::string p_up(param_name);
                std::transform(p_up.begin(), p_up.end(), p_up.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

                const char* tag_val = info.meta_get(p_low.c_str(), 0);
                if (!tag_val) tag_val = info.meta_get(p_up.c_str(), 0);
                if (!tag_val) tag_val = info.meta_get(param_name, 0);

                if (tag_val && tag_val[0] != '\0') {
                    return tag_val;
                }

                // Check @ metadata tag (underlying target stream URL in .tags / m-tags files)
                const char* at_meta = info.meta_get("@", 0);
                if (at_meta && at_meta[0] != '\0') {
                    pfc::string8 at_val = extract_param_value_from_url_string(at_meta, param_name);
                    if (!at_val.is_empty()) return at_val;
                }
            }
        } catch (...) {}
    }

    return "";
}

int artwork_manager::extract_coversync_seconds(metadb_handle_ptr track) {
    pfc::string8 sync_val = artwork_manager::get_url_param_value(g_current_stream_url.c_str(), "coversync", track);
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

    metadb_handle_ptr active_track = g_active_playing_track;
    bool reject_station = artwork_manager::has_url_flag(g_current_stream_url.c_str(), "rejectstationcovers", active_track);

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
        if (reject_station) {
            std::string fl(field);
            std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (fl.find("station") != std::string::npos || fl.find("logo") != std::string::npos || 
                fl.find("radio_cover") != std::string::npos || fl.find("stream_cover") != std::string::npos) {
                continue;
            }
        }
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
        if (reject_station) {
            std::string tl(tag);
            std::transform(tl.begin(), tl.end(), tl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (tl.find("station") != std::string::npos || tl.find("logo") != std::string::npos || 
                tl.find("radio_cover") != std::string::npos || tl.find("stream_cover") != std::string::npos) {
                continue;
            }
        }
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

pfc::string8 artwork_manager::extract_broadcast_album_from_info(const file_info& info) {
    const char* album_fields[] = {
        "album", "ALBUM", "album_name", "ALBUM_NAME", "album_title", "ALBUM_TITLE",
        "icy-album", "ICY-ALBUM", "song_album", "SONG_ALBUM", "radio_album", "RADIO_ALBUM"
    };
    for (const char* field : album_fields) {
        const char* val = info.info_get(field);
        if (val && strlen(val) > 0) {
            return val;
        }
        val = info.meta_get(field, 0);
        if (val && strlen(val) > 0) {
            return val;
        }
    }

    t_size info_cnt = info.info_get_count();
    for (t_size i = 0; i < info_cnt; ++i) {
        const char* name = info.info_enum_name(i);
        const char* val = info.info_enum_value(i);
        if (name && val && strlen(val) > 0) {
            std::string n(name);
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (n.find("url") != std::string::npos || n.find("art") != std::string::npos || n.find("cover") != std::string::npos) continue;
            if (n == "album" || n == "album_title" || n == "album_name" || n == "icy-album") {
                return val;
            }
        }
    }
    return "";
}

pfc::string8 artwork_manager::extract_broadcast_album(metadb_handle_ptr track) {
    if (!track.is_valid()) track = g_active_playing_track;
    if (track.is_valid()) {
        try {
            metadb_info_container::ptr info_container = track->get_info_ref();
            if (info_container.is_valid()) {
                pfc::string8 alb = extract_broadcast_album_from_info(info_container->info());
                if (!alb.is_empty()) return alb;
            }
        } catch (...) {}
    }

    try {
        static_api_ptr_t<playback_control> pc;
        if (pc->is_playing() || pc->is_paused()) {
            static_api_ptr_t<titleformat_compiler> compiler;
            const char* tf_patterns[] = {
                "$info(album)", "$info(ALBUM)", "$info(album_name)", "$info(album_title)", "$info(icy-album)", "%album%"
            };
            for (const char* pat : tf_patterns) {
                service_ptr_t<titleformat_object> script;
                compiler->compile_force(script, pat);
                pfc::string8 formatted;
                if (pc->playback_format_title(nullptr, formatted, script, nullptr, playback_control::display_level_all)) {
                    if (!formatted.is_empty() && formatted != "?") {
                        return formatted;
                    }
                }
            }
        }
    } catch (...) {}

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

    const uint64_t generation = g_search_generation.load();
    download_image_async(clean_url.c_str(), [clean_url, cache_key, callback, generation](const artwork_result& res) {
        if (generation != g_search_generation.load() || g_force_noart) return;
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
            pfc::string8 bc_art = !g_last_stream_artist_full.is_empty() ? g_last_stream_artist_full : g_last_stream_artist;
            titleformat_provider::set_track_artwork_info(now_track, bc_art.c_str(), g_last_stream_title.c_str(), cache_file.c_str(), "Broadcast Artwork", bc_art.c_str());
            std::vector<uint8_t> vec(final_res.data.get_ptr(), final_res.data.get_ptr() + final_res.data.get_size());
            g_current_artwork_source = "Broadcast Artwork";
            if (g_manual_search_generation != generation) create_bitmap_from_image_data(vec);
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
    size_t q = s.find_first_of("?#;&");
    if (q != std::string::npos) s = s.substr(0, q);

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
        std::transform(seg_lower.begin(), seg_lower.end(), seg_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
    size_t q_pos = stream_lower.find_first_of("?#;&");
    if (q_pos != std::string::npos) stream_lower = stream_lower.substr(0, q_pos);
    std::transform(stream_lower.begin(), stream_lower.end(), stream_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

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
                            std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                            if (!path.empty()) {
                                if (stream_path == path || stream_path.find(path) != std::string::npos || path.find(stream_path) != std::string::npos) {
                                    return &item;
                                }
                            }
                        }
                        if (m.contains("url") && m["url"].is_string()) {
                            std::string m_url = m["url"].get<std::string>();
                            std::transform(m_url.begin(), m_url.end(), m_url.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
                std::transform(hls.begin(), hls.end(), hls.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
    std::transform(slug.begin(), slug.end(), slug.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& item : j_array) {
        if (!item.is_object()) continue;

        if (item.contains("station") && item["station"].is_object()) {
            const auto& st = item["station"];

            if (st.contains("shortcode") && st["shortcode"].is_string()) {
                std::string sc = st["shortcode"].get<std::string>();
                std::transform(sc.begin(), sc.end(), sc.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
    g_manual_external_probe = false;
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
            metadb_handle_ptr now_track = g_active_playing_track;
            if (!now_track.is_valid() && core_api::is_main_thread()) {
                playback_control::get()->get_now_playing(now_track);
            }
            bool force_autoprobe = g_manual_external_probe || artwork_manager::has_url_flag(stream_url.c_str(), "ext_api_autoprobe", now_track);
            if (!force_autoprobe) {
                // Guard: Check if native ICY metadata has already arrived on the stream
                metadb_handle_ptr track = now_track;
                if (!track.is_valid() && playback_control::get()->get_now_playing(track) && track.is_valid()) {
                    // track retrieved
                }
                if (track.is_valid()) {
                    pfc::string8 art, tit;
                    extract_track_metadata_dynamic(track, art, tit);
                    StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
                    if (meta.is_valid_search && !meta.is_station_or_url) {
                        // Native ICY metadata is active on the stream - do not launch auto-probed poller
                        return;
                    }
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

    // If stream_url is a .tags file or file:// path, resolve underlying HTTP/HTTPS URL from @ tag if available
    if (s.find("file://") == 0 || s.rfind(".tags") != std::string::npos) {
        metadb_handle_ptr track = g_active_playing_track;
        if (!track.is_valid() && core_api::is_main_thread()) {
            playback_control::get()->get_now_playing(track);
        }
        if (track.is_valid()) {
            try {
                metadb_info_container::ptr info_container = track->get_info_ref();
                if (info_container.is_valid()) {
                    const char* at_meta = info_container->info().meta_get("@", 0);
                    if (at_meta && at_meta[0] != '\0') {
                        s = at_meta;
                    }
                }
            } catch (...) {}
        }
    }

    size_t proto = s.find("://");
    if (proto == std::string::npos || s.find("file://") == 0) return;

    size_t host_end = s.find_first_of("/?#;", proto + 3);
    std::string base = (host_end != std::string::npos) ? s.substr(0, host_end) : s;
    if (base.empty()) return;

    metadb_handle_ptr now_track = g_active_playing_track;
    if (!now_track.is_valid() && core_api::is_main_thread()) {
        playback_control::get()->get_now_playing(now_track);
    }
    bool force_autoprobe = g_manual_external_probe || has_url_flag(stream_url.c_str(), "ext_api_autoprobe", now_track);

    // Check probe cache
    {
        std::lock_guard<std::mutex> lock(g_probe_cache_mutex);
        auto it = g_probed_stream_endpoints_cache.find(stream_url.c_str());
        if (it != g_probed_stream_endpoints_cache.end()) {
            if (it->second.first == StreamProbeStatus::SUCCESS) {
                // If stream already has metadata, do not poll cached endpoint unless forced by ext_api_autoprobe
                if (!force_autoprobe) {
                    metadb_handle_ptr track;
                    if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
                        pfc::string8 art, tit;
                        extract_track_metadata_dynamic(track, art, tit);
                        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
                        if (meta.is_valid_search && !meta.is_station_or_url) {
                            return;
                        }
                    }
                }
                pfc::string8 endpoint = it->second.second;
                foo_artwork::log_printf("foo_artwork: Using cached external now-playing endpoint: %s", endpoint.c_str());
                poll_external_stream_api(endpoint, session_token);
                return;
            } else if (it->second.first == StreamProbeStatus::FAILED && !g_manual_external_probe) {
                return; // Probed previously and not an external API endpoint
            }
        }
    }

    pfc::string8 slug = extract_station_slug_from_url(s.c_str());

    // Extract numeric segment / sub-id if present
    std::string sub_id;
    size_t q = s.find_first_of("?#;&");
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
    const bool manual_probe = g_manual_external_probe;
    stop_external_stream_api_poller();
    g_manual_external_probe = manual_probe;

    metadb_handle_ptr now_playing = g_active_playing_track;
    if (!now_playing.is_valid() && core_api::is_main_thread()) {
        playback_control::get()->get_now_playing(now_playing);
    }

    if (has_url_flag(stream_url.c_str(), "forceacr", now_playing)) return;

    pfc::string8 azuracast_val = get_url_param_value(stream_url.c_str(), "azuracast_api", now_playing);
    pfc::string8 radioreg_val = get_url_param_value(stream_url.c_str(), "radioreg_api", now_playing);

    // Also check metadb tags (e.g. from foo_external_tags)
    if (azuracast_val.is_empty() && radioreg_val.is_empty() && now_playing.is_valid()) {
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

    bool is_azura = !azuracast_val.is_empty() || contains_case_insensitive(stream_url.c_str(), "/api/nowplaying");
    bool is_radioreg = !radioreg_val.is_empty() || contains_case_insensitive(stream_url.c_str(), "radioreg.net");

    uint64_t current_token = ++g_external_api_session_token;

    if (!is_azura && !is_radioreg) {
        bool force_autoprobe = g_manual_external_probe || has_url_flag(stream_url.c_str(), "ext_api_autoprobe", now_playing);
        if (cfg_disable_ext_api_autoprobe && !force_autoprobe) {
            // Auto-probing on radio connection disabled in Preferences (can still be triggered manually or via ?ext_api_autoprobe)
            return;
        }
        if (!force_autoprobe) {
            // Check if stream metadata is already valid and not a placeholder/station URL
            metadb_handle_ptr track = now_playing;
            if (!track.is_valid() && playback_control::get()->get_now_playing(track) && track.is_valid()) {
                // track retrieved
            }
            if (track.is_valid()) {
                pfc::string8 art, tit;
                extract_track_metadata_dynamic(track, art, tit);
                StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(art.c_str(), tit.c_str());
                if (meta.is_valid_search && !meta.is_station_or_url) {
                    // Native ICY stream metadata exists - do not auto-probe external server endpoints
                    return;
                }
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
                size_t host_end = s.find_first_of("/?#;", proto + 3);
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
                    double track_duration = 0.0;
                    double track_elapsed = 0.0;

                    if (j.is_object() && j.contains("now_playing") && j["now_playing"].is_object()) {
                        auto& np = j["now_playing"];
                        if (np.contains("duration") && np["duration"].is_number()) track_duration = np["duration"].get<double>();
                        if (np.contains("elapsed") && np["elapsed"].is_number()) track_elapsed = np["elapsed"].get<double>();

                        if (np.contains("song") && np["song"].is_object()) {
                            auto& song = np["song"];
                            if (song.contains("artist") && song["artist"].is_string()) artist = song["artist"].get<std::string>();
                            if (song.contains("title") && song["title"].is_string()) title = song["title"].get<std::string>();
                            if (song.contains("album") && song["album"].is_string()) album = song["album"].get<std::string>();
                            if (song.contains("art") && song["art"].is_string()) art_url = song["art"].get<std::string>();
                            if (track_duration <= 0.0 && song.contains("duration") && song["duration"].is_number()) {
                                track_duration = song["duration"].get<double>();
                            }
                            if (track_elapsed <= 0.0 && song.contains("elapsed") && song["elapsed"].is_number()) {
                                track_elapsed = song["elapsed"].get<double>();
                            }
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
                                if (np.contains("duration") && np["duration"].is_number()) track_duration = np["duration"].get<double>();
                                if (np.contains("elapsed") && np["elapsed"].is_number()) track_elapsed = np["elapsed"].get<double>();

                                if (np.contains("song") && np["song"].is_object()) {
                                    auto& song = np["song"];
                                    if (song.contains("artist") && song["artist"].is_string()) artist = song["artist"].get<std::string>();
                                    if (song.contains("title") && song["title"].is_string()) title = song["title"].get<std::string>();
                                    if (song.contains("album") && song["album"].is_string()) album = song["album"].get<std::string>();
                                    if (song.contains("art") && song["art"].is_string()) art_url = song["art"].get<std::string>();
                                    if (track_duration <= 0.0 && song.contains("duration") && song["duration"].is_number()) {
                                        track_duration = song["duration"].get<double>();
                                    }
                                    if (track_elapsed <= 0.0 && song.contains("elapsed") && song["elapsed"].is_number()) {
                                        track_elapsed = song["elapsed"].get<double>();
                                    }
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
                                if (track_duration <= 0.0 && song.contains("duration") && song["duration"].is_number()) {
                                    track_duration = song["duration"].get<double>();
                                }
                                if (track_elapsed <= 0.0 && song.contains("elapsed") && song["elapsed"].is_number()) {
                                    track_elapsed = song["elapsed"].get<double>();
                                }
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
                        if (j.contains("duration") && j["duration"].is_number()) track_duration = j["duration"].get<double>();
                        if (j.contains("elapsed") && j["elapsed"].is_number()) track_elapsed = j["elapsed"].get<double>();

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
                            if (track_duration <= 0.0 && song.contains("duration") && song["duration"].is_number()) {
                                track_duration = song["duration"].get<double>();
                            }
                            if (track_elapsed <= 0.0 && song.contains("elapsed") && song["elapsed"].is_number()) {
                                track_elapsed = song["elapsed"].get<double>();
                            }
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
                        metadb_handle_ptr active_track;
                        if (pc_resp->get_now_playing(active_track) && active_track.is_valid()) {
                            // active_track found
                        } else {
                            active_track = g_active_playing_track;
                        }

                        if (artwork_manager::has_url_flag(g_current_stream_url.c_str(), "inverted", active_track)) {
                            std::swap(artist, title);
                        }

                        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), title.c_str());
                        pfc::string8 clean_art = meta.first_artist.c_str();
                        pfc::string8 clean_tit = meta.clean_title.c_str();
                        pfc::string8 clean_art_full = meta.clean_artist.c_str();
                        if (album.empty() && !meta.clean_album.empty()) {
                            album = meta.clean_album;
                        }

                        // Case-insensitive comparison against last stream track and pending cue
                        bool is_different_from_last = (pfc::stricmp_ascii(clean_art.c_str(), g_last_stream_artist.c_str()) != 0 ||
                                                       pfc::stricmp_ascii(clean_tit.c_str(), g_last_stream_title.c_str()) != 0);
                        bool is_different_from_pending = (pfc::stricmp_ascii(clean_art.c_str(), g_pending_external_api_artist.c_str()) != 0 ||
                                                           pfc::stricmp_ascii(clean_tit.c_str(), g_pending_external_api_title.c_str()) != 0);

                        if (is_different_from_last && is_different_from_pending) {
                            auto apply_cue = [session_token](std::string artist_c, std::string title_c, std::string art_url_c, std::string album_c, std::string listeners_c, double dur_c, double elap_c, int applied_sync_delay) {
                                ASSERT_MAIN_THREAD();
                                if (g_is_shutting_down.load() || g_external_api_session_token != session_token) return;
                                static_api_ptr_t<playback_control> pc_cue;
                                if (!pc_cue->is_playing() && !pc_cue->is_paused()) return;

                                g_pending_external_api_artist.reset();
                                g_pending_external_api_title.reset();

                                foo_artwork::log_printf("foo_artwork: External API cue - Track changed: '%s - %s'", artist_c.c_str(), title_c.c_str());
                                g_force_noart = false;
                                ++g_search_generation;
                                g_rejected_providers_for_current_track.clear();
                                g_active_resolved_provider.reset();

                                titleformat_provider::reset_stream_track_timer(applied_sync_delay);
                                if (dur_c > 0.0) {
                                    titleformat_provider::set_stream_track_timing(dur_c, elap_c + applied_sync_delay);
                                }

                                StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist_c.c_str(), title_c.c_str());
                                pfc::string8 clean_art = meta.first_artist.c_str();
                                pfc::string8 clean_tit = meta.clean_title.c_str();
                                pfc::string8 clean_art_full = meta.clean_artist.c_str();
                                if (album_c.empty() && !meta.clean_album.empty()) {
                                    album_c = meta.clean_album;
                                }

                                reset_acrcloud_cooldown();
                                remember_search_metadata(clean_art, clean_tit, cfg_single_file_cache ? pfc::string8("current") :
                                    generate_cache_key(clean_art.c_str(), clean_tit.c_str()));

                                metadb_handle_ptr track;
                                if (pc_cue->get_now_playing(track) && track.is_valid()) {
                                    titleformat_provider::set_track_artwork_info(track, clean_art_full.c_str(), clean_tit.c_str(), "", "", clean_art_full.c_str(), album_c.c_str(), listeners_c.c_str());
                                }

                                metadb_handle_ptr cur_t;
                                if (pc_cue->get_now_playing(cur_t) && cur_t.is_valid()) {
                                    // cur_t retrieved
                                } else {
                                    cur_t = g_active_playing_track;
                                }
                                bool reject_station = artwork_manager::has_url_flag(g_current_stream_url.c_str(), "rejectstationcovers", cur_t);

                                if (!reject_station && !cfg_disable_instream_artwork && !art_url_c.empty() && (art_url_c.find("http://") == 0 || art_url_c.find("https://") == 0) &&
                                    (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end())) {
                                    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key(clean_art.c_str(), clean_tit.c_str());
                                    search_broadcast_artwork_async(art_url_c.c_str(), cache_key, [clean_art, clean_tit, clean_art_full, album_c, listeners_c, cache_key](const artwork_result& res) {
                                        if (res.success) {
                                            g_last_stream_artist = clean_art;
                                            g_last_stream_artist_full = clean_art_full;
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
                                                titleformat_provider::set_track_artwork_info(track, clean_art_full.c_str(), clean_tit.c_str(), cache_file.c_str(), res.source.c_str(), clean_art_full.c_str(), album_c.c_str(), listeners_c.c_str());
                                            }

                                            int w = 0, h = 0;
                                            if (res.data.get_size() > 0) get_image_dimensions_from_data(res.data.get_ptr(), res.data.get_size(), w, h);
                                            titleformat_provider::set_status_artwork_loaded(res.source.c_str(), w, h, (size_t)res.data.get_size(), res.is_acrcloud_recognized);

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
                            bool is_first_cue = !g_has_received_first_stream_cue.exchange(true);
                            int sync_delay_sec = is_first_cue ? 0 : extract_coversync_seconds(cur_track);
                            if (sync_delay_sec > 0) {
                                g_pending_external_api_artist = clean_art.c_str();
                                g_pending_external_api_title = clean_tit.c_str();
                                uint64_t cue_token = ++g_coversync_cue_token;
                                foo_artwork::log_printf("foo_artwork: Applying coversync delay of %d seconds for external API track cue '%s - %s'", 
                                                       sync_delay_sec, clean_art.c_str(), clean_tit.c_str());
                                char status_buf[128];
                                snprintf(status_buf, sizeof(status_buf), "Next cue in %ds: %s - %s", sync_delay_sec, clean_art.c_str(), clean_tit.c_str());
                                titleformat_provider::set_status(status_buf);

                                async_io_manager::instance().submit_task([clean_art, clean_tit, art_url, album, listeners, track_duration, track_elapsed, session_token, cue_token, sync_delay_sec, apply_cue]() {
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

                                    async_io_manager::instance().post_to_main_thread([clean_art, clean_tit, art_url, album, listeners, track_duration, track_elapsed, session_token, cue_token, sync_delay_sec, apply_cue]() {
                                        if (g_is_shutting_down.load() || g_external_api_session_token != session_token || g_coversync_cue_token != cue_token) {
                                            return;
                                        }
                                        apply_cue(clean_art.c_str(), clean_tit.c_str(), art_url, album, listeners, track_duration, track_elapsed, sync_delay_sec);
                                    });
                                });
                            } else {
                                apply_cue(clean_art.c_str(), clean_tit.c_str(), art_url, album, listeners, track_duration, track_elapsed, 0);
                            }
                        } else {
                            if (track_duration > 0.0) {
                                titleformat_provider::set_stream_track_timing(track_duration, track_elapsed);
                            }
                            if (!listeners.empty()) {
                                metadb_handle_ptr cur_track;
                                if (pc_resp->get_now_playing(cur_track) && cur_track.is_valid()) {
                                    pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(generate_cache_key(g_last_stream_artist.c_str(), g_last_stream_title.c_str()));
                                    titleformat_provider::set_track_artwork_info(cur_track, g_last_stream_artist.c_str(), g_last_stream_title.c_str(), cache_file.c_str(), g_active_source.c_str(), "", album.c_str(), listeners.c_str());
                                }
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

static void display_manual_artwork(const artwork_manager::artwork_result& result,
    metadb_handle_ptr track, const pfc::string8& cache_key, uint64_t generation) {
    async_io_manager::instance().post_to_main_thread([result, track, cache_key, generation]() {
        if (g_is_shutting_down.load() || generation != g_search_generation.load() || g_force_noart) return;
        if (!result.success || result.data.get_size() == 0) {
            foo_artwork::log_printf("foo_artwork: Manual artwork search failed: %s", result.error_message.c_str());
            return;
        }
        g_active_resolved_provider = result.source;
        g_active_source = result.source;
        g_current_artwork_source = result.source;
        const pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
        titleformat_provider::set_track_artwork_info(track, result.artist.c_str(), result.title.c_str(),
            cache_file.c_str(), result.source.c_str(), result.artist.c_str(), result.album.c_str());
        std::vector<BYTE> data(result.data.get_ptr(), result.data.get_ptr() + result.data.get_size());
        if (create_bitmap_from_image_data(data)) {
            int width = 0, height = 0;
            get_image_dimensions_from_data(result.data.get_ptr(), result.data.get_size(), width, height);
            titleformat_provider::set_status_artwork_loaded(result.source.c_str(), width, height,
                result.data.get_size(), result.is_acrcloud_recognized);
            refresh_all_dui_artwork_panels();
            refresh_all_cui_artwork_panels();
        }
    });
}

static void get_provider_retry_metadata(metadb_handle_ptr track, pfc::string8& artist, pfc::string8& title) {
    if (g_search_metadata_generation == g_search_generation.load() &&
        !g_search_artist.is_empty() && !g_search_title.is_empty()) {
        artist = g_search_artist;
        title = g_search_title;
        return;
    }
    if (!g_last_recognized_artist.is_empty() && !g_last_recognized_title.is_empty()) {
        artist = g_last_recognized_artist;
        title = g_last_recognized_title;
        return;
    }
    if (!g_last_stream_artist.is_empty() && !g_last_stream_title.is_empty()) {
        artist = g_last_stream_artist;
        title = g_last_stream_title;
        return;
    }

    extract_track_metadata_dynamic(track, artist, title);
    const bool is_youtube = !artwork_manager::extract_youtube_video_id(track->get_path()).is_empty() ||
        !artwork_manager::extract_youtube_video_id(g_current_stream_url.c_str()).is_empty();
    const auto meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), title.c_str(),
        is_youtube, is_youtube && (g_is_youtube_art_track || g_is_youtube_topic_track));
    artist = meta.is_valid_search && !meta.is_station_or_url ? meta.first_artist.c_str() : "";
    title = meta.is_valid_search && !meta.is_station_or_url ? meta.clean_title.c_str() : "";
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

    // Build list of all enabled providers for this track in search order (ACRCloud excluded)
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
    if (!cfg_disable_instream_artwork && !extract_broadcast_artwork_url(track).is_empty()) {
        available_providers.push_back("Broadcast Artwork");
    }
    if (track.is_valid() && (!extract_youtube_video_id(track->get_path()).is_empty() || !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty())) {
        available_providers.push_back("YouTube Thumbnail");
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

    // Reuse the cleaned/recognized query and its actual cache key before resetting
    // recognition state. Raw station tags may differ from the active radio cue.
    pfc::string8 artist, title;
    get_provider_retry_metadata(track, artist, title);
    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") :
        (g_search_metadata_generation == g_search_generation.load() && !g_active_cache_key.is_empty()
            ? g_active_cache_key : generate_cache_key_for_track(track));

    // Invalidate current cache on disk
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

    // Search providers directly: local/disk/recognized artwork must not win again.
    g_force_noart = false;
    reset_acrcloud_cooldown();
    const uint64_t generation = ++g_search_generation;
    g_manual_search_generation = generation;
    remember_search_metadata(artist, title, cache_key);
    search_rejected_artwork_pass(track, artist, title, cache_key, generation, true);
}

void artwork_manager::search_rejected_artwork_pass(metadb_handle_ptr track,
    const pfc::string8& artist, const pfc::string8& title, const pfc::string8& cache_key,
    uint64_t generation, bool allow_wrap) {
    auto deliver = [track, artist, title, cache_key, generation, allow_wrap](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
        if (!result.success && allow_wrap && !g_rejected_providers_for_current_track.empty()) {
            // Failed/empty providers cannot be explicitly rejected by the user.
            // Reaching the end is enough to wrap, but only once per command.
            g_rejected_providers_for_current_track.clear();
            foo_artwork::log_printf("foo_artwork: End of artwork provider chain. Retrying from the first provider...");
            search_rejected_artwork_pass(track, artist, title, cache_key, generation, false);
            return;
        }
        display_manual_artwork(result, track, cache_key, generation);
    };
    pfc::string8 video_id = extract_youtube_video_id(track->get_path());
    if (video_id.is_empty()) video_id = extract_youtube_video_id(g_current_stream_url.c_str());
    const bool crop = g_is_youtube_art_track || g_is_youtube_topic_track;
    auto thumbnail_fallback = [video_id, crop, cache_key, generation, deliver](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
        if (result.success || video_id.is_empty() || g_rejected_providers_for_current_track.count("YouTube Thumbnail")) {
            deliver(result);
        } else {
            search_youtube_thumbnail_async(video_id, cache_key, deliver, crop);
        }
    };
    const pfc::string8 broadcast_url = extract_broadcast_artwork_url(track);
    auto provider_fallback = [broadcast_url, cache_key, generation, thumbnail_fallback](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
        if (!result.success && !broadcast_url.is_empty() && !cfg_disable_instream_artwork &&
            !g_rejected_providers_for_current_track.count("Broadcast Artwork")) {
            search_broadcast_artwork_async(broadcast_url, cache_key, thumbnail_fallback);
        } else {
            thumbnail_fallback(result);
        }
    };
    if (artist.is_empty() || title.is_empty()) {
        provider_fallback(artwork_result());
    } else {
        search_apis_by_priority(artist, title, cache_key, provider_fallback, get_api_search_order(), 0, false);
    }
}

void artwork_manager::force_show_noart() {
    ASSERT_MAIN_THREAD();
    foo_artwork::log_printf("foo_artwork: Force 'No-Art' command triggered by user.");

    metadb_handle_ptr track;
    if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
        g_active_playing_track = track;
    } else {
        track = g_active_playing_track;
    }

    g_force_noart = true;
    ++g_search_generation;
    ++g_coversync_cue_token;
    reset_acrcloud_cooldown();
    g_active_resolved_provider = "No-Art";
    g_active_source = "None";

    if (::g_shared_artwork_bitmap) {
        DeleteObject(::g_shared_artwork_bitmap);
        ::g_shared_artwork_bitmap = NULL;
    }

    if (track.is_valid()) {
        titleformat_provider::set_track_artwork_info(track, "", "", "", "None");
    }
    titleformat_provider::set_status("No-Art (Forced)");

    refresh_all_dui_artwork_panels();
    refresh_all_cui_artwork_panels();

    notify_artwork_cleared("No-Art");
}

void artwork_manager::force_external_api_autoprobe() {
    ASSERT_MAIN_THREAD();
    foo_artwork::log_printf("foo_artwork: Manual force external now-playing API autoprobe triggered by user.");

    metadb_handle_ptr track;
    if (!playback_control::get()->get_now_playing(track) || !track.is_valid()) {
        track = g_active_playing_track;
    }
    if (!track.is_valid()) {
        foo_artwork::log_printf("foo_artwork: Autoprobe failed: No track currently playing.");
        return;
    }

    pfc::string8 stream_url = track->get_path();
    if (!is_internet_stream_track(track, &stream_url)) {
        foo_artwork::log_printf("foo_artwork: Autoprobe failed: Current track is not an internet stream.");
        return;
    }

    g_active_playing_track = track;
    g_force_noart = false;
    g_manual_external_probe = true;
    titleformat_provider::set_status("Probing external stream API...");
    // Resolve configured AzuraCast/RadioReg endpoints as well as auto-discovery.
    start_external_stream_api_poller(stream_url);
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
    metadb_handle_ptr track = g_active_playing_track;
    if (force || !artwork_manager::has_url_flag(g_current_stream_url.c_str(), "forceacr", track)) {
        g_rms_detector_token++;
    }
}

static void start_rms_silence_detector(const pfc::string8& stream_url) {
    pfc::string8 resolved_url = stream_url;
    std::string s_url = resolved_url.c_str();
    if (s_url.find("file://") == 0 || s_url.rfind(".tags") != std::string::npos) {
        metadb_handle_ptr track = g_active_playing_track;
        if (!track.is_valid() && core_api::is_main_thread()) {
            playback_control::get()->get_now_playing(track);
        }
        if (track.is_valid()) {
            pfc::string8 extracted;
            if (artwork_manager::is_internet_stream_track(track, &extracted) && !extracted.is_empty()) {
                resolved_url = extracted;
            }
        }
    }

    bool is_stream = is_remote_artwork_path(resolved_url.c_str());
    if (!is_stream || resolved_url.is_empty()) {
        return; // Never run acoustic shift detector for local music files
    }

    const bool is_force_acr_stream = artwork_manager::has_url_flag(resolved_url.c_str(), "forceacr", g_active_playing_track);
    uint64_t current_token = ++g_rms_detector_token;

    async_io_manager::instance().submit_task([resolved_url, current_token, is_force_acr_stream]() {
        PerceptualVector prev_vec;
        auto last_trigger_time = std::chrono::steady_clock::now();

        while (true) {
            // Stable 3-second background poll sleep
            std::this_thread::sleep_for(std::chrono::seconds(3));

            if (g_is_shutting_down.load() || current_token != g_rms_detector_token.load()) {
                return; // Stream changed, stopped, or app exiting -> exit worker thread cleanly
            }

            auto now = std::chrono::steady_clock::now();

            PerceptualVector curr_vec;
            auto vec_promise = std::make_shared<std::promise<PerceptualVector>>();
            auto vec_future = vec_promise->get_future();

            async_io_manager::instance().post_to_main_thread([vec_promise, current_token, is_force_acr_stream]() {
                PerceptualVector vec;
                // Playback handles, visualisation streams and cooldown state are
                // owned by the main thread; the worker only reads atomic tokens.
                if (current_token != g_rms_detector_token.load() ||
                    (!is_force_acr_stream && std::chrono::steady_clock::now() < g_acrcloud_cooldown_until)) {
                    vec_promise->set_value(vec);
                    return;
                }
                try {
                    auto stream = get_persistent_vis_stream();
                    if (stream.is_valid()) {
                        double abs_time = 0;
                        if (stream->get_absolute_time(abs_time) && abs_time >= 0.6) {
                            // Stay behind the playback clock, as in PCM capture.
                            // Asking for audio ending at the clock can miss forever.
                            audio_chunk_impl chunk;
                            if (stream->get_chunk_absolute(chunk, abs_time - 0.6, 0.5)) {
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
            if (!trigger_needed && is_force_acr_stream && time_since_last_trigger >= 90) {
                trigger_needed = true;
                trigger_reason = "90-second safety periodic rescan timer elapsed";
            }

            if (trigger_needed && trigger_reason != nullptr) {
                last_trigger_time = now;

                async_io_manager::instance().post_to_main_thread([resolved_url, current_token, trigger_reason]() {
                    if (current_token == g_rms_detector_token.load() && g_current_stream_url == resolved_url) {
                        foo_artwork::log_printf("foo_artwork: %s. Waiting 2s for new song to settle before sampling...", trigger_reason);

                        // Schedule 2-second post-transition settling delay on background thread
                        async_io_manager::instance().submit_task([resolved_url, current_token]() {
                            std::this_thread::sleep_for(std::chrono::seconds(2));

                            async_io_manager::instance().post_to_main_thread([resolved_url, current_token]() {
                                if (current_token == g_rms_detector_token.load() && g_current_stream_url == resolved_url) {
                                    foo_artwork::log_printf("foo_artwork: Settling period complete. Initiating ACRCloud audio recognition...");
                                    artwork_manager::rescan_stream_acrcloud();
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

    metadb_handle_ptr active_track;
    if (pc->get_now_playing(active_track) && active_track.is_valid()) {
        // active_track found
    } else {
        active_track = g_active_playing_track;
    }

    // Local decoders can emit dynamic metadata too. Do not let those events
    // supersede local artwork with the radio cache/API-only search pipeline.
    if (active_track.is_valid() && !is_internet_stream_track(active_track)) return;

    if (has_url_flag(g_current_stream_url.c_str(), "forceacr", active_track)) return;

    bool is_youtube = false;
    if (active_track.is_valid()) {
        is_youtube = !extract_youtube_video_id(active_track->get_path()).is_empty();
    }
    if (!is_youtube) {
        is_youtube = !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty();
    }

    pfc::string8 youtube_artist, youtube_title;
    if (is_youtube && active_track.is_valid()) {
        extract_track_metadata_dynamic(active_track, youtube_artist, youtube_title);
        if (g_is_youtube_art_track) {
            raw_artist = youtube_artist.c_str();
            raw_title = youtube_title.c_str();
            raw_artist_full = raw_artist;
            raw_album = g_youtube_extracted_album.c_str();
        }
        // The track pipeline handles recognition when Release supplies no artist.
        if (g_is_youtube_release_topic && !g_is_youtube_art_track) return;
    }
    StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(raw_artist, raw_title, is_youtube, is_youtube && (g_is_youtube_art_track || g_is_youtube_topic_track));
    if (!meta.is_valid_search || meta.is_station_or_url) return;

    pfc::string8 clean_art = meta.first_artist.c_str();
    pfc::string8 clean_tit = meta.clean_title.c_str();
    pfc::string8 clean_art_full = (!is_youtube && raw_artist_full && raw_artist_full[0] != '\0') ? raw_artist_full : meta.clean_artist.c_str();
    pfc::string8 clean_album = raw_album ? raw_album : "";
    pfc::string8 clean_listeners = raw_listeners ? raw_listeners : "";

    if (clean_album.is_empty() && active_track.is_valid()) {
        clean_album = extract_broadcast_album(active_track);
    }
    if (clean_album.is_empty() && !meta.clean_album.empty()) {
        clean_album = meta.clean_album.c_str();
    }

    if (clean_art == g_last_stream_artist && clean_tit == g_last_stream_title) {
        return; // Avoid duplicate searches for identical stream metadata when already in-flight or resolved
    }
    if (clean_art == g_pending_external_api_artist && clean_tit == g_pending_external_api_title) {
        return;
    }

    auto execute_stream_cue = [clean_art, clean_tit, clean_art_full, clean_album, clean_listeners](int applied_delay) {
        ASSERT_MAIN_THREAD();
        static_api_ptr_t<playback_control> pc_cue;
        if (!pc_cue->is_playing() && !pc_cue->is_paused()) return;

        // A station URL is shared by many songs: discard metadata from the previous cue.
        titleformat_provider::clear_track_artwork_info();
        titleformat_provider::reset_stream_track_timer(applied_delay);

        g_last_stream_artist = clean_art;
        g_last_stream_artist_full = clean_art_full;
        g_last_stream_title = clean_tit;

        g_force_noart = false;

        // Reset rejected providers and active provider for the new stream song
        g_rejected_providers_for_current_track.clear();
        g_active_resolved_provider.reset();

        // Increment monitor token to cancel any pending 10s initial metadata fallback monitor
        g_stream_monitor_token++;

        // Stop acoustic shift detector since valid song metadata is now available
        stop_rms_silence_detector();

        // If external stream API was auto-probed (not explicitly configured via ?azuracast_api, ?radioreg_api, or ?ext_api_autoprobe),
        // stop the background poller since valid native ICY stream metadata is actively arriving
        metadb_handle_ptr track;
        if (pc_cue->get_now_playing(track) && track.is_valid()) {
            // track found
        } else {
            track = g_active_playing_track;
        }
        pfc::string8 az_param = get_url_param_value(g_current_stream_url.c_str(), "azuracast_api", track);
        pfc::string8 rr_param = get_url_param_value(g_current_stream_url.c_str(), "radioreg_api", track);
        bool force_autoprobe = g_manual_external_probe || has_url_flag(g_current_stream_url.c_str(), "ext_api_autoprobe", track);
        if (az_param.is_empty() && rr_param.is_empty() && !force_autoprobe) {
            stop_external_stream_api_poller();
        }

        // Reset ACRCloud cooldown on fresh dynamic track update
        reset_acrcloud_cooldown();

        uint64_t gen = ++g_search_generation;

        log_simplified_track_info(clean_art.c_str(), clean_tit.c_str());

        if (track.is_valid()) {
            titleformat_provider::set_track_artwork_info(track, clean_art_full.c_str(), clean_tit.c_str(), "", "", clean_art_full.c_str(), clean_album.c_str(), clean_listeners.c_str());
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

        remember_search_metadata(clean_art, clean_tit, cache_key);

        pfc::string8 broadcast_art_url = extract_broadcast_artwork_url(track);
        bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
            (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());

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
            pfc::string8 final_artist = (res.is_acrcloud_recognized && !res.artist.is_empty()) ? res.artist :
                (!clean_art_full.is_empty() ? clean_art_full : (!res.artist.is_empty() ? res.artist : clean_art));
            pfc::string8 final_title = !res.title.is_empty() ? res.title : clean_tit;
            pfc::string8 final_album = !res.album.is_empty() ? res.album : clean_album;

            if (cfg_enable_disk_cache || cfg_single_file_cache) {
                pfc::string8 key = cfg_single_file_cache ? pfc::string8("current") : cache_key;
                async_io_manager::instance().cache_set_async(key, res.data);
                async_io_manager::instance().cache_set_metadata(key, final_artist, final_title, final_album, effective_source);
            }
            if (res.data.get_size() > 0) {
                std::vector<BYTE> vec(res.data.get_ptr(), res.data.get_ptr() + res.data.get_size());
                create_bitmap_from_image_data(vec);
            }

            metadb_handle_ptr now_track;
            if (playback_control::get()->get_now_playing(now_track) && now_track.is_valid()) {
                pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
                titleformat_provider::set_track_artwork_info(now_track, final_artist.c_str(), final_title.c_str(), cache_file.c_str(), effective_source.c_str(), final_artist.c_str(), final_album.c_str(), clean_listeners.c_str());
            }

            int w = 0, h = 0;
            if (res.data.get_size() > 0) get_image_dimensions_from_data(res.data.get_ptr(), res.data.get_size(), w, h);
            titleformat_provider::set_status_artwork_loaded(effective_source.c_str(), w, h, (size_t)res.data.get_size(), res.is_acrcloud_recognized);

            refresh_all_dui_artwork_panels();
            refresh_all_cui_artwork_panels();
        };

        // Apply the same YouTube policy to later metadata cues.
        if (is_youtube) {
            search_apis_async(clean_art, clean_tit, cache_key, [apply_success_result](const artwork_result& res) {
                if (res.success && res.data.get_size() > 0) apply_success_result(res);
            });
            return;
        }

        if (try_broadcast_artwork) {
            foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
        } else if (g_rejected_providers_for_current_track.find("Broadcast Artwork") != g_rejected_providers_for_current_track.end()) {
            foo_artwork::log_printf("foo_artwork: Skipping rejected provider 'Broadcast Artwork' for current track.");
        }

        if (cfg_single_file_cache) {
            if (try_broadcast_artwork) {
                search_broadcast_artwork_async(broadcast_art_url, cache_key, [clean_art, clean_tit, cache_key, apply_success_result](const artwork_result& res) {
                    if (res.success && res.data.get_size() > 0) {
                        apply_success_result(res);
                    } else {
                        foo_artwork::log_printf("foo_artwork: Broadcast artwork download failed. Falling back to online APIs...");
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
            async_io_manager::instance().cache_get_async(cache_key, [broadcast_art_url, try_broadcast_artwork, clean_art, clean_tit, cache_key, apply_success_result, gen](bool cache_hit, const pfc::array_t<t_uint8>& data, const pfc::string8& err) {
                if (gen != g_search_generation.load()) return;
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

                    pfc::string8 c_artist, c_title, c_album, c_source;
                    if (async_io_manager::instance().cache_get_metadata(cache_key, c_artist, c_title, c_album, c_source)) {
                        if (cfg_normalize_api_metadata_case) {
                            if (!c_title.is_empty()) c_title = MetadataCleaner::to_title_case(c_title.c_str()).c_str();
                            if (!c_album.is_empty()) c_album = MetadataCleaner::to_title_case(c_album.c_str()).c_str();
                        }
                        cache_res.artist = c_artist;
                        cache_res.title = c_title;
                        cache_res.album = c_album;
                        if (!c_source.is_empty()) cache_res.source = c_source;
                    }

                    apply_success_result(cache_res);
                } else if (try_broadcast_artwork) {
                    search_broadcast_artwork_async(broadcast_art_url, cache_key, [clean_art, clean_tit, cache_key, apply_success_result](const artwork_result& res) {
                        if (res.success && res.data.get_size() > 0) {
                            apply_success_result(res);
                        } else {
                            foo_artwork::log_printf("foo_artwork: Broadcast artwork download failed. Falling back to online APIs...");
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
    };

    bool is_first_cue = !g_has_received_first_stream_cue.exchange(true);
    int sync_delay_sec = is_first_cue ? 0 : extract_coversync_seconds(active_track);
    if (sync_delay_sec > 0) {
        g_pending_external_api_artist = clean_art.c_str();
        g_pending_external_api_title = clean_tit.c_str();
        uint64_t cue_token = ++g_coversync_cue_token;
        foo_artwork::log_printf("foo_artwork: Applying coversync delay of %d seconds for stream track cue '%s - %s'", 
                               sync_delay_sec, clean_art.c_str(), clean_tit.c_str());
        char status_buf[128];
        snprintf(status_buf, sizeof(status_buf), "Next cue in %ds: %s - %s", sync_delay_sec, clean_art.c_str(), clean_tit.c_str());
        titleformat_provider::set_status(status_buf);

        async_io_manager::instance().submit_task([clean_art, clean_tit, cue_token, sync_delay_sec, execute_stream_cue]() {
            for (int s = sync_delay_sec; s > 0; --s) {
                if (g_is_shutting_down.load() || g_coversync_cue_token != cue_token) return;
                char status_buf[128];
                snprintf(status_buf, sizeof(status_buf), "Next cue in %ds: %s - %s", s, clean_art.c_str(), clean_tit.c_str());
                titleformat_provider::set_status(status_buf);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (g_is_shutting_down.load() || g_coversync_cue_token != cue_token) return;

            async_io_manager::instance().post_to_main_thread([clean_art, clean_tit, cue_token, sync_delay_sec, execute_stream_cue]() {
                if (g_is_shutting_down.load() || g_coversync_cue_token != cue_token) return;
                g_pending_external_api_artist.reset();
                g_pending_external_api_title.reset();
                execute_stream_cue(sync_delay_sec);
            });
        });
    } else {
        execute_stream_cue(0);
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
                if (pc->get_now_playing(track) && track.is_valid()) {
                    pfc::string8 track_stream_url;
                    bool is_st = artwork_manager::is_internet_stream_track(track, &track_stream_url);
                    if (is_st && (track->get_path() == stream_url || track_stream_url == stream_url || g_current_stream_url == stream_url)) {
                    if (has_url_flag(stream_url.c_str(), "forceacr", track)) return;
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
                        if (is_acrcloud_configured()) {
                            auto now = std::chrono::steady_clock::now();
                            if (now >= g_acrcloud_cooldown_until) {
                                foo_artwork::log_printf("foo_artwork: Triggering ACRCloud audio recognition fallback for stream without song metadata...");
                                pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : pfc::string8("stream_fallback");
                                const auto track = g_active_playing_track;
                                const auto generation = g_search_generation.load();
                                search_acrcloud_fallback_async(cache_key, [track, cache_key, generation](const artwork_result& res) {
                                    display_manual_artwork(res, track, cache_key, generation);
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
    g_pending_acrcloud_request.reset();
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

    g_active_playing_track = track;
    g_force_noart = false;
    reset_acrcloud_cooldown();
    const uint64_t generation = ++g_search_generation;
    g_manual_search_generation = generation;
    const pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key_for_track(track);
    search_acrcloud_fallback_async(cache_key, [track, cache_key, generation](const artwork_result& result) {
        display_manual_artwork(result, track, cache_key, generation);
    }, true /* is_manual_trigger */);
}

void artwork_manager::rescan_stream_acrcloud() {
    ASSERT_MAIN_THREAD();
    if (g_is_shutting_down.load() || g_force_noart || !cfg_enable_acrcloud || !is_acrcloud_configured()) return;
    metadb_handle_ptr track;
    if (!playback_control::get()->get_now_playing(track) || !track.is_valid() ||
        !is_internet_stream_track(track)) return;
    if (g_pending_acrcloud_request && g_pending_acrcloud_request->task_id == g_acrcloud_task_id.load() &&
        g_pending_acrcloud_request->generation == g_search_generation.load()) return;

    // A repaint does not request artwork. Start a new acoustic scan and deliver
    // its result explicitly, also when there is no artwork panel subscribed.
    g_acrcloud_cooldown_until = std::chrono::steady_clock::time_point{};
    const uint64_t generation = ++g_search_generation;
    g_rejected_providers_for_current_track.clear();
    const pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : pfc::string8("stream_fallback");
    search_acrcloud_fallback_async(cache_key, [track, cache_key, generation](const artwork_result& result) {
        display_manual_artwork(result, track, cache_key, generation);
    });
}

static void extract_track_metadata_dynamic(metadb_handle_ptr track, pfc::string8& out_artist, pfc::string8& out_title) {
    out_artist.reset();
    out_title.reset();

    if (!track.is_valid()) return;

    pfc::string8 file_path = track->get_path();
    bool is_internet_stream = artwork_manager::is_internet_stream_track(track);

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

    bool is_youtube = !artwork_manager::extract_youtube_video_id(file_path.c_str()).is_empty() ||
                      !artwork_manager::extract_youtube_video_id(g_current_stream_url.c_str()).is_empty();

    g_is_youtube_art_track = false;
    g_is_youtube_topic_track = false;
    g_is_youtube_release_topic = false;
    g_youtube_extracted_album.reset();

    if (is_youtube) {
        g_is_youtube_topic_track = MetadataCleaner::is_youtube_topic_artist(out_artist.c_str());
        g_is_youtube_release_topic = normalize_for_matching(out_artist.c_str()) == "release topic";
        // The dynamic artist may already have been cleaned by the input component.
        try {
            auto info = track->get_info_ref();
            if (info.is_valid()) {
                const char* artist = info->info().meta_get("ARTIST", 0);
                g_is_youtube_topic_track = g_is_youtube_topic_track || MetadataCleaner::is_youtube_topic_artist(artist);
                g_is_youtube_release_topic = g_is_youtube_release_topic ||
                    normalize_for_matching(artist ? artist : "") == "release topic";
            }
        } catch (...) {}
        pfc::string8 desc_str;
        try {
            metadb_info_container::ptr info_container = track->get_info_ref();
            if (info_container.is_valid()) {
                const file_info& info = info_container->info();
                const char* d = info.meta_get("DESCRIPTION", 0);
                if (!d || !*d) d = info.meta_get("COMMENT", 0);
                if (d) desc_str = d;
            }
        } catch (...) {}

        if (desc_str.is_empty() && track.is_valid()) {
            static_api_ptr_t<titleformat_compiler> compiler;
            service_ptr_t<titleformat_object> script_desc;
            compiler->compile_force(script_desc, "%description%");
            metadb_handle_ptr now_p;
            if (pc->get_now_playing(now_p) && now_p == track) {
                pc->playback_format_title(nullptr, desc_str, script_desc, nullptr, playback_control::display_level_titles);
            } else {
                track->format_title(nullptr, desc_str, script_desc, nullptr);
            }
        }

        if (!desc_str.is_empty()) {
            std::string yt_art, yt_tit, yt_alb;
            if (MetadataCleaner::try_parse_youtube_description(desc_str.c_str(), yt_art, yt_tit, yt_alb)) {
                g_is_youtube_art_track = true;
                g_youtube_extracted_album = yt_alb.c_str();
                out_artist = yt_art.c_str();
                out_title = yt_tit.c_str();
                foo_artwork::log_printf("foo_artwork: Parsed YouTube Music Art Track from DESCRIPTION: Artist='%s', Title='%s', Album='%s'",
                                        yt_art.c_str(), yt_tit.c_str(), yt_alb.c_str());
            }
        }
    }

    if (is_internet_stream && (out_artist.is_empty() || out_artist == "Unknown Artist" || out_title.is_empty() || out_title == "Unknown Track")) {
        if (!g_last_stream_artist.is_empty() && !g_last_stream_title.is_empty()) {
            out_artist = g_last_stream_artist;
            out_title = g_last_stream_title;
        }
    }

    if (is_internet_stream) {
        if (artwork_manager::has_url_flag(file_path.c_str(), "inverted", track) ||
            artwork_manager::has_url_flag(g_current_stream_url.c_str(), "inverted", track)) {
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
    pfc::string8 effective_stream_url;
    bool is_internet_stream = is_internet_stream_track(track, &effective_stream_url);

    // URL Flag: ?bypass - Completely skip artwork lookup
    if (has_url_flag(file_path.c_str(), "bypass", track) || 
        has_url_flag(effective_stream_url.c_str(), "bypass", track) || 
        has_url_flag(g_current_stream_url.c_str(), "bypass", track)) {
        foo_artwork::log_printf("foo_artwork: Stream URL contains '?bypass' tag. Skipping artwork lookup.");
        artwork_result res;
        res.success = false;
        res.source = "Bypassed";
        res.error_message = "Bypassed by stream URL flag (?bypass)";
        callback(res);
        return;
    }

    bool is_youtube = !extract_youtube_video_id(file_path.c_str()).is_empty() || 
                      !extract_youtube_video_id(effective_stream_url.c_str()).is_empty() ||
                      !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty();

    pfc::string8 artist, track_name;
    extract_track_metadata_dynamic(track, artist, track_name);

    if (!is_internet_stream) {
        log_simplified_track_info(artist.c_str(), track_name.c_str());
    } else {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track_name.c_str(), is_youtube, is_youtube && (g_is_youtube_art_track || g_is_youtube_topic_track));
        if (meta.is_valid_search && !meta.is_station_or_url) {
            log_simplified_track_info(meta.first_artist.c_str(), meta.clean_title.c_str());
        }
    }
    
    pfc::string8 cache_key = cfg_single_file_cache ? pfc::string8("current") : generate_cache_key_for_track(track);

    // Requests from different panels are subscribers to the same playback cue.
    // Only lifecycle/cue changes and explicit manual commands supersede it.
    const uint64_t gen = g_search_generation.load();
    auto original_callback = callback;
    auto wrapped_callback = [gen, track, artist, track_name, cache_key, original_callback](const artwork_result& res) {
        if (gen != g_search_generation.load()) {
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
            pfc::string8 disp_artist = !res.artist.is_empty() ? res.artist : artist;
            pfc::string8 disp_title = !res.title.is_empty() ? res.title : track_name;
            pfc::string8 disp_album = !res.album.is_empty() ? res.album : (!g_youtube_extracted_album.is_empty() ? g_youtube_extracted_album : pfc::string8(""));
            titleformat_provider::set_track_artwork_info(track, disp_artist.c_str(), disp_title.c_str(), cache_file.c_str(), effective_source.c_str(), disp_artist.c_str(), disp_album.c_str());

            int w = 0, h = 0;
            if (res.data.get_size() > 0) get_image_dimensions_from_data(res.data.get_ptr(), res.data.get_size(), w, h);
            titleformat_provider::set_status_artwork_loaded(effective_source.c_str(), w, h, (size_t)res.data.get_size(), res.is_acrcloud_recognized);

            artwork_result final_res = res;
            final_res.source = effective_source;
            original_callback(final_res);
            return;
        }
        titleformat_provider::set_status("No artwork found");
        original_callback(res);
    };
    callback = wrapped_callback;

    // YouTube decoder artwork and cached thumbnails must not bypass API policy.
    if (is_youtube) {
        search_apis_async(artist, track_name, cache_key, callback);
        return;
    }

    if (is_internet_stream) {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track_name.c_str(), is_youtube, is_youtube && (g_is_youtube_art_track || g_is_youtube_topic_track));
        pfc::string8 target_stream_url = !effective_stream_url.is_empty() ? effective_stream_url : file_path;
        bool force_acrcloud = has_url_flag(file_path.c_str(), "forceacr", track) || 
                              has_url_flag(target_stream_url.c_str(), "forceacr", track) || 
                              has_url_flag(g_current_stream_url.c_str(), "forceacr", track);
        bool is_reject_station_covers = has_url_flag(file_path.c_str(), "rejectstationcovers", track) || 
                                        has_url_flag(target_stream_url.c_str(), "rejectstationcovers", track) || 
                                        has_url_flag(g_current_stream_url.c_str(), "rejectstationcovers", track);

        bool new_stream_connect = (g_current_stream_url != target_stream_url);
        if (new_stream_connect) {
            g_current_stream_url = target_stream_url;
            g_last_stream_artist = "";
            g_last_stream_title = "";
            g_last_logged_track_info = ""; // Reset logged track info history for fresh stream
            reset_acrcloud_cooldown(); // Reset cooldown on new radio stream connection
            stop_rms_silence_detector(); // Disabled by default until Stage 3 ends with no artwork
            if (force_acrcloud) {
                start_rms_silence_detector(target_stream_url); // Active immediately for ?forceacr streams
            }
            if (meta.is_station_or_url || !meta.is_valid_search) {
                start_initial_stream_metadata_monitor(target_stream_url);
            }
            start_external_stream_api_poller(target_stream_url);
        }

        // RECOGNIZED STREAM ARTWORK GUARD:
        // If ACRCloud (or manual trigger) recently recognized this stream and cached the result,
        // and we are within the track length circuit-breaker cooldown, return the recognized artwork!
        auto now = std::chrono::steady_clock::now();
        if ((g_last_recognized_stream_url == target_stream_url || g_last_recognized_stream_url == file_path) && g_last_recognized_result.success && now < g_acrcloud_cooldown_until) {
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

        if (is_youtube && g_is_youtube_release_topic && !g_is_youtube_art_track) {
            search_acrcloud_fallback_async(cache_key, callback);
            return;
        }

        // UNTAGGED STREAMS, PLACEHOLDER METADATA, STATION DOMAINS/SLOGANS OR ?FORCEACR TAG:
        // On untagged streams, station names/URLs, or ?forceacr streams, do NOT query online commercial APIs.
        if (force_acrcloud) {
            if (cfg_enable_acrcloud && is_acrcloud_configured()) {
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
                if (try_broadcast_artwork && !is_reject_station_covers) {
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
            if (try_broadcast_artwork) {
                search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track_name, cache_key, callback](const artwork_result& res) {
                    if (res.success) {
                        callback(res);
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
                if (try_broadcast_artwork) {
                    search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track_name, cache_key, track, callback, is_youtube, is_reject_station_covers](const artwork_result& bcast_res) {
                        if (bcast_res.success) {
                            cancel_acrcloud_tasks();
                            callback(bcast_res);
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
    const uint64_t generation = g_search_generation.load();
    async_io_manager::instance().cache_get_async(cache_key, 
        [cache_key, track, callback, generation](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (generation != g_search_generation.load()) return;
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
                bool is_stream = is_internet_stream_track(track);

                // Local embedded/folder artwork takes precedence over cached online
                // results, including covers cached while a relative path was misclassified.
                if (track.is_valid() && !is_stream && !cfg_skip_local_artwork) {
                    find_local_artwork_async(track, [cache_key, data, callback, generation](const artwork_result& local_result) {
                        if (generation != g_search_generation.load()) return;
                        if (local_result.success && local_result.data.get_size() > 0) {
                            if (cfg_enable_disk_cache && !cache_key.is_empty()) {
                                async_io_manager::instance().cache_set_async(cache_key, local_result.data);
                            }
                            callback(local_result);
                        } else {
                            validate_and_complete_result(data, callback, cache_key.c_str());
                        }
                    });
                } else {
                    if (!is_already_resolved) {
                        foo_artwork::log_printf("foo_artwork: SUCCESS - Artwork displayed from disk cache");
                    }
                    // Cache hit - validate and return
                    validate_and_complete_result(data, callback, cache_key.c_str());
                }
            } else {
                // Cache miss - continue to local search
                pfc::string8 file_path = track->get_path();
                search_local_async(file_path, cache_key, track, callback);
            }
        });
}

void artwork_manager::check_cache_async_metadata(const pfc::string8& cache_key, const pfc::string8& artist, const pfc::string8& track, artwork_callback callback) {
    const uint64_t generation = g_search_generation.load();
    // Check cache first, then fall back to Broadcast Artwork / API search on miss
    async_io_manager::instance().cache_get_async(cache_key,
        [cache_key, artist, track, callback, generation](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (generation != g_search_generation.load()) return;
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
                validate_and_complete_result(data, callback, cache_key.c_str());
            } else {
                // Cache miss - check broadcast artwork before online APIs
                pfc::string8 broadcast_art_url = extract_broadcast_artwork_url();
                bool try_broadcast_artwork = !broadcast_art_url.is_empty() &&
                    (g_rejected_providers_for_current_track.find("Broadcast Artwork") == g_rejected_providers_for_current_track.end());

                if (try_broadcast_artwork) {
                    foo_artwork::log_printf("foo_artwork: In-stream broadcast artwork URL detected: '%s'", broadcast_art_url.c_str());
                    search_broadcast_artwork_async(broadcast_art_url, cache_key, [artist, track, cache_key, callback, generation](const artwork_result& res) {
                        if (generation != g_search_generation.load()) return;
                        if (res.success) {
                            callback(res);
                        } else {
                            foo_artwork::log_printf("foo_artwork: Broadcast artwork download failed. Falling back to online APIs...");
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
    const uint64_t generation = g_search_generation.load();

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
    find_local_artwork_async(track, [artist, track_name, cache_key, track, callback, generation](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
        if (result.success) {
            // Local artwork found - in single-file cache mode, write to current.cache
            // so external consumers (e.g., JScript Panel 3 Thumbs) see the correct artwork.
            // In normal cache mode, write to disk cache for fast offline retrieval.
            if (cfg_single_file_cache) {
                async_io_manager::instance().cache_set_async("current", result.data);
            } else if (cfg_enable_disk_cache && !cache_key.is_empty()) {
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
    const uint64_t generation = g_search_generation.load();
    bool is_youtube = !extract_youtube_video_id(g_current_stream_url.c_str()).is_empty() || 
                      (g_active_playing_track.is_valid() && !extract_youtube_video_id(g_active_playing_track->get_path()).is_empty());

    // Explicit thumbnail-only mode also works without searchable song metadata.
    if (is_youtube && cfg_skip_youtube_apis) {
        cancel_acrcloud_tasks();
        stop_rms_silence_detector(true);
        pfc::string8 video_id = g_active_playing_track.is_valid()
            ? extract_youtube_video_id(g_active_playing_track->get_path()) : pfc::string8();
        if (video_id.is_empty()) video_id = extract_youtube_video_id(g_current_stream_url.c_str());
        if (g_rejected_providers_for_current_track.count("YouTube Thumbnail")) {
            artwork_result fail;
            fail.error_message = "YouTube thumbnail rejected";
            callback(fail);
        } else {
            search_youtube_thumbnail_async(video_id, cache_key, callback,
                g_is_youtube_art_track || g_is_youtube_topic_track);
        }
        return;
    }

    StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(raw_artist.c_str(), raw_track.c_str(), is_youtube, is_youtube && (g_is_youtube_art_track || g_is_youtube_topic_track));

    metadb_handle_ptr active_t = g_active_playing_track;
    bool force_acrcloud = has_url_flag(g_current_stream_url.c_str(), "forceacr", active_t);

    // Direct ACRCloud Tier 4 fallback if URL explicitly contains 'forceacr' tag
    if (force_acrcloud) {
        if (cfg_enable_acrcloud && is_acrcloud_configured()) {
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

    if (is_youtube && g_is_youtube_release_topic && !g_is_youtube_art_track) {
        search_acrcloud_fallback_async(cache_key, callback);
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

    remember_search_metadata(meta.first_artist.c_str(), meta.clean_title.c_str(), cache_key);

    // Deduplicate in-flight search requests for identical metadata
    pfc::string8 dedup_key_pfc = generate_cache_key(meta.clean_artist.c_str(), meta.clean_title.c_str());
    std::string dedup_key = std::to_string(generation) + "|" + dedup_key_pfc.c_str();

    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        const std::string prefix = std::to_string(generation) + "|";
        for (auto it = g_in_flight_queries.begin(); it != g_in_flight_queries.end();) {
            if (it->first.compare(0, prefix.size(), prefix) != 0) it = g_in_flight_queries.erase(it);
            else ++it;
        }
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
    auto final_callback = [dedup_key, generation](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
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

    const bool known_youtube_metadata = is_youtube && (g_is_youtube_art_track || g_is_youtube_topic_track);
    const bool description_parsed = g_is_youtube_art_track;
    auto notify_text_search_failed = [=]() {
        if (generation != g_search_generation.load()) return;
        foo_artwork::log_printf("foo_artwork: Text search failed for '%s - %s'. No artwork found from any online API.",
                                 meta.clean_artist.c_str(), meta.clean_title.c_str());

        // YouTube fallback check (Art Track square thumbnail OR ACRCloud -> thumbnail fallback)
        pfc::string8 yt_video_id = extract_youtube_video_id(g_current_stream_url.c_str());
        if (yt_video_id.is_empty() && g_active_playing_track.is_valid()) {
            yt_video_id = extract_youtube_video_id(g_active_playing_track->get_path());
        }

        if (!yt_video_id.is_empty()) {
            bool is_yt_thumb_rejected = (g_rejected_providers_for_current_track.find("YouTube Thumbnail") != g_rejected_providers_for_current_track.end());

            // YouTube Music Art Track: Canonical metadata already parsed from DESCRIPTION.
            // Avoid ACRCloud quota consumption and PCM delay by directly fetching square-cropped thumbnail fallback.
            if (description_parsed) {
                if (!is_yt_thumb_rejected) {
                    foo_artwork::log_printf("foo_artwork: YouTube Music Art Track detected. Using square-cropped video thumbnail for ID '%s'...", yt_video_id.c_str());
                    search_youtube_thumbnail_async(yt_video_id, cache_key, final_callback, true);
                    return;
                }
            } else {
                // Non-Art Track YouTube video: Prioritize ACRCloud audio recognition before thumbnail fallback
                bool can_acr = (cfg_enable_acrcloud && is_acrcloud_configured());
                if (can_acr) {
                    auto now = std::chrono::steady_clock::now();
                    if (now >= g_acrcloud_cooldown_until) {
                        foo_artwork::log_printf("foo_artwork: YouTube non-art track: Triggering ACRCloud audio recognition before YouTube thumbnail fallback...");
                        search_acrcloud_fallback_async(cache_key, final_callback);
                        return;
                    }
                }

                // If ACRCloud is disabled, unconfigured, or on cooldown: fall back directly to video thumbnail
                if (!is_yt_thumb_rejected) {
                    foo_artwork::log_printf("foo_artwork: Querying YouTube video thumbnail for ID '%s' as fallback...", yt_video_id.c_str());
                    search_youtube_thumbnail_async(yt_video_id, cache_key, final_callback, false);
                    return;
                }
            }
        }

        // Valid radio/file metadata reached this path. A missing cover does not
        // justify fingerprinting or enabling the acoustic detector. Untagged
        // streams and explicit forceacr/manual requests have separate paths.
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
    // Tagged radio/file failures stop here; non-Art-Track YouTube retains its recognition fallback.

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
                if (known_youtube_metadata) {
                    notify_text_search_failed();
                    return;
                }
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
// Owned by the main-thread sampling callbacks, including callbacks delivered after timeout.
struct AcrAudioCapture {
    std::vector<int16_t> samples;
    int sample_rate = 16000;
    double next_time = -1.0;
};

void artwork_manager::search_acrcloud_fallback_async(const pfc::string8& cache_key, artwork_callback callback, bool is_manual_trigger) {
    pfc::string8 video_id = g_active_playing_track.is_valid()
        ? extract_youtube_video_id(g_active_playing_track->get_path()) : pfc::string8();
    if (video_id.is_empty()) video_id = extract_youtube_video_id(g_current_stream_url.c_str());
    const bool crop_thumbnail = g_is_youtube_art_track || g_is_youtube_topic_track;
    const bool thumbnail_rejected = g_rejected_providers_for_current_track.count("YouTube Thumbnail") != 0;
    if (!video_id.is_empty() && !thumbnail_rejected) {
        auto original_callback = callback;
        const uint64_t generation = g_search_generation.load();
        callback = [video_id, cache_key, crop_thumbnail, original_callback, generation](const artwork_result& res) {
            if (generation != g_search_generation.load()) return;
            if (res.success) {
                original_callback(res);
                return;
            }
            search_youtube_thumbnail_async(video_id, cache_key,
                [res, cache_key, original_callback](const artwork_result& thumbnail) {
                    artwork_result result = thumbnail;
                    if (result.success && res.is_acrcloud_recognized) {
                        result.artist = res.artist;
                        result.title = res.title;
                        result.album = res.album;
                        result.is_acrcloud_recognized = true;
                        g_last_recognized_result = result;
                        g_last_recognized_stream_url = g_current_stream_url;
                        stop_rms_silence_detector();
                        pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
                        titleformat_provider::set_track_artwork_info(g_active_playing_track,
                            result.artist.c_str(), result.title.c_str(), cache_file.c_str(),
                            result.source.c_str(), result.artist.c_str(), result.album.c_str());
                        if (cfg_enable_disk_cache && !cache_key.is_empty())
                            async_io_manager::instance().cache_set_metadata(cache_key, result.artist, result.title, result.album, result.source);
                        if (cfg_single_file_cache)
                            async_io_manager::instance().cache_set_metadata("current", result.artist, result.title, result.album, result.source);
                    }
                    original_callback(result);
                }, crop_thumbnail);
        };
    }

    const uint64_t request_generation = g_search_generation.load();
    auto completion = callback;
    callback = [completion, request_generation](const artwork_result& result) {
        async_io_manager::instance().post_to_main_thread([completion, request_generation, result]() {
            if (g_is_shutting_down.load() || request_generation != g_search_generation.load() || g_force_noart) return;
            completion(result);
        });
    };

    if (!cfg_enable_acrcloud || !is_acrcloud_configured()) {
        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = "All text search tiers failed; ACRCloud fallback is disabled or unconfigured";
        callback(fail_res);
        return;
    }

    if (!is_manual_trigger && g_pending_acrcloud_request &&
        g_pending_acrcloud_request->generation == request_generation &&
        g_pending_acrcloud_request->task_id == g_acrcloud_task_id.load()) {
        g_pending_acrcloud_request->callbacks.push_back(callback);
        return;
    }

    // Smart Trigger & Circuit-Breaker Cooldown Check (Bypassed if manual hotkey trigger)
    auto now = std::chrono::steady_clock::now();
    if (!is_manual_trigger && now < g_acrcloud_cooldown_until) {
        if (g_last_recognized_stream_url == g_current_stream_url && g_last_recognized_result.success) {
            callback(g_last_recognized_result);
            return;
        }
        auto remaining_sec = std::chrono::duration_cast<std::chrono::seconds>(g_acrcloud_cooldown_until - now).count();
        foo_artwork::log_printf("foo_artwork: ACRCloud recognition on cooldown (%d seconds remaining). Skipping scan to protect API quota.", (int)remaining_sec);

        artwork_result fail_res;
        fail_res.success = false;
        fail_res.error_message = "ACRCloud recognition on cooldown to protect API quota";
        callback(fail_res);
        return;
    }

    uint64_t current_task_id = ++g_acrcloud_task_id;
    auto request = std::make_shared<AcrPendingRequest>();
    request->generation = request_generation;
    request->task_id = current_task_id;
    request->callbacks.push_back(callback);
    g_pending_acrcloud_request = request;
    callback = [request](const artwork_result& result) {
        async_io_manager::instance().post_to_main_thread([request, result]() {
            if (g_is_shutting_down.load() || request->generation != g_search_generation.load() ||
                request->task_id != g_acrcloud_task_id.load() || g_force_noart) return;
            if (g_pending_acrcloud_request == request) g_pending_acrcloud_request.reset();
            auto callbacks = std::move(request->callbacks);
            for (const auto& subscriber : callbacks) subscriber(result);
        });
    };

    if (is_manual_trigger) {
        foo_artwork::log_printf("foo_artwork: Manual Trigger: Bypassing Circuit-Breaker cooldown to force ACRCloud audio recognition...");
    } else {
        foo_artwork::log_printf("foo_artwork: Initiating ACRCloud audio recognition fallback...");
    }
    titleformat_provider::set_status("ACRCloud: Listening for audio stream...");

    // Create the stream before waiting, then retain it throughout sampling.
    get_persistent_vis_stream();

    // Submit task to background worker thread (NON-BLOCKING FOR FOOBAR2000 UI)
    async_io_manager::instance().submit_task([cache_key, callback, is_manual_trigger, current_task_id]() {
        std::vector<int16_t> pcm_samples;
        int sample_rate = 16000;

        auto capture = std::make_shared<AcrAudioCapture>();
        for (int attempt = 0; attempt < 60; ++attempt) {
            if (current_task_id != g_acrcloud_task_id) {
                foo_artwork::log_printf("foo_artwork: ACRCloud background sampling task cancelled (superseded or artwork found).");
                return;
            }

            foo_artwork::log_printf("foo_artwork: Collecting live PCM audio (attempt %d/60)...", attempt + 1);

            // Sleep on BACKGROUND WORKER THREAD (UI stays completely fluid and responsive!)
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }

            using AudioSnapshot = std::pair<std::vector<int16_t>, int>;
            auto query_promise = std::make_shared<std::promise<AudioSnapshot>>();
            auto query_future = query_promise->get_future();

            // Non-blocking sub-millisecond snapshot on main thread
            async_io_manager::instance().post_to_main_thread([query_promise, capture, current_task_id]() {
                if (current_task_id != g_acrcloud_task_id.load()) {
                    query_promise->set_value(AudioSnapshot({}, 16000));
                    return;
                }
                try {
                    auto stream = get_persistent_vis_stream();
                    double now = 0;
                    if (stream.is_valid() && stream->get_absolute_time(now) && now > 0.1) {
                        // Leave a small margin behind the playback clock. Only request recent
                        // audio: a new visualisation stream has no multi-second history yet.
                        const double end = now - 0.1;
                        if (capture->next_time < 0 || end < capture->next_time ||
                            end - capture->next_time > 0.5) {
                            capture->samples.clear(); // Seek, startup, or a gap: start a contiguous segment.
                            capture->next_time = end > 0.25 ? end - 0.25 : 0;
                        }
                        const double length = end - capture->next_time;
                        audio_chunk_impl chunk;
                        if (length > 0.01 && stream->get_chunk_absolute(chunk, capture->next_time, length)) {
                            const audio_sample* data = chunk.get_data();
                            const size_t count = chunk.get_sample_count();
                            const unsigned channels = chunk.get_channels();
                            const int rate = chunk.get_sample_rate();
                            if (data && count > 0 && channels > 0 && rate > 0) {
                                if (rate != capture->sample_rate) capture->samples.clear();
                                capture->sample_rate = rate;
                                for (size_t i = 0; i < count; ++i) {
                                    float mono = 0;
                                    for (unsigned c = 0; c < channels; ++c) mono += (float)data[i * channels + c];
                                    mono /= (float)channels;
                                    mono = std::max(-1.0f, std::min(1.0f, mono));
                                    if (capture->samples.size() < (size_t)(rate * 5))
                                        capture->samples.push_back((int16_t)(mono * 32767.0f));
                                }
                                capture->next_time += (double)count / rate;
                            }
                        }
                    }
                } catch (...) {}
                query_promise->set_value(AudioSnapshot(capture->samples, capture->sample_rate));
            });

            if (query_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
                auto snapshot = query_future.get();
                pcm_samples = std::move(snapshot.first);
                sample_rate = snapshot.second;
                if (pcm_samples.size() >= (size_t)(sample_rate * 4.5)) {
                    break;
                }
            }
        }

        foo_artwork::log_printf("foo_artwork: Sampled %u PCM audio samples (%d Hz) for fingerprinting",
                       (unsigned int)pcm_samples.size(), sample_rate);

        if (pcm_samples.empty() || pcm_samples.size() < (size_t)(sample_rate * 3.5)) {
            foo_artwork::log_printf("foo_artwork: Stream audio buffering (%u samples). Setting short 4s grace period for audio playback to settle.", (unsigned int)pcm_samples.size());
            async_io_manager::instance().post_to_main_thread([current_task_id, callback]() {
                if (current_task_id != g_acrcloud_task_id.load()) return;
                g_acrcloud_cooldown_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                artwork_result fail_res;
                fail_res.error_message = "Stream audio buffering";
                callback(fail_res);
            });
            return;
        }

        if (current_task_id != g_acrcloud_task_id.load()) return;

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

        pfc::string8 primary_key = get_acrcloud_access_key();
        pfc::string8 primary_secret = get_acrcloud_access_secret();
        pfc::string8 secondary_key = get_acrcloud_access_key2();
        pfc::string8 secondary_secret = get_acrcloud_access_secret2();

        bool has_primary = !cfg_acrcloud_host.is_empty() && !primary_key.is_empty() && !primary_secret.is_empty();
        bool has_secondary = !cfg_acrcloud_host2.is_empty() && !secondary_key.is_empty() && !secondary_secret.is_empty();

        ACRCloudClient::RecognitionResult rec;
        int active_acc = g_active_acrcloud_account.load();

        if (active_acc == 2 && has_secondary) {
            rec = ACRCloudClient::recognize_audio(
                cfg_acrcloud_host2.get_ptr(),
                secondary_key.c_str(),
                secondary_secret.c_str(),
                audio_bytes.data(),
                audio_bytes.size()
            );

            bool is_limit_exceeded = (rec.status_code == 3001) ||
                (rec.error_message.find("limit") != std::string::npos && rec.error_message.find("exceeded") != std::string::npos) ||
                (rec.error_message.find("3001") != std::string::npos);

            if (!rec.success && is_limit_exceeded && has_primary) {
                foo_artwork::log_printf("foo_artwork: ACRCloud Secondary account requests limit exceeded (Code 3001). Switching back to Primary credentials...");
                g_active_acrcloud_account.store(1);
                rec = ACRCloudClient::recognize_audio(
                    cfg_acrcloud_host.get_ptr(),
                    primary_key.c_str(),
                    primary_secret.c_str(),
                    audio_bytes.data(),
                    audio_bytes.size()
                );
            }
        } else if (has_primary) {
            rec = ACRCloudClient::recognize_audio(
                cfg_acrcloud_host.get_ptr(),
                primary_key.c_str(),
                primary_secret.c_str(),
                audio_bytes.data(),
                audio_bytes.size()
            );

            // Automatic failover on request limit exceeded (error 3001)
            bool is_limit_exceeded = (rec.status_code == 3001) ||
                (rec.error_message.find("limit") != std::string::npos && rec.error_message.find("exceeded") != std::string::npos) ||
                (rec.error_message.find("3001") != std::string::npos);

            if (!rec.success && is_limit_exceeded && has_secondary) {
                foo_artwork::log_printf("foo_artwork: ACRCloud Primary account requests limit exceeded (Code 3001). Latching active account to Secondary credentials...");
                g_active_acrcloud_account.store(2);
                rec = ACRCloudClient::recognize_audio(
                    cfg_acrcloud_host2.get_ptr(),
                    secondary_key.c_str(),
                    secondary_secret.c_str(),
                    audio_bytes.data(),
                    audio_bytes.size()
                );
            }
        } else if (has_secondary) {
            g_active_acrcloud_account.store(2);
            rec = ACRCloudClient::recognize_audio(
                cfg_acrcloud_host2.get_ptr(),
                secondary_key.c_str(),
                secondary_secret.c_str(),
                audio_bytes.data(),
                audio_bytes.size()
            );
        }

        async_io_manager::instance().post_to_main_thread([rec, cache_key, callback, current_task_id]() {
            if (g_is_shutting_down.load() || current_task_id != g_acrcloud_task_id.load()) return;
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

                {
                    metadb_handle_ptr track;
                    if (playback_control::get()->get_now_playing(track) && track.is_valid()) {
                        titleformat_provider::set_track_artwork_info(track, rec_meta.clean_artist.c_str(), rec_meta.clean_title.c_str(), "", "ACRCloud", rec_meta.clean_artist.c_str(), rec.album.c_str(), "");
                    }
                }

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
                    remember_search_metadata(rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str(), cache_key);

                    foo_artwork::log_printf("foo_artwork: Querying online APIs with ACRCloud recognized track '%s - %s'...", rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str());
                    auto api_order = get_api_search_order();
                    pfc::string8 current_url = g_current_stream_url;
                    search_apis_by_priority(rec_meta.first_artist.c_str(), rec_meta.clean_title.c_str(), cache_key, [callback, current_url, rec_meta, rec](const artwork_result& res) {
                        artwork_result res_acr = res;
                        res_acr.is_acrcloud_recognized = true;
                        if (res_acr.artist.is_empty()) res_acr.artist = rec_meta.clean_artist.c_str();
                        if (res_acr.title.is_empty()) res_acr.title = rec_meta.clean_title.c_str();
                        if (res_acr.album.is_empty()) res_acr.album = rec.album.c_str();
                        if (res_acr.success && res_acr.data.get_size() > 0) {
                            g_last_recognized_result = res_acr;
                            g_last_recognized_stream_url = current_url;
                            metadb_handle_ptr active_t = g_active_playing_track;
                            if (!artwork_manager::has_url_flag(current_url.c_str(), "forceacr", active_t)) {
                                stop_rms_silence_detector();
                            }
                        } else {
                            if (cfg_enable_acrcloud && !current_url.is_empty()) {
                                foo_artwork::log_printf("foo_artwork: Stage 3 text search for ACRCloud metadata returned no artwork. Enabling Log-Spectral Acoustic Shift detection...");
                                start_rms_silence_detector(current_url);
                            }
                        }
                        callback(res_acr);
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
            if (rec.success) {
                fail_res.artist = rec.artist.c_str();
                fail_res.title = rec.title.c_str();
                fail_res.album = rec.album.c_str();
                fail_res.is_acrcloud_recognized = true;
            }
            callback(fail_res);
        });
    });
}

void artwork_manager::search_apis_by_priority(const pfc::string8& artist, const pfc::string8& track, const pfc::string8& cache_key, artwork_callback callback, const std::vector<ApiType>& api_order, size_t index, bool force_enable_apis) {
    ASSERT_MAIN_THREAD();
    if (g_is_shutting_down.load() || core_api::is_shutting_down()) return;
    const uint64_t generation = g_search_generation.load();
    auto original_callback = callback;
    callback = [generation, original_callback](const artwork_result& result) {
        if (generation != g_search_generation.load()) return;
        original_callback(result);
    };
    
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

    // Check if this provider has been rejected by user for current track
    if (g_rejected_providers_for_current_track.find(current_api_name.c_str()) != g_rejected_providers_for_current_track.end()) {
        foo_artwork::log_printf("foo_artwork: Skipping rejected provider '%s' for current track.", current_api_name.c_str());
        search_apis_by_priority(artist, track, cache_key, callback, api_order, index + 1, force_enable_apis);
        return;
    }

    std::string api_dedup_key = std::to_string(generation) + "|" + current_api_name.c_str();
    api_dedup_key += "|";
    api_dedup_key += artist.c_str();
    api_dedup_key += "|";
    api_dedup_key += track.c_str();

    {
        std::lock_guard<std::mutex> lock(g_in_flight_mutex);
        auto now = std::chrono::steady_clock::now();

        // Clean up stale completed entries older than 60 seconds
        for (auto it = g_api_dedup_map.begin(); it != g_api_dedup_map.end(); ) {
            if (it->first.compare(0, std::to_string(generation).size() + 1, std::to_string(generation) + "|") != 0 ||
                (it->second.completed && (now - it->second.completed_time > std::chrono::seconds(60)))) {
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
    auto api_callback = [artist, track, cache_key, api_order, index, force_enable_apis, api_dedup_key, generation](const artwork_result& in_result) {
        if (generation != g_search_generation.load() || g_force_noart) return;
        artwork_result result = in_result;
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
            remember_search_metadata(artist, track, cache_key);

            int w = 0, h = 0;
            if (result.data.get_size() > 0) get_image_dimensions_from_data(result.data.get_ptr(), result.data.get_size(), w, h);
            titleformat_provider::set_status_artwork_loaded(api_name.c_str(), w, h, (size_t)result.data.get_size(), result.is_acrcloud_recognized);

            cancel_acrcloud_tasks(); // Cancel any pending background ACRCloud sampling tasks
            pfc::string8 disp_artist;
            if (!g_last_stream_artist_full.is_empty()) {
                disp_artist = g_last_stream_artist_full;
            } else if (!result.artist.is_empty()) {
                disp_artist = result.artist;
            } else {
                disp_artist = artist;
            }
            pfc::string8 disp_title = !result.title.is_empty() ? result.title : track;
            pfc::string8 disp_album = result.album;

            if (cfg_normalize_api_metadata_case) {
                disp_title = MetadataCleaner::to_title_case(disp_title.c_str()).c_str();
                if (!disp_album.is_empty()) {
                    disp_album = MetadataCleaner::to_title_case(disp_album.c_str()).c_str();
                }
                result.title = disp_title;
                result.album = disp_album;

                std::lock_guard<std::mutex> lock(g_in_flight_mutex);
                auto it = g_api_dedup_map.find(api_dedup_key);
                if (it != g_api_dedup_map.end()) {
                    it->second.result = result;
                }
            }

            if (cfg_enable_disk_cache || cfg_single_file_cache) {
                if (!cache_key.is_empty()) {
                    async_io_manager::instance().cache_set_async(cache_key, result.data);
                    async_io_manager::instance().cache_set_metadata(cache_key, disp_artist, disp_title, disp_album, api_name);
                }
                if (cfg_single_file_cache) {
                    async_io_manager::instance().cache_set_async("current", result.data);
                    async_io_manager::instance().cache_set_metadata("current", disp_artist, disp_title, disp_album, api_name);
                }
            }
            pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(cache_key);
            metadb_handle_ptr now_track = g_active_playing_track;
            titleformat_provider::set_track_artwork_info(now_track, disp_artist.c_str(), disp_title.c_str(), cache_file.c_str(), api_name.c_str(), disp_artist.c_str(), disp_album.c_str());
            std::vector<uint8_t> vec(result.data.get_ptr(), result.data.get_ptr() + result.data.get_size());
            g_current_artwork_source = api_name;
            if (g_manual_search_generation != generation) create_bitmap_from_image_data(vec);
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

    pfc::string8 status_str = pfc::string8("Querying ") + current_api_name + "...";
    titleformat_provider::set_status(status_str.c_str());

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
    const uint64_t generation = g_search_generation.load();
    const bool is_local = track.is_valid() && !is_internet_stream_track(track);
    auto complete = [callback, generation](const artwork_result& result) {
        async_io_manager::instance().post_to_main_thread([callback, generation, result]() {
            if (generation != g_search_generation.load()) return;
            if (result.success) {
                g_active_resolved_provider = "Local artwork";
                g_active_source = "Local artwork";
                titleformat_provider::set_status("Artwork loaded from Local artwork");
            }
            callback(result);
        });
    };

    async_io_manager::instance().submit_task([track, is_local, generation, complete]() {
        if (generation != g_search_generation.load()) return;
        artwork_result result;
        if (!track.is_valid()) {
            result.error_message = "Invalid metadb handle";
            complete(result);
            return;
        }

        const GUID artwork_ids[] = {
            album_art_ids::cover_front,
            album_art_ids::disc,
            album_art_ids::artist,
            album_art_ids::icon,
            album_art_ids::cover_back
        };
        auto accept_artwork = [&result, &complete](album_art_data::ptr art_data) {
            if (!art_data.is_valid() || art_data->get_size() == 0) return false;
            result.mime_type = detect_mime_type(
                static_cast<const t_uint8*>(art_data->get_ptr()), art_data->get_size());
            if (!is_supported_image_format(result.mime_type)) return false;
            result.data.set_size(art_data->get_size());
            memcpy(result.data.get_ptr(), art_data->get_ptr(), art_data->get_size());
            result.success = true;
            result.source = "Local artwork";
            complete(result);
            return true;
        };

        // Read the audio file's embedded images directly. The general artwork
        // manager follows global display/search preferences and registered
        // fallbacks, so a miss there does not establish that the file has no art.
        if (is_local) {
            try {
                auto embedded = album_art_extractor::g_open(nullptr, track->get_path(), fb2k::noAbort);
                for (const auto& id : artwork_ids) {
                    try {
                        if (accept_artwork(embedded->query(id, fb2k::noAbort))) return;
                    } catch (...) {
                        // Missing artwork type: try the remaining embedded images.
                    }
                }
            } catch (...) {
                // No embedded extractor (e.g. a proxy track): use the manager below.
            }
        }

        // Preserve folder artwork, configured search patterns and proxy support.
        try {
            static_api_ptr_t<album_art_manager_v2> aam;
            for (const auto& id : artwork_ids) {
                try {
                    auto extractor = aam->open(pfc::list_single_ref_t<metadb_handle_ptr>(track),
                                             pfc::list_single_ref_t<GUID>(id), fb2k::noAbort);
                    if (accept_artwork(extractor->query(id, fb2k::noAbort))) return;
                } catch (...) {
                    // Continue to the next artwork type.
                }
            }
        } catch (...) {}

        result.error_message = "No supported embedded or folder artwork found";
        complete(result);
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
        pfc::string8 album_name;
        pfc::string8 api_artist;
        pfc::string8 api_title;
        if (!parse_itunes_json(artist_str, track_str, response, artwork_url, &album_name, &api_artist, &api_title)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in itunes response";
            callback(result);
            return;
        }

        // Download the artwork image with 600x600 fallback if 1200x1200 fails
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback, artwork_url, album_name, api_artist, api_title](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            if (success && data.get_size() > 0) {
                artwork_result result;
                result.success = true;
                result.data = data;
                result.album = album_name;
                result.artist = api_artist;
                result.title = api_title;
                result.mime_type = detect_mime_type(data.get_ptr(), data.get_size());
                result.source = "iTunes";  // Set source for OSD display
                callback(result);
            } else {
                pfc::string8 fallback_url = artwork_url;
                fallback_url.replace_string("1200x1200", "600x600");
                if (fallback_url != artwork_url) {
                    async_io_manager::instance().http_get_binary_async(fallback_url, [callback, album_name, api_artist, api_title](bool success2, const pfc::array_t<t_uint8>& data2, const pfc::string8& error2) {
                        artwork_result result;
                        if (success2 && data2.get_size() > 0) {
                            result.success = true;
                            result.data = data2;
                            result.album = album_name;
                            result.artist = api_artist;
                            result.title = api_title;
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
    
    // Constrain the release search by song, rather than matching arbitrary
    // occurrences of the artist/title words anywhere in a release.
    pfc::string8 url = "https://api.discogs.com/database/search?type=release&artist=";
    url << url_encode(artist);
    url << "&track=" << url_encode(track);

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
        pfc::string8 album_name;
        pfc::string8 api_artist;
        pfc::string8 api_title;
        if (!parse_discogs_json(artist_str, track_str, response, artwork_url, &album_name, &api_artist, &api_title)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in Discogs response";
            callback(result);
            return;
        }
       
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback, artwork_url, album_name, api_artist, api_title](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.album = album_name;
                result.artist = api_artist;
                result.title = api_title;
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
        pfc::string8 album_name;
        pfc::string8 api_artist;
        pfc::string8 api_title;
        if (!parse_lastfm_json(response, artwork_url, &album_name, &api_artist, &api_title)) {
            artwork_result result;
            result.success = false;
            result.error_message = "No artwork found in Last.fm response";
            callback(result);
            return;
        }
        
        // Download the artwork image
        async_io_manager::instance().http_get_binary_async(artwork_url, [callback, album_name, api_artist, api_title](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
            artwork_result result;
            if (success && data.get_size() > 0) {
                result.success = true;
                result.data = data;
                result.album = album_name;
                result.artist = api_artist;
                result.title = api_title;
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

// Deezer's free-text results contain covers, compilations and alternate recordings.
// Popularity (rank) says nothing about which release the listener is playing.
static std::string deezer_string(const json& object, const char* key) {
    auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : "";
}

static std::string deezer_artist_key(std::string artist) {
    // Radio metadata commonly spells AC/DC as AC DC. Preserve word boundaries.
    std::replace(artist.begin(), artist.end(), '/', ' ');
    return normalize_for_matching(strip_the_prefix(artist));
}

static bool deezer_same_artist(const std::string& a, const std::string& b) {
    const auto key = deezer_artist_key(a);
    return !key.empty() && pfc::stringCompareCaseInsensitive(key.c_str(), deezer_artist_key(b).c_str()) == 0;
}

static int deezer_artist_quality(const std::string& result, const std::string& requested) {
    if (deezer_same_artist(result, requested)) return 0;
    // Permit primary-artist credits, but never accept an unrelated lead artist
    // just because the requested artist appears as a guest or in a tribute name.
    const auto primary_result = MetadataCleaner::extract_first_artist(result.c_str());
    const auto primary_requested = MetadataCleaner::extract_first_artist(requested.c_str());
    return deezer_same_artist(primary_result, primary_requested) ? 1 : -1;
}

static std::string deezer_without_remaster(const std::string& title) {
    // A remaster preserves the recording. Do not strip arbitrary parentheses:
    // "Live", "Remix", "Acoustic" and meaningful title text must stay distinct.
    static const std::regex suffix(
        R"(\s*(?:\((?:[0-9]{4}\s+)?remaster(?:ed)?(?:\s+[0-9]{4})?\)|\[(?:[0-9]{4}\s+)?remaster(?:ed)?(?:\s+[0-9]{4})?\]|-\s*(?:[0-9]{4}\s+)?remaster(?:ed)?(?:\s+[0-9]{4})?)\s*$)",
        std::regex_constants::icase);
    return std::regex_replace(title, suffix, "");
}

static int deezer_title_quality(const json& item, const std::string& requested) {
    auto title = deezer_string(item, "title");
    if (title.empty() || requested.empty()) return -1;
    const auto short_title = deezer_string(item, "title_short");
    const auto version = deezer_string(item, "title_version");
    // Some catalog entries keep the version only in title_version.
    if (!version.empty() && title == short_title) title += " " + version;
    if (strings_match_fuzzy(title, requested)) return 0;
    const auto base = deezer_without_remaster(title);
    const auto requested_base = deezer_without_remaster(requested);
    if (!base.empty() && strings_match_fuzzy(base, requested_base)) return 0;
    // Retain the version fallback added for Issue #54, after studio/remaster
    // matches, and never remove an explicitly requested live/remix qualifier.
    return !short_title.empty() && strings_match_fuzzy(short_title, requested) ? 1 : -1;
}

static int deezer_release_penalty(const json& album, const std::string& artist) {
    const auto type = deezer_string(album, "record_type");
    if (type == "compile") return 2;
    auto album_artist = album.find("artist");
    const auto name = album_artist != album.end() ? deezer_string(*album_artist, "name") : "";
    if (!name.empty() && deezer_artist_quality(name, artist) < 0) return 2;
    static const std::regex compilation(
        R"(\b(best of|greatest hits|the hits|all the hits|essential|anthology|collection|compilation|various artists)\b)",
        std::regex_constants::icase);
    if (std::regex_search(deezer_string(album, "title"), compilation)) return 2;
    // Verified artist releases outrank unknown releases. If detail requests fail,
    // keep a validated track available instead of turning a usable cover into a miss.
    return !name.empty() && (type == "album" || type == "ep" || type == "single") ? 0 : 1;
}

static std::string deezer_cover_url(const json& album) {
    auto url = deezer_string(album, "cover_xl");
    if (url.empty()) url = deezer_string(album, "cover_big");
    return url;
}

struct deezer_candidate {
    size_t index;
    int title_quality;
    int artist_quality;
    int release_penalty;
};

static std::vector<deezer_candidate> deezer_candidates(const json& data,
    const std::string& artist, const std::string& track) {
    std::vector<deezer_candidate> candidates;
    if (artist.empty() || track.empty() || !data.is_object() || data.contains("error")) return candidates;
    auto results = data.find("data");
    if (results == data.end() || !results->is_array()) return candidates;
    for (size_t index = 0; index < results->size(); ++index) {
        const auto& item = (*results)[index];
        if (!item.is_object()) continue;
        auto result_artist = item.find("artist");
        auto album = item.find("album");
        if (result_artist == item.end() || !result_artist->is_object() ||
            album == item.end() || !album->is_object() || deezer_cover_url(*album).empty()) continue;
        const int artist_quality = deezer_artist_quality(deezer_string(*result_artist, "name"), artist);
        const int title_quality = deezer_title_quality(item, track);
        if (artist_quality < 0 || title_quality < 0) continue;
        candidates.push_back({index, title_quality, artist_quality, deezer_release_penalty(*album, artist)});
    }
    // Stable ties preserve the service's relevance order; never sort by popularity.
    std::stable_sort(candidates.begin(), candidates.end(), [](const deezer_candidate& a, const deezer_candidate& b) {
        return std::tie(a.title_quality, a.artist_quality, a.release_penalty) <
               std::tie(b.title_quality, b.artist_quality, b.release_penalty);
    });
    return candidates;
}

static void enrich_deezer_albums_async(json data, const std::vector<deezer_candidate>& candidates,
    uint64_t generation, std::function<void(const pfc::string8&)> callback) {
    // Search results omit album artist/type. Check a bounded set of distinct
    // matching releases concurrently; never fetch details for unrelated tracks.
    std::set<uint64_t> album_ids;
    for (const auto& candidate : candidates) {
        const auto& album = data["data"][candidate.index]["album"];
        auto id = album.find("id");
        if (id != album.end() && id->is_number_integer() && id->get<int64_t>() > 0) {
            album_ids.insert(id->get<uint64_t>());
            if (album_ids.size() == 5) break;
        }
    }
    if (album_ids.empty()) {
        callback(data.dump().c_str());
        return;
    }
    struct lookup_state {
        json data;
        size_t remaining;
        std::mutex mutex;
    };
    auto state = std::make_shared<lookup_state>();
    state->data = std::move(data);
    state->remaining = album_ids.size();
    for (const auto album_id : album_ids) {
        if (generation != g_search_generation.load()) return;
        pfc::string8 url = "https://api.deezer.com/album/";
        url << std::to_string(album_id).c_str();
        auto completed = [state, album_id, generation, callback](bool success, const pfc::string8& response, const pfc::string8&) {
            if (generation != g_search_generation.load()) return;
            pfc::string8 enriched;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                try {
                    const auto details = success ? json::parse(response.c_str()) : json();
                    auto id = details.find("id");
                    if (details.is_object() && !details.contains("error") && id != details.end() &&
                        id->is_number_integer() && id->get<uint64_t>() == album_id) {
                        for (auto& item : state->data["data"]) {
                            if (!item.is_object() || !item.contains("album") || !item["album"].is_object()) continue;
                            auto& album = item["album"];
                            if (!album.contains("id") || album["id"] != album_id) continue;
                            for (const auto* field : {"artist", "record_type"}) {
                                if (details.contains(field)) album[field] = details[field];
                            }
                        }
                    }
                } catch (...) {} // Malformed or unavailable details leave the track usable.
                if (--state->remaining != 0) return;
                enriched = state->data.dump().c_str();
            }
            callback(enriched);
        };
        try {
            async_io_manager::instance().http_get_async(url, completed);
        } catch (...) {
            completed(false, "", "");
        }
    }
}

static pfc::string8 deezer_search_query(const char* artist, const char* track, bool quoted) {
    // Quotes/backslashes in tags must not break the query's phrase boundaries.
    auto clean = [](const char* text) {
        std::string value = text ? text : "";
        for (char& c : value) if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20) c = ' ';
        return value;
    };
    const auto a = clean(artist);
    const auto t = clean(track);
    const auto query = quoted ? "\"" + a + "\" \"" + t + "\"" : a + " " + t;
    return query.c_str();
}

void artwork_manager::perform_deezer_fallback_search(const char* artist, const char* track, artwork_callback callback) {
    // Retry without phrase constraints, retaining BOTH artist and title.
    search_deezer_query_async(artist, track, callback, false);
}

void artwork_manager::search_deezer_api_async(const char* artist, const char* track, artwork_callback callback) {
    search_deezer_query_async(artist, track, callback, true);
}

void artwork_manager::search_deezer_query_async(const char* artist, const char* track, artwork_callback callback, bool quoted) {
    const uint64_t generation = g_search_generation.load();
    const pfc::string8 artist_str = artist ? artist : "";
    const pfc::string8 track_str = track ? track : "";
    if (artist_str.is_empty() || track_str.is_empty()) {
        artwork_result result;
        result.error_message = "Deezer requires both artist and title";
        callback(result);
        return;
    }
    // Field filters can return empty results for available songs. Keep free text,
    // but inspect enough track candidates to find releases below compilations.
    pfc::string8 url = "https://api.deezer.com/search/track?q=";
    url << url_encode(deezer_search_query(artist, track, quoted)) << "&limit=50";
    try {
        async_io_manager::instance().http_get_async(url, [artist_str, track_str, callback, quoted, generation](bool success, const pfc::string8& response, const pfc::string8& error) {
            if (generation != g_search_generation.load()) return;
            if (!success) {
                artwork_result result;
                result.error_message = "Deezer API request failed: ";
                result.error_message << error;
                callback(result);
                return;
            }
            json data;
            std::vector<deezer_candidate> candidates;
            try {
                data = json::parse(response.c_str());
                candidates = deezer_candidates(data, artist_str.c_str(), track_str.c_str());
            } catch (...) {}
            if (candidates.empty()) {
                if (quoted) {
                    perform_deezer_fallback_search(artist_str, track_str, callback);
                } else {
                    artwork_result result;
                    result.error_message = "No matching artist and title found in Deezer";
                    callback(result);
                }
                return;
            }
            enrich_deezer_albums_async(std::move(data), candidates, generation,
                [artist_str, track_str, callback, generation](const pfc::string8& enriched) {
                    if (generation != g_search_generation.load()) return;
                    pfc::string8 artwork_url, album_name, api_artist, api_title;
                    if (!parse_deezer_json(artist_str, track_str, enriched, artwork_url, &album_name, &api_artist, &api_title)) {
                        artwork_result result;
                        result.error_message = "No usable Deezer artwork";
                        callback(result);
                        return;
                    }
                    async_io_manager::instance().http_get_binary_async(artwork_url,
                        [callback, album_name, api_artist, api_title, generation](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
                            if (generation != g_search_generation.load()) return;
                            artwork_result result;
                            if (success && data.get_size() > 0) {
                                result.success = true;
                                result.data = data;
                                result.album = album_name;
                                result.artist = api_artist;
                                result.title = api_title;
                                result.mime_type = artwork_manager::detect_mime_type(data.get_ptr(), data.get_size());
                                result.source = "Deezer";
                            } else {
                                result.error_message = "Failed to download Deezer artwork: ";
                                result.error_message << error;
                            }
                            callback(result);
                        });
                });
        });
    } catch (...) {
        artwork_result result;
        result.error_message = "Exception in Deezer HTTP request";
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

void artwork_manager::validate_and_complete_result(const pfc::array_t<t_uint8>& data, artwork_callback callback, const char* cache_key) {
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
    
    if (cache_key && cache_key[0] != '\0') {
        pfc::string8 c_artist, c_title, c_album, c_source;
        if (async_io_manager::instance().cache_get_metadata(cache_key, c_artist, c_title, c_album, c_source)) {
            if (cfg_normalize_api_metadata_case) {
                if (!c_title.is_empty()) c_title = MetadataCleaner::to_title_case(c_title.c_str()).c_str();
                if (!c_album.is_empty()) c_album = MetadataCleaner::to_title_case(c_album.c_str()).c_str();
            }
            result.artist = c_artist;
            result.title = c_title;
            result.album = c_album;
            if (!c_source.is_empty() && result.source == "Cache") result.source = c_source;
        }
    }

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
    bool is_internet_stream = is_internet_stream_track(track);

    extract_track_metadata_dynamic(track, artist, track_name);

    if (is_internet_stream) {
        StreamMetadataResult meta = MetadataCleaner::sanitize_stream_metadata(artist.c_str(), track_name.c_str());
        if (meta.is_valid_search && !meta.clean_artist.empty() && !meta.clean_title.empty()) {
            return generate_cache_key(meta.clean_artist.c_str(), meta.clean_title.c_str());
        }
        if (!artist.is_empty() && !track_name.is_empty() && artist != "Unknown Artist" && track_name != "Unknown Track") {
            return generate_cache_key(artist.c_str(), track_name.c_str());
        }
        // Do not generate a generic "Unknown Artist_Unknown Track" cache key for untagged streams
        return pfc::string8();
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
bool artwork_manager::parse_itunes_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url, pfc::string8* out_album, pfc::string8* out_artist, pfc::string8* out_title) {
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
                        if (out_album && item.contains("collectionName") && item["collectionName"].is_string()) {
                            std::string alb = item["collectionName"].get<std::string>();
                            if (cfg_normalize_api_metadata_case) alb = MetadataCleaner::to_title_case(alb);
                            *out_album = alb.c_str();
                        }
                        if (out_artist) *out_artist = result_artist.c_str();
                        if (out_title) *out_title = (cfg_normalize_api_metadata_case ? MetadataCleaner::to_title_case(result_track) : result_track).c_str();
                        return true;
                    }
                }
            }
        }

    } catch (...) {
        return false;
    }

    return false;
}

bool artwork_manager::parse_deezer_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url, pfc::string8* out_album, pfc::string8* out_artist, pfc::string8* out_title) {
    try {
        const auto data = json::parse(json_in.c_str());
        const auto candidates = deezer_candidates(data, artist ? artist : "", track ? track : "");
        if (candidates.empty()) return false;
        const auto& item = data["data"][candidates.front().index];
        const auto& album = item["album"];
        // JSON parsing already unescapes URLs. Keep the provider's actual image
        // size/path instead of inventing a 1200x1200 URL that may not exist.
        artwork_url = deezer_cover_url(album).c_str();
        auto album_name = deezer_string(album, "title");
        auto title = deezer_string(item, "title");
        if (cfg_normalize_api_metadata_case) {
            album_name = MetadataCleaner::to_title_case(album_name);
            title = MetadataCleaner::to_title_case(title);
        }
        if (out_album) *out_album = album_name.c_str();
        if (out_artist) *out_artist = deezer_string(item["artist"], "name").c_str();
        if (out_title) *out_title = title.c_str();
        return true;
    } catch (...) {
        return false;
    }
}

bool artwork_manager::parse_lastfm_json(const pfc::string8& json_in, pfc::string8& artwork_url, pfc::string8* out_album, pfc::string8* out_artist, pfc::string8* out_title) {
    try {
        std::string json_data;
        json_data += json_in;

        json data = json::parse(json_data);

        if (data.contains("message") && data["message"] == "Track not found") return false;
        if (!data.contains("track") || !data["track"].contains("album") || !data["track"]["album"].contains("image") || !data["track"]["album"]["image"].is_array()) {
            return false;
        }

        auto extract_meta = [&]() {
            if (out_album && data.contains("track") && data["track"].contains("album") && data["track"]["album"].contains("title") && data["track"]["album"]["title"].is_string()) {
                std::string alb = data["track"]["album"]["title"].get<std::string>();
                if (cfg_normalize_api_metadata_case) alb = MetadataCleaner::to_title_case(alb);
                *out_album = alb.c_str();
            }
            if (out_artist && data.contains("track") && data["track"].contains("artist")) {
                std::string art;
                if (data["track"]["artist"].is_object() && data["track"]["artist"].contains("name") && data["track"]["artist"]["name"].is_string()) {
                    art = data["track"]["artist"]["name"].get<std::string>();
                } else if (data["track"]["artist"].is_string()) {
                    art = data["track"]["artist"].get<std::string>();
                }
                if (!art.empty()) {
                    *out_artist = art.c_str();
                }
            }
            if (out_title && data.contains("track") && data["track"].contains("name") && data["track"]["name"].is_string()) {
                std::string tit = data["track"]["name"].get<std::string>();
                if (cfg_normalize_api_metadata_case) tit = MetadataCleaner::to_title_case(tit);
                *out_title = tit.c_str();
            }
        };

        json s = data["track"]["album"]["image"];

        for (const auto& item : s.items()) {
            if (item.value().contains("size") && item.value()["size"].is_string() && item.value()["size"].get<std::string>() == "extralarge") {
                if (item.value().contains("#text") && item.value()["#text"].is_string()) {
                    artwork_url = item.value()["#text"].get<std::string>().c_str();
                    artwork_url = artwork_url.replace("u/300x300", "u/");
                    extract_meta();
                    return true;
                }
            }
        }

        for (const auto& item : s.items()) {
            if (item.value().contains("size") && item.value()["size"].is_string() && item.value()["size"].get<std::string>() == "large") {
                if (item.value().contains("#text") && item.value()["#text"].is_string()) {
                    artwork_url = item.value()["#text"].get<std::string>().c_str();
                    artwork_url = artwork_url.replace("u/174s", "u/");
                    extract_meta();
                    return true;
                }
            }
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool artwork_manager::parse_discogs_json(const char* artist, const char* track, const pfc::string8& json_in, pfc::string8& artwork_url, pfc::string8* out_album, pfc::string8* out_artist, pfc::string8* out_title) {
    try {
        json data = json::parse(std::string(json_in.c_str()));
        if (!data.contains("results") || !data["results"].is_array()) return false;
        const std::string artist_str = artist ? artist : "";
        const std::string track_str = track ? track : "";
        if (artist_str.empty() || track_str.empty()) return false;

        // Results come from an artist + track-filtered release search.
        // Prefer a release named after the song, but also allow an album
        // containing it. Discogs' "title" is Artist - RELEASE, not a song title.
        for (int match_pass = 0; match_pass < 2; ++match_pass) {
            for (const auto& item : data["results"]) {
                if (!item.contains("title") || !item["title"].is_string()) continue;
                const std::string release = item["title"].get<std::string>();
                const size_t sep = release.find(" - ");
                if (sep == std::string::npos) continue;
                const std::string release_artist = release.substr(0, sep);
                std::string album = release.substr(sep + 3);
                if (!artists_match(release_artist, artist_str)) continue;
                if (match_pass == 0 && !strings_match_fuzzy(album, track_str)) continue;

                std::string image_url;
                if (item.contains("cover_image") && item["cover_image"].is_string())
                    image_url = item["cover_image"].get<std::string>();
                if (image_url.empty() && item.contains("thumb") && item["thumb"].is_string())
                    image_url = item["thumb"].get<std::string>();
                if (image_url.empty()) continue;

                artwork_url = image_url.c_str();
                if (cfg_normalize_api_metadata_case) album = MetadataCleaner::to_title_case(album);
                if (out_album) *out_album = album.c_str();
                if (out_artist) *out_artist = release_artist.c_str();
                // Preserve the requested song; the search response does not
                // provide canonical track metadata.
                if (out_title) *out_title = (cfg_normalize_api_metadata_case
                    ? MetadataCleaner::to_title_case(track_str) : track_str).c_str();
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
