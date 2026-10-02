#include "guest_q3_combat_state.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons.h"

static bool fail(qa_error *error, qa_status status, const char *text)
{ return application_fail(error, status, text); }
static const application_q3_combat_definition *definition(const application_q3_combat_actor *a)
{ return application_q3_combat_profile_definition(a->profile); }
static bool table_equal(qa_q3_host_game_data a, qa_q3_host_game_data b)
{
    return a.entity_stride == b.entity_stride && a.client_count == b.client_count && a.client_stride == b.client_stride &&
        a.entities_address == b.entities_address && a.clients_address == b.clients_address;
}
bool application_q3_combat_actor_current(const application_q3_combat_actor *a, qa_error *error)
{
    if (!a || !a->role || !a->profile || !a->actor.registry)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 combat requires its retained full actor");
    q3g_role *role = a->role; qa_q3_host_game_data table; uint32_t slot;
    if (role->kind != QA_QVM_GAME || !role->host || !role->vm ||
        role->image != application_q3_combat_profile_image(a->profile) ||
        role->abi != application_q3_combat_profile_abi(a->profile) ||
        !role->path || strcmp(role->path, application_q3_combat_profile_path(a->profile)) ||
        qa_qvm_get_role(role->vm) != QA_QVM_GAME || qa_qvm_get_abi(role->vm) != role->abi ||
        !qa_sha256_equal(qa_qvm_digest(role->vm), qa_qvm_image_digest(role->image)) ||
        !qa_q3_host_game_data_read(role->host, &table) || !table_equal(table, a->table) || a->slot >= table.entity_count)
        return fail(error, QA_ERROR_NOT_FOUND, "Original Q3 combat executor or located table changed");
    return qa_qvm_read(role->vm, 0, NULL, 0, error) &&
        qa_q3_host_actor_slot(role->host, a->actor, &slot, error) &&
        (slot == a->slot || fail(error, QA_ERROR_NOT_FOUND, "Original Q3 combat actor changed source slot"));
}
bool application_q3_combat_actor_read(q3g_role *role,
    const application_q3_combat_profile *profile, qa_actor_id actor,
    application_q3_combat_actor *out, qa_error *error)
{
    if (!role || !profile || !out || !role->host)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 combat actor requires its actual host and metadata");
    application_q3_combat_actor a = {.role = role, .profile = profile, .actor = actor};
    const application_q3_combat_definition *d = definition(&a);
    if (!qa_q3_host_game_data_read(role->host, &a.table) ||
        a.table.entity_stride != d->entity_stride || a.table.client_stride != d->client_stride ||
        !qa_q3_host_actor_slot(role->host, actor, &a.slot, error) || a.slot >= a.table.entity_count)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 combat declaration differs from located actor records");
    uint64_t entity = a.table.entities_address + (uint64_t)a.slot * d->entity_stride;
    uint64_t player = a.slot < a.table.client_count ?
        a.table.clients_address + (uint64_t)a.slot * d->client_stride : 0;
    if (!a.table.entities_address || entity > UINT32_MAX ||
        (a.slot < a.table.client_count && (!a.table.clients_address || player > UINT32_MAX)))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 combat pointers leave their QVM address space");
    a.entity = (uint32_t)entity; a.player = (uint32_t)player;
    if (!application_q3_combat_actor_current(&a, error) ||
        entity > qa_qvm_memory_size(role->vm) || d->entity_stride > qa_qvm_memory_size(role->vm) - entity ||
        (player && (player > qa_qvm_memory_size(role->vm) || d->client_stride > qa_qvm_memory_size(role->vm) - player)))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 combat records leave live source memory");
    *out = a; return true;
}
bool application_q3_combat_word_read(const application_q3_combat_actor *a,
    uint32_t address, int32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!out || !application_q3_combat_actor_current(a, error) ||
        !qa_qvm_read(a->role->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_i32le(bytes); return application_q3_combat_actor_current(a, error);
}
bool application_q3_combat_word_write(const application_q3_combat_actor *a,
    uint32_t address, int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return application_q3_combat_actor_current(a, error) &&
        qa_qvm_write(a->role->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error) &&
        application_q3_combat_actor_current(a, error);
}
static bool exact_float(int32_t word, float *out, qa_error *error)
{
    *out = (float)word;
    return (double)*out == (double)word ||
        fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 integer exceeds exact shared combat representation");
}
static bool integer(float value, int32_t *out, qa_error *error)
{
    if (!isfinite(value) || trunc((double)value) != value || value < INT32_MIN || (double)value > INT32_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 combat store requires a signed integer");
    *out = (int32_t)value; return true;
}
bool application_q3_combat_actor_live(const application_q3_combat_actor *a,
    bool *out, qa_error *error)
{
    int32_t inuse;
    if (!out || !application_q3_combat_actor_current(a, error) ||
        !application_q3_combat_word_read(a, a->entity + definition(a)->fields.inuse, &inuse, error)) return false;
    *out = inuse != 0; return true;
}
static bool active_tiers(const application_q3_combat_actor *a, bool *out, qa_error *error)
{
    const application_q3_combat_definition *d = definition(a); *out = false;
    for (size_t i = 0; i < d->armor.mode_count; ++i) {
        const application_q3_combat_mode *mode = d->armor.modes + i; int32_t value;
        if (!application_q3_combat_word_read(a, mode->offset, &value, error)) return false;
        if (mode->equal ? value == mode->value : value != mode->value) { *out = true; break; }
    }
    return true;
}
static bool protection(const application_q3_combat_actor *a, bool active, float *out, qa_error *error)
{
    const application_q3_combat_definition *d = definition(a); *out = d->armor.protection;
    if (!active) return true;
    int32_t tier;
    if (!application_q3_combat_word_read(a, a->player + 184 + d->armor.tier_stat * 4, &tier, error)) return false;
    *out = d->armor.fallback;
    for (size_t i = 0; i < d->armor.tier_count; ++i)
        if (d->armor.tiers[i].value == tier) { *out = d->armor.tiers[i].protection; break; }
    return true;
}
bool application_q3_combat_armor_read(const application_q3_combat_actor *a,
    qa_armor *out, qa_error *error)
{
    if (!out || !application_q3_combat_actor_current(a, error)) return false;
    qa_armor armor = {0};
    if (a->player) {
        int32_t points; bool active;
        const application_q3_combat_definition *d = definition(a);
        armor.regular.kind = QA_ARMOR_Q3;
        if (!application_q3_combat_word_read(a, a->player + 184 + d->armor.points_stat * 4, &points, error) ||
            !exact_float(points, &armor.regular.points, error) || !active_tiers(a, &active, error) ||
            !protection(a, active, &armor.regular.protection.q3_protection, error)) return false;
    }
    *out = armor; return true;
}
typedef struct armor_store { int32_t points, tier; bool write_tier; } armor_store;
static bool armor_plan(const application_q3_combat_actor *a, const qa_armor *armor,
    armor_store *out, qa_error *error)
{
    if (!armor || !out || !application_q3_combat_actor_current(a, error)) return false;
    if (armor->powered.kind != QA_POWER_NONE ||
        (armor->regular.kind != QA_ARMOR_NONE && armor->regular.kind != QA_ARMOR_Q3))
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 armor requires source Q3 values");
    *out = (armor_store){0};
    if (!a->player) return armor->regular.kind == QA_ARMOR_NONE ||
        fail(error, QA_ERROR_ARGUMENT, "Original Q3 non-client has no player armor");
    if (armor->regular.kind == QA_ARMOR_NONE) return true;
    if (!integer(armor->regular.points, &out->points, error) || out->points < 0)
        return fail(error, QA_ERROR_ARGUMENT, "Original Q3 armor points require a nonnegative source integer");
    bool active; float current;
    if (!active_tiers(a, &active, error) || !protection(a, active, &current, error)) return false;
    float requested = armor->regular.protection.q3_protection;
    if (requested == current) return true;
    const application_q3_combat_definition *d = definition(a);
    if (active) for (size_t i = 0; i < d->armor.tier_count; ++i)
        if (d->armor.tiers[i].protection == requested) {
            out->tier = d->armor.tiers[i].value; out->write_tier = true; return true;
        }
    return fail(error, QA_ERROR_ARGUMENT, "Requested armor protection has no active original Q3 tier");
}
bool application_q3_combat_armor_validate(void *opaque, const qa_armor *armor, qa_error *error)
{ armor_store store; return armor_plan(opaque, armor, &store, error); }
bool application_q3_combat_armor_write(void *opaque, const qa_armor *armor, qa_error *error)
{
    application_q3_combat_actor *a = opaque; armor_store store;
    if (!armor_plan(a, armor, &store, error)) return false;
    if (!a->player) return true;
    const application_q3_combat_definition *d = definition(a);
    return application_q3_combat_word_write(a, a->player + 184 + d->armor.points_stat * 4, store.points, error) &&
        (!store.write_tier || application_q3_combat_word_write(a,
            a->player + 184 + d->armor.tier_stat * 4, store.tier, error));
}
bool application_q3_combat_health_write(void *opaque, float health, qa_error *error)
{
    application_q3_combat_actor *a = opaque; int32_t value;
    if (!application_q3_combat_actor_current(a, error) || !integer(health, &value, error)) return false;
    const application_q3_combat_definition *d = definition(a);
    return application_q3_combat_word_write(a, a->entity + d->fields.health, value, error) &&
        (!a->player || application_q3_combat_word_write(a,
            a->player + 184 + d->state.health_stat * 4, value, error));
}
bool application_q3_combat_empty_armor(void *opaque, float points,
    qa_regular_armor *out, bool *selected, qa_error *error)
{
    application_q3_combat_actor *a = opaque; qa_armor armor; int32_t value;
    if (!out || !selected || !integer(points, &value, error) || value < 0 ||
        !application_q3_combat_armor_read(a, &armor, error)) return false;
    *selected = a->player != 0;
    if (*selected) { armor.regular.points = points; *out = armor.regular; }
    return true;
}
bool application_q3_combat_state_read(void *opaque, qa_combat_state *out, qa_error *error)
{
    application_q3_combat_actor *a = opaque;
    if (!a || !out || !application_q3_combat_actor_current(a, error)) return false;
    const application_q3_combat_definition *d = definition(a);
    qa_combat_state state = {0}; int32_t inuse, health, damageable, flags;
    if (!application_q3_combat_word_read(a, a->entity + d->fields.inuse, &inuse, error) ||
        !application_q3_combat_word_read(a, a->entity + d->fields.health, &health, error) ||
        !application_q3_combat_word_read(a, a->entity + d->fields.takedamage, &damageable, error) ||
        !application_q3_combat_word_read(a, a->entity + d->reactions.flags, &flags, error) ||
        !application_q3_combat_armor_read(a, &state.armor, error) ||
        (inuse && !exact_float(health, &state.health, error))) return false;
    state.can_take_damage = inuse && damageable;
    state.invulnerable = ((uint32_t)flags & d->state.invulnerable) != 0;
    state.no_knockback = ((uint32_t)flags & d->state.no_knockback) != 0;
    if (d->state.mass_kind == Q3_COMBAT_MASS_CONSTANT) state.mass = d->state.mass.constant;
    else {
        int32_t mass;
        if (!application_q3_combat_word_read(a, a->entity + d->state.mass.offset, &mass, error)) return false;
        if (d->state.mass_kind == Q3_COMBAT_MASS_INT32) {
            if (!exact_float(mass, &state.mass, error)) return false;
        } else memcpy(&state.mass, &mass, sizeof(mass));
    }
    if (!isfinite(state.mass) || state.mass < 0)
        return fail(error, QA_ERROR_FORMAT, "Original Q3 source mass is not finite and nonnegative");
    if (a->player) {
        int32_t team;
        if (!application_q3_combat_word_read(a, a->player + 248 + d->state.team_stat * 4, &team, error)) return false;
        for (size_t i = 0; i < d->state.team_count; ++i)
            if (d->state.teams[i].value == team) { state.team = d->state.teams[i].team; break; }
        const application_q3_weapon_profile *weapons = application_q3_weapons_profile(a->role->weapons);
        if (weapons && weapons->has_match)
            for (size_t i = 0; i < weapons->team_count; ++i)
                if (weapons->teams[i].source == state.team) { state.team = weapons->teams[i].team; break; }
    }
    *out = state; return true;
}
bool application_q3_combat_notarget(const application_q3_combat_actor *a,
    bool *out, qa_error *error)
{
    int32_t flags;
    if (!a || !out || !application_q3_combat_word_read(a,
        a->entity + definition(a)->reactions.flags, &flags, error)) return false;
    *out = ((uint32_t)flags & definition(a)->state.notarget) != 0; return true;
}
bool application_q3_combat_velocity(const application_q3_combat_actor *a,
    qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!out || !application_q3_combat_actor_current(a, error) ||
        !qa_qvm_read(a->role->vm, a->player ? a->player + 32 : a->entity + 36,
            bytes, sizeof(bytes), error)) return false;
    qa_vec3 value = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    if (!qa_vec_finite(value)) return fail(error, QA_ERROR_FORMAT, "Original Q3 source velocity is not finite");
    *out = value; return application_q3_combat_actor_current(a, error);
}
