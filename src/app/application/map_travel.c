#include "map_travel_private.h"
#include "qa/source_save.h"
#include "rankings.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_native_q2_presentation.h"
#include "map_players_private.h"
#include "save_private.h"
#include "save_content.h"
#include "guest_qc_original_save.h"
#include "qa/save.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


static bool same_vector(qa_vec3 a, qa_vec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static struct application_map_state *map_state(qa_application *application,
                                               qa_error *error)
{
    if (application->map_state == NULL) {
        application->map_state = calloc(1, sizeof(*application->map_state));
        if (application->map_state == NULL) {
            application_fail(error, QA_ERROR_MEMORY,
                             "cannot retain application map continuation");
            return NULL;
        }
    }
    return application->map_state;
}

static bool map_safe(const qa_application *application)
{
    return application != NULL && application->operation == APPLICATION_IDLE &&
           !qa_application_startup_pending(application) && !application->failed_publications &&
           !application->q3_round_active && !application->q3_world_restart &&
           !application->frame_preparing &&
           application_rankings_idle(application) &&
           !application->destroy_requested && !application->finalizing &&
           application->state != QA_APPLICATION_FAULTED &&
           application->state != QA_APPLICATION_STOPPING &&
           qa_session_destroy_ready(application->session) &&
           (application->world == NULL || qa_world_idle(application->world)) &&
           qa_combat_idle(application->combat);
}

static void campaign_travel_dispose(qa_application *application)
{
    application_campaign_travel *travel=application->campaign_travel;
    if (!travel) return;
    application_players_dispose(travel->players);
    qa_campaign_visit_destroy(travel->visit);
    free(travel); application->campaign_travel=NULL;
}

bool application_campaign_location(qa_application *application, qa_product_id geometry,
    const char *map, qa_campaign_location *out, qa_error *error)
{
    application_provider *source=application_world_provider(application,QA_ROLE_ENTITIES,"");
    const qa_product *product=qa_catalog_product(application->catalog,geometry);
    if (!product || !product->identity || !source || !source->product ||
        !source->product->identity || !source->launch)
        return application_fail(error,QA_ERROR_ARGUMENT,"Campaign location lost its actual content owner");
    const char *implementation=source->launch->selection.implementation;
    if (!implementation) implementation="";
    size_t a=strlen(product->identity), b=strlen(source->product->identity), c=strlen(implementation);
    if (a>SIZE_MAX-64 || b>SIZE_MAX-a-64 || c>SIZE_MAX-a-b-64)
        return application_fail(error,QA_ERROR_MEMORY,"Campaign content identity extent exhausted");
    size_t size=a+b+c+64;
    char *text=malloc(size);
    if (!text) return application_fail(error,QA_ERROR_MEMORY,"Retaining campaign content identity");
    int length=snprintf(text,size,"%zu:%s%zu:%s%zu:%s",a,product->identity,b,source->product->identity,c,implementation);
    qa_string_id content=0;
    bool ok=length>=0 && (size_t)length<size &&
        qa_strings_intern(qa_session_strings(application->session),
            (qa_bytes){(const uint8_t *)text,(size_t)length},&content,error);
    free(text);
    if (ok) ok=qa_campaign_location_make(qa_session_strings(application->session),content,
        (qa_bytes){(const uint8_t *)map,strlen(map)},out,error);
    return ok;
}

static bool campaign_q2_policy(qa_application *application, bool *retain,
    bool *reload, qa_error *error)
{
    *retain=false; *reload=false;
    application_provider *source=application_world_provider(application,QA_ROLE_ENTITIES,"");
    if (!source || !source->product || source->product->family!=QA_GAME_Q2) return true;
    qa_q2_edition edition=QA_Q2_CLASSIC;
    bool found=false;
    if (!qa_application_native_q2_source_profile_read(application,source->owner,&edition,&found,error)) return false;
    if (!found) return application_fail(error,QA_ERROR_UNSUPPORTED,
        "Campaign travel requires its selected Q2 GAME profile");
    qa_application_startup_source physical;
    const qa_launch_snapshot *snapshot=qa_application_launch(application);
    const qa_launch_instance *selected=qa_launch_snapshot_find(snapshot,
        source->launch->selection.instance);
    if (!qa_application_startup_source_read(application,snapshot,
        selected,&physical,error)) return false;
    const qa_cvar_view *deathmatch=qa_cvars_find(physical.cvars,"deathmatch");
    const qa_cvar_view *noreload=qa_cvars_find(physical.cvars,"sv_noreload");
    if (!deathmatch) return application_fail(error,QA_ERROR_FORMAT,
        "Campaign travel lost its selected Q2 deathmatch setting");
    bool competitive=edition==QA_Q2_CLASSIC ? deathmatch->number!=0 : deathmatch->integer!=0;
    bool fresh=noreload && (edition==QA_Q2_CLASSIC ? noreload->number!=0 : noreload->integer!=0);
    /* Classic SV_GameMap_f writes every non-* departure. The enhanced
     * rerelease host omits deathmatch saves; neither host reloads them. */
    *retain=edition==QA_Q2_CLASSIC || !competitive;
    *reload=!competitive && !fresh;
    return true;
}

static bool campaign_level_entry(qa_application *application, qa_error *error)
{
    if (!application->campaign_unit) {
        application->campaign_unit=qa_campaign_unit_create(qa_session_strings(application->session),error);
        if (!application->campaign_unit) return false;
    }
    if (application->campaign_travel) {
        application_campaign_travel *travel=application->campaign_travel;
        if (!travel->visit || !qa_campaign_visit_commit(travel->visit,error)) return false;
        campaign_travel_dispose(application);
        return true;
    }
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(application));
    qa_campaign_location destination,current;
    if (!choices || !application_campaign_location(application,choices->world.geometry,choices->world.map,&destination,error)) return false;
    bool new_unit=application->map_state && application->map_state->load_new_unit;
    bool has_current=qa_campaign_unit_current(application->campaign_unit,&current);
    if (!new_unit && has_current &&
        qa_campaign_location_equal(current,destination)) return true;
    qa_campaign_visit *visit=NULL;
    bool ok=qa_campaign_unit_stage(application->campaign_unit,destination,new_unit,false,NULL,&visit,error) &&
        qa_campaign_visit_commit(visit,error);
    qa_campaign_visit_destroy(visit);
    if (ok && (new_unit || (has_current && current.content!=destination.content))) {
        application_save_content_destroy(application->content_graph);
        application->content_graph=NULL;
    }
    if (ok && application->map_state) application->map_state->load_new_unit=false;
    return ok;
}

