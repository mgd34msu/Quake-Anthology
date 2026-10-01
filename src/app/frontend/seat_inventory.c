#include "seat_inventory.h"
#include "seat_save.h"
#include "ui_restore.h"
#include "wheels_save.h"
#include "font_inventory.h"
#include "menu_save.h"
#include "capture.h"
#include "save_private.h"
#include "qa/persistence_content.h"
#include "qa/ui_menu_save.h"
#include "qa/ui_account_save.h"
#include "qa/ui_assistance_save.h"
#include "ui_features_private.h"

enum { SEAT_INPUT, SEAT_CONSOLE, SEAT_UI, SEAT_HUD, SEAT_WHEEL,
    SEAT_LIBRARY, SEAT_MODS, SEAT_RANKINGS, SEAT_ASSISTANCE, SEAT_MENU,
    SEAT_COMPONENTS };
typedef struct seat_record {
    qa_input_command_builder builder;
    qa_actor_id actor;
    uint64_t sequence, classic, primary;
    size_t fallback_offset, fallback_count;
    bool scores, chat_team;
    size_t wheel_capacity, wheel_labels;
    qa_bytes components[SEAT_COMPONENTS];
} seat_record;

static bool actual(const frontend_seat *seat)
{
    const qa_frontend *f=seat?seat->frontend:NULL;
    const frontend_ui_features *features=f?f->ui_features:NULL;
    size_t skip=features && features->fallback_count && seat->fonts.primary==features->bold &&
        features->fallbacks[0]==features->bold ? 1 : 0;
    return f && f->application && !f->stepping && !f->options.dedicated && f->seats &&
        seat->id<f->options.seats && seat==f->seats+seat->id && seat->input && seat->console &&
        seat->ui && seat->hud && seat->wheel && seat->library && seat->rankings && seat->assistance &&
        features && seat->fonts.seat==seat->id && seat->fonts.classic==f->classic &&
        (seat->fonts.primary==f->primary || seat->fonts.primary==features->bold) &&
        skip<=features->fallback_count && seat->fonts.fallback_count==features->fallback_count-skip &&
        seat->fonts.fallbacks==(features->fallbacks?features->fallbacks+skip:NULL) &&
        qa_ui_llm_service(seat->assistance)==frontend_tools_llm((qa_frontend *)f);
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,size);
    if (size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
/* One wire cell per reserved slot keeps the extent independent of native
 * pointer sizes. Factories overwrite the unused borrowed rows before reads. */
static bool reservation(qa_source_save_io *io,size_t count,size_t element)
{
    if (!element || count>SIZE_MAX/element) return false;
    size_t bytes=count;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (bytes>io->input.size-io->offset) return false;
        const uint8_t *data=io->input.data+io->offset;
        for (size_t i=0;i<bytes;++i) if (data[i]) return false;
        io->offset+=bytes; return true;
    }
    uint8_t zero[128]={0};
    while (bytes) {
        size_t size=bytes<sizeof(zero)?bytes:sizeof(zero);
        if (!qa_source_save_bytes(io,zero,size)) return false;
        bytes-=size;
    }
    return true;
}
static bool builder_fields(qa_source_save_io *io,qa_input_command_builder *builder)
{
    uint32_t kind=builder->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_MOVEMENT_Q3 ||
        !qa_source_save_vec3(io,&builder->angles) || !qa_vec_finite(builder->angles) ||
        !qa_source_save_f32(io,&builder->mouse.previous.x) || !isfinite(builder->mouse.previous.x) ||
        !qa_source_save_f32(io,&builder->mouse.previous.y) || !isfinite(builder->mouse.previous.y) ||
        !qa_source_save_bool(io,&builder->drift.drifting) ||
        !qa_source_save_f32(io,&builder->drift.velocity) || !isfinite(builder->drift.velocity) ||
        !qa_source_save_f32(io,&builder->drift.moving_seconds) || !isfinite(builder->drift.moving_seconds) ||
        !qa_source_save_bool(io,&builder->previous_mouse_look)) return false;
    builder->kind=(qa_movement_kind)kind; return true;
}
static bool header(qa_source_save_io *io,qa_frontend *f,bool input)
{
    uint8_t magic[4]={'Q','F','S',input?'U':'V'};
    uint32_t version=input?1:2,seats=f->options.seats; bool dedicated=f->options.dedicated;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,input?"QFSU":"QFSV",4) &&
        qa_source_save_u32(io,&version) && version==(input?1u:2u) && qa_source_save_u32(io,&seats) && seats==f->options.seats &&
        qa_source_save_bool(io,&dedicated) && dedicated==f->options.dedicated;
}
static bool fields(qa_source_save_io *io,qa_frontend *f,bool input,seat_record *records)
{
    if (!header(io,f,input)) return false;
    if (f->options.dedicated) return true;
    for (unsigned i=0;i<f->options.seats;++i) {
        seat_record *row=records+i; uint32_t seat=i;
        if (!qa_source_save_u32(io,&seat) || seat!=i) return false;
        if (input) {
            if (!builder_fields(io,&row->builder) || !qa_source_save_actor(io,&row->actor) ||
                !qa_source_save_u64(io,&row->sequence) || !qa_source_save_bool(io,&row->scores) ||
                !qa_source_save_bool(io,&row->chat_team) ||
                !blob(io,&row->components[SEAT_INPUT]) || !row->components[SEAT_INPUT].size ||
                !blob(io,&row->components[SEAT_CONSOLE]) || !row->components[SEAT_CONSOLE].size) return false;
        } else {
            if (!qa_source_save_u64(io,&row->classic) || !row->classic ||
                !qa_source_save_u64(io,&row->primary) || !row->primary ||
                !qa_source_save_count(io,&row->fallback_offset,1) ||
                !qa_source_save_count(io,&row->fallback_count,SIZE_MAX/sizeof(const qa_font *)) ||
                !qa_source_save_count(io,&row->wheel_capacity,SIZE_MAX/sizeof(qa_hud_wheel_item)) ||
                !reservation(io,row->wheel_capacity,sizeof(qa_hud_wheel_item)) ||
                !reservation(io,row->wheel_capacity,sizeof(qa_item_definition)) ||
                !qa_source_save_count(io,&row->wheel_labels,SIZE_MAX) || !reservation(io,row->wheel_labels,1)) return false;
            for (unsigned j=SEAT_UI;j<SEAT_COMPONENTS;++j)
                if (!blob(io,row->components+j) || (j!=SEAT_MODS && !row->components[j].size)) return false;
        }
    }
    return true;
}
static bool catalog_encode(void *context,const qa_catalog *catalog,uint64_t *out,qa_error *error)
{
    qa_application_content_graph *graph=qa_application_content_graph_read(((frontend_seat *)context)->frontend->application);
    uint64_t key=qa_application_content_catalog_id(graph,catalog);
    if (!catalog || !out || !key) return frontend_fail(error,QA_ERROR_FORMAT,"Seat menu catalog leaves its actual captured content graph");
    *out=key; return true;
}
static bool catalog_decode(void *context,uint64_t key,qa_catalog **out,qa_error *error)
{
    qa_application_content_graph *graph=qa_application_content_graph_read(((frontend_seat *)context)->frontend->application);
    qa_catalog *catalog=qa_application_content_catalog(graph,key);
    if (!out || !key || !catalog) return frontend_fail(error,QA_ERROR_FORMAT,"Seat menu catalog has no restored immutable graph owner");
    *out=catalog; return true;
}
static bool llm_encode(void *context,const qa_llm *llm,uint64_t *out,qa_error *error)
{
    frontend_seat *seat=context;
    if (!out || !llm || llm!=frontend_tools_llm(seat->frontend) || qa_ui_llm_service(seat->assistance)!=llm)
        return frontend_fail(error,QA_ERROR_FORMAT,"Seat assistance differs from its actual tools service");
    *out=1; return true;
}
static bool llm_decode(void *context,uint64_t key,qa_llm **out,qa_error *error)
{
    frontend_seat *seat=context; qa_llm *llm=frontend_tools_llm(seat->frontend);
    if (!out || key!=1 || !llm || qa_ui_llm_service(seat->assistance)!=llm)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved assistance service differs from the prepared tools owner");
    *out=llm; return true;
}
static bool hud_restore(frontend_seat *seat,const qa_hud_checkpoint_refs *refs,qa_bytes bytes,qa_error *error)
{
    qa_hud_options options; qa_hud *restored=NULL;
    if (!frontend_seat_hud_options(seat,&options,error) || !qa_hud_restore(bytes,&options,refs,&restored,error)) return false;
    if (!qa_hud_destroy(seat->hud,error)) {
        qa_error cleanup={0}; (void)qa_hud_destroy(restored,&cleanup); return false;
    }
    seat->hud=restored; return true;
}
bool frontend_seats_checkpoint(qa_frontend *f,frontend_scene_namespace *space,
    qa_buffer *input,qa_buffer *presentation,qa_error *error)
{
    if (!f || !f->application || !f->capture || !space || !input || input->data || input->size ||
        !presentation || presentation->data || presentation->size || input==presentation ||
        !frontend_seat_callbacks_idle(f) || !f->options.seats || f->options.seats>QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Seat capture requires genuine held owners and distinct empty outputs");
    seat_record records[QA_INPUT_LOCAL_SEATS]={0};
    qa_buffer owned[QA_INPUT_LOCAL_SEATS][SEAT_COMPONENTS]={0};
    bool ok=true;
    for (unsigned i=0;ok && !f->options.dedicated && i<f->options.seats;++i) {
        frontend_seat *seat=f->seats+i; seat_record *row=records+i;
        if (!actual(seat) || (seat->wheel_capacity && (!seat->wheel_items || !seat->wheel_definitions)) ||
            (seat->wheel_label_capacity && !seat->wheel_labels)) { ok=false; break; }
        *row=(seat_record){.builder=seat->builder,.actor=seat->actor,.sequence=seat->sequence,
            .scores=seat->scores,.chat_team=seat->chat_team,.wheel_capacity=seat->wheel_capacity,.wheel_labels=seat->wheel_label_capacity};
        row->fallback_count=seat->fonts.fallback_count;
        row->fallback_offset=f->ui_features->fallbacks && seat->fonts.fallbacks==f->ui_features->fallbacks+1 ? 1 : 0;
        qa_input_checkpoint_refs in=frontend_seat_input_refs(seat);
        qa_seat_console_save_resolvers console=frontend_seat_console_refs(seat);
        qa_ui_checkpoint_refs ui=frontend_seat_ui_refs(seat);
        qa_hud_checkpoint_refs hud=frontend_hud_image_refs(space);
        qa_hud_wheel_checkpoint_refs wheel=frontend_wheel_refs(seat);
        qa_ui_menu_checkpoint_refs menu={seat,catalog_encode,catalog_decode};
        qa_ui_assistance_checkpoint_refs assistance={seat,llm_encode,llm_decode};
        ok=frontend_font_encode(f,seat->fonts.classic,&row->classic,error) &&
            frontend_font_encode(f,seat->fonts.primary,&row->primary,error) &&
            qa_input_seat_checkpoint(seat->input,&in,&owned[i][SEAT_INPUT],error) &&
            qa_seat_console_save_capture(seat->console,&console,&owned[i][SEAT_CONSOLE],error) &&
            qa_ui_checkpoint(seat->ui,&ui,&owned[i][SEAT_UI],error) &&
            qa_hud_checkpoint(seat->hud,&hud,&owned[i][SEAT_HUD],error) &&
            qa_hud_wheel_checkpoint(seat->wheel,&wheel,&owned[i][SEAT_WHEEL],error) &&
            qa_ui_library_checkpoint(seat->library,&menu,&owned[i][SEAT_LIBRARY],error) &&
            (!seat->mods || qa_ui_mods_checkpoint(seat->mods,&menu,&owned[i][SEAT_MODS],error)) &&
            qa_ui_rankings_checkpoint(seat->rankings,&owned[i][SEAT_RANKINGS],error) &&
            qa_ui_llm_checkpoint(seat->assistance,&assistance,&owned[i][SEAT_ASSISTANCE],error) &&
            frontend_menu_checkpoint(seat,&owned[i][SEAT_MENU],error);
        for (unsigned j=0;j<SEAT_COMPONENTS;++j) row->components[j]=(qa_bytes){owned[i][j].data,owned[i][j].size};
    }
    qa_source_save_io in={0},shown={0}; qa_buffer input_saved={0},shown_saved={0};
    ok=ok && qa_source_save_writer(&in,qa_application_session(f->application),error) && fields(&in,f,true,records) &&
        qa_source_save_finish(&in,&input_saved) && qa_source_save_writer(&shown,qa_application_session(f->application),error) &&
        fields(&shown,f,false,records) && qa_source_save_finish(&shown,&shown_saved);
    if (ok) { *input=input_saved; input_saved=(qa_buffer){0}; *presentation=shown_saved; shown_saved=(qa_buffer){0}; }
    qa_source_save_dispose(&in); qa_source_save_dispose(&shown); qa_buffer_free(&input_saved); qa_buffer_free(&shown_saved);
    for (unsigned i=0;i<QA_INPUT_LOCAL_SEATS;++i) for (unsigned j=0;j<SEAT_COMPONENTS;++j) qa_buffer_free(&owned[i][j]);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Actual frontend seat continuation is not completely qualified");
    return ok;
}
bool frontend_seats_restore(qa_frontend *f,frontend_scene_namespace *space,
    qa_bytes input,qa_bytes presentation,qa_error *error)
{
    if (!f || !f->application || !f->source_restoring || f->capture || f->stepping || !space ||
        !frontend_seat_callbacks_idle(f) || !f->options.seats || f->options.seats>QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Seat import requires its isolated prepared service graph");
    seat_record records[QA_INPUT_LOCAL_SEATS]={0}; qa_source_save_io in={0},shown={0};
    bool ok=qa_source_save_reader(&in,qa_application_session(f->application),input,error) && fields(&in,f,true,records) &&
        qa_source_save_finish(&in,NULL) && qa_source_save_reader(&shown,qa_application_session(f->application),presentation,error) &&
        fields(&shown,f,false,records) && qa_source_save_finish(&shown,NULL);
    for (unsigned i=0;ok && !f->options.dedicated && i<f->options.seats;++i) {
        frontend_seat *seat=f->seats+i; seat_record *row=records+i;
        const qa_font *classic=NULL,*primary=NULL;
        ok=actual(seat) && ((seat->mods!=NULL)==(row->components[SEAT_MODS].size!=0)) &&
            frontend_font_decode(f,row->classic,&classic,error) && frontend_font_decode(f,row->primary,&primary,error) &&
            classic==f->classic && (primary==f->primary || primary==f->ui_features->bold) &&
            row->fallback_offset<=f->ui_features->fallback_count &&
            row->fallback_count==f->ui_features->fallback_count-row->fallback_offset &&
            row->fallback_offset==(f->ui_features->fallback_count && primary==f->ui_features->bold &&
                f->ui_features->fallbacks[0]==f->ui_features->bold ? 1u : 0u) &&
            !seat->wheel_items && !seat->wheel_definitions && !seat->wheel_labels &&
            !seat->wheel_capacity && !seat->wheel_label_capacity;
        if (ok) {
            qa_font_selection fonts;
            ok=qa_font_selection_init(&fonts,i,classic,primary,
                f->ui_features->fallbacks?f->ui_features->fallbacks+row->fallback_offset:NULL,row->fallback_count,error) &&
                qa_ui_set_presentation(seat->ui,&fonts,1,QA_UI_COLOR_STANDARD,error);
            if (ok) seat->fonts=fonts;
        }
    }
    for (unsigned i=0;ok && !f->options.dedicated && i<f->options.seats;++i) {
        frontend_seat *seat=f->seats+i; seat_record *row=records+i;
        qa_input_checkpoint_refs input_refs=frontend_seat_input_refs(seat);
        qa_seat_console_save_resolvers console=frontend_seat_console_refs(seat);
        qa_ui_checkpoint_refs ui=frontend_seat_ui_refs(seat);
        qa_hud_checkpoint_refs hud=frontend_hud_image_refs(space);
        qa_hud_wheel_checkpoint_refs wheel=frontend_wheel_refs(seat);
        qa_ui_menu_checkpoint_refs menu={seat,catalog_encode,catalog_decode};
        qa_ui_assistance_checkpoint_refs assistance={seat,llm_encode,llm_decode};
        ok=qa_input_seat_restore(seat->input,row->components[SEAT_INPUT],&input_refs,error) &&
            qa_seat_console_save_restore(seat->console,&console,row->components[SEAT_CONSOLE],error) &&
            qa_ui_library_restore(seat->library,&menu,row->components[SEAT_LIBRARY],error) &&
            (!seat->mods || qa_ui_mods_restore(seat->mods,&menu,row->components[SEAT_MODS],error)) &&
            qa_ui_rankings_restore(seat->rankings,row->components[SEAT_RANKINGS],error) &&
            qa_ui_llm_restore(seat->assistance,&assistance,row->components[SEAT_ASSISTANCE],error) &&
            frontend_menu_restore(seat,row->components[SEAT_MENU],error) &&
            qa_ui_restore(seat->ui,&ui,row->components[SEAT_UI],error) &&
            hud_restore(seat,&hud,row->components[SEAT_HUD],error) &&
            qa_hud_wheel_restore(seat->wheel,&wheel,row->components[SEAT_WHEEL],error);
        if (!ok) break;
        seat->wheel_items=row->wheel_capacity?calloc(row->wheel_capacity,sizeof(*seat->wheel_items)):NULL;
        seat->wheel_definitions=row->wheel_capacity?calloc(row->wheel_capacity,sizeof(*seat->wheel_definitions)):NULL;
        seat->wheel_labels=row->wheel_labels?malloc(row->wheel_labels):NULL;
        if ((row->wheel_capacity && (!seat->wheel_items || !seat->wheel_definitions)) || (row->wheel_labels && !seat->wheel_labels)) {
            ok=frontend_fail(error,QA_ERROR_MEMORY,"Restoring actual wheel scratch reservations"); break;
        }
        seat->wheel_capacity=row->wheel_capacity; seat->wheel_label_capacity=row->wheel_labels;
        seat->builder=row->builder; seat->actor=row->actor; seat->sequence=row->sequence;
        seat->scores=row->scores; seat->chat_team=row->chat_team;
    }
    qa_source_save_dispose(&in); qa_source_save_dispose(&shown);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved seats differ from their real prepared owners");
    return ok;
}
