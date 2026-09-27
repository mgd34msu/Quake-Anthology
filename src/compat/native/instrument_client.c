#if defined(QA_NATIVE_DYNAMORIO_CLIENT)

#include "dr_api.h"
#include "drmgr.h"

#include <stdint.h>
#include <string.h>

#define QA_HOOK_MAGIC 0x524e4151u
#define QA_HOOK_VERSION 1u
#define QA_HOOK_REPLY 0x8000u
#define QA_HOOK_READ 4u
#define QA_HOOK_WRITE 5u
#define QA_HOOK_REGION 69u
#define QA_HOOK_MAX_FRAME (256u * 1024u * 1024u)
#define QA_HOOK_REPLY_BYTES_OVERHEAD 28u
#define QA_HOOK_STATE_BYTES (16u * 8u + 16u * 16u + 16u)
#ifdef X64
#define QA_HOOK_SIMD_COUNT 16u
#else
#define QA_HOOK_SIMD_COUNT 8u
#endif

typedef struct hook_region {
    uint32_t id;
    uint32_t entry;
    uint32_t join;
    uint32_t has_frame;
    uint32_t frame_entry;
    uint32_t frame_exit;
} hook_region;

typedef struct hook_state {
    uint64_t registers[16];
    uint8_t simd[16][16];
    uint64_t flags;
    uint64_t instruction;
} hook_state;

typedef struct hook_frame {
    uint16_t opcode;
    uint32_t depth;
    uint64_t sequence;
    uint64_t reply_to;
    uint8_t *payload;
    size_t payload_size;
} hook_frame;

static hook_region *regions;
static size_t region_count;
static char *module_name;
static uint64_t module_image_bytes;
static app_pc module_base;
static uint64_t sequence = UINT64_C(0xc000000000000001);
static uint32_t call_depth;
static thread_id_t owner_thread;
static file_t hook_input;
static file_t hook_output;

static bool hook_u64_fits_size(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return true;
#endif
}

static uint16_t load_u16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | (uint16_t)bytes[1] << 8u);
}

static uint32_t load_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8u | (uint32_t)bytes[2] << 16u |
           (uint32_t)bytes[3] << 24u;
}

static uint64_t load_u64(const uint8_t *bytes) {
    return (uint64_t)load_u32(bytes) | (uint64_t)load_u32(bytes + 4u) << 32u;
}

static void store_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void store_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned index = 0; index < 4u; ++index)
        bytes[index] = (uint8_t)(value >> (index * 8u));
}

static void store_u64(uint8_t *bytes, uint64_t value) {
    store_u32(bytes, (uint32_t)value);
    store_u32(bytes + 4u, (uint32_t)(value >> 32u));
}

static bool read_exact(file_t file, void *destination, size_t size) {
    uint8_t *bytes = destination;
    size_t offset = 0;
    while (offset < size) {
        ssize_t received = dr_read_file(file, bytes + offset, size - offset);
        if (received <= 0)
            return false;
        offset += (size_t)received;
    }
    return true;
}

static bool write_exact(file_t file, const void *source, size_t size) {
    const uint8_t *bytes = source;
    size_t offset = 0;
    while (offset < size) {
        ssize_t written = dr_write_file(file, bytes + offset, size - offset);
        if (written <= 0)
            return false;
        offset += (size_t)written;
    }
    return true;
}

static bool send_frame(uint16_t opcode, uint64_t reply_to, const uint8_t *payload,
                       size_t payload_size, uint64_t *sent_sequence) {
    if (payload_size > QA_HOOK_MAX_FRAME)
        return false;
    uint8_t header[40] = {0};
    uint64_t current = ++sequence;
    store_u32(header, QA_HOOK_MAGIC);
    store_u16(header + 4u, QA_HOOK_VERSION);
    store_u16(header + 6u, opcode);
    store_u32(header + 8u, call_depth);
    store_u64(header + 16u, current);
    store_u64(header + 24u, reply_to);
    store_u64(header + 32u, payload_size);
    if (!write_exact(hook_output, header, sizeof(header)) ||
        !write_exact(hook_output, payload, payload_size))
        return false;
    if (sent_sequence)
        *sent_sequence = current;
    return true;
}

