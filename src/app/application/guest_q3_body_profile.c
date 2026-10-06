#include "guest_q3_reference.h"
#include "guest_q3_body_profile.h"
#include "internal.h"
#include "qa/json.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BODY_SAFE_INTEGER INT64_C(9007199254740991)

static bool integer(const qa_json_document *doc, qa_json_id value,
    int64_t minimum, int64_t *out, qa_error *error)
{
    double number;
    if (!qa_json_number(doc, value, &number, error)) return false;
    if (!isfinite(number) || number < (double)minimum || number > (double)BODY_SAFE_INTEGER ||
        floor(number) != number)
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body field is not a safe source integer");
    *out = (int64_t)number; return true;
}
static bool index(const qa_json_document *doc, qa_json_id value, uint32_t *out, qa_error *error)
{
    int64_t number;
    if (!integer(doc, value, 0, &number, error)) return false;
    if (number > UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body index exceeds its instruction or argument domain");
    *out = (uint32_t)number; return true;
}
static bool field(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{ return index(doc, qa_json_get(doc, object, name), out, error); }

void application_q3_body_profile_free(application_q3_body_profile *profile)
{
    if (!profile) return;
    for (size_t i = 0; profile->submissions && i < profile->count; ++i)
        free(profile->submissions[i].calls);
    free(profile->submissions); free(profile->artifact_path);
    *profile = (application_q3_body_profile){0};
}
static bool function(const qa_qvm_instruction *code, size_t count, uint32_t entry)
{ return entry < count && code[entry].opcode == QA_QVM_ENTER; }
static size_t function_end(const qa_qvm_instruction *code, size_t count, uint32_t entry)
{
    size_t end = (size_t)entry + 1;
    while (end < count && code[end].opcode != QA_QVM_ENTER) ++end;
    return end;
}
static bool direct_call(const qa_qvm_instruction *code, size_t end,
    const application_q3_body_submission *row, uint32_t call)
{
    return call > row->entry && call < end && code[call].opcode == QA_QVM_CALL &&
        code[call - 1].opcode == QA_QVM_CONST && code[call - 1].operand >= 0 &&
        (uint32_t)code[call - 1].operand == row->mesh_entry;
}
static bool calls_from_image(const qa_qvm_image *image, application_q3_body_submission *row,
    qa_error *error)
{
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    if (!function(code, count, row->entry) || !function(code, count, row->mesh_entry))
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body mesh requires original function entries");
    size_t end = function_end(code, count, row->entry), length = 0;
    for (size_t i = (size_t)row->entry + 1; i < end; ++i)
        if (direct_call(code, end, row, (uint32_t)i)) ++length;
    if (!length || length > SIZE_MAX / sizeof(*row->calls))
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body has no original player-to-mesh calls");
    row->calls = calloc(length, sizeof(*row->calls));
    if (!row->calls) return application_fail(error, QA_ERROR_MEMORY, "Retaining original body mesh calls");
    for (size_t i = (size_t)row->entry + 1; i < end; ++i)
        if (direct_call(code, end, row, (uint32_t)i))
            row->calls[row->call_count++] = (application_q3_body_call){(uint32_t)i, QA_APPLICATION_Q3_BODY};
    return true;
}

bool application_q3_body_profile_qualify(const qa_qvm_image *image, qa_qvm_abi abi,
    const char *artifact_path, const application_q3_body_profile *profile, qa_error *error)
{
    if (!image || !artifact_path || !profile || !profile->artifact_path ||
        (unsigned)abi > QA_QVM_Q3_116N || profile->abi != abi ||
        profile->image != image)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body profile lost its actual bytecode artifact");
    char *normalized = qa_vfs_normalize_path(artifact_path, error);
    bool matches = normalized && !strcmp(normalized, profile->artifact_path);
    free(normalized);
    if (!matches) return application_fail(error, QA_ERROR_FORMAT, "CGAME body profile has a different artifact path");
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    if ((!profile->present && profile->count) || profile->count > count ||
        (profile->count != 0) != (profile->submissions != NULL))
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body declaration inventory is incomplete");
    for (size_t i = 0; i < profile->count; ++i) {
        const application_q3_body_submission *row = profile->submissions + i;
        if (!function(code, count, row->entry) || code[row->entry].operand < 8 ||
            row->actor_argument >= 62 || row->entity_number_offset > BODY_SAFE_INTEGER ||
            (row->entity_number_offset & 3) ||
            (row->argument_reference ? row->reference_argument >= 62 : row->reference_argument != 0) ||
            (row->conditional ? row->condition_argument >= 62 ||
                row->condition_value < -BODY_SAFE_INTEGER || row->condition_value > BODY_SAFE_INTEGER :
                row->condition_argument != 0 || row->condition_value != 0))
            return application_fail(error, QA_ERROR_FORMAT, "CGAME body storage leaves its original source ABI");
        for (size_t j = 0; j < i; ++j)
            if (row->entry == profile->submissions[j].entry)
                return application_fail(error, QA_ERROR_FORMAT, "CGAME body repeats an original function");
        if (!row->mesh) {
            if (row->mesh_entry || row->mesh_entity_argument || row->mesh_state_argument ||
                row->mesh_shader_offset || row->call_count || row->calls)
                return application_fail(error, QA_ERROR_FORMAT, "CGAME body without meshes retains mesh fields");
            continue;
        }
        if (!function(code, count, row->mesh_entry) || row->mesh_entity_argument >= 62 ||
            row->mesh_state_argument >= 62 || row->mesh_shader_offset != 112 ||
            !row->call_count || !row->calls || row->call_count > count)
            return application_fail(error, QA_ERROR_FORMAT, "CGAME body mesh differs from its original refEntity ABI");
        size_t end = function_end(code, count, row->entry);
        for (size_t j = 0; j < row->call_count; ++j) {
            if ((unsigned)row->calls[j].part > QA_APPLICATION_Q3_BODY_HEAD ||
                !direct_call(code, end, row, row->calls[j].instruction))
                return application_fail(error, QA_ERROR_FORMAT, "CGAME body part is not its original direct mesh call");
            for (size_t k = 0; k < j; ++k)
                if (row->calls[j].instruction == row->calls[k].instruction)
                    return application_fail(error, QA_ERROR_FORMAT, "CGAME body repeats a mesh call");
        }
    }
    return true;
}

static bool parts(const qa_json_document *doc, qa_json_id value,
    application_q3_body_submission *row, qa_error *error)
{
    if (qa_json_type(doc, value) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body mesh parts require a list");
    row->call_count = qa_json_size(doc, value);
    if (row->call_count > SIZE_MAX / sizeof(*row->calls))
        return application_fail(error, QA_ERROR_MEMORY, "CGAME body mesh call inventory exceeds address space");
    row->calls = row->call_count ? calloc(row->call_count, sizeof(*row->calls)) : NULL;
    if (row->call_count && !row->calls)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining declared body mesh calls");
    static const char *const names[] = {"body", "lower", "upper", "head"};
    for (size_t i = 0; i < row->call_count; ++i) {
        qa_json_id item = qa_json_at(doc, value, i), part = qa_json_get(doc, item, "part");
        if (!field(doc, item, "call", &row->calls[i].instruction, error)) return false;
        unsigned p = 0;
        while (p < 4 && !qa_json_string_equal(doc, part, names[p])) ++p;
        if (p == 4) return application_fail(error, QA_ERROR_FORMAT, "Unknown CGAME body anatomical part");
        row->calls[i].part = (qa_application_q3_body_part)p;
    }
    return true;
}
static bool declaration(const qa_qvm_image *image, qa_bytes bytes,
    application_q3_body_profile *profile, qa_error *error)
{
    qa_json_document *doc = NULL;
    if (!qa_json_parse(bytes, &doc, error)) return false;
    qa_json_id root = qa_json_root(doc);
    int64_t version = 0;
    qa_buffer path = {0}; char *normalized = NULL;
    bool okay = integer(doc, qa_json_get(doc, root, "version"), -BODY_SAFE_INTEGER, &version, error) &&
        version == 1 && qa_json_string(doc, qa_json_get(doc, root, "artifactPath"), &path, error);
    if (okay && memchr(path.data, 0, path.size)) okay = false;
    if (okay) normalized = qa_vfs_normalize_path((const char *)path.data, error);
    if (okay) okay = normalized && !strcmp(normalized, profile->artifact_path);
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Body presentation belongs to different cgame bytes");
    free(normalized); qa_buffer_free(&path);
    qa_json_id rows = qa_json_get(doc, root, "bodySubmissions");
    if (okay && qa_json_type(doc, rows) != QA_JSON_ARRAY)
        okay = application_fail(error, QA_ERROR_FORMAT, "CGAME body submissions require a list");
    if (okay) {
        profile->count = qa_json_size(doc, rows);
        if (profile->count > SIZE_MAX / sizeof(*profile->submissions))
            okay = application_fail(error, QA_ERROR_MEMORY, "CGAME body declaration exceeds address space");
        else profile->submissions = profile->count ? calloc(profile->count, sizeof(*profile->submissions)) : NULL;
        if (okay && profile->count && !profile->submissions)
            okay = application_fail(error, QA_ERROR_MEMORY, "Retaining original body declarations");
    }
    for (size_t i = 0; okay && i < profile->count; ++i) {
        application_q3_body_submission *row = profile->submissions + i;
        qa_json_id item = qa_json_at(doc, rows, i), reference = qa_json_get(doc, item, "reference"),
            kind = qa_json_get(doc, reference, "kind"), when = qa_json_get(doc, item, "when"),
            mesh = qa_json_get(doc, item, "mesh");
        int64_t offset;
        okay = field(doc, item, "entry", &row->entry, error) &&
            field(doc, item, "actorArgument", &row->actor_argument, error) &&
            integer(doc, qa_json_get(doc, item, "entityNumberOffset"), 0, &offset, error);
        if (!okay) break;
        row->entity_number_offset = (uint64_t)offset;
        row->argument_reference = qa_json_string_equal(doc, kind, "argument");
        if (row->argument_reference) okay = field(doc, reference, "index", &row->reference_argument, error);
        else if (!qa_json_string_equal(doc, kind, "locals"))
            okay = application_fail(error, QA_ERROR_FORMAT, "CGAME body reference requires locals or an argument");
        row->conditional = when != QA_JSON_NONE;
        if (okay && row->conditional) okay = field(doc, when, "argument", &row->condition_argument, error) &&
            integer(doc, qa_json_get(doc, when, "equals"), -BODY_SAFE_INTEGER, &row->condition_value, error);
        row->mesh = mesh != QA_JSON_NONE;
        if (okay && row->mesh) {
            okay = field(doc, mesh, "entry", &row->mesh_entry, error) &&
                field(doc, mesh, "entityArgument", &row->mesh_entity_argument, error) &&
                field(doc, mesh, "stateArgument", &row->mesh_state_argument, error) &&
                field(doc, mesh, "shaderOffset", &row->mesh_shader_offset, error);
            qa_json_id declared_parts = qa_json_get(doc, mesh, "parts");
            if (okay) okay = declared_parts == QA_JSON_NONE ? calls_from_image(image, row, error) :
                parts(doc, declared_parts, row, error);
        }
    }
    qa_json_destroy(doc); return okay;
}

static bool stock(application_q3_body_profile *profile, qa_error *error)
{

    bool first = application_q3_reference_image(profile->image, Q3_REFERENCE_LRCTF_CGAME),
        second = application_q3_reference_image(profile->image, Q3_REFERENCE_THREEWAVE_CGAME);
    if (!first && !second) return true;
    static const uint32_t a[] = {45608,81284,46022,47069,47502,47708,47822,48149,47699,45882,45800};
    static const uint32_t b[] = {36612,62937,36804,38561,39712,39826,39870,39534,40520};
    const uint32_t *entries = first ? a : b;
    profile->present = true; profile->count = first ? sizeof(a) / sizeof(*a) : sizeof(b) / sizeof(*b);
    profile->submissions = calloc(profile->count, sizeof(*profile->submissions));
    if (!profile->submissions) return application_fail(error, QA_ERROR_MEMORY, "Retaining stock body boundaries");
    for (size_t i = 0; i < profile->count; ++i) {
        application_q3_body_submission *row = profile->submissions + i;
        row->entry = entries[i];
        if (row->entry != (first ? 81284u : 62937u)) continue;
        row->mesh = true; row->mesh_entry = first ? 80824 : 61925;
        row->mesh_state_argument = 1; row->mesh_shader_offset = 112;
        row->call_count = 3; row->calls = calloc(3, sizeof(*row->calls));
        if (!row->calls) return application_fail(error, QA_ERROR_MEMORY, "Retaining stock body mesh calls");
        row->calls[0] = (application_q3_body_call){first ? 82085u : 63314u, QA_APPLICATION_Q3_BODY_LOWER};
        row->calls[1] = (application_q3_body_call){first ? 82484u : 63546u, QA_APPLICATION_Q3_BODY_UPPER};
        row->calls[2] = (application_q3_body_call){first ? 82781u : 63772u, QA_APPLICATION_Q3_BODY_HEAD};
    }
    return true;
}

bool application_q3_body_profile_read(const qa_qvm_image *image, qa_qvm_role role,
    qa_qvm_abi abi, const char *artifact_path, const qa_bytes *bytes,
    application_q3_body_profile *out, qa_error *error)
{
    if (!image || role != QA_QVM_CGAME || (unsigned)abi > QA_QVM_Q3_116N ||
        !artifact_path || !out || out->artifact_path || out->submissions || out->count ||
        (bytes && bytes->size && !bytes->data))
        return application_fail(error, QA_ERROR_ARGUMENT, "Body presentation requires its admitted CGAME bytecode");
    application_q3_body_profile profile = {.image = image, .abi = abi};
    profile.artifact_path = qa_vfs_normalize_path(artifact_path, error);
    if (!profile.artifact_path) return false;
    profile.present = bytes != NULL;
    bool okay = bytes ? declaration(image, *bytes, &profile, error) : stock(&profile, error);
    if (okay) okay = application_q3_body_profile_qualify(image, abi, artifact_path, &profile, error);
    if (!okay) { application_q3_body_profile_free(&profile); return false; }
    *out = profile; return true;
}
