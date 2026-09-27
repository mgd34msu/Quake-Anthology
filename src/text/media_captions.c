#include "qa/media_captions.h"

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
} sound_catalog;
typedef struct caption_voice {
    struct caption_voice *next;
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
    char *platform;
    caption_voice *voices;
    sound_catalog *catalogs;
    bool visiting;
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
static void voice_free(caption_voice *voice) {
    qa_audio_asset_release(voice->asset);
    free(voice);
}
void qa_sound_captions_clear(qa_sound_captions *captions) {
    if (!captions || captions->visiting)
        return;
    while (captions->voices) {
        caption_voice *next = captions->voices->next;
        voice_free(captions->voices);
        captions->voices = next;
    }
    while (captions->catalogs) {
        sound_catalog *next = captions->catalogs->next;
        qa_audio_asset_release(captions->catalogs->asset);
        qa_media_captions_destroy(captions->catalogs->captions);
        free(captions->catalogs);
        captions->catalogs = next;
    }
}
void qa_sound_captions_destroy(qa_sound_captions *captions) {
    if (!captions || captions->visiting)
        return;
    qa_sound_captions_clear(captions);
    qa_localization_release(captions->options.captions.override_catalog);
    free(captions->platform);
    free(captions);
}
bool qa_sound_captions_event(qa_sound_captions *captions, const qa_audio_voice_event *event,
                             qa_error *e) {
    if (!captions || captions->visiting || !event)
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
    if (!event->asset) {
        if (*link) {
            caption_voice *old = *link;
            *link = old->next;
            voice_free(old);
        }
        return true;
    }
    if (!event->sample_rate || !isfinite(event->source_offset_seconds))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid caption voice clock");
    caption_voice *voice = calloc(1, sizeof(*voice));
    if (!voice)
        return fail(e, QA_ERROR_MEMORY, "Allocating caption voice");
    voice->id = event->voice_id;
    voice->asset = qa_audio_asset_retain(event->asset);
    voice->start = event->output_frame;
    voice->rate = event->sample_rate;
    voice->offset = event->source_offset_seconds;
    if (*link) {
        caption_voice *old = *link;
        voice->next = old->next;
        *link = voice;
        voice_free(old);
    } else
        *link = voice;
    return true;
}
bool qa_sound_captions_prepare(qa_sound_captions *captions, const char *language, int64_t frame,
                               qa_error *e) {
    if (!captions || captions->visiting || !language)
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
            voice_free(voice);
            continue;
        }
        sound_catalog *catalog = voice->catalog;
        if (!catalog) {
            for (catalog = captions->catalogs; catalog; catalog = catalog->next)
                if (catalog->asset == voice->asset)
                    break;
            if (!catalog) {
                catalog = calloc(1, sizeof(*catalog));
                if (!catalog) {
                    ok = fail(e, QA_ERROR_MEMORY, "Allocating sound caption catalog");
                    break;
                }
                catalog->captions = qa_media_captions_create(&captions->options.captions, e);
                if (!catalog->captions) {
                    free(catalog);
                    ok = false;
                    break;
                }
                catalog->asset = qa_audio_asset_retain(voice->asset);
                catalog->next = captions->catalogs;
                captions->catalogs = catalog;
            }
            voice->catalog = catalog;
        }
        qa_media_captions *media = catalog->captions;
        if (!media->language || strcmp(media->language, language)) {
            qa_vfs *view = NULL;
            if (!captions->options.content_view(captions->options.context, voice->asset, &view,
                                                e) ||
                !qa_media_captions_prepare(media, view, qa_audio_asset_name(voice->asset), language,
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
    if (!captions || captions->visiting || !visit)
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
