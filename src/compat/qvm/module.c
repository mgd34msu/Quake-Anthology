#include "internal.h"
#include "qa/qvm_save.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

#define CHECKPOINT_HEADER 156u

bool qa_qvm_checkpoint_host(qa_bytes state, qa_bytes *out, qa_error *error)
{
    if (!out)
        return qa_qvm_error(error, QA_ERROR_ARGUMENT, 0, "missing QVM host checkpoint view");
    if (!state.data || state.size < CHECKPOINT_HEADER || memcmp(state.data, "QAVM", 4))
        return qa_qvm_error(error, QA_ERROR_FORMAT, 0, "QVM checkpoint envelope mismatch");
    size_t payload = state.size - CHECKPOINT_HEADER;
    uint64_t host = qa_load_u64le(state.data + 28);
    if (host > payload)
        return qa_qvm_error(error, QA_ERROR_FORMAT, 0, "QVM checkpoint extent mismatch");
    *out = (qa_bytes){state.data + state.size - (size_t)host, (size_t)host};
    return true;
}

static bool safe_point(const qa_qvm *vm, qa_error *error)
{
    if (!qa_qvm_live(vm,error) || vm->publication_depth) return false;
    return qa_qvm_can_destroy(vm)
        || qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"QVM operation requires a completed source call and write delivery");
}

bool qa_qvm_can_destroy(const qa_qvm *vm)
{
    return vm && !vm->retired && !vm->publication_depth &&
        !qa_qvm_execution_active(vm) && !vm->write_delivery_depth && !vm->lifecycle_depth;
}

bool qa_qvm_create(qa_qvm_image *image, const qa_qvm_options *options, qa_qvm **out, qa_error *error)
{
    if (image == NULL || options == NULL || out == NULL
        || (unsigned)options->role > QA_QVM_UI || (unsigned)options->abi > QA_QVM_Q3_116N
        || (unsigned)options->semantics > QA_QVM_COMPILED_SEMANTICS
        || (options->checkpoint == NULL) != (options->restore == NULL))
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM instance options");
    qa_qvm *vm = calloc(1,sizeof(*vm));
    if (vm == NULL) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating QVM instance");
    vm->data = calloc(image->memory_size,1);
    if (vm->data == NULL) { free(vm); return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating private QVM memory"); }
    vm->image = image;
    vm->options = *options;
    vm->api_version = options->role == QA_QVM_GAME ? (options->abi == QA_QVM_Q3_MODERN ? 8u : 7u)
        : options->role == QA_QVM_CGAME ? (options->abi == QA_QVM_Q3_MODERN ? 4u : 3u)
        : options->abi == QA_QVM_Q3_MODERN ? 6u : 4u;
    vm->data_size = image->memory_size;
    vm->data_mask = (uint32_t)image->memory_size - 1;
    vm->next_watch = 1;
    if (image->initialized.size > 0) memcpy(vm->data,image->initialized.data,image->initialized.size);
    if (!qa_qvm_execution_create(vm,error)) { free(vm->data); free(vm); return false; }
    qa_qvm_image_retain(image);
    *out = vm;
    return true;
}

bool qa_qvm_destroy(qa_qvm *vm, qa_error *error)
{
    if (vm == NULL) return true;
    if (!safe_point(vm,error)) return false;
    vm->retired = true;
    qa_qvm_memory_close(vm);
    qa_qvm_execution_destroy(vm);
    qa_qvm_image_release(vm->image);
    free(vm->data); free(vm);
    return true;
}

bool qa_qvm_restart(qa_qvm *vm, qa_bytes replacement, qa_error *error)
{
    if (!safe_point(vm,error) || vm->candidate_inventory) return false;
    qa_buffer initialized = {0};
    size_t allocation;
    if (!qa_qvm_parse_restart(replacement,&initialized,&allocation,error)) return false;
    if (allocation > vm->data_size) {
        qa_buffer_free(&initialized);
        return qa_qvm_error(error,QA_ERROR_FORMAT,28,"QVM restart exceeds original allocation");
    }
    qa_qvm_memory_close(vm);
    qa_qvm_execution_reset(vm);
    /* The original restart clears only the replacement allocation. Older
     * bytes above that allocation remain addressable through the original mask. */
    memset(vm->data,0,allocation);
    if (initialized.size > 0) memcpy(vm->data,initialized.data,initialized.size);
    qa_buffer_free(&initialized);
    return true;
}

bool qa_qvm_restart_original(qa_qvm *vm, qa_error *error)
{
    if (!safe_point(vm, error) || vm->candidate_inventory) return false;
    qa_qvm_memory_close(vm);
    qa_qvm_execution_reset(vm);
    memset(vm->data, 0, vm->image->memory_size);
    if (vm->image->initialized.size)
        memcpy(vm->data, vm->image->initialized.data, vm->image->initialized.size);
    return true;
}

bool qa_qvm_active(const qa_qvm *vm) { return vm != NULL && qa_qvm_execution_active(vm); }
qa_qvm_role qa_qvm_get_role(const qa_qvm *vm) { return vm == NULL ? QA_QVM_GAME : vm->options.role; }
qa_qvm_abi qa_qvm_get_abi(const qa_qvm *vm) { return vm == NULL ? QA_QVM_Q3_MODERN : vm->options.abi; }
uint32_t qa_qvm_api_version(const qa_qvm *vm) { return vm == NULL ? 0 : vm->api_version; }
const qa_sha256_digest *qa_qvm_digest(const qa_qvm *vm) { return vm == NULL ? NULL : &vm->image->digest; }

bool qa_qvm_call_argument(const qa_qvm_call *call, size_t index, int32_t *out, qa_error *error)
{
    if (out == NULL || call == NULL || index >= call->argument_count)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,index,"QVM argument index is outside call");
    if (!qa_qvm_execution_token(call,error)) return false;
    if (index > (UINT32_MAX - call->argument_base) / 4u)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,index,"QVM argument address overflow");
    uint32_t offset = call->argument_base + (uint32_t)index * 4u;
    if (!qa_qvm_raw_range(call->vm,offset,4,error)) return false;
    *out = qa_load_i32le(call->vm->data + offset);
    return true;
}

