#include "character_selection.h"
#include "qa/application_startup_prepare.h"
#include "qa/game_q2_preferences.h"
#include "qa/network_q3.h"
#include "qa/text.h"
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool declare(qa_cvars *cvars,const char *name,const char *value,uint32_t flags,qa_error *error)
{
    const qa_cvar_view *previous=qa_cvars_find(cvars,name);
    if (qa_cvars_dialect(cvars)<=QA_RULESET_QUAKEWORLD && previous && !previous->console_created)
        return (previous->flags&flags)==flags || qa_cvars_add_flags(cvars,name,flags,error);
    return qa_cvars_register(cvars,name,value,flags,0,"Prepared client identity",error);
}
static const char *q2_default_skin(const char *model)
{ return !strcmp(model,"female")?"athena":!strcmp(model,"cyborg")?"oni911":"grunt"; }

bool qa_application_player_userinfo_register(qa_cvars *cvars,uint32_t seat,const char *model,qa_error *error)
{
    if (!cvars || !model || !*model) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Client configuration needs its actual identity declaration"); return false;
    }
    qa_ruleset_id dialect=qa_cvars_dialect(cvars);
    const uint32_t identity=QA_CVAR_ARCHIVE|QA_CVAR_USERINFO;
    char name[64]; snprintf(name,sizeof(name),"Player %" PRIu64,(uint64_t)seat+1);
    if (dialect<=QA_RULESET_QUAKEWORLD && !declare(cvars,"qts_weapon_autoswitch","always",identity,error)) return false;
    if (dialect==QA_RULESET_Q2_RERELEASE && !declare(cvars,"autoswitch","0",identity,error)) return false;
    if (dialect==QA_RULESET_NETQUAKE) {
        const qa_cvar_view *old_name=qa_cvars_find(cvars,"name"),*color=qa_cvars_find(cvars,"color");
        return declare(cvars,"_cl_name",old_name?old_name->value:name,QA_CVAR_ARCHIVE,error) &&
            declare(cvars,"_cl_color",color?color->value:"0",QA_CVAR_ARCHIVE,error);
    }
    if (dialect==QA_RULESET_QUAKEWORLD) {
        static const char *names[]={"topcolor","bottomcolor","team","skin"};
        if (!declare(cvars,"name",name,identity,error)) return false;
        for (size_t i=0;i<4;++i) if (!declare(cvars,names[i],i<2?"0":"",identity,error)) return false;
        return true;
    }
    if (dialect==QA_RULESET_Q3) {
        static const struct {const char *name,*value; uint32_t flags;} prefix[]={
            {"vm_ui","2",QA_CVAR_ARCHIVE},{"vm_cgame","2",QA_CVAR_ARCHIVE},
            {"cl_allowDownload","0",QA_CVAR_ARCHIVE},
            {"cl_timeNudge","0",QA_CVAR_TEMPORARY},{"rate","25000",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"cl_maxpackets","30",QA_CVAR_ARCHIVE},{"cl_packetdup","1",QA_CVAR_ARCHIVE},
            {"snaps","20",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO}};
        for (size_t i=0;i<sizeof(prefix)/sizeof(*prefix);++i)
            if (!declare(cvars,prefix[i].name,prefix[i].value,prefix[i].flags,error)) return false;
        if (!declare(cvars,"name",name,identity,error)) return false;
        size_t length=strlen(model);
        char *body=length<=SIZE_MAX-9?malloc(length+9):NULL;
        if (!body) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual prepared client model"); return false; }
        memcpy(body,model,length); memcpy(body+length,"/default",9);
        static const char *names[]={"model","headmodel","team_model","team_headmodel"};
        bool ok=true;
        for (size_t i=0;ok && i<4;++i) ok=declare(cvars,names[i],body,identity,error);
        free(body); if (!ok) return false;
        static const struct {const char *name,*value; uint32_t flags;} suffix[]={
            {"color1","4",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"color2","5",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"sex","male",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"cl_anonymous","0",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"cg_predictItems","1",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},{"teamtask","0",QA_CVAR_USERINFO},
            {"password","",QA_CVAR_USERINFO},{"handicap","100",QA_CVAR_ARCHIVE|QA_CVAR_USERINFO},
            {"cl_maxPing","800",QA_CVAR_ARCHIVE},{"cl_serverStatusResendTime","750",0},
            {"sv_master1","master.quake3arena.com",0}};
        for (size_t i=0;i<sizeof(suffix)/sizeof(*suffix);++i)
            if (!declare(cvars,suffix[i].name,suffix[i].value,suffix[i].flags,error)) return false;
        return true;
    }
    if (!declare(cvars,"name",name,identity,error) || !declare(cvars,"spectator","0",QA_CVAR_USERINFO,error) ||
        !declare(cvars,"password","",QA_CVAR_USERINFO,error)) return false;
    const char *skin=q2_default_skin(model);
    size_t a=strlen(model),b=strlen(skin);
    char *body=a<=SIZE_MAX-b-2?malloc(a+b+2):NULL;
    if (!body) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual prepared client skin"); return false; }
    memcpy(body,model,a); body[a]='/'; memcpy(body+a+1,skin,b+1);
    bool ok=declare(cvars,"skin",body,identity,error); free(body);
    static const char *names[]={"rate","msg","hand","fov","gender"};
    const char *values[]={"25000","1","0","90",!strcmp(model,"female")?"female":"male"};
    for (size_t i=0;ok && i<5;++i) ok=declare(cvars,names[i],values[i],identity,error);
    return ok;
}