bool application_map_level_entry(qa_application *application, bool fresh, qa_error *error)
{
    if (!application || !application->map_revision || !application->world) return true;
    struct application_map_state *state = map_state(application, error);
    if (!state) return false;
    if (state->entry_generation == application->map_revision) return true;
    if (!campaign_level_entry(application,error)) return false;
    if (state->save_request_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "level save request identity exhausted");
    if (!application_q1_music_cue(application, fresh, error)) return false;
    state->entry_generation = application->map_revision;
    ++state->save_request_revision;
    state->save_requested = true;
    state->fresh_entry = fresh;
    state->authored_autosave = false;
    return true;
}

bool application_map_autosave_request(qa_application *application, qa_error *error)
{
    struct application_map_state *state = map_state(application, error);
    if (!state) return false;
    if (state->save_request_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "authored save request identity exhausted");
    state->entry_generation = application->map_revision;
    ++state->save_request_revision;
    state->save_requested = true;
    state->authored_autosave = true;
    return true;
}

bool qa_application_save_request_read(const qa_application *application, qa_application_save_request *out)
{
    const struct application_map_state *state = application ? application->map_state : NULL;
    if (!out || !state || !state->save_requested || !state->entry_generation ||
        state->loading || state->busy || qa_application_startup_pending(application)) return false;
    *out = (qa_application_save_request){state->entry_generation, state->save_request_revision,
        state->fresh_entry, state->authored_autosave};
    return true;
}

bool qa_application_save_request_complete(qa_application *application,
    const qa_application_save_request *request, qa_error *error)
{
    qa_application_save_request current;
    if (!request || !qa_application_save_request_read(application, &current) ||
        request->world_generation != current.world_generation || request->revision != current.revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "save request changed before completion");
    application->map_state->save_requested = false;
    application->map_state->fresh_entry = false;
    application->map_state->authored_autosave = false;
    return true;
}

static char *map_path(const char *input, qa_error *error)
{
    if (input == NULL || input[0] == '\0') {
        application_fail(error, QA_ERROR_ARGUMENT, "map load has no destination");
        return NULL;
    }
    char *name = qa_vfs_normalize_path(input, error);
    if (name == NULL)
        return NULL;
    size_t length = strlen(name);
    bool prefix = strncmp(name, "maps/", 5) == 0;
    bool suffix = length >= 4 && strcmp(name + length - 4, ".bsp") == 0;
    if (length > SIZE_MAX - 10) {
        free(name);
        application_fail(error, QA_ERROR_MEMORY, "map path is too long");
        return NULL;
    }
    char *path = malloc(length + (prefix ? 0u : 5u) + (suffix ? 0u : 4u) + 1u);
    if (path == NULL) {
        free(name);
        application_fail(error, QA_ERROR_MEMORY, "cannot retain map path");
        return NULL;
    }
    size_t offset = 0;
    if (!prefix) { memcpy(path, "maps/", 5); offset = 5; }
    memcpy(path + offset, name, length);
    offset += length;
    if (!suffix) { memcpy(path + offset, ".bsp", 4); offset += 4; }
    path[offset] = '\0';
    free(name);
    return path;
}

static char *start_expression(const char *path, const char *spawn, bool unit,
                               qa_error *error)
{
    if (spawn == NULL)
        spawn = "";
    for (const unsigned char *at = (const unsigned char *)spawn; *at; ++at)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
              (*at >= '0' && *at <= '9') || *at == '_' || *at == '-')) {
            application_fail(error, QA_ERROR_ARGUMENT, "invalid map spawn point");
            return NULL;
        }
    size_t length = strlen(path), spawn_length = strlen(spawn);
    if (length > SIZE_MAX - 4 || spawn_length > SIZE_MAX - length - 4) {
        application_fail(error, QA_ERROR_MEMORY, "map start expression is too long");
        return NULL;
    }
    char *text = malloc(length + spawn_length + 3);
    if (text == NULL) {
        application_fail(error, QA_ERROR_MEMORY, "cannot retain map start expression");
        return NULL;
    }
    size_t offset = 0;
    if (unit) text[offset++] = '*';
    memcpy(text + offset, path, length);
    offset += length;
    if (spawn_length != 0) {
        text[offset++] = '$';
        memcpy(text + offset, spawn, spawn_length);
        offset += spawn_length;
    }
    text[offset] = '\0';
    return text;
}

static bool set_map_draft(qa_launch_draft *draft, const char *path,
    const qa_application_map_request *request, bool restoring, qa_error *error)
{
    char *start=start_expression(path,request->spawn_point,request->new_unit,error);
    if (!start) return false;
    qa_launch_world world=qa_launch_draft_choices(draft)->world;
    world.map=path;
    world.start_command=start;
    world.spawn_point=request->spawn_point?request->spawn_point:"";
    world.explicit_spawn_point=true;
    if (request->geometry) world.geometry=request->geometry;
    if (request->presentation) world.presentation=request->presentation;
    bool ok=qa_launch_set_world(draft,&world,error);
    free(start);
    const qa_launch_choices *choices=qa_launch_draft_choices(draft);
    for (size_t i=0;ok && !restoring && i<choices->provider_count;++i) {
        qa_launch_provider provider=choices->providers[i];
        if (provider.clock.kind!=QA_RULESET_NETQUAKE ||
            provider.options.size!=sizeof(application_q1_original_constructor) || !provider.options.data ||
            memcmp(provider.options.data,application_q1_original_constructor,sizeof(application_q1_original_constructor))) continue;
        provider.options=(qa_bytes){0};
        provider.clock.initial_time_ns=qa_clock_defaults(provider.clock.kind).initial_time_ns;
        ok=qa_launch_set_provider(draft,&provider,error);
    }
    return ok;
}