bool qa_qvm_call_set_argument(const qa_qvm_call *call, size_t index, int32_t value, qa_error *error)
{
    int32_t previous;
    if (!qa_qvm_call_argument(call,index,&previous,error)) return false;
    uint8_t bytes[4];
    qa_store_u32le(bytes,(uint32_t)value);
    return qa_qvm_write(call->vm,call->argument_base + (uint32_t)index * 4u,(qa_bytes){bytes,4},error);
}

static void checkpoint_digest(qa_bytes bytes, qa_sha256_digest *out)
{
    qa_sha256_context hash;
    qa_sha256_init(&hash);
    qa_sha256_update(&hash,(qa_bytes){bytes.data,76});
    qa_sha256_update(&hash,(qa_bytes){bytes.data + 108,bytes.size - 108});
    qa_sha256_final(&hash,out);
}

bool qa_qvm_checkpoint(qa_qvm *vm, qa_buffer *out, qa_error *error)
{
    if (out == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM checkpoint output");
    if (!safe_point(vm,error) || vm->candidate_inventory) return false;
    if (vm->options.checkpoint == NULL)
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,0,"QVM host has not bound checkpoint services");
    uint64_t execution[3];
    qa_qvm_execution_checkpoint(vm,execution);
    if (!qa_qvm_execution_checkpoint_ready(vm,execution,false,error)) return false;
    qa_buffer host = {0};
    ++vm->lifecycle_depth;
    bool captured = vm->options.checkpoint(vm->options.context,&host,error);
    --vm->lifecycle_depth;
    if (!captured) { qa_buffer_free(&host); return false; }
    if ((host.size > 0 && host.data == NULL) || host.size > SIZE_MAX - CHECKPOINT_HEADER) {
        qa_buffer_free(&host);
        return qa_qvm_error(error,QA_ERROR_FORMAT,0,"invalid QVM host checkpoint");
    }
    uint8_t header[CHECKPOINT_HEADER] = {0};
    memcpy(header,"QAVM",4);
    qa_store_u32le(header + 4,(uint32_t)vm->options.role);
    qa_store_u32le(header + 8,(uint32_t)vm->options.abi);
    qa_store_u32le(header + 12,(uint32_t)vm->options.semantics);
    qa_store_u32le(header + 16,vm->api_version);
    qa_store_u64le(header + 20,(uint64_t)vm->data_size);
    qa_store_u64le(header + 28,(uint64_t)host.size);
    qa_store_u64le(header + 36,vm->write_sequence);
    memcpy(header + 44,vm->image->digest.bytes,32);
    qa_store_u64le(header + 108,vm->options.instruction_limit);
    qa_store_u32le(header + 116,vm->options.debug);
    qa_store_u64le(header + 124,vm->next_watch);
    qa_store_u64le(header + 132,execution[0]);
    qa_store_u64le(header + 140,execution[1]);
    qa_store_u64le(header + 148,execution[2]);
    qa_source_save_io io;
    qa_buffer state = {0};
    if (!qa_source_save_writer(&io, NULL, error)) { qa_buffer_free(&host); return false; }
    bool encoded = qa_source_save_bytes(&io, header, sizeof(header)) &&
        qa_source_save_memory_delta(&io, vm->data, vm->data_size,
            (qa_bytes){vm->image->initialized.data, vm->image->initialized.size}) &&
        qa_source_save_bytes(&io, host.data, host.size) && qa_source_save_finish(&io, &state);
    qa_source_save_dispose(&io);
    qa_buffer_free(&host);
    if (!encoded) return false;
    qa_sha256_digest digest;
    checkpoint_digest((qa_bytes){state.data,state.size},&digest);
    memcpy(state.data + 76,digest.bytes,32);
    *out = state;
    return true;
}

