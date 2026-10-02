#include "selected_effects_private.h"
#include "selected_effects_save.h"
#include "save_private.h"
#include "native_q3_client.h"
#include "source_restore.h"
#include "../../presentation/q3/internal.h"
#include "../../presentation/q3_native/events_internal.h"
#include "qa/q3_assets_save.h"

typedef struct effects_plan {
    qa_actor_owner provider;
    qa_product_id product;
    qa_q3_product q3_product;
    uint64_t identity, primary_identity, source_view, publication, map;
    uint32_t seat;
    size_t visual, parent;
    frontend_selected_effects_parent_kind parent_kind;
} effects_plan;

static bool provider(qa_source_save_io *io, qa_actor_owner *value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_strings *strings = qa_session_strings(io->session);
    char *name = !reading && *value <= UINT32_MAX ? (char *)qa_strings_cstr(strings, (qa_string_id)*value) : NULL;
    if (!reading && (!*value || !name || !*name)) return false;
    bool okay = frontend_save_text(io, &name);
    if (reading) {
        *value = name ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
        free(name);
    }
    return okay && *value;
}
static bool plan_fields(qa_source_save_io *io, effects_plan *plan)
{
    uint32_t product = plan->product, q3 = plan->q3_product, kind = plan->parent_kind;
    if (!provider(io, &plan->provider) || !qa_source_save_u32(io, &product) || !product ||
        !qa_source_save_u32(io, &q3) || q3 > QA_Q3_TEAM_ARENA ||
        !qa_source_save_u64(io, &plan->identity) || !plan->identity ||
        !qa_source_save_u64(io, &plan->primary_identity) || !plan->primary_identity || plan->primary_identity == plan->identity ||
        !qa_source_save_u32(io, &plan->seat) || !qa_source_save_u64(io, &plan->source_view) || !plan->source_view ||
        !qa_source_save_u64(io, &plan->publication) || !qa_source_save_u64(io, &plan->map) ||
        !qa_source_save_count(io, &plan->visual, SIZE_MAX) || !qa_source_save_u32(io, &kind) || kind > FRONTEND_EFFECTS_PARENT_NATIVE ||
        !qa_source_save_count(io, &plan->parent, SIZE_MAX)) return false;
    plan->product = product; plan->q3_product = (qa_q3_product)q3;
    plan->parent_kind = (frontend_selected_effects_parent_kind)kind; return true;
}
static bool identity_unused(const qa_frontend *frontend, uint64_t identity, qa_error *error)
{
    if (identity <= QA_FRONTEND_COMMAND_OWNER || identity - QA_FRONTEND_COMMAND_OWNER > frontend->next_source_id ||
        frontend_source_identity_used(frontend, identity)) return false;
    for (size_t i = 0; i < frontend_native_q3_count(frontend); ++i) {
        frontend_native_q3_view actual;
        if (!frontend_native_q3_read(frontend, i, &actual, error) || actual.identity == identity) return false;
    }
    return true;
}
static bool quiet(const frontend_selected_effects_group *group)
{
    return group && group->owner && !group->owner->busy && !group->active_source && !group->active_primary && !group->active_event && !group->active_pose &&
        q3n_events_idle(group->view.events) && q3n_media_idle(group->view.media) &&
        group->view.presentation && !group->view.presentation->busy;
}
static bool capture(const frontend_selected_effects_group *group)
{
    const qa_q3_presentation_assets *assets = group ? group->view.assets : NULL;
    return quiet(group) && assets && assets->capturing && assets->busy == 1 && !assets->codec_busy;
}
bool frontend_selected_effects_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    qa_application_content_graph *graph = frontend ? qa_application_content_graph_read(frontend->application) : NULL;
    if (!frontend || frontend->stepping || !graph || !out || out->data || out->size ||
        (frontend->selected_effects && frontend->selected_effects->busy))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect topology requires its actual retained graph");
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','E','X'}; uint32_t schema = 1;
    bool present = frontend->selected_effects != NULL;
    size_t count = frontend_selected_effects_count(frontend);
    bool okay = qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && qa_source_save_u32(&io, &schema) &&
        qa_source_save_bool(&io, &present) && qa_source_save_count(&io, &count, SIZE_MAX);
    size_t ordinal = 0;
    for (const frontend_selected_effects_group *group = frontend->selected_effects ? frontend->selected_effects->groups : NULL;
        okay && group; group = group->next, ++ordinal) {
        frontend_selected_effects_parent parent = {0}; qa_application_selected_effects source;
        effects_plan plan = {.provider = group->view.provider, .product = group->view.product, .q3_product = group->view.q3_product,
            .identity = group->view.identity, .primary_identity = group->view.primary_identity, .seat = group->view.physical_seat,
            .source_view = qa_application_content_view_id(graph, group->view.source_files),
            .publication = group->publication_generation, .map = group->map_revision};
        okay = quiet(group) && !group->restoring &&
            qa_application_effects_retained_read(frontend->application, group->view.provider, &source, error) &&
            frontend_selected_effects_group_current(group, &source) &&
            frontend_selected_effects_parent_read(frontend, ordinal, &parent, error);
        for (; okay && plan.visual < frontend_visual_owner_count(frontend); ++plan.visual) {
            frontend_visual_owner_view actual;
            okay = frontend_visual_owner_read(frontend, plan.visual, &actual);
            if (okay && actual.owner == group->view.content.owner && actual.family == group->view.content.family &&
                actual.mounts == group->view.content.mounts && actual.images == group->view.content.images &&
                actual.materials == group->view.content.materials) break;
        }
        plan.parent_kind = parent.kind; plan.parent = parent.ordinal;
        okay = okay && plan.visual < frontend_visual_owner_count(frontend) && plan_fields(&io, &plan);
    }
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Selected effect topology leaves its actual parent or content owner");
    return okay;
}
bool frontend_selected_effects_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    qa_application_content_graph *graph = frontend ? qa_application_content_graph_read(frontend->application) : NULL;
    if (!frontend || frontend->stepping || frontend->selected_effects || !frontend->source_restoring || !graph)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect topology needs its empty detached frontend");
    qa_source_save_io io = {0}; uint8_t magic[4]; uint32_t schema = 0; bool present = false; size_t count = 0;
    effects_plan *plans = NULL;
    bool okay = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFEX", 4) && qa_source_save_u32(&io, &schema) && schema == 1 &&
        qa_source_save_bool(&io, &present) && qa_source_save_count(&io, &count, bytes.size / 64) && (present || !count);
    if (okay && count) {
        plans = calloc(count, sizeof(*plans));
        if (!plans) okay = frontend_fail(error, QA_ERROR_MEMORY, "Decoding actual selected effect owner topology");
    }
    for (size_t i = 0; okay && i < count; ++i) {
        okay = plan_fields(&io, plans + i) && plans[i].seat < frontend->options.seats &&
            identity_unused(frontend, plans[i].identity, error) && qa_application_content_view(graph, plans[i].source_view);
        for (size_t j = 0; okay && j < i; ++j) if (plans[i].identity == plans[j].identity) okay = false;
    }
    okay = okay && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    frontend_selected_effects *owner = NULL;
    if (okay && present) {
        owner = calloc(1, sizeof(*owner));
        if (!owner) okay = frontend_fail(error, QA_ERROR_MEMORY, "Preparing selected effect continuation owner");
        else owner->frontend = frontend;
    }
    for (size_t i = 0; okay && i < count; ++i) {
        effects_plan *plan = plans + i;
        frontend_selected_effects_group *group = calloc(1, sizeof(*group));
        if (!group) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Preparing actual selected effect physical row"); break; }
        if (owner->tail) owner->tail->next = group; else owner->groups = group; owner->tail = group;
        group->owner = owner; group->restoring = true;
        group->view.provider = plan->provider; group->view.product = plan->product; group->view.q3_product = plan->q3_product;
        group->view.identity = plan->identity; group->view.primary_identity = plan->primary_identity; group->view.physical_seat = plan->seat;
        group->view.source_files = qa_application_content_view(graph, plan->source_view);
        group->publication_generation = plan->publication; group->map_revision = plan->map;
        qa_q3_presentation *primary = NULL; qa_q3_presentation_binding binding;
        okay = frontend_visual_owner_read(frontend, plan->visual, &group->view.content) &&
            group->view.content.owner == plan->provider && group->view.content.family == QA_SCENE_Q3 &&
            qa_vfs_lookup_equal(group->view.content.mounts, group->view.source_files) &&
            frontend_selected_effects_parent_resolve(frontend, plan->parent_kind, plan->parent, &primary, error) &&
            qa_q3_presentation_binding_read(primary, &binding, error) && binding.options.owner == plan->primary_identity &&
            binding.options.seat == plan->seat && (!binding.frame || binding.frame == &frontend->frame);
        if (okay) {
            group->primary = primary; group->world = binding.world; group->geometry = binding.geometry;
            okay = frontend_selected_effects_empty(group, &binding, error);
        }
        for (const frontend_selected_effects_group *prior = owner->groups; okay && prior != group; prior = prior->next)
            if (prior->view.provider == group->view.provider && prior->view.product == group->view.product &&
                prior->publication_generation == group->publication_generation && prior->map_revision == group->map_revision &&
                qa_vfs_lookup_equal(prior->view.content.mounts, group->view.content.mounts)) okay = false;
    }
    free(plans);
    if (!okay) {
        if (owner) {
            while (owner->groups) {
                frontend_selected_effects_group *group = owner->groups; owner->groups = group->next;
                frontend_selected_effects_group_dispose(group, NULL);
            }
            free(owner);
        }
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid selected effect topology");
        return false;
    }
    frontend->selected_effects = owner; return true;
}

