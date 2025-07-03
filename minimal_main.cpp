#include <windows.h>
#include <string>

#define FOOBAR2000_CLIENT_VERSION 80

// Forward declarations
class foobar2000_api {};
class stream_writer {};
class stream_reader {};  
class abort_callback {};

typedef unsigned int t_uint32;
typedef void* pservice_factory_base;

// Minimal service factory (EXACTLY like the working version)
struct service_factory_base {
    static service_factory_base* __internal__list;
    service_factory_base* m_next;
    service_factory_base() : m_next(__internal__list) { __internal__list = this; }
};

service_factory_base* service_factory_base::__internal__list = nullptr;

// Dummy service to satisfy foobar2000's requirement (EXACTLY like working version)
class dummy_service {
public:
    virtual ~dummy_service() {}
};

class dummy_service_impl : public dummy_service {
public:
    dummy_service_impl() {
        // Empty constructor to test if MessageBox is causing the crash
    }
};

// Service factory for dummy service (EXACTLY like working version)
class dummy_service_factory : public service_factory_base {
    dummy_service_impl m_service;
public:
    dummy_service_impl* get_service() { return &m_service; }
};

static dummy_service_factory g_dummy_factory;

// foobar2000_client implementation (EXACTLY like working version)
class foobar2000_client {
public:
    virtual t_uint32 get_version() = 0;
    virtual pservice_factory_base get_service_list() = 0;
    virtual void get_config(stream_writer * p_stream, abort_callback & p_abort) = 0;
    virtual void set_config(stream_reader * p_stream, abort_callback & p_abort) = 0;
    virtual void set_library_path(const char * path, const char * name) = 0;
    virtual void services_init(bool val) = 0;
    virtual bool is_debug() = 0;
};

class foobar2000_client_impl : public foobar2000_client {
public:
    t_uint32 get_version() override { return FOOBAR2000_CLIENT_VERSION; }
    pservice_factory_base get_service_list() override { 
        return service_factory_base::__internal__list; 
    }
    void get_config(stream_writer * p_stream, abort_callback & p_abort) override {}
    void set_config(stream_reader * p_stream, abort_callback & p_abort) override {}
    void set_library_path(const char * path, const char * name) override {}
    void services_init(bool val) override {
        // Services are initialized automatically when the component loads
    }
    bool is_debug() override { return false; }
};

static foobar2000_client_impl g_client;

extern "C" {
    __declspec(dllexport) foobar2000_client* _cdecl foobar2000_get_interface(foobar2000_api* p_api, HINSTANCE hIns) {
        return &g_client;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    return TRUE;
}