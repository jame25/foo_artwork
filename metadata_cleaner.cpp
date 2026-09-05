#include "stdafx.h"
#include "metadata_cleaner.h"
#include "preferences.h"
#include <algorithm>
#include <cctype>

// Locale-independent ASCII case conversion helpers
// Operates exclusively on ASCII characters [A-Za-z] to protect multi-byte UTF-8 sequences
inline char ascii_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline char ascii_toupper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

inline void to_lower_ascii(std::string& s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    }
}

inline void to_upper_ascii(std::string& s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    }
}

bool MetadataCleaner::has_non_ascii(const std::string& str) {
    for (unsigned char c : str) {
        if (c >= 0x80) return true;
    }
    return false;
}

bool MetadataCleaner::contains_non_latin(const std::string& str) {
    // Check for non-Latin UTF-8 sequences (Greek U+0370+, Cyrillic U+0400+, Hebrew, Arabic, CJK, etc.)
    for (size_t i = 0; i < str.length(); ++i) {
        unsigned char byte = static_cast<unsigned char>(str[i]);
        if (byte >= 0xCD) {
            return true;
        }
    }
    return false;
}

static std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    if (len <= 0) return L"";
    std::wstring wstr(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], len);
    return wstr;
}

static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &str[0], len, NULL, NULL);
    return str;
}

static inline std::wstring wide_to_lower(const std::wstring& wstr) {
    std::wstring res(wstr.length(), 0);
    int ret = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, wstr.c_str(), (int)wstr.length(), &res[0], (int)res.length(), NULL, NULL, 0);
    if (ret <= 0) {
        res = wstr;
        for (wchar_t& c : res) c = towlower(c);
    }
    return res;
}

static inline std::wstring wide_to_upper(const std::wstring& wstr) {
    std::wstring res(wstr.length(), 0);
    int ret = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, wstr.c_str(), (int)wstr.length(), &res[0], (int)res.length(), NULL, NULL, 0);
    if (ret <= 0) {
        res = wstr;
        for (wchar_t& c : res) c = towupper(c);
    }
    return res;
}

bool MetadataCleaner::is_minor_word(const std::string& word) {
    static const std::vector<std::string> minor_words = {
        "a", "an", "the", "and", "but", "or", "nor", "for", "yet", "so",
        "at", "by", "in", "of", "on", "to", "with", "as", "into", "like", "over",
        "de", "del", "la", "le", "el", "los", "las", "du", "des", "y", "e", "o", "da", "do", "das", "dos",
        "und", "von", "van", "der", "die", "das", "d'", "l'", "à", "au", "aux", "por", "para", "com", "em"
    };
    std::string lower = word;
    to_lower_ascii(lower);
    for (const auto& w : minor_words) {
        if (lower == w) return true;
    }
    return false;
}

bool MetadataCleaner::is_roman_numeral(const std::string& word) {
    if (word.empty()) return false;
    std::string upper = word;
    to_upper_ascii(upper);
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
    to_upper_ascii(upper);
    for (const auto& a : acronyms) {
        if (upper == a) return true;
    }
    return false;
}

