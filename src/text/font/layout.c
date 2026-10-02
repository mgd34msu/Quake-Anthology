#include "internal.h"
#include "qa/text.h"

#include <float.h>
#include <math.h>
#include <string.h>

typedef struct layout_cell {
    qa_font_glyph glyph;
    qa_scene_vec4 color;
    size_t source_offset;
    uint32_t codepoint;
    bool newline, tab, breakable;
} layout_cell;

typedef struct row_span {
    size_t first, end;
    float width;
} row_span;

static bool finite_color(qa_scene_vec4 color) {
    return isfinite(color.x) && isfinite(color.y) && isfinite(color.z) && isfinite(color.w);
}

static qa_scene_vec4 q3_color(uint32_t codepoint, float alpha) {
    static const qa_scene_vec4 colors[8] = {
        {0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 1, 0, 1},
        {0, 0, 1, 1}, {0, 1, 1, 1}, {1, 0, 1, 1}, {1, 1, 1, 1},
    };
    qa_scene_vec4 result = colors[(codepoint - (uint32_t)'0') & 7u];
    result.w = alpha;
    return result;
}

static float cell_scale(const layout_cell *cell, float line_height) {
    float source = cell->glyph.font ? cell->glyph.font->line_height : line_height;
    return line_height / fmaxf(1.0f, source);
}

static float cell_advance(const layout_cell *cell, float width, float line_height,
                          uint32_t tab_columns) {
    if (cell->tab) {
        float tab = line_height * (float)tab_columns;
        float remainder = fmodf(width, tab);
        return remainder != 0 ? tab - remainder : tab;
    }
    return cell->glyph.advance * cell_scale(cell, line_height);
}

static bool add_row(row_span *rows, size_t capacity, size_t *count, size_t first, size_t end,
                    float width, qa_error *error) {
    if (*count >= capacity)
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Text line storage overflow");
    rows[(*count)++] = (row_span){first, end, width};
    return true;
}

static bool wrap_segment(const layout_cell *cells, size_t first, size_t end, float maximum,
                         float line_height, uint32_t tabs, row_span *rows, size_t capacity,
                         size_t *row_count, qa_error *error) {
    if (first == end)
        return add_row(rows, capacity, row_count, first, end, 0, error);
    size_t start = first;
    while (start < end) {
        float width = 0, break_width = 0;
        size_t last_break = SIZE_MAX;
        bool emitted = false;
        for (size_t i = start; i < end; ++i) {
            float advance = cell_advance(&cells[i], width, line_height, tabs);
            if (maximum > 0 && width + advance > maximum && i > start) {
                if (last_break != SIZE_MAX) {
                    if (!add_row(rows, capacity, row_count, start, last_break, break_width, error))
                        return false;
                    start = last_break + 1;
                } else {
                    if (!add_row(rows, capacity, row_count, start, i, width, error))
                        return false;
                    start = i;
                }
                emitted = true;
                break;
            }
            if (cells[i].breakable) {
                last_break = i;
                break_width = width;
            }
            width += advance;
        }
        if (!emitted) {
            if (!add_row(rows, capacity, row_count, start, end, width, error))
                return false;
            break;
        }
    }
    return true;
}

