#include "internal.h"
#include "guest/internal.h"
#include "qa/source_save.h"

typedef struct native_signature_buffer {
    uint8_t *data;
    size_t size, capacity;
} native_signature_buffer;
typedef struct native_signature_reader {
    qa_bytes bytes;
    size_t offset;
} native_signature_reader;
#define NATIVE_SIGNATURE_MAX_TYPES 1024u

static bool signature_grow(native_signature_buffer *buffer, size_t added, qa_error *error) {
    size_t required;
    if (!native_size_add(buffer->size, added, &required))
        return native_fail(error, QA_ERROR_MEMORY, 0, "native observer message size overflows");
    if (required <= buffer->capacity)
        return true;
    size_t capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < required) {
        size_t next = capacity <= SIZE_MAX / 2u ? capacity * 2u : required;
        if (next < capacity)
            return native_fail(error, QA_ERROR_MEMORY, 0,
                               "native observer message capacity overflows");
        capacity = next;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (!grown)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native observer message");
    buffer->data = grown;
    buffer->capacity = capacity;
    return true;
}

static void native_signature_buffer_free(native_signature_buffer *buffer) {
    if (!buffer)
        return;
    free(buffer->data);
    *buffer = (native_signature_buffer){0};
}

static bool native_signature_put_raw(native_signature_buffer *buffer, const void *bytes, size_t size,
                         qa_error *error) {
    if ((!bytes && size) || !signature_grow(buffer, size, error))
        return false;
    if (size)
        memcpy(buffer->data + buffer->size, bytes, size);
    buffer->size += size;
    return true;
}

static bool native_signature_put_u8(native_signature_buffer *buffer, uint8_t value, qa_error *error) {
    return native_signature_put_raw(buffer, &value, 1, error);
}

static bool native_signature_put_u32(native_signature_buffer *buffer, uint32_t value, qa_error *error) {
    uint8_t bytes[4];
    qa_store_u32le(bytes, value);
    return native_signature_put_raw(buffer, bytes, sizeof(bytes), error);
}

static bool native_signature_put_u64(native_signature_buffer *buffer, uint64_t value, qa_error *error) {
    uint8_t bytes[8];
    qa_store_u64le(bytes, value);
    return native_signature_put_raw(buffer, bytes, sizeof(bytes), error);
}

static bool signature_put_type(native_signature_buffer *buffer, const qa_native_type *type, unsigned depth,
                          size_t *count, qa_error *error) {
    if (!type || !type->count || type->kind > QA_NATIVE_BYTES || depth > 16u ||
        ++*count > NATIVE_SIGNATURE_MAX_TYPES)
        return native_fail(error, QA_ERROR_ARGUMENT, depth, "invalid native observer ABI type tree");
    if ((type->kind == QA_NATIVE_BYTES) != (type->field_count != 0) ||
        (type->field_count && !type->fields))
        return native_fail(error, QA_ERROR_ARGUMENT, depth,
                           "native observer aggregate fields are invalid");
    if (!native_signature_put_u32(buffer, (uint32_t)type->kind, error) ||
        !native_signature_put_u64(buffer, type->count, error) ||
        !native_signature_put_u64(buffer, type->field_count, error))
        return false;
    for (size_t index = 0; index < type->field_count; ++index)
        if (!signature_put_type(buffer, &type->fields[index], depth + 1u, count, error))
            return false;
    return true;
}

static bool native_signature_put_signature(native_signature_buffer *buffer, const qa_native_signature *signature,
                               qa_error *error) {
    if (!signature || signature->parameter_count > NATIVE_MAX_ARGUMENTS ||
        (signature->parameter_count && !signature->parameters))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "valid native observer signature is required");
    if (!native_signature_put_u32(buffer, (uint32_t)signature->abi, error) ||
        !native_signature_put_u8(buffer, signature->variadic ? 1u : 0u, error) ||
        !native_signature_put_u64(buffer, signature->parameter_count, error))
        return false;
    size_t count = 0;
    for (size_t index = 0; index < signature->parameter_count; ++index)
        if (!signature_put_type(buffer, &signature->parameters[index], 0, &count, error))
            return false;
    return signature_put_type(buffer, &signature->result, 0, &count, error);
}

static bool signature_take(native_signature_reader *reader, size_t size, const uint8_t **out,
                      qa_error *error) {
    if (!reader || reader->offset > reader->bytes.size ||
        size > reader->bytes.size - reader->offset)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native observer message is truncated");
    *out = reader->bytes.data + reader->offset;
    reader->offset += size;
    return true;
}

static bool native_signature_get_u8(native_signature_reader *reader, uint8_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !signature_take(reader, 1, &bytes, error))
        return false;
    *out = bytes[0];
    return true;
}

static bool native_signature_get_u32(native_signature_reader *reader, uint32_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !signature_take(reader, 4, &bytes, error))
        return false;
    *out = qa_load_u32le(bytes);
    return true;
}

