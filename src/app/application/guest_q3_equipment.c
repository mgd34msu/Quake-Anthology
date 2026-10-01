#include "guest_q3_equipment.h"
#include "guest_q3_private.h"

typedef struct equipment_scope {
    struct equipment_scope *previous;
    qa_qvm_source_frame frame;
    uint32_t gun;
    void *token;
} equipment_scope;
typedef struct equipment_hook {
    struct application_q3_equipment *owner;
    qa_qvm_binding binding;
    uint32_t instruction;
    size_t status;
    qa_qvm_function_hook function;
} equipment_hook;
struct application_q3_equipment {
    application_q3_equipment_module module;
    const application_q3_equipment_profile *profile;
    application_q3_equipment_services services;
    application_q3_equipment_draw draw;
    equipment_hook *hooks;
    size_t hook_count;
    equipment_scope *scopes;
    bool drawing, hud_requested, view_requested;
};

static bool selected(application_q3_equipment *owner, bool *out, qa_error *error)
{
    *out = false;
    if (!owner->drawing) return true;
    if (!owner->services.current(owner->services.context, &owner->draw))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment draw lost its actual selected actor/media owner");
    *out = owner->draw.selected;
    return true;
}

static bool hud(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    equipment_hook *hook = context;
    bool active;
    if (!selected(hook->owner, &active, error)) return false;
    if (!active) return qa_qvm_proceed(call, result, error);
    hook->owner->hud_requested = true;
    *result = 0;
    return true;
}

static bool visibility(void *context, const qa_qvm_call *call, bool original,
    bool *taken, qa_error *error)
{
    (void)call;
    application_q3_equipment *owner = context;
    bool active;
    if (!selected(owner, &active, error)) return false;
    if (original == owner->profile->view_taken) owner->view_requested = true;
    *taken = original;
    return true;
}

static bool view(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    equipment_hook *hook = context;
    application_q3_equipment *owner = hook->owner;
    bool active;
    if (!selected(owner, &active, error)) return false;
    (void)active;
    if (!owner->drawing) return qa_qvm_proceed(call, result, error);
    if (!owner->draw.view_visible) { *result = 0; return true; }
    qa_qvm_branch_binding branch = {owner->profile->view_decision, visibility, owner};
    return qa_qvm_bind_branches(call, &branch, 1, error) && qa_qvm_proceed(call, result, error);
}

static bool status_visibility(void *context, const qa_qvm_call *call, bool original,
    bool *taken, qa_error *error)
{
    (void)call;
    equipment_hook *hook = context;
    bool active;
    if (!selected(hook->owner, &active, error)) return false;
    if (original == hook->owner->profile->status[hook->status].taken)
        hook->owner->hud_requested = true;
    *taken = original;
    return true;
}

static bool skip_ammo(void *context, const qa_qvm_call *call, bool *skip, qa_error *error)
{
    (void)call;
    equipment_hook *hook = context;
    return selected(hook->owner, skip, error);
}

static bool status(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    equipment_hook *hook = context;
    application_q3_equipment *owner = hook->owner;
    bool active;
    if (!selected(owner, &active, error)) return false;
    if (!active) return qa_qvm_proceed(call, result, error);
    if (!owner->profile->status_regions) {
        owner->hud_requested = true;
        *result = 0;
        return true;
    }
    const application_q3_equipment_status *source = owner->profile->status + hook->status;
    qa_qvm_branch_binding branch = {source->decision, status_visibility, hook};
    qa_qvm_region_binding *regions = source->ammo_count ? calloc(source->ammo_count, sizeof(*regions)) : NULL;
    if (source->ammo_count && !regions)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining scoped source ammo boundaries");
    for (size_t i = 0; i < source->ammo_count; ++i)
        regions[i] = (qa_qvm_region_binding){source->ammo[i].entry, source->ammo[i].join,
            skip_ammo, NULL, hook};
    bool ok = qa_qvm_bind_branches(call, &branch, 1, error) &&
        qa_qvm_bind_regions(call, regions, source->ammo_count, error);
    free(regions);
    return ok && qa_qvm_proceed(call, result, error);
}

