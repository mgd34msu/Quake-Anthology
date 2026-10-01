/* Included by instrument_client.c; this implementation uses the client's
 * existing framed transport and checked-memory service while source code is
 * suspended. Entry replacements run in application code, never on this stack. */
typedef struct hook_entry {
    struct hook_entry *next;
    uint64_t id;
    app_pc address, replacement;
} hook_entry;

typedef struct hook_watch {
    struct hook_watch *next;
    uint64_t id;
    app_pc address;
    size_t size;
} hook_watch;

typedef struct hook_pending {
    struct hook_pending *next;
    uint64_t id;
    app_pc instruction, address;
    size_t watch_size, offset, size;
    uint8_t *before, *after;
} hook_pending;

typedef struct hook_instruction {
    struct hook_instruction *next;
    app_pc pc;
    bool predicated;
    instr_noalloc_t storage;
} hook_instruction;

static hook_entry *observed_entries;
static hook_watch *observed_watches;
static hook_pending *pending_writes;
static hook_instruction *observed_instructions[1024];
static app_pc control_entry;
static uint64_t bypass_entry;
static void *instruction_lock;

static bool executable_address(app_pc address);

static hook_entry *observer_entry(uint64_t id) {
    hook_entry *entry = observed_entries;
    while (entry && entry->id != id)
        entry = entry->next;
    return entry;
}

static void observer_pending_free(hook_pending *pending) {
    dr_global_free(pending->before, pending->watch_size);
    if (pending->after)
        dr_global_free(pending->after, pending->watch_size);
    dr_global_free(pending, sizeof(*pending));
}

static bool observer_control(native_hook_control *control) {
    if (control->magic != NATIVE_HOOK_CONTROL_MAGIC)
        return false;
    if (control->operation == NATIVE_HOOK_DEPTH) {
        if (control->size > UINT32_MAX)
            return false;
        call_depth = (uint32_t)control->size;
        return true;
    }
    if (control->operation == NATIVE_HOOK_REGION_SERVICE)
        return region_service_qualified(control);
    if (control->operation == NATIVE_HOOK_IMAGE) {
        if (!module_base || !module_generation || region_frames || region_bypass_pc ||
            observed_entries || observed_watches || pending_writes || bypass_entry)
            return false;
        control->id = module_generation;
        control->address = (uint64_t)(ptr_uint_t)module_base;
        control->size = module_image_bytes;
        return true;
    }
    if (control->operation == NATIVE_HOOK_ENTRY_ADD) {
        app_pc address = (app_pc)(ptr_uint_t)control->address;
        app_pc replacement = (app_pc)(ptr_uint_t)control->replacement;
        if (!module_base || !control->id || observer_entry(control->id) ||
            (uint64_t)(ptr_uint_t)address != control->address ||
            (uint64_t)(ptr_uint_t)replacement != control->replacement ||
            address < module_base || (uint64_t)(address - module_base) >= module_image_bytes ||
            !executable_address(address) || !executable_address(replacement))
            return false;
        for (hook_entry *entry = observed_entries; entry; entry = entry->next)
            if (entry->address == address)
                return false;
        hook_entry *entry = dr_global_alloc(sizeof(*entry));
        if (!entry)
            return false;
        *entry = (hook_entry){observed_entries, control->id, address, replacement};
        observed_entries = entry;
        return true;
    }
    if (control->operation == NATIVE_HOOK_ENTRY_REMOVE) {
        hook_entry **cursor = &observed_entries;
        while (*cursor && (*cursor)->id != control->id)
            cursor = &(*cursor)->next;
        if (!*cursor || bypass_entry == control->id)
            return false;
        hook_entry *entry = *cursor;
        *cursor = entry->next;
        dr_global_free(entry, sizeof(*entry));
        return true;
    }
    if (control->operation == NATIVE_HOOK_BYPASS_ARM) {
        if (bypass_entry || !observer_entry(control->id))
            return false;
        bypass_entry = control->id;
        return true;
    }
    if (control->operation == NATIVE_HOOK_BYPASS_CLEAR) {
        bool consumed = !bypass_entry;
        bypass_entry = 0;
        return consumed;
    }
    if (control->operation == NATIVE_HOOK_WATCH_ADD) {
        app_pc address = (app_pc)(ptr_uint_t)control->address;
        if (!control->id || !address || !control->size ||
            control->size > NATIVE_HOOK_MAX_WATCH_BYTES ||
            (uint64_t)(ptr_uint_t)address != control->address ||
            control->size > (uint64_t)(ptr_uint_t)-1 - control->address)
            return false;
        for (hook_watch *watch = observed_watches; watch; watch = watch->next)
            if (watch->id == control->id)
                return false;
        uint8_t probe;
        size_t read_count;
        if (!dr_safe_read(address, 1u, &probe, &read_count) || read_count != 1u ||
            !dr_safe_read(address + (size_t)control->size - 1u, 1u, &probe, &read_count) ||
            read_count != 1u)
            return false;
        hook_watch *watch = dr_global_alloc(sizeof(*watch));
        if (!watch)
            return false;
        *watch = (hook_watch){observed_watches, control->id, address, (size_t)control->size};
        observed_watches = watch;
        return true;
    }
    if (control->operation == NATIVE_HOOK_WATCH_REMOVE) {
        hook_watch **cursor = &observed_watches;
        while (*cursor && (*cursor)->id != control->id)
            cursor = &(*cursor)->next;
        if (!*cursor)
            return false;
        hook_watch *watch = *cursor;
        *cursor = watch->next;
        dr_global_free(watch, sizeof(*watch));
        hook_pending **pending_cursor = &pending_writes;
        while (*pending_cursor) {
            hook_pending *pending = *pending_cursor;
            if (pending->id != control->id) {
                pending_cursor = &pending->next;
                continue;
            }
            *pending_cursor = pending->next;
            observer_pending_free(pending);
        }
        return true;
    }
    return false;
}

