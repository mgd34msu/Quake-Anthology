#include "internal.h"
#include "qa/hud.h"
#include "qa/source_save.h"
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
    bool drawing, checkpoint_active;
};
bool qa_hud_idle(const qa_hud *hud) { return hud && !hud->drawing && !hud->checkpoint_active; }
static bool hud_signature(qa_source_save_io *io) {
    uint8_t magic[4] = {'Q', 'A', 'H', 'D'}; uint32_t version = 3;
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QAHD", 4) &&
        qa_source_save_u32(io, &version) && version == 3;
}
static bool hud_reservation(qa_source_save_io *io, size_t count, size_t capacity) {
    if (count > capacity || capacity > SIZE_MAX / sizeof(hud_message)) return false;
    if (capacity) {
        size_t grown = 8;
        while (grown < capacity && grown <= SIZE_MAX / 2 && grown * 2 <= SIZE_MAX / sizeof(hud_message)) grown *= 2;
        if (capacity < 8 || (grown != capacity && capacity <= grown)) return false;
    }
    if (io->direction == QA_SOURCE_SAVE_READ && capacity > io->input.size - io->offset) return false;
    /* Each real reserved slot has a saved presence cell. Inactive slots retain
     * no message ownership; their old allocation bytes are operation scratch. */
    for (size_t i = 0; i < capacity; ++i) {
        bool active = i < count;
        if (!qa_source_save_bool(io, &active) || active != (i < count)) return false;
    }
    return true;
}
static bool hud_text(qa_source_save_io *io, char **owned) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool present = !reading && *owned;
    size_t length = present ? strlen(*owned) : 0;
    if (!qa_source_save_bool(io, &present) ||
        (present && !qa_source_save_count(io, &length, SIZE_MAX - 1))) return false;
    if (!reading) return !present || qa_source_save_bytes(io, *owned, length);
    char *copy = NULL;
    if (present) {
        if (io->offset > io->input.size || length > io->input.size - io->offset) {
            qa_error_set(io->error, QA_ERROR_FORMAT, 0, "Truncated HUD text"); return false;
        }
        copy = malloc(length + 1);
        if (!copy) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring HUD text"); return false; }
        if (!qa_source_save_bytes(io, copy, length)) { free(copy); return false; }
        if (memchr(copy, 0, length)) {
            free(copy); qa_error_set(io->error, QA_ERROR_FORMAT, 0, "HUD text contains a NUL byte"); return false;
        }
        copy[length] = 0;
    }
    free(*owned); *owned = copy;
    return true;
}
static bool hud_message_fields(qa_source_save_io *io, hud_message *message) {
    return hud_text(io, &message->text) && message->text && qa_source_save_u64(io, &message->starts) &&
        qa_source_save_u64(io, &message->until) && qa_source_save_u64(io, &message->character_ns) &&
        qa_source_save_bool(io, &message->chat) && qa_source_save_bool(io, &message->instant) && message->until >= message->starts;
}
static bool hud_image_fields(qa_source_save_io *io, const qa_hud_checkpoint_refs *refs, const qa_scene_image **image) {
    bool present = *image != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    uint64_t key = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        if (!refs || !refs->image_encode || !refs->image_encode(refs->context, *image, &key, io->error) || !key) {
            if (!refs || !refs->image_encode) qa_error_set(io->error, QA_ERROR_ARGUMENT, 0, "HUD image identity encoder is absent");
            return false;
        }
    }
    bool ok = qa_source_save_u64(io, &key) && key;
    if (ok && io->direction == QA_SOURCE_SAVE_READ) {
        const qa_scene_image *decoded = NULL;
        ok = refs && refs->image_decode && refs->image_decode(refs->context, key, &decoded, io->error) && decoded;
        if (!refs || !refs->image_decode) qa_error_set(io->error, QA_ERROR_ARGUMENT, 0, "HUD candidate image resolver is absent");
        if (ok) { qa_scene_image_retain(decoded); *image = decoded; }
    }
    return ok;
}
bool qa_hud_checkpoint(qa_hud *hud, const qa_hud_checkpoint_refs *refs, qa_buffer *out, qa_error *error) {
    if (!hud || !out || out->data || out->size || hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD checkpoint requires completed callbacks and empty output");
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, qa_application_session(hud->options.application), error)) return false;
    hud->checkpoint_active = true;
    uint32_t seat = hud->options.seat; size_t count = hud->notice_count, capacity = hud->notice_capacity;
    bool ok = hud_signature(&io) && qa_source_save_u32(&io, &seat) && qa_source_save_count(&io, &count, SIZE_MAX) &&
        qa_source_save_count(&io, &capacity, SIZE_MAX / sizeof(hud_message)) &&
        (!capacity || hud->notices) && hud_reservation(&io,count,capacity);
    for (size_t i = 0; ok && i < count; ++i) { hud_message copy = hud->notices[i]; ok = hud_message_fields(&io, &copy); }
    count = hud->center_count; capacity = hud->center_capacity;
    ok = ok && qa_source_save_count(&io, &count, SIZE_MAX) &&
        qa_source_save_count(&io, &capacity, SIZE_MAX / sizeof(hud_message)) &&
        (!capacity || hud->centers) && hud_reservation(&io,count,capacity);
    for (size_t i = 0; ok && i < count; ++i) { hud_message copy = hud->centers[i]; ok = hud_message_fields(&io, &copy); }
    char *pickup = hud->pickup; const qa_scene_image *icon = hud->pickup_icon;
    uint64_t pickup_until = hud->pickup_until, hit_until = hud->hit_until; float damage = hud->hit_damage;
    ok = ok && hud_text(&io, &pickup) && hud_image_fields(&io, refs, &icon) &&
        qa_source_save_u64(&io, &pickup_until) && (pickup || (!icon && !pickup_until)) &&
        qa_source_save_u64(&io, &hit_until) && qa_source_save_f32(&io, &damage) && isfinite(damage);
    if (ok) ok = qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid retained HUD state");
    qa_source_save_dispose(&io); hud->checkpoint_active = false; return ok;
}
bool qa_hud_restore(qa_bytes bytes, const qa_hud_options *options, const qa_hud_checkpoint_refs *refs, qa_hud **out, qa_error *error) {
    if (!options || !out || *out) return ui_fail(error, "HUD restore requires candidate options and empty output");
    qa_source_save_io io = {0}; uint32_t seat = 0; qa_hud *hud = NULL;
    bool ok = qa_source_save_reader(&io, qa_application_session(options->application), bytes, error) && hud_signature(&io) &&
        qa_source_save_u32(&io, &seat) && seat == options->seat && qa_hud_create(options, &hud, error);
    size_t count = 0, capacity = 0;
    if (ok) ok = qa_source_save_count(&io, &count, bytes.size / 8) &&
        qa_source_save_count(&io, &capacity, SIZE_MAX / sizeof(hud_message)) && hud_reservation(&io,count,capacity);
    if (ok && capacity) {
        hud->notices = calloc(capacity, sizeof(*hud->notices)); hud->notice_capacity = capacity;
        if (!hud->notices) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring retained HUD notice storage"); ok = false; }
    }
    for (size_t i = 0; ok && i < count; ++i) {
        hud_message *message = &hud->notices[hud->notice_count++]; *message = (hud_message){0}; ok = hud_message_fields(&io, message);
    }
    if (ok) ok = qa_source_save_count(&io, &count, bytes.size / 8) &&
        qa_source_save_count(&io, &capacity, SIZE_MAX / sizeof(hud_message)) && hud_reservation(&io,count,capacity);
    if (ok && capacity) {
        hud->centers = calloc(capacity, sizeof(*hud->centers)); hud->center_capacity = capacity;
        if (!hud->centers) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring retained HUD center storage"); ok = false; }
    }
    for (size_t i = 0; ok && i < count; ++i) {
        hud_message *message = &hud->centers[hud->center_count++]; *message = (hud_message){0}; ok = hud_message_fields(&io, message);
    }
    if (ok) ok = hud_text(&io, &hud->pickup) && hud_image_fields(&io, refs, &hud->pickup_icon) &&
        qa_source_save_u64(&io, &hud->pickup_until) && (hud->pickup || (!hud->pickup_icon && !hud->pickup_until)) &&
        qa_source_save_u64(&io, &hud->hit_until) &&
        qa_source_save_f32(&io, &hud->hit_damage) && isfinite(hud->hit_damage) && qa_source_save_finish(&io, NULL);
    if (!ok) {
        qa_hud_destroy(hud, NULL);
        if (!error || error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid saved HUD state");
    } else *out = hud;
    qa_source_save_dispose(&io); return ok;
}
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
    if (hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD callback or checkpoint is active");
    for (size_t i = 0; i < hud->notice_count; ++i) free(hud->notices[i].text);
    hud->notice_count = 0;
    return true;
}
bool qa_hud_clear_center(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD callback or checkpoint is active");
    for (size_t i = 0; i < hud->center_count; ++i) free(hud->centers[i].text);
    hud->center_count = 0;
    return true;
}
bool qa_hud_destroy(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD callback or checkpoint is active");
    qa_hud_clear_notify(hud, NULL); qa_hud_clear_center(hud, NULL);
    free(hud->notices); free(hud->centers); free(hud->pickup); qa_scene_image_release(hud->pickup_icon); free(hud);
    return true;
}
bool qa_hud_notify(qa_hud *hud, const char *text, bool chat, uint64_t starts,
                    uint64_t duration, qa_error *error) {
    if (!hud || hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD message callback or checkpoint is active");
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
    if (!hud || hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD center callback or checkpoint is active");
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
    if (!hud || hud->drawing || hud->checkpoint_active) return ui_fail(error, "HUD pickup callback or checkpoint is active");
    char *copy = copy_text(text, error); if (!copy) return false;
    qa_scene_image_retain(icon); qa_scene_image_release(hud->pickup_icon);
    free(hud->pickup); hud->pickup = copy; hud->pickup_icon = icon; hud->pickup_until = until;
    return true;
}
void qa_hud_hit_marker(qa_hud *hud, float damage, uint64_t until) {
    if (hud && !hud->drawing && !hud->checkpoint_active && isfinite(damage)) { hud->hit_damage = damage; hud->hit_until = until; }
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
static bool caption_draw(qa_ui *ui,const qa_active_caption *values,size_t count,qa_scene_rect target,
    qa_scene_rect_f area,float fit,qa_scene_frame *scene,qa_error *error)
{
    float width=area.width,height=area.height,scale=fit*ui->text_scale;
    if (!count || width<16 || height<12) return true;
    qa_font_info font;
    if (!qa_font_describe(ui->options.fonts.primary?ui->options.fonts.primary:ui->options.fonts.classic,&font))
        return ui_fail(error,"Caption presentation lost its actual font metrics");
    float cap_height=font.has_cap_ink?font.cap_height:8,cap_top=font.has_cap_ink?font.cap_top:0;
    float line_height=ceilf(cap_height*scale)+4;
    if (line_height+8>height) return true;
    if (count>SIZE_MAX/sizeof(qa_font_layout)) return ui_fail(error,"Caption layout count overflows");
    qa_font_layout *layouts=qa_arena_alloc(&scene->storage,count*sizeof(*layouts),_Alignof(qa_font_layout),error);
    if (!layouts) return false;
    size_t lines=0;
    for (size_t i=0;i<count;++i) {
        const qa_active_caption *caption=values+i;
        if (!caption->text) return ui_fail(error,"Caption presentation lost its localized text");
        const char *speaker=caption->speaker?caption->speaker:"",*value=caption->text;
        size_t speaker_size=strlen(speaker),text_size=strlen(value);
        if (speaker_size>SIZE_MAX-3 || text_size>SIZE_MAX-speaker_size-3)
            return ui_fail(error,"Caption text extent overflows");
        char *joined=qa_arena_alloc(&scene->storage,speaker_size+text_size+3,1,error);
        if (!joined) return false;
        size_t offset=0;
        if (speaker_size) { memcpy(joined,speaker,speaker_size); offset=speaker_size; joined[offset++]=':'; joined[offset++]=' '; }
        memcpy(joined+offset,value,text_size+1);
        qa_font_layout_options options={.text={(const uint8_t *)joined,offset+text_size},.scale=scale,
            .color={1,1,1,1},.color_codes=QA_FONT_COLOR_LITERAL,.force_color=true,
            .alignment=QA_FONT_ALIGN_CENTER,.max_width=width-8};
        if (!qa_font_layout_build(&ui->options.fonts,&options,&scene->storage,layouts+i,error)) return false;
        if (layouts[i].line_count>SIZE_MAX-lines) return ui_fail(error,"Caption line count overflows");
        lines+=layouts[i].line_count;
    }
    long double maximum=floorl(((long double)height-8)/(long double)line_height);
    size_t visible=maximum>=(long double)lines?lines:(size_t)maximum;
    float panel_height=(float)visible*line_height+8;
    float x=area.x,y=area.y+area.height-panel_height;
    if (!qa_scene_frame_picture_f(scene,ui->options.white,target,(qa_scene_rect_f){x,y,width,panel_height},
        (qa_scene_vec4){0,0,1,1},(qa_scene_vec4){0,0,0,.92f},error)) return false;
    size_t skip=lines-visible,shown=0;
    for (size_t i=0;i<count;++i) {
        qa_font_layout *layout=layouts+i;
        for (size_t row=0;row<layout->line_count;++row) {
            if (skip) { --skip; continue; }
            const qa_font_line *line=layout->lines+row;
            qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)layout->glyphs+line->first_glyph;
            for (size_t j=0;j<line->glyph_count;++j) glyphs[j].rect.y-=line->y;
            qa_font_line single={line->width,0,0,line->glyph_count};
            qa_font_layout draw=*layout; draw.glyphs=glyphs; draw.glyph_count=line->glyph_count;
            draw.lines=&single; draw.line_count=1; draw.height=draw.line_height;
            qa_font_draw_options options={.seat=ui->options.seat,.target=target,.space=QA_FONT_PIXELS,
                .origin={x+4-(float)target.x,y+4+(float)shown*line_height-cap_top*scale-(float)target.y},.shadow_offset=fit};
            if (!qa_font_draw_layout(scene,&draw,&options,error)) return false;
            ++shown;
        }
    }
    return true;
}
bool qa_ui_captions_draw(qa_ui *ui,qa_scene_frame *scene,qa_scene_rect target,qa_scene_rect_f area,
    float fit,const qa_active_caption *values,size_t count,qa_error *error)
{
    if (!qa_ui_idle(ui) || !scene || (count && !values) || !isfinite(fit) || fit<=0 ||
        !isfinite(area.x) || !isfinite(area.y) || !isfinite(area.width) || !isfinite(area.height) ||
        area.width<0 || area.height<0 || !isfinite(area.x+area.width) || !isfinite(area.y+area.height))
        return ui_fail(error,"Caption draw requires its idle actual seat UI and finite viewport region");
    if (!target.width || !target.height) return true;
    ui->handling=ui->drawing=true;
    bool ok=caption_draw(ui,values,count,target,area,fit,scene,error);
    ui->handling=ui->drawing=false; return ok;
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
        float size=data.crosshair_size>0?data.crosshair_size:8;
        if (!isfinite(size) || size<2 || size>32) return ui_fail(error,"Invalid HUD crosshair size");
        if (data.crosshair) {
            float picture_size=data.crosshair_size>0?size:16;
            qa_scene_rect_f pixels = {ui->bias_x + (320-picture_size*.5f) * ui->scale,
                ui->bias_y + (240-picture_size*.5f) * ui->scale,
                picture_size * ui->scale, picture_size * ui->scale};
            if (!qa_scene_frame_picture_f(scene, data.crosshair, target, pixels,
                (qa_scene_vec4){0, 0, 1, 1}, data.crosshair_color, error)) return false;
        } else if (!ui_fill(ui, scene, target, (qa_scene_rect_f){320-size*.125f, 240-size*.5f, size*.25f, size}, data.crosshair_color, error) ||
                   !ui_fill(ui, scene, target, (qa_scene_rect_f){320-size*.5f, 240-size*.125f, size, size*.25f}, data.crosshair_color, error)) return false;
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
        if (!ui_draw_source_text(ui, scene, target, 16, 20 + (float)i * 16, notice->text,
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
        if (!ui_draw_source_text(ui, scene, target, 320, 180, value, (qa_scene_vec4){1, 1, 1, 1}, 1.2f,
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
    float fit=fminf((float)target.width/640,(float)target.height/480);
    return caption_draw(ui,data.captions,data.caption_count,target,
        (qa_scene_rect_f){(float)target.x+8,(float)target.y+(float)target.height*.60f,
            (float)target.width-16,(float)target.height*.22f},fit,scene,error);
}
bool qa_hud_draw(qa_hud *hud, const qa_hud_frame *frame, qa_scene_frame *scene, qa_error *error) {
    if (!hud || !frame || !scene || frame->seat != hud->options.seat || hud->drawing || hud->checkpoint_active ||
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
