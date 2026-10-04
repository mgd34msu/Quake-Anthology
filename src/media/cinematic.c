#include "cinematic_internal.h"
#include "qa/binary.h"
#include "qa/audio_save.h"
#include "qa/cinematic_restore.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool cinematic_fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text);
    return false;
}
static bool wall_time(qa_cinematic *movie, double *out, qa_error *error) {
    double now = movie->options.clock.sample(movie->options.clock.context);
    if ((!isfinite(now) || now < 0) && movie->format==QA_CINEMATIC_ROQ && movie->options.roq_scratch) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid original cinematic Source clock");
        return false;
    }
    if (!isfinite(now) || now < 0)
        return cinematic_fail(error, "Invalid cinematic wall clock");
    *out = now;
    return true;
}
bool cinematic_elapsed(qa_cinematic *movie, double *out, qa_error *error) {
    if (movie->format==QA_CINEMATIC_ROQ && movie->options.roq_scratch)
        return wall_time(movie,out,error);
    double now = movie->paused_at;
    if (!movie->paused && !wall_time(movie, &now, error))
        return false;
    double elapsed = movie->offset_ms + now - movie->start_ms - movie->paused_duration;
    if (!isfinite(elapsed))
        return cinematic_fail(error, "Cinematic elapsed time overflow");
    *out = elapsed < 0 ? 0 : elapsed;
    return true;
}
static double sample(void *context) {
    double result;
    return cinematic_elapsed(context, &result, NULL) ? result : NAN;
}
static bool pause_clock(qa_cinematic *movie, bool paused, qa_error *error) {
    if (movie->paused == paused)
        return true;
    double now;
    if (!wall_time(movie, &now, error))
        return false;
    if (paused)
        movie->paused_at = now;
    else
        movie->paused_duration += now - movie->paused_at;
    movie->paused = paused;
    return true;
}
static qa_audio_raw_stream *current_raw(const qa_cinematic *movie) {
    return movie->raw_attached && movie->options.audio ?
        qa_audio_engine_bus_stream(movie->options.audio, movie->options.audio_bus) : NULL;
}
static void reset_audio(qa_cinematic *movie) {
    if (!movie->raw_attached)
        return;
    if (!movie->restore_pending)
        qa_audio_engine_remove_stream(movie->options.audio, movie->options.audio_bus);
    movie->raw_attached = false;
}
static bool before_audio_reset(void *context, qa_error *error) {
    (void)error;
    qa_cinematic *movie = context;
    if (!movie->suppress_audio)
        reset_audio(movie);
    return true;
}
static uint32_t audio_audience(const qa_cinematic_options *options) {
    if (options->audio_audience.kind == QA_CINEMATIC_AUDIO_SEAT)
        return options->audio_audience.seat;
    if (options->audio_audience.kind == QA_CINEMATIC_AUDIO_WORLD)
        return QA_AUDIO_WORLD;
    return options->target.kind == QA_CINEMATIC_SEAT ? options->target.id.seat : QA_AUDIO_WORLD;
}
static bool queue_audio(void *context, const qa_media_audio *sound, qa_error *error) {
    qa_cinematic *movie = context;
    if (movie->suppress_audio)
        return true;
    if (!movie->options.audio || (sound->channels != 1 && sound->channels != 2) ||
        (sound->sample_bytes != 1 && sound->sample_bytes != 2) || !sound->rate ||
        sound->pcm.size % ((size_t)sound->channels * sound->sample_bytes) ||
        (sound->pcm.size && !sound->pcm.data))
        return cinematic_fail(error, "Invalid cinematic audio");
    size_t samples = sound->pcm.size / sound->sample_bytes;
    if (samples > SIZE_MAX / sizeof(int16_t))
        return cinematic_fail(error, "Cinematic PCM size overflow");
    const int16_t *pcm;
    if (sound->native_pcm && sound->sample_bytes == 2)
        pcm = (const int16_t *)sound->pcm.data;
    else {
        if (samples > movie->pcm_capacity) {
            int16_t *grown = realloc(movie->pcm, samples * sizeof(*grown));
            if (!grown) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating cinematic PCM conversion");
                return false;
            }
            movie->pcm = grown;
            movie->pcm_capacity = samples;
        }
        for (size_t i = 0; i < samples; ++i)
            movie->pcm[i] = sound->sample_bytes == 1
                                ? (int16_t)(((int)sound->pcm.data[i] - 128) * 256)
                                : qa_load_i16le(sound->pcm.data + i * 2);
        pcm = movie->pcm;
    }
    qa_audio_raw_stream *raw = current_raw(movie);
    bool installed = raw != NULL;
    uint32_t rate = qa_audio_engine_rate(movie->options.audio);
    if (!raw && !qa_audio_raw_create(rate, &raw, error))
        return false;
    if (!qa_audio_raw_set_rate(raw, rate, error) ||
        !qa_audio_raw_queue(
            raw, pcm, samples / sound->channels, sound->channels, sound->rate, sound->source_sample,
            sound->reset || !installed || movie->audio_loop != sound->loop, error)) {
        if (!installed)
            qa_audio_raw_destroy(raw);
        return false;
    }
    if (!installed) {
        uint32_t audience = audio_audience(&movie->options);
        if (!qa_audio_engine_stream(movie->options.audio, movie->options.audio_bus, audience,
                                    movie->options.gain, raw, error)) {
            qa_audio_raw_destroy(raw);
            return false;
        }
    }
    movie->raw_attached = true;
    movie->audio_loop = sound->loop;
    return true;
}
static void diagnostic(void *context, const char *text) {
    qa_cinematic *movie = context;
    if (!movie->suppress_audio && movie->options.diagnostic)
        movie->options.diagnostic(movie->options.context, text);
}
static void dropped(void *context, uint64_t requested, uint64_t decoded) {
    (void)requested;
    (void)decoded;
    diagnostic(context, "Dropped cinematic frame\n");
}
static void picture(qa_cinematic *movie) {
    const qa_media_frame *frame = NULL;
    switch (movie->format) {
    case QA_CINEMATIC_CIN:
        frame = qa_cin_playback_frame(movie->movie.cin);
        break;
    case QA_CINEMATIC_ROQ:
        frame = qa_roq_playback_frame(movie->movie.roq);
        break;
    case QA_CINEMATIC_OGV:
        frame = qa_ogv_playback_frame(movie->movie.ogv);
        break;
    case QA_CINEMATIC_IMAGE: {
        const qa_scene_image_level *level = movie->movie.image->levels;
        movie->picture = (qa_media_frame){
            .rgba = {level->pixels, level->bytes}, .width = level->width, .height = level->height};
        movie->has_picture = true;
        return;
    }
    }
    movie->has_picture = frame != NULL;
    movie->picture = frame ? *frame : (qa_media_frame){0};
}
static void free_movie(qa_cinematic *movie) {
    reset_audio(movie);
    switch (movie->format) {
    case QA_CINEMATIC_CIN:
        qa_cin_playback_destroy(movie->movie.cin);
        break;
    case QA_CINEMATIC_ROQ:
        qa_roq_playback_destroy(movie->movie.roq);
        break;
    case QA_CINEMATIC_OGV:
        qa_ogv_playback_destroy(movie->movie.ogv);
        break;
    case QA_CINEMATIC_IMAGE:
        qa_scene_image_release(movie->movie.image);
        break;
    }
    qa_scene_image_release(movie->image);
    qa_cinematic_asset_release(movie->asset);
    free(movie->name);
    free(movie->pcm);
    free(movie);
}
static bool same_target(qa_cinematic_target left, qa_cinematic_target right) {
    return left.kind == right.kind &&
           (left.kind == QA_CINEMATIC_SEAT ? left.id.seat == right.id.seat
                                           : left.id.material == right.id.material);
}
static bool restore(qa_cinematic *movie, const qa_cinematic_checkpoint *saved, bool qualified, qa_error *error) {
    if (qualified && movie->format==QA_CINEMATIC_ROQ && movie->options.roq_scratch &&
        saved->elapsed_ms!=movie->start_ms)
        return cinematic_fail(error,"Source cinematic checkpoint differs from its retained absolute clock receipt");
    if (!saved->source || saved->format != movie->format ||
        !same_target(saved->target, movie->options.target) || saved->loop != movie->options.loop ||
        saved->audio_audience.kind != movie->options.audio_audience.kind ||
        (saved->audio_audience.kind == QA_CINEMATIC_AUDIO_SEAT &&
         saved->audio_audience.seat != movie->options.audio_audience.seat) ||
        saved->hold != movie->options.hold || saved->silent != movie->options.silent ||
        !isfinite(saved->elapsed_ms) || saved->elapsed_ms < 0 || saved->status < QA_MEDIA_PLAYING ||
        saved->status > QA_MEDIA_STOPPED || saved->decoder_status < QA_MEDIA_PLAYING ||
        saved->decoder_status > QA_MEDIA_STOPPED ||
        (saved->completed !=
         (saved->status == QA_MEDIA_ENDED || saved->status == QA_MEDIA_STOPPED)) ||
        (saved->status != QA_MEDIA_PLAYING && !saved->paused))
        return cinematic_fail(error, "Invalid cinematic checkpoint");
    bool ok = false;
    switch (movie->format) {
    case QA_CINEMATIC_CIN:
        ok = qa_cin_playback_restore(movie->movie.cin, &saved->decoder.cin, error);
        break;
    case QA_CINEMATIC_ROQ:
        ok = qa_roq_playback_restore(movie->movie.roq, &saved->decoder.roq, error);
        break;
    case QA_CINEMATIC_OGV:
        ok = qa_ogv_playback_restore(movie->movie.ogv, &saved->decoder.ogv, error);
        break;
    case QA_CINEMATIC_IMAGE:
        ok = true;
        break;
    }
    if (!ok)
        return false;
    movie->offset_ms = saved->elapsed_ms;
    movie->paused = saved->paused;
    movie->paused_at = movie->start_ms;
    movie->status = saved->status;
    movie->decoder_status = saved->decoder_status;
    movie->revision = saved->revision;
    movie->audio_loop = saved->audio_loop;
    movie->dirty = saved->dirty;
    movie->completed = saved->completed;
    movie->focus_paused = saved->focus_paused;
    picture(movie);
    if ((saved->audio.size && !saved->audio_attached) ||
        (saved->audio_attached && (saved->format == QA_CINEMATIC_IMAGE || saved->completed ||
                                  saved->silent || !movie->options.audio)))
        return cinematic_fail(error, "Saved cinematic has an invalid shared audio attachment");
    if (qualified && saved->audio_attached) {
        if (!qa_audio_engine_raw_ready(movie->options.audio, movie->options.audio_bus,
            audio_audience(&movie->options), movie->options.gain,
            saved->audio.size != 0, error)) return false;
    } else if (saved->audio.size) {
        if (!movie->options.audio || saved->completed || saved->silent || !saved->audio.data)
            return cinematic_fail(error, "Saved cinematic audio has no active owner");
        qa_audio_raw_stream *raw;
        if (!qa_audio_raw_restore((qa_bytes){saved->audio.data, saved->audio.size},
                                  qa_audio_engine_rate(movie->options.audio), &raw, error))
            return false;
        uint32_t audience = audio_audience(&movie->options);
        if (!qa_audio_engine_stream(movie->options.audio, movie->options.audio_bus, audience,
                                    movie->options.gain, raw, error)) {
            qa_audio_raw_destroy(raw);
            return false;
        }
    }
    movie->raw_attached = saved->audio_attached;
    return true;
}
static bool create(const qa_cinematic_source *source, const qa_cinematic_options *options,
                   const qa_cinematic_checkpoint *saved, bool qualified, double anchor,
                   qa_cinematic **out, qa_error *error) {
    if (!source || !source->name || !options || !options->clock.sample || !out ||
        source->format < QA_CINEMATIC_CIN || source->format > QA_CINEMATIC_IMAGE ||
        options->target.kind < QA_CINEMATIC_SEAT || options->target.kind > QA_CINEMATIC_MATERIAL ||
        (options->target.kind == QA_CINEMATIC_SEAT && options->target.id.seat >= 4) ||
        options->audio_audience.kind < QA_CINEMATIC_AUDIO_TARGET ||
        options->audio_audience.kind > QA_CINEMATIC_AUDIO_WORLD ||
        (options->audio_audience.kind == QA_CINEMATIC_AUDIO_SEAT && options->audio_audience.seat >= 4) ||
        !isfinite(options->gain) || options->gain < 0 ||
        (!options->silent && source->format != QA_CINEMATIC_IMAGE && !options->audio))
        return cinematic_fail(error, "Invalid cinematic configuration");
    qa_cinematic *movie = calloc(1, sizeof(*movie));
    if (!movie) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating cinematic owner");
        return false;
    }
    movie->options = *options;
    movie->format = source->format;
    movie->restore_pending = qualified;
    movie->suppress_audio = saved != NULL;
    movie->status = movie->decoder_status = QA_MEDIA_PLAYING;
    movie->image_revision = UINT64_MAX;
    size_t length = strlen(source->name);
    movie->name = malloc(length + 1);
    if (!movie->name) {
        free(movie);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating cinematic name");
        return false;
    }
    memcpy(movie->name, source->name, length + 1);
    movie->asset = source->asset;
    qa_cinematic_asset_retain(movie->asset);
    if (qualified) {
        movie->start_ms=movie->paused_at=anchor; movie->paused=true;
    }
    if (!qualified && !wall_time(movie, &movie->start_ms, error)) {
        free_movie(movie);
        return false;
    }
    bool ok = false;
    switch (source->format) {
    case QA_CINEMATIC_CIN: {
        qa_cin_playback_options cin = {.loop = options->loop,
                                       .hold = options->hold,
                                       .silent = options->silent,
                                       .context = movie,
                                       .audio = queue_audio,
                                       .dropped_frame = dropped};
        ok = qa_cin_playback_create(source->data.cin, &cin, 0, &movie->movie.cin, error);
        break;
    }
    case QA_CINEMATIC_ROQ: {
        qa_roq_playback_options roq = {.loop = options->loop,
                                       .hold = options->hold,
                                       .silent = options->silent,
                                       .shader = options->target.kind == QA_CINEMATIC_MATERIAL,
                                       .scratch = options->roq_scratch,
                                       .context = movie,
                                       .audio = queue_audio,
                                       .before_audio_reset = before_audio_reset,
                                       .diagnostic = diagnostic};
        ok = qa_roq_playback_create(source->data.roq, &roq, (qa_media_clock){movie, sample},
                                    &movie->movie.roq, error);
        break;
    }
    case QA_CINEMATIC_OGV: {
        qa_ogv_options ogv = {.loop = options->loop,
                              .hold = options->hold,
                              .silent = options->silent,
                              .context = movie,
                              .audio = queue_audio};
        ok = qa_ogv_playback_create(source->data.ogv, &ogv, 0, &movie->movie.ogv, error);
        break;
    }
    case QA_CINEMATIC_IMAGE: {
        const qa_scene_image *image = source->data.image;
        if (!image || image->kind != QA_SCENE_RGBA8 || !image->level_count || !image->levels ||
            !image->levels[0].width || !image->levels[0].height || !image->levels[0].pixels ||
            (uint64_t)image->levels[0].width * image->levels[0].height > SIZE_MAX / 4 ||
            (size_t)image->levels[0].width * image->levels[0].height * 4 != image->levels[0].bytes)
            cinematic_fail(error, "Invalid cinematic still image");
        else {
            qa_scene_image_retain(image);
            movie->movie.image = image;
            movie->status = movie->decoder_status = QA_MEDIA_HELD;
            movie->paused = true;
            movie->paused_at = movie->start_ms;
            ok = true;
        }
        break;
    }
    }
    if (!ok) {
        free_movie(movie);
        return false;
    }
    picture(movie);
    movie->dirty = movie->has_picture;
    if (saved && !restore(movie, saved, qualified, error)) {
        free_movie(movie);
        return false;
    }
    movie->suppress_audio = movie->restore_pending;
    *out = movie;
    return true;
}
bool qa_cinematic_create(const qa_cinematic_source *source, const qa_cinematic_options *options,
    const qa_cinematic_checkpoint *saved, qa_cinematic **out, qa_error *error)
{
    return create(source,options,saved,false,0,out,error);
}
bool qa_cinematic_restore_qualified(const qa_cinematic_source *source, const qa_cinematic_options *options,
    const qa_cinematic_checkpoint *saved, double anchor, qa_cinematic **out, qa_error *error)
{
    if (!source || !source->name || !saved || !saved->source || strcmp(source->name,saved->source) ||
        !out || *out || !isfinite(anchor) || anchor<0)
        return cinematic_fail(error,"Qualified cinematic restore requires an empty candidate and actual clock anchor");
    return create(source,options,saved,true,anchor,out,error);
}
void qa_cinematic_restore_commit(qa_cinematic *movie) {
    if (!movie || movie->busy || !movie->restore_pending) return;
    movie->restore_pending = false;
    movie->suppress_audio = false;
}
void qa_cinematic_restore_discard(qa_cinematic *movie) {
    if (!movie || movie->busy || !movie->restore_pending) return;
    movie->busy = true;
    free_movie(movie);
}
static bool finish(qa_cinematic *movie, qa_cinematic_end reason, qa_error *error) {
    if (movie->completed)
        return true;
    if (!pause_clock(movie, true, error))
        return false;
    movie->completed = true;
    movie->status = reason == QA_CINEMATIC_STOPPED ? QA_MEDIA_STOPPED : QA_MEDIA_ENDED;
    reset_audio(movie);
    if (movie->options.complete)
        movie->options.complete(movie->options.context, movie->options.target, reason);
    return true;
}
void qa_cinematic_destroy(qa_cinematic *movie) {
    if (!movie || movie->busy || movie->restore_pending)
        return;
    movie->busy = true;
    if (!movie->completed) {
        movie->completed = true;
        movie->status = QA_MEDIA_STOPPED;
        reset_audio(movie);
        if (movie->options.complete)
            movie->options.complete(movie->options.context, movie->options.target,
                                    QA_CINEMATIC_STOPPED);
    }
    free_movie(movie);
}
bool qa_cinematic_roq_restart(qa_cinematic *movie, qa_error *error)
{
    if (!movie || movie->busy || movie->restore_pending || movie->format!=QA_CINEMATIC_ROQ ||
        !movie->options.roq_scratch)
        return cinematic_fail(error,"Original RoQ restart requires its returned shared decoder owner");
    movie->busy=true;
    bool ok=pause_clock(movie,false,error) &&
        qa_roq_playback_restart(movie->movie.roq,(qa_media_clock){movie,sample},false,error);
    if (ok) {
        movie->status=movie->decoder_status=QA_MEDIA_PLAYING;
        movie->completed=false; movie->faulted=false;
        picture(movie);
    }
    movie->busy=false; return ok;
}
bool qa_cinematic_roq_scratch_rebind_ready(const qa_cinematic *movie,
    const qa_roq_scratch *scratch, qa_error *error)
{
    if (!movie || movie->busy || movie->format != QA_CINEMATIC_ROQ || !movie->options.roq_scratch)
        return cinematic_fail(error,"RoQ scratch adoption requires its returned explicit cinematic owner");
    return qa_roq_playback_scratch_rebind_ready(movie->movie.roq,scratch,error);
}
void qa_cinematic_roq_scratch_rebind(qa_cinematic *movie, qa_roq_scratch *scratch)
{
    qa_roq_playback_scratch_rebind(movie->movie.roq,scratch);
    movie->options.roq_scratch=scratch;
}
bool qa_cinematic_tick(qa_cinematic *movie, qa_media_tick *out, qa_error *error) {
    if (!movie || !out || movie->busy || movie->faulted || movie->restore_pending)
        return cinematic_fail(error, "Cinematic is unavailable");
    movie->busy = true;
    bool ok = true, changed = movie->dirty, looped = false;
    if (movie->status == QA_MEDIA_PLAYING && movie->format != QA_CINEMATIC_IMAGE) {
        double now;
        qa_media_tick tick = {0};
        ok = cinematic_elapsed(movie, &now, error);
        if (ok)
            switch (movie->format) {
            case QA_CINEMATIC_CIN:
                ok = qa_cin_playback_tick(movie->movie.cin, now, true, &tick, error);
                break;
            case QA_CINEMATIC_ROQ:
                ok = qa_roq_playback_tick(movie->movie.roq, (qa_media_clock){movie, sample}, &tick,
                                          error);
                break;
            case QA_CINEMATIC_OGV:
                ok = qa_ogv_playback_tick(movie->movie.ogv, now, &tick, error);
                break;
            case QA_CINEMATIC_IMAGE:
                break;
            }
        if (ok) {
            movie->decoder_status = tick.status;
            looped = tick.looped;
            if (tick.changed) {
                if (movie->revision == UINT64_MAX)
                    ok = cinematic_fail(error, "Cinematic image revision overflow");
                else {
                    ++movie->revision;
                    changed = true;
                    picture(movie);
                }
            }
            if (ok && tick.status == QA_MEDIA_HELD) {
                ok = pause_clock(movie, true, error);
                if (ok)
                    movie->status = QA_MEDIA_HELD;
            } else if (ok && tick.status == QA_MEDIA_ENDED)
                ok = finish(movie, QA_CINEMATIC_FINISHED, error);
        }
    }
    movie->busy = false;
    movie->faulted = !ok;
    if (!ok)
        return false;
    movie->dirty = false;
    *out = (qa_media_tick){movie->status, qa_cinematic_frame(movie), changed, looped};
    return true;
}
bool qa_cinematic_pause(qa_cinematic *movie, bool paused, qa_error *error) {
    if (!movie || movie->busy || movie->faulted || movie->restore_pending)
        return cinematic_fail(error, "Cannot pause active or failed cinematic");
    if ((paused && movie->status != QA_MEDIA_PLAYING) ||
        (!paused && movie->status != QA_MEDIA_PAUSED))
        return true;
    movie->busy = true;
    bool ok = pause_clock(movie, paused, error);
    if (ok) {
        movie->status = paused ? QA_MEDIA_PAUSED : QA_MEDIA_PLAYING;
        qa_audio_raw_pause(current_raw(movie), paused);
    }
    movie->busy = false;
    return ok;
}
bool qa_cinematic_end_playback(qa_cinematic *movie, qa_cinematic_end reason, qa_error *error) {
    if (!movie || movie->busy || movie->restore_pending || reason < QA_CINEMATIC_FINISHED || reason > QA_CINEMATIC_STOPPED)
        return cinematic_fail(error, "Cannot end active cinematic");
    movie->busy = true;
    bool ok = finish(movie, reason, error);
    movie->busy = false;
    return ok;
}
qa_media_status qa_cinematic_status(const qa_cinematic *movie) {
    return movie && !movie->restore_pending ? movie->status : QA_MEDIA_STOPPED;
}
qa_cinematic_target qa_cinematic_destination(const qa_cinematic *movie) {
    return movie && !movie->restore_pending ? movie->options.target :
        (qa_cinematic_target){.kind=(qa_cinematic_target_kind)-1};
}
const qa_media_frame *qa_cinematic_frame(const qa_cinematic *movie) {
    return movie && !movie->restore_pending && movie->has_picture ? &movie->picture : NULL;
}
uint64_t qa_cinematic_revision(const qa_cinematic *movie) {
    return movie && !movie->restore_pending ? movie->revision : 0;
}
bool qa_cinematic_checkpoint_revision_read(const qa_cinematic *movie, uint64_t *out) {
    if (!movie || !out || movie->busy) return false;
    *out=movie->revision; return true;
}
bool qa_cinematic_time(qa_cinematic *movie, double *elapsed, double *source, uint64_t *loop,
                       qa_error *error) {
    if (!movie || !elapsed || !source || !loop || movie->busy || movie->restore_pending)
        return cinematic_fail(error, "Cannot query active cinematic time");
    movie->busy = true;
    bool ok = cinematic_elapsed(movie, elapsed, error);
    if (ok) {
        *source = movie->has_picture
                      ? movie->picture.source_ms + fmax(0, *elapsed - movie->picture.presentation_ms)
                      : *elapsed;
        *loop = movie->has_picture ? movie->picture.loop : 0;
    }
    movie->busy = false;
    return ok;
}
static bool capture(qa_cinematic *movie, qa_cinematic_checkpoint *out, qa_error *error) {
    qa_audio_raw_stream *raw = current_raw(movie);
    if (movie->raw_attached && (movie->format == QA_CINEMATIC_IMAGE || movie->completed ||
                               movie->options.silent || !movie->options.audio))
        return cinematic_fail(error, "Cinematic has an invalid shared audio attachment");
    qa_cinematic_checkpoint saved = {.format = movie->format,
                                     .target = movie->options.target,
                                     .audio_audience = movie->options.audio_audience,
                                     .status = movie->status,
                                     .decoder_status = movie->decoder_status,
                                     .revision = movie->revision,
                                     .audio_loop = movie->audio_loop,
                                     .loop = movie->options.loop,
                                     .hold = movie->options.hold,
                                     .silent = movie->options.silent,
                                     .paused = movie->paused,
                                     .dirty = movie->dirty,
                                     .completed = movie->completed,
                                     .focus_paused = movie->focus_paused,
                                     .audio_attached = raw != NULL};
    if (!cinematic_elapsed(movie, &saved.elapsed_ms, error))
        return false;
    size_t length = strlen(movie->name);
    saved.source = malloc(length + 1);
    if (!saved.source) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing cinematic source name");
        return false;
    }
    memcpy(saved.source, movie->name, length + 1);
    bool ok = false;
    switch (movie->format) {
    case QA_CINEMATIC_CIN:
        ok = qa_cin_playback_capture(movie->movie.cin, &saved.decoder.cin, error);
        break;
    case QA_CINEMATIC_ROQ:
        ok = qa_roq_playback_capture(movie->movie.roq, &saved.decoder.roq, error);
        break;
    case QA_CINEMATIC_OGV:
        ok = qa_ogv_playback_capture(movie->movie.ogv, &saved.decoder.ogv, error);
        break;
    case QA_CINEMATIC_IMAGE:
        ok = true;
        break;
    }
    if (ok && raw)
        ok = qa_audio_raw_checkpoint(raw, &saved.audio, error);
    if (!ok) {
        qa_cinematic_checkpoint_free(&saved);
        return false;
    }
    *out = saved;
    return true;
}
bool qa_cinematic_capture(qa_cinematic *movie, qa_cinematic_checkpoint *out, qa_error *error) {
    if (!movie || !out || movie->busy || movie->faulted || movie->restore_pending)
        return cinematic_fail(error, "Cannot checkpoint active or failed cinematic");
    movie->busy = true;
    bool ok = capture(movie, out, error);
    movie->busy = false;
    return ok;
}
void qa_cinematic_checkpoint_free(qa_cinematic_checkpoint *saved) {
    if (!saved)
        return;
    free(saved->source);
    qa_buffer_free(&saved->audio);
    switch (saved->format) {
    case QA_CINEMATIC_CIN:
        qa_cin_playback_checkpoint_free(&saved->decoder.cin);
        break;
    case QA_CINEMATIC_ROQ:
        qa_roq_playback_checkpoint_free(&saved->decoder.roq);
        break;
    case QA_CINEMATIC_OGV:
        qa_ogv_checkpoint_free(&saved->decoder.ogv);
        break;
    case QA_CINEMATIC_IMAGE:
        break;
    }
    memset(saved, 0, sizeof(*saved));
}

