#include "stdafx.h"
#include "async_io_manager.h"
#include "artwork_manager.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")

// External configuration variables for network settings
extern cfg_int cfg_http_timeout;
extern cfg_int cfg_retry_count;

// MusicBrainz rate limiter - enforces 1 request per second
static std::mutex g_musicbrainz_rate_mutex;
static std::chrono::steady_clock::time_point g_last_musicbrainz_request = std::chrono::steady_clock::now() - std::chrono::seconds(2);

static void enforce_musicbrainz_rate_limit() {
    std::lock_guard<std::mutex> lock(g_musicbrainz_rate_mutex);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_musicbrainz_request);

    // MusicBrainz requires minimum 1 second between requests
    if (elapsed.count() < 1000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 - elapsed.count()));
    }
    g_last_musicbrainz_request = std::chrono::steady_clock::now();
}

// Helper to check if an error is retryable (network issues, timeouts, server errors)
static bool is_retryable_error(const pfc::string8& error) {
    // Retryable: connection failures, timeouts, 5xx server errors
    if (error.find_first("connect") != pfc_infinite) return true;
    if (error.find_first("timeout") != pfc_infinite) return true;
    if (error.find_first("503") != pfc_infinite) return true;  // Service Unavailable
    if (error.find_first("502") != pfc_infinite) return true;  // Bad Gateway
    if (error.find_first("504") != pfc_infinite) return true;  // Gateway Timeout
    if (error.find_first("429") != pfc_infinite) return true;  // Too Many Requests
    return false;
}

// Static member definitions
HWND async_io_manager::main_thread_dispatcher::message_window = nullptr;
std::queue<async_io_manager::main_thread_callback> async_io_manager::main_thread_dispatcher::callback_queue;
std::mutex async_io_manager::main_thread_dispatcher::callback_mutex;
UINT async_io_manager::main_thread_dispatcher::WM_ASYNC_CALLBACK = 0;

async_io_manager& async_io_manager::instance() {
    static async_io_manager instance;
    return instance;
}

async_io_manager::async_io_manager() 
    : completion_port_(nullptr)
    , shutdown_requested_(false)
    , main_thread_id_(GetCurrentThreadId()) {
}

async_io_manager::~async_io_manager() {
    shutdown();
}

void async_io_manager::initialize(size_t thread_count) {
    if (thread_pool_) {
        return; // Already initialized
    }
    
    thread_pool_ = std::make_unique<thread_pool>(thread_count);
    cache_ = std::make_unique<async_cache>();
    
    // Initialize cache directory
    pfc::string8 cache_dir = core_api::get_profile_path();
    cache_dir << "\\foo_artwork_cache\\";
    cache_->initialize(cache_dir);
    
    // Setup I/O completion port
    setup_completion_port();
    
    // Initialize main thread dispatcher
    main_thread_dispatcher::initialize();
}