static bool native_signature_get_u64(native_signature_reader *reader, uint64_t *out, qa_error *error) {
    const uint8_t *bytes;
    if (!out || !signature_take(reader, 8, &bytes, error))
        return false;
    *out = qa_load_u64le(bytes);
    return true;
}

static void signature_type_free(qa_native_type *type) {
    if (!type || !type->fields)
        return;
    qa_native_type *fields = (qa_native_type *)type->fields;
    for (size_t index = 0; index < type->field_count; ++index)
        signature_type_free(&fields[index]);
    free(fields);
    type->fields = NULL;
    type->field_count = 0;
}

static bool signature_get_type(native_signature_reader *reader, qa_native_type *out, unsigned depth,
                          size_t *count, qa_error *error) {
    uint32_t kind;
    uint64_t repetition, field_count;
    if (depth > 16u || ++*count > NATIVE_SIGNATURE_MAX_TYPES ||
        !native_signature_get_u32(reader, &kind, error) ||
        !native_signature_get_u64(reader, &repetition, error) ||
        !native_signature_get_u64(reader, &field_count, error) || kind > QA_NATIVE_BYTES ||
        !repetition ||
#if SIZE_MAX < UINT64_MAX
        repetition > (uint64_t)SIZE_MAX || field_count > (uint64_t)SIZE_MAX ||
#endif
        field_count > NATIVE_SIGNATURE_MAX_TYPES || ((kind == QA_NATIVE_BYTES) != (field_count != 0)))
        return native_fail(error, QA_ERROR_FORMAT, reader->offset,
                           "native observer ABI type tree is invalid");
    qa_native_type type = {.kind = (qa_native_value_type)kind, .count = (size_t)repetition};
    if (field_count) {
        qa_native_type *fields = calloc((size_t)field_count, sizeof(*fields));
        if (!fields)
            return native_fail(error, QA_ERROR_MEMORY, reader->offset,
                               "allocating native observer ABI fields");
        type.fields = fields;
        type.field_count = (size_t)field_count;
        for (size_t index = 0; index < type.field_count; ++index) {
            if (!signature_get_type(reader, &fields[index], depth + 1u, count, error)) {
                signature_type_free(&type);
                return false;
            }
        }
    }
    *out = type;
    return true;
}

static void native_signature_signature_free(qa_native_signature *signature) {
    if (!signature)
        return;
    qa_native_type *parameters = (qa_native_type *)signature->parameters;
    for (size_t index = 0; index < signature->parameter_count; ++index)
        signature_type_free(&parameters[index]);
    free(parameters);
    signature_type_free(&signature->result);
    *signature = (qa_native_signature){0};
}


static bool native_signature_get_signature(native_signature_reader *reader, qa_native_signature *out,
                               qa_error *error) {
    uint32_t abi;
    uint8_t variadic;
    uint64_t parameter_count;
    if (!out || !native_signature_get_u32(reader, &abi, error) ||
        !native_signature_get_u8(reader, &variadic, error) || variadic > 1u ||
        !native_signature_get_u64(reader, &parameter_count, error) || abi > QA_NATIVE_ABI_AAPCS64 ||
        parameter_count > NATIVE_MAX_ARGUMENTS)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native observer signature header is invalid");
    qa_native_signature signature = {.abi = (qa_native_abi)abi,
                                     .variadic = variadic != 0,
                                     .parameter_count = (size_t)parameter_count};
    qa_native_type *parameters = NULL;
    if (signature.parameter_count) {
        parameters = calloc(signature.parameter_count, sizeof(*parameters));
        if (!parameters)
            return native_fail(error, QA_ERROR_MEMORY, reader->offset,
                               "allocating native observer signature");
        signature.parameters = parameters;
    }
    size_t count = 0;
    for (size_t index = 0; index < signature.parameter_count; ++index) {
        if (!signature_get_type(reader, &parameters[index], 0, &count, error)) {
            native_signature_signature_free(&signature);
            return false;
        }
    }
    if (!signature_get_type(reader, &signature.result, 0, &count, error)) {
        native_signature_signature_free(&signature);
        return false;
    }
    *out = signature;
    return true;
}

static bool native_signature_end(native_signature_reader *reader, qa_error *error) {
    if (!reader || reader->offset != reader->bytes.size)
        return native_fail(error, QA_ERROR_FORMAT, reader ? reader->offset : 0,
                           "native observer message has trailing bytes");
    return true;
}

static bool observer_boundary(qa_native_instance *instance, qa_error *error) {
    if (!instance || instance->checkpointing || instance->destroying || instance->unloading)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native observation requires a live instance");
    if (!instance->guest || (!instance->options.observe && !instance->region_count))
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native observation requires its actual guest execution owner");
    return true;
}

