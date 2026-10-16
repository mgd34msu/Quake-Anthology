#include "internal.h"
#include "qa/application_weapon_availability.h"
#include "qa/application_equipment.h"
#include "qa/application_native_q2_presentation.h"
#include <inttypes.h>
#include <stdio.h>

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
    if (!total) return true;
    if (total > SIZE_MAX / sizeof(qa_item_definition) || total > SIZE_MAX / sizeof(qa_hud_wheel_item))
        return frontend_fail(error, QA_ERROR_MEMORY, "wheel definition storage overflow");
    qa_arena *storage = &seat->frontend->frame.storage;
    qa_item_definition *definitions = qa_arena_alloc(storage, total * sizeof(*definitions),
        _Alignof(qa_item_definition), error);
    qa_hud_wheel_item *rows = definitions ? qa_arena_alloc(storage, total * sizeof(*rows),
        _Alignof(qa_hud_wheel_item), error) : NULL;
    if (!rows || !qa_inventory_item_definitions(inventory, actor, definitions, total, &total, error)) return false;
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
        double owned;
        if (!qa_inventory_count_read(inventory, actor, definition.item, &owned, error)) return false;
        bool ammunition = owned > 0;
        double amount = owned;
        if (definition.ammo) {
            double ammo;
            if (!qa_inventory_count_read(inventory, actor, definition.ammo, &ammo, error)) return false;
            ammunition = ammo > 0; amount = ammo;
        }
        if (definition.weapon && !native_definition) {
            bool available, found;
            if (!qa_application_weapon_availability_read(seat->frontend->application,actor,
                &definition,&available,&found,error)) return false;
            if (found) ammunition=available;
        }
        rows[written++] = (qa_hud_wheel_item){
            .identity = {.key = definition.item, .item = definition.item, .source_ordinal = (int32_t)i},
            .sort_order = (int32_t)i, .label = definition.label ? definition.label : "Item",
            .owned = owned > 0, .has_ammunition = ammunition, .has_count = true, .count = amount};
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
    *out = rows; *count = written;
    return true;
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
    *out = weapon; return true;
}
static bool select_item(void *context, uint32_t id, qa_hud_wheel_mode mode,
                        qa_hud_wheel_identity identity, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    qa_actor_id actor; uint32_t launch_seat;
    if (!frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        !qa_application_player_actor(seat->frontend->application, launch_seat, &actor) ||
        identity.key != identity.item || !identity.item)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "wheel selection no longer belongs to a live player");
    qa_item_definition definition; bool found;
    if (!qa_inventory_item_definition_find(qa_application_inventory(seat->frontend->application),
        actor, NULL, identity.item, &definition, &found, error)) return false;
    if (found && (mode != QA_HUD_WHEEL_WEAPONS || definition.weapon) &&
        (mode != QA_HUD_WHEEL_POWERUPS || (definition.actions & QA_ITEM_USE))) {
        qa_command_context command=qa_input_seat_context(seat->input);
        command.actor=actor; command.seat=launch_seat;
        command.owner=0; command.registry=command.generation=command.cvar_view=0;
        command.origin=QA_COMMAND_SEAT; command.script=NULL; command.direct=true;
        char text[64];
        int length=snprintf(text,sizeof(text),"useitem %" PRIu32 " %" PRIu32,definition.owner,identity.item);
        frontend_queued_command queued={.console=qa_application_console(seat->frontend->application),
            .context=command,.time_ns=seat->frontend->time_ns,.frame_number=seat->frontend->frame_number};
        qa_platform_event_result result=qa_platform_events_push(seat->frontend->platform_events,
            &(qa_sys_event){.kind=QA_PLATFORM_EVENT_CONSOLE_COMMAND,.time_ns=seat->frontend->wall_time_ns},
            (qa_bytes){(const uint8_t *)&queued,sizeof(queued)},
            (qa_bytes){(const uint8_t *)text,(size_t)length+1});
        if(result!=QA_PLATFORM_EVENT_ACCEPTED) frontend_print(seat->frontend,"Input queue is full; item selection was not applied.");
        return true;
    }
    return true;
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
