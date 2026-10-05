#include "library_internal.h"
#include "qa/ui_menu_save.h"
#include "qa/source_save.h"

static bool text(qa_source_save_io *io, char **owned)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = reading ? 0 : (*owned ? strlen(*owned) : 0);
    if ((!reading && !*owned) || !qa_source_save_count(io, &length,
        reading ? io->input.size - io->offset : SIZE_MAX - 1) || length == SIZE_MAX) return false;
    if (reading) {
        *owned = malloc(length + 1);
        if (!*owned) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library profile text"); return false; }
    }
    if (!qa_source_save_bytes(io, *owned, length)) return false;
    if (reading) {
        (*owned)[length] = 0;
        if (memchr(*owned, 0, length)) {
            qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "library profile text contains NUL"); return false;
        }
    }
    return true;
}
static bool optional_text(qa_source_save_io *io, char **owned)
{
    bool present = *owned != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    return !present || text(io, owned);
}
static bool profiles(qa_source_save_io *io, qa_ui_library *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = saved->local_player_count;
    size_t maximum = reading ? (io->input.size - io->offset) / 51 : SIZE_MAX / sizeof(library_profile);
    if (maximum > SIZE_MAX / sizeof(library_profile)) maximum = SIZE_MAX / sizeof(library_profile);
    if (!qa_source_save_count(io, &count, maximum) || !count) return false;
    if (reading) {
        saved->local_players = calloc(count, sizeof(*saved->local_players));
        if (!saved->local_players) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library local roster"); return false; }
        saved->local_player_count = count;
    }
    bool own_seat = false;
    for (size_t i = 0; i < count; ++i) {
        library_profile copy = reading ? (library_profile){0} : saved->local_players[i];
        library_profile *p = reading ? &saved->local_players[i] : &copy;
        if (!reading && (p->seat.name != p->name || p->seat.team != p->team ||
            p->seat.character_model != p->character_model || p->seat.character_skin != p->character_skin ||
            p->seat.character_head_model != p->character_head_model ||
            p->seat.character_head_skin != p->character_head_skin)) return false;
        if (!qa_source_save_u32(io, &p->seat.id) ||
            !qa_source_save_u64(io, &p->seat.actor.registry) ||
            !qa_source_save_u64(io, &p->seat.actor.generation) ||
            !qa_source_save_u32(io, &p->seat.actor.slot) ||
            !text(io, &p->name) || !text(io, &p->team) ||
            !optional_text(io, &p->character_model) || !optional_text(io, &p->character_skin) ||
            !optional_text(io, &p->character_head_model) || !optional_text(io, &p->character_head_skin) ||
            !qa_source_save_u32(io, &p->seat.input_device) ||
            !qa_source_save_bool(io, &p->seat.local) ||
            !qa_source_save_bool(io, &p->seat.spectator) ||
            !qa_source_save_bool(io, &p->seat.bot) ||
            !qa_source_save_f32(io, &p->seat.bot_skill) ||
            !p->seat.local || p->seat.bot || p->seat.actor.registry) return false;
        if (reading) {
            p->seat.name = p->name; p->seat.team = p->team;
            p->seat.character_model = p->character_model; p->seat.character_skin = p->character_skin;
            p->seat.character_head_model = p->character_head_model;
            p->seat.character_head_skin = p->character_head_skin;
        }
        for (size_t j = 0; j < i; ++j)
            if (saved->local_players[j].seat.id == p->seat.id) return false;
        own_seat |= p->seat.id == saved->ui->options.seat;
    }
    return own_seat;
}
static bool draft(qa_source_save_io *io,qa_ui_library *saved) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=saved->draft!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    const qa_actor_registry *actors=qa_session_actors(qa_application_session(saved->application));
    qa_buffer bytes={0}; size_t size=0; bool ok;
    if (reading) {
        qa_bytes view={0};
        return qa_source_save_count(io,&size,io->input.size-io->offset) && qa_source_save_span(io,size,&view) &&
            qa_launch_draft_restore(saved->catalog,actors,view,&saved->draft,io->error);
    }
    if (!qa_launch_draft_checkpoint(saved->draft,actors,&bytes,io->error)) return false;
    size=bytes.size; ok=qa_source_save_count(io,&size,SIZE_MAX) && qa_source_save_bytes(io,bytes.data,size);
    qa_buffer_free(&bytes); return ok;
}
static bool fields(qa_source_save_io *io,qa_ui_library *saved,const qa_ui_library *qualified,const qa_ui_menu_checkpoint_refs *refs) {
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','S','E','L'}; uint32_t seat=qualified->ui->options.seat;
    uint64_t menu=qualified->menu,catalog=0;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QSEL",4) ||
        !qa_source_save_u32(io,&seat) || seat!=qualified->ui->options.seat ||
        !qa_source_save_u64(io,&menu) || menu!=qualified->menu) return false;
    if (!reading && !refs->catalog_encode(refs->context,saved->catalog,&catalog,io->error)) return false;
    if (!qa_source_save_u64(io,&catalog)) return false;
    if (reading) {
        qa_catalog *actual=NULL; if (!refs->catalog_decode(refs->context,catalog,&actual,io->error) || !actual) return false;
        qa_catalog_retain(actual); saved->catalog=actual;
    }
    uint32_t family=saved->family,edition=saved->edition,group=saved->group,field=saved->field,mode=saved->mode_preference;
    if (!profiles(io,saved) || !draft(io,saved) ||
        !qa_source_save_u32(io,&mode) ||
        (mode!=QA_MODE_SINGLE_PLAYER && mode!=QA_MODE_COOPERATIVE && mode!=QA_MODE_FFA) ||
        !qa_source_save_u32(io,&family) || family>QA_GAME_Q3 ||
        !qa_source_save_u32(io,&edition) || edition>QA_EDITION_DEMO ||
        !qa_source_save_u32(io,&saved->native_product) ||
        (saved->native_product && !qa_catalog_product(saved->catalog,saved->native_product)) ||
        !qa_source_save_i32(io,&saved->native_skill) || saved->native_skill<0 || saved->native_skill>5 ||
        !qa_source_save_i32(io,&saved->arena_number) || !qa_source_save_i32(io,&saved->arena_tier) ||
        !qa_source_save_u32(io,&group) || group>3 ||
        !qa_source_save_u32(io,&field) || field>QA_UI_LIBRARY_DOPPLER ||
        !qa_source_save_count(io,&saved->page,SIZE_MAX) || !qa_source_save_count(io,&saved->roster_page,SIZE_MAX) ||
        !optional_text(io,&saved->monster_classname) || !qa_source_save_bool(io,&saved->last_authored) ||
        !qa_source_save_bytes(io,saved->status,sizeof(saved->status)) || !memchr(saved->status,0,sizeof(saved->status))) return false;
    saved->family=(qa_game_family)family; saved->edition=(qa_product_edition)edition;
    saved->group=group; saved->field=(qa_ui_library_field)field; saved->mode_preference=(qa_mode_kind)mode; return true;
}
bool qa_ui_library_checkpoint(const qa_ui_library *menu,const qa_ui_menu_checkpoint_refs *refs,qa_buffer *out,qa_error *error) {
    if (!menu || !refs || !refs->catalog_encode || !out || out->data || out->size ||
        menu->ui->handling || menu->ui->drawing || !menu->catalog || !menu->local_player_count)
        return ui_fail(error,"selection capture requires idle catalog and roster owners");
    qa_ui_library saved=*menu; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && fields(&io,&saved,menu,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) qa_error_set(error,QA_ERROR_FORMAT,0,"selection continuation leaves its owner domains");
    return ok;
}
bool qa_ui_library_restore(qa_ui_library *menu,const qa_ui_menu_checkpoint_refs *refs,qa_bytes bytes,qa_error *error) {
    if (!menu || !refs || !refs->catalog_decode || menu->ui->handling || menu->ui->drawing)
        return ui_fail(error,"selection restore requires its idle controller and catalog resolver");
    qa_ui_library saved={.ui=menu->ui,.application=menu->application,.menu=menu->menu,.services=menu->services};
    memcpy(saved.pages,menu->pages,sizeof(saved.pages));
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,&saved,menu,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        ui_library_clear(&saved); if (error && error->code==QA_OK) qa_error_set(error,QA_ERROR_FORMAT,0,"unqualified startup selection continuation"); return false;
    }
    qa_ui_library displaced=*menu; *menu=saved; ui_library_clear(&displaced); return true;
}