static bool next_id(qa_native_instance *instance, uint64_t *id, qa_error *error) {
    if (instance->next_observer_id == UINT64_MAX)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native observer identity is exhausted");
    *id = ++instance->next_observer_id;
    return true;
}

static bool copy_signature(const qa_native_signature *signature, qa_native_signature *out,
                            qa_error *error) {
    native_signature_buffer encoded = {0};
    bool ok = native_signature_put_signature(&encoded, signature, error);
    if (ok) {
        native_signature_reader reader = {.bytes = {encoded.data, encoded.size}};
        ok = native_signature_get_signature(&reader, out, error) && native_signature_end(&reader, error);
    }
    native_signature_buffer_free(&encoded);
    return ok;
}

static bool process_entry(void *context, qa_native_guest *guest, uint64_t id, qa_error *error) {
    qa_native_entry_observer *binding = context;
    qa_native_instance *instance = binding->instance;
    if (instance->guest != guest || binding->guest_id != id || native_active_instance != instance ||
        !binding->callback || instance->pending_entry_observers)
        return native_fail(error, QA_ERROR_ARGUMENT, id, "observed entry lost its actual source owner");
    size_t count = binding->signature.parameter_count;
    if (count > NATIVE_MAX_ARGUMENTS) return native_fail(error, QA_ERROR_ARGUMENT, count, "observed source ABI exceeds its argument limit");
    qa_native_value arguments[NATIVE_MAX_ARGUMENTS] = {{0}};
    qa_native_value result = {.type = binding->signature.result.kind};
    qa_buffer storage = {0}, output = {0};
    if (result.type == QA_NATIVE_BYTES) {
        output.size = guest_abi_result_bytes(binding->guest_plan);
        output.data = calloc(1, output.size);
        if (!output.data) return native_fail(error, QA_ERROR_MEMORY, id, "owning actual observed aggregate result");
        result.as.bytes = (qa_native_memory){output.data, output.size};
    }
    bool okay = guest_abi_decode(binding->guest_plan, guest, arguments, count, &storage, error);
    if (okay) {
        ++binding->active_calls; ++instance->callback_depth;
        okay = binding->callback(binding->context, instance, binding, arguments, count, &result, error);
        --instance->callback_depth; --binding->active_calls;
    }
    if (okay) okay = guest_abi_return(binding->guest_plan, guest, &result, error);
    qa_buffer_free(&storage); qa_buffer_free(&output); return okay;
}