typedef struct character_selection_owner {
    qa_application *application;
    uint32_t seat;
    qa_launch_instance_lease *metadata;
    qa_native_q3_character_selection selection;
    application_character_names names;
    const qa_launch_choices *choices;
    qa_actor_id actor;
} character_selection_owner;

bool qa_native_q3_character_default_declaration(qa_game_family family,
    qa_native_q3_character_declaration *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor declaration requires output");
    const char *model;
    switch (family) {
    case QA_GAME_Q1: model = "player"; break;
    case QA_GAME_Q2: model = "male"; break;
    case QA_GAME_Q3: model = "sarge"; break;
    default: return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor has no declared source family");
    }
    *out = (qa_native_q3_character_declaration){model, "default", model, "default"};
    return true;
}

static const qa_launch_binding *actor_binding(const qa_launch_choices *choices, qa_actor_id actor)
{
    if (!actor.registry) return NULL;
    for (size_t i = 0; i < choices->binding_count; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (binding->role == QA_ROLE_CHARACTER && !*binding->selector &&
            binding->scope.kind == QA_SCOPE_ACTOR && qa_actor_id_equal(binding->scope.actor, actor))
            return binding;
    }
    return NULL;
}

static const qa_launch_binding *seat_binding(const qa_launch_choices *choices, const qa_launch_seat *seat)
{
    const qa_launch_binding *binding = actor_binding(choices, seat->actor);
    return binding ? binding : qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, QA_ROLE_CHARACTER, "");
}

