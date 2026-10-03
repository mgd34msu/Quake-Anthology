#include "native_character.h"
#include "selected_character_lifetime.h"
#include "save_private.h"
#include <stdlib.h>
#include <string.h>

typedef struct character_output {
    struct character_output *next;
    qa_actor_id actor;
    frontend_selected_character_output *output;
    frontend_selected_character_pass *passes;
    size_t pass_count, pass_capacity;
    uint32_t first_order;
} character_output;
struct frontend_native_character {
    qa_frontend *frontend;
    frontend_native_q3 *row;
    frontend_native_q3_view owners;
    const q3n_frame *frame;
    character_output *outputs, *tail;
    int32_t previous_time, frame_milliseconds;
    uint32_t first_order;
    bool entered;
};
static bool current(const frontend_native_character *owner, const q3n_frame *frame)
{
    return owner && owner->entered && frame && owner->frame == frame &&
        frame->application == owner->frontend->application && frame->reader == owner->owners.reader &&
        frame->assets == owner->owners.assets && frame->presentation == owner->owners.presentation &&
        frame->seat == owner->owners.launch_seat && frame->physical_presentation_seat == owner->owners.seat &&
        qa_actor_id_equal(frame->viewing_actor, owner->owners.actor) &&
        qa_application_native_q3_presentation_current(frame->application, &frame->source);
}
static bool recipient_current(void *context)
{ frontend_native_character *owner = context; return current(owner, owner->frame); }
static character_output *output_for(const frontend_native_character *owner, qa_actor_id actor)
{
    for (character_output *output = owner ? owner->outputs : NULL; output; output = output->next)
        if (qa_actor_id_equal(output->actor, actor)) return output;
    return NULL;
}
bool frontend_native_character_create(qa_frontend *frontend, frontend_native_q3 *row,
    frontend_native_character **out, qa_error *error)
{
    frontend_native_q3_view view;
    if (!frontend || !out || *out || !frontend_native_q3_factory_view(row, &view, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character constructor requires its real receiver and heaps");
    frontend_native_character *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining native character frame owner");
    owner->frontend = frontend; owner->row = row; owner->owners = view; *out = owner; return true;
}
bool frontend_native_character_begin(frontend_native_character *owner, const q3n_frame *frame, qa_error *error)
{
    if (!owner || owner->entered || owner->outputs)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character frame requires its returned output owner");
    owner->entered = true; owner->frame = frame;
    if (!current(owner, frame)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character frame lost its actual source cut");
    if (!frontend_selected_character_refresh(owner->frontend, error)) return false;
    uint32_t difference = (uint32_t)frame->time - (uint32_t)owner->previous_time;
    int32_t elapsed; memcpy(&elapsed, &difference, sizeof(elapsed));
    owner->frame_milliseconds = elapsed < 0 ? 0 : elapsed;
    qa_actor_registry *actors = qa_world_actors(qa_application_world(frame->application));
    const qa_actor_record *record; uint32_t cursor = 0, order = 0;
    while (qa_actors_next(actors, &cursor, &record)) {
        qa_actor_id actor = record->id; qa_application_selected_q3_character source; bool found;
        if (!qa_application_selected_q3_character_read(frame->application, actor, &source, &found, error)) return false;
        if (!found) continue;
        qa_application_q3_asset_selection appearance; bool appearance_found;
        if (!qa_application_q3_asset_selection_read(frame->application, actor, QA_ROLE_SKIN,
            &appearance, &appearance_found, error)) return false;
        if (!appearance_found || (source.provider == frame->source.source_owner &&
            appearance.provider == frame->source.source_owner)) continue;
        frontend_selected_character_pose *pose = NULL; bool admitted = false;
        if (!frontend_selected_character_prepare(owner->frontend, frame->seat, owner->owners.seat,
            &source, owner, recipient_current, &pose, &admitted, error)) return false;
        if (!admitted) continue;
        character_output *output = calloc(1, sizeof(*output));
        if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining admitted full actor body output");
        if (owner->tail) owner->tail->next = output; else owner->outputs = output; owner->tail = output;
        output->actor = actor; output->first_order = order;
        frontend_selected_character_frame settings = {.time = frame->time,
            .frame_milliseconds = owner->frame_milliseconds, .swing_speed = .3f,
            .personal_model = qa_actor_id_equal(actor, frame->viewing_actor)};
        if (!frontend_selected_character_build(pose, &source, &settings, &output->output, error) ||
            !current(owner, frame)) return false;
        size_t parts = frontend_selected_character_output_count(output->output);
        if (parts > 1022u - order)
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected character frame exceeds the actual Q3 model submission extent");
        order += (uint32_t)parts;
    }
    return current(owner, frame);
}
void frontend_native_character_end(frontend_native_character *owner)
{
    if (!owner) return;
    while (owner->outputs) {
        character_output *output = owner->outputs; owner->outputs = output->next;
        frontend_selected_character_output_destroy(output->output); free(output->passes); free(output);
    }
    owner->tail = NULL;
    if (owner->entered && owner->frame) owner->previous_time = owner->frame->time;
    owner->entered = false; owner->frame = NULL; owner->frame_milliseconds = 0;
}
bool frontend_native_character_idle(const frontend_native_character *owner)
{ return owner && !owner->entered && !owner->frame && !owner->outputs; }
bool frontend_native_character_destroy(frontend_native_character *owner, qa_error *error)
{
    if (!frontend_native_character_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character retirement retains an entered frame or pose output");
    free(owner); return true;
}
bool frontend_native_character_rebind_ready(const frontend_native_character *owner,
    const qa_frontend *frontend, qa_error *error)
{
    return owner && owner->frontend == frontend && frontend_native_character_idle(owner) ? true :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Native character rebind requires its actual idle frontend owner");
}
void frontend_native_character_rebind(frontend_native_character *owner, qa_frontend *frontend)
{ if (owner) owner->frontend = frontend; }
bool frontend_native_character_admitted(const frontend_native_character *owner, qa_actor_id actor)
{
    character_output *output = output_for(owner, actor);
    return current(owner, owner ? owner->frame : NULL) && output &&
        frontend_selected_character_output_current(output->output);
}
bool frontend_native_character_origin(const frontend_native_character *owner, const q3n_frame *frame,
    qa_actor_id actor, qa_vec3 *origin, bool *found, qa_error *error)
{
    if (!origin || !found || !current(owner, frame))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character origin requires its actual entered frame");
    *found = false; character_output *output = output_for(owner, actor);
    if (!output) return true;
    if (!frontend_selected_character_output_origin(output->output, actor, origin, error)) return false;
    *found = true; return true;
}
bool frontend_native_character_body_hidden(frontend_native_character *owner, const q3n_frame *frame,
    const qa_application_native_q3_entity *actual, bool *hidden, qa_error *error)
{
    if (!hidden || !actual || !current(owner, frame))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native body visibility requires its actual admitted frame");
    character_output *output = output_for(owner, actual->binding.actor);
    *hidden = output && !frontend_selected_character_output_count(output->output);
    return !output || frontend_selected_character_output_current(output->output);
}
bool frontend_native_character_body(frontend_native_character *owner, const q3n_frame *frame,
    const qa_application_native_q3_entity *actual, uint32_t part, const qa_q3_ref_entity *material,
    bool base, bool *consumed, qa_error *error)
{
    (void)base;
    if (!consumed || !actual || !material || part > 3 || !current(owner, frame))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native body material requires its actual frame and part");
    *consumed = false; character_output *output = output_for(owner, actual->binding.actor);
    if (!output) return true;
    if (!frontend_selected_character_output_current(output->output)) return false;
    if (output->pass_count == output->pass_capacity) {
        size_t capacity = output->pass_capacity ? output->pass_capacity * 2 : 8;
        if (capacity < output->pass_capacity || capacity > SIZE_MAX / sizeof(*output->passes))
            return frontend_fail(error, QA_ERROR_MEMORY, "Selected body pass extent is exhausted");
        void *grown = realloc(output->passes, capacity * sizeof(*output->passes));
        if (!grown) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual source body material pass");
        output->passes = grown; output->pass_capacity = capacity;
    }
    output->passes[output->pass_count++] = (frontend_selected_character_pass){.part = part, .helper = part, .material = *material};
    *consumed = true; return true;
}
bool frontend_native_character_packet(frontend_native_character *owner, const q3n_frame *frame,
    const qa_application_native_q3_entity *actual, const qa_q3_ref_entity *material,
    bool *consumed, qa_error *error)
{ return frontend_native_character_body(owner, frame, actual, 3, material, true, consumed, error); }
bool frontend_native_character_torso(const frontend_native_character *owner, qa_actor_id actor,
    const qa_q3_presentation_assets **assets, const qa_q3_ref_entity **torso, bool *found, qa_error *error)
{
    if (!assets || !torso || !found || !current(owner, owner ? owner->frame : NULL))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected torso requires its actual entered character frame");
    *found = false; *assets = NULL; *torso = NULL; character_output *output = output_for(owner, actor);
    if (!output) return true;
    if (!frontend_selected_character_output_current(output->output)) return false;
    *torso = frontend_selected_character_output_part(output->output, 1);
    if (*torso) { *assets = frontend_selected_character_output_assets(output->output); *found = true; }
    return true;
}
bool frontend_native_character_submit(frontend_native_character *owner, const qa_q3_scene_options *options,
    qa_scene_frame *frame, qa_error *error)
{
    if (!options || !frame || !current(owner, owner ? owner->frame : NULL) || frame != &owner->frontend->frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected characters require the actual native submission lease");
    if (options->world.no_world) return true;
    for (character_output *output = owner->outputs; output; output = output->next) {
        bool okay = output->pass_count ? frontend_selected_character_output_passes(output->output,
            owner->owners.presentation, owner->owners.assets, output->passes, output->pass_count,
            options, owner->first_order + output->first_order, frame, error) :
            frontend_selected_character_output_submit(output->output, owner->owners.presentation,
                options, owner->first_order + output->first_order, frame, error);
        if (!okay || !current(owner, owner->frame)) return false;
    }
    return true;
}
bool frontend_native_character_prepare_view(frontend_native_character *owner,
    qa_q3_scene_options *options, qa_error *error)
{
    if (!options || !current(owner, owner ? owner->frame : NULL))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected body order requires its actual prepared native frame");
    size_t count = 0;
    if (!options->world.no_world)
        for (character_output *output = owner->outputs; output; output = output->next)
            count += frontend_selected_character_output_count(output->output);
    if (options->first_entity >= 1022 || count > 1022u - options->first_entity)
        return frontend_fail(error, QA_ERROR_FORMAT, "Selected bodies exceed the actual native source scene order");
    owner->first_order = options->first_entity; options->first_entity += (uint32_t)count;
    return true;
}
bool frontend_native_character_checkpoint(const frontend_native_character *owner, qa_buffer *out, qa_error *error)
{
    if (!frontend_native_character_idle(owner) || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character capture requires its actual returned frame");
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','N','C'}; int32_t time = owner->previous_time;
    bool okay = qa_source_save_writer(&io, qa_application_session(owner->frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && qa_source_save_i32(&io, &time) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_native_character_restore(frontend_native_character *owner, qa_bytes bytes, qa_error *error)
{
    if (!frontend_native_character_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native character import requires its actual empty frame owner");
    qa_source_save_io io = {0}; uint8_t magic[4]; int32_t time;
    bool okay = qa_source_save_reader(&io, qa_application_session(owner->frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFNC", 4) &&
        qa_source_save_i32(&io, &time) &&
        qa_source_save_finish(&io, NULL);
    if (okay) owner->previous_time = time;
    qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid native character frame continuation");
    return okay;
}
