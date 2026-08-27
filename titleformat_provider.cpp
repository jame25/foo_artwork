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
static pfc::string8 g_tf_artist_full;
static pfc::string8 g_tf_title;
static pfc::string8 g_tf_album;
static pfc::string8 g_tf_listeners;
static pfc::string8 g_tf_cover;
static pfc::string8 g_tf_source;
static pfc::string8 g_tf_status;
static pfc::string8 g_tf_track_path;

void titleformat_provider::set_track_artwork_info(metadb_handle_ptr track, 
                                                   const char* artist, 
                                                   const char* title, 
                                                   const char* cover_path, 
                                                   const char* source,
                                                   const char* artist_full,
                                                   const char* album,
                                                   const char* listeners) {
    if (core_api::is_shutting_down()) return;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_tf_mutex);
        pfc::string8 new_path = track.is_valid() ? track->get_path() : "";
        pfc::string8 new_artist = artist ? artist : "";
        pfc::string8 new_artist_full = (artist_full && artist_full[0] != '\0') ? artist_full : (artist ? artist : "");
        pfc::string8 new_title = title ? title : "";
        pfc::string8 new_album = album ? album : "";
        pfc::string8 new_listeners = listeners ? listeners : "";
        pfc::string8 new_cover = cover_path ? cover_path : "";
        pfc::string8 new_source = source ? source : "";

        // If this is the same track/metadata and we already have a resolved non-Cache provider,
        // do not let a subsequent "Cache" source downgrade it.
        if (g_tf_track_path == new_path && g_tf_artist == new_artist && g_tf_title == new_title &&
            !g_tf_source.is_empty() && g_tf_source != "Cache" && new_source == "Cache") {
            new_source = g_tf_source;
        }

        if (!new_source.is_empty()) {
            pfc::string8 auto_status = pfc::string8("Artwork loaded from ") + new_source;
            if (g_tf_status != auto_status) {
                g_tf_status = auto_status;
                changed = true;
            }
        }

        if (g_tf_track_path != new_path || g_tf_artist != new_artist || g_tf_artist_full != new_artist_full ||
            g_tf_title != new_title || g_tf_album != new_album || g_tf_listeners != new_listeners ||
            g_tf_cover != new_cover || g_tf_source != new_source) {
            changed = true;
            g_tf_track_path = new_path;
            g_tf_artist = new_artist;
            g_tf_artist_full = new_artist_full;
            g_tf_title = new_title;
            g_tf_album = new_album;
            g_tf_listeners = new_listeners;
            g_tf_cover = new_cover;
            g_tf_source = new_source;
        }
    }
}

void titleformat_provider::set_status(const char* status) {
    if (core_api::is_shutting_down()) return;
    std::lock_guard<std::mutex> lock(g_tf_mutex);
    g_tf_status = status ? status : "";
}

void titleformat_provider::get_status(pfc::string8& out_status) {
    std::lock_guard<std::mutex> lock(g_tf_mutex);
    out_status = g_tf_status;
}

void titleformat_provider::clear_track_artwork_info() {
    {
        std::lock_guard<std::mutex> lock(g_tf_mutex);
        g_tf_track_path.reset();
        g_tf_artist.reset();
        g_tf_artist_full.reset();
        g_tf_title.reset();
        g_tf_album.reset();
        g_tf_listeners.reset();
        g_tf_cover.reset();
        g_tf_source.reset();
        g_tf_status.reset();
    }
}

void titleformat_provider::get_track_artwork_info(pfc::string8& out_artist, pfc::string8& out_title, pfc::string8& out_cover, pfc::string8& out_source) {
    std::lock_guard<std::mutex> lock(g_tf_mutex);
    out_artist = g_tf_artist;
    out_title = g_tf_title;
    out_cover = g_tf_cover;
    out_source = g_tf_source;
}

void titleformat_provider::get_track_artwork_info_extended(pfc::string8& out_artist, pfc::string8& out_artist_full, pfc::string8& out_title, pfc::string8& out_album, pfc::string8& out_listeners, pfc::string8& out_cover, pfc::string8& out_source) {
    std::lock_guard<std::mutex> lock(g_tf_mutex);
    out_artist = g_tf_artist;
    out_artist_full = g_tf_artist_full;
    out_title = g_tf_title;
    out_album = g_tf_album;
    out_listeners = g_tf_listeners;
    out_cover = g_tf_cover;
    out_source = g_tf_source;
}