static bool declared(qa_catalog *catalog, const qa_launch_choices *choices, const qa_launch_seat *seat,
    const qa_launch_binding *binding, qa_application_character_declaration *out, bool *found, qa_error *error)
{
    *found = false;
    const char *fields[] = {seat->character_model, seat->character_skin,
        seat->character_head_model, seat->character_head_skin};
    if (!fields[0] && !fields[1] && !fields[2] && !fields[3]) return true;
    for (size_t i = 0; i < 4; ++i)
        if (!fields[i] || (i != 2 && !*fields[i]) || strpbrk(fields[i], "\\\";\r\n"))
            return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor declaration is incomplete or invalid");
    const qa_launch_provider *provider = NULL;
    if (binding)
        for (size_t i = 0; i < choices->provider_count; ++i)
            if (!strcmp(choices->providers[i].instance, binding->instance)) {
                provider = &choices->providers[i]; break;
            }
    const qa_product *product = provider ? qa_catalog_product(catalog, provider->product) : NULL;
    if (!binding || !binding->definition || !provider || !product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration lost its actual selected binding or product");
    *out = (qa_application_character_declaration){.provider = provider, .binding = binding,
        .product = provider->product, .family = product->family, .definition = binding->definition,
        .appearance = {fields[0], fields[1], fields[2], fields[3]}};
    *found = true;
    return true;
}

bool qa_application_character_declaration_read(qa_catalog *catalog, const qa_launch_choices *choices,
    const qa_launch_seat *seat, qa_application_character_declaration *out, bool *found, qa_error *error)
{
    if (!catalog || !choices || !seat || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration requires its actual candidate choices");
    return declared(catalog, choices, seat, seat_binding(choices, seat), out, found, error);
}

bool application_character_userinfo(qa_application *app,qa_catalog *catalog,const qa_launch_choices *choices,
    const qa_launch_seat *seat, qa_game_family protocol, bool local_ip,
    char *out, size_t capacity, qa_error *error)
{
    qa_application_character_declaration declaration;
    bool found;
    if (!app || !out || !capacity || !seat || !seat->name)
        return application_fail(error, QA_ERROR_ARGUMENT, "Initial userinfo requires its actual CHARACTER declaration");
    if (!qa_application_character_declaration_read(catalog, choices, seat, &declaration, &found, error)) return false;
    const char *name = strchr(seat->name, '\\') ? "badinfo" : seat->name;
    if (protocol == QA_GAME_Q2 || protocol == QA_GAME_Q3) {
        qa_cvars *prepared=NULL;
        const qa_cvar_view *field_of_view=NULL;
        bool configured=false;
        const qa_application_startup_hooks *hooks=app->startup_hooks;
        if (seat->local && !seat->bot && hooks && hooks->local_userinfo &&
            !hooks->local_userinfo(hooks->context,app,choices,seat,&prepared,&field_of_view,
                &configured,error)) return false;
        const qa_product *product=found?qa_catalog_product(catalog,declaration.product):NULL;
        qa_ruleset_id dialect=protocol==QA_GAME_Q3?QA_RULESET_Q3:configured && prepared &&
            (qa_cvars_dialect(prepared)==QA_RULESET_Q2_CLASSIC || qa_cvars_dialect(prepared)==QA_RULESET_Q2_RERELEASE)?
            qa_cvars_dialect(prepared):product && product->family==QA_GAME_Q2 && product->edition==QA_EDITION_RERELEASE?
            QA_RULESET_Q2_RERELEASE:QA_RULESET_Q2_CLASSIC;
        const char *model=found && (protocol==QA_GAME_Q3 || declaration.family==QA_GAME_Q2)?
            declaration.appearance.model:protocol==QA_GAME_Q3?"sarge":"male";
        char *fov = NULL;
        if (protocol==QA_GAME_Q2 && field_of_view) {
            size_t size = strlen(field_of_view->value) + 1;
            fov = malloc(size);
            if (!fov) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual player FOV");
            memcpy(fov, field_of_view->value, size);
        }
        qa_cvar_options projection = {.dialect=dialect, .side=QA_CVAR_SIDE_CLIENT,
            .role=QA_CVAR_ROLE_CGAME, .seat=seat->id};
        qa_cvars *protocol_cvars=configured && prepared && qa_cvars_dialect(prepared)==dialect
            ? prepared : qa_cvars_create_view(app->cvars,&projection,error);
        if (!protocol_cvars || (protocol_cvars==prepared && !qa_cvars_retain(protocol_cvars,error))) {
            free(fov); return false;
        }
        bool ok=qa_application_player_userinfo_register(protocol_cvars,seat->id,model,error);
        if (ok && !configured && !qa_cvars_is_set(protocol_cvars,"name"))
            ok=qa_cvars_set(protocol_cvars,"name",name,true,error);
        if (ok && protocol==QA_GAME_Q2)
            ok=qa_cvars_set(protocol_cvars,"spectator",seat->spectator?"1":"0",true,error);
        qa_buffer info={0};
        if (ok) ok=qa_cvars_info(protocol_cvars,QA_CVAR_USERINFO,capacity,&info,error);
        if (ok && fov) {
            double preference;
            qa_buffer projected={0};
            ok=qa_parse_number((qa_bytes){(const uint8_t *)fov,strlen(fov)},&preference,error) &&
                qa_q2_userinfo_field_of_view((const char *)info.data,preference,&projected,error);
            if (ok && projected.size>=capacity)
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Initial userinfo exceeds its actual Source extent");
            if (ok) { qa_buffer_free(&info); info=projected; projected=(qa_buffer){0}; }
            qa_buffer_free(&projected);
        }
        if (ok) memcpy(out,info.data,info.size+1);
        if (ok && protocol==QA_GAME_Q3) {
            const char *team=seat->spectator?"s":seat->team && *seat->team?seat->team:"free";
            char q3_name[901]; snprintf(q3_name,sizeof(q3_name),"%.900s",name);
            ok=qa_q3_info_set(out,capacity,"name",q3_name,error) &&
                qa_q3_info_set(out,capacity,"team",team,error) &&
                qa_q3_info_set(out,capacity,"ip",local_ip?"localhost":"",error);
        }
        if (ok && (protocol==QA_GAME_Q2 || found)) {
            qa_native_q3_character_declaration fallback={"male","grunt","male","grunt"};
            const qa_native_q3_character_declaration *appearance=found &&
                (protocol==QA_GAME_Q3 || declaration.family==QA_GAME_Q2)?&declaration.appearance:&fallback;
            const char *head=*appearance->head_model?appearance->head_model:appearance->model;
            const char *keys[]={protocol==QA_GAME_Q2?"skin":"model","headmodel"};
            const char *models[]={appearance->model,head};
            const char *skin=protocol==QA_GAME_Q2 && !strcmp(appearance->skin,"default")?
                q2_default_skin(appearance->model):appearance->skin;
            const char *skins[]={skin,appearance->head_skin};
            size_t count=protocol==QA_GAME_Q2?1:2;
            char *body=malloc(capacity);
            if (!body) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining actual character identity");
            for (size_t i=0;ok && i<count;++i) {
                if (protocol==QA_GAME_Q2 && !strcmp(appearance->skin,"default")) {
                    ok=qa_q3_info_value(out,"skin",body,capacity,error);
                    if (!ok) break;
                    const char *separator=strchr(body,'/');
                    size_t model_length=strlen(models[i]);
                    if (separator && (size_t)(separator-body)==model_length && separator[1] &&
                        !memcmp(body,models[i],model_length)) continue;
                }
                int length=snprintf(body,capacity,"%s/%s",models[i],skins[i]);
                ok=(length>=0 && (size_t)length<capacity) ||
                    application_fail(error,QA_ERROR_ARGUMENT,"Initial CHARACTER userinfo exceeds its source extent");
                if (ok) ok=qa_q3_info_set(out,capacity,keys[i],body,error);
            }
            free(body);
        }
        free(fov); qa_buffer_free(&info); qa_cvars_destroy(protocol_cvars); return ok;
    } else return application_fail(error, QA_ERROR_ARGUMENT, "Initial userinfo has no declared protocol constructor");
}

bool application_character_q2_initial_userinfo(const char *source,const char *defaults,
    char *out,size_t capacity,qa_error *error)
{
    if (!source || !defaults || !out || !capacity)
        return application_fail(error,QA_ERROR_ARGUMENT,"Initial Q2 userinfo requires its actual Source and declared defaults");
    size_t length=strlen(source);
    if (length>=capacity)
        return application_fail(error,QA_ERROR_ARGUMENT,"Initial Q2 userinfo exceeds its source extent");
    memcpy(out,source,length+1);
    /* Retain every supplied byte, including an explicitly empty value. Only
     * absent protocol keys receive the canonical prepared default/settings. */
    for (const char *p=defaults;*p;) {
        const char *pair=p++;
        const char *key=p;
        while (*p && *p!='\\') ++p;
        size_t key_length=(size_t)(p-key);
        if (!*p) break;
        ++p;
        while (*p && *p!='\\') ++p;
        bool present=false;
        const char *existing=source;
        if (*existing=='\\') ++existing;
        while (*existing) {
            const char *at=existing;
            while (*existing && *existing!='\\') ++existing;
            if (!*existing) break;
            size_t size=(size_t)(existing-at);
            ++existing;
            if (size==key_length && !memcmp(at,key,size)) { present=true; break; }
            while (*existing && *existing!='\\') ++existing;
            if (*existing) ++existing;
        }
        if (present) continue;
        size_t added=(size_t)(p-pair);
        if (added>=capacity-length)
            return application_fail(error,QA_ERROR_ARGUMENT,"Initial prepared Q2 userinfo exceeds its source extent");
        memcpy(out+length,pair,added);length+=added;out[length]=0;
    }
    return true;
}

static bool constructor_choices(qa_application *app, qa_actor_owner receiver,
    const qa_launch_snapshot **out, qa_error *error)
{
    if (!app || !receiver || !out || app->destroy_requested || app->state == QA_APPLICATION_FAULTED)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor requires its actual source callback owner");
    const qa_launch_snapshot *snapshot = app->routing_snapshot ? app->routing_snapshot : qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    application_provider **providers = app->routing_snapshot ? app->routing_providers : app->providers;
    size_t count = app->routing_snapshot ? app->routing_provider_count : app->provider_count;
    application_provider *source = NULL;
    for (size_t i = 0; providers && i < count; ++i)
        if (providers[i] && providers[i]->owner == receiver) { source = providers[i]; break; }
    const qa_launch_provider *selected = NULL;
    if (choices && source && source->launch)
        for (size_t i = 0; i < choices->provider_count; ++i)
            if (!strcmp(choices->providers[i].instance, source->launch->selection.instance)) {
                selected = &choices->providers[i]; break;
            }
    if (!snapshot || !choices || !source || source->application != app || source->close_pending ||
        !selected || selected->product != source->launch->selection.product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor callback left its actual routing source");
    *out = snapshot;
    return true;
}

bool qa_application_character_constructor_read(qa_application *app, qa_actor_owner receiver,
    uint32_t id, qa_application_character_declaration *out, bool *found, qa_error *error)
{
    const qa_launch_snapshot *snapshot;
    if (!out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor requires declaration output");
    if (!constructor_choices(app, receiver, &snapshot, error)) return false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    *found = false;
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == id)
            return qa_application_character_declaration_read(qa_launch_snapshot_catalog(snapshot), choices,
                &choices->seats[i], out, found, error);
    return true;
}

bool qa_application_constructor_seat_ordinal(qa_application *app, qa_actor_owner receiver,
    uint32_t id, uint32_t *physical_ordinal, qa_error *error)
{
    const qa_launch_snapshot *snapshot;
    if (!physical_ordinal)
        return application_fail(error, QA_ERROR_ARGUMENT, "Constructor seat requires physical ordinal output");
    if (!constructor_choices(app, receiver, &snapshot, error)) return false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == id) {
            if (i > UINT32_MAX)
                return application_fail(error, QA_ERROR_ARGUMENT, "Constructor seat exceeds source client extent");
            *physical_ordinal = (uint32_t)i;
            return true;
        }
    return application_fail(error, QA_ERROR_ARGUMENT, "Constructor seat is absent from the actual routing choices");
}

static bool published(qa_application *app, uint32_t id, qa_actor_id actor,
    qa_application_character_declaration *out, bool *found, qa_error *error)
{
    const qa_launch_snapshot *snapshot = qa_application_launch(app);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!snapshot || !choices)
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration has no actual publication");
    const qa_launch_seat *seat = NULL;
    for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == id) { seat = &choices->seats[i]; break; }
    *found = false;
    if (!seat) return true;
    const qa_launch_binding *binding = actor_binding(choices, actor);
    qa_actor_id configured;
    if (!binding && application_player_source_actor(app, actor, &configured))
        binding = actor_binding(choices, configured);
    if (!binding) binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, QA_ROLE_CHARACTER, "");
    return declared(qa_launch_snapshot_catalog(snapshot), choices, seat, binding, out, found, error);
}