bool application_campaign_restore_draft(qa_application *previous,
    qa_launch_draft *draft, qa_error *error)
{
    application_campaign_travel *travel=previous?previous->campaign_travel:NULL;
    if (!travel || !travel->visit || !draft ||
        previous->operation!=APPLICATION_PERSISTING ||
        travel->request.target.kind!=QA_TRAVEL_MAP)
        return application_fail(error,QA_ERROR_ARGUMENT,
            "Visited level construction requires its actual current unit travel");
    const qa_application_map_request request={
        .geometry=travel->request.geometry,
        .map=travel->request.target.name,
        .spawn_point=travel->request.target.spawn_point,
        .new_unit=travel->request.target.new_unit,
        .carry_players=travel->request.carry_players,
    };
    char *path=map_path(request.map,error);
    if (!path) return false;
    bool ok=set_map_draft(draft,path,&request,true,error);
    free(path);
    return ok;
}

bool qa_application_load_map(qa_application *application,
                              const qa_application_map_request *request,
                              qa_error *error)
{
    if (request == NULL || !map_safe(application))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map load requires idle shared authorities");
    struct application_map_state *state = map_state(application, error);
    if (state == NULL)
        return false;
    if (state->loading)
        return application_fail(error, QA_ERROR_ARGUMENT, "map load cannot reenter");
    char *path = map_path(request->map, error);
    if (path == NULL)
        return false;
    const qa_launch_snapshot *current = qa_application_launch(application);
    qa_launch_draft *draft = NULL;
    bool ok = current != NULL
                  ? qa_launch_snapshot_draft_copy(current, &draft, error)
                  : state->restart_draft != NULL
                        ? qa_launch_draft_copy(state->restart_draft, &draft, error)
                        : request->geometry != 0 && qa_launch_draft_create(
                            application->catalog, request->geometry, path, &draft, error);
    if (!ok && current == NULL && !state->restart_draft && request->geometry == 0)
        application_fail(error, QA_ERROR_ARGUMENT,
                         "initial map load requires a content preset");
    if (ok) ok=set_map_draft(draft,path,request,false,error);
    if (ok) {
        bool previous_force = application->map_force_reload;
        state->load_revision = state->revision;
        state->loading = true;
        state->load_carry = request->carry_players;
        state->load_new_unit = request->new_unit;
        application->map_force_reload = true;
        ok = qa_application_apply(application, draft, error);
        if (ok) {
            qa_launch_draft_destroy(state->restart_draft);
            state->restart_draft = NULL;
        }
        application->map_force_reload = previous_force;
        if (!ok || !qa_application_startup_pending(application)) {
            application_map_load_finish(application, ok);
            if (ok) ok = application_map_level_entry(application, true, error);
        }
    }
    qa_launch_draft_destroy(draft);
    free(path);
    return ok;
}

static bool queue_travel(qa_application *application,
                         const qa_application_travel_request *request,
                         bool literal_map, qa_error *error)
{
    if (application == NULL || request == NULL || request->expression == NULL ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING ||
        application->destroy_requested || application->finalizing ||
        application->operation == APPLICATION_DESTROYING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel request requires a live application");
    struct application_map_state *state = map_state(application, error);
    if (state == NULL)
        return false;
    if (state->busy || state->loading || state->publication_complete)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel cannot reenter its publication");
    if (state->revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "travel continuation identity is exhausted");
    qa_travel_route route = {0};
    if (literal_map) {
        route.storage = map_path(request->expression, error);
        if (route.storage == NULL)
            return false;
        route.targets = calloc(1, sizeof(*route.targets));
        if (route.targets == NULL) {
            qa_travel_route_free(&route);
            return application_fail(error, QA_ERROR_MEMORY,
                                    "cannot retain single-map travel target");
        }
        route.count = 1;
        route.targets[0] = (qa_travel_target){.kind = QA_TRAVEL_MAP,
                                             .name = route.storage,
                                             .spawn_point = ""};
    } else if (!qa_q2_travel_parse(request->expression, &route, error))
        return false;
    route.targets[0].new_unit |= request->new_unit;
    if (state->pending) {
        const qa_travel_target *old = &state->route.targets[state->cursor];
        const qa_travel_target *next = &route.targets[0];
        bool same = state->provider == request->provider &&
                    state->geometry == request->geometry &&
                    state->carry_players == request->carry_players &&
                    state->complete_campaign == request->complete_campaign &&
                    qa_actor_id_equal(state->cause, request->cause) &&
                    state->has_landmark == (request->landmark != NULL) &&
                    state->route.count - state->cursor == route.count;
        if (same && request->landmark != NULL)
            same = state->landmark.name == request->landmark->name &&
                   same_vector(state->landmark.relative_origin, request->landmark->relative_origin) &&
                   same_vector(state->landmark.relative_velocity, request->landmark->relative_velocity) &&
                   same_vector(state->landmark.relative_view_angles, request->landmark->relative_view_angles);
        for (size_t i = 0; same && i < route.count; ++i) {
            old = &state->route.targets[state->cursor + i];
            next = &route.targets[i];
            same = old->kind == next->kind && old->new_unit == next->new_unit &&
                   strcmp(old->name, next->name) == 0 &&
                   strcmp(old->spawn_point, next->spawn_point) == 0;
        }
        qa_travel_route_free(&route);
        return same || application_fail(error, QA_ERROR_ARGUMENT,
                                       "a different world transition is already pending");
    }
    if (request->landmark != NULL &&
        (!qa_vec_finite(request->landmark->relative_origin) ||
         !qa_vec_finite(request->landmark->relative_velocity) ||
         !qa_vec_finite(request->landmark->relative_view_angles))) {
        qa_travel_route_free(&route);
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel landmark contains a nonfinite pose");
    }
    qa_travel_route_free(&state->route);
    state->route = route;
    state->cursor = 0;
    state->provider = request->provider;
    state->cause = request->cause;
    state->geometry = request->geometry;
    state->carry_players = request->carry_players;
    state->complete_campaign = request->complete_campaign;
    state->has_landmark = request->landmark != NULL;
    state->landmark = request->landmark != NULL ? *request->landmark : (qa_q2_landmark){0};
    state->pending = true;
    ++state->revision;
    return true;
}

bool qa_application_queue_travel(qa_application *application,
                                  const qa_application_travel_request *request,
                                  qa_error *error)
{
    if (application && (application->q3_round_active || application->frame_preparing ||
        application->q3_world_restart || !application_rankings_idle(application)))
        return application_fail(error, QA_ERROR_ARGUMENT, "travel request cannot reenter a source replacement");
    return queue_travel(application, request, false, error);
}

bool qa_application_queue_map_travel(qa_application *application,
                                      const qa_application_travel_request *request,
                                      qa_error *error)
{
    if (application && (application->q3_round_active || application->frame_preparing ||
        application->q3_world_restart || !application_rankings_idle(application)))
        return application_fail(error, QA_ERROR_ARGUMENT, "travel request cannot reenter a source replacement");
    return queue_travel(application, request, true, error);
}

static bool source_queue_travel(qa_application *application,
    const qa_application_travel_request *request, bool literal, qa_error *error)
{
    application_provider *provider = NULL;
    for (size_t i = 0; application && request && i < application->provider_count; ++i)
        if (application->providers[i]->owner == request->provider) {
            provider = application->providers[i];
            break;
        }
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        (application->operation != APPLICATION_IDLE && application->operation != APPLICATION_ADVANCING &&
         application->operation != APPLICATION_CONFIGURING))
        return application_fail(error, QA_ERROR_ARGUMENT, "source travel has no admitted producer");
    return queue_travel(application, request, literal, error);
}

