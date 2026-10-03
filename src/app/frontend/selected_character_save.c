#include "selected_character_private.h"
#include "selected_character_save.h"
#include "save_private.h"
#include "../../presentation/q3/internal.h"

static frontend_selected_character *owner_at(const qa_frontend *frontend, size_t ordinal)
{
    frontend_selected_character *owner = frontend ? frontend->selected_characters : NULL;
    while (owner && ordinal) { owner = owner->next; --ordinal; }
    return owner;
}
static bool quiet(const frontend_selected_character *owner)
{
    if (!owner || owner->admitting) return false;
    for (const frontend_selected_character_pose *pose = owner->poses; pose; pose = pose->next)
        if (pose->users) return false;
    return true;
}
static bool topology_fields(qa_source_save_io *io, qa_application *app,
    uint32_t *seat, qa_actor_owner *character, qa_actor_owner *appearance, size_t *visual)
{
    return qa_source_save_u32(io, seat) && frontend_save_provider(io, app, character) && *character &&
        frontend_save_provider(io, app, appearance) && *appearance && qa_source_save_count(io, visual, SIZE_MAX);
}
bool frontend_selected_character_topology_checkpoint(const qa_frontend *frontend,
    qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character topology requires its actual inactive frontend");
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','C','T'}; size_t count = frontend_selected_character_count(frontend);
    bool okay = qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (const frontend_selected_character *owner = frontend->selected_characters; okay && owner; owner = owner->next) {
        size_t visual = 0; frontend_visual_owner_view media;
        for (; visual < frontend_visual_owner_count(frontend); ++visual) {
            if (!frontend_visual_owner_read(frontend, visual, &media)) { okay = false; break; }
            if (media.owner == owner->view.appearance.provider && media.family == QA_SCENE_Q3 &&
                media.mounts == owner->view.content.mounts && media.images == owner->view.content.images &&
                media.materials == owner->view.content.materials) break;
        }
        uint32_t seat = owner->view.launch_seat;
        qa_actor_owner character = owner->view.selection.owner, appearance = owner->view.appearance.provider;
        okay = okay && quiet(owner) && !owner->restoring && visual < frontend_visual_owner_count(frontend) &&
            topology_fields(&io, frontend->application, &seat, &character, &appearance, &visual);
    }
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "Selected character topology leaves its actual visual heaps");
    return okay;
}
bool frontend_selected_character_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->selected_characters)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character topology requires an empty candidate");
    qa_source_save_io io = {0}; uint8_t magic[4]; size_t count = 0;
    frontend_selected_character *head = NULL, *tail = NULL;
    bool okay = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFCT", 4) && qa_source_save_count(&io, &count, bytes.size / 16);
    for (size_t i = 0; okay && i < count; ++i) {
        frontend_selected_character *owner = calloc(1, sizeof(*owner));
        if (!owner) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Preparing actual selected character registry"); break; }
        if (tail) tail->next = owner; else head = owner; tail = owner;
        owner->frontend = frontend; owner->restoring = true; size_t visual = 0;
        okay = topology_fields(&io, frontend->application, &owner->view.launch_seat,
            &owner->view.selection.owner, &owner->view.appearance.provider, &visual) &&
            frontend_visual_owner_read(frontend, visual, &owner->view.content) &&
            owner->view.content.owner == owner->view.appearance.provider && owner->view.content.family == QA_SCENE_Q3;
        for (const frontend_selected_character *prior = head; okay && prior != owner; prior = prior->next)
            if (prior->view.launch_seat == owner->view.launch_seat &&
                prior->view.selection.owner == owner->view.selection.owner &&
                prior->view.appearance.provider == owner->view.appearance.provider) okay = false;
        qa_q3_presentation_asset_options options = {.provider = {owner->view.content.mounts,
            owner->view.content.images, owner->view.content.materials, QA_SCENE_Q3}};
        if (okay) okay = qa_q3_presentation_assets_create(&options, &owner->view.assets, error);
    }
    okay = okay && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (!okay) {
        while (head) { frontend_selected_character *next = head->next; frontend_selected_character_dispose(head); head = next; }
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid selected character topology");
        return false;
    }
    frontend->selected_characters = head; return true;
}
static bool animation_fields(qa_source_save_io *io, qa_player_animation_config *config)
{
    uint32_t footsteps = config->footsteps, gender = config->gender;
    if (!qa_source_save_u32(io, &footsteps) || footsteps > QA_FOOTSTEP_ENERGY ||
        !qa_source_save_u32(io, &gender) || gender > QA_MODEL_NEUTER ||
        !qa_source_save_bool(io, &config->fixed_legs) || !qa_source_save_bool(io, &config->fixed_torso)) return false;
    config->footsteps = (qa_model_footstep)footsteps; config->gender = (qa_model_gender)gender;
    for (unsigned i = 0; i < 3; ++i) if (!qa_source_save_f32(io, config->head_offset + i)) return false;
    for (unsigned i = 0; i < QA_PLAYER_ANIMATION_COUNT; ++i) {
        qa_player_animation *a = config->animations + i;
        if (!qa_source_save_i32(io, &a->first_frame) || !qa_source_save_i32(io, &a->num_frames) ||
            !qa_source_save_i32(io, &a->loop_frames) || !qa_source_save_i32(io, &a->frame_lerp) ||
            !qa_source_save_i32(io, &a->initial_lerp) || !qa_source_save_bool(io, &a->reversed) ||
            !qa_source_save_bool(io, &a->flipflop) || !qa_source_save_bool(io, &a->present) ||
            (i != 31 && !a->present)) return false;
    }
    return true;
}
static bool acquisition_fields(qa_source_save_io *io, qa_vfs_acquisition *receipt)
{
    return qa_source_save_u64(io, &receipt->mount) && receipt->mount &&
        qa_source_save_u64(io, &receipt->resource_id) && receipt->resource_id &&
        frontend_save_text(io, &receipt->path) && receipt->path && *receipt->path &&
        frontend_save_text(io, &receipt->lookup_path) && receipt->lookup_path && *receipt->lookup_path &&
        frontend_save_text(io, &receipt->link_source) && frontend_save_text(io, &receipt->link_target) &&
        (!!receipt->link_source == !!receipt->link_target);
}
static bool resource_fields(qa_source_save_io *io, qa_application_content_graph *graph,
    frontend_selected_character *owner, unsigned slot)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = owner->resources[slot] != NULL;
    if (!qa_source_save_bool(io, &present) || (slot != 7 && !present)) return false;
    if (!present) return true;
    uint64_t pool = 0, resource = 0;
    if ((!reading && !qa_application_content_resource_id(graph, owner->resources[slot], &pool, &resource)) ||
        !qa_source_save_u64(io, &pool) || !pool || !qa_source_save_u64(io, &resource) || !resource ||
        !acquisition_fields(io, owner->receipts + slot)) return false;
    if (reading) {
        const qa_resource *actual = qa_application_content_resource(graph, pool, resource);
        if (!actual) return false;
        owner->resources[slot] = (qa_resource *)actual; qa_resource_retain(owner->resources[slot]);
    }
    return owner->receipts[slot].resource_id == qa_resource_id(owner->resources[slot]) &&
        qa_resource_pool_find(qa_vfs_resources(owner->view.content.mounts), owner->receipts[slot].resource_id) == owner->resources[slot] &&
        qa_vfs_acquisition_retained(owner->view.content.mounts, owner->receipts + slot, io->error);
}
static bool finite_float(qa_source_save_io *io, float *value)
{ return qa_source_save_f32(io, value) && isfinite(*value); }
static bool pose_frame(qa_source_save_io *io, q3n_pose_frame *pose)
{
    q3n_lerp_frame *lerp = &pose->animation;
    return qa_source_save_i32(io, &lerp->old_frame) && qa_source_save_i32(io, &lerp->old_frame_time) &&
        qa_source_save_i32(io, &lerp->frame) && qa_source_save_i32(io, &lerp->frame_time) && finite_float(io, &lerp->back_lerp) &&
        qa_source_save_i32(io, &lerp->animation_number) && qa_source_save_i32(io, &lerp->animation_time) &&
        qa_source_save_bool(io, &lerp->selected) && finite_float(io, &pose->yaw_angle) && finite_float(io, &pose->pitch_angle) &&
        qa_source_save_bool(io, &pose->yawing) && qa_source_save_bool(io, &pose->pitching);
}
static bool selection_fields(qa_source_save_io *io, qa_application_content_graph *graph,
    const qa_native_q3_character_selection *selection, const qa_application_q3_asset_selection *appearance)
{
    uint32_t product = selection->product, skin_product = appearance->product;
    uint64_t publication = selection->publication_generation;
    uint64_t character_view = qa_application_content_view_id(graph, selection->content);
    uint64_t skin_view = qa_application_content_view_id(graph, appearance->content);
    if (!qa_source_save_u32(io, &product) || product != selection->product ||
        !qa_source_save_u32(io, &skin_product) || skin_product != appearance->product ||
        !qa_source_save_u64(io, &publication) || publication != selection->publication_generation ||
        !qa_source_save_u64(io, &character_view) || !character_view ||
        qa_application_content_view(graph, character_view) != selection->content ||
        !qa_source_save_u64(io, &skin_view) || !skin_view || qa_application_content_view(graph, skin_view) != appearance->content) return false;
    const char *fields[] = {selection->definition, selection->model, selection->skin, selection->head_model, selection->head_skin};
    for (unsigned i = 0; i < 5; ++i) {
        char *text = io->direction == QA_SOURCE_SAVE_WRITE ? (char *)fields[i] : NULL;
        bool okay = frontend_save_text(io, &text) && text && !strcmp(text, fields[i]);
        if (io->direction == QA_SOURCE_SAVE_READ) free(text);
        if (!okay) return false;
    }
    return true;
}
static bool handles_valid(const frontend_selected_character *owner, qa_error *error)
{
    for (unsigned i = 0; i < 3; ++i) {
        qa_q3_asset_model_holder model; qa_q3_asset_skin_holder skin;
        if (owner->view.models[i] <= 0 || owner->view.skins[i] <= 0 ||
            !qa_q3_assets_model_holder(owner->view.assets, (size_t)owner->view.models[i] - 1, &model, error) ||
            !qa_q3_assets_skin_holder(owner->view.assets, (size_t)owner->view.skins[i] - 1, &skin, error) ||
            model.resource != owner->resources[i] || skin.resource != owner->resources[i + 3] ||
            model.provider.mounts != owner->view.content.mounts || skin.provider.mounts != owner->view.content.mounts ||
            !model.sources[0] || model.sources[0]->format != QA_MODEL_MD3) return false;
    }
    return true;
}
static bool capture(const frontend_selected_character *owner)
{
    const qa_q3_presentation_assets *assets = owner ? owner->view.assets : NULL;
    return quiet(owner) && assets && assets->capturing && assets->busy == 1 && !assets->codec_busy;
}
bool frontend_selected_character_checkpoint(const qa_frontend *frontend, size_t ordinal,
    qa_buffer *out, qa_error *error)
{
    frontend_selected_character *owner = owner_at(frontend, ordinal);
    qa_application_content_graph *graph = frontend ? qa_application_content_graph_read(frontend->application) : NULL;
    if (!capture(owner) || owner->restoring || !graph || !out || out->data || out->size ||
        !owner->view.selection.current(owner->view.selection.lifetime, &owner->view.selection))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character checkpoint requires its real registry lease and retained declaration");
    frontend_selected_character copy = *owner;
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','C','P'}; size_t count = 0;
    for (const frontend_selected_character_pose *pose = owner->poses; pose; pose = pose->next) ++count;
    bool okay = qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && selection_fields(&io, graph, &owner->view.selection, &owner->view.appearance);
    for (unsigned i = 0; okay && i < 8; ++i) okay = resource_fields(&io, graph, &copy, i);
    okay = okay && animation_fields(&io, &copy.animation);
    for (unsigned i = 0; okay && i < 3; ++i)
        okay = qa_source_save_i32(&io, copy.view.models + i) && qa_source_save_i32(&io, copy.view.skins + i);
    okay = okay && handles_valid(owner, error) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (const frontend_selected_character_pose *pose = owner->poses; okay && pose; pose = pose->next) {
        frontend_selected_character_pose saved = *pose;
        okay = qa_source_save_actor(&io, &saved.actor) && saved.actor.registry &&
            qa_source_save_u32(&io, &saved.physical_seat) && saved.physical_seat < frontend->options.seats &&
            qa_source_save_bool(&io, &saved.reset) && pose_frame(&io, &saved.pose.legs) && pose_frame(&io, &saved.pose.torso) &&
            qa_source_save_i32(&io, &saved.pose.pain_time) && qa_source_save_bool(&io, &saved.pose.pain_direction);
    }
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid selected character continuation");
    return okay;
}
bool frontend_selected_character_restore(qa_frontend *frontend, size_t ordinal, qa_bytes bytes, qa_error *error)
{
    frontend_selected_character *target = owner_at(frontend, ordinal);
    qa_application_content_graph *graph = frontend ? qa_application_content_graph_read(frontend->application) : NULL;
    if (!capture(target) || !target->restoring || target->poses || target->view.selection.release || !graph)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character restore requires its empty imported physical registry");
    frontend_selected_character *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing selected character continuation candidate");
    candidate->frontend = frontend; candidate->view = target->view; candidate->view.assets = NULL;
    bool found = false; qa_actor_id viewer;
    bool okay = qa_application_character_selection_read(frontend->application, target->view.launch_seat,
        &candidate->view.selection, &found, error) && found && candidate->view.selection.owner == target->view.selection.owner &&
        qa_application_player_actor(frontend->application, target->view.launch_seat, &viewer) &&
        qa_application_q3_asset_selection_read(frontend->application, viewer, QA_ROLE_SKIN, &candidate->view.appearance, &found, error) &&
        found && candidate->view.appearance.family == QA_GAME_Q3 &&
        candidate->view.appearance.provider == target->view.appearance.provider &&
        qa_launch_instance_retain_metadata(candidate->view.appearance.launch, &candidate->appearance_lease, error);
    if (okay) candidate->view.appearance_launch = qa_launch_instance_lease_view(candidate->appearance_lease);
    qa_source_save_io io = {0}; uint8_t magic[4]; size_t count = 0;
    okay = okay && qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFCP", 4) && selection_fields(&io, graph, &candidate->view.selection, &candidate->view.appearance);
    for (unsigned i = 0; okay && i < 8; ++i) okay = resource_fields(&io, graph, candidate, i);
    okay = okay && animation_fields(&io, &candidate->animation);
    for (unsigned i = 0; okay && i < 3; ++i)
        okay = qa_source_save_i32(&io, candidate->view.models + i) && qa_source_save_i32(&io, candidate->view.skins + i);
    if (okay) {
        candidate->view.assets = target->view.assets;
        okay = handles_valid(candidate, error);
        candidate->view.assets = NULL;
    }
    okay = okay && qa_source_save_count(&io, &count, bytes.size / 100);
    frontend_selected_character_pose *tail = NULL;
    for (size_t i = 0; okay && i < count; ++i) {
        frontend_selected_character_pose *pose = calloc(1, sizeof(*pose));
        if (!pose) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Restoring selected full actor and display pose"); break; }
        if (tail) tail->next = pose; else candidate->poses = pose; tail = pose;
        okay = qa_source_save_actor(&io, &pose->actor) && pose->actor.registry && qa_source_save_u32(&io, &pose->physical_seat) &&
            pose->physical_seat < frontend->options.seats && qa_source_save_bool(&io, &pose->reset) &&
            pose_frame(&io, &pose->pose.legs) && pose_frame(&io, &pose->pose.torso) &&
            qa_source_save_i32(&io, &pose->pose.pain_time) && qa_source_save_bool(&io, &pose->pose.pain_direction);
        for (const frontend_selected_character_pose *prior = candidate->poses; okay && prior != pose; prior = prior->next)
            if (prior->physical_seat == pose->physical_seat && qa_actor_id_equal(prior->actor, pose->actor)) okay = false;
    }
    okay = okay && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    if (okay) {
        candidate->next = target->next; candidate->view.assets = target->view.assets;
        *target = *candidate; candidate->view.assets = NULL; candidate->poses = NULL; candidate->appearance_lease = NULL;
        candidate->view.selection.release = NULL;
        for (unsigned i = 0; i < 8; ++i) {
            target->view.resources[i] = target->resources[i]; target->view.receipts[i] = target->receipts + i;
            candidate->resources[i] = NULL; candidate->receipts[i] = (qa_vfs_acquisition){0};
        }
        target->view.animation = &target->animation;
        for (frontend_selected_character_pose *pose = target->poses; pose; pose = pose->next) pose->owner = target;
    }
    frontend_selected_character_dispose(candidate);
    if (!okay && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved selected character leaves its genuine declaration or holders");
    return okay;
}
bool frontend_selected_character_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend_selected_character_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character topology retains an active registry or output");
    for (const frontend_selected_character *owner = frontend->selected_characters; owner; owner = owner->next)
        if (owner->restoring || !owner->view.selection.release || !owner->appearance_lease)
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected character topology lacks its real declaration continuation");
    return true;
}
bool frontend_selected_character_rebind_ready(const qa_frontend *owned, const qa_frontend *destination, qa_error *error)
{
    return owned && destination && (owned == destination || !destination->selected_characters) &&
        frontend_selected_character_idle(owned) ? true :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character rebind requires its actual idle source and empty destination");
}
void frontend_selected_character_rebind(qa_frontend *owned, qa_frontend *destination)
{
    if (!owned || !destination || owned == destination) return;
    destination->selected_characters = owned->selected_characters; owned->selected_characters = NULL;
    for (frontend_selected_character *owner = destination->selected_characters; owner; owner = owner->next)
        owner->frontend = destination;
}