static bool receive_frame(hook_frame *out) {
    uint8_t header[40];
    if (!read_exact(hook_input, header, sizeof(header)) || load_u32(header) != QA_HOOK_MAGIC ||
        load_u16(header + 4u) != QA_HOOK_VERSION)
        return false;
    uint64_t payload_size = load_u64(header + 32u);
    if (payload_size > QA_HOOK_MAX_FRAME || !hook_u64_fits_size(payload_size))
        return false;
    uint8_t *payload = NULL;
    if (payload_size) {
        payload = dr_global_alloc((size_t)payload_size);
        if (!payload || !read_exact(hook_input, payload, (size_t)payload_size)) {
            if (payload)
                dr_global_free(payload, (size_t)payload_size);
            return false;
        }
    }
    *out = (hook_frame){.opcode = load_u16(header + 6u),
                        .depth = load_u32(header + 8u),
                        .sequence = load_u64(header + 16u),
                        .reply_to = load_u64(header + 24u),
                        .payload = payload,
                        .payload_size = (size_t)payload_size};
    return true;
}

static void free_frame(hook_frame *frame) {
    if (frame->payload)
        dr_global_free(frame->payload, frame->payload_size);
    memset(frame, 0, sizeof(*frame));
}

static bool send_operation_reply(const hook_frame *request, bool ok, const uint8_t *body,
                                 size_t body_size, const char *message) {
    size_t message_size = ok || !message ? 0 : strlen(message);
    size_t payload_size = 20u + message_size + (ok ? body_size : 0u);
    uint8_t *payload = dr_global_alloc(payload_size);
    if (!payload)
        return false;
    store_u32(payload, ok ? 0u : 1u);
    store_u64(payload + 4u, 0u);
    store_u64(payload + 12u, message_size);
    if (message_size)
        memcpy(payload + 20u, message, message_size);
    if (ok && body_size)
        memcpy(payload + 20u, body, body_size);
    bool sent = send_frame((uint16_t)(request->opcode | QA_HOOK_REPLY), request->sequence, payload,
                           payload_size, NULL);
    dr_global_free(payload, payload_size);
    return sent;
}

static bool service_memory_request(const hook_frame *frame) {
    if (frame->opcode == QA_HOOK_READ) {
        if (frame->payload_size != 16u)
            return send_operation_reply(frame, false, NULL, 0,
                                        "instrumented read request is invalid");
        app_pc source = (app_pc)(ptr_uint_t)load_u64(frame->payload);
        uint64_t count = load_u64(frame->payload + 8u);
        if (count > QA_HOOK_MAX_FRAME - QA_HOOK_REPLY_BYTES_OVERHEAD || !hook_u64_fits_size(count))
            return send_operation_reply(frame, false, NULL, 0,
                                        "instrumented read range is too large");
        size_t body_size = 8u + (size_t)count;
        uint8_t *body = dr_global_alloc(body_size);
        if (!body)
            return false;
        store_u64(body, count);
        size_t read_count = 0;
        bool ok = dr_safe_read(source, (size_t)count, body + 8u, &read_count) &&
                  read_count == (size_t)count;
        bool sent = send_operation_reply(frame, ok, body, body_size,
                                         ok ? NULL : "instrumented native memory read failed");
        dr_global_free(body, body_size);
        return sent;
    }
    if (frame->opcode == QA_HOOK_WRITE) {
        if (frame->payload_size < 16u)
            return send_operation_reply(frame, false, NULL, 0,
                                        "instrumented write request is invalid");
        app_pc destination = (app_pc)(ptr_uint_t)load_u64(frame->payload);
        uint64_t count = load_u64(frame->payload + 8u);
        if (!hook_u64_fits_size(count) || count != (uint64_t)(frame->payload_size - 16u))
            return send_operation_reply(frame, false, NULL, 0,
                                        "instrumented write length is invalid");
        size_t written = 0;
        bool ok = dr_safe_write(destination, (size_t)count, frame->payload + 16u, &written) &&
                  written == (size_t)count;
        return send_operation_reply(frame, ok, NULL, 0,
                                    ok ? NULL : "instrumented native memory write failed");
    }
    return send_operation_reply(frame, false, NULL, 0,
                                "operation is unavailable while an inline region is suspended");
}

static void encode_state(uint8_t *bytes, const hook_state *state) {
    size_t offset = 0;
    for (size_t index = 0; index < 16u; ++index, offset += 8u)
        store_u64(bytes + offset, state->registers[index]);
    memcpy(bytes + offset, state->simd, sizeof(state->simd));
    offset += sizeof(state->simd);
    store_u64(bytes + offset, state->flags);
    store_u64(bytes + offset + 8u, state->instruction);
}