typedef struct warning_call { int32_t *result; } warning_call;
static bool warning_proceed(void *context, const qa_qvm_call *call, qa_error *error)
{
    warning_call *request = context;
    return qa_qvm_proceed(call, request->result, error);
}

static bool warning(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    equipment_hook *hook = context;
    application_q3_equipment *owner = hook->owner;
    bool active;
    if (!selected(owner, &active, error)) return false;
    if (!active) return qa_qvm_proceed(call, result, error);
    int32_t value = owner->draw.warning == QA_APPLICATION_AMMO_EMPTY ? owner->profile->warning_empty :
        owner->draw.warning == QA_APPLICATION_AMMO_LOW ? owner->profile->warning_low : owner->profile->warning_none;
    warning_call request = {result};
    return qa_qvm_source_global_word(call, owner->module.image, owner->profile->warning_state,
        value, warning_proceed, &request, error);
}

static uint32_t word(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool held(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    equipment_hook *hook = context;
    application_q3_equipment *owner = hook->owner;
    if (!owner->drawing) return qa_qvm_proceed(call, result, error);
    bool active;
    if (!selected(owner, &active, error)) return false;
    (void)active;
    int32_t state, parent_pointer, entity_pointer;
    const application_q3_equipment_profile *profile = owner->profile;
    if (!qa_qvm_call_argument(call, profile->state_argument, &state, error)) return false;
    if (state) return qa_qvm_proceed(call, result, error);
    qa_bytes parent_bytes, entity_bytes;
    qa_q3_ref_entity parent;
    if (!qa_qvm_call_argument(call, profile->parent_argument, &parent_pointer, error) ||
        !qa_qvm_call_argument(call, profile->entity_argument, &entity_pointer, error) ||
        !qa_qvm_span(call->vm, parent_pointer, 0, 140, &parent_bytes, error) ||
        !qa_q3_host_ref_entity_decode(parent_bytes, &parent, error) ||
        !qa_qvm_span(call->vm, entity_pointer, profile->entity_number_offset, 4, &entity_bytes, error)) return false;
    uint32_t bits = word(entity_bytes.data);
    int32_t number; memcpy(&number, &bits, sizeof(number));
    if (number < 0)
        return application_fail(error, QA_ERROR_FORMAT, "Held source entity number is negative");
    qa_actor_id actor = {0}; bool present = false;
    if (!owner->module.client.source_actor ||
        !owner->module.client.source_actor(owner->module.client.context,
            (uint32_t)number, &actor, &present, error)) return false;
    if (!present) return qa_qvm_proceed(call, result, error);
    equipment_scope scope = {.previous = owner->scopes};
    bool replace = false;
    if (!owner->services.held_begin(owner->services.context, actor, &parent,
            &scope.token, &replace, error)) {
        if (scope.token) owner->services.held_release(owner->services.context, scope.token);
        return false;
    }
    if (!replace) {
        if (scope.token) owner->services.held_release(owner->services.context, scope.token);
        return qa_qvm_proceed(call, result, error);
    }
    bool ok = qa_qvm_call_source_frame(call, owner->module.image, &scope.frame, error);
    if (ok && (uint64_t)scope.frame.start + profile->gun + 140 > scope.frame.end)
        ok = application_fail(error, QA_ERROR_FORMAT, "Held gun leaves its actual source function frame");
    if (ok) {
        scope.gun = scope.frame.start + profile->gun;
        owner->scopes = &scope;
        ok = qa_qvm_proceed(call, result, error);
        owner->scopes = scope.previous;
        bool cancelled = false;
        if (ok) ok = qa_qvm_call_cancelled(call, &cancelled, error);
        if (ok && !cancelled) ok = owner->services.held_submit(owner->services.context, scope.token, error);
    }
    owner->services.held_release(owner->services.context, scope.token);
    return ok;
}

bool application_q3_equipment_source_entity(void *context, const qa_qvm_call *call,
    int32_t pointer, const qa_q3_ref_entity *entity, bool *suppress, qa_error *error)
{
    application_q3_equipment *owner = context;
    if (!owner || !call || !entity || !suppress || call->vm != owner->module.vm)
        return application_fail(error, QA_ERROR_ARGUMENT, "Held source submission requires its actual executor");
    *suppress = false;
    equipment_scope *scope = owner->scopes;
    if (!scope) return true;
    bool active;
    if (!selected(owner, &active, error)) return false;
    (void)active;
    if (pointer < 0 || (uint32_t)pointer < scope->frame.start ||
        (uint64_t)(uint32_t)pointer + 140 > scope->frame.end) return true;
    if ((uint32_t)pointer == scope->gun &&
        !owner->services.held_pass(owner->services.context, scope->token, entity, error)) return false;
    *suppress = true;
    return true;
}

bool application_q3_equipment_idle(const application_q3_equipment *owner)
{
    return !owner || (!owner->drawing && !owner->scopes);
}

bool application_q3_equipment_executor(const application_q3_equipment *owner,
    const qa_session *session, const qa_qvm *vm)
{
    return !owner || (owner->module.session == session && owner->module.vm == vm);
}

bool application_q3_equipment_destroy(application_q3_equipment *owner, qa_error *error)
{
    if (!owner) return true;
    if (!application_q3_equipment_idle(owner) || !qa_qvm_can_destroy(owner->module.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment source owner is executing");
    for (size_t i = owner->hook_count; i; --i) {
        equipment_hook *hook = owner->hooks + i - 1;
        if (hook->binding && !qa_qvm_unbind(owner->module.vm, hook->binding, error)) return false;
        hook->binding = 0;
    }
    free(owner->hooks); free(owner);
    return true;
}

bool application_q3_equipment_create(q3g_role *role,
    const application_q3_equipment_services *services, application_q3_equipment **out, qa_error *error)
{
    if (!role || !role->artifact || !role->engine || !role->engine->provider || role->kind != QA_QVM_CGAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment source requires its actual CGAME role");
    application_q3_equipment_module module = {.session = role->engine->provider->application->session,
        .vm = role->vm, .image = role->image,
        .profile = &role->artifact->equipment_profile, .receiver = role->engine->provider->owner,
        .seat = role->seat, .client = role->client_services};
    return application_q3_equipment_create_module(&module, services, out, error);
}

bool application_q3_equipment_create_module(const application_q3_equipment_module *module,
    const application_q3_equipment_services *services, application_q3_equipment **out, qa_error *error)
{
    if (!module || !module->session || !module->vm || !module->image || !module->profile || !module->receiver ||
        qa_qvm_get_role(module->vm) != QA_QVM_CGAME ||
        (module->profile->present && (!module->client.context || !module->client.source_actor)) ||
        !out || *out || !services || !services->context || !services->prepare || !services->current ||
        !services->release_draw || !services->held_begin || !services->held_pass ||
        !services->held_submit || !services->held_release || !qa_qvm_can_destroy(module->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment source constructor requires actual cgame/media owners");
    application_q3_equipment *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating original equipment boundary owner");
    owner->module = *module; owner->profile = module->profile; owner->services = *services;
    *out = owner;
    if (!owner->profile->present) return true;
    if (owner->profile->status_count > SIZE_MAX / sizeof(*owner->hooks) - 4)
        return application_fail(error, QA_ERROR_MEMORY, "Equipment binding inventory exceeds address space");
    owner->hook_count = owner->profile->status_count + 4;
    owner->hooks = calloc(owner->hook_count, sizeof(*owner->hooks));
    if (!owner->hooks) { owner->hook_count = 0; return application_fail(error, QA_ERROR_MEMORY, "Retaining actual equipment binding inventory"); }
    const uint32_t entries[] = {owner->profile->hud, owner->profile->view_entry,
        owner->profile->warning_entry, owner->profile->held_entry};
    const qa_qvm_function_hook functions[] = {hud, view, warning, held};
    for (size_t i = 0; i < owner->hook_count; ++i) {
        equipment_hook *hook = owner->hooks + i;
        *hook = (equipment_hook){.owner = owner, .instruction = i < 4 ? entries[i] : owner->profile->status[i - 4].entry,
            .status = i < 4 ? 0 : i - 4, .function = i < 4 ? functions[i] : status};
        if (!qa_qvm_bind_function(module->vm, hook->instruction, false, hook->function,
                hook, &hook->binding, error)) return false;
    }
    return true;
}

bool application_q3_equipment_draw_begin(application_q3_equipment *owner, qa_error *error)
{
    if (!owner || owner->drawing || owner->scopes)
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment draw requires its idle boundary owner");
    owner->hud_requested = owner->view_requested = false;
    owner->draw.selected = false;
    application_q3_equipment_draw draw = {.view_visible = true};
    if (!owner->services.prepare(owner->services.context, owner->module.receiver,
            owner->module.seat, &draw, error)) {
        owner->services.release_draw(owner->services.context);
        return false;
    }
    if ((unsigned)draw.warning > QA_APPLICATION_AMMO_EMPTY ||
        ((draw.selected || !draw.view_visible) && !owner->profile->present)) {
        owner->services.release_draw(owner->services.context);
        return application_fail(error, QA_ERROR_FORMAT, "Selected source equipment requires its artifact-declared presentation boundaries");
    }
    owner->draw = draw; owner->hud_requested = owner->view_requested = false;
    owner->drawing = true;
    return true;
}

void application_q3_equipment_draw_end(application_q3_equipment *owner)
{
    if (!owner || !owner->drawing) return;
    owner->drawing = false;
    owner->services.release_draw(owner->services.context);
}

bool application_q3_equipment_hud(const application_q3_equipment *owner)
{
    return owner && owner->draw.selected && owner->hud_requested &&
        owner->services.current(owner->services.context, &owner->draw);
}

bool application_q3_equipment_view(const application_q3_equipment *owner)
{
    return owner && owner->draw.selected && owner->view_requested &&
        owner->services.current(owner->services.context, &owner->draw);
}

size_t application_q3_equipment_descriptor_count(const application_q3_equipment *owner)
{
    return owner ? owner->hook_count : 0;
}

bool application_q3_equipment_descriptors(const application_q3_equipment *owner,
    qa_qvm_saved_function *out, size_t count, qa_error *error)
{
    if (!application_q3_equipment_idle(owner) || count != application_q3_equipment_descriptor_count(owner) ||
        (count && !out)) return application_fail(error, QA_ERROR_ARGUMENT, "Equipment descriptors require their complete idle constructor inventory");
    for (size_t i = 0; i < count; ++i) {
        equipment_hook *hook = owner->hooks + i;
        if (!hook->binding) return application_fail(error, QA_ERROR_FORMAT, "Equipment constructor binding is incomplete");
        out[i] = (qa_qvm_saved_function){hook->binding, hook->instruction, false, hook->function, hook};
    }
    return true;
}

void application_q3_equipment_adopt(application_q3_equipment *owner, const qa_qvm_binding *bindings)
{
    for (size_t i = 0; owner && i < owner->hook_count; ++i) owner->hooks[i].binding = bindings[i];
}

bool application_q3_equipment_state_qualify(const application_q3_equipment *owner,
    const application_q3_equipment_saved *saved, qa_error *error)
{
    if (!saved || !application_q3_equipment_idle(owner) ||
        (unsigned)saved->draw.warning > QA_APPLICATION_AMMO_EMPTY ||
        (saved->draw.selected && (!owner || !owner->profile->present || !saved->draw.actor.registry)) ||
        (!owner && (saved->draw.actor.registry || saved->draw.selected || saved->draw.view_visible ||
            saved->draw.warning != QA_APPLICATION_AMMO_NONE || saved->hud_requested || saved->view_requested)) ||
        (owner && !owner->profile->present && (saved->hud_requested || saved->view_requested)))
        return application_fail(error, QA_ERROR_FORMAT, "Equipment continuation differs from its actual idle declaration owner");
    return true;
}

bool application_q3_equipment_state_read(const application_q3_equipment *owner,
    application_q3_equipment_saved *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Equipment continuation requires its output");
    application_q3_equipment_saved saved = {0};
    if (owner) saved = (application_q3_equipment_saved){owner->draw,
        owner->hud_requested, owner->view_requested};
    if (!application_q3_equipment_state_qualify(owner, &saved, error)) return false;
    *out = saved;
    return true;
}

void application_q3_equipment_state_adopt(application_q3_equipment *owner,
    const application_q3_equipment_saved *saved)
{
    if (!owner) return;
    owner->draw = saved->draw;
    owner->hud_requested = saved->hud_requested;
    owner->view_requested = saved->view_requested;
}