static bool signature_equal(const qa_native_signature *a,const qa_native_signature *b,qa_error *error)
{
    native_signature_buffer left={0},right={0};
    bool ok=native_signature_put_signature(&left,a,error) && native_signature_put_signature(&right,b,error) &&
        left.size==right.size && !memcmp(left.data,right.data,left.size);
    native_signature_buffer_free(&left); native_signature_buffer_free(&right); return ok;
}
static bool observer_signature_fields(qa_source_save_io *io,qa_native_signature *signature)
{
    native_signature_buffer wire={0}; size_t count=0;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    bool ok=reading || native_signature_put_signature(&wire,signature,io->error);
    if (!reading) count=wire.size;
    if (ok) ok=qa_source_save_count(io,&count,SIZE_MAX);
    if (ok && reading) {
        ok=io->offset<=io->input.size && count<=io->input.size-io->offset;
        if (!ok) native_fail(io->error,QA_ERROR_FORMAT,io->offset,"Saved Source signature leaves its counted extent");
        if (ok) {
            native_signature_reader reader={.bytes={io->input.data+io->offset,count}};
            ok=native_signature_get_signature(&reader,signature,io->error) && native_signature_end(&reader,io->error);
            if (ok) io->offset+=count;
        }
    } else if (ok) ok=qa_source_save_bytes(io,wire.data,count);
    native_signature_buffer_free(&wire); return ok;
}
bool native_observers_fields(qa_source_save_io *io,qa_native_instance *instance)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=0;
    if (instance->pending_entry_observers || (reading && instance->entry_observers))
        return native_fail(io->error,QA_ERROR_ARGUMENT,0,"Native observer recipe requires its actual isolated custody");
    if (!reading) for (qa_native_entry_observer *row=instance->entry_observers;row;row=row->next) ++count;
    if (!qa_source_save_u64(io,&instance->next_observer_id) || !qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/24))
        return native_fail(io->error,QA_ERROR_FORMAT,io->offset,"Saved Source observer count exceeds its stored rows");
    qa_native_entry_observer *row=instance->entry_observers;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            row=calloc(1,sizeof(*row));
            if (!row) return native_fail(io->error,QA_ERROR_MEMORY,i,"Owning saved Source entry observer");
            row->instance=instance; row->next=instance->pending_entry_observers; instance->pending_entry_observers=row;
        }
        if (!qa_source_save_u64(io,&row->id) || !qa_source_save_u64(io,&row->address) ||
            !observer_signature_fields(io,&row->signature) || !row->id || row->id>instance->next_observer_id ||
            row->address<instance->image_base || row->address-instance->image_base>=instance->image_bytes ||
            row->signature.abi!=instance->module->info.image.target.abi || row->signature.variadic || row->active_calls ||
            (!reading && !row->callback))
            return native_fail(io->error,QA_ERROR_FORMAT,i,"Saved Source observer leaves its actual image or signature");
        if (reading) {
            const native_profile_spec *profile=native_profile(instance->module->info.profile);
            uint64_t slots=profile->q3_vm?1:profile->import_count;
            if (slots>UINT64_MAX-instance->first_callback || row->id>UINT64_MAX-instance->first_callback-slots)
                return native_fail(io->error,QA_ERROR_FORMAT,row->id,"Saved Source observer namespace overflows");
            row->guest_id=instance->first_callback+slots+row->id;
            if (instance->process_kind==QA_NATIVE_PROCESS_WINDOWS && row->guest_id>=qa_native_windows_process_callback_minimum())
                return native_fail(io->error,QA_ERROR_FORMAT,row->id,"Saved Source observer overlaps its Windows runtime");
            for (qa_native_entry_observer *other=row->next;other;other=other->next)
                if (other->id==row->id || other->address==row->address)
                    return native_fail(io->error,QA_ERROR_FORMAT,row->id,"Saved Source observer identity repeats");
            if (!guest_abi_plan_native(&row->signature,NULL,0,&row->guest_plan,io->error)) return false;
        } else row=row->next;
    }
    return true;
}
bool native_observers_resolve(qa_native_instance *instance,uint64_t id,uint64_t address,
    qa_native_guest_callback *out,qa_error *error)
{
    for (qa_native_entry_observer *row=instance->pending_entry_observers;row;row=row->next)
        if (row->guest_id==id) {
            if (row->address!=address) return native_fail(error,QA_ERROR_FORMAT,id,"Saved Source observer address differs");
            *out=(qa_native_guest_callback){id,address,process_entry,row}; return true;
        }
    return native_fail(error,QA_ERROR_NOT_FOUND,id,"Native restored callback has no actual SDK or Source observer identity");
}
bool qa_native_observers_restore_ready(const qa_native_instance *instance,qa_error *error)
{
    return (instance && !instance->pending_entry_observers) ||
        native_fail(error,QA_ERROR_ARGUMENT,0,"Native Source observers still require their actual restored owners");
}
static bool stopped_write_subscription(const qa_native_instance *instance)
{
    return instance->guest&&
        instance->active_write_event&&instance->write_depth&&instance->callback_depth&&
        native_active_instance==instance&&
        instance->guest->publication_depth&&!instance->guest->faulting&&!instance->guest->stepping;
}
static bool process_entry_bind(qa_native_entry_observer *binding, qa_error *error) {
    qa_native_instance *instance = binding->instance;
    const native_profile_spec *profile = native_profile(instance->module->info.profile);
    uint64_t slots = profile->q3_vm ? 1 : profile->import_count;
    if (slots > UINT64_MAX - instance->first_callback ||
        binding->id > UINT64_MAX - instance->first_callback - slots)
        return native_fail(error, QA_ERROR_ARGUMENT, binding->id, "source observer callback namespace overflows");
    binding->guest_id = instance->first_callback + slots + binding->id;
    if (instance->process_kind == QA_NATIVE_PROCESS_WINDOWS &&
        binding->guest_id >= qa_native_windows_process_callback_minimum())
        return native_fail(error, QA_ERROR_ARGUMENT, binding->id, "source observer overlaps the actual Windows service namespace");
    if (!guest_abi_plan_native(&binding->signature, NULL, 0, &binding->guest_plan, error)) return false;
    qa_native_guest_callback callback = {binding->guest_id, binding->address, process_entry, binding};
    bool stopped=stopped_write_subscription(instance);
    if(stopped&&instance->guest->stopped_write_bindings==UINT_MAX)
        return native_fail(error,QA_ERROR_ARGUMENT,0,"Stopped Source subscription depth exhausted");
    if(stopped)++instance->guest->stopped_write_bindings;
    bool okay=qa_native_guest_bind(instance->guest,&callback,error);
    if(stopped)--instance->guest->stopped_write_bindings;
    return okay;
}