bool application_source_queue_travel(qa_application *application,
    const qa_application_travel_request *request, qa_error *error)
{ return source_queue_travel(application, request, false, error); }

bool application_source_queue_map_travel(qa_application *application,
    const qa_application_travel_request *request, qa_error *error)
{ return source_queue_travel(application, request, true, error); }

bool qa_application_travel_read(const qa_application *application,
                                 qa_application_travel_view *out)
{
    if (application == NULL || out == NULL || application->map_state == NULL ||
        (!application->map_state->pending && !application->map_state->publication_complete))
        return false;
    const struct application_map_state *state = application->map_state;
    *out = (qa_application_travel_view){
        .target = state->route.targets[state->cursor],
        .provider = state->provider, .cause = state->cause,
        .geometry = state->geometry, .revision = state->revision,
        .carry_players = state->carry_players,
        .complete_campaign = state->complete_campaign,
        .has_landmark = state->has_landmark, .landmark = state->landmark};
    return true;
}

static bool nextserver_prepare(qa_application *application,
                                struct application_map_state *state,
                                qa_string_id *out, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_q2_nextserver(&state->route, state->cursor, &text, error))
        return false;
    bool ok = qa_strings_intern(qa_session_strings(application->session),
                                (qa_bytes){text.data, text.size}, out, error);
    qa_buffer_free(&text);
    return ok;
}

static bool travel_current(qa_application *application, uint64_t revision,
                            struct application_map_state **out, qa_error *error)
{
    if (!map_safe(application) || application->map_state == NULL ||
        !application->map_state->pending || application->map_state->busy ||
        application->map_state->revision != revision)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "travel publication needs an idle current request");
    *out = application->map_state;
    return true;
}

bool qa_application_campaign_depart(qa_application *application, uint64_t revision,
    bool *needed, qa_error *error)
{
    struct application_map_state *state=NULL;
    if (!needed || !travel_current(application,revision,&state,error)) return false;
    *needed=false;
    if (application->campaign_travel) {
        if (application->campaign_travel->request.revision!=revision)
            return application_fail(error,QA_ERROR_ARGUMENT,"Campaign departure changed before publication");
        *needed=application->campaign_travel->visit==NULL;
        return true;
    }
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(application));
    const qa_travel_target *target=state->route.targets+state->cursor;
    if (!choices || target->new_unit || target->kind!=QA_TRAVEL_MAP ||
        (state->geometry && state->geometry!=choices->world.geometry)) return true;
    bool retain, reload;
    if (!campaign_q2_policy(application,&retain,&reload,error)) return false;
    if (!retain) return true;
    application_campaign_travel *travel=calloc(1,sizeof(*travel));
    if (!travel) return application_fail(error,QA_ERROR_MEMORY,"Retaining campaign departure");
    bool ok=qa_application_travel_read(application,&travel->request) &&
        application_campaign_location(application,choices->world.geometry,choices->world.map,&travel->source,error) &&
        application_campaign_location(application,choices->world.geometry,target->name,&travel->destination,error) &&
        application_players_campaign_prepare(application,state->carry_players,
            state->has_landmark?&state->landmark:NULL,
            &travel->players,error);
    if (!ok) { application_players_dispose(travel->players); free(travel); return false; }
    application->campaign_travel=travel;
    if (!application_players_campaign_exclude(application,error)) return false;
    *needed=true;
    return true;
}