static bool same_declaration(const qa_native_q3_character_declaration *a,
    const qa_native_q3_character_declaration *b)
{
    return a && a->model && a->skin && a->head_model && a->head_skin &&
        !strcmp(a->model, b->model) && !strcmp(a->skin, b->skin) &&
        !strcmp(a->head_model, b->head_model) && !strcmp(a->head_skin, b->head_skin);
}

bool application_character_names_retain(qa_strings *strings,
    const qa_application_character_declaration *declaration, application_character_names *names,
    qa_native_q3_character_selection *selection, qa_error *error)
{
    qa_strings_retain(strings); names->strings = strings;
    if (!qa_strings_intern_cstr(strings, declaration->definition, &names->definition, error) ||
        !qa_strings_intern_cstr(strings, declaration->appearance.model, &names->model, error) ||
        !qa_strings_intern_cstr(strings, declaration->appearance.skin, &names->skin, error) ||
        !qa_strings_intern_cstr(strings, declaration->appearance.head_model, &names->head_model, error) ||
        !qa_strings_intern_cstr(strings, declaration->appearance.head_skin, &names->head_skin, error)) return false;
    selection->definition = qa_strings_cstr(strings, names->definition);
    selection->model = qa_strings_cstr(strings, names->model);
    selection->skin = qa_strings_cstr(strings, names->skin);
    selection->head_model = qa_strings_cstr(strings, names->head_model);
    selection->head_skin = qa_strings_cstr(strings, names->head_skin);
    return true;
}
static qa_string_id name_id(const qa_strings *strings, const char *text)
{ return qa_strings_find(strings, (qa_bytes){(const uint8_t *)text, strlen(text)}); }
bool application_character_names_match(const application_character_names *names,
    const qa_application_character_declaration *declaration)
{
    return name_id(names->strings, declaration->definition) == names->definition &&
        name_id(names->strings, declaration->appearance.model) == names->model &&
        name_id(names->strings, declaration->appearance.skin) == names->skin &&
        name_id(names->strings, declaration->appearance.head_model) == names->head_model &&
        name_id(names->strings, declaration->appearance.head_skin) == names->head_skin;
}
void application_character_names_release(application_character_names *names)
{ qa_strings_destroy(names->strings); }

