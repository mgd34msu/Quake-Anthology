#include "internal.h"
#include "save_content.h"
#include "guest_native_q2_private.h"
#include "map_private.h"
#include "save_private.h"
#include "bots_save_private.h"
#include "network_q1_signon.h"
#include "portals.h"
#include "q3_world_restart.h"
#include "native_q3_clients.h"
#include "rankings.h"
#include "bots_round.h"
#include "guest_q3_restart.h"
#include "q3_campaign_launch.h"
#include "startup_flow.h"
#include "native_q2_checkpoint.h"
#include "native_q3_checkpoint.h"

#include <stdlib.h>
#include <string.h>

static void close_world(void *opaque)
{
    (void)qa_world_destroy(opaque, NULL);
}

static const qa_launch_resource *map_resource(
    const qa_launch_snapshot *snapshot)
{
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    for (size_t index = 0; index < qa_launch_snapshot_resource_count(snapshot);
         ++index) {
        const qa_launch_resource *resource =
            qa_launch_snapshot_resource(snapshot, index);
        if (resource->product == choices->world.geometry &&
            strcmp(resource->path, choices->world.map) == 0)
            return resource;
    }
    return NULL;
}

static bool same_text(const char *left, const char *right)
{
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static bool same_scope(qa_launch_scope left, qa_launch_scope right)
{
    if (left.kind != right.kind)
        return false;
    if (left.kind == QA_SCOPE_ACTOR)
        return qa_actor_id_equal(left.actor, right.actor);
    return left.kind != QA_SCOPE_SEAT || left.seat == right.seat;
}

static bool same_world_entities(const qa_launch_choices *left,
                                const qa_launch_choices *right)
{
    qa_launch_scope scope = {.kind = QA_SCOPE_WORLD};
    const qa_launch_binding *a =
        qa_launch_binding_for(left, scope, QA_ROLE_ENTITIES, "");
    const qa_launch_binding *b =
        qa_launch_binding_for(right, scope, QA_ROLE_ENTITIES, "");
    return a != NULL && b != NULL && same_text(a->instance, b->instance) &&
           same_text(a->definition, b->definition);
}

static bool same_mode(const qa_launch_mode *left,
                      const qa_launch_mode *right)
{
    const qa_mode_rules *a = &left->rules;
    const qa_mode_rules *b = &right->rules;
    if (!same_text(left->instance, right->instance) ||
        a->source != b->source || a->kind != b->kind ||
        a->forced_team != b->forced_team ||
        a->frag_limit != b->frag_limit ||
        a->capture_limit != b->capture_limit ||
        a->warmup_seconds != b->warmup_seconds ||
        a->competition != b->competition ||
        a->setup_seconds != b->setup_seconds ||
        a->countdown_seconds != b->countdown_seconds ||
        a->match_seconds != b->match_seconds ||
        a->max_game_players != b->max_game_players ||
        a->election_percent != b->election_percent ||
        a->teamplay != b->teamplay || a->rune_mask != b->rune_mask ||
        a->vote_limit != b->vote_limit || a->flags != b->flags ||
        a->referee_flags != b->referee_flags ||
        a->time_limit_minutes != b->time_limit_minutes ||
        a->obelisk_health != b->obelisk_health ||
        a->obelisk_regen != b->obelisk_regen ||
        a->obelisk_regen_ns != b->obelisk_regen_ns ||
        a->obelisk_respawn_ns != b->obelisk_respawn_ns ||
        a->enabled != b->enabled ||
        a->friendly_fire != b->friendly_fire ||
        a->force_join != b->force_join ||
        a->match_lock != b->match_lock || a->paused != b->paused ||
        a->auto_lock != b->auto_lock || a->relics != b->relics ||
        a->single_player_active != b->single_player_active ||
        a->tournament_restart != b->tournament_restart ||
        a->q2_rerelease != b->q2_rerelease ||
        a->start_map != b->start_map ||
        a->force_balance != b->force_balance ||
        a->voting_disabled != b->voting_disabled ||
        left->primary_score != right->primary_score ||
        !same_text(left->forced_team, right->forced_team))
        return false;
    for (size_t index = 0; index < 3; ++index)
        if (a->teams[index] != b->teams[index] ||
            !same_text(left->teams[index], right->teams[index]))
            return false;
    return true;
}

static bool same_equipment(const qa_launch_equipment *left,
                           const qa_launch_equipment *right)
{
    const qa_equipment_selection *a = &left->selection;
    const qa_equipment_selection *b = &right->selection;
    return same_scope(left->scope, right->scope) &&
           same_text(left->instance, right->instance) &&
           same_text(left->grapple_source, right->grapple_source) &&
           same_text(left->grenade_source, right->grenade_source) &&
           a->grapple == b->grapple && a->binding == b->binding &&
           a->retain_on_weapon_change == b->retain_on_weapon_change &&
           a->release_on_jump == b->release_on_jump &&
           a->release_on_teleport == b->release_on_teleport &&
           a->grenades.enabled == b->grenades.enabled &&
           a->grenades.infinite_ammo == b->grenades.infinite_ammo &&
           a->grenades.initial_ammo == b->grenades.initial_ammo &&
           a->grenades.capacity == b->grenades.capacity;
}

static bool same_loadout(const qa_launch_loadout *left,
                         const qa_launch_loadout *right)
{
    return same_scope(left->scope, right->scope) &&
           same_text(left->item, right->item) &&
           left->quantity == right->quantity &&
           left->capacity == right->capacity &&
           left->override_capacity == right->override_capacity &&
           left->drop_on_death == right->drop_on_death;
}

static bool same_monster(const qa_launch_monster *left,
                         const qa_launch_monster *right)
{
    return same_text(left->authored_classname, right->authored_classname) &&
           same_text(left->instance, right->instance) &&
           same_text(left->classname, right->classname) &&
           left->map_defined == right->map_defined;
}

static bool same_map_configuration(const qa_launch_snapshot *previous,
                                   const qa_launch_snapshot *candidate)
{
    if (previous == NULL)
        return false;
    const qa_launch_choices *left = qa_launch_snapshot_choices(previous);
    const qa_launch_choices *right = qa_launch_snapshot_choices(candidate);
    const qa_launch_resource *a = map_resource(previous);
    const qa_launch_resource *b = map_resource(candidate);
    if (a == NULL || b == NULL || a->product != b->product ||
        !same_text(a->path, b->path) ||
        !qa_sha256_equal(qa_resource_digest(a->resource),
                         qa_resource_digest(b->resource)) ||
        left->world.preset != right->world.preset ||
        left->world.presentation != right->world.presentation ||
        left->world.campaign != right->world.campaign ||
        left->world.skill != right->world.skill ||
        !same_text(left->world.start_command, right->world.start_command) ||
        left->world.explicit_spawn_point != right->world.explicit_spawn_point ||
        !same_text(left->world.spawn_point, right->world.spawn_point) ||
        !same_world_entities(left, right) ||
        left->mode_count != right->mode_count ||
        left->equipment_count != right->equipment_count ||
        left->monster_count != right->monster_count ||
        left->loadout_count != right->loadout_count)
        return false;
    for (size_t index = 0; index < left->mode_count; ++index)
        if (!same_mode(&left->modes[index], &right->modes[index]))
            return false;
    for (size_t index = 0; index < left->equipment_count; ++index)
        if (!same_equipment(&left->equipment[index],
                            &right->equipment[index]))
            return false;
    for (size_t index = 0; index < left->loadout_count; ++index)
        if (!same_loadout(&left->loadout[index], &right->loadout[index]))
            return false;
    for (size_t index = 0; index < left->monster_count; ++index)
        if (!same_monster(&left->monsters[index], &right->monsters[index]))
            return false;
    return true;
}

static bool contains(application_provider *const *providers, size_t count,
                     const application_provider *provider)
{
    for (size_t index = 0; index < count; ++index)
        if (providers[index] == provider)
            return true;
    return false;
}

static bool provider_roster(application_publication *publication,
                            qa_error *error)
{
    size_t count = qa_launch_snapshot_instance_count(publication->candidate);
    if (count > SIZE_MAX / sizeof(*publication->next))
        return application_fail(error, QA_ERROR_MEMORY,
                                "provider roster is too large");
    publication->next = calloc(count == 0 ? 1 : count,
                               sizeof(*publication->next));
    if (publication->next == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain candidate provider roster");
    for (size_t index = 0; index < count; ++index) {
        const qa_launch_instance *instance =
            qa_launch_snapshot_instance(publication->candidate, index);
        if (instance == NULL || instance->state == NULL)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "candidate provider has no prepared state");
        application_provider *provider = instance->state;
        if (provider->application == NULL)
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "candidate provider lost its application owner");
        for (size_t prior = 0; prior < index; ++prior)
            if (publication->next[prior] == provider)
                return application_fail(error, QA_ERROR_ARGUMENT,
                                        "candidate repeats one provider state");
        publication->next[publication->next_count++] = provider;
    }
    return true;
}