bool qa_application_campaign_stage(qa_application *application, const qa_save_image *image,
    qa_q2_save_level *original, qa_error *error)
{
    application_campaign_travel *travel=application?application->campaign_travel:NULL;
    const qa_save_metadata *metadata=qa_save_image_metadata(image);
    bool fresh=!image && !original;
    if (!map_safe(application) || (!fresh && (!travel || travel->visit)) ||
        (image && original) ||
        (image && (!metadata || metadata->purpose!=QA_SAVE_TRANSITION ||
            metadata->world_generation!=application->map_revision)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Campaign stage requires its actual departed state image");
    if (fresh) {
        struct application_map_state *state=application->map_state;
        if (!state || !state->pending || state->busy ||
            state->route.targets[state->cursor].kind!=QA_TRAVEL_MAP ||
            (travel && (travel->request.revision!=state->revision || !travel->players ||
                !travel->players->roster || travel->players->roster->map_provider!=
                    application_world_provider(application,QA_ROLE_ENTITIES,""))))
            return application_fail(error,QA_ERROR_ARGUMENT,"Fresh campaign travel requires its untransferred departure carry");
        if (!travel) return true;
        /* Replacing a cached visit preserves its captured departure and player
         * carry. Reentry that moved the carry cannot use this fallback. */
        qa_campaign_unit_checkpoint checkpoint={0};
        bool ok=!travel->visit || qa_campaign_visit_capture(travel->visit,&checkpoint,error);
        qa_campaign_world *departure=NULL;
        for (size_t i=0;ok && i<checkpoint.count;++i)
            if (qa_campaign_location_equal(qa_campaign_world_location(checkpoint.worlds[i]),travel->source))
                departure=checkpoint.worlds[i];
        qa_campaign_visit *visit=NULL;
        if (ok) ok=qa_campaign_unit_stage(application->campaign_unit,travel->destination,false,false,
            departure,&visit,error);
        if (ok) {
            qa_campaign_visit_destroy(travel->visit);
            travel->visit=visit;
        }
        qa_campaign_unit_checkpoint_free(&checkpoint);
        return ok;
    }
    qa_bytes bytes={0}; qa_campaign_world *departure=NULL;
    bool retain, reload;
    bool ok=campaign_q2_policy(application,&retain,&reload,error);
    if (ok && original) {
        const char *map=qa_strings_cstr(qa_session_strings(application->session),travel->source.map);
        if (!map || strcmp(map,original->name) || !original->game.data || !original->game.size)
            ok=application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 departure differs from its actual map");
        qa_q2_save_level *value=ok?malloc(sizeof(*value)):NULL;
        if (ok && !value) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining original Q2 departure");
        if (ok) {
            *value=*original;
            *original=(qa_q2_save_level){0};
            ok=qa_campaign_world_q2_take(travel->source,&value,&departure,error);
        }
        if (value) {qa_q2_save_level_dispose(value);free(value);}
    } else if (ok) ok=qa_save_image_encode(image,&bytes,error) &&
        qa_campaign_world_create(travel->source,bytes,&departure,error);
    if (ok) ok=qa_campaign_unit_stage(application->campaign_unit,travel->destination,false,reload,
        departure,&travel->visit,error);
    qa_campaign_world_release(departure);
    return ok;
}

qa_bytes qa_application_campaign_restore(const qa_application *application)
{
    const application_campaign_travel *travel=application?application->campaign_travel:NULL;
    const qa_campaign_world *world=travel && travel->visit?qa_campaign_visit_restore(travel->visit):NULL;
    return world?qa_campaign_world_bytes(world):(qa_bytes){0};
}
const qa_q2_save_level *application_campaign_q2_level(const qa_application *application)
{
    const application_campaign_travel *travel=application?application->campaign_travel:NULL;
    const qa_campaign_world *world=travel && travel->visit?qa_campaign_visit_restore(travel->visit):NULL;
    return qa_campaign_world_q2(world);
}

bool qa_application_commit_travel(qa_application *application,
                                   uint64_t revision, qa_error *error)
{
    struct application_map_state *state;
    if (!travel_current(application, revision, &state, error))
        return false;
    const qa_travel_target *target = &state->route.targets[state->cursor];
    if (target->kind != QA_TRAVEL_MAP)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "media travel must complete through its presentation owner");
    qa_string_id nextserver;
    if (!nextserver_prepare(application, state, &nextserver, error))
        return false;
    state->busy = true;
    state->load_nextserver = nextserver;
    bool ok = qa_application_load_map(
        application,
        &(qa_application_map_request){.geometry = state->geometry,
                                      .map = target->name,
                                      .spawn_point = target->spawn_point,
                                      .new_unit = target->new_unit,
                                      .carry_players = state->carry_players}, error);
    if (!ok && !state->loading) state->busy = false;
    return ok;
}

void application_map_load_finish(qa_application *application, bool published)
{
    struct application_map_state *state = application ? application->map_state : NULL;
    if (!state) return;
    if (published && qa_application_launch(application)) {
        qa_launch_draft_destroy(state->restart_draft);
        state->restart_draft=NULL;
    }
    if (!state->loading) return;
    state->loading = false;
    if (published && state->revision == state->load_revision) {
        if (state->busy) {
            state->nextserver = state->load_nextserver;
            state->publication_complete = true;
            state->match_finished = false;
        } else {
            qa_travel_route_free(&state->route);
            state->cursor = 0;
            state->nextserver = QA_STRING_NONE;
        }
        state->pending = false;
        state->has_landmark = false;
    }
    state->busy = false;
    state->load_revision = 0;
    state->load_nextserver = QA_STRING_NONE;
}

bool qa_application_travel_publication_read(const qa_application *application,
                                            uint64_t *revision)
{
    const struct application_map_state *state = application ? application->map_state : NULL;
    if (!state || !revision || !state->publication_complete || state->loading || state->busy)
        return false;
    *revision = state->revision;
    return true;
}

bool qa_application_finish_travel_publication(qa_application *application,
                                              uint64_t revision, qa_error *error)
{
    uint64_t current;
    if (!map_safe(application) || !qa_application_travel_publication_read(application, &current) || current != revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "travel completion requires its published world and revision");
    struct application_map_state *state = application->map_state;
    if (!state->match_finished) {
        if (!qa_application_finish_match_travel(application, revision, error)) return false;
        state->match_finished = true;
    }
    if (!qa_application_rankings_start(application, error)) return false;
    state->publication_complete = false;
    state->match_finished = false;
    return true;
}

bool qa_application_complete_travel(qa_application *application,
                                     uint64_t revision, qa_error *error)
{
    struct application_map_state *state;
    if (!travel_current(application, revision, &state, error))
        return false;
    if (state->route.targets[state->cursor].kind == QA_TRAVEL_MAP)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map travel requires world publication");
    if (state->revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY,
                                "travel continuation identity is exhausted");
    qa_string_id nextserver;
    if (!nextserver_prepare(application, state, &nextserver, error))
        return false;
    state->nextserver = nextserver;
    state->pending = ++state->cursor < state->route.count;
    ++state->revision;
    return true;
}

qa_string_id qa_application_nextserver(const qa_application *application)
{
    return application == NULL || application->map_state == NULL
               ? QA_STRING_NONE : application->map_state->nextserver;
}

void application_map_travel_options(const qa_application *application,
                                     bool *carry, bool *unit,
                                     const qa_q2_landmark **landmark)
{
    const struct application_map_state *state = application->map_state;
    const qa_launch_snapshot *current = qa_application_launch(application);
    *carry = current != NULL && qa_launch_snapshot_choices(current)->world.campaign;
    *unit = false;
    *landmark = NULL;
    if (state != NULL && state->loading) {
        *carry = state->load_carry;
        *unit = state->load_new_unit;
        if (state->busy && state->has_landmark) *landmark = &state->landmark;
    }
}

void application_map_publication_dispose(application_publication *publication)
{
    if (publication == NULL)
        return;
    application_players_dispose(publication->players);
    publication->players = NULL;
}

bool application_map_stop_prepare(qa_application *application, qa_error *error)
{
    struct application_map_state *state = application->map_state;
    if (!state) state=map_state(application,error);
    if (!state) return false;
    if (state->restart_draft) return true;
    return qa_launch_snapshot_draft_copy(qa_application_launch(application), &state->restart_draft, error);
}

bool qa_application_server_restart_pending(const qa_application *application)
{ return application && application->map_state && application->map_state->restart_draft; }

void application_map_dispose(qa_application *application)
{
    if (application == NULL)
        return;
    application_players_close(application);
    campaign_travel_dispose(application);
    qa_campaign_unit_destroy(application->campaign_unit);
    application->campaign_unit=NULL;
    if (application->map_state == NULL) return;
    qa_launch_draft_destroy(application->map_state->restart_draft);
    qa_travel_route_free(&application->map_state->route);
    free(application->map_state);
    application->map_state = NULL;
}

bool application_map_server_command(application_provider *provider,
                                      qa_string_id command, qa_error *error)
{
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "authored map command has no live source producer");
    const char *text = qa_strings_cstr(qa_session_strings(provider->application->session), command);
    if (text == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "authored server command has no text");
    while (*text == ' ' || *text == '\t') ++text;
    const char *start = text;
    while (*text && *text != ' ' && *text != '\t' && *text != '\n' && *text != '\r') ++text;
    size_t length = (size_t)(text - start);
    bool map = length == 3 && !memcmp(start, "map", 3);
    bool gamemap = length == 7 && !memcmp(start, "gamemap", 7);
    bool changelevel = length == 11 && !memcmp(start, "changelevel", 11);
    if (!map && !gamemap && !changelevel)
        return true; /* The command consumer also receives the original event. */
    while (*text == ' ' || *text == '\t') ++text;
    bool quoted = *text == '"';
    if (quoted) ++text;
    const char *destination = text;
    while (*text && (quoted ? *text != '"' : *text != ' ' && *text != '\t' && *text != '\n' && *text != '\r')) ++text;
    size_t size = (size_t)(text - destination);
    if (!size || (quoted && *text != '"'))
        return application_fail(error, QA_ERROR_FORMAT, "authored map command has no valid destination");
    if (quoted) ++text;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    if (*text)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "authored map command contains additional commands");
    char *expression = malloc(size + 1);
    if (expression == NULL)
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain authored map command");
    memcpy(expression, destination, size);
    expression[size] = '\0';
    if (size > 4 && !strcmp(expression + size - 4, ".bsp")) expression[size - 4] = '\0';
    bool ok = queue_travel(provider->application,
        &(qa_application_travel_request){.provider = provider->owner,
            .expression = expression, .new_unit = map, .carry_players = !map,
            .complete_campaign = changelevel}, false, error);
    free(expression);
    return ok;
}

