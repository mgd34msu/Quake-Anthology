#include "qa/hud_q2.h"
#include "qa/tokenizer.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct layout_context {
    const qa_hud_q2_options *options;
    const qa_hud_q2_frame *frame;
    qa_scene_frame *scene;
    qa_error *error;
    qa_tokenizer parser;
    qa_hud_q2_table *table;
    float x, y, width, height;
    int32_t images, items, skins, max_images, max_strings, max_clients;
    bool rerelease, failed;
} layout_context;
static const qa_scene_vec4 white = {1, 1, 1, 1}, black = {0, 0, 0, 1};
static void fail(layout_context *c, const char *message) {
    if (!c->failed) qa_error_set(c->error, QA_ERROR_FORMAT, c->parser.offset, "%s", message);
    c->failed = true;
}
static char *allocate(layout_context *c, size_t bytes) {
    char *out = qa_arena_alloc(&c->scene->storage, bytes, 1, c->error);
    if (!out) c->failed = true;
    return out;
}
static const char *copy_span(layout_context *c, const char *text, size_t length) {
    if (length == SIZE_MAX) { fail(c, "HUD text is too long"); return ""; }
    char *out = allocate(c, length + 1);
    if (!out) return "";
    memcpy(out, text, length); out[length] = 0;
    return out;
}
static const char *copy(layout_context *c, const char *text) {
    return text ? copy_span(c, text, strlen(text)) : "";
}
static const char *config(layout_context *c, int32_t index) {
    return copy(c, c->options->configstring ? c->options->configstring(c->options->context, index) : NULL);
}
static const char *next(layout_context *c, bool *found) {
    qa_token token;
    if (!qa_tokenizer_next(&c->parser, &token, found, c->error)) { c->failed = true; return ""; }
    if (!*found) return "";
    char *out = allocate(c, token.text.size + 1);
    if (out && !qa_token_copy(&token, out, token.text.size + 1, c->error)) c->failed = true;
    return out ? out : "";
}
static const char *argument(layout_context *c) { bool found; return next(c, &found); }
static int32_t decimal(const char *p) {
    while (*p && (uint8_t)*p <= 32) ++p;
    bool negative = *p == '-';
    if (*p == '+' || *p == '-') ++p;
    uint32_t value = 0;
    while (*p >= '0' && *p <= '9') value = value * 10u + (uint32_t)(*p++ - '0');
    if (negative) value = 0u - value;
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}
static int32_t integer(layout_context *c) { return decimal(argument(c)); }
static double stat(layout_context *c, int32_t index) {
    if (index < 0 || (size_t)index >= c->frame->stat_count) { fail(c, "HUD stat outside playerstate"); return 0; }
    const qa_hud_q2_arsenal *a = c->frame->arsenal;
    if (a && a->has_ammunition && index == 2) return a->unlimited_ammunition ? 0 : 1;
    if (a && a->has_ammunition && index == 3) return a->unlimited_ammunition ? -1 : a->ammunition;
    return c->frame->stats[index];
}
static int32_t stat_int(layout_context *c, int32_t index) {
    double value = stat(c, index);
    if (value < INT32_MIN || value > INT32_MAX) { fail(c, "HUD integer stat exceeds source range"); return 0; }
    return (int32_t)value;
}
static bool quad(layout_context *c, const qa_scene_image *image, float x, float y, float w, float h,
                 qa_scene_vec4 uv, qa_scene_vec4 color) {
    if (c->failed || !image) return !c->failed;
    const qa_hud_q2_options *o = c->options;
    if (!qa_scene_frame_picture_f(c->scene, image, o->viewport,
        (qa_scene_rect_f){(float)o->viewport.x + x * o->scale, (float)o->viewport.y + y * o->scale,
                          w * o->scale, h * o->scale}, uv, color, c->error)) c->failed = true;
    return !c->failed;
}
static void fill(layout_context *c, float x, float y, float w, float h, qa_scene_vec4 color) {
    quad(c, c->options->white, x, y, w, h, (qa_scene_vec4){0, 0, 1, 1}, color);
}
static void picture(layout_context *c, const char *name, float x, float y, float w, float h, bool before) {
    if (c->failed || !*name || !c->options->picture) return;
    const qa_scene_image *image = c->options->picture(c->options->context, name, c->error);
    if (!image) return;
    if (before) x -= (float)image->logical_width + 2;
    quad(c, image, x, y, w > 0 ? w : (float)image->logical_width, h > 0 ? h : (float)image->logical_height,
         (qa_scene_vec4){0, 0, 1, 1}, white);
}
static void classic_glyph(layout_context *c, uint32_t code, float x, float y, bool shadow) {
    code &= 255;
    if ((code & 127) == 32) return;
    qa_font_glyph glyph;
    if (!qa_font_find_glyph(c->options->fonts.classic, code, &glyph) || !glyph.visible) return;
    if (shadow) quad(c, glyph.image, x + 1, y + 1, 8, 8, glyph.uv, black);
    quad(c, glyph.image, x, y, 8, 8, glyph.uv, white);
}
static size_t utf16_length(const char *value) {
    qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
    size_t cursor = 0, units = 0; uint32_t code;
    while (qa_utf8_next(bytes, &cursor, &code)) units += code > 0xffff ? 2 : 1;
    return units;
}
static void classic_text(layout_context *c, const char *value, float x, float y,
                         bool alternate, bool byte_xor, bool shadow) {
    uint32_t mask = alternate ? 128 : 0;
    if (byte_xor) {
        for (size_t i = 0; value[i]; ++i) classic_glyph(c, (uint8_t)value[i] ^ mask, x + (float)i * 8, y, shadow);
    } else {
        qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
        size_t cursor = 0, index = 0; uint32_t code;
        while (qa_utf8_next(bytes, &cursor, &code)) {
            if (code > 0xffff) {
                code -= 0x10000;
                classic_glyph(c, (0xd800 + (code >> 10)) | mask, x + (float)index++ * 8, y, shadow);
                code = 0xdc00 + (code & 1023);
            }
            classic_glyph(c, code | mask, x + (float)index++ * 8, y, shadow);
        }
    }
}
static bool font_layout(layout_context *c, const char *value, qa_font_layout *out) {
    qa_font_info info;
    const qa_font *font = c->options->fonts.primary ? c->options->fonts.primary : c->options->fonts.classic;
    if (!qa_font_describe(font, &info) || info.line_height <= 0) { fail(c, "HUD font unavailable"); return false; }
    qa_font_layout_options o = {.text = {(const uint8_t *)value, strlen(value)},
        .scale = c->options->font_line_height / info.line_height, .color = white,
        .line_height = c->options->font_line_height, .color_codes = QA_FONT_COLOR_LITERAL};
    if (!qa_font_layout_build(&c->options->fonts, &o, &c->scene->storage, out, c->error)) {
        c->failed = true; return false;
    }
    return true;
}
static float measure(layout_context *c, const char *value, bool force_font) {
    if (!c->rerelease) return (float)utf16_length(value) * 8;
    if (!c->options->use_font && !force_font) return (float)strlen(value) * 8;
    qa_font_layout layout;
    return font_layout(c, value, &layout) ? layout.width : 0;
}
static void text(layout_context *c, const char *value, bool alternate, float x, float y, bool force_font) {
    bool font = c->rerelease && (c->options->use_font || force_font);
    if (!font) { classic_text(c, value, x, y, alternate, c->rerelease, c->rerelease); return; }
    qa_font_layout layout;
    if (!font_layout(c, value, &layout)) return;
    qa_scene_vec4 color = alternate ? (qa_scene_vec4){112.f / 255, 1, 52.f / 255, 1} : white;
    float offset = (c->options->font_line_height - 8) / 2;
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < layout.glyph_count; ++i) {
            const qa_font_positioned_glyph *g = &layout.glyphs[i];
            if (!g->glyph.visible) continue;
            float shadow = pass == 0 ? 1 : 0;
            quad(c, g->glyph.image, x + g->rect.x + shadow, y - offset + g->rect.y + shadow,
                 g->rect.width, g->rect.height, g->glyph.uv,
                 pass == 0 ? black : g->glyph.baked_color ? white : color);
        }
    }
}
static void centered(layout_context *c, const char *value, bool alternate, float x, float y) {
    const char *line = value;
    for (;;) {
        const char *end = strchr(line, '\n');
        const char *part = end ? copy_span(c, line, (size_t)(end - line)) : line;
        float offset = (320 - measure(c, part, false)) / 2;
        text(c, part, alternate, x + (c->rerelease ? offset : truncf(offset)), y, false);
        if (!end) break;
        line = end + 1; y += c->rerelease && c->options->use_font ? 10 : 8;
    }
}
static const char *number_string(layout_context *c, double value, bool truncate) {
    char buffer[512] = {0};
    if (truncate) {
        if (!qa_format_fixed(trunc(value), 0, buffer, sizeof buffer, c->error)) c->failed = true;
    } else if (!qa_format_number(value, buffer, c->error)) c->failed = true;
    return copy(c, buffer);
}
static void field(layout_context *c, double value, int32_t digits, bool alternate) {
    if (digits > 5) digits = 5;
    if (digits < 1) return;
    const char *s = number_string(c, value, true);
    size_t length = strlen(s); if (length > (size_t)digits) length = (size_t)digits;
    float x = c->x + 2 + (float)(16 * (digits - (int32_t)length));
    for (size_t i = 0; i < length; ++i, x += 16) {
        char name[32];
        if (s[i] == '-') snprintf(name, sizeof name, "%s_minus", alternate ? "anum" : "num");
        else snprintf(name, sizeof name, "%s_%c", alternate ? "anum" : "num", s[i]);
        picture(c, name, x, c->y, 0, 0, false);
    }
}
static const char *client_info(layout_context *c, int32_t slot) {
    double maximum = 256;
    if (!c->rerelease) {
        const char *borrowed = c->options->configstring ? c->options->configstring(c->options->context, c->max_clients) : NULL;
        const char *raw = copy(c, borrowed);
        if (borrowed) {
            maximum = 0;
            if (*raw && !qa_parse_number((qa_bytes){(const uint8_t *)raw, strlen(raw)}, &maximum, NULL)) maximum = NAN;
        }
        if (!isnan(maximum)) maximum = fmax(1, maximum);
    }
    if (slot < 0 || slot >= maximum) { fail(c, "HUD client outside clientinfo"); return ""; }
    return config(c, c->skins + slot);
}
static const char *client_name(layout_context *c, int32_t slot) {
    const char *info = client_info(c, slot), *slash = strchr(info, '\\');
    return slash ? copy_span(c, info, (size_t)(slash - info)) : info;
}
static const char *localized(layout_context *c, const char *base, const char *const *args, size_t count) {
    const qa_localization_entry *entry = *base == '$' ? qa_localization_find(c->options->localization, base + 1) : NULL;
    size_t base_length = strlen(base), format_length = entry ? strlen(entry->format) : 0;
    if (base_length > SIZE_MAX - format_length || base_length + format_length > SIZE_MAX - 8193) {
        fail(c, "HUD localized text too long"); return "";
    }
    size_t buffer_size = entry && entry->argument_count ? base_length + format_length + 8193
                                                       : (entry ? format_length : base_length) + 1;
    char *buffer = allocate(c, buffer_size); if (!buffer) return "";
    qa_localize(c->options->localization, base, args, count, false, false, buffer, buffer_size);
    if (!strstr(buffer, "##P")) return buffer;
    size_t total = strlen(buffer), capacity = total + 1;
    typedef struct replacement { const char *name; size_t offset, end; } replacement;
    size_t maximum = total / 4;
    if (maximum > SIZE_MAX / sizeof(replacement)) { fail(c, "HUD player substitution overflow"); return ""; }
    replacement *replacements = maximum ? qa_arena_alloc(&c->scene->storage, maximum * sizeof(replacement),
                                                         _Alignof(replacement), c->error) : NULL;
    if (maximum && !replacements) { c->failed = true; return ""; }
    size_t found = 0;
    for (size_t i = 0; i + 3 < total; ++i) {
        if (buffer[i] != '#' || buffer[i + 1] != '#' || buffer[i + 2] != 'P' ||
            buffer[i + 3] < '0' || buffer[i + 3] > '9') continue;
        size_t end = i + 3; uint32_t slot = 0;
        while (end < total && buffer[end] >= '0' && buffer[end] <= '9') {
            if (slot > 255) { fail(c, "HUD localized player outside clientinfo"); return ""; }
            slot = slot * 10 + (uint32_t)(buffer[end++] - '0');
        }
        if (slot >= 256) { fail(c, "HUD localized player outside clientinfo"); return ""; }
        const char *name = client_name(c, (int32_t)slot);
        replacements[found++] = (replacement){name, i, end};
        size_t n = strlen(name);
        if (n > SIZE_MAX - capacity) { fail(c, "HUD localized text too long"); return ""; }
        capacity += n; i = end - 1;
    }
    if (!found) return buffer;
    char *out = allocate(c, capacity); if (!out) return "";
    size_t cursor = 0, written = 0;
    for (size_t i = 0; i < found; ++i) {
        replacement r = replacements[i];
        size_t n = r.offset - cursor; memcpy(out + written, buffer + cursor, n); written += n;
        n = strlen(r.name); memcpy(out + written, r.name, n); written += n; cursor = r.end;
    }
    memcpy(out + written, buffer + cursor, total - cursor + 1);
    return out;
}
static const char *config_stat(layout_context *c, int32_t index) {
    int32_t value = stat_int(c, index);
    if (value < 0 || value >= c->max_strings) { fail(c, "HUD stat string outside configstrings"); return ""; }
    return config(c, value);
}
static void item_picture(layout_context *c, int32_t index) {
    const qa_hud_q2_arsenal *a = c->frame->arsenal;
    const qa_scene_image *image = NULL; float aspect = 1;
    if (a && index == 2 && a->has_ammunition) {
        if (!a->unlimited_ammunition) image = a->ammunition_icon;
        aspect = a->ammunition_aspect;
    } else if (a && index == 6 && a->has_selected_item) {
        if (stat(c, index) != 0) image = a->selected_icon;
        aspect = a->selected_aspect;
    } else {
        int32_t value = stat_int(c, index);
        if (value < 0 || value >= c->max_images) { fail(c, "HUD image outside configstrings"); return; }
        picture(c, config(c, c->images + value), c->x, c->y, 0, 0, false); return;
    }
    if (image) {
        if (!isfinite(aspect) || aspect <= 0) { fail(c, "HUD arsenal image has invalid aspect"); return; }
        float w = 24 * fminf(1, aspect), h = 24 / fmaxf(1, aspect);
        quad(c, image, c->x + (24 - w) / 2, c->y + (24 - h) / 2, w, h,
             (qa_scene_vec4){0, 0, 1, 1}, white);
    }
}
static bool initialize(layout_context *c, const qa_hud_q2_options *options,
                       const qa_hud_q2_frame *frame, qa_scene_frame *scene, qa_error *error) {
    if (!options || !frame || !scene || (frame->stat_count && !frame->stats) ||
        (frame->inventory_count && !frame->inventory) || !isfinite(options->scale) || options->scale <= 0 ||
        !options->fonts.classic || !options->white || !isfinite(options->font_line_height) ||
        options->font_line_height <= 0 ||
        (frame->arsenal && ((frame->arsenal->inventory_count && !frame->arsenal->inventory) ||
                           !isfinite(frame->arsenal->ammunition)))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q2 HUD frame or resources"); return false;
    }
    *c = (layout_context){.options = options, .frame = frame, .scene = scene, .error = error,
        .width = (float)options->viewport.width / options->scale, .height = (float)options->viewport.height / options->scale,
        .table = options->table};
    if (!isfinite(c->width) || !isfinite(c->height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 HUD scale exceeds logical coordinate range"); return false;
    }
    if (frame->protocol.kind != QA_NET_Q2_34 && frame->protocol.kind != QA_NET_R1Q2_35 &&
        frame->protocol.kind != QA_NET_Q2PRO_36 && frame->protocol.kind != QA_NET_Q2REPRO_1038 &&
        frame->protocol.kind != QA_NET_Q2KEX_2023 && frame->protocol.kind != QA_NET_Q2KEX_DEMO_2022 &&
        frame->protocol.kind != QA_NET_Q2PRIVATE_4038) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 HUD requires a Q2 protocol"); return false;
    }
    c->rerelease = frame->protocol.kind == QA_NET_Q2REPRO_1038 || frame->protocol.kind == QA_NET_Q2KEX_2023 ||
                   frame->protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    c->images = c->rerelease ? 10302 : 544;
    c->items = c->rerelease ? 11326 : 1056;
    c->skins = c->rerelease ? 11582 : 1312;
    c->max_clients = c->rerelease ? 60 : 30;
    c->max_images = c->rerelease ? 512 : 256;
    c->max_strings = c->rerelease ? 12448 : 2080;
    return true;
}
static void table_cell(layout_context *c, char out[70], const char *value) {
    size_t length = strlen(value); if (length > 23) length = 23;
    qa_bytes bytes = {(const uint8_t *)value, length};
    size_t cursor = 0, written = 0; uint32_t scalar;
    while (qa_utf8_next(bytes, &cursor, &scalar)) {
        char encoded[4]; size_t n = qa_utf8_encode(scalar, encoded);
        memcpy(out + written, encoded, n); written += n;
    }
    out[written] = 0;
    (void)c;
}
static void draw_table(layout_context *c) {
    qa_hud_q2_table *t = c->table;
    float space = truncf(measure(c, " ", true)), w = 0;
    for (size_t i = 0; i < t->column_count; ++i) w += t->columns[i];
    if (t->column_count) w += (float)(t->column_count - 1) * space;
    float step = 8 + (c->options->font_line_height - 8) / 2;
    float h = (float)t->row_count * step, left = c->x - truncf(w / 2), top = c->y + 8;
    classic_glyph(c, 18, left - 8, top - 8, false);
    classic_glyph(c, 20, left + w, top - 8, false);
    classic_glyph(c, 24, left - 8, top + h, false);
    classic_glyph(c, 26, left + w, top + h, false);
    double first_x = left + ceil(fmax(0, -(double)left - 8) / 8) * 8;
    double end_x = fmin((double)left + w, (double)c->width + 8);
    for (double x = first_x; x < end_x; x += 8) {
        classic_glyph(c, 19, (float)x, top - 8, false); classic_glyph(c, 25, (float)x, top + h, false);
    }
    double first_y = top + ceil(fmax(0, -(double)top - 8) / 8) * 8;
    double end_y = fmin((double)top + h, (double)c->height + 8);
    for (double y = first_y; y < end_y; y += 8) {
        classic_glyph(c, 21, left - 8, (float)y, false); classic_glyph(c, 23, left + w, (float)y, false);
    }
    fill(c, left, top, w, h, black);
    float x = left;
    for (size_t column = 0; column < t->column_count; ++column) {
        for (size_t row = 0; row < t->row_count; ++row) {
            const char *value = t->cells[row][column];
            float offset = t->columns[column] - measure(c, value, true);
            if (!row) offset /= 2; else if (!column) offset = 0;
            text(c, value, row == 0, x + offset, top + (float)row * step, true);
        }
        x += t->columns[column] + space;
    }
}
static const char *padded(layout_context *c, const char *value, size_t width, bool right) {
    size_t units = utf16_length(value), bytes = strlen(value);
    if (units >= width) return value;
    size_t padding = width - units;
    if (bytes > SIZE_MAX - padding - 1) { fail(c, "HUD padding overflow"); return ""; }
    char *out = allocate(c, bytes + padding + 1); if (!out) return "";
    if (right) { memcpy(out, value, bytes); memset(out + bytes, ' ', padding); }
    else { memset(out, ' ', padding); memcpy(out + padding, value, bytes); }
    out[bytes + padding] = 0; return out;
}
static const char *join(layout_context *c, const char *a, const char *b, const char *d) {
    size_t n = strlen(a), m = strlen(b), k = strlen(d);
    if (n > SIZE_MAX - m || n + m > SIZE_MAX - k - 1) { fail(c, "HUD string overflow"); return ""; }
    char *out = allocate(c, n + m + k + 1); if (!out) return "";
    memcpy(out, a, n); memcpy(out + n, b, m); memcpy(out + n + m, d, k + 1); return out;
}
static void name12(layout_context *c, const char *value, bool alternate, float x, float y) {
    qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
    size_t cursor = 0, units = 0; uint32_t scalar;
    while (units < 12 && qa_utf8_next(bytes, &cursor, &scalar)) {
        uint32_t mask = alternate ? 128 : 0;
        if (scalar > 0xffff) {
            scalar -= 0x10000;
            classic_glyph(c, (0xd800 + (scalar >> 10)) | mask, x + (float)units++ * 8, y, false);
            if (units == 12) break;
            scalar = 0xdc00 + (scalar & 1023);
        }
        classic_glyph(c, scalar | mask, x + (float)units++ * 8, y, false);
    }
}
static bool execute(layout_context *c, const char *source) {
    source = copy(c, source);
    if (c->failed || !qa_tokenizer_init(&c->parser, (qa_bytes){(const uint8_t *)source, strlen(source)}, c->error)) return false;
    c->x = c->y = 0;
    bool local_conditions[16], *conditions = local_conditions;
    size_t depth = 0, capacity = 16;
    for (;;) {
        bool found;
        const char *command = next(c, &found);
        if (c->failed || !found) break;
        bool draw = !depth || conditions[depth - 1];
        if (!strcmp(command, "if") || (c->rerelease && !strcmp(command, "ifgef"))) {
            int32_t value = integer(c);
            if (!c->rerelease) {
                if (stat(c, value) == 0)
                    do { command = next(c, &found); } while (!c->failed && found && strcmp(command, "endif"));
                continue;
            }
            bool enabled = draw && (!strcmp(command, "if") ? stat(c, value) != 0 : c->frame->server_frame >= value);
            if (depth == capacity) {
                if (capacity > SIZE_MAX / (2 * sizeof(bool))) { fail(c, "HUD conditional nesting overflow"); break; }
                bool *larger = qa_arena_alloc(&c->scene->storage, capacity * 2 * sizeof(bool), _Alignof(bool), c->error);
                if (!larger) { c->failed = true; break; }
                memcpy(larger, conditions, depth * sizeof(bool)); conditions = larger; capacity *= 2;
            }
            conditions[depth++] = enabled;
        } else if (!strcmp(command, "endif")) {
            if (c->rerelease) { if (!depth) fail(c, "HUD endif without matching if"); else --depth; }
        } else if (!strcmp(command, "xl") || !strcmp(command, "xr") || !strcmp(command, "xv")) {
            int32_t value = integer(c);
            if (draw) c->x = (float)value + (command[1] == 'r' ? c->width : command[1] == 'v' ? truncf(c->width / 2) - 160 : 0);
        } else if (!strcmp(command, "yt") || !strcmp(command, "yb") || !strcmp(command, "yv")) {
            int32_t value = integer(c);
            if (draw) c->y = (float)value + (command[1] == 'b' ? c->height : command[1] == 'v' ? truncf(c->height / 2) - 120 : 0);
        } else if (!strcmp(command, "pic")) {
            int32_t index = integer(c); if (draw) item_picture(c, index);
        } else if (!strcmp(command, "picn")) {
            const char *value = argument(c); if (draw) picture(c, value, c->x, c->y, 0, 0, false);
        } else if (!strcmp(command, "num")) {
            int32_t digits = integer(c), index = integer(c); if (draw) field(c, stat(c, index), digits, false);
        } else if (c->rerelease && !strcmp(command, "lives_num")) {
            int32_t index = integer(c);
            if (draw) { double value = stat(c, index); field(c, fmax(0, value - 2), 1, value <= 2 && c->frame->time_ns % UINT64_C(1000000000) < UINT64_C(500000000)); }
        } else if (!strcmp(command, "hnum") || !strcmp(command, "anum") || !strcmp(command, "rnum")) {
            if (!draw) continue;
            int32_t index = command[0] == 'h' ? 1 : command[0] == 'a' ? 3 : 5;
            double value = stat(c, index);
            if (index != 1 && value < (c->rerelease || index == 3 ? 0 : 1)) continue;
            bool flash = c->rerelease ? c->frame->time_ns % UINT64_C(1000000000) < UINT64_C(500000000)
                                      : (((uint32_t)c->frame->server_frame >> 2) & 1) != 0;
            int32_t warning = 5;
            if (c->rerelease) {
                const char *weapon = config(c, 12350 + stat_int(c, 53));
                for (size_t i = 0; i < 6; ++i) { const char *bar = strchr(weapon, '|'); weapon = bar ? bar + 1 : ""; }
                int32_t parsed = decimal(weapon); if (parsed) warning = parsed;
            }
            bool alternate = index == 1 ? value <= 0 || (value <= 25 && flash) : index == 3 && value <= warning && flash;
            if (stat_int(c, 15) & (index == 1 ? 1 : index == 3 ? 4 : 2)) picture(c, "field_3", c->x, c->y, 0, 0, false);
            field(c, value, 3, alternate);
        } else if (!strcmp(command, "stat_string") || (c->rerelease && !strncmp(command, "loc_stat_", 9))) {
            if (strcmp(command, "stat_string") && strcmp(command, "loc_stat_string") && strcmp(command, "loc_stat_rstring") &&
                strcmp(command, "loc_stat_cstring") && strcmp(command, "loc_stat_cstring2")) continue;
            int32_t index = integer(c); if (!draw) continue;
            const qa_hud_q2_arsenal *a = c->frame->arsenal;
            bool selected = c->rerelease && index == 51 && stat(c, index) != 0 && a && a->has_selected_item;
            const char *raw = selected && a->selected_label ? a->selected_label : config_stat(c, index);
            const char *value = !strcmp(command, "stat_string") ? raw : selected && a->selected_localized_label
                ? a->selected_localized_label : localized(c, raw, NULL, 0);
            if (!strncmp(command, "loc_stat_cstring", 16)) centered(c, value, command[strlen(command) - 1] == '2', c->x, c->y);
            else text(c, value, false, c->x - (!strcmp(command, "loc_stat_rstring") ? measure(c, value, false) : 0), c->y, false);
        } else if (!strcmp(command, "string") || !strcmp(command, "string2") || !strcmp(command, "cstring") || !strcmp(command, "cstring2")) {
            const char *value = argument(c);
            if (draw) {
                bool alternate = command[strlen(command) - 1] == '2';
                if (command[0] == 'c') centered(c, value, alternate, c->x, c->y);
                else text(c, value, alternate, c->x, c->y, false);
            }
        } else if (c->rerelease && (!strcmp(command, "loc_string") || !strcmp(command, "loc_string2") ||
                   !strcmp(command, "loc_rstring") || !strcmp(command, "loc_rstring2") ||
                   !strcmp(command, "loc_cstring") || !strcmp(command, "loc_cstring2"))) {
            int32_t count = integer(c);
            if (count < 0 || count > 7) { fail(c, "HUD localized argument count exceeds source limit"); break; }
            const char *base = argument(c), *args[7];
            for (int32_t i = 0; i < count; ++i) args[i] = argument(c);
            if (!draw) continue;
            const char *value = localized(c, base, args, (size_t)count);
            bool alternate = command[strlen(command) - 1] == '2';
            if (command[4] == 'c') centered(c, value, alternate, c->x, c->y);
            else text(c, value, alternate, c->x - (command[4] == 'r' ? measure(c, value, false) : 0), c->y, false);
        } else if (!strcmp(command, "client")) {
            int32_t px = integer(c), py = integer(c), slot = integer(c), score = integer(c), ping = integer(c);
            int32_t time = c->rerelease ? 0 : integer(c);
            if (!draw) continue;
            c->x = truncf(c->width / 2) - 160 + (float)px + (c->rerelease ? 8 : 0);
            c->y = truncf(c->height / 2) - 120 + (float)py + (c->rerelease ? 7 : 0);
            const char *info = client_info(c, slot), *slash = strchr(info, '\\');
            const char *name = slash ? copy_span(c, info, (size_t)(slash - info)) : info;
            text(c, name, !c->rerelease, c->x + 32, c->y, false);
            if (c->rerelease) {
                text(c, number_string(c, score, true), !c->options->use_font, c->x + 32, c->y + 10, false);
                picture(c, "ping", c->x + 96, c->y + 10, 9, 9, false);
                text(c, number_string(c, ping, true), false, c->x + (c->options->use_font ? 107 : 105), c->y + 10, false);
            } else {
                text(c, "Score: ", false, c->x + 32, c->y + 8, false);
                text(c, number_string(c, score, true), true, c->x + 88, c->y + 8, false);
                text(c, join(c, "Ping:  ", number_string(c, ping, true), ""), false, c->x + 32, c->y + 16, false);
                text(c, join(c, "Time:  ", number_string(c, time, true), ""), false, c->x + 32, c->y + 24, false);
                const char *skin = slash && slash[1] ? slash + 1 : "male/grunt";
                picture(c, join(c, "/players/", skin, "_i.pcx"), c->x, c->y, 0, 0, false);
            }
        } else if (!strcmp(command, "ctf")) {
            int32_t px = integer(c), py = integer(c), slot = integer(c), score = integer(c), ping = integer(c);
            if (ping > 999) ping = 999;
            const char *icon = c->rerelease ? argument(c) : "";
            if (!draw) continue;
            c->x = truncf(c->width / 2) - 160 + (float)px; c->y = truncf(c->height / 2) - 120 + (float)py;
            bool alternate = slot == c->frame->player_number;
            const char *name = client_name(c, slot);
            if (c->rerelease) {
                text(c, number_string(c, score, true), alternate, c->x, c->y, true); c->x += 27;
                text(c, number_string(c, ping, true), alternate, c->x, c->y, true); c->x += 27;
                text(c, name, alternate, c->x, c->y, true);
                picture(c, icon, c->x, c->y, 0, 0, true);
            } else {
                const char *s = join(c, padded(c, number_string(c, score, true), 3, false), " ", padded(c, number_string(c, ping, true), 3, false));
                const char *prefix = join(c, s, " ", "");
                text(c, prefix, alternate, c->x, c->y, false);
                name12(c, name, alternate, c->x + (float)utf16_length(prefix) * 8, c->y);
            }
        } else if (c->rerelease && !strcmp(command, "time_limit")) {
            int32_t end = integer(c); if (!draw || end < c->frame->server_frame) continue;
            double ns = c->frame->frame_ns ? (double)c->frame->frame_ns : 25000000.;
            double seconds = trunc(((double)end - c->frame->server_frame) * ns / 1.e9);
            const char *minutes = padded(c, number_string(c, trunc(seconds / 60), true), 2, false);
            const char *remainder = padded(c, number_string(c, fmod(seconds, 60), true), 2, false);
            /* Source padStart uses zero rather than blank for the clock. */
            char *clock = (char *)join(c, minutes, ":", remainder);
            for (size_t i = 0; clock[i]; ++i) if (clock[i] == ' ') clock[i] = '0';
            const char *args[] = {clock}, *value = localized(c, "$g_score_time", args, 1);
            text(c, value, true, c->x - measure(c, value, false), c->y, false);
        } else if (c->rerelease && !strcmp(command, "dogtag")) {
            int32_t slot = integer(c); if (!draw) continue;
            const char *info = client_info(c, slot), *tag = strchr(info, '\\');
            tag = tag ? strchr(tag + 1, '\\') : NULL; tag = tag && tag[1] ? tag + 1 : "default";
            const char *end = strchr(tag, '\\'); if (end) tag = copy_span(c, tag, (size_t)(end - tag));
            picture(c, join(c, "/tags/", tag, ".pcx"), c->x, c->y, 198, 32, false);
        } else if (c->rerelease && (!strcmp(command, "start_table") || !strcmp(command, "table_row"))) {
            bool start = !strcmp(command, "start_table");
            int32_t count = integer(c); const char *values[6];
            if (count < 0 || count > (start ? 5 : 6)) { fail(c, "HUD table argument count exceeds source limit"); break; }
            for (int32_t i = 0; i < count; ++i) values[i] = argument(c);
            if (!draw) continue;
            if (start) { memset(c->table, 0, sizeof *c->table); c->table->column_count = (size_t)count; }
            if (c->table->row_count >= 11) { fail(c, "HUD table exceeds source dimensions"); break; }
            size_t row = c->table->row_count++;
            size_t cells = (size_t)count > c->table->column_count ? (size_t)count : c->table->column_count;
            for (size_t i = 0; i < cells; ++i) {
                const char *value = i < (size_t)count ? values[i] : "";
                if (start) value = localized(c, value, NULL, 0);
                table_cell(c, c->table->cells[row][i], value);
                if (i < c->table->column_count) c->table->columns[i] = fmaxf(c->table->columns[i], truncf(measure(c, c->table->cells[row][i], true)));
            }
        } else if (c->rerelease && !strcmp(command, "draw_table")) {
            if (draw) draw_table(c);
        } else if (c->rerelease && !strcmp(command, "stat_pname")) {
            int32_t index = integer(c); if (draw) text(c, client_name(c, stat_int(c, index) - 1), false, c->x, c->y, false);
        } else if (c->rerelease && !strcmp(command, "health_bars")) {
            if (!draw) continue;
            centered(c, localized(c, config(c, 12104), NULL, 0), false, truncf(c->width / 2) - 160, c->y);
            c->y += c->options->font_line_height;
            uint32_t value = (uint32_t)stat_int(c, 52);
            float w = c->width / 2, left = c->width / 4;
            for (size_t i = 0; i < 2; ++i) {
                uint32_t packed = (value >> (i * 8)) & 255; if (!(packed & 128)) continue;
                float fraction = (float)(packed & 127u) / 127.f;
                fill(c, left, c->y, w + 1, 5, black);
                if (fraction > 0) fill(c, left, c->y, w * fraction, 4, (qa_scene_vec4){1, 0, 0, 1});
                if (fraction < 1) fill(c, left + w * fraction, c->y, w * (1 - fraction), 4, (qa_scene_vec4){80.f / 255, 80.f / 255, 80.f / 255, 1});
                c->y += 12;
            }
        } else if (c->rerelease && !strcmp(command, "story")) {
            /* Source story deliberately ignores the conditional draw flag. */
            const char *raw = config(c, 12105); if (!*raw) continue;
            const char *value = localized(c, raw, NULL, 0);
            qa_font_layout block; if (!font_layout(c, value, &block)) break;
            const char *line = value; size_t index = 0;
            for (;;) {
                const char *end = strchr(line, '\n'), *part = end ? copy_span(c, line, (size_t)(end - line)) : line;
                /* text() applies the ordinary font baseline adjustment. */
                text(c, part, false, (c->width - measure(c, part, true)) / 2,
                     (c->height - block.height) / 2 + (float)index * c->options->font_line_height + (c->options->font_line_height - 8) / 2, true);
                if (!end) break;
                line = end + 1; ++index;
            }
        }
        if (c->failed) break;
    }
    if (c->rerelease && depth && !c->failed) fail(c, "HUD if without matching endif");
    return !c->failed;
}
static bool inventory_draw(layout_context *c) {
    const qa_hud_q2_arsenal *a = c->frame->arsenal;
    bool foreign = a && a->has_inventory;
    size_t count = 0, selected_row = 0;
    int32_t selected = c->frame->stat_count > 12 ? c->frame->stats[12] : 0;
    if (foreign) {
        count = a->inventory_count;
        for (size_t i = 0; i < count; ++i) if (a->inventory[i].selected) { selected_row = i; break; }
    } else {
        for (size_t i = 0; i < c->frame->inventory_count; ++i) {
            if (!c->frame->inventory[i]) continue;
            if (selected >= 0 && (size_t)selected < c->frame->inventory_count && i < (size_t)selected) ++selected_row;
            ++count;
        }
    }
    size_t page = c->rerelease ? 19 : 17, half = c->rerelease ? 9 : 8;
    size_t maximum = count > page ? count - page : 0;
    size_t top = selected_row > half ? selected_row - half : 0;
    if (top > maximum) top = maximum;
    float x = c->rerelease ? truncf(c->width / 2) - 128 : floorf((c->width - 256) / 2);
    float y = c->rerelease ? truncf(c->height / 2) - 108 : floorf((c->height - 240) / 2);
    picture(c, "inventory", x, y + 8, 0, 0, false);
    if (!c->rerelease) {
        classic_text(c, "hotkey ### item", x + 24, y + 24, false, false, false);
        classic_text(c, "------ --- ----", x + 24, y + 32, false, false, false);
    }
    size_t ordinal = 0, rows = 0, source_count = foreign ? a->inventory_count : c->frame->inventory_count;
    for (size_t i = 0; i < source_count && rows < page; ++i) {
        if (!foreign && !c->frame->inventory[i]) continue;
        if (ordinal++ < top) continue;
        const char *name = foreign ? copy(c, a->inventory[i].label) :
            i <= (size_t)(INT32_MAX - c->items) ? config(c, c->items + (int32_t)i) : "";
        double quantity = foreign ? a->inventory[i].count : c->frame->inventory[i];
        if (!isfinite(quantity)) { fail(c, "HUD inventory has nonfinite quantity"); break; }
        bool chosen = foreign ? a->inventory[i].selected : selected >= 0 && (size_t)selected == i;
        float py = y + (c->rerelease ? 27 : 40) + (float)rows++ * 8;
        const char *quantity_text = number_string(c, quantity, false);
        if (c->rerelease) {
            name = localized(c, name, NULL, 0);
            /* Source inventory cursor uses timeMilliseconds * 10, then ToInt32. */
            bool blink = ((c->frame->time_ns / UINT64_C(100000)) & 1) != 0;
            if (chosen && blink) classic_glyph(c, 15, x + 14, py, false);
            if (c->options->use_font) {
                text(c, quantity_text, chosen, x + 222 - measure(c, quantity_text, false), py, false);
                text(c, name, chosen, x + 38, py, false);
            } else classic_text(c, join(c, padded(c, quantity_text, 3, false), " ", name),
                                x + 22, py, chosen, true, false);
        } else {
            const char *command = join(c, "use ", name, "");
            const char *binding = copy(c, c->options->binding ? c->options->binding(c->options->context, command) : NULL);
            const char *line = join(c, padded(c, binding, 6, false), " ", padded(c, quantity_text, 3, false));
            classic_text(c, join(c, line, " ", name), x + 24, py, !chosen, false, false);
            if (chosen && ((c->frame->time_ns / UINT64_C(100000000)) & 1)) classic_glyph(c, 15, x + 16, py, false);
        }
        if (c->failed) break;
    }
    return !c->failed;
}
static bool table_valid(const qa_hud_q2_table *table, qa_error *error) {
    if (table->row_count > 11 || table->column_count > 5) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid retained Q2 HUD table"); return false;
    }
    for (size_t column = 0; column < table->column_count; ++column) {
        if (!isfinite(table->columns[column]) || table->columns[column] < 0) {
            qa_error_set(error, QA_ERROR_ARGUMENT, column, "invalid Q2 HUD table width"); return false;
        }
        for (size_t row = 0; row < table->row_count; ++row)
            if (!memchr(table->cells[row][column], 0, sizeof table->cells[row][column])) {
                qa_error_set(error, QA_ERROR_ARGUMENT, row, "unterminated Q2 HUD table cell"); return false;
            }
    }
    return true;
}
bool qa_hud_q2_layout(const qa_hud_q2_options *options, const qa_hud_q2_frame *frame,
                      const char *source, qa_scene_frame *scene, qa_error *error) {
    layout_context c;
    if (!source || !initialize(&c, options, frame, scene, error)) {
        if (!source) qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing Q2 HUD layout");
        return false;
    }
    qa_hud_q2_table table = {0};
    if (!c.table) c.table = &table;
    if (!table_valid(c.table, error)) return false;
    return execute(&c, source);
}
bool qa_hud_q2_draw(const qa_hud_q2_options *options, const qa_hud_q2_frame *frame,
                    bool overlay_only, qa_scene_frame *scene, qa_error *error) {
    layout_context c;
    if (!initialize(&c, options, frame, scene, error)) return false;
    qa_hud_q2_table table = {0};
    if (!c.table) c.table = &table;
    if (!table_valid(c.table, error)) return false;
    int32_t layouts = frame->stat_count > 13 ? frame->stats[13] : 0;
    if (!overlay_only && !(c.rerelease && (layouts & 4)) && !execute(&c, config(&c, 5))) return false;
    if ((layouts & 1) && !execute(&c, frame->layout ? frame->layout : "")) return false;
    if (!overlay_only && (layouts & 2)) return inventory_draw(&c);
    return !c.failed;
}
