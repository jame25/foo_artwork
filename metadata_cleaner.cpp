#include "stdafx.h"
#include "metadata_cleaner.h"
#include <algorithm>
#include <cctype>

std::string MetadataCleaner::clean_for_search(const char* metadata, bool preserve_cyrillic) {
    if (!metadata || strlen(metadata) == 0) {
        return "";
    }
    
    std::string str(metadata);
    
    // Use v1.3.1's proven approach: simple hex byte replacements for UTF-8 safety
    // This approach preserves Cyrillic and other non-Latin characters correctly
    
    // Handle all variants of apostrophes and quotes (UTF-8 safe using hex sequences)
    size_t pos = 0;
    while ((pos = str.find("\xE2\x80\x98", pos)) != std::string::npos) { // Left single quotation mark
        str.replace(pos, 3, "'");
        pos += 1;
    }
    pos = 0;
    while ((pos = str.find("\xE2\x80\x99", pos)) != std::string::npos) { // Right single quotation mark  
        str.replace(pos, 3, "'");
        pos += 1;
    }
    pos = 0;
    while ((pos = str.find("\xE2\x80\x9A", pos)) != std::string::npos) { // Single low-9 quotation mark
        str.replace(pos, 3, "'");
        pos += 1;
    }
    
    // Remove timestamp patterns at the end (from v1.5.8)
    // Pattern 1: " - MM:SS" or " - M:SS" (like " - 0:00")
    str = std::regex_replace(str, std::regex("\\s+-\\s+\\d{1,2}:\\d{2}\\s*$"), "");
    
    // Pattern 2: " - MM.SS" or " - M.SS" (like " - 0.00") - handle decimal point
    str = std::regex_replace(str, std::regex("\\s+-\\s+\\d{1,2}\\.\\d{2}\\s*$"), "");
    
    // Remove parenthetical timestamps (MM:SS) or (M:SS)
    str = std::regex_replace(str, std::regex("\\s*\\(\\d{1,2}:\\d{2}\\)\\s*"), " ");
    
    // Remove parenthetical content (respects preserve_cyrillic parameter)
    str = remove_parenthetical_content(str, preserve_cyrillic);

    // Remove bracketed content (respects preserve_cyrillic parameter)
    str = remove_bracketed_content(str, preserve_cyrillic);

    // Remove everything after pipe | (like "Hit 'N Run Lover || 4153 || S || 2ca82642-1c07-42f0-972b-1a663c1c39b9")
    str = std::regex_replace(str, std::regex("\\|.*"), "");
    
    // Remove common prefixes
    std::vector<std::string> prefixes = {
        "Now Playing: ", "Now Playing:", "Live: ", "Live:", "Playing: ", "Playing:",
        "Current: ", "Current:", "On Air: ", "On Air:", "♪ ", "♫ ", "🎵 ", "🎶 "
    };
    
    for (const auto& prefix : prefixes) {
        if (str.substr(0, prefix.length()) == prefix) {
            str = str.substr(prefix.length());
            break; // Only remove the first matching prefix
        }
    }
    
    // Clean up whitespace (safe for all character sets)
    str = std::regex_replace(str, std::regex("\\s{2,}"), " ");
    str = trim(str);
    
    return str;
}

bool MetadataCleaner::is_valid_for_search(const char* artist, const char* title) {
    std::string artist_str = artist ? artist : "";
    std::string title_str = title ? title : "";
    
    // Rule 1: Must have a title - no search without title
    if (title_str.empty() || title_str.length() < 2) {
        return false;
    }
    
    // Rule 2: Block common invalid patterns
    if (title_str == "?" || artist_str == "?" ||
        title_str == "? - ?" || artist_str == "? - ?") {
        return false;
    }
    
    // Rule 3: Block advertisement breaks
    std::string title_lower = title_str;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::tolower);
    if (title_lower.find("adbreak") != std::string::npos) {
        return false;
    }
    
    // Rule 4: Block "Unknown" patterns
    if (title_str == "Unknown Track" || artist_str == "Unknown Artist" ||
        title_str == "Unknown" || artist_str == "Unknown") {
        return false;
    }
    
    return true;
}

