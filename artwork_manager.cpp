#include "stdafx.h"
#include "artwork_manager.h"
#include "preferences.h"
#include <wininet.h>
#include <shlwapi.h>
#include <shlobj.h>

#pragma comment(lib, "wininet.lib")
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

void artwork_manager::get_artwork_async(metadb_handle_ptr track, 
                                        artwork_result& result) {
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
        return;
    }
    
    // For local files, check local directory first
    if (!file_path.is_empty() && !pfc::string_find_first(file_path, "://", 1)) {
        if (find_local_artwork(file_path, result)) {
            save_to_cache(cache_key, result);
            return;
        }
    }
    
    // For now, just return - API integration can be added later
    // This simplified version focuses on local artwork only
    result.success = false;
}

bool artwork_manager::find_local_artwork(const char* file_path, artwork_result& result) {
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
}

bool artwork_manager::search_itunes_api(const char* artist, const char* album, artwork_result& result) {
    // iTunes Search API doesn't require an API key
    // For now, this is a placeholder implementation
    result.success = false;
    return false;
}

bool artwork_manager::search_discogs_api(const char* artist, const char* album, artwork_result& result) {
    // Discogs API requires authentication
    if (cfg_discogs_key.is_empty()) {
        result.success = false;
        return false;
    }
    
    // Placeholder implementation
    result.success = false;
    return false;
}

bool artwork_manager::search_lastfm_api(const char* artist, const char* album, artwork_result& result) {
    if (cfg_lastfm_key.is_empty()) {
        result.success = false;
        return false;
    }
    
    // Placeholder implementation
    result.success = false;
    return false;
}

bool artwork_manager::download_image(const char* url, artwork_result& result) {
    // Simple HTTP download using WinINet
    HINTERNET hInternet = InternetOpenA("foo_artwork/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;
    
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }
    
    // Read data
    const DWORD buffer_size = 8192;
    BYTE buffer[buffer_size];
    DWORD bytes_read;
    pfc::array_t<t_uint8> temp_data;
    
    while (InternetReadFile(hUrl, buffer, buffer_size, &bytes_read) && bytes_read > 0) {
        size_t old_size = temp_data.get_size();
        temp_data.set_size(old_size + bytes_read);
        memcpy(temp_data.get_ptr() + old_size, buffer, bytes_read);
    }
    
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    
    if (temp_data.get_size() > 0 && is_valid_image_data(temp_data.get_ptr(), temp_data.get_size())) {
        result.data = temp_data;
        result.mime_type = detect_mime_type(temp_data.get_ptr(), temp_data.get_size());
        result.success = true;
        return true;
    }
    
    return false;
}

bool artwork_manager::get_from_cache(const char* cache_key, artwork_result& result) {
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
}

void artwork_manager::save_to_cache(const char* cache_key, const artwork_result& result) {
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
        WriteFile(hFile, result.data.get_ptr(), result.data.get_size(), &bytes_written, NULL);
        CloseHandle(hFile);
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
