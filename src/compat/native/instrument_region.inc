/* Included after state conversion helpers. Service code executes in the runner
 * application, with an owned DR context retained until the resume marker. */
typedef struct hook_region_frame {
    struct hook_region_frame *previous;
    dr_mcontext_t context;
    native_region_service packet;
    uint64_t cookie;
    uint32_t id, phase, depth;
    byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
} hook_region_frame;

static hook_region_frame *region_frames;
static uint32_t region_frame_count, region_bypass_through;
static uint64_t region_cookie;
static app_pc region_service_entry, region_resume_entry, region_bypass_pc;
static void observer_flush(void);

static bool region_service_qualified(const native_hook_control *control) {
    return region_frames && control->address == (uint64_t)(ptr_uint_t)&region_frames->packet &&
           control->id == region_frames->cookie && control->size == region_frames->depth &&
           call_depth == region_frames->depth &&
           region_frames->packet.magic == NATIVE_REGION_SERVICE_MAGIC &&
           region_frames->packet.id == region_frames->id &&
           region_frames->packet.phase == region_frames->phase &&
           region_frames->packet.depth == region_frames->depth &&
           !region_frames->packet.completed;
}

static byte *region_fp(hook_region_frame *frame) {
    return (byte *)(((ptr_uint_t)frame->fp_raw + DR_FPSTATE_ALIGN - 1u) &
                    ~((ptr_uint_t)DR_FPSTATE_ALIGN - 1u));
}

static bool region_helper_stack(dr_mcontext_t *context, native_region_service *packet) {
    ptr_uint_t stack = (ptr_uint_t)context->xsp;
    ptr_uint_t words[2] = {0, (ptr_uint_t)packet};
    size_t written;
#ifdef X64
#ifdef WINDOWS
    if (stack < 56u) return false;
    stack = (stack & ~(ptr_uint_t)15u) - 40u;
    byte shadow[40] = {0};
    if (!dr_safe_write((void *)stack, sizeof(shadow), shadow, &written) ||
        written != sizeof(shadow)) return false;
    context->xcx = (reg_t)(ptr_uint_t)packet;
#else
    if (stack < 152u) return false;
    stack = ((stack - 128u) & ~(ptr_uint_t)15u) - 8u;
    if (!dr_safe_write((void *)stack, sizeof(words[0]), words, &written) ||
        written != sizeof(words[0])) return false;
    context->xdi = (reg_t)(ptr_uint_t)packet;
#endif
#else
    if (stack < 36u) return false;
    stack = (stack & ~(ptr_uint_t)15u) - 20u;
    if (!dr_safe_write((void *)stack, sizeof(words), words, &written) ||
        written != sizeof(words)) return false;
#endif
    context->xsp = (reg_t)stack;
    context->xip = region_service_entry;
    /* Enter a C function with the ABI's clear direction flag. The interrupted
     * flags remain in the retained context and are restored at resume. */
    context->xflags &= ~(reg_t)0x400u;
    return true;
}