void async_io_manager::shutdown() {
    shutdown_requested_ = true;
    
    if (cache_) {
        cache_->shutdown();
        cache_.reset();
    }
    
    if (thread_pool_) {
        thread_pool_->shutdown();
        thread_pool_.reset();
    }
    
    // Wait for completion workers
    for (auto& worker : completion_workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    completion_workers_.clear();
    
    if (completion_port_) {
        CloseHandle(completion_port_);
        completion_port_ = nullptr;
    }
    
    main_thread_dispatcher::shutdown();
}

void async_io_manager::read_file_async(const pfc::string8& file_path, file_read_callback callback) {
    ASSERT_MAIN_THREAD();
    
    thread_pool_->enqueue([this, file_path, callback]() {
        perform_overlapped_read(file_path, callback);
    });
}

void async_io_manager::write_file_async(const pfc::string8& file_path, const pfc::array_t<t_uint8>& data, file_write_callback callback) {
    ASSERT_MAIN_THREAD();
    
    thread_pool_->enqueue([this, file_path, data, callback]() {
        perform_overlapped_write(file_path, data, callback);
    });
}

void async_io_manager::scan_directory_async(const pfc::string8& directory, const pfc::string8& pattern, directory_scan_callback callback) {
    ASSERT_MAIN_THREAD();
    
    thread_pool_->enqueue([this, directory, pattern, callback]() {
        perform_directory_scan(directory, pattern, callback);
    });
}

void async_io_manager::http_get_async(const pfc::string8& url, http_request_callback callback) {
    ASSERT_MAIN_THREAD();
    
    thread_pool_->enqueue([this, url, callback]() {
        perform_http_get(url, callback);
    });
}

void async_io_manager::http_get_binary_async(const pfc::string8& url, file_read_callback callback) {
    ASSERT_MAIN_THREAD();
    
    thread_pool_->enqueue([this, url, callback]() {
        perform_http_get_binary(url, callback);
    });
}

void async_io_manager::cache_get_async(const pfc::string8& key, file_read_callback callback) {
    ASSERT_MAIN_THREAD();
    cache_->get_async(key, callback);
}

void async_io_manager::cache_set_async(const pfc::string8& key, const pfc::array_t<t_uint8>& data, file_write_callback callback) {
    cache_->set_async(key, data, callback);
}

void async_io_manager::post_to_main_thread(main_thread_callback callback) {
    // Simple approach: use fb2k_async which is a common pattern in foobar2000 components
    fb2k::inMainThread([callback]() {
        try {
            callback();
        } catch (const std::exception& e) {
            // Error handling for callback exceptions
        } catch (...) {
            // Error handling for unknown exceptions
        }
    });
}

void async_io_manager::submit_task(std::function<void()> task) {
    ASSERT_MAIN_THREAD();
    
    thread_pool_->enqueue(task);
}

bool async_io_manager::is_main_thread() const {
    return GetCurrentThreadId() == main_thread_id_;
}

void async_io_manager::assert_main_thread() const {
#ifdef _DEBUG
    if (!is_main_thread()) {
        DebugBreak();
    }
#endif
}

void async_io_manager::assert_background_thread() const {
#ifdef _DEBUG
    if (is_main_thread()) {
        DebugBreak();
    }
#endif
}

// Thread Pool Implementation
async_io_manager::thread_pool::thread_pool(size_t threads) : stop(false) {
    // Create auto-reset event for thread signaling
    condition_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                
                // Wait for work or shutdown signal
                while (true) {
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        if (stop && tasks.empty()) return;
                        if (!tasks.empty()) {
                            task = std::move(tasks.front());
                            tasks.pop();
                            break;
                        }
                    }
                    // Wait for signal
                    WaitForSingleObject(condition_event, INFINITE);
                }
                
                try {
                    task();
                } catch (const std::exception& e) {
                    pfc::string8 error_msg = "foo_artwork: Thread pool task exception: ";
                    error_msg << e.what();
                } catch (...) {
                }
            }
        });
    }
}

async_io_manager::thread_pool::~thread_pool() {
    shutdown();
}

void async_io_manager::thread_pool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    
    // Signal all waiting threads multiple times to ensure they wake up
    for (size_t i = 0; i < workers.size(); ++i) {
        SetEvent(condition_event);
    }
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();
    
    // Clean up the event handle
    if (condition_event != NULL) {
        CloseHandle(condition_event);
        condition_event = NULL;
    }
}

// Overlapped I/O Implementation
void async_io_manager::setup_completion_port() {
    completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!completion_port_) {
        return;
    }
    
    // Start completion workers
    size_t worker_count = std::max(1u, std::thread::hardware_concurrency() / 2);
    for (size_t i = 0; i < worker_count; ++i) {
        completion_workers_.emplace_back([this]() {
            completion_worker();
        });
    }
}

void async_io_manager::completion_worker() {
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    LPOVERLAPPED overlapped;
    
    while (!shutdown_requested_) {
        BOOL result = GetQueuedCompletionStatus(
            completion_port_,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            1000 // 1 second timeout
        );
        
        if (!result && overlapped == nullptr) {
            // Timeout or error - continue
            continue;
        }
        
        if (overlapped) {
            io_context* context = reinterpret_cast<io_context*>(overlapped);
            DWORD error_code = result ? ERROR_SUCCESS : GetLastError();
            file_io_completion(error_code, bytes_transferred, overlapped);
        }
    }
}