typedef struct saved_execution {
    uint32_t api;
    uint64_t watch, counters[3];
    qa_bytes memory, host;
} saved_execution;

static bool restore_envelope(qa_qvm *vm, qa_bytes state, bool candidate,
    saved_execution *out, qa_error *error)
{
    if (vm->options.restore == NULL)
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,0,"QVM host has not bound restore services");
    qa_bytes host;
    if (!qa_qvm_checkpoint_host(state, &host, error)) return false;
    if (qa_load_u32le(state.data + 4) != (uint32_t)vm->options.role
        || qa_load_u32le(state.data + 8) != (uint32_t)vm->options.abi
        || qa_load_u32le(state.data + 12) != (uint32_t)vm->options.semantics
        || qa_load_u64le(state.data + 20) != vm->data_size
        || memcmp(state.data + 44,vm->image->digest.bytes,32) != 0
        || qa_load_u64le(state.data + 108) != vm->options.instruction_limit
        || qa_load_u32le(state.data + 116) != (uint32_t)vm->options.debug
        || qa_load_u32le(state.data + 120))
        return qa_qvm_error(error,QA_ERROR_FORMAT,0,"QVM checkpoint artifact, profile, or envelope mismatch");
    qa_sha256_digest digest;
    checkpoint_digest(state,&digest);
    if (memcmp(digest.bytes,state.data + 76,32) != 0)
        return qa_qvm_error(error,QA_ERROR_FORMAT,76,"QVM checkpoint checksum mismatch");
    qa_bytes memory = {state.data + CHECKPOINT_HEADER, state.size - CHECKPOINT_HEADER - host.size};
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, memory, error) ||
        !qa_source_save_memory_delta(&io, NULL, vm->data_size,
            (qa_bytes){vm->image->initialized.data, vm->image->initialized.size}) ||
        !qa_source_save_finish(&io, NULL)) return false;
    uint32_t api = qa_load_u32le(state.data + 16);
    uint64_t saved_watch = qa_load_u64le(state.data + 124);
    uint64_t execution[3] = {qa_load_u64le(state.data + 132),
        qa_load_u64le(state.data + 140),qa_load_u64le(state.data + 148)};
    bool api_matches = vm->options.role == QA_QVM_UI
        ? api == 4 || (vm->options.abi == QA_QVM_Q3_MODERN && api == 6)
        : api == vm->api_version;
    if (!api_matches || !saved_watch ||
        (candidate && (vm->write_sequence || saved_watch < vm->next_watch)))
        return qa_qvm_error(error,QA_ERROR_FORMAT,16,"QVM source API or candidate execution generation differs");
    if (!qa_qvm_execution_checkpoint_ready(vm,execution,candidate,error)) return false;
    *out = (saved_execution){api, saved_watch, {execution[0], execution[1], execution[2]}, memory, host};
    return true;
}

