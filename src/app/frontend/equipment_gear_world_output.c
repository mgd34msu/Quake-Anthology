#include "equipment_gear_private.h"
#include "equipment_gear_world_output.h"
#include "../../presentation/q3_native/pose.h"

#include <math.h>

struct frontend_equipment_gear_world_output {
    equipment_gear_content *owner;
    application_equipment_gear_world_view source;
    qa_q3_ref_entity projectile, *segments;
    size_t segment_count;
    const qa_material *beam;
    qa_vec3 start, end;
    double width;
    void *context;
    bool (*current)(void *);
    bool projectile_present;
};
static float length(qa_vec3 vector)
{
    float square = (((vector.x * vector.x) + (vector.y * vector.y)) + (vector.z * vector.z));
    return ((float)(sqrt((double)square)));
}
static qa_vec3 scaled(qa_vec3 vector, double scale)
{ return qa_v3(((float)(vector.x * scale)), ((float)(vector.y * scale)), ((float)(vector.z * scale))); }
static void model_axis(qa_vec3 angles, qa_vec3 axis[3])
{
    const double radians = 6.28318530717958647693 / 360.0;
    float yaw = ((float)(angles.y * radians)), pitch = ((float)(angles.x * radians)), roll = ((float)(angles.z * radians));
    float sy = ((float)(sin(yaw))), cy = ((float)(cos(yaw))), sp = ((float)(sin(pitch))), cp = ((float)(cos(pitch)));
    float sr = ((float)(sin(roll))), cr = ((float)(cos(roll))), rp = (-sr * sp);
    axis[0] = qa_v3((cp * cy), (cp * sy), -sp);
    axis[1] = qa_v3(-((rp * cy) + (-cr * -sy)),
        -((rp * sy) + (-cr * cy)), -(-sr * cp));
    rp = (cr * sp);
    axis[2] = qa_v3(((rp * cy) + (-sr * -sy)),
        ((rp * sy) + (-sr * cy)), (cr * cp));
}
static qa_vec3 vector_angles(qa_vec3 vector)
{
    float yaw, pitch, pi = 3.14159274101257324219f;
    if (vector.x == 0 && vector.y == 0) {
        yaw = 0; pitch = vector.z > 0 ? 90 : 270;
    } else {
        yaw = vector.x != 0 ? ((float)((((float)(atan2(vector.y, vector.x))) * 180) / (double)pi)) :
            vector.y > 0 ? 90 : 270;
        if (yaw < 0) yaw = (yaw + 360);
        float forward = ((float)(sqrt(((vector.x * vector.x) + (vector.y * vector.y)))));
        pitch = ((float)((((float)(atan2(vector.z, forward))) * 180) / (double)pi));
        if (pitch < 0) pitch = (pitch + 360);
    }
    return qa_v3(-pitch, yaw, 0);
}
static bool source_current(const frontend_equipment_gear_world_output *output)
{
    return output && output->owner && output->current(output->context) &&
        qa_application_equipment_content_current(output->owner->frontend->application, &output->owner->view.source) &&
        application_equipment_gear_world_current(output->owner->frontend->application, &output->source);
}
void frontend_equipment_gear_world_destroy(frontend_equipment_gear_world_output *output)
{
    if (!output) return;
    if (output->owner) --output->owner->world_users;
    free(output->segments); free(output);
}

