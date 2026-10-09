#include "remote_q1_skins.h"
#include "remote_q1_private.h"
#include "qa/image.h"
#include "qa/binary.h"
#include <inttypes.h>
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
    bool busy, loading, again, waiting, staged;
    bool paused, waiting_block, resume_requested;
    bool cleanup;
};
static bool current(const frontend_remote_q1_skins *o, qa_error *e)
{
    return o && remote_q1_mutable(o->row) && remote_q1_live(o->row, e) &&
        o->bindings.current(o->bindings.context, &o->row->options.domain, e);
}
static bool enter(frontend_remote_q1_skins *o, qa_error *e)
{
    if (!o || o->busy || o->row->busy || o->cleanup || !current(o, e))
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
    o->cleanup = true;
    if (!qa_fs_stage_close_checked(&o->stage, false, e)) return false;
    o->received = 0; o->percent = 0; o->nonce = 0;
    o->waiting = o->staged = false;
    o->waiting_block = false;
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
    snprintf(entry->name, sizeof(entry->name), "qw-skin:%p:%" PRIu64 ":crop:0,0,296,194:stride320",
        (void *)qa_vfs_resources(o->files), qa_resource_id(entry->resource));
    qa_image_free(&image); return true;
}
static bool prepare(frontend_remote_q1_skins *o, qa_error *e)
{
    if (o->loading) return true;
    const qa_cvar_view *n = qa_cvars_read(o->row->options.domain.cvars, o->row->noskins);
    const qa_cvar_view *b = qa_cvars_read(o->row->options.domain.cvars, o->row->baseskin);
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
        const qa_cvar_view *noskins = qa_cvars_read(o->row->options.domain.cvars, o->row->noskins);
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
    const qa_cvar_view *n = qa_cvars_read(o->row->options.domain.cvars, o->row->noskins);
    const qa_cvar_view *b = qa_cvars_read(o->row->options.domain.cvars, o->row->baseskin);
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
    if (!o || o->busy || o->cleanup || o->row->loaded || o->loading || o->waiting || o->staged ||
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
            if (ok) o->staged = true;
        }
        size_t at = 0;
        while (ok && at < bytes.size) {
            size_t written = 0;
            ok = qa_fs_stage_write(o->stage, o->received, (qa_bytes){bytes.data + at, bytes.size - at}, &written, &disk);
            if (written) {
                o->received += written; at += written;
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
    o->loading = o->again = o->paused = o->resume_requested = false;
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
    if (!o || !out || !present || slot >= 32 || o->busy || o->cleanup || !current(o, e)) return false;
    *out = (frontend_remote_q1_skin){0}; *present = !o->loading && o->selected[slot] != 0;
    if (*present) {
        const skin_entry *entry = o->cache + o->selected[slot] - 1;
        *out = (frontend_remote_q1_skin){entry->name, 296, 194,
            {entry->pixels.data, entry->pixels.size}, entry->resource, &entry->opening};
    }
    return true;
}
qa_vfs *frontend_remote_q1_skins_files(const frontend_remote_q1_skins *o) { return o ? o->files : NULL; }