static bool map_geometry_field(qa_source_save_io *io, qa_product_id *geometry, qa_catalog *catalog)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    char *product = NULL;
    if (!reading && *geometry != 0) {
        const qa_product *selected = qa_catalog_product(catalog, *geometry);
        if (!selected || !selected->identity) {
            qa_error_set(io->error, QA_ERROR_FORMAT, 0, "map continuation product has no stable identity");
            io->failed = true; return false;
        }
        product = (char *)selected->identity;
    }
    bool ok = qa_source_save_owned_text(io, &product);
    if (reading) {
        *geometry = 0;
        if (ok && product != NULL) {
            const qa_product *selected = qa_catalog_find(catalog, product);
            if (!selected || strcmp(selected->identity, product)) {
                qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "map continuation product is absent from candidate catalog");
                io->failed = true; ok = false;
            } else *geometry = selected->id;
        }
        free(product);
    }
    return ok;
}

static bool map_checkpoint_fields(qa_source_save_io *io, struct application_map_state *state,
                                   qa_product_id *geometry, qa_catalog *catalog)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t maximum = reading ? io->input.size / 4 : SIZE_MAX / sizeof(*state->route.targets);
    if (maximum > SIZE_MAX / sizeof(*state->route.targets)) maximum = SIZE_MAX / sizeof(*state->route.targets);
    if (!qa_source_save_count(io, &state->route.count, maximum) ||
        !qa_source_save_count(io, &state->cursor, state->route.count) ||
        !qa_source_save_u64(io, &state->revision) ||
        !qa_source_save_string(io, &state->provider) || !qa_source_save_actor(io, &state->cause) ||
        !map_geometry_field(io, geometry, catalog) || !qa_source_save_string(io, &state->nextserver) ||
        !qa_source_save_bool(io, &state->pending) || !qa_source_save_bool(io, &state->has_landmark) ||
        !qa_source_save_bool(io, &state->publication_complete) || !qa_source_save_bool(io, &state->match_finished) ||
        !qa_source_save_bool(io, &state->carry_players) || !qa_source_save_bool(io, &state->complete_campaign) ||
        !qa_source_save_bool(io, &state->load_carry) || !qa_source_save_bool(io, &state->load_new_unit) ||
        !qa_source_save_string(io, &state->landmark.name) ||
        !qa_source_save_vec3(io, &state->landmark.relative_origin) ||
        !qa_source_save_vec3(io, &state->landmark.relative_velocity) ||
        !qa_source_save_vec3(io, &state->landmark.relative_view_angles)) return false;
    if (reading) {
        state->route.targets = state->route.count ? calloc(state->route.count, sizeof(*state->route.targets)) : NULL;
        if (state->route.count && !state->route.targets) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating authored map continuation targets");
            io->failed = true; return false;
        }
    }
    for (size_t i = 0; i < state->route.count; ++i) {
        qa_travel_target *target = &state->route.targets[i];
        uint32_t kind = reading ? 0 : (uint32_t)target->kind;
        char *name = reading ? NULL : (char *)target->name;
        char *spawn = reading ? NULL : (char *)target->spawn_point;
        bool decoded = qa_source_save_u32(io, &kind) && qa_source_save_bool(io, &target->new_unit) &&
            qa_source_save_owned_text(io, &name) && qa_source_save_owned_text(io, &spawn);
        if (reading) { target->name = name; target->spawn_point = spawn; }
        if (!decoded) return false;
        if (kind > QA_TRAVEL_DEMO || !target->name || !*target->name || !target->spawn_point) {
            qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "invalid authored map continuation target");
            io->failed = true; return false;
        }
        if (reading) target->kind = (qa_travel_kind)kind;
    }
    if (((state->pending || state->publication_complete) && (!state->revision || state->cursor >= state->route.count)) ||
        (state->publication_complete && (state->pending || state->has_landmark ||
            state->route.targets[state->cursor].kind != QA_TRAVEL_MAP)) ||
        (state->match_finished && !state->publication_complete) ||
        !qa_vec_finite(state->landmark.relative_origin) ||
        !qa_vec_finite(state->landmark.relative_velocity) || !qa_vec_finite(state->landmark.relative_view_angles)) {
        qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "map continuation cursor or landmark disagrees");
        io->failed = true; return false;
    }
    return true;
}

