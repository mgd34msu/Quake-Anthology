#include "shared_storage.h"
#include "qa/console_cvar_observer.h"
#include "shared_register.h"
#include "save_private.h"
#include "qa/json.h"
#include "qa/json_writer.h"
#include "qa/text.h"
#include <math.h>

struct frontend_shared_storage {
    qa_settings_store user,devices,input;
    qa_resource *images;
    qa_cvar_archive archive;
    frontend_shared_audio_preferences audio;
    frontend_shared_view_preferences view;
    char *device,*menu;
    bool graphical;
};
static const char *const input_names[]={"in_midi","in_midiport","in_midichannel","in_mididevice",
    "in_midiseat","in_mouse","in_dgamouse","in_subframe","in_nograb","in_joystick",
    "in_debugjoystick","joy_threshold","in_joystickProfile","in_joyBallScale","in_joystickSeat"};
static bool fail(qa_error *e,const char *text)
{ return frontend_fail(e,QA_ERROR_FORMAT,text); }
static bool store_valid(qa_settings_store store)
{
    for (size_t i=0;store.vfs && i<qa_vfs_mount_count(store.vfs);++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(store.vfs,i,&mount)) return false;
        if (mount.id==store.mount) return mount.writable && !mount.is_archive &&
            qa_vfs_mount_root(store.vfs,store.mount);
    }
    return false;
}
static bool input_valid(qa_settings_store store)
{ return store.vfs?store_valid(store):store.mount==0; }
static bool same_store(qa_settings_store a,qa_settings_store b)
{
    return a.vfs && b.vfs?qa_fs_root_same_object(qa_vfs_mount_root(a.vfs,a.mount),
        qa_vfs_mount_root(b.vfs,b.mount)):!a.vfs && !a.mount && !b.vfs && !b.mount;
}
static bool image_owned(const frontend_shared_storage *owner,const qa_resource *image)
{
    if (!image) return true;
    for (size_t i=0;i<qa_vfs_read_count(owner->user.vfs);++i) {
        qa_vfs_read_reference opening;
        if (!qa_vfs_read_at(owner->user.vfs,i,&opening)) return false;
        if (opening.resource==image && opening.mount==owner->user.mount &&
            !strcmp(opening.path,"settings/images.cfg")) return true;
    }
    return false;
}
bool frontend_shared_storage_current(const frontend_shared_storage *owner,
    qa_settings_store user,qa_settings_store devices,qa_settings_store input,bool graphical)
{
    return owner && owner->graphical==graphical && store_valid(user) && store_valid(devices) && input_valid(input) &&
        store_valid(owner->user) && store_valid(owner->devices) && input_valid(owner->input) &&
        qa_fs_root_same_object(qa_vfs_mount_root(owner->user.vfs,owner->user.mount),qa_vfs_mount_root(user.vfs,user.mount)) &&
        qa_fs_root_same_object(qa_vfs_mount_root(owner->devices.vfs,owner->devices.mount),qa_vfs_mount_root(devices.vfs,devices.mount)) &&
        same_store(owner->input,input);
}
static bool string(const qa_json_document *d,qa_json_id id,char **out,qa_error *e)
{
    qa_buffer text={0};
    if (!qa_json_string(d,id,&text,e)) return false;
    if (memchr(text.data,0,text.size)) { qa_buffer_free(&text); return fail(e,"Shared preference contains embedded NUL"); }
    *out=(char *)text.data; return true;
}
static bool number(const qa_json_document *d,qa_json_id root,const char *name,double *out,qa_error *e)
{ return qa_json_number(d,qa_json_get(d,root,name),out,e) && isfinite(*out); }
static bool format_valid(qa_audio_output_format format)
{
    return format.sample_rate>=8000 && format.sample_rate<=192000 &&
        (format.sample_bits==8 || format.sample_bits==16) && (format.channels==1 || format.channels==2);
}
static bool archive_add(frontend_shared_storage *owner,const char *name,const char *value,qa_error *e)
{
    if (owner->archive.count==SIZE_MAX/sizeof(*owner->archive.entries))
        return fail(e,"Shared archive exceeds native storage");
    qa_cvar_archive_entry *rows=realloc(owner->archive.entries,(owner->archive.count+1)*sizeof(*rows));
    if (!rows) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining shared preference archive");
    owner->archive.entries=rows;
    qa_cvar_archive_entry *row=rows+owner->archive.count++;
    *row=(qa_cvar_archive_entry){0};
    row->name=malloc(strlen(name)+1); row->value=malloc(strlen(value)+1);
    if (!row->name || !row->value) return frontend_fail(e,QA_ERROR_MEMORY,"Copying shared preference value");
    strcpy(row->name,name); strcpy(row->value,value); return true;
}
static bool audio_load(frontend_shared_storage *owner,qa_error *e)
{
    qa_resource *file=NULL; bool found=false;
    if (!qa_settings_read(owner->input,"audio.json",&file,&found,e)) return false;
    if (!found) return true;
    qa_json_document *d=NULL; bool ok=qa_json_parse(qa_resource_bytes(file),&d,e);
    qa_json_id root=ok?qa_json_root(d):QA_JSON_NONE;
    double version=0;
    frontend_shared_audio_preferences value={.format={48000,2,16},.present=true};
    if (ok) ok=qa_json_type(d,root)==QA_JSON_OBJECT && number(d,root,"version",&version,e) && version==1 &&
        number(d,root,"effectsVolume",&value.effects,e) && value.effects>=0 && value.effects<=1 &&
        number(d,root,"musicVolume",&value.music,e) && value.music>=0 && value.music<=1;
    qa_json_id device=qa_json_get(d,root,"deviceName");
    if (ok && qa_json_type(d,device)!=QA_JSON_NULL)
        ok=string(d,device,&owner->device,e) && *owner->device;
    qa_json_id format=qa_json_get(d,root,"outputFormat");
    if (ok && format!=QA_JSON_NONE) {
        double rate=0,bits=0,channels=0;
        ok=qa_json_type(d,format)==QA_JSON_OBJECT && number(d,format,"sampleRate",&rate,e) &&
            number(d,format,"sampleBits",&bits,e) && number(d,format,"channels",&channels,e) &&
            rate>=8000 && rate<=192000 && floor(rate)==rate && (bits==8 || bits==16) && (channels==1 || channels==2);
        if (ok) value.format=(qa_audio_output_format){(uint32_t)rate,(unsigned)channels,(unsigned)bits};
    }
    qa_json_id shuffle=qa_json_get(d,root,"musicShuffle"),menu=qa_json_get(d,root,"menuTrack");
    value.has_shuffle=shuffle!=QA_JSON_NONE; value.has_menu_track=menu!=QA_JSON_NONE;
    if (ok && value.has_shuffle) ok=qa_json_bool(d,shuffle,&value.shuffle,e);
    if (ok && value.has_menu_track) ok=string(d,menu,&owner->menu,e) && frontend_shared_menu_track_valid(owner->menu);
    qa_json_destroy(d); qa_resource_release(file);
    if (!ok) return fail(e,"Invalid shared audio preferences");
    value.device=owner->device; value.menu_track=owner->menu; owner->audio=value;
    char text[32];
    static const char *const fields[]={"s_outputRate","s_outputBits","s_outputChannels","volume","bgmvolume"};
    const double values[]={value.format.sample_rate,value.format.sample_bits,value.format.channels,value.effects,value.music};
    for (size_t i=0;i<sizeof(fields)/sizeof(*fields);++i)
        if (!qa_format_ecmascript_number(values[i],text,e) || !archive_add(owner,fields[i],text,e)) return false;
    return (!value.has_shuffle || archive_add(owner,"music_shuffle",value.shuffle?"1":"0",e)) &&
        (!value.has_menu_track || archive_add(owner,"music_menu_track",value.menu_track,e));
}
static bool view_valid(frontend_shared_view_preferences view)
{
    return view.present?(isfinite(view.field_of_view) && view.field_of_view>=60 && view.field_of_view<=160):
        view.field_of_view==0;
}
static bool view_load(frontend_shared_storage *owner,qa_error *e)
{
    qa_resource *file=NULL; bool found=false;
    if (!qa_settings_read(owner->input,"view.json",&file,&found,e)) return false;
    if (!found) return true;
    qa_json_document *document=NULL;
    bool ok=qa_json_parse(qa_resource_bytes(file),&document,e);
    qa_json_id root=ok?qa_json_root(document):QA_JSON_NONE;
    double version=0;
    frontend_shared_view_preferences view={.present=true};
    if (ok) ok=qa_json_type(document,root)==QA_JSON_OBJECT &&
        number(document,root,"version",&version,e) && version==1 &&
        number(document,root,"fieldOfView",&view.field_of_view,e) && view_valid(view);
    qa_json_destroy(document); qa_resource_release(file);
    if (!ok) return fail(e,"Invalid explicit sticky view preferences");
    owner->view=view; return true;
}
bool frontend_shared_storage_load_devices(const frontend_shared_storage *owner,
    qa_cvar_archive *out,qa_error *e)
{
    if (!owner || !out || out->entries || out->count || !store_valid(owner->devices))
        return fail(e,"Fresh input preferences require their retained actual device store");
    static const char *const input_owner[]={"input","devices"};
    return qa_settings_load_cvars(owner->devices,input_owner,2,QA_CONSOLE_Q3,out,e);
}
bool frontend_shared_storage_open(qa_settings_store user,qa_settings_store devices,
    qa_settings_store input,bool graphical,
    frontend_shared_storage **out,qa_error *e)
{
    if (!out || *out || !store_valid(user) || !store_valid(devices) || !input_valid(input))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Shared storage requires real global stores and an actual or absent sticky product");
    frontend_shared_storage *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining shared settings storage");
    owner->user=(qa_settings_store){qa_vfs_clone(user.vfs,e),user.mount};
    owner->devices=(qa_settings_store){qa_vfs_clone(devices.vfs,e),devices.mount};
    owner->input=(qa_settings_store){input.vfs?qa_vfs_clone(input.vfs,e):NULL,input.mount};
    owner->graphical=graphical;
    bool found=false;
    static const char *const input_owner[]={"input","devices"};
    bool ok=owner->user.vfs && owner->devices.vfs && (!input.vfs || owner->input.vfs) &&
        frontend_shared_storage_current(owner,user,devices,input,graphical) &&
        (!graphical || qa_settings_read(owner->user,"settings/images.cfg",&owner->images,&found,e)) &&
        qa_settings_load_cvars(owner->devices,input_owner,2,QA_CONSOLE_Q3,&owner->archive,e) &&
        (!graphical || !input.vfs || (audio_load(owner,e) && view_load(owner,e)));
    if (ok && owner->images) {
        qa_bytes bytes=qa_resource_bytes(owner->images);
        ok=!bytes.size || !memchr(bytes.data,0,bytes.size);
        if (!ok) fail(e,"Shared image script contains embedded NUL");
    }
    if (!ok) { frontend_shared_storage_destroy(owner); return false; }
    *out=owner; return true;
}
bool frontend_shared_storage_adopt_input(frontend_shared_storage *owner,qa_settings_store input,qa_error *e)
{
    if (!owner || !store_valid(input) || !store_valid(owner->user) || !store_valid(owner->devices))
        return fail(e,"Sticky preference adoption requires the real global and selected product stores");
    if (owner->input.vfs) return (store_valid(owner->input) && same_store(owner->input,input)) ||
        fail(e,"Sticky product preference authority cannot be replaced");
    frontend_shared_storage *pending=calloc(1,sizeof(*pending));
    if (!pending) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining sticky product preference admission");
    pending->input=(qa_settings_store){qa_vfs_clone(input.vfs,e),input.mount};
    bool ok=pending->input.vfs && (!owner->graphical || (audio_load(pending,e) && view_load(pending,e)));
    if (ok && pending->archive.count) {
        if (pending->archive.count>SIZE_MAX/sizeof(*owner->archive.entries)-owner->archive.count)
            ok=fail(e,"Sticky preference archive exceeds native storage");
        else {
            size_t count=owner->archive.count+pending->archive.count;
            qa_cvar_archive_entry *rows=realloc(owner->archive.entries,count*sizeof(*rows));
            if (!rows) ok=frontend_fail(e,QA_ERROR_MEMORY,"Joining sticky product preferences");
            else {
                memcpy(rows+owner->archive.count,pending->archive.entries,
                    pending->archive.count*sizeof(*rows));
                owner->archive.entries=rows; owner->archive.count=count;
                free(pending->archive.entries); pending->archive=(qa_cvar_archive){0};
            }
        }
    }
    if (ok) {
        owner->input=pending->input; pending->input=(qa_settings_store){0};
        owner->audio=pending->audio; owner->view=pending->view;
        owner->device=pending->device; pending->device=NULL;
        owner->menu=pending->menu; pending->menu=NULL;
    }
    frontend_shared_storage_destroy(pending); return ok;
}
bool frontend_shared_storage_input(const frontend_shared_storage *owner,qa_settings_store *out)
{
    if (!owner || !out || !store_valid(owner->user) || !store_valid(owner->devices) ||
        !input_valid(owner->input)) return false;
    *out=owner->input; return true;
}
qa_bytes frontend_shared_storage_images(const frontend_shared_storage *owner)
{ return owner && owner->images?qa_resource_bytes(owner->images):(qa_bytes){0}; }
const qa_cvar_archive *frontend_shared_storage_archive(const frontend_shared_storage *owner)
{ return owner?&owner->archive:NULL; }
const frontend_shared_audio_preferences *frontend_shared_storage_audio(const frontend_shared_storage *owner)
{ return owner?&owner->audio:NULL; }
const frontend_shared_view_preferences *frontend_shared_storage_view(const frontend_shared_storage *owner)
{ return owner?&owner->view:NULL; }
static bool same_name(const qa_cvars *registry,const char *a,const char *b)
{
    bool folded=qa_cvars_dialect(registry)==QA_CONSOLE_Q3;
    while (*a && *b) {
        unsigned char left=(unsigned char)*a++,right=(unsigned char)*b++;
        if (folded && left>='A' && left<='Z') left+='a'-'A';
        if (folded && right>='A' && right<='Z') right+='a'-'A';
        if (left!=right) return false;
    }
    return !*a && !*b;
}
static bool input_name(const qa_cvars *registry,const char *name)
{
    for (size_t i=0;i<sizeof(input_names)/sizeof(*input_names);++i)
        if (same_name(registry,name,input_names[i])) return true;
    return false;
}
static bool image_filter(void *context,const qa_cvars *registry,const qa_cvar_view *row)
{
    (void)context;
    static const char *const audio[]={"gamma","volume","bgmvolume","music_shuffle","music_menu_track",
        "s_outputRate","s_outputBits","s_outputChannels"};
    if (input_name(registry,row->name)) return false;
    for (size_t i=0;i<sizeof(audio)/sizeof(*audio);++i)
        if (same_name(registry,row->name,audio[i])) return false;
    return true;
}
bool frontend_shared_storage_save_images(frontend_shared_storage *owner,const qa_cvars *registry,qa_error *e)
{
    if (!owner || !owner->graphical || !qa_cvars_observer_idle(registry)) return fail(e,"Shared image save requires published returned graphical scalars");
    qa_buffer text={0};
    bool ok=qa_cvars_config_filtered(registry,image_filter,NULL,&text,e) &&
        qa_settings_write(owner->user,"settings/images.cfg",(qa_bytes){text.data,text.size},e);
    qa_buffer_free(&text); return ok;
}
static void key_number(qa_json_writer *w,const char *name,double value)
{ qa_json_writer_key(w,name); qa_json_writer_number(w,value); }
static void key_string(qa_json_writer *w,const char *name,const char *value)
{ qa_json_writer_key(w,name); qa_json_writer_string(w,value); }
static bool write_json(qa_settings_store store,const char *path,qa_json_writer *w,qa_error *e)
{
    qa_buffer bytes={0}; bool ok=qa_json_writer_finish(w,&bytes,e) &&
        qa_settings_write(store,path,(qa_bytes){bytes.data,bytes.size},e);
    qa_buffer_free(&bytes); qa_json_writer_destroy(w); return ok;
}
bool frontend_shared_storage_save_input(frontend_shared_storage *owner,const qa_cvars *registry,qa_error *e)
{
    if (!owner || !qa_cvars_observer_idle(registry)) return fail(e,"Shared input save requires published returned scalars");
    qa_json_writer w={0}; qa_json_writer_object(&w); key_number(&w,"version",1); key_string(&w,"dialect","q3");
    qa_json_writer_key(&w,"entries"); qa_json_writer_array(&w);
    for (size_t i=0;i<qa_cvars_count(registry);++i) {
        const qa_cvar_view *row=qa_cvars_at(registry,i);
        const char *value=qa_cvars_archive_value(registry,row);
        if (!value || !input_name(registry,row->name)) continue;
        qa_json_writer_object(&w); key_string(&w,"name",row->name); key_string(&w,"value",value); qa_json_writer_end(&w);
    }
    qa_json_writer_end(&w); qa_json_writer_end(&w);
    return write_json(owner->devices,"cvars/input/devices.json",&w,e);
}
bool frontend_shared_storage_save_audio(frontend_shared_storage *owner,
    const frontend_shared_audio_preferences *value,qa_error *e)
{
    if (!owner || !owner->graphical || !store_valid(owner->input) || !value || !format_valid(value->format) || !isfinite(value->effects) ||
        value->effects<0 || value->effects>1 || !isfinite(value->music) || value->music<0 || value->music>1 ||
        (value->device && !*value->device) || !frontend_shared_menu_track_valid(value->menu_track))
        return fail(e,"Invalid published shared audio preferences");
    qa_json_writer w={0}; qa_json_writer_object(&w); key_number(&w,"version",1);
    qa_json_writer_key(&w,"deviceName");
    if (value->device) qa_json_writer_string(&w,value->device); else qa_json_writer_null(&w);
    key_number(&w,"effectsVolume",value->effects); key_number(&w,"musicVolume",value->music);
    qa_json_writer_key(&w,"musicShuffle"); qa_json_writer_bool(&w,value->shuffle);
    key_string(&w,"menuTrack",value->menu_track); qa_json_writer_key(&w,"outputFormat"); qa_json_writer_object(&w);
    key_number(&w,"sampleRate",value->format.sample_rate); key_number(&w,"sampleBits",value->format.sample_bits);
    key_number(&w,"channels",value->format.channels); qa_json_writer_end(&w); qa_json_writer_end(&w);
    return write_json(owner->input,"audio.json",&w,e);
}
bool frontend_shared_storage_save_view(frontend_shared_storage *owner,
    const frontend_shared_view_preferences *value,qa_error *e)
{
    if (!owner || !owner->graphical || !store_valid(owner->input) || !value || !view_valid(*value))
        return fail(e,"View save requires its actual published explicit preference");
    if (!value->present) return true;
    qa_json_writer writer={0}; qa_json_writer_object(&writer);
    key_number(&writer,"version",1); key_number(&writer,"fieldOfView",value->field_of_view);
    qa_json_writer_end(&writer);
    return write_json(owner->input,"view.json",&writer,e);
}
bool frontend_shared_storage_visit(const frontend_shared_storage *owner,
    const qa_application_content_visitor *visitor,qa_error *e)
{
    return owner && visitor && visitor->view && visitor->view(visitor->context,owner->user.vfs,e) &&
        visitor->view(visitor->context,owner->devices.vfs,e) &&
        (!owner->input.vfs || visitor->view(visitor->context,owner->input.vfs,e));
}
typedef struct storage_state {
    uint64_t user,devices,input,user_mount,devices_mount,input_mount,image_pool,image;
    qa_cvar_archive archive;
    frontend_shared_audio_preferences audio;
    frontend_shared_view_preferences view;
    char *device,*menu;
    bool graphical;
} storage_state;
static bool state_valid(const storage_state *state)
{
    const frontend_shared_audio_preferences *value=&state->audio;
    if (!state->user || !state->devices || state->user==state->devices ||
        state->user==state->input || state->devices==state->input || !state->user_mount ||
        !state->devices_mount || (!!state->input!=!!state->input_mount) ||
        (!!state->image!=!!state->image_pool) || (state->archive.count && !state->archive.entries)) return false;
    if (!view_valid(state->view) || (!state->input && (state->audio.present || state->view.present)) || (!state->graphical &&
        (state->image || state->audio.present || state->view.present))) return false;
    for (size_t i=0;i<state->archive.count;++i)
        if (!state->archive.entries[i].name || !state->archive.entries[i].value) return false;
    if (!value->present) return !state->device && !state->menu && !value->has_shuffle && !value->shuffle &&
        !value->has_menu_track && !value->format.sample_rate && !value->format.sample_bits && !value->format.channels &&
        value->effects==0 && value->music==0;
    return format_valid(value->format) && isfinite(value->effects) && value->effects>=0 && value->effects<=1 &&
        isfinite(value->music) && value->music>=0 && value->music<=1 && (!state->device || *state->device) &&
        (value->has_shuffle || !value->shuffle) && value->has_menu_track==(state->menu!=NULL) &&
        (!state->menu || frontend_shared_menu_track_valid(state->menu));
}
static bool fields(qa_source_save_io *io,storage_state *state)
{
    uint8_t magic[4]={'Q','F','S','H'}; uint32_t version=4;
    uint32_t rate=state->audio.format.sample_rate,bits=state->audio.format.sample_bits,channels=state->audio.format.channels;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFSH",4) || !qa_source_save_u32(io,&version) || version!=4 ||
        !qa_source_save_u64(io,&state->user) || !qa_source_save_u64(io,&state->devices) || !qa_source_save_u64(io,&state->input) ||
        !qa_source_save_u64(io,&state->user_mount) || !qa_source_save_u64(io,&state->devices_mount) ||
        !qa_source_save_u64(io,&state->input_mount) || !qa_source_save_bool(io,&state->graphical) ||
        !qa_source_save_u64(io,&state->image_pool) || !qa_source_save_u64(io,&state->image) ||
        !qa_source_save_bool(io,&state->audio.present) || !qa_source_save_u32(io,&rate) ||
        !qa_source_save_u32(io,&bits) || !qa_source_save_u32(io,&channels) ||
        !qa_source_save_f64(io,&state->audio.effects) || !qa_source_save_f64(io,&state->audio.music) ||
        !qa_source_save_bool(io,&state->audio.has_shuffle) || !qa_source_save_bool(io,&state->audio.shuffle) ||
        !qa_source_save_bool(io,&state->audio.has_menu_track) || !frontend_save_text(io,&state->device) ||
        !frontend_save_text(io,&state->menu) || !qa_source_save_bool(io,&state->view.present) ||
        !qa_source_save_f64(io,&state->view.field_of_view)) return false;
    state->audio.format=(qa_audio_output_format){rate,channels,bits};
    size_t count=state->archive.count;
    size_t limit=io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/2:SIZE_MAX;
    if (!qa_source_save_count(io,&count,limit) || count>SIZE_MAX/sizeof(*state->archive.entries)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        state->archive.entries=count?calloc(count,sizeof(*state->archive.entries)):NULL;
        if (count && !state->archive.entries) return frontend_fail(io->error,QA_ERROR_MEMORY,"Importing shared settings archive");
        state->archive.count=count;
    }
    for (size_t i=0;i<count;++i)
        if (!frontend_save_text(io,&state->archive.entries[i].name) ||
            !frontend_save_text(io,&state->archive.entries[i].value)) return false;
    return state_valid(state);
}
bool frontend_shared_storage_checkpoint(const frontend_shared_storage *owner,
    const qa_application_content_graph *graph,qa_buffer *out,qa_error *e)
{
    if (!owner || !graph || !out || out->data || out->size || !image_owned(owner,owner->images))
        return fail(e,"Shared storage capture needs its actual graph owners");
    storage_state state={.user=qa_application_content_view_id(graph,owner->user.vfs),
        .devices=qa_application_content_view_id(graph,owner->devices.vfs),
        .input=owner->input.vfs?qa_application_content_view_id(graph,owner->input.vfs):0,.user_mount=owner->user.mount,
        .devices_mount=owner->devices.mount,.input_mount=owner->input.mount,.graphical=owner->graphical,
        .archive=owner->archive,.audio=owner->audio,.view=owner->view,.device=owner->device,.menu=owner->menu};
    if (owner->images && !qa_application_content_resource_id(graph,owner->images,&state.image_pool,&state.image))
        return fail(e,"Shared image script leaves its genuine captured resource graph");
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_shared_storage_restore(qa_application_content_graph *graph,qa_bytes bytes,
    qa_settings_store user,qa_settings_store devices,qa_settings_store input,bool graphical,
    frontend_shared_storage **out,qa_error *e)
{
    if (!graph || !out || *out || !store_valid(user) || !store_valid(devices) || !input_valid(input))
        return fail(e,"Shared storage import needs actual mapped writable stores");
    storage_state state={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    qa_vfs *user_view=ok?qa_application_content_view(graph,state.user):NULL;
    qa_vfs *devices_view=ok?qa_application_content_view(graph,state.devices):NULL;
    qa_vfs *input_view=ok && state.input?qa_application_content_view(graph,state.input):NULL;
    const qa_resource *image=ok && state.image?qa_application_content_resource(graph,state.image_pool,state.image):NULL;
    frontend_shared_storage *owner=ok?calloc(1,sizeof(*owner)):NULL;
    if (ok && !owner) ok=frontend_fail(e,QA_ERROR_MEMORY,"Importing shared storage owner");
    if (ok) {
        owner->user=(qa_settings_store){user_view,state.user_mount};
        owner->devices=(qa_settings_store){devices_view,state.devices_mount};
        owner->input=(qa_settings_store){input_view,state.input_mount}; owner->graphical=state.graphical;
        ok=frontend_shared_storage_current(owner,user,devices,input,graphical) && (!state.image || image) && image_owned(owner,image);
        owner->user.vfs=owner->devices.vfs=owner->input.vfs=NULL;
    }
    if (ok && image) {
        qa_bytes text=qa_resource_bytes(image);
        ok=!text.size || !memchr(text.data,0,text.size);
    }
    if (ok) ok=qa_application_content_claim_view(graph,state.user,&owner->user.vfs,e) &&
        qa_application_content_claim_view(graph,state.devices,&owner->devices.vfs,e) &&
        (!state.input || qa_application_content_claim_view(graph,state.input,&owner->input.vfs,e));
    if (ok) {
        owner->archive=state.archive; state.archive=(qa_cvar_archive){0}; owner->audio=state.audio; owner->view=state.view;
        owner->device=state.device; state.device=NULL; owner->menu=state.menu; state.menu=NULL;
        owner->audio.device=owner->device; owner->audio.menu_track=owner->menu;
        owner->images=(qa_resource *)image; qa_resource_retain(owner->images); *out=owner;
    } else { frontend_shared_storage_destroy(owner); fail(e,"Invalid actual shared storage continuation"); }
    qa_cvar_archive_free(&state.archive); free(state.device); free(state.menu); return ok;
}
void frontend_shared_storage_destroy(frontend_shared_storage *owner)
{
    if (!owner) return;
    qa_resource_release(owner->images); qa_cvar_archive_free(&owner->archive);
    free(owner->device); free(owner->menu); qa_vfs_destroy(owner->input.vfs);
    qa_vfs_destroy(owner->devices.vfs); qa_vfs_destroy(owner->user.vfs); free(owner);
}
