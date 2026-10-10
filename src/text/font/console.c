#include "qa/console_draw.h"
#include "qa/text.h"

#include <float.h>
#include <math.h>
#include <string.h>

static bool console_fail(qa_error *error, qa_status status, const char *message) {
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

static bool font_metrics(const qa_font_selection *selection, uint32_t scalar, bool alternate,
                         qa_font_glyph *glyph, qa_font_info *info, qa_error *error) {
    if (!selection || !glyph || !info || !qa_font_resolve(selection, scalar, alternate, glyph) ||
        !qa_font_describe(glyph->font, info) || !(info->line_height > 0) ||
        !isfinite(info->line_height))
        return console_fail(error, QA_ERROR_ARGUMENT, "Console font cannot resolve glyph metrics");
    return true;
}

static bool text_scalar(uint32_t scalar) {
    if (scalar >= 32u && scalar <= 126u)
        return true;
    if (scalar < 160u || scalar > 0x10ffffu || (scalar >= 0xd800u && scalar <= 0xdfffu) ||
        qa_unicode_whitespace(scalar))
        return false;
    if (scalar >= 0x2000u && scalar <= 0x2bffu)
        return scalar >= 0x20d0u && scalar <= 0x20ffu;
    if (scalar >= 0x3000u && scalar <= 0x303fu)
        return false;
    if (scalar >= 0x1f000u && scalar <= 0x1faffu)
        return false;
    if ((scalar >= 0xe000u && scalar <= 0xf8ffu) || (scalar >= 0xf0000u && scalar <= 0xffffdu) ||
        (scalar >= 0x100000u && scalar <= 0x10fffdu))
        return false;
    return true;
}

static float cap_normalization(const qa_font_info *info, uint32_t scalar) {
    return text_scalar(scalar) && info->has_cap_ink && info->cap_height > 0 &&
                   isfinite(info->cap_height)
               ? 6.0f / info->cap_height
               : 1.0f;
}

static bool measure_resolved(const qa_font_glyph *glyph, const qa_font_info *info, uint32_t scalar,
                             float line_height, float cell_width, qa_console_glyph_metrics *out,
                             qa_error *error) {
    float normalization = cap_normalization(info, scalar);
    float natural_width = glyph->width * line_height / info->line_height * normalization;
    float fit = fminf(1.0f, cell_width / fmaxf(1.0f, natural_width));
    qa_console_glyph_metrics result = {
        .line_height = line_height * normalization * fit,
        .top = info->has_cap_ink && text_scalar(scalar)
                   ? info->cap_top * line_height / 8.0f * normalization * fit
                   : 0,
    };
    if (!(result.line_height > 0) || !isfinite(result.line_height) || !isfinite(result.top))
        return console_fail(error, QA_ERROR_ARGUMENT, "Console glyph metrics exceed float range");
    *out = result;
    return true;
}

bool qa_console_cell_width(const qa_font_selection *selection, float line_height, float *out,
                           qa_error *error) {
    if (!selection || !out || !(line_height > 0) || !isfinite(line_height))
        return console_fail(error, QA_ERROR_ARGUMENT, "Invalid console cell width request");
    qa_font_glyph glyph;
    qa_font_info info;
    if (!font_metrics(selection, 'M', false, &glyph, &info, error))
        return false;
    float width =
        ceilf(glyph.advance * line_height / info.line_height * cap_normalization(&info, 'M'));
    if (!(width > 0) || !isfinite(width))
        return console_fail(error, QA_ERROR_FORMAT, "Console font has no usable M advance");
    *out = width;
    return true;
}

bool qa_console_metrics_calculate(const qa_console_metrics_options *options,
                                  const qa_font_selection *font, qa_console_metrics *out,
                                  qa_error *error) {
    if (!options || !font || !out || !options->width || !options->height ||
        !isfinite(options->pixel_ratio))
        return console_fail(error, QA_ERROR_ARGUMENT, "Invalid console metrics request");
    double ratio = fmax(1.0, options->pixel_ratio);
    double automatic = fmax(2.0, floor((double)options->height / ratio / 300.0));
    bool explicit_scale = isfinite(options->requested_scale) && options->requested_scale > 0;
    double requested =
        explicit_scale ? fmax(1.0, fmin(4.0, round(options->requested_scale))) : automatic;
    double fit =
        explicit_scale
            ? fmax(1.0, floor(fmin((double)options->width / 40.0, (double)options->height / 16.0)))
            : fmax(1.0,
                   floor(fmin((double)options->width / 256.0, (double)options->height / 96.0)));
    double scale = fmax(1.0, fmin(fit, round(requested * ratio)));
    double line_height_value = scale * 8.0;
    if (!isfinite(scale) || scale > FLT_MAX || line_height_value > FLT_MAX)
        return console_fail(error, QA_ERROR_ARGUMENT, "Console metrics exceed float range");
    float line_height = (float)line_height_value;
    float cell_width;
    if (!qa_console_cell_width(font, line_height, &cell_width, error))
        return false;
    double usable = (double)options->width - scale * 16.0;
    double columns = floor(usable / cell_width);
    size_t column_count;
    if (columns < 1)
        column_count = 1;
    else if (columns >= (double)SIZE_MAX)
        column_count = SIZE_MAX;
    else
        column_count = (size_t)columns;
    *out = (qa_console_metrics){(float)scale, line_height, cell_width, column_count};
    return true;
}

bool qa_console_glyph_measure(const qa_font_selection *selection, uint32_t scalar,
                              float line_height, float cell_width, qa_console_glyph_metrics *out,
                              qa_error *error) {
    if (!selection || !out || !(line_height > 0) || !isfinite(line_height) || !(cell_width > 0) ||
        !isfinite(cell_width))
        return console_fail(error, QA_ERROR_ARGUMENT, "Invalid console glyph metrics request");
    qa_font_glyph glyph;
    qa_font_info info;
    if (!font_metrics(selection, scalar, false, &glyph, &info, error))
        return false;
    return measure_resolved(&glyph, &info, scalar, line_height, cell_width, out, error);
}

typedef struct draw_context {
    qa_scene_frame *frame;
    qa_scene_rect target;
    const qa_font_selection *font;
    float line_height, cell_width, clip_top, clip_bottom;
    qa_error *error;
} draw_context;

static bool clipped_picture(draw_context *context, const qa_font_glyph *glyph, qa_scene_rect_f rect,
                            qa_vec4 color) {
    if (!glyph->visible || !glyph->image || rect.width <= 0 || rect.height <= 0)
        return true;
    qa_vec4 uv = glyph->uv;
    float bottom = rect.y + rect.height;
    if (!isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) ||
        !isfinite(bottom))
        return console_fail(context->error, QA_ERROR_ARGUMENT, "Console glyph exceeds float range");
    if (bottom <= context->clip_top || rect.y >= context->clip_bottom)
        return true;
    if (rect.y < context->clip_top) {
        float fraction = (context->clip_top - rect.y) / rect.height;
        uv.y += (uv.w - uv.y) * fraction;
        rect.height -= context->clip_top - rect.y;
        rect.y = context->clip_top;
    }
    bottom = rect.y + rect.height;
    if (bottom > context->clip_bottom) {
        float fraction = (context->clip_bottom - rect.y) / rect.height;
        uv.w = uv.y + (uv.w - uv.y) * fraction;
        rect.height = context->clip_bottom - rect.y;
    }
    return qa_scene_frame_picture_f(context->frame, glyph->image, context->target, rect, uv, color,
                                    context->error);
}

