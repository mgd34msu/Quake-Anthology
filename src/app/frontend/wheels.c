#include "internal.h"
#include "equipment_media.h"
#include "qa/application_weapon_availability.h"
#include "qa/application_equipment.h"
#include "qa/application_native_q2_presentation.h"
#include <inttypes.h>
#include <stdio.h>

static bool definition_row(frontend_seat *seat, qa_actor_id actor,
    const qa_item_definition *definition, bool availability, int32_t ordinal,
    qa_hud_wheel_item *out, qa_error *error)
{
    qa_inventory *inventory = qa_application_inventory(seat->frontend->application);
    double owned;
    if (!qa_inventory_count_read(inventory, actor, definition->item, &owned, error)) return false;
    bool ammunition = owned > 0;
    double amount = owned;
    if (definition->ammo) {
        double ammo;
        if (!qa_inventory_count_read(inventory, actor, definition->ammo, &ammo, error)) return false;
        ammunition = ammo > 0; amount = ammo;
    }
    if (definition->weapon && availability) {
        bool available, found;
        if (!qa_application_weapon_availability_read(seat->frontend->application, actor,
            definition, &available, &found, error)) return false;
        if (found) ammunition = available;
    }
    *out = (qa_hud_wheel_item){
        .identity = {.key = definition->item, .item = definition->item, .source_ordinal = ordinal},
        .sort_order = ordinal, .label = definition->label ? definition->label : "Item",
        .owned = owned > 0, .has_ammunition = ammunition, .has_count = true, .count = amount};
    return true;
}
typedef struct q1_wheel_rows {
    qa_actor_owner owner;
    const frontend_q1_wheel_slot *slots;
    size_t count;
} q1_wheel_rows;
static uint64_t q1_wheel_key(qa_actor_owner owner, size_t ordinal)
{ return (uint64_t)owner << 32 | (uint32_t)ordinal; }
static bool authored_rows(frontend_seat *seat, qa_actor_id actor,
    const qa_item_definition *definitions, size_t definition_count,
    const qa_hud_wheel_item *original, size_t original_count,
    const qa_hud_wheel_item **out, size_t *count, qa_error *error)
{
    qa_frontend *f = seat->frontend;
    qa_application_equipment_view selected;
    if (!qa_application_equipment_read(f->application, actor, &selected, error)) return false;
    if (definition_count == SIZE_MAX || definition_count + 1 > SIZE_MAX / sizeof(q1_wheel_rows))
        return frontend_fail(error, QA_ERROR_MEMORY, "Authored wheel sources exceed address space");
    qa_arena *storage = &f->frame.storage;
    q1_wheel_rows *sources = qa_arena_alloc(storage, (definition_count + 1) * sizeof(*sources),
        _Alignof(q1_wheel_rows), error);
    if (!sources) return false;
    size_t source_count = 0, capacity = original_count;
    for (size_t i = 0; i <= definition_count; ++i) {
        qa_actor_owner owner = i < definition_count ? definitions[i].owner :
            selected.family == QA_GAME_Q1 ? selected.provider : 0;
        if (!owner) continue;
        bool retained = false;
        for (size_t j = 0; j < source_count; ++j) if (sources[j].owner == owner) retained = true;
        if (retained) continue;
        q1_wheel_rows *source = sources + source_count++;
        *source = (q1_wheel_rows){.owner = owner};
        if (!frontend_equipment_media_q1_wheel_read(f, owner, &source->slots, &source->count, error)) return false;
        if (source->count > SIZE_MAX - capacity)
            return frontend_fail(error, QA_ERROR_MEMORY, "Authored wheel slots exceed address space");
        capacity += source->count;
    }
    if (capacity == original_count) { *out = original; *count = original_count; return true; }
    if (capacity > SIZE_MAX / sizeof(qa_hud_wheel_item))
        return frontend_fail(error, QA_ERROR_MEMORY, "Authored wheel rows exceed address space");
    qa_hud_wheel_item *rows = qa_arena_alloc(storage, capacity * sizeof(*rows), _Alignof(qa_hud_wheel_item), error);
    bool *replaced = original_count ? qa_arena_alloc(storage, original_count, 1, error) : NULL;
    if (!rows || (original_count && !replaced)) return false;
    if (original_count) { memcpy(rows, original, original_count * sizeof(*rows)); memset(replaced, 0, original_count); }
    size_t written = original_count;
    for (size_t i = 0; i < source_count; ++i) {
        const q1_wheel_rows *source = sources + i;
        if (!source->count) continue;
        for (size_t j = 0; j < source->count; ++j) {
            const frontend_q1_wheel_slot *slot = source->slots + j;
            qa_item_definition definition;
            bool found;
            if (!qa_application_q1_wheel_item_read(f->application, actor, source->owner,
                slot->has_impulse ? slot->impulse : 0, slot->has_weapon_bits ? &slot->weapon_bits : NULL,
                &definition, &found, error)) return false;
            if (!found) {
                char label[48];
                int length = snprintf(label, sizeof(label), "Slot %" PRId64, (int64_t)slot->slot + 1);
                char *retained = qa_arena_alloc(storage, (size_t)length + 1, 1, error);
                if (!retained) return false;
                memcpy(retained, label, (size_t)length + 1);
                rows[written++] = (qa_hud_wheel_item){
                    .identity = {.key = q1_wheel_key(source->owner, j), .source_ordinal = slot->slot},
                    .sort_order = slot->slot, .label = retained, .has_count = slot->has_count,
                    .icon = slot->icon, .selected_icon = slot->selected_icon};
                continue;
            }
            size_t target = SIZE_MAX;
            for (size_t k = 0; k < original_count; ++k) {
                int32_t ordinal = rows[k].identity.source_ordinal;
                if (!replaced[k] && rows[k].identity.item == definition.item && ordinal >= 0 &&
                    (size_t)ordinal < definition_count && definitions[ordinal].owner == source->owner) {
                    target = k; break;
                }
            }
            if (target == SIZE_MAX) target = written++;
            else replaced[target] = true;
            if (!definition_row(seat, actor, &definition, true, slot->slot, rows + target, error)) return false;
            rows[target].identity.key = q1_wheel_key(source->owner, j);
            rows[target].has_count = slot->has_count;
            rows[target].icon = slot->icon;
            rows[target].selected_icon = slot->selected_icon;
        }
    }
    *out = rows; *count = written;
    return true;
}
static bool items(void *context, uint32_t id, qa_hud_wheel_mode mode,
                  const qa_hud_wheel_item **out, size_t *count, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    *out = NULL; *count = 0;
    qa_actor_id actor; uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        !qa_application_player_actor(seat->frontend->application, launch_seat, &actor)) return true;
    qa_inventory *inventory = qa_application_inventory(seat->frontend->application);
    qa_application_native_q2_wheel native = {0};bool native_found;
    if (!qa_application_native_q2_wheel_read(seat->frontend->application,launch_seat,
        mode==QA_HUD_WHEEL_POWERUPS,&native,&native_found,error)) return false;
    size_t total;
    if (!qa_inventory_item_definitions(inventory, actor, NULL, 0, &total, error)) return false;
    if (total > SIZE_MAX / sizeof(qa_item_definition) || total > SIZE_MAX / sizeof(qa_hud_wheel_item))
        return frontend_fail(error, QA_ERROR_MEMORY, "wheel definition storage overflow");
    qa_arena *storage = &seat->frontend->frame.storage;
    qa_item_definition *definitions = total ? qa_arena_alloc(storage, total * sizeof(*definitions),
        _Alignof(qa_item_definition), error) : NULL;
    qa_hud_wheel_item *rows = total ? qa_arena_alloc(storage, total * sizeof(*rows),
        _Alignof(qa_hud_wheel_item), error) : NULL;
    if ((total && (!definitions || !rows)) ||
        !qa_inventory_item_definitions(inventory, actor, definitions, total, &total, error)) return false;
    uint64_t revision = qa_inventory_definition_revision(inventory, actor);
    size_t written = 0;
    for (size_t i = 0; i < total; ++i) {
        qa_item_definition definition = definitions[i];
        const qa_hud_q2_wheel_definition *native_definition=NULL;
        uint32_t native_slot=0;bool native_powerup=false;
        if (native_found && definition.owner==native.owner) {
            for (uint32_t j=0;j<QA_Q2_MAX_WHEEL_ITEMS && !native_definition;++j) {
                if (native.weapons[j]==definition.item) {
                    native_definition=native.definitions->wheel+j;native_slot=j;
                } else if (native.powerups[j]==definition.item) {
                    native_definition=native.definitions->powerups+j;native_slot=j;native_powerup=true;
                }
            }
        }
        if ((!native_definition && definition.weapon != (mode == QA_HUD_WHEEL_WEAPONS)) ||
            (mode == QA_HUD_WHEEL_POWERUPS && !(definition.actions & QA_ITEM_USE))) continue;
        if (!definition_row(seat, actor, &definition, native_definition == NULL,
            (int32_t)i, rows + written, error)) return false;
        ++written;
        if (native_definition) {
            qa_hud_wheel_item *row=rows+written-1;
            int32_t native_count=native_powerup?native.state.powerups[native_slot]:
                (native.state.owned&(UINT32_C(1)<<native_slot))!=0;
            int32_t ammo=native_definition->ammo>=0?native.state.ammo[native_definition->ammo]:
                native_powerup?native_count:-1;
            row->owned=native_count>0;
            row->has_ammunition=row->owned && (ammo==-1 ||
                ammo>=(native_powerup?1:native_definition->minimum));
            row->has_count=ammo!=-1 && !(native_definition->toggle && native_definition->ammo<0);
            row->count=ammo;row->warning_count=native_definition->warning;
            row->sort_order=native_definition->sort;row->identity.source_ordinal=(int32_t)native_slot;
            row->has_toggle=native_definition->toggle;row->enabled=native_definition->toggle && native_count>1;
        }
    }
    if (qa_inventory_definition_revision(inventory, actor) != revision) {
        size_t retained = 0;
        for (size_t i = 0; i < written; ++i) {
            qa_item_definition definition; bool found;
            if (!qa_inventory_item_definition_find(inventory, actor, NULL, rows[i].identity.item,
                &definition, &found, error)) return false;
            if (!found || (mode==QA_HUD_WHEEL_WEAPONS && !definition.weapon) ||
                (mode == QA_HUD_WHEEL_POWERUPS && !(definition.actions & QA_ITEM_USE))) continue;
            rows[i].label = definition.label ? definition.label : "Item";
            rows[retained++] = rows[i];
        }
        written = retained;
    }
    if (mode == QA_HUD_WHEEL_WEAPONS)
        return authored_rows(seat, actor, definitions, total, rows, written, out, count, error);
    *out = rows; *count = written; return true;
}
static bool active(void *context, uint32_t id, uint64_t *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id; (void)error;
    *out = 0;
    qa_actor_id actor; uint32_t launch_seat;
    qa_item_id weapon;
    if (!frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        !qa_application_player_actor(seat->frontend->application, launch_seat, &actor)) return true;
    if (!qa_application_weapon_read(seat->frontend->application, actor, &weapon, error)) return false;
    qa_application_native_q2_wheel native;bool native_found;
    if (!qa_application_native_q2_wheel_read(seat->frontend->application,launch_seat,false,
        &native,&native_found,error)) return false;
    qa_actor_owner owner;
    if (native_found && native.active && qa_inventory_item_owner(
        qa_application_inventory(seat->frontend->application),actor,weapon,&owner,NULL) && owner==native.owner)
        weapon=native.active;
    *out = weapon;
    qa_application_equipment_view equipment;
    if (!qa_application_equipment_read(seat->frontend->application, actor, &equipment, error)) return false;
    if (weapon && equipment.family == QA_GAME_Q1 && equipment.item == weapon) {
        owner = equipment.provider;
        const frontend_q1_wheel_slot *slots; size_t count;
        if (!frontend_equipment_media_q1_wheel_read(seat->frontend, owner, &slots, &count, error)) return false;
        for (size_t i = 0; i < count; ++i) {
            qa_item_definition definition;
            bool found;
            if (!qa_application_q1_wheel_item_read(seat->frontend->application, actor, owner,
                slots[i].has_impulse ? slots[i].impulse : 0,
                slots[i].has_weapon_bits ? &slots[i].weapon_bits : NULL, &definition, &found, error)) return false;
            if (found && definition.item == weapon) { *out = q1_wheel_key(owner, i); break; }
        }
    }
    return true;
}
static bool request_item(frontend_seat *seat,const qa_item_definition *definition,qa_error *error)
{
    qa_application *app=seat->frontend->application;
    qa_strings *strings=qa_session_strings(qa_application_session(app));
    const char *instance=qa_strings_cstr(strings,definition->owner);
    const char *item=qa_strings_cstr(strings,definition->item);
    if (!instance || !item)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Wheel selection lost its published provider or item name");
    frontend_queued_command queued={.target=FRONTEND_COMMAND_ITEM,.physical_seat=seat->id,
        .time_ns=seat->frontend->time_ns,.frame_number=seat->frontend->frame_number};
    qa_platform_event_result result=frontend_command_queue(seat->frontend,&queued,instance,item,
        seat->frontend->wall_time_ns,error);
    if(result!=QA_PLATFORM_EVENT_ACCEPTED && error && error->code!=QA_OK) return false;
    if(result!=QA_PLATFORM_EVENT_ACCEPTED) frontend_print(seat->frontend,"Input queue is full; item selection was not applied.");
    return true;
}
static bool select_item(void *context, uint32_t id, qa_hud_wheel_mode mode,
                        qa_hud_wheel_identity identity, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    qa_actor_id actor; uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        !qa_application_player_actor(seat->frontend->application, launch_seat, &actor) ||
        (!(identity.key >> 32) && identity.key != identity.item) || !identity.item)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "wheel selection no longer belongs to a live player");
    qa_item_definition definition; bool found;
    qa_actor_owner owner = (qa_actor_owner)(identity.key >> 32);
    if (!qa_inventory_item_definition_find(qa_application_inventory(seat->frontend->application),
        actor, owner ? &owner : NULL, identity.item, &definition, &found, error)) return false;
    if (found && (mode != QA_HUD_WHEEL_WEAPONS || definition.weapon) &&
        (mode != QA_HUD_WHEEL_POWERUPS || (definition.actions & QA_ITEM_USE))) {
        return request_item(seat,&definition,error);
    }
    return true;
}
static bool switch_item(frontend_seat *seat, qa_actor_id actor, qa_actor_owner owner,
    const frontend_q1_wheel_slot *slots, size_t count, int32_t ordinal,
    qa_item_definition *out, qa_error *error)
{
    *out = (qa_item_definition){0};
    const frontend_q1_wheel_slot *slot = NULL;
    for (size_t i = 0; i < count; ++i) if (slots[i].slot == ordinal) { slot = slots + i; break; }
    if (!slot && (ordinal < 0 || ordinal > 7)) return true;
    bool found;
    if (!qa_application_q1_wheel_item_read(seat->frontend->application, actor, owner,
        slot ? (slot->has_impulse ? slot->impulse : 0) : ordinal == 7 ? 1 : ordinal + 2,
        slot && slot->has_weapon_bits ? &slot->weapon_bits : NULL, out, &found, error)) return false;
    if (!found) { *out = (qa_item_definition){0}; return true; }
    double owned;
    if (!qa_inventory_count_read(qa_application_inventory(seat->frontend->application), actor, out->item, &owned, error)) return false;
    if (owned <= 0) *out = (qa_item_definition){0};
    return true;
}
bool frontend_wheel_switch(frontend_seat *seat, int32_t first, int32_t second, qa_error *error)
{
    qa_frontend *f = seat->frontend;
    qa_actor_id actor; uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(f, seat->id, &launch_seat) ||
        !qa_application_player_actor(f->application, launch_seat, &actor)) return true;
    qa_application_equipment_view source;
    if (!qa_application_equipment_read(f->application, actor, &source, error)) return false;
    if (source.family != QA_GAME_Q1) return true;
    const frontend_q1_wheel_slot *slots; size_t count;
    if (!frontend_equipment_media_q1_wheel_read(f, source.provider, &slots, &count, error)) return false;
    qa_item_definition a, b;
    if (!switch_item(seat, actor, source.provider, slots, count, first, &a, error) ||
        !switch_item(seat, actor, source.provider, slots, count, second, &b, error)) return false;
    const qa_item_definition *selected = a.item == source.item ? (b.item ? &b : &a) : a.item ? &a : &b;
    return !selected->item || request_item(seat, selected, error);
}
bool frontend_wheel_create(frontend_seat *seat, qa_error *error)
{
    qa_hud_wheel_options options = qa_hud_wheel_defaults(seat->id);
    options.context = seat; options.items = items; options.active = active; options.select = select_item;
    return qa_hud_wheel_create(&options, seat->frontend->time_ns, &seat->wheel, error);
}
void frontend_wheel_command(void *context, qa_input_seat *input, bool powerups, bool down)
{
    qa_frontend *frontend = context;
    uint32_t ordinal = qa_input_seat_ordinal(input);
    if (ordinal >= frontend->options.seats || !frontend->seats[ordinal].wheel) return;
    frontend_seat *seat = &frontend->seats[ordinal];
    qa_error error = {0}; bool opened;
    bool ok = down ? qa_hud_wheel_open(seat->wheel,
        powerups ? QA_HUD_WHEEL_POWERUPS : QA_HUD_WHEEL_WEAPONS, &opened, &error) :
        qa_hud_wheel_close(seat->wheel, true, &error);
    if (!ok) frontend_print(frontend, error.message);
}