std::string MetadataCleaner::to_title_case(const std::string& str) {
    if (str.empty()) return "";

    std::wstring wstr = utf8_to_wstring(str);
    if (wstr.empty()) return str;

    size_t upper_count = 0;
    size_t lower_count = 0;
    for (wchar_t wc : wstr) {
        if (iswupper(wc)) upper_count++;
        else if (iswlower(wc)) lower_count++;
    }

    bool should_normalize = (lower_count == 0 && upper_count > 2) || (upper_count == 0 && lower_count > 0);
    if (!should_normalize) {
        return str;
    }

    std::wstring result;
    result.reserve(wstr.length());

    size_t i = 0;
    size_t word_index = 0;

    while (i < wstr.length()) {
        while (i < wstr.length() && (wstr[i] == L' ' || wstr[i] == L'\t')) {
            result += wstr[i++];
        }
        if (i >= wstr.length()) break;

        size_t start = i;
        while (i < wstr.length() && wstr[i] != L' ' && wstr[i] != L'\t') {
            i++;
        }
        std::wstring word = wstr.substr(start, i - start);
        std::string word_utf8 = wstring_to_utf8(word);

        if (is_roman_numeral(word_utf8)) {
            result += wide_to_upper(word);
        } else if (is_known_acronym(word_utf8)) {
            result += wide_to_upper(word);
        } else {
            std::wstring lower_word = wide_to_lower(word);
            std::string lower_utf8 = wstring_to_utf8(lower_word);

            if (word_index > 0 && i < wstr.length() && is_minor_word(lower_utf8)) {
                result += lower_word;
            } else {
                if (!lower_word.empty()) {
                    wchar_t first_char = lower_word[0];
                    wchar_t upper_char = first_char;
                    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, &first_char, 1, &upper_char, 1, NULL, NULL, 0);
                    lower_word[0] = (upper_char != 0) ? upper_char : towupper(first_char);

                    size_t apo = lower_word.find(L'\'');
                    if (apo != std::wstring::npos && apo + 1 < lower_word.length() && apo <= 2) {
                        wchar_t after_apo = lower_word[apo + 1];
                        wchar_t upper_after_apo = after_apo;
                        LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, &after_apo, 1, &upper_after_apo, 1, NULL, NULL, 0);
                        lower_word[apo + 1] = (upper_after_apo != 0) ? upper_after_apo : towupper(after_apo);
                    }
                }
                result += lower_word;
            }
        }
        word_index++;
    }

    return wstring_to_utf8(result);
}

std::string MetadataCleaner::strip_track_numbers(const std::string& str) {
    if (str.empty()) return "";

    std::string lower = str;
    to_lower_ascii(lower);

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
        std::regex("^\\s*(?:track|faixa|pista|traccia|titel|#)\\s*\\d{1,3}\\s*(?:[:\\.\\-]|\\xE2\\x80\\x93|\\xE2\\x80\\x94)\\s*", std::regex_constants::icase), "");

    // Pattern 2: Leading track number with separator: "06. ", "01 - ", "001. ", "1. ", "02: "
    result = std::regex_replace(result,
        std::regex("^\\s*\\d{1,3}\\s*(?:[\\.\\-:]|\\xE2\\x80\\x93|\\xE2\\x80\\x94)\\s+"), "");

    // Pattern 3: Leading 2 or 3 digits followed by a space (e.g. "06 Karma Police" -> "Karma Police", "01 Μανταλένα" -> "Μανταλένα")
    result = std::regex_replace(result,
        std::regex("^\\s*0\\d{1,2}\\s+"), "");

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