static bool draw_scalar(draw_context *context, uint32_t scalar, float x, float y,
                        qa_vec4 color, bool alternate) {
    qa_font_glyph glyph;
    qa_font_info info;
    if (!font_metrics(context->font, scalar, alternate, &glyph, &info, context->error))
        return false;
    qa_console_glyph_metrics metrics;
    if (!measure_resolved(&glyph, &info, scalar, context->line_height, context->cell_width,
                          &metrics, context->error))
        return false;
    float scale = metrics.line_height / info.line_height;
    float advance = glyph.advance * scale;
    qa_scene_rect_f rect = {
        x + (context->cell_width - advance) * 0.5f + glyph.bearing_x * scale,
        y - metrics.top + (info.ascent - glyph.bearing_y) * scale,
        glyph.width * scale,
        glyph.height * scale,
    };
    qa_vec4 tint = color;
    if (glyph.baked_color)
        tint = (qa_vec4){1, 1, 1, color.w};
    else if (alternate)
        tint = (qa_vec4){0.85f, 0.65f, 0.12f, color.w};
    return clipped_picture(context, &glyph, rect, tint);
}

static uint32_t clean_scalar(uint32_t scalar) {
    return scalar == '\r' || scalar == '\n' || scalar == '\t' ? ' ' : scalar;
}

