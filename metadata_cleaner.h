#pragma once
#include "stdafx.h"
#include <string>
#include <regex>
#include <vector>

// Unified UTF-8 safe metadata cleaning for consistent artwork search results
// across Default UI and Columns UI modes
class MetadataCleaner {
public:
    // Main cleaning function - UTF-8 safe for Latin, Cyrillic, and other scripts
    static std::string clean_for_search(const char* metadata, bool preserve_cyrillic = true);
    
    // Validation function to check if metadata is suitable for artwork search
    static bool is_valid_for_search(const char* artist, const char* title);
    
    // Extract only the first artist from multi-artist string for better artwork search results
    static std::string extract_first_artist(const char* artist);
    
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
};
