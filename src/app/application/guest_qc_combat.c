#include "guest_qc_combat.h"
#include "guest_qc_armor.h"
#include "guest_mod_item_definition.h"
#include "qa/qc_observation.h"
#include "qa/text.h"
#include <float.h>
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
    bool declared, transform;
} damage_scale;
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
    qa_qc_inline_region regions[2];
    size_t region_count;
    bool armor_declared, empty_declared;
};
typedef struct combat_actor {
    struct combat_actor *next;
    application_qc_combat *owner;
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
struct application_qc_combat {
    struct application_qc_state *engine;
    const application_qc_combat_profile *profile;
    combat_actor *actors;
    damage_frame *frame;
    incoming_damage *incoming;
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
static bool immutable_layout(application_provider *provider, application_qc_combat_profile *profile,
    qa_error *error)
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
    free(written);
    return ok && (reactions || reject(error, "QC combat has no typed original pain or death call sites"));
}
static bool scale_parse(application_qc_combat_profile *, const qa_json_document *, qa_json_id, qa_error *);
bool application_qc_combat_qualify(application_provider *provider, const qa_json_document *doc,
    qa_json_id root, qa_error *error)
{
    if (root == QA_JSON_NONE) return true;
    if (qa_json_type(doc, root) != QA_JSON_OBJECT || !provider->state.qc.qualified)
        return reject(error, "QC combat requires its actual declaration and qualified program");
    application_qc_combat_profile *profile = calloc(1, sizeof(*profile));
    if (!profile) return application_fail(error, QA_ERROR_MEMORY, "Owning QC combat declaration");
    provider->state.qc.qualified->combat = profile; profile->program = provider->state.qc.program;
    uint64_t inputs = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_ATTACKER) |
        (UINT64_C(1) << QC_INPUT_INFLICTOR) | (UINT64_C(1) << QC_INPUT_AMOUNT) |
        (UINT64_C(1) << QC_INPUT_KNOCKBACK) | (UINT64_C(1) << QC_INPUT_POINT) |
        (UINT64_C(1) << QC_INPUT_DIRECTION) | (UINT64_C(1) << QC_INPUT_NORMAL) | (UINT64_C(1) << QC_INPUT_TIME);
    if (!application_qc_call_parse(doc, qa_json_get(doc, root, "damage"), profile->program,
        inputs, false, &profile->damage, error) || !call_layout(profile, error)) return false;
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
    if (!profile->velocity || profile->velocity->type != QA_QC_VECTOR || !profile->pain ||
        profile->pain->type != QA_QC_FUNCTION || !profile->die || profile->die->type != QA_QC_FUNCTION ||
        !profile->self || profile->self->type != QA_QC_ENTITY || !immutable_layout(provider, profile, error))
        return reject(error, "QC combat lacks its actual typed Source reaction and armor layout");
    qa_json_id armor = qa_json_get(doc, root, "armorStage");
    if (armor != QA_JSON_NONE) {
        if (!application_qc_armor_parse(profile->program, doc, armor, &profile->armor, error) ||
            profile->armor.region.function != profile->damage.function ||
            !application_qc_armor_inputs(profile->program, &profile->damage, &profile->armor, false, error)) return false;
        profile->armor_declared = true;
    }
    if (!scale_parse(profile, doc, qa_json_get(doc, root, "damageScale"), error)) return false;
    if (profile->scale.declared) profile->regions[profile->region_count++] = profile->scale.region;
    if (profile->armor_declared) profile->regions[profile->region_count++] = profile->armor.region;
    qa_json_id empty = qa_json_get(doc, root, "emptyArmor");
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
void application_qc_combat_profile_free(application_qc_combat_profile *profile)
{
    if (!profile) return;
    application_qc_call_free(&profile->damage); application_qc_armor_free(&profile->armor);
    free(profile->locations); free(profile->reactions); free(profile->scale.scratch);
    free(profile->scale.constants); free(profile->scale.initial); free(profile);
}
const qa_qc_inline_region *application_qc_combat_regions(const application_qc_combat_profile *profile, size_t *count)
{ *count = profile ? profile->region_count : 0; return profile ? profile->regions : NULL; }
static combat_actor *find(application_qc_combat *owner, qa_actor_id actor)
{
    for (combat_actor *at = owner->actors; at; at = at->next)
        if (qa_actor_id_equal(at->actor, actor)) return at;
    return NULL;
}
static bool held(const application_qc_combat *owner, qa_error *error)
{
    struct application_qc_state *engine = owner->engine;
    return (engine && engine->provider->state.qc.engine == engine &&
        engine->provider->state.qc.program == owner->profile->program &&
        engine->provider->state.qc.qualified && engine->provider->state.qc.qualified->combat == owner->profile &&
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
    *out = (qa_armor){0}; uint32_t flags = (uint32_t)qa_number_to_i32(items);
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
    float damage, invincible;
    if (!current(actor, error) || !raw_float(actor, profile->health, NULL, &out->health, error) ||
        !raw_armor(actor, NULL, &out->armor, error) ||
        !raw_float(actor, profile->takedamage, NULL, &damage, error) ||
        !raw_float(actor, profile->invincible, NULL, &invincible, error)) return false;
    out->mass = 200; out->can_take_damage = damage != 0;
    out->invulnerable = invincible > (float)((double)actor->owner->engine->source_time_ns / 1e9);
    out->no_knockback = false; out->team = 0;
    return true;
}
static bool write_health(void *context, float value, qa_error *error)
{
    combat_actor *actor = context;
    return current(actor, error) && qa_qc_project_entity_float(actor->owner->engine->provider->state.qc.instance,
        actor->reference, actor->owner->profile->health->offset, value, error);
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
        if (!known || armor->regular.points > FLT_MAX || (double)(float)armor->regular.points != armor->regular.points)
            return reject(error, "QC primary armor differs from its exact Source identity or precision");
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
    uint32_t bits = (uint32_t)qa_number_to_i32(value) & ~(profile->masks[0] | profile->masks[1] | profile->masks[2]);
    if (armor->regular.kind == QA_ARMOR_Q1)
        for (size_t i = 0; i < 3; ++i) if (armor->regular.item == profile->armor_items[i]) bits |= profile->masks[i];
    qa_qc_instance *vm = actor->owner->engine->provider->state.qc.instance;
    return qa_qc_project_entity_float(vm, actor->reference, profile->armorvalue->offset,
        armor->regular.kind == QA_ARMOR_Q1 ? (float)armor->regular.points : 0, error) &&
        qa_qc_project_entity_float(vm, actor->reference, profile->armortype->offset,
        armor->regular.kind == QA_ARMOR_Q1 ? armor->regular.protection.q1_absorption : 0, error) &&
        qa_qc_project_entity_float(vm, actor->reference, profile->items->offset, (float)bits, error);
}
static bool empty_armor(void *context, double points, qa_regular_armor *out, bool *present, qa_error *error)
{
    combat_actor *actor = context;
    const application_qc_combat_profile *profile = actor->owner->profile;
    if (!current(actor, error)) return false;
    *present = profile->empty_declared;
    if (*present) {
        if (!isfinite(points) || points < 0 || points > FLT_MAX || (double)(float)points != points)
            return reject(error, "QC empty armor points exceed exact Source precision");
        *out = (qa_regular_armor){.kind = QA_ARMOR_Q1, .points = points,
            .item = profile->empty_item, .protection.q1_absorption = profile->empty_absorption};
    }
    return true;
}
static application_qc_inputs inputs_for(struct application_qc_state *engine, const qa_damage_request *request)
{
    return (application_qc_inputs){.self = request->target, .attacker = request->attack.attacker,
        .inflictor = request->attack.inflictor, .amount = request->amount, .knockback = request->knockback,
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
    owner->incoming = &incoming; ++owner->calls;
    application_qc_inputs inputs = inputs_for(owner->engine, request);
    uint32_t words[3];
    bool ok = application_qc_run_call(owner->engine, &owner->profile->damage, &inputs, words, error);
    --owner->calls; owner->incoming = incoming.previous;
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
    const struct application_qc_profile *qualified = engine->provider->state.qc.qualified;
    const application_qc_combat_profile *profile = qualified ? qualified->combat : NULL;
    if (!profile) return true;
    if (engine->combat) return held(engine->combat, error);
    engine->combat = calloc(1, sizeof(*engine->combat));
    if (!engine->combat) return application_fail(error, QA_ERROR_MEMORY, "Owning QC combat callback contexts");
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
    actor = calloc(1, sizeof(*actor));
    if (!actor) return application_fail(error, QA_ERROR_MEMORY, "Owning QC full-actor combat context");
    *actor = (combat_actor){.next = owner->actors, .owner = owner, .actor = slot.actor,
        .slot = slot.slot, .reference = reference}; owner->actors = actor;
    qa_combat_binding source = binding(actor);
    if (!qa_combat_bind(owner->engine->services.combat, actor->actor, &source, true, error)) return false;
    actor->serial = qa_combat_storage_serial(owner->engine->services.combat, actor->actor); actor->bound = true;
    return true;
}
bool application_qc_combat_prepare(struct application_qc_state *engine, qa_qc_instance *vm,
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
        !engine->provider->state.qc.qualified || engine->provider->state.qc.qualified->combat != owner->profile) return false;
    combat_actor *actor = find(owner, id); uint32_t slot; int32_t reference; qa_error error = {0};
    return actor && actor->bound && !actor->retired && !actor->restoring &&
        qa_combat_primary_current(engine->services.combat, id, actor->serial, actor) &&
        qa_qc_actor_observation_slot(engine->provider->state.qc.instance, id, &slot, &error) && slot == actor->slot &&
        qa_qc_slot_reference(engine->provider->state.qc.instance, slot, &reference, &error) && reference == actor->reference;
}
static bool touches(const qa_qc_store_event *event, const qa_qc_definition *field, uint32_t width)
{ return event->word < field->offset + width && event->word + event->count > field->offset; }
bool application_qc_combat_source_stored(struct application_qc_state *engine, qa_qc_instance *vm,
    const qa_qc_store_event *event, qa_error *error)
{
    application_qc_combat *owner = engine->combat;
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
    uint32_t *saved = profile->location_count ? calloc(profile->location_count, sizeof(*saved)) : NULL;
    if (profile->location_count && !saved) return application_fail(error, QA_ERROR_MEMORY, "Staging transformed QC damage context");
    size_t staged = 0; bool ok = current(entered->actor, error);
    for (size_t i = 0; ok && i < profile->location_count; ++i) {
        const damage_location *location = profile->locations + i; int32_t prior;
        ok = qa_qc_global_int(vm, location->word, &prior, error);
        if (!ok) break;
        saved[i] = (uint32_t)prior; ++staged;
        int32_t value = 0; float amount = request->amount;
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
    free(saved);
    if (entered->actor->retired && !entered->actor->calls) free(entered->actor);
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
    const qa_qc_definition *death = qa_qc_program_find_field(owner->profile->program, "deathtype");
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
            !qa_qc_call_set_arg_float(call->next, 1, effective->source.pain.damage, error)) return false;
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
    if (event->function != owner->profile->damage.function)
        return reaction_replace(owner, vm, event, next, handled, error);
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
    if (!authored_request(owner, target, attacker, inflictor, amount, &request, error)) return false;
    qa_damage_outcome outcome = {0}; ++owner->calls;
    bool ok;
    if (!eligible(owner, target_slot)) {
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
        if (ok) ok = qa_combat_run_source(engine->services.combat, &request, continued_damage, &entered, &outcome, error);
        if (ok && !qa_qc_call_completed(next)) ok = qa_qc_call_skip(next, (uint32_t[3]){0}, error);
    }
    --owner->calls; qa_damage_outcome_free(&outcome); return ok;
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
    if (profile->scale.declared && event->region.function == profile->scale.region.function &&
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
        uint32_t bits = (uint32_t)qa_number_to_i32(value);
        if (stage.no_armor) flags.no_armor = (bits & stage.no_armor) != 0;
        if (stage.no_power) flags.no_power_armor = (bits & stage.no_power) != 0;
        if (stage.no_regular) flags.no_regular_armor = (bits & stage.no_regular) != 0;
        if (stage.energy) flags.energy = (bits & stage.energy) != 0;
    }
    float amount, power_saved = 0, regular_saved = 0; int32_t original;
    if (!qa_qc_global_int(vm, stage.damage, &original, error) ||
        !qa_qc_global_float(vm, stage.damage, &amount, error)) return false;
    bool ok = !power || qa_combat_absorb(engine->services.combat, frame->request, QA_PROTECTION_POWERED,
        NULL, amount, flags, NULL, &power_saved, NULL, error);
    float remaining = amount - power_saved;
    uint32_t bits = float_bits(remaining);
    if (ok) ok = isfinite(remaining) && qa_qc_stage_globals(vm, stage.damage, &bits, 1, error);
    regular = qa_combat_protection_owner(engine->services.combat, frame->actor->actor, QA_PROTECTION_REGULAR, NULL, NULL);
    if (ok && regular) {
        ok = qa_combat_absorb(engine->services.combat, frame->request, QA_PROTECTION_REGULAR,
            NULL, remaining, flags, NULL, &regular_saved, NULL, error) && qa_qc_inline_skip_to_join(next, error);
    } else if (ok) ok = qa_qc_inline_continue(next, error) && qa_qc_global_float(vm, stage.region.saved_word, &regular_saved, error);
    float total = regular_saved + power_saved;
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
    combat_actor **link = &owner->actors;
    while (*link) {
        combat_actor *actor = *link;
        if (!qa_actor_id_equal(actor->actor, id)) { link = &actor->next; continue; }
        if (actor->bound && qa_combat_primary_current(engine->services.combat, id, actor->serial, actor) &&
            !qa_combat_detach_primary(engine->services.combat, id, actor->serial, actor, error)) return false;
        *link = actor->next; actor->bound = false; actor->retired = true;
        if (!actor->calls) free(actor);
    }
    return true;
}
bool application_qc_combat_idle(const struct application_qc_state *engine)
{ return !engine || !engine->combat || (!engine->combat->calls && !engine->combat->frame && !engine->combat->incoming); }
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
    if (find(owner, id)) return reject(error, "Saved QC combat duplicates its actual callback context");
    uint32_t physical; int32_t reference; qa_qc_slot_binding slot;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    if (!held(owner, error) || !qa_qc_actor_observation_slot(vm, id, &physical, error) ||
        !qa_qc_slot(vm, physical, &slot) || !eligible(owner, slot) ||
        !qa_qc_slot_reference(vm, physical, &reference, error))
        return reject(error, "Saved QC combat lost its actual restored full-actor Source row");
    combat_actor *actor = calloc(1, sizeof(*actor));
    if (!actor) return application_fail(error, QA_ERROR_MEMORY, "Restoring QC combat callback context");
    *actor = (combat_actor){.next = owner->actors, .owner = owner, .actor = id,
        .slot = physical, .reference = reference, .serial = serial, .bound = true, .restoring = true}; owner->actors = actor;
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
    bool transform, identity, multiplied;
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
        return out->kind != SCALE_ACTOR || out->identity ||
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
        if (!scale_read(analysis, values, statement->a, &left, error) || left.kind != SCALE_ACTOR || !left.identity ||
            !scale_read(analysis, values, statement->b, &right, error) || right.kind == SCALE_AMOUNT)
            return reject(error, "QC damage scale entity read does not belong to its actual attacker");
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
    if (node == QA_JSON_NONE) return true;
    damage_scale *scale = &profile->scale;
    scale_analysis analysis = {.profile = profile, .function = qa_qc_program_function(profile->program, profile->damage.function),
        .info = qa_qc_program_describe(profile->program)};
    qa_json_id kind = qa_json_get(doc, node, "kind");
    analysis.transform = qa_json_string_equal(doc, kind, "transform");
    analysis.identity = qa_json_string_equal(doc, kind, "identity");
    if (kind != QA_JSON_NONE && !analysis.transform && !analysis.identity && !qa_json_string_equal(doc, kind, "multiplier"))
        return reject(error, "QC damage scale declaration kind is invalid");
    char *name = application_qc_declaration_string(doc, qa_json_get(doc, node, "function"), error);
    bool same = name && !strcmp(name, analysis.function->name); free(name);
    if (!same || !scale_number(doc, qa_json_get(doc, node, "entry"), &scale->region.entry, error) ||
        !scale_number(doc, qa_json_get(doc, node, "exit"), &scale->region.exit, error) ||
        !scale_number(doc, qa_json_get(doc, node, "damage"), &analysis.damage, error) ||
        scale->region.entry < (uint32_t)analysis.function->first_statement || scale->region.exit <= scale->region.entry ||
        scale->region.exit >= qa_qc_program_function_end(profile->program, analysis.function))
        return reject(error, "QC damage scale differs from its actual compiled damage function");
    const damage_location *amount = NULL;
    for (size_t i = 0; i < profile->location_count; ++i) if (profile->locations[i].role == QC_INPUT_AMOUNT) {
        if (amount) return reject(error, "QC damage scale requires one actual incoming amount word");
        amount = profile->locations + i;
    }
    if (!amount || amount->frame_word != analysis.damage || !application_qc_statements_validate(doc,
        qa_json_get(doc, node, "statements"), profile->program, scale->region.entry, scale->region.exit, error))
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
    if (ok) {
        pending[0] = calloc(words, sizeof(**pending));
        if (!pending[0]) ok = application_fail(error, QA_ERROR_MEMORY, "Owning QC scale entry state");
        else {
            pending[0][analysis.damage] = (scale_value){.kind = SCALE_AMOUNT, .identity = true, .powers = true};
            for (size_t i = 0; i < profile->location_count; ++i) {
                const damage_location *location = profile->locations + i;
                if (location->role == QC_INPUT_ATTACKER && location->parameter) {
                    if (analysis.prefix[location->frame_word]) { ok = reject(error, "QC Source changes attacker before its scale region"); break; }
                    pending[0][location->frame_word] = (scale_value){.kind = SCALE_ACTOR, .identity = true};
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
    if (ok) { scale->declared = true; scale->transform = analysis.transform; scale->region.replaceable = true; }
    return ok;
}
bool application_qc_combat_damage_amount(struct application_qc_state *engine, qa_actor_id actor,
    float amount, float *out, bool *available, qa_error *error)
{
    if (!out || !available || !isfinite(amount) || amount < 0)
        return reject(error, "QC Source amount query requires finite original damage");
    application_qc_combat *owner = engine->combat; *available = owner && owner->profile->scale.declared;
    if (!*available) { *out = amount; return true; }
    if (!held(owner, error)) return false;
    const damage_scale *scale = &owner->profile->scale; qa_qc_instance *vm = engine->provider->state.qc.instance;
    uint32_t slot = 0; qa_actor_id source_actor = actor;
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
    uint32_t *saved = scale->scratch_count ? calloc(scale->scratch_count, sizeof(*saved)) : NULL;
    if (scale->scratch_count && !saved) return application_fail(error, QA_ERROR_MEMORY, "Borrowing QC scale scratch words");
    bool ok = true; size_t staged = 0;
    for (size_t i = 0; ok && i < scale->scratch_count; ++i) {
        int32_t actual;
        ok = qa_qc_global_int(vm, scale->scratch[i], &actual, error);
        if (ok) { saved[i] = (uint32_t)actual; ++staged; }
    }
    application_qc_inputs inputs = {.self = source_actor, .attacker = source_actor, .inflictor = source_actor,
        .amount = amount, .time_ns = engine->source_time_ns}; uint32_t result[3];
    if (ok) {
        ++owner->calls;
        ok = application_qc_run_call_region(engine, &owner->profile->damage, &scale->region, &inputs, result, error);
        --owner->calls;
        if (ok) {
            *out = bits_float(result[0]);
            if (!isfinite(*out) || *out < 0) ok = reject(error, "QC original amount query returned invalid Source damage");
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
    free(saved); if (!ok && error) *error = first; return ok;
}
bool application_qc_combat_damage_scale_declared(const struct application_qc_state *engine)
{
    const application_qc_combat *owner = engine ? engine->combat : NULL;
    return owner && owner->engine == engine && owner->profile->scale.declared &&
        engine->provider->state.qc.engine == engine && engine->provider->state.qc.instance &&
        engine->provider->state.qc.program == owner->profile->program &&
        engine->provider->state.qc.qualified && engine->provider->state.qc.qualified->combat == owner->profile;
}