static bool decode_state(const uint8_t *bytes, size_t size, hook_state *state) {
    if (size < QA_HOOK_STATE_BYTES)
        return false;
    size_t offset = 0;
    for (size_t index = 0; index < 16u; ++index, offset += 8u)
        state->registers[index] = load_u64(bytes + offset);
    memcpy(state->simd, bytes + offset, sizeof(state->simd));
    offset += sizeof(state->simd);
    state->flags = load_u64(bytes + offset);
    state->instruction = load_u64(bytes + offset + 8u);
    return true;
}

static bool region_request(uint32_t id, uint32_t phase, const hook_state *state, uint32_t *action,
                           bool *replace_state, hook_state *replacement) {
    uint8_t payload[8u + QA_HOOK_STATE_BYTES];
    store_u32(payload, id);
    store_u32(payload + 4u, phase);
    encode_state(payload + 8u, state);
    if (call_depth == UINT32_MAX)
        return false;
    ++call_depth;
    uint64_t request_sequence;
    if (!send_frame(QA_HOOK_REGION, 0, payload, sizeof(payload), &request_sequence)) {
        --call_depth;
        return false;
    }
    for (;;) {
        hook_frame frame = {0};
        if (!receive_frame(&frame)) {
            --call_depth;
            return false;
        }
        bool reply =
            frame.opcode == (QA_HOOK_REGION | QA_HOOK_REPLY) && frame.reply_to == request_sequence;
        bool request = !(frame.opcode & QA_HOOK_REPLY) && !frame.reply_to;
        if ((reply && frame.depth != call_depth) ||
            (request && (call_depth == UINT32_MAX || frame.depth != call_depth + 1u))) {
            free_frame(&frame);
            --call_depth;
            return false;
        }
        if (reply) {
            if (frame.payload_size < 20u) {
                free_frame(&frame);
                --call_depth;
                return false;
            }
            uint32_t status = load_u32(frame.payload);
            uint64_t message_size = load_u64(frame.payload + 12u);
            size_t offset = 20u;
            if (message_size > frame.payload_size - offset) {
                free_frame(&frame);
                --call_depth;
                return false;
            }
            offset += (size_t)message_size;
            bool ok = status == 0u && frame.payload_size - offset == 5u + QA_HOOK_STATE_BYTES;
            if (ok) {
                *action = load_u32(frame.payload + offset);
                *replace_state = frame.payload[offset + 4u] != 0;
                ok = *action <= 3u && decode_state(frame.payload + offset + 5u,
                                                   frame.payload_size - offset - 5u, replacement);
            }
            free_frame(&frame);
            --call_depth;
            return ok;
        }
        bool serviced = request && service_memory_request(&frame);
        free_frame(&frame);
        if (!serviced) {
            --call_depth;
            return false;
        }
    }
}

static void state_from_context(const dr_mcontext_t *context, hook_state *state) {
    state->registers[0] = context->xax;
    state->registers[1] = context->xcx;
    state->registers[2] = context->xdx;
    state->registers[3] = context->xbx;
    state->registers[4] = context->xsp;
    state->registers[5] = context->xbp;
    state->registers[6] = context->xsi;
    state->registers[7] = context->xdi;
#ifdef X64
    state->registers[8] = context->r8;
    state->registers[9] = context->r9;
    state->registers[10] = context->r10;
    state->registers[11] = context->r11;
    state->registers[12] = context->r12;
    state->registers[13] = context->r13;
    state->registers[14] = context->r14;
    state->registers[15] = context->r15;
#endif
    if (dr_mcontext_xmm_fields_valid())
        for (size_t index = 0; index < QA_HOOK_SIMD_COUNT; ++index)
            memcpy(state->simd[index], &context->simd[index], 16u);
    state->flags = context->xflags;
    state->instruction = (uint64_t)(ptr_uint_t)context->xip;
}

static void state_to_context(const hook_state *state, dr_mcontext_t *context) {
    context->xax = (reg_t)state->registers[0];
    context->xcx = (reg_t)state->registers[1];
    context->xdx = (reg_t)state->registers[2];
    context->xbx = (reg_t)state->registers[3];
    context->xsp = (reg_t)state->registers[4];
    context->xbp = (reg_t)state->registers[5];
    context->xsi = (reg_t)state->registers[6];
    context->xdi = (reg_t)state->registers[7];
#ifdef X64
    context->r8 = (reg_t)state->registers[8];
    context->r9 = (reg_t)state->registers[9];
    context->r10 = (reg_t)state->registers[10];
    context->r11 = (reg_t)state->registers[11];
    context->r12 = (reg_t)state->registers[12];
    context->r13 = (reg_t)state->registers[13];
    context->r14 = (reg_t)state->registers[14];
    context->r15 = (reg_t)state->registers[15];
#endif
    if (dr_mcontext_xmm_fields_valid())
        for (size_t index = 0; index < QA_HOOK_SIMD_COUNT; ++index)
            memcpy(&context->simd[index], state->simd[index], 16u);
    context->xflags = (reg_t)state->flags;
}

