#include "qa/media_captions.h"
#include "qa/media_captions_save.h"
#include "qa/media_caption_prepare.h"
#include "save_private.h"
#include "qa/pool.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_media_captions {
    qa_media_caption_options options;
    qa_captions *timeline;
    qa_vfs *view;
    char *source, *language, *platform;
    bool visiting;
};
typedef struct sound_catalog {
    struct sound_catalog *next;
    qa_audio_asset *asset;
    qa_media_captions *captions;
    qa_vfs *view;
} sound_catalog;
typedef struct caption_voice {
    struct caption_voice *next;
    size_t slot;
    uint64_t id;
    qa_audio_asset *asset;
    sound_catalog *catalog;
    int64_t start, stop;
    uint32_t rate;
    double offset;
    bool stopping;
} caption_voice;
struct qa_sound_captions {
    qa_sound_caption_options options;
    qa_arena voice_storage;
    qa_pool voice_records;
    char *platform;
    caption_voice *voices;
    sound_catalog *catalogs;
    qa_sound_caption_language *language_ticket;
    bool visiting;
};
struct qa_sound_caption_language {
    qa_sound_captions *owner,*candidate;
    bool published;
};
static bool fail(qa_error *e, qa_status code, const char *message) {
    qa_error_set(e, code, 0, "%s", message);
    return false;
}
static char *copy_text(const char *text, qa_error *e) {
    size_t n = strlen(text) + 1;
    char *copy = malloc(n);
    if (!copy)
        fail(e, QA_ERROR_MEMORY, "Allocating caption resource identity");
    else
        memcpy(copy, text, n);
    return copy;
}
qa_media_captions *qa_media_captions_create(const qa_media_caption_options *options, qa_error *e) {
    if (!options || !options->tracks || (!options->override_catalog && !options->catalogs) ||
        (options->kind != QA_CAPTION_SUBTITLE && options->kind != QA_CAPTION_SOUND)) {
        fail(e, QA_ERROR_ARGUMENT, "Caption owner requires resource pools");
        return NULL;
    }
    qa_media_captions *captions = calloc(1, sizeof(*captions));
    if (!captions) {
        fail(e, QA_ERROR_MEMORY, "Allocating media captions");
        return NULL;
    }
    captions->options = *options;
    qa_localization_retain(options->override_catalog);
    if (options->localization.platform) {
        captions->platform = copy_text(options->localization.platform, e);
        if (!captions->platform) {
            qa_media_captions_destroy(captions);
            return NULL;
        }
        captions->options.localization.platform = captions->platform;
    }
    captions->timeline = qa_captions_create(options->seat, options->override_catalog, e);
    if (!captions->timeline) {
        qa_media_captions_destroy(captions);
        return NULL;
    }
    return captions;
}
void qa_media_captions_clear(qa_media_captions *captions) {
    if (!captions || captions->visiting)
        return;
    qa_captions_clear(captions->timeline);
    free(captions->source);
    free(captions->language);
    captions->source = captions->language = NULL;
    captions->view = NULL;
}
void qa_media_captions_destroy(qa_media_captions *captions) {
    if (!captions || captions->visiting)
        return;
    qa_media_captions_clear(captions);
    qa_captions_destroy(captions->timeline);
    qa_localization_release(captions->options.override_catalog);
    free(captions->platform);
    free(captions);
}
bool qa_media_captions_prepared_is(const qa_media_captions *owner,const qa_vfs *view,const char *source,const char *language)
{
    return owner && !owner->visiting && owner->view==view &&
        ((!owner->source && !source) || (owner->source && source && !strcmp(owner->source,source))) &&
        ((!owner->language && !language) || (owner->language && language && !strcmp(owner->language,language)));
}
bool qa_media_captions_prepare(qa_media_captions *captions, qa_vfs *view, const char *source,
                               const char *language, qa_error *e) {
    if (!captions || captions->visiting || !view || !source || !language)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid caption preparation");
    if (captions->view == view && captions->source && !strcmp(captions->source, source) &&
        !strcmp(captions->language, language))
        return true;
    char *source_copy = copy_text(source, e), *language_copy = copy_text(language, e);
    if (!source_copy || !language_copy) {
        free(source_copy);
        free(language_copy);
        return false;
    }
    qa_media_captions_clear(captions);
    captions->visiting = true;
    qa_caption_track *track = NULL;
    qa_localization *catalog = captions->options.override_catalog;
    bool ok = qa_caption_library_load(captions->options.tracks, view, source_copy, language_copy,
                                      captions->options.kind, &track, e);
    if (ok && !catalog)
        ok = qa_localization_acquire(captions->options.catalogs, view, language_copy,
                                     &captions->options.localization, &catalog, e);
    if (ok)
        ok = qa_captions_replace(captions->timeline, track, e);
    if (ok) {
        qa_captions_localization(captions->timeline, catalog);
        captions->source = source_copy;
        captions->language = language_copy;
        captions->view = view;
    } else {
        free(source_copy);
        free(language_copy);
    }
    qa_caption_track_release(track);
    if (!captions->options.override_catalog)
        qa_localization_release(catalog);
    captions->visiting = false;
    return ok;
}
bool qa_media_captions_visit(qa_media_captions *captions, const char *source, double time_ms,
                             qa_media_status status, qa_caption_preferences prefs,
                             void (*visit)(void *, const qa_active_caption *), void *context,
                             qa_error *e) {
    if (!captions || captions->visiting || !source || !isfinite(time_ms) || !visit)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid media caption query");
    if (!captions->source || strcmp(captions->source, source) || status == QA_MEDIA_ENDED ||
        status == QA_MEDIA_STOPPED)
        return true;
    captions->visiting = true;
    bool ok = qa_captions_visit(captions->timeline, time_ms, prefs, visit, context, e);
    captions->visiting = false;
    return ok;
}
qa_sound_captions *qa_sound_captions_create(const qa_sound_caption_options *options, qa_error *e) {
    if (!options || !options->content_view || !options->captions.tracks ||
        (!options->captions.catalogs && !options->captions.override_catalog)) {
        fail(e, QA_ERROR_ARGUMENT, "Sound captions require content resolution");
        return NULL;
    }
    qa_sound_captions *captions = calloc(1, sizeof(*captions));
    if (!captions) {
        fail(e, QA_ERROR_MEMORY, "Allocating sound captions");
        return NULL;
    }
    captions->options = *options;
    captions->options.captions.kind = QA_CAPTION_SOUND;
    qa_localization_retain(options->captions.override_catalog);
    if (!qa_pool_prepare(&captions->voice_records, &captions->voice_storage,
            options->voice_capacity ? options->voice_capacity : 1024,
            sizeof(caption_voice), _Alignof(caption_voice), e)) {
        qa_sound_captions_destroy(captions); return NULL;
    }
    qa_arena_seal(&captions->voice_storage);
    if (options->captions.localization.platform) {
        captions->platform = copy_text(options->captions.localization.platform, e);
        if (!captions->platform) {
            qa_sound_captions_destroy(captions);
            return NULL;
        }
        captions->options.captions.localization.platform = captions->platform;
    }
    return captions;
}
static caption_voice *voice_take(qa_sound_captions *captions, qa_error *e) {
    size_t slot;
    caption_voice *voice = qa_pool_take(&captions->voice_records, &slot);
    if (!voice) { fail(e, QA_ERROR_CAPACITY, "Sound caption voice storage is full"); return NULL; }
    *voice = (caption_voice){.slot = slot}; return voice;
}
static void voice_free(qa_sound_captions *captions, caption_voice *voice) {
    qa_audio_asset_release(voice->asset);
    qa_pool_release(&captions->voice_records, voice->slot);
}
void qa_sound_captions_clear(qa_sound_captions *captions) {
    if (!captions || captions->visiting || captions->language_ticket)
        return;
    while (captions->voices) {
        caption_voice *next = captions->voices->next;
        voice_free(captions, captions->voices);
        captions->voices = next;
    }
    while (captions->catalogs) {
        sound_catalog *next = captions->catalogs->next;
        qa_audio_asset_release(captions->catalogs->asset);
        qa_media_captions_destroy(captions->catalogs->captions);
        qa_vfs_destroy(captions->catalogs->view);
        free(captions->catalogs);
        captions->catalogs = next;
    }
}
void qa_sound_captions_destroy(qa_sound_captions *captions) {
    if (!captions || captions->visiting || captions->language_ticket)
        return;
    qa_sound_captions_clear(captions);
    qa_localization_release(captions->options.captions.override_catalog);
    free(captions->platform);
    qa_arena_destroy(&captions->voice_storage);
    free(captions);
}
static sound_catalog *sound_catalog_read(qa_sound_captions *captions, qa_audio_asset *asset, qa_error *e)
{
    sound_catalog *catalog = captions->catalogs;
    while (catalog && catalog->asset != asset) catalog = catalog->next;
    if (catalog) return catalog;
    qa_vfs *source = NULL;
    catalog = calloc(1, sizeof(*catalog));
    if(catalog && captions->options.content_view(captions->options.context,asset,&source,e) &&
        source && qa_vfs_retain(source,e)) {
        catalog->view=source;
        catalog->captions=qa_media_captions_create(&captions->options.captions,e);
    }
    if (!catalog || !catalog->captions) {
        if (catalog) { qa_vfs_destroy(catalog->view); qa_media_captions_destroy(catalog->captions); free(catalog); }
        if (!e || e->code == QA_OK) fail(e, QA_ERROR_MEMORY, "Retaining original sound caption content");
        return NULL;
    }
    catalog->asset = qa_audio_asset_retain(asset);
    catalog->next = captions->catalogs; captions->catalogs = catalog;
    return catalog;
}
bool qa_sound_captions_prepare_asset(qa_sound_captions *captions, qa_audio_asset *asset,
    const char *language, qa_error *e)
{
    if (!captions || captions->visiting || captions->language_ticket || !asset || !language)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid sound caption asset preparation");
    sound_catalog *catalog = sound_catalog_read(captions, asset, e);
    return catalog && qa_media_captions_prepare(catalog->captions, catalog->view,
        qa_audio_asset_name(asset), language, e);
}
bool qa_sound_captions_event(qa_sound_captions *captions, const qa_audio_voice_event *event,
                             qa_error *e) {
    if (!captions || captions->visiting || captions->language_ticket || !event)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid caption voice event");
    if (event->seat != captions->options.captions.seat)
        return true;
    caption_voice **link = &captions->voices;
    while (*link && (*link)->id != event->voice_id)
        link = &(*link)->next;
    if (!event->started) {
        if (*link) {
            (*link)->stopping = true;
            (*link)->stop = event->output_frame;
        }
        return true;
    }
    if (!event->asset || !qa_audio_asset_resource(event->asset)) {
        if (*link) {
            caption_voice *old = *link;
            *link = old->next;
            voice_free(captions, old);
        }
        return true;
    }
    if (!event->sample_rate || !isfinite(event->source_offset_seconds))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid caption voice clock");
    caption_voice *voice = voice_take(captions, e);
    if (!voice) return false;
    voice->id = event->voice_id;
    voice->asset = qa_audio_asset_retain(event->asset);
    voice->start = event->output_frame;
    voice->rate = event->sample_rate;
    voice->offset = event->source_offset_seconds;
    sound_catalog *catalog = sound_catalog_read(captions, event->asset, e);
    if (!catalog) { voice_free(captions, voice); return false; }
    voice->catalog = catalog;
    if (*link) {
        caption_voice *old = *link;
        voice->next = old->next;
        *link = voice;
        voice_free(captions, old);
    } else
        *link = voice;
    return true;
}
bool qa_sound_captions_prepare(qa_sound_captions *captions, const char *language, int64_t frame,
                               qa_error *e) {
    if (!captions || captions->visiting || captions->language_ticket || !language)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid sound caption preparation");
    /* Prevent resource/diagnostic callbacks from changing this list mid-prepare.
     */
    captions->visiting = true;
    bool ok = true;
    caption_voice **link = &captions->voices;
    while (*link) {
        caption_voice *voice = *link;
        if (voice->stopping && frame >= voice->stop) {
            *link = voice->next;
            voice_free(captions, voice);
            continue;
        }
        sound_catalog *catalog = voice->catalog;
        if (!catalog || !catalog->view) { ok = fail(e, QA_ERROR_FORMAT, "Sound caption lost its original content owner"); break; }
        qa_media_captions *media = catalog->captions;
        if (!media->language || strcmp(media->language, language)) {
            if (!qa_media_captions_prepare(media, catalog->view, qa_audio_asset_name(voice->asset), language,
                                           e)) {
                qa_media_captions_clear(media);
                ok = false;
                break;
            }
        }
        link = &voice->next;
    }
    captions->visiting = false;
    return ok;
}
bool qa_sound_captions_visit(qa_sound_captions *captions, int64_t frame,
                             qa_caption_preferences prefs,
                             void (*visit)(void *, const qa_active_caption *), void *context,
                             qa_error *e) {
    if (!captions || captions->visiting || captions->language_ticket || !visit)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid sound caption query");
    captions->visiting = true;
    bool ok = true;
    for (caption_voice *voice = captions->voices; voice; voice = voice->next) {
        if (!voice->catalog || frame < voice->start || (voice->stopping && frame >= voice->stop))
            continue;
        uint64_t elapsed = (uint64_t)frame - (uint64_t)voice->start;
        double time_ms = voice->offset * 1000 + (double)elapsed * 1000 / voice->rate;
        if (!qa_media_captions_visit(voice->catalog->captions, qa_audio_asset_name(voice->asset),
                                     time_ms, QA_MEDIA_PLAYING, prefs, visit, context, e)) {
            ok = false;
            break;
        }
    }
    captions->visiting = false;
    return ok;
}
bool qa_sound_captions_idle(const qa_sound_captions *captions)
{ return captions && !captions->visiting && !captions->language_ticket; }
bool qa_sound_captions_assets_read(const qa_sound_captions *captions,
    qa_audio_asset ***out, size_t *count, qa_error *e)
{
    if (!qa_sound_captions_idle(captions) || !out || *out || !count || *count)
        return fail(e, QA_ERROR_ARGUMENT, "Sound caption holders require an idle owner and empty output");
    size_t size = 0;
    for (const caption_voice *voice = captions->voices; voice; voice = voice->next) {
        if (size == SIZE_MAX / sizeof(qa_audio_asset *)) return fail(e, QA_ERROR_MEMORY, "Caption voice inventory overflows");
        ++size;
    }
    for (const sound_catalog *catalog = captions->catalogs; catalog; catalog = catalog->next) {
        if (size == SIZE_MAX / sizeof(qa_audio_asset *)) return fail(e, QA_ERROR_MEMORY, "Caption catalog inventory overflows");
        ++size;
    }
    qa_audio_asset **assets = size ? malloc(size * sizeof(*assets)) : NULL;
    if (size && !assets) return fail(e, QA_ERROR_MEMORY, "Reading actual caption audio holders");
    size_t i = 0;
    for (const caption_voice *voice = captions->voices; voice; voice = voice->next) assets[i++] = voice->asset;
    for (const sound_catalog *catalog = captions->catalogs; catalog; catalog = catalog->next) assets[i++] = catalog->asset;
    *out = assets; *count = size; return true;
}
bool qa_sound_captions_views_visit(const qa_sound_captions *captions,
    bool (*visit)(void *, const qa_vfs *, qa_error *), void *context, qa_error *e)
{
    if (!qa_sound_captions_idle(captions) || !visit)
        return fail(e, QA_ERROR_ARGUMENT, "Sound caption content requires an idle actual owner");
    for (const sound_catalog *catalog = captions->catalogs; catalog; catalog = catalog->next)
        if (!catalog->view || !visit(context, catalog->view, e)) return false;
    return true;
}