bool qa_native_observe_entry(qa_native_instance *instance, qa_native_address entry,
                             const qa_native_signature *signature,
                             qa_native_entry_observer_fn callback, void *context,
                             qa_native_entry_observer **out, qa_error *error) {
    if (!observer_boundary(instance, error))
        return false;
    if (!callback || !signature || !out || entry < instance->image_base ||
        entry - instance->image_base >= instance->image_bytes ||
        signature->abi != instance->module->info.image.target.abi || signature->variadic ||
        ((instance->region_depth||instance->write_depth)&&!stopped_write_subscription(instance)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native entry requires an image address and exact nonvariadic ABI");
    for (qa_native_entry_observer *other = instance->entry_observers; other; other = other->next)
        if (other->address == entry)
            return native_fail(error, QA_ERROR_ARGUMENT, 0, "native entry is already intercepted");
    qa_native_entry_observer **pending=&instance->pending_entry_observers;
    while (*pending && (*pending)->address!=entry) pending=&(*pending)->next;
    if (*pending) {
        qa_native_entry_observer *binding=*pending;
        if (!signature_equal(signature,&binding->signature,error))
            return native_fail(error,QA_ERROR_FORMAT,binding->id,"Restored Source observer changes its exact signature");
        bool retained=false;
        for (size_t i=0;i<instance->guest->callback_count;++i) {
            const qa_native_guest_callback *actual=instance->guest->callbacks+i;
            if (actual->id==binding->guest_id && actual->address==entry &&
                actual->invoke==process_entry && actual->context==binding) retained=true;
        }
        if (!retained) return native_fail(error,QA_ERROR_FORMAT,binding->id,
            "Restored Source observer lacks its actual guest callback custody");
        *pending=binding->next; binding->callback=callback; binding->context=context;
        binding->next=instance->entry_observers; instance->entry_observers=binding; *out=binding;
        return true;
    }
    qa_native_entry_observer *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native entry observer");
    binding->instance = instance;
    binding->address = entry;
    binding->callback = callback;
    binding->context = context;
    bool ok = next_id(instance, &binding->id, error) &&
              copy_signature(signature, &binding->signature, error) &&
              process_entry_bind(binding, error);
    if (!ok) {
        native_signature_signature_free(&binding->signature);
        guest_abi_plan_destroy(binding->guest_plan);
        free(binding);
        return false;
    }
    binding->next = instance->entry_observers;
    instance->entry_observers = binding;
    *out = binding;
    return true;
}

bool qa_native_unobserve_entry(qa_native_entry_observer *binding, qa_error *error) {
    if (!binding)
        return true;
    qa_native_instance *instance = binding->instance;
    if (!observer_boundary(instance, error))
        return false;
    if (binding->active_calls || instance->region_depth || instance->write_depth)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "active native entry cannot be removed");
    if (!qa_native_guest_unbind(instance->guest, binding->guest_id, error)) return false;
    qa_native_entry_observer **cursor = &instance->entry_observers;
    while (*cursor && *cursor != binding)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = binding->next;
    native_signature_signature_free(&binding->signature);
    guest_abi_plan_destroy(binding->guest_plan);
    free(binding);
    return true;
}

bool qa_native_invoke_original(qa_native_entry_observer *binding,
                               const qa_native_value *arguments, size_t count,
                               qa_native_value *result, qa_error *error) {
    if (!binding || !observer_boundary(binding->instance, error))
        return false;
    qa_native_instance *instance = binding->instance;
    bool region_original = instance->active_region_event && binding->active_calls &&
        native_active_instance == instance && instance->callback_depth &&
        instance->region_invocation_entry == binding->address &&
        instance->region_invocation_depth == instance->active_depth;
    bool write_original = instance->write_scope && binding->active_calls &&
        native_active_instance==instance && instance->callback_depth &&
        instance->write_scope->event==instance->active_write_event &&
        instance->write_scope->depth==instance->write_depth &&
        instance->active_depth>instance->write_scope->invocation_depth;
    if ((instance->region_depth && !region_original && !write_original) ||
        (instance->write_depth && !write_original) ||
        instance->lifecycle != QA_NATIVE_INITIALIZED ||
        count != binding->signature.parameter_count || (count && !arguments))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "original native invocation requires an application call boundary");
    ++binding->active_calls;
    ++instance->active_depth;
    qa_native_instance *previous = native_active_instance;
    native_active_instance = instance;
    bool ok = instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
            qa_native_sysv_process_invoke_original(instance->sysv_process, binding->guest_id, binding->address,
                &binding->signature, arguments, count, result, error) :
            qa_native_windows_process_invoke_original(instance->windows_process, binding->guest_id, binding->address,
                &binding->signature, arguments, count, result, error);
    native_active_instance = previous;
    --instance->active_depth;
    --binding->active_calls;
    return ok;
}