std::string MetadataCleaner::remove_timestamps(const std::string& str) {
    std::string result = str;
    
    // Remove timestamp patterns at the end
    // Pattern 1: " - MM:SS" or " - M:SS" (like " - 0:00")
    result = std::regex_replace(result, std::regex("\\s+-\\s+\\d{1,2}:\\d{2}\\s*$"), "");
    
    // Pattern 2: " - MM.SS" or " - M.SS" (like " - 0.00") - handle decimal point
    result = std::regex_replace(result, std::regex("\\s+-\\s+\\d{1,2}\\.\\d{2}\\s*$"), "");
    
    // Remove parenthetical timestamps (MM:SS) or (M:SS)
    result = std::regex_replace(result, std::regex("\\s*\\(\\d{1,2}:\\d{2}\\)\\s*"), " ");
    
    // Remove everything after pipe | (like "Title || extra || data")
    result = std::regex_replace(result, std::regex("\\|.*"), "");
    
    return result;
}

std::string MetadataCleaner::remove_parenthetical_content(const std::string& str, bool preserve_cyrillic) {
    // Auto-detect Cyrillic if preserve_cyrillic is true
    bool use_conservative = preserve_cyrillic && contains_cyrillic(str);
    
    if (!preserve_cyrillic || !use_conservative) {
        // Standard removal for Latin scripts
        std::string result = str;
        
        // Remove common remix/version patterns (case insensitive)
        result = std::regex_replace(result, 
            std::regex("\\s*\\((?:live|acoustic|unplugged|remix|remaster|demo|instrumental|explicit|clean|radio edit|extended|single version|album version)(?:\\s+[^)]*)?\\)\\s*", 
            std::regex_constants::icase), " ");
        
        // Remove featuring patterns
        result = std::regex_replace(result,
            std::regex("\\s*\\((?:feat\\.|featuring|ft\\.|with)\\s+[^)]*\\)\\s*", 
            std::regex_constants::icase), " ");
        
        // Remove all remaining parentheses content (aggressive for Latin)
        result = std::regex_replace(result, std::regex("\\s*\\([^)]*\\)\\s*"), " ");
        
        return result;
    } else {
        // Conservative removal for Cyrillic scripts - only remove common patterns
        std::string result = str;
        
        // Only remove very common English patterns that are safe to remove
        result = std::regex_replace(result,
            std::regex("\\s*\\((?:remix|remaster|demo|radio edit|extended)\\)\\s*",
            std::regex_constants::icase), " ");
        
        // Remove explicit/clean markers (safe for all languages)
        result = std::regex_replace(result,
            std::regex("\\s*\\((?:explicit|clean)\\)\\s*",
            std::regex_constants::icase), " ");
            
        return result;
    }
}

std::string MetadataCleaner::remove_bracketed_content(const std::string& str, bool preserve_cyrillic) {
    // Auto-detect Cyrillic if preserve_cyrillic is true
    bool use_conservative = preserve_cyrillic && contains_cyrillic(str);
    
    if (!preserve_cyrillic || !use_conservative) {
        // Standard removal for Latin scripts
        return std::regex_replace(str, std::regex("\\s*\\[[^\\]]*\\]\\s*"), " ");
    } else {
        // Conservative removal for Cyrillic scripts
        std::string result = str;
        
        // Only remove common English patterns in brackets
        result = std::regex_replace(result,
            std::regex("\\s*\\[(?:remix|remaster|demo|radio edit|extended)\\]\\s*",
            std::regex_constants::icase), " ");
            
        return result;
    }
}

std::string MetadataCleaner::normalize_quotes_and_apostrophes(const std::string& str) {
    std::string result = str;
    
    // UTF-8 safe quote normalization
    std::vector<std::pair<std::string, std::string>> quote_patterns = {
        // Left and right single quotation marks
        {"\u2018", "'"}, {"\u2019", "'"}, {"\u201A", "'"},
        // Left and right double quotation marks  
        {"\u201C", "\""}, {"\u201D", "\""}, {"\u201E", "\""},
        // Other quote-like characters
        {"\u2039", "<"}, {"\u203A", ">"},
        // Prime marks (often confused with quotes)
        {"\u2032", "'"}, {"\u2033", "\""}
    };
    
    for (const auto& pattern : quote_patterns) {
        size_t pos = 0;
        while ((pos = result.find(pattern.first, pos)) != std::string::npos) {
            result.replace(pos, pattern.first.length(), pattern.second);
            pos += pattern.second.length();
        }
    }
    
    return result;
}