static bool removed_roster(qa_application *application,
                           application_publication *publication,
                           qa_error *error)
{
    size_t count = 0;
    for (size_t index = 0; index < application->provider_count; ++index)
        if (!contains(publication->next, publication->next_count,
                      application->providers[index]))
            ++count;
    if (count > SIZE_MAX / sizeof(*publication->removed))
        return application_fail(error, QA_ERROR_MEMORY,
                                "retiring provider roster is too large");
    publication->removed = calloc(count == 0 ? 1 : count,
                                  sizeof(*publication->removed));
    if (publication->removed == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain retiring provider roster");
    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (!contains(publication->next, publication->next_count, provider))
            publication->removed[publication->removed_count++] = provider;
    }
    return true;
}

static qa_actor_owner retiring_component(application_publication *publication,
                                         application_provider *provider,
                                         bool *used)
{
    for (size_t index = 0; index < publication->removed_count; ++index)
        if (!used[index] && publication->removed[index]->component_attached &&
            publication->removed[index]->owner == provider->owner) {
            used[index] = true;
            return provider->owner;
        }
    for (size_t index = 0; index < publication->removed_count; ++index)
        if (!used[index] && publication->removed[index]->component_attached) {
            used[index] = true;
            return publication->removed[index]->owner;
        }
    return 0;
}