static bool content_prepare(qa_frontend *frontend, const application_equipment_gear_world_view *source,
    void *context, bool (*current)(void *), equipment_gear_content **out, qa_error *error)
{
    if (!frontend->equipment_gear) {
        frontend->equipment_gear = calloc(1, sizeof(*frontend->equipment_gear));
        if (!frontend->equipment_gear) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining world gear registry roster");
    }
    frontend_equipment_gear *equipment = frontend->equipment_gear;
    equipment_gear_content *owner = equipment->contents;
    while (owner && owner->view.source.owner != source->source.source.gear_owner) owner = owner->next;
    bool fresh = !owner; equipment->admitting = true;
    qa_application_equipment_content actual; const application_q3_grapple_definition *definition = NULL;
    bool okay = frontend_equipment_gear_source(frontend, source->source.source.gear_owner, &actual, &definition, error);
    if (okay && fresh) {
        frontend_visual_owner_view content; qa_q3_product product;
        okay = frontend_visual_media_acquire(frontend, actual.selected_owner, QA_GAME_Q3, &content, error) &&
            qa_application_equipment_q3_product_read(frontend->application, actual.selected_owner, &product, error) &&
            frontend_equipment_gear_content_create(frontend, &actual, definition, &content, product, &owner, error);
    }
    if (okay && (owner->restoring || owner->admitting || definition != source->source.source.definition ||
        definition != owner->view.definition || !qa_application_equipment_content_current(frontend->application, &owner->view.source) ||
        !current(context) || !application_equipment_gear_world_current(frontend->application, source)))
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "World gear admission changed its actual source or recipient");
    equipment->admitting = false;
    if (!okay) { if (fresh) frontend_equipment_gear_content_dispose(owner); return false; }
    if (fresh) {
        if (equipment->tail) equipment->tail->next = owner; else equipment->contents = owner;
        equipment->tail = owner;
    }
    *out = owner; return true;
}
static bool register_model(frontend_equipment_gear_world_output *output, const char *path,
    int32_t *handle, qa_error *error)
{
    if (!source_current(output))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "World gear model lost its retained source");
    if (!qa_q3_register_model(output->owner->view.assets, path, handle, error)) return false;
    if (!*handle) return frontend_fail(error, QA_ERROR_NOT_FOUND, "World gear model has no actual retained resource");
    return source_current(output) || frontend_fail(error, QA_ERROR_ARGUMENT, "World gear model changed its source during admission");
}
static qa_q3_ref_entity model_ref(int32_t handle, qa_vec3 origin, qa_vec3 angles)
{
    qa_q3_ref_entity ref = {.kind = QA_Q3_REF_MODEL, .model = handle,
        .origin = origin, .old_origin = origin, .lighting_origin = origin, .color = {255, 255, 255, 255}};
    model_axis(angles, ref.axis); return ref;
}

