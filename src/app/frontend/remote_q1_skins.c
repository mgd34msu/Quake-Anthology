#include "remote_q1_skins.h"
#include "remote_q1_private.h"
#include "save_private.h"
#include "qa/image.h"
#include "qa/binary.h"
#include "qa/vfs_view_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SKIN_COUNT 128
#define SKIN_PIXELS (296u * 194u)
typedef struct skin_entry {
    char selected[16], base[16], name[112];
    qa_resource *resource;
    qa_vfs_acquisition opening;
    qa_buffer pixels;
} skin_entry;
struct frontend_remote_q1_skins {
    frontend_remote_q1 *row;
    frontend_remote_q1_skin_bindings bindings;
    qa_vfs *files;
    qa_mount_id root_mount;
    skin_entry cache[SKIN_COUNT];
    size_t count;
    uint8_t selected[32];
    char *infos[32], *base, *all;
    float noskins;
    char paths[32][27];
    size_t path_count, cursor, received;
    qa_fs_stage *stage;
    uint64_t nonce;
    uint8_t percent;
    qa_sha256_context hash;
    qa_sha256_digest saved_hash;
    bool busy, loading, again, waiting, staged, restoring, checkpointed;
    bool paused, waiting_block, resume_requested;
    bool cleanup, cleanup_keep;
};
static bool current(const frontend_remote_q1_skins *o, qa_error *e)
{
    return o && remote_q1_mutable(o->row) && remote_q1_live(o->row, e) &&
        o->bindings.current(o->bindings.context, &o->row->options.domain, e);
}
static bool root_current(const frontend_remote_q1_skins *o)
{
    for (size_t i = 0; i < qa_vfs_mount_count(o->files); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(o->files, i, &mount) && mount.id == o->root_mount)
            return !mount.is_archive && mount.writable &&
                qa_fs_root_same_object(o->bindings.root, qa_vfs_mount_root(o->files, mount.id));
    }
    return false;
}
static bool enter(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!o || o->busy || o->row->busy || o->restoring || o->cleanup || !current(o, e))
        return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW skins lost their actual CLIENT resource owner");
    o->busy = true; return true;
}
static void cache_clear(frontend_remote_q1_skins *o)
{
    for (size_t i = 0; i < o->count; ++i) {
        qa_resource_release(o->cache[i].resource);
        qa_vfs_acquisition_dispose(&o->cache[i].opening);
        qa_buffer_free(&o->cache[i].pixels);
    }
    memset(o->cache, 0, sizeof(o->cache)); memset(o->selected, 0, sizeof(o->selected)); o->count = 0;
}
static bool transfer_clear(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!o->cleanup) { o->cleanup_keep = o->checkpointed; o->cleanup = true; }
    if (!qa_fs_stage_close_checked(&o->stage, o->cleanup_keep, e)) return false;
    o->received = 0; o->percent = 0; o->nonce = 0;
    o->waiting = o->staged = false; memset(&o->hash, 0, sizeof(o->hash));
    memset(&o->saved_hash, 0, sizeof(o->saved_hash));
    o->waiting_block = false;
    o->checkpointed = false;
    o->cleanup = false; return true;
}
static void info(const char *text, const char *key, char *out, size_t capacity)
{
    out[0] = 0; const char *p = text ? text : ""; size_t n = strlen(key);
    if (*p == '\\') ++p;
    while (*p) {
        const char *a = p; while (*p && *p != '\\') ++p;
        size_t k = (size_t)(p - a); if (!*p) break;
        const char *b = ++p; while (*p && *p != '\\') ++p;
        if (k == n && !memcmp(a, key, n)) {
            size_t bytes = (size_t)(p - b); if (bytes >= capacity) bytes = capacity - 1;
            memcpy(out, b, bytes); out[bytes] = 0;
        }
        if (*p) ++p;
    }
}
static void skin_name(const char *text, char out[16])
{
    memset(out, 0, 16);
    bool valid = text && *text && *text != '.' && !strstr(text, "..");
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); valid && *p; ++p)
        valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '+' || *p == '.' || *p == '-';
    if (!valid) text = "base";
    const char *dot = strrchr(text, '.'); size_t n = dot ? (size_t)(dot - text) : strlen(text);
    if (n > 15) n = 15;
    if (!n) { text = "base"; n = 4; }
    memcpy(out, text, n); out[n] = 0;
}
static bool stem_valid(const char *text)
{
    if (!text || !*text || *text == '.' || strstr(text, "..") || strlen(text) > 15) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '+' || *p == '.' || *p == '-')) return false;
    return true;
}
static void player_name(const frontend_remote_q1_skins *o, unsigned slot, char out[16])
{
    char skin[1024]; info(o->row->clients[slot].userinfo, "skin", skin, sizeof(skin));
    skin_name(o->all && *o->all ? o->all : *skin ? skin : o->base, out);
}
static bool acquire(frontend_remote_q1_skins *o, const char *path, qa_resource **resource,
    qa_vfs_acquisition *opening, qa_error *e)
{
    qa_error local = {0};
    if (qa_vfs_acquire_receipt(o->files, path, resource, opening, &local)) return current(o, e);
    if (local.code == QA_ERROR_NOT_FOUND) return current(o, e);
    if (e) *e = local;
    return false;
}
static bool load(frontend_remote_q1_skins *o, skin_entry *entry, qa_error *e)
{
    char path[27]; snprintf(path, sizeof(path), "skins/%s.pcx", entry->selected);
    if (!acquire(o, path, &entry->resource, &entry->opening, e)) return false;
    if (!entry->resource && strcmp(entry->selected, entry->base)) {
        snprintf(path, sizeof(path), "skins/%s.pcx", entry->base);
        if (!acquire(o, path, &entry->resource, &entry->opening, e)) return false;
    }
    if (!entry->resource) return true;
    qa_bytes bytes = qa_resource_bytes(entry->resource);
    if (bytes.size < 128 || qa_load_u16le(bytes.data + 8) >= 320 || qa_load_u16le(bytes.data + 10) >= 200) return true;
    qa_image image = {0}; qa_error local = {0};
    if (!qa_image_decode_pcx(bytes, QA_IMAGE_FORMAT, &image, &local)) {
        if (local.code == QA_ERROR_MEMORY) { if (e) *e = local; return false; }
        return true;
    }
    entry->pixels.data = calloc(1, SKIN_PIXELS); entry->pixels.size = SKIN_PIXELS;
    if (!entry->pixels.data) { qa_image_free(&image); return remote_q1_fail(e, QA_ERROR_MEMORY, "Retaining QW player PCX crop"); }
    for (uint32_t y = 0; y < image.height && y < 194; ++y) {
        size_t width = image.width < 296 ? image.width : 296;
        memcpy(entry->pixels.data + (size_t)y * 296, image.indices.data + (size_t)y * image.width, width);
    }
    qa_sha256_digest digest; char hex[65]; qa_sha256(bytes, &digest); qa_sha256_hex(&digest, hex);
    snprintf(entry->name, sizeof(entry->name), "qw-skin:%s:crop:0,0,296,194:stride320", hex);
    qa_image_free(&image); return true;
}
static bool prepare(frontend_remote_q1_skins *o, qa_error *e)
{
    if (o->loading) return true;
    const qa_cvar_view *n = qa_cvars_find(o->row->options.domain.cvars, "noskins");
    const qa_cvar_view *b = qa_cvars_find(o->row->options.domain.cvars, "baseskin");
    if (!n || !b) return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW skin policy lacks its actual CLIENT cvars");
    bool changed = o->noskins != n->number || !o->base || strcmp(o->base, b->value);
    if (changed) {
        if (!remote_q1_string(&o->base, b->value, e)) return false;
        o->noskins = n->number; cache_clear(o);
    }
    for (unsigned slot = 0; slot < 32; ++slot) {
        const char *raw = o->row->clients[slot].userinfo ? o->row->clients[slot].userinfo : "";
        if (!changed && o->infos[slot] && !strcmp(raw, o->infos[slot])) continue;
        if (!remote_q1_string(o->infos + slot, raw, e)) return false;
        o->selected[slot] = 0; char named[1024]; info(raw, "name", named, sizeof(named));
        if (!*named || o->noskins == 1) continue;
        char selected[16], base[16]; player_name(o, slot, selected); skin_name(o->base, base);
        size_t index = 0;
        while (index < o->count && (strcmp(o->cache[index].selected, selected) || strcmp(o->cache[index].base, base))) ++index;
        if (index == o->count) {
            if (o->count == SKIN_COUNT) {
                cache_clear(o); for (unsigned j = 0; j < 32; ++j) { free(o->infos[j]); o->infos[j] = NULL; }
                return prepare(o, e);
            }
            skin_entry *entry = o->cache + o->count++;
            memcpy(entry->selected, selected, sizeof(selected)); memcpy(entry->base, base, sizeof(base));
            if (!load(o, entry, e)) {
                qa_resource_release(entry->resource); qa_vfs_acquisition_dispose(&entry->opening);
                qa_buffer_free(&entry->pixels); memset(entry, 0, sizeof(*entry)); --o->count;
                free(o->infos[slot]); o->infos[slot] = NULL; return false;
            }
        }
        if (o->cache[index].pixels.data) o->selected[slot] = (uint8_t)(index + 1);
    }
    return current(o, e);
}
static bool refresh(frontend_remote_q1_skins *o, bool *ready, qa_error *e);
static bool advance(frontend_remote_q1_skins *o, bool *ready, qa_error *e)
{
    *ready = false;
    if (o->paused) return true;
    while (o->cursor < o->path_count) {
        bool found, allowed, recording, playback; uint64_t size;
        if (!qa_vfs_probe(o->files, o->paths[o->cursor], &found, &size, e) || !current(o, e)) return false;
        if (found) { ++o->cursor; continue; }
        if (!o->bindings.permission(o->bindings.context, &allowed, &recording, &playback, e) || !current(o, e)) return false;
        const qa_cvar_view *noskins = qa_cvars_find(o->row->options.domain.cvars, "noskins");
        if (!noskins) return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW download lost its actual noskins policy");
        if (!allowed || recording || playback || noskins->number != 0) {
            char message[104]; snprintf(message, sizeof(message), "Skipping QuakeWorld download %s: %s\n",
                o->paths[o->cursor], !allowed ? "local permission" : "demo or skin policy");
            if (!o->bindings.print(o->bindings.context, message, e) || !current(o, e)) return false;
            ++o->cursor; continue;
        }
        char command[36]; snprintf(command, sizeof(command), "download %s", o->paths[o->cursor]);
        o->waiting = o->waiting_block = true;
        if (!o->bindings.reliable(o->bindings.context, command, e) || !current(o, e)) return false;
        return true;
    }
    o->loading = false; cache_clear(o);
    for (unsigned i = 0; i < 32; ++i) { free(o->infos[i]); o->infos[i] = NULL; }
    if (o->again) { o->again = false; return refresh(o, ready, e); }
    if (!prepare(o, e)) return false;
    *ready = true; return true;
}
static bool refresh(frontend_remote_q1_skins *o, bool *ready, qa_error *e)
{
    *ready = false;
    if (o->waiting || o->paused) { o->again = true; return true; }
    const qa_cvar_view *n = qa_cvars_find(o->row->options.domain.cvars, "noskins");
    const qa_cvar_view *b = qa_cvars_find(o->row->options.domain.cvars, "baseskin");
    if (!n || !b || !remote_q1_string(&o->base, b->value, e)) return false;
    o->noskins = n->number; o->loading = true; o->path_count = o->cursor = 0;
    memset(o->selected, 0, sizeof(o->selected));
    if (o->noskins == 0) for (unsigned slot = 0; slot < 32; ++slot) {
        char named[1024], selected[16], path[27];
        info(o->row->clients[slot].userinfo, "name", named, sizeof(named)); if (!*named) continue;
        player_name(o, slot, selected); snprintf(path, sizeof(path), "skins/%s.pcx", selected);
        size_t i = 0; while (i < o->path_count && strcmp(o->paths[i], path)) ++i;
        if (i == o->path_count) strcpy(o->paths[o->path_count++], path);
    }
    return advance(o, ready, e);
}
bool frontend_remote_q1_skins_create(frontend_remote_q1 *row,
    const frontend_remote_q1_skin_bindings *bindings, frontend_remote_q1_skins **out, qa_error *e)
{
    if (!row || !bindings || !out || *out || !qa_q1_is_qw(row->options.domain.protocol) ||
        !bindings->files || !bindings->root || !bindings->maximum_bytes || !bindings->current ||
        !bindings->permission || !bindings->nonce || !bindings->reliable || !bindings->print)
        return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW skins require the actual shared skin content and download authority");
    frontend_remote_q1_skins *o = calloc(1, sizeof(*o));
    if (!o) return remote_q1_fail(e, QA_ERROR_MEMORY, "Creating QW player skin owner");
    *out = o; o->row = row; o->bindings = *bindings;
    o->files = qa_vfs_clone(bindings->files, e); qa_fs_root_retain(bindings->root);
    for (size_t i = 0; o->files && i < qa_vfs_mount_count(o->files); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(o->files, i, &mount) && !mount.is_archive && mount.writable &&
            qa_fs_root_same_object(bindings->root, qa_vfs_mount_root(o->files, mount.id))) {
            o->root_mount = mount.id; break;
        }
    }
    if (!o->files) return false;
    if (!o->root_mount) return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW skin view omits its actual writable base root");
    return remote_q1_string(&o->all, "", e);
}
bool frontend_remote_q1_skins_refresh(frontend_remote_q1_skins *o, bool *ready, qa_error *e)
{
    if (!ready || !enter(o, e)) return false;
    bool ok = refresh(o, ready, e); o->busy = false; return ok;
}
bool frontend_remote_q1_skins_content(frontend_remote_q1_skins *o, qa_vfs *files,
    qa_fs_root *root, qa_error *e)
{
    if (!o || o->busy || o->restoring || o->cleanup || o->row->loaded || o->loading || o->waiting || o->staged ||
        o->count || !files || files != o->row->content.mounts || !root || !current(o, e))
        return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW skin content requires the actual reset receiver's new selected view");
    qa_mount_id selected = 0;
    for (size_t i = 0; i < qa_vfs_mount_count(files); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(files, i, &mount) && !mount.is_archive && mount.writable &&
            qa_fs_root_same_object(root, qa_vfs_mount_root(files, mount.id))) {
            selected = mount.id; break;
        }
    }
    if (!selected) return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW selected view omits its genuine base download authority");
    o->busy = true; qa_vfs *next = qa_vfs_clone(files, e);
    bool ok = next && current(o, e);
    if (ok) {
        qa_fs_root_retain(root); qa_fs_root_close(o->bindings.root); o->bindings.root = root;
        qa_vfs_destroy(o->files); o->files = next; o->root_mount = selected;
    } else qa_vfs_destroy(next);
    o->busy = false; return ok;
}
bool frontend_remote_q1_skins_prepare(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!enter(o, e)) return false;
    bool ok = prepare(o, e); o->busy = false; return ok;
}
bool frontend_remote_q1_skins_receive(frontend_remote_q1_skins *o, const qa_qw_service *m,
    bool *completed, qa_error *e)
{
    if (!m || m->kind != QA_QW_DOWNLOAD || !completed || !enter(o, e)) return false;
    *completed = false; bool ok = true, allowed = false, recording = false, playback = false;
    if (!o->bindings.permission(o->bindings.context, &allowed, &recording, &playback, e) || !current(o, e)) ok = false;
    else if (playback) {
        ok = transfer_clear(o, e);
        if (ok) o->paused = o->resume_requested = false;
    }
    else if (o->paused) {
        o->waiting_block = false;
        if (o->resume_requested) {
            o->paused = o->resume_requested = false; ok = advance(o, completed, e);
        }
    }
    else if (!o->waiting || o->cursor >= o->path_count) ok = remote_q1_fail(e, QA_ERROR_FORMAT, "Unsolicited QW skin download block");
    else if (m->data.download.missing || !allowed) {
        if (m->data.download.missing) {
            char message[80]; snprintf(message, sizeof(message), "QuakeWorld file not found: %s\n", o->paths[o->cursor]);
            ok = o->bindings.print(o->bindings.context, message, e) && current(o, e);
        }
        if (ok) ok = transfer_clear(o, e);
        if (ok) { ++o->cursor; ok = advance(o, completed, e); }
    } else {
        o->waiting_block = false;
        const char *path = o->paths[o->cursor];
        qa_bytes bytes = m->data.download.bytes;
        qa_error disk = {0}; bool disk_failed = false, published = false;
        if (m->data.download.percent > 100 || m->data.download.percent < o->percent || bytes.size > 768 ||
            (bytes.size && !bytes.data))
            ok = remote_q1_fail(e, QA_ERROR_FORMAT, "Invalid bounded QW skin download progression");
        if (ok && bytes.size > o->bindings.maximum_bytes - o->received) {
            ok = false; disk_failed = true;
            qa_error_set(&disk, QA_ERROR_FORMAT, 0, "QW skin exceeds its declared download limit");
        }
        if (ok && !o->stage) {
            uint64_t size = 0;
            ok = o->bindings.nonce(o->bindings.context, &o->nonce, e) && current(o, e);
            if (ok && !o->nonce) ok = remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW download requires its actual nonzero stage nonce");
            if (ok) {
                ok = qa_fs_root_create_directory(o->bindings.root, "skins", &disk) &&
                    qa_fs_stage_open_checked(o->bindings.root, o->paths[o->cursor], o->nonce, false, &o->stage, &size, &disk) && !size;
                disk_failed = !ok;
            }
            if (ok) { o->staged = true; qa_sha256_init(&o->hash); }
        }
        size_t at = 0;
        while (ok && at < bytes.size) {
            size_t written = 0;
            ok = qa_fs_stage_write(o->stage, o->received, (qa_bytes){bytes.data + at, bytes.size - at}, &written, &disk);
            if (written) {
                qa_sha256_update(&o->hash, (qa_bytes){bytes.data + at, written}); o->received += written; at += written;
            }
            if (ok && !written) ok = remote_q1_fail(&disk, QA_ERROR_IO, "QW skin stage made no native write progress");
            disk_failed = !ok;
        }
        if (ok) {
            o->percent = m->data.download.percent;
            if (o->percent < 100) {
                o->waiting_block = true;
                ok = o->bindings.reliable(o->bindings.context, "nextdl", e) && current(o, e);
            }
            else {
                qa_fs_identity identity;
                ok = qa_fs_stage_seal(o->stage, &identity, &disk) && qa_fs_stage_publish(o->stage, &identity, false, &published, &disk);
                disk_failed = !ok;
                if (published) {
                    if (!transfer_clear(o, e)) { o->busy = false; return false; }
                    ++o->cursor;
                }
                if (ok) ok = current(o, e) && advance(o, completed, e);
            }
        }
        if (disk_failed) {
            if (disk.code == QA_ERROR_MEMORY) { if (e) *e = disk; }
            else {
                char message[320]; snprintf(message, sizeof(message), "QuakeWorld download failed for %s: %s\n", path, disk.message);
                if (!published) {
                    if (!transfer_clear(o, e)) { o->busy = false; return false; }
                    ++o->cursor;
                }
                ok = o->bindings.print(o->bindings.context, message, e) && current(o, e) && advance(o, completed, e);
            }
        }
    }
    o->busy = false; return ok;
}
bool frontend_remote_q1_skins_all(frontend_remote_q1_skins *o, const char *all, bool *ready, qa_error *e)
{
    if (!all || !ready || !enter(o, e)) return false;
    bool ok = remote_q1_string(&o->all, all, e);
    if (ok) { cache_clear(o); ok = refresh(o, ready, e); }
    o->busy = false; return ok;
}
bool frontend_remote_q1_skins_cancel(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!enter(o, e)) return false;
    bool ok = true;
    if (!o->paused && o->waiting) {
        bool block = o->waiting_block; ok = transfer_clear(o, e);
        if (ok) { o->paused = true; o->waiting_block = block; }
    }
    o->busy = false; return ok;
}
bool frontend_remote_q1_skins_retry(frontend_remote_q1_skins *o, bool *ready, qa_error *e)
{
    if (!ready || !enter(o, e)) return false;
    *ready = false; bool ok = true;
    if (o->paused) {
        if (o->waiting_block) o->resume_requested = true;
        else { o->paused = o->resume_requested = false; ok = advance(o, ready, e); }
    }
    o->busy = false; return ok;
}
bool frontend_remote_q1_skins_reset(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!o || o->busy) return remote_q1_fail(e, QA_ERROR_ARGUMENT, "QW skins retain an active operation");
    if (!transfer_clear(o, e)) return false;
    cache_clear(o);
    for (unsigned i = 0; i < 32; ++i) { free(o->infos[i]); o->infos[i] = NULL; }
    o->loading = o->again = o->restoring = o->paused = o->resume_requested = false;
    o->path_count = o->cursor = 0; return true;
}
bool frontend_remote_q1_skins_idle(const frontend_remote_q1_skins *o) { return o && !o->busy && !o->cleanup; }
bool frontend_remote_q1_skins_destroy(frontend_remote_q1_skins **owned, qa_error *e)
{
    frontend_remote_q1_skins *o = owned ? *owned : NULL; if (!o) return true;
    if (!frontend_remote_q1_skins_reset(o, e)) return false;
    qa_vfs_destroy(o->files); qa_fs_root_close(o->bindings.root); free(o->base); free(o->all);
    free(o); *owned = NULL; return true;
}
bool frontend_remote_q1_skins_at(const frontend_remote_q1_skins *o, uint32_t slot,
    frontend_remote_q1_skin *out, bool *present, qa_error *e)
{
    if (!o || !out || !present || slot >= 32 || o->busy || o->restoring || o->cleanup || !current(o, e)) return false;
    *out = (frontend_remote_q1_skin){0}; *present = !o->loading && o->selected[slot] != 0;
    if (*present) {
        const skin_entry *entry = o->cache + o->selected[slot] - 1;
        *out = (frontend_remote_q1_skin){entry->name, 296, 194,
            {entry->pixels.data, entry->pixels.size}, entry->resource, &entry->opening};
    }
    return true;
}
qa_vfs *frontend_remote_q1_skins_files(const frontend_remote_q1_skins *o) { return o ? o->files : NULL; }

