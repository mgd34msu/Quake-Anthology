#include "qa/application_native_q2_presentation.h"
#include "qa/q2_sound.h"
#include "ui_features_private.h"
#include "accessibility.h"
#include "source_restore.h"
#include "campaign_menu.h"
#include "qa/q1_text.h"
#include "cinematic_captions.h"
#include "qa/text.h"
#include "qa/ui_presentation_prepare.h"
#include "qa/application_q1_composition.h"
#include "qa/game_q1_source_obituary.h"
#include "music_sources.h"
#include <limits.h>
#include <math.h>

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
    owner->frontend = f; owner->menu_sound_family = QA_GAME_Q1; f->ui_features = owner;
    static const char *const graph_names[FRONTEND_GRAPH_CVARS] = {
        "debuggraph", "timegraph", "netgraph", "graphheight", "graphscale", "graphshift"
    };
    for (size_t i = 0; i < FRONTEND_GRAPH_CVARS; ++i)
        owner->graph_cvars[i] = qa_cvars_resolve(qa_application_cvars(f->application), graph_names[i]);
    owner->catalogs = qa_localization_pool_create(error);
    if (!owner->catalogs) return false;
    if (f->options.dedicated) return true;
    owner->tracks = qa_caption_library_create(error);
    if (!owner->tracks || !qa_audio_bank_create(f->ui_mounts, qa_session_strings(qa_application_session(f->application)), &owner->sounds, error)) return false;
    for (unsigned i = 0; i < QA_INPUT_LOCAL_SEATS; ++i) {
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
        qa_localization_release(owner->seats[i].localization);
        qa_save_slot_listing_free(&owner->seats[i].saves);
        free(owner->seats[i].save_qualification);
        free(owner->seats[i].save_rows); free(owner->seats[i].save_details); free(owner->seats[i].save_name);
        free(owner->seats[i].campaign_rows); free(owner->seats[i].campaign_labels);
        free(owner->seats[i].campaign_instance); free(owner->seats[i].shown_instance);
    }
    qa_audio_bank_destroy(owner->sounds);
    qa_caption_library_destroy(owner->tracks); qa_localization_pool_destroy(owner->catalogs);
    free(owner->fallbacks); free(owner); f->ui_features = NULL; return true;
}
/* Quake II cl_scrn.c: SCR_DebugGraph, SCR_DrawDebugGraph, CL_AddNetgraph. */
static float graph_value(const frontend_ui_features *owner, unsigned setting, float fallback)
{
    const qa_cvar_view *value = qa_cvars_read(qa_application_cvars(owner->frontend->application),
        owner->graph_cvars[setting]);
    return value ? value->number : fallback;
}
void frontend_ui_graph_sample(qa_frontend *f, float value, int32_t color)
{
    frontend_ui_features *owner = f->ui_features;
    if (!owner) return;
    owner->graph_samples[owner->graph_next++ & (FRONTEND_GRAPH_SAMPLES - 1)] =
        (frontend_graph_sample){value, color};
}
void frontend_ui_graph_frame(qa_frontend *f, uint64_t elapsed_ns)
{
    frontend_ui_features *owner = f->ui_features;
    if (owner && graph_value(owner, FRONTEND_GRAPH_TIME, 0) != 0)
        frontend_ui_graph_sample(f, (float)((double)elapsed_ns / 1000000000.0) * 300.0f, 0);
}
static void graph_repeat(frontend_ui_features *owner, uint32_t count, int32_t color)
{
    if (count >= FRONTEND_GRAPH_SAMPLES) {
        for (size_t i = 0; i < FRONTEND_GRAPH_SAMPLES; ++i)
            owner->graph_samples[i] = (frontend_graph_sample){30, color};
        owner->graph_next += count;
    } else {
        for (uint32_t i = 0; i < count; ++i)
            frontend_ui_graph_sample(owner->frontend, 30, color);
    }
}
void frontend_ui_graph_network(qa_frontend *f, uint32_t dropped, uint32_t suppressed,
    bool has_ping, double ping_ms)
{
    frontend_ui_features *owner = f->ui_features;
    if (!owner || graph_value(owner, FRONTEND_GRAPH_DEBUG, 0) != 0 ||
        graph_value(owner, FRONTEND_GRAPH_TIME, 0) != 0) return;
    graph_repeat(owner, dropped, 0x40);
    graph_repeat(owner, suppressed, 0xdf);
    if (has_ping)
        frontend_ui_graph_sample(f, (float)fmin(30.0, trunc(ping_ms / 30.0)), 0xd0);
}
static qa_vec4 graph_color(qa_bytes palette, int32_t index)
{
    size_t color = (uint32_t)index & 255u;
    if (palette.size >= 768) {
        const uint8_t *rgb = palette.data + color * 3;
        return (qa_vec4){rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1};
    }
    float gray = (float)color / 255.0f;
    return (qa_vec4){gray, gray, gray, 1};
}
bool frontend_ui_graph_draw(qa_frontend *f, uint32_t physical_seat, qa_scene_frame *frame,
    qa_scene_rect viewport, qa_bytes palette, qa_error *error)
{
    frontend_ui_features *owner = f->ui_features;
    if (!owner || !viewport.width || !viewport.height) return true;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    /* netgraph aliases cg_lagometer, whose Q3 presentation is owned by CGAME. */
    bool network_graph = qa_input_seat_recipient_read(f->seats[physical_seat].input,
        &console, &cvars, &command) && qa_cvars_dialect(cvars) != QA_RULESET_Q3 &&
        graph_value(owner, FRONTEND_GRAPH_NETWORK, 0) != 0;
    if (graph_value(owner, FRONTEND_GRAPH_DEBUG, 0) == 0 &&
        graph_value(owner, FRONTEND_GRAPH_TIME, 0) == 0 && !network_graph) return true;
    float raw_height = graph_value(owner, FRONTEND_GRAPH_HEIGHT, 32);
    if (!isfinite(raw_height) || raw_height < 1 || (double)raw_height > INT32_MAX) return true;
    int32_t height = (int32_t)raw_height;
    int32_t bottom = viewport.y + (int32_t)viewport.height;
    const qa_scene_image *white = qa_scene_white(f->ui_images);
    if (!qa_scene_frame_picture(frame, white, viewport,
            (qa_scene_rect){viewport.x, bottom - height, viewport.width, (uint32_t)height},
            (qa_vec4){0, 0, 1, 1}, graph_color(palette, 8), error)) return false;
    float scale = graph_value(owner, FRONTEND_GRAPH_SCALE, 1);
    float shift = graph_value(owner, FRONTEND_GRAPH_SHIFT, 0);
    for (uint32_t a = 0; a < viewport.width; ++a) {
        const frontend_graph_sample *sample = owner->graph_samples +
            ((owner->graph_next - 1 - a) & (FRONTEND_GRAPH_SAMPLES - 1));
        float value = sample->value * scale + shift;
        if (value < 0) value += raw_height * (1 + truncf(-value / raw_height));
        if (!isfinite(value)) continue;
        int32_t bar = (int32_t)fmod((double)truncf(value), height);
        if (bar <= 0) continue;
        qa_scene_rect column = {viewport.x + (int32_t)(viewport.width - 1 - a),
            bottom - bar, 1, (uint32_t)bar};
        if (!qa_scene_frame_picture(frame, white, viewport, column,
                (qa_vec4){0, 0, 1, 1}, graph_color(palette, sample->color), error)) return false;
    }
    return true;
}
static bool sound_bank(frontend_seat *seat, qa_audio_bank **out,
    qa_game_family *family, qa_error *error)
{
    qa_frontend *f = seat->frontend;
    *out = f->ui_features->sounds;
    *family = f->ui_features->menu_sound_family;
    qa_command_context command;
    qa_application_client_source source;
    qa_console *console;
    qa_cvars *cvars;
    if (seat->input && qa_input_seat_recipient_read(seat->input, &console, &cvars, &command) &&
        command.owner && command.owner <= UINT32_MAX &&
        qa_application_client_physical_read(f->application, (qa_actor_owner)command.owner,
            command.seat, &source, NULL)) {
        *family = frontend_music_family(qa_launch_instance_catalog(source.descriptor),
            source.descriptor->selection.product);
        return frontend_event_sounds(f, source.context.receiver, *family, out, error);
    }
    return true;
}
static bool local_sound(frontend_seat *seat, qa_audio_bank *bank,
    qa_game_family family, const char *name, bool *found, qa_error *error)
{
    qa_frontend *f = seat->frontend;
    qa_audio_asset *asset = NULL;
    if (!qa_audio_bank_register(bank, name, family, &asset, error)) return false;
    if (found) *found = asset != NULL;
    if (!asset) return true;
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .name = qa_audio_asset_name(asset), .family = family, .actor = QA_AUDIO_NO_ACTOR,
        .owner = QA_AUDIO_NO_OWNER, .audience = seat->id, .origin_kind = QA_AUDIO_LOCAL,
        .channel = family == QA_GAME_Q1 ? -1 : family == QA_GAME_Q3 ? 6 : 0,
        .volume = 1, .attenuation = 0};
    bool okay = frontend_audio_event(f, &(qa_audio_event){.kind = QA_AUDIO_EVENT_PLAY,
        .sound = sound, .milliseconds = (int32_t)((f->time_ns / 1000000) & INT32_MAX)}, error);
    qa_audio_asset_release(asset);
    return okay;
}
void frontend_ui_sound(void *context, uint32_t physical, qa_ui_sound event)
{
    frontend_seat *seat = context;
    qa_frontend *f = seat ? seat->frontend : NULL;
    frontend_ui_seat_features *features = frontend_ui_features_seat(seat);
    if (!features || physical != seat->id || !f->audio || !f->ui_features->sounds ||
        f->ui_features->audio_error.code != QA_OK) return;
    qa_audio_bank *bank;
    qa_game_family family;
    qa_error error = {0};
    if (!sound_bank(seat, &bank, &family, &error)) return;
    const char *name;
    if (family == QA_GAME_Q1)
        name = event == QA_UI_OPEN ? "misc/menu2.wav" :
            event == QA_UI_MOVE ? "misc/menu1.wav" : "misc/menu3.wav";
    else
        name = event == QA_UI_OPEN ? "misc/menu1.wav" :
            event == QA_UI_CLOSE ? "misc/menu3.wav" :
            family == QA_GAME_Q3 && event == QA_UI_REJECT ? "misc/menu4.wav" : "misc/menu2.wav";
    (void)local_sound(seat, bank, family, name, NULL, &error);
}
bool frontend_sound_command(qa_frontend *f, const qa_command_invocation *call,
    bool *handled, qa_error *error)
{
    const char *name = call->argv[0];
    bool info = qa_text_ascii_equal(name, "soundinfo") || qa_text_ascii_equal(name, "s_info");
    bool list = qa_text_ascii_equal(name, "soundlist") || qa_text_ascii_equal(name, "s_list");
    bool stop = qa_text_ascii_equal(name, "stopsound") || qa_text_ascii_equal(name, "s_stop");
    bool play = qa_text_ascii_equal(name, "play");
    *handled = info || list || stop || play;
    if (!*handled) return true;
    if (info) {
        char text[512];
        if (!f->device) {
            frontend_console_print(f, &call->context, "Sound output is unavailable.\n");
            return true;
        }
        qa_audio_device_options options = qa_audio_device_configuration(f->device);
        const char *device = qa_audio_device_name(f->device);
        snprintf(text, sizeof(text), "Sound device: %s\nRate: %u Hz\nChannels: %u\nSample bits: %u\nBuffer: %u frames\n",
            device ? device : "default", options.format.sample_rate, options.format.channels,
            options.format.sample_bits, options.buffer_frames);
        frontend_console_print(f, &call->context, text);
        return true;
    }
    if (stop) {
        return !f->audio || frontend_audio_event(f,
            &(qa_audio_event){.kind = QA_AUDIO_EVENT_STOP_SOUNDS}, error);
    }
    if (play && call->argc < 2) {
        frontend_console_print(f, &call->context, "usage: play <sound> [...]\n");
        return true;
    }
    uint32_t physical;
    if (!f->audio || !f->ui_features || !frontend_command_seat_read(f, &call->context, &physical)) {
        frontend_console_print(f, &call->context, "No local sound listener.\n");
        return true;
    }
    frontend_seat *seat = f->seats + physical;
    qa_audio_bank *bank;
    qa_game_family family;
    if (!sound_bank(seat, &bank, &family, error)) return false;
    if (list) {
        unsigned long long total = 0;
        for (size_t i = 0; i < qa_audio_bank_record_count(bank); ++i) {
            qa_audio_asset *asset = qa_audio_bank_record_at(bank, i);
            const qa_audio_sample *sample = qa_audio_asset_sample(asset);
            if (!sample) continue;
            unsigned long long bytes = sample->frame_count * sample->channels * sizeof(*sample->samples);
            total += bytes;
            char text[1200];
            snprintf(text, sizeof(text), "%c %u Hz %u channels %llu frames %llu bytes %s\n",
                sample->loop_start == UINT64_MAX ? ' ' : 'L', sample->sample_rate, sample->channels,
                (unsigned long long)sample->frame_count, bytes, qa_audio_asset_name(asset));
            frontend_console_print(f, &call->context, text);
        }
        char text[80];
        snprintf(text, sizeof(text), "Total resident PCM: %llu bytes\n", total);
        frontend_console_print(f, &call->context, text);
        return true;
    }
    for (size_t i = 1; i < call->argc; ++i) {
        const char *path = call->argv[i];
        const char *slash = strrchr(path, '/');
        if (!strchr(slash ? slash + 1 : path, '.')) {
            size_t length = strlen(path);
            char *extended = qa_arena_alloc(&f->frame.storage, length + sizeof(".wav"), 1, error);
            if (!extended) return false;
            memcpy(extended, path, length);
            memcpy(extended + length, ".wav", sizeof(".wav"));
            path = extended;
        }
        bool found;
        if (!local_sound(seat, bank, family, path, &found, error)) return false;
        if (!found) {
            frontend_console_print(f, &call->context, "Couldn't load sound ");
            frontend_console_print(f, &call->context, path);
            frontend_console_print(f, &call->context, "\n");
        }
    }
    return true;
}
bool frontend_ui_audio_prepare_asset(qa_frontend *f, qa_audio_asset *asset, qa_error *error)
{
    if (!f->ui_features || f->options.dedicated) return true;
    for (unsigned i = 0; i < f->options.seats; ++i) {
        qa_ui_preferences preferences;
        if (!qa_ui_preferences_read(qa_application_cvars(f->application),
                qa_application_ui_preference_handles(f->application), i, &preferences, error) ||
            !qa_sound_captions_prepare_asset(f->ui_features->seats[i].captions, asset, preferences.language, error)) return false;
    }
    return true;
}
bool frontend_ui_audio_prepare_sound(qa_frontend *f, qa_audio_bank *bank,
    const char *name, qa_game_family family, qa_error *error)
{
    if (!f->audio) return true;
    qa_audio_asset *asset = NULL;
    qa_audio_sample *sample = NULL;
    bool okay = name[0] == '*' && family == QA_GAME_Q2 ?
        qa_audio_bank_sexed(bank, name, "", &asset, error) :
        qa_audio_bank_register(bank, name, family, &asset, error);
    if (okay && asset) okay = qa_audio_asset_resample(asset, qa_audio_engine_rate(f->audio), family, &sample, error) &&
        frontend_ui_audio_prepare_asset(f, asset, error);
    qa_audio_sample_release(sample);
    qa_audio_asset_release(asset);
    return okay;
}
typedef struct source_audio_preparation {
    qa_frontend *frontend;
    qa_audio_bank *bank;
    size_t sexed_count;
} source_audio_preparation;
static bool prepare_source_sound(void *context, const char *path, qa_error *error)
{
    source_audio_preparation *preparation = context;
    if (*path == '*' && !qa_audio_bank_sexed_reserve(preparation->bank,
        ++preparation->sexed_count * 257, error)) return false;
    return frontend_ui_audio_prepare_sound(preparation->frontend, preparation->bank, path, QA_GAME_Q2, error);
}
bool frontend_ui_audio_prepare_q2_source(qa_frontend *f, qa_audio_bank *bank, qa_actor_owner owner, qa_error *error)
{
    size_t sexed_count = 0;
    for (size_t i = 0; i < qa_q2_sound_count(); ++i) sexed_count += *qa_q2_sound_path(i) == '*';
    if (!qa_audio_bank_sexed_reserve(bank, sexed_count * 257, error)) return false;
    for (size_t i = 0; i < qa_q2_sound_count(); ++i)
        if (!frontend_ui_audio_prepare_sound(f, bank, qa_q2_sound_path(i), QA_GAME_Q2, error)) return false;
    source_audio_preparation preparation = {f, bank, sexed_count};
    return !owner || qa_application_native_q2_sound_resources(f->application, owner, prepare_source_sound, &preparation, error);
}
void frontend_ui_audio_event(void *context, const qa_audio_voice_event *event)
{
    qa_frontend *f = context; frontend_ui_features *owner = f ? f->ui_features : NULL;
    if (!owner || !event || event->seat >= f->options.seats || f->options.dedicated || owner->audio_error.code != QA_OK) return;
    owner->handling = true;
    (void)qa_sound_captions_event(owner->seats[event->seat].captions, event, &owner->audio_error);
    if (event->started && event->asset && f->input) {
        const qa_vfs *files = qa_audio_asset_files(event->asset);
        const char *name = qa_audio_asset_name(event->asset);
        qa_error tactile_error = {0};
        if (files && name && !qa_input_platform_tactile(f->input, event->seat,
                (qa_vfs *)files, name, (double)(f->wall_time_ns / 1000000), &tactile_error)) {
            frontend_print(f, "Controller vibration: ");
            frontend_print(f, tactile_error.message);
            frontend_print(f, "\n");
        }
    }
    owner->handling = false;
}
bool frontend_ui_features_sync(qa_frontend *f, qa_error *error)
{
    if (!f || !f->ui_features || !frontend_ui_features_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "UI feature preparation requires its idle actual owner");
    if (f->options.dedicated) return true;
    frontend_ui_features *owner = f->ui_features;
    if (owner->audio_error.code != QA_OK) { if (error) *error = owner->audio_error; return false; }
    if (!frontend_accessibility_sync(f, error)) return false;
    uint64_t frame = f->audio ? qa_audio_engine_clock(f->audio) : 0;
    if (frame > INT64_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "Caption delivery clock exceeds source range");
    const qa_cvars *cvars=qa_application_cvars(f->application);
    const qa_ui_preference_handles *handles=qa_application_ui_preference_handles(f->application);
    uint64_t identity=qa_cvars_view_identity(cvars);
    for (unsigned i = 0; !f->options.dedicated && i < f->options.seats; ++i) {
        qa_ui_preferences preferences;
        if (!qa_ui_preferences_read(cvars,handles, i, &preferences, error) ||
            !qa_sound_captions_prepare(owner->seats[i].captions, preferences.language, (int64_t)frame, error)) return false;
        frontend_ui_seat_features *seat=owner->seats+i;
        qa_cvar_handle language_handle=handles->seats[i][QA_UI_PREF_LANGUAGE];
        const qa_cvar_view *language=qa_cvars_read(cvars,language_handle);
        if (!seat->localization || seat->language_identity!=identity ||
            seat->language_handle.slot!=language_handle.slot || seat->language_revision!=language->modification_count) {
            qa_localization *catalog=NULL;
            if (!qa_localization_acquire(owner->catalogs,f->ui_mounts,preferences.language,
                &(qa_localization_options){.profile=owner->ui_profile},&catalog,error)) return false;
            qa_localization_release(seat->localization);
            seat->language_handle=language_handle; seat->language_identity=identity;
            seat->language_revision=language->modification_count; seat->localization=catalog;
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
bool frontend_ui_source_text(qa_frontend *f,uint32_t seat,qa_game_family family,
    qa_vfs *view,qa_product_edition edition,qa_strings *strings,const char *format,
    const qa_builtin_message_arg *args,size_t count,qa_arena *storage,qa_buffer *out,qa_error *error)
{
    if (count>SIZE_MAX/(sizeof(const char *)+32))
        return frontend_fail(error,QA_ERROR_MEMORY,"Localized source argument count overflows");
    const char **arguments=count?qa_arena_alloc(storage,
        count*(sizeof(*arguments)+32),_Alignof(const char *),error):NULL;
    if (count && !arguments) return false;
    char *numbers=arguments?(char *)(arguments+count):NULL;
    for (size_t i=0;i<count;++i) {
        switch (args[i].kind) {
        case QA_BUILTIN_MESSAGE_NUMBER:
            arguments[i]=numbers+i*32;
            if (!qa_format_number(args[i].value.number,numbers+i*32,error)) return false;
            break;
        case QA_BUILTIN_MESSAGE_STRING:
            arguments[i]=qa_strings_cstr(strings,args[i].value.text);
            break;
        case QA_BUILTIN_MESSAGE_TEXT:
            arguments[i]=args[i].value.literal;
            break;
        default: arguments[i]=NULL; break;
        }
        if (!arguments[i])
            return frontend_fail(error,QA_ERROR_FORMAT,"Localized source argument has no genuine string value");
    }
    qa_localization *catalog=NULL;
    bool ok=true;
    if (view && (family==QA_GAME_Q1 || edition==QA_EDITION_RERELEASE || format[0]=='$')) {
        qa_ui_preferences preferences;
        ok=qa_ui_preferences_read(qa_application_cvars(f->application),
            qa_application_ui_preference_handles(f->application),seat,&preferences,error) &&
            qa_localization_acquire(f->ui_features->catalogs,view,preferences.language,
                &(qa_localization_options){.profile=family==QA_GAME_Q1 ?
                    QA_LOCALIZATION_Q1_RERELEASE : QA_LOCALIZATION_Q2_RERELEASE},&catalog,error);
    }
    if (ok && family==QA_GAME_Q1 && edition!=QA_EDITION_RERELEASE &&
        (format[0]!='$' || !qa_localization_find(catalog,format+1))) {
        char *text=qa_arena_alloc(storage,1024,1,error);
        ok=text && qa_q1_classic_text(format,arguments,count,storage,text,1024,error);
        if (ok) *out=(qa_buffer){(uint8_t *)text,strlen(text)};
    } else if (ok) ok=qa_localize_presentation_arena(catalog,format,arguments,count,true,storage,out,error);
    qa_localization_release(catalog);
    return ok;
}
frontend_q2_print_policy frontend_ui_q2_print_policy(bool legacy_center,uint32_t flags,
    bool rerelease,const qa_cvars *cvars,const qa_hud_cvar_handles *handles)
{
    uint32_t type=flags&7u;
    const qa_cvar_view *time=qa_cvars_read(cvars,handles->center_time);
    const qa_cvar_view *speed=qa_cvars_read(cvars,handles->print_speed);
    double seconds=time && isfinite(time->number)?fmax(0,time->number):
        rerelease?5:2.5;
    double interval=0.04;
    if (speed && isfinite(speed->number)) {
        bool seconds_view=qa_cvars_role(cvars)==QA_CVAR_ROLE_CGAME &&
            qa_cvars_dialect(cvars)==QA_RULESET_Q2_RERELEASE;
        interval=seconds_view?fmax(0,speed->number):speed->number>0?1/speed->number:0;
    }
    long double duration_ns=(long double)seconds*1e9L;
    long double character_ns=(long double)interval*1e9L;
    return (frontend_q2_print_policy){.center=legacy_center || type==4 || type==5,
        .chat=type==3,.notify=(flags&16u)==0,
        .duration_ns=duration_ns>=(long double)UINT64_MAX?UINT64_MAX:(uint64_t)duration_ns,
        .center_policy={.instant=type!=4,
            .character_ns=character_ns>=(long double)UINT64_MAX?UINT64_MAX:(uint64_t)character_ns}};
}
bool frontend_ui_console_text(qa_arena *storage,const char *text,bool notify,
    const char **out,qa_error *error)
{
    *out=text;
    if (notify) return true;
    size_t length=strlen(text);
    if (length>SIZE_MAX-13)
        return frontend_fail(error,QA_ERROR_MEMORY,"Console print extent overflows");
    char *copy=qa_arena_alloc(storage,length+13,1,error);
    if (!copy) return false;
    memcpy(copy,"[skipnotify]",12);memcpy(copy+12,text,length+1);
    *out=copy;return true;
}
bool frontend_ui_source_message(qa_frontend *f,uint32_t seat,const qa_builtin_event *event,
    const char **out,qa_error *error)
{
    if (!f || !f->application || !f->ui_features || !event || !out || seat>=f->options.seats ||
        f->source_restoring || f->ui_features->handling)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Localized source message requires its actual recipient boundary");
    qa_strings *strings=qa_session_strings(qa_application_session(f->application));
    const char *format=event->message_format?event->message_format:qa_strings_cstr(strings,event->text);
    if (!format) return frontend_fail(error,QA_ERROR_FORMAT,"Source message has no actual format string");
    *out=format;
    if ((event->family==QA_GAME_Q1 && (event->flags&QA_Q1_SOURCE_MESSAGE_LITERAL)) ||
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
    qa_vfs *view=NULL;qa_product_edition edition=QA_EDITION_CLASSIC;
    if (event->family==QA_GAME_Q1 || format[0]=='$') {
        view=qa_application_provider_files(f->application,event->provider);
        if (!view) return frontend_fail(error,QA_ERROR_NOT_FOUND,"Localized source message lost its actual content view");
        const qa_launch_snapshot *publication=qa_application_launch(f->application);
        const char *instance=qa_application_provider_instance(f->application,event->provider);
        const qa_launch_instance *source=instance?qa_launch_snapshot_find(publication,instance):NULL;
        const qa_product *product=source?qa_catalog_product(qa_launch_snapshot_catalog(publication),source->selection.product):NULL;
        if (!product) return frontend_fail(error,QA_ERROR_ARGUMENT,"Localized event lost its actual published source edition");
        edition=product->edition;
    }
    qa_buffer text;
    if (!frontend_ui_source_text(f,seat,event->family,view,edition,strings,format,
        event->arguments,event->argument_count,&f->frame.storage,&text,error)) return false;
    *out=(const char *)text.data;return true;
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
    qa_vfs *view=qa_application_provider_files(f->application,event->provider);
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const char *instance=qa_application_provider_instance(f->application,event->provider);
    const qa_launch_instance *source=instance?qa_launch_snapshot_find(publication,instance):NULL;
    const qa_product *product=source?qa_catalog_product(qa_launch_snapshot_catalog(publication),source->selection.product):NULL;
    if (!view || !product || product->family!=QA_GAME_Q1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source prompt lost its actual content view and edition");
    qa_ui_preferences preferences; qa_localization *catalog=NULL;
    if (!qa_ui_preferences_read(qa_application_cvars(f->application),qa_application_ui_preference_handles(f->application),seat,&preferences,error) ||
        !qa_localization_acquire(f->ui_features->catalogs,view,preferences.language,
            &(qa_localization_options){.profile=QA_LOCALIZATION_Q1_RERELEASE},&catalog,error)) return false;
    const char *format=qa_strings_cstr(strings,field);
    bool ok=true;
    if (product->edition!=QA_EDITION_RERELEASE &&
        (format[0]!='$' || !qa_localization_find(catalog,format+1)))
        ok=qa_q1_classic_text(format,NULL,0,&f->frame.storage,output,1024,error);
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
    if (!qa_ui_preferences_read(qa_application_cvars(seat->frontend->application),qa_application_ui_preference_handles(seat->frontend->application), seat->id, &preferences, error)) return false;
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
