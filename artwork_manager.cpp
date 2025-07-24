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

void artwork_manager::get_artwork_async(metadb_handle_ptr track, artwork_callback callback) {
    try {
        // Launch simple background thread - completely invisible to user
        std::thread background_thread([track, callback]() {
            search_artwork_background(track, callback);
        });
        background_thread.detach();
    } catch (const std::exception& e) {
        artwork_result result;
        result.success = false;
        result.error_message = e.what();
        callback(result);
    }
}

void artwork_manager::search_artwork_background(metadb_handle_ptr track, artwork_callback callback) {
    artwork_result result;
    
    try {
        // Get track metadata
        metadb_info_container::ptr info_container = track->get_info_ref();
        const file_info* info = &info_container->info();
        pfc::string8 artist, album, file_path;
        
        if (!info->meta_get("ARTIST", 0)) {
            artist = "Unknown Artist";
        } else {
            artist = info->meta_get("ARTIST", 0);
        }
        
        if (!info->meta_get("ALBUM", 0)) {
            album = "Unknown Album";
        } else {
            album = info->meta_get("ALBUM", 0);
        }
        
        file_path = track->get_path();
        
        // Check cache first
        pfc::string8 cache_key = generate_cache_key(artist, album);
        if (get_from_cache(cache_key, result)) {
            callback(result);
            return;
        }
        
        // For local files, check local directory first
        if (!file_path.is_empty() && !pfc::string_find_first(file_path, "://", 1)) {
            if (find_local_artwork(file_path, result)) {
                save_to_cache(cache_key, result);
                callback(result);
                return;
            }
        }
        
        // Try online sources if enabled (silent background operations)
        if (cfg_enable_itunes) {
            if (search_itunes_api(artist, album, result)) {
                save_to_cache(cache_key, result);
                callback(result);
                return;
            }
        }
        
        if (cfg_enable_lastfm && !cfg_lastfm_key.is_empty()) {
            if (search_lastfm_api(artist, album, result)) {
                save_to_cache(cache_key, result);
                callback(result);
                return;
            }
        }
        
        if (cfg_enable_discogs && !cfg_discogs_key.is_empty()) {
            if (search_discogs_api(artist, album, result)) {
                save_to_cache(cache_key, result);
                callback(result);
                return;
            }
        }
        
        // No artwork found
        result.success = false;
        result.error_message = "No artwork found";
        callback(result);
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
        callback(result);
    }
}