static void region_clean_call(ptr_uint_t encoded_id, ptr_uint_t encoded_phase) {
    byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
    byte *fp = (byte *)(((ptr_uint_t)fp_raw + DR_FPSTATE_ALIGN - 1u) &
                       ~((ptr_uint_t)DR_FPSTATE_ALIGN - 1u));
    if (!proc_save_fpstate(fp)) dr_abort();
    uint32_t id = (uint32_t)encoded_id, phase = (uint32_t)encoded_phase;
    if (!module_base || id >= region_count || phase > 1u ||
        !region_service_entry || !region_resume_entry) dr_abort();
    app_pc pc = module_base + (phase ? regions[id].join : regions[id].entry);
    if (region_bypass_pc == pc && id <= region_bypass_through) {
        if (id == region_bypass_through) region_bypass_pc = NULL;
        proc_restore_fpstate(fp);
        return;
    }
    void *drcontext = dr_get_current_drcontext();
    if (dr_get_thread_id(drcontext) != owner_thread ||
        region_frame_count == NATIVE_REGION_SERVICE_LIMIT || region_cookie == UINT64_MAX)
        dr_abort();
    hook_region_frame *frame = dr_global_alloc(sizeof(*frame));
    if (!frame) dr_abort();
    memset(frame, 0, sizeof(*frame));
    frame->context.size = sizeof(frame->context);
    frame->context.flags = DR_MC_ALL;
    memcpy(region_fp(frame), fp, DR_FPSTATE_BUF_SIZE);
    if (!dr_get_mcontext(drcontext, &frame->context)) dr_abort();
    frame->context.xip = pc;
    frame->packet.magic = NATIVE_REGION_SERVICE_MAGIC;
    frame->packet.cookie = ++region_cookie;
    frame->packet.id = id;
    frame->packet.phase = phase;
    frame->packet.depth = call_depth;
    frame->cookie = frame->packet.cookie;
    frame->id = id;
    frame->phase = phase;
    frame->depth = call_depth;
    state_from_context(&frame->context, &frame->packet.before);
    frame->previous = region_frames;
    dr_mcontext_t helper = frame->context;
    if (!region_helper_stack(&helper, &frame->packet)) dr_abort();
    region_frames = frame;
    ++region_frame_count;
    proc_restore_fpstate(region_fp(frame));
    if (!dr_redirect_execution(&helper)) dr_abort();
}

static void region_resume_clean(void) {
    void *drcontext = dr_get_current_drcontext();
    if (dr_get_thread_id(drcontext) != owner_thread || !region_frames || !region_frame_count)
        dr_abort();
    /* Flush the last nested source write before restoring the interrupted
     * instruction. This also keeps committed-write ordering on helper exit. */
    observer_flush();
    dr_mcontext_t current = {.size = sizeof(current), .flags = DR_MC_ALL};
    if (!dr_get_mcontext(drcontext, &current)) dr_abort();
    native_region_service *address = NULL;
#ifdef X64
#ifdef WINDOWS
    address = (native_region_service *)current.xcx;
#else
    address = (native_region_service *)current.xdi;
#endif
#else
    size_t read_count;
    if (!dr_safe_read((void *)(current.xsp + sizeof(void *)), sizeof(address), &address,
                      &read_count) || read_count != sizeof(address)) dr_abort();
#endif
    hook_region_frame *frame = region_frames;
    if (address != &frame->packet || frame->packet.magic != NATIVE_REGION_SERVICE_MAGIC ||
        frame->packet.cookie != frame->cookie || frame->packet.completed != 1u ||
        frame->packet.replace_state > 1u || frame->packet.id != frame->id ||
        frame->packet.phase != frame->phase || frame->packet.depth != frame->depth ||
        frame->depth != call_depth || frame->id >= region_count || frame->phase > 1u) dr_abort();
    dr_mcontext_t restored = frame->context;
    if (frame->packet.replace_state) state_to_context(&frame->packet.after, &restored);
    uint32_t id = frame->packet.id, phase = frame->packet.phase, action = frame->packet.action;
    if (action == 0u) {
        region_bypass_pc = restored.xip;
        region_bypass_through = id;
    } else if (action == 1u && !phase) restored.xip = module_base + regions[id].join;
    else if (action == 2u && regions[id].has_frame) restored.xip = module_base + regions[id].frame_exit;
    else dr_abort();
    byte fp_raw[DR_FPSTATE_BUF_SIZE + DR_FPSTATE_ALIGN];
    byte *fp = (byte *)(((ptr_uint_t)fp_raw + DR_FPSTATE_ALIGN - 1u) &
                       ~((ptr_uint_t)DR_FPSTATE_ALIGN - 1u));
    memcpy(fp, region_fp(frame), DR_FPSTATE_BUF_SIZE);
    region_frames = frame->previous;
    --region_frame_count;
    dr_global_free(frame, sizeof(*frame));
    proc_restore_fpstate(fp);
    if (!dr_redirect_execution(&restored)) dr_abort();
}

static void region_frames_destroy(void) {
    while (region_frames) {
        hook_region_frame *frame = region_frames;
        region_frames = frame->previous;
        dr_global_free(frame, sizeof(*frame));
    }
    region_frame_count = 0;
}
