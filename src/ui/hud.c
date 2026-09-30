#include "internal.h"
#include "qa/hud.h"
#include <stdio.h>

typedef struct hud_message { char *text; uint64_t starts, until, character_ns; bool chat, instant; } hud_message;
struct qa_hud {
    qa_hud_options options;
    hud_message *notices, *centers;
    size_t notice_count, notice_capacity, center_count, center_capacity;
    char *pickup;
    const qa_scene_image *pickup_icon;
    uint64_t pickup_until, hit_until;
    float hit_damage;
    bool drawing;
};
static uint64_t after(uint64_t now, uint64_t duration) {
    return UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
}
static char *copy_text(const char *text, qa_error *error) {
    if (!text) { ui_fail(error, "missing HUD text"); return NULL; }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating HUD text"); return NULL; }
    memcpy(copy, text, length + 1);
    return copy;
}
bool qa_hud_create(const qa_hud_options *options, qa_hud **out, qa_error *error) {
    if (!options || !out || !options->ui || !options->application ||
        options->ui->options.seat != options->seat)
        return ui_fail(error, "HUD requires matching seat UI and application");
    qa_hud *hud = calloc(1, sizeof(*hud));
    if (!hud) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating seat HUD"); return false; }
    hud->options = *options;
    *out = hud;
    return true;
}
bool qa_hud_clear_notify(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing) return ui_fail(error, "HUD draw callback is active");
    for (size_t i = 0; i < hud->notice_count; ++i) free(hud->notices[i].text);
    hud->notice_count = 0;
    return true;
}
bool qa_hud_clear_center(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing) return ui_fail(error, "HUD draw callback is active");
    for (size_t i = 0; i < hud->center_count; ++i) free(hud->centers[i].text);
    hud->center_count = 0;
    return true;
}
bool qa_hud_destroy(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing) return ui_fail(error, "HUD draw callback is active");
    qa_hud_clear_notify(hud, NULL); qa_hud_clear_center(hud, NULL);
    free(hud->notices); free(hud->centers); free(hud->pickup); free(hud);
    return true;
}
bool qa_hud_notify(qa_hud *hud, const char *text, bool chat, uint64_t starts,
                    uint64_t duration, qa_error *error) {
    if (!hud || hud->drawing) return ui_fail(error, "HUD message callback is active");
    char *copy = copy_text(text, error);
    if (!copy) return false;
    if (!ui_reserve((void **)&hud->notices, &hud->notice_capacity, hud->notice_count + 1,
                     sizeof(*hud->notices), error)) { free(copy); return false; }
    hud->notices[hud->notice_count++] = (hud_message){.text = copy, .starts = starts,
        .until = after(starts, duration), .chat = chat, .instant = true};
    return true;
}
bool qa_hud_center_print(qa_hud *hud, const char *text, uint64_t starts, uint64_t duration,
                          bool instant, uint64_t character_ns, qa_error *error) {
    if (!hud || hud->drawing) return ui_fail(error, "HUD center callback is active");
    char *copy = copy_text(text, error);
    if (!copy) return false;
    if (!ui_reserve((void **)&hud->centers, &hud->center_capacity, hud->center_count + 1,
                     sizeof(*hud->centers), error)) { free(copy); return false; }
    if (instant) qa_hud_clear_center(hud, NULL);
    else if (hud->center_count && starts < hud->centers[hud->center_count - 1].until)
        starts = hud->centers[hud->center_count - 1].until;
    if (!instant) {
        qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
        size_t cursor = 0; uint32_t scalar;
        while (qa_utf8_next(bytes, &cursor, &scalar)) duration = after(duration, character_ns);
    }
    hud->centers[hud->center_count++] = (hud_message){.text = copy, .starts = starts,
        .until = after(starts, duration), .instant = instant, .character_ns = character_ns};
    return true;
}
bool qa_hud_pickup(qa_hud *hud, const char *text, const qa_scene_image *icon, uint64_t until,
                    qa_error *error) {
    if (!hud || hud->drawing) return ui_fail(error, "HUD pickup callback is active");
    char *copy = copy_text(text, error); if (!copy) return false;
    free(hud->pickup); hud->pickup = copy; hud->pickup_icon = icon; hud->pickup_until = until;
    return true;
}
void qa_hud_hit_marker(qa_hud *hud, float damage, uint64_t until) {
    if (hud && !hud->drawing && isfinite(damage)) { hud->hit_damage = damage; hud->hit_until = until; }
}
static void expire(hud_message *messages, size_t *count, uint64_t now) {
    size_t retained = 0;
    for (size_t i = 0; i < *count; ++i) {
        if (messages[i].until <= now) free(messages[i].text);
        else messages[retained++] = messages[i];
    }
    *count = retained;
}
static bool text(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target, float x, float y,
                  const char *value, qa_scene_vec4 color, float scale, qa_font_alignment align,
                  qa_error *error) {
    return ui_draw_text(hud->options.ui, scene, target, x, y, value, color, scale, align, error);
}
static bool icon(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target,
                  const qa_scene_image *image, qa_scene_rect_f rect, qa_scene_vec4 color,
                  qa_error *error) {
    if (!image) return true;
    qa_ui *ui = hud->options.ui;
    qa_scene_rect_f pixels = {ui->bias_x + rect.x * ui->scale, ui->bias_y + rect.y * ui->scale,
                             rect.width * ui->scale, rect.height * ui->scale};
    return qa_scene_frame_picture_f(scene, image, target, pixels,
        (qa_scene_vec4){0, 0, 1, 1}, color, error);
}
static bool number(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target, float x, float y,
                    const char *label, double value, bool warning, qa_error *error) {
    char numeric[32];
    if (!qa_format_number(value, numeric, error)) return false;
    qa_scene_vec4 color = warning ? (qa_scene_vec4){1, .3f, .2f, 1} : (qa_scene_vec4){1, 1, 1, 1};
    return text(hud, scene, target, x, y, label, color, .9f, QA_FONT_ALIGN_CENTER, error) &&
           text(hud, scene, target, x, y + 13, numeric, color, 1.5f, QA_FONT_ALIGN_CENTER, error);
}
static bool draw(qa_hud *hud, const qa_hud_frame *frame, qa_scene_frame *scene, qa_error *error) {
    qa_ui *ui = hud->options.ui;
    ui->scale = fminf((float)frame->safe_area.width / 640, (float)frame->safe_area.height / 480) * frame->scale;
    if (!isfinite(ui->scale) || ui->scale <= 0) return ui_fail(error, "HUD scale overflow");
    ui->bias_x = frame->safe_area.x + ((float)frame->safe_area.width - 640 * ui->scale) * .5f;
    ui->bias_y = frame->safe_area.y + ((float)frame->safe_area.height - 480 * ui->scale) * .5f;
    qa_hud_data data = {.crosshair_visible = true, .crosshair_color = {1, 1, 1, 1}};
    if (hud->options.read && !hud->options.read(hud->options.context, frame, &data, error)) return false;
    if ((data.vital_count && !data.vitals) || (data.bar_count && !data.bars) ||
        (data.timer_count && !data.timers) || (data.score_count && !data.scores) ||
        (data.help_count && !data.help_lines) || (data.caption_count && !data.captions))
        return ui_fail(error, "HUD source returned invalid spans");
    qa_scene_rect target = frame->safe_area;
    if (hud->options.source_draw && !hud->options.source_draw(hud->options.context,
        frame, scene, error)) return false;
    qa_combat_state combat;
    if (frame->actor.registry && !data.source_vitals && !qa_combat_read(qa_application_combat(hud->options.application),
        frame->actor, &combat, error)) return false;
    if (frame->actor.registry && !data.source_vitals) {
        if (!number(hud, scene, target, 160, 434, "Health", combat.health, combat.health <= 25, error) ||
            !number(hud, scene, target, 320, 434, "Armor", combat.armor.regular.points, false, error)) return false;
    }
    qa_inventory *inventory = qa_application_inventory(hud->options.application);
    qa_item_definition *definitions = NULL;
    size_t definition_count = 0;
    if (frame->actor.registry && (data.selected_weapon || frame->show_inventory)) {
        if (!qa_inventory_item_definitions(inventory, frame->actor, NULL, 0, &definition_count, error)) return false;
        if (definition_count > SIZE_MAX / sizeof(*definitions)) return ui_fail(error, "HUD item definition overflow");
        if (definition_count) {
            definitions = qa_arena_alloc(&scene->storage, definition_count * sizeof(*definitions),
                                          _Alignof(qa_item_definition), error);
            if (!definitions || !qa_inventory_item_definitions(inventory, frame->actor,
                definitions, definition_count, &definition_count, error)) return false;
            for (size_t i = 0; i < definition_count; ++i) {
                const char *label = definitions[i].label ? definitions[i].label : "";
                size_t length = strlen(label);
                char *copy = qa_arena_alloc(&scene->storage, length + 1, 1, error);
                if (!copy) return false;
                memcpy(copy, label, length + 1); definitions[i].label = copy;
            }
        }
    }
    if (frame->actor.registry && data.selected_weapon && !data.source_vitals) {
        for (size_t i = 0; i < definition_count; ++i) {
            const qa_item_definition *weapon = &definitions[i];
            if (weapon->item != data.selected_weapon || !weapon->ammo) continue;
            qa_inventory_entry ammo;
            if (!qa_inventory_entry_read(inventory, frame->actor, weapon->ammo, &ammo, error) ||
                !number(hud, scene, target, 480, 434, weapon->label, ammo.count, ammo.count <= 0, error)) return false;
            break;
        }
    }
    for (size_t i = 0; i < data.vital_count; ++i) {
        float x = 80 + (float)i * 110;
        if (!icon(hud, scene, target, data.vitals[i].icon, (qa_scene_rect_f){x - 12, 374, 24, 24},
                    (qa_scene_vec4){1, 1, 1, 1}, error) ||
            !number(hud, scene, target, x, 398, data.vitals[i].label,
                    data.vitals[i].value, data.vitals[i].warning, error)) return false;
    }
    for (size_t i = 0; i < data.bar_count; ++i) {
        const qa_hud_value *bar = &data.bars[i];
        float width = bar->maximum > 0 ? (float)fmax(0, fmin(1, bar->value / bar->maximum)) * 240 : 0;
        float y = 90 + (float)i * 24;
        if (!ui_fill(ui, scene, target, (qa_scene_rect_f){200, y, 240, 16},
                       (qa_scene_vec4){.1f, .1f, .1f, .8f}, error) ||
            !ui_fill(ui, scene, target, (qa_scene_rect_f){200, y, width, 16},
                       (qa_scene_vec4){.7f, .2f, .1f, .9f}, error) ||
            !text(hud, scene, target, 320, y + 2, bar->label, (qa_scene_vec4){1, 1, 1, 1},
                    .9f, QA_FONT_ALIGN_CENTER, error)) return false;
    }
    for (size_t i = 0; i < data.timer_count; ++i) {
        const qa_hud_timer *timer = &data.timers[i];
        if (timer->until_ns <= frame->time_ns) continue;
        double seconds = ceil((double)(timer->until_ns - frame->time_ns) / 1e9);
        float y = 120 + (float)i * 40;
        if (!icon(hud, scene, target, timer->icon, (qa_scene_rect_f){544, y, 28, 28},
            (qa_scene_vec4){1, 1, 1, 1}, error) ||
            !number(hud, scene, target, 590, y, timer->label, seconds, seconds <= 5, error)) return false;
    }
    if (data.crosshair_visible && !frame->show_scores && !frame->show_inventory) {
        if (data.crosshair) {
            qa_scene_rect_f pixels = {ui->bias_x + 312 * ui->scale, ui->bias_y + 232 * ui->scale,
                                     16 * ui->scale, 16 * ui->scale};
            if (!qa_scene_frame_picture_f(scene, data.crosshair, target, pixels,
                (qa_scene_vec4){0, 0, 1, 1}, data.crosshair_color, error)) return false;
        } else if (!ui_fill(ui, scene, target, (qa_scene_rect_f){319, 236, 2, 8}, data.crosshair_color, error) ||
                   !ui_fill(ui, scene, target, (qa_scene_rect_f){316, 239, 8, 2}, data.crosshair_color, error)) return false;
    }
    if (frame->show_inventory && frame->actor.registry) {
        size_t count = 0;
        if (!qa_inventory_entries(inventory, frame->actor, NULL, 0, &count, error)) return false;
        if (count > SIZE_MAX / sizeof(qa_inventory_entry)) return ui_fail(error, "HUD inventory overflow");
        qa_inventory_entry *entries = count ? qa_arena_alloc(&scene->storage, count * sizeof(*entries),
            _Alignof(qa_inventory_entry), error) : NULL;
        if (count && (!entries || !qa_inventory_entries(inventory, frame->actor, entries, count, &count, error))) return false;
        float y = 110;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].count <= 0) continue;
            for (size_t j = 0; j < definition_count; ++j) {
                if (definitions[j].item != entries[i].item) continue;
                if (!number(hud, scene, target, 320, y, definitions[j].label, entries[i].count, false, error)) return false;
                y += 30; break;
            }
        }
    }
    if (frame->show_scores) {
        for (size_t i = 0; i < data.score_count; ++i) {
            char row[256];
            const qa_hud_score *score = &data.scores[i];
            snprintf(row, sizeof(row), "%s  %s  %d  %d%s", score->name ? score->name : "",
                score->team ? score->team : "", score->score, score->ping, score->spectator ? "  spectator" : "");
            if (!text(hud, scene, target, 64, 110 + (float)i * 20, row,
                score->local ? (qa_scene_vec4){1, .8f, .3f, 1} : (qa_scene_vec4){1, 1, 1, 1},
                1, QA_FONT_ALIGN_LEFT, error)) return false;
        }
    }
    for (size_t i = 0; i < hud->notice_count; ++i) {
        const hud_message *notice = &hud->notices[i];
        if (notice->starts > frame->time_ns) continue;
        if (!text(hud, scene, target, 16, 20 + (float)i * 16, notice->text,
            notice->chat ? (qa_scene_vec4){.6f, 1, .6f, 1} : (qa_scene_vec4){1, 1, 1, 1},
            1, QA_FONT_ALIGN_LEFT, error)) return false;
    }
    if (hud->center_count && hud->centers[0].starts <= frame->time_ns) {
        const hud_message *center = &hud->centers[0];
        const char *value = center->text;
        if (!center->instant && center->character_ns) {
            uint64_t characters = (frame->time_ns - center->starts) / center->character_ns;
            qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
            size_t offset = 0; uint32_t scalar;
            while (characters && qa_utf8_next(bytes, &offset, &scalar)) --characters;
            char *shown = qa_arena_alloc(&scene->storage, offset + 1, 1, error);
            if (!shown) return false;
            memcpy(shown, value, offset); shown[offset] = 0; value = shown;
        }
        if (!text(hud, scene, target, 320, 180, value, (qa_scene_vec4){1, 1, 1, 1}, 1.2f,
            QA_FONT_ALIGN_CENTER, error)) return false;
    }
    if (hud->pickup && hud->pickup_until > frame->time_ns &&
        (!icon(hud, scene, target, hud->pickup_icon, (qa_scene_rect_f){304, 328, 32, 32},
            (qa_scene_vec4){1, 1, 1, 1}, error) || !text(hud, scene, target, 320, 360, hud->pickup, (qa_scene_vec4){1, 1, .5f, 1},
               1, QA_FONT_ALIGN_CENTER, error))) return false;
    if (hud->hit_damage > 0 && hud->hit_until > frame->time_ns &&
        !text(hud, scene, target, 320, 256, "X", (qa_scene_vec4){1, 1, 1, 1}, 1,
               QA_FONT_ALIGN_CENTER, error)) return false;
    if (data.help_title && !text(hud, scene, target, 24, 90, data.help_title,
        (qa_scene_vec4){1, 1, 1, 1}, 1, QA_FONT_ALIGN_LEFT, error)) return false;
    for (size_t i = 0; i < data.help_count; ++i)
        if (!text(hud, scene, target, 24, 108 + (float)i * 16, data.help_lines[i],
            (qa_scene_vec4){1, 1, 1, 1}, 1, QA_FONT_ALIGN_LEFT, error)) return false;
    for (size_t i = 0; i < data.caption_count; ++i) {
        const qa_active_caption *caption = &data.captions[i];
        float y = 368 - (float)(data.caption_count - i - 1) * 18;
        if (caption->speaker && *caption->speaker && !text(hud, scene, target, 320, y - 10,
            caption->speaker, (qa_scene_vec4){1, .8f, .4f, 1}, .8f, QA_FONT_ALIGN_CENTER, error)) return false;
        if (!text(hud, scene, target, 320, y, caption->text, (qa_scene_vec4){1, 1, 1, 1},
            1, QA_FONT_ALIGN_CENTER, error)) return false;
    }
    return true;
}
bool qa_hud_draw(qa_hud *hud, const qa_hud_frame *frame, qa_scene_frame *scene, qa_error *error) {
    if (!hud || !frame || !scene || frame->seat != hud->options.seat || hud->drawing ||
        hud->options.ui->handling ||
        !frame->safe_area.width || !frame->safe_area.height || !isfinite(frame->scale) || frame->scale <= 0 ||
        (double)frame->safe_area.x + frame->safe_area.width > INT32_MAX ||
        (double)frame->safe_area.y + frame->safe_area.height > INT32_MAX)
        return ui_fail(error, "invalid or reentrant seat HUD draw");
    expire(hud->notices, &hud->notice_count, frame->time_ns);
    expire(hud->centers, &hud->center_count, frame->time_ns);
    if (!frame->visible) return true;
    qa_ui *ui = hud->options.ui;
    float scale = ui->scale, x = ui->bias_x, y = ui->bias_y;
    hud->drawing = true;
    ui->handling = true;
    ui->drawing = true;
    bool ok = draw(hud, frame, scene, error);
    hud->drawing = false;
    ui->handling = false;
    ui->drawing = false;
    ui->scale = scale; ui->bias_x = x; ui->bias_y = y;
    return ok;
}