bool frontend_equipment_gear_world_prepare(qa_frontend *frontend,
    const application_equipment_gear_world_view *source, qa_actor_id viewer,
    void *context, bool (*current)(void *), frontend_equipment_gear_world_output **out, qa_error *error)
{
    if (!frontend || !frontend->application || !source || !out || *out || !current || !current(context) ||
        (viewer.registry ? !qa_actors_get(qa_session_actors(qa_application_session(frontend->application)), viewer) :
            viewer.slot || viewer.generation) ||
        (frontend->equipment_gear && frontend->equipment_gear->admitting) ||
        !application_equipment_gear_world_current(frontend->application, source) ||
        !qa_vec_finite(source->player_body.origin) || !qa_vec_finite(source->player_angles) ||
        !qa_vec_finite(source->tether_body.origin) || !qa_vec_finite(source->tether_body.angles) || !isfinite(source->view_height))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "World gear preparation requires its actual completed hook and recipient");
    equipment_gear_content *owner = NULL;
    if (!content_prepare(frontend, source, context, current, &owner, error)) return false;
    if (owner->world_users == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_MEMORY, "World gear outputs exceed their actual owner lifetime");
    frontend_equipment_gear_world_output *output = calloc(1, sizeof(*output));
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining completed world gear output");
    ++owner->world_users; output->owner = owner; output->source = *source;
    output->context = context; output->current = current;
    const application_q3_grapple_definition *definition = owner->view.definition;
    bool okay = true; int32_t handle = 0;
    if (definition->presentation.projectile_model && definition->presentation.projectile_model[0]) {
        okay = register_model(output, definition->presentation.projectile_model, &handle, error);
        if (okay) { output->projectile = model_ref(handle, source->tether_body.origin, source->tether_body.angles);
            output->projectile_present = true; }
    }
    bool local = qa_actor_id_equal(viewer, source->source.actor);
    output->start = qa_vec_add(source->player_body.origin, qa_v3(0, 0,
        source->offhand ? 26 : local ? source->view_height : 0));
    if (source->offhand) {
        qa_vec3 aim[3]; q3n_angles_axis(source->player_angles, aim);
        output->start = qa_vec_add(output->start, scaled(aim[1], local ? 10 : 6));
        if (local) output->start = qa_vec_add(output->start, scaled(aim[0], 3));
    }
    output->end = source->tether_body.origin;
    if (okay && definition->presentation.cable_shader) {
        output->start = qa_vec_add(source->player_body.origin, qa_v3(0, 0, source->view_height));
        output->width = definition->presentation.cable_width;
        qa_scene_image_options images = {.family = QA_SCENE_Q3, .wrap = QA_SCENE_REPEAT,
            .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .usage = QA_IMAGE_USAGE_WALL, .transparent_index = -1};
        okay = qa_material_register_kind(owner->view.content.materials, definition->presentation.cable_path,
            &images, QA_MATERIAL_DYNAMIC, &output->beam, error);
        if (okay && !output->beam)
            okay = frontend_fail(error, QA_ERROR_FORMAT, "World gear shader cable has no retained named material");
    } else if (okay) {
        qa_vec3 delta = qa_vec_sub(output->start, output->end); float distance = length(delta);
        uint32_t segment = definition->presentation.cable_segment_length;
        double count = segment ? floor((double)distance / segment) : INFINITY;
        if (!isfinite(distance) || !isfinite(count) || count < 0 || count > 65536)
            okay = frontend_fail(error, QA_ERROR_FORMAT, "World grapple cable exceeds the authored segment limit");
        const char *path = !source->source.gear.pulling ? definition->presentation.cable_flight :
            distance > 64 ? definition->presentation.cable_pull : definition->presentation.cable_hold;
        if (okay) okay = register_model(output, path, &handle, error);
        if (okay) {
            output->segment_count = (size_t)count + 1;
            output->segments = calloc(output->segment_count, sizeof(*output->segments));
            if (!output->segments) okay = frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored world cable segments");
        }
        if (okay) {
            qa_vec3 direction = distance == 0 ? qa_v3(0, 0, 0) : scaled(delta, ((float)(1.0 / distance)));
            qa_q3_ref_entity ref = model_ref(handle, source->tether_body.origin, vector_angles(direction));
            for (size_t i = 0; i < output->segment_count; ++i) {
                ref.origin = qa_vec_sub(output->start, scaled(direction, (double)(i + 1) * segment));
                if (!qa_vec_finite(ref.origin)) { okay = frontend_fail(error, QA_ERROR_FORMAT, "World cable segment leaves finite scene coordinates"); break; }
                output->segments[i] = ref;
            }
        }
    }
    if (okay && (!qa_vec_finite(output->start) || !source_current(output)))
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "World gear output changed its actual source during preparation");
    if (!okay) { frontend_equipment_gear_world_destroy(output); return false; }
    *out = output; return true;
}
bool frontend_equipment_gear_world_submit(frontend_equipment_gear_world_output *output,
    qa_q3_presentation *presentation, const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    if (!source_current(output) || !presentation || !options || !frame || options->world.no_world)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "World gear submission requires its actual renderer lease and source");
    if (output->projectile_present && !qa_q3_presentation_selected_world_models(presentation, output->owner->view.assets,
        &output->projectile, 1, options, frame, error)) return false;
    if (output->segments && !qa_q3_presentation_selected_world_models(presentation, output->owner->view.assets,
        output->segments, output->segment_count, options, frame, error)) return false;
    if (output->beam && !qa_q3_presentation_selected_world_beam(presentation, output->owner->view.assets,
        output->beam, output->start, output->end, output->width, options, frame, error)) return false;
    return source_current(output) || frontend_fail(error, QA_ERROR_ARGUMENT, "World gear source changed during its actual submission");
}
