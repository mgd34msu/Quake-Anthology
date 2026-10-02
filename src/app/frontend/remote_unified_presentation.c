#include "remote_unified_private.h"
#include "remote_unified_presentation.h"
#include "remote_unified_render.h"
#include "remote_unified_events.h"
#include "remote_unified_q1.h"
#include "remote_unified_q2.h"
#include "remote_unified_q3.h"
#include "remote_unified_components.h"
#include "remote_unified_input.h"
#include "unified_q3_sources.h"
#include "remote_unified_save.h"
#include "remote_unified_presentation_save.h"
#include "remote_unified_prediction_save.h"
#include "remote_unified_input_save.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct unified_audio_identity {
    struct unified_audio_identity *next;
    qa_actor_id actor;
    uint64_t audio;
} unified_audio_identity;
typedef struct unified_q3_client_row {
    struct unified_q3_client_row *next;
    frontend_unified_q3_client *client;
    frontend_unified_q3_client_frame *frame;
    uint64_t receiver;
    bool selected, born;
} unified_q3_client_row;
typedef enum unified_child_kind {
    UNIFIED_MEDIA,UNIFIED_PENDING_MEDIA,UNIFIED_EVENTS,UNIFIED_Q1,UNIFIED_Q2,UNIFIED_Q3,
    UNIFIED_COMPONENTS,UNIFIED_Q3_SOURCES,UNIFIED_PREDICTION,UNIFIED_INPUT,UNIFIED_RENDER,
    UNIFIED_CHILD_COUNT
} unified_child_kind;
typedef struct unified_saved_q3_client {
    size_t source;
    uint64_t receiver;
    qa_buffer bytes;
} unified_saved_q3_client;
typedef struct unified_presentation_import {
    qa_buffer child[UNIFIED_CHILD_COUNT];
    unified_saved_q3_client *clients;
    size_t client_count;
    bool imported[UNIFIED_CHILD_COUNT];
    bool received, decoded, media_prepared, roots, media_roots, pending_roots, components_finished, q1_finished;
} unified_presentation_import;
typedef struct unified_presentation {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media, *candidate_media;
    frontend_unified_render *render, *candidate_render;
    frontend_remote_unified_prediction *prediction;
    frontend_unified_input *physical;
    qa_unified_document *candidate_prediction;
    frontend_unified_events *events;
    frontend_unified_q1 *q1;
    frontend_unified_q2 *q2;
    frontend_unified_q3 *q3;
    frontend_unified_components *components;
    frontend_unified_component_frame *component_frame;
    frontend_unified_q3_sources *q3_sources;
    frontend_unified_q3_source_frame *q3_source_frame;
    unified_q3_client_row *q3_clients;
    unified_presentation_import *import;
    unified_audio_identity *audio;
    qa_scene_light *draw_lights;
    uint64_t audio_owner;
    qa_unified_vec3 pending_angles;
    qa_actor_id pending_view_actor;
    bool pending_view;
    bool received, frame_prepared, busy;
    bool restore_complete;
} unified_presentation;