static void region_clean_call(ptr_uint_t encoded_id, ptr_uint_t encoded_phase) {
    byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
    byte *fp_state = (byte *)(((ptr_uint_t)fp_raw + DR_FPSTATE_ALIGN - 1u) &
                              ~((ptr_uint_t)DR_FPSTATE_ALIGN - 1u));
    if (!proc_save_fpstate(fp_state))
        dr_abort();
    uint32_t id = (uint32_t)encoded_id;
    uint32_t phase = (uint32_t)encoded_phase;
    if (!module_base || id >= region_count || phase > 1u)
        dr_abort();
    void *drcontext = dr_get_current_drcontext();
    if (dr_get_thread_id(drcontext) != owner_thread)
        dr_abort();
    dr_mcontext_t context = {.size = sizeof(context), .flags = DR_MC_ALL};
    if (!dr_get_mcontext(drcontext, &context))
        dr_abort();
    hook_state before = {0}, replacement = {0};
    state_from_context(&context, &before);
    before.instruction =
        (uint64_t)(ptr_uint_t)(module_base + (phase == 0u ? regions[id].entry : regions[id].join));
    uint32_t action;
    bool replace;
    if (!region_request(id, phase, &before, &action, &replace, &replacement))
        dr_abort();
    if (replace)
        state_to_context(&replacement, &context);
    app_pc redirect = NULL;
    if (action == 1u && phase == 0u)
        redirect = module_base + regions[id].join;
    else if (action == 2u && regions[id].has_frame)
        redirect = module_base + regions[id].frame_exit;
    else if (action == 3u)
        dr_abort();
    else if (action != 0u)
        dr_abort();
    if (redirect) {
        context.xip = redirect;
        proc_restore_fpstate(fp_state);
        if (!dr_redirect_execution(&context))
            dr_abort();
    } else {
        if (replace && !dr_set_mcontext(drcontext, &context))
            dr_abort();
        proc_restore_fpstate(fp_state);
    }
}

static dr_emit_flags_t instrument_instruction(void *drcontext, void *tag, instrlist_t *list,
                                              instr_t *instruction, bool for_trace,
                                              bool translating, void *user_data) {
    (void)tag;
    (void)for_trace;
    (void)translating;
    (void)user_data;
    if (!module_base || !instr_is_app(instruction))
        return DR_EMIT_DEFAULT;
    app_pc pc = instr_get_app_pc(instruction);
    for (size_t index = 0; index < region_count; ++index) {
        uint32_t phase;
        if (pc == module_base + regions[index].entry)
            phase = 0u;
        else if (pc == module_base + regions[index].join)
            phase = 1u;
        else
            continue;
        dr_insert_clean_call_ex(drcontext, list, instruction, (void *)region_clean_call,
                                DR_CLEANCALL_READS_APP_CONTEXT | DR_CLEANCALL_WRITES_APP_CONTEXT, 2,
                                OPND_CREATE_INTPTR(index), OPND_CREATE_INTPTR(phase));
    }
    return DR_EMIT_DEFAULT;
}

static bool executable_address(app_pc address) {
    byte *base;
    size_t size;
    uint protection;
    return dr_query_memory(address, &base, &size, &protection) &&
           (protection & DR_MEMPROT_EXEC) != 0;
}

static bool decodes_to(app_pc entry, app_pc join) {
    if (entry >= join)
        return true;
    void *drcontext = dr_get_current_drcontext();
    app_pc cursor = entry;
    size_t instructions = 0;
    while (cursor < join && instructions++ < 1048576u) {
        instr_t instruction;
        instr_init(drcontext, &instruction);
        app_pc next = decode(drcontext, cursor, &instruction);
        instr_free(drcontext, &instruction);
        if (!next || next <= cursor)
            return false;
        cursor = next;
    }
    return cursor == join;
}