static bool observer_write_request(const hook_pending *pending, const uint8_t *after) {
    size_t payload_size = 48u + pending->watch_size * 2u;
    uint8_t *payload = dr_global_alloc(payload_size);
    if (!payload || call_depth == UINT32_MAX) {
        if (payload)
            dr_global_free(payload, payload_size);
        return false;
    }
    store_u64(payload, pending->id);
    store_u64(payload + 8u, (uint64_t)(ptr_uint_t)pending->instruction);
    store_u64(payload + 16u, pending->offset);
    store_u64(payload + 24u, pending->size);
    store_u64(payload + 32u, pending->watch_size);
    memcpy(payload + 40u, pending->before, pending->watch_size);
    store_u64(payload + 40u + pending->watch_size, pending->watch_size);
    memcpy(payload + 48u + pending->watch_size, after, pending->watch_size);
    ++call_depth;
    uint64_t request_sequence;
    bool ok = send_frame(NATIVE_WIRE_OBSERVER_WRITE, 0, payload, payload_size, &request_sequence);
    dr_global_free(payload, payload_size);
    while (ok) {
        hook_frame frame = {0};
        if (!receive_frame(&frame)) {
            ok = false;
            break;
        }
        bool reply = frame.opcode == (NATIVE_WIRE_OBSERVER_WRITE | NATIVE_WIRE_REPLY) &&
                     frame.reply_to == request_sequence;
        bool request = !(frame.opcode & NATIVE_WIRE_REPLY) && !frame.reply_to;
        if ((reply && frame.depth != call_depth) ||
            (request && (call_depth == UINT32_MAX || frame.depth != call_depth + 1u))) {
            free_frame(&frame);
            ok = false;
            break;
        }
        if (reply) {
            ok = frame.payload_size == 20u && load_u32(frame.payload) == 0u &&
                 load_u64(frame.payload + 12u) == 0u;
            free_frame(&frame);
            break;
        }
        ok = request && service_memory_request(&frame);
        free_frame(&frame);
    }
    --call_depth;
    return ok;
}

static void observer_flush(void) {
    /* Snapshot every range before any host repair can affect another observer
     * for the same source instruction. Subscription removal may then cancel an
     * undelivered range without invalidating the currently dispatched event. */
    for (hook_pending *pending = pending_writes; pending; pending = pending->next) {
        pending->after = dr_global_alloc(pending->watch_size);
        size_t read_count;
        if (!pending->after ||
            !dr_safe_read(pending->address, pending->watch_size, pending->after, &read_count) ||
            read_count != pending->watch_size)
            dr_abort();
    }
    while (pending_writes) {
        hook_pending *pending = pending_writes;
        pending_writes = pending->next;
        if (!observer_write_request(pending, pending->after))
            dr_abort();
        observer_pending_free(pending);
    }
}