void CALLBACK async_io_manager::file_io_completion(DWORD error_code, DWORD bytes_transferred, LPOVERLAPPED overlapped) {
    std::unique_ptr<io_context> context(reinterpret_cast<io_context*>(overlapped));
    
    bool success = (error_code == ERROR_SUCCESS);
    pfc::string8 error_message;
    
    if (!success) {
        error_message = "File I/O error: ";
        error_message << pfc::format_int(error_code);
    }
    
    // Marshal callback to main thread
    if (context->is_write_operation && context->write_callback) {
        instance().post_to_main_thread([callback = context->write_callback, success, error_message]() {
            callback(success, error_message);
        });
    } else if (!context->is_write_operation && context->read_callback) {
        pfc::array_t<t_uint8> data;
        if (success && bytes_transferred > 0) {
            data = std::move(context->buffer);
            data.set_size(bytes_transferred);
        }
        
        instance().post_to_main_thread([callback = context->read_callback, success, data, error_message]() {
            callback(success, data, error_message);
        });
    }
}

void async_io_manager::perform_overlapped_read(const pfc::string8& file_path, file_read_callback callback) {
    ASSERT_BACKGROUND_THREAD();
    
    auto context = std::make_unique<io_context>();
    context->read_callback = callback;
    context->file_path = file_path;
    context->is_write_operation = false;
    
    // Open file for overlapped I/O
    context->file_handle = CreateFileA(
        file_path.get_ptr(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );
    
    if (context->file_handle == INVALID_HANDLE_VALUE) {
        // Post error to main thread
        DWORD error = GetLastError();
        post_to_main_thread([callback, error]() {
            pfc::string8 error_msg = "Failed to open file: ";
            error_msg << pfc::format_int(error);
            callback(false, pfc::array_t<t_uint8>(), error_msg);
        });
        return;
    }
    
    // Get file size
    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(context->file_handle, &file_size)) {
        post_to_main_thread([callback]() {
            callback(false, pfc::array_t<t_uint8>(), "Failed to get file size");
        });
        return;
    }
    
    // Limit file size to prevent excessive memory usage
    const LONGLONG MAX_FILE_SIZE = 50 * 1024 * 1024; // 50MB
    if (file_size.QuadPart > MAX_FILE_SIZE) {
        post_to_main_thread([callback]() {
            callback(false, pfc::array_t<t_uint8>(), "File too large");
        });
        return;
    }
    
    // Prepare buffer
    context->buffer.set_size(static_cast<size_t>(file_size.QuadPart));
    
    // Associate with completion port
    if (!CreateIoCompletionPort(context->file_handle, completion_port_, 0, 0)) {
        post_to_main_thread([callback]() {
            callback(false, pfc::array_t<t_uint8>(), "Failed to associate with completion port");
        });
        return;
    }
    
    // Start overlapped read
    io_context* context_ptr = context.release(); // Will be cleaned up in completion handler
    BOOL result = ReadFile(
        context_ptr->file_handle,
        context_ptr->buffer.get_ptr(),
        static_cast<DWORD>(context_ptr->buffer.get_size()),
        nullptr,
        &context_ptr->overlapped
    );
    
    if (!result && GetLastError() != ERROR_IO_PENDING) {
        // Immediate error
        std::unique_ptr<io_context> cleanup_context(context_ptr);
        post_to_main_thread([callback]() {
            callback(false, pfc::array_t<t_uint8>(), "Failed to start read operation");
        });
    }
}

