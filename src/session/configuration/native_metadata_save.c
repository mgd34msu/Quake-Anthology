#include "qa/launch_native_save.h"
#include <string.h>

static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}
static bool text_equal(const char *left, const char *right)
{
    return (!left && !right) || (left && right && !strcmp(left, right));
}
static bool selection_equal(const qa_launch_provider *a, const qa_launch_provider *b)
{
    const qa_clock_config *x = &a->clock, *y = &b->clock;
    return a->product == b->product && a->runtime == b->runtime &&
        text_equal(a->instance, b->instance) && text_equal(a->implementation, b->implementation) &&
        text_equal(a->artifact, b->artifact) && text_equal(a->component, b->component) &&
        a->options.size == b->options.size && (!a->options.size ||
            (a->options.data && b->options.data && !memcmp(a->options.data, b->options.data, a->options.size))) &&
        x->kind == y->kind && x->initial_time_ns == y->initial_time_ns &&
        x->interval_ns == y->interval_ns && x->minimum_frame_ns == y->minimum_frame_ns &&
        x->maximum_frame_ns == y->maximum_frame_ns && x->initial_lead_ns == y->initial_lead_ns &&
        x->maximum_steps == y->maximum_steps;
}
static bool receipt_equal(const qa_vfs_acquisition *a, const qa_vfs_acquisition *b)
{
    return (!a && !b) || (a && b && a->mount == b->mount && a->resource_id == b->resource_id &&
        text_equal(a->path, b->path) && text_equal(a->lookup_path, b->lookup_path) &&
        text_equal(a->link_source, b->link_source) && text_equal(a->link_target, b->link_target));
}
bool qa_launch_instance_restore_native_metadata(const qa_launch_instance *source,
    const qa_launch_restored_instance *saved, uint64_t roles,
    qa_launch_instance_lease **out, qa_error *error)
{
    if (!source || !source->storage || !source->state || !saved || !out || *out ||
        source->selection.runtime != QA_PROGRAM_BUILTIN || !source->content ||
        !roles || (roles >> QA_ROLE_COUNT) || !(roles & QA_ROLE_BIT(QA_ROLE_ENTITIES)))
        return fail(error, "Native metadata needs its actual GAME storage and saved roles");
    qa_catalog *catalog = qa_launch_instance_catalog(source);
    const qa_product *product = catalog ? qa_catalog_product(catalog, source->selection.product) : NULL;
    if (!product || product->family != QA_GAME_Q3 || catalog != saved->catalog ||
        source->content != saved->content || !selection_equal(&source->selection, &saved->selection) ||
        source->artifact != saved->artifact || source->declaration != saved->declaration ||
        !receipt_equal(source->artifact_acquisition, saved->artifact_acquisition) ||
        source->interface_count != saved->interface_count || source->behavior_count != saved->behavior_count ||
        (saved->interface_count && (!saved->interfaces || !source->interfaces)) ||
        (saved->behavior_count && (!saved->behaviors || !source->behaviors)))
        return fail(error, "Saved native metadata differs from its restored immutable GAME owner");
    qa_resource_pool *pool = qa_vfs_resources(source->content);
    if (!pool || (saved->artifact && qa_resource_pool_find(pool, qa_resource_id(saved->artifact)) != saved->artifact) ||
        (saved->declaration && qa_resource_pool_find(pool, qa_resource_id(saved->declaration)) != saved->declaration) ||
        (saved->artifact && (!saved->artifact_acquisition ||
            saved->artifact_acquisition->resource_id != qa_resource_id(saved->artifact) ||
            !qa_vfs_acquisition_retained(saved->content, saved->artifact_acquisition, error))))
        return fail(error, "Native metadata resource recipe leaves its real retained pool");
    for (size_t i = 0; i < saved->interface_count; ++i) {
        const qa_launch_resource *a = source->interfaces + i, *b = saved->interfaces + i;
        if (a->product != b->product || a->resource != b->resource || !b->resource ||
            !text_equal(a->path, b->path) ||
            qa_resource_pool_find(pool, qa_resource_id(b->resource)) != b->resource)
            return fail(error, "Native metadata interface order or resource owner changed");
    }
    for (size_t i = 0; i < saved->behavior_count; ++i) {
        const qa_catalog_weapon_behavior *behavior = saved->behaviors[i];
        if (!behavior || behavior != source->behaviors[i] ||
            qa_catalog_weapon_behavior_find(catalog, saved->selection.product, behavior->id) != behavior)
            return fail(error, "Native metadata behavior leaves its actual retained catalog");
    }
    qa_launch_instance retained = *source;
    retained.roles = roles;
    return qa_launch_instance_retain_metadata(&retained, out, error);
}