bool qa_font_layout_build(const qa_font_selection *selection, const qa_font_layout_options *options,
                          qa_arena *arena, qa_font_layout *out, qa_error *error) {
    if (!selection || !selection->classic || !options || !arena || !out ||
        (!options->text.data && options->text.size) || !isfinite(options->scale) ||
        options->scale <= 0 || !finite_color(options->color) || (options->max_width < 0) ||
        !isfinite(options->max_width) || (options->line_height < 0) ||
        !isfinite(options->line_height) || options->alignment < QA_FONT_ALIGN_LEFT ||
        options->alignment > QA_FONT_ALIGN_RIGHT || options->color_codes < QA_FONT_COLOR_LITERAL ||
        options->color_codes > QA_FONT_COLOR_Q3 ||
        (selection->fallback_count && !selection->fallbacks))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid text layout request");
    float line_height = options->line_height > 0 ? options->line_height : 8.0f * options->scale;
    if (!isfinite(line_height) || line_height <= 0)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid text line height");
    uint32_t tabs = options->tab_columns ? options->tab_columns : 4u;
    if ((double)line_height * tabs > FLT_MAX)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Text tab width exceeds float range");
    size_t capacity = options->text.size == SIZE_MAX ? 0 : options->text.size + 1;
    if (!capacity || capacity > SIZE_MAX / sizeof(layout_cell) ||
        capacity > SIZE_MAX / sizeof(row_span) ||
        capacity > SIZE_MAX / sizeof(qa_font_positioned_glyph) ||
        capacity > SIZE_MAX / sizeof(qa_font_line))
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Text layout size overflow");
    layout_cell *cells =
        qa_arena_alloc(arena, capacity * sizeof(*cells), _Alignof(layout_cell), error);
    row_span *rows = qa_arena_alloc(arena, capacity * sizeof(*rows), _Alignof(row_span), error);
    if (!cells || !rows)
        return false;

    size_t cell_count = 0, glyph_limit_count = 0, at = 0;
    qa_scene_vec4 color = options->color;
    while (at < options->text.size) {
        size_t source_offset = at;
        uint32_t codepoint;
        if (!qa_utf8_next(options->text, &at, &codepoint))
            break;
        if (codepoint == 0)
            break;
        if (options->color_codes == QA_FONT_COLOR_Q3 && codepoint == '^' &&
            at < options->text.size) {
            size_t next_at = at;
            uint32_t next;
            if (qa_utf8_next(options->text, &next_at, &next) && next != 0 && next != '^') {
                if (!options->force_color)
                    color = q3_color(next, options->color.w);
                at = next_at;
                continue;
            }
        }
        if (codepoint == '\r')
            continue;
        if (codepoint == '\n') {
            cells[cell_count++] = (layout_cell){.newline = true, .source_offset = source_offset};
            continue;
        }
        if (options->max_glyphs && glyph_limit_count >= options->max_glyphs)
            break;
        uint32_t resolved = codepoint == '\t' ? ' ' : codepoint;
        qa_font_glyph glyph;
        if (!qa_font_resolve(selection, resolved, options->alternate, &glyph))
            return qa_font_fail(error, QA_ERROR_FORMAT, source_offset,
                                "Font selection cannot resolve replacement glyph");
        qa_scene_vec4 tint = color;
        if (glyph.baked_color)
            tint = (qa_scene_vec4){1, 1, 1, color.w};
        else if (options->alternate)
            tint = (qa_scene_vec4){0.85f, 0.65f, 0.12f, color.w};
        cells[cell_count++] = (layout_cell){
            .glyph = glyph,
            .color = tint,
            .source_offset = source_offset,
            .codepoint = codepoint,
            .tab = codepoint == '\t',
            .breakable = qa_unicode_whitespace(codepoint) && codepoint != '\r' && codepoint != '\n',
        };
        ++glyph_limit_count;
    }

    size_t row_count = 0, segment = 0;
    for (;;) {
        size_t end = segment;
        while (end < cell_count && !cells[end].newline)
            ++end;
        if (!wrap_segment(cells, segment, end, options->max_width, line_height, tabs, rows,
                          capacity, &row_count, error))
            return false;
        if (end == cell_count)
            break;
        segment = end + 1;
        if (segment > cell_count)
            break;
    }

    qa_font_positioned_glyph *glyphs = qa_arena_alloc(arena, capacity * sizeof(*glyphs),
                                                      _Alignof(qa_font_positioned_glyph), error);
    qa_font_line *lines =
        qa_arena_alloc(arena, row_count * sizeof(*lines), _Alignof(qa_font_line), error);
    if (!glyphs || !lines)
        return false;
    size_t glyph_count = 0;
    float widest = 0;
    for (size_t row = 0; row < row_count; ++row) {
        row_span span = rows[row];
        float available = options->max_width > 0 ? options->max_width : span.width;
        float pen = options->alignment == QA_FONT_ALIGN_CENTER  ? (available - span.width) * 0.5f
                    : options->alignment == QA_FONT_ALIGN_RIGHT ? available - span.width
                                                                : 0;
        size_t first_glyph = glyph_count;
        for (size_t i = span.first; i < span.end; ++i) {
            const layout_cell *cell = &cells[i];
            float relative = pen - (options->alignment == QA_FONT_ALIGN_LEFT ? 0
                                    : options->alignment == QA_FONT_ALIGN_CENTER
                                        ? (available - span.width) * 0.5f
                                        : available - span.width);
            float scale = cell_scale(cell, line_height);
            float baseline = (float)row * line_height +
                             (cell->glyph.font ? cell->glyph.font->ascent * scale : line_height);
            glyphs[glyph_count++] = (qa_font_positioned_glyph){
                .glyph = cell->glyph,
                .rect = {pen + cell->glyph.bearing_x * scale,
                         baseline - cell->glyph.bearing_y * scale, cell->glyph.width * scale,
                         cell->glyph.height * scale},
                .color = cell->color,
                .source_offset = cell->source_offset,
            };
            pen += cell_advance(cell, relative, line_height, tabs);
        }
        lines[row] = (qa_font_line){span.width, (float)row * line_height, first_glyph,
                                    glyph_count - first_glyph};
        if (span.width > widest)
            widest = span.width;
    }
    *out = (qa_font_layout){
        .seat = selection->seat,
        .width = widest,
        .height = (float)row_count * line_height,
        .line_height = line_height,
        .glyphs = glyphs,
        .glyph_count = glyph_count,
        .lines = lines,
        .line_count = row_count,
    };
    return true;
}