bool application_map_checkpoint_capture(qa_application *application, bool departed, qa_buffer *out,
                                         qa_error *error)
{
    if (!application || !out || !application->session || !qa_session_safe(application->session) ||
        application->publication_started || qa_application_server_restart_pending(application) || (application->map_state &&
            (application->map_state->busy || application->map_state->loading)))
        return application_fail(error, QA_ERROR_ARGUMENT, "map continuation capture requires a committed safe point");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, application->session, error)) return false;
    uint8_t signature[] = {'Q','A','M','T'};
    bool present = !departed && application->map_state != NULL;
    bool ok = qa_source_save_bytes(&io, signature, sizeof(signature)) &&
        qa_source_save_bool(&io, &present);
    if (ok && present) {
        const struct application_map_state *live = application->map_state;
        if (live->route.count && !live->route.targets)
            ok = application_fail(error, QA_ERROR_FORMAT, "map continuation has no retained target storage");
        else {
            struct application_map_state retained = *live;
            uint64_t revision = live->revision;
            ok = map_checkpoint_fields(&io, &retained, &retained.geometry, application->catalog);
            if (ok && (live != application->map_state || revision != live->revision || live->busy || live->loading))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "map continuation changed during capture");
        }
    }
    if (ok) ok = application_campaign_fields(&io,application,departed) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

static bool own_map_route(qa_travel_route *route, const qa_travel_route *source,
    qa_error *error)
{
    size_t size=0;
    for (size_t i=0;i<source->count;++i) {
        size_t name=strlen(source->targets[i].name)+1, spawn=strlen(source->targets[i].spawn_point)+1;
        if (name>SIZE_MAX-size || spawn>SIZE_MAX-size-name)
            return application_fail(error,QA_ERROR_MEMORY,"Authored map continuation text extent exhausted");
        size+=name+spawn;
    }
    bool decoded=route==source;
    if (source->count>SIZE_MAX/sizeof(*source->targets))
        return application_fail(error,QA_ERROR_MEMORY,"Map continuation target extent exhausted");
    qa_travel_target *targets=decoded?route->targets:
        source->count?malloc(source->count*sizeof(*targets)):NULL;
    if (!decoded && source->count && !targets)
        return application_fail(error,QA_ERROR_MEMORY,"Retaining map continuation targets");
    char *storage=size?malloc(size):NULL;
    if (size && !storage) {
        if (!decoded) free(targets);
        return application_fail(error,QA_ERROR_MEMORY,"Retaining map continuation text");
    }
    size_t offset=0;
    for (size_t i=0;i<source->count;++i) {
        const char *name=source->targets[i].name, *spawn=source->targets[i].spawn_point;
        targets[i]=source->targets[i];
        size_t n=strlen(name)+1, m=strlen(spawn)+1;
        memcpy(storage+offset,name,n); targets[i].name=storage+offset; offset+=n;
        memcpy(storage+offset,spawn,m); targets[i].spawn_point=storage+offset; offset+=m;
        if (decoded) { free((void *)name); free((void *)spawn); }
    }
    size_t count=source->count;
    if (!decoded) qa_travel_route_free(route);
    *route=(qa_travel_route){.targets=targets,.count=count,.storage=storage};
    return true;
}

