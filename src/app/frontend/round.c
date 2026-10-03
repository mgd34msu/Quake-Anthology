#include "remote_q1_effects.h"
#include "internal.h"
#include "round.h"
#include "network_q3_restart.h"
#include "source_restore.h"
#include "native_q2_save.h"
#include "capture.h"
#include "qa/scene_world_save.h"

typedef enum frontend_round_phase {
    FRONTEND_ROUND_PREPARED, FRONTEND_ROUND_BEGUN, FRONTEND_ROUND_BOUND,
    FRONTEND_ROUND_FINISHED
} frontend_round_phase;
typedef struct frontend_round_local {
    frontend_seat *seat;
    qa_input_seat *input;
    qa_seat_console *console;
    qa_ui *ui;
    qa_hud *hud;
    qa_hud_wheel *wheel;
    qa_ui_library *library;
    qa_ui_mods *mods;
    qa_ui_rankings *rankings;
    qa_ui_llm *assistance;
    qa_movement_kind movement;
    bool bound;
} frontend_round_local;
typedef struct frontend_round_group {
    frontend_source_group_view source;
    frontend_source_role_identity *roles;
    size_t role_count;
} frontend_round_group;
struct qa_application_q3_round_cut {
    qa_frontend *frontend;
    qa_application *application;
    qa_actor_owner source;
    const qa_application_q3_round_client *clients;
    size_t count;
    qa_network_q3_round *network;
    frontend_round_group *groups;
    size_t group_count;
    frontend_round_local locals[4];
    qa_world *world;
    qa_collision_geometry *geometry;
    qa_resource *map_resource, *render_resource;
    qa_scene_world *scene_world;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_audio_bank *sounds;
    qa_audio_engine *audio;
    qa_audio_device *device;
    qa_display *display;
    qa_cpu_renderer *cpu;
    qa_gl_renderer *gl;
    qa_input_platform *input;
    qa_input_console *input_commands;
    qa_vfs *ui_mounts;
    qa_scene_resources *ui_images;
    qa_font_library *fonts;
    qa_material_order *order;
    uint64_t configuration, map_revision, render_revision, time, frame;
    bool queued[64], resolved[64];
    frontend_round_phase phase;
};
bool frontend_round_audio_pending(const qa_frontend *frontend)
{
    return frontend && frontend->round && frontend->round->phase != FRONTEND_ROUND_PREPARED;
}

static bool same_command(const qa_q3_usercmd *a, const qa_q3_usercmd *b)
{
    return a->serverTime == b->serverTime && a->angles[0] == b->angles[0] &&
        a->angles[1] == b->angles[1] && a->angles[2] == b->angles[2] &&
        a->forwardmove == b->forwardmove && a->rightmove == b->rightmove &&
        a->upmove == b->upmove && a->buttons == b->buttons && a->weapon == b->weapon;
}

