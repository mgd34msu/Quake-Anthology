#ifndef QA_Q2_EXTENSION_CHECKPOINT_INTERNAL_H
#define QA_Q2_EXTENSION_CHECKPOINT_INTERNAL_H
#include "../internal.h"
#include "qa/game_q2_entities.h"

static inline bool q2_saved_resource(qa_q2_game *g, qa_string_id id) {
    return id == 0 || qa_strings_cstr(qa_session_strings(g->services.session), id) != NULL;
}
static inline bool q2_saved_visual(qa_q2_game *g, const qa_q2_visual *v) {
    if (!isfinite(v->scale) || !isfinite(v->alpha))
        return false;
    for (size_t i = 0; i < 4; i++)
        if (!q2_saved_resource(g, v->models[i]))
            return false;
    return true;
}
static inline bool q2_saved_fog(const qa_q2_fog *f) {
    return isfinite(f->density) && isfinite(f->sky_factor) && qa_vec_finite(f->color) &&
           qa_vec_finite(f->start_color) && qa_vec_finite(f->end_color) &&
           isfinite(f->start_distance) && isfinite(f->end_distance) && isfinite(f->falloff) &&
           isfinite(f->height_density);
}
static inline bool q2_saved_landmark(qa_q2_game *g, const qa_q2_landmark *l) {
    return !l->player.registry && q2_saved_resource(g, l->name) &&
           qa_vec_finite(l->relative_origin) && qa_vec_finite(l->relative_velocity) &&
           qa_vec_finite(l->relative_view_angles);
}
static inline bool q2_saved_array(const void *source, size_t count, size_t width, void **out,
                                  qa_error *e) {
    *out = NULL;
    if (!count)
        return true;
    if (!source || count > SIZE_MAX / width) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 checkpoint array");
        return false;
    }
    void *copy = malloc(count * width);
    if (!copy) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Copying Q2 checkpoint array");
        return false;
    }
    memcpy(copy, source, count * width);
    *out = copy;
    return true;
}
static inline bool q2_saved_inventory(qa_q2_game *g, const qa_inventory_entry *items, size_t count,
                                      qa_error *e) {
    if ((count && !items) || count > SIZE_MAX / sizeof(*items))
        return false;
    for (size_t i = 0; i < count; i++) {
        qa_inventory_entry normalized;
        if (!items[i].item || !q2_saved_resource(g, items[i].item) ||
            !qa_inventory_validate_entry(items + i, &normalized, e))
            return false;
        for (size_t j = 0; j < i; j++)
            if (items[j].item == items[i].item)
                return false;
    }
    return true;
}
#endif