std::vector<std::string> MetadataCleaner::get_active_blacklist_tokens() {
    pfc::string8 bl_cfg = get_custom_blacklist_active_content();
    std::vector<std::string> tokens;
    if (bl_cfg.is_empty()) return tokens;

    std::string bl_str = bl_cfg.c_str();
    std::string token;
    for (char c : bl_str) {
        if (c == '\r' || c == '\n' || c == ',' || c == ';') {
            token = trim(token);
            if (!token.empty() && token[0] != '#' && token.rfind("//", 0) != 0) {
                tokens.push_back(token);
            }
            token.clear();
        } else {
            token += c;
        }
    }
    token = trim(token);
    if (!token.empty() && token[0] != '#' && token.rfind("//", 0) != 0) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string MetadataCleaner::filter_multilingual_keywords(const std::string& str) {
    if (str.empty()) return "";
    std::string result = str;

    auto tokens = get_active_blacklist_tokens();
    for (const auto& t : tokens) {
        if (t.length() < 2) continue;
        try {
            std::string escaped;
            for (char ch : t) {
                if (ch == '.' || ch == '^' || ch == '$' || ch == '*' || ch == '+' || ch == '?' ||
                    ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
                    ch == '|' || ch == '\\') {
                    escaped += '\\';
                }
                escaped += ch;
            }
            // Strip leading label tags e.g. "^Artist: ...", "^Artista - ..."
            std::string lead_pat = "(?i)^\\s*" + escaped + "\\s*(?:[:\\-]|\\xE2\\x80\\x93|\\xE2\\x80\\x94)\\s*";
            result = std::regex_replace(result, std::regex(lead_pat), "");

            // Strip inline album / media noise tags like "- Album: OK Computer", "/ CD: Greatest Hits", "• Disco: ..."
            std::string inline_pat = "(?i)\\s*(?:[\\-\\/\\|~]|\\xE2\\x80\\xA2)\\s*" + escaped + "\\s*(?:[:\\-]|\\xE2\\x80\\x93|\\xE2\\x80\\x94)\\s*[^-\\/\\|~]+";
            result = std::regex_replace(result, std::regex(inline_pat), "");
        } catch (...) {}
    }

    return trim(result);
}

std::string MetadataCleaner::filter_custom_blacklist(const std::string& str) {
    if (str.empty()) return "";
    auto tokens = get_active_blacklist_tokens();
    if (tokens.empty()) return str;

    std::string result = str;
    for (const auto& t : tokens) {
        if (t.empty()) continue;
        try {
            std::string escaped_term;
            for (char ch : t) {
                if (ch == '.' || ch == '^' || ch == '$' || ch == '*' || ch == '+' || ch == '?' ||
                    ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
                    ch == '|' || ch == '\\') {
                    escaped_term += '\\';
                }
                escaped_term += ch;
            }
            std::string pattern = "(?i)(?:^|\\b|\\s*[-/|~•]\\s*)" + escaped_term + "(?:\\b|\\s*[-/|~•]|\\s*$)";
            result = std::regex_replace(result, std::regex(pattern), " ");
        } catch (...) {}
    }
    return trim(result);
}

std::string MetadataCleaner::clean_for_search(const char* metadata, bool preserve_cyrillic, bool apply_title_case) {
    if (!metadata || strlen(metadata) == 0) {
        return "";
    }
    
    std::string str(metadata);

    if (str.length() > 100) str.resize(100);

    // Normalize quotes and apostrophes using UTF-8 byte sequences
    str = normalize_quotes_and_apostrophes(str);

    // Remove UTF-8 BOM
    size_t pos = 0;
    while ((pos = str.find("\xEF\xBB\xBF", pos)) != std::string::npos) {
        str.replace(pos, 3, "");
        pos += 1;
    }

    // 1. Strip multilingual keyword markers ("Artista: ", "Album: ...")
    str = filter_multilingual_keywords(str);

    // 1b. Strip user custom blacklist words
    str = filter_custom_blacklist(str);

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
    str = std::regex_replace(str, std::regex("\\xE2\\x80\\xA2.*"), ""); // UTF-8 bullet •

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
    static const std::vector<std::string> prefixes = {
        "Now Playing: ", "Now Playing:", "Live: ", "Live:", "Playing: ", "Playing:",
        "Current: ", "Current:", "On Air: ", "On Air:",
        "\xE2\x99\xAA ", "\xE2\x99\xAB ", "\xF0\x9F\x8E\xB5 ", "\xF0\x9F\x8E\xB6 "
    };
    for (const auto& prefix : prefixes) {
        if (str.substr(0, prefix.length()) == prefix) {
            str = str.substr(prefix.length());
            break;
        }
    }

    static const std::vector<std::string> suffixes = {
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

    // 8. Title Case normalization (only applied if explicitly requested for queries)
    if (apply_title_case) {
        str = to_title_case(str);
    }

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

    // Rule 2: Block station names, stream URLs, and broadcast taglines
    if (is_station_name_or_url(artist_str.c_str()) || is_station_name_or_url(title_str.c_str())) {
        return false;
    }

    // Rule 3: Block common invalid patterns
    if (title_str == "?" || artist_str == "?" ||
        title_str == "? - ?" || artist_str == "? - ?") {
        return false;
    }

    // Rule 4: Block advertisement breaks
    std::string title_lower = title_str;
    to_lower_ascii(title_lower);
    if (title_lower.find("adbreak") != std::string::npos || title_lower.find("ad_break") != std::string::npos || title_lower.find("advertisement") != std::string::npos) {
        return false;
    }

    // Rule 5: Block "Unknown" and active unified blacklist keywords
    static const std::vector<std::string> hardcoded_fallbacks = {
        "unknown", "unknown artist", "unknown track"
    };
    std::string artist_lower = artist_str;
    to_lower_ascii(artist_lower);

    for (const auto& bl : hardcoded_fallbacks) {
        if (artist_lower == bl || title_lower == bl) {
            return false;
        }
    }

    auto active_tokens = get_active_blacklist_tokens();
    for (const auto& token : active_tokens) {
        std::string t_lower = token;
        to_lower_ascii(t_lower);
        if (artist_lower == t_lower || title_lower == t_lower) {
            return false;
        }
    }

    // Rule 6: Block known problematic station names
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
    // Auto-detect non-Latin (Cyrillic, Greek, etc.) if preserve_cyrillic is true
    bool use_conservative = preserve_cyrillic && (contains_cyrillic(str) || contains_non_latin(str));
    
    if (!preserve_cyrillic || !use_conservative) {
        // Standard removal for Latin scripts
        std::string result = str;
        
        // Remove common remix/version patterns (case insensitive)
        result = std::regex_replace(result,
            std::regex("\\s*\\([^)]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\)\\s*",
            std::regex_constants::icase), " ");
        
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
        // Conservative removal for non-Latin scripts - only remove common patterns
        std::string result = str;
        
        result = std::regex_replace(result,
            std::regex("\\s*\\([^)]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\)\\s*",
            std::regex_constants::icase), " ");
        
        result = std::regex_replace(result,
            std::regex("\\s*\\((?:remix|remaster|demo|radio edit|extended|rmx)\\)\\s*",
            std::regex_constants::icase), " ");
        
        result = std::regex_replace(result,
            std::regex("\\s*\\((?:explicit|clean)\\)\\s*",
            std::regex_constants::icase), " ");
            
        return result;
    }
}

std::string MetadataCleaner::remove_bracketed_content(const std::string& str, bool preserve_cyrillic) {
    // Auto-detect non-Latin if preserve_cyrillic is true
    bool use_conservative = preserve_cyrillic && (contains_cyrillic(str) || contains_non_latin(str));
    
    if (!preserve_cyrillic || !use_conservative) {
        // Standard removal for Latin scripts
        std::string result = str;
        
        result = std::regex_replace(result,
            std::regex("\\s*\\[[^\\]]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\]\\s*",
            std::regex_constants::icase), " ");
        
        result = std::regex_replace(result,
            std::regex("\\s*\\[(?:remix|remaster|demo|radio edit|extended|rmx)[^\\]]*\\]\\s*",
            std::regex_constants::icase), " ");
        
        // Remove all remaining brackets content (aggressive for Latin)
        result = std::regex_replace(result, std::regex("\\s*\\[[^\\]]*\\]\\s*"), " ");
        
        return result;
    } else {
        // Conservative removal for non-Latin scripts
        std::string result = str;
        
        result = std::regex_replace(result,
            std::regex("\\s*\\[[^\\]]*\\s+(?:remix|remaster|demo|mix|version|edit|cut|rmx)\\]\\s*",
            std::regex_constants::icase), " ");
        
        result = std::regex_replace(result,
            std::regex("\\s*\\[(?:remix|remaster|demo|radio edit|extended|rmx)\\]\\s*",
            std::regex_constants::icase), " ");
            
        return result;
    }
}

std::string MetadataCleaner::normalize_quotes_and_apostrophes(const std::string& str) {
    std::string result = str;
    
    // Explicit UTF-8 byte sequences for quote normalization (safe across all compilers & code pages)
    static const std::vector<std::pair<std::string, std::string>> quote_patterns = {
        // Single quotation marks
        {"\xE2\x80\x98", "'"},  // U+2018 left single quote ‘
        {"\xE2\x80\x99", "'"},  // U+2019 right single quote ’
        {"\xE2\x80\x9A", "'"},  // U+201A single low-9 quote ‚
        {"\xE2\x80\x9B", "'"},  // U+201B single high-reversed-9 quote ‛
        {"\xE2\x80\xB2", "'"},  // U+2032 prime ′
        {"\xC2\xB4", "'"},      // U+00B4 acute accent ´
        {"`", "'"},             // U+0060 backtick `
        // Double quotation marks  
        {"\xE2\x80\x9C", "\""}, // U+201C left double quote “
        {"\xE2\x80\x9D", "\""}, // U+201D right double quote ”
        {"\xE2\x80\x9E", "\""}, // U+201E double low-9 quote „
        {"\xE2\x80\x9F", "\""}, // U+201F double high-reversed-9 quote ‟
        {"\xE2\x80\xB3", "\""}, // U+2033 double prime ″
        {"\xC2\xAB", "\""},     // U+00AB left guillemet «
        {"\xC2\xBB", "\""},     // U+00BB right guillemet »
        // Angle quotes
        {"\xE2\x80\xB9", "<"},  // U+2039 single left-pointing angle ‹
        {"\xE2\x80\xBA", ">"}   // U+203A single right-pointing angle ›
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
    static const std::vector<std::pair<std::string, std::string>> feat_patterns = {
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
    // Check for Cyrillic Unicode range (U+0400 to U+052F)
    // In UTF-8, this is encoded as 0xD0 0x80 to 0xD4 0xAF
    for (size_t i = 0; i < str.length() - 1; ++i) {
        unsigned char byte1 = static_cast<unsigned char>(str[i]);
        unsigned char byte2 = static_cast<unsigned char>(str[i + 1]);
        
        // Check for Cyrillic range
        if ((byte1 == 0xD0 && byte2 >= 0x80) ||  // U+0400-U+047F
            (byte1 == 0xD1 && byte2 <= 0xBF) ||  // U+0480-U+04FF  
            (byte1 == 0xD2 && byte2 <= 0xBF) ||  // U+0500-U+052F (Cyrillic Supplement)
            (byte1 == 0xD3 && byte2 <= 0xBF) ||  // Extended Cyrillic
            (byte1 == 0xD4 && byte2 <= 0xAF)) {  // Cyrillic Supplement continuation
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
    return str;
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
    to_lower_ascii(lower);
    
    return (lower == "remix" || lower == "remaster" || lower == "demo" ||
            lower == "live" || lower == "acoustic" || lower == "unplugged" ||
            lower == "instrumental" || lower == "radio edit" || lower == "extended");
}

bool MetadataCleaner::is_featuring_pattern(const std::string& term) {
    std::string lower = term;
    to_lower_ascii(lower);
    
    return (lower == "feat." || lower == "featuring" || lower == "ft." || lower == "with" ||
            lower == "pres." || lower == "pres" || lower == "presents" || lower == "presenting" ||
            lower == "meets" || lower == "w/");
}

std::string MetadataCleaner::extract_first_artist(const char* artist) {
    if (!artist || strlen(artist) == 0) {
        return "";
    }
    
    if (!cfg_trim_secondary_artists) {
        return trim(std::string(artist));
    }
    
    std::string artist_str(artist);
    
    // High-confidence multi-artist separators (clearly indicate collaborations across multiple languages)
    static const std::vector<std::string> high_confidence_separators = {
        " feat. ", " ft. ", " featuring ", " feat ", " ft ",
        " / ", " // ", " /// ", "/",
        " vs. ", " vs ", " versus ",
        " with ", " w/ ",
        " x ", " X ",
        " pres. ", " pres ", " presents ", " presenting ",
        " meets ", " intro. ", " introduces ",
        " aka ", " a.k.a. ", " pka ", " p.k.a. ",
        " part. ", " part ", " part. esp. ", " part. especial ",
        " parti\xC3\xA7\xC3\xA3o ", " participacao ",
        " con ", " avec ", " und ",
        ", ", "; ", ";", ","
    };
    
    // Contextual separators that need additional validation
    static const std::vector<std::string> contextual_separators = {
        " & ", " and ", " y ", " e "
    };
    
    std::string artist_lower = artist_str;
    to_lower_ascii(artist_lower);

    // First, check high-confidence separators
    size_t earliest_pos = std::string::npos;
    for (const auto& separator : high_confidence_separators) {
        std::string sep_lower = separator;
        to_lower_ascii(sep_lower);
        
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
            to_lower_ascii(sep_lower);
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
    to_lower_ascii(artist_lower);

    static const std::vector<std::string> high_confidence_separators = {
        " feat. ", " ft. ", " featuring ", " feat ", " ft ",
        " / ", " // ", " /// ", "/",
        " vs. ", " vs ", " versus ",
        " with ", " w/ ",
        " x ", " X ",
        " pres. ", " pres ", " presents ", " presenting ",
        " meets ", " intro. ", " introduces ",
        " aka ", " a.k.a. ", " pka ", " p.k.a. ",
        " part. ", " part ", " part. esp. ", " part. especial ",
        " parti\xC3\xA7\xC3\xA3o ", " participacao ",
        " con ", " avec ", " und ",
        ", ", "; ", ";", ","
    };

    size_t earliest_pos = std::string::npos;
    size_t sep_len = 0;
    for (const auto& separator : high_confidence_separators) {
        std::string sep_lower = separator;
        to_lower_ascii(sep_lower);
        
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
        static const std::vector<std::string> contextual_separators = { " & ", " and ", " y ", " e " };
        for (const auto& separator : contextual_separators) {
            std::string sep_lower = separator;
            to_lower_ascii(sep_lower);
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
    to_lower_ascii(lower_str);

    // 1. Check URLs, streams, protocols, and stream playlist file extensions
    if (lower_str.find("http://") != std::string::npos ||
        lower_str.find("https://") != std::string::npos ||
        lower_str.find("www.") != std::string::npos ||
        lower_str.find("://") != std::string::npos ||
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

    // 3. Known station directory / streaming networks & domains (exact or whole-token word matches)
    static const std::vector<std::regex> station_network_regexes = {
        // Station networks
        std::regex("\\b(?:webradio|hitradio|somafm|tunein|radioparadise|walmradio|ipmusic|live365|di\\.fm|accuradio|iheartradio|laut\\.fm|1\\.fm|101\\.ru|trancebase|technobase|hardbase|housetime|coretime|clubtime|teatime|zenofm|azuracast|zeno\\.fm|zeno\\.live)\\b", std::regex_constants::icase),
        // Domain suffix patterns for radio stations (.fm, .radio, .stream, .audio, etc.)
        std::regex("\\b[a-z0-9_-]+\\.(?:fm|radio|stream|audio|digital|live|club|party|cc|to)\\b", std::regex_constants::icase),
        // Station names ending in "radio" (e.g. "Metal Lab radio", "Rock Radio", "Chillout Radio")
        std::regex("\\b[a-z0-9_-]+\\s+radio\\b", std::regex_constants::icase),
        // Radio frequency patterns: "98.5 FM", "101.1 FM", "FM 104", "106.7 FM", "89.3 FM"
        std::regex("\\b\\d{2,3}(?:\\.\\d+)?\\s*(?:fm|am|mhz|khz)\\b", std::regex_constants::icase),
        std::regex("\\b(?:fm|am)\\s*\\d{2,3}(?:\\.\\d+)?\\b", std::regex_constants::icase),
        // Station broadcast markers & taglines
        std::regex("^radio\\s+[a-z0-9_-]+\\b", std::regex_constants::icase),
        std::regex("\\b(?:on\\s+air|now\\s+on\\s+air|live\\s+broadcast|streaming\\s+live|web\\s*radio|internet\\s*radio|online\\s*radio|radio\\s*station|radio\\s*stream)\\b", std::regex_constants::icase),
        // 24h / 24/7 / non-stop slogans & genre taglines
        std::regex("\\b(?:24h|24/7|non-stop|non stop|commercial free)\\b", std::regex_constants::icase),
        std::regex("\\b(?:and more|the best of|the best music|all the hits)\\b", std::regex_constants::icase)
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
    static const std::vector<std::string> delimiters = {
        " - ", " / ", " ~ ",
        " \xE2\x80\x93 ", // en-dash " – "
        " \xE2\x80\x94 ", // em-dash " — "
        " \xCB\x97 ",     // modifier letter minus " ˗ "
        " \xE2\x88\x92 "  // minus sign " − "
    };
    for (const auto& delim : delimiters) {
        size_t pos = str.find(delim);
        if (pos != std::string::npos && pos > 2) {
            str = str.substr(0, pos);
            break;
        }
    }

    return trim(str);
}

bool MetadataCleaner::try_parse_xml_stream_title(const std::string& raw, std::string& out_artist, std::string& out_title, std::string& out_album) {
    if (raw.empty() || raw.find('<') == std::string::npos || raw.find('>') == std::string::npos) {
        return false;
    }

    auto extract_xml_tag = [](const std::string& xml, const std::string& tag) -> std::string {
        std::string open_tag = "<" + tag;
        std::string close_tag = "</" + tag + ">";
        size_t start = 0;
        while ((start = xml.find(open_tag, start)) != std::string::npos) {
            size_t tag_end = xml.find('>', start);
            if (tag_end == std::string::npos) break;
            size_t val_start = tag_end + 1;
            size_t val_end = xml.find(close_tag, val_start);
            if (val_end != std::string::npos) {
                std::string val = xml.substr(val_start, val_end - val_start);
                // Strip CDATA wrapper if present
                if (val.find("<![CDATA[") == 0 && val.rfind("]]>") == val.length() - 3) {
                    val = val.substr(9, val.length() - 12);
                }
                // Unescape standard XML entities
                val = std::regex_replace(val, std::regex("&amp;"), "&");
                val = std::regex_replace(val, std::regex("&quot;"), "\"");
                val = std::regex_replace(val, std::regex("&apos;"), "'");
                val = std::regex_replace(val, std::regex("&lt;"), "<");
                val = std::regex_replace(val, std::regex("&gt;"), ">");
                return trim(val);
            }
            start = tag_end + 1;
        }
        return "";
    };

    std::string art = extract_xml_tag(raw, "artist");
    if (art.empty()) art = extract_xml_tag(raw, "ARTIST");
    if (art.empty()) art = extract_xml_tag(raw, "Artist");
    if (art.empty()) art = extract_xml_tag(raw, "singer");
    if (art.empty()) art = extract_xml_tag(raw, "performer");

    std::string tit = extract_xml_tag(raw, "title");
    if (tit.empty()) tit = extract_xml_tag(raw, "TITLE");
    if (tit.empty()) tit = extract_xml_tag(raw, "Title");
    if (tit.empty()) tit = extract_xml_tag(raw, "song");
    if (tit.empty()) tit = extract_xml_tag(raw, "track");

    std::string alb = extract_xml_tag(raw, "album");
    if (alb.empty()) alb = extract_xml_tag(raw, "ALBUM");
    if (alb.empty()) alb = extract_xml_tag(raw, "Album");

    // Attribute fallback: <track artist="..." title="..." album="..." />
    if (art.empty() || tit.empty()) {
        std::smatch m;
        if (std::regex_search(raw, m, std::regex("(?i)artist=[\"']([^\"']+)[\"']"))) {
            art = m[1].str();
        }
        if (std::regex_search(raw, m, std::regex("(?i)title=[\"']([^\"']+)[\"']"))) {
            tit = m[1].str();
        }
        if (std::regex_search(raw, m, std::regex("(?i)album=[\"']([^\"']+)[\"']"))) {
            alb = m[1].str();
        }
    }

    if (!art.empty() || !tit.empty() || !alb.empty()) {
        out_artist = art;
        out_title = tit;
        out_album = alb;
        return true;
    }
    return false;
}

StreamMetadataResult MetadataCleaner::sanitize_stream_metadata(const char* raw_artist, const char* raw_title) {
    StreamMetadataResult res;
    res.raw_artist = raw_artist ? raw_artist : "";
    res.raw_title = raw_title ? raw_title : "";

    // Check for XML formatted metadata (e.g. Bauer Media Cidade FM)
    std::string xml_art, xml_tit, xml_alb;
    bool has_xml = try_parse_xml_stream_title(res.raw_title, xml_art, xml_tit, xml_alb);
    if (!has_xml && !res.raw_artist.empty()) {
        has_xml = try_parse_xml_stream_title(res.raw_artist, xml_art, xml_tit, xml_alb);
    }

    if (has_xml) {
        if (!xml_art.empty()) res.clean_artist = clean_for_search(xml_art.c_str(), true);
        if (!xml_tit.empty()) res.clean_title = clean_for_search(xml_tit.c_str(), true);
        if (!xml_alb.empty()) res.clean_album = clean_for_search(xml_alb.c_str(), true);
    } else {
        res.clean_artist = clean_for_search(res.raw_artist.c_str(), true);
        res.clean_title = clean_for_search(res.raw_title.c_str(), true);
    }

    // Stage 1: Noise Pre-Cleaning & Station/URL Detection
    res.is_station_or_url = is_station_name_or_url(res.raw_artist.c_str()) || is_station_name_or_url(res.raw_title.c_str());

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
        to_lower_ascii(lower);

        // Check " by " delimiter first (e.g. "Jail House Rock by Elvis Presley")
        size_t by_pos = lower.find(" by ");
        if (by_pos != std::string::npos && by_pos > 1 && by_pos + 4 < combined.length()) {
            out_title = clean_for_search(combined.substr(0, by_pos).c_str(), true);
            out_artist = clean_for_search(combined.substr(by_pos + 4).c_str(), true);
            return !out_artist.empty() && !out_title.empty();
        }

        // Check tilde delimiter (e.g. "Title~Artist~Album~Year..." or "Artist~Title")
        size_t tilde_pos = combined.find('~');
        if (tilde_pos != std::string::npos && tilde_pos > 0) {
            std::string part1 = clean_for_search(combined.substr(0, tilde_pos).c_str(), true);
            size_t next_tilde = combined.find('~', tilde_pos + 1);
            std::string part2_raw = (next_tilde != std::string::npos) 
                ? combined.substr(tilde_pos + 1, next_tilde - tilde_pos - 1)
                : combined.substr(tilde_pos + 1);
            std::string part2 = clean_for_search(part2_raw.c_str(), true);
            if (!part1.empty() && !part2.empty()) {
                out_artist = part1;
                out_title = part2;
                return true;
            }
        }

        // Check delimiters in order of confidence
        static const std::vector<std::string> delims = {
            " - ", " / ", " ~ ",
            " \xE2\x80\x93 ", // en-dash " – "
            " \xE2\x80\x94 ", // em-dash " — "
            " \xCB\x97 ",     // modifier letter minus " ˗ "
            " \xE2\x88\x92 ", // minus sign " − "
            "-", "/"
        };
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

    // Stage 4: Search Validation & Comprehensive Station/URL Detection
    res.is_station_or_url = res.is_station_or_url ||
                            is_station_name_or_url(res.clean_artist.c_str()) ||
                            is_station_name_or_url(res.clean_title.c_str()) ||
                            is_station_name_or_url(res.first_artist.c_str()) ||
                            is_station_name_or_url(res.primary_title.c_str());

    res.is_valid_search = is_valid_for_search(res.clean_artist.c_str(), res.clean_title.c_str()) && !res.is_station_or_url;

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
    to_lower_ascii(artist_lower);

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
    static const std::vector<std::string> band_name_indicators = {
        "sons", "daughters", "brothers", "sisters",
        "boys", "girls", "men", "women",
        "band", "group", "orchestra", "ensemble",
        "collective", "crew", "gang", "mob", "news", "family", "trio", "quartet", "quintet"
    };
    
    // Convert to lowercase for comparison
    std::string after_lower = after;
    to_lower_ascii(after_lower);
    
    // If the part after separator is a common band name indicator, likely NOT a collaboration
    for (const auto& indicator : band_name_indicators) {
        if (after_lower == indicator || after_lower.find(indicator) == 0) {
            return false; // Don't split band names like "Mumford & Sons"
        }
    }
    
    // Additional heuristics for legitimate collaborations:
    // 1. Both parts look like complete artist names (have capital letters or non-ASCII characters)
    bool before_has_capitals = std::any_of(before.begin(), before.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) >= 0x80;
    });
    bool after_has_capitals = std::any_of(after.begin(), after.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) >= 0x80;
    });
    
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

