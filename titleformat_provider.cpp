#include "stdafx.h"
#include "titleformat_provider.h"
#include "metadata_cleaner.h"
#include "artwork_manager.h"
#include "async_io_manager.h"
#include "preferences.h"
#include <shlwapi.h>
#include <mutex>

static std::mutex g_tf_mutex;
static pfc::string8 g_tf_artist;
static pfc::string8 g_tf_title;
static pfc::string8 g_tf_cover;
static pfc::string8 g_tf_source;
static pfc::string8 g_tf_track_path;

void titleformat_provider::set_track_artwork_info(metadb_handle_ptr track, const char* artist, const char* title, const char* cover_path, const char* source) {
    if (core_api::is_shutting_down()) return;

    {
        std::lock_guard<std::mutex> lock(g_tf_mutex);
        if (track.is_valid()) {
            g_tf_track_path = track->get_path();
        } else {
            g_tf_track_path.reset();
        }
        if (artist && strlen(artist) > 0) g_tf_artist = artist;
        if (title && strlen(title) > 0) g_tf_title = title;
        if (cover_path && strlen(cover_path) > 0) g_tf_cover = cover_path;
        if (source && strlen(source) > 0) g_tf_source = source;
    }

    if (track.is_valid() && core_api::is_main_thread() && !core_api::is_shutting_down()) {
        try {
            metadb_io::get()->dispatch_refresh(track);
        } catch (...) {}
    }
}

void titleformat_provider::clear_track_artwork_info() {
    {
        std::lock_guard<std::mutex> lock(g_tf_mutex);
        g_tf_track_path.reset();
        g_tf_artist.reset();
        g_tf_title.reset();
        g_tf_cover.reset();
        g_tf_source.reset();
    }
}

void titleformat_provider::get_track_artwork_info(pfc::string8& out_artist, pfc::string8& out_title, pfc::string8& out_cover, pfc::string8& out_source) {
    std::lock_guard<std::mutex> lock(g_tf_mutex);
    out_artist = g_tf_artist;
    out_title = g_tf_title;
    out_cover = g_tf_cover;
    out_source = g_tf_source;
}

class foo_artwork_display_field_provider : public metadb_display_field_provider {
public:
    enum {
        field_artist = 0,
        field_title,
        field_cover,
        field_source,
        field_path,
        field_count
    };

    t_uint32 get_field_count() override {
        return field_count;
    }

    void get_field_name(t_uint32 index, pfc::string_base& out) override {
        switch (index) {
            case field_artist: out = "foo_artwork_artist"; break;
            case field_title: out = "foo_artwork_title"; break;
            case field_cover: out = "foo_artwork_cover"; break;
            case field_source: out = "foo_artwork_source"; break;
            case field_path: out = "foo_artwork_path"; break;
            default: out = ""; break;
        }
    }

    bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) override {
        if (!out || core_api::is_shutting_down()) return false;

        try {
            pfc::string8 current_artist, current_title, current_cover, current_source, current_path;
            {
                std::lock_guard<std::mutex> lock(g_tf_mutex);
                current_artist = g_tf_artist;
                current_title = g_tf_title;
                current_cover = g_tf_cover;
                current_source = g_tf_source;
                current_path = g_tf_track_path;
            }

            pfc::string8 field_val;
            bool is_current_track = false;

            if (handle != nullptr && !current_path.is_empty()) {
                const char* h_path = handle->get_path();
                if (h_path && strcmp(h_path, current_path.c_str()) == 0) {
                    is_current_track = true;
                }
            } else if (handle == nullptr || current_path.is_empty()) {
                is_current_track = true;
            }

            // 1. If matching active playing track and dynamic/resolved values are present, use them
            if (is_current_track) {
                switch (index) {
                    case field_artist: if (!current_artist.is_empty()) field_val = current_artist; break;
                    case field_title: if (!current_title.is_empty()) field_val = current_title; break;
                    case field_cover:
                    case field_path: if (!current_cover.is_empty()) field_val = current_cover; break;
                    case field_source: if (!current_source.is_empty()) field_val = current_source; break;
                }
            }

            // 2. If field_val is empty and handle is provided, extract directly from handle's metadata / disk cache
            if (field_val.is_empty() && handle != nullptr && !core_api::is_shutting_down()) {
                try {
                    metadb_info_container::ptr info_container = handle->get_info_ref();
                    if (info_container.is_valid()) {
                        const file_info& info = info_container->info();
                        if (index == field_artist) {
                            const char* art = info.meta_get("ARTIST", 0);
                            if (art && strlen(art) > 0) {
                                field_val = MetadataCleaner::clean_for_search(art, true).c_str();
                            }
                        } else if (index == field_title) {
                            const char* tit = info.meta_get("TITLE", 0);
                            if (tit && strlen(tit) > 0) {
                                field_val = MetadataCleaner::clean_for_search(tit, true).c_str();
                            }
                        }
                    }
                } catch (...) {}

                if (index == field_cover || index == field_path) {
                    pfc::string8 key = artwork_manager::generate_cache_key_for_track(handle);
                    pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(key);
                    if (PathFileExistsA(cache_file.c_str())) {
                        field_val = cache_file;
                    } else if (cfg_single_file_cache) {
                        pfc::string8 cur = async_io_manager::instance().get_cache_file_path("current");
                        if (PathFileExistsA(cur.c_str())) {
                            field_val = cur;
                        }
                    }
                } else if (index == field_source) {
                    pfc::string8 key = artwork_manager::generate_cache_key_for_track(handle);
                    pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(key);
                    if (PathFileExistsA(cache_file.c_str())) {
                        field_val = "Cache";
                    }
                }
            }

            // 3. Fallback for Preferences Preview formatting when stopped or testing with sample handles
            if (field_val.is_empty()) {
                switch (index) {
                    case field_artist:
                        if (!current_artist.is_empty()) field_val = current_artist;
                        else field_val = "Artist";
                        break;
                    case field_title:
                        if (!current_title.is_empty()) field_val = current_title;
                        else field_val = "Title";
                        break;
                    case field_cover:
                    case field_path:
                        if (!current_cover.is_empty()) field_val = current_cover;
                        else {
                            pfc::string8 cur = async_io_manager::instance().get_cache_file_path("current");
                            field_val = cur;
                        }
                        break;
                    case field_source:
                        if (!current_source.is_empty()) field_val = current_source;
                        else field_val = "Artwork";
                        break;
                }
            }

            if (!field_val.is_empty()) {
                out->write(titleformat_inputtypes::meta, field_val.c_str());
                return true;
            }
        } catch (...) {}

        return false;
    }
};

static service_factory_single_t<foo_artwork_display_field_provider> g_foo_artwork_display_field_provider_factory;