void async_io_manager::perform_overlapped_write(const pfc::string8& file_path, const pfc::array_t<t_uint8>& data, file_write_callback callback) {
    ASSERT_BACKGROUND_THREAD();
    
    auto context = std::make_unique<io_context>();
    context->write_callback = callback;
    context->file_path = file_path;
    context->buffer = data;
    context->is_write_operation = true;
    
    // Create directory if needed
    pfc::string8 directory = file_path;
    t_size pos = directory.find_last('\\');
    if (pos != pfc_infinite) {
        directory.truncate(pos);
        SHCreateDirectoryExA(nullptr, directory.get_ptr(), nullptr);
    }
    
    // Open file for overlapped I/O
    context->file_handle = CreateFileA(
        file_path.get_ptr(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
    );
    
    if (context->file_handle == INVALID_HANDLE_VALUE) {
        if (callback) {
            post_to_main_thread([callback]() {
                callback(false, "Failed to create file");
            });
        }
        return;
    }
    
    // Associate with completion port
    if (!CreateIoCompletionPort(context->file_handle, completion_port_, 0, 0)) {
        if (callback) {
            post_to_main_thread([callback]() {
                callback(false, "Failed to associate with completion port");
            });
        }
        return;
    }
    
    // Start overlapped write
    io_context* context_ptr = context.release();
    BOOL result = WriteFile(
        context_ptr->file_handle,
        context_ptr->buffer.get_ptr(),
        static_cast<DWORD>(context_ptr->buffer.get_size()),
        nullptr,
        &context_ptr->overlapped
    );
    
    if (!result && GetLastError() != ERROR_IO_PENDING) {
        std::unique_ptr<io_context> cleanup_context(context_ptr);
        if (callback) {
            post_to_main_thread([callback]() {
                callback(false, "Failed to start write operation");
            });
        }
    }
}

void async_io_manager::perform_directory_scan(const pfc::string8& directory, const pfc::string8& pattern, directory_scan_callback callback) {
    ASSERT_BACKGROUND_THREAD();
    
    std::vector<pfc::string8> files;
    pfc::string8 search_pattern = directory;
    search_pattern << "\\" << pattern;
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_pattern.get_ptr(), &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        post_to_main_thread([callback]() {
            callback(false, std::vector<pfc::string8>(), "Directory not found or empty");
        });
        return;
    }
    
    // Process files in chunks to avoid blocking
    const size_t CHUNK_SIZE = 100;
    size_t count = 0;
    
    do {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            pfc::string8 full_path = directory;
            full_path << "\\" << find_data.cFileName;
            files.push_back(full_path);
        }
        
        // Yield periodically
        if (++count % CHUNK_SIZE == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
    } while (FindNextFileA(find_handle, &find_data) && !shutdown_requested_);
    
    FindClose(find_handle);
    
    post_to_main_thread([callback, files]() {
        callback(true, files, "");
    });
}

// Async Cache Implementation
async_io_manager::async_cache::async_cache() : shutdown_requested(false) {
    // Create auto-reset event for write thread signaling
    write_condition_event = CreateEvent(NULL, FALSE, FALSE, NULL);
}

async_io_manager::async_cache::~async_cache() {
    shutdown();
    
    // Clean up the event handle
    if (write_condition_event != NULL) {
        CloseHandle(write_condition_event);
        write_condition_event = NULL;
    }
}

void async_io_manager::async_cache::initialize(const pfc::string8& cache_dir) {
    cache_directory = cache_dir;
    
    // Create cache directory
    SHCreateDirectoryExA(nullptr, cache_directory.get_ptr(), nullptr);
    
    // Start write-behind thread
    write_thread = std::thread([this]() {
        write_worker();
    });
}

void async_io_manager::async_cache::shutdown() {
    shutdown_requested = true;
    SetEvent(write_condition_event);  // Signal write thread to wake up
    
    if (write_thread.joinable()) {
        write_thread.join();
    }
    
    // Flush any remaining writes
    flush_all();
}

void async_io_manager::async_cache::get_async(const pfc::string8& key, file_read_callback callback) {
    // Check memory cache first
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache_entry* entry = cache_map.query_ptr(key);
        if (entry != nullptr) {
            // Hit in memory cache
            entry->last_access = std::chrono::steady_clock::now();
            
            instance().post_to_main_thread([callback, data = entry->data]() {
                callback(true, data, "");
            });
            return;
        }
    }
    
    // Load from disk asynchronously
    pfc::string8 file_path = get_cache_file_path(key);
    instance().read_file_async(file_path, [this, key, callback](bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error) {
        if (success && data.get_size() > 0) {
            // Store in memory cache
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                cache_entry entry;
                entry.data = data;
                entry.last_access = std::chrono::steady_clock::now();
                entry.dirty = false;
                cache_map.set(key, entry);
            }
        }
        callback(success, data, error);
    });
}

