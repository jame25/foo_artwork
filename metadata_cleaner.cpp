#include "stdafx.h"
#include "metadata_cleaner.h"
#include <algorithm>
#include <cctype>

bool MetadataCleaner::is_minor_word(const std::string& word) {
    static const std::vector<std::string> minor_words = {
        "a", "an", "the", "and", "but", "or", "nor", "for", "yet", "so",
        "at", "by", "in", "of", "on", "to", "with", "as", "into", "like", "over",
        "de", "del", "la", "le", "el", "los", "las", "du", "des", "y", "e", "o", "da", "do", "das", "dos",
        "und", "von", "van", "der", "die", "das", "d'", "l'"
    };
    std::string lower = word;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& w : minor_words) {
        if (lower == w) return true;
    }
    return false;
}

bool MetadataCleaner::is_roman_numeral(const std::string& word) {
    if (word.empty()) return false;
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    if (upper == "I" || upper == "II" || upper == "III" || upper == "IV" || upper == "V" ||
        upper == "VI" || upper == "VII" || upper == "VIII" || upper == "IX" || upper == "X" ||
        upper == "XI" || upper == "XII" || upper == "XIII" || upper == "XIV" || upper == "XV" ||
        upper == "XVI" || upper == "XVII" || upper == "XVIII" || upper == "XIX" || upper == "XX") {
        return true;
    }
    return false;
}

bool MetadataCleaner::is_known_acronym(const std::string& word) {
    static const std::vector<std::string> acronyms = {
        "DJ", "MC", "TV", "AC/DC", "ZZ", "ELO", "ABBA", "EP", "LP", "BOM", "UK", "USA", "FM", "AM", "OK", "ID", "R&B", "OST", "VIP", "BPM", "HQ"
    };
    std::string upper = word;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    for (const auto& a : acronyms) {
        if (upper == a) return true;
    }
    return false;
}

std::string MetadataCleaner::to_title_case(const std::string& str) {
    if (str.empty()) return "";
    if (contains_cyrillic(str)) return str;

    size_t upper_count = 0;
    size_t lower_count = 0;
    for (char c : str) {
        if (std::isupper(static_cast<unsigned char>(c))) upper_count++;
        else if (std::islower(static_cast<unsigned char>(c))) lower_count++;
    }

    bool should_normalize = (lower_count == 0 && upper_count > 2) || (upper_count == 0 && lower_count > 0);
    if (!should_normalize) {
        return str;
    }

    std::string result;
    result.reserve(str.length());

    size_t i = 0;
    size_t word_index = 0;

    while (i < str.length()) {
        while (i < str.length() && (str[i] == ' ' || str[i] == '\t')) {
            result += str[i++];
        }
        if (i >= str.length()) break;

        size_t start = i;
        while (i < str.length() && str[i] != ' ' && str[i] != '\t') {
            i++;
        }
        std::string word = str.substr(start, i - start);

        if (is_roman_numeral(word)) {
            std::string upper = word;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            result += upper;
        } else if (is_known_acronym(word)) {
            std::string upper = word;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            result += upper;
        } else {
            std::string lower = word;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

            if (word_index > 0 && i < str.length() && is_minor_word(lower)) {
                result += lower;
            } else {
                if (!lower.empty()) {
                    lower[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(lower[0])));
                    size_t apo = lower.find('\'');
                    if (apo != std::string::npos && apo + 1 < lower.length() && apo <= 2) {
                        lower[apo + 1] = static_cast<char>(std::toupper(static_cast<unsigned char>(lower[apo + 1])));
                    }
                }
                result += lower;
            }
        }
        word_index++;
    }

    return result;
}

std::string MetadataCleaner::strip_track_numbers(const std::string& str) {
    if (str.empty()) return "";

    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Whitelist check: known bands starting with numbers/digits that should never be stripped
    static const std::vector<std::string> protected_num_artists = {
        "10cc", "1975", "the 1975", "2pac", "50 cent", "3 doors down", "24kgoldn",
        "100 gecs", "6ix9ine", "30 seconds to mars", "4 non blondes", "112", "blink-182",
        "sum 41", "u2", "ub40", "level 42", "heaven 17", "e17", "east 17", "iron 67", "catch 22"
    };

    for (const auto& a : protected_num_artists) {
        if (lower.find(a) == 0) {
            return str;
        }
    }

    std::string result = str;

    // Pattern 1: Leading "Track 01 - ", "Track 05: ", "Faixa 02 - ", "Pista 03. ", "#01 - "
    result = std::regex_replace(result,
        std::regex("^\\s*(?:track|faixa|pista|traccia|titel|#)\\s*\\d{1,3}\\s*[:\\.\\-–—]\\s*", std::regex_constants::icase), "");

    // Pattern 2: Leading track number with separator: "06. ", "01 - ", "001. ", "1. ", "02: "
    result = std::regex_replace(result,
        std::regex("^\\s*\\d{1,3}\\s*[\\.\\-–—:]\\s+"), "");

    // Pattern 3: Leading 2 or 3 digits followed by a space (e.g. "06 Karma Police" -> "Karma Police")
    result = std::regex_replace(result,
        std::regex("^\\s*0\\d{1,2}\\s+([A-Za-zА-Яа-я])"), "$1");

    return trim(result);
}