static bool audio_actor(void *context, qa_actor_id actor, uint64_t *out, qa_error *error)
{
    unified_presentation *p = context;
    qa_saved_actor_id reference;
    if (!p || !out || !qa_actors_save_reference(frontend_remote_unified_registry(p->replica),actor,&reference,error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified audio actor belongs to another replica");
    qa_saved_actor_id wire;
    if (!frontend_remote_unified_wire_actor(p->replica, actor, &wire))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified audio actor has no actual wire identity");
    for (unified_audio_identity *row = p->audio; row; row = row->next)
        if (qa_actor_id_equal(row->actor, actor)) { *out = row->audio; return true; }
    unified_audio_identity *row = calloc(1, sizeof(*row));
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining private unified audio identity");
    if (!frontend_source_identity_allocate(p->frontend, &row->audio, error)) { free(row); return false; }
    row->actor = actor; row->next = p->audio; p->audio = row; *out = row->audio; return true;
}
static bool family(const qa_unified_document *doc, qa_json_id row, const char *kind)
{
    const qa_json_document *json = qa_unified_document_json(doc);
    return qa_json_string_equal(json, qa_json_get(json, row, "kind"), kind);
}
static bool q1_family(const unified_presentation *p,const qa_unified_document *doc, qa_json_id row)
{
    if (family(doc,row,"q1") || family(doc,row,"q1-composition") || family(doc,row,"q1-client") ||
        family(doc,row,"q1-sky") || family(doc,row,"q1-fog") || family(doc,row,"q1-level") || family(doc,row,"q1-session")) return true;
    if (!family(doc,row,"view-reset") && !family(doc,row,"music")) return false;
    const qa_json_document *j=qa_unified_document_json(doc);
    qa_catalog *catalog=qa_executable_recipe_catalog(frontend_remote_unified_recipe(p->replica));
    for (size_t i=0;i<qa_catalog_count(catalog);++i) {
        const qa_product *product=qa_catalog_at(catalog,i);
        if (product->family==QA_GAME_Q1 && qa_json_string_equal(j,qa_json_get(j,row,"content"),product->identity)) return true;
    }
    return false;
}
static bool q3_family(const qa_unified_document *doc, qa_json_id row)
{ return family(doc,row,"q3-source") || family(doc,row,"q3-character") || family(doc,row,"q3-ballistics"); }
static bool validate(void *context, bool simulation, const qa_unified_document *doc, qa_json_id row, qa_error *error)
{
    unified_presentation *p = context;
    const qa_json_document *json=qa_unified_document_json(doc);
    qa_json_id payload=simulation?qa_json_get(json,row,"payload"):row;
    if (!simulation && family(doc,row,"presentation-owner"))
        return frontend_unified_q1_owner_validate(p->q1,doc,row,error) &&
            frontend_unified_q2_owner_validate(p->q2,doc,row,error) &&
            frontend_unified_q3_owner_validate(p->q3,doc,row,error);
    if (!simulation && q1_family(p,doc,payload)) return frontend_unified_q1_validate(p->q1,false,doc,row,error);
    if (q3_family(doc,payload)) return frontend_unified_q3_validate(p->q3,simulation,doc,row,error);
    return frontend_unified_q2_validate(p->q2,simulation,doc,row,error);
}
static bool presentation(void *context, const qa_unified_document *doc, qa_json_id row, bool *mirrored, qa_error *error)
{
    unified_presentation *p = context;
    if (family(doc,row,"presentation-owner")) {
        *mirrored=false;
        return frontend_unified_q1_owner_retire(p->q1,doc,row,error) &&
            frontend_unified_q2_owner_retire(p->q2,doc,row,error) &&
            frontend_unified_q3_owner_retire(p->q3,doc,row,error);
    }
    if (q1_family(p,doc,row)) {
        const qa_json_document *json=qa_unified_document_json(doc);
        qa_json_id event=qa_json_get(json,row,"event");
        if (qa_json_string_equal(json,qa_json_get(json,event,"kind"),"sound")) {
            bool paired=false; *mirrored=false;
            return frontend_unified_events_sound_mirrored(p->events,doc,row,&paired,error) &&
                frontend_unified_q1_sound_presentation(p->q1,doc,row,paired,error);
        }
        return frontend_unified_q1_presentation(p->q1,doc,row,mirrored,error);
    }
    if (q3_family(doc,row)) return frontend_unified_q3_presentation(p->q3,doc,row,mirrored,error);
    return frontend_unified_q2_presentation(p->q2,doc,row,mirrored,error);
}
static bool simulation(void *context, const qa_unified_document *doc, qa_json_id row, qa_error *error)
{
    unified_presentation *p = context;
    const qa_json_document *json = qa_unified_document_json(doc);
    qa_json_id payload = qa_json_get(json,row,"payload");
    if (q3_family(doc,payload)) return frontend_unified_q3_simulation(p->q3,doc,row,error);
    return frontend_unified_q2_simulation(p->q2,doc,row,error);
}
static bool idle(void *context, const frontend_remote_unified *replica)
{
    unified_presentation *p = context;
    if (p) for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!frontend_unified_q3_client_idle(row->client)) return false;
    return p && (!p->replica || p->replica == replica) && !p->busy &&
        frontend_unified_media_idle(p->media) && frontend_unified_media_idle(p->candidate_media) &&
        frontend_unified_render_idle(p->render) && frontend_unified_render_idle(p->candidate_render) &&
        (!p->prediction || frontend_remote_unified_prediction_idle(p->prediction)) &&
        (!p->physical || frontend_unified_input_idle(p->physical)) &&
        (!p->events || frontend_unified_events_idle(p->events)) &&
        (!p->q1 || frontend_unified_q1_idle(p->q1)) &&
        (!p->q2 || frontend_unified_q2_idle(p->q2)) &&
        (!p->q3 || frontend_unified_q3_idle(p->q3)) &&
        (!p->q3_sources || frontend_unified_q3_sources_idle(p->q3_sources)) &&
        (!p->components || frontend_unified_components_idle(p->components));
}
static void import_free(unified_presentation_import *saved)
{
    if (!saved) return;
    for (size_t i=0;i<UNIFIED_CHILD_COUNT;++i) qa_buffer_free(saved->child+i);
    if (saved->clients) for (size_t i=0;i<saved->client_count;++i) qa_buffer_free(&saved->clients[i].bytes);
    free(saved->clients); free(saved);
}
static void frame_abort(unified_presentation *p)
{
    unified_q3_client_row **slot=&p->q3_clients;
    while (*slot) {
        unified_q3_client_row *row=*slot;
        frontend_unified_q3_client_abort(&row->frame);
        row->selected=false;
        if (row->born && frontend_unified_q3_client_destroy(&row->client,NULL)) {
            *slot=row->next; free(row);
        } else slot=&row->next;
    }
    frontend_unified_events_frame_abort(p->events);
    frontend_unified_q1_frame_abort(p->q1);
    frontend_unified_q2_frame_abort(p->q2);
    frontend_unified_q3_frame_abort(p->q3);
    frontend_unified_components_frame_abort(&p->component_frame);
    frontend_unified_q3_sources_abort(&p->q3_source_frame);
    p->frame_prepared = false;
}
static bool close_children(unified_presentation *p, qa_error *error)
{
    frame_abort(p);
    if (!idle(p,p->replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified CLIENT children have not returned");
    while (p->q3_clients) {
        unified_q3_client_row *row=p->q3_clients;
        if (!frontend_unified_q3_client_destroy(&row->client,error)) return false;
        p->q3_clients=row->next; free(row);
    }
    if (!frontend_unified_input_destroy(&p->physical,error) ||
        !frontend_unified_q1_destroy(&p->q1,error) ||
        !frontend_unified_q2_destroy(&p->q2,error) ||
        !frontend_unified_q3_destroy(&p->q3,error) ||
        !frontend_unified_components_destroy(&p->components,error) ||
        !frontend_unified_q3_sources_destroy(&p->q3_sources,error) ||
        !frontend_unified_events_destroy(&p->events,error) ||
        !frontend_unified_render_destroy(&p->candidate_render,error) ||
        !frontend_unified_render_destroy(&p->render,error) ||
        !frontend_remote_unified_prediction_destroy(&p->prediction,error)) return false;
    qa_unified_document_destroy(p->candidate_prediction); p->candidate_prediction = NULL;
    if (!frontend_unified_media_destroy(p->media,error)) return false;
    p->media = NULL; p->received = false;
    free(p->draw_lights); p->draw_lights=NULL;
    while (p->audio) { unified_audio_identity *row = p->audio; p->audio = row->next; free(row); }
    p->audio_owner = 0; p->pending_view=false;
    import_free(p->import); p->import=NULL; p->restore_complete=false;
    return true;
}
static bool q1_action_read(unified_presentation *p,const qa_unified_document *doc,qa_json_id row,
    bool *departure,qa_saved_actor_id *actor,qa_unified_vec3 *angles,qa_error *error)
{
    const qa_json_document *j=qa_unified_document_json(doc);
    qa_json_id event=qa_json_get(j,row,"event"),kind=qa_json_get(j,event,"kind"),wire=QA_JSON_NONE,value=QA_JSON_NONE;
    *departure=false;
    if (family(doc,row,"q1-session") && qa_json_string_equal(j,kind,"back-to-lobby")) {
        *departure=true;
        return p->replica->session!=NULL || frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Q1 departure has no actual live transport owner");
    }
    if (family(doc,row,"view-reset")) { wire=qa_json_get(j,row,"actor"); value=qa_json_get(j,row,"angles"); }
    else if (family(doc,row,"q1") && (qa_json_string_equal(j,kind,"teleport-player") ||
        qa_json_string_equal(j,kind,"camera"))) {
        wire=qa_json_get(j,event,"player"); value=qa_json_get(j,event,"angles");
    } else return frontend_unified_fail(error,QA_ERROR_UNSUPPORTED,"Q1 action has no installed progress, camera or soundtrack owner");
    uint64_t slot,generation;
    if (!qa_json_u64(j,qa_json_get(j,wire,"slot"),&slot,error) || slot>UINT32_MAX ||
        !qa_json_u64(j,qa_json_get(j,wire,"generation"),&generation,error) ||
        !qa_unified_document_number(doc,qa_json_get(j,value,"x"),&angles->x,error) ||
        !qa_unified_document_number(doc,qa_json_get(j,value,"y"),&angles->y,error) ||
        !qa_unified_document_number(doc,qa_json_get(j,value,"z"),&angles->z,error)) return false;
    actor->slot=(uint32_t)slot; actor->generation=generation;
    return (isfinite(angles->x) && isfinite(angles->y) && isfinite(angles->z)) ||
        frontend_unified_fail(error,QA_ERROR_FORMAT,"Received Q1 view reset has nonfinite angles");
}
static bool q1_action_validate(void *context,const qa_unified_document *doc,qa_json_id row,qa_error *error)
{
    bool departure; qa_saved_actor_id actor; qa_unified_vec3 angles;
    return q1_action_read(context,doc,row,&departure,&actor,&angles,error);
}
static bool q1_action(void *context,const qa_unified_document *doc,qa_json_id row,qa_error *error)
{
    unified_presentation *p=context; bool departure; qa_saved_actor_id wire; qa_unified_vec3 angles;
    if (!q1_action_read(p,doc,row,&departure,&wire,&angles,error)) return false;
    if (departure) return qa_unified_session_close(p->replica->session,"Q1 Source requested back to lobby",error);
    qa_actor_id player,actor; uint32_t source_entity;
    if (!frontend_remote_unified_actor(p->replica,wire.slot,wire.generation,&actor,error)) return false;
    if (!frontend_remote_unified_player(p->replica,&player,&source_entity)) return false;
    if (qa_actor_id_equal(actor,player)) { p->pending_angles=angles; p->pending_view_actor=actor; p->pending_view=true; }
    return true;
}
static bool prepare(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe,
    bool *ready, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || !ready || p->busy || (p->replica && p->replica != replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified media preparation changed its physical CLIENT");
    p->replica = replica;
    if (!p->candidate_media && !frontend_unified_media_create(p->frontend,recipe,&p->candidate_media,error)) return false;
    *ready = true; return true;
}
static bool offer_publish(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->candidate_media || !recipe ||
        !frontend_unified_media_current(p->candidate_media) || !close_children(p,error)) return false;
    p->media = p->candidate_media; p->candidate_media = NULL; return true;
}
static bool offer_ready(void *context, frontend_remote_unified *replica, qa_executable_recipe *recipe, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->media || recipe != frontend_remote_unified_recipe(replica))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified child construction precedes actual recipe publication");
    if (!p->audio_owner && !frontend_source_identity_allocate(p->frontend,&p->audio_owner,error)) return false;
    frontend_unified_event_options options = {.audio_owner=p->audio_owner,.context=p,
        .validate=validate,.presentation=presentation,.simulation=simulation,.audio_actor=audio_actor};
    if (!p->events && !frontend_unified_events_create(p->frontend,replica,p->media,&options,&p->events,error)) return false;
    frontend_unified_q1_options q1 = {.audio_owner=p->audio_owner,.context=p,.audio_actor=audio_actor,
        .action_validate=q1_action_validate,.action=q1_action};
    if (!p->q1 && !frontend_unified_q1_create(p->frontend,replica,p->media,&q1,&p->q1,error)) return false;
    if (!p->q2 && !frontend_unified_q2_create(p->frontend,replica,p->media,p->events,&p->q2,error)) return false;
    if (!p->q3 && !frontend_unified_q3_create(p->frontend,replica,p->media,&p->q3,error)) return false;
    if (!p->components && !frontend_unified_components_create(p->frontend,replica,p->media,&p->components,error)) return false;
    if (!p->q3_sources && !frontend_unified_q3_sources_create(replica,p->media,&p->q3_sources,error)) return false;
    return frontend_unified_q1_events(p->q1,p->events,error) &&
        frontend_unified_components_events_bind(p->components,p->events,error) &&
        frontend_unified_q3_audio(p->q3,p->audio_owner,p,audio_actor,error) &&
        frontend_unified_q3_events(p->q3,p->events,error) &&
        frontend_unified_q3_components(p->q3,p->components,error);
}
static bool control(void *context, frontend_remote_unified *replica, const qa_unified_document *doc, qa_error *error)
{
    unified_presentation *p = context;
    const qa_json_document *json = qa_unified_document_json(doc);
    qa_json_id value = qa_json_get(json,qa_unified_document_root(doc),"value");
    qa_json_id kind = qa_json_get(json,value,"kind");
    if (!p || p->replica != replica || !p->events)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified control has no prepared CLIENT consumers");
    if (qa_json_string_equal(json,kind,"components"))
        return frontend_unified_components_control(p->components,doc,error);
    return frontend_unified_events_control(p->events,doc,error);
}
static bool q3_clients_prepare(unified_presentation *p,qa_error *error)
{
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next) row->selected=false;
    for (size_t i=0;i<frontend_unified_q3_source_frame_count(p->q3_source_frame);++i) {
        frontend_unified_q3_source_view view;
        if (!frontend_unified_q3_source_frame_read(p->q3_source_frame,i,&view,error)) return false;
        if (!view.has_client) continue;
        unified_q3_client_row *row=p->q3_clients;
        while (row && !frontend_unified_q3_client_matches(row->client,&view)) row=row->next;
        if (!row) {
            row=calloc(1,sizeof(*row));
            if (!row) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Retaining a per-Source compiled CLIENT");
            if (!frontend_source_identity_allocate(p->frontend,&row->receiver,error)) { free(row); return false; }
            row->born=true; row->next=p->q3_clients; p->q3_clients=row;
            if (!frontend_unified_q3_client_create(p->replica,p->q3_sources,&view,row->receiver,&row->client,error)) return false;
        } else if (!row->born && !row->frame &&
            !frontend_unified_q3_client_prepare(row->client,&view,&row->frame,error)) return false;
        if (row->selected) return frontend_unified_fail(error,QA_ERROR_FORMAT,"Compiled Source repeats its actual CLIENT activation");
        row->selected=true;
    }
    return true;
}
static bool q3_clients_ready(const unified_presentation *p)
{
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next) {
        if (!row->client) return false;
        if (row->selected) {
            if (row->born ? !frontend_unified_q3_client_current(row->client) :
                !frontend_unified_q3_client_ready(row->frame)) return false;
        } else if (!frontend_unified_q3_client_idle(row->client)) return false;
    }
    return true;
}
static bool q3_clients_commit(unified_presentation *p,qa_error *error)
{
    unified_q3_client_row **slot=&p->q3_clients;
    while (*slot) {
        unified_q3_client_row *row=*slot;
        if (row->selected) {
            frontend_unified_q3_client_commit(&row->frame);
            row->selected=false; row->born=false; slot=&row->next;
        } else {
            if (!frontend_unified_q3_client_destroy(&row->client,error)) return false;
            *slot=row->next; free(row);
        }
    }
    return true;
}
static bool events_enter(unified_presentation *p,qa_error *error)
{
    if (p->events && !frontend_unified_events_enter(p->events,error)) return false;
    if (p->frame_prepared) return true;
    for (unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!row->born && !frontend_unified_q3_client_seal(row->client,error)) return false;
    return true;
}
static bool frame(void *context, frontend_remote_unified *replica, const qa_unified_document *doc,
    const qa_unified_document *prediction, frontend_unified_frame_preparation *state, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !state || !p->events || !p->q1 || !p->q2 || !p->q3)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame has incomplete actual CLIENT children");
    if (p->frame_prepared) { *state=FRONTEND_UNIFIED_FRAME_READY; return true; }
    bool component_ready=false;
    if (!frontend_unified_components_frame_prepare(p->components,doc,&p->component_frame,&component_ready,error)) {
        frontend_unified_components_frame_abort(&p->component_frame); return false;
    }
    if (!component_ready) { *state=FRONTEND_UNIFIED_FRAME_OBSOLETE; return true; }
    if (!p->q3_source_frame && !frontend_unified_q3_sources_prepare(p->q3_sources,doc,&p->q3_source_frame,error)) {
        frame_abort(p); return false;
    }
    if (!q3_clients_prepare(p,error)) { frame_abort(p); return false; }
    if (!p->prediction && !frontend_remote_unified_prediction_create(replica,&p->prediction,error)) return false;
    if (!p->candidate_render && !frontend_unified_render_create(p->frontend,replica,p->media,doc,&p->candidate_render,error)) return false;
    if (!p->candidate_prediction && !frontend_unified_clone(prediction,&p->candidate_prediction,error)) return false;
    bool okay = frontend_unified_events_frame_prepare(p->events,doc,error) &&
        frontend_unified_q1_frame_prepare(p->q1,doc,error) && frontend_unified_q2_frame_prepare(p->q2,doc,error) &&
        frontend_unified_q3_frame_prepare(p->q3,doc,error);
    if (!okay) { frame_abort(p); return false; }
    p->frame_prepared = true; *state=FRONTEND_UNIFIED_FRAME_READY; return true;
}
static bool publish(void *context, frontend_remote_unified *replica, const qa_unified_document *doc, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->frame_prepared || !p->candidate_render ||
        !p->candidate_prediction || p->busy || !frontend_unified_render_idle(p->render) ||
        !frontend_unified_render_idle(p->candidate_render) ||
        !frontend_remote_unified_prediction_idle(p->prediction) ||
        !frontend_unified_events_frame_ready(p->events,doc,error) ||
        !frontend_unified_q1_frame_ready(p->q1,doc,error) ||
        !frontend_unified_q2_frame_ready(p->q2,doc,error) ||
        !frontend_unified_q3_frame_ready(p->q3,doc,error) ||
        !frontend_unified_q3_sources_ready(p->q3_source_frame) ||
        !q3_clients_ready(p) ||
        !frontend_unified_components_frame_ready(p->component_frame,doc,error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame publication lost its prepared children");
    /* This is the last fallible adoption. Metadata was installed by the replica
     * and will be restored there if prediction refuses this frame. */
    if (!frontend_remote_unified_prediction_receive(p->prediction,p->candidate_prediction,error)) return false;
    frontend_unified_q1_frame_commit(p->q1); frontend_unified_q2_frame_commit(p->q2);
    frontend_unified_q3_frame_commit(p->q3); frontend_unified_events_frame_commit(p->events);
    frontend_unified_components_frame_commit(&p->component_frame);
    frontend_unified_q3_sources_commit(&p->q3_source_frame);
    if (!q3_clients_commit(p,error)) return false;
    /* Retirement was qualified above; no callback or allocation separates that
     * check from this owned HUD/frame disposal. */
    if (!frontend_unified_render_destroy(&p->render,error)) return false;
    p->render = p->candidate_render; p->candidate_render = NULL;
    qa_unified_document_destroy(p->candidate_prediction); p->candidate_prediction = NULL;
    p->frame_prepared = false; p->received = true; return true;
}
static bool input(void *context, frontend_remote_unified *replica, const qa_unified_input *command,
    double time, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->received)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified input awaits its authoritative prediction frame");
    return frontend_remote_unified_prediction_input(p->prediction,command,time,error);
}
static bool sample(void *context, frontend_remote_unified *replica, uint64_t now, qa_error *error)
{
    unified_presentation *p = context; (void)now;
    return p && p->replica == replica && events_enter(p,error);
}
static bool physical_ready(void *context, frontend_remote_unified *replica,
    uint64_t *sequence, bool *needed, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !sequence || !needed || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified physical input changed its CLIENT owner");
    *sequence=0; *needed=false;
    if (!p->received) return true;
    if (!p->physical && !frontend_unified_input_create(p->frontend,replica,p->prediction,&p->physical,error)) return false;
    bool completed=false;
    if (!frontend_unified_input_retry(p->physical,&completed,sequence,error)) return false;
    if (p->pending_view) {
        qa_actor_id actor; uint32_t source_entity;
        if (!frontend_remote_unified_player(replica,&actor,&source_entity) ||
            !qa_actor_id_equal(actor,p->pending_view_actor) ||
            !frontend_unified_input_view_angles(p->physical,&p->pending_angles,error)) return false;
        p->pending_view=false;
    }
    if (!completed) *sequence=0;
    *needed=true; return true;
}
static bool physical_input(void *context, frontend_remote_unified *replica,
    const qa_seat_input_sample *sample_value, uint64_t sequence, double source_elapsed_ms, qa_error *error)
{
    unified_presentation *p=context;
    if (!p || p->replica != replica || !p->physical || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified physical sample has no authoritative input owner");
    return frontend_unified_input_build(p->physical,sample_value,sequence,source_elapsed_ms,error);
}
static bool world(void *context, const qa_scene_view *view, const qa_scene_world_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    unified_presentation *p = context;
    return frontend_unified_q1_world(p->q1,view,input,frame,error) &&
        frontend_unified_q2_world(p->q2,view,input,frame,error) && frontend_unified_q3_world(p->q3,view,input,frame,error) &&
        frontend_unified_components_world(p->components,view,input,frame,error) &&
        frontend_unified_events_draw(p->events,view,frame,error);
}
static bool lights(void *context, const qa_scene_view *view, const qa_scene_world_input *input,
    const qa_scene_light **out, size_t *count, qa_error *error)
{
    unified_presentation *p = context;
    const qa_scene_light *q2=NULL,*q3=NULL,*components=NULL; size_t n=0,m=0,k=0;
    if (!frontend_unified_components_prepare_draw(p->components,view,p->frontend->frame.sequence,error) ||
        !frontend_unified_components_lights(p->components,&components,&k,error) ||
        !frontend_unified_q2_lights(p->q2,view,input,&q2,&n,error) ||
        !frontend_unified_q3_lights(p->q3,view,input,&q3,&m,error)) return false;
    if (!n && !m && !k) { *out=input->lights; *count=input->light_count; return true; }
    size_t limit=SIZE_MAX/sizeof(*q2);
    if (input->light_count>limit || n>limit-input->light_count || m>limit-input->light_count-n ||
        k>limit-input->light_count-n-m)
        return frontend_unified_fail(error,QA_ERROR_MEMORY,"Unified light pool exceeds actual draw storage");
    size_t total=n+m+k+input->light_count;
    qa_scene_light *joined=malloc(total*sizeof(*joined));
    if (!joined) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Joining received family light pools");
    if (input->light_count) memcpy(joined,input->lights,input->light_count*sizeof(*joined));
    if (n) memcpy(joined+input->light_count,q2,n*sizeof(*joined));
    if (m) memcpy(joined+input->light_count+n,q3,m*sizeof(*joined));
    if (k) memcpy(joined+input->light_count+n+m,components,k*sizeof(*joined));
    free(p->draw_lights); p->draw_lights=joined; *out=joined; *count=total; return true;
}
static bool world_input(void *context, qa_scene_world_input *input, qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q1_world_input(p->q1,input,error) &&
        frontend_unified_q2_world_input(p->q2,input,error);
}
static bool hud(void *context, qa_ui *ui, qa_scene_rect viewport, qa_scene_frame *frame, qa_error *error)
{
    unified_presentation *p = context;
    return frontend_unified_q1_hud(p->q1,ui,viewport,frame,error) &&
        frontend_unified_q2_hud(p->q2,ui,viewport,frame,error) &&
        frontend_unified_components_hud(p->components,ui,viewport,frame,error);
}
static bool model(void *context, qa_actor_id actor, const char *content, const char *path,
    qa_scene_model_input *input, qa_error *error)
{
    unified_presentation *p=context;
    return frontend_unified_q1_model(p->q1,actor,content,path,input,error) &&
        frontend_unified_q2_model(p->q2,actor,content,path,input,error);
}
static bool model_after(void *context,qa_actor_id actor,const char *content,const char *path,
    const qa_scene_model_input *input_value,qa_scene_frame *frame_value,qa_error *error)
{
    return frontend_unified_q2_model_after(((unified_presentation *)context)->q2,actor,content,path,
        input_value,frame_value,error);
}
static bool draw(void *context, frontend_remote_unified *replica, float stereo, qa_audio_listener *listener, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || p->replica != replica || !p->render || !p->received)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified drawing awaits its actual received frame");
    frontend_unified_prediction_view prediction;
    frontend_unified_render_children children = {.context=p,.world_input=world_input,.lights=lights,.world=world,.hud=hud,.model=model,.model_after=model_after};
    if (!frontend_remote_unified_prediction_read(p->prediction,&prediction,error) ||
        !events_enter(p,error) ||
        !frontend_unified_render_draw(p->render,&prediction,&children,stereo,listener,error)) return false;
    return audio_actor(p,prediction.actor,&listener->actor,error);
}
static bool close(void *context, frontend_remote_unified *replica, qa_error *error)
{
    unified_presentation *p = context;
    if (!p || (p->replica && p->replica != replica) || !close_children(p,error)) return false;
    if (!frontend_unified_media_destroy(p->candidate_media,error)) return false;
    p->candidate_media = NULL; return true;
}
static bool content_visit(void *context, const frontend_remote_unified *replica,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    unified_presentation *p = context;
    return p && idle(p,replica) && (!p->media || frontend_unified_media_visit(p->media,visitor,error)) &&
        (!p->q2 || frontend_unified_q2_visit(p->q2,visitor,error)) &&
        (!p->q3 || frontend_unified_q3_visit(p->q3,visitor,error)) &&
        (!p->components || frontend_unified_components_visit(p->components,visitor,error)) &&
        (!p->candidate_media || frontend_unified_media_visit(p->candidate_media,visitor,error));
}
static void dispose(void *context) { free(context); }
static frontend_remote_unified_consumers consumers(unified_presentation *p)
{
    return (frontend_remote_unified_consumers){.context=p,.prepare=prepare,.offer_publish=offer_publish,
        .offer_ready=offer_ready,.control=control,.frame=frame,.publish=publish,.input=input,
        .physical_ready=physical_ready,.physical_input=physical_input,.sample=sample,.draw=draw,
        .idle=idle,.close=close,.content_visit=content_visit,.dispose=dispose};
}
bool frontend_remote_unified_presentation_create(qa_frontend *frontend,
    const frontend_remote_unified_options *source, frontend_remote_unified **out, qa_error *error)
{
    if (!source || !frontend || !out || *out)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified factory needs its actual CLIENT construction tuple");
    unified_presentation *p = calloc(1,sizeof(*p));
    if (!p) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating readonly unified presentation owner");
    p->frontend = frontend;
    frontend_remote_unified_options options = *source;
    options.consumers = consumers(p);
    if (!frontend_remote_unified_create(frontend,&options,out,error)) { free(p); return false; }
    p->replica = *out; return true;
}
bool frontend_remote_unified_presentation_restore_prefix(qa_frontend *frontend,
    const frontend_remote_unified_options *source,const qa_net_client *peer,
    qa_application_content_graph *graph,qa_bytes bytes,frontend_remote_unified **out,qa_error *error)
{
    if (!frontend || !source || !peer || !graph || !out || *out || !frontend->source_restoring)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified factory import requires its actual cold CLIENT prefix");
    unified_presentation *p=calloc(1,sizeof(*p));
    if (!p) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Importing readonly unified presentation owner");
    p->frontend=frontend;
    frontend_remote_unified_options options=*source;
    options.consumers=consumers(p);
    bool okay=frontend_remote_unified_restore_prefix(frontend,&options,peer,graph,bytes,out,error);
    if (*out) {
        p->replica=*out;
        /* The imported factory itself owns checked cleanup, including a
         * decoder failure before any saved child-presence field is reached. */
        (*out)->consumers_live=true;
    }
    else free(p);
    return okay;
}
bool frontend_remote_unified_presentation_time(const frontend_remote_unified *replica, double *out, qa_error *error)
{
    if (!replica || replica->options.consumers.input != input)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified command clock has no actual prediction factory");
    const unified_presentation *p = replica->options.consumers.context;
    if (!p->received) return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified command clock awaits its first authoritative frame");
    return frontend_remote_unified_prediction_time(p->prediction,out,error);
}
bool frontend_remote_unified_presentation_media(const frontend_remote_unified *replica,
    frontend_unified_media **installed,frontend_unified_media **pending,qa_error *error)
{
    if (!replica || !installed || !pending || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media inventory has no actual presentation factory");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || replica->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media inventory overlaps its actual factory callback");
    *installed=p->media; *pending=p->candidate_media; return true;
}
frontend_unified_q3_sources *frontend_remote_unified_presentation_q3_sources(const frontend_remote_unified *replica)
{
    if (!replica || replica->options.consumers.input!=input) return NULL;
    const unified_presentation *p=replica->options.consumers.context;
    return p && p->replica==replica ? p->q3_sources : NULL;
}
size_t frontend_remote_unified_presentation_q3_client_count(const frontend_remote_unified *replica)
{
    if (!replica || replica->options.consumers.input!=input) return 0;
    const unified_presentation *p=replica->options.consumers.context;
    size_t count=0;
    if (p && p->replica==replica) for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!row->born) ++count;
    return count;
}
frontend_unified_q3_client *frontend_remote_unified_presentation_q3_client(
    const frontend_remote_unified *replica,size_t index)
{
    if (!replica || replica->options.consumers.input!=input) return NULL;
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica) return NULL;
    for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next)
        if (!row->born && !index--) return row->client;
    return NULL;
}
bool frontend_remote_unified_presentation_restore_media(frontend_remote_unified *replica,
    frontend_unified_media **installed,frontend_unified_media **pending,qa_error *error)
{
    if (!replica || !installed || !pending || !replica->frontend->source_restoring ||
        replica->options.consumers.input!=input || (*installed && *installed==*pending))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media attachment requires its actual detached restore prefix");
    unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || p->media || p->candidate_media || p->render ||
        p->prediction || p->events || p->q1 || p->q2 || p->q3 || p->components || p->q3_sources ||
        (*installed && frontend_unified_media_recipe(*installed)!=replica->recipe) ||
        (*pending && frontend_unified_media_recipe(*pending)!=replica->preparing_recipe))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified media attachment changed its real recipe or existing destructor owner");
    p->media=*installed; p->candidate_media=*pending;
    *installed=NULL; *pending=NULL; replica->consumers_live=true; return true;
}
bool frontend_remote_unified_presentation_children_read(const frontend_remote_unified *replica,
    frontend_unified_presentation_children *out,qa_error *error)
{
    if (!replica || !out || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified child inventory has no actual presentation factory");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || replica->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified child inventory overlaps its actual factory callback");
    *out=(frontend_unified_presentation_children){.media=p->media,.pending_media=p->candidate_media,
        .render=p->render,.pending_render=p->candidate_render,.prediction=p->prediction,.input=p->physical,
        .events=p->events,.q1=p->q1,.q2=p->q2,.q3=p->q3,.q3_sources=p->q3_sources,
        .components=p->components,.audio_owner=p->audio_owner};
    return true;
}
size_t frontend_remote_unified_presentation_audio_count(const frontend_remote_unified *replica)
{
    if (!replica || replica->options.consumers.input!=input) return 0;
    const unified_presentation *p=replica->options.consumers.context;
    size_t count=0;
    if (p && p->replica==replica) for (const unified_audio_identity *row=p->audio;row;row=row->next) ++count;
    return count;
}
bool frontend_remote_unified_presentation_audio_read(const frontend_remote_unified *replica,size_t index,
    qa_actor_id *actor,uint64_t *audio)
{
    if (!replica || !actor || !audio || replica->options.consumers.input!=input) return false;
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || p->busy || replica->busy) return false;
    for (const unified_audio_identity *row=p->audio;row;row=row->next)
        if (!index--) { *actor=row->actor; *audio=row->audio; return true; }
    return false;
}
bool frontend_remote_unified_presentation_trace(const frontend_remote_unified *replica,
    const qa_trace_query *query,qa_trace_result *out,qa_error *error)
{
    if (!replica || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified trace has no actual CLIENT collision owner");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified trace awaits a returned authoritative prediction scene");
    return frontend_remote_unified_prediction_trace(p->prediction,query,out,error);
}
bool frontend_remote_unified_presentation_body(const frontend_remote_unified *replica,
    qa_actor_id actor,qa_body_state *out,qa_error *error)
{
    if (!replica || replica->options.consumers.input!=input)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified body has no actual CLIENT collision owner");
    const unified_presentation *p=replica->options.consumers.context;
    if (!p || p->replica!=replica || !p->received || p->busy)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified body awaits a returned authoritative prediction scene");
    return frontend_remote_unified_prediction_body_read(p->prediction,actor,out,error);
}