bool qa_cinematic_audio_rebind_ready(qa_cinematic *movie, qa_audio_engine *engine,
    uint64_t bus, qa_error *error)
{
    if (!movie || movie->busy || movie->faulted || movie->restore_pending ||
        (!engine && (!movie->options.silent && movie->format != QA_CINEMATIC_IMAGE)))
        return cinematic_fail(error, "Cinematic audio exchange requires idle qualified owners");
    if (!movie->raw_attached) return true;
    qa_audio_raw_stream *next = engine ? qa_audio_engine_bus_stream(engine, bus) : NULL;
    qa_audio_raw_stream *raw = current_raw(movie);
    if ((raw != NULL) != (next != NULL))
        return cinematic_fail(error, "Cinematic raw queue presence differs from restored engine");
    return true;
}

void qa_cinematic_audio_rebind(qa_cinematic *movie, qa_audio_engine *engine, uint64_t bus)
{
    if (!movie || movie->busy || movie->faulted || movie->restore_pending) return;
    movie->options.audio = engine; movie->options.audio_bus = bus;
}

bool qa_cinematic_frame_rebind_ready(const qa_cinematic *movie, const qa_scene_frame *current,
    qa_error *error)
{
    if (!movie || movie->busy || movie->faulted || movie->restore_pending || (movie->image_frame && movie->image_frame != current))
        return cinematic_fail(error, "Cinematic publication exchange requires an idle matching frame");
    return true;
}

void qa_cinematic_frame_rebind(qa_cinematic *movie, const qa_scene_frame *current,
    const qa_scene_frame *destination)
{
    if (!movie || movie->busy || movie->faulted || movie->restore_pending) return;
    if (movie->image_frame && movie->image_frame == current) movie->image_frame = destination;
}
