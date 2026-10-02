#include "music_sources_private.h"
#include "qa/bsp.h"
#include "qa/text.h"
#include "qa/application_startup_prepare.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>

static bool fail(qa_error *e, const char *text) { return frontend_fail(e, QA_ERROR_ARGUMENT, text); }
static bool recipe_origin(const frontend_music_origin *origin) {
    if ((origin->kind != FRONTEND_MUSIC_COMPONENT && origin->kind != FRONTEND_MUSIC_REMOTE) || origin->descriptor || !origin->recipe ||
        qa_executable_recipe_catalog(origin->recipe) != origin->catalog ||
        !qa_executable_recipe_current(origin->recipe, origin->catalog)) return false;
    if (origin->kind == FRONTEND_MUSIC_REMOTE && !origin->recipe_provider && origin->recipe_content) {
        qa_vfs *files=NULL;const qa_product *product=NULL;
        return qa_executable_recipe_content_read(origin->recipe,origin->recipe_content,&files,&product) &&
            files==origin->files && product && product->id==origin->product;
    }
    if (!origin->recipe_provider || origin->recipe_content) return false;
    for (size_t i = 0; i < qa_executable_recipe_provider_count(origin->recipe); ++i) {
        const qa_recipe_provider *actual = qa_executable_recipe_provider(origin->recipe, i);
        if (actual == origin->recipe_provider) return actual->selection.product == origin->product;
    }
    return false;
}
static bool origin_current(const frontend_music_sources *owner) {
    const frontend_music_origin *origin = &owner->origin;
    const qa_launch_instance *held = qa_launch_instance_lease_view(owner->origin_metadata);
    return owner->has_origin && owner->origin_bound && origin->current &&
        origin->current(origin->context, origin) && frontend_music_sources_current(owner) &&
        (owner->origin_recipe ? !held && recipe_origin(origin) : held && origin->descriptor &&
            !origin->recipe && !origin->recipe_provider && !origin->recipe_content && held->storage == origin->descriptor->storage);
}
static void origin_dispose(frontend_music_sources *owner) {
    qa_launch_instance_lease_release(owner->origin_metadata); owner->origin_metadata = NULL;
    free(owner->origin_instance); free(owner->origin_product); owner->origin_instance = owner->origin_product = NULL;
    owner->origin = (frontend_music_origin){0}; owner->has_origin = owner->origin_bound = owner->origin_recipe = false;
}
static bool origin_admit(frontend_music_sources *owner, const frontend_music_origin *origin, qa_error *e) {
    const qa_product *row = origin && origin->catalog ? qa_catalog_product(origin->catalog, origin->product) : NULL;
    if (!frontend_music_sources_current(owner) || !origin || (unsigned)origin->kind > FRONTEND_MUSIC_COMPONENT ||
        !origin->bus || !origin->receiver ||
        !row || row->availability != QA_CONTENT_INSTALLED || !origin->files || !origin->music ||
        !origin->context || !origin->current || !origin->stop || origin->physical_seat >= owner->frontend->options.seats ||
        !origin->current(origin->context, origin) ||
        (origin->recipe ? !recipe_origin(origin) : !origin->descriptor || !origin->descriptor->storage ||
            origin->recipe_provider || origin->recipe_content ||
            (origin->kind == FRONTEND_MUSIC_REMOTE ? !qa_catalog_product_view_current(origin->catalog,origin->product,origin->files) :
                qa_launch_instance_catalog(origin->descriptor) != origin->catalog || origin->descriptor->selection.product != origin->product)) ||
        (!owner->restoring && !qa_audio_music_idle(origin->music)))
        return fail(e, "Explicit music requires its actual source declaration, player and retained caller");
    return true;
}
void frontend_music_command_free(frontend_music_command *command) {
    if (!command) return;
    for (size_t i = 0; i < command->argc; ++i) free(command->argv[i]);
    free(command->argv); free(command->script); free(command);
}
bool frontend_music_sources_current(const frontend_music_sources *owner) {
    return owner && owner->frontend && owner->slot && *owner->slot == owner &&
        owner->frontend->application == owner->application && owner->frontend->audio == owner->engine;
}
static const qa_launch_instance *world_instance(const frontend_music_sources *owner) {
    const qa_launch_snapshot *snapshot = qa_application_launch(owner->application);
    const qa_launch_snapshot *previous = qa_application_startup_publication_previous(owner->application, snapshot);
    if (previous && qa_application_startup_publication_consuming(owner->application, snapshot)) snapshot = previous;
    const qa_launch_binding *binding = qa_launch_binding_for(qa_launch_snapshot_choices(snapshot),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    return binding ? qa_launch_snapshot_find(snapshot, binding->instance) : NULL;
}
bool frontend_music_world_current(const frontend_music_sources *owner) {
    const qa_launch_instance *instance = world_instance(owner);
    const qa_launch_instance *saved = qa_launch_instance_lease_view(owner->world.metadata);
    qa_application_map_view map; qa_actor_owner provider;
    return frontend_music_sources_current(owner) && instance && saved &&
        instance->storage == saved->storage && !strcmp(instance->selection.instance, owner->world.instance) &&
        qa_application_provider_owner(owner->application, owner->world.instance, &provider) && provider == owner->world.provider &&
        qa_application_map_read(owner->application, &map) && map.resource == owner->world.map && map.revision == owner->world.map_revision;
}
void frontend_music_world_dispose(frontend_music_world *world) {
    qa_launch_instance_lease_release(world->metadata); qa_resource_release(world->map);
    free(world->instance); *world = (frontend_music_world){0};
}
bool frontend_music_world_capture(frontend_music_sources *owner, qa_error *e) {
    const qa_launch_instance *instance = world_instance(owner); qa_application_map_view map;
    if (!instance || !qa_application_map_read(owner->application, &map) || owner->world.metadata || owner->world.map)
        return fail(e, "WORLD music requires its actual published entity source and retained map");
    size_t n = strlen(instance->selection.instance) + 1;
    owner->world.instance = malloc(n);
    if (!owner->world.instance) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining WORLD music source identity");
    memcpy(owner->world.instance, instance->selection.instance, n);
    if (!qa_application_provider_owner(owner->application, owner->world.instance, &owner->world.provider) ||
        !qa_launch_instance_retain_metadata(instance, &owner->world.metadata, e)) return false;
    owner->world.map = map.resource; qa_resource_retain(map.resource); owner->world.map_revision = map.revision;
    return frontend_music_world_current(owner) || fail(e, "WORLD music source changed during actual receipt acquisition");
}
static qa_audio_family family(const qa_product *product) {
    return product->family == QA_GAME_Q1 ? QA_AUDIO_Q1 : product->family == QA_GAME_Q2 ? QA_AUDIO_Q2 : QA_AUDIO_Q3;
}
static bool attach(frontend_music_sources *owner, frontend_music_slot slot, const qa_product *source,
    const frontend_music_content *content, size_t count, const char *cue, qa_error *e) {
    qa_audio_music *music = NULL;
    if (!frontend_source_identity_allocate(owner->frontend, owner->buses + slot, e) ||
        !qa_audio_music_create(qa_audio_engine_rate(owner->engine), family(source), slot == FRONTEND_MUSIC_WORLD, &music, e)) return false;
    if (!qa_audio_music_controls_bind(music, owner->controls, e)) { qa_audio_music_destroy(music); return false; }
    if (!qa_audio_engine_music_source(owner->engine, owner->buses[slot], QA_AUDIO_WORLD, 1, music,
        slot == FRONTEND_MUSIC_MENU ? QA_AUDIO_MUSIC_MENU : QA_AUDIO_MUSIC_WORLD, owner->output == slot, e)) {
        qa_audio_music_destroy(music); return false;
    }
    frontend_music_policy_options options = {.music = music, .bus = owner->buses[slot], .audience = QA_AUDIO_WORLD,
        .bus_gain = 1, .menu = slot == FRONTEND_MUSIC_MENU, .sources = content, .source_count = count,
        .authored_cue = cue, .random_seed = owner->seed};
    if (!frontend_music_policy_create(owner->frontend, &options, owner->policies + slot, e)) {
        qa_audio_engine_remove_music(owner->engine, owner->buses[slot]); owner->buses[slot] = 0; return false;
    }
    return true;
}
static bool menu_create(frontend_music_sources *owner, qa_error *e) {
    qa_catalog *catalog = owner->menu_catalog;
    const qa_product *selected = owner->frontend->options.game ?
        frontend_product_selection(catalog, owner->frontend->options.game) : NULL;
    if (owner->frontend->options.game && (!selected || selected->availability != QA_CONTENT_INSTALLED))
        return fail(e, "Menu soundtrack lacks its actual selected installed product");
    const qa_product *theme = NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *row = qa_catalog_at(catalog, i);
        if (row->availability != QA_CONTENT_INSTALLED) continue;
        if (!selected) selected = row;
        if (!theme && row->family == QA_GAME_Q2 && row->edition == QA_EDITION_RERELEASE && !strcmp(row->campaign, "baseq2")) theme = row;
    }
    if (!selected) return true;
    owner->menu_product = selected->id;
    qa_vfs *views[2] = {0}; frontend_music_content contents[2] = {0}; size_t count = theme ? 2 : 1;
    const qa_product *rows[2] = {theme ? theme : selected, selected}; bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = qa_catalog_open(catalog, rows[i]->id, views + i, e);
        contents[i] = (frontend_music_content){.catalog = catalog, .product = rows[i]->id, .files = views[i]};
    }
    if (ok) ok = attach(owner, FRONTEND_MUSIC_MENU, selected, contents, count, "", e);
    for (size_t i = 0; i < count; ++i) qa_vfs_destroy(views[i]);
    return ok;
}
bool frontend_music_sources_create(qa_frontend *f, frontend_music_sources **out, qa_error *e) {
    if (!f || !f->application || !out || *out || f->capture || f->source_restoring)
        return fail(e, "Music constructor requires its actual fresh frontend owner");
    frontend_music_sources *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining native application music sources");
    owner->frontend = f; owner->application = f->application; owner->engine = f->audio; owner->slot = out;
    *out = owner;
    owner->menu_catalog = qa_application_catalog(owner->application);
    if (!owner->menu_catalog) return fail(e, "Music constructor lacks its actual catalog receipt");
    qa_catalog_retain(owner->menu_catalog);
    if (!qa_audio_music_controls_create(&owner->controls, e)) return false;
    /* Fresh native seed for this independent owner, sampled once. Playback
     * and source clocks never seed or advance this retained shuffle stream. */
    owner->seed = SDL_GetPerformanceCounter();
    return !owner->engine || menu_create(owner, e);
}
static const qa_product *fallback(const qa_catalog *catalog, const qa_product *source) {
    static const char *const pairs[3][2] = {{"q1-classic-id1", "q1-rerelease-id1"},
        {"q1-classic-hipnotic", "q1-rerelease-hipnotic"}, {"q1-classic-rogue", "q1-rerelease-rogue"}};
    for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 2; ++j) if (!strcmp(source->key, pairs[i][j])) {
        const qa_product *other = qa_catalog_find(catalog, pairs[i][1 - j]);
        return other && other->availability == QA_CONTENT_INSTALLED ? other : NULL;
    }
    return NULL;
}
qa_product_id frontend_music_world_fallback(const frontend_music_sources *owner) {
    const qa_launch_instance *world = owner ? qa_launch_instance_lease_view(owner->world.metadata) : NULL;
    qa_catalog *catalog = world ? qa_launch_instance_catalog(world) : NULL;
    const qa_product *source = catalog ? qa_catalog_product(catalog, world->selection.product) : NULL;
    const qa_product *other = source ? fallback(catalog, source) : NULL;
    return other ? other->id : 0;
}
static bool world_cue(const frontend_music_sources *owner, const qa_product *source, char **out, qa_error *e) {
    qa_bsp_view map; qa_entities entities = {0};
    bool ok = qa_bsp_open(qa_resource_bytes(owner->world.map), &map, e) &&
        qa_entities_parse(map.lumps[QA_BSP_ENTITIES].bytes, map.family == QA_BSP_Q3 ? QA_ENTITY_Q3 : QA_ENTITY_Q1, &entities, e);
    qa_bytes cue = {0};
    for (size_t i = 0; ok && i < entities.count; ++i) {
        qa_bytes classname;
        if (!qa_entity_value(&entities, i, "classname", &classname) || classname.size != 10 || memcmp(classname.data, "worldspawn", 10)) continue;
        if (source->family == QA_GAME_Q3 || (source->family == QA_GAME_Q2 && source->edition == QA_EDITION_RERELEASE))
            (void)qa_entity_value(&entities, i, "music", &cue);
        if (source->family != QA_GAME_Q3 && !cue.size) (void)qa_entity_value(&entities, i, "sounds", &cue);
        break;
    }
    if (ok) { *out = malloc(cue.size + 1);
        if (!*out) ok = frontend_fail(e, QA_ERROR_MEMORY, "Retaining actual WORLD soundtrack cue");
        else { if (cue.size) memcpy(*out, cue.data, cue.size); (*out)[cue.size] = 0; } }
    qa_entities_free(&entities); return ok;
}
bool frontend_music_sources_idle(const frontend_music_sources *owner) {
    return !owner || (frontend_music_sources_current(owner) && !owner->busy &&
        frontend_music_policy_idle(owner->policies[0]) && frontend_music_policy_idle(owner->policies[1]));
}
bool frontend_music_sources_queued(const frontend_music_sources *owner, size_t *count) {
    if (!frontend_music_sources_current(owner) || !count || owner->busy || owner->restoring) return false;
    size_t total = 0;
    for (const frontend_music_command *command = owner->commands; command; command = command->next) ++total;
    *count = total; return true;
}
bool frontend_music_sources_parent_is(const frontend_music_sources *owner, const qa_frontend *f, const qa_audio_engine *engine) {
    if (!frontend_music_sources_current(owner) || owner->frontend != f || owner->engine != engine || !owner->controls || owner->restoring || owner->busy) return false;
    if (!owner->menu_catalog || owner->menu_catalog != qa_application_catalog(owner->application) ||
        (!!owner->policies[FRONTEND_MUSIC_MENU] != (engine && owner->menu_product))) return false;
    if (owner->menu_product) {
        const qa_product *selected = qa_catalog_product(owner->menu_catalog, owner->menu_product);
        if (!engine || !selected || selected->availability != QA_CONTENT_INSTALLED) return false;
    } else if (engine) {
        for (size_t i = 0; i < qa_catalog_count(owner->menu_catalog); ++i)
            if (qa_catalog_at(owner->menu_catalog, i)->availability == QA_CONTENT_INSTALLED) return false;
    }
    for (size_t i = 0; i < 2; ++i) {
        if (!!owner->policies[i] != !!owner->buses[i]) return false;
        if (owner->policies[i] && (!frontend_music_policy_binding_is(owner->policies[i], f, engine, owner->buses[i], i == FRONTEND_MUSIC_MENU) ||
            !qa_audio_music_controls_is(frontend_music_policy_player(owner->policies[i]), owner->controls))) return false;
        qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, owner->buses[i]);
        if (attached && !qa_audio_engine_music_source_is(owner->engine, owner->buses[i], attached,
            i == FRONTEND_MUSIC_MENU ? QA_AUDIO_MUSIC_MENU : QA_AUDIO_MUSIC_WORLD, owner->output == (frontend_music_slot)i)) return false;
    }
    return owner->has_origin ? origin_current(owner) : !owner->policies[FRONTEND_MUSIC_WORLD] || frontend_music_world_current(owner);
}
qa_audio_music_controls *frontend_music_sources_controls(const frontend_music_sources *owner) {
    return frontend_music_sources_current(owner) && (!owner->restoring || owner->frontend->source_restoring) ? owner->controls : NULL;
}
bool frontend_music_sources_preferences_read(const frontend_music_sources *owner, bool *shuffle, const char **menu_track) {
    if (!owner || !shuffle || !menu_track || !frontend_music_sources_parent_is(owner, owner->frontend, owner->engine)) return false;
    const qa_cvars *cvars = qa_application_cvars(owner->application);
    const qa_cvar_view *a = qa_cvars_find(cvars, "music_shuffle"), *b = qa_cvars_find(cvars, "music_menu_track");
    if (!a || !a->value || !b || !b->value || (strcmp(a->value, "0") && strcmp(a->value, "1"))) return false;
    bool canonical_a = false, canonical_b = false;
    for (size_t i = 0; i < qa_cvars_count(cvars); ++i) {
        const qa_cvar_view *row = qa_cvars_at(cvars, i);
        canonical_a |= row == a; canonical_b |= row == b;
    }
    if (!canonical_a || !canonical_b) return false;
    *shuffle = !strcmp(a->value, "1"); *menu_track = b->value; return true;
}
static bool retire_world(frontend_music_sources *owner, bool map, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->frontend->capture)
        return fail(e, "WORLD soundtrack retirement requires its returned actual audio owner");
    if (owner->has_origin) {
        if (!origin_current(owner) || !owner->origin.stop(owner->origin.context, e) || !origin_current(owner))
            return fail(e, "Explicit music retirement lost its actual retained source caller");
    }
    if (owner->policies[FRONTEND_MUSIC_WORLD] &&
        !frontend_music_policy_random(owner->policies[FRONTEND_MUSIC_WORLD], &owner->seed))
        return fail(e, "WORLD music retirement lost its actual independent random continuation");
    if (!frontend_music_policy_destroy(owner->policies + FRONTEND_MUSIC_WORLD, e)) return false;
    if (owner->buses[FRONTEND_MUSIC_WORLD]) {
        qa_audio_engine_remove_music(owner->engine, owner->buses[FRONTEND_MUSIC_WORLD]);
        if (qa_audio_engine_bus_music(owner->engine, owner->buses[FRONTEND_MUSIC_WORLD]))
            return fail(e, "WORLD soundtrack bus retirement retains its actual engine parent");
    }
    owner->buses[FRONTEND_MUSIC_WORLD] = 0;
    if (map) frontend_music_world_dispose(&owner->world);
    origin_dispose(owner); return true;
}
bool frontend_music_sources_world_retire(frontend_music_sources *owner, qa_error *e) {
    return retire_world(owner, true, e);
}
bool frontend_music_sources_explicit_selected(const frontend_music_sources *owner, const frontend_music_origin *origin) {
    return owner && origin && owner->has_origin && origin_current(owner) &&
        owner->origin.context == origin->context && owner->origin.music == origin->music && owner->origin.bus == origin->bus &&
        (owner->origin_recipe ? owner->origin.recipe == origin->recipe &&
            owner->origin.recipe_provider == origin->recipe_provider &&
            ((!owner->origin.recipe_content && !origin->recipe_content) || (owner->origin.recipe_content && origin->recipe_content &&
                !strcmp(owner->origin.recipe_content,origin->recipe_content))) && !origin->descriptor :
            origin->descriptor && !origin->recipe && !origin->recipe_provider && !origin->recipe_content &&
            owner->origin.descriptor->storage == origin->descriptor->storage) && owner->origin.kind == origin->kind &&
        owner->origin.receiver == origin->receiver && owner->origin.physical_seat == origin->physical_seat &&
        owner->origin.catalog == origin->catalog && owner->origin.product == origin->product && owner->origin.files == origin->files;
}
bool frontend_music_sources_explicit_begin(frontend_music_sources *owner, const frontend_music_origin *origin, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || owner->frontend->capture ||
        !origin_admit(owner, origin, e)) return false;
    if (frontend_music_sources_explicit_selected(owner, origin)) return true;
    qa_application_map_view map;
    if (qa_application_map_read(owner->application, &map)) {
        if (owner->world.metadata ? !frontend_music_world_current(owner) : !frontend_music_world_capture(owner, e))
            return fail(e, "Explicit music requires its actual current published WORLD receipt");
    }
    qa_launch_instance_lease *metadata = NULL;
    if (origin->descriptor && !qa_launch_instance_retain_metadata(origin->descriptor, &metadata, e)) return false;
    if (!retire_world(owner, false, e)) { qa_launch_instance_lease_release(metadata); return false; }
    if (!qa_audio_music_controls_is(origin->music, owner->controls) && !qa_audio_music_controls_bind(origin->music, owner->controls, e)) {
        qa_launch_instance_lease_release(metadata); return false;
    }
    const qa_product *source=qa_catalog_product(origin->catalog,origin->product);
    const qa_product *other=origin->kind==FRONTEND_MUSIC_REMOTE?fallback(origin->catalog,source):NULL;
    qa_vfs *alternate=NULL;
    if(other && !qa_catalog_open(origin->catalog,other->id,&alternate,e)){
        qa_launch_instance_lease_release(metadata);return false;}
    frontend_music_content content = {.catalog = origin->catalog, .product = origin->product, .files = origin->files,
        .fallback_product=other?other->id:0,.fallback_files=alternate};
    frontend_music_policy_options options = {.music = origin->music, .bus = origin->bus, .audience = origin->physical_seat,
        .bus_gain = 1, .external_player = true, .sources = &content, .source_count = 1, .authored_cue = "", .random_seed = owner->seed};
    if (!frontend_music_policy_create(owner->frontend, &options, owner->policies + FRONTEND_MUSIC_WORLD, e)) {
        qa_vfs_destroy(alternate);qa_launch_instance_lease_release(metadata); return false;
    }
    qa_vfs_destroy(alternate);
    owner->buses[FRONTEND_MUSIC_WORLD] = origin->bus; owner->origin = *origin;
    owner->origin_recipe = origin->recipe != NULL;
    owner->origin_metadata = metadata; owner->has_origin = owner->origin_bound = true;
    return origin_current(owner) || fail(e, "Explicit music source changed during actual selection");
}
bool frontend_music_sources_explicit(frontend_music_sources *owner, const frontend_music_origin *origin,
    const char *intro, const char *loop, bool looping, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || !origin_current(owner) ||
        !origin || owner->origin.context != origin->context || owner->origin.music != origin->music ||
        owner->origin.bus != origin->bus || !origin_admit(owner, origin, e))
        return fail(e, "Explicit music return lost its actual admitted source player");
    if (!frontend_music_policy_explicit(owner->policies[FRONTEND_MUSIC_WORLD], intro ? intro : "", loop ? loop : "", looping, e)) return false;
    qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, origin->bus);
    return (!attached || (attached == origin->music && qa_audio_engine_music_output(owner->engine, origin->bus,
        attached, owner->output == FRONTEND_MUSIC_WORLD, e))) && origin_current(owner);
}
bool frontend_music_sources_explicit_play(frontend_music_sources *owner,const frontend_music_origin *origin,
    const char *cue,qa_error *e) {
    if (!frontend_music_sources_explicit_begin(owner,origin,e)) return false;
    if (!origin_current(owner) || !frontend_music_policy_source_play(owner->policies[FRONTEND_MUSIC_WORLD],cue,e)) return false;
    qa_audio_music *attached=qa_audio_engine_bus_music(owner->engine,origin->bus);
    return (!attached || (attached==origin->music && qa_audio_engine_music_output(owner->engine,origin->bus,
        attached,owner->output==FRONTEND_MUSIC_WORLD,e))) && origin_current(owner);
}
bool frontend_music_sources_explicit_pause(frontend_music_sources *owner,const frontend_music_origin *origin,
    bool paused,qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || !frontend_music_sources_explicit_selected(owner,origin) ||
        !origin_admit(owner,origin,e)) return fail(e,"Received music pause lost its actual selected source player");
    qa_audio_music_state state;
    if(!qa_audio_music_state_read(origin->music,&state))return false;
    if(!state.enabled)return origin_current(owner);
    qa_audio_music_pause(origin->music,paused);
    return qa_audio_music_state_read(origin->music,&state) && state.paused==paused && origin_current(owner);
}
bool frontend_music_sources_received_pause(frontend_music_sources *owner,bool paused,qa_error *e) {
    if(!owner || !frontend_music_sources_idle(owner) || owner->restoring || owner->frontend->capture || owner->frontend->source_restoring)
        return fail(e,"Received music pause requires its returned shared soundtrack owner");
    frontend_music_policy *policy=owner->policies[FRONTEND_MUSIC_WORLD];
    if(!policy)return true;
    qa_audio_music *music=frontend_music_policy_player(policy);qa_audio_music_state state;
    if(!music || !qa_audio_music_state_read(music,&state))return fail(e,"Received pause lost its actual soundtrack player");
    if(!state.enabled)return true;
    qa_audio_music_pause(music,paused);
    return qa_audio_music_state_read(music,&state) && state.paused==paused && frontend_music_sources_current(owner);
}
bool frontend_music_sources_explicit_retire(frontend_music_sources *owner, const void *context, qa_error *e) {
    if (!owner || !frontend_music_sources_current(owner)) return fail(e, "Explicit music retirement lost its installed owner");
    if (!owner->has_origin || owner->origin.context != context) return true;
    if (owner->restoring) {
        if (owner->busy || owner->frontend->capture || !owner->frontend->source_restoring ||
            !frontend_music_policy_destroy(owner->policies + FRONTEND_MUSIC_WORLD, e)) return false;
        owner->buses[FRONTEND_MUSIC_WORLD] = 0; origin_dispose(owner); return true;
    }
    return retire_world(owner, false, e);
}
bool frontend_music_sources_restore_origin_matches(const frontend_music_sources *owner, const frontend_music_origin *origin) {
    if (!origin || !origin->current || !origin->current(origin->context, origin)) return false;
    const qa_product *product = origin && origin->catalog ? qa_catalog_product(origin->catalog, origin->product) : NULL;
    const char *instance = origin && origin->descriptor ? origin->descriptor->selection.instance :
        origin && origin->recipe_provider ? origin->recipe_provider->selection.instance :
        origin ? origin->recipe_content : NULL;
    const qa_sha256_digest *identity = origin && origin->descriptor ? &origin->descriptor->identity :
        origin && origin->recipe ? qa_executable_recipe_digest(origin->recipe) : NULL;
    return frontend_music_sources_current(owner) && owner->restoring && owner->frontend->source_restoring &&
        owner->has_origin && origin && product && instance && identity && owner->origin_instance && owner->origin_product &&
        owner->origin_recipe == (origin->recipe != NULL) &&
        (owner->origin_recipe ? recipe_origin(origin) : origin->descriptor && !origin->recipe_provider) &&
        origin->kind == owner->origin.kind && origin->bus == owner->buses[FRONTEND_MUSIC_WORLD] &&
        origin->physical_seat == owner->origin.physical_seat && origin->receiver == owner->origin.receiver &&
        !strcmp(instance, owner->origin_instance) && !strcmp(product->key, owner->origin_product) &&
        qa_sha256_equal(identity, &owner->origin_identity);
}
bool frontend_music_sources_restore_origin(frontend_music_sources *owner, const frontend_music_origin *origin, qa_error *e) {
    if (!owner || !owner->restoring || !owner->frontend->source_restoring || !owner->has_origin || owner->origin_bound ||
        !origin_admit(owner, origin, e) || !frontend_music_sources_restore_origin_matches(owner, origin))
        return fail(e, "Saved explicit music does not resolve to its actual retained source declaration");
    const qa_product *row = qa_catalog_product(origin->catalog, origin->product);
    const qa_product *other=origin->kind==FRONTEND_MUSIC_REMOTE?fallback(origin->catalog,row):NULL;
    frontend_music_content content = {.catalog = origin->catalog, .product = origin->product, .files = origin->files,
        .fallback_product=other?other->id:0};
    if (!owner->origin_product || strcmp(row->key, owner->origin_product) ||
        !frontend_music_policy_source_is(owner->policies[FRONTEND_MUSIC_WORLD], &content))
        return fail(e, "Saved explicit music leaves its actual source product and private mounts");
    qa_launch_instance_lease *metadata = NULL;
    if (origin->descriptor && !qa_launch_instance_retain_metadata(origin->descriptor, &metadata, e)) return false;
    if (!qa_audio_music_controls_bind(origin->music, owner->controls, e) ||
        !frontend_music_policy_restore_player(owner->policies[FRONTEND_MUSIC_WORLD], origin->music, e)) {
        qa_launch_instance_lease_release(metadata); return false;
    }
    owner->origin = *origin; owner->origin_metadata = metadata; owner->origin_bound = true;
    return origin_current(owner) || fail(e, "Restored explicit music lost its actual constructed source");
}
bool frontend_music_sources_world(frontend_music_sources *owner, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || owner->frontend->capture || owner->frontend->source_restoring)
        return fail(e, "WORLD soundtrack selection requires its genuine published source boundary");
    if (!owner->engine) return true;
    bool retained_world = owner->world.metadata && frontend_music_world_current(owner);
    if (retained_world && (owner->policies[FRONTEND_MUSIC_WORLD] || owner->has_origin)) return true;
    if (owner->has_origin && !owner->world.metadata) {
        qa_application_map_view existing;
        if (!qa_application_map_read(owner->application, &existing) && origin_current(owner)) return true;
    }
    if (!retained_world) {
        if (!frontend_music_sources_world_retire(owner, e)) return false;
        qa_application_map_view map;
        if (!qa_application_map_read(owner->application, &map)) return true;
        if (!frontend_music_world_capture(owner, e)) return false;
    }
    const qa_launch_instance *instance = qa_launch_instance_lease_view(owner->world.metadata);
    qa_catalog *catalog = qa_launch_instance_catalog(instance);
    const qa_product *source = qa_catalog_product(catalog, instance->selection.product);
    if (!source || source->availability != QA_CONTENT_INSTALLED) return fail(e, "WORLD music lacks its actual selected source product");
    const qa_product *other = fallback(catalog, source); qa_vfs *alternate = NULL; char *cue = NULL;
    bool ok = world_cue(owner, source, &cue, e) && (!other || qa_catalog_open(catalog, other->id, &alternate, e));
    frontend_music_content content = {.catalog = catalog, .product = source->id, .files = instance->content,
        .fallback_product = other ? other->id : 0, .fallback_files = alternate};
    if (ok) ok = attach(owner, FRONTEND_MUSIC_WORLD, source, &content, 1, cue, e) && frontend_music_world_current(owner);
    free(cue); qa_vfs_destroy(alternate); return ok;
}
frontend_music_policy *frontend_music_sources_policy(const frontend_music_sources *owner, frontend_music_slot slot) {
    return frontend_music_sources_current(owner) && (unsigned)slot < 2 && owner->policies[slot] &&
        frontend_music_policy_binding_is(owner->policies[slot], owner->frontend, owner->engine,
            owner->buses[slot], slot == FRONTEND_MUSIC_MENU) ? owner->policies[slot] : NULL;
}
bool frontend_music_sources_bus(const frontend_music_sources *owner, frontend_music_slot slot, uint64_t *out) {
    if (!frontend_music_sources_current(owner) || !out || (unsigned)slot >= 2 || !owner->buses[slot] ||
        (!owner->restoring && !frontend_music_sources_policy(owner, slot))) return false;
    *out = owner->buses[slot]; return true;
}
bool frontend_music_sources_update(frontend_music_sources *owner, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || owner->frontend->capture)
        return fail(e, "Music frame requires its returned real source and playback owners");
    if (owner->policies[FRONTEND_MUSIC_WORLD] && !(owner->has_origin ? origin_current(owner) : frontend_music_world_current(owner)))
        return fail(e, "Automatic WORLD music retained a displaced published source");
    if (!frontend_music_sources_output(owner, owner->output, e)) return false;
    if (!frontend_music_sources_flush(owner, e)) return false;
    owner->busy = true; bool ok = true;
    if (owner->output == FRONTEND_MUSIC_WORLD && owner->policies[FRONTEND_MUSIC_WORLD])
        ok = frontend_music_policy_update(owner->policies[FRONTEND_MUSIC_WORLD], e);
    owner->busy = false; return ok;
}
bool frontend_music_sources_output(frontend_music_sources *owner, frontend_music_slot slot, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || owner->frontend->capture ||
        (unsigned)slot >= 2 || (slot == FRONTEND_MUSIC_WORLD && owner->policies[slot] &&
            !(owner->has_origin ? origin_current(owner) : frontend_music_world_current(owner))))
        return fail(e, "Music output requires its actual retained menu/GAME publication");
    if (owner->policies[slot] && !frontend_music_policy_attach(owner->policies[slot], e)) return false;
    for (size_t i = 0; i < 2; ++i) {
        qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, owner->buses[i]);
        if (attached && !qa_audio_engine_music_output(owner->engine, owner->buses[i], attached, i == (size_t)slot, e)) return false;
    }
    owner->output = slot; return true;
}
static bool command_print(frontend_music_sources *owner, const qa_command_invocation *call, const char *text, qa_error *e) {
    frontend_console_print(owner->frontend, &call->context, text);
    return (frontend_music_sources_current(owner) && qa_application_command_context_active(owner->application, &call->context)) ||
        fail(e, "Music command output retired its actual captured origin");
}
static bool track_number(const char *text, unsigned *out) {
    if (!text || !*text) return false;
    unsigned value = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9' || value > 255 / 10) return false;
        value = value * 10 + *p - '0'; if (value > 255) return false;
    }
    *out = value; return true;
}
static bool nonempty(const char *text) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)}; size_t at = 0; uint32_t point;
    while (qa_utf8_next(bytes, &at, &point)) if (!qa_unicode_whitespace(point)) return true;
    return false;
}
/* CD controls are real application state even when there is no installed
 * soundtrack. This branch owns no fabricated player or playback bus. */