static size_t scalar_count(const char *text) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    size_t at = 0, count = 0;
    uint32_t scalar;
    while (qa_utf8_next(bytes, &at, &scalar) && scalar)
        ++count;
    return count;
}

static bool draw_string_range(draw_context *context, const char *text, size_t first, size_t maximum,
                              float x, float y, qa_vec4 color, size_t *drawn) {
    qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
    size_t at = 0, index = 0, count = 0;
    uint32_t scalar;
    while (count < maximum && qa_utf8_next(bytes, &at, &scalar) && scalar) {
        if (index++ < first)
            continue;
        if (!draw_scalar(context, clean_scalar(scalar), x + (float)count * context->cell_width, y,
                         color, false))
            return false;
        ++count;
    }
    if (drawn)
        *drawn = count;
    return true;
}

static bool draw_fitted(draw_context *context, const char *text, size_t maximum, bool ellipsis,
                        float x, float y, qa_vec4 color) {
    size_t count = scalar_count(text);
    size_t content = count < maximum ? count : maximum;
    size_t dots = 0;
    if (count > maximum && ellipsis && maximum >= 3) {
        content = maximum - 3;
        dots = 3;
    }
    if (!draw_string_range(context, text, 0, content, x, y, color, NULL))
        return false;
    for (size_t i = 0; i < dots; ++i)
        if (!draw_scalar(context, '.', x + (float)(content + i) * context->cell_width, y, color,
                         false))
            return false;
    return true;
}

static size_t cells_that_fit(double width, double cell_width) {
    if (!(width > 0) || !(cell_width > 0))
        return 0;
    double count = floor(width / cell_width);
    return count >= (double)SIZE_MAX ? SIZE_MAX : (size_t)count;
}

static bool joined(qa_arena *arena, const char *prefix, const char *value, char **out,
                   qa_error *error) {
    size_t a = strlen(prefix), b = strlen(value);
    if (a > SIZE_MAX - b - 1)
        return console_fail(error, QA_ERROR_MEMORY, "Console help text size overflow");
    char *text = qa_arena_alloc(arena, a + b + 1, _Alignof(char), error);
    if (!text)
        return false;
    memcpy(text, prefix, a);
    memcpy(text + a, value, b + 1);
    *out = text;
    return true;
}

static size_t quoted_size(const char *value, bool *overflow) {
    size_t size = 2;
    for (const unsigned char *at = (const unsigned char *)value; *at; ++at) {
        size_t add = *at < 32 ? 6 : (*at == '"' || *at == '\\' ? 2 : 1);
        if (size > SIZE_MAX - add) {
            *overflow = true;
            return 0;
        }
        size += add;
    }
    return size;
}

static bool current_line(qa_arena *arena, const char *value, char **out, qa_error *error) {
    static const char prefix[] = "Current: ";
    bool overflow = false;
    size_t quoted = quoted_size(value, &overflow);
    if (overflow || quoted > SIZE_MAX - sizeof(prefix))
        return console_fail(error, QA_ERROR_MEMORY, "Console help text size overflow");
    size_t bytes = sizeof(prefix) - 1 + quoted + 1;
    char *text = qa_arena_alloc(arena, bytes, _Alignof(char), error);
    if (!text)
        return false;
    size_t at = 0;
    memcpy(text, prefix, sizeof(prefix) - 1);
    at += sizeof(prefix) - 1;
    text[at++] = '"';
    static const char hex[] = "0123456789abcdef";
    for (const unsigned char *source = (const unsigned char *)value; *source; ++source) {
        if (*source == '"' || *source == '\\') {
            text[at++] = '\\';
            text[at++] = (char)*source;
        } else if (*source < 32) {
            text[at++] = '\\';
            text[at++] = 'u';
            text[at++] = '0';
            text[at++] = '0';
            text[at++] = hex[*source >> 4];
            text[at++] = hex[*source & 15u];
        } else {
            text[at++] = (char)*source;
        }
    }
    text[at++] = '"';
    text[at] = 0;
    *out = text;
    return true;
}