void async_io_manager::async_cache::set_async(const pfc::string8& key, const pfc::array_t<t_uint8>& data, file_write_callback callback) {
    // Store in memory cache immediately
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache_entry entry;
        entry.data = data;
        entry.last_access = std::chrono::steady_clock::now();
        entry.dirty = true;
        cache_map.set(key, entry);
    }
    
    // Queue for write-behind
    {
        std::lock_guard<std::mutex> lock(write_queue_mutex);
        write_queue.emplace(key, data);
    }
    SetEvent(write_condition_event);  // Signal write thread
    
    if (callback) {
        instance().post_to_main_thread([callback]() {
            callback(true, "");
        });
    }
}

void async_io_manager::async_cache::write_worker() {
    while (!shutdown_requested) {
        // Wait for work or shutdown signal
        while (true) {
            {
                std::unique_lock<std::mutex> lock(write_queue_mutex);
                if (shutdown_requested && write_queue.empty()) {
                    return;
                }
                if (!write_queue.empty()) {
                    break;
                }
            }
            // Wait for signal
            WaitForSingleObject(write_condition_event, INFINITE);
        }
        
        std::unique_lock<std::mutex> lock(write_queue_mutex);
        
        if (!write_queue.empty()) {
            auto item = std::move(write_queue.front());
            write_queue.pop();
            lock.unlock();
            
            // Perform write operation
            pfc::string8 file_path = get_cache_file_path(item.first);
            instance().write_file_async(file_path, item.second, nullptr);
        }
    }
}

pfc::string8 async_io_manager::async_cache::get_cache_file_path(const pfc::string8& key) {
    pfc::string8 file_path = cache_directory;
    file_path << key << ".cache";
    return file_path;
}

void async_io_manager::async_cache::flush_all() {
    std::lock_guard<std::mutex> lock(write_queue_mutex);
    while (!write_queue.empty()) {
        auto item = std::move(write_queue.front());
        write_queue.pop();
        
        pfc::string8 file_path = get_cache_file_path(item.first);
        // Synchronous write for shutdown
        HANDLE file = CreateFileA(file_path.get_ptr(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(file, item.second.get_ptr(), static_cast<DWORD>(item.second.get_size()), &written, nullptr);
            CloseHandle(file);
        }
    }
}

// Main Thread Dispatcher Implementation
void async_io_manager::main_thread_dispatcher::initialize() {
    if (message_window) {
        return;
    }
    
    WM_ASYNC_CALLBACK = RegisterWindowMessageA("foo_artwork_async_callback");
    
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "foo_artwork_async_dispatcher";
    RegisterClassExA(&wc);
    
    message_window = CreateWindowExA(
        0, "foo_artwork_async_dispatcher", "",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr
    );
}

void async_io_manager::main_thread_dispatcher::shutdown() {
    if (message_window) {
        DestroyWindow(message_window);
        message_window = nullptr;
    }
}

void async_io_manager::main_thread_dispatcher::post_callback(main_thread_callback callback) {
    {
        std::lock_guard<std::mutex> lock(callback_mutex);
        callback_queue.push(std::move(callback));
    }
    
    if (message_window) {
        // Validate window handle before posting
        if (IsWindow(message_window)) {
            BOOL result = PostMessage(message_window, WM_ASYNC_CALLBACK, 0, 0);
            if (!result) {
                // Try to recreate the window and post again
                message_window = nullptr;
                initialize();
                if (message_window && IsWindow(message_window)) {
                    PostMessage(message_window, WM_ASYNC_CALLBACK, 0, 0);
                }
            }
        } else {
            message_window = nullptr;
            initialize();
            if (message_window && IsWindow(message_window)) {
                PostMessage(message_window, WM_ASYNC_CALLBACK, 0, 0);
            }
        }
    } else {
        initialize();
        if (message_window && IsWindow(message_window)) {
            PostMessage(message_window, WM_ASYNC_CALLBACK, 0, 0);
        }
    }
}

LRESULT CALLBACK async_io_manager::main_thread_dispatcher::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_ASYNC_CALLBACK) {
        std::queue<main_thread_callback> callbacks;
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            callbacks.swap(callback_queue);
        }
        
        while (!callbacks.empty()) {
            try {
                callbacks.front()();
            } catch (const std::exception& e) {
                // Error handling for callback exceptions
            } catch (...) {
                // Error handling for unknown exceptions
            }
            callbacks.pop();
        }
        return 0;
    }
    
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

