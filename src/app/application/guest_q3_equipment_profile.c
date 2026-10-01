#include "guest_q3_equipment_profile.h"
#include "internal.h"
#include "qa/json.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool number(const qa_json_document *doc, qa_json_id value, uint32_t *out, qa_error *error)
{
    uint64_t result;
    if (!qa_json_u64(doc, value, &result, error)) return false;
    if (result > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Equipment presentation index exceeds its source word");
    *out = (uint32_t)result;
    return true;
}

static bool field(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    return number(doc, qa_json_get(doc, object, name), out, error);
}

static bool signed_field(const qa_json_document *doc, qa_json_id object, const char *name,
    int32_t *out, qa_error *error)
{
    int64_t result;
    if (!qa_json_i64(doc, qa_json_get(doc, object, name), &result, error)) return false;
    if (result < INT32_MIN || result > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Equipment warning state exceeds original int32");
    *out = (int32_t)result;
    return true;
}

void application_q3_equipment_profile_free(application_q3_equipment_profile *profile)
{
    if (!profile) return;
    if (profile->status)
        for (size_t i = 0; i < profile->status_count; ++i) free(profile->status[i].ammo);
    free(profile->status);
    *profile = (application_q3_equipment_profile){0};
}

static bool entry(const qa_qvm_instruction *code, size_t count, uint32_t pc, qa_error *error)
{
    return (pc < count && code[pc].opcode == QA_QVM_ENTER) ||
        application_fail(error, QA_ERROR_FORMAT, "Equipment presentation entry is not an original function");
}

static bool decision(const qa_qvm_instruction *code, size_t count, uint32_t owner,
    uint32_t pc, qa_error *error)
{
    if (pc <= owner || pc >= count || code[pc].opcode < QA_QVM_EQ || code[pc].opcode > QA_QVM_GEF)
        return application_fail(error, QA_ERROR_FORMAT, "Equipment visibility is not an original conditional");
    for (uint32_t at = owner + 1; at <= pc; ++at)
        if (code[at].opcode == QA_QVM_ENTER)
            return application_fail(error, QA_ERROR_FORMAT, "Equipment visibility leaves its original function");
    return true;
}

static bool qualify(const qa_qvm_image *image, const application_q3_equipment_profile *profile,
    qa_error *error)
{
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    const uint32_t fixed[] = {profile->hud, profile->view_entry, profile->warning_entry, profile->held_entry};
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (!entry(code, count, fixed[i], error)) return false;
        for (size_t j = 0; j < i; ++j)
            if (fixed[i] == fixed[j])
                return application_fail(error, QA_ERROR_FORMAT, "Equipment presentation functions overlap");
    }
    if (!profile->status_count)
        return application_fail(error, QA_ERROR_FORMAT, "Equipment status omits its original functions");
    if (!decision(code, count, profile->view_entry, profile->view_decision, error) ||
        !qa_qvm_qualify_global_word(image, profile->warning_state, error)) return false;
    int32_t frame = code[profile->held_entry].operand;
    if (frame < 8 || (frame & 3) || profile->gun < 8 || (profile->gun & 3) ||
        (uint64_t)profile->gun + 140 > (uint32_t)frame ||
        profile->parent_argument >= 62 || profile->state_argument >= 62 || profile->entity_argument >= 62 ||
        (profile->entity_number_offset & 3) ||
        (uint64_t)profile->entity_number_offset + 4 > qa_qvm_image_memory_size(image))
        return application_fail(error, QA_ERROR_FORMAT, "Equipment held records exceed their original source frame");
    for (size_t i = 0; i < profile->status_count; ++i) {
        const application_q3_equipment_status *status = profile->status + i;
        if (!entry(code, count, status->entry, error)) return false;
        for (size_t j = 0; j < sizeof(fixed) / sizeof(fixed[0]); ++j)
            if (status->entry == fixed[j])
                return application_fail(error, QA_ERROR_FORMAT, "Equipment status overlaps another source function");
        for (size_t j = 0; j < i; ++j)
            if (status->entry == profile->status[j].entry)
                return application_fail(error, QA_ERROR_FORMAT, "Equipment status repeats its source function");
        if (!profile->status_regions) continue;
        if (!decision(code, count, status->entry, status->decision, error)) return false;
        for (size_t j = 0; j < status->ammo_count; ++j)
            if (!qa_qvm_qualify_source_region(image, status->entry,
                status->ammo[j].entry, status->ammo[j].join, error)) return false;
    }
    return true;
}

static bool status_rows(const qa_json_document *doc, qa_json_id object,
    application_q3_equipment_profile *profile, qa_error *error)
{
    qa_json_id kind = qa_json_get(doc, object, "kind");
    if (qa_json_string_equal(doc, kind, "regions")) profile->status_regions = true;
    else if (!qa_json_string_equal(doc, kind, "functions"))
        return application_fail(error, QA_ERROR_FORMAT, "Equipment status requires source functions or regions");
    qa_json_id rows = qa_json_get(doc, object, "entries");
    if (qa_json_type(doc, rows) != QA_JSON_ARRAY || !(profile->status_count = qa_json_size(doc, rows)) ||
        profile->status_count > SIZE_MAX / sizeof(*profile->status))
        return application_fail(error, QA_ERROR_FORMAT, "Equipment status omits its original functions");
    profile->status = calloc(profile->status_count, sizeof(*profile->status));
    if (!profile->status)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining original equipment status boundaries");
    for (size_t i = 0; i < profile->status_count; ++i) {
        qa_json_id row = qa_json_at(doc, rows, i);
        application_q3_equipment_status *status = profile->status + i;
        if (!profile->status_regions) {
            if (!number(doc, row, &status->entry, error)) return false;
            continue;
        }
        if (!field(doc, row, "entry", &status->entry, error) ||
            !field(doc, row, "decision", &status->decision, error) ||
            !qa_json_bool(doc, qa_json_get(doc, row, "taken"), &status->taken, error)) return false;
        qa_json_id ammo = qa_json_get(doc, row, "ammo");
        if (qa_json_type(doc, ammo) != QA_JSON_ARRAY)
            return application_fail(error, QA_ERROR_FORMAT, "Equipment status ammo omits its source regions");
        status->ammo_count = qa_json_size(doc, ammo);
        if (status->ammo_count > SIZE_MAX / sizeof(*status->ammo))
            return application_fail(error, QA_ERROR_MEMORY, "Equipment ammo region inventory exceeds address space");
        if (status->ammo_count) status->ammo = calloc(status->ammo_count, sizeof(*status->ammo));
        if (status->ammo_count && !status->ammo)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining original equipment ammo regions");
        for (size_t j = 0; j < status->ammo_count; ++j) {
            qa_json_id region = qa_json_at(doc, ammo, j);
            if (!field(doc, region, "entry", &status->ammo[j].entry, error) ||
                !field(doc, region, "join", &status->ammo[j].join, error)) return false;
        }
    }
    return true;
}

static bool declared(qa_bytes bytes, application_q3_equipment_profile *profile, qa_error *error)
{
    qa_json_document *doc = NULL;
    if (!qa_json_parse(bytes, &doc, error)) return false;
    qa_json_id root = qa_json_root(doc), view = qa_json_get(doc, root, "view"),
        warning = qa_json_get(doc, root, "warning"), states = qa_json_get(doc, warning, "states"),
        held = qa_json_get(doc, root, "held");
    bool ok = field(doc, root, "hud", &profile->hud, error) &&
        field(doc, view, "entry", &profile->view_entry, error) &&
        field(doc, view, "decision", &profile->view_decision, error) &&
        qa_json_bool(doc, qa_json_get(doc, view, "taken"), &profile->view_taken, error) &&
        field(doc, warning, "entry", &profile->warning_entry, error) &&
        field(doc, warning, "state", &profile->warning_state, error) &&
        signed_field(doc, states, "none", &profile->warning_none, error) &&
        signed_field(doc, states, "low", &profile->warning_low, error) &&
        signed_field(doc, states, "empty", &profile->warning_empty, error) &&
        field(doc, held, "entry", &profile->held_entry, error) &&
        field(doc, held, "gun", &profile->gun, error) &&
        field(doc, held, "parentArgument", &profile->parent_argument, error) &&
        field(doc, held, "stateArgument", &profile->state_argument, error) &&
        field(doc, held, "entityArgument", &profile->entity_argument, error) &&
        field(doc, held, "entityNumberOffset", &profile->entity_number_offset, error) &&
        status_rows(doc, qa_json_get(doc, root, "status"), profile, error);
    qa_json_destroy(doc);
    return ok;
}

static bool stock(const qa_qvm_image *image, application_q3_equipment_profile *profile, qa_error *error)
{
    char digest[65];
    qa_sha256_hex(qa_qvm_image_digest(image), digest);
    bool first = !strcmp(digest, "a4744482c9b93852cc71f4d7ce03b3e4337e5d89844d27d272c2c16d74df07fa");
    bool second = !strcmp(digest, "14858804fb98609ed8b3b3c3b825f0a7cb544063f7e43735c884cd5e4a51157c");
    if (!first && !second) return true;
    profile->present = true; profile->view_taken = true; profile->warning_low = 1; profile->warning_empty = 2;
    profile->parent_argument = 0; profile->state_argument = 1; profile->entity_argument = 2;
    profile->hud = first ? 108261 : 106815;
    profile->view_entry = first ? 107738 : 105885;
    profile->view_decision = first ? 107828 : 105975;
    profile->warning_entry = first ? 22881 : 26123;
    profile->warning_state = first ? 1084512 : 1012656;
    profile->held_entry = first ? 106742 : 103468;
    profile->gun = first ? 28 : 184;
    profile->status_regions = second; profile->status_count = 2;
    profile->status = calloc(profile->status_count, sizeof(*profile->status));
    if (!profile->status) return application_fail(error, QA_ERROR_MEMORY, "Retaining stock equipment boundaries");
    if (first) {
        profile->status[0].entry = 24534; profile->status[1].entry = 24736;
        return true;
    }
    static const application_q3_equipment_region regions[2][2] = {
        {{10141, 10228}, {10402, 10525}}, {{14011, 14098}, {14833, 14960}}
    };
    for (size_t i = 0; i < 2; ++i) {
        application_q3_equipment_status *status = profile->status + i;
        status->entry = i ? 13793 : 10083; status->decision = i ? 13797 : 10087; status->taken = true;
        status->ammo_count = 2; status->ammo = malloc(sizeof(regions[i]));
        if (!status->ammo) return application_fail(error, QA_ERROR_MEMORY, "Retaining stock equipment ammo regions");
        memcpy(status->ammo, regions[i], sizeof(regions[i]));
    }
    return true;
}

bool application_q3_equipment_profile_read(const qa_qvm_image *image, qa_qvm_role role,
    qa_qvm_abi abi, qa_bytes declaration, application_q3_equipment_profile *out, qa_error *error)
{
    if (!image || role != QA_QVM_CGAME || (unsigned)abi > QA_QVM_Q3_116N || !out ||
        (declaration.size && !declaration.data))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment presentation requires its admitted cgame artifact");
    application_q3_equipment_profile profile = {0};
    bool ok;
    if (declaration.size) {
        profile.present = true;
        ok = declared(declaration, &profile, error);
    } else ok = stock(image, &profile, error);
    if (ok && profile.present) ok = qualify(image, &profile, error);
    if (!ok) { application_q3_equipment_profile_free(&profile); return false; }
    *out = profile;
    return true;
}
