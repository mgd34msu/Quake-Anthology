#include "qa/network_q3_client_download.h"
#include "qa/archive.h"
#include "qa/source_save.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct package_request { char *remote, *local; uint32_t checksum; } package_request;
struct qa_q3_client_downloads {
    qa_q3_client_download_bindings bindings;
    package_request *requests;
    size_t count, next;
    uint64_t generation, stage_nonce, native_stage_nonce;
    qa_fs_stage *stage;
    qa_fs_identity publication_identity;
    qa_q3_download pending;
    size_t write_cursor;
    qa_error failure;
    uint8_t finish_sent;
    char *stage_path;
    int32_t block, received, size, advertised_size;
    bool active, paused, receiving, reload_pending;
    bool request_pending,block_pending,block_written,acknowledged,inspected,published;
    bool finish_pending,reloaded,stop_pending,stop_queued;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static void release_requests(qa_q3_client_downloads *owner)
{
    for (size_t i = 0; owner->requests && i < owner->count; ++i) { free(owner->requests[i].remote); free(owner->requests[i].local); }
    free(owner->requests); owner->requests = NULL; owner->count = owner->next = 0;
}
static bool current(qa_q3_client_downloads *owner, qa_error *error)
{ return owner && owner->bindings.current(owner->bindings.context, error); }
static bool returned(qa_q3_client_downloads *owner,uint64_t generation,qa_error *error)
{
    return (owner->generation==generation && current(owner,error)) ||
        fail(error,QA_ERROR_ARGUMENT,"Q3 download callback retired its actual package continuation");
}
static void retained_failure(qa_q3_client_downloads *,const qa_error *);
static bool destination(qa_q3_client_downloads *owner, const char *name, qa_buffer *out, qa_error *error)
{
    if (!qa_q3_download_name(name, error) || !owner->bindings.destination(owner->bindings.context, name, out, error)) return false;
    return (out->data && out->size && out->data[out->size - 1] == 0 &&
        !memchr(out->data, 0, out->size - 1) && qa_q3_download_name((const char *)out->data, error)) ||
        fail(error, QA_ERROR_FORMAT, "Q3 package destination lacks its actual contained mount spelling");
}
bool qa_q3_client_downloads_create(const qa_q3_client_download_bindings *bindings,
    qa_q3_client_downloads **out, qa_error *error)
{
    if (!bindings || !bindings->root || !bindings->current || !bindings->permission || !bindings->destination || !bindings->reference ||
        !bindings->nonce || !bindings->reliable || !bindings->send_packet || !bindings->reload_packages ||
        !bindings->progress || !bindings->prepare_stage || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 client downloads require complete installed filesystem and connection bindings");
    qa_q3_client_downloads *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Allocating admitted Q3 client downloads");
    owner->bindings = *bindings; owner->generation = 1; qa_fs_root_retain(bindings->root); *out = owner; return true;
}
void qa_q3_client_downloads_close(qa_q3_client_downloads *owner)
{
    if (!owner) return;
    qa_fs_stage_close(owner->stage, false); owner->stage = NULL; owner->stage_nonce = owner->native_stage_nonce = 0;
    free(owner->stage_path); owner->stage_path = NULL;
    release_requests(owner); owner->active = owner->paused = owner->reload_pending = false; owner->advertised_size = 0;
    owner->request_pending=owner->block_pending=owner->block_written=owner->acknowledged=false;
    owner->inspected=owner->published=owner->finish_pending=owner->reloaded=false;
    owner->stop_pending=owner->stop_queued=false; owner->write_cursor=0; owner->finish_sent=0;
    owner->pending=(qa_q3_download){0}; owner->failure=(qa_error){0};
    if (owner->generation != UINT64_MAX) ++owner->generation;
}
void qa_q3_client_downloads_destroy(qa_q3_client_downloads *owner)
{ if (owner) { qa_q3_client_downloads_close(owner); qa_fs_root_close(owner->bindings.root); free(owner); } }
void qa_q3_client_downloads_rebind(qa_q3_client_downloads *owner, void *context)
{ if (owner) owner->bindings.context = context; }
bool qa_q3_client_downloads_active(const qa_q3_client_downloads *owner)
{ return owner && (owner->active || owner->finish_pending || owner->reload_pending) && !owner->paused; }
size_t qa_q3_client_downloads_progress_count(const qa_q3_client_downloads *owner)
{ return owner && (owner->active || owner->paused) ? owner->count - owner->next : 0; }
bool qa_q3_client_downloads_progress_at(const qa_q3_client_downloads *owner, size_t ordinal,
    qa_q3_client_download_progress *out)
{
    if (!out || ordinal >= qa_q3_client_downloads_progress_count(owner)) return false;
    bool running = owner->active && !ordinal;
    *out = (qa_q3_client_download_progress){.path = owner->requests[owner->next + ordinal].local,
        .phase = running ? QA_Q3_CLIENT_DOWNLOAD_RUNNING : QA_Q3_CLIENT_DOWNLOAD_PENDING,
        .received = running ? (uint64_t)owner->received : 0,
        .total = running && owner->advertised_size > 0 ? (uint64_t)owner->advertised_size : 0,
        .total_known = running && owner->advertised_size > 0};
    return true;
}
static bool start_next(qa_q3_client_downloads *owner, bool *started, qa_error *error)
{
    uint64_t generation=owner->generation;
    owner->active = owner->next < owner->count; *started = owner->active; owner->advertised_size = 0;
    if (!owner->active) return true;
    package_request *request = owner->requests + owner->next;
    owner->block = owner->received = owner->size = 0;
    owner->bindings.progress(owner->bindings.context, request->remote, 0, 0);
    if(!returned(owner,generation,error)) return false;
    char command[80]; snprintf(command, sizeof(command), "download %s", request->remote);
    owner->request_pending=true;
    if (!owner->bindings.reliable(owner->bindings.context, command, error) || !returned(owner,generation,error)) return false;
    owner->request_pending=false; return true;
}
typedef struct existence { qa_q3_client_downloads *owner; qa_error failure; } existence;
static bool exists(void *context, const char *name)
{
    existence *lookup = context; qa_buffer mapped = {0}; qa_fs_entry_kind kind = QA_FS_MISSING;
    bool ok = destination(lookup->owner, name, &mapped, &lookup->failure) &&
        qa_fs_root_status(lookup->owner->bindings.root, (const char *)mapped.data, &kind, NULL, &lookup->failure);
    qa_buffer_free(&mapped); return ok && kind != QA_FS_MISSING;
}
static char *copy_string(const char *text, qa_error *error)
{
    size_t length = strlen(text); char *copy = malloc(length + 1);
    if (!copy) { fail(error, QA_ERROR_MEMORY, "Retaining Q3 admitted package request"); return NULL; }
    memcpy(copy, text, length + 1); return copy;
}
bool qa_q3_client_downloads_begin(qa_q3_client_downloads *owner, const qa_q3_package *references, size_t count,
    const uint32_t *loaded, size_t loaded_count, bool *downloading, qa_error *error)
{
    if (!current(owner, error) || !downloading || owner->receiving || count > QA_Q3_SEARCH_PATHS ||
        (count && !references) || (loaded_count && !loaded))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 package admission lacks its current references and idle receiver");
    bool allowed;
    if (!owner->bindings.permission(owner->bindings.context, &allowed, error)) return false;
    qa_q3_client_downloads_close(owner); *downloading = false;
    if (!allowed) return true;
    if (owner->generation == UINT64_MAX) return fail(error, QA_ERROR_FORMAT, "Q3 download generation exhausted");
    char list[1024]; existence lookup = {.owner = owner};
    bool ok = qa_q3_compare_packages(references, count, loaded, loaded_count, exists, &lookup, true, list, sizeof(list), error);
    if (lookup.failure.code) { if (error) *error = lookup.failure; ok = false; }
    if (ok && *list) {
        owner->requests = calloc(count ? count : 1, sizeof(*owner->requests));
        if (!owner->requests) ok = fail(error, QA_ERROR_MEMORY, "Retaining Q3 package queue");
    }
    char *at = list;
    while (ok && *at) {
        if (*at++ != '@') { ok = fail(error, QA_ERROR_FORMAT, "Incomplete Q3 package request pair"); break; }
        char *remote = at, *separator = strchr(at, '@');
        if (!separator) { ok = fail(error, QA_ERROR_FORMAT, "Incomplete Q3 package request pair"); break; }
        *separator = 0; at = separator + 1; char *local = at; separator = strchr(at, '@');
        if (separator) { *separator = 0; at = separator; } else at += strlen(at);
        if (strlen(remote) >= 64 || !qa_q3_download_name(remote, error) || !qa_q3_download_name(local, error)) { ok = false; break; }
        bool found = false; uint32_t checksum = 0;
        size_t remote_length = strlen(remote) - 4;
        for (size_t i = 0; ok && i < count; ++i) {
            const char *name = references[i].name;
            if (!name || strlen(name) != remote_length || memcmp(name, remote, remote_length)) continue;
            bool present = false;
            for (size_t j = 0; j < loaded_count; ++j) if (loaded[j] == references[i].checksum) present = true;
            if (present) continue;
            if (found && checksum != references[i].checksum) ok = fail(error, QA_ERROR_FORMAT, "Q3 server references conflicting download identities");
            found = true; checksum = references[i].checksum;
        }
        if (!ok || !found || owner->count == count) { if (ok) ok = fail(error, QA_ERROR_FORMAT, "Q3 package pair has no actual server reference"); break; }
        package_request *request = owner->requests + owner->count++;
        request->checksum = checksum; request->remote = copy_string(remote, error); request->local = copy_string(local, error);
        if (!request->remote || !request->local) ok = false;
        if (ok) ok = owner->bindings.reference(owner->bindings.context, remote, checksum, error);
        if (separator) *separator = '@';
    }
    if (ok) {
        qa_error operation={0}; ok=start_next(owner,downloading,&operation);
        if (!ok && owner->request_pending) { retained_failure(owner,&operation); ok=true; }
        else if (!ok && error) *error=operation;
    }
    if (!ok) qa_q3_client_downloads_close(owner);
    return ok;
}
bool qa_q3_client_downloads_size(qa_q3_client_downloads *owner, int32_t size, int32_t *effective, qa_error *error)
{
    if (!current(owner, error) || !effective) return false;
    if ((owner->stage || owner->block_pending) && size != owner->advertised_size)
        return fail(error, QA_ERROR_FORMAT, "Q3 package size changed during the genuine native transfer");
    owner->advertised_size = owner->size = size; *effective = size;
    const char *name = owner->active ? owner->requests[owner->next].local : "";
    owner->bindings.progress(owner->bindings.context, name, owner->received, size); return true;
}
static bool inspect(qa_q3_client_downloads *owner, const package_request *request, qa_error *error)
{
    qa_fs_stage_mapping *mapping = NULL; qa_archive *archive = NULL;
    if (!qa_fs_stage_map(owner->stage, &mapping, error)) return false;
    qa_bytes bytes = qa_fs_stage_mapping_bytes(mapping);
    bool ok = bytes.size == (size_t)owner->advertised_size;
    if (!ok) fail(error, QA_ERROR_FORMAT, "Q3 downloaded stage differs from its advertised prefix");
    if (ok) ok = qa_archive_open_memory(bytes, QA_ARCHIVE_PK3, &archive, error);
    uint32_t checksum = 0;
    if (ok) ok = qa_archive_q3_checksums(archive, 0, &checksum, NULL, error);
    if (ok && checksum != request->checksum) ok = fail(error, QA_ERROR_FORMAT, "Downloaded Q3 package checksum differs from its server reference");
    qa_archive_close(archive); qa_fs_stage_unmap(mapping); return ok;
}
static void retained_failure(qa_q3_client_downloads *owner,const qa_error *error)
{
    if (!error || error->code==QA_OK) return;
    bool changed=owner->failure.code!=error->code || owner->failure.offset!=error->offset ||
        strcmp(owner->failure.message,error->message);
    owner->failure=*error;
    if(changed && owner->bindings.failure) owner->bindings.failure(owner->bindings.context,error);
}
static void clear_block(qa_q3_client_downloads *owner)
{
    owner->block_pending=owner->block_written=owner->acknowledged=false;
    owner->inspected=owner->published=false; owner->write_cursor=0;
    owner->pending=(qa_q3_download){0};
}
static bool receive(qa_q3_client_downloads *owner,const qa_q3_download *download,qa_error *error)
{
    if(download->file_size<0) {
        if(!memchr(download->error,0,sizeof(download->error)))
            return fail(error,QA_ERROR_FORMAT,"Q3 download denial exceeds its decoded source string");
        return fail(error,QA_ERROR_FORMAT,download->error);
    }
    if(download->block!=owner->block) return true;
    if(!owner->active) { owner->stop_pending=true; return true; }
    if(download->size>sizeof(download->data) || owner->advertised_size<=0 ||
        owner->received>owner->advertised_size || owner->block==INT32_MAX)
        return fail(error,QA_ERROR_FORMAT,"Q3 package block exceeds its admitted source size or sequence");
    if(owner->block_pending) {
        return (download->block==owner->pending.block && download->file_size==owner->pending.file_size &&
            download->size==owner->pending.size && !memcmp(download->data,owner->pending.data,download->size)) ||
            fail(error,QA_ERROR_FORMAT,"Repeated Q3 block differs from its retained delivery");
    }
    if(download->size>(size_t)(owner->advertised_size-owner->received) ||
        (!download->size && owner->received!=owner->advertised_size))
        return fail(error,QA_ERROR_FORMAT,"Q3 package block exceeds or ends before its advertised size");
    owner->pending=*download; owner->block_pending=true; return true;
}
static bool progress(qa_q3_client_downloads *owner,bool *terminal,qa_error *error)
{
    *terminal=false;
    uint64_t generation=owner->generation;
    if(owner->stop_pending) {
        if(!owner->stop_queued) {
            if(!owner->bindings.reliable(owner->bindings.context,"stopdl",error) || !returned(owner,generation,error)) return false;
            owner->stop_queued=true;
        }
        if(!owner->bindings.send_packet(owner->bindings.context,error) || !returned(owner,generation,error)) return false;
        owner->stop_pending=owner->stop_queued=false;
    }
    if(owner->paused) return true;
    if(owner->request_pending) {
        char command[80]; snprintf(command,sizeof(command),"download %s",owner->requests[owner->next].remote);
        if(!owner->bindings.reliable(owner->bindings.context,command,error) || !returned(owner,generation,error)) return false;
        owner->request_pending=false;
    }
    if(owner->block_pending) {
        package_request *request=owner->requests+owner->next;
        if(!owner->stage) {
            qa_buffer mapped={0}; uint64_t initial=0;
            bool ok=destination(owner,request->local,&mapped,error);
            if(ok && !owner->stage_path) owner->stage_path=copy_string((const char *)mapped.data,error);
            if(ok) ok=owner->stage_path!=NULL;
            if(ok && !owner->stage_nonce)
                ok=owner->bindings.nonce(owner->bindings.context,&owner->stage_nonce,error) &&
                    returned(owner,generation,error) && owner->stage_nonce;
            if(ok) {
                if(!owner->native_stage_nonce) owner->native_stage_nonce=owner->stage_nonce;
                ok=qa_fs_stage_open_unique_checked(owner->bindings.root,(const char *)mapped.data,
                    &owner->native_stage_nonce,0,&owner->stage,&initial,error);
            }
            qa_buffer_free(&mapped);
            if(!ok) { qa_fs_stage_close(owner->stage,false); owner->stage=NULL; return false; }
            if(initial) { *terminal=true; return fail(error,QA_ERROR_FORMAT,"Q3 package requires its fresh actual private stage"); }
        }
        if(!owner->block_written) {
            size_t written=0;
            qa_bytes remaining={owner->pending.data+owner->write_cursor,owner->pending.size-owner->write_cursor};
            bool ok=!remaining.size || qa_fs_stage_write(owner->stage,
                (uint64_t)owner->received+owner->write_cursor,remaining,&written,error);
            owner->write_cursor+=written;
            if(!ok || owner->write_cursor!=owner->pending.size) return false;
            owner->received+=(int32_t)owner->pending.size; owner->block_written=true;
        }
        if(!owner->acknowledged) {
            char acknowledgement[40]; snprintf(acknowledgement,sizeof(acknowledgement),"nextdl %" PRId32,owner->block);
            if(!owner->bindings.reliable(owner->bindings.context,acknowledgement,error) || !returned(owner,generation,error)) return false;
            owner->acknowledged=true; ++owner->block;
            owner->bindings.progress(owner->bindings.context,request->local,owner->received,owner->size);
            if(!returned(owner,generation,error)) return false;
        }
        if(owner->pending.size) clear_block(owner);
        else {
            if(!owner->inspected) {
                if(!qa_fs_stage_seal(owner->stage,&owner->publication_identity,error)) return false;
                if(!inspect(owner,request,error)) { *terminal=error && error->code==QA_ERROR_FORMAT; return false; }
                owner->inspected=true;
            }
            bool allowed;
            if(!owner->bindings.permission(owner->bindings.context,&allowed,error) || !returned(owner,generation,error)) return false;
            if(!allowed) return qa_q3_client_downloads_cancel(owner,error);
            bool created=false;
            if(!qa_fs_stage_publish(owner->stage,&owner->publication_identity,true,&created,error)) {
                owner->published=created; return false;
            }
            if(!created) { *terminal=true; return fail(error,QA_ERROR_IO,"Q3 package destination was already published"); }
            owner->published=true;
            qa_fs_stage_close(owner->stage,false); owner->stage=NULL;
            owner->stage_nonce=owner->native_stage_nonce=0;
            free(owner->stage_path); owner->stage_path=NULL;
            clear_block(owner); owner->active=false; ++owner->next;
            owner->finish_pending=true; owner->finish_sent=0;
            owner->bindings.progress(owner->bindings.context,"",owner->received,owner->size);
            if(!returned(owner,generation,error)) return false;
        }
    }
    if(owner->finish_pending) {
        while(owner->finish_sent<2) {
            if(!owner->bindings.send_packet(owner->bindings.context,error) || !returned(owner,generation,error)) return false;
            ++owner->finish_sent;
        }
        owner->finish_pending=false; owner->finish_sent=0;
        bool started;
        if(!start_next(owner,&started,error)) return false;
        if(!started) owner->reload_pending=true;
    }
    if(owner->reload_pending) {
        if(!owner->reloaded) {
            if(!owner->bindings.reload_packages(owner->bindings.context,error) || !returned(owner,generation,error)) return false;
            owner->reloaded=true;
        }
        if(!owner->bindings.reliable(owner->bindings.context,"donedl",error) || !returned(owner,generation,error)) return false;
        owner->reload_pending=owner->reloaded=false;
    }
    return current(owner,error);
}
bool qa_q3_client_downloads_receive(qa_q3_client_downloads *owner,const qa_q3_download *download,qa_error *error)
{
    if(!current(owner,error) || !download || owner->receiving)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 download block lacks its idle current receiver");
    bool allowed;
    if(!owner->bindings.permission(owner->bindings.context,&allowed,error)) return false;
    if(!allowed) return qa_q3_client_downloads_cancel(owner,error);
    owner->receiving=true; bool ok=receive(owner,download,error); owner->receiving=false;
    if(!ok) qa_q3_client_downloads_close(owner);
    return ok;
}
bool qa_q3_client_downloads_pump(qa_q3_client_downloads *owner,qa_error *error)
{
    if(!current(owner,error) || owner->receiving)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 package continuation requires its current returned packet owner");
    if(owner->active && !owner->paused) {
        bool allowed;
        if(!owner->bindings.permission(owner->bindings.context,&allowed,error)) return false;
        if(!allowed && !qa_q3_client_downloads_cancel(owner,error)) return false;
    }
    uint64_t generation=owner->generation; bool terminal=false; qa_error operation={0};
    owner->receiving=true;
    bool ok=progress(owner,&terminal,&operation); owner->receiving=false;
    if((owner->generation!=generation && !(owner->paused && owner->stop_pending &&
        owner->generation==generation+1)) || !current(owner,error))
        return fail(error,QA_ERROR_ARGUMENT,"Q3 download retired during its actual continuation");
    if(!ok) {
        if(terminal) { if(error) *error=operation; qa_q3_client_downloads_close(owner); return false; }
        if(operation.code==QA_OK) fail(&operation,QA_ERROR_IO,"Q3 package operation retained an incomplete result");
        retained_failure(owner,&operation); return true;
    }
    owner->failure=(qa_error){0}; return true;
}
bool qa_q3_client_downloads_cancel(qa_q3_client_downloads *owner, qa_error *error)
{
    if (!current(owner, error)) return false;
    if (owner->paused) return true;
    qa_fs_stage_close(owner->stage, false); owner->stage = NULL; owner->stage_nonce = owner->native_stage_nonce = 0;
    free(owner->stage_path); owner->stage_path = NULL;
    owner->paused = true; owner->active = owner->reload_pending = false; owner->advertised_size = 0;
    clear_block(owner); owner->request_pending=owner->finish_pending=owner->reloaded=false;
    owner->finish_sent=0; owner->stop_pending=true; owner->stop_queued=false;
    if (owner->generation == UINT64_MAX) return fail(error, QA_ERROR_FORMAT, "Q3 download generation exhausted");
    ++owner->generation;
    return true;
}
bool qa_q3_client_downloads_retry(qa_q3_client_downloads *owner, bool *started, qa_error *error)
{
    if (!current(owner, error) || !started || owner->receiving) return false;
    *started = false; bool allowed;
    if (owner->stop_pending) return true;
    if (!owner->paused || !owner->bindings.permission(owner->bindings.context, &allowed, error)) return !owner->paused;
    if (!allowed) return true;
    owner->paused = false; qa_error operation={0};
    if (start_next(owner,started,&operation)) return true;
    if (owner->request_pending) { retained_failure(owner,&operation); return true; }
    if(error) *error=operation;
    return false;
}
static bool saved_text(qa_source_save_io *io, char **text, size_t maximum, bool optional)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present) || (!present && !optional)) return false;
    if (!present) return true;
    size_t length = io->direction == QA_SOURCE_SAVE_READ ? 0 : strlen(*text);
    if (!qa_source_save_count(io, &length, maximum) || !length) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *text = malloc(length + 1);
        if (!*text) return fail(io->error, QA_ERROR_MEMORY, "Restoring actual Q3 package request text");
        (*text)[length] = 0;
    }
    return qa_source_save_bytes(io, *text, length) && !memchr(*text, 0, length);
}
static bool valid(qa_q3_client_downloads *owner, bool staged, qa_error *error)
{
    if (!current(owner, error) || owner->receiving || !owner->generation || owner->generation == UINT64_MAX ||
        owner->count > QA_Q3_SEARCH_PATHS || owner->next > owner->count ||
        (owner->active && (owner->paused || owner->next == owner->count)) ||
        (!owner->active && !owner->paused && !owner->finish_pending && owner->next != owner->count) || owner->block < 0 || owner->received < 0 ||
        (owner->reload_pending && (owner->active || owner->paused || owner->next != owner->count || !owner->count)) ||
        (owner->stage_nonce!=0) != (owner->stage_path!=NULL) || (staged && !owner->stage_nonce) ||
        (staged && (!owner->active || owner->advertised_size <= 0 || owner->advertised_size != owner->size ||
            owner->received > owner->advertised_size || (!owner->block && !owner->block_pending))) ||
        (owner->active && !staged && (owner->block || owner->received || owner->write_cursor || owner->block_written)) ||
        (owner->request_pending && (!owner->active || owner->block_pending || staged || owner->block || owner->received)) ||
        (owner->acknowledged && !owner->block_written) || (owner->inspected && (!owner->acknowledged || owner->pending.size)) ||
        (owner->published && !owner->inspected) || (owner->stop_queued && !owner->stop_pending) ||
        (owner->reloaded && !owner->reload_pending) || owner->finish_sent>2 ||
        (owner->finish_pending && (owner->active || owner->paused || staged || !owner->next || owner->block_pending)) ||
        (!owner->finish_pending && owner->finish_sent) ||
        (owner->block_pending && (!owner->active || owner->paused || owner->request_pending ||
            owner->pending.file_size<0 || owner->advertised_size<=0 || owner->received>owner->advertised_size ||
            owner->pending.size>sizeof(owner->pending.data) || owner->write_cursor>owner->pending.size ||
            (!owner->block_written && owner->pending.size>(size_t)(owner->advertised_size-owner->received)) ||
            (owner->block_written && owner->pending.size>(size_t)owner->received) ||
            (!owner->pending.size && owner->received!=owner->advertised_size) || (owner->inspected && !staged) ||
            (owner->block_written && owner->write_cursor!=owner->pending.size) ||
            owner->block!=(int32_t)owner->pending.block+(owner->acknowledged?1:0))) ||
        (!owner->block_pending && (owner->pending.block || owner->pending.file_size || owner->pending.size ||
            owner->write_cursor || owner->block_written || owner->acknowledged || owner->inspected || owner->published)) ||
        (unsigned)owner->failure.code>QA_ERROR_NOT_FOUND || !memchr(owner->failure.message,0,sizeof(owner->failure.message)))
        return fail(error, QA_ERROR_FORMAT, "Q3 client continuation differs from its genuine queue and staged source cursors");
    for (size_t i = 0; i < owner->count; ++i) {
        package_request *request = owner->requests + i;
        if (!request->remote || !request->local || strlen(request->remote) >= 64 ||
            !qa_q3_download_name(request->remote, error) || !qa_q3_download_name(request->local, error) ||
            !owner->bindings.reference(owner->bindings.context, request->remote, request->checksum, error)) return false;
        char renamed[80]; size_t length = strlen(request->remote) - 4;
        snprintf(renamed, sizeof(renamed), "%.*s.%08" PRIx32 ".pk3", (int)length, request->remote, request->checksum);
        if (strcmp(request->local, request->remote) && strcmp(request->local, renamed))
            return fail(error, QA_ERROR_FORMAT, "Q3 local package name differs from its admitted source checksum recipe");
    }
    if (owner->stage_path) {
        qa_buffer mapped = {0};
        bool ok = destination(owner, owner->requests[owner->next].local, &mapped, error) &&
            !strcmp((const char *)mapped.data, owner->stage_path);
        qa_buffer_free(&mapped);
        if (!ok) return fail(error, QA_ERROR_FORMAT, "Q3 native stage belongs to another selected mount destination");
        if (owner->stage) {
            uint64_t size = 0;
            uint64_t expected=(uint64_t)owner->received+(owner->block_written?0:owner->write_cursor);
            if ((!owner->native_stage_nonce && !owner->published) || !qa_fs_stage_size(owner->stage, &size, error) || size != expected)
                return fail(error, QA_ERROR_FORMAT, "Q3 stage bytes differ from its real source receive cursor");
        }
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_q3_client_downloads *owner, bool *staged)
{
    uint32_t magic = UINT32_C(0x44433351);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x44433351) ||
        !qa_source_save_u64(io, &owner->generation) ||
        !qa_source_save_count(io, &owner->count, QA_Q3_SEARCH_PATHS) || !qa_source_save_count(io, &owner->next, owner->count) ||
        !qa_source_save_bool(io, &owner->active) || !qa_source_save_bool(io, &owner->paused) ||
        !qa_source_save_bool(io, &owner->reload_pending) ||
        !qa_source_save_i32(io, &owner->block) || !qa_source_save_i32(io, &owner->received) ||
        !qa_source_save_i32(io, &owner->size) || !qa_source_save_i32(io, &owner->advertised_size)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && owner->count) {
        owner->requests = calloc(owner->count, sizeof(*owner->requests));
        if (!owner->requests) return fail(io->error, QA_ERROR_MEMORY, "Restoring actual Q3 native package queue");
    }
    for (size_t i = 0; i < owner->count; ++i) {
        package_request *request = owner->requests + i;
        if (!saved_text(io, &request->remote, 63, false) || !saved_text(io, &request->local, 4095, false) ||
            !qa_source_save_u32(io, &request->checksum)) return false;
    }
    if (!qa_source_save_bool(io, staged) || !qa_source_save_u64(io, &owner->stage_nonce) ||
        !saved_text(io, &owner->stage_path, 4095, true)) return false;
    int32_t block=owner->pending.block; uint32_t status=(uint32_t)owner->failure.code;
    uint64_t offset=owner->failure.offset;
    if (!qa_source_save_bool(io,&owner->request_pending) || !qa_source_save_bool(io,&owner->block_pending) ||
        !qa_source_save_bool(io,&owner->block_written) || !qa_source_save_bool(io,&owner->acknowledged) ||
        !qa_source_save_bool(io,&owner->inspected) || !qa_source_save_bool(io,&owner->published) ||
        !qa_source_save_bool(io,&owner->finish_pending) || !qa_source_save_u8(io,&owner->finish_sent) ||
        !qa_source_save_bool(io,&owner->reloaded) || !qa_source_save_bool(io,&owner->stop_pending) ||
        !qa_source_save_bool(io,&owner->stop_queued) || !qa_source_save_count(io,&owner->write_cursor,sizeof(owner->pending.data)) ||
        !qa_source_save_i32(io,&block) || block<INT16_MIN || block>INT16_MAX ||
        !qa_source_save_i32(io,&owner->pending.file_size) ||
        !qa_source_save_count(io,&owner->pending.size,sizeof(owner->pending.data)) ||
        !qa_source_save_bytes(io,owner->pending.data,owner->pending.size) ||
        !qa_source_save_u32(io,&status) || status>QA_ERROR_NOT_FOUND || !qa_source_save_u64(io,&offset) || offset>SIZE_MAX ||
        !qa_source_save_bytes(io,owner->failure.message,sizeof(owner->failure.message))) return false;
    owner->pending.block=(int16_t)block; owner->failure.code=(qa_status)status; owner->failure.offset=(size_t)offset;
    return true;
}
bool qa_q3_client_downloads_checkpoint(const qa_q3_client_downloads *source, qa_buffer *out, qa_error *error)
{
    qa_q3_client_downloads *owner = (qa_q3_client_downloads *)source;
    if (!owner || !out || !valid(owner, owner->stage != NULL, error)) return false;
    qa_q3_client_downloads copy = *owner; bool staged = owner->stage != NULL;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, &copy, &staged);
    size_t length = staged ? (size_t)owner->received+(owner->block_written?0:owner->write_cursor) : 0;
    if (ok) ok = qa_source_save_count(&io, &length, INT32_MAX);
    uint8_t buffer[65536]; size_t offset = 0;
    while (ok && offset < length) {
        size_t span = length - offset; if (span > sizeof(buffer)) span = sizeof(buffer); size_t read = 0;
        ok = qa_fs_stage_read(owner->stage, offset, buffer, span, &read, error) && read == span &&
            qa_source_save_bytes(&io, buffer, span);
        if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "Q3 staged prefix changed during actual capture");
        offset += span;
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_client_downloads_restore(qa_bytes bytes, const qa_q3_client_download_bindings *bindings,
    qa_q3_client_downloads **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 client download restore requires an empty candidate");
    qa_q3_client_downloads *owner = NULL;
    if (!qa_q3_client_downloads_create(bindings, &owner, error)) return false;
    qa_source_save_io io = {0}; bool staged = false; size_t length = 0; qa_bytes prefix = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, owner, &staged) &&
        qa_source_save_count(&io, &length, INT32_MAX);
    if (ok) {
        ok = length <= io.input.size - io.offset;
        if (ok) { prefix = (qa_bytes){io.input.data + io.offset, length}; io.offset += length; }
        else fail(error, QA_ERROR_FORMAT, "Truncated actual Q3 staged prefix");
    }
    if (ok) ok = qa_source_save_finish(&io, NULL) &&
        length == (staged ? (size_t)owner->received+(owner->block_written?0:owner->write_cursor) : 0) && valid(owner, staged, error);
    qa_source_save_dispose(&io);
    if (ok && staged) {
        if(owner->published) ok=qa_fs_stage_open_published_checked(owner->bindings.root,owner->stage_path,prefix,
            &owner->stage,&owner->publication_identity,error);
        else ok = owner->bindings.prepare_stage(owner->bindings.context, owner->stage_path, owner->stage_nonce,
            prefix, &owner->stage, &owner->native_stage_nonce, error);
        ok=ok && owner->stage && (owner->published || owner->native_stage_nonce) && valid(owner,true,error);
        size_t written = 0;
        if (ok && !owner->published) ok = qa_fs_stage_write(owner->stage, prefix.size, (qa_bytes){0}, &written, error) && !written;
        uint8_t buffer[65536]; size_t offset = 0;
        while (ok && offset < prefix.size) {
            size_t span = prefix.size - offset; if (span > sizeof(buffer)) span = sizeof(buffer); size_t read = 0;
            ok = qa_fs_stage_read(owner->stage, offset, buffer, span, &read, error) && read == span && !memcmp(buffer, prefix.data + offset, span);
            if (!ok && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "Q3 candidate stage differs from its original source prefix");
            offset += span;
        }
        if(ok && owner->inspected && !owner->published) ok=qa_fs_stage_seal(owner->stage,&owner->publication_identity,error);
    }
    if (!ok) { qa_q3_client_downloads_destroy(owner); return false; }
    *out = owner; return true;
}
bool qa_q3_client_downloads_handoff_ready(const qa_q3_client_downloads *active,
    const qa_q3_client_downloads *candidate, qa_error *error)
{
    qa_buffer before = {0}, after = {0};
    bool ok = active && candidate && qa_q3_client_downloads_checkpoint(active, &before, error) &&
        qa_q3_client_downloads_checkpoint(candidate, &after, error);
    if (ok && (before.size != after.size || memcmp(before.data, after.data, before.size)))
        ok = fail(error, QA_ERROR_UNSUPPORTED, "Live Q3 receiver advanced beyond its genuine saved queue and prefix cut");
    qa_buffer_free(&before); qa_buffer_free(&after); return ok;
}
void qa_q3_client_downloads_handoff_publish(qa_q3_client_downloads *active, qa_q3_client_downloads *candidate)
{
    qa_fs_stage *stage = active->stage; active->stage = candidate->stage; candidate->stage = stage;
    uint64_t nonce = active->native_stage_nonce; active->native_stage_nonce = candidate->native_stage_nonce; candidate->native_stage_nonce = nonce;
    qa_fs_identity identity=active->publication_identity;
    active->publication_identity=candidate->publication_identity; candidate->publication_identity=identity;
}