static bool opening(qa_source_save_io *io, const qa_vfs *files, qa_vfs_acquisition *a)
{
    return qa_source_save_u64(io, &a->mount) && qa_source_save_u64(io, &a->resource_id) &&
        qa_source_save_owned_text(io, &a->path) && qa_source_save_owned_text(io, &a->lookup_path) &&
        qa_source_save_owned_text(io, &a->link_source) && qa_source_save_owned_text(io, &a->link_target) &&
        qa_vfs_acquisition_opening_codec(io, files, a) && a->opening_present;
}
static bool fields(frontend_remote_q1_skins *o, const frontend_remote_q1_restore_refs *refs,
    qa_source_save_io *io)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t signature[8] = {'Q','F','Q','W','S',1,0,0}, expected[8]; memcpy(expected, signature, 8);
    uint64_t view = reading ? 0 : qa_application_content_view_id(refs->content, o->files);
    size_t maximum = o->bindings.maximum_bytes;
    if (!qa_source_save_bytes(io, signature, 8) || memcmp(signature, expected, 8) ||
        !qa_source_save_u64(io, &view) || !view || !qa_source_save_u64(io, &o->root_mount) ||
        !qa_source_save_count(io, &maximum, SIZE_MAX) || maximum != o->bindings.maximum_bytes) return false;
    if (reading) {
        qa_vfs_destroy(o->files); o->files = NULL;
        if (!qa_application_content_claim_view(refs->content, view, &o->files, io->error)) return false;
    }
    if (!root_current(o)) return false;
    if (!qa_source_save_f32(io, &o->noskins) || !qa_source_save_owned_text(io, &o->base) ||
        !qa_source_save_owned_text(io, &o->all) || !o->all || !qa_source_save_bool(io, &o->loading) ||
        !qa_source_save_bool(io, &o->again) || !qa_source_save_bool(io, &o->waiting) ||
        !qa_source_save_bool(io, &o->staged) || !qa_source_save_bool(io, &o->paused) ||
        !qa_source_save_bool(io, &o->waiting_block) || !qa_source_save_bool(io, &o->resume_requested) ||
        !qa_source_save_count(io, &o->path_count, 32) ||
        !qa_source_save_count(io, &o->cursor, o->path_count)) return false;
    for (size_t i = 0; i < o->path_count; ++i) {
        if (!qa_source_save_bytes(io, o->paths[i], sizeof(o->paths[i])) ||
            !memchr(o->paths[i], 0, sizeof(o->paths[i])) || strncmp(o->paths[i], "skins/", 6)) return false;
        size_t n = strlen(o->paths[i]);
        if (n < 11 || strcmp(o->paths[i] + n - 4, ".pcx")) return false;
        char name[16]; size_t length = n - 10;
        if (length > 15) return false;
        memcpy(name, o->paths[i] + 6, length); name[length] = 0;
        if (!stem_valid(name)) return false;
        for (size_t j = 0; j < i; ++j) if (!strcmp(o->paths[j], o->paths[i])) return false;
    }
    if (!qa_source_save_count(io, &o->received, maximum) || !qa_source_save_u64(io, &o->nonce) ||
        !qa_source_save_u8(io, &o->percent) || o->percent > 100) return false;
    qa_sha256_digest hash = o->saved_hash;
    if (!reading && o->stage) { qa_sha256_context copy = o->hash; qa_sha256_final(&copy, &hash); }
    if (!qa_source_save_bytes(io, hash.bytes, sizeof(hash.bytes))) return false;
    if (reading) o->saved_hash = hash;
    if ((o->waiting && (!o->loading || o->cursor >= o->path_count)) ||
        (o->staged && (!o->waiting || !o->nonce)) || (!o->staged && (o->received || o->nonce || o->percent)) ||
        (!o->loading && (o->waiting || o->again || o->paused)) || (o->waiting && o->paused) ||
        (o->paused && o->cursor >= o->path_count) || (o->waiting_block && !o->waiting && !o->paused) ||
        (o->resume_requested && (!o->paused || !o->waiting_block))) return false;
    if (!qa_source_save_count(io, &o->count, SKIN_COUNT)) return false;
    for (size_t i = 0; i < o->count; ++i) {
        skin_entry *entry = o->cache + i; uint64_t pool = 0, resource = 0;
        if (!reading && entry->resource &&
            !qa_application_content_resource_id(refs->content, entry->resource, &pool, &resource)) return false;
        if (!qa_source_save_bytes(io, entry->selected, sizeof(entry->selected)) ||
            !qa_source_save_bytes(io, entry->base, sizeof(entry->base)) ||
            !memchr(entry->selected, 0, sizeof(entry->selected)) || !memchr(entry->base, 0, sizeof(entry->base)) ||
            !qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &resource) || !!pool != !!resource) return false;
        if (!stem_valid(entry->selected) || !stem_valid(entry->base)) return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(entry->selected, o->cache[j].selected) && !strcmp(entry->base, o->cache[j].base)) return false;
        if (reading && resource) {
            entry->resource = (qa_resource *)qa_application_content_resource(refs->content, pool, resource);
            if (!entry->resource || qa_application_content_pool(refs->content, pool) != qa_vfs_resources(o->files)) return false;
            qa_resource_retain(entry->resource);
        }
        if (resource && (!opening(io, o->files, &entry->opening) ||
            entry->opening.resource_id != qa_resource_id(entry->resource) ||
            !qa_vfs_acquisition_retained(o->files, &entry->opening, io->error))) return false;
        if (resource) {
            char selected_path[27], base_path[27];
            snprintf(selected_path, sizeof(selected_path), "skins/%s.pcx", entry->selected);
            snprintf(base_path, sizeof(base_path), "skins/%s.pcx", entry->base);
            if (!entry->opening.path || (strcmp(entry->opening.path, selected_path) && strcmp(entry->opening.path, base_path))) return false;
        }
        bool pixels = !reading && entry->pixels.data;
        if (!qa_source_save_bool(io, &pixels) || (pixels && !resource)) return false;
        if (pixels) {
            if (reading) {
                entry->pixels.data = malloc(SKIN_PIXELS); entry->pixels.size = SKIN_PIXELS;
                if (!entry->pixels.data) return remote_q1_fail(io->error, QA_ERROR_MEMORY, "Importing actual QW skin indices");
            }
            if (entry->pixels.size != SKIN_PIXELS || !qa_source_save_bytes(io, entry->pixels.data, SKIN_PIXELS)) return false;
            char hex[65]; qa_sha256_hex(qa_resource_digest(entry->resource), hex);
            snprintf(entry->name, sizeof(entry->name), "qw-skin:%s:crop:0,0,296,194:stride320", hex);
        }
    }
    for (unsigned i = 0; i < 32; ++i) {
        if (!qa_source_save_owned_text(io, o->infos + i) || !qa_source_save_u8(io, o->selected + i) ||
            o->selected[i] > o->count || (o->selected[i] &&
                (o->loading || !o->cache[o->selected[i] - 1].pixels.data))) return false;
    }
    return true;
}
bool frontend_remote_q1_skins_checkpoint(const frontend_remote_q1_skins *owner,
    const frontend_remote_q1_restore_refs *refs, qa_buffer *out, qa_error *e)
{
    if (!owner || owner->busy || owner->cleanup || !refs || !refs->content || !out || out->data) return false;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, e)) return false;
    bool ok = fields((frontend_remote_q1_skins *)owner, refs, &io) && qa_source_save_finish(&io, out);
    if (ok && owner->staged) ((frontend_remote_q1_skins *)owner)->checkpointed = true;
    qa_source_save_dispose(&io); return ok;
}
bool frontend_remote_q1_skins_restore(frontend_remote_q1 *row,
    const frontend_remote_q1_skin_bindings *bindings, const frontend_remote_q1_restore_refs *refs,
    qa_bytes bytes, frontend_remote_q1_skins **out, qa_error *e)
{
    if (!row || !row->importing || !refs || !refs->content || !frontend_remote_q1_skins_create(row, bindings, out, e)) return false;
    frontend_remote_q1_skins *o = *out; qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, bytes, e)) return false;
    bool ok = fields(o, refs, &io) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io); o->restoring = o->staged;
    return ok;
}
bool frontend_remote_q1_skins_resume(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!o || o->busy || o->cleanup || !current(o, e)) return false;
    if (!o->restoring) return true;
    o->busy = true; uint64_t size = 0;
    /* Resume never creates or truncates. Hash under the actual exclusive
     * writer admission so inspection cannot race a later handle acquisition. */
    bool ok = qa_fs_stage_open_checked(o->bindings.root, o->paths[o->cursor], o->nonce, true, &o->stage, &size, e);
    if (ok && size != o->received) ok = remote_q1_fail(e, QA_ERROR_FORMAT, "Retained QW skin stage size changed after capture");
    qa_sha256_context hash; qa_sha256_init(&hash); uint8_t buffer[4096]; uint64_t offset = 0;
    while (ok && offset < size) {
        size_t got = 0, capacity = size - offset < sizeof(buffer) ? (size_t)(size - offset) : sizeof(buffer);
        ok = qa_fs_stage_read(o->stage, offset, buffer, capacity, &got, e);
        if (ok && got != capacity) ok = remote_q1_fail(e, QA_ERROR_IO, "Retained QW skin stage ended during resume inspection");
        if (ok) { qa_sha256_update(&hash, (qa_bytes){buffer, got}); offset += got; }
    }
    qa_sha256_digest digest; qa_sha256_context copy = hash; qa_sha256_final(&copy, &digest);
    if (ok && !qa_sha256_equal(&digest, &o->saved_hash))
        ok = remote_q1_fail(e, QA_ERROR_FORMAT, "Retained QW skin stage bytes changed after capture");
    if (ok) ok = current(o, e);
    if (ok) { o->hash = hash; o->restoring = false; }
    else if (!qa_fs_stage_close_checked(&o->stage, true, e)) o->cleanup = o->cleanup_keep = true;
    o->busy = false; return ok;
}
