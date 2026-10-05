#include "ui_features_private.h"
#include "accessibility.h"
#include "source_restore.h"
#include "campaign_menu.h"
#include "q1_text.h"
#include "cinematic_captions.h"
#include "qa/text.h"
#include "qa/ui_presentation_prepare.h"
#include "qa/application_q1_composition.h"
#include "qa/game_q1_source_obituary.h"
#include <limits.h>

frontend_ui_seat_features *frontend_ui_features_seat(frontend_seat *seat)
{
    qa_frontend *f = seat ? seat->frontend : NULL;
    return f && f->ui_features && f->ui_features->frontend == f && f->seats &&
        seat->id < f->options.seats && seat == &f->seats[seat->id] ? &f->ui_features->seats[seat->id] : NULL;
}
static bool sound_view(void *context, qa_audio_asset *asset, qa_vfs **out, qa_error *error)
{
    (void)context;
    const qa_vfs *files = qa_audio_asset_files(asset);
    if (!out || !files)
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Caption voice lost its actual source bank view");
    *out = (qa_vfs *)files;
    return true;
}
bool frontend_ui_features_prepare(qa_frontend *f, qa_error *error)
{
    if (!f || !f->application || f->ui_features || !f->options.seats || f->options.seats > QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI features require their empty actual frontend owner");
    frontend_ui_features *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual UI feature owner");
    owner->frontend = f; f->ui_features = owner;
    owner->tracks = qa_caption_library_create(error); owner->catalogs = qa_localization_pool_create(error);
    if (!owner->tracks || !owner->catalogs) return false;
    for (unsigned i = 0; !f->options.dedicated && i < QA_INPUT_LOCAL_SEATS; ++i) {
        qa_sound_caption_options options = {.captions = {.seat = i, .kind = QA_CAPTION_SOUND,
            .tracks = owner->tracks, .catalogs = owner->catalogs,
            .localization = {.profile = QA_LOCALIZATION_Q1_RERELEASE}}, .context = f, .content_view = sound_view};
        owner->seats[i].captions = qa_sound_captions_create(&options, error);
        if (!owner->seats[i].captions) return false;
    }
    return frontend_ui_cinematic_init(f,error);
}
bool frontend_ui_features_idle(const qa_frontend *f)
{
    if (!f) return false;
    const frontend_ui_features *owner=f->ui_features;
    if (!owner) return true;
    if (owner->frontend!=f || owner->handling || owner->shared_ui || !frontend_ui_cinematic_idle(f)) return false;
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i)
        if (owner->seats[i].captions && !qa_sound_captions_idle(owner->seats[i].captions)) return false;
    if (f->seats) for (unsigned i=0;i<f->options.seats;++i)
        if (f->seats[i].ui && !qa_ui_presentation_idle(f->seats[i].ui)) return false;
    return true;
}
bool frontend_ui_features_destroy(qa_frontend *f, qa_error *error)
{
    if (!f || !f->ui_features) return true;
    frontend_ui_features *owner = f->ui_features;
    if (!frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"UI features retain an active presentation or caption child");
    frontend_ui_cinematic_destroy(f);
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) {
        qa_sound_captions_destroy(owner->seats[i].captions);
        qa_localization_release(owner->seats[i].localization); free(owner->seats[i].language);
        qa_save_slot_listing_free(&owner->seats[i].saves);
        for (size_t j=0;j<owner->seats[i].save_product_count;++j) {
            free(owner->seats[i].save_product_keys?owner->seats[i].save_product_keys[j]:NULL);
            free(owner->seats[i].save_product_labels?owner->seats[i].save_product_labels[j]:NULL);
        }
        free(owner->seats[i].save_product_keys); free(owner->seats[i].save_product_labels);
        free(owner->seats[i].save_qualification);
        free(owner->seats[i].save_rows); free(owner->seats[i].save_details); free(owner->seats[i].save_name);
        free(owner->seats[i].campaign_rows); free(owner->seats[i].campaign_labels);
        free(owner->seats[i].campaign_instance); free(owner->seats[i].shown_instance);
    }
    qa_caption_library_destroy(owner->tracks); qa_localization_pool_destroy(owner->catalogs);
    free(owner->fallbacks); free(owner); f->ui_features = NULL; return true;
}
void frontend_ui_audio_event(void *context, const qa_audio_voice_event *event)
{
    qa_frontend *f = context; frontend_ui_features *owner = f ? f->ui_features : NULL;
    if (!owner || !event || event->seat >= f->options.seats || f->options.dedicated || owner->audio_error.code != QA_OK) return;
    owner->handling = true;
    (void)qa_sound_captions_event(owner->seats[event->seat].captions, event, &owner->audio_error);
    owner->handling = false;
}
bool frontend_ui_features_sync(qa_frontend *f, qa_error *error)
{
    if (!f || !f->ui_features || !frontend_ui_features_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI feature preparation requires its idle actual owner");
    frontend_ui_features *owner = f->ui_features;
    if (owner->audio_error.code != QA_OK) { if (error) *error = owner->audio_error; return false; }
    if (!frontend_accessibility_sync(f, error)) return false;
    uint64_t frame = f->audio ? qa_audio_engine_clock(f->audio) : 0;
    if (frame > INT64_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Caption delivery clock exceeds source range");
    for (unsigned i = 0; !f->options.dedicated && i < f->options.seats; ++i) {
        qa_ui_preferences preferences;
        if (!qa_ui_preferences_read(qa_application_cvars(f->application), i, &preferences, error) ||
            !qa_sound_captions_prepare(owner->seats[i].captions, preferences.language, (int64_t)frame, error)) return false;
        frontend_ui_seat_features *seat=owner->seats+i;
        if (!seat->language || strcmp(seat->language,preferences.language)) {
            qa_localization *catalog=NULL;
            if (!qa_localization_acquire(owner->catalogs,f->ui_mounts,preferences.language,
                &(qa_localization_options){.profile=owner->ui_profile},&catalog,error)) return false;
            size_t size=strlen(preferences.language)+1; char *language=malloc(size);
            if (!language) { qa_localization_release(catalog); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual seat UI language"); }
            memcpy(language,preferences.language,size); free(seat->language); qa_localization_release(seat->localization);
            seat->language=language; seat->localization=catalog;
        }
    }
    return frontend_campaign_menu_sync(f,error);
}
const char *frontend_ui_localize(void *context,const char *text)
{
    frontend_ui_seat_features *seat=frontend_ui_features_seat(context);
    if (!seat || !seat->localization || !text) return text;
    (void)qa_localize_presentation(seat->localization,text,NULL,0,false,seat->localized,sizeof(seat->localized));
    return seat->localized;
}
bool frontend_ui_source_message(qa_frontend *f,uint32_t seat,const qa_builtin_event *event,
    char output[1024],const char **out,qa_error *error)
{
    if (!f || !f->application || !f->ui_features || !event || !output || !out || seat>=f->options.seats ||
        f->source_restoring || f->ui_features->handling)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Localized source message requires its actual recipient boundary");
    qa_strings *strings=qa_session_strings(qa_application_session(f->application));
    const char *format=qa_strings_cstr(strings,event->text);
    if (!format) return frontend_fail(error,QA_ERROR_FORMAT,"Source message has no actual format string");
    *out=format;
    if (event->family!=QA_GAME_Q1 || (event->flags&QA_Q1_SOURCE_MESSAGE_LITERAL) ||
        (event->kind!=QA_BUILTIN_MESSAGE && event->kind!=QA_BUILTIN_CENTERPRINT) ||
        (!(event->flags&2u) && !event->argument_count && format[0]!='$')) return true;
    if (event->argument_count && !event->arguments)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Localized source message lost its actual argument tuple");
    if (event->actor.registry) {
        qa_actor_id actor; uint32_t launch_seat;
        if (!frontend_seat_launch_id_read(f,seat,&launch_seat) ||
            !qa_application_player_actor(f->application,launch_seat,&actor) || !qa_actor_id_equal(actor,event->actor))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Localized source message belongs to another recipient");
    }
    if (event->argument_count>SIZE_MAX/(sizeof(const char *)+32))
        return frontend_fail(error,QA_ERROR_MEMORY,"Localized source argument count overflows");
    const char **arguments=event->argument_count?malloc(event->argument_count*(sizeof(*arguments)+32)):NULL;
    if (event->argument_count && !arguments) return frontend_fail(error,QA_ERROR_MEMORY,"Reading genuine source localization arguments");
    char *numbers=arguments?(char *)(arguments+event->argument_count):NULL;
    bool ok=true;
    for (size_t i=0;ok && i<event->argument_count;++i) {
        if (event->arguments[i].kind==QA_BUILTIN_MESSAGE_NUMBER) {
            arguments[i]=numbers+i*32;
            ok=qa_format_number(event->arguments[i].value.number,numbers+i*32,error);
        } else if (event->arguments[i].kind!=QA_BUILTIN_MESSAGE_STRING ||
            !(arguments[i]=qa_strings_cstr(strings,event->arguments[i].value.text)))
            ok=frontend_fail(error,QA_ERROR_FORMAT,"Localized source argument has no genuine string value");
    }
    qa_command_context context={.owner=event->provider,.origin=QA_COMMAND_SERVER,.dialect=QA_CONSOLE_Q1},captured;
    if (ok) ok=qa_application_capture_command_context(f->application,&context,&captured,error);
    qa_vfs *view=ok?qa_application_context_files(f->application,&captured,NULL):NULL;
    if (ok && !view) ok=frontend_fail(error,QA_ERROR_NOT_FOUND,"Localized source message lost its actual content view");
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const char *instance=ok?qa_application_provider_instance(f->application,event->provider):NULL;
    const qa_launch_instance *source=instance?qa_launch_snapshot_find(publication,instance):NULL;
    const qa_product *product=source?qa_catalog_product(qa_launch_snapshot_catalog(publication),source->selection.product):NULL;
    if (ok && (!product || product->family!=QA_GAME_Q1))
        ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Localized Q1 event lost its actual published source edition");
    qa_ui_preferences preferences; qa_localization *catalog=NULL;
    if (ok) ok=qa_ui_preferences_read(qa_application_cvars(f->application),seat,&preferences,error) &&
        qa_localization_acquire(f->ui_features->catalogs,view,preferences.language,
            &(qa_localization_options){.profile=QA_LOCALIZATION_Q1_RERELEASE},&catalog,error);
    if (ok) {
        if (product->edition!=QA_EDITION_RERELEASE &&
            (format[0]!='$' || !qa_localization_find(catalog,format+1)))
            ok=frontend_q1_classic_text(format,arguments,event->argument_count,output,1024,error);
        else (void)qa_localize_presentation(catalog,format,arguments,event->argument_count,true,output,1024);
        if (ok) *out=output;
    }
    qa_localization_release(catalog); free(arguments); return ok;
}
bool frontend_ui_source_prompt_text(qa_frontend *f,uint32_t seat,const qa_builtin_event *event,
    qa_string_id field,char output[1024],const char **out,qa_error *error)
{
    if (!f || !f->application || !f->ui_features || !f->seats || !event || !output || !out ||
        seat>=f->options.seats || f->source_restoring || f->ui_features->handling ||
        !frontend_ui_features_seat(f->seats+seat) || event->kind!=QA_BUILTIN_SOURCE_PROMPT ||
        event->family!=QA_GAME_Q1 || !event->provider || !event->actor.registry || !field ||
        !event->text || (event->prompt_choice_count && !event->prompt_choices) ||
        event->prompt_choice_count>SIZE_MAX/sizeof(*event->prompt_choices))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Localized prompt requires its actual Source recipient and fields");
    qa_strings *strings=qa_session_strings(qa_application_session(f->application));
    bool selected=field==event->text;
    if (!qa_strings_cstr(strings,event->text))
        return frontend_fail(error,QA_ERROR_FORMAT,"Source prompt lost its actual title string");
    for (size_t i=0;i<event->prompt_choice_count;++i) {
        const qa_builtin_prompt_choice *choice=event->prompt_choices+i;
        if (!choice->label || !qa_strings_cstr(strings,choice->label) || choice->impulse<1 || choice->impulse>UINT8_MAX)
            return frontend_fail(error,QA_ERROR_FORMAT,"Source prompt lost an actual ordered choice");
        if (field==choice->label) selected=true;
    }
    if (!selected) return frontend_fail(error,QA_ERROR_ARGUMENT,"Text is not a field of this Source prompt");
    qa_actor_id actor; uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(f,seat,&launch_seat) ||
        !qa_application_player_actor(f->application,launch_seat,&actor) || !qa_actor_id_equal(actor,event->actor))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source prompt belongs to another full actor");
    uint64_t source_time=0; bool found=false;
    if (!qa_application_q1_ctf_recipient_read(f->application,event->provider,event->actor,&source_time,&found,error)) return false;
    if (!found) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source prompt lost its actual provider recipient");
    qa_command_context context={.owner=event->provider,.origin=QA_COMMAND_SERVER,.dialect=QA_CONSOLE_Q1},captured;
    if (!qa_application_capture_command_context(f->application,&context,&captured,error)) return false;
    qa_vfs *view=qa_application_context_files(f->application,&captured,NULL);
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const char *instance=qa_application_provider_instance(f->application,event->provider);
    const qa_launch_instance *source=instance?qa_launch_snapshot_find(publication,instance):NULL;
    const qa_product *product=source?qa_catalog_product(qa_launch_snapshot_catalog(publication),source->selection.product):NULL;
    if (!view || !product || product->family!=QA_GAME_Q1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source prompt lost its actual content view and edition");
    qa_ui_preferences preferences; qa_localization *catalog=NULL;
    if (!qa_ui_preferences_read(qa_application_cvars(f->application),seat,&preferences,error) ||
        !qa_localization_acquire(f->ui_features->catalogs,view,preferences.language,
            &(qa_localization_options){.profile=QA_LOCALIZATION_Q1_RERELEASE},&catalog,error)) return false;
    const char *format=qa_strings_cstr(strings,field);
    bool ok=true;
    if (product->edition!=QA_EDITION_RERELEASE &&
        (format[0]!='$' || !qa_localization_find(catalog,format+1)))
        ok=frontend_q1_classic_text(format,NULL,0,output,1024,error);
    else (void)qa_localize_presentation(catalog,format,NULL,0,true,output,1024);
    if (ok) *out=output;
    qa_localization_release(catalog);
    return ok;
}
bool frontend_ui_features_assets_read(const qa_frontend *f, qa_audio_asset ***out, size_t *count, qa_error *error)
{
    if (!f || !f->ui_features || !frontend_ui_features_idle(f) || !out || *out || !count || *count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI audio inventory requires its idle actual owners and empty outputs");
    qa_audio_asset **assets = NULL; size_t size = 0;
    for (unsigned i = 0; !f->options.dedicated && i < f->options.seats; ++i) {
        qa_audio_asset **part = NULL; size_t added = 0;
        if (!qa_sound_captions_assets_read(f->ui_features->seats[i].captions, &part, &added, error)) { free(assets); return false; }
        if (added > SIZE_MAX / sizeof(*assets) - size) { free(part); free(assets); return frontend_fail(error, QA_ERROR_MEMORY, "Caption audio inventory overflows"); }
        qa_audio_asset **grown = added ? realloc(assets, (size + added) * sizeof(*assets)) : assets;
        if (added && !grown) { free(part); free(assets); return frontend_fail(error, QA_ERROR_MEMORY, "Collecting actual caption audio holders"); }
        assets = grown; if (added) memcpy(assets + size, part, added * sizeof(*assets)); size += added; free(part);
    }
    *out = assets; *count = size; return true;
}
bool frontend_ui_features_content_visit(const qa_frontend *f,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!f || !f->ui_features || !frontend_ui_features_idle(f) || !visitor || !visitor->view)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI content inventory requires its idle actual owners");
    for (unsigned i = 0; !f->options.dedicated && i < f->options.seats; ++i)
        if (!qa_sound_captions_views_visit(f->ui_features->seats[i].captions, visitor->view, visitor->context, error)) return false;
    return frontend_ui_cinematic_content_visit(f,visitor,error);
}
void frontend_caption_count(void *context, const qa_active_caption *caption)
{ frontend_caption_collection *collection = context; (void)caption; ++collection->count; }
void frontend_caption_collect(void *context, const qa_active_caption *caption)
{
    frontend_caption_collection *collection = context;
    if (collection->failed) return;
    qa_active_caption value = *caption;
    size_t text_size = strlen(caption->text) + 1, speaker_size = caption->speaker ? strlen(caption->speaker) + 1 : 0;
    char *text = qa_arena_alloc(collection->arena, text_size, 1, collection->error);
    char *speaker = speaker_size ? qa_arena_alloc(collection->arena, speaker_size, 1, collection->error) : NULL;
    if (!text || (speaker_size && !speaker)) { collection->failed = true; return; }
    memcpy(text, caption->text, text_size); if (speaker_size) memcpy(speaker, caption->speaker, speaker_size);
    value.text = text; value.speaker = speaker; collection->values[collection->count++] = value;
}
bool frontend_ui_features_captions(frontend_seat *seat, const qa_active_caption **out, size_t *count, qa_error *error)
{
    frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    if (!state || !out || !count) return frontend_fail(error, QA_ERROR_ARGUMENT, "Caption presentation requires its actual seat owner");
    *out = NULL; *count = 0;
    qa_ui_preferences preferences;
    if (!qa_ui_preferences_read(qa_application_cvars(seat->frontend->application), seat->id, &preferences, error)) return false;
    if (!preferences.captions || !seat->frontend->audio) return true;
    uint64_t frame = qa_audio_engine_clock(seat->frontend->audio);
    if (frame > INT64_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Caption delivery clock exceeds source range");
    qa_caption_preferences enabled = {.sound_captions = true, .subtitles = true, .speakers = true};
    frontend_caption_collection collection = {.arena = &seat->frontend->frame.storage, .error = error};
    if (!qa_sound_captions_visit(state->captions, (int64_t)frame, enabled, frontend_caption_count, &collection, error)) return false;
    if (collection.count > SIZE_MAX / sizeof(qa_active_caption)) return frontend_fail(error, QA_ERROR_MEMORY, "Active captions exceed memory extent");
    collection.values = collection.count ? qa_arena_alloc(collection.arena, collection.count * sizeof(*collection.values), _Alignof(qa_active_caption), error) : NULL;
    if (collection.count && !collection.values) return false;
    collection.count = 0;
    if (!qa_sound_captions_visit(state->captions, (int64_t)frame, enabled, frontend_caption_collect, &collection, error) || collection.failed) return false;
    *out = collection.values; *count = collection.count; return true;
}
