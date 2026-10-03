#include "qa/hud_wheel.h"
#include "qa/hud_wheel_save.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum carousel_state { CAROUSEL_CLOSED, CAROUSEL_OPEN, CAROUSEL_CLOSING } carousel_state;
struct qa_hud_wheel {
    qa_hud_wheel_options options;
    qa_hud_wheel_item *items;
    size_t count, capacity;
    qa_hud_wheel_mode mode;
    carousel_state carousel;
    bool open, busy;
    uint64_t selected, carousel_selected, deselect_until, carousel_until, lock_until, last_update;
    qa_input_pair position, analog;
    float opacity;
};
static bool wheel_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
static bool begin(qa_hud_wheel *w, qa_error *error) {
    if (!w || w->busy) return wheel_fail(error, "wheel operation is unavailable during a callback");
    w->busy = true; return true;
}
static uint64_t deadline(uint64_t now, uint64_t duration) {
    return duration > UINT64_MAX - now ? UINT64_MAX : now + duration;
}
static void changed(qa_hud_wheel *w) {
    if (w->options.changed) w->options.changed(w->options.context, w->options.seat);
}
static int compare(const void *a, const void *b) {
    const qa_hud_wheel_item *x = a, *y = b;
    if (x->sort_order != y->sort_order) return x->sort_order < y->sort_order ? -1 : 1;
    if (x->identity.source_ordinal != y->identity.source_ordinal)
        return x->identity.source_ordinal < y->identity.source_ordinal ? -1 : 1;
    return 0;
}
static bool observe(qa_hud_wheel *w, qa_hud_wheel_mode mode, bool owned_only, qa_error *error) {
    const qa_hud_wheel_item *items = NULL; size_t count = 0;
    if (!w->options.items(w->options.context, w->options.seat, mode, &items, &count, error)) return false;
    if ((count && !items) || count > SIZE_MAX / sizeof(*items)) return wheel_fail(error, "invalid wheel item observation");
    if (count > w->capacity) {
        qa_hud_wheel_item *larger = realloc(w->items, count * sizeof(*larger));
        if (!larger) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating wheel item order"); return false; }
        w->items = larger; w->capacity = count;
    }
    w->count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!items[i].identity.key || !items[i].label ||
            (items[i].has_count && (!isfinite(items[i].count) || !isfinite(items[i].warning_count))))
            return wheel_fail(error, "invalid wheel item identity or quantity");
        for (size_t j = 0; j < i; ++j)
            if (items[i].identity.key == items[j].identity.key) return wheel_fail(error, "duplicate wheel item identity");
        if (!owned_only || items[i].owned) w->items[w->count++] = items[i];
    }
    /* Stable insertion keeps the source order of tied sort keys. Typical wheels
     * contain a small fixed arsenal; retained storage avoids frame allocations. */
    for (size_t i = 1; i < w->count; ++i) {
        qa_hud_wheel_item item = w->items[i]; size_t j = i;
        while (j && compare(&item, &w->items[j - 1]) < 0) { w->items[j] = w->items[j - 1]; --j; }
        w->items[j] = item;
    }
    return true;
}
static bool active(qa_hud_wheel *w, uint64_t *key, qa_error *error) {
    *key = 0;
    return w->options.active(w->options.context, w->options.seat, key, error);
}
qa_hud_wheel_options qa_hud_wheel_defaults(uint32_t seat) {
    return (qa_hud_wheel_options){.seat = seat, .radius = 180, .selection_distance = 140,
        .fade_per_second = 3, .carousel_timeout_ns = UINT64_C(400000000),
        .carousel_lock_ns = UINT64_C(300000000), .q2_slot_zero_deselect = true};
}
bool qa_hud_wheel_create(const qa_hud_wheel_options *options, uint64_t now,
                         qa_hud_wheel **out, qa_error *error) {
    if (!options || !out || !options->items || !options->active || !options->select ||
        !isfinite(options->radius) || options->radius <= 0 || !isfinite(options->selection_distance) ||
        options->selection_distance < 0 || options->selection_distance >= options->radius ||
        !isfinite(options->fade_per_second) || options->fade_per_second < 0)
        return wheel_fail(error, "invalid wheel services or geometry");
    qa_hud_wheel *w = calloc(1, sizeof *w);
    if (!w) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating seat wheel"); return false; }
    w->options = *options; w->last_update = now; *out = w; return true;
}
bool qa_hud_wheel_destroy(qa_hud_wheel *w, qa_error *error) {
    if (!w) return true;
    if (w->busy) return wheel_fail(error, "cannot destroy wheel during its callback");
    free(w->items); free(w); return true;
}
bool qa_hud_wheel_open(qa_hud_wheel *w, qa_hud_wheel_mode mode,
                       bool *opened, qa_error *error) {
    if (!opened || mode < QA_HUD_WHEEL_WEAPONS || mode > QA_HUD_WHEEL_POWERUPS)
        return wheel_fail(error, "invalid wheel open request");
    if (!begin(w, error)) return false;
    bool ok = observe(w, mode, false, error); *opened = false;
    if (ok && w->count) {
        w->mode = mode; w->open = true; w->selected = 0; w->deselect_until = 0;
        w->position = w->analog = (qa_input_pair){0}; *opened = true; changed(w);
    }
    w->busy = false; return ok;
}
static bool close_wheel(qa_hud_wheel *w, bool select, qa_error *error) {
    if (!w->open) return true;
    w->open = false;
    bool ok = true;
    if (select && w->selected) {
        ok = observe(w, w->mode, false, error);
        if (ok) for (size_t i = 0; i < w->count; ++i) {
            const qa_hud_wheel_item *item = &w->items[i];
            if (item->identity.key == w->selected && item->owned) {
                qa_hud_wheel_identity identity = item->identity;
                ok = w->options.select(w->options.context, w->options.seat, w->mode, identity, error);
                break;
            }
        }
    }
    changed(w); return ok;
}
bool qa_hud_wheel_close(qa_hud_wheel *w, bool select, qa_error *error) {
    if (!begin(w, error)) return false;
    bool ok = close_wheel(w, select, error); w->busy = false; return ok;
}
static void move(qa_hud_wheel *w, double x, double y) {
    double distance = hypot(x, y), factor = distance > w->options.radius ? w->options.radius / distance : 1;
    w->position = (qa_input_pair){(float)(x * factor), (float)(y * factor)};
}
bool qa_hud_wheel_input(qa_hud_wheel *w, uint32_t seat, const qa_input_event *event,
                        bool *handled, qa_error *error) {
    if (!w || !event || !handled || seat != w->options.seat)
        return wheel_fail(error, "wheel input belongs to another seat");
    if (!begin(w, error)) return false;
    *handled = false; bool ok = true;
    if (w->open) {
        if (event->kind == QA_INPUT_EVENT_FOCUS && !event->down) {
            ok = close_wheel(w, false, error); *handled = true;
        } else if (event->kind == QA_INPUT_EVENT_MOUSE) {
            if (!isfinite(event->delta.x) || !isfinite(event->delta.y)) ok = wheel_fail(error, "invalid wheel pointer delta");
            else move(w, (double)w->position.x + event->delta.x, (double)w->position.y + event->delta.y);
            *handled = true;
        } else if (event->kind == QA_INPUT_EVENT_AXIS) {
            if (event->input.kind != QA_PHYSICAL_AXIS || event->input.code >= QA_AXIS_COUNT || !isfinite(event->value))
                ok = wheel_fail(error, "invalid wheel controller axis");
            else if (event->input.code == QA_AXIS_RIGHT_X || event->input.code == QA_AXIS_RIGHT_Y) {
                if (event->input.code == QA_AXIS_RIGHT_X) w->analog.x = event->value; else w->analog.y = event->value;
                move(w, (double)w->analog.x * w->options.radius, (double)w->analog.y * w->options.radius);
            }
            *handled = true;
        }
    }
    w->busy = false; return ok;
}
bool qa_hud_wheel_cycle(qa_hud_wheel *w, int direction, uint64_t now, qa_error *error) {
    if (direction != -1 && direction != 1) return wheel_fail(error, "wheel cycle direction must be -1 or 1");
    if (!begin(w, error)) return false;
    uint64_t selected = w->carousel_selected;
    bool ok = w->carousel == CAROUSEL_OPEN || active(w, &selected, error);
    if (ok) ok = observe(w, QA_HUD_WHEEL_WEAPONS, true, error);
    if (ok && !w->count) w->carousel = CAROUSEL_CLOSED;
    else if (ok) {
        size_t candidate = direction > 0 ? w->count - 1 : 0;
        for (size_t i = 0; i < w->count; ++i) if (w->items[i].identity.key == selected) { candidate = i; break; }
        for (size_t offset = 0; offset < w->count; ++offset) {
            candidate = direction > 0 ? (candidate + 1 == w->count ? 0 : candidate + 1)
                                      : (candidate == 0 ? w->count - 1 : candidate - 1);
            if (w->items[candidate].has_ammunition) { selected = w->items[candidate].identity.key; break; }
        }
        w->carousel_selected = selected; w->carousel = CAROUSEL_OPEN;
        w->carousel_until = deadline(now, w->options.carousel_timeout_ns); changed(w);
    }
    w->busy = false; return ok;
}
bool qa_hud_wheel_prepare(qa_hud_wheel *w, bool attack, uint64_t now,
                          qa_hud_wheel_command *out, qa_error *error) {
    if (!out) return wheel_fail(error, "missing wheel command output");
    if (!begin(w, error)) return false;
    if (w->carousel == CAROUSEL_CLOSING && now >= w->carousel_until) w->carousel = CAROUSEL_CLOSED;
    bool selecting = w->carousel == CAROUSEL_OPEN, ok = true;
    if (selecting && (attack || now >= w->carousel_until)) {
        uint64_t current;
        ok = active(w, &current, error);
        if (ok) ok = observe(w, QA_HUD_WHEEL_WEAPONS, false, error);
        const qa_hud_wheel_item *selected = NULL;
        if (ok) for (size_t i = 0; i < w->count; ++i) {
            const qa_hud_wheel_item *item = &w->items[i];
            if (item->identity.key == w->carousel_selected && item->owned && item->has_ammunition) { selected = item; break; }
        }
        if (ok && selected && selected->identity.key != current) {
            qa_hud_wheel_identity identity = selected->identity;
            ok = w->options.select(w->options.context, w->options.seat, QA_HUD_WHEEL_WEAPONS, identity, error);
            if (ok) {
                w->lock_until = deadline(now, w->options.carousel_lock_ns);
                w->carousel = CAROUSEL_CLOSING; w->carousel_until = w->lock_until;
            }
        } else if (ok) w->carousel = CAROUSEL_CLOSED;
    }
    if (ok) *out = (qa_hud_wheel_command){
        .holster = (w->open && w->mode == QA_HUD_WHEEL_WEAPONS) || w->carousel == CAROUSEL_OPEN || selecting,
        .consume_attack = attack && selecting, .lock_until_ns = w->lock_until,
        .time_scale = fmaxf(.1f, 1 - w->opacity)};
    w->busy = false; return ok;
}
bool qa_hud_wheel_update(qa_hud_wheel *w, uint64_t now, qa_error *error) {
    if (!begin(w, error)) return false;
    double elapsed = now > w->last_update ? (double)(now - w->last_update) / 1.e9 : 0;
    w->last_update = now;
    w->opacity = (float)fmax(0, fmin(1, w->opacity + elapsed * w->options.fade_per_second * (w->open ? 1 : -1)));
    bool ok = true;
    if (w->open) {
        ok = observe(w, w->mode, false, error);
        if (ok && !w->count) ok = close_wheel(w, false, error);
        else if (ok) {
            uint64_t prior = w->selected;
            double distance = hypot(w->position.x, w->position.y), slice = 6.2831853071795864769 / (double)w->count;
            if (distance > w->options.selection_distance) {
                for (size_t i = 0; i < w->count; ++i) if (w->items[i].owned) {
                    double angle = slice * (double)i;
                    double dot = w->position.x / distance * sin(angle) - w->position.y / distance * cos(angle);
                    if (dot > cos(slice / 2)) { w->selected = w->items[i].identity.key; w->deselect_until = 0; }
                }
            } else {
                size_t selected_index = SIZE_MAX;
                for (size_t i = 0; i < w->count; ++i) if (w->items[i].identity.key == w->selected) { selected_index = i; break; }
                bool deselect = w->options.q2_slot_zero_deselect ? selected_index != 0 : selected_index != SIZE_MAX;
                if (deselect && !w->deselect_until) w->deselect_until = deadline(now, UINT64_C(200000000));
            }
            if (w->deselect_until && w->deselect_until < now) { w->selected = 0; w->deselect_until = 0; }
            if (prior != w->selected) changed(w);
        }
    }
    w->busy = false; return ok;
}
bool qa_hud_wheel_read(const qa_hud_wheel *w, qa_hud_wheel_status *out) {
    if (!w || !out) return false;
    *out = (qa_hud_wheel_status){.open = w->open, .visible = w->open || w->opacity > 0,
        .carousel_visible = w->carousel == CAROUSEL_OPEN, .mode = w->mode,
        .selected = w->selected, .carousel_selected = w->carousel_selected, .opacity = w->opacity,
        .cursor = {w->position.x / w->options.radius, w->position.y / w->options.radius}};
    return true;
}
bool qa_hud_wheel_round_ready(const qa_hud_wheel *wheel) { return wheel && !wheel->busy; }
typedef struct wheel_draw {
    const qa_hud_wheel_draw_options *options;
    qa_scene_frame *frame;
    qa_error *error;
    float scale, x, y;
} wheel_draw;
static bool draw_quad(wheel_draw *d, const qa_scene_image *image, float x, float y, float w, float h,
                      qa_scene_vec4 uv, qa_scene_vec4 color) {
    return !image || qa_scene_frame_picture_f(d->frame, image, d->options->viewport,
        (qa_scene_rect_f){d->x + x * d->scale, d->y + y * d->scale, w * d->scale, h * d->scale}, uv, color, d->error);
}
static bool draw_fill(wheel_draw *d, float x, float y, float w, float h, qa_scene_vec4 color) {
    return draw_quad(d, d->options->white, x, y, w, h, (qa_scene_vec4){0, 0, 1, 1}, color);
}
static bool draw_text(wheel_draw *d, const char *text, float x, float y, qa_scene_vec4 color) {
    if (!text || !*text) return true;
    qa_font_layout layout;
    qa_font_layout_options options = {.text = {(const uint8_t *)text, strlen(text)}, .scale = d->scale,
        .color = color, .color_codes = QA_FONT_COLOR_LITERAL, .alignment = QA_FONT_ALIGN_CENTER};
    if (!qa_font_layout_build(&d->options->fonts, &options, &d->frame->storage, &layout, d->error)) return false;
    qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)layout.glyphs;
    for (size_t row=0;row<layout.line_count;++row) {
        const qa_font_line *line=layout.lines+row;
        for (size_t i=0;i<line->glyph_count;++i) glyphs[line->first_glyph+i].rect.x-=line->width*.5f;
    }
    qa_font_draw_options draw = {.seat = d->options->fonts.seat, .target = d->options->viewport,
        .origin = {d->x + x * d->scale - (float)d->options->viewport.x,
                   d->y + y * d->scale - (float)d->options->viewport.y},
        .space = QA_FONT_PIXELS, .shadow_offset = d->scale};
    return qa_font_draw_layout(d->frame, &layout, &draw, d->error);
}
static bool color_valid(qa_scene_vec4 color) {
    return isfinite(color.x) && isfinite(color.y) && isfinite(color.z) && isfinite(color.w);
}
bool qa_hud_wheel_draw(qa_hud_wheel *w, const qa_hud_wheel_draw_options *options,
                       qa_scene_frame *frame, qa_error *error) {
    if (!w || !options || !frame || !options->fonts.classic || !options->white ||
        options->fonts.seat != w->options.seat || !isfinite(options->scale) || options->scale <= 0 ||
        !color_valid(options->text) || !color_valid(options->accent) ||
        !color_valid(options->disabled) || !color_valid(options->panel))
        return wheel_fail(error, "invalid wheel drawing resources");
    if (!begin(w, error)) return false;
    float base = fminf((float)options->viewport.width / 640.f, (float)options->viewport.height / 480.f);
    if (base <= 0) { w->busy = false; return true; }
    wheel_draw d = {.options = options, .frame = frame, .error = error,
        .scale = base * fminf(1, options->scale)};
    d.x = (float)options->viewport.x + (float)options->viewport.width / 2.f - 320 * d.scale;
    d.y = (float)options->viewport.y + (float)options->viewport.height / 2.f - 240 * d.scale;
    bool ok = true;
    if (w->opacity > 0 || w->open) {
        ok = observe(w, w->mode, false, error);
        float opacity = options->reduced_flashes ? 1 : w->opacity;
        qa_scene_vec4 panel = options->panel; panel.w *= opacity;
        if (ok) ok = draw_fill(&d, 128, 48, 384, 384, panel);
        for (size_t i = 0; ok && i < w->count; ++i) {
            const qa_hud_wheel_item *item = &w->items[i];
            double angle = (double)i * 6.2831853071795864769 / (double)w->count;
            float x = 320 + (float)sin(angle) * 136, y = 240 - (float)cos(angle) * 136;
            bool selected = item->identity.key == w->selected;
            qa_scene_vec4 tint = selected ? options->accent : item->owned ? options->text : options->disabled;
            tint.w = opacity;
            const qa_scene_image *icon = selected && item->selected_icon ? item->selected_icon : item->icon;
            if (icon) ok = draw_quad(&d, icon, x - 20, y - 20, 40, 40, (qa_scene_vec4){0, 0, 1, 1}, tint);
            else ok = draw_text(&d, item->label, x, y - 8, tint);
            if (ok && item->has_count) {
                char count[32]; ok = qa_format_number(item->count, count, error);
                qa_scene_vec4 color = item->count <= item->warning_count ? options->accent : tint; color.w = opacity;
                if (ok) ok = draw_text(&d, count, x, y + 24, color);
            }
            if (ok && selected) ok = draw_text(&d, item->label, 320, 220, tint);
        }
        if (ok) ok = draw_fill(&d, 318 + w->position.x / w->options.radius * 150,
                               238 + w->position.y / w->options.radius * 150, 4, 4, options->accent);
    }
    if (ok && w->carousel == CAROUSEL_OPEN) {
        ok = observe(w, QA_HUD_WHEEL_WEAPONS, true, error);
        d.scale = base * options->scale;
        d.x = (float)options->viewport.x + (float)options->viewport.width / 2.f - 320 * d.scale;
        d.y = (float)options->viewport.y + (float)options->viewport.height - 480 * d.scale;
        float width = fminf(48, 600.f / (float)(w->count ? w->count : 1));
        float start = 320 - (float)w->count * width / 2;
        for (size_t i = 0; ok && i < w->count; ++i) {
            const qa_hud_wheel_item *item = &w->items[i];
            float x = start + (float)i * width;
            bool selected = item->identity.key == w->carousel_selected;
            if (selected) ok = draw_fill(&d, x, 324, width - 2, 50, options->accent);
            const qa_scene_image *icon = selected && item->selected_icon ? item->selected_icon : item->icon;
            if (ok && icon && width > 10) ok = draw_quad(&d, icon, x + 4, 328, width - 10, width - 10,
                                                       (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){1, 1, 1, 1});
            if (ok && item->has_count) {
                char count[32]; ok = qa_format_number(item->count, count, error);
                if (ok) ok = draw_text(&d, count, x + width / 2, 358, selected ? options->accent : options->text);
            }
        }
    }
    w->busy = false; return ok;
}

