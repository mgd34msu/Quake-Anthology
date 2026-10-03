#include "internal.h"
#include "capture.h"
#include "music_sources.h"
#include "view_bindings.h"
#include "qa/application_character_selection.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_client.h"
#include "qa/input_release.h"
#include <inttypes.h>
bool frontend_seat_launch_id_read(const qa_frontend *f,uint32_t ordinal,uint32_t *out)
{
    if (!f || !f->application || !out || ordinal>=f->options.seats) return false;
    if (f->seats && f->seats[ordinal].input) {
        qa_console *console; qa_cvars *cvars; qa_command_context command;
        qa_application_client_source source;
        if (!qa_input_seat_recipient_read(f->seats[ordinal].input,&console,&cvars,&command)) return false;
        if (command.owner) {
            if (command.owner>UINT32_MAX ||
                !qa_application_client_physical_read(f->application,(qa_actor_owner)command.owner,command.seat,&source,NULL) ||
                !qa_application_client_associated(f->application,&source) || source.context.physical_seat!=ordinal ||
                source.context.console!=console || source.context.cvars!=cvars ||
                !qa_application_command_context_active(f->application,&command)) return false;
            *out=source.context.seat; return true;
        }
    }
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(f->application));
    if (!choices || ordinal>=choices->seat_count) return false;
    *out=choices->seats[ordinal].id; return true;
}
bool frontend_seat_ordinal_read(const qa_frontend *f,uint32_t launch_seat,uint32_t *out)
{
    if (!f || !f->application || !out) return false;
    uint32_t found=0; bool present=false;
    for (uint32_t i=0;i<f->options.seats;++i) {
        uint32_t actual;
        if (!frontend_seat_launch_id_read(f,i,&actual) || actual!=launch_seat) continue;
        if (present) return false;
        found=i; present=true;
    }
    if (present) *out=found;
    return present;
}
bool frontend_seat_actor_read(const qa_frontend *f,uint32_t ordinal,qa_actor_id *out)
{
    uint32_t launch_seat;
    return out && frontend_seat_launch_id_read(f,ordinal,&launch_seat) &&
        qa_application_player_actor(f->application,launch_seat,out);
}
bool frontend_command_seat_read(const qa_frontend *f,const qa_command_context *command,uint32_t *out)
{
    if (!f || !f->application || !command || !out || f->options.dedicated ||
        ((command->registry || command->generation) &&
         !qa_application_command_context_active(f->application,command))) return false;
    if (command->owner && command->owner<=UINT32_MAX) {
        qa_application_client_source source;
        if (qa_application_client_physical_read(f->application,(qa_actor_owner)command->owner,command->seat,&source,NULL) &&
            qa_application_client_associated(f->application,&source) && source.context.physical_seat<f->options.seats &&
            qa_application_command_context_active(f->application,command)) {
            *out=source.context.physical_seat; return true;
        }
    }
    const qa_launch_choices *choices=qa_launch_snapshot_choices(qa_application_launch(f->application));
    if (!choices) {
        if (command->actor.registry || command->seat>=f->options.seats) return false;
        *out=command->seat; return true;
    }
    bool scoped=command->origin==QA_COMMAND_SEAT || command->actor.registry!=0;
    for (size_t i=0;i<choices->seat_count && i<f->options.seats;++i) {
        if (!choices->seats[i].local || (scoped && choices->seats[i].id!=command->seat)) continue;
        if (command->actor.registry) {
            qa_actor_id actor;
            if (!qa_application_player_actor(f->application,choices->seats[i].id,&actor) ||
                !qa_actor_id_equal(actor,command->actor)) return false;
        }
        *out=(uint32_t)i; return true;
    }
    return false;
}
static bool context_seat(const qa_launch_snapshot *snapshot,uint32_t ordinal,uint32_t logical)
{
    const qa_launch_choices *choices=qa_launch_snapshot_choices(snapshot);
    return choices && ordinal<choices->seat_count && choices->seats[ordinal].id==logical &&
        choices->seats[ordinal].local && !choices->seats[ordinal].bot;
}
bool frontend_seat_context_ready(void *context,uint32_t ordinal,const qa_command_context *command,qa_error *error)
{
    const frontend_seat *seat=context;
    const qa_frontend *f=seat?seat->frontend:NULL;
    if (f && f->application && f->seats && ordinal<f->options.seats && seat==f->seats+ordinal &&
        seat->id==ordinal && command && command->owner && command->owner<=UINT32_MAX) {
        qa_application_client_source source;
        qa_input_release_scope all={.all=true,.controller=-1};
        qa_input_release *release=qa_input_seat_release_read(seat->input);
        bool captured_release=f->capture && release &&
            qa_input_release_scope_owned(release,seat->input,&all,NULL);
        if (qa_application_client_physical_read(f->application,(qa_actor_owner)command->owner,command->seat,&source,NULL) &&
            source.context.physical_seat==ordinal && qa_application_client_associated(f->application,&source) &&
            (qa_application_command_context_active(f->application,command) ||
                ((f->source_restoring || (captured_release &&
                        qa_input_release_console(release)==source.context.console)) &&
                    qa_application_client_retirement_current(f->application,&source)))) {
            const qa_command_context *actual=&source.context.command;
            if (command->origin==QA_COMMAND_SEAT && !command->script && !command->console_text &&
                command->owner==actual->owner && command->session==actual->session &&
                command->client==actual->client && command->seat==actual->seat &&
                command->dialect==actual->dialect && command->registry==actual->registry &&
                command->generation==actual->generation && command->direct==actual->direct &&
                qa_actor_id_equal(command->actor,actual->actor)) return true;
        }
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input context lost its actual physical CLIENT namespace");
    }
    if (!f || !f->application || !f->seats || ordinal>=f->options.seats || seat!=f->seats+ordinal ||
        seat->id!=ordinal || !command || command->origin!=QA_COMMAND_SEAT || command->owner ||
        command->session || command->client || command->script || command->console_text || !command->direct ||
        command->dialect<QA_CONSOLE_Q1 || command->dialect>QA_CONSOLE_Q3 ||
        (!command->actor.registry && (command->actor.generation || command->actor.slot)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input context requires its actual physical ENGINE seat");
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    if (command->registry || command->generation || command->actor.registry) {
        if (!qa_application_command_context_active(f->application,command) ||
            !context_seat(publication,ordinal,command->seat))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Input context belongs to a retired or different launch seat");
        return true;
    }
    const qa_launch_snapshot *candidate=qa_application_startup_candidate(f->application);
    if (context_seat(candidate,ordinal,command->seat) || context_seat(publication,ordinal,command->seat) ||
        (!candidate && !publication && command->seat==ordinal)) return true;
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Input template has no actual candidate or published launch seat");
}
#include <stdio.h>

const qa_product *frontend_product_selection(qa_catalog *catalog, const char *name)
{
    if (!catalog || !name) return NULL;
    const qa_product *product = qa_catalog_find(catalog, name);
    if (product) return product;
    qa_game_family family;
    if (!strcmp(name, "q1") || !strcmp(name, "qw")) family = QA_GAME_Q1;
    else if (!strcmp(name, "q2")) family = QA_GAME_Q2;
    else if (!strcmp(name, "q3")) family = QA_GAME_Q3;
    else return NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        product = qa_catalog_at(catalog, i);
        bool edition = !strcmp(name, "qw") ? product->edition == QA_EDITION_QUAKEWORLD : product->edition == QA_EDITION_CLASSIC;
        if (product->family == family && edition && product->builtin && product->availability == QA_CONTENT_INSTALLED) return product;
    }
    return NULL;
}
static bool overlay(qa_launch_draft *draft, const char *name, uint64_t roles, const char *instance,
    qa_launch_scope scope, qa_error *error)
{
    qa_catalog *catalog = qa_launch_draft_catalog(draft);
    const qa_product *product = frontend_product_selection(catalog, name);
    if (!product) return frontend_fail(error, QA_ERROR_ARGUMENT, "selected source product is not installed");
    qa_launch_draft *source = NULL;
    if (!qa_launch_draft_create(catalog, product->id, "", &source, error)) return false;
    const qa_launch_choices *choices = qa_launch_draft_choices(source);
    bool ok = true;
    for (size_t i = 0; i < choices->binding_count && ok; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (binding->scope.kind!=QA_SCOPE_DEFAULT_PLAYER || !(roles & QA_ROLE_BIT(binding->role))) continue;
        const qa_launch_provider *provider = NULL;
        for (size_t j = 0; j < choices->provider_count; ++j)
            if (!strcmp(choices->providers[j].instance, binding->instance)) provider = &choices->providers[j];
        if (!provider) { ok = frontend_fail(error, QA_ERROR_ARGUMENT, "source preset lacks selected role provider"); break; }
        qa_launch_provider selected = *provider;
        selected.instance = instance;
        qa_launch_binding route = *binding;
        route.instance = instance;
        route.scope = scope;
        ok = qa_launch_set_provider(draft, &selected, error) && qa_launch_bind(draft, &route, error);
    }
    qa_launch_draft_destroy(source);
    return ok;
}
bool frontend_player_source_select(qa_frontend *frontend,uint32_t physical,qa_launch_role role,
    const qa_product *product,qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->seats || physical>=frontend->options.seats ||
        frontend->options.dedicated || frontend_network_remote(frontend) ||
        frontend->player_source_draft || qa_application_startup_pending(frontend->application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Player source selection requires its published local seat");
    const qa_launch_snapshot *publication=qa_application_launch(frontend->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
    qa_actor_id actor; uint32_t logical;
    qa_catalog *catalog=qa_application_catalog(frontend->application);
    if (!choices || physical>=choices->seat_count || !choices->seats[physical].local || choices->seats[physical].bot ||
        !frontend_seat_launch_id_read(frontend,physical,&logical) || logical!=choices->seats[physical].id ||
        !qa_application_player_actor(frontend->application,logical,&actor) ||
        !product || qa_catalog_product(catalog,product->id)!=product || !product->builtin ||
        product->program_kind!=QA_PROGRAM_BUILTIN || product->availability!=QA_CONTENT_INSTALLED ||
        (role!=QA_ROLE_MOVEMENT && role!=QA_ROLE_CHARACTER && role!=QA_ROLE_ARSENAL))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Player source selection lacks its actual actor and installed native product");
    qa_launch_scope scope={.kind=QA_SCOPE_SEAT,.seat=logical};
    const qa_launch_binding *binding=qa_launch_binding_for(choices,scope,role,"");
    const qa_launch_instance *current=binding?qa_launch_snapshot_find(publication,binding->instance):NULL;
    if (current && current->selection.product==product->id) return true;
    qa_launch_draft *draft=NULL;
    if (!qa_launch_snapshot_draft_copy(publication,&draft,error)) return false;
    const char *name=role==QA_ROLE_MOVEMENT?"movement":role==QA_ROLE_CHARACTER?"character":"arsenal";
    char instance_name[64];
    snprintf(instance_name,sizeof(instance_name),"frontend:seat:%" PRIu32 ":%s",logical,name);
    uint64_t roles=QA_ROLE_BIT(role);
    if (role==QA_ROLE_CHARACTER)
        roles|=QA_ROLE_BIT(QA_ROLE_BODY)|QA_ROLE_BIT(QA_ROLE_SKIN)|QA_ROLE_BIT(QA_ROLE_VOICE);
    bool ok=overlay(draft,product->key,roles,instance_name,scope,error);
    if (ok && role==QA_ROLE_CHARACTER) {
        qa_native_q3_character_declaration declaration;
        ok=qa_native_q3_character_default_declaration(product->family,&declaration,error);
        if (ok) {
            qa_launch_seat seat=choices->seats[physical];
            seat.character_model=declaration.model; seat.character_skin=declaration.skin;
            seat.character_head_model=declaration.head_model; seat.character_head_skin=declaration.head_skin;
            ok=qa_launch_set_seat(draft,&seat,error);
        }
    }
    if (!ok) { qa_launch_draft_destroy(draft); return false; }
    frontend->player_source_draft=draft;
    frontend->player_source_publication=publication;
    frontend->player_source_generation=qa_application_configuration_generation(frontend->application);
    frontend->player_source_actor=actor;
    frontend->player_source_physical=physical;
    frontend->player_source_logical=logical;
    return true;
}
void frontend_player_sources_discard(qa_frontend *frontend)
{
    if (!frontend) return;
    qa_launch_draft_destroy(frontend->player_source_draft);
    frontend->player_source_draft=NULL;
    frontend->player_source_publication=NULL;
    frontend->player_source_generation=0;
    frontend->player_source_actor=(qa_actor_id){0};
    frontend->player_source_physical=frontend->player_source_logical=0;
}
bool frontend_player_sources_drain(qa_frontend *frontend,qa_error *error)
{
    if (!frontend || !frontend->player_source_draft) return true;
    uint32_t logical; qa_actor_id actor;
    if (frontend->stepping || frontend->preparing || frontend->round || frontend->capture ||
        frontend->resource_inventory || frontend->shutdown || frontend->source_restoring ||
        !frontend_owners_idle(frontend) || !frontend_seat_callbacks_idle(frontend) ||
        qa_application_startup_pending(frontend->application) ||
        qa_application_configuration_generation(frontend->application)!=frontend->player_source_generation ||
        qa_application_launch(frontend->application)!=frontend->player_source_publication ||
        !frontend_seat_launch_id_read(frontend,frontend->player_source_physical,&logical) ||
        logical!=frontend->player_source_logical ||
        !qa_application_player_actor(frontend->application,logical,&actor) ||
        !qa_actor_id_equal(actor,frontend->player_source_actor))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Player source draft lost its returned published seat");
    bool ok=qa_application_apply(frontend->application,frontend->player_source_draft,error);
    frontend_player_sources_discard(frontend);
    return ok;
}
static char *launch_map_path(const char *input, qa_error *error)
{
    char *name = qa_vfs_normalize_path(input, error);
    if (!name) return NULL;
    size_t length = strlen(name);
    if (!length || length > SIZE_MAX - 10) {
        free(name);
        frontend_fail(error, length ? QA_ERROR_MEMORY : QA_ERROR_ARGUMENT,
            length ? "launch map path is too long" : "launch map has no destination");
        return NULL;
    }
    bool prefix = !strncmp(name, "maps/", 5);
    bool suffix = length >= 4 && !strcmp(name + length - 4, ".bsp");
    char *path = malloc(length + (prefix ? 0u : 5u) + (suffix ? 0u : 4u) + 1u);
    if (!path) {
        free(name);
        frontend_fail(error, QA_ERROR_MEMORY, "cannot retain launch map path");
        return NULL;
    }
    size_t used = 0;
    if (!prefix) { memcpy(path, "maps/", 5); used = 5; }
    memcpy(path + used, name, length);
    used += length;
    if (!suffix) { memcpy(path + used, ".bsp", 4); used += 4; }
    path[used] = 0;
    free(name);
    return path;
}
bool frontend_launch(qa_frontend *frontend, qa_error *error)
{
    if (!frontend->options.game) return qa_application_startup_bootstrap(frontend->application,error);
    qa_catalog *catalog = qa_application_catalog(frontend->application);
    const qa_product *product = frontend_product_selection(catalog, frontend->options.game);
    if (!product || product->availability != QA_CONTENT_INSTALLED)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "launch product is not installed");
    const char *map = frontend->options.map;
    const qa_catalog_start *starts = NULL;
    const qa_catalog_episode *episode = NULL;
    size_t count = 0;
    if (!map) {
        starts = qa_catalog_starts(catalog, product->id, &episode, &count);
        if (count) map = starts[0].path;
        else {
            const qa_catalog_map *maps = qa_catalog_maps(catalog, product->id, &count);
            if (count) map = maps[0].path;
        }
    }
    if (!map) return frontend_fail(error, QA_ERROR_ARGUMENT, "launch product has no installed map");
    char *selected_map = frontend->options.map ? launch_map_path(map, error) : NULL;
    if (frontend->options.map && !selected_map) return false;
    if (selected_map) map = selected_map;
    qa_launch_draft *draft = NULL;
    bool created = qa_launch_draft_create(catalog, product->id, map, &draft, error);
    free(selected_map);
    if (!created) return false;
    if (frontend->options.original && !qa_launch_select_original(draft, "native:primary", error)) {
        qa_launch_draft_destroy(draft);
        return false;
    }
    if (frontend->options.game_type && !qa_launch_select_game_type(draft, frontend->options.game_type, error)) {
        qa_launch_draft_destroy(draft);
        return false;
    }
    if ((product->family == QA_GAME_Q1 || product->family == QA_GAME_Q2) && frontend->options.seats > 1 &&
        !frontend->options.dedicated) {
        const qa_launch_choices *choices = qa_launch_draft_choices(draft);
        for (size_t i = 0; i < choices->mode_count; ++i) {
            qa_launch_mode mode = choices->modes[i];
            if (mode.rules.kind != QA_MODE_SINGLE_PLAYER) continue;
            mode.rules.kind = QA_MODE_COOPERATIVE;
            if (!qa_launch_set_mode(draft, &mode, error)) {
                qa_launch_draft_destroy(draft);
                return false;
            }
            choices = qa_launch_draft_choices(draft);
        }
    }
    qa_launch_world world = qa_launch_draft_choices(draft)->world;
    if (starts && count) world.start_command = episode && episode->command && *episode->command ? episode->command : starts[0].bsp;
    bool ok = true;
    if (frontend->options.map_game) {
        const qa_product *geometry = frontend_product_selection(catalog, frontend->options.map_game);
        if (!geometry || geometry->availability != QA_CONTENT_INSTALLED) ok = frontend_fail(error, QA_ERROR_ARGUMENT, "map source is not installed");
        else { world.geometry = geometry->id; world.start_command = NULL; }
    }
    if (ok) ok = qa_launch_set_world(draft, &world, error);
    if (ok && frontend->options.movement) ok = overlay(draft, frontend->options.movement,
        QA_ROLE_BIT(QA_ROLE_MOVEMENT), "frontend:movement", (qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER}, error);
    if (ok && frontend->options.character) ok = overlay(draft, frontend->options.character,
        QA_ROLE_BIT(QA_ROLE_CHARACTER) | QA_ROLE_BIT(QA_ROLE_BODY) | QA_ROLE_BIT(QA_ROLE_SKIN) | QA_ROLE_BIT(QA_ROLE_VOICE), "frontend:character",
        (qa_launch_scope){.kind=QA_SCOPE_DEFAULT_PLAYER}, error);
    for (size_t i = 0; i < frontend->options.mod_count && ok; ++i) {
        char instance[64];
        snprintf(instance, sizeof(instance), "frontend:addon:%zu", i);
        ok = qa_launch_set_mod(draft, &(qa_launch_mod_selection){.instance = instance,
            .component = frontend->options.mods[i], .enabled = true}, error);
    }
    for (unsigned i = 0; i < frontend->options.seats && !frontend->options.dedicated && ok; ++i) {
        char name[32]; snprintf(name, sizeof(name), "Player %u", i + 1);
        const qa_launch_choices *choices=qa_launch_draft_choices(draft);
        const qa_launch_binding *binding=qa_launch_binding_for(choices,
            (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=i},QA_ROLE_CHARACTER,"");
        const qa_launch_provider *provider=NULL;
        for (size_t j=0;binding && j<choices->provider_count;++j)
            if (!strcmp(choices->providers[j].instance,binding->instance)) { provider=&choices->providers[j]; break; }
        const qa_product *character=provider?qa_catalog_product(catalog,provider->product):NULL;
        qa_native_q3_character_declaration declaration;
        if (!character) { ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Local character constructor has no selected product"); break; }
        if (!qa_native_q3_character_default_declaration(character->family,&declaration,error)) { ok=false; break; }
        if (frontend->options.character_model)
            declaration.model=declaration.head_model=frontend->options.character_model;
        ok = qa_launch_set_seat(draft, &(qa_launch_seat){.id = i, .name = name, .local = true,
            .input_device = i,.character_model=declaration.model,.character_skin=declaration.skin,
            .character_head_model=declaration.head_model,.character_head_skin=declaration.head_skin}, error);
    }
    if (ok) ok = qa_application_apply(frontend->application, draft, error);
    if (ok && !qa_application_startup_pending(frontend->application))
        ok=frontend_view_bindings_apply_restored(frontend,error);
    if (ok && !qa_application_startup_pending(frontend->application) && frontend->music_sources)
        ok=frontend_music_sources_world(frontend->music_sources,error) &&
            frontend_source_publish_music(frontend,error) &&
            frontend_music_sources_output(frontend->music_sources,FRONTEND_MUSIC_WORLD,error);
    qa_launch_draft_destroy(draft);
    return ok;
}