// Internal HTTP GET implementation (single attempt)
// Returns: true on success, false on failure
// On failure, error_message contains the error description
static bool perform_http_get_internal(const pfc::string8& url, pfc::string8& response, pfc::string8& error_message, int timeout_seconds) {
    response.reset();
    error_message.reset();

    // Convert URL to wide string
    pfc::stringcvt::string_wide_from_utf8 wide_url(url);

    // Parse URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;

    if (!WinHttpCrackUrl(wide_url, 0, 0, &urlComp)) {
        error_message = "Failed to parse URL";
        return false;
    }

    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error_message = "Failed to initialize WinHTTP";
        return false;
    }

    // Set timeouts using user-configurable value (in milliseconds)
    int timeout_ms = timeout_seconds * 1000;
    WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms * 2);

    // Connect to server
    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        error_message = "Failed to connect to server";
        return false;
    }

    // Create request
    std::wstring object(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo) {
        object += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", object.c_str(),
                                          NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Failed to create request";
        return false;
    }

    // Send request
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Failed to send request (timeout or connection error)";
        return false;
    }

    // Receive response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Failed to receive response (timeout)";
        return false;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode >= 500) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Server error (";
        error_message << (int)statusCode << ")";
        return false;
    }

    if (statusCode == 429) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Rate limited (429)";
        return false;
    }

    // Read response data
    DWORD dwSize = 0;
    pfc::string8 temp_response;

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            break;
        }

        if (dwSize == 0) break;

        pfc::array_t<char> buffer;
        buffer.set_size(dwSize + 1);

        DWORD dwDownloaded = 0;
        if (!WinHttpReadData(hRequest, buffer.get_ptr(), dwSize, &dwDownloaded)) {
            break;
        }

        buffer[dwDownloaded] = 0;
        temp_response << buffer.get_ptr();

    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    response = temp_response;
    return true;
}

// HTTP Operations Implementation with retry logic
void async_io_manager::perform_http_get(const pfc::string8& url, http_request_callback callback) {
    ASSERT_BACKGROUND_THREAD();

    // Check if this is a MusicBrainz request and enforce rate limiting
    if (url.find_first("musicbrainz.org") != pfc_infinite) {
        enforce_musicbrainz_rate_limit();
    }

    bool success = false;
    pfc::string8 response;
    pfc::string8 error_message;

    int timeout_seconds = cfg_http_timeout.get_value();
    int max_retries = cfg_retry_count.get_value();

    // Retry loop with exponential backoff
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        if (attempt > 0) {
            // Exponential backoff: 1s, 2s, 4s, ...
            int delay_ms = 1000 * (1 << (attempt - 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        success = perform_http_get_internal(url, response, error_message, timeout_seconds);

        if (success) {
            break;  // Success, no need to retry
        }

        // Check if error is retryable
        if (!is_retryable_error(error_message)) {
            break;  // Non-retryable error, don't retry
        }

        // Log retry attempt
        if (attempt < max_retries) {
            console::printf("foo_artwork: HTTP request failed (attempt %d/%d), retrying: %s",
                           attempt + 1, max_retries + 1, error_message.c_str());
        }
    }

    post_to_main_thread([callback, success, response, error_message]() {
        callback(success, response, error_message);
    });
}

// Internal binary HTTP GET implementation (single attempt)
static bool perform_http_get_binary_internal(const pfc::string8& url, pfc::array_t<t_uint8>& data, pfc::string8& error_message, int timeout_seconds) {
    data.set_size(0);
    error_message.reset();

    // Convert URL to wide string
    pfc::stringcvt::string_wide_from_utf8 wide_url(url);

    // Parse URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    urlComp.dwExtraInfoLength = -1;

    if (!WinHttpCrackUrl(wide_url, 0, 0, &urlComp)) {
        error_message = "Failed to parse URL";
        return false;
    }

    // Initialize WinHTTP
    HINTERNET hSession = WinHttpOpen(L"foobar2000-artwork/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error_message = "Failed to initialize WinHTTP";
        return false;
    }

    // Set timeouts using user-configurable value
    int timeout_ms = timeout_seconds * 1000;
    WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms * 2);

    // Connect to server
    std::wstring hostname(urlComp.lpszHostName, urlComp.dwHostNameLength);
    HINTERNET hConnect = WinHttpConnect(hSession, hostname.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        error_message = "Failed to connect to server";
        return false;
    }

    // Create request
    std::wstring object(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo) {
        object += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", object.c_str(),
                                          NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Failed to create request";
        return false;
    }

    // Send request
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Failed to send request (timeout or connection error)";
        return false;
    }

    // Receive response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Failed to receive response (timeout)";
        return false;
    }

    // Check HTTP status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode >= 500) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Server error (";
        error_message << (int)statusCode << ")";
        return false;
    }

    if (statusCode == 429) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Rate limited (429)";
        return false;
    }

    if (statusCode == 404) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error_message = "Not found (404)";
        return false;
    }

    // Read binary data
    DWORD dwSize = 0;
    pfc::array_t<t_uint8> temp_data;

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            break;
        }

        if (dwSize == 0) break;

        size_t current_size = temp_data.get_size();
        temp_data.set_size(current_size + dwSize);

        DWORD dwDownloaded = 0;
        if (!WinHttpReadData(hRequest, temp_data.get_ptr() + current_size, dwSize, &dwDownloaded)) {
            break;
        }

        // Adjust size if less data was read
        if (dwDownloaded < dwSize) {
            temp_data.set_size(current_size + dwDownloaded);
        }

    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    data = temp_data;
    return true;
}