static bool help_lines(qa_scene_frame *frame, const qa_console_discovery_entry *entry,
                       const char *lines[3], size_t *count, qa_error *error) {
    *count = 0;
    if (!entry)
        return true;
    if (entry->kind < QA_CONSOLE_COMMAND || entry->kind > QA_CONSOLE_CVAR || !entry->name)
        return console_fail(error, QA_ERROR_ARGUMENT, "Invalid selected console entry");
    const char *usage = entry->documentation ? entry->documentation->usage : NULL;
    if (usage) {
        char *line;
        if (!joined(&frame->storage, "Usage: ", usage, &line, error))
            return false;
        lines[(*count)++] = line;
    } else {
        lines[(*count)++] = entry->name;
    }
    if (entry->summary)
        lines[(*count)++] = entry->summary;
    if (entry->kind == QA_CONSOLE_CVAR) {
        char *line;
        if (!current_line(&frame->storage, entry->value ? entry->value : "", &line, error))
            return false;
        lines[(*count)++] = line;
    } else if (entry->kind == QA_CONSOLE_ALIAS) {
        char *line;
        if (!joined(&frame->storage, "Expands to: ", entry->value ? entry->value : "", &line,
                    error))
            return false;
        lines[(*count)++] = line;
    }
    return true;
}

static bool draw_field(draw_context *context, const qa_field_view *field, const char *prompt, double now, float x,
                       float y, float available) {
    size_t prefix = scalar_count(prompt);
    size_t scroll = field->scroll < field->cursor ? field->scroll : field->cursor;
    double occupied = ((double)(field->cursor - scroll) + (double)prefix + 1.0) * context->cell_width;
    while (scroll < field->cursor && occupied > available) {
        ++scroll;
        occupied -= context->cell_width;
    }
    size_t maximum = 0;
    double room = available - ((double)prefix + 1.0) * context->cell_width;
    if (room > 0)
        maximum = cells_that_fit(room, context->cell_width);
    const qa_vec4 white = {1, 1, 1, 1};
    if (!draw_string_range(context, prompt, 0, prefix, x, y, white, NULL) ||
        !draw_string_range(context, field->text, scroll, maximum, x + (float)prefix * context->cell_width, y, white,
                           NULL))
        return false;
    double phase = fmod(trunc(now / 256.0), 2.0);
    if (phase == 0 &&
        !draw_scalar(context, field->overstrike ? '_' : '|',
                     x + (float)((double)(field->cursor - scroll) + (double)prefix) * context->cell_width, y,
                     white, false))
        return false;
    return true;
}