static bool construct_and_reserve(qa_application *application,
                                  application_publication *publication,
                                  const qa_save_image *image,
                                  qa_error *error)
{
    if (publication->next_count > SIZE_MAX / sizeof(*publication->admissions))
        return application_fail(error, QA_ERROR_MEMORY,
                                "provider admission roster is too large");
    publication->admissions =
        calloc(publication->next_count == 0 ? 1 : publication->next_count,
               sizeof(*publication->admissions));
    bool *used = calloc(publication->removed_count == 0
                            ? 1
                            : publication->removed_count,
                        sizeof(*used));
    if (publication->admissions == NULL || used == NULL) {
        free(used);
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain provider admissions");
    }

    qa_catalog *catalog =
        qa_launch_snapshot_catalog(publication->candidate);
    const qa_launch_choices *choices =
        qa_launch_snapshot_choices(publication->candidate);
    qa_world *world = publication->initial_world != NULL
                          ? publication->initial_world
                          : application->world;
    size_t primary = 0;
    while (primary < publication->next_count && publication->next[primary] != publication->map_provider)
        ++primary;
    if (primary == publication->next_count) {
        free(used);
        return application_fail(error, QA_ERROR_ARGUMENT, "provider construction has no actual primary map source");
    }
    const qa_launch_snapshot *routing_snapshot = application->routing_snapshot;
    application_provider **routing_providers = application->routing_providers;
    size_t routing_count = application->routing_provider_count;
    application->routing_snapshot = publication->candidate;
    application->routing_providers = publication->next;
    application->routing_provider_count = publication->next_count;
    bool okay = true;
    for (size_t ordinal = 0; ordinal < publication->next_count; ++ordinal) {
        size_t index = ordinal == 0 ? primary : ordinal <= primary ? ordinal - 1 : ordinal;
        application_provider *provider = publication->next[index];
        application_provider_admission *admission =
            &publication->admissions[publication->admission_count++];
        admission->provider = provider;
        if (!provider->constructed) {
            qa_catalog *provider_catalog = qa_launch_instance_catalog(provider->launch);
            if (!provider_catalog) provider_catalog = catalog;
            const qa_product *product = qa_catalog_product(
                provider_catalog, provider->launch->selection.product);
            if (image != NULL) {
                application_saved_instance_content saved = {0};
                if (!application_save_content_instance(application->content_graph,
                    provider->launch->selection.instance, &saved, error)) {
                    okay = false;
                    break;
                }
                provider_catalog = saved.product_catalog;
                product = saved.product;
            }
            bool constructed = false;
            if (product != NULL && image != NULL && provider->kind == APPLICATION_PROVIDER_QVM) {
                const qa_save_record *record = qa_save_image_find(image, QA_SAVE_PROVIDER,
                    provider->launch->selection.instance);
                constructed = application_provider_construct_qvm_restored(application,
                    provider, world, provider_catalog, product, choices, record, error);
            } else if (product != NULL) {
                constructed = application_provider_construct(application, provider, world,
                    provider_catalog, product, choices, error);
            }
            if (!constructed) {
                okay = false;
                break;
            }
            admission->constructed = true;
        }
        if (image != NULL && provider->kind == APPLICATION_PROVIDER_Q2) {
            const qa_save_record *record = qa_save_image_find(image, QA_SAVE_PROVIDER,
                provider->launch->selection.instance);
            if (!application_native_q2_checkpoint_prepare(provider, record, error)) {
                okay = false;
                break;
            }
        }
        if (image != NULL && provider->kind == APPLICATION_PROVIDER_Q3) {
            const qa_save_record *record = qa_save_image_find(image, QA_SAVE_PROVIDER,
                provider->launch->selection.instance);
            if (!application_native_q3_checkpoint_prepare(provider, record, error)) {
                okay = false;
                break;
            }
        }
        if (!provider->component_attached && provider->component.owner != 0) {
            qa_actor_owner retiring =
                retiring_component(publication, provider, used);
            if (!qa_session_prepare_component(application->session,
                                              &provider->component, retiring,
                                              &admission->component, error)) {
                okay = false;
                break;
            }
        }
        if (!provider->policy_attached && provider->policy.describe != NULL) {
            bool replacement = false;
            for (size_t old = 0; old < publication->removed_count; ++old)
                if (publication->removed[old]->policy_attached &&
                    publication->removed[old]->owner == provider->owner)
                    replacement = true;
            if (!qa_combat_prepare_policy(application->combat,
                                          &provider->policy, replacement,
                                          &admission->policy, error)) {
                okay = false;
                break;
            }
        }
    }
    application->routing_snapshot = routing_snapshot;
    application->routing_providers = routing_providers;
    application->routing_provider_count = routing_count;
    free(used);
    return okay;
}

