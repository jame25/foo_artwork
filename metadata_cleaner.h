#pragma once
#include "stdafx.h"
#include <string>
#include <regex>
#include <vector>

// Unified UTF-8 safe metadata cleaning for consistent artwork search results
// across Default UI and Columns UI modes

struct StreamMetadataResult {
    std::string raw_artist;
    std::string raw_title;
    std::string clean_artist;
    std::string clean_title;
    std::string first_artist;
    std::string second_artist;
    std::string primary_title;
    bool is_valid_search = false;
    bool is_station_or_url = false;
};

class MetadataCleaner {
public:
    // Main 4-Stage Stream Metadata Sanitizer
    static StreamMetadataResult sanitize_stream_metadata(const char* raw_artist, const char* raw_title);

    // Station name / stream URL detector
    static bool is_station_name_or_url(const char* text);

    // Main cleaning function - UTF-8 safe for Latin, Cyrillic, and other scripts
    static std::string clean_for_search(const char* metadata, bool preserve_cyrillic = true);
    
    // Validation function to check if metadata is suitable for artwork search
    static bool is_valid_for_search(const char* artist, const char* title);
    
    // Extract only the first artist from multi-artist string for better artwork search results
    static std::string extract_first_artist(const char* artist);

    // Extract second/project artist from multi-artist string (e.g. Gouryella from Ferry Corsten pres. Gouryella)
    static std::string extract_second_artist(const char* artist);

    // Extract primary track title (title before extra album/bracket tags)
    static std::string extract_primary_title(const char* title);
    
private:
    // Core cleaning operations - UTF-8 safe
    static std::string remove_timestamps(const std::string& str);
    static std::string remove_parenthetical_content(const std::string& str, bool preserve_cyrillic = true);
    static std::string remove_bracketed_content(const std::string& str, bool preserve_cyrillic = true);
    static std::string normalize_quotes_and_apostrophes(const std::string& str);
    static std::string normalize_collaborations(const std::string& str);
    static std::string normalize_whitespace(const std::string& str);
    
    // UTF-8 character detection and preservation
    static bool contains_cyrillic(const std::string& str);
    static bool is_multibyte_utf8_sequence(const std::string& str, size_t pos);
    static std::string preserve_important_characters(const std::string& str);
    
    // Helper functions
    static std::string trim(const std::string& str);
    static bool is_common_remix_term(const std::string& term);
    static bool is_featuring_pattern(const std::string& term);
    static bool is_likely_collaboration(const std::string& artist_str, const std::string& separator, size_t pos);
};