static bool selection_current(void *context, const qa_native_q3_character_selection *selection)
{
    character_selection_owner *owner = context;
    qa_application *app = owner ? owner->application : NULL;
    qa_actor_id actor;
    qa_application_character_declaration declaration;
    bool found;
    if (!app || !selection || app->destroy_requested || app->routing_snapshot ||
        app->state == QA_APPLICATION_FAULTED || app->publication_generation != owner->selection.publication_generation ||
        !qa_application_player_actor(app, owner->seat, &actor)) return false;
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(app));
    if (choices != owner->choices || !qa_actor_id_equal(actor, owner->actor)) {
        if (!published(app, owner->seat, actor, &declaration, &found, NULL) || !found ||
            name_id(owner->names.strings, declaration.provider->instance) != owner->selection.owner ||
            !application_character_names_match(&owner->names, &declaration)) return false;
    }
    bool valid = provider && provider->application == app && provider->constructed && provider->attached &&
        !provider->close_pending && provider->owner == owner->selection.owner && provider->launch &&
        provider->launch->storage == owner->selection.launch->storage && provider->launch->content == owner->selection.content &&
        provider->launch->selection.product == owner->selection.product &&
        selection->owner == owner->selection.owner && selection->product == owner->selection.product &&
        selection->publication_generation == owner->selection.publication_generation &&
        selection->launch == owner->selection.launch && selection->content == owner->selection.content &&
        selection->definition == owner->selection.definition && selection->model == owner->selection.model &&
        selection->skin == owner->selection.skin && selection->head_model == owner->selection.head_model &&
        selection->head_skin == owner->selection.head_skin && selection->lifetime == owner &&
        selection->current == selection_current;
    if (valid) {
        owner->actor = actor;
        owner->choices = choices;
    }
    return valid;
}

