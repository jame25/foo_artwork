#include "stdafx.h"
#include "metadata_cleaner.h"
#include <algorithm>
#include <cctype>

std::string MetadataCleaner::clean_for_search(const char* metadata, bool preserve_cyrillic) {
    if (!metadata || strlen(metadata) == 0) {
        return "";
    }
    
    std::string str(metadata);

    //FIXME
    //resize str to a resonable amount of characters
    //crashed with http://stream.revma.ihrhls.com/zc7934.m3u8 that had 285 characters in bullshit title..
    if (str.length() > 100) str.resize(100);

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

    // Remove UTF-8 BOM
    pos = 0;
    while ((pos = str.find("\xEF\xBB\xBF", pos)) != std::string::npos) { // utf-8 bom
        str.replace(pos, 3, "");
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

    // Remove everything after •  (like "DERNIÈRE DANSE • 00:01/03:17, 7172003159940796416")
    str = std::regex_replace(str, std::regex("\\•.*"), "");

    // Process tidle (like "Electric Light Orchestra~Last Train To London~Discovery~1979")
    std::regex pattern("^(([^~]*~){1}[^~]*)");
    std::smatch match;


    if (std::regex_search(str, match, pattern)) {
        std::string result = match[1];
        str = result;
        str = std::regex_replace(str, std::regex("~"), " - ");
    }
    
    // Replace underscores with spaces (e.g., "Black_Sabbath" -> "Black Sabbath")
    // Common in some internet radio stream metadata
    std::replace(str.begin(), str.end(), '_', ' ');

    // Normalize "[+]" collaboration marker to " & " before bracket removal
    // (e.g., "Chris Cornell [+] Soundgarden" -> "Chris Cornell & Soundgarden")
    pos = 0;
    while ((pos = str.find("[+]", pos)) != std::string::npos) {
        // Determine replacement: if already surrounded by spaces, just use "&"
        size_t start = pos;
        size_t end = pos + 3;
        std::string replacement = " & ";
        // Avoid double spaces: trim adjacent spaces
        if (start > 0 && str[start - 1] == ' ') { start--; }
        if (end < str.length() && str[end] == ' ') { end++; }
        str.replace(start, end - start, replacement);
        pos = start + replacement.length();
    }

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
    
    // Remove common suffixes
    std::vector<std::string> suffixes = {
        "*** www.ipmusic.ch", "Classic Vinyl on walmradio.com","Adroit Jazz Underground on walmradio.com","OTR on walmradio.com" ,"Christmas Vinyl on walmradio.com","walmradio.com"
    };

    for (const auto& suffix : suffixes) {
        if (str.size() >= suffix.size()) {
            if (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0) {
                str.erase(str.size() - suffix.size()); // remove suffix
            }
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
    

    // Rule 1: Must have a artist title - no search without artist title 
    // Prevent searches with empty/invalid metadata that could overwrite good results
    if (artist_str.empty() || title_str.empty() || artist_str.length() < 2 || title_str.length() < 2) {
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
    if (title_lower.find("adbreak") != std::string::npos || title_lower.find("ad_break") != std::string::npos || title_lower.find("advertisement") != std::string::npos) {
        return false;
    }
    
    // Rule 4: Block "Unknown" patterns
    if (title_str == "Unknown Track" || artist_str == "Unknown Artist" ||
        title_str == "Unknown" || artist_str == "Unknown") {
        return false;
    }

    // Rule 5: Block "Known" problematic patterns
    if (artist_str == "RADIO BOB") {
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
        // Pattern 1: (word remix) - like "(Lemongrass Remix)"
        result = std::regex_replace(result,
            std::regex("\\s*\\([^)]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\)\\s*",
            std::regex_constants::icase), " ");
        
        // Pattern 2: (remix word) - like "(Remix by Artist)" 
        result = std::regex_replace(result, 
            std::regex("\\s*\\((?:live|acoustic|unplugged|remix|remaster|demo|instrumental|explicit|clean|radio edit|extended|single version|album version|rmx)(?:\\s+[^)]*)?\\)\\s*", 
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
        // Pattern 1: (word remix) - like "(Lemongrass Remix)"
        result = std::regex_replace(result,
            std::regex("\\s*\\([^)]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\)\\s*",
            std::regex_constants::icase), " ");
        
        // Pattern 2: (remix word) - like "(Remix by Artist)"
        result = std::regex_replace(result,
            std::regex("\\s*\\((?:remix|remaster|demo|radio edit|extended|rmx)\\)\\s*",
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
        std::string result = str;
        
        // Pattern 1: [word remix] - like "[Lemongrass Remix]"
        result = std::regex_replace(result,
            std::regex("\\s*\\[[^\\]]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\]\\s*",
            std::regex_constants::icase), " ");
        
        // Pattern 2: [remix word] - like "[Remix by Artist]"
        result = std::regex_replace(result,
            std::regex("\\s*\\[(?:remix|remaster|demo|radio edit|extended|rmx)[^\\]]*\\]\\s*",
            std::regex_constants::icase), " ");
        
        // Remove all remaining brackets content (aggressive for Latin)
        result = std::regex_replace(result, std::regex("\\s*\\[[^\\]]*\\]\\s*"), " ");
        
        return result;
    } else {
        // Conservative removal for Cyrillic scripts
        std::string result = str;
        
        // Pattern 1: [word remix] - like "[Lemongrass Remix]"
        result = std::regex_replace(result,
            std::regex("\\s*\\[[^\\]]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\]\\s*",
            std::regex_constants::icase), " ");
        
        // Pattern 2: [remix word] - like "[Remix by Artist]"
        result = std::regex_replace(result,
            std::regex("\\s*\\[(?:remix|remaster|demo|radio edit|extended|rmx)\\]\\s*",
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
    
    return (lower == "feat." || lower == "featuring" || lower == "ft." || lower == "with" ||
            lower == "pres." || lower == "pres" || lower == "presents" || lower == "presenting" ||
            lower == "meets" || lower == "w/");
}

std::string MetadataCleaner::extract_first_artist(const char* artist) {
    if (!artist || strlen(artist) == 0) {
        return "";
    }
    
    std::string artist_str(artist);
    
    // High-confidence multi-artist separators (clearly indicate collaborations)
    std::vector<std::string> high_confidence_separators = {
        " feat. ", " ft. ", " featuring ", " feat ", " ft ",
        " / ", " // ", " /// ", "/",
        " vs. ", " vs ", " versus ",
        " with ", " w/ ",
        " x ", " X ",
        " pres. ", " pres ", " presents ", " presenting ",
        " meets ", " intro. ", " introduces ",
        " aka ", " a.k.a. ", " pka ", " p.k.a. ",
        ", ", "; ", ";", ","
    };
    
    // Contextual separators that need additional validation
    std::vector<std::string> contextual_separators = {
        " & ", " and "
    };
    
    // Create lowercase version of artist string for case-insensitive separator search
    std::string artist_lower = artist_str;
    std::transform(artist_lower.begin(), artist_lower.end(), artist_lower.begin(), ::tolower);

    // First, check high-confidence separators
    size_t earliest_pos = std::string::npos;
    for (const auto& separator : high_confidence_separators) {
        std::string sep_lower = separator;
        std::transform(sep_lower.begin(), sep_lower.end(), sep_lower.begin(), ::tolower);
        
        // Protect band names containing slashes like "AC/DC"
        if (sep_lower == "/" && artist_lower.find("ac/dc") != std::string::npos) {
            continue;
        }

        size_t pos = artist_lower.find(sep_lower);
        if (pos != std::string::npos && pos < earliest_pos) {
            earliest_pos = pos;
        }
    }
    
    // If no high-confidence separator found, check contextual separators with validation
    if (earliest_pos == std::string::npos) {
        for (const auto& separator : contextual_separators) {
            std::string sep_lower = separator;
            std::transform(sep_lower.begin(), sep_lower.end(), sep_lower.begin(), ::tolower);
            size_t pos = artist_lower.find(sep_lower);
            if (pos != std::string::npos) {
                // Validate if this is likely a collaboration vs. a band name
                if (is_likely_collaboration(artist_str, separator, pos)) {
                    if (pos < earliest_pos) {
                        earliest_pos = pos;
                    }
                }
            }
        }
    }
    
    // If we found a separator, extract everything before it
    if (earliest_pos != std::string::npos) {
        artist_str = artist_str.substr(0, earliest_pos);
    }
    
    // Clean up whitespace and return
    return trim(artist_str);
}

std::string MetadataCleaner::extract_second_artist(const char* artist) {
    if (!artist || strlen(artist) == 0) {
        return "";
    }
    
    std::string artist_str(artist);
    std::string artist_lower = artist_str;
    std::transform(artist_lower.begin(), artist_lower.end(), artist_lower.begin(), ::tolower);

    std::vector<std::string> high_confidence_separators = {
        " feat. ", " ft. ", " featuring ", " feat ", " ft ",
        " / ", " // ", " /// ", "/",
        " vs. ", " vs ", " versus ",
        " with ", " w/ ",
        " x ", " X ",
        " pres. ", " pres ", " presents ", " presenting ",
        " meets ", " intro. ", " introduces ",
        " aka ", " a.k.a. ", " pka ", " p.k.a. ",
        ", ", "; ", ";", ","
    };

    size_t earliest_pos = std::string::npos;
    size_t sep_len = 0;
    for (const auto& separator : high_confidence_separators) {
        std::string sep_lower = separator;
        std::transform(sep_lower.begin(), sep_lower.end(), sep_lower.begin(), ::tolower);
        
        // Protect band names containing slashes like "AC/DC"
        if (sep_lower == "/" && artist_lower.find("ac/dc") != std::string::npos) {
            continue;
        }

        size_t pos = artist_lower.find(sep_lower);
        if (pos != std::string::npos && pos < earliest_pos) {
            earliest_pos = pos;
            sep_len = separator.length();
        }
    }

    if (earliest_pos != std::string::npos && earliest_pos + sep_len < artist_str.length()) {
        std::string second = artist_str.substr(earliest_pos + sep_len);
        // If second part also contains another separator, get first artist of second part
        return clean_for_search(extract_first_artist(second.c_str()).c_str(), true);
    }

    return "";
}

bool MetadataCleaner::is_station_name_or_url(const char* text) {
    if (!text || strlen(text) == 0) return false;

    std::string str(text);
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);

    // Check URLs and domains
    if (lower_str.find("http://") != std::string::npos ||
        lower_str.find("https://") != std::string::npos ||
        lower_str.find("www.") != std::string::npos ||
        lower_str.find(".m3u") != std::string::npos ||
        lower_str.find(".pls") != std::string::npos ||
        lower_str.find(".aac") != std::string::npos) {
        return true;
    }

    // Check radio station keywords
    std::vector<std::string> station_keywords = {
        "radio", "webradio", "hitradio", "stream", "station", "fm",
        "live365", "somafm", "di.fm", "tunein", "radioparadise",
        "walmradio", "ipmusic", "on air", "broadcast"
    };

    for (const auto& kw : station_keywords) {
        if (lower_str.find(kw) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::string MetadataCleaner::extract_primary_title(const char* title) {
    if (!title || strlen(title) == 0) return "";

    std::string str(title);

    // Split at common track version or extra tag delimiters
    std::vector<std::string> delimiters = { " - ", " / ", " ~ " };
    for (const auto& delim : delimiters) {
        size_t pos = str.find(delim);
        if (pos != std::string::npos && pos > 2) {
            str = str.substr(0, pos);
            break;
        }
    }

    return trim(str);
}

StreamMetadataResult MetadataCleaner::sanitize_stream_metadata(const char* raw_artist, const char* raw_title) {
    StreamMetadataResult res;
    res.raw_artist = raw_artist ? raw_artist : "";
    res.raw_title = raw_title ? raw_title : "";

    // Stage 1: Noise Pre-Cleaning & Station/URL Detection
    res.is_station_or_url = is_station_name_or_url(res.raw_artist.c_str()) || is_station_name_or_url(res.raw_title.c_str());

    res.clean_artist = clean_for_search(res.raw_artist.c_str(), true);
    res.clean_title = clean_for_search(res.raw_title.c_str(), true);

    // Stage 2: Smart Stream Splitter
    bool artist_is_placeholder = res.clean_artist.empty() ||
                                  res.clean_artist == "?" ||
                                  res.clean_artist == "Unknown" ||
                                  res.clean_artist == "Unknown Artist" ||
                                  is_station_name_or_url(res.clean_artist.c_str());

    bool title_is_placeholder = res.clean_title.empty() ||
                                 res.clean_title == "?" ||
                                 res.clean_title == "Unknown" ||
                                 res.clean_title == "Unknown Track" ||
                                 is_station_name_or_url(res.clean_title.c_str());

    // Helper lambda to split combined metadata like "Artist - Title", "Title by Artist", or "artist-title"
    auto try_split_combined = [](const std::string& combined, std::string& out_artist, std::string& out_title) -> bool {
        std::string lower = combined;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Check " by " delimiter first (e.g. "Jail House Rock by Elvis Presley")
        size_t by_pos = lower.find(" by ");
        if (by_pos != std::string::npos && by_pos > 1 && by_pos + 4 < combined.length()) {
            out_title = clean_for_search(combined.substr(0, by_pos).c_str(), true);
            out_artist = clean_for_search(combined.substr(by_pos + 4).c_str(), true);
            return !out_artist.empty() && !out_title.empty();
        }

        // Check delimiters in order of confidence: " - ", " / ", " ~ ", " ˗ ", "-", "/"
        std::vector<std::string> delims = { " - ", " / ", " ~ ", " ˗ ", "-", "/" };
        for (const auto& delim : delims) {
            size_t pos = combined.find(delim);
            if (pos != std::string::npos && pos > 0 && pos + delim.length() < combined.length()) {
                std::string part1 = clean_for_search(combined.substr(0, pos).c_str(), true);
                std::string part2 = clean_for_search(combined.substr(pos + delim.length()).c_str(), true);

                if (!part1.empty() && !part2.empty()) {
                    out_artist = part1;
                    out_title = part2;
                    return true;
                }
            }
        }
        return false;
    };

    if (artist_is_placeholder && !res.clean_title.empty()) {
        std::string split_art, split_tit;
        if (try_split_combined(res.clean_title, split_art, split_tit)) {
            res.clean_artist = split_art;
            res.clean_title = split_tit;
        }
    } else if (title_is_placeholder && !res.clean_artist.empty()) {
        std::string split_art, split_tit;
        if (try_split_combined(res.clean_artist, split_art, split_tit)) {
            res.clean_artist = split_art;
            res.clean_title = split_tit;
        }
    }

    // Stage 3: Multilingual Collaboration & Token Extraction
    res.first_artist = extract_first_artist(res.clean_artist.c_str());
    res.second_artist = extract_second_artist(res.clean_artist.c_str());
    res.primary_title = extract_primary_title(res.clean_title.c_str());

    if (res.first_artist.empty()) {
        res.first_artist = res.clean_artist;
    }
    if (res.primary_title.empty()) {
        res.primary_title = res.clean_title;
    }

    // Stage 4: Search Validation
    res.is_valid_search = is_valid_for_search(res.clean_artist.c_str(), res.clean_title.c_str());

    return res;
}

bool MetadataCleaner::is_likely_collaboration(const std::string& artist_str, const std::string& separator, size_t pos) {
    // Extract the parts before and after the separator
    std::string before = trim(artist_str.substr(0, pos));
    std::string after = trim(artist_str.substr(pos + separator.length()));
    
    if (before.empty() || after.empty()) {
        return false;
    }
    
    // Known band name patterns - these should NOT be split
    std::vector<std::string> band_name_indicators = {
        "sons", "daughters", "brothers", "sisters",
        "boys", "girls", "men", "women",
        "band", "group", "orchestra", "ensemble",
        "collective", "crew", "gang", "mob"
    };
    
    // Convert to lowercase for comparison
    std::string after_lower = after;
    std::transform(after_lower.begin(), after_lower.end(), after_lower.begin(), ::tolower);
    
    // If the part after separator is a common band name indicator, likely NOT a collaboration
    for (const auto& indicator : band_name_indicators) {
        if (after_lower == indicator || after_lower.find(indicator) == 0) {
            return false; // Don't split band names like "Mumford & Sons"
        }
    }
    
    // Additional heuristics for legitimate collaborations:
    // 1. Both parts look like complete artist names (have capital letters)
    bool before_has_capitals = std::any_of(before.begin(), before.end(), ::isupper);
    bool after_has_capitals = std::any_of(after.begin(), after.end(), ::isupper);
    
    // 2. Both parts are reasonably long (not just single words)
    bool before_substantial = before.length() > 3 && before.find(' ') != std::string::npos;
    bool after_substantial = after.length() > 3;
    
    // If both parts look like substantial artist names, likely a collaboration
    if (before_has_capitals && after_has_capitals && before_substantial && after_substantial) {
        return true;
    }
    
    // Conservative default: don't split unless we're confident it's a collaboration
    return false;
}