bool artwork_manager::find_local_artwork(const char* file_path, artwork_result& result) {
    try {
        pfc::string8 directory = get_file_directory(file_path);
        if (directory.is_empty()) return false;
        
        // Search for ANY .jpg, .jpeg, or .png file in the directory
        pfc::string8 search_pattern = directory;
        search_pattern << "\\*";
        
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA(search_pattern.get_ptr(), &findData);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                // Skip directories
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    continue;
                }
                
                // Check if file has image extension
                const char* filename = findData.cFileName;
                const char* ext = strrchr(filename, '.');
                if (ext) {
                    if (_stricmp(ext, ".jpg") == 0 || 
                        _stricmp(ext, ".jpeg") == 0 || 
                        _stricmp(ext, ".png") == 0) {
                        
                        pfc::string8 full_path = directory;
                        full_path << "\\" << filename;
                        
                        if (PathFileExistsA(full_path.get_ptr())) {
                            // Try to load the file
                            HANDLE hFile = CreateFileA(full_path.get_ptr(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                            if (hFile != INVALID_HANDLE_VALUE) {
                                DWORD size = GetFileSize(hFile, NULL);
                                if (size > 0 && size < 10 * 1024 * 1024) { // Max 10MB
                                    result.data.set_size(size);
                                    DWORD bytes_read;
                                    
                                    if (ReadFile(hFile, result.data.get_ptr(), size, &bytes_read, NULL) && bytes_read == size) {
                                        if (is_valid_image_data(result.data.get_ptr(), size)) {
                                            result.mime_type = detect_mime_type(result.data.get_ptr(), size);
                                            result.success = true;
                                            CloseHandle(hFile);
                                            FindClose(hFind);
                                            return true;
                                        }
                                    }
                                }
                                CloseHandle(hFile);
                            }
                        }
                    }
                }
            } while (FindNextFileA(hFind, &findData));
            
            FindClose(hFind);
        }
        
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

bool artwork_manager::search_itunes_api(const char* artist, const char* album, artwork_result& result) {
    try {
        // iTunes Search API doesn't require an API key
        pfc::string8 url = "https://itunes.apple.com/search?term=";
        url << url_encode(artist) << "+" << url_encode(album);
        url << "&entity=album&limit=1";
        
        // For now, we would parse JSON response and extract artworkUrl100
        // This is a simplified implementation that would need JSON parsing
        // Let's implement a basic version that tries to download from a constructed URL
        
        // iTunes typically provides artwork URLs in this format:
        pfc::string8 artwork_url = "https://is1-ssl.mzstatic.com/image/thumb/Music/";
        // This would need proper API integration to get actual URLs
        
        return false; // Placeholder - would need full JSON parsing implementation
        
    } catch (const std::exception&) {
        return false;
    }
}

bool artwork_manager::search_discogs_api(const char* artist, const char* album, artwork_result& result) {
    try {
        if (cfg_discogs_key.is_empty()) {
            return false;
        }
        
        // Discogs API requires authentication and proper REST calls
        pfc::string8 url = "https://api.discogs.com/database/search?type=release&artist=";
        url << url_encode(artist) << "&release_title=" << url_encode(album);
        url << "&key=" << cfg_discogs_key.get_ptr();
        
        // This would need JSON parsing to extract image URLs
        return false; // Placeholder
        
    } catch (const std::exception&) {
        return false;
    }
}

bool artwork_manager::search_lastfm_api(const char* artist, const char* album, artwork_result& result) {
    try {
        if (cfg_lastfm_key.is_empty()) {
            return false;
        }
        
        // Last.fm API call for album info
        pfc::string8 url = "https://ws.audioscrobbler.com/2.0/?method=album.getinfo&api_key=";
        url << cfg_lastfm_key.get_ptr();
        url << "&artist=" << url_encode(artist);
        url << "&album=" << url_encode(album);
        url << "&format=json";
        
        // This would need JSON parsing to extract image URLs
        return false; // Placeholder
        
    } catch (const std::exception&) {
        return false;
    }
}

bool artwork_manager::download_image_winhttp(const char* url, artwork_result& result) {
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    
    try {
        // Parse URL using WinHTTP
        DWORD url_len = strlen(url);
        std::vector<wchar_t> url_wide(url_len + 1);
        MultiByteToWideChar(CP_UTF8, 0, url, -1, url_wide.data(), url_len + 1);
        
        URL_COMPONENTS urlComps = {0};
        urlComps.dwStructSize = sizeof(urlComps);
        wchar_t hostname[256] = {0};
        wchar_t urlPath[1024] = {0};
        urlComps.lpszHostName = hostname;
        urlComps.dwHostNameLength = sizeof(hostname) / sizeof(wchar_t);
        urlComps.lpszUrlPath = urlPath;
        urlComps.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);
        
        if (!WinHttpCrackUrl(url_wide.data(), 0, 0, &urlComps)) {
            result.error_message = "Invalid URL format";
            return false;
        }
        
        // Initialize WinHTTP
        hSession = WinHttpOpen(L"foo_artwork/1.0", 
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, 
                              WINHTTP_NO_PROXY_BYPASS, 
                              0);
        if (!hSession) {
            result.error_message = "Failed to initialize WinHTTP";
            return false;
        }
        
        // Set timeouts (in milliseconds) - shorter timeouts for silent operation
        WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 15000);
        
        hConnect = WinHttpConnect(hSession, hostname, 
                                 urlComps.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 
                                 0);
        if (!hConnect) {
            result.error_message = "Failed to connect to server";
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     urlComps.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) {
            result.error_message = "Failed to create request";
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            result.error_message = "Failed to send request";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        // Receive response
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            result.error_message = "Failed to receive response";
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        // Check status code
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           NULL, &statusCode, &statusCodeSize, NULL);
        
        if (statusCode != 200) {
            result.error_message = "HTTP error: ";
            result.error_message << pfc::format_int(statusCode);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return false;
        }
        
        // Read data
        pfc::array_t<t_uint8> temp_data;
        DWORD bytesAvailable;
        const DWORD buffer_size = 8192;
        BYTE buffer[buffer_size];
        
        do {
            bytesAvailable = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable)) {
                break;
            }
            
            if (bytesAvailable == 0) {
                break;
            }
            
            DWORD bytesToRead = std::min(bytesAvailable, buffer_size);
            DWORD bytesRead = 0;
            
            if (WinHttpReadData(hRequest, buffer, bytesToRead, &bytesRead) && bytesRead > 0) {
                size_t old_size = temp_data.get_size();
                temp_data.set_size(old_size + bytesRead);
                memcpy(temp_data.get_ptr() + old_size, buffer, bytesRead);
            }
            
        } while (bytesAvailable > 0);
        
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        
        if (temp_data.get_size() > 0 && is_valid_image_data(temp_data.get_ptr(), temp_data.get_size())) {
            result.data = temp_data;
            result.mime_type = detect_mime_type(temp_data.get_ptr(), temp_data.get_size());
            result.success = true;
            return true;
        }
        
        result.error_message = "Downloaded data is not a valid image";
        return false;
        
    } catch (const std::exception& e) {
        result.error_message = "Unexpected error during download: ";
        result.error_message << e.what();
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
        return false;
    }
}