static bool wheel_key(qa_source_save_io *io, const qa_hud_wheel_checkpoint_refs *refs,
    qa_hud_wheel_mode mode, uint64_t *value)
{
    bool present=*value!=0; uint64_t key=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE && present && (!refs || !refs->encode ||
        !refs->encode(refs->context,mode,*value,&key,io->error))) return false;
    if (!qa_source_save_bool(io,&present) || !qa_source_save_u64(io,&key)) return false;
    if (!present && key) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        *value=0;
        if (present && (!refs || !refs->decode || !refs->decode(refs->context,mode,key,value,io->error) || !*value)) return false;
    }
    return true;
}
static bool wheel_fields(qa_source_save_io *io, qa_hud_wheel *saved, const qa_hud_wheel *qualified,
    const qa_hud_wheel_checkpoint_refs *refs)
{
    uint8_t magic[4]={'Q','A','W','H'}; uint32_t seat=qualified->options.seat;
    uint32_t mode=saved->mode,carousel=saved->carousel;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QAWH",4) ||
        !qa_source_save_u32(io,&seat) || seat!=qualified->options.seat) return false;
    float radius=qualified->options.radius,distance=qualified->options.selection_distance,fade=qualified->options.fade_per_second;
    uint64_t timeout=qualified->options.carousel_timeout_ns,lock=qualified->options.carousel_lock_ns;
    bool deselect=qualified->options.q2_slot_zero_deselect;
    if (!qa_source_save_f32(io,&radius) || memcmp(&radius,&qualified->options.radius,sizeof(radius)) ||
        !qa_source_save_f32(io,&distance) || memcmp(&distance,&qualified->options.selection_distance,sizeof(distance)) ||
        !qa_source_save_f32(io,&fade) || memcmp(&fade,&qualified->options.fade_per_second,sizeof(fade)) ||
        !qa_source_save_u64(io,&timeout) || timeout!=qualified->options.carousel_timeout_ns ||
        !qa_source_save_u64(io,&lock) || lock!=qualified->options.carousel_lock_ns ||
        !qa_source_save_bool(io,&deselect) || deselect!=qualified->options.q2_slot_zero_deselect ||
        !qa_source_save_u32(io,&mode) || mode>QA_HUD_WHEEL_POWERUPS ||
        !qa_source_save_u32(io,&carousel) || carousel>CAROUSEL_CLOSING || !qa_source_save_bool(io,&saved->open)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) { saved->mode=(qa_hud_wheel_mode)mode; saved->carousel=(carousel_state)carousel; }
    if (!wheel_key(io,refs,saved->mode,&saved->selected) ||
        !wheel_key(io,refs,QA_HUD_WHEEL_WEAPONS,&saved->carousel_selected) ||
        !qa_source_save_u64(io,&saved->deselect_until) || !qa_source_save_u64(io,&saved->carousel_until) ||
        !qa_source_save_u64(io,&saved->lock_until) || !qa_source_save_u64(io,&saved->last_update) ||
        !qa_source_save_f32(io,&saved->position.x) || !qa_source_save_f32(io,&saved->position.y) ||
        !qa_source_save_f32(io,&saved->analog.x) || !qa_source_save_f32(io,&saved->analog.y) ||
        !qa_source_save_f32(io,&saved->opacity)) return false;
    return isfinite(saved->position.x) && isfinite(saved->position.y) && isfinite(saved->analog.x) && isfinite(saved->analog.y) &&
        isfinite(saved->opacity) && saved->opacity>=0 && saved->opacity<=1;
}
bool qa_hud_wheel_checkpoint(const qa_hud_wheel *wheel, const qa_hud_wheel_checkpoint_refs *refs,
    qa_buffer *out, qa_error *error)
{
    if (!wheel || !out || wheel->busy) return wheel_fail(error,"Wheel capture requires an idle owner");
    qa_hud_wheel saved=*wheel; qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=wheel_fields(&io,&saved,wheel,refs) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) wheel_fail(error,"Wheel continuation or item identity is inconsistent");
    qa_source_save_dispose(&io); return ok;
}
bool qa_hud_wheel_restore(qa_hud_wheel *wheel, const qa_hud_wheel_checkpoint_refs *refs,
    qa_bytes bytes, qa_error *error)
{
    if (!wheel || wheel->busy || wheel->count || wheel->open || wheel->carousel!=CAROUSEL_CLOSED)
        return wheel_fail(error,"Wheel restore requires an empty idle candidate");
    qa_hud_wheel saved=*wheel; qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=wheel_fields(&io,&saved,wheel,refs) && qa_source_save_finish(&io,NULL);
    if (ok) *wheel=saved;
    else if (error && error->code==QA_OK) wheel_fail(error,"Saved wheel continuation or item identity is inconsistent");
    qa_source_save_dispose(&io); return ok;
}
