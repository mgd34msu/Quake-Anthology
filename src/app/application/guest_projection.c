#include "guest_projection_private.h"
#include "guest_native_q2_private.h"

static bool current(guest_projection_actor *context, qa_q3_host_game_data *data,
                    qa_error *error)
{
    application_guest_projection *p = context->projection;
    q3g_role *role = p->role;
    uint32_t slot;
    if (!qa_actors_get(qa_session_actors(role->engine->provider->application->session), context->actor) ||
        !qa_q3_host_actor_slot(role->host, context->actor, &slot, error) ||
        slot != context->slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest projection actor generation retired");
    if (!qa_q3_host_game_data_read(role->host, data) ||
        data->entity_stride != p->entity_stride || data->client_stride != p->client_stride ||
        context->slot >= data->entity_count || context->slot >= data->client_count)
        return application_fail(error, QA_ERROR_FORMAT, "Guest projection source records changed");
    return true;
}

static bool read_word(q3g_role *role, uint32_t address, uint32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(role->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_u32le(bytes); return true;
}

static bool write_word(q3g_role *role, uint32_t address, uint32_t word, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, word);
    return qa_qvm_write(role->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

static bool address(guest_projection_actor *context, guest_field field, uint32_t *out,
                    qa_error *error)
{
    qa_q3_host_game_data data;
    if (!current(context, &data, error)) return false;
    application_guest_projection *p = context->projection;
    uint64_t entity = data.entities_address + (uint64_t)context->slot * data.entity_stride;
    uint64_t client = data.clients_address + (uint64_t)context->slot * data.client_stride;
    if (entity > UINT32_MAX - p->client_pointer || client > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Guest source client pointer exceeds QVM memory");
    uint32_t pointer;
    if (!read_word(p->role, (uint32_t)entity + p->client_pointer, &pointer, error)) return false;
    if (pointer != client)
        return application_fail(error, QA_ERROR_FORMAT, "Guest entity does not own its located client record");
    uint64_t base = field.record == GUEST_CLIENT_RECORD ? client : entity;
    if (base > UINT32_MAX - field.offset)
        return application_fail(error, QA_ERROR_FORMAT, "Guest projection address exceeds QVM memory");
    *out = (uint32_t)base + field.offset; return true;
}

static bool capacity_read(guest_projection_actor *context, const guest_inventory_field *field,
                          int32_t *out, qa_error *error)
{
    const guest_capacity *capacity = &field->capacity;
    q3g_role *role = context->projection->role;
    if (capacity->kind == GUEST_CAPACITY_CONSTANT) *out = capacity->value.constant;
    else if (capacity->kind == GUEST_CAPACITY_FIELD) {
        uint32_t at, word;
        if (!address(context, capacity->value.field, &at, error) || !read_word(role, at, &word, error)) return false;
        *out = (int32_t)word;
    } else if (capacity->kind == GUEST_CAPACITY_SOURCE) {
        *out = capacity->value.constant;
        for (size_t i = 0; i < capacity->override_count; ++i) {
            const guest_capacity_override *value = &capacity->overrides[i];
            uint32_t word;
            if (!read_word(role, value->condition.address, &word, error)) return false;
            bool match = (int32_t)word == value->condition.value;
            if (match == value->condition.equal) { *out = value->value; break; }
        }
    } else return application_fail(error, QA_ERROR_UNSUPPORTED, "Guest public source capacity query is not admitted");
    return *out >= 0 || application_fail(error, QA_ERROR_FORMAT, "Guest source capacity is negative");
}

static size_t inventory_count(void *context)
{
    return ((guest_projection_actor *)context)->projection->inventory_count;
}

static bool inventory_at_inner(void *opaque, size_t index, qa_inventory_entry *out, qa_error *error)
{
    guest_projection_actor *context = opaque;
    application_guest_projection *p = context->projection;
    if (index >= p->inventory_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory index exceeds its declaration");
    const guest_inventory_field *field = &p->inventory[index];
    uint32_t at, word; int32_t capacity;
    if (!address(context, field->field, &at, error) || !read_word(p->role, at, &word, error) ||
        !capacity_read(context, field, &capacity, error)) return false;
    if (field->mask && (word & ~field->allowed_mask))
        return application_fail(error, QA_ERROR_FORMAT, "Guest inventory contains undeclared source bits");
    *out = (qa_inventory_entry){field->item,
        field->mask ? (word & field->mask ? 1 : 0) : (double)(int32_t)word,
        (double)capacity, field->mask ? QA_COUNT_STACK : QA_COUNT_SOURCE_INT32};
    return true;
}

static bool inventory_write_inner(void *opaque, const qa_inventory_entry *entry, qa_error *error)
{
    guest_projection_actor *context = opaque;
    application_guest_projection *p = context->projection;
    const guest_inventory_field *field = NULL;
    for (size_t i = 0; i < p->inventory_count; ++i)
        if (p->inventory[i].item == entry->item) { field = &p->inventory[i]; break; }
    if (!field) return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory item has no source storage");
    if (!isfinite(entry->count) || floor(entry->count) != entry->count ||
        entry->count < INT32_MIN || entry->count > INT32_MAX ||
        !isfinite(entry->capacity) || floor(entry->capacity) != entry->capacity ||
        entry->capacity < 0 || entry->capacity > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory value exceeds source representation");
    uint32_t at, capacity_at = 0; int32_t capacity;
    if (!address(context, field->field, &at, error) || !capacity_read(context, field, &capacity, error)) return false;
    if (field->mask) {
        if (entry->policy != QA_COUNT_STACK || (entry->count != 0 && entry->count != 1) || entry->capacity != 1)
            return application_fail(error, QA_ERROR_ARGUMENT, "Guest packed item requires zero or one ownership");
        uint32_t previous;
        if (!read_word(p->role, at, &previous, error)) return false;
        if (previous & ~field->allowed_mask)
            return application_fail(error, QA_ERROR_FORMAT, "Guest inventory contains undeclared source bits");
        return write_word(p->role, at, entry->count ? previous | field->mask : previous & ~field->mask, error);
    }
    if (entry->policy != QA_COUNT_SOURCE_INT32)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest counter requires its signed source policy");
    if (field->capacity.kind == GUEST_CAPACITY_FIELD) {
        if (!address(context, field->capacity.value.field, &capacity_at, error)) return false;
    } else if (entry->capacity != capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest inventory capacity is owned by original source");
    if (!write_word(p->role, at, (uint32_t)(int32_t)entry->count, error)) return false;
    if (field->capacity.kind != GUEST_CAPACITY_FIELD) return true;
    uint32_t current_capacity;
    if (!address(context, field->capacity.value.field, &current_capacity, error)) return false;
    if (current_capacity != capacity_at)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest capacity record changed during count publication");
    return write_word(p->role, capacity_at, (uint32_t)(int32_t)entry->capacity, error);
}

static bool inventory_at(void *opaque, size_t index, qa_inventory_entry *out, qa_error *error)
{
    q3g_role *role = ((guest_projection_actor *)opaque)->projection->role;
    ++role->engine->calls;
    bool ok = inventory_at_inner(opaque, index, out, error);
    --role->engine->calls; return ok;
}

static bool inventory_write(void *opaque, const qa_inventory_entry *entry, qa_error *error)
{
    q3g_role *role = ((guest_projection_actor *)opaque)->projection->role;
    ++role->engine->calls;
    bool ok = inventory_write_inner(opaque, entry, error);
    --role->engine->calls; return ok;
}

static bool mutable_capacity(void *opaque, qa_item_id item)
{
    application_guest_projection *p = ((guest_projection_actor *)opaque)->projection;
    for (size_t i = 0; i < p->inventory_count; ++i)
        if (p->inventory[i].item == item) return p->inventory[i].capacity.kind == GUEST_CAPACITY_FIELD;
    return false;
}

bool application_guest_projection_prepare(q3g_role *role, qa_bytes primary, qa_error *error)
{
    if (role->projection) return true;
    application_guest_projection *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing qualified guest projection");
    if (!application_guest_projection_profile_read(role, primary, p, error)) {
        application_guest_projection_profile_free(p); free(p); return false;
    }
    role->projection = p; return true;
}

static bool player_state(q3g_role *role, qa_actor_id actor, qa_combat_state *out, qa_error *error)
{
    application_guest_projection *p = role->projection;
    if (!p || !p->has_state)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Guest player state requires a qualified original combat interface");
    uint32_t slot;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error)) return false;
    guest_projection_actor context = {.projection = p, .actor = actor, .slot = slot};
    const guest_state_profile *s = &p->state;
    uint32_t at, inuse, health, damageable, flags;
    if (!address(&context, (guest_field){GUEST_ENTITY_RECORD, s->inuse}, &at, error) ||
        !read_word(role, at, &inuse, error) ||
        !address(&context, (guest_field){GUEST_ENTITY_RECORD, s->health}, &at, error) ||
        !read_word(role, at, &health, error) ||
        !address(&context, (guest_field){GUEST_ENTITY_RECORD, s->takedamage}, &at, error) ||
        !read_word(role, at, &damageable, error) ||
        !address(&context, (guest_field){GUEST_ENTITY_RECORD, s->flags}, &at, error) ||
        !read_word(role, at, &flags, error)) return false;
    if (!inuse) return application_fail(error, QA_ERROR_ARGUMENT, "Guest player source entity has not been admitted");
    qa_combat_state state = {.health = (float)(int32_t)health,
        .can_take_damage = damageable != 0, .invulnerable = (flags & s->invulnerable) != 0,
        .no_knockback = (flags & s->no_knockback) != 0,
        .armor = {.regular = {.kind = QA_ARMOR_NONE}, .powered = {.kind = QA_POWER_NONE}}};
    if (s->mass_kind == GUEST_MASS_CONSTANT) state.mass = s->mass.constant;
    else {
        uint32_t word;
        if (!address(&context, (guest_field){GUEST_ENTITY_RECORD, s->mass.offset}, &at, error) ||
            !read_word(role, at, &word, error)) return false;
        if (s->mass_kind == GUEST_MASS_INT32) state.mass = (float)(int32_t)word;
        else memcpy(&state.mass, &word, sizeof(word));
    }
    if (!isfinite(state.mass) || state.mass < 0)
        return application_fail(error, QA_ERROR_FORMAT, "Guest source mass is not finite and nonnegative");
    qa_q3_player player;
    if (!qa_q3_host_source_player(role->host, slot, &player, error)) return false;
    for (size_t i = 0; i < s->team_count; ++i)
        if (s->teams[i].value == player.persistant[s->team_stat]) { state.team = s->teams[i].team; break; }
    int32_t points = player.stats[s->armor_stat];
    if (points < 0) return application_fail(error, QA_ERROR_FORMAT, "Guest source armor points are negative");
    float protection = s->armor_protection;
    bool tiers = false;
    for (size_t i = 0; i < s->tier_condition_count; ++i) {
        const guest_condition *condition = &s->tier_conditions[i];
        uint32_t word;
        if (!read_word(role, condition->address, &word, error)) return false;
        if (((int32_t)word == condition->value) == condition->equal) { tiers = true; break; }
    }
    if (tiers) {
        protection = s->tier_fallback;
        for (size_t i = 0; i < s->tier_count; ++i)
            if (s->tiers[i].value == player.stats[s->tier_stat]) { protection = s->tiers[i].protection; break; }
    }
    state.armor.regular = (qa_regular_armor){.kind = QA_ARMOR_Q3, .points = (float)points,
        .protection.q3_protection = protection};
    qa_q3_host_game_data data;
    if (!current(&context, &data, error)) return false;
    *out = state; return true;
}

bool application_guest_player_state(application_provider *provider, qa_actor_id actor,
                                    qa_combat_state *out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !engine->game || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest player observation requires a game owner");
    ++engine->calls;
    bool ok = player_state(engine->game, actor, out, error);
    --engine->calls; return ok;
}

bool application_guest_projection_admit(q3g_role *role, qa_actor_id actor, qa_error *error)
{
    application_guest_projection *p = role->projection;
    qa_application *app = role->engine->provider->application;
    uint32_t slot;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error)) return false;
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(role->host, &data))
        return application_fail(error, QA_ERROR_FORMAT, "Guest admission requires located source records");
    if (slot >= data.client_count || application_provider_for(app, actor, QA_ROLE_INVENTORY, NULL) != role->engine->provider)
        return true;
    if (!p || !p->has_inventory)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Guest inventory requires a qualified original storage interface");
    guest_projection_actor *context;
    for (context = p->actors; context; context = context->next)
        if (qa_actor_id_equal(context->actor, actor)) break;
    if (!context) {
        context = calloc(1, sizeof(*context));
        if (!context) return application_fail(error, QA_ERROR_MEMORY, "Retaining guest actor projection ownership");
        *context = (guest_projection_actor){.next = p->actors, .projection = p, .actor = actor, .slot = slot};
        p->actors = context;
    }
    if (!context->inventory_bound) {
        qa_inventory_binding binding = {context, inventory_count, inventory_at, inventory_write, mutable_capacity};
        if (!qa_inventory_adopt_primary(app->inventory, actor, &binding, &context->inventory_lease, error)) return false;
        context->inventory_bound = true;
        context->inventory_prepared = false;
    }
    return true;
}

bool application_guest_projection_detach(q3g_role *role, qa_actor_id actor, qa_error *error)
{
    application_guest_projection *p = role->projection;
    if (!p) return true;
    qa_application *app = role->engine->provider->application;
    for (guest_projection_actor *context = p->actors; context; context = context->next) {
        if (!qa_actor_id_equal(context->actor, actor) || !context->inventory_bound) continue;
        bool unpublished = context->inventory_prepared &&
            !qa_inventory_primary_current(app->inventory, context->inventory_lease, context);
        if (!unpublished && qa_actors_get(qa_session_actors(app->session), actor) &&
            !qa_inventory_detach_primary(app->inventory, context->inventory_lease, context, error)) return false;
        context->inventory_bound = false;
        context->inventory_prepared = false;
    }
    return true;
}

bool application_guest_projection_inventory_binding(q3g_role *role,
    qa_actor_id actor, uint64_t serial, qa_inventory_binding *out, qa_error *error)
{
    application_guest_projection *projection = role ? role->projection : NULL;
    if (!projection || !projection->has_inventory || !out || !serial)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Restored guest primary inventory requires its actual source declaration");
    uint32_t slot;
    if (!qa_q3_host_actor_slot(role->host, actor, &slot, error))
        return false;
    guest_projection_actor *context = projection->actors;
    while (context && !qa_actor_id_equal(context->actor, actor))
        context = context->next;
    guest_projection_actor temporary = {.projection = projection, .actor = actor, .slot = slot};
    qa_q3_host_game_data data;
    if (!current(&temporary, &data, error))
        return false;
    if (context && (context->slot != slot ||
        (context->inventory_bound && context->inventory_lease.serial != serial)))
        return application_fail(error, QA_ERROR_FORMAT,
                                "Restored guest inventory differs from its source lease");
    if (!context) {
        context = calloc(1, sizeof(*context));
        if (!context)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Retaining restored guest inventory callback context");
        *context = temporary;
        context->next = projection->actors;
        projection->actors = context;
    }
    context->inventory_lease = (qa_inventory_lease){actor, serial};
    if (!context->inventory_bound)
        context->inventory_prepared = true;
    context->inventory_bound = true;
    *out = (qa_inventory_binding){context, inventory_count, inventory_at,
                                  inventory_write, mutable_capacity};
    return true;
}

bool application_guest_inventory_restore_finish(application_provider *provider, qa_error *error)
{
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
        return application_native_q2_restore_finish(provider, error);
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine)
        return true;
    if (engine->calls || !qa_session_safe(provider->application->session) ||
        !qa_world_idle(engine->world))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Guest inventory finish requires idle source owners");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->projection)
            for (guest_projection_actor *context = role->projection->actors; context; context = context->next)
                if (context->inventory_prepared && (!context->inventory_bound ||
                    !qa_inventory_primary_current(provider->application->inventory,
                                                   context->inventory_lease, context)))
                    return application_fail(error, QA_ERROR_FORMAT,
                                            "Restored guest primary inventory was not published");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->projection)
            for (guest_projection_actor *context = role->projection->actors; context; context = context->next)
                context->inventory_prepared = false;
    return true;
}

bool application_guest_projection_close(q3g_role *role, qa_error *error)
{
    application_guest_projection *p = role->projection;
    if (!p) return true;
    for (guest_projection_actor *context = p->actors; context; context = context->next)
        if (!application_guest_projection_detach(role, context->actor, error)) return false;
    while (p->actors) { guest_projection_actor *next = p->actors->next; free(p->actors); p->actors = next; }
    application_guest_projection_profile_free(p); free(p); role->projection = NULL; return true;
}
