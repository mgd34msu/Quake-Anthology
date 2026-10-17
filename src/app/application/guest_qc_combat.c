#include "guest_qc_combat.h"
#include "guest_qc_armor.h"
#include "guest_mod_item_definition.h"
#include "control_frame.h"
#include "qa/application_qc_presentation.h"
#include "qa/pool.h"
#include "qa/qc_observation.h"
#include "qa/text.h"
#include <ctype.h>
#include <stdio.h>

typedef struct damage_location {
    application_qc_input_id role;
    uint32_t word, frame_word, argument;
    bool parameter;
} damage_location;
typedef struct damage_scale {
    qa_qc_inline_region region;
    uint32_t *scratch, *constants, *initial;
    size_t scratch_count, constant_count;
    bool qualified, transform, implicit, inflictor;
} damage_scale;
typedef struct synchronous_layout {
    uint32_t weapons[QA_Q1_WEAPON_COUNT], trace_attack, add_multi, apply_multi;
    uint32_t launch_spike, radius, lightning_damage;
    const qa_qc_definition *point, *normal, *trace_target, *owner;
} synchronous_layout;
typedef struct environment_layout {
    uint32_t water, postthink, hurt, door, secret, plat, train;
    const qa_qc_definition *other, *world;
    uint8_t *hazards;
    uint32_t statement_count;
} environment_layout;
struct application_qc_combat_profile {
    const qa_qc_program *program;
    application_qc_call damage;
    damage_location *locations;
    size_t location_count;
    const qa_qc_definition *health, *takedamage, *flags, *invincible;
    const qa_qc_definition *armorvalue, *armortype, *items, *velocity, *pain, *die, *self;
    uint32_t masks[3];
    qa_item_id armor_items[3], empty_item;
    float empty_absorption;
    qa_reaction *reactions;
    application_qc_armor_stage armor;
    damage_scale scale;
    synchronous_layout synchronous;
    environment_layout environment;
    qa_qc_inline_region regions[2];
    size_t region_count;
    bool armor_declared, empty_declared;
};
typedef struct combat_actor {
    struct combat_actor *next,*previous;
    application_qc_combat *owner;
    size_t storage_slot;
    qa_actor_id actor;
    uint32_t slot;
    int32_t reference;
    uint64_t serial;
    unsigned calls;
    bool bound, restoring, retired;
} combat_actor;
typedef struct damage_frame {
    struct damage_frame *previous;
    combat_actor *actor;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result *result;
    qa_qc_call_next next;
    float regular_scale;
    unsigned reaction_depth;
    bool health_written;
} damage_frame;
typedef struct incoming_damage {
    struct incoming_damage *previous;
    combat_actor *actor;
    const qa_damage_request *request;
    qa_damage_observer *observer;
    qa_damage_result *result;
    bool entered;
} incoming_damage;
typedef struct synchronous_trace {
    struct synchronous_trace *next;
    qa_actor_id target;
    qa_vec3 point, normal;
} synchronous_trace;
typedef struct synchronous_attack {
    struct synchronous_attack *previous;
    qa_actor_id actor;
    qa_item_id weapon;
    int32_t reference;
    uint32_t function;
    uint64_t time_ns;
    synchronous_trace *traces;
    qa_unified_frame_lease *storage;
} synchronous_attack;
typedef struct radius_attack {
    struct radius_attack *previous;
    qa_actor_id inflictor, attacker;
} radius_attack;
struct application_qc_combat {
    struct application_qc_state *engine;
    const application_qc_combat_profile *profile;
    combat_actor *actors;
    qa_actor_index index;
    qa_unified_frame_lease *actor_storage;
    qa_pool actor_pool;
    damage_frame *frame;
    incoming_damage *incoming;
    synchronous_attack *synchronous;
    radius_attack *radius;
    uint64_t sequence;
    unsigned calls;
};
static bool reject(qa_error *error, const char *message)
{ return application_fail(error, QA_ERROR_FORMAT, message); }
static bool called(qa_qc_opcode opcode)
{ return opcode >= QA_QC_CALL0 && opcode <= QA_QC_CALL8; }
static bool role(application_qc_input_id id)
{ return id == QC_INPUT_SELF || id == QC_INPUT_ATTACKER || id == QC_INPUT_INFLICTOR || id == QC_INPUT_AMOUNT; }
static const qa_qc_definition *definition_at(const qa_qc_program *program, uint32_t word, qa_qc_value_type type)
{
    qa_qc_program_info info = qa_qc_program_describe(program);
    for (uint32_t i = 0; i < info.global_count; ++i) {
        const qa_qc_definition *definition = qa_qc_program_global(program, i);
        if (definition->offset == word && definition->type == type) return definition;
    }
    return NULL;
}
static bool source_field(const qa_qc_program *, const uint8_t *, uint32_t, const char *, qa_qc_value_type);
static bool call_layout(application_qc_combat_profile *profile, qa_error *error)
{
    const qa_qc_function *function = qa_qc_program_function(profile->program, profile->damage.function);
    if (!function || function->first_statement <= 0 || function->named_builtin ||
        function->parameter_count != profile->damage.argument_count)
        return reject(error, "QC damage requires its actual compiled Source function");
    size_t extent = profile->damage.argument_count + profile->damage.global_count;
    if (extent > SIZE_MAX / sizeof(*profile->locations)) return reject(error, "QC damage call layout is too large");
    profile->locations = extent ? calloc(extent, sizeof(*profile->locations)) : NULL;
    if (extent && !profile->locations) return application_fail(error, QA_ERROR_MEMORY, "Owning QC damage call layout");
    uint32_t parameter = function->parameter_start;
    uint64_t found = 0;
    for (size_t i = 0; i < profile->damage.argument_count; ++i) {
        const application_qc_value *value = profile->damage.arguments + i;
        if (value->kind == QC_VALUE_INPUT && role(value->source)) {
            qa_qc_value_type type = value->source == QC_INPUT_AMOUNT ? QA_QC_FLOAT : QA_QC_ENTITY;
            if (function->parameter_sizes[i] != 1 || !definition_at(profile->program, parameter, type))
                return reject(error, "QC damage argument differs from its actual typed Source parameter");
            profile->locations[profile->location_count++] = (damage_location){value->source, 4 + 3 * (uint32_t)i, parameter, (uint32_t)i, true};
            found |= UINT64_C(1) << value->source;
        }
        parameter += function->parameter_sizes[i];
    }
    for (size_t i = 0; i < profile->damage.global_count; ++i) {
        const application_qc_global *global = profile->damage.globals + i;
        uint32_t word = global->definition->offset;
        uint32_t width = global->definition->type == QA_QC_VECTOR ? 3 : 1;
        if (word < 28 || (word < function->parameter_start + function->local_words &&
            word + width > function->parameter_start))
            return reject(error, "QC damage context overlaps captured call staging or private locals");
        if (global->value.kind != QC_VALUE_INPUT || !role(global->value.source)) continue;
        qa_qc_value_type type = global->value.source == QC_INPUT_AMOUNT ? QA_QC_FLOAT : QA_QC_ENTITY;
        if (global->definition->type != type) return reject(error, "QC damage global differs from its declared semantic type");
        profile->locations[profile->location_count++] = (damage_location){global->value.source, word, word, 0, false};
        found |= UINT64_C(1) << global->value.source;
    }
    const uint64_t required = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_ATTACKER) |
        (UINT64_C(1) << QC_INPUT_INFLICTOR) | (UINT64_C(1) << QC_INPUT_AMOUNT);
    return (found & required) == required || reject(error, "QC damage call omits an actual target, attacker, inflictor or amount");
}
static uint32_t source_void_function(const qa_qc_program *program, const char *name,
    const qa_qc_value_type *parameters, size_t count, const uint8_t *written)
{
    uint32_t index;
    const qa_qc_function *function = qa_qc_program_find_function(program, name, &index);
    const qa_qc_definition *global = qa_qc_program_find_global(program, name);
    int32_t initial; qa_error ignored = {0};
    if (!function || function->first_statement <= 0 || function->named_builtin ||
        function->parameter_count != count || !global || global->type != QA_QC_FUNCTION ||
        written[global->offset] || !qa_qc_program_initial_int(program, global->offset, &initial, &ignored) ||
        initial != (int32_t)index) return 0;
    uint32_t word = function->parameter_start;
    for (size_t i = 0; i < count; ++i) {
        uint32_t width = parameters[i] == QA_QC_VECTOR ? 3u : 1u;
        if (function->parameter_sizes[i] != width || !definition_at(program, word, parameters[i])) return 0;
        word += width;
    }
    for (uint32_t pc = (uint32_t)function->first_statement,
        end = qa_qc_program_function_end(program, function); pc < end; ++pc) {
        const qa_qc_statement *statement = qa_qc_program_statement(program, pc);
        if ((statement->opcode == QA_QC_RETURN || statement->opcode == QA_QC_DONE) && statement->a) return 0;
    }
    return index;
}
static void synchronous_prepare(application_qc_combat_profile *profile, const uint8_t *written)
{
    const qa_qc_program *program = profile->program;
    synchronous_layout *layout = &profile->synchronous;
    layout->point = qa_qc_program_find_global(program, "trace_endpos");
    layout->normal = qa_qc_program_find_global(program, "trace_plane_normal");
    layout->trace_target = qa_qc_program_find_global(program, "trace_ent");
    if (!layout->point || layout->point->type != QA_QC_VECTOR ||
        !layout->normal || layout->normal->type != QA_QC_VECTOR ||
        !layout->trace_target || layout->trace_target->type != QA_QC_ENTITY) {
        layout->point = layout->normal = layout->trace_target = NULL;
    }
    static const char *const weapons[QA_Q1_WEAPON_COUNT] = {
        [QA_Q1_AXE] = "W_FireAxe", [QA_Q1_SHOTGUN] = "W_FireShotgun",
        [QA_Q1_SUPER_SHOTGUN] = "W_FireSuperShotgun", [QA_Q1_NAILGUN] = "W_FireSpikes",
        [QA_Q1_SUPER_NAILGUN] = "W_FireSuperSpikes", [QA_Q1_GRENADE] = "W_FireGrenade",
        [QA_Q1_ROCKET] = "W_FireRocket", [QA_Q1_LIGHTNING] = "W_FireLightning"};
    static const qa_qc_value_type scalar[] = {QA_QC_FLOAT};
    for (size_t i = 0; i < QA_Q1_WEAPON_COUNT; ++i) if (weapons[i])
        layout->weapons[i] = source_void_function(program, weapons[i],
            i == QA_Q1_NAILGUN ? scalar : NULL, i == QA_Q1_NAILGUN ? 1 : 0, written);
    static const qa_qc_value_type trace_parameters[] = {QA_QC_FLOAT, QA_QC_VECTOR};
    static const qa_qc_value_type multi_parameters[] = {QA_QC_ENTITY, QA_QC_FLOAT};
    layout->trace_attack = source_void_function(program, "TraceAttack", trace_parameters, 2, written);
    layout->add_multi = source_void_function(program, "AddMultiDamage", multi_parameters, 2, written);
    layout->apply_multi = source_void_function(program, "ApplyMultiDamage", NULL, 0, written);
    static const qa_qc_value_type spike_parameters[] = {QA_QC_VECTOR, QA_QC_VECTOR};
    static const qa_qc_value_type lightning_parameters[] = {QA_QC_VECTOR, QA_QC_VECTOR, QA_QC_ENTITY, QA_QC_FLOAT};
    static const qa_qc_value_type radius_parameters[] = {QA_QC_ENTITY, QA_QC_ENTITY, QA_QC_FLOAT, QA_QC_ENTITY, QA_QC_STRING};
    layout->launch_spike = source_void_function(program, "launch_spike", spike_parameters, 2, written);
    layout->lightning_damage = source_void_function(program, "LightningDamage", lightning_parameters, 4, written);
    const qa_qc_function *radius = qa_qc_program_find_function(program, "T_RadiusDamage", NULL);
    if (radius && (radius->parameter_count == 4 || radius->parameter_count == 5))
        layout->radius = source_void_function(program, "T_RadiusDamage", radius_parameters, radius->parameter_count, written);
    const qa_qc_definition *owner = qa_qc_program_find_global(program, "owner");
    if (owner && source_field(program, written, owner->offset, "owner", QA_QC_ENTITY))
        layout->owner = qa_qc_program_find_field(program, "owner");
}
typedef enum environment_value_kind {
    ENVIRONMENT_UNKNOWN, ENVIRONMENT_CONSTANT, ENVIRONMENT_DAMAGE, ENVIRONMENT_WATERLEVEL
} environment_value_kind;
typedef struct environment_value {
    environment_value_kind kind;
    float coefficient;
    uint32_t entry;
} environment_value;
static bool environment_origin(const qa_qc_program *program, const qa_qc_function *function,
    uint32_t *before, uint32_t *word, uint32_t *entry, const qa_qc_statement **out, qa_error *error)
{
    *out = NULL; *entry = *before;
    while (*before > (uint32_t)function->first_statement) {
        const qa_qc_statement *statement = qa_qc_program_statement(program, --*before);
        if (called(statement->opcode) || statement->opcode == QA_QC_IF ||
            statement->opcode == QA_QC_IFNOT || statement->opcode == QA_QC_GOTO ||
            statement->opcode == QA_QC_RETURN || statement->opcode == QA_QC_DONE) return true;
        qa_qc_statement_access access;
        if (!qa_qc_program_statement_access(program, *before, &access, error)) return false;
        bool writes = false;
        for (uint32_t i = 0; i < access.write_count; ++i) writes |= access.write[i] == *word;
        if (!writes) continue;
        *entry = *before;
        if (statement->opcode == QA_QC_STORE_F || statement->opcode == QA_QC_STORE_V) {
            *word = statement->a + (*word - statement->b);
            continue;
        }
        *out = statement; return true;
    }
    return true;
}
static bool environment_term(application_qc_combat_profile *profile, const qa_qc_function *function,
    uint32_t before, uint32_t word, const uint8_t *written, environment_value *out, qa_error *error)
{
    *out = (environment_value){0};
    const qa_qc_program *program = profile->program;
    const qa_qc_statement *origin; uint32_t entry;
    if (!environment_origin(program, function, &before, &word, &entry, &origin, error)) return false;
    int32_t initial;
    if (!origin) {
        if (!written[word] && definition_at(program, word, QA_QC_FLOAT)) {
            if (!qa_qc_program_initial_int(program, word, &initial, error)) return false;
            float value; memcpy(&value, &initial, sizeof(value));
            if (isfinite(value)) *out = (environment_value){ENVIRONMENT_CONSTANT, value, entry};
        }
    } else if (origin->opcode == QA_QC_LOAD_F && origin->a == profile->self->offset &&
        !written[origin->b] && definition_at(program, origin->b, QA_QC_FIELD)) {
        if (!qa_qc_program_initial_int(program, origin->b, &initial, error)) return false;
        const qa_qc_game_fields *fields = qa_qc_program_resolved_fields(program);
        if (fields->dmg && fields->dmg->type == QA_QC_FLOAT && initial == fields->dmg->offset)
            *out = (environment_value){ENVIRONMENT_DAMAGE, 1, entry};
        else if (fields->waterlevel && fields->waterlevel->type == QA_QC_FLOAT && initial == fields->waterlevel->offset)
            *out = (environment_value){ENVIRONMENT_WATERLEVEL, 1, entry};
    }
    return true;
}
static bool environment_amount(application_qc_combat_profile *profile, const qa_qc_function *function,
    uint32_t before, uint32_t word, const uint8_t *written, environment_value *out, qa_error *error)
{
    uint32_t source_before = before, source_word = word;
    const qa_qc_statement *origin; uint32_t entry;
    if (!environment_origin(profile->program, function, &before, &word, &entry, &origin, error)) return false;
    if (!origin || origin->opcode != QA_QC_MUL_F)
        return environment_term(profile, function, source_before, source_word, written, out, error);
    environment_value a, b;
    if (!environment_term(profile, function, before, origin->a, written, &a, error) ||
        !environment_term(profile, function, before, origin->b, written, &b, error)) return false;
    *out = (environment_value){0};
    if (a.entry < entry) entry = a.entry;
    if (b.entry < entry) entry = b.entry;
    if (a.kind == ENVIRONMENT_CONSTANT && b.kind >= ENVIRONMENT_DAMAGE)
        *out = (environment_value){b.kind, a.coefficient * b.coefficient, entry};
    else if (b.kind == ENVIRONMENT_CONSTANT && a.kind >= ENVIRONMENT_DAMAGE)
        *out = (environment_value){a.kind, a.coefficient * b.coefficient, entry};
    return true;
}
static bool environment_entry_safe(const qa_qc_program *program, const qa_qc_function *function,
    uint32_t entry, uint32_t call)
{
    for (uint32_t pc = (uint32_t)function->first_statement,
        end = qa_qc_program_function_end(program, function); pc < end; ++pc) {
        if (pc >= entry && pc <= call) continue;
        const qa_qc_statement *statement = qa_qc_program_statement(program, pc);
        int64_t target;
        if (statement->opcode == QA_QC_GOTO) target = (int64_t)pc + (int16_t)statement->a;
        else if (statement->opcode == QA_QC_IF || statement->opcode == QA_QC_IFNOT)
            target = (int64_t)pc + (int16_t)statement->b;
        else continue;
        if (target > entry && target <= call) return false;
    }
    return true;
}
static bool environment_prepare(application_qc_combat_profile *profile, const uint8_t *written,
    qa_error *error)
{
    const qa_qc_program *program = profile->program;
    environment_layout *layout = &profile->environment;
    layout->water = source_void_function(program, "WaterMove", NULL, 0, written);
    layout->postthink = source_void_function(program, "PlayerPostThink", NULL, 0, written);
    layout->hurt = source_void_function(program, "hurt_touch", NULL, 0, written);
    layout->door = source_void_function(program, "door_blocked", NULL, 0, written);
    layout->secret = source_void_function(program, "secret_blocked", NULL, 0, written);
    layout->plat = source_void_function(program, "plat_crush", NULL, 0, written);
    layout->train = source_void_function(program, "train_blocked", NULL, 0, written);
    layout->other = qa_qc_program_find_global(program, "other");
    layout->world = qa_qc_program_find_global(program, "world");
    uint32_t functions[] = {layout->water, layout->postthink, layout->hurt, layout->door,
        layout->secret, layout->plat, layout->train};
    const damage_location *amount = NULL;
    for (size_t i = 0; i < profile->location_count; ++i)
        if (profile->locations[i].role == QC_INPUT_AMOUNT) {
            if (amount || !profile->locations[i].parameter) return true;
            amount = profile->locations + i;
        }
    if (!amount) return true;
    qa_qc_program_info info = qa_qc_program_describe(program);
    layout->hazards = calloc(info.statement_count, sizeof(*layout->hazards));
    if (info.statement_count && !layout->hazards)
        return application_fail(error, QA_ERROR_MEMORY, "Owning QC environmental call sites");
    layout->statement_count = info.statement_count;
    const qa_qc_function *damage = qa_qc_program_function(program, profile->damage.function);
    const qa_qc_definition *damage_global = qa_qc_program_find_global(program, damage->name);
    for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); ++i) {
        if (!functions[i]) continue;
        const qa_qc_function *function = qa_qc_program_function(program, functions[i]);
        for (uint32_t pc = (uint32_t)function->first_statement,
            end = qa_qc_program_function_end(program, function); pc < end; ++pc) {
            const qa_qc_statement *statement = qa_qc_program_statement(program, pc);
            if (!called(statement->opcode) || statement->a != damage_global->offset ||
                (uint32_t)(statement->opcode - QA_QC_CALL0) != profile->damage.argument_count) continue;
            environment_value value;
            if (!environment_amount(profile, function, pc, amount->word, written, &value, error)) return false;
            if (value.kind == ENVIRONMENT_UNKNOWN || !environment_entry_safe(program, function, value.entry, pc)) continue;
            qa_hazard hazard;
            if (functions[i] == layout->water) {
                if (value.kind == ENVIRONMENT_DAMAGE && value.coefficient == 1) hazard = QA_HAZARD_DROWN;
                else if (value.kind == ENVIRONMENT_WATERLEVEL && value.coefficient == 10) hazard = QA_HAZARD_LAVA;
                else if (value.kind == ENVIRONMENT_WATERLEVEL && value.coefficient == 4) hazard = QA_HAZARD_SLIME;
                else continue;
            } else if (functions[i] == layout->postthink) {
                if (value.kind != ENVIRONMENT_CONSTANT || value.coefficient != 5) continue;
                hazard = QA_HAZARD_FALL;
            } else {
                if (!(value.kind == ENVIRONMENT_DAMAGE && value.coefficient == 1) &&
                    !(functions[i] == layout->plat && value.kind == ENVIRONMENT_CONSTANT && value.coefficient == 1)) continue;
                hazard = functions[i] == layout->hurt ? QA_HAZARD_TRIGGER : QA_HAZARD_CRUSH;
            }
            layout->hazards[pc] = (uint8_t)(hazard + 1);
        }
    }
    return true;
}
static bool source_field(const qa_qc_program *program, const uint8_t *written,
    uint32_t word, const char *name, qa_qc_value_type type)
{
    const qa_qc_definition *field = qa_qc_program_find_field(program, name);
    int32_t initial; qa_error ignored = {0};
    return field && field->type == type && definition_at(program, word, QA_QC_FIELD) &&
        !written[word] && qa_qc_program_initial_int(program, word, &initial, &ignored) &&
        initial == field->offset;
}
static bool quad_constant(const qa_qc_program *program, const uint8_t *written,
    uint32_t word, float expected)
{
    int32_t initial; float value; qa_error ignored = {0};
    if (!definition_at(program, word, QA_QC_FLOAT) || written[word] ||
        !qa_qc_program_initial_int(program, word, &initial, &ignored)) return false;
    memcpy(&value, &initial, sizeof(value)); return value == expected;
}
static bool quad_door(const qa_qc_program *program, const uint8_t *written, uint32_t word)
{
    int32_t initial; qa_error ignored = {0};
    if (!definition_at(program, word, QA_QC_STRING) || written[word] ||
        !qa_qc_program_initial_int(program, word, &initial, &ignored) || initial < 0) return false;
    qa_bytes strings = qa_qc_program_strings(program);
    return (uint32_t)initial < strings.size && strings.size - (uint32_t)initial >= sizeof("door") &&
        !memcmp(strings.data + initial, "door", sizeof("door"));
}
static bool quad_product(const qa_qc_program *program, const uint8_t *written,
    uint32_t pc, uint32_t amount, float factor)
{
    const qa_qc_statement *multiply = qa_qc_program_statement(program, pc);
    const qa_qc_statement *store = qa_qc_program_statement(program, pc + 1);
    return multiply->opcode == QA_QC_MUL_F && store->opcode == QA_QC_STORE_F &&
        store->a == multiply->c && store->b == amount &&
        ((multiply->a == amount && quad_constant(program, written, multiply->b, factor)) ||
         (multiply->b == amount && quad_constant(program, written, multiply->a, factor)));
}
static void scale_discover(application_qc_combat_profile *profile, const uint8_t *written)
{
    const qa_qc_program *program = profile->program;
    const qa_qc_function *function = qa_qc_program_function(program, profile->damage.function);
    const qa_qc_definition *time = qa_qc_program_find_global(program, "time");
    const qa_qc_definition *deathmatch = qa_qc_program_find_global(program, "deathmatch");
    const damage_location *attacker = NULL, *inflictor = NULL, *amount = NULL;
    for (size_t i = 0; i < profile->location_count; ++i) {
        const damage_location *location = profile->locations + i;
        const damage_location **selected = location->role == QC_INPUT_ATTACKER ? &attacker :
            location->role == QC_INPUT_INFLICTOR ? &inflictor : location->role == QC_INPUT_AMOUNT ? &amount : NULL;
        if (!selected) continue;
        if (*selected || !location->parameter) return;
        *selected = location;
    }
    if (!attacker || !inflictor || !amount || !time || time->type != QA_QC_FLOAT) return;
    uint32_t end = qa_qc_program_function_end(program, function);
    qa_qc_inline_region found = {0}; bool needs_inflictor = false;
    for (uint32_t pc = (uint32_t)function->first_statement; end - pc > 4; ++pc) {
        const qa_qc_statement *load = qa_qc_program_statement(program, pc);
        const qa_qc_statement *compare = qa_qc_program_statement(program, pc + 1);
        if (load->opcode != QA_QC_LOAD_F || load->a != attacker->frame_word ||
            !source_field(program, written, load->b, "super_damage_finished", QA_QC_FLOAT) ||
            compare->opcode != QA_QC_GT || compare->a != load->c || compare->b != time->offset) continue;
        uint32_t branch_pc = pc + 2, predicate = compare->c;
        const qa_qc_statement *door = qa_qc_program_statement(program, branch_pc);
        bool qw = door->opcode == QA_QC_LOAD_S;
        if (qw) {
            if (end - branch_pc <= 3 || door->a != inflictor->frame_word ||
                !source_field(program, written, door->b, "classname", QA_QC_STRING)) continue;
            const qa_qc_statement *different = qa_qc_program_statement(program, branch_pc + 1);
            const qa_qc_statement *both = qa_qc_program_statement(program, branch_pc + 2);
            if (different->opcode != QA_QC_NE_S ||
                !((different->a == door->c && quad_door(program, written, different->b)) ||
                  (different->b == door->c && quad_door(program, written, different->a))) ||
                both->opcode != QA_QC_AND ||
                !((both->a == predicate && both->b == different->c) ||
                  (both->b == predicate && both->a == different->c))) continue;
            predicate = both->c; branch_pc += 3;
        }
        const qa_qc_statement *branch = qa_qc_program_statement(program, branch_pc);
        int64_t join = (int64_t)branch_pc + (int16_t)branch->b;
        uint32_t body = branch_pc + 1;
        if (branch->opcode != QA_QC_IFNOT || branch->a != predicate || join >= end) continue;
        if (!qw) {
            if (join != (int64_t)body + 2 || !quad_product(program, written, body, amount->frame_word, 4)) continue;
        } else {
            if (join != (int64_t)body + 7 || !deathmatch || deathmatch->type != QA_QC_FLOAT) continue;
            const qa_qc_statement *mode = qa_qc_program_statement(program, body);
            const qa_qc_statement *choose = qa_qc_program_statement(program, body + 1);
            const qa_qc_statement *skip = qa_qc_program_statement(program, body + 4);
            if (mode->opcode != QA_QC_EQ_F ||
                !((mode->a == deathmatch->offset && quad_constant(program, written, mode->b, 4)) ||
                  (mode->b == deathmatch->offset && quad_constant(program, written, mode->a, 4))) ||
                choose->opcode != QA_QC_IFNOT || choose->a != mode->c ||
                (int64_t)body + 1 + (int16_t)choose->b != (int64_t)body + 5 ||
                skip->opcode != QA_QC_GOTO || (int64_t)body + 4 + (int16_t)skip->a != join ||
                !quad_product(program, written, body + 2, amount->frame_word, 8) ||
                !quad_product(program, written, body + 5, amount->frame_word, 4)) continue;
        }
        if (found.exit) return;
        found = (qa_qc_inline_region){.function = profile->damage.function,
            .entry = pc, .exit = (uint32_t)join, .saved_word = amount->frame_word};
        needs_inflictor = qw;
    }
    if (found.exit) {
        profile->scale.region = found;
        profile->scale.implicit = true; profile->scale.inflictor = needs_inflictor;
    }
}
static bool immutable_layout(application_provider *provider, application_qc_combat_profile *profile,
    bool discover_scale, qa_error *error)
{
    const qa_qc_program *program = profile->program;
    qa_qc_program_info info = qa_qc_program_describe(program);
    uint8_t *written = calloc(info.global_words, 1);
    if (!written) return application_fail(error, QA_ERROR_MEMORY, "Qualifying actual QC combat globals");
    bool ok = true;
    for (uint32_t i = 0; ok && i < info.statement_count; ++i) {
        qa_qc_statement_access access;
        ok = qa_qc_program_statement_access(program, i, &access, error);
        for (uint32_t j = 0; ok && j < access.write_count; ++j) written[access.write[j]] = 1;
    }
    const qa_qc_function *damage = qa_qc_program_function(program, profile->damage.function);
    const qa_qc_definition *global = damage ? qa_qc_program_find_global(program, damage->name) : NULL;
    int32_t initial;
    if (ok && (!global || global->type != QA_QC_FUNCTION || written[global->offset] ||
        !qa_qc_program_initial_int(program, global->offset, &initial, error) || initial != (int32_t)profile->damage.function))
        ok = reject(error, "QC damage entry is not its immutable authored function global");
    for (uint32_t i = 0; ok && i < info.field_count; ++i) {
        const qa_qc_definition *field = qa_qc_program_field(program, i);
        if (field->type != QA_QC_FLOAT || strncmp(field->name, "items", 5)) continue;
        const char *suffix = field->name + 5;
        bool numeric = true;
        for (const char *at = suffix; *at; ++at) numeric = numeric && isdigit((unsigned char)*at);
        if (!numeric) continue;
        uint32_t masks[3]; bool found = true;
        for (unsigned j = 0; j < 3; ++j) {
            char name[128]; int count = snprintf(name, sizeof(name), "IT%s_ARMOR%u", suffix, j + 1);
            const qa_qc_definition *constant = count > 0 && (size_t)count < sizeof(name) ? qa_qc_program_find_global(program, name) : NULL;
            if (!constant) { found = false; break; }
            float value;
            if (constant->type != QA_QC_FLOAT || written[constant->offset] ||
                !qa_qc_program_initial_int(program, constant->offset, &initial, error)) { ok = false; break; }
            memcpy(&value, &initial, 4);
            if (!isfinite(value) || value < 1 || value > UINT32_C(0x80000000) || truncf(value) != value) { ok = false; break; }
            masks[j] = (uint32_t)value;
            if (masks[j] & (masks[j] - 1)) { ok = false; break; }
        }
        if (!ok) { reject(error, "QC armor selection lacks immutable authored single-bit constants"); break; }
        if (!found) continue;
        if (profile->items || masks[0] == masks[1] || masks[0] == masks[2] || masks[1] == masks[2]) {
            ok = reject(error, "QC armor inventory is ambiguous or overlaps"); break;
        }
        profile->items = field; memcpy(profile->masks, masks, sizeof(masks));
    }
    if (ok && !profile->items) ok = reject(error, "QC combat has no actual authored armor inventory");
    static const char *const names[] = {"q1:item_armor1", "q1:item_armor2", "q1:item_armorInv"};
    for (size_t i = 0; ok && i < 3; ++i)
        ok = qa_strings_intern(qa_session_strings(provider->application->session),
            (qa_bytes){(const uint8_t *)names[i], strlen(names[i])}, profile->armor_items + i, error);
    profile->reactions = ok ? calloc(info.statement_count, sizeof(*profile->reactions)) : NULL;
    if (ok && !profile->reactions) ok = application_fail(error, QA_ERROR_MEMORY, "Owning actual QC reaction call sites");
    size_t reactions = 0;
    for (uint32_t i = 0; ok && i < info.function_count; ++i) {
        const qa_qc_function *function = qa_qc_program_function(program, i);
        if (function->first_statement <= 0 || function->named_builtin) continue;
        uint32_t start = (uint32_t)function->first_statement, end = qa_qc_program_function_end(program, function);
        for (uint32_t pc = start; pc < end; ++pc) {
            const qa_qc_statement *call = qa_qc_program_statement(program, pc);
            if (!called(call->opcode)) continue;
            uint32_t word = call->a;
            for (uint32_t cursor = pc; cursor > start;) {
                const qa_qc_statement *statement = qa_qc_program_statement(program, --cursor);
                if (statement->opcode == QA_QC_LOAD_FN && statement->c == word) {
                    if (!written[statement->b] && definition_at(program, statement->b, QA_QC_FIELD) &&
                        qa_qc_program_initial_int(program, statement->b, &initial, error)) {
                        qa_reaction reaction = initial == profile->pain->offset ? QA_REACTION_PAIN :
                            initial == profile->die->offset ? QA_REACTION_DEATH : QA_REACTION_NONE;
                        profile->reactions[pc] = reaction; reactions += reaction != QA_REACTION_NONE;
                    }
                    break;
                }
                if (statement->opcode == QA_QC_STORE_FN && statement->b == word) { word = statement->a; continue; }
                if (called(statement->opcode) || statement->opcode == QA_QC_IF || statement->opcode == QA_QC_IFNOT || statement->opcode == QA_QC_GOTO) break;
                qa_qc_statement_access access;
                if (!qa_qc_program_statement_access(program, cursor, &access, error)) { ok = false; break; }
                bool changed = false;
                for (uint32_t j = 0; j < access.write_count; ++j) changed |= access.write[j] == word;
                if (changed) break;
            }
        }
    }
    if (ok) {
        synchronous_prepare(profile, written);
        ok = environment_prepare(profile, written, error);
        if (ok && discover_scale) scale_discover(profile, written);
    }
    free(written);
    return ok && (reactions || reject(error, "QC combat has no typed original pain or death call sites"));
}
static bool scale_parse(application_qc_combat_profile *, const qa_json_document *, qa_json_id, qa_error *);
static void scale_free(damage_scale *scale)
{
    free(scale->scratch); free(scale->constants); free(scale->initial); *scale = (damage_scale){0};
}
static bool combat_profile_prepare(application_provider *provider, const qa_json_document *doc,
    qa_json_id root, application_qc_combat_profile *profile, qa_error *error)
{
    bool declared = root != QA_JSON_NONE;
    if (declared && (qa_json_type(doc, root) != QA_JSON_OBJECT || !provider->state.qc.qualified))
        return reject(error, "QC combat requires its actual declaration and qualified program");
    uint64_t inputs = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_ATTACKER) |
        (UINT64_C(1) << QC_INPUT_INFLICTOR) | (UINT64_C(1) << QC_INPUT_AMOUNT) |
        (UINT64_C(1) << QC_INPUT_KNOCKBACK) | (UINT64_C(1) << QC_INPUT_POINT) |
        (UINT64_C(1) << QC_INPUT_DIRECTION) | (UINT64_C(1) << QC_INPUT_NORMAL) | (UINT64_C(1) << QC_INPUT_TIME);
    if (declared) {
        if (!application_qc_call_parse(doc, qa_json_get(doc, root, "damage"), profile->program,
            inputs, false, &profile->damage, error)) return false;
    } else {
        const qa_qc_function *damage = qa_qc_program_find_function(profile->program, "T_Damage", &profile->damage.function);
        if (!damage || damage->first_statement <= 0 || damage->named_builtin || damage->parameter_count != 4)
            return false;
        static const application_qc_input_id arguments[] = {
            QC_INPUT_SELF, QC_INPUT_INFLICTOR, QC_INPUT_ATTACKER, QC_INPUT_AMOUNT};
        profile->damage.argument_count = 4;
        for (size_t i = 0; i < 4; ++i)
            profile->damage.arguments[i] = (application_qc_value){.kind = QC_VALUE_INPUT, .source = arguments[i]};
    }
    if (!call_layout(profile, error)) return false;
    profile->health = qa_qc_program_find_field(profile->program, "health");
    profile->takedamage = qa_qc_program_find_field(profile->program, "takedamage");
    profile->flags = qa_qc_program_find_field(profile->program, "flags");
    profile->invincible = qa_qc_program_find_field(profile->program, "invincible_finished");
    profile->armorvalue = qa_qc_program_find_field(profile->program, "armorvalue");
    profile->armortype = qa_qc_program_find_field(profile->program, "armortype");
    profile->velocity = qa_qc_program_find_field(profile->program, "velocity");
    profile->pain = qa_qc_program_find_field(profile->program, "th_pain");
    profile->die = qa_qc_program_find_field(profile->program, "th_die");
    profile->self = qa_qc_program_find_global(profile->program, "self");
    const qa_qc_definition *floats[] = {profile->health, profile->takedamage, profile->flags,
        profile->invincible, profile->armorvalue, profile->armortype};
    for (size_t i = 0; i < sizeof(floats) / sizeof(*floats); ++i)
        if (!floats[i] || floats[i]->type != QA_QC_FLOAT) return reject(error, "QC combat lacks an actual required Source float field");
    qa_json_id scale_node = declared ? qa_json_get(doc, root, "damageScale") : QA_JSON_NONE;
    if (!profile->velocity || profile->velocity->type != QA_QC_VECTOR || !profile->pain ||
        profile->pain->type != QA_QC_FUNCTION || !profile->die || profile->die->type != QA_QC_FUNCTION ||
        !profile->self || profile->self->type != QA_QC_ENTITY ||
        !immutable_layout(provider, profile, scale_node == QA_JSON_NONE, error))
        return reject(error, "QC combat lacks its actual typed Source reaction and armor layout");
    qa_json_id armor = declared ? qa_json_get(doc, root, "armorStage") : QA_JSON_NONE;
    if (armor != QA_JSON_NONE) {
        if (!application_qc_armor_parse(profile->program, doc, armor, &profile->armor, error) ||
            profile->armor.region.function != profile->damage.function ||
            !application_qc_armor_inputs(profile->program, &profile->damage, &profile->armor, false, error)) return false;
        profile->armor_declared = true;
    } else if (!application_qc_armor_compile(profile->program, &profile->damage,
        &profile->armor, &profile->armor_declared, error)) return false;
    if (!declared && !profile->armor_declared) return false;
    qa_error scale_error = {0};
    if (!scale_parse(profile, doc, scale_node, &scale_error)) {
        if (scale_node != QA_JSON_NONE || scale_error.code == QA_ERROR_MEMORY) {
            if (error) *error = scale_error;
            return false;
        }
        scale_free(&profile->scale);
    }
    if (profile->scale.qualified) profile->regions[profile->region_count++] = profile->scale.region;
    if (profile->armor_declared) profile->regions[profile->region_count++] = profile->armor.region;
    qa_json_id empty = declared ? qa_json_get(doc, root, "emptyArmor") : QA_JSON_NONE;
    if (empty != QA_JSON_NONE) {
        if (!application_mod_item_identity(doc, qa_json_get(doc, empty, "item"),
            qa_session_strings(provider->application->session), &profile->empty_item, error) ||
            !application_qc_declaration_number(doc, qa_json_get(doc, empty, "absorption"), &profile->empty_absorption, error) ||
            profile->empty_absorption < 0 || profile->empty_absorption > 1) return reject(error, "QC empty armor declaration is invalid");
        bool known = false;
        for (size_t i = 0; i < 3; ++i) known |= profile->empty_item == profile->armor_items[i];
        if (!known) return reject(error, "QC empty armor is not a Source Q1 armor identity");
        profile->empty_declared = true;
    }
    return true;
}
bool application_qc_combat_qualify(application_provider *provider, const qa_json_document *doc,
    qa_json_id root, qa_error *error)
{
    application_qc_combat_profile *profile = calloc(1, sizeof(*profile));
    if (!profile) return application_fail(error, QA_ERROR_MEMORY, "Owning QC combat metadata");
    profile->program = provider->state.qc.program;
    qa_error reason = {0};
    if (!combat_profile_prepare(provider, doc, root, profile, &reason)) {
        application_qc_combat_profile_free(profile);
        if (root == QA_JSON_NONE && reason.code != QA_ERROR_MEMORY) return true;
        if (error) *error = reason;
        return false;
    }
    provider->state.qc.combat_profile = profile;
    return true;
}
void application_qc_combat_profile_free(application_qc_combat_profile *profile)
{
    if (!profile) return;
    application_qc_call_free(&profile->damage); application_qc_armor_free(&profile->armor);
    free(profile->locations); free(profile->reactions); scale_free(&profile->scale);
    free(profile->environment.hazards); free(profile);
}
const qa_qc_inline_region *application_qc_combat_regions(const application_qc_combat_profile *profile, size_t *count)
{ *count = profile ? profile->region_count : 0; return profile ? profile->regions : NULL; }
static combat_actor *find(application_qc_combat *owner, qa_actor_id actor)
{
    size_t slot;
    return qa_actor_index_find(&owner->index,actor,&slot)?(combat_actor *)(uintptr_t)slot:NULL;
}
static void actor_reclaim(combat_actor *actor)
{
    if(actor->retired&&!actor->calls)
        qa_pool_release(&actor->owner->actor_pool,actor->storage_slot);
}
static combat_actor *actor_create(application_qc_combat *owner,qa_actor_id id,uint32_t physical,
    int32_t reference,qa_error *error)
{
    if(owner->actor_pool.head==SIZE_MAX&&!qa_pool_extend(&owner->actor_pool,
        qa_unified_frame_lease_arena(owner->actor_storage),owner->index.capacity,error))return NULL;
    size_t storage_slot;
    combat_actor *actor=qa_pool_take(&owner->actor_pool,&storage_slot);
    if(!actor){application_fail(error,QA_ERROR_MEMORY,"Owning QC combat callback context");return NULL;}
    *actor=(combat_actor){.next=owner->actors,.owner=owner,.storage_slot=storage_slot,.actor=id,.slot=physical,
        .reference=reference};
    if(actor->next)actor->next->previous=actor;
    owner->actors=actor;
    qa_actor_index_put(&owner->index,id,(size_t)(uintptr_t)actor);return actor;
}
static void actor_retire(combat_actor *actor)
{
    application_qc_combat *owner=actor->owner;
    qa_actor_index_remove(&owner->index,actor->actor);
    if(actor->previous)actor->previous->next=actor->next;else owner->actors=actor->next;
    if(actor->next)actor->next->previous=actor->previous;
    actor->bound=false;actor->retired=true;actor_reclaim(actor);
}
static bool held(const application_qc_combat *owner, qa_error *error)
{
    struct application_qc_state *engine = owner->engine;
    return (engine && engine->provider->state.qc.engine == engine &&
        engine->provider->state.qc.program == owner->profile->program &&
        engine->provider->state.qc.combat_profile == owner->profile &&
        engine->provider->state.qc.instance) || reject(error, "QC combat lost its actual retained Source owner");
}
static bool current(combat_actor *actor, qa_error *error)
{
    application_qc_combat *owner = actor->owner;
    if (!held(owner, error) || actor->retired) return false;
    qa_qc_instance *vm = owner->engine->provider->state.qc.instance;
    uint32_t slot; int32_t reference;
    if (!qa_qc_actor_observation_slot(vm, actor->actor, &slot, error) || slot != actor->slot ||
        !qa_qc_slot_reference(vm, slot, &reference, error) || reference != actor->reference)
        return reject(error, "QC combat changed its actual full-actor Source row");
    return true;
}
static float bits_float(uint32_t bits)
{ float value; memcpy(&value, &bits, 4); return value; }
static uint32_t float_bits(float value)
{ uint32_t bits; memcpy(&bits, &value, 4); return bits; }
static bool raw_float(combat_actor *actor, const qa_qc_definition *field, const qa_qc_store_event *prior,
    float *out, qa_error *error)
{
    if (prior && field->offset >= prior->word && field->offset - prior->word < prior->count) {
        *out = bits_float(prior->before[field->offset - prior->word]); return true;
    }
    return qa_qc_actor_observation_float(actor->owner->engine->provider->state.qc.instance,
        actor->slot, actor->actor, field->offset, out, error);
}
static bool raw_armor(combat_actor *actor, const qa_qc_store_event *prior, qa_armor *out, qa_error *error)
{
    const application_qc_combat_profile *profile = actor->owner->profile;
    float items, points, absorption;
    if (!raw_float(actor, profile->items, prior, &items, error) || !isfinite(items) ||
        !raw_float(actor, profile->armorvalue, prior, &points, error) ||
        !raw_float(actor, profile->armortype, prior, &absorption, error)) return false;
    *out = (qa_armor){0}; uint32_t flags = (uint32_t)qa_source_to_i32(items);
    for (size_t i = 3; i > 0; --i) if (flags & profile->masks[i - 1]) {
        out->regular = (qa_regular_armor){.kind = QA_ARMOR_Q1, .points = points,
            .item = profile->armor_items[i - 1], .protection.q1_absorption = absorption}; break;
    }
    return qa_armor_validate(out, error);
}
static bool read(void *context, qa_combat_state *out, qa_error *error)
{
    combat_actor *actor = context;
    const application_qc_combat_profile *profile = actor->owner->profile;
    float health, damage, invincible;
    if (!current(actor, error) || !raw_float(actor, profile->health, NULL, &health, error) ||
        !raw_armor(actor, NULL, &out->armor, error) ||
        !raw_float(actor, profile->takedamage, NULL, &damage, error) ||
        !raw_float(actor, profile->invincible, NULL, &invincible, error)) return false;
    out->health = health; out->mass = 200; out->can_take_damage = damage != 0;
    out->invulnerable = invincible > (float)((double)actor->owner->engine->source_time_ns / 1e9);
    out->no_knockback = false; out->team = (qa_team_ref){0};
    return true;
}
static bool write_health(void *context, double value, qa_error *error)
{
    combat_actor *actor = context;
    return current(actor, error) && qa_qc_project_entity_float(actor->owner->engine->provider->state.qc.instance,
        actor->reference, actor->owner->profile->health->offset, (float)value, error);
}
static bool validate_armor(void *context, const qa_armor *armor, qa_error *error)
{
    combat_actor *actor = context;
    if (!current(actor, error) || !qa_armor_validate(armor, error)) return false;
    if (armor->powered.kind != QA_POWER_NONE || (armor->regular.kind != QA_ARMOR_NONE && armor->regular.kind != QA_ARMOR_Q1))
        return reject(error, "QC primary cannot store foreign armor in its original Source fields");
    if (armor->regular.kind == QA_ARMOR_Q1) {
        bool known = false;
        for (size_t i = 0; i < 3; ++i) known |= actor->owner->profile->armor_items[i] == armor->regular.item;
        if (!known)
            return reject(error, "QC primary armor differs from its Source identity");
    }
    return true;
}
static bool write_armor(void *context, const qa_armor *armor, qa_error *error)
{
    combat_actor *actor = context;
    const application_qc_combat_profile *profile = actor->owner->profile;
    if (!validate_armor(context, armor, error)) return false;
    float value;
    if (!raw_float(actor, profile->items, NULL, &value, error) || !isfinite(value)) return false;
    uint32_t bits = (uint32_t)qa_source_to_i32(value) & ~(profile->masks[0] | profile->masks[1] | profile->masks[2]);
    if (armor->regular.kind == QA_ARMOR_Q1)
        for (size_t i = 0; i < 3; ++i) if (armor->regular.item == profile->armor_items[i]) bits |= profile->masks[i];
    int32_t source_bits;
    memcpy(&source_bits, &bits, sizeof(source_bits));
    qa_qc_instance *vm = actor->owner->engine->provider->state.qc.instance;
    return qa_qc_project_entity_float(vm, actor->reference, profile->armorvalue->offset,
        armor->regular.kind == QA_ARMOR_Q1 ? (float)armor->regular.points : 0, error) &&
        qa_qc_project_entity_float(vm, actor->reference, profile->armortype->offset,
        armor->regular.kind == QA_ARMOR_Q1 ? armor->regular.protection.q1_absorption : 0, error) &&
        qa_qc_project_entity_float(vm, actor->reference, profile->items->offset, (float)source_bits, error);
}
static bool empty_armor(void *context, double points, qa_regular_armor *out, bool *present, qa_error *error)
{
    combat_actor *actor = context;
    const application_qc_combat_profile *profile = actor->owner->profile;
    if (!current(actor, error)) return false;
    *present = profile->empty_declared;
    if (*present) {
        if (!isfinite(points) || points < 0)
            return reject(error, "QC empty armor points must be finite and nonnegative");
        *out = (qa_regular_armor){.kind = QA_ARMOR_Q1, .points = points,
            .item = profile->empty_item, .protection.q1_absorption = profile->empty_absorption};
    }
    return true;
}
static application_qc_inputs inputs_for(struct application_qc_state *engine, const qa_damage_request *request)
{
    return (application_qc_inputs){.self = request->target, .attacker = request->attack.attacker,
        .inflictor = request->attack.inflictor, .amount = (float)request->amount,
        .knockback = (float)request->knockback,
        .point = request->point, .direction = request->direction, .normal = request->normal,
        .time_ns = engine->source_time_ns};
}
static bool source_damage(void *context, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    combat_actor *actor = context;
    application_qc_combat *owner = actor->owner;
    if (combat != owner->engine->services.combat || !qa_actor_id_equal(actor->actor, request->target) ||
        !current(actor, error)) return reject(error, "QC canonical hit lost its actual Source target");
    *result = (qa_damage_result){0}; qa_combat_state state;
    if (!read(actor, &state, error)) return false;
    if (!state.can_take_damage) return true;
    incoming_damage incoming = {.previous = owner->incoming, .actor = actor,
        .request = request, .observer = observer, .result = result};
    owner->incoming = &incoming; ++owner->calls; ++actor->calls;
    application_qc_inputs inputs = inputs_for(owner->engine, request);
    uint32_t words[3];
    bool ok = application_qc_run_call(owner->engine, &owner->profile->damage, &inputs, words, error);
    --owner->calls; owner->incoming = incoming.previous; --actor->calls;actor_reclaim(actor);
    return ok && (incoming.entered || reject(error, "QC canonical hit did not reach its actual declared damage boundary"));
}
static qa_combat_binding binding(combat_actor *actor)
{
    const application_qc_combat_profile *profile = actor->owner->profile;
    return (qa_combat_binding){.context = actor, .read = read, .write_health = write_health,
        .validate_armor = validate_armor, .write_armor = write_armor, .empty_regular_armor = empty_armor,
        .source_damage = source_damage,
        .source_armor_stages = {profile->armor_declared && profile->armor.region.replaceable, profile->armor_declared},
        .has_primary_protection = {true, false}, .primary_protection = {actor->owner->engine->provider->owner, 0}};
}
bool application_qc_combat_create(struct application_qc_state *engine, qa_error *error)
{
    const application_qc_combat_profile *profile = engine->provider->state.qc.combat_profile;
    if (!profile) return true;
    if (engine->combat) return held(engine->combat, error);
    engine->combat = calloc(1, sizeof(*engine->combat));
    if (!engine->combat) return application_fail(error, QA_ERROR_MEMORY, "Owning QC combat callback contexts");
    const qa_actor_registry *registry=qa_session_actors(engine->services.session);
    engine->combat->actor_storage=application_control_storage_acquire(engine->provider->application,error);
    qa_arena *arena=engine->combat->actor_storage?
        qa_unified_frame_lease_arena(engine->combat->actor_storage):NULL;
    if (!arena || !qa_actor_index_create(&engine->combat->index,registry,arena,error) ||
        !qa_pool_prepare(&engine->combat->actor_pool,arena,qa_actors_capacity(registry),
            sizeof(combat_actor),_Alignof(combat_actor),error)) {
        qa_actor_index_destroy(&engine->combat->index);
        qa_unified_frame_lease_release(engine->combat->actor_storage);
        free(engine->combat); engine->combat = NULL;
        return false;
    }
    engine->combat->engine = engine; engine->combat->profile = profile; return true;
}
static bool eligible(application_qc_combat *owner, qa_qc_slot_binding slot)
{
    if (!slot.slot || !slot.actor.registry || (slot.kind != QA_QC_SLOT_OWNED && slot.kind != QA_QC_SLOT_BORROWED)) return false;
    if (slot.kind == QA_QC_SLOT_OWNED) return slot.owner == owner->engine->provider->owner;
    return application_provider_for(owner->engine->provider->application, slot.actor, QA_ROLE_COMBAT, "") == owner->engine->provider;
}
static bool admit(application_qc_combat *owner, qa_qc_slot_binding slot, int32_t reference, qa_error *error)
{
    if (!eligible(owner, slot)) return true;
    combat_actor *actor = find(owner, slot.actor);
    if (actor) {
        if (!current(actor, error)) return false;
        if (actor->restoring) return true;
        return !actor->bound || qa_combat_primary_current(owner->engine->services.combat, actor->actor,
            actor->serial, actor) || reject(error, "QC combat lost its actual canonical primary");
    }
    actor = actor_create(owner,slot.actor,slot.slot,reference,error);
    if (!actor) return false;
    qa_combat_binding source = binding(actor);
    if (!qa_combat_bind(owner->engine->services.combat, actor->actor, &source, true, error)) {
        actor_retire(actor);
        return false;
    }
    actor->serial = qa_combat_storage_serial(owner->engine->services.combat, actor->actor); actor->bound = true;
    return true;
}
bool application_qc_combat_bind(struct application_qc_state *engine, qa_qc_instance *vm,
    const qa_qc_entity_access *access, qa_error *error)
{
    if (!engine->combat || engine->projecting || !access->binding.actor.registry) return true;
    if (vm != engine->provider->state.qc.instance || !held(engine->combat, error)) return false;
    qa_application *application = engine->provider->application;
    if (application->operation == APPLICATION_PERSISTING && application->native_restore_image) return true;
    return admit(engine->combat, access->binding, access->reference, error);
}
bool application_qc_combat_health_owned(const struct application_qc_state *engine,
    qa_actor_id id, const qa_qc_definition *field)
{
    application_qc_combat *owner = engine ? engine->combat : NULL;
    if (!owner || field != owner->profile->health || owner->profile->program != engine->provider->state.qc.program ||
        engine->provider->state.qc.combat_profile != owner->profile) return false;
    combat_actor *actor = find(owner, id); uint32_t slot; int32_t reference; qa_error error = {0};
    return actor && actor->bound && !actor->retired && !actor->restoring &&
        qa_combat_primary_current(engine->services.combat, id, actor->serial, actor) &&
        qa_qc_actor_observation_slot(engine->provider->state.qc.instance, id, &slot, &error) && slot == actor->slot &&
        qa_qc_slot_reference(engine->provider->state.qc.instance, slot, &reference, &error) && reference == actor->reference;
}
static bool touches(const qa_qc_store_event *event, const qa_qc_definition *field, uint32_t width)
{ return event->word < field->offset + width && event->word + event->count > field->offset; }
static bool projectile_stored(application_qc_combat *, qa_qc_instance *, const qa_qc_store_event *, qa_error *);
bool application_qc_combat_source_stored(struct application_qc_state *engine, qa_qc_instance *vm,
    const qa_qc_store_event *event, qa_error *error)
{
    application_qc_combat *owner = engine->combat;
    if (owner && !projectile_stored(owner, vm, event, error)) return false;
    damage_frame *frame = owner ? owner->frame : NULL;
    if (!frame || frame->reaction_depth || event->kind != QA_QC_STORE_ENTITY) return true;
    const application_qc_combat_profile *profile = owner->profile;
    bool health = touches(event, profile->health, 1), velocity = touches(event, profile->velocity, 3);
    bool armor = touches(event, profile->armorvalue, 1) || touches(event, profile->armortype, 1) || touches(event, profile->items, 1);
    if (!health && !velocity && !armor) return true;
    if (vm != engine->provider->state.qc.instance || event->entity_reference != frame->actor->reference)
        return reject(error, "QC damage redirected a committed combat store outside its actual full target");
    if (frame->result->reaction != QA_REACTION_NONE)
        return reject(error, "QC damage committed a combat store after its consumed reaction");
    if (health) {
        uint32_t offset = profile->health->offset - event->word;
        qa_damage_mutation mutation = {.kind = QA_MUTATION_HEALTH,
            .value.health = {bits_float(event->before[offset]), bits_float(event->after[offset])}};
        if (!qa_damage_observe(frame->observer, &mutation, error)) return false;
        frame->health_written = true;
        frame->result->applied_damage += mutation.value.health.before - mutation.value.health.after;
    }
    uint64_t protection_serial;
    bool leased_regular = qa_combat_protection_owner(engine->services.combat, frame->actor->actor,
        QA_PROTECTION_REGULAR, NULL, &protection_serial) &&
        qa_combat_protection_bound(engine->services.combat,
            (qa_protection_lease){frame->actor->actor, protection_serial, QA_PROTECTION_REGULAR});
    if (armor && !leased_regular) {
        qa_damage_mutation mutation = {.kind = QA_MUTATION_ARMOR};
        if (!raw_armor(frame->actor, event, &mutation.value.armor.before, error) ||
            !raw_armor(frame->actor, NULL, &mutation.value.armor.after, error) ||
            !qa_damage_observe(frame->observer, &mutation, error)) return false;
    }
    if (velocity) {
        qa_damage_mutation mutation = {.kind = QA_MUTATION_SOURCE_VELOCITY,
            .value.velocity.movement = frame->request->attack.movement_provider};
        if (!qa_qc_actor_observation_vector(vm, frame->actor->slot, frame->actor->actor,
            profile->velocity->offset, &mutation.value.velocity.after, error)) return false;
        mutation.value.velocity.before = mutation.value.velocity.after;
        float before[3] = {mutation.value.velocity.before.x, mutation.value.velocity.before.y, mutation.value.velocity.before.z};
        for (uint32_t i = 0; i < event->count; ++i)
            if (event->word + i >= profile->velocity->offset && event->word + i - profile->velocity->offset < 3)
                before[event->word + i - profile->velocity->offset] = bits_float(event->before[i]);
        mutation.value.velocity.before = qa_v3(before[0], before[1], before[2]);
        if (!qa_damage_observe(frame->observer, &mutation, error)) return false;
    }
    return true;
}
static bool reference(combat_actor *actor, qa_actor_id id, int32_t *out, qa_error *error)
{
    *out = 0;
    if (!id.registry) return true;
    return qa_qc_actor_reference(actor->owner->engine->provider->state.qc.instance, id, true, out, error);
}
typedef struct entered_damage {
    application_qc_combat *owner;
    combat_actor *actor;
    qa_qc_call_next next;
    qa_damage_request original;
    float regular_scale;
} entered_damage;
static bool continued_damage(void *context, qa_combat *combat, const qa_damage_request *request,
    qa_damage_observer *observer, qa_damage_result *result, qa_error *error)
{
    entered_damage *entered = context;
    application_qc_combat *owner = entered->owner;
    const application_qc_combat_profile *profile = owner->profile;
    qa_qc_instance *vm = owner->engine->provider->state.qc.instance;
    if (combat != owner->engine->services.combat || !qa_actor_id_equal(request->target, entered->actor->actor))
        return reject(error, "QC Source damage transformation changed its physical target");
    if (request->knockback != entered->original.knockback || request->radius != entered->original.radius ||
        memcmp(&request->direction, &entered->original.direction, sizeof(qa_vec3)) ||
        memcmp(&request->point, &entered->original.point, sizeof(qa_vec3)) ||
        memcmp(&request->normal, &entered->original.normal, sizeof(qa_vec3)))
        return reject(error, "QC Source damage geometry changes require an actual replacement");
    qa_unified_frame_lease *storage = profile->location_count ?
        application_control_storage_acquire(owner->engine->provider->application, error) : NULL;
    uint32_t *saved = storage ? qa_unified_frame_lease_alloc(storage, profile->location_count,
        sizeof(*saved), _Alignof(uint32_t), error) : NULL;
    if (profile->location_count && !saved) {
        qa_unified_frame_lease_release(storage);
        return application_fail(error, QA_ERROR_MEMORY, "Staging transformed QC damage context");
    }
    size_t staged = 0; bool ok = current(entered->actor, error);
    for (size_t i = 0; ok && i < profile->location_count; ++i) {
        const damage_location *location = profile->locations + i; int32_t prior;
        ok = qa_qc_global_int(vm, location->word, &prior, error);
        if (!ok) break;
        saved[i] = (uint32_t)prior; ++staged;
        int32_t value = 0; float amount = (float)request->amount;
        if (location->role != QC_INPUT_AMOUNT) {
            qa_actor_id id = location->role == QC_INPUT_SELF ? request->target :
                location->role == QC_INPUT_ATTACKER ? request->attack.attacker : request->attack.inflictor;
            ok = reference(entered->actor, id, &value, error);
        }
        if (!ok) break;
        if (location->parameter) ok = location->role == QC_INPUT_AMOUNT ?
            qa_qc_call_set_arg_float(entered->next, location->argument, amount, error) :
            qa_qc_call_set_arg_int(entered->next, location->argument, value, error);
        else {
            uint32_t bits = location->role == QC_INPUT_AMOUNT ? float_bits(amount) : (uint32_t)value;
            ok = qa_qc_stage_globals(vm, location->word, &bits, 1, error);
        }
    }
    damage_frame frame = {.previous = owner->frame, .actor = entered->actor,
        .request = request, .observer = observer, .result = result, .next = entered->next,
        .regular_scale = entered->regular_scale};
    *result = (qa_damage_result){0};
    if (ok) {
        owner->frame = &frame; ++entered->actor->calls;
        ok = qa_qc_call_continue(entered->next, error);
        if (ok && frame.health_written && result->reaction == QA_REACTION_NONE && !entered->actor->retired) {
            float health;
            ok = raw_float(entered->actor, profile->health, NULL, &health, error);
            if (ok && health <= 0) {
                result->reaction = QA_REACTION_DEATH;
                ok = qa_damage_before_reaction(observer, result, error);
            }
        }
        owner->frame = frame.previous; --entered->actor->calls;
    }
    qa_error first = error ? *error : (qa_error){0};
    for (size_t i = 0; i < staged; ++i) {
        qa_error unwind = {0};
        if (!qa_qc_stage_globals(vm, profile->locations[i].word, saved + i, 1, &unwind)) {
            if (ok) first = unwind;
            ok = false;
        }
    }
    qa_unified_frame_lease_release(storage);
    actor_reclaim(entered->actor);
    if (!ok && error) *error = first;
    return ok;
}
static bool read_call(application_qc_combat *owner, qa_qc_instance *vm,
    int32_t *target, int32_t *attacker, int32_t *inflictor, float *amount, qa_error *error)
{
    uint64_t found = 0; *target = *attacker = *inflictor = 0; *amount = 0;
    for (size_t i = 0; i < owner->profile->location_count; ++i) {
        const damage_location *location = owner->profile->locations + i; int32_t word;
        if (!qa_qc_global_int(vm, location->word, &word, error)) return false;
        bool repeated = (found & (UINT64_C(1) << location->role)) != 0;
        if (location->role == QC_INPUT_AMOUNT) {
            float value; memcpy(&value, &word, 4);
            if (!isfinite(value) || (repeated && float_bits(value) != float_bits(*amount)))
                return reject(error, "QC damage amount aliases disagree or are nonfinite");
            *amount = value;
        } else {
            int32_t *value = location->role == QC_INPUT_SELF ? target : location->role == QC_INPUT_ATTACKER ? attacker : inflictor;
            if (repeated && *value != word) return reject(error, "QC damage actor aliases disagree at their actual Source boundary");
            *value = word;
        }
        found |= UINT64_C(1) << location->role;
    }
    return true;
}
static bool actual_actor(application_qc_combat *owner, int32_t reference, qa_actor_id *out,
    qa_qc_slot_binding *slot, qa_error *error)
{
    *out = (qa_actor_id){0}; *slot = (qa_qc_slot_binding){0};
    if (!reference) return true;
    qa_qc_instance *vm = owner->engine->provider->state.qc.instance; uint32_t physical;
    if (!qa_qc_reference_actor(vm, reference, out, error) ||
        !qa_qc_actor_observation_slot(vm, *out, &physical, error) || !qa_qc_slot(vm, physical, slot))
        return reject(error, "QC damage reference lost its actual full Source actor");
    return true;
}
static bool projectile_stored(application_qc_combat *owner, qa_qc_instance *vm,
    const qa_qc_store_event *event, qa_error *error)
{
    const synchronous_layout *layout = &owner->profile->synchronous;
    const synchronous_attack *active = owner->synchronous;
    if (!active || !active->weapon || !layout->owner || event->kind != QA_QC_STORE_ENTITY ||
        !event->entity_reference || !touches(event, layout->owner, 1)) return true;
    bool direct = active->function == layout->weapons[QA_Q1_ROCKET] ||
        active->function == layout->weapons[QA_Q1_GRENADE];
    bool spike = active->function == layout->weapons[QA_Q1_NAILGUN] ||
        active->function == layout->weapons[QA_Q1_SUPER_NAILGUN];
    if (!((direct && event->function == active->function) ||
        (spike && event->function == layout->launch_spike))) return true;
    const qa_qc_statement *statement = qa_qc_program_statement(owner->profile->program, event->statement);
    if (!statement || statement->opcode != QA_QC_STOREP_ENT ||
        event->after[layout->owner->offset - event->word] != (uint32_t)active->reference) return true;
    qa_actor_id projectile; qa_error ignored = {0};
    if (!qa_qc_reference_actor(vm, event->entity_reference, &projectile, &ignored) ||
        qa_actor_id_equal(projectile, active->actor)) return true;
    struct application_qc_state *engine = owner->engine;
    application_provider *inventory = application_provider_for(engine->provider->application,
        active->actor, QA_ROLE_INVENTORY, "");
    qa_attack origin = {.attacker = active->actor, .inflictor = projectile, .projectile = projectile,
        .weapon = active->weapon, .weapon_provider = engine->provider->owner,
        .inventory_provider = inventory ? inventory->owner : 0, .time_ns = active->time_ns,
        .cause = {.kind = QA_CAUSE_Q1, .source.q1.armor = QA_Q1_ARMOR_NORMAL}};
    if (!qa_attack_next(&owner->sequence, &origin, error)) return false;
    return qa_combat_attack_origin_store(engine->services.combat, projectile, &origin, error);
}
static bool synchronous_weapon(application_qc_combat *owner, synchronous_attack *attack,
    qa_error *error)
{
    struct application_qc_state *engine = owner->engine;
    const struct application_qc_profile *declared = engine->provider->state.qc.qualified;
    const qa_qc_definition *field = declared ? declared->weapon_field : engine->field_bindings->weapon;
    if (!field || field->type != QA_QC_FLOAT) return true;
    uint32_t slot; float value;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    if (!qa_qc_actor_observation_slot(vm, attack->actor, &slot, error) ||
        !qa_qc_actor_observation_float(vm, slot, attack->actor, field->offset, &value, error)) return false;
    if (declared) {
        for (size_t i = 0; i < declared->weapon_count; ++i)
            if (declared->weapon_values[i].value == value) {
                attack->weapon = declared->weapon_values[i].item; break;
            }
        return true;
    }
    const synchronous_layout *layout = &owner->profile->synchronous;
    qa_q1_weapon weapon = QA_Q1_WEAPON_COUNT;
    for (size_t i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
        if (layout->weapons[i] && layout->weapons[i] == attack->function) { weapon = (qa_q1_weapon)i; break; }
    if (weapon == QA_Q1_WEAPON_COUNT) return true;
    qa_item_id item = engine->provider->application->ui_names.q1_weapons[weapon];
    if (!item) return true;
    for (size_t i = 0; i < 8; ++i) {
        qa_application_qc_weapon_ui_binding binding; uint32_t via;
        if (!application_qc_weapon_binding_at(engine, i, &binding, &via, error)) return false;
        if (binding.item == item) {
            /* The original super shotgun calls W_FireShotgun with one shell. */
            if (value == (float)binding.bit || (attack->function == layout->weapons[QA_Q1_SHOTGUN] &&
                attack->previous && attack->previous->function == layout->weapons[QA_Q1_SUPER_SHOTGUN] &&
                qa_actor_id_equal(attack->previous->actor, attack->actor))) attack->weapon = item;
            break;
        }
    }
    return true;
}
static bool synchronous_trace_read(application_qc_combat *owner, int32_t target,
    qa_vec3 *point, qa_vec3 *normal, bool *present, qa_error *error)
{
    const synchronous_layout *layout = &owner->profile->synchronous;
    qa_qc_instance *vm = owner->engine->provider->state.qc.instance;
    int32_t traced;
    *present = false;
    if (!layout->trace_target || !layout->point || !layout->normal) return true;
    if (!qa_qc_global_int(vm, layout->trace_target->offset, &traced, error)) return false;
    if (traced != target) return true;
    if (!qa_qc_global_vector(vm, layout->point->offset, point, error) ||
        !qa_qc_global_vector(vm, layout->normal->offset, normal, error)) return false;
    *present = qa_vec_finite(*point) && qa_vec_finite(*normal);
    return true;
}
static bool synchronous_call(application_qc_combat *owner, qa_qc_instance *vm,
    const qa_qc_call_event *event, qa_qc_call_next next, bool *handled, qa_error *error)
{
    const synchronous_layout *layout = &owner->profile->synchronous;
    synchronous_attack *active = owner->synchronous;
    if (event->function && event->function == layout->radius) {
        int32_t inflictor, attacker;
        radius_attack radius = {.previous = owner->radius}; qa_qc_slot_binding slot;
        if (!qa_qc_arg_int(vm, 0, &inflictor, error) || !qa_qc_arg_int(vm, 1, &attacker, error) ||
            !actual_actor(owner, inflictor, &radius.inflictor, &slot, error) ||
            !actual_actor(owner, attacker, &radius.attacker, &slot, error)) return false;
        *handled = true; owner->radius = &radius; ++owner->calls;
        bool ok = qa_qc_call_continue(next, error);
        --owner->calls; owner->radius = radius.previous; return ok;
    }
    if (active && active->weapon && event->function == layout->add_multi &&
        event->caller == layout->trace_attack) {
        int32_t reference;
        if (!qa_qc_arg_int(vm, 0, &reference, error)) return false;
        if (!reference) return true;
        qa_vec3 point, normal; bool present;
        if (!synchronous_trace_read(owner, reference, &point, &normal, &present, error)) return false;
        if (!present) return true;
        qa_actor_id target; qa_qc_slot_binding slot;
        if (!actual_actor(owner, reference, &target, &slot, error)) return false;
        synchronous_trace *trace = active->traces;
        while (trace && !qa_actor_id_equal(trace->target, target)) trace = trace->next;
        if (!trace) {
            if (!active->storage) active->storage = application_control_storage_acquire(
                owner->engine->provider->application, error);
            trace = active->storage ? qa_unified_frame_lease_alloc(active->storage, 1,
                sizeof(*trace), _Alignof(synchronous_trace), error) : NULL;
            if (!trace) return false;
            *trace = (synchronous_trace){.next = active->traces, .target = target};
            active->traces = trace;
        }
        trace->point = point; trace->normal = normal;
        return true;
    }
    bool firing = false;
    for (size_t i = 0; event->function && i < QA_Q1_WEAPON_COUNT; ++i)
        firing = firing || layout->weapons[i] == event->function;
    if (!firing) return true;
    synchronous_attack attack = {.previous = active, .function = event->function,
        .time_ns = owner->engine->source_time_ns};
    qa_qc_slot_binding slot;
    if (!qa_qc_global_int(vm, owner->profile->self->offset, &attack.reference, error) ||
        !actual_actor(owner, attack.reference, &attack.actor, &slot, error)) return false;
    application_qc_client *client = application_qc_client_for_actor(owner->engine, attack.actor);
    if (!client || !client->connected || !client->spawned) return true;
    if (!synchronous_weapon(owner, &attack, error)) return false;
    if (!attack.weapon) return true;
    *handled = true;
    owner->synchronous = &attack; ++owner->calls;
    bool ok = qa_qc_call_continue(next, error);
    --owner->calls; owner->synchronous = attack.previous;
    qa_unified_frame_lease_release(attack.storage);
    return ok;
}
static bool source_geometry(application_qc_combat *owner, qa_actor_id target, qa_actor_id inflictor,
    qa_vec3 *point, qa_vec3 *direction, bool *present, qa_error *error)
{
    struct application_qc_state *engine = owner->engine;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    const qa_qc_game_fields *fields = engine->field_bindings;
    *present = false; *direction = qa_v3(0, 0, 0);
    if (!fields->origin || fields->origin->type != QA_QC_VECTOR) return true;
    uint32_t target_slot;
    if (!qa_qc_actor_observation_slot(vm, target, &target_slot, error) ||
        !qa_qc_actor_observation_vector(vm, target_slot, target, fields->origin->offset, point, error)) return false;
    if (!qa_vec_finite(*point)) return true;
    if (inflictor.registry) {
        uint32_t source_slot; qa_vec3 minimum, maximum;
        if (!fields->absmin || fields->absmin->type != QA_QC_VECTOR ||
            !fields->absmax || fields->absmax->type != QA_QC_VECTOR) return true;
        if (!qa_qc_actor_observation_slot(vm, inflictor, &source_slot, error) ||
            !qa_qc_actor_observation_vector(vm, source_slot, inflictor, fields->absmin->offset, &minimum, error) ||
            !qa_qc_actor_observation_vector(vm, source_slot, inflictor, fields->absmax->offset, &maximum, error)) return false;
        if (!qa_vec_finite(minimum) || !qa_vec_finite(maximum)) return true;
        *direction = qa_vec_normalize(qa_vec_sub(*point, qa_vec_scale(qa_vec_add(minimum, maximum), .5f)));
    }
    *present = true; return true;
}
static bool synchronous_request(application_qc_combat *owner, const qa_qc_call_event *event,
    int32_t target, qa_damage_request *request, qa_error *error)
{
    synchronous_attack *attack = owner->synchronous;
    const synchronous_layout *layout = &owner->profile->synchronous;
    if (!attack || !attack->weapon || !qa_actor_id_equal(request->attack.attacker, attack->actor) ||
        !qa_actor_id_equal(request->attack.inflictor, attack->actor)) return true;
    qa_vec3 point, normal; bool present = false;
    if (event->caller == layout->weapons[QA_Q1_AXE] && attack->function == layout->weapons[QA_Q1_AXE]) {
        if (!synchronous_trace_read(owner, target, &point, &normal, &present, error)) return false;
    } else if (event->caller == layout->apply_multi &&
        (attack->function == layout->weapons[QA_Q1_SHOTGUN] || attack->function == layout->weapons[QA_Q1_SUPER_SHOTGUN])) {
        for (const synchronous_trace *trace = attack->traces; trace; trace = trace->next)
            if (qa_actor_id_equal(trace->target, request->target)) {
                point = trace->point; normal = trace->normal; present = true; break;
            }
    }
    if (!present) return true;
    qa_vec3 origin, direction;
    if (!source_geometry(owner, request->target, attack->actor, &origin, &direction, &present, error)) return false;
    if (!present) return true;
    request->attack.weapon = attack->weapon;
    request->point = point; request->normal = normal;
    request->direction = direction;
    request->knockback = request->amount;
    return true;
}
static bool projectile_request(application_qc_combat *owner, const qa_qc_call_event *event,
    int32_t target, qa_damage_request *request, qa_error *error)
{
    if (request->attack.weapon) return true;
    struct application_qc_state *engine = owner->engine;
    const synchronous_layout *layout = &owner->profile->synchronous;
    qa_attack origin; bool emitted = false;
    if (!qa_combat_attack_origin_read(engine->services.combat, request->attack.inflictor,
        &origin, &emitted, error)) return false;
    emitted = emitted && origin.weapon && origin.weapon_provider == engine->provider->owner;
    const synchronous_attack *active = owner->synchronous;
    bool firing = active && active->weapon &&
        qa_actor_id_equal(request->attack.attacker, active->actor) &&
        qa_actor_id_equal(request->attack.inflictor, active->actor) &&
        (event->caller == active->function || (layout->radius && event->caller == layout->radius) ||
         (active->function == layout->weapons[QA_Q1_LIGHTNING] && layout->lightning_damage &&
          event->caller == layout->lightning_damage));
    if (!emitted && !firing) return true;
    request->attack.weapon = emitted ? origin.weapon : active->weapon;
    if (emitted) {
        request->attack.weapon_provider = origin.weapon_provider;
        request->attack.inventory_provider = origin.inventory_provider;
        request->attack.projectile = request->attack.inflictor;
    }
    const radius_attack *radius = owner->radius;
    request->radius = radius && qa_actor_id_equal(radius->inflictor, request->attack.inflictor) &&
        qa_actor_id_equal(radius->attacker, request->attack.attacker);
    qa_vec3 point, direction; bool present;
    if (!source_geometry(owner, request->target, request->attack.inflictor, &point, &direction, &present, error)) return false;
    if (present) request->direction = direction;
    const qa_qc_definition *source_origin = engine->field_bindings->origin;
    if ((emitted || request->radius) && source_origin && source_origin->type == QA_QC_VECTOR) {
        uint32_t slot;
        if (!qa_qc_actor_observation_slot(engine->provider->state.qc.instance, request->attack.inflictor, &slot, error) ||
            !qa_qc_actor_observation_vector(engine->provider->state.qc.instance, slot, request->attack.inflictor,
                source_origin->offset, &request->point, error)) return false;
    }
    const qa_touch_contact *contact = engine->touch_contact;
    if (!request->radius && contact && qa_actor_id_equal(contact->self, request->attack.inflictor) &&
        qa_actor_id_equal(contact->other, request->target)) {
        if (contact->has_plane) request->normal = contact->plane.normal;
        if (contact->has_source_trace) request->point = contact->source_trace.end;
    } else if (!request->radius && firing && active->function == layout->weapons[QA_Q1_LIGHTNING] &&
        event->caller == layout->lightning_damage) {
        qa_vec3 normal;
        if (!synchronous_trace_read(owner, target, &point, &normal, &present, error)) return false;
        if (present) { request->point = point; request->normal = normal; }
    }
    request->knockback = request->amount;
    return true;
}
static bool environment_request(application_qc_combat *owner, const qa_qc_call_event *event,
    int32_t target, int32_t inflictor, int32_t attacker, qa_damage_request *request, qa_error *error)
{
    const environment_layout *layout = &owner->profile->environment;
    if (event->statement >= layout->statement_count || !layout->hazards[event->statement]) return true;
    qa_hazard hazard = (qa_hazard)(layout->hazards[event->statement] - 1);
    struct application_qc_state *engine = owner->engine;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    int32_t self;
    if (!qa_qc_global_int(vm, owner->profile->self->offset, &self, error)) return false;
    if (hazard != QA_HAZARD_TRIGGER && hazard != QA_HAZARD_CRUSH) {
        int32_t world;
        if (!layout->world || layout->world->type != QA_QC_ENTITY || target != self || inflictor || attacker) return true;
        if (!qa_qc_global_int(vm, layout->world->offset, &world, error)) return false;
        if (world) return true;
    } else {
        const qa_qc_game_fields *fields = engine->field_bindings;
        const qa_qc_definition *callback = hazard == QA_HAZARD_TRIGGER ? fields->touch : fields->blocked;
        if (!layout->other || layout->other->type != QA_QC_ENTITY || !callback ||
            callback->type != QA_QC_FUNCTION || !inflictor || inflictor != self) return true;
        int32_t other, function; uint32_t slot;
        if (!qa_qc_global_int(vm, layout->other->offset, &other, error)) return false;
        if (other != target) return true;
        if (!qa_qc_actor_observation_slot(vm, request->attack.inflictor, &slot, error) ||
            !qa_qc_actor_observation_int(vm, slot, request->attack.inflictor, callback->offset, &function, error)) return false;
        if (function != (int32_t)event->caller) return true;
        if (attacker != self) {
            if (event->caller != layout->door || !fields->goalentity || fields->goalentity->type != QA_QC_ENTITY) return true;
            int32_t goal;
            if (!qa_qc_actor_observation_int(vm, slot, request->attack.inflictor, fields->goalentity->offset, &goal, error)) return false;
            if (attacker != goal) return true;
        }
    }
    request->attack.cause = (qa_damage_cause){.kind = QA_CAUSE_ENVIRONMENT, .source.hazard = hazard};
    qa_vec3 point, direction; bool present;
    if (!source_geometry(owner, request->target, request->attack.inflictor, &point, &direction, &present, error)) return false;
    if (present) request->point = point;
    if (present && inflictor) {
        const qa_qc_definition *movetype = engine->field_bindings->movetype;
        if (movetype && movetype->type == QA_QC_FLOAT) {
            uint32_t slot; float movement;
            if (!qa_qc_actor_observation_slot(vm, request->target, &slot, error) ||
                !qa_qc_actor_observation_float(vm, slot, request->target, movetype->offset, &movement, error)) return false;
            if (movement == 3) { request->direction = direction; request->knockback = request->amount; }
        }
    }
    return true;
}
static bool authored_request(application_qc_combat *owner, qa_actor_id target, qa_actor_id attacker,
    qa_actor_id inflictor, float amount, qa_damage_request *out, qa_error *error)
{
    struct application_qc_state *engine = owner->engine;
    qa_application *app = engine->provider->application;
    qa_clock_state clock;
    if (!qa_session_clock(app->session, engine->provider->owner, &clock) || clock.frame.provider != engine->provider->owner)
        return reject(error, "Authored QC damage has no actual admitted Source clock");
    *out = (qa_damage_request){.target = target, .amount = amount};
    out->attack = (qa_attack){.attacker = attacker, .inflictor = inflictor,
        .weapon_provider = engine->provider->owner, .time_ns = clock.frame.time_ns,
        .cause = {.kind = QA_CAUSE_Q1, .source.q1.armor = QA_Q1_ARMOR_NORMAL}};
    application_provider *combat = application_provider_for(app, target, QA_ROLE_COMBAT, "");
    application_provider *movement = application_provider_for(app, target, QA_ROLE_MOVEMENT, "");
    application_provider *inventory = application_provider_for(app, attacker, QA_ROLE_INVENTORY, "");
    out->attack.combat_provider = combat ? combat->owner : 0;
    out->attack.movement_provider = movement ? movement->owner : 0;
    out->attack.inventory_provider = inventory ? inventory->owner : 0;
    if (qa_world_body_storage_serial(engine->world, target)) {
        qa_body_state body;
        if (!qa_world_body_read(engine->world, target, &body, error)) return false;
        out->point = body.origin;
    }
    const qa_qc_definition *death = engine->field_bindings->deathtype;
    if (death && death->type == QA_QC_STRING) {
        uint32_t slot; int32_t string; const char *text;
        if (!qa_qc_actor_observation_slot(engine->provider->state.qc.instance, target, &slot, error) ||
            !qa_qc_actor_observation_int(engine->provider->state.qc.instance, slot, target, death->offset, &string, error) ||
            !qa_qc_string(engine->provider->state.qc.instance, string, &text, error) ||
            !qa_strings_intern(qa_session_strings(app->session), (qa_bytes){(const uint8_t *)text, strlen(text)},
                &out->attack.cause.source.q1.death_type, error)) return false;
    }
    return qa_attack_next(&owner->sequence, &out->attack, error);
}
typedef struct reaction_call {
    damage_frame *frame;
    qa_qc_call_next next;
    bool entered;
} reaction_call;
static bool reaction_body(void *context, const application_q3_mod_actor_request *effective,
    bool *result, qa_error *error)
{
    reaction_call *call = context;
    damage_frame *frame = call->frame;
    if (!qa_actor_id_equal(effective->self, frame->actor->actor))
        return reject(error, "QC Source reaction target changes require a callback replacement");
    if (frame->result->reaction == QA_REACTION_PAIN) {
        int32_t attacker;
        if (!reference(frame->actor, effective->source.pain.attacker, &attacker, error) ||
            !qa_qc_call_set_arg_int(call->next, 0, attacker, error) ||
            !qa_qc_call_set_arg_float(call->next, 1, (float)effective->source.pain.damage, error)) return false;
    } else if (!qa_actor_id_equal(effective->source.die.attacker, frame->request->attack.attacker) ||
        !qa_actor_id_equal(effective->source.die.inflictor, frame->request->attack.inflictor) ||
        effective->source.die.damage != frame->result->applied_damage ||
        effective->source.die.kick != frame->request->knockback ||
        memcmp(&effective->source.die.point, &frame->request->point, sizeof(qa_vec3)))
        return reject(error, "QC Source death arguments require an actual callback replacement");
    call->entered = true;
    bool ok = qa_qc_call_continue(call->next, error);
    if (ok) *result = true;
    return ok;
}
static bool reaction_original(void *context, qa_error *error)
{
    reaction_call *call = context; damage_frame *frame = call->frame;
    application_q3_mod_actor_request request = {.self = frame->actor->actor,
        .has_attack = true, .attack = frame->request->attack};
    bool death = frame->result->reaction == QA_REACTION_DEATH;
    if (death) {
        request.source.die.attacker = frame->request->attack.attacker;
        request.source.die.inflictor = frame->request->attack.inflictor;
        request.source.die.damage = frame->result->applied_damage;
        request.source.die.kick = frame->request->knockback; request.source.die.point = frame->request->point;
    } else {
        request.source.pain.attacker = frame->request->attack.attacker;
        request.source.pain.damage = frame->result->applied_damage;
        request.source.pain.kick = frame->request->knockback;
    }
    bool result;
    return application_q3_mod_actor_dispatch(frame->actor->owner->engine->provider->application->mod_operations,
        death ? Q3_MOD_DIE : Q3_MOD_PAIN, &request, reaction_body, call, &result, error);
}
static bool reaction_replace(application_qc_combat *owner, qa_qc_instance *vm,
    const qa_qc_call_event *event, qa_qc_call_next next, bool *handled, qa_error *error)
{
    damage_frame *frame = owner->frame;
    if (!frame || frame->reaction_depth || frame->result->reaction != QA_REACTION_NONE) return true;
    qa_qc_program_info info = qa_qc_program_describe(owner->profile->program);
    qa_reaction reaction = event->statement < info.statement_count ? owner->profile->reactions[event->statement] : QA_REACTION_NONE;
    if (reaction == QA_REACTION_NONE) return true;
    int32_t self, function;
    const qa_qc_definition *field = reaction == QA_REACTION_DEATH ? owner->profile->die : owner->profile->pain;
    if (!qa_qc_global_int(vm, owner->profile->self->offset, &self, error) ||
        !qa_qc_actor_observation_int(vm, frame->actor->slot, frame->actor->actor, field->offset, &function, error)) return false;
    if (self != frame->actor->reference || function != (int32_t)event->function) return true;
    float health;
    if (!frame->health_written || !raw_float(frame->actor, owner->profile->health, NULL, &health, error) ||
        ((health <= 0) != (reaction == QA_REACTION_DEATH)))
        return reject(error, "QC Source reaction differs from its committed target health");
    const qa_qc_function *callee = qa_qc_program_function(owner->profile->program, event->function);
    if (!callee || (reaction == QA_REACTION_PAIN && (event->argument_count != 2 || callee->parameter_count > 2 ||
        (callee->parameter_count > 0 && callee->parameter_sizes[0] != 1) ||
        (callee->parameter_count > 1 && callee->parameter_sizes[1] != 1))))
        return reject(error, "QC Source pain callback does not retain its actual actor/amount signature");
    *handled = true; frame->result->reaction = reaction;
    reaction_call call = {.frame = frame, .next = next}; ++frame->reaction_depth;
    bool ok = qa_damage_dispatch_source_reaction(frame->observer, frame->result,
        frame->request->knockback, frame->request->point, owner->engine->provider->owner,
        reaction_original, &call, error);
    --frame->reaction_depth;
    if (ok && !call.entered) ok = qa_qc_call_skip(next, (uint32_t[3]){0}, error);
    return ok;
}
bool application_qc_combat_replace(struct application_qc_state *engine, qa_qc_instance *vm,
    const qa_qc_call_event *event, qa_qc_call_next next, bool *handled, qa_error *error)
{
    *handled = false; application_qc_combat *owner = engine->combat;
    if (!owner) return true;
    if (vm != engine->provider->state.qc.instance || !held(owner, error)) return false;
    if (event->function != owner->profile->damage.function) {
        if (!reaction_replace(owner, vm, event, next, handled, error)) return false;
        if (*handled) return true;
        return synchronous_call(owner, vm, event, next, handled, error);
    }
    if (event->argument_count != owner->profile->damage.argument_count)
        return reject(error, "QC physical damage call differs from its declared actual arguments");
    *handled = true;
    incoming_damage *incoming = owner->incoming;
    if (incoming && !incoming->entered) {
        incoming->entered = true;
        entered_damage entered = {.owner = owner, .actor = incoming->actor, .next = next,
            .original = *incoming->request, .regular_scale = 1};
        return continued_damage(&entered, engine->services.combat, incoming->request,
            incoming->observer, incoming->result, error);
    }
    int32_t target_reference, attacker_reference, inflictor_reference; float amount;
    if (!read_call(owner, vm, &target_reference, &attacker_reference, &inflictor_reference, &amount, error)) return false;
    qa_actor_id target, attacker, inflictor; qa_qc_slot_binding target_slot, attacker_slot, inflictor_slot;
    if (!actual_actor(owner, target_reference, &target, &target_slot, error) ||
        !actual_actor(owner, attacker_reference, &attacker, &attacker_slot, error) ||
        !actual_actor(owner, inflictor_reference, &inflictor, &inflictor_slot, error)) return false;
    if (!target.registry) return qa_qc_call_continue(next, error);
    qa_damage_request request;
    if (!authored_request(owner, target, attacker, inflictor, amount, &request, error) ||
        !synchronous_request(owner, event, target_reference, &request, error) ||
        !projectile_request(owner, event, target_reference, &request, error) ||
        !environment_request(owner, event, target_reference, inflictor_reference, attacker_reference, &request, error)) return false;
    bool primary = eligible(owner, target_slot);
    if (!primary || request.attack.weapon) {
        qa_damage_request prepared = request;
        if (!application_qc_combat_prepare_damage(engine, &prepared, error)) return false;
        if (request.attack.weapon) request.knockback = prepared.amount;
        if (!primary) { prepared.knockback = request.knockback; request = prepared; }
    }
    qa_damage_outcome outcome = {0}; ++owner->calls;
    bool ok;
    if (!primary) {
        ok = qa_combat_apply(engine->services.combat, &request, &outcome, error);
        if (ok) ok = qa_qc_call_skip(next, (uint32_t[3]){0}, error);
    } else {
        ok = admit(owner, target_slot, target_reference, error);
        combat_actor *actor = find(owner, target);
        if (ok && (!actor || !actor->bound)) ok = reject(error, "QC physical damage target has no actual canonical primary");
        entered_damage entered = {.owner = owner, .actor = actor, .next = next,
            .original = request, .regular_scale = 1};
        for (size_t i = 0; i < owner->profile->armor.scale_count; ++i) {
            const application_qc_armor_scale *scale = owner->profile->armor.scales + i;
            if (scale->caller == event->caller && scale->statement == event->statement) entered.regular_scale = scale->scale;
        }
        if (ok) {
            ++actor->calls;
            ok = qa_combat_run_source(engine->services.combat, &request, continued_damage, &entered, &outcome, error);
            --actor->calls;actor_reclaim(actor);
        }
        if (ok && !qa_qc_call_completed(next)) ok = qa_qc_call_skip(next, (uint32_t[3]){0}, error);
    }
    --owner->calls; qa_damage_outcome_release(&outcome); return ok;
}
bool application_qc_combat_inline(struct application_qc_state *engine, qa_qc_instance *vm,
    const qa_qc_inline_event *event, qa_qc_inline_next next, bool *handled, qa_error *error)
{
    *handled = false; application_qc_combat *owner = engine->combat;
    if (!owner) return true;
    if (vm != engine->provider->state.qc.instance || !held(owner, error)) return false;
    const application_qc_combat_profile *profile = owner->profile;
    damage_frame *frame = owner->frame;
    if (!frame || frame->reaction_depth) return true;
    if (profile->scale.qualified && event->region.function == profile->scale.region.function &&
        event->region.entry == profile->scale.region.entry) {
        *handled = true;
        return frame->request->attack.powerup_applied && frame->request->attack.powerup_owner == engine->provider->owner ?
            qa_qc_inline_skip_to_join(next, error) : qa_qc_inline_continue(next, error);
    }
    if (!profile->armor_declared || event->region.function != profile->armor.region.function ||
        event->region.entry != profile->armor.region.entry) return true;
    application_qc_armor_stage stage = profile->armor;
    int32_t target;
    if (!qa_qc_global_int(vm, stage.target, &target, error) || target != frame->actor->reference)
        return reject(error, "QC armor stage changed its actual Source target");
    bool power = qa_combat_protection_owner(engine->services.combat, frame->actor->actor, QA_PROTECTION_POWERED, NULL, NULL);
    bool regular = qa_combat_protection_owner(engine->services.combat, frame->actor->actor, QA_PROTECTION_REGULAR, NULL, NULL);
    if (!power && !regular) return true;
    *handled = true;
    if (regular && !stage.region.replaceable) return reject(error, "QC regular armor continuation is not safely replaceable");
    qa_damage_flags flags = qa_attack_flags(&frame->request->attack);
    flags.regular_scale *= frame->regular_scale;
    if (stage.flag_bits) {
        float value;
        if (!qa_qc_global_float(vm, stage.flags, &value, error) || !isfinite(value)) return false;
        uint32_t bits = (uint32_t)qa_source_to_i32(value);
        if (stage.no_armor) flags.no_armor = (bits & stage.no_armor) != 0;
        if (stage.no_power) flags.no_power_armor = (bits & stage.no_power) != 0;
        if (stage.no_regular) flags.no_regular_armor = (bits & stage.no_regular) != 0;
        if (stage.energy) flags.energy = (bits & stage.energy) != 0;
    }
    float amount; double power_saved = 0, regular_saved = 0; int32_t original;
    if (!qa_qc_global_int(vm, stage.damage, &original, error) ||
        !qa_qc_global_float(vm, stage.damage, &amount, error)) return false;
    bool ok = !power || qa_combat_absorb(engine->services.combat, frame->request, QA_PROTECTION_POWERED,
        NULL, amount, flags, NULL, &power_saved, NULL, error);
    float remaining = (float)((double)amount - power_saved);
    uint32_t bits = float_bits(remaining);
    if (ok) ok = isfinite(remaining) && qa_qc_stage_globals(vm, stage.damage, &bits, 1, error);
    regular = qa_combat_protection_owner(engine->services.combat, frame->actor->actor, QA_PROTECTION_REGULAR, NULL, NULL);
    if (ok && regular) {
        ok = qa_combat_absorb(engine->services.combat, frame->request, QA_PROTECTION_REGULAR,
            NULL, remaining, flags, NULL, &regular_saved, NULL, error) && qa_qc_inline_skip_to_join(next, error);
    } else if (ok) {
        float source_saved;
        ok = qa_qc_inline_continue(next, error) &&
            qa_qc_global_float(vm, stage.region.saved_word, &source_saved, error);
        if (ok) regular_saved = source_saved;
    }
    float total = (float)(regular_saved + power_saved);
    if (ok && !isfinite(total)) ok = reject(error, "QC composed armor savings exceed actual Source precision");
    bits = float_bits(total);
    if (ok) ok = qa_qc_stage_globals(vm, stage.region.saved_word, &bits, 1, error);
    qa_error first = error ? *error : (qa_error){0}, unwind = {0}; bits = (uint32_t)original;
    if (!qa_qc_stage_globals(vm, stage.damage, &bits, 1, &unwind)) {
        if (ok) first = unwind;
        ok = false;
    }
    if (!ok && error) *error = first;
    return ok;
}
bool application_qc_combat_release(struct application_qc_state *engine, qa_actor_id id, qa_error *error)
{
    application_qc_combat *owner = engine->combat;
    if (!owner) return true;
    combat_actor *actor=find(owner,id);
    if(actor){
        ++actor->calls;
        bool ok=!actor->bound||!qa_combat_primary_current(engine->services.combat,id,actor->serial,actor)||
            qa_combat_detach_primary(engine->services.combat,id,actor->serial,actor,error);
        if(ok&&!actor->retired)actor_retire(actor);
        --actor->calls;actor_reclaim(actor);
        return ok;
    }
    return true;
}
bool application_qc_combat_idle(const struct application_qc_state *engine)
{ return !engine || !engine->combat || (!engine->combat->calls && !engine->combat->frame && !engine->combat->incoming && !engine->combat->synchronous && !engine->combat->radius); }
bool application_qc_combat_suspend(struct application_qc_state *engine, qa_error *error)
{
    application_qc_combat *owner = engine->combat;
    if (!owner) return true;
    if (!application_qc_combat_idle(engine)) return reject(error, "QC combat suspension retains an active Source call");
    while (owner->actors) if (!application_qc_combat_release(engine, owner->actors->actor, error)) return false;
    return true;
}
bool application_qc_combat_destroy(struct application_qc_state *engine, qa_error *error)
{
    if (!application_qc_combat_suspend(engine, error)) return false;
    if (engine->combat) {
        qa_actor_index_destroy(&engine->combat->index);
        qa_unified_frame_lease_release(engine->combat->actor_storage);
    }
    free(engine->combat); engine->combat = NULL; return true;
}
bool application_qc_combat_ready(const struct application_qc_state *engine, qa_error *error)
{
    application_qc_combat *owner = engine ? engine->combat : NULL;
    if (!owner) return true;
    if (!held(owner, error) || !application_qc_combat_idle(engine)) return false;
    if (engine->provider->application->operation == APPLICATION_PERSISTING) return true;
    for (combat_actor *actor = owner->actors; actor; actor = actor->next) {
        if (actor->restoring || !current(actor, error) || !actor->bound ||
            !qa_combat_primary_current(engine->services.combat, actor->actor, actor->serial, actor))
            return reject(error, "QC combat differs from its actual canonical primary");
        qa_combat_state state;
        if (!read(actor, &state, error)) return false;
    }
    return true;
}
bool application_qc_combat_saved_binding(struct application_qc_state *engine, qa_actor_id id,
    uint64_t serial, qa_combat_binding *out, qa_error *error)
{
    if (!serial || !out || engine->provider->application->operation != APPLICATION_PERSISTING ||
        !application_qc_combat_create(engine, error) || !engine->combat)
        return reject(error, "Saved QC combat has no actual declared primary");
    application_qc_combat *owner = engine->combat;
    if (find(owner,id)) return reject(error, "Saved QC combat duplicates its actual callback context");
    uint32_t physical; int32_t reference; qa_qc_slot_binding slot;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    if (!held(owner, error) || !qa_qc_actor_observation_slot(vm, id, &physical, error) ||
        !qa_qc_slot(vm, physical, &slot) || !eligible(owner, slot) ||
        !qa_qc_slot_reference(vm, physical, &reference, error))
        return reject(error, "Saved QC combat lost its actual restored full-actor Source row");
    combat_actor *actor = actor_create(owner,id,physical,reference,error);
    if (!actor) return false;
    actor->serial=serial;actor->bound=true;actor->restoring=true;
    *out = binding(actor); return true;
}
bool application_qc_combat_restore_attach(struct application_qc_state *engine, qa_error *error)
{
    application_qc_combat *owner = engine->combat;
    if (!owner) return true;
    if (!held(owner, error) || !application_qc_combat_idle(engine)) return false;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    for (combat_actor *actor = owner->actors; actor; actor = actor->next) {
        qa_qc_slot_binding slot;
        if (!qa_qc_actor_observation_slot(vm, actor->actor, &actor->slot, error) || !qa_qc_slot(vm, actor->slot, &slot) ||
            !eligible(owner, slot) || !qa_qc_slot_reference(vm, actor->slot, &actor->reference, error) ||
            !qa_combat_primary_current(engine->services.combat, actor->actor, actor->serial, actor))
            return reject(error, "Restored QC combat lost its actual full-actor primary custody");
        actor->restoring = false;
        qa_attack attack; bool present;
        if (!qa_combat_last_attack_read(engine->services.combat, actor->actor, &attack, &present, error)) return false;
        if (present && attack.sequence > owner->sequence) owner->sequence = attack.sequence;
    }
    uint32_t cursor = 0; const qa_actor_record *record;
    while (qa_actors_next(qa_session_actors(engine->services.session), &cursor, &record)) {
        qa_attack attack; bool present;
        if (!qa_combat_attack_origin_read(engine->services.combat, record->id, &attack, &present, error)) return false;
        if (present && attack.weapon_provider == engine->provider->owner && attack.sequence > owner->sequence)
            owner->sequence = attack.sequence;
    }
    return application_qc_combat_ready(engine, error);
}
typedef enum scale_value_kind { SCALE_UNSET, SCALE_SCALAR, SCALE_ACTOR, SCALE_AMOUNT } scale_value_kind;
typedef struct scale_value {
    scale_value_kind kind;
    float constant;
    uint32_t operations;
    bool known, identity, powers;
} scale_value;
typedef struct scale_analysis {
    application_qc_combat_profile *profile;
    const qa_qc_function *function;
    qa_qc_program_info info;
    uint8_t *written, *named, *prefix, *scratch, *constants;
    scale_value *context;
    uint32_t parameter_end, damage;
    bool transform, identity, multiplied, inflictor;
} scale_analysis;
static bool scale_number(const qa_json_document *doc, qa_json_id node, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, node, &value, error) || value > UINT32_MAX)
        return reject(error, "QC damage scale exceeds its actual compiled word extent");
    *out = (uint32_t)value; return true;
}
static bool scale_read(scale_analysis *analysis, scale_value *values, uint32_t word,
    scale_value *out, qa_error *error)
{
    if (word >= analysis->info.global_words || word < 28)
        return reject(error, "QC damage scale reads unavailable Source call staging");
    if (values[word].kind != SCALE_UNSET) { *out = values[word]; return true; }
    if ((word >= analysis->function->parameter_start &&
        word - analysis->function->parameter_start < analysis->function->local_words) ||
        (!analysis->named[word] && analysis->written[word]))
        return reject(error, "QC damage scale reads an unstaged private Source word");
    if (analysis->context[word].kind != SCALE_UNSET) {
        *out = analysis->context[word];
        return out->kind != SCALE_ACTOR || out->identity || analysis->inflictor ||
            reject(error, "QC damage scale depends on a target or inflictor entity");
    }
    const qa_qc_definition *time = qa_qc_program_find_global(analysis->profile->program, "time");
    if (analysis->prefix[word] || (time && time->offset == word) ||
        definition_at(analysis->profile->program, word, QA_QC_ENTITY))
        return reject(error, "QC damage scale reads unexecuted or undeclared entity/time context");
    *out = (scale_value){.kind = SCALE_SCALAR};
    if (!analysis->written[word]) {
        int32_t initial;
        if (!qa_qc_program_initial_int(analysis->profile->program, word, &initial, error)) return false;
        memcpy(&out->constant, &initial, 4); out->known = true; analysis->constants[word] = 1;
    }
    return true;
}
static bool scale_write(scale_analysis *analysis, scale_value *values, uint32_t word,
    scale_value value, qa_error *error)
{
    bool local = word >= analysis->function->parameter_start &&
        word - analysis->function->parameter_start < analysis->function->local_words;
    if (word < 28 || word >= analysis->info.global_words || (word != analysis->damage &&
        ((local && word < analysis->parameter_end) || (analysis->named[word] && !local))))
        return reject(error, "QC damage scale writes outside its actual private frame");
    if (word == analysis->damage && (value.kind == SCALE_ACTOR ||
        (!analysis->transform && value.kind != SCALE_AMOUNT)))
        return reject(error, "QC damage scale loses the actual incoming amount");
    analysis->scratch[word] = 1; values[word] = value; return true;
}
static bool scale_merge(scale_analysis *analysis, scale_value *left, scale_value right, qa_error *error)
{
    if (left->kind == SCALE_UNSET || right.kind == SCALE_UNSET) { *left = (scale_value){0}; return true; }
    if (left->kind != right.kind) {
        if (analysis->transform && left->kind != SCALE_ACTOR && right.kind != SCALE_ACTOR) {
            *left = (scale_value){.kind = SCALE_AMOUNT}; return true;
        }
        return reject(error, "QC damage scale joins incompatible original values");
    }
    if (left->kind == SCALE_ACTOR) return left->identity == right.identity ||
        reject(error, "QC damage scale joins different Source entity roles");
    left->known = left->known && right.known && float_bits(left->constant) == float_bits(right.constant);
    left->identity = left->identity && right.identity; left->powers = left->powers && right.powers;
    if (right.operations > left->operations) left->operations = right.operations;
    return true;
}
static bool scale_edge(scale_analysis *analysis, scale_value **pending, uint32_t pc,
    int64_t destination, const scale_value *values, qa_error *error)
{
    damage_scale *scale = &analysis->profile->scale;
    if (destination <= pc || destination < scale->region.entry || destination > scale->region.exit)
        return reject(error, "QC damage scale has an escaping or repeated Source branch");
    size_t key = (size_t)destination - scale->region.entry;
    if (!pending[key]) {
        pending[key] = malloc((size_t)analysis->info.global_words * sizeof(**pending));
        if (!pending[key]) return application_fail(error, QA_ERROR_MEMORY, "Owning QC damage scale path");
        memcpy(pending[key], values, (size_t)analysis->info.global_words * sizeof(**pending)); return true;
    }
    for (uint32_t i = 0; i < analysis->info.global_words; ++i)
        if (!scale_merge(analysis, pending[key] + i, values[i], error)) return false;
    return true;
}
static bool scale_operations(scale_analysis *analysis, scale_value **pending, uint32_t pc,
    scale_value *values, qa_error *error)
{
    const qa_qc_statement *statement = qa_qc_program_statement(analysis->profile->program, pc);
    qa_qc_opcode opcode = statement->opcode;
    scale_value left = {0}, right = {0}, result = {0};
    if (opcode == QA_QC_IF || opcode == QA_QC_IFNOT) {
        if (!scale_read(analysis, values, statement->a, &left, error) ||
            (!analysis->transform && left.kind == SCALE_AMOUNT))
            return reject(error, "QC multiplier predicate depends on its incoming amount");
        if (!scale_edge(analysis, pending, pc, (int64_t)pc + (int16_t)statement->b, values, error)) return false;
    } else if (opcode == QA_QC_GOTO)
        return scale_edge(analysis, pending, pc, (int64_t)pc + (int16_t)statement->a, values, error);
    else if (opcode == QA_QC_LOAD_F || opcode == QA_QC_LOAD_S) {
        if (!scale_read(analysis, values, statement->a, &left, error) || left.kind != SCALE_ACTOR ||
            (!left.identity && !analysis->inflictor) ||
            !scale_read(analysis, values, statement->b, &right, error) || right.kind == SCALE_AMOUNT)
            return reject(error, "QC damage scale reads an undeclared Source entity");
        if (!scale_write(analysis, values, statement->c, (scale_value){.kind = SCALE_SCALAR}, error)) return false;
    } else if (opcode == QA_QC_STORE_F || opcode == QA_QC_STORE_ENT || opcode == QA_QC_STORE_S) {
        if (!scale_read(analysis, values, statement->a, &left, error) ||
            !scale_write(analysis, values, statement->b, left, error)) return false;
    } else if (opcode == QA_QC_NOT_F || opcode == QA_QC_NOT_S || opcode == QA_QC_NOT_ENT) {
        if (!scale_read(analysis, values, statement->a, &left, error) ||
            (!analysis->transform && left.kind == SCALE_AMOUNT))
            return reject(error, "QC multiplier predicate depends on its incoming amount");
        result = left.kind == SCALE_AMOUNT ? (scale_value){.kind = SCALE_AMOUNT} : (scale_value){.kind = SCALE_SCALAR};
        if (!scale_write(analysis, values, statement->c, result, error)) return false;
    } else {
        bool binary = opcode == QA_QC_ADD_F || opcode == QA_QC_SUB_F || opcode == QA_QC_MUL_F || opcode == QA_QC_DIV_F ||
            opcode == QA_QC_EQ_F || opcode == QA_QC_NE_F || opcode == QA_QC_EQ_S || opcode == QA_QC_NE_S ||
            opcode == QA_QC_LE || opcode == QA_QC_GE || opcode == QA_QC_LT || opcode == QA_QC_GT ||
            opcode == QA_QC_AND || opcode == QA_QC_OR || opcode == QA_QC_BITAND || opcode == QA_QC_BITOR;
        if (!binary || !scale_read(analysis, values, statement->a, &left, error) ||
            !scale_read(analysis, values, statement->b, &right, error))
            return reject(error, "QC damage scale contains an impure or unsupported Source instruction");
        if (left.kind == SCALE_ACTOR || right.kind == SCALE_ACTOR)
            return reject(error, "QC damage scale arithmetic consumes an entity reference");
        if (analysis->transform) result = (scale_value){.kind = left.kind == SCALE_AMOUNT || right.kind == SCALE_AMOUNT ? SCALE_AMOUNT : SCALE_SCALAR};
        else if (left.kind != SCALE_AMOUNT && right.kind != SCALE_AMOUNT) result = (scale_value){.kind = SCALE_SCALAR};
        else {
            scale_value scaled = left.kind == SCALE_AMOUNT ? left : right;
            scale_value factor = left.kind == SCALE_SCALAR ? left : right;
            if (factor.kind != SCALE_SCALAR || (opcode != QA_QC_MUL_F && opcode != QA_QC_DIV_F) ||
                (opcode == QA_QC_DIV_F && left.kind != SCALE_AMOUNT))
                return reject(error, "QC original multiplier has a nonlinear amount transformation");
            bool power = factor.known && isfinite(factor.constant) && factor.constant > 0 &&
                truncf(log2f(factor.constant)) == log2f(factor.constant);
            if (opcode == QA_QC_DIV_F && !power) return reject(error, "QC multiplier reciprocal is not exactly representable");
            bool increasing = power && (opcode == QA_QC_DIV_F ? factor.constant <= 1 : factor.constant >= 1);
            if (scaled.operations && !(scaled.powers && increasing))
                return reject(error, "QC original rounded scales cannot be represented by one multiplier");
            result = scaled; ++result.operations; result.identity &= factor.known && factor.constant == 1;
            result.powers &= increasing; analysis->multiplied = true;
        }
        if (!scale_write(analysis, values, statement->c, result, error)) return false;
    }
    return scale_edge(analysis, pending, pc, (int64_t)pc + 1, values, error);
}
static bool scale_parse(application_qc_combat_profile *profile, const qa_json_document *doc,
    qa_json_id node, qa_error *error)
{
    damage_scale *scale = &profile->scale;
    if (node == QA_JSON_NONE && !scale->implicit) return true;
    scale_analysis analysis = {.profile = profile, .function = qa_qc_program_function(profile->program, profile->damage.function),
        .info = qa_qc_program_describe(profile->program), .inflictor = scale->inflictor,
        .damage = scale->region.saved_word};
    if (node != QA_JSON_NONE) {
        qa_json_id kind = qa_json_get(doc, node, "kind");
        analysis.transform = qa_json_string_equal(doc, kind, "transform");
        analysis.identity = qa_json_string_equal(doc, kind, "identity");
        if (kind != QA_JSON_NONE && !analysis.transform && !analysis.identity && !qa_json_string_equal(doc, kind, "multiplier"))
            return reject(error, "QC damage scale declaration kind is invalid");
        char *name = application_qc_declaration_string(doc, qa_json_get(doc, node, "function"), error);
        bool same = name && !strcmp(name, analysis.function->name); free(name);
        if (!same || !scale_number(doc, qa_json_get(doc, node, "entry"), &scale->region.entry, error) ||
            !scale_number(doc, qa_json_get(doc, node, "exit"), &scale->region.exit, error) ||
            !scale_number(doc, qa_json_get(doc, node, "damage"), &analysis.damage, error))
            return reject(error, "QC damage scale differs from its actual compiled damage function");
    }
    if (scale->region.entry < (uint32_t)analysis.function->first_statement || scale->region.exit <= scale->region.entry ||
        scale->region.exit >= qa_qc_program_function_end(profile->program, analysis.function))
        return reject(error, "QC damage scale differs from its actual compiled damage function");
    const damage_location *amount = NULL;
    for (size_t i = 0; i < profile->location_count; ++i) if (profile->locations[i].role == QC_INPUT_AMOUNT) {
        if (amount) return reject(error, "QC damage scale requires one actual incoming amount word");
        amount = profile->locations + i;
    }
    if (!amount || amount->frame_word != analysis.damage || (node != QA_JSON_NONE && !application_qc_statements_validate(doc,
        qa_json_get(doc, node, "statements"), profile->program, scale->region.entry, scale->region.exit, error)))
        return reject(error, "QC damage scale result differs from its original typed amount");
    analysis.parameter_end = analysis.function->parameter_start;
    for (uint32_t i = 0; i < analysis.function->parameter_count; ++i) analysis.parameter_end += analysis.function->parameter_sizes[i];
    scale->region.function = profile->damage.function; scale->region.saved_word = analysis.damage;
    scale->region.saved_scope = amount->parameter ? QA_QC_INLINE_FRAME : QA_QC_INLINE_GLOBAL;
    size_t words = analysis.info.global_words;
    analysis.written = calloc(words, 1); analysis.named = calloc(words, 1);
    analysis.prefix = calloc(words, 1); analysis.scratch = calloc(words, 1); analysis.constants = calloc(words, 1);
    analysis.context = calloc(words, sizeof(*analysis.context));
    size_t paths = (size_t)scale->region.exit - scale->region.entry + 1;
    scale_value **pending = calloc(paths, sizeof(*pending));
    bool ok = analysis.written && analysis.named && analysis.prefix && analysis.scratch && analysis.constants && analysis.context && pending;
    if (!ok) application_fail(error, QA_ERROR_MEMORY, "Owning QC damage scale qualification");
    for (uint32_t i = 0; ok && i < analysis.info.global_count; ++i) {
        const qa_qc_definition *definition = qa_qc_program_global(profile->program, i);
        if (*definition->name) for (uint32_t j = 0; j < (definition->type == QA_QC_VECTOR ? 3u : 1u); ++j)
            analysis.named[definition->offset + j] = 1;
    }
    for (uint32_t i = 0; ok && i < analysis.info.statement_count; ++i) {
        qa_qc_statement_access access;
        ok = qa_qc_program_statement_access(profile->program, i, &access, error);
        for (uint32_t j = 0; ok && j < access.write_count; ++j) analysis.written[access.write[j]] = 1;
    }
    for (size_t i = 0; ok && i < profile->damage.global_count; ++i) {
        const application_qc_global *global = profile->damage.globals + i;
        scale_value value = {.kind = SCALE_SCALAR};
        if (global->value.kind == QC_VALUE_INPUT) {
            application_qc_input_id input = global->value.source;
            if (input == QC_INPUT_AMOUNT) value = (scale_value){.kind = SCALE_AMOUNT, .identity = true, .powers = true};
            else if (input == QC_INPUT_SELF || input == QC_INPUT_ATTACKER || input == QC_INPUT_INFLICTOR)
                value = (scale_value){.kind = SCALE_ACTOR, .identity = input == QC_INPUT_ATTACKER};
            else if (input != QC_INPUT_TIME) ok = reject(error, "QC damage scale has an undeclared query dependency");
        } else if (global->value.kind == QC_VALUE_CONSTANT && global->value.constant.kind == QA_QC_GAME_FLOAT)
            value = (scale_value){.kind = SCALE_SCALAR, .known = true, .constant = global->value.constant.value.number};
        analysis.context[global->definition->offset] = value;
    }
    if (ok && scale->implicit) {
        const qa_qc_definition *time = qa_qc_program_find_global(profile->program, "time");
        analysis.context[time->offset] = (scale_value){.kind = SCALE_SCALAR};
        if (scale->inflictor) {
            const qa_qc_definition *deathmatch = qa_qc_program_find_global(profile->program, "deathmatch");
            analysis.context[deathmatch->offset] = (scale_value){.kind = SCALE_SCALAR};
        }
    }
    for (uint32_t pc = (uint32_t)analysis.function->first_statement; ok && pc < scale->region.entry; ++pc) {
        const qa_qc_statement *statement = qa_qc_program_statement(profile->program, pc);
        qa_qc_statement_access access;
        ok = qa_qc_program_statement_access(profile->program, pc, &access, error);
        if (!ok) break;
        if ((statement->opcode >= QA_QC_STOREP_F && statement->opcode <= QA_QC_STOREP_FN) ||
            called(statement->opcode) || statement->opcode == QA_QC_STATE)
            ok = reject(error, "QC damage scale query follows Source side effects");
        for (uint32_t i = 0; ok && i < access.read_count; ++i)
            if (access.read[i] == analysis.damage || analysis.context[access.read[i]].kind == SCALE_AMOUNT)
                ok = reject(error, "QC damage-dependent policy precedes the declared query region");
        if (!(statement->opcode >= QA_QC_STORE_F && statement->opcode <= QA_QC_STORE_FN && statement->a == statement->b))
            for (uint32_t i = 0; ok && i < access.write_count; ++i) analysis.prefix[access.write[i]] = 1;
    }
    if (ok && analysis.prefix[analysis.damage]) ok = reject(error, "QC Source changes amount before its declared scale region");
    if (ok && scale->implicit) {
        const qa_qc_definition *time = qa_qc_program_find_global(profile->program, "time");
        const qa_qc_definition *deathmatch = scale->inflictor ? qa_qc_program_find_global(profile->program, "deathmatch") : NULL;
        if (analysis.prefix[time->offset] || (deathmatch && analysis.prefix[deathmatch->offset]))
            ok = reject(error, "QC Source changes live Quad context before its scale region");
    }
    if (ok) {
        pending[0] = calloc(words, sizeof(**pending));
        if (!pending[0]) ok = application_fail(error, QA_ERROR_MEMORY, "Owning QC scale entry state");
        else {
            pending[0][analysis.damage] = (scale_value){.kind = SCALE_AMOUNT, .identity = true, .powers = true};
            for (size_t i = 0; i < profile->location_count; ++i) {
                const damage_location *location = profile->locations + i;
                if ((location->role == QC_INPUT_ATTACKER ||
                    (analysis.inflictor && location->role == QC_INPUT_INFLICTOR)) && location->parameter) {
                    if (analysis.prefix[location->frame_word]) { ok = reject(error, "QC Source changes scale entity before its region"); break; }
                    pending[0][location->frame_word] = (scale_value){.kind = SCALE_ACTOR,
                        .identity = location->role == QC_INPUT_ATTACKER};
                }
            }
        }
    }
    bool joined = false;
    for (size_t i = 0; ok && i < paths; ++i) {
        scale_value *values = pending[i];
        if (!values) continue;
        uint32_t pc = scale->region.entry + (uint32_t)i;
        if (pc == scale->region.exit) {
            scale_value value = values[analysis.damage];
            ok = value.kind != SCALE_UNSET && value.kind != SCALE_ACTOR &&
                (analysis.transform || value.kind == SCALE_AMOUNT) && (!analysis.identity || value.identity);
            if (!ok) reject(error, "QC damage scale join loses its declared amount semantics");
            joined = true;
        } else ok = scale_operations(&analysis, pending, pc, values, error);
        free(values); pending[i] = NULL;
    }
    if (ok && (!joined || (!analysis.transform && !analysis.identity && !analysis.multiplied)))
        ok = reject(error, "QC damage scale has no actual original amount result");
    uint32_t end = qa_qc_program_function_end(profile->program, analysis.function);
    for (uint32_t pc = (uint32_t)analysis.function->first_statement; ok && pc < end; ++pc) {
        if (pc >= scale->region.entry && pc < scale->region.exit) continue;
        const qa_qc_statement *statement = qa_qc_program_statement(profile->program, pc);
        int64_t destination = statement->opcode == QA_QC_GOTO ? (int64_t)pc + (int16_t)statement->a :
            (statement->opcode == QA_QC_IF || statement->opcode == QA_QC_IFNOT) ? (int64_t)pc + (int16_t)statement->b : -1;
        if ((destination > scale->region.entry && destination < scale->region.exit) ||
            (pc < scale->region.entry && destination >= scale->region.exit) ||
            (pc >= scale->region.exit && destination >= analysis.function->first_statement && destination <= scale->region.entry))
            ok = reject(error, "QC Source control flow bypasses, enters or repeats its private scale region");
    }
    for (uint32_t word = 28; ok && word < analysis.info.global_words; ++word) {
        scale->scratch_count += analysis.scratch[word] != 0;
        scale->constant_count += analysis.constants[word] != 0;
    }
    if (ok) {
        scale->scratch = scale->scratch_count ? calloc(scale->scratch_count, sizeof(*scale->scratch)) : NULL;
        scale->constants = scale->constant_count ? calloc(scale->constant_count, sizeof(*scale->constants)) : NULL;
        scale->initial = scale->constant_count ? calloc(scale->constant_count, sizeof(*scale->initial)) : NULL;
        if ((scale->scratch_count && !scale->scratch) || (scale->constant_count && (!scale->constants || !scale->initial)))
            ok = application_fail(error, QA_ERROR_MEMORY, "Owning qualified QC damage scale words");
    }
    size_t scratch = 0, constant = 0;
    for (uint32_t word = 28; ok && word < analysis.info.global_words; ++word) {
        if (analysis.scratch[word]) scale->scratch[scratch++] = word;
        if (analysis.constants[word]) {
            int32_t initial;
            ok = qa_qc_program_initial_int(profile->program, word, &initial, error);
            if (ok) { scale->constants[constant] = word; scale->initial[constant++] = (uint32_t)initial; }
        }
    }
    if (ok) {
        size_t private_count = 0;
        uint32_t *private_words = scale->scratch_count ? malloc(scale->scratch_count * sizeof(*private_words)) : NULL;
        if (scale->scratch_count && !private_words) ok = application_fail(error, QA_ERROR_MEMORY, "Qualifying QC private scale continuation");
        for (size_t i = 0; ok && i < scale->scratch_count; ++i)
            if (scale->scratch[i] != analysis.damage) private_words[private_count++] = scale->scratch[i];
        if (ok) ok = application_qc_region_private_writes_dead(profile->program, &scale->region,
            analysis.damage, private_words, private_count, error);
        free(private_words);
    }
    if (pending) for (size_t i = 0; i < paths; ++i) free(pending[i]);
    free(pending); free(analysis.written); free(analysis.named); free(analysis.prefix);
    free(analysis.scratch); free(analysis.constants); free(analysis.context);
    if (ok) { scale->qualified = true; scale->transform = analysis.transform; scale->region.replaceable = true; }
    return ok;
}
bool application_qc_combat_damage_amount(struct application_qc_state *engine, const qa_damage_request *request,
    double *out, bool *available, qa_error *error)
{
    if (!request || !out || !available || !isfinite(request->amount))
        return reject(error, "QC Source amount query requires finite original damage");
    application_qc_combat *owner = engine->combat; *available = owner && owner->profile->scale.qualified;
    if (!*available) { *out = request->amount; return true; }
    if (!held(owner, error)) return false;
    const damage_scale *scale = &owner->profile->scale; qa_qc_instance *vm = engine->provider->state.qc.instance;
    uint32_t slot = 0; qa_actor_id actor = request->attack.attacker, source_actor = actor;
    if (actor.registry) {
        if (!qa_qc_actor_observation_slot(vm, actor, &slot, error)) return false;
        if (!slot) source_actor = (qa_actor_id){0};
    } else {
        qa_qc_slot_binding world; int32_t reference;
        if (!qa_qc_slot(vm, 0, &world) ||
            (world.kind != QA_QC_SLOT_WORLD && world.kind != QA_QC_SLOT_OWNED) ||
            !qa_qc_slot_reference(vm, 0, &reference, error) || reference != 0)
            return reject(error, "QC amount query lost its actual Source world reference");
        if (world.kind == QA_QC_SLOT_OWNED &&
            (!qa_qc_actor_observation_slot(vm, world.actor, &slot, error) || slot != 0))
            return reject(error, "QC amount query lost its actual owned Source world actor");
    }
    for (size_t i = 0; i < scale->constant_count; ++i) {
        int32_t actual;
        if (!qa_qc_global_int(vm, scale->constants[i], &actual, error) || (uint32_t)actual != scale->initial[i])
            return reject(error, "QC scaling constant changed from its actual qualified artifact");
    }
    qa_unified_frame_lease *storage = scale->scratch_count ?
        application_control_storage_acquire(engine->provider->application, error) : NULL;
    uint32_t *saved = storage ? qa_unified_frame_lease_alloc(storage, scale->scratch_count,
        sizeof(*saved), _Alignof(uint32_t), error) : NULL;
    if (scale->scratch_count && !saved) {
        qa_unified_frame_lease_release(storage);
        return application_fail(error, QA_ERROR_MEMORY, "Borrowing QC scale scratch words");
    }
    bool ok = true; size_t staged = 0;
    for (size_t i = 0; ok && i < scale->scratch_count; ++i) {
        int32_t actual;
        ok = qa_qc_global_int(vm, scale->scratch[i], &actual, error);
        if (ok) { saved[i] = (uint32_t)actual; ++staged; }
    }
    application_qc_inputs inputs = {.self = source_actor, .attacker = source_actor,
        .inflictor = scale->inflictor ? request->attack.inflictor : source_actor,
        .amount = (float)request->amount, .time_ns = engine->source_time_ns}; uint32_t result[3];
    if (ok) {
        ++owner->calls;
        ok = application_qc_run_call_region(engine, &owner->profile->damage, &scale->region, &inputs, result, error);
        --owner->calls;
        if (ok) {
            *out = bits_float(result[0]);
            if (!isfinite(*out)) ok = reject(error, "QC original amount query returned invalid Source damage");
        }
    }
    qa_error first = error ? *error : (qa_error){0};
    for (size_t i = 0; i < staged; ++i) {
        qa_error unwind = {0};
        if (!qa_qc_stage_globals(vm, scale->scratch[i], saved + i, 1, &unwind)) {
            if (ok) first = unwind;
            ok = false;
        }
    }
    qa_unified_frame_lease_release(storage);
    if (!ok && error) *error = first;
    return ok;
}
bool application_qc_combat_damage_scale_available(const struct application_qc_state *engine)
{
    const application_qc_combat *owner = engine ? engine->combat : NULL;
    return owner && owner->engine == engine && owner->profile->scale.qualified &&
        engine->provider->state.qc.engine == engine && engine->provider->state.qc.instance &&
        engine->provider->state.qc.program == owner->profile->program &&
        engine->provider->state.qc.combat_profile == owner->profile;
}
static bool combat_source_amount(void *opaque, const qa_damage_request *request,
    double *out, qa_error *error)
{
    bool available = false;
    return application_qc_combat_damage_amount(opaque, request, out, &available, error) &&
        (available || application_fail(error, QA_ERROR_ARGUMENT, "QC Source lost its damage scale"));
}
bool application_qc_combat_prepare_damage(struct application_qc_state *engine,
    qa_damage_request *request, qa_error *error)
{
    if (!application_qc_combat_damage_scale_available(engine)) return true;
    qa_damage_modifier modifier = {.owner = engine->provider->owner,
        .context = engine, .transform = combat_source_amount};
    return qa_damage_apply_modifier(qa_session_actors(engine->services.session), request, &modifier, request, error);
}