static void module_load(void *drcontext, const module_data_t *module, bool loaded) {
    (void)loaded;
    if (module_base)
        return;
    const char *preferred = dr_module_preferred_name(module);
    if (!preferred || dr_strcasecmp(preferred, module_name))
        return;
    if ((uint64_t)(module->end - module->start) < module_image_bytes)
        dr_abort();
    for (size_t index = 0; index < region_count; ++index) {
        app_pc entry = module->start + regions[index].entry;
        app_pc join = module->start + regions[index].join;
        if (entry >= module->end || join >= module->end || !executable_address(entry) ||
            !executable_address(join) || !decodes_to(entry, join))
            dr_abort();
        if (regions[index].has_frame) {
            app_pc frame_entry = module->start + regions[index].frame_entry;
            app_pc frame_exit = module->start + regions[index].frame_exit;
            if (frame_entry >= module->end || frame_exit >= module->end ||
                !executable_address(frame_entry) || !executable_address(frame_exit))
                dr_abort();
        }
    }
    module_base = module->start;
    owner_thread = dr_get_thread_id(drcontext);
}

static void module_unload(void *drcontext, const module_data_t *module) {
    (void)drcontext;
    if (module->start == module_base)
        module_base = NULL;
}

static bool read_descriptor(const char *path) {
    file_t file = dr_open_file(path, DR_FILE_READ);
    if (file == INVALID_FILE)
        return false;
    uint64_t file_size;
    bool ok = dr_file_size(file, &file_size) && file_size >= 64u &&
              file_size <= QA_HOOK_MAX_FRAME && hook_u64_fits_size(file_size);
    uint8_t *bytes = ok ? dr_global_alloc((size_t)file_size) : NULL;
    if (!bytes)
        ok = false;
    if (ok)
        ok = read_exact(file, bytes, (size_t)file_size);
    dr_close_file(file);
    if (!ok) {
        if (bytes)
            dr_global_free(bytes, (size_t)file_size);
        return false;
    }
    uint32_t count = load_u32(bytes + 48u);
    uint32_t pointer_bytes = load_u32(bytes + 52u);
    uint64_t records_size = (uint64_t)count * 24u;
    if (memcmp(bytes, "QANHOOK\0", 8u) || load_u32(bytes + 8u) != 1u ||
        load_u32(bytes + 12u) > 1u || pointer_bytes != sizeof(void *) ||
        records_size > file_size - 64u || file_size - 64u - records_size == 0u) {
        dr_global_free(bytes, (size_t)file_size);
        return false;
    }
    regions = dr_global_alloc((size_t)count * sizeof(*regions));
    size_t name_size = (size_t)(file_size - 64u - records_size);
    module_name = dr_global_alloc(name_size + 1u);
    if ((!regions && count) || !module_name) {
        if (regions)
            dr_global_free(regions, (size_t)count * sizeof(*regions));
        dr_global_free(bytes, (size_t)file_size);
        return false;
    }
    for (uint32_t index = 0; index < count; ++index) {
        const uint8_t *record = bytes + 64u + (size_t)index * 24u;
        regions[index] =
            (hook_region){load_u32(record),       load_u32(record + 4u),  load_u32(record + 8u),
                          load_u32(record + 12u), load_u32(record + 16u), load_u32(record + 20u)};
        if (regions[index].id != index || regions[index].entry == regions[index].join ||
            regions[index].has_frame > 1u) {
            dr_global_free(regions, (size_t)count * sizeof(*regions));
            dr_global_free(module_name, name_size + 1u);
            dr_global_free(bytes, (size_t)file_size);
            regions = NULL;
            module_name = NULL;
            return false;
        }
    }
    memcpy(module_name, bytes + 64u + (size_t)records_size, name_size);
    module_name[name_size] = 0;
    module_image_bytes = load_u64(bytes + 56u);
    region_count = count;
    dr_global_free(bytes, (size_t)file_size);
    return true;
}

static void client_exit(void) {
    drmgr_unregister_bb_insertion_event(instrument_instruction);
    drmgr_unregister_module_load_event(module_load);
    drmgr_unregister_module_unload_event(module_unload);
    drmgr_exit();
    if (regions)
        dr_global_free(regions, region_count * sizeof(*regions));
    if (module_name)
        dr_global_free(module_name, strlen(module_name) + 1u);
}

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    (void)id;
    dr_set_client_name("Quake Anthology native region client", "");
    hook_input = dr_get_stdin_file();
    hook_output = dr_get_stdout_file();
    if (argc < 1 || !read_descriptor(argv[argc - 1]) || !drmgr_init() ||
        !drmgr_register_module_load_event(module_load) ||
        !drmgr_register_module_unload_event(module_unload) ||
        !drmgr_register_bb_instrumentation_event(NULL, instrument_instruction, NULL))
        dr_abort();
    dr_register_exit_event(client_exit);
}

#else

int qa_native_instrument_client_requires_dynamorio;

#endif