static bool caption_blob(qa_source_save_io *io, qa_buffer *blob, qa_bytes *input)
{
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? blob->size : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, blob->data, size);
    if (size > io->input.size - io->offset) return false;
    *input = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}

static bool media_fields(qa_source_save_io *io, qa_media_captions *media, qa_vfs *view)
{
    if (media->visiting || (media->view && media->view != view)) return false;
    if (!qa_source_save_owned_text(io, &media->source) || !qa_source_save_owned_text(io, &media->language) ||
        ((media->source != NULL) != (media->language != NULL))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) media->view = media->source ? view : NULL;
    qa_buffer blob = {0}; qa_bytes input = {0};
    bool ok = io->direction == QA_SOURCE_SAVE_READ || qa_captions_checkpoint(media->timeline,
        media->options.tracks, media->options.catalogs, &blob, io->error);
    if (ok) ok = caption_blob(io, &blob, &input);
    if (ok && io->direction == QA_SOURCE_SAVE_READ)
        ok = qa_captions_restore(media->timeline, media->options.tracks, media->options.catalogs, input, io->error);
    free(blob.data); return ok;
}
bool qa_media_captions_idle(const qa_media_captions *owner)
{ return owner && !owner->visiting; }
static bool media_capsule_fields(qa_source_save_io *io,qa_media_captions *owner,qa_vfs *view,const char *source)
{
    if (!qa_text_save_header(io,"QMCP")) return false;
    uint32_t seat=owner->options.seat,kind=owner->options.kind,profile=owner->options.localization.profile;
    uint64_t override=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE && !qa_localization_pool_catalog_key(owner->options.catalogs,
        owner->options.override_catalog,&override)) return false;
    char *platform=io->direction==QA_SOURCE_SAVE_WRITE?owner->platform:NULL;
    bool ok=qa_source_save_u32(io,&seat) && qa_source_save_u32(io,&kind) &&
        qa_source_save_u32(io,&profile) && qa_source_save_u64(io,&override) && qa_source_save_owned_text(io,&platform);
    ok=ok && seat==owner->options.seat && kind==(uint32_t)owner->options.kind &&
        profile==(uint32_t)owner->options.localization.profile &&
        ((platform==NULL && owner->platform==NULL) || (platform && owner->platform && !strcmp(platform,owner->platform))) &&
        (!override || qa_localization_pool_catalog(owner->options.catalogs,override)) &&
        qa_localization_pool_catalog(owner->options.catalogs,override)==owner->options.override_catalog;
    if (io->direction==QA_SOURCE_SAVE_READ) free(platform);
    return ok && media_fields(io,owner,view) && (!owner->source || view) &&
        ((!owner->source && !source) || (owner->source && source && !strcmp(owner->source,source)));
}
bool qa_media_captions_checkpoint(const qa_media_captions *owner,const qa_vfs *view,const char *source,qa_buffer *out,qa_error *e)
{
    if (!qa_media_captions_idle(owner) || owner->view!=view || (view!=NULL)!=(source!=NULL) ||
        !out || out->data || out->size)
        return fail(e,QA_ERROR_ARGUMENT,"Media caption capture requires its actual idle view owner");
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) &&
        media_capsule_fields(&io,(qa_media_captions *)owner,(qa_vfs *)view,source) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!e || e->code==QA_OK)) fail(e,QA_ERROR_FORMAT,"Invalid compiled media caption continuation");
    return ok;
}
bool qa_media_captions_restore(qa_media_captions *owner,qa_vfs *view,const char *source,qa_bytes bytes,qa_error *e)
{
    if (!qa_media_captions_idle(owner) || owner->source || owner->language || owner->view ||
        (view!=NULL)!=(source!=NULL))
        return fail(e,QA_ERROR_ARGUMENT,"Media caption import requires its empty actual owner");
    qa_media_captions *candidate=qa_media_captions_create(&owner->options,e);
    if (!candidate) return false;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && media_capsule_fields(&io,candidate,view,source) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        qa_captions_destroy(owner->timeline); owner->timeline=candidate->timeline; candidate->timeline=NULL;
        owner->source=candidate->source; candidate->source=NULL;
        owner->language=candidate->language; candidate->language=NULL;
        owner->view=candidate->view; candidate->view=NULL;
    }
    qa_media_captions_destroy(candidate);
    if (!ok && (!e || e->code==QA_OK)) fail(e,QA_ERROR_FORMAT,"Invalid compiled media caption continuation");
    return ok;
}

