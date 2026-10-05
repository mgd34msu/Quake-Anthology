#include "guest_qc_protection.h"
#include "guest_qc_profile.h"
#include "guest_mod_item_definition.h"
#include "guest_qc_armor.h"
#include "qa/qc_observation.h"
#include "qa/text.h"
#include <float.h>
#include <limits.h>

typedef struct protection_selection { float value; uint32_t selected; } protection_selection;
typedef struct protection_definition {
    qa_protection_channel channel;
    qa_protection_claim claim;
    const qa_qc_definition *count, *selection;
    uint32_t item, mask, no_armor, no_power, no_regular, energy, radius;
    protection_selection *values;
    size_t value_count, region;
    application_qc_call absorb;
    bool has_region;
} protection_definition;
struct application_qc_protection_profile {
    protection_definition definitions[2];
    size_t count, region_count;
    qa_qc_inline_region regions[2];
};
typedef struct protection_channel {
    struct protection_channel *next;
    application_qc_protection *owner;
    qa_actor_id actor;
    qa_protection_lease lease;
    uint32_t client;
    size_t definition;
    unsigned calls;
    bool bound, restoring, retired;
} protection_channel;
typedef struct protection_scope {
    struct protection_scope *previous;
    qa_actor_id actor;
    qa_protection_observer *observer;
} protection_scope;
struct application_qc_protection {
    struct application_qc_state *engine;
    const application_qc_protection_profile *profile;
    protection_channel *channels;
    protection_scope *scope;
};

