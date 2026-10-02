#include "guest_q3_combat_profile.h"
#include "guest_q3_grapple_profile.h"
#include "qa/json.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct application_q3_combat_profile {
    qa_qvm_image *image;
    qa_qvm_abi abi;
    char *path;
    application_q3_combat_definition definition;
    application_q3_combat_team *teams;
    application_q3_combat_mode *modes;
    application_q3_combat_tier *tiers;
};
static bool fail(qa_error *error, qa_status status, const char *text)
{ qa_error_set(error, status, 0, "%s", text); return false; }
static bool word(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, id, &value, error)) return false;
    if (value > UINT32_MAX) return fail(error, QA_ERROR_FORMAT, "Combat declaration exceeds its source word");
    *out = (uint32_t)value; return true;
}
static bool integer(const qa_json_document *doc, qa_json_id id, int32_t *out, qa_error *error)
{
    int64_t value;
    if (!qa_json_i64(doc, id, &value, error)) return false;
    if (value < INT32_MIN || value > INT32_MAX)
        return fail(error, QA_ERROR_FORMAT, "Combat declaration exceeds its signed source word");
    *out = (int32_t)value; return true;
}
static bool array(const qa_json_document *doc, qa_json_id id, qa_error *error)
{ return qa_json_type(doc, id) == QA_JSON_ARRAY || fail(error, QA_ERROR_FORMAT, "Combat declaration requires its source list"); }
static bool field(const qa_json_document *doc, qa_json_id id, size_t minimum,
    uint32_t extent, uint32_t *out, qa_error *error)
{
    return word(doc, id, out, error) && ((*out % 4 == 0 && extent >= 4 &&
        *out >= minimum && *out <= extent - 4) ||
        fail(error, QA_ERROR_FORMAT, "Combat field leaves its original private entity record"));
}
static bool entry(const qa_json_document *doc, qa_json_id id, const qa_qvm_image *image,
    uint32_t *out, qa_error *error)
{
    size_t count; const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    return word(doc, id, out, error) && ((*out < count && code[*out].opcode == QA_QVM_ENTER) ||
        fail(error, QA_ERROR_FORMAT, "Combat callback is not its original function entry"));
}
static bool stat(const qa_json_document *doc, qa_json_id id, uint32_t *out, qa_error *error)
{ return word(doc, id, out, error) && (*out < 16 || fail(error, QA_ERROR_FORMAT, "Combat stat leaves its public player record")); }
static bool fraction(const qa_json_document *doc, qa_json_id id, float *out, qa_error *error)
{
    double value;
    if (!qa_json_number(doc, id, &value, error)) return false;
    float native = (float)value;
    if (!isfinite(value) || value < 0 || value > 1 || (double)native != value)
        return fail(error, QA_ERROR_FORMAT, "Combat protection is not its original binary32 fraction");
    *out = native; return true;
}
static bool masks(const qa_json_document *doc, qa_json_id object,
    const char *const *names, uint32_t **values, size_t count, qa_error *error)
{
    uint32_t occupied = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!word(doc, qa_json_get(doc, object, names[i]), values[i], error)) return false;
        uint32_t value = *values[i];
        if (!value || (value & (value - 1)) || (occupied & value))
            return fail(error, QA_ERROR_FORMAT, "Combat masks overlap or are not individual source bits");
        occupied |= value;
    }
    return true;
}
static bool call(const qa_json_document *doc, qa_json_id object,
    const char *const *names, size_t roles_count, const qa_qvm_image *image,
    application_q3_combat_call *out, qa_error *error)
{
    qa_json_id roles = qa_json_get(doc, object, "roles"), extras = qa_json_get(doc, object, "extras");
    if (!array(doc, extras, error)) return false;
    size_t extra_count = qa_json_size(doc, extras);
    if (extra_count > APPLICATION_Q3_COMBAT_ARGUMENTS - roles_count)
        return fail(error, QA_ERROR_FORMAT, "Combat arguments exceed original OP_ARG capacity");
    out->count = roles_count + extra_count;
    bool occupied[APPLICATION_Q3_COMBAT_ARGUMENTS] = {0};
    for (size_t i = 0; i < roles_count; ++i) {
        uint32_t position;
        if (!word(doc, qa_json_get(doc, roles, names[i]), &position, error)) return false;
        if (position >= out->count || occupied[position])
            return fail(error, QA_ERROR_FORMAT, "Combat roles do not own distinct original arguments");
        occupied[position] = true; out->positions[i] = position;
    }
    for (size_t i = 0; i < extra_count; ++i) {
        qa_json_id at = qa_json_at(doc, extras, i), kind = qa_json_get(doc, at, "kind");
        uint32_t position;
        if (!word(doc, qa_json_get(doc, at, "index"), &position, error)) return false;
        if (position >= out->count || occupied[position])
            return fail(error, QA_ERROR_FORMAT, "Combat extra aliases another original argument");
        occupied[position] = true;
        qa_json_id value = qa_json_get(doc, at, "value");
        if (qa_json_string_equal(doc, kind, "int32")) {
            if (!integer(doc, value, &out->words[position], error)) return false;
        } else if (qa_json_string_equal(doc, kind, "address")) {
            uint32_t address;
            if (!word(doc, value, &address, error)) return false;
            /* Address extras name bytes inside the original source extent. */
            if (!qa_qvm_qualify_source_span(image, address, 1, error)) return false;
            memcpy(out->words + position, &address, sizeof(address));
        } else if (qa_json_string_equal(doc, kind, "float32")) {
            double number;
            if (!qa_json_number(doc, value, &number, error)) return false;
            float native = (float)number;
            if (!isfinite(number) || !isfinite(native))
                return fail(error, QA_ERROR_FORMAT, "Combat extra is not a finite binary32 value");
            memcpy(out->words + position, &native, sizeof(native));
        } else return fail(error, QA_ERROR_FORMAT, "Combat extra has no source word representation");
    }
    return true;
}
static bool reaction(const qa_json_document *doc, qa_json_id object,
    application_q3_reaction_call *out, qa_error *error)
{
    qa_json_id roles = qa_json_get(doc, object, "roles");
    if (!word(doc, qa_json_get(doc, object, "arguments"), &out->count, error) ||
        !word(doc, qa_json_get(doc, roles, "target"), &out->target, error) ||
        !word(doc, qa_json_get(doc, roles, "amount"), &out->amount, error)) return false;
    return (out->count >= 2 && out->count <= APPLICATION_Q3_COMBAT_ARGUMENTS &&
        out->target < out->count && out->amount < out->count && out->target != out->amount) ||
        fail(error, QA_ERROR_FORMAT, "Combat reaction roles leave their original call");
}
static bool teams(application_q3_combat_profile *p, const qa_json_document *doc,
    qa_json_id list, qa_strings *strings, qa_error *error)
{
    if (!array(doc, list, error)) return false;
    size_t count = qa_json_size(doc, list);
    if (count > SIZE_MAX / sizeof(*p->teams)) return fail(error, QA_ERROR_MEMORY, "Combat team list is too large");
    p->teams = count ? calloc(count, sizeof(*p->teams)) : NULL;
    if (count && !p->teams) return fail(error, QA_ERROR_MEMORY, "Retaining original combat teams");
    p->definition.state.teams = p->teams; p->definition.state.team_count = count;
    for (size_t i = 0; i < count; ++i) {
        qa_json_id at = qa_json_at(doc, list, i); qa_buffer text = {0};
        if (!integer(doc, qa_json_get(doc, at, "value"), &p->teams[i].value, error) ||
            !qa_json_string(doc, qa_json_get(doc, at, "team"), &text, error)) return false;
        const uint8_t *colon = memchr(text.data, ':', text.size);
        bool valid = colon && colon != text.data && colon != text.data + text.size - 1 &&
            !memchr(text.data, 0, text.size);
        bool ok = valid ? qa_strings_intern(strings, (qa_bytes){text.data, text.size}, &p->teams[i].team, error) :
            fail(error, QA_ERROR_FORMAT, "Combat team requires its namespaced identity");
        qa_buffer_free(&text);
        if (!ok) return false;
        for (size_t j = 0; j < i; ++j) if (p->teams[j].value == p->teams[i].value)
            return fail(error, QA_ERROR_FORMAT, "Combat source team value is ambiguous");
    }
    return true;
}
static bool armor_modes(application_q3_combat_profile *p, const qa_json_document *doc,
    qa_json_id object, qa_error *error)
{
    if (qa_json_type(doc, object) == QA_JSON_NULL) return true;
    application_q3_combat_definition *d = &p->definition;
    qa_json_id modes = qa_json_get(doc, object, "whenAny"), tiers = qa_json_get(doc, object, "values");
    if (!stat(doc, qa_json_get(doc, object, "stat"), &d->armor.tier_stat, error) ||
        !fraction(doc, qa_json_get(doc, object, "fallback"), &d->armor.fallback, error) ||
        !array(doc, modes, error) || !array(doc, tiers, error)) return false;
    d->armor.mode_count = qa_json_size(doc, modes); d->armor.tier_count = qa_json_size(doc, tiers);
    if (d->armor.tier_stat == d->armor.points_stat || !d->armor.mode_count || !d->armor.tier_count ||
        d->armor.mode_count > SIZE_MAX / sizeof(*p->modes) || d->armor.tier_count > SIZE_MAX / sizeof(*p->tiers))
        return fail(error, QA_ERROR_FORMAT, "Combat armor tier selection is empty or aliases its points");
    p->modes = calloc(d->armor.mode_count, sizeof(*p->modes));
    p->tiers = calloc(d->armor.tier_count, sizeof(*p->tiers));
    if (!p->modes || !p->tiers) return fail(error, QA_ERROR_MEMORY, "Retaining original combat armor modes");
    d->armor.modes = p->modes; d->armor.tiers = p->tiers;
    for (size_t i = 0; i < d->armor.mode_count; ++i) {
        qa_json_id at = qa_json_at(doc, modes, i), comparison = qa_json_get(doc, at, "comparison");
        application_q3_combat_mode *mode = p->modes + i;
        if (!word(doc, qa_json_get(doc, at, "offset"), &mode->offset, error) ||
            !qa_qvm_qualify_global_word(p->image, mode->offset, error) ||
            !integer(doc, qa_json_get(doc, at, "value"), &mode->value, error)) return false;
        mode->equal = qa_json_string_equal(doc, comparison, "equal");
        if (!mode->equal && !qa_json_string_equal(doc, comparison, "not-equal"))
            return fail(error, QA_ERROR_FORMAT, "Combat armor mode has no source comparison");
    }
    for (size_t i = 0; i < d->armor.tier_count; ++i) {
        qa_json_id at = qa_json_at(doc, tiers, i);
        if (!integer(doc, qa_json_get(doc, at, "tier"), &p->tiers[i].value, error) ||
            !fraction(doc, qa_json_get(doc, at, "protection"), &p->tiers[i].protection, error)) return false;
        for (size_t j = 0; j < i; ++j) if (p->tiers[j].value == p->tiers[i].value)
            return fail(error, QA_ERROR_FORMAT, "Combat armor tier value is ambiguous");
    }
    return true;
}
static bool parse(application_q3_combat_profile *p, const qa_json_document *doc,
    qa_strings *strings, qa_error *error)
{
    static const char *const damage_names[] = {"target", "inflictor", "attacker", "direction", "point", "amount", "flags", "method"};
    static const char *const armor_names[] = {"target", "amount", "flags"};
    static const char *const damage_mask_names[] = {"radius", "noArmor", "noKnockback", "noProtection", "noTeamProtection"};
    static const char *const state_mask_names[] = {"notarget", "invulnerable", "noKnockback"};
    application_q3_combat_definition *d = &p->definition;
    qa_json_id root = qa_json_root(doc), combat = qa_json_get(doc, root, "combat");
    qa_json_id fields = qa_json_get(doc, combat, "fields"), callbacks = qa_json_get(doc, combat, "callbacks");
    qa_json_id reactions = qa_json_get(doc, combat, "reactions"), armor = qa_json_get(doc, combat, "armor");
    qa_json_id state = qa_json_get(doc, combat, "state"), team = qa_json_get(doc, state, "team"), mass = qa_json_get(doc, state, "mass");
    size_t shared = qa_qvm_shared_entity_bytes(p->abi), memory = qa_qvm_image_memory_size(p->image);
    if (!word(doc, qa_json_get(doc, combat, "entityStride"), &d->entity_stride, error) ||
        !word(doc, qa_json_get(doc, combat, "clientStride"), &d->client_stride, error)) return false;
    if (d->entity_stride < shared || d->client_stride < qa_qvm_player_bytes(p->abi) ||
        d->entity_stride % 4 || d->client_stride % 4 || d->entity_stride > memory || d->client_stride > memory)
        return fail(error, QA_ERROR_FORMAT, "Combat records leave the actual artifact ABI");
    const char *const field_names[] = {"inuse", "health", "takedamage", "parent", "client"};
    uint32_t *field_values[] = {&d->fields.inuse, &d->fields.health, &d->fields.takedamage, &d->fields.parent, &d->fields.client};
    for (size_t i = 0; i < 5; ++i)
        if (!field(doc, qa_json_get(doc, fields, field_names[i]), shared, d->entity_stride, field_values[i], error)) return false;
    if (!entry(doc, qa_json_get(doc, callbacks, "allocate"), p->image, &d->callbacks.allocate, error) ||
        !entry(doc, qa_json_get(doc, callbacks, "free"), p->image, &d->callbacks.free, error) ||
        !entry(doc, qa_json_get(doc, callbacks, "damage"), p->image, &d->callbacks.damage, error) ||
        !call(doc, qa_json_get(doc, combat, "damageCall"), damage_names, Q3_DAMAGE_WORDS, p->image, &d->damage_call, error) ||
        !field(doc, qa_json_get(doc, reactions, "flags"), shared, d->entity_stride, &d->reactions.flags, error) ||
        !field(doc, qa_json_get(doc, reactions, "pain"), shared, d->entity_stride, &d->reactions.pain, error) ||
        !field(doc, qa_json_get(doc, reactions, "die"), shared, d->entity_stride, &d->reactions.die, error) ||
        !reaction(doc, qa_json_get(doc, reactions, "painCall"), &d->reactions.pain_call, error) ||
        !reaction(doc, qa_json_get(doc, reactions, "dieCall"), &d->reactions.die_call, error) ||
        !entry(doc, qa_json_get(doc, armor, "checkArmor"), p->image, &d->armor.check, error) ||
        !call(doc, qa_json_get(doc, armor, "call"), armor_names, Q3_ARMOR_WORDS, p->image, &d->armor.call, error) ||
        !stat(doc, qa_json_get(doc, armor, "pointsStat"), &d->armor.points_stat, error) ||
        !fraction(doc, qa_json_get(doc, armor, "protection"), &d->armor.protection, error) ||
        !armor_modes(p, doc, qa_json_get(doc, armor, "tiers"), error) ||
        !stat(doc, qa_json_get(doc, state, "healthStat"), &d->state.health_stat, error) ||
        !stat(doc, qa_json_get(doc, team, "persistentStat"), &d->state.team_stat, error) ||
        !teams(p, doc, qa_json_get(doc, team, "values"), strings, error) ||
        !word(doc, qa_json_get(doc, combat, "grappleDamageMethod"), &d->grapple_method, error)) return false;
    if (d->grapple_method > INT32_MAX)
        return fail(error, QA_ERROR_FORMAT, "Combat damage method exceeds its signed original argument");
    uint32_t *damage_masks[] = {&d->damage_flags.radius, &d->damage_flags.no_armor, &d->damage_flags.no_knockback,
        &d->damage_flags.no_protection, &d->damage_flags.no_team_protection};
    uint32_t *state_masks[] = {&d->state.notarget, &d->state.invulnerable, &d->state.no_knockback};
    if (!masks(doc, qa_json_get(doc, combat, "damageFlags"), damage_mask_names, damage_masks, 5, error) ||
        !masks(doc, qa_json_get(doc, state, "flags"), state_mask_names, state_masks, 3, error)) return false;
    if (qa_json_string_equal(doc, qa_json_get(doc, mass, "kind"), "constant")) {
        double value;
        if (!qa_json_number(doc, qa_json_get(doc, mass, "value"), &value, error)) return false;
        d->state.mass_kind = Q3_COMBAT_MASS_CONSTANT; d->state.mass.constant = (float)value;
        if (!isfinite(value) || value < 0 || !isfinite(d->state.mass.constant))
            return fail(error, QA_ERROR_FORMAT, "Combat mass is not finite and nonnegative");
    } else {
        if (!qa_json_string_equal(doc, qa_json_get(doc, mass, "kind"), "entity") ||
            !field(doc, qa_json_get(doc, mass, "offset"), shared, d->entity_stride, &d->state.mass.offset, error))
            return fail(error, QA_ERROR_FORMAT, "Combat mass has no original private field");
        qa_json_id storage = qa_json_get(doc, mass, "storage");
        if (qa_json_string_equal(doc, storage, "int32")) d->state.mass_kind = Q3_COMBAT_MASS_INT32;
        else if (qa_json_string_equal(doc, storage, "float32")) d->state.mass_kind = Q3_COMBAT_MASS_FLOAT32;
        else return fail(error, QA_ERROR_FORMAT, "Combat mass has no original source representation");
    }
    return qa_qvm_source_scratch_qualify(p->image, 24, &d->scratch, error);
}
static bool builtin(application_q3_combat_profile *p, qa_strings *strings,
    bool *found, qa_error *error)
{
    application_q3_grapple_profile *grapple = NULL;
    if (!application_q3_grapple_profile_create(p->image, QA_QVM_GAME, p->abi,
        p->path, &grapple, error)) return false;
    if (!grapple) { *found = false; return true; }
    const application_q3_grapple_definition *source = application_q3_grapple_profile_definition(grapple);
    bool threewave = !strcmp(source->id, "threewave-1.7");
    bool lrctf = !strcmp(source->id, "lrctf-1.2");
    application_q3_combat_definition *d = &p->definition;
    *found = threewave || lrctf;
    if (!*found) { application_q3_grapple_profile_destroy(grapple); return true; }
    d->entity_stride = source->entity_stride; d->client_stride = source->client_stride;
    d->fields.inuse = source->fields.inuse; d->fields.health = source->fields.health;
    d->fields.takedamage = source->fields.takedamage; d->fields.parent = source->fields.parent;
    d->fields.client = source->fields.client;
    d->callbacks.allocate = source->callbacks.allocate; d->callbacks.free = source->callbacks.free;
    d->callbacks.damage = source->callbacks.damage; d->grapple_method = source->damage_method;
    d->damage_call.count = Q3_DAMAGE_WORDS;
    for (size_t i = 0; i < Q3_DAMAGE_WORDS; ++i) d->damage_call.positions[i] = (uint32_t)i;
    d->reactions.flags = 536; d->reactions.pain = threewave ? 712 : 728;
    d->reactions.die = threewave ? 716 : 732;
    d->reactions.pain_call = (application_q3_reaction_call){3, 0, 2};
    d->reactions.die_call = (application_q3_reaction_call){5, 0, 3};
    d->damage_flags.radius = 1; d->damage_flags.no_armor = 2; d->damage_flags.no_knockback = 4;
    d->damage_flags.no_protection = 8; d->damage_flags.no_team_protection = 16;
    d->state.health_stat = 0; d->state.team_stat = 3; d->state.notarget = 32;
    d->state.invulnerable = 16; d->state.no_knockback = 2048;
    d->state.mass_kind = Q3_COMBAT_MASS_CONSTANT; d->state.mass.constant = 200;
    d->armor.check = threewave ? 161690 : 139839; d->armor.points_stat = threewave ? 6 : 3;
    d->armor.protection = .66f; d->armor.call.count = Q3_ARMOR_WORDS;
    for (size_t i = 0; i < Q3_ARMOR_WORDS; ++i) d->armor.call.positions[i] = (uint32_t)i;
    application_q3_grapple_profile_destroy(grapple);
    p->teams = calloc(2, sizeof(*p->teams));
    if (!p->teams) return fail(error, QA_ERROR_MEMORY, "Retaining source-authored combat teams");
    d->state.team_count = 2; d->state.teams = p->teams;
    p->teams[0].value = 1; p->teams[1].value = 2;
    if (!qa_strings_intern_cstr(strings, "q3:1", &p->teams[0].team, error) ||
        !qa_strings_intern_cstr(strings, "q3:2", &p->teams[1].team, error)) return false;
    if (threewave) {
        p->modes = calloc(2, sizeof(*p->modes)); p->tiers = calloc(3, sizeof(*p->tiers));
        if (!p->modes || !p->tiers) return fail(error, QA_ERROR_MEMORY, "Retaining source-authored armor modes");
        p->modes[0] = (application_q3_combat_mode){107944, 10, true};
        p->modes[1] = (application_q3_combat_mode){1089728, 0, false};
        p->tiers[0] = (application_q3_combat_tier){0, .3f};
        p->tiers[1] = (application_q3_combat_tier){1, .6f};
        p->tiers[2] = (application_q3_combat_tier){2, .8f};
        d->armor.modes = p->modes; d->armor.mode_count = 2;
        d->armor.tiers = p->tiers; d->armor.tier_count = 3;
        d->armor.tier_stat = 3; d->armor.fallback = .3f;
        for (size_t i = 0; i < 2; ++i)
            if (!qa_qvm_qualify_global_word(p->image, p->modes[i].offset, error)) return false;
    }
    size_t count; const qa_qvm_instruction *code = qa_qvm_image_instructions(p->image, &count);
    if (d->armor.check >= count || code[d->armor.check].opcode != QA_QVM_ENTER)
        return fail(error, QA_ERROR_FORMAT, "Source-authored CheckArmor is not its actual function entry");
    uint32_t offsets[] = {d->reactions.flags, d->reactions.pain, d->reactions.die};
    for (size_t i = 0; i < 3; ++i)
        if (offsets[i] % 4 || offsets[i] < qa_qvm_shared_entity_bytes(p->abi) ||
            offsets[i] > d->entity_stride - 4)
            return fail(error, QA_ERROR_FORMAT, "Source-authored reaction leaves its private entity record");
    return qa_qvm_source_scratch_qualify(p->image, 24, &d->scratch, error);
}
bool application_q3_combat_profile_create(qa_qvm_image *image, qa_qvm_role role, qa_qvm_abi abi,
    const char *path, qa_bytes primary, qa_strings *strings, application_q3_combat_profile **out, qa_error *error)
{
    if (!image || role != QA_QVM_GAME || (unsigned)abi > QA_QVM_Q3_116N ||
        !path || !*path || !strings || !out || *out || (primary.size && !primary.data))
        return fail(error, QA_ERROR_ARGUMENT, "Combat metadata requires its actual GAME artifact and empty owner");
    application_q3_combat_profile *p = calloc(1, sizeof(*p));
    if (!p) return fail(error, QA_ERROR_MEMORY, "Retaining immutable original combat metadata");
    qa_qvm_image_retain(image); p->image = image; p->abi = abi;
    size_t length = strlen(path) + 1;
    p->path = malloc(length);
    if (!p->path) { application_q3_combat_profile_destroy(p); return fail(error, QA_ERROR_MEMORY, "Retaining original combat artifact path"); }
    memcpy(p->path, path, length);
    qa_json_document *doc = NULL; bool found = true;
    bool ok = primary.size ? qa_json_parse(primary, &doc, error) && parse(p, doc, strings, error) :
        builtin(p, strings, &found, error);
    qa_json_destroy(doc);
    if (!ok) { application_q3_combat_profile_destroy(p); return false; }
    if (!found) { application_q3_combat_profile_destroy(p); return true; }
    *out = p; return true;
}
void application_q3_combat_profile_destroy(application_q3_combat_profile *p)
{
    if (!p) return;
    free(p->teams); free(p->modes); free(p->tiers); free(p->path);
    qa_qvm_image_release(p->image); free(p);
}
const application_q3_combat_definition *application_q3_combat_profile_definition(const application_q3_combat_profile *p)
{ return p ? &p->definition : NULL; }
const qa_qvm_image *application_q3_combat_profile_image(const application_q3_combat_profile *p)
{ return p ? p->image : NULL; }
qa_qvm_abi application_q3_combat_profile_abi(const application_q3_combat_profile *p)
{ return p ? p->abi : QA_QVM_Q3_MODERN; }
const char *application_q3_combat_profile_path(const application_q3_combat_profile *p)
{ return p ? p->path : NULL; }