bool qa_console_draw(qa_scene_frame *frame, const qa_console_draw_options *options,
                     qa_error *error) {
    if (!frame || !options || !options->font ||
        (!options->buffer && (!options->prompt || !options->field)) || !options->target.width ||
        !options->target.height || !isfinite(options->height) || options->height < 0 ||
        !isfinite(options->scale) || !(options->scale > 0) || !isfinite(options->cell_width) ||
        options->cell_width < 0 || !isfinite(options->now_milliseconds) ||
        !isfinite(options->notify_milliseconds) || !isfinite(options->field_scale) || options->field_scale < 0 ||
        (options->field &&
         (!options->field->text || options->field->cursor > options->field->length ||
          options->field->scroll > options->field->length)))
        return console_fail(error, QA_ERROR_ARGUMENT, "Invalid console draw request");
    float line_height = options->scale * 8.0f;
    float cell_width = options->cell_width;
    if (!isfinite(line_height) || !(line_height > 0))
        return console_fail(error, QA_ERROR_ARGUMENT, "Console line height exceeds float range");
    if (cell_width == 0 && !qa_console_cell_width(options->font, line_height, &cell_width, error))
        return false;
    double visible_height = fmin((double)options->height, options->target.height);
    double bottom = fmax(0.0, visible_height - options->scale * 2.0);
    double rows_value = floor(bottom / line_height);
    size_t total_rows = rows_value >= (double)SIZE_MAX ? SIZE_MAX : (size_t)rows_value;
    size_t field_lines = options->field ? 1 : 0;

    const char *help[3] = {0};
    size_t help_count = 0;
    if (options->field && !help_lines(frame, options->selected, help, &help_count, error))
        return false;
    size_t help_limit = total_rows > field_lines ? total_rows - field_lines : 0;
    if (help_count > help_limit)
        help_count = help_limit;

    if (options->background && visible_height > 0) {
        qa_scene_rect_f background = {(float)options->target.x, (float)options->target.y,
                                      (float)options->target.width, (float)visible_height};
        if (!qa_scene_frame_picture_f(frame, options->background, options->target, background,
                                      (qa_vec4){0, 0, 1, 1}, (qa_vec4){1, 1, 1, 1},
                                      error))
            return false;
    }
    if (total_rows < field_lines)
        return true;

    size_t maximum_rows = total_rows - field_lines - help_count;
    size_t first = 0, row_count = 0;
    if (options->buffer) {
        if (options->notify_rows) {
            size_t count = qa_console_buffer_count(options->buffer);
            qa_console_row current;
            if (count && qa_console_buffer_row(options->buffer, count - 1, &current) && !current.count)
                --count;
            row_count = count < options->notify_rows ? count : options->notify_rows;
            first = count - row_count;
        } else {
            row_count = qa_console_buffer_visible(options->buffer, maximum_rows, &first);
        }
    }
    draw_context context = {
        .frame = frame,
        .target = options->target,
        .font = options->font,
        .line_height = line_height,
        .cell_width = cell_width,
        .clip_top = (float)options->target.y,
        .clip_bottom = (float)((double)options->target.y + visible_height),
        .error = error,
    };
    float margin = options->scale * 8.0f;
    float available = fmaxf(1.0f, (float)options->target.width - margin * 2.0f);
    size_t maximum_cells = cells_that_fit(available, cell_width);
    float y = options->notify_rows ? (float)options->target.y :
        (float)((double)options->target.y + bottom -
                      (double)line_height * ((double)row_count + (double)field_lines + (double)help_count));
    static const qa_vec4 colors[8] = {
        {0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 1, 0, 1},
        {0, 0, 1, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 1, 1},
    };
    float x0 = (float)options->target.x + margin;
    for (size_t index = 0; index < row_count; ++index) {
        qa_console_row row;
        if (!qa_console_buffer_row(options->buffer, first + index, &row) ||
            (row.count && !row.cells))
            return console_fail(error, QA_ERROR_ARGUMENT, "Console buffer returned an invalid row");
        if (options->notify_rows && (row.time_ms == 0 || !qa_console_row_notifies(&row,
            options->now_milliseconds, options->notify_milliseconds))) continue;
        size_t cell_count = row.count < maximum_cells ? row.count : maximum_cells;
        for (size_t column = 0; column < cell_count; ++column) {
            const qa_console_cell *cell = &row.cells[column];
            qa_vec4 color = cell->color < 8 ? colors[cell->color] : colors[7];
            if (!draw_scalar(&context, cell->scalar, x0 + (float)column * cell_width, y, color,
                             cell->alternate))
                return false;
        }
        y += line_height;
    }
    if (options->field) {
        if (options->field_scale > 0) {
            float ratio = options->field_scale / options->scale;
            context.line_height *= ratio;
            context.cell_width *= ratio;
            available = fmaxf(1.0f, (float)options->target.width - options->field_scale * 16.0f);
        }
        if (!draw_field(&context, options->field, options->prompt ? options->prompt : "]", options->now_milliseconds, x0, y, available))
            return false;
        y += line_height;
        const qa_vec4 help_color = {0.65f, 0.85f, 1, 1};
        for (size_t i = 0; i < help_count; ++i) {
            if (!draw_fitted(&context, help[i], maximum_cells, true, x0, y, help_color))
                return false;
            y += line_height;
        }
    }
    return true;
}