static bool absent_command(frontend_music_sources *owner, const qa_command_invocation *call, qa_error *e) {
    if (call->console != qa_application_console(owner->application) || !call->argv || !call->argc || call->argc > 1024 ||
        !qa_application_command_context_active(owner->application, &call->context))
        return fail(e, "Music command lacks its actual captured console origin");
    for (size_t i = 0; i < call->argc; ++i) if (!call->argv[i]) return fail(e, "Music command has an absent source token");
    if (!strcmp(call->argv[0], "music")) {
        bool valid = call->argc >= 2 && call->argc <= 3 && nonempty(call->argv[1]) &&
            (call->argc != 3 || nonempty(call->argv[2]));
        return command_print(owner, call, valid ? "No soundtrack source selected.\n" : "music <intro> [loop]\n", e);
    }
    if (strcmp(call->argv[0], "cd")) return fail(e, "That invocation is not an application music command");
    if (call->argc == 1) return true;
    qa_buffer lowered = {0};
    if (!qa_utf8_lower((qa_bytes){(const uint8_t *)call->argv[1], strlen(call->argv[1])}, &lowered, e)) return false;
    const char *command = (const char *)lowered.data;
    bool ok = true;
    if (!strcmp(command, "on") || !strcmp(command, "off"))
        ok = qa_audio_music_controls_enable(owner->controls, !strcmp(command, "on"), e);
    else if (!strcmp(command, "reset")) ok = qa_audio_music_controls_reset(owner->controls, e);
    else if (!strcmp(command, "remap")) {
        if (call->argc == 2) for (unsigned i = 1; ok && i < 100; ++i) {
            unsigned mapped = qa_audio_music_controls_mapped_track(owner->controls, i);
            if (mapped != i) { char text[64]; snprintf(text, sizeof(text), "  %u -> %u\n", i, mapped); ok = command_print(owner, call, text, e); }
        }
        else {
            uint8_t values[99]; bool valid = call->argc - 2 <= 99;
            for (size_t i = 2; valid && i < call->argc; ++i) { unsigned value; valid = track_number(call->argv[i], &value); if (valid) values[i - 2] = (uint8_t)value; }
            ok = valid ? qa_audio_music_controls_remap(owner->controls, values, call->argc - 2, e) :
                command_print(owner, call, "cd remap requires at most 99 track numbers from 0 through 255.\n", e);
        }
    } else if (!strcmp(command, "info")) {
        bool enabled = false; const qa_cvar_view *row = qa_cvars_find(qa_application_cvars(owner->application), "bgmvolume");
        double gain = 0; char value[32], text[64];
        ok = qa_audio_music_controls_enabled(owner->controls, &enabled) && row && row->value &&
            qa_parse_ecmascript_number((qa_bytes){(const uint8_t *)row->value, strlen(row->value)}, &gain, e) && isfinite(gain);
        if (ok) ok = command_print(owner, call, enabled ? "Not playing.\n" : "CD music is disabled.\n", e) &&
            qa_format_ecmascript_number(fmax(0, fmin(1, gain)), value, e);
        if (ok) { snprintf(text, sizeof(text), "Volume is %s\n", value); ok = command_print(owner, call, text, e); }
    } else if (!strcmp(command, "close") || !strcmp(command, "eject")) {
        char text[128]; snprintf(text, sizeof(text), "cd %s: disc tray operations are unavailable with file-backed music.\n", command);
        ok = command_print(owner, call, text, e);
    } else if (strcmp(command, "stop") && strcmp(command, "pause") && strcmp(command, "resume"))
        ok = command_print(owner, call, "No soundtrack source selected.\n", e);
    qa_buffer_free(&lowered);
    return ok && frontend_music_sources_current(owner) && qa_application_command_context_active(owner->application, &call->context);
}
bool frontend_music_sources_flush(frontend_music_sources *owner, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || owner->frontend->capture)
        return fail(e, "Menu music flush requires its returned actual source owner");
    owner->busy = true;
    bool ok = !owner->policies[FRONTEND_MUSIC_MENU] || frontend_music_policy_update(owner->policies[FRONTEND_MUSIC_MENU], e);
    while (ok && owner->commands) {
        frontend_music_command *command = owner->commands;
        owner->commands = command->next; if (!owner->commands) owner->commands_tail = NULL;
        qa_command_invocation call = {.console = qa_application_console(owner->application), .context = command->context,
            .argc = command->argc, .argv = (const char *const *)command->argv};
        if (!qa_application_command_context_active(owner->application, &call.context))
            ok = fail(e, "Queued menu music belongs to a retired command publication");
        else if (owner->policies[FRONTEND_MUSIC_MENU]) {
            ok = frontend_music_policy_command(owner->policies[FRONTEND_MUSIC_MENU], &call, e);
            qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, owner->buses[FRONTEND_MUSIC_MENU]);
            if (ok && attached) ok = qa_audio_engine_music_output(owner->engine, owner->buses[FRONTEND_MUSIC_MENU], attached,
                owner->output == FRONTEND_MUSIC_MENU, e);
        }
        else ok = absent_command(owner, &call, e);
        frontend_music_command_free(command);
    }
    owner->busy = false; return ok;
}
static bool queue(frontend_music_sources *owner, const qa_command_invocation *call, qa_error *e) {
    if (call->console != qa_application_console(owner->application) || !call->argv || !call->argc || call->argc > 1024 ||
        !qa_application_command_context_active(owner->application, &call->context))
        return fail(e, "Menu music requires its genuine captured console request");
    size_t size = 0;
    for (size_t i = 0; i < call->argc; ++i) {
        if (!call->argv[i]) return fail(e, "Menu music has an absent captured argument");
        size_t n = strlen(call->argv[i]); if (n >= 9216 - size) return fail(e, "Menu music exceeds its actual console token capacity");
        size += n + 1;
    }
    if (strcmp(call->argv[0], "music") && strcmp(call->argv[0], "cd")) return fail(e, "That request is not menu music");
    frontend_music_command *command = calloc(1, sizeof(*command));
    if (!command) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining queued menu music");
    command->context = call->context; command->argc = call->argc;
    command->argv = calloc(command->argc, sizeof(*command->argv));
    if (!command->argv) { command->argc = 0; frontend_music_command_free(command); return frontend_fail(e, QA_ERROR_MEMORY, "Retaining menu music arguments"); }
    for (size_t i = 0; i < command->argc; ++i) {
        size_t n = strlen(call->argv[i]) + 1; command->argv[i] = malloc(n);
        if (!command->argv[i]) { frontend_music_command_free(command); return frontend_fail(e, QA_ERROR_MEMORY, "Retaining menu music argument bytes"); }
        memcpy(command->argv[i], call->argv[i], n);
    }
    if (call->context.script) {
        size_t n = strlen(call->context.script) + 1; command->script = malloc(n);
        if (!command->script) { frontend_music_command_free(command); return frontend_fail(e, QA_ERROR_MEMORY, "Retaining music script origin"); }
        memcpy(command->script, call->context.script, n); command->context.script = command->script;
    }
    if (owner->commands_tail) owner->commands_tail->next = command; else owner->commands = command;
    owner->commands_tail = command; return true;
}
bool frontend_music_sources_command(frontend_music_sources *owner, const qa_command_invocation *call, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || !call)
        return fail(e, "Music command requires its actual retained application music constructor");
    if (owner->output == FRONTEND_MUSIC_MENU) return queue(owner, call, e);
    frontend_music_policy *policy = owner->policies[FRONTEND_MUSIC_WORLD];
    if (policy && !(owner->has_origin ? origin_current(owner) : frontend_music_world_current(owner)))
        return fail(e, "Music command retained a displaced WORLD source");
    if (!policy) {
        owner->busy = true; bool ok = absent_command(owner, call, e); owner->busy = false; return ok;
    }
    owner->busy = true; bool ok = true, replacing = false;
    if (owner->has_origin) {
        ok = frontend_music_policy_manual_start(policy, call, &replacing);
        if (ok && replacing) ok = owner->origin.stop(owner->origin.context, e) && origin_current(owner);
    }
    if (ok) ok = frontend_music_policy_command(policy, call, e);
    qa_audio_music *attached = qa_audio_engine_bus_music(owner->engine, owner->buses[FRONTEND_MUSIC_WORLD]);
    if (ok && attached) ok = qa_audio_engine_music_output(owner->engine, owner->buses[FRONTEND_MUSIC_WORLD], attached, true, e);
    owner->busy = false; return ok;
}
size_t frontend_music_sources_bank_count(const frontend_music_sources *owner) {
    return owner ? frontend_music_policy_bank_count(owner->policies[0]) + frontend_music_policy_bank_count(owner->policies[1]) : 0;
}
qa_audio_bank *frontend_music_sources_bank_at(const frontend_music_sources *owner, size_t ordinal) {
    if (!owner) return NULL;
    size_t first = frontend_music_policy_bank_count(owner->policies[0]);
    return ordinal < first ? frontend_music_policy_bank_at(owner->policies[0], ordinal) : frontend_music_policy_bank_at(owner->policies[1], ordinal - first);
}
bool frontend_music_sources_content_visit(const frontend_music_sources *owner, const qa_application_content_visitor *visitor, qa_error *e) {
    if (!owner) return true;
    if (!frontend_music_sources_idle(owner) || !visitor || !visitor->catalog)
        return fail(e, "Music source inventory requires its actual returned owner");
    frontend_music_sources *held = (frontend_music_sources *)owner;
    held->busy = true;
    bool ok = visitor->catalog(visitor->context, owner->menu_catalog, e) && frontend_music_sources_current(owner) &&
        frontend_music_policy_content_visit(owner->policies[0], visitor, e) && frontend_music_sources_current(owner) &&
        frontend_music_policy_content_visit(owner->policies[1], visitor, e) && frontend_music_sources_current(owner);
    held->busy = false;
    return ok || fail(e, "Music source inventory retired its actual parent");
}
bool frontend_music_sources_destroy(frontend_music_sources **in, qa_error *e) {
    if (!in || !*in) return true;
    frontend_music_sources *owner = *in;
    if (!frontend_music_sources_current(owner) || owner->busy || owner->frontend->capture ||
        (!owner->restoring && !frontend_music_sources_idle(owner))) return fail(e, "Music sources still retain a callback or resource preparation");
    while (owner->commands) { frontend_music_command *command = owner->commands; owner->commands = command->next; frontend_music_command_free(command); }
    owner->commands_tail = NULL;
    if (owner->restoring) {
        for (size_t i = 0; i < 2; ++i) if (!frontend_music_policy_destroy(owner->policies + i, e)) return false;
        frontend_music_world_dispose(&owner->world); origin_dispose(owner); qa_audio_music_controls_release(owner->controls);
        qa_catalog_release(owner->menu_catalog); *in = NULL; free(owner); return true;
    }
    if (!frontend_music_sources_world_retire(owner, e) || !frontend_music_policy_destroy(owner->policies, e)) return false;
    if (owner->buses[0]) { qa_audio_engine_remove_music(owner->engine, owner->buses[0]);
        if (qa_audio_engine_bus_music(owner->engine, owner->buses[0])) return fail(e, "Menu soundtrack retirement retains its actual audio parent"); }
    qa_audio_music_controls_release(owner->controls); qa_catalog_release(owner->menu_catalog); *in = NULL; free(owner); return true;
}
void frontend_music_sources_rebind(frontend_music_sources *owner, qa_frontend *f, frontend_music_sources **slot) {
    owner->frontend = f; owner->application = f->application; owner->engine = f->audio; owner->slot = slot;
    for (size_t i = 0; i < 2; ++i) if (owner->policies[i]) frontend_music_policy_rebind(owner->policies[i], f, owner->policies + i);
}
