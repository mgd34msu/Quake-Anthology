#include "shared_storage.h"
#include "shared_register.h"
#include "save_private.h"
#include "qa/json.h"
#include "qa/json_writer.h"
#include "qa/text.h"
#include <math.h>
#include <limits.h>

typedef struct storage_player {
    uint32_t seat;
    qa_cvar_archive archive;
} storage_player;
static const char canonical_path[]="cvars/shared/canonical.json";
struct frontend_shared_storage {
    qa_settings_store user,devices,input;
    qa_resource *images;
    qa_cvar_archive archive,canonical;
    storage_player *players;
    size_t player_count;
    bool canonical_present;
    frontend_shared_audio_preferences audio;
    frontend_shared_view_preferences view;
    char *device,*menu;
    bool graphical;
};
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
    return owner && owner->graphical==graphical &&
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
static void players_free(storage_player *players,size_t count)
{
    for (size_t i=0;i<count;++i) qa_cvar_archive_free(&players[i].archive);
    free(players);
}
static bool archive_put(qa_cvar_archive *archive,const char *name,const char *value,qa_error *e)
{
    size_t at=0;
    while (at<archive->count && strcmp(archive->entries[at].name,name)) ++at;
    char *text=malloc(strlen(value)+1);
    if (!text) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining canonical setting value");
    strcpy(text,value);
    if (at<archive->count) { free(archive->entries[at].value); archive->entries[at].value=text; return true; }
    if (archive->count==SIZE_MAX/sizeof(*archive->entries)) { free(text); return fail(e,"Canonical archive exceeds storage"); }
    char *key=malloc(strlen(name)+1);
    qa_cvar_archive_entry *rows=key?realloc(archive->entries,(archive->count+1)*sizeof(*rows)):NULL;
    if (!rows) { free(key); free(text); return frontend_fail(e,QA_ERROR_MEMORY,"Retaining canonical setting name"); }
    strcpy(key,name); archive->entries=rows; rows[archive->count++]=(qa_cvar_archive_entry){key,text}; return true;
}
static bool archive_merge(qa_cvar_archive *out,const qa_cvar_archive *source,qa_error *e)
{
    for (size_t i=0;source && i<source->count;++i)
        if (!archive_put(out,source->entries[i].name,source->entries[i].value,e)) return false;
    return true;
}
static bool archive_read(const qa_json_document *d,qa_json_id entries,qa_cvar_archive *out,qa_error *e)
{
    if (qa_json_type(d,entries)!=QA_JSON_ARRAY) return fail(e,"Canonical settings require archive entries");
    size_t count=qa_json_size(d,entries);
    if (count>SIZE_MAX/sizeof(*out->entries)) return fail(e,"Canonical settings archive exceeds storage");
    out->entries=count?calloc(count,sizeof(*out->entries)):NULL;
    if (count && !out->entries) return frontend_fail(e,QA_ERROR_MEMORY,"Reading canonical setting entries");
    for (size_t i=0;i<count;++i) {
        qa_json_id row=qa_json_at(d,entries,i); qa_cvar_archive_entry *entry=out->entries+i;
        ++out->count;
        if (qa_json_type(d,row)!=QA_JSON_OBJECT || !string(d,qa_json_get(d,row,"name"),&entry->name,e) ||
            !*entry->name || !string(d,qa_json_get(d,row,"value"),&entry->value,e)) return false;
        for (size_t previous=0;previous<i;++previous)
            if (!strcmp(out->entries[previous].name,entry->name)) return fail(e,"Duplicate canonical setting name");
    }
    return true;
}
static bool canonical_load(frontend_shared_storage *owner,qa_error *e)
{
    qa_resource *file=NULL; bool found=false;
    if (!qa_settings_read(owner->user,canonical_path,&file,&found,e)) return false;
    if (!found) return true;
    qa_json_document *d=NULL; bool ok=qa_json_parse(qa_resource_bytes(file),&d,e);
    qa_json_id root=ok?qa_json_root(d):QA_JSON_NONE; double version=0;
    qa_json_id players=ok?qa_json_get(d,root,"players"):QA_JSON_NONE;
    if (ok) ok=qa_json_type(d,root)==QA_JSON_OBJECT && number(d,root,"version",&version,e) && version==1 &&
        qa_json_string_equal(d,qa_json_get(d,root,"dialect"),"q3") &&
        archive_read(d,qa_json_get(d,root,"entries"),&owner->canonical,e) && qa_json_type(d,players)==QA_JSON_ARRAY;
    size_t count=ok?qa_json_size(d,players):0;
    if (count>SIZE_MAX/sizeof(*owner->players)) ok=fail(e,"Canonical player archive exceeds storage");
    if (ok && count) {
        owner->players=calloc(count,sizeof(*owner->players));
        if (!owner->players) ok=frontend_fail(e,QA_ERROR_MEMORY,"Reading canonical player settings");
    }
    for (size_t i=0;ok && i<count;++i) {
        qa_json_id row=qa_json_at(d,players,i); uint64_t seat=0;
        ++owner->player_count;
        ok=qa_json_type(d,row)==QA_JSON_OBJECT && qa_json_u64(d,qa_json_get(d,row,"seat"),&seat,e) &&
            seat<=UINT32_MAX && archive_read(d,qa_json_get(d,row,"entries"),&owner->players[i].archive,e);
        owner->players[i].seat=(uint32_t)seat;
        for (size_t previous=0;ok && previous<i;++previous)
            if (owner->players[previous].seat==owner->players[i].seat) ok=fail(e,"Duplicate canonical player seat");
    }
    qa_json_destroy(d); qa_resource_release(file);
    if (!ok) return fail(e,"Invalid canonical settings archive");
    owner->canonical_present=true; return true;
}
bool frontend_shared_storage_has_archive(const frontend_shared_storage *owner)
{ return owner && owner->canonical_present; }
const qa_cvar_archive *frontend_shared_storage_canonical_archive(const frontend_shared_storage *owner)
{ return owner && owner->canonical_present?&owner->canonical:NULL; }
const qa_cvar_archive *frontend_shared_storage_player_archive(const frontend_shared_storage *owner,uint32_t seat)
{
    for (size_t i=0;owner && i<owner->player_count;++i)
        if (owner->players[i].seat==seat) return &owner->players[i].archive;
    return NULL;
}
static qa_cvars *player_view(qa_cvars *shared,uint32_t seat,qa_error *e)
{
    qa_cvar_options options={.dialect=QA_RULESET_Q3,.side=QA_CVAR_SIDE_CLIENT,
        .role=QA_CVAR_ROLE_ENGINE,.seat=seat,.default_save_policy=QA_CVAR_SAVE_SETTING};
    return qa_cvars_create_view(shared,&options,e);
}
static bool archive_capture(const qa_cvars *registry,bool player,qa_cvar_archive *archive,qa_error *e)
{
    for (const qa_cvar_view *row=qa_cvars_next(registry,NULL);row;row=qa_cvars_next(registry,row)) {
        const char *value=row->player_scoped==player?qa_cvars_archive_value(registry,row):NULL;
        if (value && !archive_put(archive,row->name,value,e)) return false;
    }
    return true;
}
bool frontend_shared_storage_seed_player(const frontend_shared_storage *owner,qa_cvars *shared,
    uint32_t seat,bool preserve_current,qa_cvar_archive *archive,qa_error *e)
{
    qa_cvars *view=player_view(shared,seat,e);
    if (!view) return false;
    const qa_cvar_archive *saved=frontend_shared_storage_player_archive(owner,seat);
    bool ok=archive_merge(archive,saved,e);
    if (ok && preserve_current) ok=archive_capture(view,true,archive,e);
    qa_cvars_destroy(view); return ok;
}
bool frontend_shared_storage_apply_players(const frontend_shared_storage *owner,qa_cvars *shared,qa_error *e)
{
    for (size_t i=0;i<owner->player_count;++i) {
        const storage_player *row=owner->players+i;
        qa_cvars *view=player_view(shared,row->seat,e);
        if (!view) return false;
        bool ok=qa_cvar_archive_apply(view,&row->archive,e);
        qa_cvars_destroy(view);
        if (!ok) return false;
    }
    return true;
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
        if (!qa_format_number(values[i],text,e) || !archive_add(owner,fields[i],text,e)) return false;
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
    static const char *const input_owner[]={"input","devices"};
    return qa_settings_load_cvars(owner->devices,input_owner,2,QA_RULESET_Q3,out,e);
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
        canonical_load(owner,e) &&
        (owner->canonical_present || !graphical || qa_settings_read(owner->user,"settings/images.cfg",&owner->images,&found,e)) &&
        (owner->canonical_present || qa_settings_load_cvars(owner->devices,input_owner,2,QA_RULESET_Q3,&owner->archive,e)) &&
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
    if (!store_valid(input))
        return fail(e,"Sticky preference adoption requires the real global and selected product stores");
    if (owner->input.vfs) return same_store(owner->input,input) ||
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
static void archive_write(qa_json_writer *w,const qa_cvar_archive *archive)
{
    qa_json_writer_key(w,"entries"); qa_json_writer_array(w);
    for (size_t i=0;i<archive->count;++i) {
        qa_json_writer_object(w); key_string(w,"name",archive->entries[i].name);
        key_string(w,"value",archive->entries[i].value); qa_json_writer_end(w);
    }
    qa_json_writer_end(w);
}
static bool selected_seat(const uint32_t *seats,size_t count,uint32_t seat)
{
    for (size_t i=0;i<count;++i) if (seats[i]==seat) return true;
    return false;
}
bool frontend_shared_storage_save_archive(frontend_shared_storage *owner,qa_cvars *shared,qa_error *e)
{
    size_t actual=qa_cvars_player_count(shared);
    if (owner->player_count>SIZE_MAX/sizeof(storage_player) || actual>SIZE_MAX/sizeof(storage_player)-owner->player_count)
        return frontend_fail(e,QA_ERROR_MEMORY,"Canonical player archives exceed native storage");
    size_t selected_count=0,selected_capacity=actual;
    uint32_t *selected=selected_capacity?malloc(selected_capacity*sizeof(*selected)):NULL;
    if (selected_capacity && !selected) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining canonical player identities");
    for (size_t i=0;i<actual;++i) {
        uint32_t seat=0;
        (void)qa_cvars_player_at(shared,i,&seat);
        if (!selected_seat(selected,selected_count,seat)) selected[selected_count++]=seat;
    }
    qa_cvar_archive global={0}; size_t capacity=owner->player_count+selected_count;
    storage_player *players=capacity?calloc(capacity,sizeof(*players)):NULL; size_t used=0;
    bool ok=(!capacity || players) && archive_capture(shared,false,&global,e);
    if (!players && capacity) frontend_fail(e,QA_ERROR_MEMORY,"Capturing canonical player settings");
    for (size_t i=0;ok && i<owner->player_count;++i) {
        if (selected_seat(selected,selected_count,owner->players[i].seat)) continue;
        storage_player *row=players+used++; row->seat=owner->players[i].seat;
        ok=archive_merge(&row->archive,&owner->players[i].archive,e);
    }
    for (size_t i=0;ok && i<selected_count;++i) {
        storage_player *row=players+used++; row->seat=selected[i];
        qa_cvars *view=player_view(shared,row->seat,e);
        ok=view && archive_capture(view,true,&row->archive,e); qa_cvars_destroy(view);
    }
    if (ok) {
        qa_json_writer writer={0}; qa_json_writer_object(&writer); key_number(&writer,"version",1);
        key_string(&writer,"dialect","q3"); archive_write(&writer,&global);
        qa_json_writer_key(&writer,"players"); qa_json_writer_array(&writer);
        for (size_t i=0;i<used;++i) {
            qa_json_writer_object(&writer); qa_json_writer_key(&writer,"seat");
            qa_json_writer_u64(&writer,players[i].seat); archive_write(&writer,&players[i].archive); qa_json_writer_end(&writer);
        }
        qa_json_writer_end(&writer); qa_json_writer_end(&writer);
        ok=write_json(owner->user,canonical_path,&writer,e);
    }
    if (ok) {
        qa_cvar_archive_free(&owner->canonical); players_free(owner->players,owner->player_count);
        owner->canonical=global; global=(qa_cvar_archive){0}; owner->players=players; players=NULL;
        owner->player_count=used; owner->canonical_present=true;
    }
    free(selected); qa_cvar_archive_free(&global); players_free(players,players?used:0); return ok;
}

bool frontend_shared_storage_save_audio(frontend_shared_storage *owner,
    const frontend_shared_audio_preferences *value,qa_error *e)
{
    if (!format_valid(value->format) || !isfinite(value->effects) ||
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
    if (!view_valid(*value))
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
    qa_cvar_archive archive,canonical;
    storage_player *players;
    size_t player_count;
    bool canonical_present;
    frontend_shared_audio_preferences audio;
    frontend_shared_view_preferences view;
    char *device,*menu;
    bool graphical;
} storage_state;
static bool archive_fields(qa_source_save_io *io,qa_cvar_archive *archive)
{
    size_t count=archive->count;
    size_t limit=io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/2:SIZE_MAX;
    if (!qa_source_save_count(io,&count,limit) || count>SIZE_MAX/sizeof(*archive->entries)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        archive->entries=count?calloc(count,sizeof(*archive->entries)):NULL;
        if (count && !archive->entries) return frontend_fail(io->error,QA_ERROR_MEMORY,"Importing shared settings archive");
        archive->count=count;
    }
    for (size_t i=0;i<count;++i)
        if (!qa_source_save_owned_text(io,&archive->entries[i].name) ||
            !qa_source_save_owned_text(io,&archive->entries[i].value)) return false;
    return true;
}
static bool archive_valid(const qa_cvar_archive *archive,bool unique)
{
    if (archive->count && !archive->entries) return false;
    for (size_t i=0;i<archive->count;++i) {
        if (!archive->entries[i].name || !*archive->entries[i].name || !archive->entries[i].value) return false;
        for (size_t previous=0;unique && previous<i;++previous)
            if (!strcmp(archive->entries[previous].name,archive->entries[i].name)) return false;
    }
    return true;
}
static bool state_valid(const storage_state *state)
{
    const frontend_shared_audio_preferences *value=&state->audio;
    if (!state->user || !state->devices || state->user==state->devices ||
        state->user==state->input || state->devices==state->input || !state->user_mount ||
        !state->devices_mount || (!!state->input!=!!state->input_mount) ||
        (!!state->image!=!!state->image_pool) || (state->archive.count && !state->archive.entries)) return false;
    if (!view_valid(state->view) || (!state->input && (state->audio.present || state->view.present)) || (!state->graphical &&
        (state->image || state->audio.present || state->view.present))) return false;
    if (!archive_valid(&state->archive,false) || !archive_valid(&state->canonical,true) ||
        (state->player_count && !state->players) ||
        (!state->canonical_present && (state->canonical.count || state->player_count))) return false;
    for (size_t i=0;i<state->player_count;++i) {
        if (!archive_valid(&state->players[i].archive,true)) return false;
        for (size_t previous=0;previous<i;++previous)
            if (state->players[previous].seat==state->players[i].seat) return false;
    }
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
    uint8_t magic[4]={'Q','F','S','H'}; uint32_t rate=state->audio.format.sample_rate,bits=state->audio.format.sample_bits,channels=state->audio.format.channels;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFSH",4) || !qa_source_save_u64(io,&state->user) || !qa_source_save_u64(io,&state->devices) || !qa_source_save_u64(io,&state->input) ||
        !qa_source_save_u64(io,&state->user_mount) || !qa_source_save_u64(io,&state->devices_mount) ||
        !qa_source_save_u64(io,&state->input_mount) || !qa_source_save_bool(io,&state->graphical) ||
        !qa_source_save_u64(io,&state->image_pool) || !qa_source_save_u64(io,&state->image) ||
        !qa_source_save_bool(io,&state->audio.present) || !qa_source_save_u32(io,&rate) ||
        !qa_source_save_u32(io,&bits) || !qa_source_save_u32(io,&channels) ||
        !qa_source_save_f64(io,&state->audio.effects) || !qa_source_save_f64(io,&state->audio.music) ||
        !qa_source_save_bool(io,&state->audio.has_shuffle) || !qa_source_save_bool(io,&state->audio.shuffle) ||
        !qa_source_save_bool(io,&state->audio.has_menu_track) || !qa_source_save_owned_text(io,&state->device) ||
        !qa_source_save_owned_text(io,&state->menu) || !qa_source_save_bool(io,&state->view.present) ||
        !qa_source_save_f64(io,&state->view.field_of_view)) return false;
    state->audio.format=(qa_audio_output_format){rate,channels,bits};
    if (!archive_fields(io,&state->archive) || !qa_source_save_bool(io,&state->canonical_present) ||
        !archive_fields(io,&state->canonical)) return false;
    size_t count=state->player_count;
    size_t limit=io->direction==QA_SOURCE_SAVE_READ?(io->input.size-io->offset)/5:SIZE_MAX;
    if (!qa_source_save_count(io,&count,limit) || count>SIZE_MAX/sizeof(*state->players)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        state->players=count?calloc(count,sizeof(*state->players)):NULL;
        if (count && !state->players) return frontend_fail(io->error,QA_ERROR_MEMORY,"Importing canonical player settings");
        state->player_count=count;
    }
    for (size_t i=0;i<count;++i)
        if (!qa_source_save_u32(io,&state->players[i].seat) || !archive_fields(io,&state->players[i].archive)) return false;
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
        .archive=owner->archive,.canonical=owner->canonical,.players=owner->players,.player_count=owner->player_count,
        .canonical_present=owner->canonical_present,.audio=owner->audio,.view=owner->view,.device=owner->device,.menu=owner->menu};
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
        owner->archive=state.archive; state.archive=(qa_cvar_archive){0};
        owner->canonical=state.canonical; state.canonical=(qa_cvar_archive){0};
        owner->players=state.players; state.players=NULL; owner->player_count=state.player_count;
        owner->canonical_present=state.canonical_present; owner->audio=state.audio; owner->view=state.view;
        owner->device=state.device; state.device=NULL; owner->menu=state.menu; state.menu=NULL;
        owner->audio.device=owner->device; owner->audio.menu_track=owner->menu;
        owner->images=(qa_resource *)image; qa_resource_retain(owner->images); *out=owner;
    } else { frontend_shared_storage_destroy(owner); fail(e,"Invalid actual shared storage continuation"); }
    qa_cvar_archive_free(&state.archive); qa_cvar_archive_free(&state.canonical);
    players_free(state.players,state.players?state.player_count:0); free(state.device); free(state.menu); return ok;
}
void frontend_shared_storage_destroy(frontend_shared_storage *owner)
{
    if (!owner) return;
    qa_resource_release(owner->images); qa_cvar_archive_free(&owner->archive);
    qa_cvar_archive_free(&owner->canonical); players_free(owner->players,owner->player_count);
    free(owner->device); free(owner->menu); qa_vfs_destroy(owner->input.vfs);
    qa_vfs_destroy(owner->devices.vfs); qa_vfs_destroy(owner->user.vfs); free(owner);
}