std::string MetadataCleaner::strip_broadcast_dates(const std::string& str) {
    if (str.empty()) return "";
    std::string result = str;

    // Remove dates like (YYYY-MM-DD), [YYYY.MM.DD], (DD.MM.YYYY), (YYYY/MM/DD)
    result = std::regex_replace(result,
        std::regex("[\\(\\[\\s\\-@](?:19\\d\\d|20\\d\\d)[-\\.\\/](?:0?[1-9]|1[0-2])[-\\.\\/](?:0?[1-9]|[12]\\d|3[01])[\\)\\]\\s]?"), " ");

    result = std::regex_replace(result,
        std::regex("[\\(\\[\\s\\-@](?:0?[1-9]|[12]\\d|3[01])[-\\.\\/](?:0?[1-9]|1[0-2])[-\\.\\/](?:19\\d\\d|20\\d\\d)[\\)\\]\\s]?"), " ");

    // Remove broadcast time stamps like "12:30 PM", "14:00 CET", "08:15 UTC"
    result = std::regex_replace(result,
        std::regex("\\b\\d{1,2}:\\d{2}(?::\\d{2})?\\s*(?:am|pm|AM|PM|utc|UTC|est|EST|gmt|GMT|cet|CET|edt|EDT|cst|CST)?\\b"), " ");

    return trim(result);
}

std::string MetadataCleaner::filter_multilingual_keywords(const std::string& str) {
    if (str.empty()) return "";
    std::string result = str;

    // Strip leading label tags like "Artist: ...", "Artista: ...", "Track - ...", "Faixa: ..."
    result = std::regex_replace(result,
        std::regex("^\\s*(?:artist|artista|artiste|künstler|interprete|interprète|interpret|title|titel|track|piste|traccia|faixa|song)\\s*[:\\-–—]\\s*", std::regex_constants::icase), "");

    // Strip inline album / media noise tags like "- Album: OK Computer", "/ CD: Greatest Hits", "• Disco: ..."
    result = std::regex_replace(result,
        std::regex("\\s*[\\-\\/\\|\\•~]\\s*(?:album|álbum|disco|disque|cd\\d*|dvd|vinyl|disc\\s*\\d*|disk\\s*\\d*)\\s*[:\\-–—]\\s*[^-\\/\\|\\•~]+", std::regex_constants::icase), "");

    // Strip standalone leading album labels e.g. "Album: ..."
    result = std::regex_replace(result,
        std::regex("^\\s*(?:album|álbum|disco|disque|cd\\d*|dvd|vinyl|disc\\s*\\d*|disk\\s*\\d*)\\s*[:\\-–—]\\s*", std::regex_constants::icase), "");

    return trim(result);
}

