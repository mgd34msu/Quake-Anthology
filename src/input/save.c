#include "internal.h"
#include "qa/input_save.h"
#include "qa/source_save.h"

bool qa_input_seat_ui_binding_read(const qa_input_seat *seat, qa_input_ui_token token,
    qa_input_ui_handler *handler, void **context)
{
    if (!seat || !handler || !context) return false;
    for (size_t i = 0; i < seat->ui_count; ++i) {
        const qa_ui_record *record = &seat->ui[i];
        if (record->id != token || !record->active) continue;
        *handler = record->handler; *context = record->user; return true;
    }
    return false;
}

#define FIELD(type, object, name) do { if (!qa_source_save_##type(io,&(object)->name)) return false; } while (0)
static bool fail(qa_error *error, const char *message)
{ qa_error_set(error,QA_ERROR_FORMAT,0,"%s",message); return false; }
static bool refs_ready(const qa_input_checkpoint_refs *refs)
{ return refs && refs->services_encode && refs->services_decode && refs->ui_encode && refs->ui_decode && refs->catcher_ready; }
static bool allocate(qa_source_save_io *io, void **out, size_t count, size_t size)
{
    if (count>SIZE_MAX/size) return false;
    *out=count?calloc(count,size):NULL;
    if (count && !*out) { qa_error_set(io->error,QA_ERROR_MEMORY,io->offset,"Allocating saved input state"); return false; }
    return true;
}
static bool pair(qa_source_save_io *io, qa_input_pair *value)
{ return qa_source_save_f32(io,&value->x) && qa_source_save_f32(io,&value->y) && isfinite(value->x) && isfinite(value->y); }
static bool physical(qa_source_save_io *io, qa_physical_input *value)
{
    uint32_t kind=value->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_PHYSICAL_AXIS) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) value->kind=(qa_physical_kind)kind;
    FIELD(i32,value,device); FIELD(u32,value,code); FIELD(bool,value,positive);
    return qa_input_physical_valid(*value);
}
static bool button(qa_source_save_io *io, qa_input_button *value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=value->count;
    if (!qa_source_save_count(io,&count,reading?io->input.size/8:SIZE_MAX)) return false;
    if (reading) {
        if (count>4 && !allocate(io,(void **)&value->sources,count,sizeof(*value->sources))) return false;
        value->count=count; value->capacity=value->sources?count:0;
    } else if ((value->sources && count>value->capacity) || (!value->sources && count>4)) return false;
    uint64_t *sources=value->sources?value->sources:value->inline_sources;
    for (size_t i=0;i<count;++i) {
        if (!qa_source_save_u64(io,&sources[i])) return false;
        for (size_t j=0;j<i;++j) if (sources[j]==sources[i]) return false;
    }
    FIELD(f64,value,down_ms); FIELD(f64,value,held_ms); FIELD(bool,value,pressed); FIELD(bool,value,released);
    return isfinite(value->down_ms) && value->down_ms>=0 && isfinite(value->held_ms) && value->held_ms>=0;
}
static bool curve(qa_source_save_io *io, qa_stick_curve *value)
{
    uint32_t kind=value->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_STICK_AXIAL) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) value->kind=(qa_stick_kind)kind;
    FIELD(f32,value,deadzone); FIELD(f32,value,outer_threshold); FIELD(f32,value,exponent); return true;
}
static bool tuning(qa_source_save_io *io, qa_gamepad_tuning *value)
{
    if (!curve(io,&value->move) || !curve(io,&value->look)) return false;
    FIELD(f32,value,yaw_speed); FIELD(f32,value,pitch_speed); FIELD(f32,value,forward_sensitivity);
    FIELD(f32,value,side_sensitivity); FIELD(f32,value,trigger_threshold);
    FIELD(f32,value,gyro_yaw_sensitivity); FIELD(f32,value,gyro_pitch_sensitivity);
    uint32_t yaw=value->gyro_yaw_axis;
    if (!qa_source_save_u32(io,&yaw) || yaw>QA_GYRO_YAW_Z) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) value->gyro_yaw_axis=(qa_gyro_yaw_axis)yaw;
    FIELD(bool,value,swap_sticks); FIELD(bool,value,invert_pitch); FIELD(bool,value,gyro_enabled);
    return qa_gamepad_tuning_valid(value);
}
static bool gamepad(qa_source_save_io *io, qa_gamepad_input *value)
{
    for (size_t i=0;i<QA_AXIS_COUNT;++i) {
        if (!qa_source_save_f32(io,&value->axes[i]) || !qa_source_save_f32(io,&value->preview_axes[i]) ||
            !isfinite(value->axes[i]) || fabsf(value->axes[i])>1 || !isfinite(value->preview_axes[i]) || fabsf(value->preview_axes[i])>1) return false;
    }
    FIELD(vec3,value,gyro_sample); FIELD(vec3,value,gyro_bias);
    FIELD(bool,value,has_sample); FIELD(bool,value,has_bias); FIELD(bool,value,calibrating);
    FIELD(f64,&value->capture,start_ms); FIELD(f64,&value->capture,last_ms); FIELD(u64,&value->capture,samples);
    FIELD(vec3,&value->capture,mean); FIELD(vec3,&value->capture,deviation);
    return qa_vec_finite(value->gyro_sample) && qa_vec_finite(value->gyro_bias) &&
        qa_vec_finite(value->capture.mean) && qa_vec_finite(value->capture.deviation) &&
        isfinite(value->capture.start_ms) && value->capture.start_ms>=0 &&
        isfinite(value->capture.last_ms) && value->capture.last_ms>=value->capture.start_ms;
}
static bool command_sources(qa_source_save_io *io, qa_input_seat *seat)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=qa_strings_count(seat->command_sources);
    if (!qa_source_save_count(io,&count,reading?io->input.size/8:UINT32_MAX) || count>UINT32_MAX) return false;
    if (reading && !qa_strings_create(&seat->command_sources,io->error)) return false;
    for (size_t i=0;i<count;++i) {
        qa_bytes bytes=reading?(qa_bytes){0}:qa_strings_text(seat->command_sources,(qa_string_id)(i+1));
        size_t length=bytes.size;
        if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX)) return false;
        if (reading) {
            if (length>io->input.size-io->offset || memchr(io->input.data+io->offset,0,length)) return false;
            bytes=(qa_bytes){io->input.data+io->offset,length}; io->offset+=length;
            qa_string_id id;
            if (!qa_strings_intern(seat->command_sources,bytes,&id,io->error) || id!=i+1) return false;
        } else if (memchr(bytes.data,0,length) || !qa_source_save_bytes(io,(void *)bytes.data,length)) return false;
    }
    return true;
}
static bool binding(qa_source_save_io *io, qa_binding_record **out)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_input_binding value=reading?(qa_input_binding){0}:(*out)->view;
    uint32_t kind=value.kind; int32_t action=value.action;
    if (!physical(io,&value.input) || !qa_source_save_u32(io,&kind) || kind>QA_BIND_COMMAND ||
        !qa_source_save_i32(io,&action) || (kind==QA_BIND_ACTION && (action<0 || action>=QA_INPUT_ACTION_COUNT))) return false;
    size_t length=!reading && kind==QA_BIND_COMMAND?strlen(value.command):0;
    if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX-sizeof(qa_binding_record)-1) ||
        (kind==QA_BIND_ACTION && length)) return false;
    if (reading) {
        if (length>SIZE_MAX-sizeof(qa_binding_record)-1) return false;
        *out=calloc(1,sizeof(**out)+length+1);
        if (!*out) { qa_error_set(io->error,QA_ERROR_MEMORY,io->offset,"Allocating saved input binding"); return false; }
        (*out)->references=1; value.kind=(qa_binding_kind)kind; value.action=(qa_input_action)action;
        value.command=kind==QA_BIND_COMMAND?(*out)->text:NULL; (*out)->view=value;
    } else if ((kind==QA_BIND_COMMAND && value.command!=(*out)->text) ||
        (kind==QA_BIND_ACTION && value.command)) return false;
    char *text_value=reading?(*out)->text:(*out)->text;
    return qa_source_save_bytes(io,text_value,length) && !memchr(text_value,0,length);
}
static size_t dictionary_index(qa_binding_record *const *records, size_t count, const qa_binding_record *record)
{ size_t index=0; while (index<count && records[index]!=record) ++index; return index; }
static bool binding_graph(qa_source_save_io *io, qa_input_seat *seat)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_binding_record **records=NULL; size_t count=0, capacity=0; bool ok=true;
    if (!reading) {
        if (seat->held_count>SIZE_MAX-seat->binding_count) return false;
        capacity=seat->binding_count+seat->held_count;
        if (!allocate(io,(void **)&records,capacity,sizeof(*records))) return false;
        for (size_t i=0;i<seat->binding_count;++i) {
            if (!seat->bindings[i] || dictionary_index(records,count,seat->bindings[i])<count) { ok=false; break; }
            records[count++]=seat->bindings[i];
        }
        for (size_t i=0;ok && i<seat->held_count;++i)
            if (seat->held[i].binding && dictionary_index(records,count,seat->held[i].binding)==count) records[count++]=seat->held[i].binding;
        for (size_t i=0;ok && i<count;++i) {
            size_t holders=0;
            for (size_t j=0;j<seat->binding_count;++j) holders+=seat->bindings[j]==records[i];
            for (size_t j=0;j<seat->held_count;++j) holders+=seat->held[j].binding==records[i];
            if (holders!=records[i]->references) ok=false;
        }
    }
    if (ok) ok=qa_source_save_count(io,&count,reading?io->input.size/29:SIZE_MAX);
    if (ok && reading) ok=allocate(io,(void **)&records,count,sizeof(*records));
    for (size_t i=0;ok && i<count;++i) ok=binding(io,&records[i]);
    size_t live=seat->binding_count, held=seat->held_count;
    if (ok) ok=qa_source_save_count(io,&live,reading?io->input.size/8:SIZE_MAX) &&
        qa_source_save_count(io,&held,reading?io->input.size/21:SIZE_MAX);
    if (ok && reading) {
        ok=allocate(io,(void **)&seat->bindings,live,sizeof(*seat->bindings));
        if (ok) { seat->binding_count=seat->binding_capacity=live; ok=allocate(io,(void **)&seat->held,held,sizeof(*seat->held)); }
        if (ok) seat->held_count=seat->held_capacity=held;
    }
    for (size_t i=0;ok && i<live;++i) {
        uint64_t index=reading?0:dictionary_index(records,count,seat->bindings[i]);
        ok=qa_source_save_u64(io,&index) && index<count;
        if (ok && reading) { seat->bindings[i]=records[index]; ++records[index]->references; }
        for (size_t j=0;ok && j<i;++j) if (seat->bindings[j]==seat->bindings[i] ||
            qa_input_physical_equal(seat->bindings[j]->view.input,seat->bindings[i]->view.input)) ok=false;
    }
    for (size_t i=0;ok && i<held;++i) {
        qa_held_binding *value=&seat->held[i];
        uint64_t index=!reading && value->binding?dictionary_index(records,count,value->binding):UINT64_MAX;
        ok=physical(io,&value->input) && qa_source_save_u64(io,&index) && (index==UINT64_MAX || index<count);
        if (ok && reading && index!=UINT64_MAX) { value->binding=records[index]; ++records[index]->references; }
        if (ok && value->binding && !qa_input_physical_equal(value->input,value->binding->view.input)) ok=false;
        for (size_t j=0;ok && j<i;++j) if (qa_input_physical_equal(seat->held[j].input,value->input)) ok=false;
    }
    if (ok && reading) for (size_t i=0;i<count;++i) if (records[i]->references==1) { ok=false; break; }
    if (reading) for (size_t i=0;records && i<count;++i) qa_input_binding_record_release(records[i]);
    free(records); return ok;
}
static bool ui_graph(qa_source_save_io *io, qa_input_seat *seat, const qa_input_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=seat->ui_count;
    if (!qa_source_save_count(io,&count,reading?io->input.size/21:SIZE_MAX) || !count ||
        !qa_source_save_u64(io,&seat->next_ui) || !seat->next_ui) return false;
    if (reading) {
        if (!allocate(io,(void **)&seat->ui,count,sizeof(*seat->ui))) return false;
        seat->ui_count=seat->ui_capacity=count;
    }
    for (size_t i=0;i<count;++i) {
        qa_ui_record *value=&seat->ui[i]; uint32_t focus=value->focus; uint64_t key=UINT64_MAX;
        if (!qa_source_save_u64(io,&value->id) || !qa_source_save_bool(io,&value->active) ||
            !qa_source_save_u32(io,&focus) || focus>QA_INPUT_UI || (i?(!value->id || value->id>=seat->next_ui):value->id!=0) ||
            ((i==0 || i+1==count) && !value->active)) return false;
        if (reading) value->focus=(qa_input_focus)focus;
        if (!reading && value->active && value->handler && !refs->ui_encode(refs->context,value->handler,value->user,&key,io->error)) return false;
        if (!qa_source_save_u64(io,&key) || (!value->active && key!=UINT64_MAX)) return false;
        if (reading && key!=UINT64_MAX && (!refs->ui_decode(refs->context,key,&value->handler,&value->user,io->error) || !value->handler)) return false;
        if (value->active && i && !value->handler) return false;
        if (!i && (value->handler!=seat->options.ui || (value->handler && value->user!=seat->options.ui_user))) return false;
        for (size_t j=0;j<i;++j) if (seat->ui[j].id==value->id) return false;
    }
    return true;
}
static bool continuation(qa_source_save_io *io, qa_input_seat *seat, const qa_input_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint32_t dialect=seat->options.context.dialect, focus=seat->focus;
    if (!qa_source_save_u32(io,&dialect) || dialect>QA_CONSOLE_Q3 || !qa_source_save_u32(io,&focus) || focus>QA_INPUT_UI) return false;
    if (reading) { seat->options.context.dialect=(qa_console_dialect)dialect; seat->focus=(qa_input_focus)focus; }
    FIELD(bool,seat,focused); FIELD(u8,seat,impulse);
    if (!tuning(io,&seat->options.gamepad) || !gamepad(io,&seat->gamepad) || !pair(io,&seat->mouse)) return false;
    for (size_t i=0;i<QA_INPUT_ACTION_COUNT;++i) if (!button(io,&seat->buttons[i])) return false;
    size_t count=seat->catcher_count;
    if (!qa_source_save_count(io,&count,reading?io->input.size/12:SIZE_MAX)) return false;
    if (reading) {
        if (!allocate(io,(void **)&seat->catchers,count,sizeof(*seat->catchers))) return false;
        seat->catcher_count=seat->catcher_capacity=count;
    }
    for (size_t i=0;i<count;++i) {
        if (!qa_source_save_u64(io,&seat->catchers[i].owner) || !qa_source_save_u32(io,&seat->catchers[i].mask) ||
            !seat->catchers[i].owner || !seat->catchers[i].mask || !refs->catcher_ready(refs->context,seat->catchers[i].owner,io->error)) return false;
        for (size_t j=0;j<i;++j) if (seat->catchers[j].owner==seat->catchers[i].owner) return false;
    }
    return command_sources(io,seat) && binding_graph(io,seat) && ui_graph(io,seat,refs);
}
bool qa_input_seat_checkpoint(const qa_input_seat *seat, const qa_input_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!seat || seat->release || !out || !refs_ready(refs)) return fail(error,"Input capture requires a seat and owner resolvers");
    qa_source_save_io io; uint8_t magic[4]={'Q','I','N','S'}; uint32_t schema=2; uint64_t services=0;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    qa_input_seat saved=*seat;
    bool ok=qa_input_seat_context_ready(seat,&seat->options.context,error) &&
        refs->services_encode(refs->context,&seat->options,&services,error) && qa_source_save_bytes(&io,magic,4) &&
        qa_source_save_u32(&io,&schema) && qa_source_save_u64(&io,&services) &&
        qa_source_save_u32(&io,&saved.options.seat) && qa_source_save_u32(&io,&saved.options.context.seat) &&
        continuation(&io,&saved,refs) && qa_source_save_finish(&io,out);
    if (!ok && error && error->code==QA_OK) fail(error,"Invalid retained input seat");
    qa_source_save_dispose(&io); return ok;
}
bool qa_input_seat_restore(qa_input_seat *seat, qa_bytes bytes, const qa_input_checkpoint_refs *refs, qa_error *error)
{
    if (!seat || seat->release || !refs_ready(refs)) return fail(error,"Input restore requires an installed candidate seat and owner resolvers");
    qa_source_save_io io; uint8_t magic[4]; uint32_t schema=0, ordinal=0, launch_seat=0; uint64_t services=0;
    qa_input_seat *saved=calloc(1,sizeof(*saved));
    if (!saved) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating restored input seat"); return false; }
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { free(saved); return false; }
    bool ok=qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"QINS",4) && qa_source_save_u32(&io,&schema) && schema==2 &&
        qa_source_save_u64(&io,&services) && refs->services_decode(refs->context,services,&saved->options,error) &&
        qa_source_save_u32(&io,&ordinal) && ordinal<4 && ordinal==seat->options.seat && ordinal==saved->options.seat &&
        qa_source_save_u32(&io,&launch_seat) && launch_seat==saved->options.context.seat &&
        saved->options.console==seat->options.console && saved->options.cvars==seat->options.cvars &&
        saved->options.context.origin==QA_COMMAND_SEAT &&
        saved->options.ui==seat->options.ui && saved->options.ui_user==seat->options.ui_user &&
        saved->options.before_ui==seat->options.before_ui && saved->options.before_ui_user==seat->options.before_ui_user &&
        saved->options.context_ready==seat->options.context_ready && saved->options.context_user==seat->options.context_user &&
        continuation(&io,saved,refs) && qa_source_save_finish(&io,NULL) &&
        qa_input_seat_context_ready(saved,&saved->options.context,error);
    if (ok) { qa_input_seat old=*seat; *seat=*saved; *saved=old; }
    qa_input_seat_destroy(saved); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) fail(error,"Invalid or unqualified retained input seat");
    return ok;
}