std::string MetadataCleaner::normalize_collaborations(const std::string& str) {
    std::string result = str;
    
    // Normalize featuring patterns (preserve case for Cyrillic names)
    std::vector<std::pair<std::string, std::string>> feat_patterns = {
        {" ft. ", " feat. "}, {" ft ", " feat. "}, {" featuring ", " feat. "},
        {" Ft. ", " feat. "}, {" Ft ", " feat. "}, {" Featuring ", " feat. "},
        {" FT. ", " feat. "}, {" FT ", " feat. "}, {" FEATURING ", " feat. "}
    };
    
    for (const auto& pattern : feat_patterns) {
        size_t pos = 0;
        while ((pos = result.find(pattern.first, pos)) != std::string::npos) {
            result.replace(pos, pattern.first.length(), pattern.second);
            pos += pattern.second.length();
        }
    }
    
    // Normalize & patterns (be careful not to break band names)
    result = std::regex_replace(result, std::regex("\\s+&\\s+"), " & ");
    
    return result;
}

std::string MetadataCleaner::normalize_whitespace(const std::string& str) {
    // Clean up multiple spaces (safe for all character encodings)
    std::string result = std::regex_replace(str, std::regex("\\s{2,}"), " ");
    return result;
}

bool MetadataCleaner::contains_cyrillic(const std::string& str) {
    // Check for Cyrillic Unicode range (U+0400 to U+04FF)
    // In UTF-8, this is encoded as 0xD0 0x80 to 0xD3 0xBF
    for (size_t i = 0; i < str.length() - 1; ++i) {
        unsigned char byte1 = static_cast<unsigned char>(str[i]);
        unsigned char byte2 = static_cast<unsigned char>(str[i + 1]);
        
        // Check for Cyrillic range
        if ((byte1 == 0xD0 && byte2 >= 0x80) ||  // U+0400-U+047F
            (byte1 == 0xD1 && byte2 <= 0xBF) ||  // U+0480-U+04FF  
            (byte1 == 0xD2 && byte2 <= 0xBF) ||  // U+0500-U+052F (Cyrillic Supplement)
            (byte1 == 0xD3 && byte2 <= 0xBF)) {  // Extended Cyrillic
            return true;
        }
    }
    return false;
}

bool MetadataCleaner::is_multibyte_utf8_sequence(const std::string& str, size_t pos) {
    if (pos >= str.length()) return false;
    
    unsigned char byte = static_cast<unsigned char>(str[pos]);
    
    // Check if this is the start of a multibyte UTF-8 sequence
    return (byte & 0x80) != 0;  // Non-ASCII character
}

std::string MetadataCleaner::preserve_important_characters(const std::string& str) {
    // For Cyrillic and other non-Latin scripts, preserve important punctuation
    // that might be essential for accurate searches
    std::string result = str;
    
    // Don't remove certain punctuation that might be important for Cyrillic titles
    // This is a conservative approach to prevent over-cleaning
    
    return result;
}

std::string MetadataCleaner::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool MetadataCleaner::is_common_remix_term(const std::string& term) {
    std::string lower = term;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return (lower == "remix" || lower == "remaster" || lower == "demo" ||
            lower == "live" || lower == "acoustic" || lower == "unplugged" ||
            lower == "instrumental" || lower == "radio edit" || lower == "extended");
}

bool MetadataCleaner::is_featuring_pattern(const std::string& term) {
    std::string lower = term;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return (lower == "feat." || lower == "featuring" || lower == "ft." || lower == "with");
}

std::string MetadataCleaner::extract_first_artist(const char* artist) {
    if (!artist || strlen(artist) == 0) {
        return "";
    }
    
    std::string artist_str(artist);
    
    // Common multi-artist separators (in order of precedence)
    std::vector<std::string> separators = {
        " feat. ", " ft. ", " featuring ", 
        " & ", " and ", 
        " / ", " // ", " /// ",
        " vs. ", " vs ", " versus ",
        " with ", " w/ ",
        " x ", " X ",
        ", ", "; "
    };
    
    // Find the earliest separator
    size_t earliest_pos = std::string::npos;
    for (const auto& separator : separators) {
        size_t pos = artist_str.find(separator);
        if (pos != std::string::npos && pos < earliest_pos) {
            earliest_pos = pos;
        }
    }
    
    // If we found a separator, extract everything before it
    if (earliest_pos != std::string::npos) {
        artist_str = artist_str.substr(0, earliest_pos);
    }
    
    // Clean up whitespace and return
    return trim(artist_str);
}