std::string MetadataCleaner::clean_for_search(const char* metadata, bool preserve_cyrillic) {
    if (!metadata || strlen(metadata) == 0) {
        return "";
    }
    
    std::string str(metadata);

    if (str.length() > 100) str.resize(100);

    // Normalize quotes and apostrophes
    str = normalize_quotes_and_apostrophes(str);

    // Remove UTF-8 BOM
    size_t pos = 0;
    while ((pos = str.find("\xEF\xBB\xBF", pos)) != std::string::npos) {
        str.replace(pos, 3, "");
        pos += 1;
    }

    // 1. Strip multilingual keyword markers ("Artista: ", "Album: ...")
    str = filter_multilingual_keywords(str);

    // 2. Strip track numbering prefixes ("06. ", "01 - ", "Track 05: ")
    str = strip_track_numbers(str);

    // 3. Strip broadcast dates & timestamps
    str = strip_broadcast_dates(str);
    str = remove_timestamps(str);

    // 4. Remove parenthetical content
    str = remove_parenthetical_content(str, preserve_cyrillic);

    // 5. Remove bracketed content
    str = remove_bracketed_content(str, preserve_cyrillic);

    // 6. Remove delimiter noise (| , • , ~ , [+])
    str = std::regex_replace(str, std::regex("\\|.*"), "");
    str = std::regex_replace(str, std::regex("\\•.*"), "");

    std::regex pattern("^(([^~]*~){1}[^~]*)");
    std::smatch match;
    if (std::regex_search(str, match, pattern)) {
        std::string result = match[1];
        str = result;
        str = std::regex_replace(str, std::regex("~"), " - ");
    }

    std::replace(str.begin(), str.end(), '_', ' ');

    pos = 0;
    while ((pos = str.find("[+]", pos)) != std::string::npos) {
        size_t start = pos;
        size_t end = pos + 3;
        std::string replacement = " & ";
        if (start > 0 && str[start - 1] == ' ') { start--; }
        if (end < str.length() && str[end] == ' ') { end++; }
        str.replace(start, end - start, replacement);
        pos = start + replacement.length();
    }

    // 7. Remove common prefixes and suffixes
    std::vector<std::string> prefixes = {
        "Now Playing: ", "Now Playing:", "Live: ", "Live:", "Playing: ", "Playing:",
        "Current: ", "Current:", "On Air: ", "On Air:", "♪ ", "♫ ", "🎵 ", "🎶 "
    };
    for (const auto& prefix : prefixes) {
        if (str.substr(0, prefix.length()) == prefix) {
            str = str.substr(prefix.length());
            break;
        }
    }

    std::vector<std::string> suffixes = {
        "*** www.ipmusic.ch", "Classic Vinyl on walmradio.com", "Adroit Jazz Underground on walmradio.com",
        "OTR on walmradio.com", "Christmas Vinyl on walmradio.com", "walmradio.com"
    };
    for (const auto& suffix : suffixes) {
        if (str.size() >= suffix.size()) {
            if (str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0) {
                str.erase(str.size() - suffix.size());
            }
        }
    }

    // 8. Title Case normalization
    str = to_title_case(str);

    // 9. Clean up whitespace
    str = std::regex_replace(str, std::regex("\\s{2,}"), " ");
    str = trim(str);

    return str;
}