static bool fail(qa_error *error, qa_status status, const char *message)
{ return application_fail(error, status, message); }
static bool region_parse(application_provider *provider, const qa_json_document *doc, qa_json_id node,
    protection_definition *definition, qa_qc_inline_region *out, qa_error *error)
{
    application_qc_armor_stage stage = {0};
    bool ok = application_qc_armor_parse(provider->state.qc.program, doc, node, &stage, error);
    if (ok && (!stage.region.replaceable || stage.region.saved_scope == QA_QC_INLINE_NOT_STANDALONE))
        ok = fail(error, QA_ERROR_FORMAT, "QC protection region has no safe standalone source continuation");
    if (ok) ok = application_qc_armor_inputs(provider->state.qc.program, &definition->absorb, &stage, true, error);
    if (ok && stage.flag_bits && (stage.no_armor != definition->no_armor || stage.no_power != definition->no_power ||
        stage.no_regular != definition->no_regular || stage.energy != definition->energy))
        ok = fail(error, QA_ERROR_FORMAT, "QC protection region flags differ from its actual staged source masks");
    if (ok) *out = stage.region;
    application_qc_armor_free(&stage); return ok;
}
static const application_qc_bound_field *private_field(application_provider *provider,
    const qa_json_document *doc, qa_json_id node, qa_error *error)
{
    char *name = application_qc_declaration_string(doc, node, error);
    const application_qc_bound_field *found = NULL;
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    for (size_t i = 0; name && i < profile->field_count; ++i)
        if (profile->fields[i].kind == QC_FIELD_PRIVATE &&
            profile->fields[i].definition->type == QA_QC_FLOAT &&
            !strcmp(profile->fields[i].definition->name, name)) found = profile->fields + i;
    free(name);
    if (!found) fail(error, QA_ERROR_FORMAT, "QC protection requires declared private float storage");
    return found;
}
static bool mask(const qa_json_document *doc, qa_json_id node, bool nonzero,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, node, &value, error) || value > UINT32_C(0x7fffff) ||
        (nonzero && !value)) return fail(error, QA_ERROR_FORMAT, "QC protection mask exceeds exact source precision");
    *out = (uint32_t)value; return true;
}
static bool item(application_provider *provider, const qa_json_document *doc, qa_json_id node,
    uint32_t *out, qa_error *error)
{
    if (qa_json_type(doc, node) == QA_JSON_NULL) { *out = 0; return true; }
    return application_mod_item_identity(doc, node,
        qa_session_strings(provider->application->session), out, error);
}
static bool power(const qa_json_document *doc, qa_json_id node, bool none,
    uint32_t *out, qa_error *error)
{
    if (none && qa_json_string_equal(doc, node, "none")) *out = QA_POWER_NONE;
    else if (qa_json_string_equal(doc, node, "screen")) *out = QA_POWER_SCREEN;
    else if (qa_json_string_equal(doc, node, "shield")) *out = QA_POWER_SHIELD;
    else return fail(error, QA_ERROR_FORMAT, "QC powered protection kind is invalid");
    return true;
}
static bool identity(application_provider *provider, const qa_json_document *doc, qa_json_id node,
    bool rule, qa_string_id *out, qa_error *error)
{
    char *name = application_qc_declaration_string(doc, node, error);
    bool valid = name && *name && (!rule || (strchr(name, ':') && strchr(name, ':')[1]));
    bool ok = valid && qa_strings_intern(qa_session_strings(provider->application->session),
        (qa_bytes){(const uint8_t *)name, strlen(name)}, out, error);
    free(name);
    return ok || fail(error, QA_ERROR_FORMAT, "QC protection has no canonical declared identity");
}
static bool selection_parse(application_provider *provider, const qa_json_document *doc,
    qa_json_id node, protection_definition *definition, qa_error *error)
{
    if (node == QA_JSON_NONE) return true;
    const application_qc_bound_field *field = private_field(provider, doc,
        qa_json_get(doc, node, "field"), error);
    if (!field || field->definition == definition->count)
        return fail(error, QA_ERROR_FORMAT, "QC protection selection must be a distinct private field");
    definition->selection = field->definition;
    qa_json_id masked = qa_json_get(doc, node, "mask");
    if (masked != QA_JSON_NONE && !mask(doc, masked, true, &definition->mask, error)) return false;
    if (field->private_mask && (!definition->mask ||
        (definition->mask & field->private_mask) != definition->mask))
        return fail(error, QA_ERROR_FORMAT, "QC protection selection exceeds its declared private bits");
    qa_json_id values = qa_json_get(doc, node, "values");
    if (!application_qc_declaration_array(doc, values, false, error) ||
        !(definition->value_count = qa_json_size(doc, values)) ||
        definition->value_count > SIZE_MAX / sizeof(*definition->values))
        return fail(error, QA_ERROR_FORMAT, "QC protection selection requires bounded declared values");
    definition->values = calloc(definition->value_count, sizeof(*definition->values));
    if (!definition->values) return fail(error, QA_ERROR_MEMORY, "Owning QC protection selection");
    for (size_t i = 0; i < definition->value_count; ++i) {
        qa_json_id row = qa_json_at(doc, values, i); double value;
        if (!qa_json_number(doc, qa_json_get(doc, row, "value"), &value, error) ||
            !isfinite(value) || fabs(value) > FLT_MAX || (double)(float)value != value ||
            (definition->mask && (value < 0 || value > UINT32_C(0x7fffff) || trunc(value) != value ||
             ((uint32_t)value & definition->mask) != (uint32_t)value)))
            return fail(error, QA_ERROR_FORMAT, "QC protection selection is not exactly representable");
        definition->values[i].value = (float)value;
        for (size_t j = 0; j < i; ++j)
            if (definition->values[j].value == (float)value)
                return fail(error, QA_ERROR_FORMAT, "QC protection selection values overlap");
        if (!(definition->channel == QA_PROTECTION_REGULAR ?
            item(provider, doc, qa_json_get(doc, row, "item"), &definition->values[i].selected, error) :
            power(doc, qa_json_get(doc, row, "kind"), true, &definition->values[i].selected, error))) return false;
    }
    return true;
}
void application_qc_protection_profile_free(application_qc_protection_profile *profile)
{
    if (!profile) return;
    for (size_t i = 0; i < profile->count; ++i) {
        free(profile->definitions[i].values);
        application_qc_call_free(&profile->definitions[i].absorb);
    }
    free(profile);
}
bool application_qc_protection_channel_declared(const application_provider *provider,
    qa_protection_channel channel)
{
    const struct application_qc_profile *qualified = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.qualified : NULL;
    const application_qc_protection_profile *profile = qualified ? qualified->protection : NULL;
    for (size_t i = 0; profile && i < profile->count; ++i)
        if (profile->definitions[i].channel == channel) return true;
    return false;
}
bool application_qc_protection_qualify(application_provider *provider,
    const qa_json_document *doc, qa_json_id node, qa_error *error)
{
    if (!application_qc_declaration_array(doc, node, true, error)) return false;
    size_t count = qa_json_size(doc, node);
    if (!count) return true;
    struct application_qc_profile *qualified = provider->state.qc.qualified;
    if (!qualified->clients || count > 2 || qualified->protection)
        return fail(error, QA_ERROR_FORMAT, "QC protection requires actual clients and one definition per channel");
    application_qc_protection_profile *profile = calloc(1, sizeof(*profile));
    if (!profile) return fail(error, QA_ERROR_MEMORY, "Owning QC protection declaration");
    qualified->protection = profile;
    uint64_t inputs = (UINT64_C(1) << QC_INPUT_SELF) | (UINT64_C(1) << QC_INPUT_ATTACKER) |
        (UINT64_C(1) << QC_INPUT_INFLICTOR) | (UINT64_C(1) << QC_INPUT_AMOUNT) |
        (UINT64_C(1) << QC_INPUT_KNOCKBACK) | (UINT64_C(1) << QC_INPUT_POINT) |
        (UINT64_C(1) << QC_INPUT_DIRECTION) | (UINT64_C(1) << QC_INPUT_NORMAL) |
        (UINT64_C(1) << QC_INPUT_TIME) | (UINT64_C(1) << QC_INPUT_DAMAGE_FLAGS) |
        (UINT64_C(1) << QC_INPUT_PROTECTION_SCALE);
    for (size_t i = 0; i < count; ++i) {
        protection_definition *definition = profile->definitions + profile->count++;
        qa_json_id row = qa_json_at(doc, node, i), channel = qa_json_get(doc, row, "channel");
        if (qa_json_string_equal(doc, channel, "regular")) definition->channel = QA_PROTECTION_REGULAR;
        else if (qa_json_string_equal(doc, channel, "powered")) definition->channel = QA_PROTECTION_POWERED;
        else return fail(error, QA_ERROR_FORMAT, "QC protection channel is invalid");
        for (size_t j = 0; j < i; ++j) if (profile->definitions[j].channel == definition->channel)
            return fail(error, QA_ERROR_FORMAT, "QC protection duplicates a channel");
        definition->claim.owner = provider->owner;
        if (!identity(provider, doc, qa_json_get(doc, row, "id"), true, &definition->claim.rule, error)) return false;
        qa_json_id admission = qa_json_get(doc, row, "admission");
        qa_json_id kind = qa_json_get(doc, admission, "kind");
        if (admission == QA_JSON_NONE || qa_json_string_equal(doc, kind, "claim"))
            definition->claim.admission = QA_PROTECTION_CLAIM;
        else if (qa_json_string_equal(doc, kind, "replace-current-primary"))
            definition->claim.admission = QA_PROTECTION_REPLACE_CURRENT;
        else if (qa_json_string_equal(doc, kind, "replace-primary")) {
            definition->claim.admission = QA_PROTECTION_REPLACE_PRIMARY;
            if (!identity(provider, doc, qa_json_get(doc, admission, "owner"), false,
                &definition->claim.expected_owner, error)) return false;
        } else return fail(error, QA_ERROR_FORMAT, "QC protection admission is invalid");
        qa_json_id storage = qa_json_get(doc, row, "storage");
        const application_qc_bound_field *field = private_field(provider, doc,
            qa_json_get(doc, storage, definition->channel == QA_PROTECTION_REGULAR ? "points" : "cells"), error);
        if (!field || field->private_mask)
            return fail(error, QA_ERROR_FORMAT, "QC protection count requires its complete private float field");
        definition->count = field->definition;
        if (!(definition->channel == QA_PROTECTION_REGULAR ?
            item(provider, doc, qa_json_get(doc, storage, "item"), &definition->item, error) :
            power(doc, qa_json_get(doc, storage, "kind"), false, &definition->item, error)) ||
            !selection_parse(provider, doc, qa_json_get(doc, storage, "selection"), definition, error)) return false;
        for (size_t j = 0; j < i; ++j) {
            const protection_definition *other = profile->definitions + j;
            if (definition->count == other->count || definition->count == other->selection ||
                definition->selection == other->count || (definition->selection && definition->selection == other->selection &&
                 (!definition->mask || !other->mask || (definition->mask & other->mask))))
                return fail(error, QA_ERROR_FORMAT, "QC protection channels overlap private storage");
        }
        qa_json_id flags = qa_json_get(doc, row, "flags"), absorb = qa_json_get(doc, row, "absorb");
        if (!mask(doc, qa_json_get(doc, flags, "noArmor"), false, &definition->no_armor, error) ||
            !mask(doc, qa_json_get(doc, flags, "noPowerArmor"), false, &definition->no_power, error) ||
            !mask(doc, qa_json_get(doc, flags, "noRegularArmor"), false, &definition->no_regular, error) ||
            !mask(doc, qa_json_get(doc, flags, "energy"), false, &definition->energy, error) ||
            !mask(doc, qa_json_get(doc, flags, "radius"), false, &definition->radius, error) ||
            !application_qc_call_parse(doc, qa_json_get(doc, absorb, "call"), provider->state.qc.program,
                inputs, false, &definition->absorb, error)) return false;
        if (qa_json_string_equal(doc, qa_json_get(doc, absorb, "kind"), "region")) {
            qa_qc_inline_region region;
            if (!region_parse(provider, doc, qa_json_get(doc, absorb, "stage"), definition, &region, error)) return false;
            definition->has_region = true; definition->region = profile->region_count;
            for (size_t j = 0; j < profile->region_count; ++j)
                if (profile->regions[j].function == region.function && profile->regions[j].entry == region.entry) {
                    if (profile->regions[j].exit != region.exit || profile->regions[j].saved_scope != region.saved_scope ||
                        profile->regions[j].saved_word != region.saved_word || profile->regions[j].replaceable != region.replaceable)
                        return fail(error, QA_ERROR_FORMAT, "QC protection regions disagree at one actual source entry");
                    definition->region = j; break;
                }
            if (definition->region == profile->region_count) profile->regions[profile->region_count++] = region;
        } else if (!qa_json_string_equal(doc, qa_json_get(doc, absorb, "kind"), "function"))
            return fail(error, QA_ERROR_FORMAT, "QC protection absorption kind is invalid");
    }
    return true;
}
const qa_qc_inline_region *application_qc_protection_regions(
    const application_qc_protection_profile *profile, size_t *count)
{
    if (count) *count = profile ? profile->region_count : 0;
    return profile && profile->region_count ? profile->regions : NULL;
}
static bool held(const application_qc_protection *owner, qa_error *error)
{
    const struct application_qc_state *engine = owner ? owner->engine : NULL;
    const application_provider *provider = engine ? engine->provider : NULL;
    return (provider && provider->state.qc.engine == engine && engine->protection == owner &&
        provider->state.qc.game && provider->state.qc.instance && provider->state.qc.qualified &&
        provider->state.qc.qualified->protection == owner->profile) ||
        fail(error, QA_ERROR_ARGUMENT, "QC protection lost its actual held source instance");
}
static bool client(const application_qc_protection *owner, qa_actor_id actor, uint32_t *out, qa_error *error)
{
    if (!held(owner, error) || !qa_actors_get(qa_session_actors(owner->engine->services.session), actor))
        return fail(error, QA_ERROR_ARGUMENT, "QC protection requires its live full actor");
    for (uint32_t i = 1; i <= owner->engine->max_clients; ++i)
        if (owner->engine->clients[i].connected && qa_actor_id_equal(owner->engine->clients[i].actor, actor)) {
            int32_t reference; qa_actor_id actual;
            if (!qa_qc_actor_reference(owner->engine->provider->state.qc.instance, actor, false, &reference, error) ||
                !qa_qc_reference_actor(owner->engine->provider->state.qc.instance, reference, &actual, error) ||
                !qa_actor_id_equal(actual, actor)) return false;
            *out = i; return true;
        }
    return fail(error, QA_ERROR_NOT_FOUND, "QC protection actor has no actual connected source client");
}
static protection_channel *find(application_qc_protection *owner, qa_actor_id actor, size_t definition)
{
    for (protection_channel *row = owner->channels; row; row = row->next)
        if (row->definition == definition && qa_actor_id_equal(row->actor, actor)) return row;
    return NULL;
}
static bool require(protection_channel *row, int32_t *reference, qa_error *error)
{
    uint32_t slot;
    if (!client(row->owner, row->actor, &slot, error) || slot != row->client ||
        row->definition >= row->owner->profile->count) return false;
    struct application_qc_state *engine = row->owner->engine;
    if (row->restoring) {
        if (engine->provider->application->operation != APPLICATION_PERSISTING)
            return fail(error, QA_ERROR_ARGUMENT, "QC protection restore context escaped its actual persistence boundary");
    } else if (!engine->initialized || engine->loading || row->retired ||
        !qa_combat_protection_current(engine->services.combat, row->lease))
        return fail(error, QA_ERROR_ARGUMENT, "QC protection channel lost its actual admitted lease");
    return qa_qc_actor_reference(engine->provider->state.qc.instance, row->actor, false, reference, error);
}
static uint32_t source_integer(float value)
{
    return (uint32_t)qa_source_float_to_i32(value);
}
static bool scalar(protection_channel *row, int32_t reference, const qa_qc_definition *field,
    const qa_qc_store_event *before, float *out, qa_error *error)
{
    if (before && field->offset >= before->word && field->offset - before->word < before->count) {
        memcpy(out, before->before + (field->offset - before->word), sizeof(*out));
    } else {
        qa_qc_instance *vm = row->owner->engine->provider->state.qc.instance;
        qa_actor_id actual; uint32_t slot;
        if (!qa_qc_reference_actor(vm, reference, &actual, error) ||
            !qa_actor_id_equal(actual, row->actor) ||
            !qa_qc_actor_observation_slot(vm, actual, &slot, error) ||
            !qa_qc_actor_observation_float(vm, slot, actual, field->offset, out, error)) return false;
    }
    return isfinite(*out) || fail(error, QA_ERROR_FORMAT, "QC protection source storage is nonfinite");
}
static bool read_at(protection_channel *row, int32_t reference, const qa_qc_store_event *before,
    qa_armor *out, qa_error *error)
{
    const protection_definition *definition = row->owner->profile->definitions + row->definition;
    uint32_t selected = definition->item; float count = 0;
    if (definition->selection) {
        float value;
        if (!scalar(row, reference, definition->selection, before, &value, error)) return false;
        if (definition->mask) {
            uint32_t bits = source_integer(value) & definition->mask;
            int32_t integer; memcpy(&integer, &bits, sizeof(integer));
            value = (float)integer;
        }
        size_t i = 0;
        while (i < definition->value_count && definition->values[i].value != value) ++i;
        if (i == definition->value_count)
            return fail(error, QA_ERROR_FORMAT, "QC protection source selected an undeclared value");
        selected = definition->values[i].selected;
    }
    if ((definition->channel == QA_PROTECTION_REGULAR || selected != QA_POWER_NONE) &&
        (!scalar(row, reference, definition->count, before, &count, error) || count < 0))
        return fail(error, QA_ERROR_FORMAT, "QC protection source count is invalid");
    if (definition->channel == QA_PROTECTION_REGULAR)
        out->regular = (qa_regular_armor){.kind = QA_ARMOR_SOURCE, .points = count, .item = selected};
    else out->powered = (qa_powered_armor){.kind = (qa_power_kind)selected,
        .cells = selected == QA_POWER_NONE ? 0 : count,
        .source_owner = selected == QA_POWER_NONE ? 0 : row->owner->engine->provider->owner,
        .source_kind = selected == QA_POWER_NONE ? QA_POWER_SOURCE_Q2 : QA_POWER_SOURCE_GENERIC};
    return true;
}
static bool read_binding(void *context, qa_armor *out, qa_error *error)
{
    protection_channel *row = context; int32_t reference;
    return out && require(row, &reference, error) && read_at(row, reference, NULL, out, error);
}
static bool validate_write(void *context, const qa_armor *next, qa_error *error)
{
    protection_channel *row = context; int32_t reference;
    const protection_definition *definition = row->owner->profile->definitions + row->definition;
    if (!next || !require(row, &reference, error)) return false;
    double count; uint32_t selected;
    if (definition->channel == QA_PROTECTION_REGULAR) {
        if (next->regular.kind != QA_ARMOR_SOURCE)
            return fail(error, QA_ERROR_ARGUMENT, "QC protection cannot own a foreign regular armor shape");
        count = next->regular.points;
        selected = next->regular.item;
    } else {
        count = next->powered.kind == QA_POWER_NONE ? 0 : next->powered.cells;
        selected = next->powered.kind;
        if (selected != QA_POWER_NONE && (next->powered.source_kind != QA_POWER_SOURCE_GENERIC ||
            next->powered.source_owner != row->owner->engine->provider->owner))
            return fail(error, QA_ERROR_ARGUMENT, "QC powered protection changed its actual source owner");
    }
    if (!isfinite(count) || count < 0 || count > FLT_MAX || (double)(float)count != count)
        return fail(error, QA_ERROR_ARGUMENT, "QC protection count is not exactly representable in source storage");
    if (!definition->selection) {
        if (selected != definition->item)
            return fail(error, QA_ERROR_ARGUMENT, "QC protection write changed its fixed source selection");
    } else {
        size_t i = 0; while (i < definition->value_count && definition->values[i].selected != selected) ++i;
        if (i == definition->value_count)
            return fail(error, QA_ERROR_ARGUMENT, "QC protection write selected an undeclared source value");
    }
    return true;
}
static bool write_binding(void *context, const qa_armor *next, qa_error *error)
{
    protection_channel *row = context; int32_t reference;
    const protection_definition *definition = row->owner->profile->definitions + row->definition;
    if (!validate_write(context, next, error) || !require(row, &reference, error)) return false;
    uint32_t selected = definition->channel == QA_PROTECTION_REGULAR ?
        next->regular.item : (uint32_t)next->powered.kind;
    if (definition->selection) {
        size_t i = 0; while (definition->values[i].selected != selected) ++i;
        float value = definition->values[i].value;
        if (definition->mask) {
            float current;
            if (!scalar(row, reference, definition->selection, NULL, &current, error)) return false;
            uint32_t bits = (source_integer(current) & ~definition->mask) | source_integer(value);
            int32_t integer; memcpy(&integer, &bits, sizeof(integer));
            value = (float)integer;
            if (source_integer(value) != bits)
                return fail(error, QA_ERROR_ARGUMENT, "QC protection masked write would discard other source bits");
        }
        if (!qa_qc_set_entity_float(row->owner->engine->provider->state.qc.instance,
            reference, definition->selection->offset, value, error)) return false;
    }
    double count = definition->channel == QA_PROTECTION_REGULAR ?
        next->regular.points :
        (next->powered.kind == QA_POWER_NONE ? 0 : next->powered.cells);
    return qa_qc_set_entity_float(row->owner->engine->provider->state.qc.instance,
        reference, definition->count->offset, (float)count, error);
}
static bool scale_declared(const application_qc_call *call)
{
    for (size_t i = 0; i < call->argument_count; ++i)
        if (call->arguments[i].kind == QC_VALUE_INPUT && call->arguments[i].source == QC_INPUT_PROTECTION_SCALE) return true;
    for (size_t i = 0; i < call->global_count; ++i)
        if (call->globals[i].value.kind == QC_VALUE_INPUT && call->globals[i].value.source == QC_INPUT_PROTECTION_SCALE) return true;
    return false;
}
static bool absorb_binding(void *context, const qa_damage_request *request,
    const qa_damage_geometry *geometry, float amount, qa_damage_flags flags,
    qa_protection_observer *observer, float *saved, qa_error *error)
{
    protection_channel *row = context; int32_t reference;
    application_qc_protection *owner = row->owner;
    const protection_definition *definition = owner->profile->definitions + row->definition;
    if (!request || !geometry || !observer || !saved || !qa_actor_id_equal(request->target, row->actor) ||
        row->calls == UINT_MAX || !owner->engine->provider->constructed ||
        !owner->engine->provider->attached || owner->engine->provider->close_pending ||
        owner->engine->provider->application->destroy_requested || !require(row, &reference, error))
        return fail(error, QA_ERROR_ARGUMENT, "QC absorption lost its actual target and observer");
    if (definition->channel == QA_PROTECTION_REGULAR && flags.regular_scale != 1 && !scale_declared(&definition->absorb))
        return fail(error, QA_ERROR_ARGUMENT, "QC regular protection scale has no declared source input");
    uint32_t bits = (flags.no_armor ? definition->no_armor : 0) |
        (flags.no_power_armor ? definition->no_power : 0) |
        (flags.no_regular_armor ? definition->no_regular : 0) |
        (flags.energy ? definition->energy : 0) | (request->radius ? definition->radius : 0);
    application_qc_inputs inputs = {.self = row->actor, .attacker = request->attack.attacker,
        .inflictor = request->attack.inflictor, .amount = amount, .knockback = request->knockback,
        .point = geometry->point, .direction = geometry->direction, .normal = geometry->normal,
        .time_ns = owner->engine->source_time_ns, .damage_flags = (float)bits,
        .protection_scale = flags.regular_scale};
    protection_scope scope = {.previous = owner->scope, .actor = row->actor, .observer = observer};
    ++row->calls;
    owner->scope = &scope;
    uint32_t result[3];
    bool ok = definition->has_region ? application_qc_run_call_region(owner->engine, &definition->absorb,
        owner->profile->regions + definition->region, &inputs, result, error) :
        application_qc_run_call(owner->engine, &definition->absorb, &inputs, result, error);
    owner->scope = scope.previous;
    --row->calls;
    bool retired = row->retired;
    qa_actor_id actor = row->actor;
    if (!row->calls && retired) free(row);
    if (!ok) return false;
    float value; memcpy(&value, result, sizeof(value));
    if (!isfinite(value) || value < 0 || value > amount)
        return fail(error, QA_ERROR_FORMAT, "QC protection savings exceed the actual incoming damage");
    if (!retired && qa_actors_get(qa_session_actors(owner->engine->services.session), actor) &&
        !require(row, &reference, error)) return false;
    *saved = value; return true;
}
static qa_protection_binding binding(protection_channel *row)
{
    return (qa_protection_binding){.context = row, .read = read_binding,
        .validate_write = validate_write, .write = write_binding, .absorb = absorb_binding};
}
bool application_qc_protection_create(struct application_qc_state *engine, qa_error *error)
{
    const struct application_qc_profile *qualified = engine->provider->state.qc.qualified;
    if (!qualified || !qualified->protection) return true;
    if (engine->protection) return held(engine->protection, error);
    application_qc_protection *owner = calloc(1, sizeof(*owner));
    if (!owner) return fail(error, QA_ERROR_MEMORY, "Owning QC protection callback contexts");
    owner->engine = engine; owner->profile = qualified->protection; engine->protection = owner;
    return held(owner, error);
}
bool application_qc_protection_reserve(struct application_qc_state *engine, qa_actor_id actor, qa_error *error)
{
    application_qc_protection *owner = engine->protection;
    if (!owner) return true;
    uint32_t slot;
    if (!client(owner, actor, &slot, error)) return false;
    for (size_t i = 0; i < owner->profile->count; ++i) {
        protection_channel *row = find(owner, actor, i);
        if (row) {
            if (row->client != slot || !qa_combat_protection_current(engine->services.combat, row->lease))
                return fail(error, QA_ERROR_ARGUMENT, "QC protection reservation changed its actual client or lease");
            continue;
        }
        row = calloc(1, sizeof(*row));
        if (!row) return fail(error, QA_ERROR_MEMORY, "Owning QC protection lease callback context");
        *row = (protection_channel){.owner = owner, .actor = actor, .client = slot, .definition = i};
        const protection_definition *definition = owner->profile->definitions + i;
        if (!qa_combat_reserve_protection(engine->services.combat, actor, definition->channel,
            &definition->claim, &row->lease, error)) { free(row); return false; }
        row->next = owner->channels; owner->channels = row;
    }
    return true;
}
bool application_qc_protection_activate(struct application_qc_state *engine, qa_actor_id actor, qa_error *error)
{
    application_qc_protection *owner = engine->protection;
    if (!owner) return true;
    if (!application_qc_protection_reserve(engine, actor, error)) return false;
    for (protection_channel *row = owner->channels; row; row = row->next)
        if (qa_actor_id_equal(row->actor, actor) && !row->bound) {
            qa_protection_binding source = binding(row);
            if (!qa_combat_bind_protection(engine->services.combat, row->lease, &source, error)) return false;
            row->bound = true;
        }
    return true;
}
bool application_qc_protection_source_stored(struct application_qc_state *engine, qa_qc_instance *vm,
    const qa_qc_store_event *event, qa_error *error)
{
    application_qc_protection *owner = engine->protection;
    if (!owner || event->kind != QA_QC_STORE_ENTITY || !event->entity_reference) return true;
    if (vm != engine->provider->state.qc.instance || !held(owner, error)) return false;
    qa_actor_id actor;
    if (!qa_qc_reference_actor(vm, event->entity_reference, &actor, error)) return false;
    qa_protection_store change = {0};
    for (protection_channel *row = owner->channels; row; row = row->next) {
        if (!row->bound || row->restoring || !qa_actor_id_equal(row->actor, actor)) continue;
        const protection_definition *definition = owner->profile->definitions + row->definition;
        bool touches = definition->count->offset >= event->word && definition->count->offset - event->word < event->count;
        if (definition->selection) touches = touches || (definition->selection->offset >= event->word &&
            definition->selection->offset - event->word < event->count);
        if (!touches) continue;
        int32_t reference;
        if (!require(row, &reference, error) || reference != event->entity_reference ||
            !read_at(row, reference, event, &change.before, error) || !read_at(row, reference, NULL, &change.after, error)) return false;
        if (definition->channel == QA_PROTECTION_REGULAR)
            change.regular = !qa_regular_armor_equal(change.before.regular, change.after.regular);
        else change.powered = !qa_powered_armor_equal(change.before.powered, change.after.powered);
    }
    if (!change.regular && !change.powered) return true;
    for (protection_scope *scope = owner->scope; scope; scope = scope->previous)
        if (qa_actor_id_equal(scope->actor, actor)) return qa_protection_observe(scope->observer, &change, error);
    const qa_pickup_execution *pickup = NULL; bool found;
    if (!qa_pickups_execution_read(engine->provider->application->pickups, actor,
        engine->provider->owner, &pickup, &found, error)) return false;
    return !found || qa_pickup_store_protection((qa_pickup_execution *)pickup, &change, error);
}
bool application_qc_protection_release(struct application_qc_state *engine, qa_actor_id actor, qa_error *error)
{
    application_qc_protection *owner = engine->protection;
    if (!owner) return true;
    protection_channel **link = &owner->channels;
    while (*link) {
        protection_channel *row = *link;
        if (!qa_actor_id_equal(row->actor, actor)) { link = &row->next; continue; }
        if (row->restoring && !row->lease.serial) {
            qa_actor_owner actual;
            if (qa_combat_protection_owner(engine->services.combat, actor, row->lease.channel, &actual, &row->lease.serial) &&
                actual != engine->provider->owner) row->lease.serial = 0;
        }
        if (!qa_combat_close_protection(engine->services.combat, row->lease, error)) return false;
        *link = row->next; row->retired = true;
        if (!row->calls) free(row);
    }
    return true;
}
bool application_qc_protection_suspend(struct application_qc_state *engine, qa_error *error)
{
    application_qc_protection *owner = engine->protection;
    if (!owner) return true;
    if (owner->scope) return fail(error, QA_ERROR_ARGUMENT, "QC protection suspension retains an active source absorption");
    while (owner->channels)
        if (!application_qc_protection_release(engine, owner->channels->actor, error)) return false;
    return true;
}
bool application_qc_protection_destroy(struct application_qc_state *engine, qa_error *error)
{
    if (!application_qc_protection_suspend(engine, error)) return false;
    free(engine->protection); engine->protection = NULL; return true;
}
bool application_qc_protection_idle(const struct application_qc_state *engine)
{ return !engine || !engine->protection || !engine->protection->scope; }
static bool ready_rows(const struct application_qc_state *engine, qa_error *error)
{
    application_qc_protection *owner = engine ? engine->protection : NULL;
    if (!owner) return true;
    if (!held(owner, error) || !application_qc_protection_idle(engine)) return false;
    for (protection_channel *row = owner->channels; row; row = row->next) {
        int32_t reference;
        if (row->restoring || !require(row, &reference, error) ||
            qa_combat_protection_bound(engine->services.combat, row->lease) != row->bound)
            return fail(error, QA_ERROR_FORMAT, "QC protection context differs from its actual canonical lease");
        if (row->bound) { qa_armor actual = {0}; if (!read_at(row, reference, NULL, &actual, error)) return false; }
    }
    return true;
}
bool application_qc_protection_ready(const struct application_qc_state *engine, qa_error *error)
{
    application_qc_protection *owner = engine ? engine->protection : NULL;
    if (!owner) return true;
    if (engine->provider->application->operation == APPLICATION_PERSISTING)
        return held(owner, error) && application_qc_protection_idle(engine);
    return ready_rows(engine, error);
}
bool application_qc_protection_saved_binding(struct application_qc_state *engine, qa_actor_id actor,
    qa_protection_channel channel, const qa_protection_claim *claim, qa_protection_binding *out, qa_error *error)
{
    if (!claim || !out || engine->provider->application->operation != APPLICATION_PERSISTING ||
        !application_qc_protection_create(engine, error)) return false;
    application_qc_protection *owner = engine->protection;
    uint32_t slot;
    if (!owner || !client(owner, actor, &slot, error)) return false;
    for (size_t i = 0; i < owner->profile->count; ++i) {
        const protection_definition *definition = owner->profile->definitions + i;
        if (definition->channel != channel) continue;
        const qa_protection_claim *actual = &definition->claim;
        if (claim->owner != actual->owner || claim->expected_owner != actual->expected_owner ||
            claim->rule != actual->rule || claim->admission != actual->admission)
            return fail(error, QA_ERROR_FORMAT, "Saved QC protection differs from its actual declared claim");
        protection_channel *row = find(owner, actor, i);
        if (row) return fail(error, QA_ERROR_FORMAT, "Saved QC protection duplicates its actual callback context");
        row = calloc(1, sizeof(*row));
        if (!row) return fail(error, QA_ERROR_MEMORY, "Restoring QC protection callback context");
        *row = (protection_channel){.next = owner->channels, .owner = owner, .actor = actor,
            .client = slot, .definition = i, .lease = {.actor = actor, .channel = channel},
            .bound = true, .restoring = true};
        owner->channels = row; *out = binding(row); return true;
    }
    return fail(error, QA_ERROR_NOT_FOUND, "Saved QC protection has no actual channel declaration");
}
bool application_qc_protection_restore_attach(struct application_qc_state *engine, qa_error *error)
{
    application_qc_protection *owner = engine->protection;
    if (!owner) return true;
    if (!held(owner, error) || owner->scope) return false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        if (!engine->clients[slot].connected) continue;
        qa_actor_id actor = engine->clients[slot].actor;
        for (size_t i = 0; i < owner->profile->count; ++i) {
            const protection_definition *definition = owner->profile->definitions + i;
            qa_actor_owner actual; uint64_t serial;
            if (!qa_combat_protection_owner(engine->services.combat, actor, definition->channel, &actual, &serial) ||
                actual != engine->provider->owner || !serial)
                return fail(error, QA_ERROR_FORMAT, "Restored QC client lost its canonical protection reservation");
            protection_channel *row = find(owner, actor, i);
            if (!row) {
                row = calloc(1, sizeof(*row));
                if (!row) return fail(error, QA_ERROR_MEMORY, "Attaching restored QC protection reservation");
                *row = (protection_channel){.next = owner->channels, .owner = owner, .actor = actor,
                    .client = slot, .definition = i}; owner->channels = row;
            }
            qa_protection_lease lease = {actor, serial, definition->channel};
            if (row->client != slot || qa_combat_protection_bound(engine->services.combat, lease) != row->bound)
                return fail(error, QA_ERROR_FORMAT, "Restored QC protection callback differs from its canonical bound state");
            row->lease = lease; row->restoring = false;
        }
    }
    return ready_rows(engine, error);
}
