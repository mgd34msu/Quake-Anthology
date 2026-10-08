#include "install_locations.h"
#include "qa/filesystem.h"
#include "qa/platform_events.h"
#include "qa/json.h"
#include "qa/tokenizer.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

enum { LOCATION_LIMIT=256, SETTINGS_LIMIT=1024*1024 };
static const char settings_file[]="install-locations.json";
struct frontend_install_locations {
    char *content,*user,*settings;
    char **roots;
    size_t count;
};
static bool fail(qa_error *error,qa_status status,const char *message)
{ qa_error_set(error,status,0,"%s",message); return false; }
static void paths_free(char **paths,size_t count)
{ for (size_t i=0;i<count;++i) free(paths[i]); free(paths); }
void frontend_install_locations_destroy(frontend_install_locations *owner)
{
    if (!owner) return;
    paths_free(owner->roots,owner->count); free(owner->content); free(owner->user); free(owner->settings); free(owner);
}
const char *qa_frontend_options_locations_file(const qa_frontend_options *options)
{ return options && options->install_locations?options->install_locations->settings:NULL; }
static bool path_add(char ***paths,size_t *count,char *owned,qa_error *error)
{
    for (size_t i=0;i<*count;++i) if (!strcmp((*paths)[i],owned)) { free(owned); return true; }
    if (*count==LOCATION_LIMIT) { free(owned); return fail(error,QA_ERROR_ARGUMENT,"Too many game search locations"); }
    char **next=realloc(*paths,(*count+1)*sizeof(*next));
    if (!next) { free(owned); return fail(error,QA_ERROR_MEMORY,"Retaining game search locations"); }
    next[(*count)++]=owned; *paths=next; return true;
}
static bool path_copy(char ***paths,size_t *count,const char *path,qa_error *error)
{
    char *owned=malloc(strlen(path)+1);
    if (!owned) return fail(error,QA_ERROR_MEMORY,"Retaining a game search location");
    strcpy(owned,path); return path_add(paths,count,owned,error);
}
static bool canonical(const char *path,char **out,qa_error *error)
{
    qa_fs_root *root=NULL;
    bool ok=qa_fs_root_open(path,&root,error) && qa_fs_root_join(root,"",out,error);
    qa_fs_root_close(root); return ok;
}
static bool absolute(const char *path)
{
    if (path[0]=='/') return true;
#if defined(_WIN32)
    return (path[0]=='\\' && path[1]=='\\') ||
        (path[0] && path[1]==':' && (path[2]=='/' || path[2]=='\\'));
#else
    return false;
#endif
}
static char *parent(const char *path)
{
    size_t size=strlen(path);
    while (size && (path[size-1]=='/' || path[size-1]=='\\')) --size;
    while (size && path[size-1]!='/' && path[size-1]!='\\') --size;
    while (size>1 && (path[size-1]=='/' || path[size-1]=='\\')) --size;
    if (!size) return NULL;
    if (size==2 && path[1]==':') ++size;
    char *result=malloc(size+1);
    if (result) { memcpy(result,path,size); result[size]=0; }
    return result;
}
static bool read_settings(qa_fs_root *root,char ***paths,size_t *count,qa_error *error)
{
    qa_fs_entry_kind kind; qa_fs_identity identity;
    if (!qa_fs_root_status(root,settings_file,&kind,&identity,error)) return false;
    if (kind==QA_FS_MISSING) return true;
    if (kind!=QA_FS_REGULAR || qa_fs_identity_size(&identity)>SETTINGS_LIMIT)
        return fail(error,QA_ERROR_FORMAT,"install-locations.json must be a regular JSON file smaller than 1 MiB");
    qa_fs_file *file=NULL; qa_buffer bytes={0}; qa_json_document *doc=NULL;
    bool ok=qa_fs_root_file_open(root,settings_file,&file,&identity,error) &&
        qa_fs_file_read_snapshot(file,&identity,&bytes,error) &&
        qa_json_parse((qa_bytes){bytes.data,bytes.size},&doc,error);
    qa_fs_file_close(file);
    if (ok) {
        qa_json_id object=qa_json_root(doc),list=qa_json_get(doc,object,"paths"); uint64_t version=0;
        ok=qa_json_type(doc,object)==QA_JSON_OBJECT &&
            qa_json_string_equal(doc,qa_json_get(doc,object,"schema"),"quake-anthology/install-locations") &&
            qa_json_u64(doc,qa_json_get(doc,object,"version"),&version,error) && version==1 &&
            qa_json_type(doc,list)==QA_JSON_ARRAY && qa_json_size(doc,list)<=LOCATION_LIMIT;
        for (size_t i=0;ok && i<qa_json_size(doc,list);++i) {
            qa_buffer path={0};
            ok=qa_json_string(doc,qa_json_at(doc,list,i),&path,error);
            if (ok) ok=path.size && !memchr(path.data,0,path.size) && absolute((const char *)path.data);
            if (ok) { ok=path_add(paths,count,(char *)path.data,error); path.data=NULL; }
            qa_buffer_free(&path);
        }
        if (!ok && (!error || error->code==QA_OK))
            fail(error,QA_ERROR_FORMAT,"Invalid install-locations.json: expected schema/version 1 and absolute directory paths");
    }
    qa_json_destroy(doc); qa_buffer_free(&bytes); return ok;
}
static bool append(qa_buffer *out,qa_bytes bytes,qa_error *error)
{
    if (bytes.size>SIZE_MAX-out->size) return fail(error,QA_ERROR_MEMORY,"Game locations JSON exceeds address space");
    uint8_t *next=realloc(out->data,out->size+bytes.size);
    if (!next && bytes.size) return fail(error,QA_ERROR_MEMORY,"Encoding game locations JSON");
    out->data=next; if (bytes.size) memcpy(out->data+out->size,bytes.data,bytes.size);
    out->size+=bytes.size; return true;
}
static bool append_text(qa_buffer *out,const char *text,qa_error *error)
{ return append(out,(qa_bytes){(const uint8_t *)text,strlen(text)},error); }
static bool save_settings(qa_fs_root *root,char *const *paths,size_t count,qa_error *error)
{
    qa_buffer bytes={0};
    bool ok=append_text(&bytes,"{\n  \"schema\": \"quake-anthology/install-locations\",\n  \"version\": 1,\n  \"paths\": [",error);
    for (size_t i=0;ok && i<count;++i) {
        qa_buffer quoted={0};
        ok=qa_json_quote((qa_bytes){(const uint8_t *)paths[i],strlen(paths[i])},&quoted,error) &&
            append_text(&bytes,i?",\n    ":"\n    ",error) &&
            append(&bytes,(qa_bytes){quoted.data,quoted.size},error);
        qa_buffer_free(&quoted);
    }
    if (ok) ok=append_text(&bytes,"\n  ]\n}\n",error) &&
        qa_fs_root_replace(root,settings_file,(qa_bytes){bytes.data,bytes.size},qa_platform_time_ns(),error);
    qa_buffer_free(&bytes); return ok;
}
static bool optional_directory(frontend_install_locations *owner,const char *path,qa_error *error)
{
    qa_fs_entry_kind kind; qa_error absent={0}; char *actual=NULL;
    if (!qa_fs_path_status(path,true,&kind,NULL,&absent) || kind!=QA_FS_DIRECTORY) return true;
    if (!canonical(path,&actual,&absent)) {
        if (absent.code==QA_ERROR_MEMORY) { if (error) *error=absent; return false; }
        return true;
    }
    return path_add(&owner->roots,&owner->count,actual,error);
}
static bool joined(const char *base,const char *suffix,char **out,qa_error *error)
{
    size_t a=strlen(base),b=strlen(suffix);
    if (a>SIZE_MAX-b-2) return fail(error,QA_ERROR_MEMORY,"Game directory path exceeds address space");
    *out=malloc(a+b+2);
    if (!*out) return fail(error,QA_ERROR_MEMORY,"Retaining an installation path");
    memcpy(*out,base,a); (*out)[a]='/'; memcpy(*out+a+1,suffix,b+1); return true;
}
static bool steam_library(frontend_install_locations *owner,const char *library,qa_error *error)
{
    char *common=NULL;
    bool ok=joined(library,"steamapps/common",&common,error) && optional_directory(owner,common,error);
    free(common); return ok;
}
static bool steam(frontend_install_locations *owner,const char *base,qa_error *error)
{
    if (!steam_library(owner,base,error)) return false;
    char *vdf=NULL;
    if (!joined(base,"steamapps/libraryfolders.vdf",&vdf,error)) return false;
    qa_fs_file *file=NULL; qa_fs_identity identity; qa_buffer bytes={0}; qa_error absent={0};
    bool present=qa_fs_file_open(vdf,&file,&identity,&absent);
    free(vdf);
    if (!present) return true;
    bool readable=qa_fs_identity_size(&identity)<=SETTINGS_LIMIT &&
        qa_fs_file_read_snapshot(file,&identity,&bytes,&absent);
    qa_fs_file_close(file);
    if (!readable) { qa_buffer_free(&bytes); return true; }
    qa_tokenizer parser; qa_token token; bool found=false,path_value=false; unsigned depth=0;
    qa_tokenizer_options options={.punctuation="{}",.maximum_units=0,.reject_quoted_newlines=true};
    bool ok=qa_tokenizer_init_options(&parser,(qa_bytes){bytes.data,bytes.size},&options,error);
    while (ok && qa_tokenizer_next(&parser,&token,&found,&absent) && found) {
        if (!token.quoted && token.text.size==1 && token.text.data[0]=='{') { ++depth; path_value=false; continue; }
        if (!token.quoted && token.text.size==1 && token.text.data[0]=='}') { if (depth) --depth; path_value=false; continue; }
        if (path_value) {
            char *path=malloc(token.text.size+1);
            if (!path) { ok=fail(error,QA_ERROR_MEMORY,"Reading Steam library locations"); break; }
            size_t at=0;
            for (size_t i=0;i<token.text.size;++i) {
                if (token.text.data[i]=='\\' && i+1<token.text.size && token.text.data[i+1]=='\\') ++i;
                path[at++]=(char)token.text.data[i];
            }
            path[at]=0;
            if (absolute(path)) ok=steam_library(owner,path,error);
            free(path); path_value=false; continue;
        }
        path_value=depth==2 && token.text.size==4 && !memcmp(token.text.data,"path",4);
        if (depth==1 && token.text.size) {
            bool number=true;
            for (size_t i=0;i<token.text.size;++i) if (token.text.data[i]<'0' || token.text.data[i]>'9') number=false;
            path_value=number;
        }
    }
    qa_buffer_free(&bytes); return ok;
}
static bool steam_default(frontend_install_locations *owner,const char *base,const char *suffix,qa_error *error)
{
    if (!base || !*base) return true;
    char *path=NULL; bool ok=joined(base,suffix,&path,error) && steam(owner,path,error);
    free(path); return ok;
}
bool qa_frontend_options_resolve_locations(qa_frontend_options *options,qa_error *error)
{
    if (!options || !options->native_bootstrap || !*options->native_bootstrap)
        return fail(error,QA_ERROR_ARGUMENT,"Game discovery requires the actual executable path");
    if (options->install_locations) return true;
    frontend_install_locations *owner=calloc(1,sizeof(*owner));
    qa_fs_root *user=NULL; char **saved=NULL; size_t saved_count=0;
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Retaining game discovery options");
    char *folder=parent(options->native_bootstrap);
    bool ok=folder && canonical(folder,&owner->content,error);
    free(folder);
    if (!ok && (!error || error->code==QA_OK)) fail(error,QA_ERROR_ARGUMENT,"Executable path has no containing directory");
    char *default_user=NULL;
    const char *user_path=options->application.user_root;
    if (ok && !user_path) {
        default_user=SDL_GetPrefPath("quake-anthology","content"); user_path=default_user;
        if (!user_path) ok=fail(error,QA_ERROR_IO,"Cannot find the platform user settings directory");
    }
    if (ok) ok=qa_fs_path_create_directory(user_path,error) && qa_fs_root_open(user_path,&user,error) &&
        qa_fs_root_join(user,"",&owner->user,error) && qa_fs_root_join(user,settings_file,&owner->settings,error) &&
        read_settings(user,&saved,&saved_count,error);
    SDL_free(default_user);
    for (size_t i=0;ok && i<options->save_game_path_count;++i) {
        char *path=NULL;
        ok=canonical(options->save_game_paths[i],&path,error) && path_add(&saved,&saved_count,path,error);
    }
    for (size_t i=0;ok && i<saved_count;++i) ok=path_copy(&owner->roots,&owner->count,saved[i],error);
    for (size_t i=0;ok && i<options->game_path_count;++i) {
        char *path=NULL;
        ok=canonical(options->game_paths[i],&path,error) && path_add(&owner->roots,&owner->count,path,error);
    }
    if (ok) ok=path_copy(&owner->roots,&owner->count,owner->content,error);
    char *ancestor=ok?parent(owner->content):NULL;
    for (unsigned i=0;ok && ancestor && i<3;++i) {
        size_t length=strlen(ancestor);
        if (length==1 || (length==3 && ancestor[1]==':')) break;
        ok=path_copy(&owner->roots,&owner->count,ancestor,error);
        char *next=parent(ancestor); free(ancestor); ancestor=next;
    }
    free(ancestor);
#if defined(_WIN32)
    if (ok) ok=steam_default(owner,getenv("ProgramFiles(x86)"),"Steam",error) &&
        steam_default(owner,getenv("ProgramFiles"),"Steam",error);
#elif defined(__APPLE__)
    if (ok) ok=steam_default(owner,getenv("HOME"),"Library/Application Support/Steam",error);
#else
    if (ok) ok=steam_default(owner,getenv("HOME"),".local/share/Steam",error) &&
        steam_default(owner,getenv("HOME"),".steam/steam",error) &&
        steam_default(owner,getenv("HOME"),".steam/root",error);
#endif
    if (ok && options->save_game_path_count) ok=save_settings(user,saved,saved_count,error);
    paths_free(saved,saved_count); qa_fs_root_close(user);
    if (!ok) { frontend_install_locations_destroy(owner); return false; }
    options->install_locations=owner;
    if (!options->application.content_root) options->application.content_root=owner->content;
    options->application.user_root=owner->user;
    options->application.install_roots=(const char *const *)owner->roots;
    options->application.install_root_count=owner->count;
    return true;
}