static unified_presentation *presentation_owner(const frontend_remote_unified *replica)
{
    if (!replica || replica->options.consumers.input!=input) return NULL;
    unified_presentation *p=replica->options.consumers.context;
    return p && p->replica==replica ? p : NULL;
}
static bool saved_blob(qa_source_save_io *io,qa_buffer *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        bytes->size=size; bytes->data=size?malloc(size):NULL;
        if (size && !bytes->data) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Retaining unified CLIENT child continuation");
    }
    return qa_source_save_bytes(io,bytes->data,size);
}
static bool saved_identity(const qa_frontend *f,uint64_t identity)
{
    return identity>QA_FRONTEND_COMMAND_OWNER && identity-QA_FRONTEND_COMMAND_OWNER<=f->next_source_id;
}
static bool saved_actor(unified_presentation *p,qa_source_save_io *io,qa_actor_id *actor)
{
    qa_saved_actor_id wire={0};
    if (io->direction==QA_SOURCE_SAVE_WRITE && !frontend_remote_unified_wire_actor(p->replica,*actor,&wire)) return false;
    return qa_source_save_u32(io,&wire.slot) && qa_source_save_u64(io,&wire.generation) &&
        (io->direction!=QA_SOURCE_SAVE_READ || frontend_remote_unified_actor_retained(p->replica,wire.slot,wire.generation,actor,io->error));
}
static bool saved_routes(unified_presentation *p,qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=0;
    if (!reading) for (const unified_audio_identity *row=p->audio;row;row=row->next) ++count;
    if (!qa_source_save_count(io,&count,reading?(io->input.size-io->offset)/20:SIZE_MAX)) return false;
    unified_audio_identity **tail=&p->audio;
    unified_audio_identity *row=p->audio;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            row=calloc(1,sizeof(*row));
            if (!row) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Retaining actual unified mixer actor aliases");
            *tail=row; tail=&row->next;
        }
        if (!row || !saved_actor(p,io,&row->actor) || !qa_source_save_u64(io,&row->audio) ||
            !saved_identity(p->frontend,row->audio) || row->audio==p->audio_owner) return false;
        for (const unified_audio_identity *previous=p->audio;previous!=row;previous=previous->next)
            if (previous->audio==row->audio || qa_actor_id_equal(previous->actor,row->actor)) return false;
        if (!reading) row=row->next;
    }
    return true;
}
static bool child_capture(unified_presentation *p,unified_child_kind kind,
    const frontend_unified_presentation_refs *refs,qa_buffer *out,qa_error *error)
{
    switch (kind) {
    case UNIFIED_MEDIA: return !p->media || frontend_unified_media_checkpoint(p->media,&refs->media,out,error);
    case UNIFIED_PENDING_MEDIA: return !p->candidate_media || frontend_unified_media_checkpoint(p->candidate_media,&refs->pending_media,out,error);
    case UNIFIED_EVENTS: return !p->events || frontend_unified_events_checkpoint(p->events,&refs->events,out,error);
    case UNIFIED_Q1: return !p->q1 || frontend_unified_q1_checkpoint(p->q1,&refs->q1,out,error);
    case UNIFIED_Q2: return !p->q2 || frontend_unified_q2_checkpoint(p->q2,&refs->q2,out,error);
    case UNIFIED_Q3: return !p->q3 || frontend_unified_q3_checkpoint(p->q3,refs->media.content,out,error);
    case UNIFIED_COMPONENTS: return !p->components || frontend_unified_components_checkpoint(p->components,&refs->components,out,error);
    case UNIFIED_Q3_SOURCES: return !p->q3_sources || frontend_unified_q3_sources_checkpoint(p->q3_sources,out,error);
    case UNIFIED_PREDICTION: return !p->prediction || frontend_remote_unified_prediction_checkpoint(p->prediction,out,error);
    case UNIFIED_INPUT: return !p->physical || frontend_unified_input_checkpoint(p->physical,out,error);
    case UNIFIED_RENDER: return !p->render || frontend_unified_render_checkpoint(p->render,&refs->render,out,error);
    case UNIFIED_CHILD_COUNT: break;
    }
    return false;
}
static bool presentation_fields(unified_presentation *p,unified_presentation_import *saved,
    const frontend_unified_presentation_refs *refs,qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    char magic[4]={'Q','U','P','C'}; uint32_t version=1,epoch=p->replica->epoch;
    uint32_t physical=p->replica->options.domain.physical_seat;
    uint64_t frame_number=p->replica->frame_number;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QUPC",sizeof(magic)) ||
        !qa_source_save_u32(io,&version) || version!=1 || !qa_source_save_u32(io,&epoch) || epoch!=p->replica->epoch ||
        !qa_source_save_u32(io,&physical) || physical!=p->replica->options.domain.physical_seat ||
        !qa_source_save_u64(io,&frame_number) || frame_number!=p->replica->frame_number ||
        !qa_source_save_bool(io,&saved->received) || (saved->received && !p->replica->frame) ||
        !qa_source_save_u64(io,&p->audio_owner) || (p->audio_owner && !saved_identity(p->frontend,p->audio_owner)) ||
        !qa_source_save_bool(io,&p->pending_view) || !qa_source_save_f64(io,&p->pending_angles.x) ||
        !qa_source_save_f64(io,&p->pending_angles.y) || !qa_source_save_f64(io,&p->pending_angles.z) ||
        !isfinite(p->pending_angles.x) || !isfinite(p->pending_angles.y) || !isfinite(p->pending_angles.z) ||
        (p->pending_view && (!saved_actor(p,io,&p->pending_view_actor) ||
            !qa_actor_id_equal(p->pending_view_actor,p->replica->player))) || !saved_routes(p,io)) return false;
    for (size_t i=0;i<UNIFIED_CHILD_COUNT;++i) {
        if ((!reading && !child_capture(p,(unified_child_kind)i,refs,saved->child+i,io->error)) ||
            !saved_blob(io,saved->child+i)) return false;
    }
    bool media=saved->child[UNIFIED_MEDIA].size!=0;
    if ((media && !p->replica->recipe) || (saved->child[UNIFIED_PENDING_MEDIA].size && !p->replica->preparing_recipe)) return false;
    if ((saved->child[UNIFIED_PENDING_MEDIA].size && refs->pending_media.content!=refs->media.content) ||
        (saved->child[UNIFIED_Q2].size && refs->q2.content!=refs->media.content) ||
        (saved->child[UNIFIED_COMPONENTS].size && refs->components.content!=refs->media.content)) return false;
    for (size_t i=UNIFIED_EVENTS;i<UNIFIED_CHILD_COUNT;++i)
        if (saved->child[i].size && (!media || !p->audio_owner)) return false;
    if (saved->received) for (size_t i=UNIFIED_MEDIA;i<UNIFIED_CHILD_COUNT;++i)
        if (i!=UNIFIED_PENDING_MEDIA && i!=UNIFIED_INPUT && !saved->child[i].size) return false;
    if (!saved->received && (saved->child[UNIFIED_RENDER].size || saved->child[UNIFIED_INPUT].size)) return false;
    if (!reading) for (const unified_q3_client_row *row=p->q3_clients;row;row=row->next) ++saved->client_count;
    if (!qa_source_save_count(io,&saved->client_count,reading?(io->input.size-io->offset)/24:SIZE_MAX) ||
        saved->client_count>SIZE_MAX/sizeof(*saved->clients)) return false;
    if (saved->client_count) {
        saved->clients=calloc(saved->client_count,sizeof(*saved->clients));
        if (!saved->clients) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Retaining per-Source compiled CLIENT capsule order");
    }
    const unified_q3_client_row *row=p->q3_clients;
    for (size_t i=0;i<saved->client_count;++i) {
        unified_saved_q3_client *client=saved->clients+i;
        if (!reading) {
            if (!row || row->born || row->frame) return false;
            client->receiver=row->receiver; bool found=false;
            for (size_t n=0;n<frontend_unified_q3_sources_count(p->q3_sources);++n) {
                frontend_unified_q3_source_view view;
                if (!frontend_unified_q3_sources_checkpoint_read(p->q3_sources,n,&view,io->error)) return false;
                if (frontend_unified_q3_client_checkpoint_matches(row->client,&view)) { client->source=n; found=true; break; }
            }
            if (!found || !frontend_unified_q3_client_checkpoint(row->client,&client->bytes,io->error)) return false;
            row=row->next;
        }
        if (!qa_source_save_count(io,&client->source,SIZE_MAX) || !qa_source_save_u64(io,&client->receiver) ||
            !saved_identity(p->frontend,client->receiver) || client->receiver==p->audio_owner ||
            !saved_blob(io,&client->bytes) || !client->bytes.size) return false;
        for (size_t n=0;n<i;++n)
            if (saved->clients[n].source==client->source || saved->clients[n].receiver==client->receiver) return false;
        for (const unified_audio_identity *alias=p->audio;alias;alias=alias->next)
            if (alias->audio==client->receiver) return false;
    }
    return !saved->client_count || saved->child[UNIFIED_Q3_SOURCES].size;
}
bool frontend_remote_unified_presentation_checkpoint(frontend_remote_unified *replica,
    const frontend_unified_presentation_refs *refs,qa_buffer *out,qa_error *error)
{
    unified_presentation *p=presentation_owner(replica);
    if (!p || !refs || !refs->media.content || !out || out->data || out->size || p->import ||
        p->frame_prepared || p->candidate_render || p->candidate_prediction || replica->prepared_frame ||
        !idle(p,replica) || !frontend_remote_unified_checkpoint_current(replica,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified CLIENT capture requires its returned actual child graph");
    unified_presentation_import *saved=calloc(1,sizeof(*saved));
    if (!saved) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Capturing unified CLIENT continuation");
    saved->received=p->received;
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,error) && presentation_fields(p,saved,refs,&io) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); import_free(saved);
    if (!okay && (!error || error->code==QA_OK)) frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified CLIENT child graph cannot be captured exactly");
    return okay;
}
bool frontend_remote_unified_presentation_restore_prepare(frontend_remote_unified *replica,
    const frontend_unified_presentation_refs *refs,qa_bytes bytes,qa_error *error)
{
    unified_presentation *p=presentation_owner(replica);
    if (!p || !refs || !refs->media.content || !replica->restore_pending || !p->frontend->source_restoring ||
        p->import || p->media || p->candidate_media || p->events || p->prediction || p->audio || p->q3_clients)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified CLIENT child import requires its empty actual recipe prefix");
    p->import=calloc(1,sizeof(*p->import));
    if (!p->import) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Retaining unified CLIENT cold prefix");
    qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && presentation_fields(p,p->import,refs,&io) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!okay) {
        if (!error || error->code==QA_OK)
            frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified CLIENT capsule has an invalid owner or child field");
        return false;
    }
    p->import->decoded=true;
    qa_buffer *media=&p->import->child[UNIFIED_MEDIA],*pending=&p->import->child[UNIFIED_PENDING_MEDIA];
    if (media->size && !frontend_unified_media_restore_prepare(p->frontend,replica->recipe,&refs->media,
        (qa_bytes){media->data,media->size},&p->media,error)) return false;
    if (pending->size && !frontend_unified_media_restore_prepare(p->frontend,replica->preparing_recipe,&refs->pending_media,
        (qa_bytes){pending->data,pending->size},&p->candidate_media,error)) return false;
    p->import->media_prepared=true;
    return true;
}
bool frontend_remote_unified_presentation_restore_roots(frontend_remote_unified *replica,
    const frontend_unified_presentation_refs *refs,qa_error *error)
{
    unified_presentation *p=presentation_owner(replica);
    if (!p || !p->import || !p->import->decoded || !p->import->media_prepared || !refs ||
        !replica->restore_pending || !p->frontend->source_restoring) return false;
    if (p->import->roots) return true;
    if (!p->import->media_roots) {
        if (p->media && !frontend_unified_media_roots_attach(p->media,&refs->media,error)) return false;
        p->import->media_roots=true;
    }
    if (!p->import->pending_roots) {
        if (p->candidate_media && !frontend_unified_media_roots_attach(p->candidate_media,&refs->pending_media,error)) return false;
        p->import->pending_roots=true;
    }
    p->import->roots=true; return true;
}
static bool child_import(unified_presentation *p,unified_child_kind kind,
    const frontend_unified_presentation_refs *refs,qa_error *error)
{
    qa_buffer *saved=p->import->child+kind;
    if (!saved->size || p->import->imported[kind]) return true;
    qa_bytes bytes={saved->data,saved->size}; bool okay=false;
    frontend_unified_event_options events={.audio_owner=p->audio_owner,.context=p,
        .validate=validate,.presentation=presentation,.simulation=simulation,.audio_actor=audio_actor};
    frontend_unified_q1_options q1={.audio_owner=p->audio_owner,.context=p,.audio_actor=audio_actor,
        .action_validate=q1_action_validate,.action=q1_action};
    switch (kind) {
    case UNIFIED_COMPONENTS:
        okay=frontend_unified_components_restore_prepare(p->frontend,p->replica,p->media,&refs->components,bytes,&p->components,error); break;
    case UNIFIED_EVENTS:
        okay=frontend_unified_events_restore(p->frontend,p->replica,p->media,&events,&refs->events,bytes,&p->events,error); break;
    case UNIFIED_Q1:
        okay=frontend_unified_q1_restore(p->frontend,p->replica,p->media,&q1,&refs->q1,bytes,&p->q1,error); break;
    case UNIFIED_Q2:
        okay=frontend_unified_q2_restore(p->frontend,p->replica,p->media,p->events,&refs->q2,bytes,&p->q2,error); break;
    case UNIFIED_Q3:
        okay=frontend_unified_q3_restore(p->frontend,p->replica,p->media,p->components,refs->media.content,bytes,&p->q3,error); break;
    case UNIFIED_Q3_SOURCES:
        okay=frontend_unified_q3_sources_restore(p->replica,p->media,bytes,&p->q3_sources,error); break;
    case UNIFIED_PREDICTION:
        okay=frontend_remote_unified_prediction_restore(p->replica,bytes,&p->prediction,error); break;
    case UNIFIED_INPUT:
        okay=frontend_unified_input_restore(p->frontend,p->replica,p->prediction,bytes,&p->physical,error); break;
    case UNIFIED_RENDER:
        okay=frontend_unified_render_restore(p->frontend,p->replica,p->media,&refs->render,bytes,&p->render,error); break;
    case UNIFIED_MEDIA: case UNIFIED_PENDING_MEDIA: case UNIFIED_CHILD_COUNT: break;
    }
    if (okay) p->import->imported[kind]=true;
    return okay;
}
bool frontend_remote_unified_presentation_restore_finish(frontend_remote_unified *replica,
    const frontend_unified_presentation_refs *refs,qa_error *error)
{
    unified_presentation *p=presentation_owner(replica);
    if (!p || !refs || !replica->restore_pending || !p->frontend->source_restoring)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified CLIENT activation requires its actual isolated cold graph");
    if (p->restore_complete) return frontend_remote_unified_presentation_restore_ready(replica,error);
    if (!p->import || !p->import->decoded || !p->import->roots) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified CLIENT media roots have not imported");
    if ((p->media && frontend_unified_media_importing(p->media) && !frontend_unified_media_restore_finish(p->media,&refs->media,error)) ||
        (p->candidate_media && frontend_unified_media_importing(p->candidate_media) &&
            !frontend_unified_media_restore_finish(p->candidate_media,&refs->pending_media,error))) return false;
    if (!child_import(p,UNIFIED_COMPONENTS,refs,error) || !child_import(p,UNIFIED_EVENTS,refs,error)) return false;
    if (p->components && p->events && !frontend_unified_components_events_bind(p->components,p->events,error)) return false;
    if (p->components && !p->import->components_finished) {
        if (!frontend_unified_components_restore_finish(p->components,&refs->components,error)) return false;
        p->import->components_finished=true;
    }
    if (!child_import(p,UNIFIED_Q1,refs,error)) return false;
    if (p->q1 && p->events && !frontend_unified_q1_events(p->q1,p->events,error)) return false;
    if (p->q1 && !p->import->q1_finished) {
        if (!frontend_unified_q1_restore_finish(p->q1,error)) return false;
        p->import->q1_finished=true;
    }
    if (!child_import(p,UNIFIED_Q2,refs,error) || !child_import(p,UNIFIED_Q3_SOURCES,refs,error) ||
        !child_import(p,UNIFIED_PREDICTION,refs,error) || !child_import(p,UNIFIED_Q3,refs,error) ||
        !child_import(p,UNIFIED_RENDER,refs,error) || !child_import(p,UNIFIED_INPUT,refs,error)) return false;
    if (p->q3 && (!frontend_unified_q3_audio(p->q3,p->audio_owner,p,audio_actor,error) ||
        (p->events && !frontend_unified_q3_events(p->q3,p->events,error)) ||
        (p->components && !frontend_unified_q3_components(p->q3,p->components,error)))) return false;
    unified_q3_client_row **tail=&p->q3_clients;
    for (size_t i=0;i<p->import->client_count;++i) {
        unified_saved_q3_client *saved=p->import->clients+i;
        frontend_unified_q3_source_view source;
        if (!frontend_unified_q3_sources_checkpoint_read(p->q3_sources,saved->source,&source,error) || !source.has_client) return false;
        if (!*tail) {
            *tail=calloc(1,sizeof(**tail));
            if (!*tail) return frontend_unified_fail(error,QA_ERROR_MEMORY,"Importing actual per-Source compiled CLIENT history");
            (*tail)->receiver=saved->receiver;
            if (!frontend_unified_q3_client_restore(replica,p->q3_sources,&source,saved->receiver,
                (qa_bytes){saved->bytes.data,saved->bytes.size},&(*tail)->client,error)) return false;
        }
        if ((*tail)->receiver!=saved->receiver || !frontend_unified_q3_client_checkpoint_matches((*tail)->client,&source) ||
            !frontend_unified_q3_client_checkpoint_current((*tail)->client)) return false;
        tail=&(*tail)->next;
    }
    if (*tail || !idle(p,replica)) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified imported CLIENT descendants have not returned");
    p->received=p->import->received;
    import_free(p->import); p->import=NULL; p->restore_complete=true;
    return frontend_remote_unified_presentation_restore_ready(replica,error);
}
bool frontend_remote_unified_presentation_restore_ready(const frontend_remote_unified *replica,qa_error *error)
{
    unified_presentation *p=presentation_owner(replica);
    return (p && p->restore_complete && !p->import && !p->frame_prepared &&
        !p->candidate_render && !p->candidate_prediction && idle(p,replica) &&
        (!p->received || (p->media && p->render && p->prediction && p->events && p->q1 &&
            p->q2 && p->q3 && p->components && p->q3_sources)) &&
        frontend_remote_unified_checkpoint_current(replica,error)) ||
        frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified CLIENT saved children have not finished their actual graph import");
}
void frontend_remote_unified_presentation_adopt(frontend_remote_unified *replica)
{
    unified_presentation *p=presentation_owner(replica);
    if (p && p->restore_complete && !p->import && p->events) frontend_unified_events_adopt(p->events);
}