class foo_artwork_display_field_provider : public metadb_display_field_provider {
public:
    enum {
        field_artist = 0,
        field_artist_full,
        field_title,
        field_album,
        field_listeners,
        field_cover,
        field_source,
        field_path,
        field_status,
        field_count
    };

    t_uint32 get_field_count() override {
        return field_count;
    }

    void get_field_name(t_uint32 index, pfc::string_base& out) override {
        switch (index) {
            case field_artist: out = "foo_artwork_artist"; break;
            case field_artist_full: out = "foo_artwork_artist_full"; break;
            case field_title: out = "foo_artwork_title"; break;
            case field_album: out = "foo_artwork_album"; break;
            case field_listeners: out = "foo_artwork_listeners"; break;
            case field_cover: out = "foo_artwork_cover"; break;
            case field_source: out = "foo_artwork_source"; break;
            case field_path: out = "foo_artwork_path"; break;
            case field_status: out = "foo_artwork_status"; break;
            default: out = ""; break;
        }
    }

    bool process_field(t_uint32 index, metadb_handle* handle, titleformat_text_out* out) override {
        if (!out || core_api::is_shutting_down()) return false;

        try {
            pfc::string8 current_artist, current_artist_full, current_title, current_album, current_listeners;
            pfc::string8 current_cover, current_source, current_status, current_path;
            {
                std::lock_guard<std::mutex> lock(g_tf_mutex);
                current_artist = g_tf_artist;
                current_artist_full = g_tf_artist_full;
                current_title = g_tf_title;
                current_album = g_tf_album;
                current_listeners = g_tf_listeners;
                current_cover = g_tf_cover;
                current_source = g_tf_source;
                current_status = g_tf_status;
                current_path = g_tf_track_path;
            }

            pfc::string8 field_val;
            bool is_current_track = false;

            if (handle != nullptr && !current_path.is_empty()) {
                const char* h_path = handle->get_path();
                if (h_path && strcmp(h_path, current_path.c_str()) == 0) {
                    is_current_track = true;
                }
            } else if (handle == nullptr) {
                is_current_track = true;
            }

            // 1. If matching active playing track and dynamic/resolved values are present, use them
            if (is_current_track) {
                switch (index) {
                    case field_artist: if (!current_artist.is_empty()) field_val = current_artist; break;
                    case field_artist_full:
                        if (!current_artist_full.is_empty()) field_val = current_artist_full;
                        else if (!current_artist.is_empty()) field_val = current_artist;
                        break;
                    case field_title: if (!current_title.is_empty()) field_val = current_title; break;
                    case field_album: if (!current_album.is_empty()) field_val = current_album; break;
                    case field_listeners: if (!current_listeners.is_empty()) field_val = current_listeners; break;
                    case field_cover:
                    case field_path: if (!current_cover.is_empty()) field_val = current_cover; break;
                    case field_source: if (!current_source.is_empty()) field_val = current_source; break;
                    case field_status: if (!current_status.is_empty()) field_val = current_status; break;
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
                        } else if (index == field_artist_full) {
                            const char* art = info.meta_get("ARTIST", 0);
                            if (art && strlen(art) > 0) {
                                field_val = art;
                            }
                        } else if (index == field_title) {
                            const char* tit = info.meta_get("TITLE", 0);
                            if (tit && strlen(tit) > 0) {
                                field_val = MetadataCleaner::clean_for_search(tit, true).c_str();
                            }
                        } else if (index == field_album) {
                            const char* alb = info.meta_get("ALBUM", 0);
                            if (alb && strlen(alb) > 0) {
                                field_val = alb;
                            }
                        }
                    }
                } catch (...) {}

                if (index == field_cover || index == field_path) {
                    pfc::string8 key = artwork_manager::generate_cache_key_for_track(handle);
                    pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(key);
                    if (PathFileExistsA(cache_file.c_str())) {
                        field_val = cache_file;
                    }
                } else if (index == field_source) {
                    pfc::string8 key = artwork_manager::generate_cache_key_for_track(handle);
                    pfc::string8 cache_file = async_io_manager::instance().get_cache_file_path(key);
                    if (PathFileExistsA(cache_file.c_str())) {
                        field_val = "Cache";
                    }
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