static void selection_release(void *context)
{
    character_selection_owner *owner = context;
    if (!owner) return;
    qa_launch_instance_lease_release(owner->metadata);
    application_character_names_release(&owner->names); free(owner);
}

bool qa_native_q3_character_selection_create(qa_application *app, uint32_t seat,
    const qa_native_q3_character_declaration *constructor, qa_native_q3_character_selection *out, qa_error *error)
{
    qa_actor_id actor;
    qa_application_character_declaration declaration;
    bool found;
    if (!app || !out || app->destroy_requested || app->routing_snapshot || app->state == QA_APPLICATION_FAULTED ||
        !qa_application_player_actor(app, seat, &actor) ||
        !published(app, seat, actor, &declaration, &found, error) || !found ||
        !same_declaration(constructor, &declaration.appearance))
        return application_fail(error, QA_ERROR_ARGUMENT, "Character constructor does not match the actual selected seat declaration");
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->launch || !provider->launch->content ||
        strcmp(declaration.provider->instance, provider->launch->selection.instance))
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration does not name its actual live provider");
    character_selection_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining selected CHARACTER declaration");
    owner->application = app; owner->seat = seat; owner->actor = actor;
    owner->choices = qa_launch_snapshot_choices(qa_application_launch(app));
    if (!application_character_names_retain(qa_session_strings(app->session), &declaration,
            &owner->names, &owner->selection, error) ||
        !qa_launch_instance_retain_metadata(provider->launch, &owner->metadata, error)) {
        selection_release(owner);
        if (!error || error->code == QA_OK) application_fail(error, QA_ERROR_MEMORY, "Copying actual CHARACTER declaration");
        return false;
    }
    owner->selection.owner = provider->owner; owner->selection.product = provider->launch->selection.product;
    owner->selection.publication_generation = app->publication_generation;
    owner->selection.launch = qa_launch_instance_lease_view(owner->metadata);
    owner->selection.content = provider->launch->content; owner->selection.lifetime = owner;
    owner->selection.current = selection_current; owner->selection.release = selection_release;
    if (!selection_current(owner, &owner->selection)) {
        selection_release(owner);
        return application_fail(error, QA_ERROR_ARGUMENT, "Character declaration changed during constructor capture");
    }
    *out = owner->selection;
    return true;
}

bool qa_application_character_selection_read(qa_application *app, uint32_t seat,
    qa_native_q3_character_selection *out, bool *found, qa_error *error)
{
    if (!app || !out || !found || app->destroy_requested || !qa_application_launch(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Character selection requires its actual published application");
    *found = false;
    qa_actor_id actor;
    if (!qa_application_player_actor(app, seat, &actor)) return true;
    qa_application_character_declaration declaration;
    bool declared_seat;
    if (!published(app, seat, actor, &declaration, &declared_seat, error)) return false;
    if (!declared_seat) return true;
    if (!qa_native_q3_character_selection_create(app, seat, &declaration.appearance, out, error)) return false;
    *found = true;
    return true;
}