static void observer_capture(hook_instruction *description, dr_mcontext_t *context) {
    instr_t *instruction = instr_from_noalloc(&description->storage);
    for (uint index = 0;; ++index) {
        app_pc address;
        bool write;
        uint position;
        if (!instr_compute_address_ex_pos(instruction, context, index, &address, &write, &position))
            break;
        if (!write)
            continue;
        opnd_t operand = instr_get_dst(instruction, (int)position);
        size_t size = drutil_opnd_mem_size_in_bytes(operand, instruction);
        if (!size || size > (ptr_uint_t)-1 - (ptr_uint_t)address)
            dr_abort();
        ptr_uint_t first_address = (ptr_uint_t)address;
        ptr_uint_t end = first_address + size;
        for (hook_watch *watch = observed_watches; watch; watch = watch->next) {
            ptr_uint_t watch_address = (ptr_uint_t)watch->address;
            ptr_uint_t watch_end = watch_address + watch->size;
            if (first_address >= watch_end || end <= watch_address)
                continue;
            if (description->predicated)
                dr_abort();
            ptr_uint_t first = first_address > watch_address ? first_address : watch_address;
            ptr_uint_t last = end < watch_end ? end : watch_end;
            hook_pending *pending = dr_global_alloc(sizeof(*pending));
            if (!pending)
                dr_abort();
            *pending = (hook_pending){.id = watch->id, .instruction = description->pc,
                                      .address = watch->address, .watch_size = watch->size,
                                      .offset = (size_t)(first - watch_address),
                                      .size = (size_t)(last - first)};
            pending->before = dr_global_alloc(watch->size);
            size_t read_count;
            if (!pending->before ||
                !dr_safe_read(watch->address, watch->size, pending->before, &read_count) ||
                read_count != watch->size)
                dr_abort();
            hook_pending **tail = &pending_writes;
            while (*tail)
                tail = &(*tail)->next;
            *tail = pending;
        }
    }
}

static void observer_instruction_clean(ptr_uint_t encoded_pc, ptr_uint_t encoded_write) {
    (void)encoded_write;
    app_pc pc = (app_pc)encoded_pc;
    if (!observed_entries && !observed_watches && pc != control_entry)
        return;
    byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
    byte *fp_state = (byte *)(((ptr_uint_t)fp_raw + DR_FPSTATE_ALIGN - 1u) &
                              ~((ptr_uint_t)DR_FPSTATE_ALIGN - 1u));
    if (!proc_save_fpstate(fp_state))
        dr_abort();
    void *drcontext = dr_get_current_drcontext();
    if (dr_get_thread_id(drcontext) != owner_thread)
        dr_abort();
    observer_flush();
    /* A CONTINUE redirects to the exact interrupted PC. Its entry observers
     * already ran before the region service; do not intercept them twice. */
    if (region_bypass_pc == pc) {
        proc_restore_fpstate(fp_state);
        return;
    }
    dr_mcontext_t context = {.size = sizeof(context), .flags = DR_MC_ALL};
    if (!dr_get_mcontext(drcontext, &context))
        dr_abort();
    if (pc == control_entry) {
        native_hook_control *address;
#ifdef X64
#ifdef WINDOWS
        address = (native_hook_control *)context.xcx;
#else
        address = (native_hook_control *)context.xdi;
#endif
#else
        size_t pointer_read;
        if (!dr_safe_read((void *)(context.xsp + sizeof(void *)), sizeof(address), &address,
                          &pointer_read) || pointer_read != sizeof(address))
            dr_abort();
#endif
        native_hook_control control;
        size_t read_count;
        if (!dr_safe_read(address, sizeof(control), &control, &read_count) ||
            read_count != sizeof(control))
            dr_abort();
        control.status = observer_control(&control) ? 1u : 0u;
        size_t written;
        if (!dr_safe_write(address, sizeof(control), &control, &written) || written != sizeof(control))
            dr_abort();
        proc_restore_fpstate(fp_state);
        return;
    }
    for (hook_entry *entry = observed_entries; entry; entry = entry->next) {
        if (pc != entry->address)
            continue;
        if (bypass_entry == entry->id) {
            bypass_entry = 0;
            break;
        }
        context.xip = entry->replacement;
        proc_restore_fpstate(fp_state);
        if (!dr_redirect_execution(&context))
            dr_abort();
    }
    proc_restore_fpstate(fp_state);
}

static void observer_capture_clean(ptr_uint_t encoded_write) {
    if (!observed_watches)
        return;
    void *drcontext = dr_get_current_drcontext();
    if (dr_get_thread_id(drcontext) != owner_thread)
        dr_abort();
    dr_mcontext_t context = {.size = sizeof(context), .flags = DR_MC_ALL};
    if (!dr_get_mcontext(drcontext, &context))
        dr_abort();
    observer_capture((hook_instruction *)encoded_write, &context);
}