bool qa_qvm_restore_candidate_bindings(qa_qvm *vm, qa_bytes state,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, qa_error *error)
{
    saved_execution source;
    if (!safe_point(vm, error) || vm->candidate_inventory || !restore_envelope(vm, state, true, &source, error)) return false;
    return qa_qvm_execution_restore_bindings(vm, source.counters[0], constructed, saved, count, error);
}
bool qa_qvm_restore_candidate_callbacks(qa_qvm *vm, qa_bytes state,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, const qa_qvm_saved_resolver *resolver, qa_qvm_binding saved_resolver,
    qa_error *error)
{
    saved_execution source;
    if (!safe_point(vm, error) || vm->candidate_inventory || !restore_envelope(vm, state, true, &source, error)) return false;
    return qa_qvm_execution_restore_callbacks(vm, source.counters[0], constructed, saved,
        count, resolver, saved_resolver, error);
}
bool qa_qvm_restore_candidate_inventory(qa_qvm *vm, qa_bytes state,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved, size_t count,
    const qa_qvm_saved_resolver *resolver, qa_qvm_binding saved_resolver,
    const qa_qvm_saved_write_watch *watches, const qa_qvm_binding *saved_watches,
    size_t watch_count, qa_error *error)
{
    saved_execution source;
    if (!safe_point(vm,error) || vm->candidate_inventory ||
        !restore_envelope(vm,state,true,&source,error) ||
        !qa_qvm_memory_restore_watches_ready(vm,source.watch,watches,saved_watches,watch_count,error) ||
        !qa_qvm_execution_restore_inventory(vm,source.counters[0],constructed,saved,
            count,resolver,saved_resolver,error)) return false;
    qa_qvm_memory_restore_watches(vm,source.watch,watches,saved_watches,watch_count);
    memcpy(vm->candidate_inventory_digest.bytes,state.data+76,32);
    vm->candidate_inventory=true;
    return true;
}

static bool restore(qa_qvm *vm, qa_bytes state, bool candidate, qa_error *error)
{
    saved_execution source;
    if (!safe_point(vm, error) || !restore_envelope(vm, state, candidate, &source, error)) return false;
    /* Host restoration sees the restored guest RAM, as in the donor. A host
     * restore error leaves the committed RAM visible and must be reported. */
    if (vm->candidate_inventory && (vm->candidate_inventory_invalid || !candidate ||
        memcmp(vm->candidate_inventory_digest.bytes,state.data+76,32)))
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"Candidate inventory belongs to another saved executor");
    if (!vm->candidate_inventory) qa_qvm_memory_close(vm);
    vm->candidate_inventory=false;
    qa_qvm_execution_reset(vm);
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, NULL, source.memory, error) ||
        !qa_source_save_memory_delta(&io, vm->data, vm->data_size,
            (qa_bytes){vm->image->initialized.data, vm->image->initialized.size}) ||
        !qa_source_save_finish(&io, NULL)) return false;
    uint64_t saved_sequence = qa_load_u64le(state.data + 36);
    if (candidate || saved_sequence > vm->write_sequence) vm->write_sequence = saved_sequence;
    if (candidate || source.watch > vm->next_watch) vm->next_watch = source.watch;
    vm->api_version = source.api;
    qa_qvm_execution_restore(vm,source.counters,candidate);
    qa_bytes host = source.host;
    ++vm->lifecycle_depth;
    bool restored = vm->options.restore(vm->options.context,host,error);
    --vm->lifecycle_depth;
    return restored;
}

bool qa_qvm_restore(qa_qvm *vm, qa_bytes state, qa_error *error)
{ return restore(vm,state,false,error); }

bool qa_qvm_restore_candidate(qa_qvm *vm, qa_bytes state, qa_error *error)
{ return restore(vm,state,true,error); }

const char *qa_qvm_role_name(qa_qvm_role role)
{
    switch (role) {
    case QA_QVM_GAME: return "qagame";
    case QA_QVM_CGAME: return "cgame";
    case QA_QVM_UI: return "ui";
    }
    return "invalid";
}

bool qa_qvm_validate_ui(qa_qvm *vm, int32_t *api_version, qa_error *error)
{
    if (!qa_qvm_live(vm,error)) return false;
    if (vm->options.role != QA_QVM_UI) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"QVM module is not a UI");
    int32_t command = 0, version;
    if (!qa_qvm_invoke(vm,0,&command,1,&version,error)) return false;
    if (version != 4 && !(vm->options.abi == QA_QVM_Q3_MODERN && version == 6))
        return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,0,"QVM UI returned an unsupported API version");
    vm->api_version = (uint32_t)version;
    if (api_version != NULL) *api_version = version;
    return true;
}