static bool sound_clone(const qa_sound_captions *source,qa_sound_captions *candidate,qa_error *e)
{
    sound_catalog **link=&candidate->catalogs;
    for (const sound_catalog *row=source->catalogs;row;row=row->next) {
        if (!row->view || !row->asset || !row->captions ||
            (row->captions->view && row->captions->view!=row->view))
            return fail(e,QA_ERROR_FORMAT,"Caption catalog lost its actual prepared content view");
        sound_catalog *copy=calloc(1,sizeof(*copy));
        if (!copy) return fail(e,QA_ERROR_MEMORY,"Copying retained caption catalog");
        *link=copy; link=&copy->next;
        copy->asset=qa_audio_asset_retain(row->asset);
        if(!qa_vfs_retain(row->view,e))return false;
        copy->view=row->view;
        copy->captions=qa_media_captions_create(&candidate->options.captions,e);
        if (!copy->view || !copy->captions) return false;
        qa_buffer saved={0};
        bool ok=qa_media_captions_checkpoint(row->captions,row->captions->view,row->captions->source,&saved,e) &&
            qa_media_captions_restore(copy->captions,row->captions->view?copy->view:NULL,
                row->captions->source,(qa_bytes){saved.data,saved.size},e);
        qa_buffer_free(&saved); if (!ok) return false;
    }
    caption_voice **voice_link=&candidate->voices;
    for (const caption_voice *voice=source->voices;voice;voice=voice->next) {
        caption_voice *copy=voice_take(candidate,e);
        if (!copy) return false;
        size_t slot=copy->slot;
        *copy=*voice; copy->slot=slot; copy->next=NULL; copy->asset=qa_audio_asset_retain(voice->asset); copy->catalog=NULL;
        *voice_link=copy; voice_link=&copy->next;
        const sound_catalog *old=source->catalogs;
        sound_catalog *replacement=candidate->catalogs;
        while (old && old!=voice->catalog) { old=old->next; replacement=replacement->next; }
        if (!old || !replacement || replacement->asset!=copy->asset)
            return fail(e,QA_ERROR_FORMAT,"Caption voice lost its retained source catalog");
        copy->catalog=replacement;
    }
    return true;
}
bool qa_sound_caption_language_prepare(qa_sound_captions *owner,const char *language,
    int64_t frame,qa_sound_caption_language **out,qa_error *e)
{
    if (!qa_sound_captions_idle(owner) || !language || !out || *out)
        return fail(e,QA_ERROR_ARGUMENT,"Caption language requires its idle actual owner");
    qa_sound_caption_language *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) return fail(e,QA_ERROR_MEMORY,"Retaining sound caption language preparation");
    ticket->owner=owner; owner->language_ticket=ticket;
    ticket->candidate=qa_sound_captions_create(&owner->options,e);
    bool ok=ticket->candidate && sound_clone(owner,ticket->candidate,e) &&
        qa_sound_captions_prepare(ticket->candidate,language,frame,e);
    if (!ok) {
        owner->language_ticket=NULL; qa_sound_captions_destroy(ticket->candidate); free(ticket); return false;
    }
    *out=ticket; return true;
}
bool qa_sound_caption_language_ready(const qa_sound_caption_language *ticket,qa_error *e)
{
    return (ticket && !ticket->published && ticket->owner && ticket->candidate &&
        ticket->owner->language_ticket==ticket && !ticket->owner->visiting &&
        qa_sound_captions_idle(ticket->candidate)) ||
        fail(e,QA_ERROR_ARGUMENT,"Caption language lost its actual retained publication lease");
}
void qa_sound_caption_language_publish(qa_sound_caption_language *ticket)
{
    caption_voice *voices=ticket->owner->voices;
    sound_catalog *catalogs=ticket->owner->catalogs;
    ticket->owner->voices=ticket->candidate->voices; ticket->owner->catalogs=ticket->candidate->catalogs;
    qa_pool records=ticket->owner->voice_records;
    ticket->owner->voice_records=ticket->candidate->voice_records; ticket->candidate->voice_records=records;
    qa_arena storage=ticket->owner->voice_storage;
    ticket->owner->voice_storage=ticket->candidate->voice_storage; ticket->candidate->voice_storage=storage;
    ticket->candidate->voices=voices; ticket->candidate->catalogs=catalogs; ticket->published=true;
}
void qa_sound_caption_language_commit(qa_sound_caption_language *ticket)
{
    qa_sound_caption_language_publish(ticket);
    qa_sound_captions_destroy(ticket->candidate);
    ticket->owner->language_ticket=NULL;
    free(ticket);
}
static bool language_dispose(qa_sound_caption_language **in,bool published,qa_error *e)
{
    if (!in || !*in) return true;
    qa_sound_caption_language *ticket=*in;
    if (ticket->published!=published || !ticket->owner || ticket->owner->language_ticket!=ticket ||
        ticket->owner->visiting || !qa_sound_captions_idle(ticket->candidate))
        return fail(e,QA_ERROR_ARGUMENT,"Caption language disposal requires its retained returned parents");
    qa_sound_captions_destroy(ticket->candidate); ticket->owner->language_ticket=NULL;
    free(ticket); *in=NULL; return true;
}
bool qa_sound_caption_language_finish(qa_sound_caption_language **in,qa_error *e)
{ return language_dispose(in,true,e); }
bool qa_sound_caption_language_abort(qa_sound_caption_language **in,qa_error *e)
{ return language_dispose(in,false,e); }
