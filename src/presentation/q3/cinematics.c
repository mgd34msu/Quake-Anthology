#include "internal.h"

bool q3p_movie_close(qa_q3_presentation *p, uint32_t index, qa_cinematic_end reason, qa_error *error)
{
    q3p_movie movie = p->movies[index];
    p->movies[index] = (q3p_movie){0};
    bool ok = true;
    if (movie.kind == Q3P_MOVIE_SYSTEM) {
        ok = movie.system.end(movie.system.context, reason, error);
        movie.system.release(movie.system.context);
    } else if (movie.kind == Q3P_MOVIE_LOCAL) {
        qa_cinematic_destroy(movie.local);
        qa_cinematic_asset_release(movie.asset);
    }
    free(movie.path); return ok;
}

char *q3p_movie_path(const char *path, qa_error *error)
{
    size_t length = strlen(path);
    bool prefix = strchr(path, '/') == NULL;
    const char *slash = strrchr(path, '/'), *dot = strrchr(path, '.');
    bool extension = dot && (!slash || dot > slash) && dot[1];
    size_t before = prefix ? 6u : 0u, after = extension ? 0u : 4u;
    if (length > SIZE_MAX - before - after - 1) {
        q3p_fail(error, QA_ERROR_MEMORY, "cinematic path exceeds capacity"); return NULL;
    }
    char *name = malloc(before + length + after + 1);
    if (!name) { q3p_fail(error, QA_ERROR_MEMORY, "retaining cinematic path"); return NULL; }
    if (prefix) memcpy(name, "video/", before);
    memcpy(name + before, path, length);
    if (after) memcpy(name + before + length, ".roq", after);
    name[before + length + after] = 0; return name;
}

static uint32_t free_slot(const qa_q3_presentation *p)
{
    uint32_t slot = 0;
    while (slot < 16 && p->movies[slot].kind != Q3P_MOVIE_EMPTY) ++slot;
    return slot;
}

static bool prepare(qa_q3_presentation *p, const char *request,
                      q3p_movie_source **out, qa_error *error)
{
    for (q3p_movie_source *source = p->movie_sources; source; source = source->next)
        if (!strcmp(source->request, request)) { *out = source; return true; }
    char *name = q3p_movie_path(request, error);
    if (!name) return false;
    qa_q3_presentation_assets *assets = p->options.assets;
    qa_resource *resource = NULL; qa_error local = {0};
    bool ok = qa_vfs_acquire(assets->options.provider.mounts, name, &resource, NULL, &local);
    bool missing = (!ok && local.code == QA_ERROR_NOT_FOUND) || (ok && !qa_resource_bytes(resource).size);
    qa_resource_release(resource);
    if (missing) { free(name); *out = NULL; return true; }
    if (!ok) { free(name); if (error) *error = local; return false; }
    size_t length = strlen(request);
    if (length > SIZE_MAX - sizeof(q3p_movie_source) - 1) {
        free(name); return q3p_fail(error, QA_ERROR_MEMORY, "cinematic request exceeds capacity");
    }
    q3p_movie_source *source = malloc(sizeof(*source) + length + 1);
    if (!source) { free(name); return q3p_fail(error, QA_ERROR_MEMORY, "retaining prepared cinematic"); }
    qa_cinematic_asset *asset = NULL;
    ok = qa_media_library_load(assets->options.movies, assets->options.provider.mounts, name, &asset, error);
    if (!ok) { free(name); free(source); return false; }
    *source = (q3p_movie_source){.next = p->movie_sources, .asset = asset, .path = name};
    memcpy(source->request, request, length + 1);
    p->movie_sources = source; *out = source; return true;
}

static bool play_system(qa_q3_presentation *p, const char *path, uint32_t flags,
                          int32_t *out, qa_error *error)
{
    if (!p->options.system_movie)
        return q3p_fail(error, QA_ERROR_UNSUPPORTED, "system cinematic transition owner is unavailable");
    uint32_t slot = free_slot(p);
    if (slot == 16) return q3p_fail(error, QA_ERROR_FORMAT, "CIN_HandleForVideo: none free");
    p->movies[slot].kind = Q3P_MOVIE_PENDING;
    qa_q3_system_movie movie = {0};
    qa_q3_movie_request request = {path, (flags & 2u) != 0, (flags & 4u) != 0, (flags & 8u) != 0};
    bool ok = p->options.system_movie(p->options.context, &request, &movie, error);
    if (ok && (!movie.status || !movie.end || !movie.release)) {
        if (movie.end) movie.end(movie.context, QA_CINEMATIC_STOPPED, NULL);
        if (movie.release) movie.release(movie.context);
        ok = q3p_fail(error, QA_ERROR_ARGUMENT, "system cinematic returned incomplete lifetime services");
    }
    p->movies[slot] = ok ? (q3p_movie){.kind = Q3P_MOVIE_SYSTEM, .system = movie, .flags = flags} : (q3p_movie){0};
    if (ok) *out = (int32_t)slot;
    return ok;
}