bool qa_native_write_scope_open(qa_native_instance *instance,const qa_native_write_event *event,
    qa_native_write_scope **out,qa_error *error)
{
    if(!out||*out||!event||!observer_boundary(instance,error)||
        !instance->guest||
        instance->active_write_event!=event||!instance->write_depth||!instance->callback_depth||
        native_active_instance!=instance||
        !instance->guest->publication_depth||instance->guest->faulting||
        instance->guest->stopped_write_calls==UINT_MAX||!guest_mutable(instance->guest,error))
        return native_fail(error,QA_ERROR_ARGUMENT,0,"Source reaction requires its exact stopped committed-write event");
    qa_unified_frame_lease *storage=qa_unified_frame_lease_acquire(instance->observation_storage,error);
    if(!storage)return false;
    qa_native_write_scope *scope=qa_unified_frame_lease_alloc(storage,1,sizeof(*scope),_Alignof(qa_native_write_scope),error);
    if(!scope){qa_unified_frame_lease_release(storage);return false;}
    scope->storage=storage;
    scope->instance=instance; scope->event=event; scope->depth=instance->write_depth;
    scope->invocation_depth=instance->active_depth; scope->previous=instance->write_scope;
    instance->write_scope=scope; ++instance->guest->stopped_write_calls; *out=scope;
    return true;
}
bool qa_native_invoke_original_cancellable(qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,
    qa_native_entry_cancel_fn accepts,void *context,bool *cancelled,qa_error *error)
{
    qa_native_instance *instance=binding?binding->instance:NULL;
    if(!cancelled)return native_fail(error,QA_ERROR_ARGUMENT,0,"Original cancellation omitted its actual result receipt");
    *cancelled=false;
    if(!instance||!accepts||!binding->active_calls||native_active_instance!=instance||
        !instance->callback_depth||
        !instance->guest||
        binding->signature.result.kind!=QA_NATIVE_VOID||!guest_mutable(instance->guest,error))
        return native_fail(error,QA_ERROR_UNSUPPORTED,0,"Original cancellation requires its actual stopped VOID entry");
    guest_callback_recovery recovery={.previous=instance->guest->recovery,.accepts=accepts,.context=context};
    instance->guest->recovery=&recovery;
    bool okay=qa_native_invoke_original(binding,arguments,count,result,error);
    guest_callback_recovery **link=&instance->guest->recovery;
    while(*link&&*link!=&recovery)link=&(*link)->previous;
    if(*link)*link=recovery.previous;
    else {
        instance->guest->failed=true;
        okay=native_fail(error,QA_ERROR_ARGUMENT,0,"Original cancellation lost its actual recovery custody");
    }
    *cancelled=okay&&recovery.cancelled&&recovery.restored;
    return okay;
}
struct qa_native_call_scope {
    struct qa_native_call_scope *previous;
    qa_native_instance *instance;
    qa_native_guest_cpu cpu;
    guest_host_x86_64_state hardware;
    guest_callback_recovery recovery;
    guest_run *run;
    unsigned active_depth,callback_depth,write_depth,region_depth;
    bool resolved;
};
static bool call_scope_current(const qa_native_call_scope *scope,bool closing,qa_error *error)
{
    qa_native_instance *instance=scope?scope->instance:NULL;
    bool returned=instance&&instance->guest&&closing&&scope->resolved&&
        !instance->active_depth&&!instance->callback_depth&&!instance->write_depth&&!instance->region_depth&&
        !instance->guest->run&&!instance->guest->callback_depth&&!instance->guest->publication_depth&&
        !instance->guest->stopped_write_calls&&!instance->guest->stopped_write_bindings;
    if(!instance||instance->call_scope!=scope||!instance->guest||
        instance->guest->recovery!=&scope->recovery||(!returned&&(instance->guest->run!=scope->run||
        instance->active_depth!=scope->active_depth||instance->callback_depth!=scope->callback_depth||
        instance->write_depth!=scope->write_depth||instance->region_depth!=scope->region_depth)))
        return native_fail(error,QA_ERROR_ARGUMENT,0,"Source call still owns another processor invocation");
    return true;
}
bool qa_native_call_scope_open(qa_native_instance *instance,qa_native_entry_cancel_fn accepts,
    void *context,qa_native_call_scope **out,qa_error *error)
{
    if(!out||*out||!instance||!accepts||instance->lifecycle!=QA_NATIVE_INITIALIZED||
        instance->checkpointing||instance->destroying||instance->unloading||
        !instance->guest||
        !guest_mutable(instance->guest,error))
        return native_fail(error,QA_ERROR_UNSUPPORTED,0,"Source cancellation requires its live stopped processor");
    qa_native_call_scope *scope=calloc(1,sizeof(*scope));
    if(!scope)return native_fail(error,QA_ERROR_MEMORY,0,"Retaining actual Source call processor");
    if(!qa_native_guest_cpu_read(instance->guest,&scope->cpu,error)) { free(scope); return false; }
    if(instance->guest->options.backend==QA_NATIVE_GUEST_HOST_X86_64 &&
        !guest_host_child_cpu_read(instance->guest->child,&scope->hardware,error)) { free(scope); return false; }
    scope->instance=instance; scope->previous=instance->call_scope; scope->run=instance->guest->run;
    scope->active_depth=instance->active_depth; scope->callback_depth=instance->callback_depth;
    scope->write_depth=instance->write_depth; scope->region_depth=instance->region_depth;
    scope->recovery=(guest_callback_recovery){.previous=instance->guest->recovery,
        .invocation=&scope->cpu,.accepts=accepts,.context=context};
    instance->call_scope=scope; instance->guest->recovery=&scope->recovery; *out=scope;
    return true;
}
bool qa_native_call_scope_resolve(qa_native_call_scope *scope,bool completed,
    bool *cancelled,qa_error *error)
{
    if(!cancelled||!call_scope_current(scope,false,error))return false;
    *cancelled=false;
    if(scope->resolved)return native_fail(error,QA_ERROR_ARGUMENT,0,"Source call processor was already resolved");
    scope->resolved=true;
    if(completed) {
        scope->recovery.resolved=true;
        if(scope->recovery.cancelled)
            return native_fail(error,QA_ERROR_ARGUMENT,0,"Source call lost its cancelled callback failure");
        return true;
    }
    qa_native_guest *guest=scope->instance->guest;
    bool accepted=guest_callback_failure(guest,error)&&scope->recovery.cancelled&&
        guest_callback_cancelled(guest,error);
    scope->recovery.resolved=true;
    if(!accepted)return false;
    if(!(scope->hardware.xsave.data ? guest_host_child_cpu_write(guest->child,&scope->hardware,error) :
        qa_native_guest_cpu_write(guest,&scope->cpu,error)))return false;
    scope->recovery.restored=true; *cancelled=true;
    if(error)*error=(qa_error){0};
    return true;
}
bool qa_native_call_scope_close(qa_native_call_scope **out,qa_error *error)
{
    qa_native_call_scope *scope=out?*out:NULL;
    if(!scope)return true;
    if(!call_scope_current(scope,true,error))return false;
    scope->instance->guest->recovery=scope->recovery.previous;
    scope->instance->call_scope=scope->previous;
    guest_host_x86_64_state_free(&scope->hardware); free(scope); *out=NULL;
    return true;
}
bool qa_native_call_scope_abandon(qa_native_call_scope *scope,qa_error *error)
{
    qa_native_instance *instance=scope?scope->instance:NULL;
    if(!instance||instance->call_scope!=scope||!instance->guest||
        instance->guest->recovery!=&scope->recovery||instance->active_depth||instance->callback_depth||
        instance->write_depth||instance->region_depth||instance->guest->run||
        instance->guest->callback_depth||instance->guest->publication_depth||
        instance->guest->stopped_write_calls||instance->guest->stopped_write_bindings)
        return native_fail(error,QA_ERROR_ARGUMENT,0,"Source failure retirement requires all actual callbacks returned");
    if(!scope->resolved) {
        scope->resolved=true; scope->recovery.resolved=true;
        instance->guest->failed=true;
    }
    return true;
}
bool qa_native_write_scope_close(qa_native_write_scope **out,qa_error *error)
{
    qa_native_write_scope *scope=out?*out:NULL;
    if(!scope)return true;
    qa_native_instance *instance=scope->instance;
    if(instance->write_scope!=scope||instance->active_write_event!=scope->event||
        instance->write_depth!=scope->depth||instance->active_depth!=scope->invocation_depth||
        !instance->guest||!instance->guest->stopped_write_calls)
        return native_fail(error,QA_ERROR_ARGUMENT,0,"Stopped Source reaction still owns another invocation");
    instance->write_scope=scope->previous; --instance->guest->stopped_write_calls;
    qa_unified_frame_lease_release(scope->storage); *out=NULL; return true;
}