static bool prepare_world(qa_application *application,
                          application_publication *publication,
                          qa_error *error)
{
    const qa_launch_resource *selected = map_resource(publication->candidate);
    if (selected == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "candidate map resource is absent");
    publication->map_resource = (qa_resource *)selected->resource;
    qa_resource_retain(publication->map_resource);
    if (!qa_bsp_open(qa_resource_bytes(publication->map_resource),
                     &publication->map, error) ||
        !qa_collision_create(&publication->map, &publication->geometry,
                             error))
        return false;

    if (application->world == NULL) {
        if (!qa_world_create(qa_session_actor_registry(application->session),
                             publication->geometry, NULL,
                             &publication->initial_world, error))
            return false;
        qa_physics_services services = application_physics_services(application);
        if (!qa_physics_init(application->physics,
                             publication->initial_world, (qa_actor_id){0},
                             &services, error))
            return false;
        publication->physics_initialized = true;
    } else if (!qa_world_prepare_geometry(application->world,
                                          publication->geometry,
                                          &publication->geometry_admission,
                                          error))
        return false;
    return publication->restoring
        ? application_map_prepare_content(application, publication, error)
        : application_map_prepare(application, publication, error);
}

bool application_publication_begin(qa_application *application,
                                     const qa_launch_snapshot *previous,
                                     const qa_launch_snapshot *candidate,
                                     application_publication **out, qa_error *error)
{
    if (application == NULL || candidate == NULL || out == NULL ||
        application->publication_started ||
        !application_guests_idle(application) ||
        !application_bots_can_destroy(application) ||
        !qa_session_safe(application->session) ||
        (application->world != NULL && !qa_world_idle(application->world)) ||
        !qa_combat_idle(application->combat))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "publication preparation requires idle authorities");
    *out = NULL;
    if (application->publication_generation == UINT64_MAX ||
        application->command_generation == UINT64_MAX ||
        application->map_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application publication generation exhausted");
    application_publication *publication =
        calloc(1, sizeof(*publication));
    if (publication == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot allocate publication ticket");
    publication->previous = previous;
    publication->candidate = candidate;
    *out = publication;
    if (!provider_roster(publication, error) ||
        !removed_roster(application, publication, error))
        return false;

    publication->travel =
        application->map_force_reload ||
        !same_map_configuration(previous, candidate) ||
        publication->removed_count != 0 ||
        publication->next_count != application->provider_count;
    if (!publication->travel)
        return true;
    if (application->world_change_ready != NULL &&
        !application->world_change_ready(application->guest_context, application, error))
        return false;
    return prepare_world(application, publication, error);
}

bool application_publication_finish(qa_application *application,
                                    application_publication *publication, qa_error *error)
{
    if (!publication || !publication->candidate || publication->admissions)
        return application_fail(error, QA_ERROR_ARGUMENT, "Publication completion lost its detached ticket");
    if (!publication->travel) return true;
    if (!construct_and_reserve(application, publication, NULL, error) ||
        !application_match_prepare(application, publication, error) ||
        !application_q3_world_restart_prepared(application, publication, error))
        return false;
    return true;
}

bool application_publication_prepare(qa_application *application,
                                     const qa_launch_snapshot *previous,
                                     const qa_launch_snapshot *candidate,
                                     void **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Publication requires ticket output");
    application_publication *publication = application_startup_flow_take_publication(application,
        previous, candidate);
    bool ok = publication || application_publication_begin(application, previous, candidate, &publication, error);
    *out = publication;
    return ok && application_publication_finish(application, publication, error);
}

static void abort_admissions(application_publication *publication)
{
    if (publication->admissions == NULL)
        return;
    for (size_t index = publication->admission_count; index-- > 0;) {
        application_provider_admission *admission =
            &publication->admissions[index];
        qa_component_admission_abort(admission->component);
        admission->component = NULL;
        qa_combat_policy_admission_abort(admission->policy);
        admission->policy = NULL;
    }
}

void application_publication_dispose(qa_application *application,
                                     application_publication *publication)
{
    if (publication == NULL)
        return;
    if (!publication->published)
        application_startup_flow_discard_candidate(application, publication->candidate);
    abort_admissions(publication);
    qa_world_geometry_admission_abort(publication->geometry_admission);
    publication->geometry_admission = NULL;
    if (!publication->published && publication->admissions != NULL)
        for (size_t index = publication->admission_count; index-- > 0;) {
            application_provider_admission *admission =
                &publication->admissions[index];
            if (admission->constructed) {
                qa_error ignored = {0};
                if (!application_provider_deconstruct(admission->provider,
                                                      &ignored))
                    application_fault(application, &ignored);
            }
        }
    /* A startup continuation can own physical hosts before they reach GAME
     * admission. Release all detached candidate owners before their world. */
    if (!publication->published)
        for (size_t index = publication->next_count; index-- > 0;) {
            application_provider *provider = publication->next[index];
            if (!provider->attached) {
                qa_error ignored = {0};
                if (!application_provider_deconstruct(provider, &ignored))
                    application_fault(application, &ignored);
            }
        }
    qa_equipment_destroy(publication->equipment);
    qa_modes_destroy(publication->modes);
    free(publication->mode_ids);
    qa_entities_free(&publication->entities);
    if (publication->initial_world != NULL)
        (void)qa_world_destroy(publication->initial_world, NULL);
    qa_collision_destroy(publication->geometry);
    qa_resource_release(publication->map_resource);
    if (!publication->published && publication->physics_initialized) {
        *application->physics = (qa_physics){0};
        application->physics_ready = false;
    }
    free(publication->admissions);
    free(publication->removed);
    free(publication->next);
    application_map_publication_dispose(publication);
    free(publication);
}

void application_publication_rollback(qa_application *application, void *ticket)
{
    application_publication_dispose(application, ticket);
}

static void remember_failure(bool result, const qa_error *current,
                             const char *fallback, bool *ok, qa_error *first)
{
    if (result || !*ok)
        return;
    *ok = false;
    if (current != NULL && current->code != QA_OK)
        *first = *current;
    else
        qa_error_set(first, QA_ERROR_ARGUMENT, 0, "%s", fallback);
}

static bool detach_removed(qa_application *application,
                           application_publication *publication,
                           qa_error *error)
{
    bool ok = true;
    qa_error first = {0};
    for (size_t index = 0; index < publication->removed_count; ++index) {
        application_provider *provider = publication->removed[index];
        qa_error current = {0};
        if (provider->policy_attached) {
            bool removed = qa_combat_unregister_policy(application->combat,
                                                       provider->owner,
                                                       &current);
            remember_failure(removed, &current,
                             "combat policy retirement failed", &ok, &first);
            if (removed)
                provider->policy_attached = false;
        }
        current = (qa_error){0};
        if (provider->component_attached) {
            bool removed = qa_session_remove(application->session,
                                             provider->owner, &current);
            remember_failure(removed, &current,
                             "session component retirement failed", &ok,
                             &first);
            qa_clock_state retained_clock;
            if (removed || !qa_session_clock(application->session,
                                             provider->owner,
                                             &retained_clock))
                provider->component_attached = false;
        }
        provider->attached = provider->component_attached ||
                             provider->policy_attached;
    }
    if (!ok && error != NULL)
        *error = first;
    return ok;
}

static bool deconstruct_removed(application_publication *publication, qa_error *error)
{
    /* Original client roles own real leases on their primary native source. */
    for (unsigned phase = 0; phase < 2; ++phase)
        for (size_t i = 0; i < publication->removed_count; ++i) {
            application_provider *provider = publication->removed[i];
            bool guest = provider->kind == APPLICATION_PROVIDER_QVM ||
                (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine);
            if (guest != (phase == 0)) continue;
            if (!application_provider_deconstruct(provider, error)) return false;
        }
    return true;
}

static bool commit_admissions(application_publication *publication,
                              qa_error *error)
{
    bool ok = true;
    qa_error first = {0};
    for (size_t index = 0; index < publication->admission_count; ++index) {
        application_provider_admission *admission =
            &publication->admissions[index];
        if (admission->component != NULL) {
            qa_error current = {0};
            bool committed = qa_component_admission_commit(
                admission->component, &current);
            remember_failure(committed, &current,
                             "session component publication failed", &ok,
                             &first);
            if (committed) {
                admission->component = NULL;
                admission->provider->component_attached = true;
            }
        }
        if (admission->policy != NULL) {
            qa_error current = {0};
            bool committed = qa_combat_policy_admission_commit(
                admission->policy, &current);
            remember_failure(committed, &current,
                             "combat policy publication failed", &ok,
                             &first);
            if (committed) {
                admission->policy = NULL;
                admission->provider->policy_attached = true;
            }
        }
    }
    if (!ok && error != NULL)
        *error = first;
    return ok;
}

static void publish_roster(qa_application *application,
                           application_publication *publication)
{
    for (size_t index = 0; index < publication->next_count; ++index) {
        application_provider *provider = publication->next[index];
        provider->attached = provider->constructed &&
            (provider->component.owner == 0 || provider->component_attached) &&
            (provider->policy.describe == NULL || provider->policy_attached);
    }
    free(application->providers);
    application->providers = publication->next;
    application->provider_count = publication->next_count;
    publication->next = NULL;
    publication->next_count = 0;
    application->routing_snapshot = publication->candidate;
    application->routing_providers = application->providers;
    application->routing_provider_count = application->provider_count;
}

bool application_save_prepare_content(qa_application *candidate,
                                        const qa_launch_snapshot *snapshot,
                                        const qa_save_image *image,
                                        qa_error *error)
{
    if (candidate == NULL || snapshot == NULL || image == NULL ||
        candidate->operation != APPLICATION_PERSISTING ||
        candidate->world != NULL || candidate->provider_count != 0 ||
        candidate->providers != NULL || candidate->modes != NULL ||
        candidate->equipment != NULL || !qa_session_safe(candidate->session))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "save content requires an isolated restored application");
    application_publication *publication = calloc(1, sizeof(*publication));
    if (publication == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot allocate restored content preparation");
    publication->candidate = snapshot;
    publication->restoring = true;
    bool ok = provider_roster(publication, error) &&
              prepare_world(candidate, publication, error);
    if (ok)
        ok = qa_session_adopt_restored_world(candidate->session,
                                             publication->initial_world,
                                             close_world, error);
    if (ok) {
        candidate->world = publication->initial_world;
        publication->initial_world = NULL;
        candidate->physics_ready = true;
        publication->physics_initialized = false;
        candidate->geometry = publication->geometry;
        publication->geometry = NULL;
        candidate->map_resource = publication->map_resource;
        publication->map_resource = NULL;
        candidate->routing_snapshot = snapshot;
        candidate->routing_providers = publication->next;
        candidate->routing_provider_count = publication->next_count;
        ok = application_map_restore_identity(candidate, snapshot, error) &&
             application_match_prepare_modes(candidate, publication, error);
        if (ok) {
            /* Bot and guest services borrow the actual candidate modes. Install
             * this owner before creating either runtime; teardown owns it even
             * if later source construction fails. */
            candidate->modes = publication->modes;
            publication->modes = NULL;
            candidate->mode_ids = publication->mode_ids;
            publication->mode_ids = NULL;
            const qa_save_record *bots =
                qa_save_image_find(image, QA_SAVE_BOTS, "");
            const qa_save_record *navigation =
                qa_save_image_find(image, QA_SAVE_NAVIGATION, "");
            if (bots == NULL || navigation == NULL)
                ok = application_fail(error, QA_ERROR_FORMAT,
                                      "saved application has no bot or navigation owner");
            else
                ok = application_bots_save_prepare(candidate, bots->payload,
                                                    navigation->payload, error);
        }
        if (ok)
            ok = construct_and_reserve(candidate, publication, image, error) &&
                 application_match_prepare_equipment(candidate, publication, error);
    }
    if (ok) {
        ok = commit_admissions(publication, error);
        publish_roster(candidate, publication);
        publication->published = true;
        candidate->equipment = publication->equipment;
        publication->equipment = NULL;
        if (ok)
            ok = application_map_restore_bind(candidate, snapshot, error);
    }
    candidate->routing_snapshot = NULL;
    candidate->routing_providers = NULL;
    candidate->routing_provider_count = 0;
    application_publication_dispose(candidate, publication);
    return ok;
}

static bool terminal_native_q2(const application_provider *provider)
{
    return provider->constructed &&
           provider->kind == APPLICATION_PROVIDER_NATIVE &&
           provider->state.native.q2_engine != NULL &&
           provider->state.native.host != NULL &&
           qa_native_terminal(qa_native_host_instance(provider->state.native.host));
}

static bool retire_map_services(qa_application *application, bool carry,
                                bool terminal_world, qa_error *error)
{
    if (!application_guests_idle(application) ||
        !application_bots_can_destroy(application))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map retirement has borrowed guest or bot state");
    if (!application_rankings_close(application, error) ||
        !application_bots_shutdown(application,
            application_q3_world_restart_guest_shutdown(application,
                application_world_provider(application, QA_ROLE_ENTITIES, "")), error))
        return false;
    if (application->before_world_change != NULL &&
        !application->before_world_change(application->guest_context, application, error))
        return false;
    for (size_t index = 0; index < application->provider_count; ++index) {
        application_provider *provider = application->providers[index];
        if (!provider->constructed ||
            (terminal_world && terminal_native_q2(provider)))
            continue;
        if (carry && provider->kind == APPLICATION_PROVIDER_QC &&
            provider->map_bound && !application_qc_change_parms(provider, error))
            return false;
        bool retired = provider->kind == APPLICATION_PROVIDER_NATIVE &&
                       provider->state.native.q2_engine != NULL
            ? application_native_q2_retire_map(provider, error)
            : application_q3_guest_retire_map(provider, error);
        if (!retired)
            return false;
    }
    if (terminal_world) {
        /* Healthy source callbacks run with live actors. Terminal Q2 primary
         * claims can disappear only through actual canonical retirement. */
        if (application->world != NULL &&
            !qa_session_retire_world(application->session, error))
            return false;
        for (size_t index = 0; index < application->provider_count; ++index) {
            application_provider *provider = application->providers[index];
            if (terminal_native_q2(provider) &&
                !application_native_q2_retire_map(provider, error))
                return false;
        }
    }
    if (!application_portals_close(application, 0, error))
        return false;
    return application->world_retired == NULL ||
        application->world_retired(application->guest_context, application, error);
}

static bool publish_travel(qa_application *application,
                           application_publication *publication,
                           qa_error *error)
{
    bool ok = true;
    bool native_map_cut = false;
    bool geometry_published = application->world == NULL;
    qa_error first = {0};
    qa_error current = {0};
    const qa_launch_snapshot *routing_snapshot = application->routing_snapshot;
    application_provider **routing_providers = application->routing_providers;
    size_t routing_count = application->routing_provider_count;
    application->routing_snapshot = publication->previous;
    application->routing_providers = application->providers;
    application->routing_provider_count = application->provider_count;
    application_provider *old_source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    const qa_launch_choices *choices = qa_launch_snapshot_choices(publication->candidate);
    bool retired_services = application_q3_world_restart_begin(application, publication, error);
    for (size_t i = 0; retired_services && i < application->provider_count; ++i)
        retired_services = application_q3_guest_client_sources_retire(application->providers[i],
            old_source, publication->map_provider, choices, error);
    if (retired_services)
        retired_services = retire_map_services(application, true, false, error);
    if (retired_services)
        retired_services = application_native_q3_clients_drain(application, error) &&
            application_q3_world_restart_shutdown(application, publication, error);
    if (retired_services)
        retired_services = application_q3_map_shutdown(application, publication,
            &native_map_cut, error);
    application->routing_snapshot = routing_snapshot;
    application->routing_providers = routing_providers;
    application->routing_provider_count = routing_count;
    if (!retired_services)
        return false;
    application_q1_signon_reset(application);
    application->map_view_ready = false;
    publication->published = true;
    if (application->world == NULL) {
        if (!qa_session_replace_world(application->session,
                                      publication->initial_world, close_world,
                                      error))
            return false;
        application->world = publication->initial_world;
        publication->initial_world = NULL;
        application->physics_ready = true;
    } else {
        bool retired = qa_session_retire_world(application->session,
                                               &current);
        remember_failure(retired, &current, "world retirement failed", &ok,
                         &first);
    }

    current = (qa_error){0};
    bool detached = detach_removed(application, publication, &current);
    remember_failure(detached, &current, "provider retirement failed", &ok,
                     &first);
    if (detached) {
        current = (qa_error){0};
        bool consumed = true;
        application->routing_snapshot = publication->previous;
        application->routing_providers = application->providers;
        application->routing_provider_count = application->provider_count;
        for (size_t i = 0; consumed && i < application->provider_count; ++i) {
            application_provider *provider = application->providers[i];
            bool removed = false;
            for (size_t j = 0; j < publication->removed_count; ++j)
                removed |= publication->removed[j] == provider;
            if (!removed) consumed = application_q3_guest_retire_executors(provider, &current);
        }
        if (consumed) consumed = deconstruct_removed(publication, &current);
        bool retain_bots = false;
        if (consumed) consumed = application_q3_world_restart_retired(application, publication, &retain_bots, &current);
        if (consumed && !retain_bots) consumed = application_bots_destroy(application, &current);
        application->routing_snapshot = routing_snapshot;
        application->routing_providers = routing_providers;
        application->routing_provider_count = routing_count;
        remember_failure(consumed, &current, "source executor and bot service retirement failed", &ok, &first);
    }
    if (publication->geometry_admission != NULL) {
        current = (qa_error){0};
        bool committed = qa_world_geometry_admission_commit(
            publication->geometry_admission, &current);
        remember_failure(committed, &current,
                         "world geometry publication failed", &ok, &first);
        if (committed) {
            publication->geometry_admission = NULL;
            geometry_published = true;
        }
    }
    current = (qa_error){0};
    bool admitted = commit_admissions(publication, &current);
    remember_failure(admitted, &current,
                     "provider admission publication failed", &ok, &first);

    qa_equipment *old_equipment = application->equipment;
    qa_modes *old_modes = application->modes;
    qa_mode_id *old_mode_ids = application->mode_ids;

    publish_roster(application, publication);
    application->equipment = publication->equipment;
    publication->equipment = NULL;
    application->modes = publication->modes;
    publication->modes = NULL;
    application->mode_ids = publication->mode_ids;
    publication->mode_ids = NULL;
    application->mode_count = publication->mode_count;
    application->primary_mode = publication->primary_mode;
    application->primary_mode_ready = publication->mode_count != 0;

    qa_equipment_destroy(old_equipment);
    qa_modes_destroy(old_modes);
    free(old_mode_ids);

    if (geometry_published) {
        qa_collision_geometry *old_geometry = application->geometry;
        qa_resource *old_resource = application->map_resource;
        application->geometry = publication->geometry;
        publication->geometry = NULL;
        application->map_resource = publication->map_resource;
        publication->map_resource = NULL;
        qa_collision_destroy(old_geometry);
        qa_resource_release(old_resource);
    }

    if (ok && geometry_published) {
        current = (qa_error){0};
        bool mapped = true;
        for (size_t i = 0; mapped && i < application->provider_count; ++i)
            mapped = application_q3_guest_client_sources_rebuild(application->providers[i],
                publication->map_provider, choices, &current);
        if (mapped) mapped = application_q3_world_restart_admitted(application, publication, &current) &&
            application_q3_campaign_launch_admitted(application, publication, &current) &&
            application_map_publish(application, publication, &current);
        remember_failure(mapped, &current, "map publication failed", &ok,
                         &first);
        if (mapped) {
            current = (qa_error){0};
            remember_failure(application_bots_publish(application,
                qa_launch_snapshot_choices(publication->candidate),
                &publication->map, &publication->entities, &current),
                &current, "bot population publication failed", &ok, &first);
        }
        if (ok) {
            current = (qa_error){0};
            remember_failure(application_q3_world_restart_published(application, publication, &current),
                &current, "Q3 replacement final publication failed", &ok, &first);
            if (ok)
                remember_failure(application_q3_map_published(application, publication,
                    native_map_cut, &current), &current,
                    "Q3 map wire publication failed", &ok, &first);
        }
    }
    ++application->publication_generation;
    if (!ok && error != NULL)
        *error = first;
    return ok;
}

bool application_publication_retire(qa_application *application,
                                    qa_error *error)
{
    application_provider *old_source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    for (size_t i = 0; i < application->provider_count; ++i)
        if (!application_q3_guest_client_sources_retire(application->providers[i],
                old_source, NULL, NULL, error)) return false;
    bool terminal_world = false;
    for (size_t index = 0; index < application->provider_count; ++index)
        if (terminal_native_q2(application->providers[index]))
            terminal_world = true;
    if (!retire_map_services(application, false, terminal_world, error))
        return false;
    application->map_view_ready = false;
    if (!terminal_world && application->world != NULL &&
        !qa_session_retire_world(application->session, error))
        return false;
    application_publication publication = {
        .removed = application->providers,
        .removed_count = application->provider_count,
    };
    if (!detach_removed(application, &publication, error))
        return false;
    if (!deconstruct_removed(&publication, error)) return false;
    if (!application_drain_provider_closes(application, error)) return false;
    if (!application_bots_destroy(application, error)) return false;
    free(application->providers);
    application->providers = NULL;
    application->provider_count = 0;
    application->routing_snapshot = NULL;
    application->routing_providers = NULL;
    application->routing_provider_count = 0;
    qa_equipment_destroy(application->equipment);
    application->equipment = NULL;
    qa_modes_destroy(application->modes);
    application->modes = NULL;
    free(application->mode_ids);
    application->mode_ids = NULL;
    application->mode_count = 0;
    application->primary_mode_ready = false;
    return true;
}

void application_publication_publish(qa_application *application,
                                     const qa_launch_snapshot *previous,
                                     const qa_launch_snapshot *candidate,
                                     void *ticket)
{
    (void)previous;
    application->publication_started = true;
    if (application->command_generation != UINT64_MAX)
        ++application->command_generation;
    if (candidate == NULL) {
        application->publication_started = false;
        return;
    }

    application_publication *publication = ticket;
    qa_error error = {0};
    bool ok = publication != NULL && publication->candidate == candidate;
    if (!ok)
        qa_error_set(&error, QA_ERROR_ARGUMENT, 0,
                     "configuration committed without its publication ticket");
    else if (!publication->travel) {
        publish_roster(application, publication);
        ++application->publication_generation;
    } else {
        ok = publish_travel(application, publication, &error);
    }
    if (publication != NULL) {
        publication->published = publication->published || ok;
        application_publication_dispose(application, publication);
    }
    application->publication_started = false;
    application->routing_snapshot = NULL;
    application->routing_providers = NULL;
    application->routing_provider_count = 0;
    if (!ok)
        application_fault(application, &error);
}