static bool campaign_relocate(qa_application *candidate, const qa_application *previous,
    qa_campaign_location *location, qa_error *error)
{
    qa_strings *from=qa_session_strings(previous->session), *to=qa_session_strings(candidate->session);
    return qa_strings_intern(to,qa_strings_text(from,location->content),&location->content,error) &&
        qa_strings_intern(to,qa_strings_text(from,location->map),&location->map,error);
}

static bool campaign_route_copy(const struct application_map_state *source,
    struct application_map_state *destination, qa_error *error)
{
    if (!own_map_route(&destination->route,&source->route,error)) return false;
    destination->cursor=source->cursor;
    destination->revision=source->revision;
    destination->carry_players=source->carry_players;
    destination->complete_campaign=source->complete_campaign;
    destination->pending=false; destination->has_landmark=false;
    destination->publication_complete=true; destination->match_finished=false;
    return true;
}

bool qa_application_campaign_reenter(qa_application *candidate, qa_application *previous,
    qa_error *error)
{
    application_campaign_travel *travel=previous?previous->campaign_travel:NULL;
    if (!candidate || !travel || !travel->visit || !travel->players || !map_safe(candidate) ||
        previous->map_revision==UINT64_MAX || !previous->map_state)
        return application_fail(error,QA_ERROR_ARGUMENT,"Campaign reentry requires its retained visit and restored world");
    qa_campaign_unit_checkpoint checkpoint={0};
    bool ok=qa_campaign_visit_capture(travel->visit,&checkpoint,error) &&
        campaign_relocate(candidate,previous,&checkpoint.current,error);
    for (size_t i=0;ok && i<checkpoint.count;++i) {
        qa_campaign_location location=qa_campaign_world_location(checkpoint.worlds[i]);
        qa_campaign_world *world=NULL;
        ok=campaign_relocate(candidate,previous,&location,error) &&
            qa_campaign_world_relocate(checkpoint.worlds[i],location,&world,error);
        if (ok) { qa_campaign_world_release(checkpoint.worlds[i]); checkpoint.worlds[i]=world; }
    }
    if (ok) ok=qa_campaign_unit_restore(candidate->campaign_unit,&checkpoint,error);
    qa_campaign_unit_checkpoint_free(&checkpoint);
    for (size_t i=0;ok && i<previous->provider_count;++i) {
        const application_provider *source=previous->providers[i];
        for (size_t j=0;j<candidate->provider_count;++j) {
            application_provider *next=candidate->providers[j];
            if (strcmp(source->launch->selection.instance,next->launch->selection.instance)) continue;
            next->q1_server_flags=source->q1_server_flags;
            next->q2_server_flags=source->q2_server_flags;
        }
    }
    if (ok) ok=application_players_campaign_reenter(candidate,previous,travel->players,
        travel->request.target.spawn_point,error);
    struct application_map_state *state=ok?map_state(candidate,error):NULL;
    if (ok) ok=state && campaign_route_copy(previous->map_state,state,error);
    if (ok) {
        state->geometry=qa_launch_snapshot_choices(qa_application_launch(candidate))->world.geometry;
        state->provider=0; state->cause=(qa_actor_id){0};
        for (size_t i=0;i<previous->provider_count;++i) {
            const application_provider *source=previous->providers[i];
            if (source->owner!=previous->map_state->provider) continue;
            for (size_t j=0;j<candidate->provider_count;++j)
                if (!strcmp(source->launch->selection.instance,candidate->providers[j]->launch->selection.instance))
                    state->provider=candidate->providers[j]->owner;
        }
        candidate->map_revision=previous->map_revision+1;
        state->entry_generation=0;
        ok=nextserver_prepare(candidate,state,&state->nextserver,error) &&
            application_map_level_entry(candidate,false,error);
    }
    return ok;
}

bool application_map_checkpoint_restore(qa_application *candidate, qa_bytes bytes,
                                         qa_error *error)
{
    if (!candidate || !candidate->session || !candidate->catalog || candidate->map_state != NULL ||
        !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "map continuation restore requires an isolated empty candidate");
    qa_source_save_io io;
    if (!qa_source_save_reader(&io, candidate->session, bytes, error)) return false;
    uint8_t signature[4]; bool present = false;
    bool ok = qa_source_save_bytes(&io, signature, sizeof(signature)) && !memcmp(signature, "QAMT", 4) &&
        qa_source_save_bool(&io, &present);
    struct application_map_state *state = NULL;
    if (ok && present) {
        state = calloc(1, sizeof(*state));
        if (!state) ok = application_fail(error, QA_ERROR_MEMORY, "allocating isolated map continuation");
        else ok = map_checkpoint_fields(&io, state, &state->geometry, candidate->catalog);
    }
    if (ok) ok = application_campaign_fields(&io,candidate,false) && qa_source_save_finish(&io, NULL);
    if (ok && state) ok = own_map_route(&state->route, &state->route, error);
    qa_source_save_dispose(&io);
    if (!ok) {
        if (state) {
            for (size_t i = 0; state->route.targets && i < state->route.count; ++i) {
                free((void *)state->route.targets[i].name);
                free((void *)state->route.targets[i].spawn_point);
            }
            qa_travel_route_free(&state->route); free(state);
        }
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "invalid map continuation schema or extent");
        return false;
    }
    if (state) state->entry_generation = candidate->map_revision;
    candidate->map_state = state;
    return true;
}