static bool source_groups_same(const qa_application_q3_round_cut *cut, qa_error *error)
{
    qa_frontend *f = cut->frontend;
    if (frontend_source_group_count(f) != cut->group_count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round changed its installed source lease inventory");
    for (size_t i = 0; i < cut->group_count; ++i) {
        frontend_source_group_view now;
        const frontend_round_group *group = &cut->groups[i];
        const frontend_source_group_view *old = &group->source;
        if (!frontend_source_group_read(f, i, &now) || now.owner != old->owner || now.seat != old->seat || now.launch_seat!=old->launch_seat ||
            now.identity != old->identity || memcmp(now.roles, old->roles, sizeof(now.roles)) ||
            now.source_files != old->source_files || now.mounts != old->mounts || now.images != old->images ||
            now.materials != old->materials || now.fonts != old->fonts || now.sounds != old->sounds ||
            now.movies != old->movies || now.keys != old->keys || now.assets != old->assets ||
            now.presentation != old->presentation)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round replaced an installed source frontend owner");
        for (size_t j = 0; j < group->role_count; ++j) {
            frontend_source_role_identity role;
            if (!frontend_source_group_role_read(f, i, j, &role) ||
                role.role != group->roles[j].role || role.service_owner != group->roles[j].service_owner)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round replaced an installed source role lease");
        }
    }
    return true;
}
static bool cut_ready(const qa_application_q3_round_cut *cut, qa_error *error)
{
    qa_frontend *f = cut ? cut->frontend : NULL;
    if (f && !frontend_native_q2_q3_round_ready(f, error)) return false;
    qa_application_map_view map;
    qa_application_state state = f ? qa_application_get_state(f->application) : QA_APPLICATION_FAULTED;
    if (!f || f->round != cut || f->application != cut->application || f->stepping || !frontend_owners_idle(f) ||
        (state != QA_APPLICATION_READY && state != QA_APPLICATION_RUNNING) ||
        !qa_session_safe(qa_application_session(f->application)) ||
        qa_application_world(f->application) != cut->world ||
        qa_world_geometry(cut->world) != cut->geometry ||
        !qa_application_map_read(f->application, &map) || map.resource != cut->map_resource ||
        map.revision != cut->map_revision ||
        qa_application_configuration_generation(f->application) != cut->configuration ||
        f->map_resource != cut->render_resource || f->map_revision != cut->render_revision ||
        f->scene_world != cut->scene_world || f->mounts != cut->mounts || f->images != cut->images ||
        f->materials != cut->materials || f->sounds != cut->sounds || f->audio != cut->audio ||
        f->device != cut->device || f->display != cut->display || f->cpu != cut->cpu ||
        f->gl != cut->gl || f->input != cut->input || f->input_commands != cut->input_commands ||
        f->ui_mounts != cut->ui_mounts || f->ui_images != cut->ui_images ||
        f->fonts != cut->fonts || f->order != cut->order ||
        f->time_ns != cut->time || f->frame_number != cut->frame ||
        (f->scene_world && !qa_scene_world_idle(f->scene_world)) ||
        !frontend_native_q2_callbacks_idle(f) || !frontend_tools_world_change_ready(f, error) ||
        !frontend_source_round_ready(f, cut->source, error) || !source_groups_same(cut, error) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio, error)) ||
        (f->device && !qa_audio_device_round_ready(f->device, error)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round no longer owns its exact idle retained frontend cut");
    for (unsigned i = 0; i < f->options.seats && !f->options.dedicated; ++i)
        if (f->seats + i != cut->locals[i].seat || !cut->locals[i].input ||
            f->seats[i].input != cut->locals[i].input || !cut->locals[i].console ||
            f->seats[i].console != cut->locals[i].console || !cut->locals[i].ui ||
            f->seats[i].ui != cut->locals[i].ui || !cut->locals[i].hud ||
            f->seats[i].hud != cut->locals[i].hud || f->seats[i].wheel != cut->locals[i].wheel ||
            f->seats[i].library != cut->locals[i].library || f->seats[i].mods != cut->locals[i].mods ||
            f->seats[i].rankings != cut->locals[i].rankings || f->seats[i].assistance != cut->locals[i].assistance ||
            !qa_hud_wheel_round_ready(f->seats[i].wheel))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round lost its actual local input or controller owner");
    return true;
}
static bool local_inventory(qa_application_q3_round_cut *cut, qa_error *error)
{
    qa_frontend *f = cut->frontend;
    for (unsigned i = 0; i < f->options.seats && !f->options.dedicated; ++i) {
        frontend_seat *seat = f->seats + i;
        cut->locals[i].seat = seat;
        cut->locals[i].input = seat->input; cut->locals[i].console = seat->console;
        cut->locals[i].ui = seat->ui; cut->locals[i].hud = seat->hud; cut->locals[i].wheel = seat->wheel;
        cut->locals[i].library = seat->library; cut->locals[i].mods = seat->mods;
        cut->locals[i].rankings = seat->rankings; cut->locals[i].assistance = seat->assistance;
        qa_actor_id actor; uint32_t launch_seat;
        if (!frontend_seat_launch_id_read(f,i,&launch_seat) ||
            !qa_application_player_actor(f->application,launch_seat,&actor)) continue;
        const qa_application_q3_round_client *row = NULL;
        for (size_t j = 0; j < cut->count; ++j)
            if (!cut->clients[j].remote && !cut->clients[j].bot && cut->clients[j].seat == launch_seat) {
                if (row) return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round repeats a local input seat");
                row = cut->clients + j;
            }
        qa_application_control_view control;
        uint32_t source_slot;
        if (!row || !qa_actor_id_equal(row->previous_actor, actor) ||
            !qa_application_q3_source_client_slot(f->application, cut->source, actor, &source_slot, error) ||
            source_slot != row->source_slot ||
            !qa_application_control_read(f->application, actor, &control))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round local seat lacks its actual source and control admission");
        cut->locals[i].movement = control.profile.kind;
        cut->locals[i].bound = seat->builder.kind == control.profile.kind && qa_actor_id_equal(seat->actor, actor);
    }
    for (size_t i = 0; i < cut->count && !f->options.dedicated; ++i) {
        const qa_application_q3_round_client *row = cut->clients + i;
        qa_actor_id actor; uint32_t ordinal;
        if (!row->remote && !row->bot && (!frontend_seat_ordinal_read(f,row->seat,&ordinal) ||
            !qa_application_player_actor(f->application, row->seat, &actor) ||
            !qa_actor_id_equal(actor, row->previous_actor)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round source local has no installed command owner");
    }
    return true;
}
static void dispose(qa_application_q3_round_cut *cut)
{
    if (!cut) return;
    frontend_network_q3_round_dispose(cut->network);
    if (cut->frontend && cut->frontend->round == cut) cut->frontend->round = NULL;
    if (cut->groups) for (size_t i = 0; i < cut->group_count; ++i) free(cut->groups[i].roles);
    free(cut->groups); free(cut);
}
static bool prepare(void *context, qa_application *app, qa_actor_owner source,
    const qa_application_q3_round_client *clients, size_t count,
    qa_application_q3_round_cut **out, qa_error *error)
{
    qa_frontend *f = context;
    qa_application_map_view map;
    if (!f || f->application != app || !source || !out || *out || f->round || f->stepping || !frontend_owners_idle(f) ||
        count > 64 || (count && !clients) || !qa_application_map_read(app, &map))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round prepare requires actual installed owners and empty cut output");
    qa_application_q3_round_cut *cut = calloc(1, sizeof(*cut));
    if (!cut) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Q3 frontend round cut");
    *cut = (qa_application_q3_round_cut){.frontend = f, .application = app, .source = source,
        .clients = clients, .count = count, .world = qa_application_world(app),
        .map_resource = map.resource, .map_revision = map.revision, .render_resource = f->map_resource,
        .render_revision = f->map_revision, .scene_world = f->scene_world, .mounts = f->mounts,
        .images = f->images, .materials = f->materials, .sounds = f->sounds, .audio = f->audio,
        .device = f->device, .configuration = qa_application_configuration_generation(app),
        .display = f->display, .cpu = f->cpu, .gl = f->gl, .input = f->input,
        .input_commands = f->input_commands, .ui_mounts = f->ui_mounts, .ui_images = f->ui_images,
        .fonts = f->fonts, .order = f->order,
        .time = f->time_ns, .frame = f->frame_number, .group_count = frontend_source_group_count(f)};
    cut->geometry = qa_world_geometry(cut->world);
    if (cut->group_count > SIZE_MAX / sizeof(*cut->groups)) {
        dispose(cut); return frontend_fail(error, QA_ERROR_MEMORY, "Q3 frontend source inventory overflows");
    }
    cut->groups = cut->group_count ? calloc(cut->group_count, sizeof(*cut->groups)) : NULL;
    if (cut->group_count && !cut->groups) {
        dispose(cut); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 source frontend leases");
    }
    bool ok = true;
    for (size_t i = 0; ok && i < cut->group_count; ++i) {
        frontend_round_group *group = cut->groups + i;
        ok = frontend_source_group_read(f, i, &group->source);
        if (!ok) break;
        group->role_count = (size_t)group->source.roles[QA_QVM_CGAME] + group->source.roles[QA_QVM_UI];
        if (group->role_count > SIZE_MAX / sizeof(*group->roles)) {
            ok = frontend_fail(error, QA_ERROR_MEMORY, "Q3 frontend role inventory overflows"); break;
        }
        group->roles = group->role_count ? calloc(group->role_count, sizeof(*group->roles)) : NULL;
        if (group->role_count && !group->roles) {
            ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 source role leases"); break;
        }
        for (size_t j = 0; ok && j < group->role_count; ++j)
            ok = frontend_source_group_role_read(f, i, j, group->roles + j);
    }
    f->round = cut;
    ok = ok && local_inventory(cut, error) && cut_ready(cut, error) &&
        frontend_network_q3_round_prepare(f, &cut->network, error);
    const qa_network_q3_round_client *peers = NULL;
    size_t peer_count = frontend_network_q3_round_clients(cut->network, &peers), remote_count = 0;
    if (ok && cut->network && frontend_network_q3_round_source_owner(cut->network) != source)
        ok = frontend_fail(error, QA_ERROR_FORMAT, "Q3 round frontend and retained network name different sources");
    for (size_t i = 0; ok && i < count; ++i) {
        if (!clients[i].remote) continue;
        ++remote_count; bool found = false;
        for (size_t j = 0; j < peer_count; ++j)
            if (qa_net_client_id_equal(clients[i].remote_client, peers[j].client) &&
                clients[i].remote_seat.owner == peers[j].seat.owner && clients[i].remote_seat.index == peers[j].seat.index &&
                clients[i].source_slot == peers[j].source_slot &&
                qa_actor_id_equal(clients[i].previous_actor, peers[j].previous_actor) &&
                clients[i].userinfo && peers[j].userinfo && !strcmp(clients[i].userinfo, peers[j].userinfo) &&
                same_command(&clients[i].last_command, &peers[j].last_command)) found = true;
        if (!found) ok = frontend_fail(error, QA_ERROR_FORMAT, "Q3 round remote source row has no actual retained transport");
    }
    if (ok && peer_count != remote_count) ok = frontend_fail(error, QA_ERROR_FORMAT, "Q3 round has an unrepresented retained transport");
    if (!ok) { dispose(cut); return false; }
    *out = cut; return true;
}
bool frontend_round_clear_events(qa_frontend *frontend, qa_error *error)
{
    qa_application_q3_round_cut *cut = frontend ? frontend->round : NULL;
    if (!cut || cut->frontend != frontend || cut->phase == FRONTEND_ROUND_FINISHED)
        return frontend_fail(error, QA_ERROR_ARGUMENT,
                             "Q3 event consumption requires its actual retained frontend round");
    if (!cut_ready(cut, error)) return false;
    return qa_application_q3_round_clear_events(cut->application, cut->source, error);
}

bool frontend_events_flush(qa_frontend *f, qa_error *error)
{
    if (!frontend_network_publish(f, error)) return false;
    if (!f->options.dedicated && (!frontend_map_events(f, error) ||
        !frontend_particle_events(f, error) || !frontend_player_events(f, error))) return false;
    return frontend_events(f, error);
}
static bool deliver(qa_application_q3_round_cut *cut, qa_error *error)
{
    if (!cut_ready(cut, error) || cut->phase == FRONTEND_ROUND_FINISHED) return false;
    return frontend_events_flush(cut->frontend, error);
}
static bool network_owned(qa_application_q3_round_cut *cut, bool *out, qa_error *error)
{
    if (!out || !cut_ready(cut, error) || cut->phase != FRONTEND_ROUND_PREPARED)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 network ownership requires the actual prepared frontend cut");
    *out = cut->network != NULL;
    return true;
}
static bool begin(qa_application_q3_round_cut *cut,
    qa_application_q3_round_mutation_fn mark, void *mark_context, qa_error *error)
{
    if (!mark || !cut_ready(cut, error) || cut->phase != FRONTEND_ROUND_PREPARED)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 frontend round already began or has no mutation owner");
    if (cut->network) {
        if (!frontend_network_q3_round_refresh(cut->network, error) ||
            !frontend_network_q3_round_begin(cut->network, mark, mark_context, error)) return false;
    } else mark(mark_context);
    cut->phase = FRONTEND_ROUND_BEGUN;
    qa_frontend *f = cut->frontend;
    qa_scene_frame_reset(&f->frame, f->frame_number);
    if (!frontend_source_reset_round(f, cut->source, error)) return false;
    frontend_particle_reset_round(f); frontend_event_reset_round(f);
    for (unsigned i = 0; i < f->options.seats && !f->options.dedicated; ++i) {
        if (!qa_ui_rankings_reset_binding(f->seats[i].rankings, error)) return false;
        frontend_player_retire(f->seats + i);
    }
    frontend_audio_retire_round_aliases(f);
    f->silent_audio_remainder = 0;
    for (size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(f,i);
        if (!row || !remote_q1_effects_audio_detach(row,error)) return false;
    }
    return (!f->audio || qa_audio_engine_reset_round(f->audio, error)) &&
        (!f->device || qa_audio_device_reset_round(f->device, error));
}
static bool bind(qa_application_q3_round_cut *cut, qa_error *error)
{
    if (!cut_ready(cut, error) || cut->phase != FRONTEND_ROUND_BEGUN)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 frontend bind requires the real reset source boundary");
    if (!frontend_network_q3_round_bind(cut->network, error)) return false;
    frontend_audio_retire_dead_aliases(cut->frontend);
    cut->phase = FRONTEND_ROUND_BOUND; return true;
}
static bool client_index(qa_application_q3_round_cut *cut,
    const qa_application_q3_round_client *client, size_t *index, qa_error *error)
{
    if (!cut_ready(cut, error) || cut->phase != FRONTEND_ROUND_BOUND)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round client is outside its actual bound frontend cut");
    for (size_t i = 0; i < cut->count; ++i)
        if (client == cut->clients + i && !cut->resolved[i]) { *index = i; return true; }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 round client is absent or already resolved");
}
static bool queue_client(qa_application_q3_round_cut *cut,
    const qa_application_q3_round_client *client, qa_error *error)
{
    size_t i;
    if (!client_index(cut, client, &i, error) || cut->queued[i]) return false;
    if (client->remote && !frontend_network_q3_round_queue_client(cut->network, client->remote_client, error)) return false;
    cut->queued[i] = true; return true;
}
static bool admit_client(qa_application_q3_round_cut *cut,
    const qa_application_q3_round_client *client, qa_actor_id actor, qa_error *error)
{
    size_t i;
    if (!client_index(cut, client, &i, error) || !cut->queued[i]) return false;
    uint32_t source_slot;
    if (!qa_application_q3_source_client_slot(cut->application, cut->source, actor, &source_slot, error) ||
        source_slot != client->source_slot ||
        actor.registry != client->previous_actor.registry || qa_actor_id_equal(actor, client->previous_actor))
        return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round admission lacks its real fresh source actor generation");
    if (client->remote) {
        if (!frontend_network_q3_round_activate_client(cut->network, client->remote_client, error)) return false;
    } else if (!client->bot && !cut->frontend->options.dedicated) {
        uint32_t ordinal;
        if (!frontend_seat_ordinal_read(cut->frontend,client->seat,&ordinal))
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round local client leaves its installed input seats");
        frontend_round_local *local = cut->locals + ordinal;
        frontend_seat *seat = local->seat; qa_actor_id actual; qa_application_control_view control;
        if (!qa_application_player_actor(cut->application, client->seat, &actual) ||
            !qa_actor_id_equal(actual, actor) || !qa_application_control_read(cut->application, actor, &control) ||
            control.profile.kind != local->movement)
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 round changed its actual local command execution profile");
        if ((!local->bound || local->movement != QA_MOVEMENT_Q3) &&
            !qa_input_command_angles(&seat->builder, control.command_angles, error)) return false;
        seat->builder.kind = control.profile.kind; seat->actor = actor;
    }
    cut->resolved[i] = true; return true;
}
static bool reject_client(qa_application_q3_round_cut *cut,
    const qa_application_q3_round_client *client, const char *reason, qa_error *error)
{
    size_t i;
    if (!reason || !client_index(cut, client, &i, error) || !cut->queued[i]) return false;
    if (client->remote) {
        if (!frontend_network_q3_round_reject_client(cut->network, client->remote_client, reason, error)) return false;
    } else if (!client->bot && !cut->frontend->options.dedicated)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 source rejected its retained local input client");
    cut->resolved[i] = true; return true;
}
static bool finish(qa_application_q3_round_cut *cut, qa_error *error)
{
    if (!cut_ready(cut, error) || cut->phase != FRONTEND_ROUND_BOUND) return false;
    for (size_t i = 0; i < cut->count; ++i)
        if (!cut->resolved[i]) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 frontend round left an unresolved real client");
    if (!frontend_network_publish(cut->frontend, error) ||
        !frontend_network_q3_round_finish(cut->network, error)) return false;
    cut->phase = FRONTEND_ROUND_FINISHED; return true;
}
const qa_application_q3_round_services *frontend_q3_round_services(void)
{
    static const qa_application_q3_round_services services = {
        .prepare = prepare, .deliver = deliver, .network_owned = network_owned,
        .begin = begin, .bind = bind, .queue_client = queue_client,
        .admit_client = admit_client, .reject_client = reject_client,
        .finish = finish, .dispose = dispose
    };
    return &services;
}