bool native_process_write_commit(void *context, qa_native_guest *guest,
    const qa_native_guest_commit *commit, qa_error *error) {
    qa_native_instance *instance = context;
    if (!instance || instance->guest != guest || !commit || !commit->bytes || commit->bytes > UINT64_MAX - commit->address)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native write receipt lost its actual memory owner");
    size_t count = 0;
    for (qa_native_write_observer *b = instance->write_observers; b; b = b->next) ++count;
    if (count > SIZE_MAX / sizeof(uint64_t)) return native_fail(error, QA_ERROR_MEMORY, count, "native write observer inventory overflows");
    if (!count) return true;
    qa_unified_frame_lease *storage=qa_unified_frame_lease_acquire(instance->observation_storage,error);
    if (!storage) return false;
    uint64_t *ids=qa_unified_frame_lease_alloc(storage,count,sizeof(*ids),_Alignof(uint64_t),error);
    if (!ids) { qa_unified_frame_lease_release(storage); return false; }
    size_t at = 0;
    for (qa_native_write_observer *b = instance->write_observers; b; b = b->next) ids[at++] = b->id;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        qa_native_write_observer *binding = instance->write_observers;
        while (binding && binding->id != ids[i]) binding = binding->next;
        if (!binding) continue;
        uint64_t begin = commit->address > binding->address ? commit->address : binding->address;
        uint64_t end = commit->address + commit->bytes;
        if (end > binding->address + binding->size) end = binding->address + binding->size;
        if (begin < end) {
            uint8_t *before=qa_unified_frame_lease_alloc(storage,binding->size,1,1,error);
            uint8_t *after=qa_unified_frame_lease_alloc(storage,binding->size,1,1,error);
            if (!before || !after) {
                okay=false; break;
            }
            memcpy(before, binding->snapshot.data, binding->size);
            okay = qa_native_guest_read(guest, binding->address, after, binding->size, error);
            if (okay) {
                qa_native_write_event event = {.instruction = commit->instruction, .address = binding->address,
                    .offset = (size_t)(begin - binding->address), .size = (size_t)(end - begin),
                    .before = {before, binding->size}, .after = {after, binding->size}};
                ++binding->active_calls; ++instance->callback_depth; ++instance->write_depth;
                const qa_native_write_event *previous=instance->active_write_event;
                instance->active_write_event=&event;
                okay = binding->callback(binding->context, instance, &event, error);
                instance->active_write_event=previous;
                --instance->write_depth; --instance->callback_depth; --binding->active_calls;
            }
        }
    }
    bool cancelled=!okay&&guest_callback_failure(guest,error);
    if ((okay && commit->instruction_last) || cancelled) {
        bool refreshed=true;
        for (qa_native_write_observer *b = instance->write_observers; refreshed && b; b = b->next)
            refreshed = qa_native_guest_read(guest, b->address, b->snapshot.data, b->size, error);
        if(!refreshed) okay=false;
    }
    qa_unified_frame_lease_release(storage); return okay;
}

