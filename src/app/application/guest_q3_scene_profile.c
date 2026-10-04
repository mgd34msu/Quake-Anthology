#include "guest_q3_scene_profile.h"
#include "qa/json.h"
#include "qa/vfs.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e, qa_status code, const char *message)
{ qa_error_set(e, code, 0, "%s", message); return false; }
static bool word(const qa_json_document *d, qa_json_id id, uint32_t *out, qa_error *e)
{
    uint64_t n;
    if (!qa_json_u64(d, id, &n, e)) return false;
    if (n > UINT32_MAX) return fail(e, QA_ERROR_FORMAT, "Component scene address exceeds its source word");
    *out = (uint32_t)n; return true;
}
static bool field(const qa_json_document *d, qa_json_id id, const char *name, uint32_t *v, qa_error *e)
{ return word(d, qa_json_get(d, id, name), v, e); }
static bool text(const qa_json_document *d, qa_json_id id, char **out, qa_error *e)
{
    qa_buffer b = {0};
    if (!qa_json_string(d, id, &b, e)) return false;
    if (memchr(b.data, 0, b.size)) { qa_buffer_free(&b); return fail(e, QA_ERROR_FORMAT, "Component scene text contains NUL"); }
    *out = (char *)b.data; return true;
}
static bool entry(const qa_qvm_image *image, uint32_t at, qa_error *e)
{
    size_t n; const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &n);
    return (at < n && code[at].opcode == QA_QVM_ENTER && code[at].operand >= 8) ||
        fail(e, QA_ERROR_FORMAT, "Component scene call is not an original function entry");
}
static bool allocate(const qa_json_document *d, qa_json_id id, size_t size,
    void **out, size_t *count, bool optional, qa_error *e)
{
    if (optional && id == QA_JSON_NONE) return true;
    if (qa_json_type(d, id) != QA_JSON_ARRAY) return fail(e, QA_ERROR_FORMAT, "Component scene requires a declared list");
    *count = qa_json_size(d, id);
    if (*count > SIZE_MAX / size) return fail(e, QA_ERROR_MEMORY, "Component scene list overflows");
    *out = *count ? calloc(*count, size) : NULL;
    return !*count || *out || fail(e, QA_ERROR_MEMORY, "Retaining component scene declaration");
}
static bool addresses(const qa_json_document *d, qa_json_id id, application_q3_scene_profile *p,
    q3scene_addresses *out, size_t extent, bool optional, qa_error *e)
{
    if (!allocate(d, id, sizeof(*out->rows), (void **)&out->rows, &out->count, optional, e)) return false;
    for (size_t i = 0; i < out->count; ++i)
        if (!word(d, qa_json_at(d, id, i), out->rows + i, e) ||
            !qa_qvm_qualify_source_span(p->image, out->rows[i], extent, e)) return false;
    return true;
}
static bool program(const qa_json_document *d, qa_json_id id, const char *actual_path,
    const qa_qvm_image *image, qa_qvm_abi abi, char **path, qa_sha256_digest *digest, qa_error *e)
{
    char *raw = NULL, *hex = NULL, *actual = NULL;
    bool ok = text(d, qa_json_get(d, id, "path"), &raw, e) &&
        text(d, qa_json_get(d, id, "digest"), &hex, e);
    if (ok) { *path = qa_vfs_normalize_path(raw, e); actual = qa_vfs_normalize_path(actual_path, e); }
    if (ok) ok = *path && actual && !strcmp(*path, actual) && qa_sha256_parse(hex, digest, e) &&
        qa_sha256_equal(digest, qa_qvm_image_digest(image)) &&
        qa_json_string_equal(d, qa_json_get(d, id, "abiProfile"), abi == QA_QVM_Q3_MODERN ? "q3-modern" : "q3-1.16n-base");
    free(raw); free(hex); free(actual);
    return ok || fail(e, QA_ERROR_FORMAT, "Component scene differs from its held executable identity");
}
static bool call_row(const qa_json_document *d, qa_json_id row, application_q3_scene_profile *p,
    q3scene_call *c, qa_error *e)
{
    qa_json_id args = qa_json_get(d, row, "arguments"), when = qa_json_get(d, row, "when");
    if (!field(d, row, "entry", &c->entry, e) || !entry(p->image, c->entry, e) ||
        !allocate(d, args, sizeof(*c->arguments), (void **)&c->arguments, &c->count, false, e)) return false;
    if (c->count > 62 || (when != QA_JSON_NONE && !qa_json_string_equal(d, when, "weapon-presented")))
        return fail(e, QA_ERROR_FORMAT, "Component scene caller arguments or condition are invalid");
    c->weapon_presented = when != QA_JSON_NONE;
    for (size_t j = 0; j < c->count; ++j) {
        q3scene_argument *a = c->arguments + j; qa_json_id arg = qa_json_at(d, args, j);
        qa_json_id kind = qa_json_get(d, arg, "kind"), value = qa_json_get(d, arg, "value");
        if (qa_json_string_equal(d, kind, "source")) {
            if (qa_json_string_equal(d, value, "client-number")) a->kind = Q3SCENE_CLIENT;
            else if (qa_json_string_equal(d, value, "time")) a->kind = Q3SCENE_TIME;
            else if (qa_json_string_equal(d, value, "snapshot-number")) a->kind = Q3SCENE_SNAPSHOT;
            else if (qa_json_string_equal(d, value, "server-command-sequence")) a->kind = Q3SCENE_COMMAND_SEQUENCE;
            else if(p->player_events&&qa_json_string_equal(d,value,"player-state")) a->kind=Q3SCENE_PLAYER_STATE;
            else if(p->player_events&&qa_json_string_equal(d,value,"snapshot")) a->kind=Q3SCENE_SNAPSHOT_ADDRESS;
            else if(p->player_events&&qa_json_string_equal(d,value,"entity-state")) a->kind=Q3SCENE_ENTITY_STATE;
            else if(p->player_events&&qa_json_string_equal(d,value,"centity")) a->kind=Q3SCENE_CENTITY;
            else if(p->player_events&&qa_json_string_equal(d,value,"origin")) a->kind=Q3SCENE_ORIGIN;
            else if(p->player_events&&qa_json_string_equal(d,value,"event")) a->kind=Q3SCENE_EVENT;
            else if(p->player_events&&qa_json_string_equal(d,value,"parameter")) a->kind=Q3SCENE_PARAMETER;
            else return fail(e, QA_ERROR_FORMAT, "Component call requires a declared source argument");
        } else if (qa_json_string_equal(d, kind, "address")) {
            uint32_t n;
            if (!word(d, value, &n, e) || !qa_qvm_qualify_source_span(p->image, n, 1, e)) return false;
            memcpy(&a->word, &n, 4);
        } else if (qa_json_string_equal(d, kind, "int32")) {
            int64_t n;
            if (!qa_json_i64(d, value, &n, e) || n < INT32_MIN || n > INT32_MAX)
                return fail(e, QA_ERROR_FORMAT, "Component scene literal exceeds int32");
            a->word = (int32_t)n;
        } else if (qa_json_string_equal(d, kind, "float32")) {
            double n;
            if (!qa_json_number(d, value, &n, e)) return false;
            float f = (float)n;
            if (!isfinite(f)) return fail(e, QA_ERROR_FORMAT, "Component scene literal exceeds float32");
            memcpy(&a->word, &f, 4);
        } else return fail(e, QA_ERROR_FORMAT, "Unknown component scene argument kind");
    }
    return true;
}
static bool calls(const qa_json_document *d, qa_json_id id, application_q3_scene_profile *p,
    q3scene_calls *out, qa_error *e)
{
    if (!allocate(d, id, sizeof(*out->rows), (void **)&out->rows, &out->count, false, e)) return false;
    for (size_t i = 0; i < out->count; ++i)
        if (!call_row(d, qa_json_at(d, id, i), p, out->rows + i, e)) return false;
    return true;
}
static void calls_free(q3scene_calls *rows)
{ for (size_t i = 0; rows->rows && i < rows->count; ++i) free(rows->rows[i].arguments); free(rows->rows); }
static bool independent(const q3scene_calls *rows,qa_error *e)
{
    for(size_t i=0;i<rows->count;++i) for(size_t j=0;j<rows->rows[i].count;++j)
        if(rows->rows[i].arguments[j].kind>=Q3SCENE_ENTITY_STATE)
            return fail(e,QA_ERROR_FORMAT,"Original initialization and frame callers require event-independent source arguments");
    return true;
}
void application_q3_scene_profile_destroy(application_q3_scene_profile *p)
{
    if (!p) return;
    calls_free(&p->project); calls_free(&p->event); free(p->snapshot_pointers.rows); calls_free(&p->initialize); calls_free(&p->refresh); calls_free(&p->snapshots); calls_free(&p->frame); calls_free(&p->hud);
    free(p->time.rows); free(p->frame_time.rows); free(p->origin.rows); free(p->angles.rows); free(p->axis.rows);
    for (size_t i = 0; p->cvars && i < p->cvar_count; ++i) { free(p->cvars[i].name); free(p->cvars[i].value); }
    free(p->cvars); free(p->gameplay_path); free(p->cgame_path);
    application_q3_body_profile_free(&p->body); qa_qvm_image_release(p->image); free(p);
}
static bool body(const qa_json_document *d, qa_json_id id, application_q3_scene_profile *p, qa_error *e)
{
    p->body = (application_q3_body_profile){.artifact = p->cgame_digest, .abi = p->abi, .present = true, .count = 1};
    p->body.artifact_path = qa_vfs_normalize_path(p->cgame_path, e);
    p->body.submissions = calloc(1, sizeof(*p->body.submissions));
    if (!p->body.artifact_path || !p->body.submissions) return fail(e, QA_ERROR_MEMORY, "Retaining component body declaration");
    application_q3_body_submission *r = p->body.submissions;
    qa_json_id player = qa_json_get(d, id, "player"), mesh = qa_json_get(d, id, "mesh"), parts = qa_json_get(d, mesh, "parts");
    r->mesh = true; r->entity_number_offset = p->state;
    if (!field(d, player, "entry", &r->entry, e) || !field(d, player, "centityArgument", &r->actor_argument, e) ||
        !field(d, mesh, "entry", &r->mesh_entry, e) || !field(d, mesh, "entityArgument", &r->mesh_entity_argument, e) ||
        !field(d, mesh, "stateArgument", &r->mesh_state_argument, e) || !field(d, mesh, "shaderOffset", &r->mesh_shader_offset, e)) return false;
    if (parts != QA_JSON_NONE) {
        if (!allocate(d, parts, sizeof(*r->calls), (void **)&r->calls, &r->call_count, false, e)) return false;
        static const char *const names[] = {"body", "lower", "upper", "head"};
        for (size_t i = 0; i < r->call_count; ++i) {
            qa_json_id part = qa_json_at(d, parts, i); unsigned n = 0;
            if (!field(d, part, "call", &r->calls[i].instruction, e)) return false;
            while (n < 4 && !qa_json_string_equal(d, qa_json_get(d, part, "part"), names[n])) ++n;
            if (n == 4) return fail(e, QA_ERROR_FORMAT, "Unknown component body part");
            r->calls[i].part = (qa_application_q3_body_part)n;
        }
    } else {
        size_t count; const qa_qvm_instruction *code = qa_qvm_image_instructions(p->image, &count);
        if (!entry(p->image, r->entry, e)) return false;
        for (size_t i = (size_t)r->entry + 1; i < count && code[i].opcode != QA_QVM_ENTER; ++i) {
            if (code[i].opcode != QA_QVM_CALL || code[i - 1].opcode != QA_QVM_CONST ||
                code[i - 1].operand < 0 || (uint32_t)code[i - 1].operand != r->mesh_entry) continue;
            application_q3_body_call *v = realloc(r->calls, (r->call_count + 1) * sizeof(*v));
            if (!v) return fail(e, QA_ERROR_MEMORY, "Retaining original component mesh calls");
            r->calls = v; r->calls[r->call_count++] = (application_q3_body_call){(uint32_t)i, QA_APPLICATION_Q3_BODY};
        }
    }
    return application_q3_body_profile_qualify(p->image, p->abi, p->cgame_path, &p->body, e);
}
bool application_q3_scene_profile_create(qa_qvm_image *image, qa_qvm_abi abi,
    const char *path, const qa_qvm_image *gameplay, const char *gameplay_path,
    qa_bytes bytes, application_q3_scene_profile **out, qa_error *e)
{
    if (!image || !gameplay || !path || !gameplay_path || !out || *out || (unsigned)abi > QA_QVM_Q3_116N)
        return fail(e, QA_ERROR_ARGUMENT, "Component scene requires both held executables");
    qa_json_document *d = NULL;
    if (!qa_json_parse(bytes, &d, e)) return false;
    application_q3_scene_profile *p = calloc(1, sizeof(*p));
    if (!p) { qa_json_destroy(d); return fail(e, QA_ERROR_MEMORY, "Owning component scene profile"); }
    p->image = image; qa_qvm_image_retain(image); p->abi = abi; qa_sha256(bytes, &p->declaration_digest);
    qa_json_id root = qa_json_root(d), storage = qa_json_get(d, root, "storage"), ents = qa_json_get(d, storage, "centities");
    p->player_events=qa_json_string_equal(d,qa_json_get(d,root,"runtime"),"qvm-player-events");
    uint64_t version;
    bool ok = qa_json_u64(d, qa_json_get(d, root, "version"), &version, e) && version == 1 &&
        (p->player_events||qa_json_string_equal(d, qa_json_get(d, root, "runtime"), "qvm-scene")) &&
        program(d, qa_json_get(d, root, "gameplay"), gameplay_path, gameplay, abi, &p->gameplay_path, &p->gameplay_digest, e) &&
        program(d, qa_json_get(d, root, "cgame"), path, image, abi, &p->cgame_path, &p->cgame_digest, e) &&
        field(d, storage, "gameState", &p->game_state, e) && qa_qvm_qualify_source_span(image, p->game_state, 20100, e) &&
        (p->player_events||(field(d, storage, "serverCommandSequence", &p->command_sequence, e) && qa_qvm_qualify_source_span(image, p->command_sequence, 4, e))) &&
        field(d, ents, "address", &p->entities, e) && field(d, ents, "stride", &p->stride, e) &&
        field(d, ents, "capacity", &p->capacity, e) && field(d, ents, "state", &p->state, e) &&
        (p->player_events?field(d,ents,"origin",&p->entity_origin,e):
            (field(d, ents, "previousEvent", &p->previous_event, e) && field(d, ents, "snapshotTime", &p->snapshot_time, e)));
    if (ok) ok = p->stride && p->capacity && p->stride >= qa_qvm_entity_bytes(abi) &&
        p->state <= p->stride - qa_qvm_entity_bytes(abi) && p->stride >= 4 &&
        p->previous_event <= p->stride - 4 && p->snapshot_time <= p->stride - 4 &&
        p->capacity <= SIZE_MAX / p->stride && qa_qvm_qualify_source_span(image, p->entities, (size_t)p->stride * p->capacity, e);
    if (ok) ok = addresses(d, qa_json_get(d, storage, "time"), p, &p->time, 4, false, e) &&
        addresses(d, qa_json_get(d, storage, "frameTime"), p, &p->frame_time, 4, false, e) &&
        addresses(d, qa_json_get(d, storage, "viewOrigin"), p, &p->origin, 12, false, e) &&
        addresses(d, qa_json_get(d, storage, "viewAngles"), p, &p->angles, 12, true, e) &&
        addresses(d, qa_json_get(d, storage, "viewAxis"), p, &p->axis, 36, true, e);
    qa_json_id event = qa_json_get(d, root, "eventCheck"), vars = qa_json_get(d, root, "cvars"), hud = qa_json_get(d, root, "hud");
    if(ok&&p->player_events) {
        qa_json_id snapshot=qa_json_get(d,storage,"snapshot");
        ok=qa_json_string_equal(d,qa_json_get(d,snapshot,"kind"),"synthetic-player-event")&&
            field(d,storage,"playerState",&p->player_state,e)&&qa_qvm_qualify_source_span(image,p->player_state,qa_qvm_player_bytes(abi),e)&&
            field(d,snapshot,"address",&p->snapshot_address,e)&&qa_qvm_qualify_source_span(image,p->snapshot_address,qa_qvm_snapshot_bytes(abi),e)&&
            addresses(d,qa_json_get(d,snapshot,"pointers"),p,&p->snapshot_pointers,4,false,e)&&
            p->stride>=12&&p->entity_origin<=p->stride-12&&
            calls(d,qa_json_get(d,root,"initialize"),p,&p->initialize,e)&&calls(d,qa_json_get(d,root,"refresh"),p,&p->refresh,e)&&
            calls(d,qa_json_get(d,root,"project"),p,&p->project,e)&&calls(d,qa_json_get(d,root,"frame"),p,&p->frame,e);
        if(ok) {
            p->event.rows=calloc(1,sizeof(*p->event.rows)); p->event.count=p->event.rows?1:0;
            if(!p->event.rows) ok=fail(e,QA_ERROR_MEMORY,"Retaining original player event caller");
            else ok=call_row(d,qa_json_get(d,root,"event"),p,p->event.rows,e);
        }
        if(ok) {
            uint64_t snapshot_end=(uint64_t)p->snapshot_address+qa_qvm_snapshot_bytes(abi),player_end=(uint64_t)p->player_state+qa_qvm_player_bytes(abi);
            ok=!(p->player_state<snapshot_end&&p->snapshot_address<player_end);
        }
    }
    if (ok&&!p->player_events) ok = field(d, root, "eventEntityType", &p->event_type, e) && p->event_type <= INT32_MAX &&
        field(d, event, "entry", &p->event_entry, e) && entry(image, p->event_entry, e) &&
        field(d, event, "centityArgument", &p->event_argument, e) && p->event_argument < 62 &&
        body(d, qa_json_get(d, root, "body"), p, e) &&
        calls(d, qa_json_get(d, root, "initialize"), p, &p->initialize, e) &&
        calls(d, qa_json_get(d, root, "refresh"), p, &p->refresh, e) &&
        calls(d, qa_json_get(d, root, "snapshots"), p, &p->snapshots, e) && p->snapshots.count &&
        calls(d, qa_json_get(d, root, "frame"), p, &p->frame, e) &&
        allocate(d, vars, sizeof(*p->cvars), (void **)&p->cvars, &p->cvar_count, false, e);
    for (size_t i = 0; ok && i < p->cvar_count; ++i) {
        qa_json_id v = qa_json_at(d, vars, i);
        ok = text(d, qa_json_get(d, v, "name"), &p->cvars[i].name, e) && text(d, qa_json_get(d, v, "value"), &p->cvars[i].value, e);
    }
    p->has_hud = hud != QA_JSON_NONE;
    if (ok && p->has_hud) {
        qa_json_id mode = qa_json_get(d, hud, "mode"); p->replace_status = qa_json_string_equal(d, mode, "replace-status");
        ok = (p->replace_status || qa_json_string_equal(d, mode, "overlay")) && calls(d, qa_json_get(d, hud, "frame"), p, &p->hud, e);
    }
    if(ok) ok=independent(&p->initialize,e)&&independent(&p->refresh,e)&&independent(&p->frame,e)&&independent(&p->hud,e);
    qa_json_destroy(d);
    if (!ok) { if (!e || e->code == QA_OK) fail(e, QA_ERROR_FORMAT, "Invalid original component scene declaration"); application_q3_scene_profile_destroy(p); return false; }
    *out = p; return true;
}