bool qa_q3_presentation_movie_play(qa_q3_presentation *p, const char *path, qa_scene_rect_f rect,
                                   uint32_t flags, int32_t *out, qa_error *error)
{
    if (!path || !out || !q3p_begin(p, error)) return false;
    if (flags & 1u) return q3p_end(p, play_system(p, path, flags, out, error));
    qa_q3_presentation_assets *assets = p->options.assets;
    if (!assets->options.movies)
        return q3p_end(p, q3p_fail(error, QA_ERROR_UNSUPPORTED, "shared cinematic asset owner is unavailable"));
    q3p_movie_source *prepared;
    if (!prepare(p, path, &prepared, error)) return q3p_end(p, false);
    if (!prepared) { *out = -1; return q3p_end(p, true); }
    for (uint32_t i = 0; i < 16; ++i) {
        if (p->movies[i].kind == Q3P_MOVIE_LOCAL && !strcmp(p->movies[i].path, prepared->path)) {
            *out = (int32_t)i; return q3p_end(p, true);
        }
    }
    uint32_t slot = free_slot(p);
    if (slot == 16) {
        return q3p_end(p, q3p_fail(error, QA_ERROR_FORMAT, "CIN_HandleForVideo: none free"));
    }
    if (!p->options.audio_bus) {
        return q3p_end(p, q3p_fail(error, QA_ERROR_UNSUPPORTED, "cinematic bus identity owner is unavailable"));
    }
    char *name = malloc(strlen(prepared->path) + 1);
    if (!name) return q3p_end(p, q3p_fail(error, QA_ERROR_MEMORY, "retaining cinematic playback name"));
    strcpy(name, prepared->path);
    qa_cinematic_asset *asset = prepared->asset;
    qa_cinematic_asset_retain(asset);
    uint64_t bus = p->options.audio_bus(p->options.context);
    qa_cinematic_options options = {.clock = p->options.clock,
        .target = {.kind = flags & 16u ? QA_CINEMATIC_MATERIAL : QA_CINEMATIC_SEAT},
        .loop = (flags & 2u) != 0, .hold = (flags & 4u) != 0, .silent = (flags & 8u) != 0,
        .audio = p->options.audio, .audio_bus = bus, .gain = 1,
        .audio_audience = {QA_CINEMATIC_AUDIO_SEAT, p->options.seat},
        .context = p->options.context, .diagnostic = p->options.print};
    if (flags & 16u) options.target.id.material = bus; else options.target.id.seat = p->options.seat;
    qa_cinematic_source source = qa_cinematic_asset_source(asset); source.name = name;
    qa_cinematic *movie = NULL;
    bool ok = qa_cinematic_create(&source, &options, NULL, &movie, error);
    if (ok) {
        p->movies[slot] = (q3p_movie){.kind = Q3P_MOVIE_LOCAL, .local = movie,
            .asset = asset, .rect = rect, .path = name, .flags = flags};
        *out = (int32_t)slot;
    } else { free(name); qa_cinematic_asset_release(asset); }
    return q3p_end(p, ok);
}

bool qa_q3_presentation_movie_run(qa_q3_presentation *p, int32_t handle,
                                  int32_t *out, qa_error *error)
{
    if (!out || !q3p_begin(p, error)) return false;
    if (handle < 0 || handle >= 16) { *out = 2; return q3p_end(p, true); }
    q3p_movie *movie = &p->movies[handle];
    qa_media_tick tick = {.status = QA_MEDIA_STOPPED};
    bool ok = true;
    if (movie->kind == Q3P_MOVIE_SYSTEM) tick.status = movie->system.status(movie->system.context);
    else if (movie->kind == Q3P_MOVIE_LOCAL) ok = qa_cinematic_tick(movie->local, &tick, error);
    if (ok) {
        if (tick.looped) *out = 5;
        else if (tick.status == QA_MEDIA_HELD) *out = 0;
        else if (tick.status == QA_MEDIA_PLAYING || tick.status == QA_MEDIA_PAUSED) *out = 1;
        else { *out = 2; ok = q3p_movie_close(p, (uint32_t)handle, QA_CINEMATIC_STOPPED, error); }
    }
    return q3p_end(p, ok);
}

bool qa_q3_presentation_movie_stop(qa_q3_presentation *p, int32_t handle, bool skip, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    return q3p_end(p, handle < 0 || handle >= 16 ||
        q3p_movie_close(p, (uint32_t)handle, skip ? QA_CINEMATIC_SKIPPED : QA_CINEMATIC_STOPPED, error));
}

bool qa_q3_presentation_movie_draw(qa_q3_presentation *p, int32_t handle, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    if (handle < 0 || handle >= 16 || p->movies[handle].kind != Q3P_MOVIE_LOCAL)
        return q3p_end(p, true);
    q3p_movie *movie = &p->movies[handle];
    if (!qa_cinematic_frame(movie->local)) return q3p_end(p, true);
    if (!p->frame) return q3p_end(p, q3p_fail(error, QA_ERROR_ARGUMENT, "cinematic draw has no scene frame"));
    const qa_scene_image *image; qa_scene_rect_f rect;
    bool ok = qa_cinematic_image(movie->local, p->options.assets->options.provider.images,
                                   p->frame, &image, error) &&
              qa_cinematic_pixel_rect(movie->rect, p->options.viewport, &rect, error) &&
              qa_scene_frame_picture_f(p->frame, image, p->options.viewport, rect,
                                         (qa_scene_vec4){0, 0, 1, 1}, p->color, error);
    return q3p_end(p, ok);
}

void qa_q3_presentation_movie_extents(qa_q3_presentation *p, int32_t handle, qa_scene_rect_f rect)
{
    qa_error ignored = {0};
    if (!q3p_begin(p, &ignored)) return;
    if (handle >= 0 && handle < 16 && p->movies[handle].kind == Q3P_MOVIE_LOCAL)
        p->movies[handle].rect = rect;
    q3p_end(p, true);
}