static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = io->direction == QA_SOURCE_SAVE_WRITE ? bytes->size : 0;
    if (!qa_source_save_count(io, &count, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, count);
    if (io->offset > io->input.size || count > io->input.size - io->offset) return false;
    *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count; return true;
}
static bool same_vec(qa_vec3 left, qa_vec3 right)
{ return left.x == right.x && left.y == right.y && left.z == right.z; }
static bool same_ref(const qa_q3_ref_entity *left, const qa_q3_ref_entity *right)
{
    return left->kind == right->kind && left->flags == right->flags && left->model == right->model &&
        left->frame == right->frame && left->old_frame == right->old_frame && left->skin == right->skin &&
        left->custom_skin == right->custom_skin && left->custom_shader == right->custom_shader &&
        same_vec(left->lighting_origin, right->lighting_origin) && same_vec(left->axis[0], right->axis[0]) &&
        same_vec(left->axis[1], right->axis[1]) && same_vec(left->axis[2], right->axis[2]) &&
        same_vec(left->origin, right->origin) && same_vec(left->old_origin, right->old_origin) &&
        left->shadow_plane == right->shadow_plane && left->back_lerp == right->back_lerp &&
        left->shader_time == right->shader_time && left->radius == right->radius && left->rotation == right->rotation &&
        left->shader_texcoord.x == right->shader_texcoord.x && left->shader_texcoord.y == right->shader_texcoord.y &&
        !memcmp(left->color, right->color, 4) && left->non_normalized_axes == right->non_normalized_axes;
}
static bool private_fields(qa_source_save_io *io, frontend_selected_effects_group *group)
{
    uint8_t magic[4] = {'Q','F','E','P'}; uint32_t schema = 1;
    uint64_t identity = group->view.identity, publication = group->publication_generation, map = group->map_revision;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFEP", 4) || !qa_source_save_u32(io, &schema) || schema != 1 ||
        !qa_source_save_u64(io, &identity) || identity != group->view.identity ||
        !qa_source_save_u64(io, &publication) || publication != group->publication_generation ||
        !qa_source_save_u64(io, &map) || map != group->map_revision ||
        !qa_source_save_i32(io, &group->time) || !qa_source_save_i32(io, &group->previous_time) ||
        !qa_source_save_bool(io, &group->sampled) || !qa_source_save_bool(io, &group->prepared) ||
        (group->prepared && (!group->sampled || group->time != group->previous_time)) ||
        !qa_source_save_u64(io, &group->prepared_application_frame) || !qa_source_save_u64(io, &group->event_generation) ||
        !qa_source_save_u64(io, &group->event_cursor) || !qa_source_save_u64(io, &group->audio_bus) ||
        !qa_source_save_count(io, &group->ref_count, SIZE_MAX / sizeof(*group->refs)) ||
        group->ref_count != group->view.presentation->entity_count) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && group->ref_count) {
        group->refs = calloc(group->ref_count, sizeof(*group->refs));
        if (!group->refs) return frontend_fail(io->error, QA_ERROR_MEMORY, "Importing true selected effect cull radii");
        group->ref_capacity = group->ref_count;
    }
    for (size_t i = 0; i < group->ref_count; ++i) {
        frontend_selected_effects_ref *ref = group->refs + i;
        if (!qa_source_save_f32(io, &ref->cull_radius) || !isfinite(ref->cull_radius)) return false;
        const qa_q3_ref_entity *actual = group->view.presentation->entities + i;
        if (io->direction == QA_SOURCE_SAVE_READ) ref->ref = *actual;
        else if (!same_ref(&ref->ref, actual)) return false;
    }
    return true;
}
bool frontend_selected_effects_checkpoint(const qa_frontend *frontend, size_t ordinal, qa_buffer *out, qa_error *error)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    qa_application_selected_effects source;
    if (!out || out->data || out->size || !capture(group) || group->restoring ||
        !qa_application_effects_retained_read(frontend->application, group->view.provider, &source, error) ||
        !frontend_selected_effects_group_current(group, &source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect private capture requires its actual registry and source lease");
    qa_buffer children[2] = {0}; qa_bytes media = {0}, events = {0};
    frontend_selected_effects_group copy = *group; qa_source_save_io io = {0};
    bool okay = q3n_media_checkpoint(group->view.media, children, error) &&
        q3n_events_checkpoint(group->view.events, children + 1, error);
    media = (qa_bytes){children[0].data, children[0].size}; events = (qa_bytes){children[1].data, children[1].size};
    okay = okay && qa_source_save_writer(&io, NULL, error) && private_fields(&io, &copy) &&
        blob(&io, &media) && blob(&io, &events) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(children); qa_buffer_free(children + 1);
    if (!okay && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid selected effect private continuation");
    return okay;
}
bool frontend_selected_effects_restore(qa_frontend *frontend, size_t ordinal, qa_bytes bytes, qa_error *error)
{
    frontend_selected_effects_group *target = (frontend_selected_effects_group *)frontend_selected_effects_group_at(frontend, ordinal);
    qa_application_selected_effects source; qa_q3_presentation_binding own; frontend_selected_effects_parent parent;
    if (!capture(target) || !target->restoring || target->refs || target->launch_lease ||
        !qa_application_effects_retained_read(frontend->application, target->view.provider, &source, error) ||
        source.kind != QA_APPLICATION_EFFECTS_Q3 || source.product != target->view.product || source.q3_product != target->view.q3_product ||
        source.publication_generation != target->publication_generation || source.map_revision != target->map_revision ||
        source.content != target->view.source_files || !qa_vfs_lookup_equal(source.content, target->view.content.mounts) ||
        !frontend_selected_effects_parent_read(frontend, ordinal, &parent, error) ||
        !qa_q3_presentation_binding_read(target->view.presentation, &own, error) || !own.world || !own.geometry ||
        own.world != parent.binding.world || own.geometry != parent.binding.geometry || own.frame != &frontend->frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect private import requires its actual restored source and parent binding");
    frontend_selected_effects_group copy = *target; copy.refs = NULL; copy.ref_count = copy.ref_capacity = 0;
    copy.view.media = NULL; copy.view.events = NULL; copy.launch_lease = NULL;
    qa_source_save_io io = {0}; qa_bytes media = {0}, events = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && private_fields(&io, &copy) &&
        blob(&io, &media) && blob(&io, &events) && qa_source_save_finish(&io, NULL) &&
        copy.prepared_application_frame <= source.application_frame &&
        (!copy.prepared || copy.prepared_application_frame != source.application_frame || copy.time == source.sample_time_ms) &&
        copy.event_generation <= qa_application_protocol_events_generation(frontend->application) &&
        (copy.event_generation != qa_application_protocol_events_generation(frontend->application) ||
            copy.event_cursor <= qa_application_event_count(frontend->application));
    qa_source_save_dispose(&io);
    q3n_media_options media_options = {.product=target->view.q3_product, .assets=target->view.assets};
    q3n_event_options event_options = {.product = target->view.q3_product, .assets = target->view.assets,
        .context = target, .trace = target->view.events->options.trace,
        .point_contents = target->view.events->options.point_contents, .mark_fragments = target->view.events->options.mark_fragments};
    if (okay) okay = q3n_media_create(&media_options, &copy.view.media, error) &&
        q3n_events_create_effects(&event_options, &copy.view.events, error) &&
        q3n_media_restore(copy.view.media, media, error) && q3n_media_effects_ready(copy.view.media) &&
        q3n_events_restore(copy.view.events, events, error) &&
        qa_launch_instance_retain_metadata(source.launch, &copy.launch_lease, error);
    if (okay) {
        copy.launch = qa_launch_instance_lease_view(copy.launch_lease);
        copy.world = own.world; copy.geometry = own.geometry; copy.restoring = false;
        q3n_media_destroy(target->view.media); q3n_events_destroy(target->view.events);
        *target = copy;
    } else {
        q3n_media_destroy(copy.view.media); q3n_events_destroy(copy.view.events);
        qa_launch_instance_lease_release(copy.launch_lease); free(copy.refs);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved effects differ from their actual source, handles or continuation");
    }
    return okay;
}
bool frontend_selected_effects_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend_selected_effects_idle(frontend)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect owners still hold a frame or registry lease");
    for (const frontend_selected_effects_group *group = frontend->selected_effects ? frontend->selected_effects->groups : NULL; group; group = group->next)
        if (group->restoring || !group->launch_lease || !q3n_media_effects_ready(group->view.media))
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected effect topology lacks its actual private source continuation");
    return true;
}
bool frontend_selected_effects_rebind_ready(const qa_frontend *owned, const qa_frontend *destination, qa_error *error)
{
    if (!owned || !destination || (owned != destination && destination->selected_effects) || !frontend_selected_effects_idle(owned))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect exchange requires idle actual source and empty destination");
    for (const frontend_selected_effects_group *group = owned->selected_effects ? owned->selected_effects->groups : NULL; group; group = group->next)
        if (!qa_q3_presentation_frontend_rebind_ready(group->view.presentation, &owned->frame, destination->audio, group->audio_bus, error)) return false;
    return true;
}
void frontend_selected_effects_rebind(qa_frontend *owned, qa_frontend *destination)
{
    if (!owned || !destination || owned == destination) return;
    destination->selected_effects = owned->selected_effects; owned->selected_effects = NULL;
    if (!destination->selected_effects) return;
    destination->selected_effects->frontend = destination;
    for (frontend_selected_effects_group *group = destination->selected_effects->groups; group; group = group->next)
        qa_q3_presentation_frontend_rebind(group->view.presentation, &owned->frame, &destination->frame, destination->audio, group->audio_bus);
}