bool artwork_manager::get_from_cache(const char* cache_key, artwork_result& result) {
    try {
        // Simple file-based cache
        pfc::string8 cache_dir = core_api::get_profile_path();
        cache_dir << "\\foo_artwork_cache\\";
        
        pfc::string8 cache_file = cache_dir;
        cache_file << cache_key << ".cache";
        
        if (PathFileExistsA(cache_file.get_ptr())) {
            HANDLE hFile = CreateFileA(cache_file.get_ptr(), GENERIC_READ, FILE_SHARE_READ, NULL, 
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD size = GetFileSize(hFile, NULL);
                if (size > 0) {
                    result.data.set_size(size);
                    DWORD bytes_read;
                    
                    if (ReadFile(hFile, result.data.get_ptr(), size, &bytes_read, NULL) && bytes_read == size) {
                        result.mime_type = detect_mime_type(result.data.get_ptr(), size);
                        result.success = true;
                        CloseHandle(hFile);
                        return true;
                    }
                }
                CloseHandle(hFile);
            }
        }
        
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

void artwork_manager::save_to_cache(const char* cache_key, const artwork_result& result) {
    try {
        if (!result.success) return;
        
        pfc::string8 cache_dir = core_api::get_profile_path();
        cache_dir << "\\foo_artwork_cache\\";
        
        // Create directory if it doesn't exist
        SHCreateDirectoryExA(NULL, cache_dir.get_ptr(), NULL);
        
        pfc::string8 cache_file = cache_dir;
        cache_file << cache_key << ".cache";
        
        HANDLE hFile = CreateFileA(cache_file.get_ptr(), GENERIC_WRITE, 0, NULL, 
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD bytes_written;
            WriteFile(hFile, result.data.get_ptr(), static_cast<DWORD>(result.data.get_size()), &bytes_written, NULL);
            CloseHandle(hFile);
        }
    } catch (const std::exception&) {
        // Cache save failed - not critical, ignore silently
    }
}

pfc::string8 artwork_manager::generate_cache_key(const char* artist, const char* album) {
    pfc::string8 key = artist;
    key << "_" << album;
    
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
    
    // GIF
    if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) return "image/gif";
    
    // BMP
    if (data[0] == 'B' && data[1] == 'M') return "image/bmp";
    
    return "application/octet-stream";
}

pfc::string8 artwork_manager::get_file_directory(const char* file_path) {
    pfc::string8 directory = file_path;
    
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
