#include "qa/tools.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *text) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
static bool valid_frame(const qa_image *image, qa_error *error) {
    if (!image || !image->width || !image->height || image->width > SIZE_MAX / image->height / 4 ||
        !image->rgba.data || image->rgba.size != (size_t)image->width * image->height * 4)
        return fail(error, "renderer returned invalid RGBA frame dimensions");
    return true;
}
void qa_capture_result_free(qa_capture_result *result) { if (result) { free(result->path); *result = (qa_capture_result){0}; } }
bool qa_capture_encode(const qa_image *image, qa_capture_format format, int quality, qa_buffer *out, qa_error *error) {
    if (!out || !valid_frame(image, error)) return false;
    switch (format) {
    case QA_CAPTURE_TGA: return qa_image_encode_tga(image, out, error);
    case QA_CAPTURE_PNG: return qa_image_encode_png(image, out, error);
    case QA_CAPTURE_JPEG: return qa_image_encode_jpeg(image, quality, false, out, error);
    }
    return fail(error, "invalid screenshot format");
}
bool qa_capture_levelshot(const qa_image *image, const uint8_t gamma[256], qa_image *out, qa_error *error) {
    if (!out || !valid_frame(image, error)) return false;
    qa_image shot = {.width = 128, .height = 128, .srgb_intent = -1};
    shot.rgba.size = 128 * 128 * 4; shot.rgba.data = malloc(shot.rgba.size);
    if (!shot.rgba.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating levelshot"); return false; }
    float xs = (float)image->width / 512, ys = (float)image->height / 384;
    for (uint32_t y = 0; y < 128; ++y) for (uint32_t x = 0; x < 128; ++x) {
        unsigned sum[3] = {0};
        for (uint32_t yy = 0; yy < 3; ++yy) for (uint32_t xx = 0; xx < 4; ++xx) {
            uint32_t sx = (uint32_t)fmin((double)image->width - 1, trunc((float)(x * 4 + xx) * xs));
            uint32_t sy = (uint32_t)fmin((double)image->height - 1, trunc((float)(y * 3 + yy) * ys));
            const uint8_t *pixel = image->rgba.data + ((size_t)sy * image->width + sx) * 4;
            for (size_t c = 0; c < 3; ++c) sum[c] += pixel[c];
        }
        uint8_t *pixel = shot.rgba.data + ((size_t)y * 128 + x) * 4;
        for (size_t c = 0; c < 3; ++c) { unsigned average = sum[c] / 12; pixel[c] = gamma ? gamma[average] : (uint8_t)average; }
        pixel[3] = 255;
    }
    *out = shot; return true;
}
static char *path_alloc(const char *directory, const char *name, size_t length, const char *extension, qa_error *error) {
    size_t a = strlen(directory), b = strlen(extension);
    if (length > SIZE_MAX - a - b - 2) { fail(error, "capture filename overflow"); return NULL; }
    char *path = malloc(a + length + b + 2);
    if (!path) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating capture filename"); return NULL; }
    memcpy(path, directory, a); memcpy(path + a, name, length); path[a + length] = '.';
    memcpy(path + a + length + 1, extension, b + 1); return path;
}
bool qa_capture_save(qa_vfs *files, qa_mount_id mount, const qa_image *image, qa_capture_format format,
                      const char *name, int quality, qa_capture_result *out, qa_error *error) {
    if (!files || !mount || !out) return fail(error, "invalid screenshot destination");
    qa_buffer bytes = {0};
    if (!qa_capture_encode(image, format, quality, &bytes, error)) return false;
    const char *extension = format == QA_CAPTURE_TGA ? "tga" : format == QA_CAPTURE_PNG ? "png" : "jpg";
    char *path = NULL; bool success = false;
    if (name) {
        path = path_alloc("screenshots/", name, strlen(name), extension, error);
        if (!path || !qa_vfs_write(files, mount, path, (qa_bytes){bytes.data, bytes.size}, error)) goto done;
    } else {
        for (unsigned i = 0; i < 10000; ++i) {
            char name_buffer[16]; (void)snprintf(name_buffer, sizeof name_buffer, "shot%04u", i);
            path = path_alloc("screenshots/", name_buffer, strlen(name_buffer), extension, error);
            if (!path) goto done;
            bool created = false;
            if (!qa_vfs_write_exclusive(files, mount, path, (qa_bytes){bytes.data, bytes.size}, &created, error)) goto done;
            if (created) break;
            free(path); path = NULL;
        }
        if (!path) { fail(error, "no free screenshot filename between shot0000 and shot9999"); goto done; }
    }
    *out = (qa_capture_result){path, image->width, image->height, bytes.size}; path = NULL; success = true;
done:
    free(path); qa_buffer_free(&bytes); return success;
}
bool qa_capture_save_levelshot(qa_vfs *files, qa_mount_id mount, const qa_image *source, const char *map,
                                const uint8_t gamma[256], qa_capture_result *out, qa_error *error) {
    if (!files || !mount || !map || !out) return fail(error, "invalid levelshot destination");
    if (!strncmp(map, "maps/", 5)) map += 5;
    size_t n = strlen(map); if (n >= 4 && !strcmp(map + n - 4, ".bsp")) n -= 4;
    char *path = path_alloc("levelshots/", map, n, "tga", error);
    if (!path) return false;
    qa_image shot = {0}; qa_buffer bytes = {0}; bool success = false;
    if (!qa_capture_levelshot(source, gamma, &shot, error) || !qa_image_encode_tga(&shot, &bytes, error) ||
        !qa_vfs_write(files, mount, path, (qa_bytes){bytes.data, bytes.size}, error)) goto done;
    *out = (qa_capture_result){path, shot.width, shot.height, bytes.size}; path = NULL; success = true;
done:
    free(path); qa_buffer_free(&bytes); qa_image_free(&shot); return success;
}
bool qa_capture_frame_time(double elapsed, double fps_value, float timescale, bool active, bool force,
                            qa_capture_clock *out, qa_error *error) {
    if (!out || !isfinite(elapsed) || !isfinite(fps_value) || !isfinite(timescale) || elapsed < 0 || fps_value < 0 || timescale < 0)
        return fail(error, "invalid capture clock");
    double fps = trunc(fps_value);
    if (fps == 0 || elapsed == 0) { *out = (qa_capture_clock){elapsed, false}; return true; }
    float milliseconds = (float)(trunc(1000 / fps) * timescale);
    if (!isfinite(milliseconds)) return fail(error, "capture clock exceeds native range");
    *out = (qa_capture_clock){fmax(1, trunc(milliseconds)), active || force}; return true;
}
