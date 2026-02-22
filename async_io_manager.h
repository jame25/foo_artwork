#pragma once
#include "stdafx.h"
#include <functional>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <vector>

// Use Windows API instead of std::condition_variable for better compatibility
// This avoids the _Cnd_init_in_situ issue with some Windows SDK versions

// Forward declarations
struct artwork_result;

class async_io_manager {
public:
    // Callback types
    typedef std::function<void(bool success, const pfc::array_t<t_uint8>& data, const pfc::string8& error)> file_read_callback;
    typedef std::function<void(bool success, const pfc::string8& error)> file_write_callback;
    typedef std::function<void(bool success, const std::vector<pfc::string8>& files, const pfc::string8& error)> directory_scan_callback;
    typedef std::function<void(bool success, const pfc::string8& response, const pfc::string8& error)> http_request_callback;
    typedef std::function<void()> main_thread_callback;

    // Singleton access
    static async_io_manager& instance();

    // Asynchronous file operations
    void read_file_async(const pfc::string8& file_path, file_read_callback callback);
    void write_file_async(const pfc::string8& file_path, const pfc::array_t<t_uint8>& data, file_write_callback callback);
    void scan_directory_async(const pfc::string8& directory, const pfc::string8& pattern, directory_scan_callback callback);
    
    // Asynchronous HTTP operations
    void http_get_async(const pfc::string8& url, http_request_callback callback);
    void http_get_binary_async(const pfc::string8& url, file_read_callback callback);
    
    // Cache operations with write-behind buffering
    void cache_get_async(const pfc::string8& key, file_read_callback callback);
    void cache_set_async(const pfc::string8& key, const pfc::array_t<t_uint8>& data, file_write_callback callback = nullptr);
    void cache_clear_all();
    void cache_remove(const pfc::string8& key);
    
    // Thread pool management
    void initialize(size_t thread_count = 4);
    void shutdown();
    
    // Main thread marshalling
    void post_to_main_thread(main_thread_callback callback);
    
    // Generic task submission
    void submit_task(std::function<void()> task);
    
    // Thread safety checks
    bool is_main_thread() const;
    void assert_main_thread() const;
    void assert_background_thread() const;

private:
    async_io_manager();
    ~async_io_manager();
    
    // Thread pool implementation
    class thread_pool {
    private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex queue_mutex;
        HANDLE condition_event;  // Windows Event instead of std::condition_variable
        std::atomic<bool> stop;
        
    public:
        thread_pool(size_t threads);
        ~thread_pool();
        
        template<class F>
        void enqueue(F&& f) {
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                if (stop) return;
                tasks.emplace(std::forward<F>(f));
            }
            SetEvent(condition_event);  // Signal waiting threads
        }
        
        void shutdown();
    };
    
    // Overlapped I/O context
    struct io_context {
        OVERLAPPED overlapped;
        file_read_callback read_callback;
        file_write_callback write_callback;
        pfc::array_t<t_uint8> buffer;
        pfc::string8 file_path;
        pfc::string8 error_message;
        HANDLE file_handle;
        bool is_write_operation;
        
        io_context() : file_handle(INVALID_HANDLE_VALUE), is_write_operation(false) {
            ZeroMemory(&overlapped, sizeof(overlapped));
            overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        }
        
        ~io_context() {
            if (overlapped.hEvent) {
                CloseHandle(overlapped.hEvent);
            }
            if (file_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(file_handle);
            }
        }
    };
    
    // Cache implementation with write-behind
    class async_cache {
    private:
        struct cache_entry {
            pfc::array_t<t_uint8> data;
            std::chrono::steady_clock::time_point last_access;
            bool dirty;
            
            cache_entry() : dirty(false) {}
        };
        
        pfc::map_t<pfc::string8, cache_entry> cache_map;
        std::mutex cache_mutex;
        std::queue<std::pair<pfc::string8, pfc::array_t<t_uint8>>> write_queue;
        std::mutex write_queue_mutex;
        HANDLE write_condition_event;  // Windows Event instead of std::condition_variable
        std::thread write_thread;
        std::atomic<bool> shutdown_requested;
        pfc::string8 cache_directory;
        
        void write_worker();
        pfc::string8 get_cache_file_path(const pfc::string8& key);
        
    public:
        async_cache();
        ~async_cache();
        
        void initialize(const pfc::string8& cache_dir);
        void get_async(const pfc::string8& key, file_read_callback callback);
        void set_async(const pfc::string8& key, const pfc::array_t<t_uint8>& data, file_write_callback callback = nullptr);
        void remove(const pfc::string8& key);
        void clear_all();
        void flush_all();
        void shutdown();
    };
    
    // Progressive image loader
    class progressive_loader {
    public:
        struct load_context {
            pfc::array_t<t_uint8> data;
            size_t bytes_processed;
            size_t total_size;
            std::function<void(float progress)> progress_callback;
            std::function<void(bool success, const pfc::string8& error)> completion_callback;
            
            load_context() : bytes_processed(0), total_size(0) {}
        };
        
        static void load_progressively(std::shared_ptr<load_context> context);
        
    private:
        static const size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
        static void process_chunk(std::shared_ptr<load_context> context);
    };
    
    // Main thread callback system
    class main_thread_dispatcher {
    private:
        static HWND message_window;
        static std::queue<main_thread_callback> callback_queue;
        static std::mutex callback_mutex;
        static UINT WM_ASYNC_CALLBACK;
        
        static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        
    public:
        static void initialize();
        static void shutdown();
        static void post_callback(main_thread_callback callback);
    };
    
    // File operations implementation
    void perform_overlapped_read(const pfc::string8& file_path, file_read_callback callback);
    void perform_overlapped_write(const pfc::string8& file_path, const pfc::array_t<t_uint8>& data, file_write_callback callback);
    void perform_directory_scan(const pfc::string8& directory, const pfc::string8& pattern, directory_scan_callback callback);
    
    // HTTP operations implementation
    void perform_http_get(const pfc::string8& url, http_request_callback callback);
    void perform_http_get_binary(const pfc::string8& url, file_read_callback callback);
    
    // IOCP completion handling
    void setup_completion_port();
    void completion_worker();
    static void CALLBACK file_io_completion(DWORD error_code, DWORD bytes_transferred, LPOVERLAPPED overlapped);
    
    // Member variables
    std::unique_ptr<thread_pool> thread_pool_;
    std::unique_ptr<async_cache> cache_;
    HANDLE completion_port_;
    std::vector<std::thread> completion_workers_;
    std::atomic<bool> shutdown_requested_;
    DWORD main_thread_id_;
    
    // Active I/O contexts
    std::mutex active_contexts_mutex_;
    std::vector<std::unique_ptr<io_context>> active_contexts_;
};

// Utility macros for thread safety
#ifdef _DEBUG
#define ASSERT_MAIN_THREAD() async_io_manager::instance().assert_main_thread()
#define ASSERT_BACKGROUND_THREAD() async_io_manager::instance().assert_background_thread()
#else
#define ASSERT_MAIN_THREAD()
#define ASSERT_BACKGROUND_THREAD()
#endif
