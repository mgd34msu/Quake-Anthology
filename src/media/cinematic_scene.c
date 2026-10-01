#include "cinematic_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_cinematic_image(qa_cinematic *movie, qa_scene_resources *resources, qa_scene_frame *frame,
                        const qa_scene_image **out, qa_error *error) {
    if (!movie || !resources || !frame || !out || movie->busy || movie->faulted || movie->restore_pending)
        return cinematic_fail(error, "Cinematic image publication is unavailable");
    if (!movie->image || movie->image_revision != movie->revision) {
        static const uint8_t black[4] = {0, 0, 0, 255};
        qa_scene_image_level level =
            movie->has_picture
                ? (qa_scene_image_level){movie->picture.width, movie->picture.height,
                                         movie->picture.rgba.data, movie->picture.rgba.size}
                : (qa_scene_image_level){1, 1, black, sizeof(black)};
        qa_scene_image *next = NULL;
        bool same_dimensions = movie->image && movie->image->levels[0].width == level.width &&
                               movie->image->levels[0].height == level.height;
        if (same_dimensions) {
            if (!qa_scene_image_replace(resources, movie->image, 0, &level, &next, error))
                return false;
        } else if (!qa_scene_image_create(resources, movie->name, QA_SCENE_RGBA8, &level, 1,
                                          QA_SCENE_CLAMP, QA_SCENE_LINEAR,
                                          (qa_scene_vec4){0, 0, 0, 1}, &next, error))
            return false;
        /* Do not advance publication until the command and its retained image
         * reference have both been admitted to the frame. */
        if (!qa_scene_frame_image(frame, next, error)) {
            qa_scene_image_release(next);
            return false;
        }
        qa_scene_image_release(movie->image);
        movie->image = next;
        movie->image_revision = movie->revision;
        movie->image_frame = frame;
        movie->image_sequence = frame->sequence;
    } else if (movie->image_frame != frame || movie->image_sequence != frame->sequence) {
        if (!qa_scene_frame_image(frame, movie->image, error))
            return false;
        movie->image_frame = frame;
        movie->image_sequence = frame->sequence;
    }
    *out = movie->image;
    return true;
}
bool qa_cinematic_fullscreen(qa_cinematic *movie, qa_cinematic_focus focus, qa_scene_rect viewport,
                             qa_scene_resources *resources, qa_scene_frame *frame, bool *blank,
                             qa_error *error) {
    if (!movie || !blank || !resources || !frame || movie->busy || movie->faulted || movie->restore_pending ||
        !viewport.width || !viewport.height ||
        movie->options.target.kind != QA_CINEMATIC_SEAT || focus < QA_CINEMATIC_GAME ||
        focus > QA_CINEMATIC_MENU)
        return cinematic_fail(error, "Invalid fullscreen cinematic target");
    if (focus != QA_CINEMATIC_GAME && movie->status == QA_MEDIA_PLAYING) {
        if (!qa_cinematic_pause(movie, true, error))
            return false;
        movie->focus_paused = true;
    } else if (focus == QA_CINEMATIC_GAME && movie->focus_paused) {
        if (!qa_cinematic_pause(movie, false, error))
            return false;
        movie->focus_paused = false;
    }
    qa_media_tick tick;
    if (!qa_cinematic_tick(movie, &tick, error))
        return false;
    bool visible = focus != QA_CINEMATIC_MENU && tick.status != QA_MEDIA_ENDED &&
                   tick.status != QA_MEDIA_STOPPED && tick.frame;
    if (visible) {
        const qa_scene_image *image;
        if (!qa_cinematic_image(movie, resources, frame, &image, error) ||
            !qa_scene_frame_picture(frame, image, viewport, viewport, (qa_scene_vec4){0, 0, 1, 1},
                                    (qa_scene_vec4){1, 1, 1, 1}, error))
            return false;
    } else {
        qa_scene_command clear = {.kind = QA_SCENE_COMMAND_VIEW,
                                  .data.view = {.viewport = viewport,
                                                .clear_color = true,
                                                .color = {0, 0, 0, 1},
                                                .seat = movie->options.target.id.seat}};
        if (!qa_scene_frame_emit(frame, &clear, error))
            return false;
    }
    *blank = !visible;
    return true;
}
bool qa_cinematic_pixel_rect(qa_scene_rect_f rect, qa_scene_rect viewport, qa_scene_rect_f *out,
                             qa_error *error) {
    if (!out || !isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) ||
        !isfinite(rect.height))
        return cinematic_fail(error, "Invalid cinematic source rectangle");
    float sx = (float)viewport.width / 640, sy = (float)viewport.height / 480;
    qa_scene_rect_f result = {(float)viewport.x + truncf(rect.x * sx),
                              (float)viewport.y + truncf(rect.y * sy), truncf(rect.width * sx),
                              truncf(rect.height * sy)};
    if (!isfinite(result.x) || !isfinite(result.y) || !isfinite(result.width) ||
        !isfinite(result.height))
        return cinematic_fail(error, "Cinematic destination rectangle overflow");
    *out = result;
    return true;
}
bool qa_cinematic_transition_apply(const qa_cinematic_transition *transition,
                                   qa_cinematic_end reason,
                                   const qa_cinematic_transition_host *host, qa_error *error) {
    if (!transition || !host || !host->leave || transition->seat >= 4 ||
        transition->kind < QA_CINEMATIC_RETURN || transition->kind > QA_CINEMATIC_Q3_NEXTMAP ||
        reason < QA_CINEMATIC_FINISHED || reason > QA_CINEMATIC_STOPPED ||
        (transition->kind == QA_CINEMATIC_Q2_NEXTSERVER && !host->send) ||
        (transition->kind == QA_CINEMATIC_Q3_NEXTMAP && (!host->append || !transition->command)))
        return cinematic_fail(error, "Invalid cinematic completion transition");
    char next_server[48];
    char *command = NULL;
    if (reason != QA_CINEMATIC_STOPPED && transition->kind == QA_CINEMATIC_Q3_NEXTMAP &&
        transition->command[0]) {
        size_t length = strlen(transition->command);
        if (length > SIZE_MAX - 2)
            return cinematic_fail(error, "Cinematic transition command overflow");
        command = malloc(length + 2);
        if (!command) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating cinematic transition command");
            return false;
        }
        memcpy(command, transition->command, length);
        command[length] = '\n';
        command[length + 1] = 0;
    }
    /* Completion owns a snapshot. A seat callback may invalidate the source
     * command storage while leaving the cinematic. */
    qa_cinematic_transition saved = *transition;
    qa_cinematic_transition_host callbacks = *host;
    snprintf(next_server, sizeof(next_server), "nextserver %d\n", saved.server_count);
    bool ok = callbacks.leave(callbacks.context, saved.seat, error);
    if (ok && reason != QA_CINEMATIC_STOPPED) {
        if (saved.kind == QA_CINEMATIC_Q2_NEXTSERVER)
            ok = callbacks.send(callbacks.context, saved.seat, next_server, error);
        else if (command)
            ok = callbacks.append(callbacks.context, saved.seat, command, error);
    }
    free(command);
    return ok;
}
