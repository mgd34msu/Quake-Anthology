#include "restore_topology.h"
#include "save_private.h"
#include "qa/persistence_content.h"
#include "qa/font_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"
#include "ui_features.h"
#include "root_resources.h"

struct frontend_restore_topology {
    uint32_t seats, width, height;
    bool dedicated, ui_images, fonts, order, images, materials, sounds, audio;
    bool mods[QA_INPUT_LOCAL_SEATS];
    uint64_t time_ns, wall_time_ns, frame_number, configuration, map_revision, next_source_id, next_audio_id;
    uint64_t silent_audio_remainder, ui_view, world_view;
    qa_audio_output_format output;
    frontend_source_group_plan *groups;
    qa_buffer *group_portals;
    size_t group_count;
};
static bool key(qa_source_save_io *io, qa_strings *strings, uint64_t *owner)
{
    char *text = io->direction == QA_SOURCE_SAVE_WRITE && *owner <= UINT32_MAX ?
        (char *)qa_strings_cstr(strings, (qa_string_id)*owner) : NULL;
    if (io->direction == QA_SOURCE_SAVE_WRITE && (!*owner || !text))
        return frontend_fail(io->error, QA_ERROR_FORMAT, "frontend service lacks its actual foundation key");
    if (!frontend_save_text(io, &text)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *owner = text ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)text, strlen(text)}) : 0;
        free(text);
    }
    return *owner != 0 || frontend_fail(io->error, QA_ERROR_FORMAT, "frontend service key is absent from saved foundation");
}
static bool flags(qa_source_save_io *io, struct frontend_restore_topology *p)
{
    return qa_source_save_u32(io, &p->seats) && p->seats && p->seats <= QA_INPUT_LOCAL_SEATS &&
        qa_source_save_u32(io, &p->width) && qa_source_save_u32(io, &p->height) &&
        qa_source_save_bool(io, &p->dedicated) && qa_source_save_bool(io, &p->ui_images) &&
        qa_source_save_bool(io, &p->fonts) && qa_source_save_bool(io, &p->order) &&
        qa_source_save_bool(io, &p->images) && qa_source_save_bool(io, &p->materials) &&
        qa_source_save_bool(io, &p->sounds) && qa_source_save_bool(io, &p->audio) &&
        qa_source_save_u64(io, &p->time_ns) && qa_source_save_u64(io, &p->wall_time_ns) &&
        qa_source_save_u64(io, &p->frame_number) &&
        qa_source_save_u64(io, &p->configuration) && qa_source_save_u64(io, &p->map_revision) &&
        qa_source_save_u64(io, &p->next_source_id) && qa_source_save_u64(io, &p->next_audio_id) &&
        qa_source_save_u64(io, &p->silent_audio_remainder) &&
        qa_source_save_u32(io,&p->output.sample_rate) && p->output.sample_rate>=8000 && p->output.sample_rate<=192000 &&
        qa_source_save_u32(io,&p->output.channels) && (p->output.channels==1 || p->output.channels==2) &&
        qa_source_save_u32(io,&p->output.sample_bits) && (p->output.sample_bits==8 || p->output.sample_bits==16) &&
        qa_source_save_u64(io, &p->ui_view) &&
        qa_source_save_u64(io, &p->world_view) &&
        p->next_source_id < UINT64_MAX - QA_FRONTEND_COMMAND_OWNER && p->silent_audio_remainder < UINT64_C(1000000000) &&
        (!p->ui_images || p->ui_view) && (!p->fonts || p->ui_images) &&
        (!p->images || p->world_view) && (!p->materials || (p->images && p->order)) &&
        (!p->sounds || p->world_view) &&
        (!p->ui_view || p->ui_view != p->world_view) &&
        (p->dedicated || (p->ui_view && p->ui_images && p->fonts && p->order && p->audio)) &&
        (!p->dedicated || (!p->ui_view && !p->world_view && !p->ui_images && !p->fonts && !p->order &&
            !p->images && !p->materials && !p->sounds && !p->audio));
}
static bool portals_fields(qa_source_save_io *io,frontend_source_group_plan *group)
{
    size_t count=group->portals.size;
    if (!qa_source_save_bool(io,&group->private_map) ||
        !qa_source_save_u64(io,&group->map_pool) || !qa_source_save_u64(io,&group->map_resource) ||
        !qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (count>io->input.size-io->offset) return false;
        group->portals=(qa_bytes){count?io->input.data+io->offset:NULL,count}; io->offset+=count;
    } else if ((count && !group->portals.data) ||
        !qa_source_save_bytes(io,(void *)group->portals.data,count)) return false;
    return (!group->map_pool==!group->map_resource) && (!group->map_resource==!count) &&
        (group->private_map || !group->map_resource);
}
static bool fields(qa_source_save_io *io, qa_application *app, struct frontend_restore_topology *p)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','F','T','P'}; uint32_t version = 5;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFTP", 4) ||
        !qa_source_save_u32(io, &version) || version != 5 || !flags(io, p)) return false;
    for (size_t i = 0; i < p->seats; ++i)
        if (!qa_source_save_bool(io, &p->mods[i]) || (p->dedicated && p->mods[i])) return false;
    if (!qa_source_save_count(io, &p->group_count, reading ? io->input.size / 48 : SIZE_MAX / sizeof(*p->groups))) return false;
    if (reading) {
        p->groups = calloc(p->group_count ? p->group_count : 1, sizeof(*p->groups));
        if (!p->groups) return frontend_fail(io->error, QA_ERROR_MEMORY, "retaining frontend source topology");
    }
    qa_strings *strings = qa_session_strings(qa_application_session(app));
    for (size_t i = 0; i < p->group_count; ++i) {
        frontend_source_group_plan *g = &p->groups[i]; uint64_t owner = g->owner;
        if (!key(io, strings, &owner) || owner > UINT32_MAX || !qa_source_save_u32(io, &g->seat) ||
            g->seat >= p->seats || p->dedicated || !qa_source_save_u32(io, &g->launch_seat) ||
            !qa_source_save_u64(io, &g->identity) ||
            g->identity <= QA_FRONTEND_COMMAND_OWNER || g->identity - QA_FRONTEND_COMMAND_OWNER > p->next_source_id ||
            !qa_source_save_u64(io, &g->mounts_view) || !qa_source_save_u64(io, &g->source_view) ||
            !g->mounts_view || !g->source_view || g->mounts_view == g->source_view ||
            g->mounts_view == p->ui_view || g->mounts_view == p->world_view || !p->order ||
            !portals_fields(io,g) ||
            !qa_source_save_count(io, &g->role_count, reading ? io->input.size / 13 : SIZE_MAX / sizeof(*g->roles)) || !g->role_count) return false;
        g->owner = (qa_actor_owner)owner;
        if (reading) {
            g->roles = calloc(g->role_count, sizeof(*g->roles));
            if (!g->roles) return frontend_fail(io->error, QA_ERROR_MEMORY, "retaining frontend source role topology");
        }
        frontend_source_role_identity *roles = (frontend_source_role_identity *)g->roles;
        for (size_t j = 0; j < g->role_count; ++j) {
            uint32_t role = roles[j].role;
            if (!qa_source_save_u32(io, &role) || (role != QA_QVM_CGAME && role != QA_QVM_UI) ||
                !key(io, strings, &roles[j].service_owner)) return false;
            roles[j].role = (qa_qvm_role)role;
            for (size_t a = 0; a <= i; ++a) {
                size_t limit = a == i ? j : p->groups[a].role_count;
                for (size_t b = 0; b < limit; ++b)
                    if (p->groups[a].roles[b].service_owner == roles[j].service_owner) return false;
            }
        }
        for (size_t j = 0; j < i; ++j)
            if (g->identity == p->groups[j].identity || g->mounts_view == p->groups[j].mounts_view ||
                (g->owner == p->groups[j].owner && g->seat == p->groups[j].seat && g->source_view == p->groups[j].source_view)) return false;
    }
    return true;
}
void frontend_topology_destroy(frontend_restore_topology *p)
{
    if (!p) return;
    if (p->groups) for (size_t i = 0; i < p->group_count; ++i) free((void *)p->groups[i].roles);
    if (p->group_portals) for (size_t i=0;i<p->group_count;++i) qa_buffer_free(p->group_portals+i);
    free(p->group_portals);
    free(p->groups); free(p);
}
bool frontend_topology_checkpoint(const qa_frontend *f, qa_buffer *out, qa_error *error)
{
    if (!f || !f->application || !f->options.seats || f->options.seats > QA_INPUT_LOCAL_SEATS ||
        f->stepping || f->source_restoring || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend topology requires idle actual owners and empty output");
    qa_application_content_graph *graph = qa_application_content_graph_read(f->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend topology requires the captured content graph");
    frontend_restore_topology *p = calloc(1, sizeof(*p));
    if (!p) return frontend_fail(error, QA_ERROR_MEMORY, "retaining frontend constructor topology");
    *p = (frontend_restore_topology){.seats = f->options.seats, .width = f->width, .height = f->height,
        .dedicated = f->options.dedicated, .ui_images = f->ui_images != NULL, .fonts = f->fonts != NULL,
        .order = f->order != NULL, .images = f->images != NULL, .materials = f->materials != NULL,
        .sounds = f->sounds != NULL, .audio = f->audio != NULL, .time_ns = f->time_ns,
        .wall_time_ns = f->wall_time_ns,
        .frame_number = f->frame_number, .configuration = f->configuration, .map_revision = f->map_revision,
        .next_source_id = f->next_source_id, .next_audio_id = f->next_audio_id,
        .silent_audio_remainder = f->silent_audio_remainder,
        .output=f->audio_output_format,
        .ui_view = qa_application_content_view_id(graph, f->ui_mounts),
        .world_view = qa_application_content_view_id(graph, f->mounts), .group_count = frontend_source_group_count(f)};
    bool ok = (!f->ui_mounts || p->ui_view) && (!f->mounts || p->world_view);
    for (size_t i = 0; ok && i < p->seats; ++i) {
        ok = f->seats && (p->dedicated || (f->seats[i].frontend == f && f->seats[i].id == i));
        p->mods[i] = f->seats && f->seats[i].mods != NULL;
    }
    p->groups = calloc(p->group_count ? p->group_count : 1, sizeof(*p->groups));
    p->group_portals=calloc(p->group_count?p->group_count:1,sizeof(*p->group_portals));
    if (!p->groups || !p->group_portals) ok = frontend_fail(error, QA_ERROR_MEMORY, "retaining source constructor groups");
    for (size_t i = 0; ok && i < p->group_count; ++i) {
        frontend_source_group_view actual;
        if (!frontend_source_group_read(f, i, &actual)) { ok = false; break; }
        frontend_source_group_plan *g = &p->groups[i];
        *g = (frontend_source_group_plan){.owner = actual.owner, .seat = actual.seat,
            .launch_seat = actual.launch_seat, .identity = actual.identity,
            .mounts_view = qa_application_content_view_id(graph, actual.mounts),
            .source_view = qa_application_content_view_id(graph, actual.source_files),.private_map=actual.private_map};
        if ((!actual.map_resource!=!actual.geometry) || (!actual.map_resource!=!actual.world) ||
            (!actual.private_map && actual.map_resource)) { ok=false; break; }
        if (actual.map_resource) {
            ok=qa_application_content_resource_id(graph,actual.map_resource,&g->map_pool,&g->map_resource) &&
                frontend_source_geometry_checkpoint((qa_frontend *)f,i,p->group_portals+i,error);
            if (!ok) break;
            g->portals=(qa_bytes){p->group_portals[i].data,p->group_portals[i].size};
        }
        while (frontend_source_group_role_read(f, i, g->role_count, &(frontend_source_role_identity){0})) ++g->role_count;
        frontend_source_role_identity *roles = calloc(g->role_count ? g->role_count : 1, sizeof(*roles)); g->roles = roles;
        if (!roles) { ok = frontend_fail(error, QA_ERROR_MEMORY, "retaining source constructor roles"); break; }
        for (size_t j = 0; ok && j < g->role_count; ++j) ok = frontend_source_group_role_read(f, i, j, &roles[j]);
    }
    qa_source_save_io io = {0};
    ok = ok && qa_source_save_writer(&io, qa_application_session(f->application), error) && fields(&io, f->application, p) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); frontend_topology_destroy(p);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "frontend topology is not completely qualified");
    return ok;
}
bool frontend_topology_decode(qa_application *app, qa_bytes bytes, frontend_restore_topology **out, qa_error *error)
{
    if (!app || !out || *out) return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend topology decode requires saved foundation and empty output");
    frontend_restore_topology *p = calloc(1, sizeof(*p));
    if (!p) return frontend_fail(error, QA_ERROR_MEMORY, "retaining restored frontend topology");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(app), bytes, error) && fields(&io, app, p) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { frontend_topology_destroy(p); if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "saved frontend topology is invalid"); return false; }
    *out = p; return true;
}
const bool *frontend_topology_mods(const frontend_restore_topology *p) { return p ? p->mods : NULL; }
bool frontend_topology_prepare(qa_frontend *f, const frontend_restore_topology *p, qa_error *error)
{
    if (!f || !p || !f->application || !f->seats || f->stepping || f->options.seats != p->seats ||
        f->options.dedicated != p->dedicated || f->sources || f->source_restoring || f->ui_mounts || f->mounts ||
        f->ui_images || f->images || f->fonts || f->order || f->materials || f->sounds || f->audio)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "frontend topology admission requires an empty isolated service graph");
    qa_application_content_graph *graph = qa_application_content_graph_read(f->application);
    if (!graph || (p->ui_view && !qa_application_content_view(graph, p->ui_view)) ||
        (p->world_view && !qa_application_content_view(graph, p->world_view)))
        return frontend_fail(error, QA_ERROR_FORMAT, "frontend topology views are absent from preloaded graph");
    f->width = p->width; f->height = p->height; f->time_ns = p->time_ns;
    f->wall_time_ns = p->wall_time_ns; f->frame_number = p->frame_number;
    f->configuration = p->configuration; f->map_revision = p->map_revision; f->next_source_id = p->next_source_id;
    f->next_audio_id = p->next_audio_id; f->silent_audio_remainder = p->silent_audio_remainder;
    f->audio_output_format=p->output;
    if ((p->ui_view && !qa_application_content_claim_view(graph, p->ui_view, &f->ui_mounts, error)) ||
        (p->world_view && !qa_application_content_claim_view(graph, p->world_view, &f->mounts, error))) return false;
    if (!frontend_ui_features_prepare(f,error)) return false;
    if (p->ui_images && !(f->ui_images = qa_scene_resources_create_detached(f->ui_mounts, error))) return false;
    if (p->fonts && !(f->fonts = qa_font_library_create(f->ui_mounts, f->ui_images, error))) return false;
    if (p->order && !(f->order = qa_material_order_create(error))) return false;
    if (p->images && !(f->images = qa_scene_resources_create_detached(f->mounts, error))) return false;
    if (p->materials && !(f->materials = qa_material_library_create_detached(f->images, error))) return false;
    if (p->materials && !frontend_root_resources_prepare_restored(f,error)) return false;
    if (p->sounds && !qa_audio_bank_create(f->mounts, &f->sounds, error)) return false;
    if (p->audio) {
        qa_audio_engine_options audio;
        frontend_audio_engine_options(f,&audio);
        if (!qa_audio_engine_create(&audio,&f->audio,error)) return false;
    }
    if (!p->dedicated && !frontend_seats_prepare_restored(f, error)) return false;
    return frontend_source_prepare_groups(f, p->next_source_id, p->groups, p->group_count, error);
}
