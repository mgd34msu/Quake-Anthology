#include "internal.h"
#include "qa/ui_save.h"
#include "qa/ui_presentation_prepare.h"

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io,&(object)->name)) return false; } while (0)
static bool pair(qa_source_save_io *io, qa_input_pair *p)
{
    FIELD(f32,p,x); FIELD(f32,p,y); return true;
}
typedef struct field_key { qa_ui_id menu, control; } field_key;
static int id_compare(const void *a, const void *b)
{
    qa_ui_id left=*(const qa_ui_id*)a,right=*(const qa_ui_id*)b; return (left>right)-(left<right);
}
static int field_compare(const void *a, const void *b)
{
    const field_key *left=a,*right=b;
    if (left->menu!=right->menu) return (left->menu>right->menu)-(left->menu<right->menu);
    return (left->control>right->control)-(left->control<right->control);
}
static bool topology(const qa_ui *saved, const qa_ui *qualified, qa_error *error)
{
    if (qualified->menu_count>SIZE_MAX/sizeof(qa_ui_id) || saved->field_count>SIZE_MAX/sizeof(field_key)) return false;
    qa_ui_id *menus=qualified->menu_count?malloc(qualified->menu_count*sizeof(*menus)):NULL;
    qa_ui_id *stack=saved->depth?malloc(saved->depth*sizeof(*stack)):NULL;
    field_key *fields=saved->field_count?malloc(saved->field_count*sizeof(*fields)):NULL;
    if ((qualified->menu_count && !menus) || (saved->depth && !stack) || (saved->field_count && !fields)) {
        free(menus); free(stack); free(fields); return ui_fail(error,"Allocating UI retained identity index");
    }
    for (size_t i=0;i<qualified->menu_count;++i) menus[i]=qualified->menus[i].id;
    if (qualified->menu_count>1) qsort(menus,qualified->menu_count,sizeof(*menus),id_compare);
    bool ok=true;
    for (size_t i=0;i<saved->depth;++i) {
        stack[i]=saved->stack[i].menu;
        if (!bsearch(&stack[i],menus,qualified->menu_count,sizeof(*menus),id_compare)) ok=false;
    }
    if (saved->depth>1) qsort(stack,saved->depth,sizeof(*stack),id_compare);
    for (size_t i=1;i<saved->depth;++i) if (stack[i-1]==stack[i]) ok=false;
    for (size_t i=0;i<saved->field_count;++i) fields[i]=(field_key){saved->fields[i].menu,saved->fields[i].control};
    if (saved->field_count>1) qsort(fields,saved->field_count,sizeof(*fields),field_compare);
    for (size_t i=1;i<saved->field_count;++i) if (!field_compare(&fields[i-1],&fields[i])) ok=false;
    free(menus); free(stack); free(fields); return ok;
}
static bool registration_prefix(qa_source_save_io *io, const qa_ui *qualified,
    uint32_t expected_seat, qa_ui_id menu, bool *present, size_t *ordinal)
{
    uint8_t magic[4]={'Q','A','U','I'}; uint32_t seat=expected_seat;
    bool input_clock=qualified && qualified->options.input_now_ms!=NULL;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QAUI",4) ||
        !qa_source_save_u32(io,&seat) || seat!=expected_seat) return false;
    if (!qa_source_save_bool(io,&input_clock) ||
        (qualified && input_clock!=(qualified->options.input_now_ms!=NULL))) return false;
    size_t menus=qualified?qualified->menu_count:0;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/8:SIZE_MAX;
    if (!qa_source_save_count(io,&menus,maximum) || (qualified && menus!=qualified->menu_count)) return false;
    for (size_t i=0;i<menus;++i) {
        uint64_t id=qualified?qualified->menus[i].id:0;
        if (!qa_source_save_u64(io,&id) || !id || (qualified && id!=qualified->menus[i].id)) return false;
        if (present && id==menu) {
            if (*present) return false;
            *present=true; *ordinal=i;
        }
    }
    return true;
}
bool qa_ui_checkpoint_menu_read(qa_bytes bytes, uint32_t seat, qa_ui_id menu,
    bool *present, size_t *ordinal, qa_error *error)
{
    if (!menu || !present || !ordinal) return ui_fail(error,"UI menu prefix query requires an actual menu identity");
    *present=false; *ordinal=0;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    bool ok=registration_prefix(&io,NULL,seat,menu,present,ordinal);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) ui_fail(error,"Saved UI menu registration prefix is inconsistent");
    return ok;
}
bool qa_ui_restore_menu_register(qa_ui *ui, const qa_ui_menu_registration *registration,
    size_t ordinal, qa_error *error)
{
    if (!qa_ui_presentation_idle(ui) || ui->depth || ui->field_count || ui->input_token || ordinal>ui->menu_count)
        return ui_fail(error,"UI registration restore requires an empty idle controller and saved position");
    size_t count=ui->menu_count;
    if (!qa_ui_register(ui,registration,error)) return false;
    qa_ui_menu_registration added=ui->menus[count];
    memmove(ui->menus+ordinal+1,ui->menus+ordinal,(count-ordinal)*sizeof(*ui->menus));
    ui->menus[ordinal]=added; return true;
}
static bool fields(qa_source_save_io *io, qa_ui *saved, const qa_ui *qualified)
{
    if (!registration_prefix(io,qualified,qualified->options.seat,0,NULL,NULL)) return false;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size:SIZE_MAX;
    if (!qa_source_save_count(io,&saved->depth,qualified->menu_count) ||
        !qa_source_save_count(io,&saved->field_count,maximum/34)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ &&
        (!ui_reserve((void**)&saved->stack,&saved->stack_capacity,saved->depth,sizeof(*saved->stack),io->error) ||
         !ui_reserve((void**)&saved->fields,&saved->field_capacity,saved->field_count,sizeof(*saved->fields),io->error))) return false;
    for (size_t i=0;i<saved->depth;++i) {
        ui_cursor value=io->direction==QA_SOURCE_SAVE_WRITE?saved->stack[i]:(ui_cursor){0};
        if (!qa_source_save_u64(io,&value.menu) ||
            !qa_source_save_u64(io,&value.control) || !qa_source_save_f32(io,&value.scroll) || !isfinite(value.scroll) || value.scroll<0) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) saved->stack[i]=value;
    }
    for (size_t i=0;i<saved->field_count;++i) {
        ui_field value=io->direction==QA_SOURCE_SAVE_WRITE?saved->fields[i]:(ui_field){0};
        if (!qa_source_save_u64(io,&value.menu) ||
            !qa_source_save_u64(io,&value.control) || !value.control ||
            !qa_source_save_count(io,&value.cursor,SIZE_MAX) || !qa_source_save_count(io,&value.top,SIZE_MAX) ||
            !qa_source_save_u64(io,&value.revision) || !qa_source_save_bool(io,&value.overstrike) || !qa_source_save_bool(io,&value.scrolled)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) saved->fields[i]=value;
    }
    if (!pair(io,&saved->cursor) || !pair(io,&saved->pointer)) return false;
    FIELD(f32,saved,scale); FIELD(f32,saved,bias_x); FIELD(f32,saved,bias_y);
    FIELD(f32,saved,text_scale);
    uint32_t color_mode=saved->color_mode;
    if (!qa_source_save_u32(io,&color_mode) || color_mode>QA_UI_COLOR_MONOCHROME ||
        !isfinite(saved->text_scale) || saved->text_scale<.75f || saved->text_scale>2) return false;
    saved->color_mode=(qa_ui_color_mode)color_mode;
    FIELD(i32,&saved->viewport,x); FIELD(i32,&saved->viewport,y);
    FIELD(u32,&saved->viewport,width); FIELD(u32,&saved->viewport,height);
    FIELD(u64,saved,dragging); FIELD(f32,saved,drag_offset);
    FIELD(bool,saved,shift); FIELD(bool,saved,control); FIELD(bool,saved,capture);
    FIELD(bool,saved,menu_dragging); FIELD(bool,saved,has_pointer); FIELD(f64,saved,time_ms);
    if (!isfinite(saved->pointer.x) || !isfinite(saved->pointer.y) || !isfinite(saved->time_ms) || saved->time_ms<0 ||
        (saved->capture && (!saved->depth || !qualified->options.binding)) ||
        ((saved->dragging || saved->menu_dragging) && !saved->depth)) return false;
    for (size_t i=0;i<QA_AXIS_COUNT;++i) {
        int32_t code=saved->held_direction[i];
        if (!qa_source_save_i32(io,&code) || !qa_source_save_f64(io,&saved->repeat_at[i]) || !isfinite(saved->repeat_at[i])) return false;
        bool valid=code==0 || (i==QA_AXIS_LEFT_X && (code==QA_KEY_LEFT || code==QA_KEY_RIGHT)) ||
            (i==QA_AXIS_LEFT_Y && (code==QA_KEY_UP || code==QA_KEY_DOWN));
        if (!valid || (code && !saved->depth)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) saved->held_direction[i]=code;
    }
    uint32_t status=saved->error.code;
    if (!qa_source_save_u32(io,&status) || status>QA_ERROR_NOT_FOUND || !qa_source_save_count(io,&saved->error.offset,SIZE_MAX) ||
        !qa_source_save_bytes(io,saved->error.message,sizeof(saved->error.message)) || !memchr(saved->error.message,0,sizeof(saved->error.message))) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) saved->error.code=(qa_status)status;
    return topology(saved,qualified,io->error);
}
#undef FIELD
static bool clock_ready(const qa_ui *ui,const qa_ui_checkpoint_refs *refs,qa_error *error)
{
    if (!ui->options.input_now_ms) return true;
    return (refs && refs->input_clock_ready && refs->input_clock_ready(refs->context,ui,
        ui->options.input_now_ms,ui->options.context,error)) ||
        ui_fail(error,"UI physical input clock has no actual factory qualifier");
}
static bool token_write(qa_source_save_io *io, const qa_ui *ui, const qa_ui_checkpoint_refs *refs)
{
    uint64_t key=0;
    if ((ui->depth!=0)!=(ui->input_token!=0)) return ui_fail(io->error,"UI stack and actual input handler differ");
    if (ui->input_token && (!refs || !refs->input_encode || !refs->input_encode(refs->context,ui,ui->input_token,&key,io->error)))
        return ui_fail(io->error,"UI input handler owner encoder is unavailable");
    return qa_source_save_u64(io,&key);
}
bool qa_ui_checkpoint(const qa_ui *ui, const qa_ui_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || !qa_ui_presentation_idle(ui)) return ui_fail(error,"UI capture requires an idle controller");
    if (!clock_ready(ui,refs,error)) return false;
    qa_ui saved=*ui; qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=fields(&io,&saved,ui) && token_write(&io,ui,refs) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) ui_fail(error,"UI retained controller state is inconsistent");
    qa_source_save_dispose(&io); return ok;
}
bool qa_ui_restore(qa_ui *ui, const qa_ui_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!qa_ui_presentation_idle(ui) || ui->depth || ui->field_count || ui->input_token)
        return ui_fail(error,"UI restore requires an empty idle qualified controller");
    if (!clock_ready(ui,refs,error)) return false;
    qa_ui saved=*ui; saved.stack=NULL; saved.fields=NULL; saved.stack_capacity=saved.field_capacity=0;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) return false;
    uint64_t key=0; qa_input_ui_token token=0;
    bool ok=fields(&io,&saved,ui) && qa_source_save_u64(&io,&key) && qa_source_save_finish(&io,NULL);
    if (ok && saved.depth) ok=refs && refs->input_decode && refs->input_decode(refs->context,ui,key,&token,error) && token;
    if (ok && !saved.depth && key) ok=false;
    if (ok) {
        free(ui->stack); free(ui->fields); saved.input_token=token; *ui=saved;
    } else {
        free(saved.stack); free(saved.fields);
        if (error && error->code==QA_OK) ui_fail(error,"Saved UI controller/input owner is inconsistent");
    }
    qa_source_save_dispose(&io); return ok;
}