bool MetadataCleaner::is_valid_for_search(const char* artist, const char* title) {
    std::string artist_str = artist ? artist : "";
    std::string title_str = title ? title : "";

    // Rule 1: Must have an artist and title
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

    // Rule 4: Block "Unknown" and standalone media blacklist keywords
    static const std::vector<std::string> blacklist_terms = {
        "cd", "cd1", "cd2", "dvd", "album", "álbum", "disco", "disque", "disc", "disk",
        "artista", "artiste", "artist", "künstler", "interprete", "interprète", "interpret",
        "track", "faixa", "pista", "traccia", "titel", "song", "vinyl", "ep", "lp",
        "unknown", "unknown artist", "unknown track"
    };
    std::string artist_lower = artist_str;
    std::transform(artist_lower.begin(), artist_lower.end(), artist_lower.begin(), ::tolower);

    for (const auto& bl : blacklist_terms) {
        if (artist_lower == bl || title_lower == bl) {
            return false;
        }
    }

    // Rule 5: Block known problematic station names
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
    
    // High-confidence multi-artist separators (clearly indicate collaborations across multiple languages)
    std::vector<std::string> high_confidence_separators = {
        " feat. ", " ft. ", " featuring ", " feat ", " ft ",
        " / ", " // ", " /// ", "/",
        " vs. ", " vs ", " versus ",
        " with ", " w/ ",
        " x ", " X ",
        " pres. ", " pres ", " presents ", " presenting ",
        " meets ", " intro. ", " introduces ",
        " aka ", " a.k.a. ", " pka ", " p.k.a. ",
        " part. ", " part ", " part. esp. ", " part. especial ", " participação ", " participacao ",
        " con ", " avec ", " und ",
        ", ", "; ", ";", ","
    };
    
    // Contextual separators that need additional validation
    std::vector<std::string> contextual_separators = {
        " & ", " and ", " y ", " e "
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
        " part. ", " part ", " part. esp. ", " part. especial ", " participação ", " participacao ",
        " con ", " avec ", " und ",
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

    // Also check contextual separators for second artist extraction
    if (earliest_pos == std::string::npos) {
        std::vector<std::string> contextual_separators = { " & ", " and ", " y ", " e " };
        for (const auto& separator : contextual_separators) {
            std::string sep_lower = separator;
            std::transform(sep_lower.begin(), sep_lower.end(), sep_lower.begin(), ::tolower);
            size_t pos = artist_lower.find(sep_lower);
            if (pos != std::string::npos && is_likely_collaboration(artist_str, separator, pos)) {
                if (pos < earliest_pos) {
                    earliest_pos = pos;
                    sep_len = separator.length();
                }
            }
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

    // 1. Check URLs, streams, protocols, and stream playlist file extensions
    if (lower_str.find("http://") != std::string::npos ||
        lower_str.find("https://") != std::string::npos ||
        lower_str.find("www.") != std::string::npos ||
        lower_str.find(".m3u") != std::string::npos ||
        lower_str.find(".pls") != std::string::npos ||
        lower_str.find(".aac") != std::string::npos ||
        lower_str.find(".m3u8") != std::string::npos ||
        (lower_str.find(".mp3") != std::string::npos && (lower_str.find("://") != std::string::npos || lower_str.find("/") != std::string::npos))) {
        return true;
    }

    // 2. Whitelist: legitimate artists containing "Radio", "FM", "AM" to prevent false positives
    static const std::vector<std::string> whitelisted_artists = {
        "radiohead", "radio taxi", "the radio dept.", "the radio dept", "tv on the radio",
        "radio futura", "radio birdman", "radio moscow", "radio stars", "radio silence",
        "radio inactive", "fm static", "steely dan", "fm", "am", "radio 4", "radio replay",
        "radio 1190", "radio sweethearts", "radio massacre international"
    };
    for (const auto& w : whitelisted_artists) {
        if (lower_str == w || lower_str.find(w) == 0) {
            return false;
        }
    }

    // 3. Known station directory / streaming networks (exact or whole-token word matches)
    static const std::vector<std::regex> station_network_regexes = {
        std::regex("\\b(?:webradio|hitradio|somafm|tunein|radioparadise|walmradio|ipmusic|live365|di\\.fm|accuradio|iheartradio)\\b", std::regex_constants::icase),
        // Radio frequency patterns: "98.5 FM", "101.1 FM", "FM 104", "106.7 FM", "89.3 FM"
        std::regex("\\b\\d{2,3}(?:\\.\\d+)?\\s*(?:fm|am|mhz|khz)\\b", std::regex_constants::icase),
        std::regex("\\b(?:fm|am)\\s*\\d{2,3}(?:\\.\\d+)?\\b", std::regex_constants::icase),
        // Station broadcast markers
        std::regex("^radio\\s+\\d{1,3}\\b", std::regex_constants::icase),
        std::regex("\\b(?:on\\s+air|now\\s+on\\s+air|live\\s+broadcast|streaming\\s+live)\\b", std::regex_constants::icase)
    };

    for (const auto& rx : station_network_regexes) {
        if (std::regex_search(lower_str, rx)) {
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
    
    std::string artist_lower = artist_str;
    std::transform(artist_lower.begin(), artist_lower.end(), artist_lower.begin(), ::tolower);

    // Protected band names that include connectors:
    static const std::vector<std::string> protected_bands = {
        "mumford & sons", "earth, wind & fire", "kool & the gang", "kc & the sunshine band",
        "blood, sweat & tears", "sly & the family stone", "huey lewis & the news",
        "captain & tennille", "peaches & herb", "hall & oates", "brooks & dunn",
        "simon & garfunkel", "crosby, stills, nash & young", "emerson, lake & palmer",
        "florence + the machine", "marina and the diamonds", "echo & the bunnymen",
        "king gizzard & the lizard wizard", "toots & the maytals",
        "ziggy marley and the melody makers", "al bano & romina power"
    };

    for (const auto& pb : protected_bands) {
        if (artist_lower.find(pb) != std::string::npos) {
            return false;
        }
    }

    // Known band name patterns - these should NOT be split
    std::vector<std::string> band_name_indicators = {
        "sons", "daughters", "brothers", "sisters",
        "boys", "girls", "men", "women",
        "band", "group", "orchestra", "ensemble",
        "collective", "crew", "gang", "mob", "news", "family", "trio", "quartet", "quintet"
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

    // Single-word substantial collaboration check (e.g. "Shakira y Alejandro Sanz", "Anitta part. Cardi B")
    if (before.length() >= 3 && after.length() >= 3) {
        return true;
    }
    
    // Conservative default: don't split unless we're confident it's a collaboration
    return false;
}