static bool observer_same_operands(instr_t *left, instr_t *right) {
    if (instr_get_opcode(left) != instr_get_opcode(right) ||
        instr_get_x86_mode(left) != instr_get_x86_mode(right) ||
        instr_num_srcs(left) != instr_num_srcs(right) ||
        instr_num_dsts(left) != instr_num_dsts(right))
        return false;
    for (int index = 0; index < instr_num_srcs(left); ++index)
        if (!opnd_same(instr_get_src(left, index), instr_get_src(right, index)))
            return false;
    for (int index = 0; index < instr_num_dsts(left); ++index)
        if (!opnd_same(instr_get_dst(left, index), instr_get_dst(right, index)))
            return false;
    return true;
}

static hook_instruction *observer_instruction(void *drcontext, instr_t *source, app_pc pc) {
    dr_mutex_lock(instruction_lock);
    size_t bucket = ((ptr_uint_t)pc >> 2u) % 1024u;
    for (hook_instruction *item = observed_instructions[bucket]; item; item = item->next) {
        instr_t *instruction = instr_from_noalloc(&item->storage);
        if (item->pc == pc && item->predicated ==
            (instr_is_predicated(source) || instr_is_scatter(source)) &&
            observer_same_operands(instruction, source)) {
            dr_mutex_unlock(instruction_lock);
            return item;
        }
    }
    hook_instruction *item = dr_global_alloc(sizeof(*item));
    if (!item)
        dr_abort();
    memset(item, 0, sizeof(*item));
    item->pc = pc;
    item->predicated = instr_is_predicated(source) || instr_is_scatter(source);
    instr_noalloc_init(drcontext, &item->storage);
    instr_t *instruction = instr_from_noalloc(&item->storage);
    instr_set_opcode(instruction, instr_get_opcode(source));
    instr_set_num_opnds(drcontext, instruction, instr_num_dsts(source), instr_num_srcs(source));
    for (int index = 0; index < instr_num_dsts(source); ++index)
        instr_set_dst(instruction, index, instr_get_dst(source, index));
    for (int index = 0; index < instr_num_srcs(source); ++index)
        instr_set_src(instruction, index, instr_get_src(source, index));
    instr_set_x86_mode(instruction, instr_get_x86_mode(source));
    item->next = observed_instructions[bucket];
    observed_instructions[bucket] = item;
    dr_mutex_unlock(instruction_lock);
    return item;
}

static dr_emit_flags_t observer_app2app(void *drcontext, void *tag, instrlist_t *list,
                                       bool for_trace, bool translating) {
    (void)tag;
    (void)for_trace;
    (void)translating;
    if (!drutil_expand_rep_string(drcontext, list))
        dr_abort();
    return DR_EMIT_DEFAULT;
}

static void observer_module_unload(void) {
    module_base = NULL;
    bypass_entry = 0;
    while (observed_entries) {
        hook_entry *entry = observed_entries;
        observed_entries = entry->next;
        dr_global_free(entry, sizeof(*entry));
    }
    while (observed_watches) {
        hook_watch *watch = observed_watches;
        observed_watches = watch->next;
        dr_global_free(watch, sizeof(*watch));
    }
    while (pending_writes) {
        hook_pending *pending = pending_writes;
        pending_writes = pending->next;
        observer_pending_free(pending);
    }
}

static void observer_destroy(void) {
    observer_module_unload();
    for (size_t index = 0; index < 1024u; ++index)
        while (observed_instructions[index]) {
            hook_instruction *instruction = observed_instructions[index];
            observed_instructions[index] = instruction->next;
            dr_global_free(instruction, sizeof(*instruction));
        }
    dr_mutex_destroy(instruction_lock);
}

#ifdef UNIX
static dr_signal_action_t observer_signal(void *drcontext, dr_siginfo_t *information) {
    (void)drcontext;
    (void)information;
    /* A fault or asynchronous transfer between a store and its next boundary
     * cannot be reported as a committed store. This backend fails closed. */
    if (pending_writes)
        dr_abort();
    return DR_SIGNAL_DELIVER;
}
#else
static bool observer_exception(void *drcontext, dr_exception_t *information) {
    (void)drcontext;
    (void)information;
    if (pending_writes)
        dr_abort();
    return true;
}
#endif
