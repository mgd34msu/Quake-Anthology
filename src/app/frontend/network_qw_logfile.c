#include "network_qw_logfile.h"
#include "qa/source_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_qw_logfile {
    qa_vfs *files;
    qa_mount_id mount;
    qa_fs_stream *stream;
    qa_fs_stream_reference saved;
    char *saved_path;
    uint64_t position;
    bool restoring, sync_pending, busy;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static bool writable(qa_settings_store store)
{
    if (!store.vfs || !qa_vfs_mount_root(store.vfs, store.mount)) return false;
    for (size_t i = 0; i < qa_vfs_mount_count(store.vfs); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(store.vfs, i, &mount) && mount.id == store.mount)
            return mount.writable && !mount.is_archive;
    }
    return false;
}
bool frontend_qw_logfile_enabled(const frontend_qw_logfile *owner)
{ return owner && !owner->restoring && !owner->busy && owner->stream; }
bool frontend_qw_logfile_close(frontend_qw_logfile **slot, qa_error *error)
{
    if (!slot) return fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld frag file owner");
    frontend_qw_logfile *owner = *slot;
    if (!owner) return true;
    if (owner->busy) return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag file is entered");
    if (owner->stream && owner->sync_pending && !qa_fs_stream_sync(owner->stream, error)) return false;
    qa_fs_stream *stream = owner->stream; owner->stream = NULL;
    bool okay = qa_fs_stream_close_checked(stream, error);
    qa_vfs_destroy(owner->files); free(owner->saved_path); free(owner); *slot = NULL;
    return okay;
}
static char *file_path(const frontend_qw_logfile *owner,const char *name,qa_error *error)
{
    const char *prefix=qa_vfs_mount_root_prefix(owner->files,owner->mount);
    if (!prefix) return fail(error,QA_ERROR_FORMAT,"QuakeWorld frag file lost its retained mount prefix"),NULL;
    size_t a=strlen(prefix),b=strlen(name);
    if (a>SIZE_MAX-b-2) return fail(error,QA_ERROR_MEMORY,"QuakeWorld frag path overflow"),NULL;
    char *path=malloc(a+b+2);
    if (!path) return fail(error,QA_ERROR_MEMORY,"Retaining QuakeWorld frag path"),NULL;
    memcpy(path,prefix,a); if (a) path[a++]='/'; memcpy(path+a,name,b+1); return path;
}
bool frontend_qw_logfile_toggle(qa_settings_store store, qa_console *console,
    const qa_command_context *command, frontend_qw_logfile **slot, qa_error *error)
{
    if (!slot || !console || !command || command->dialect != QA_CONSOLE_QW)
        return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag file requires its actual Source command");
    if (*slot) {
        if (!frontend_qw_logfile_close(slot, error)) return false;
        qa_console_emit(console, command, "Frag file logging off.\n"); return true;
    }
    if (!writable(store)) return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag file lost its product write authority");
    frontend_qw_logfile *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld frag file");
    owner->files = qa_vfs_clone(store.vfs, error); owner->mount = store.mount;
    if (!owner->files) { free(owner); return false; }
    qa_fs_root *root = qa_vfs_mount_root(owner->files, owner->mount);
    if (!qa_fs_root_same_object(root, qa_vfs_mount_root(store.vfs, store.mount))) {
        frontend_qw_logfile_close(&owner, NULL);
        return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag file changed its retained product root");
    }
    for (unsigned i = 0; i < 1000; ++i) {
        char name[32]; snprintf(name, sizeof(name), "frag_%u.log", i);
        qa_fs_entry_kind kind; qa_error attempt = {0};
        char *relative=file_path(owner,name,&attempt);
        if (!relative) break;
        bool found=qa_fs_root_status(root,relative,&kind,NULL,&attempt);
        if (!found) { free(relative); break; }
        if (kind!=QA_FS_MISSING) { free(relative); continue; }
        uint64_t size = 0;
        bool opened=qa_fs_root_stream_open(root,relative,QA_FS_STREAM_WRITE,false,&owner->stream,&size,&attempt);
        if (!opened) { free(relative); break; }
        if (size) { free(relative); frontend_qw_logfile_close(&owner, NULL); return fail(error, QA_ERROR_IO, "Fresh QuakeWorld frag file was not truncated"); }
        *slot = owner;
        char *path = NULL;
        bool joined=qa_fs_root_join(root,relative,&path,error); free(relative);
        if (!joined) return false;
        qa_console_emit(console, command, "Logging frags to ");
        qa_console_emit(console, command, path); qa_console_emit(console, command, ".\n");
        free(path); return true;
    }
    frontend_qw_logfile_close(&owner, NULL);
    qa_console_emit(console, command, "Can't open any logfiles.\n"); return true;
}
void frontend_qw_logfile_write(frontend_qw_logfile *owner, const char *record)
{
    if (!frontend_qw_logfile_enabled(owner) || !record) return;
    owner->busy = true;
    size_t size = strlen(record), written = 0; qa_error ignored = {0};
    /* The donor ignores fprintf/fflush failure. Keep actual accepted progress
     * and never replay the already appended Source obituary. */
    (void)qa_fs_stream_write_some(owner->stream, (qa_bytes){(const uint8_t *)record, size},
        owner->position, &written, &ignored);
    owner->position += written;
    owner->sync_pending = true;
    if (qa_fs_stream_sync(owner->stream, &ignored)) owner->sync_pending = false;
    owner->busy = false;
}
bool frontend_qw_logfile_visit(const frontend_qw_logfile *owner,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    return !owner || ((!owner->busy && !owner->restoring && owner->stream && visitor &&
        visitor->pool && visitor->view) &&
        visitor->pool(visitor->context, qa_vfs_resources(owner->files), error) &&
        visitor->view(visitor->context, owner->files, error));
}
static bool object_fields(qa_source_save_io *io, qa_fs_object_reference *object)
{
    if (!qa_source_save_u32(io, &object->platform)) return false;
    for (size_t i = 0; i < 3; ++i) if (!qa_source_save_u64(io, object->words + i)) return false;
    return true;
}
static bool fields(qa_source_save_io *io, frontend_qw_logfile *owner, uint64_t *view)
{
    uint8_t magic[4] = {'Q','W','L','F'}; uint32_t version = 2, mode = owner->saved.mode;
    size_t size = owner->saved_path ? strlen(owner->saved_path) : 0;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QWLF", 4) ||
        !qa_source_save_u32(io, &version) || (version != 1 && version != 2) || !qa_source_save_u64(io, view) || !*view ||
        !qa_source_save_u64(io, &owner->mount) || !owner->mount ||
        !qa_source_save_u64(io, &owner->position) || owner->position > INT64_MAX ||
        !qa_source_save_bool(io, &owner->sync_pending) ||
        !object_fields(io, &owner->saved.root) || !object_fields(io, &owner->saved.object) ||
        !qa_source_save_u32(io, &mode) || mode != QA_FS_STREAM_WRITE ||
        !qa_source_save_count(io, &size, version==1?12:
            io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1) || size < 10) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        owner->saved_path = calloc(size + 1, 1);
        if (!owner->saved_path) return fail(io->error, QA_ERROR_MEMORY, "Restoring QuakeWorld frag filename");
    }
    if (!qa_source_save_bytes(io, owner->saved_path, size) || memchr(owner->saved_path, 0, size)) return false;
    owner->saved.path = owner->saved_path; owner->saved.mode = (qa_fs_stream_mode)mode;
    const char *leaf=strrchr(owner->saved_path,'/'); leaf=leaf?leaf+1:owner->saved_path;
    if (version==1 && leaf!=owner->saved_path) return false;
    unsigned number = 1000; char extra = 0;
    if (sscanf(leaf, "frag_%u.log%c", &number, &extra) != 1 || number >= 1000) return false;
    char canonical[32]; snprintf(canonical, sizeof(canonical), "frag_%u.log", number);
    return !strcmp(canonical, leaf) && qa_fs_stream_reference_valid(&owner->saved, io->error);
}
bool frontend_qw_logfile_checkpoint(const frontend_qw_logfile *owner,
    const qa_application_content_graph *graph, qa_buffer *out, qa_error *error)
{
    if (!frontend_qw_logfile_enabled(owner) || !graph || !out)
        return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag capture requires its returned writer");
    frontend_qw_logfile copy = *owner;
    if (!qa_fs_stream_reference_read(owner->stream, &copy.saved)) return false;
    copy.saved_path = (char *)copy.saved.path;
    uint64_t view = qa_application_content_view_id(graph, owner->files);
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && fields(&io, &copy, &view) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
static bool mapped_root(void *context, const qa_fs_stream_reference *reference,
    qa_fs_root **out, qa_error *error)
{
    frontend_qw_logfile *owner = context;
    const qa_fs_object_reference *roots = NULL; size_t count = 0;
    if (!writable((qa_settings_store){owner->files, owner->mount}) ||
        !qa_vfs_mount_root_references(owner->files, owner->mount, &roots, &count))
        return fail(error, QA_ERROR_FORMAT, "QuakeWorld frag file lacks its retained writable root");
    bool found = false;
    for (size_t i = 0; i < count; ++i) if (roots[i].platform == reference->root.platform &&
        !memcmp(roots[i].words, reference->root.words, sizeof(reference->root.words))) found = true;
    if (!found) return fail(error, QA_ERROR_FORMAT, "QuakeWorld frag file root has no actual saved lineage");
    const char *leaf=strrchr(reference->path,'/'); leaf=leaf?leaf+1:reference->path;
    char *expected=file_path(owner,leaf,error);
    bool current=expected && !strcmp(expected,reference->path); free(expected);
    if (!current) return fail(error,QA_ERROR_FORMAT,"QuakeWorld frag file changed its actual held child prefix");
    *out = qa_vfs_mount_root(owner->files, owner->mount); qa_fs_root_retain(*out); return true;
}
bool frontend_qw_logfile_restore(qa_application_content_graph *graph, qa_bytes bytes,
    frontend_qw_logfile **out, qa_error *error)
{
    if (!graph || !out || *out) return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag import requires its isolated owner");
    frontend_qw_logfile *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Decoding QuakeWorld frag file");
    owner->restoring = true;
    qa_source_save_io io = {0}; uint64_t view = 0;
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, owner, &view) &&
        qa_source_save_finish(&io, NULL) && qa_application_content_claim_view(graph, view, &owner->files, error);
    qa_source_save_dispose(&io);
    qa_fs_root *root = NULL;
    if (okay) okay = mapped_root(owner, &owner->saved, &root, error);
    qa_fs_root_close(root);
    if (!okay) { frontend_qw_logfile_close(&owner, NULL); return false; }
    *out = owner; return true;
}
bool frontend_qw_logfile_finish_restore(frontend_qw_logfile *owner, qa_error *error)
{
    if (!owner || owner->busy) return fail(error, QA_ERROR_ARGUMENT, "QuakeWorld frag import is not returned");
    if (!owner->restoring) return frontend_qw_logfile_enabled(owner);
    qa_fs_root *root = qa_vfs_mount_root(owner->files, owner->mount); uint64_t size = 0;
    qa_fs_stream_resolver resolver = {owner, mapped_root};
    if (!qa_fs_stream_resume_mapped(root, &owner->saved, &resolver, &owner->stream, &size, error)) return false;
    owner->restoring = false; return true;
}