bool qa_native_observe_writes(qa_native_instance *instance, qa_native_address address,
                              size_t bytes, qa_native_write_observer_fn callback, void *context,
                              qa_native_write_observer **out, qa_error *error) {
    if (!observer_boundary(instance, error))
        return false;
    if (!address || !bytes || bytes > NATIVE_MAX_WATCH_BYTES ||
        bytes > UINT64_MAX - address || !callback || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, bytes, "native write watch range is invalid");
    qa_native_write_observer *binding = calloc(1, sizeof(*binding));
    if (!binding)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native write observer");
    binding->instance = instance;
    binding->address = address;
    binding->size = bytes;
    binding->callback = callback;
    binding->context = context;
    bool ok = next_id(instance, &binding->id, error);
    if (ok) {
        binding->snapshot.data = malloc(bytes); binding->snapshot.size = bytes;
        if (!binding->snapshot.data) ok = native_fail(error, QA_ERROR_MEMORY, binding->id, "owning actual write watch snapshot");
        if (ok) ok = qa_native_guest_read(instance->guest, address, binding->snapshot.data, bytes, error);
        if (ok && !instance->process_observing) {
            ok = qa_native_guest_observe(instance->guest, native_process_write_commit, instance, error);
            if (ok) instance->process_observing = true;
        }
        if (ok) ok = guest_native_interest(instance->guest, GUEST_PROFILE_INTEREST_STORE,
            binding->id, address, bytes, false, error);
    }
    if (!ok) {
        qa_buffer_free(&binding->snapshot); free(binding);
        return false;
    }
    binding->next = instance->write_observers;
    instance->write_observers = binding;
    *out = binding;
    return true;
}

bool qa_native_unobserve_writes(qa_native_write_observer *binding, qa_error *error) {
    if (!binding)
        return true;
    qa_native_instance *instance = binding->instance;
    if (!observer_boundary(instance, error))
        return false;
    if (binding->active_calls)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "active native write watch cannot be removed");
    bool control_needed = !qa_native_terminal(instance) || instance->active_depth ||
        instance->callback_depth || instance->region_depth || instance->write_depth;
    if (control_needed &&
        !guest_native_interest(instance->guest, GUEST_PROFILE_INTEREST_STORE,
            binding->id, binding->address, binding->size, true, error)) return false;
    qa_native_write_observer **cursor = &instance->write_observers;
    while (*cursor && *cursor != binding)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = binding->next;
    qa_buffer_free(&binding->snapshot); free(binding);
    return true;
}

void native_observers_destroy(qa_native_instance *instance) {
    while (instance->pending_entry_observers) {
        qa_native_entry_observer *binding=instance->pending_entry_observers;
        instance->pending_entry_observers=binding->next;
        native_signature_signature_free(&binding->signature);
        guest_abi_plan_destroy(binding->guest_plan); free(binding);
    }
    while (instance->entry_observers) {
        qa_native_entry_observer *binding = instance->entry_observers;
        instance->entry_observers = binding->next;
        native_signature_signature_free(&binding->signature);
        guest_abi_plan_destroy(binding->guest_plan);
        free(binding);
    }
    while (instance->write_observers) {
        qa_native_write_observer *binding = instance->write_observers;
        instance->write_observers = binding->next;
        qa_buffer_free(&binding->snapshot); free(binding);
    }
}