void async_io_manager::perform_http_get_binary(const pfc::string8& url, file_read_callback callback) {
    ASSERT_BACKGROUND_THREAD();

    // Check if this is a MusicBrainz/CoverArtArchive request and enforce rate limiting
    if (url.find_first("musicbrainz.org") != pfc_infinite ||
        url.find_first("coverartarchive.org") != pfc_infinite) {
        enforce_musicbrainz_rate_limit();
    }

    bool success = false;
    pfc::array_t<t_uint8> data;
    pfc::string8 error_message;

    int timeout_seconds = cfg_http_timeout.get_value();
    int max_retries = cfg_retry_count.get_value();

    // Retry loop with exponential backoff
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        if (attempt > 0) {
            // Exponential backoff: 1s, 2s, 4s, ...
            int delay_ms = 1000 * (1 << (attempt - 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        success = perform_http_get_binary_internal(url, data, error_message, timeout_seconds);

        if (success) {
            break;  // Success, no need to retry
        }

        // Check if error is retryable
        if (!is_retryable_error(error_message)) {
            break;  // Non-retryable error, don't retry
        }

        // Log retry attempt
        if (attempt < max_retries) {
            console::printf("foo_artwork: Binary HTTP request failed (attempt %d/%d), retrying: %s",
                           attempt + 1, max_retries + 1, error_message.c_str());
        }
    }

    post_to_main_thread([callback, success, data, error_message]() {
        callback(success, data, error_message);
    });
}

// Progressive Loader Implementation
void async_io_manager::progressive_loader::load_progressively(std::shared_ptr<load_context> context) {
    if (!context || context->data.get_size() == 0) {
        if (context && context->completion_callback) {
            context->completion_callback(false, "No data to process");
        }
        return;
    }
    
    context->total_size = context->data.get_size();
    context->bytes_processed = 0;
    
    // Start processing
    instance().thread_pool_->enqueue([context]() {
        process_chunk(context);
    });
}

void async_io_manager::progressive_loader::process_chunk(std::shared_ptr<load_context> context) {
    size_t remaining = context->total_size - context->bytes_processed;
    size_t chunk_size = std::min(remaining, CHUNK_SIZE);
    
    if (chunk_size == 0) {
        // Complete
        instance().post_to_main_thread([context]() {
            if (context->completion_callback) {
                context->completion_callback(true, "");
            }
        });
        return;
    }
    
    // Process chunk (simulate work)
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    
    context->bytes_processed += chunk_size;
    
    // Report progress
    if (context->progress_callback) {
        float progress = static_cast<float>(context->bytes_processed) / context->total_size;
        instance().post_to_main_thread([context, progress]() {
            if (context->progress_callback) {
                context->progress_callback(progress);
            }
        });
    }
    
    // Continue with next chunk
    instance().thread_pool_->enqueue([context]() {
        process_chunk(context);
    });
}
