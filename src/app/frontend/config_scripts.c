#include "config_scripts.h"
#include "qa/catalog_save.h"
#include "qa/vfs_view_save.h"
#include "qa/source_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct frontend_config_files {
    qa_catalog *catalog;
    qa_product_id product, base;
    qa_vfs *selected, *base_files, *console;
    qa_mount_id writable, base_writable, console_writable;
    qa_mount_id shared, loose, base_loose;
    qa_mount_id script_base_user, script_base_loose;
    qa_vfs *qw_base;
    qa_product_id qw_base_product;
    qa_mount_id qw_family;
    char *qw_name, *qw_home_prefix, *qw_write_child;
    bool qw_changed;
    uint64_t nonce;
    size_t reads;
    bool writing;
};
typedef struct script_lease { frontend_config_files *owner; qa_resource *resource; } script_lease;
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static bool local(const qa_command_context *source,qa_error *error)
{ return (source && source->origin!=QA_COMMAND_REMOTE) || fail(error,QA_ERROR_ARGUMENT,"Remote clients cannot use local configuration files"); }
static bool folded_equal(const char *a,const char *b);
static bool fs_equal(const char *a,const char *b,void *context)
{ (void)context; return folded_equal(a,b); }
static bool child_mount(frontend_config_files *owner,qa_fs_root *root,const char *directory,
    bool create,qa_mount_id *out,qa_error *error)
{
    char *resolved=NULL,*path=NULL; qa_error observed={0};
    bool ok=qa_fs_root_resolve(root,directory,fs_equal,NULL,false,&resolved,&observed);
    if (!ok && observed.code==QA_ERROR_NOT_FOUND) {
        if (!create) return true;
        ok=qa_fs_root_create_directory(root,directory,error);
        if (ok) ok=qa_fs_root_resolve(root,directory,fs_equal,NULL,false,&resolved,error);
    } else if (!ok && error) *error=observed;
    if (ok) ok=qa_fs_root_join(root,resolved,&path,error) &&
        qa_vfs_mount_directory(owner->console,path,QA_ARCHIVE_CASE_INSENSITIVE,create,out,error);
    free(resolved); free(path); return ok;
}
static bool product_user_mount(frontend_config_files *owner,qa_fs_root *user,
    const qa_product *product,qa_mount_id *out,qa_error *error)
{
    const qa_catalog_mount *mount=qa_catalog_product_write_mount(owner->catalog,product->id);
    qa_fs_root *root=qa_catalog_product_write_root(owner->catalog,product->id);
    if (!mount) return child_mount(owner,user,product->directory,true,out,error);
    if (!root || !qa_vfs_mount_retained(owner->console,qa_catalog_files(owner->catalog),mount->id,
        QA_ARCHIVE_CASE_INSENSITIVE,true,out,error)) return false;
    return qa_vfs_mount_root(owner->console,*out)==root ||
        fail(error,QA_ERROR_FORMAT,"Configuration mount lost its actual product writable capability");
}
static bool product_loose_mount(frontend_config_files *owner,const qa_product *product,
    qa_mount_id *out,qa_error *error)
{
    const qa_catalog_mount *mount=qa_catalog_product_loose_mount(owner->catalog,product->id);
    if (!mount) return true;
    qa_fs_root *root=qa_catalog_product_loose_root(owner->catalog,product->id);
    if (!root || !qa_vfs_mount_retained(owner->console,qa_catalog_files(owner->catalog),mount->id,
        QA_ARCHIVE_CASE_INSENSITIVE,false,out,error)) return false;
    return qa_vfs_mount_root(owner->console,*out)==root ||
        fail(error,QA_ERROR_FORMAT,"Configuration mount lost its actual product loose capability");
}
static bool writable_store(qa_settings_store store)
{
    if (!store.vfs || !store.mount || !qa_vfs_mount_root(store.vfs,store.mount)) return false;
    for (size_t i=0;i<qa_vfs_mount_count(store.vfs);++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(store.vfs,i,&mount)) return false;
        if (mount.id==store.mount) return mount.writable && !mount.is_archive &&
            mount.comparison==QA_ARCHIVE_CASE_INSENSITIVE;
    }
    return false;
}
static bool actual_roots(frontend_config_files *owner,qa_settings_store user_store,qa_settings_store devices,
    const qa_product *selected,const qa_product *base,qa_error *error)
{
    if (!qa_vfs_mount_retained(owner->console,user_store.vfs,user_store.mount,
        QA_ARCHIVE_CASE_INSENSITIVE,true,&owner->shared,error)) return false;
    qa_fs_root *user=qa_vfs_mount_root(owner->console,owner->shared);
    if (!product_user_mount(owner,user,selected,&owner->writable,error)) return false;
    if (selected==base) owner->base_writable=owner->writable;
    else if (!product_user_mount(owner,user,base,&owner->base_writable,error)) return false;
    qa_product_id immediate=qa_catalog_configuration_base(owner->catalog,selected->id);
    const qa_product *script_base=immediate?qa_catalog_product(owner->catalog,immediate):selected;
    if (!script_base) return fail(error,QA_ERROR_FORMAT,"Source scripts lack their actual immediate base");
    if (script_base==selected) owner->script_base_user=owner->writable;
    else if (script_base==base) owner->script_base_user=owner->base_writable;
    else if (!product_user_mount(owner,user,script_base,&owner->script_base_user,error)) return false;
    if (!qa_vfs_mount_retained(owner->console,devices.vfs,devices.mount,
        QA_ARCHIVE_CASE_INSENSITIVE,true,&owner->console_writable,error)) return false;
    bool ok=product_loose_mount(owner,selected,&owner->loose,error);
    if (selected==base) owner->base_loose=owner->loose;
    else if (ok) ok=product_loose_mount(owner,base,&owner->base_loose,error);
    if (script_base==selected) owner->script_base_loose=owner->loose;
    else if (script_base==base) owner->script_base_loose=owner->base_loose;
    else if (ok) ok=product_loose_mount(owner,script_base,&owner->script_base_loose,error);
    return ok;
}
static char *copy_text(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1; char *copy=malloc(size);
    if (!copy) fail(error,QA_ERROR_MEMORY,"Retaining Source directory state");
    else memcpy(copy,text,size);
    return copy;
}
static bool source_roots(frontend_config_files *owner,const qa_product *product,qa_error *error)
{
    if (product->edition!=QA_EDITION_QUAKEWORLD) return true;
    const qa_catalog_mount *family=qa_catalog_product_family_mount(owner->catalog,product->id);
    if (!family || !qa_vfs_mount_retained(owner->console,qa_catalog_files(owner->catalog),family->id,
        QA_ARCHIVE_CASE_INSENSITIVE,false,&owner->qw_family,error))
        return fail(error,QA_ERROR_FORMAT,"QuakeWorld Source lacks its actual installed family authority");
    const qa_product *base=product; size_t depth=0;
    while (base->base) {
        const qa_product *next=qa_catalog_product(owner->catalog,base->base);
        if (!next || ++depth>qa_catalog_count(owner->catalog))
            return fail(error,QA_ERROR_FORMAT,"QuakeWorld Source has invalid fixed-base ancestry");
        if (next->edition!=QA_EDITION_QUAKEWORLD) break;
        base=next;
    }
    owner->qw_base_product=base->id;
    if (!qa_catalog_open(owner->catalog,base->id,&owner->qw_base,error)) return false;
    const char *leaf=strrchr(product->directory,'/');
    if (!leaf || !leaf[1]) return fail(error,QA_ERROR_FORMAT,"QuakeWorld Source lacks its logical family directory");
    owner->qw_name=copy_text(leaf+1,error);
    size_t size=(size_t)(leaf-product->directory);
    owner->qw_home_prefix=malloc(size+1);
    if (!owner->qw_home_prefix) return fail(error,QA_ERROR_MEMORY,"Retaining Source writable family namespace");
    memcpy(owner->qw_home_prefix,product->directory,size); owner->qw_home_prefix[size]=0;
    return owner->qw_name!=NULL;
}
frontend_config_files *frontend_config_files_create(qa_catalog *catalog,qa_product_id product,
    qa_settings_store user,qa_settings_store devices,qa_error *error)
{
    const qa_product *selected=qa_catalog_product(catalog,product);
    if (!selected || !writable_store(user) || !writable_store(devices) ||
        qa_fs_root_same_object(qa_vfs_mount_root(user.vfs,user.mount),qa_vfs_mount_root(devices.vfs,devices.mount)))
        return fail(error,QA_ERROR_ARGUMENT,"Configuration requires its actual selected product and retained global stores"),NULL;
    const qa_product *base=selected; size_t depth=0;
    qa_product_id ancestor;
    while ((ancestor=qa_catalog_configuration_base(catalog,base->id))!=QA_PRODUCT_NONE) {
        base=qa_catalog_product(catalog,ancestor);
        if (!base || ++depth>qa_catalog_count(catalog)) return fail(error,QA_ERROR_FORMAT,"Configuration source has cyclic or absent base ancestry"),NULL;
    }
    frontend_config_files *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Allocating selected source configuration files"),NULL;
    owner->catalog=catalog; qa_catalog_retain(catalog); owner->product=product; owner->base=base->id;
    bool ok=qa_catalog_open(catalog,product,&owner->selected,error);
    if (ok && base->id==product) owner->base_files=owner->selected;
    else if (ok) ok=qa_catalog_open(catalog,base->id,&owner->base_files,error);
    if (ok) owner->console=qa_vfs_create(qa_catalog_resources(catalog),error);
    if (ok) ok=owner->console && actual_roots(owner,user,devices,selected,base,error);
    if (ok) ok=source_roots(owner,selected,error);
    if (!ok) { frontend_config_files_destroy(owner,NULL); return NULL; }
    return owner;
}
bool frontend_config_files_destroy(frontend_config_files *owner,qa_error *error)
{
    if (!owner) return true;
    if (owner->reads || owner->writing) return fail(error,QA_ERROR_ARGUMENT,"Configuration files retain an active read or write");
    if (owner->base_files!=owner->selected) qa_vfs_destroy(owner->base_files);
    qa_vfs_destroy(owner->selected); qa_vfs_destroy(owner->qw_base); qa_vfs_destroy(owner->console);
    free(owner->qw_name); free(owner->qw_home_prefix); free(owner->qw_write_child); qa_catalog_release(owner->catalog); free(owner); return true;
}
bool frontend_config_files_clone(const frontend_config_files *source,frontend_config_files **out,qa_error *error)
{
    if (!source || source->reads || source->writing || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration carry requires its returned actual file authorities");
    frontend_config_files *owner=malloc(sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Retaining carried source configuration roots");
    *owner=*source; owner->selected=owner->base_files=owner->console=owner->qw_base=NULL;
    owner->qw_name=owner->qw_home_prefix=owner->qw_write_child=NULL;
    qa_catalog_retain(owner->catalog);
    owner->selected=qa_vfs_clone(source->selected,error);
    bool ok=owner->selected!=NULL;
    if (ok && source->base_files==source->selected) owner->base_files=owner->selected;
    else if (ok) { owner->base_files=qa_vfs_clone(source->base_files,error); ok=owner->base_files!=NULL; }
    if (ok) { owner->console=qa_vfs_clone(source->console,error); ok=owner->console!=NULL; }
    if (ok && source->qw_base) {
        owner->qw_base=qa_vfs_clone(source->qw_base,error);
        owner->qw_name=copy_text(source->qw_name,error);
        owner->qw_home_prefix=copy_text(source->qw_home_prefix,error);
        if (source->qw_write_child) owner->qw_write_child=copy_text(source->qw_write_child,error);
        ok=owner->qw_base && owner->qw_name && owner->qw_home_prefix &&
            (!source->qw_write_child || owner->qw_write_child);
    }
    if (!ok) { frontend_config_files_destroy(owner,NULL); return false; }
    *out=owner; return true;
}
bool frontend_config_files_idle(const frontend_config_files *owner)
{ return owner && !owner->reads && !owner->writing; }
qa_product_id frontend_config_files_product(const frontend_config_files *owner) { return owner?owner->product:0; }
qa_catalog *frontend_config_files_catalog(const frontend_config_files *owner) { return owner?owner->catalog:NULL; }
qa_settings_store frontend_config_files_store(const frontend_config_files *owner,bool base)
{ return owner?(qa_settings_store){owner->console,base?owner->base_writable:owner->writable}:(qa_settings_store){0}; }
qa_settings_store frontend_config_files_shared_store(const frontend_config_files *owner)
{ return owner?(qa_settings_store){owner->console,owner->shared}:(qa_settings_store){0}; }
qa_settings_store frontend_config_files_device_store(const frontend_config_files *owner)
{ return owner?(qa_settings_store){owner->console,owner->console_writable}:(qa_settings_store){0}; }
bool frontend_config_files_global_current(const frontend_config_files *owner,qa_settings_store user,qa_settings_store devices)
{
    return owner && writable_store(user) && writable_store(devices) &&
        writable_store(frontend_config_files_shared_store(owner)) &&
        writable_store(frontend_config_files_device_store(owner)) &&
        qa_fs_root_same_object(qa_vfs_mount_root(owner->console,owner->shared),qa_vfs_mount_root(user.vfs,user.mount)) &&
        qa_fs_root_same_object(qa_vfs_mount_root(owner->console,owner->console_writable),qa_vfs_mount_root(devices.vfs,devices.mount));
}
qa_fs_root *frontend_config_files_shared_root(const frontend_config_files *owner)
{
    qa_settings_store store=frontend_config_files_shared_store(owner);
    return store.vfs?qa_vfs_mount_root(store.vfs,store.mount):NULL;
}
qa_fs_root *frontend_config_files_root(const frontend_config_files *owner,bool base)
{
    qa_settings_store store=frontend_config_files_store(owner,base);
    return store.vfs?qa_vfs_mount_root(store.vfs,store.mount):NULL;
}
qa_fs_root *frontend_config_files_loose_root(const frontend_config_files *owner,bool base)
{
    if (!owner) return NULL;
    return qa_vfs_mount_root(owner->console,base?owner->base_loose:owner->loose);
}
const char *frontend_config_files_game_directory(const frontend_config_files *owner)
{
    if (!owner || owner->product==owner->base) return "";
    const qa_product *product=qa_catalog_product(owner->catalog,owner->product);
    const char *last=product?strrchr(product->directory,'/'):NULL;
    return product?last?last+1:product->directory:"";
}
qa_vfs *frontend_config_files_source_content(const frontend_config_files *owner)
{ return owner && owner->qw_base ? owner->selected : NULL; }
const char *frontend_config_files_source_directory(const frontend_config_files *owner)
{ return owner && owner->qw_base ? qa_vfs_mount_path(owner->console,owner->writable) : NULL; }
static char *source_child_path(const char *parent,const char *child,qa_error *error)
{
    size_t a=strlen(parent),b=strlen(child);
    if (a>SIZE_MAX-b-2) return fail(error,QA_ERROR_MEMORY,"Source directory path overflow"),NULL;
    char *path=malloc(a+b+2);
    if (!path) return fail(error,QA_ERROR_MEMORY,"Retaining Source child path"),NULL;
    memcpy(path,parent,a);
    if (a && b) path[a++]='/';
    memcpy(path+a,child,b+1); return path;
}
static bool source_order(qa_vfs *view,const qa_vfs *base,qa_error *error)
{
    size_t count=qa_vfs_mount_count(view),fixed=qa_vfs_mount_count(base),extra=count-fixed;
    if (count>SIZE_MAX/sizeof(qa_mount_id)) return fail(error,QA_ERROR_MEMORY,"Source search path overflow");
    qa_mount_id *order=count?malloc(count*sizeof(*order)):NULL;
    if (count && !order) return fail(error,QA_ERROR_MEMORY,"Retaining Source search order");
    bool okay=true;
    for (size_t i=0;i<count && okay;++i) {
        qa_vfs_mount_info mount;
        size_t source=i<extra?count-i-1:i-extra;
        okay=qa_vfs_mount_at(view,source,&mount);
        if (okay) order[i]=mount.id;
    }
    if (okay) okay=qa_vfs_set_order(view,order,count,error);
    free(order); return okay;
}
bool frontend_config_files_source_gamedir(frontend_config_files *owner,const char *directory,
    bool *changed,qa_error *error)
{
    if (!owner || !owner->qw_base || !directory || !changed || owner->writing ||
        strstr(directory,"..") || strchr(directory,'/') || strchr(directory,'\\') || strchr(directory,':'))
        return fail(error,QA_ERROR_ARGUMENT,"Source gamedir requires its idle actual QW file owner and a single filename");
    *changed=false;
    if (!strcmp(owner->qw_name,directory)) return true;
    char *name=copy_text(directory,error);
    qa_vfs *base=name?qa_vfs_clone(owner->qw_base,error):NULL;
    if (!base) { free(name); return false; }
    qa_vfs *retired=owner->selected;
    owner->selected=base; owner->qw_changed=true;
    free(owner->qw_name); owner->qw_name=name; *changed=true;
    qa_vfs_destroy(retired);
    if (!strcmp(directory,"id1") || !strcmp(directory,"qw")) return true;
    qa_fs_root *family=qa_vfs_mount_root(owner->console,owner->qw_family);
    qa_fs_root *home=qa_vfs_mount_root(owner->console,owner->shared);
    const char *relative_directory=!strcmp(directory,".")?"":directory;
    char *destination=source_child_path(owner->qw_home_prefix,relative_directory,error);
    qa_mount_id physical,user;
    bool okay=destination && qa_vfs_mount_child(owner->console,home,destination,
        QA_ARCHIVE_CASE_INSENSITIVE,true,&owner->writable,error);
    if (okay) {
        free(owner->qw_write_child); owner->qw_write_child=destination; destination=NULL;
    }
    if (okay) okay=*relative_directory?qa_vfs_mount_child(base,family,relative_directory,
        QA_ARCHIVE_CASE_INSENSITIVE,false,&physical,error):
        qa_vfs_mount_retained(base,owner->console,owner->qw_family,
            QA_ARCHIVE_CASE_INSENSITIVE,false,&physical,error);
    if (okay) okay=qa_vfs_mount_retained(base,owner->console,owner->writable,
        QA_ARCHIVE_CASE_INSENSITIVE,true,&user,error);
    free(destination);
    for (size_t i=0;okay;++i) {
        char leaf[64];
        int length=snprintf(leaf,sizeof(leaf),"pak%zu.pak",i);
        if (length<0 || (size_t)length>=sizeof(leaf)) { okay=fail(error,QA_ERROR_FORMAT,"Source package ordinal overflow"); break; }
        char *relative=source_child_path(relative_directory,leaf,error),*resolved=NULL;
        qa_error observed={0};
        okay=relative!=NULL;
        if (okay && !qa_fs_root_resolve(family,relative,fs_equal,NULL,false,&resolved,&observed)) {
            if (observed.code==QA_ERROR_NOT_FOUND) { free(relative); break; }
            if (error) *error=observed;
            okay=false;
        }
        qa_mount_id archive;
        if (okay) okay=qa_vfs_mount_archive_from(base,family,resolved,QA_ARCHIVE_PAK,
            QA_ARCHIVE_CASE_INSENSITIVE,&archive,error);
        free(relative); free(resolved);
        if (i==SIZE_MAX) { okay=fail(error,QA_ERROR_FORMAT,"Source package ordinal overflow"); break; }
    }
    qa_error order_error={0};
    bool ordered=source_order(base,owner->qw_base,&order_error);
    if (okay && !ordered && error) *error=order_error;
    return okay && ordered;
}
bool frontend_config_files_source_read(const frontend_config_files *owner,qa_launch_source_files *out,qa_error *error)
{
    if (!owner || !out || !owner->qw_base)
        return fail(error,QA_ERROR_ARGUMENT,"Source filesystem read requires its genuine retained QW owner");
    *out=(qa_launch_source_files){.catalog=owner->catalog,.product=owner->product,.base_product=owner->qw_base_product,
        .content=owner->selected,.base=owner->qw_base,.authority=owner->console,.family=owner->qw_family,
        .home=owner->shared,.directory=owner->qw_name,.home_prefix=owner->qw_home_prefix,.changed=owner->qw_changed};
    return qa_launch_source_files_current(out,error);
}
static bool source_current(const frontend_config_files *owner,qa_error *error)
{
    qa_launch_source_files source;
    if (!frontend_config_files_source_read(owner,&source,error)) return false;
    if (!owner->qw_write_child) return true;
    const char *prefix=qa_vfs_mount_root_prefix(owner->console,owner->writable);
    size_t length=strlen(owner->qw_home_prefix);
    if (strlen(owner->qw_write_child)<length)
        return fail(error,QA_ERROR_FORMAT,"Source write destination lost its retained family prefix");
    const char *leaf=owner->qw_write_child+length;
    if (strncmp(owner->qw_write_child,owner->qw_home_prefix,length) || (*leaf && *leaf!='/') ||
        (*leaf=='/' && (strstr(leaf+1,"..") || strpbrk(leaf+1,"/\\:"))) || !prefix || strcmp(prefix,owner->qw_write_child) ||
        !qa_fs_root_same_object(qa_vfs_mount_root(owner->console,owner->writable),qa_vfs_mount_root(owner->console,owner->shared)))
        return fail(error,QA_ERROR_FORMAT,"Source write destination lost its genuine retained home child");
    return true;
}
static bool acquire(qa_vfs *view,qa_mount_id mount,const char *name,qa_resource **out,qa_error *error)
{
    qa_error local_error={0};
    bool found=mount?qa_vfs_acquire_from(view,mount,name,out,&local_error):qa_vfs_acquire(view,name,out,NULL,&local_error);
    if (!found && local_error.code!=QA_ERROR_NOT_FOUND && error) *error=local_error;
    return found;
}
static bool loose(frontend_config_files *owner,bool base,const char *name,qa_resource **out,qa_error *error)
{
    qa_mount_id user=base?owner->script_base_user:owner->writable;
    qa_mount_id content=base?owner->script_base_loose:owner->loose;
    if (acquire(owner->console,user,name,out,error)) return true;
    return (!error || error->code==QA_OK) && content && acquire(owner->console,content,name,out,error);
}
static bool seat_path(uint32_t seat,const char *name,char **out,qa_error *error)
{
    size_t length=strlen(name);
    if (length>SIZE_MAX-64) return fail(error,QA_ERROR_FORMAT,"Seat configuration path overflow");
    char *path=malloc(length+64);
    if (!path) return fail(error,QA_ERROR_MEMORY,"Allocating seat configuration path");
    snprintf(path,length+64,"settings/seat-%u/%s",seat,name); *out=path; return true;
}
static bool legacy(frontend_config_files *owner,const char *name,qa_resource **out,qa_error *error)
{
    const qa_product *selected=qa_catalog_product(owner->catalog,owner->product);
    if (!selected || selected->family!=QA_GAME_Q1 || selected->edition==QA_EDITION_QUAKEWORLD || !folded_equal(name,"config.cfg")) return false;
    if (acquire(owner->console,owner->shared,name,out,error)) return true;
    if (error && error->code!=QA_OK) return false;
    qa_fs_root *shared=qa_vfs_mount_root(owner->console,owner->shared);
    char *newest=NULL; qa_fs_timestamp modified={0}; bool ok=true;
    qa_mount_id newest_mount=0;
    qa_fs_root *newest_root=NULL;
    for (size_t i=0;ok && i<=qa_catalog_count(owner->catalog);++i) {
        const qa_product *product=i?qa_catalog_at(owner->catalog,i-1):selected;
        if (!product || product->family!=QA_GAME_Q1 || product->edition==QA_EDITION_QUAKEWORLD || (i && product==selected)) continue;
        const qa_catalog_mount *mount=qa_catalog_product_write_mount(owner->catalog,product->id);
        qa_fs_root *root=qa_catalog_product_write_root(owner->catalog,product->id);
        size_t length=root?0:strlen(product->directory);
        char *path=length<=SIZE_MAX-12?malloc(length+12):NULL;
        if (!path) { ok=fail(error,QA_ERROR_MEMORY,"Selecting newest Q1 user configuration"); break; }
        if (root) memcpy(path,"config.cfg",11);
        else { root=shared; memcpy(path,product->directory,length); memcpy(path+length,"/config.cfg",12); }
        char *resolved=NULL; qa_error observed={0};
        bool found=qa_fs_root_resolve(root,path,fs_equal,NULL,false,&resolved,&observed);
        free(path);
        if (!found) {
            if (observed.code!=QA_ERROR_NOT_FOUND) { if (error) *error=observed; ok=false; }
            continue;
        }
        qa_fs_entry_kind kind; qa_fs_identity identity; qa_fs_timestamp time;
        ok=qa_fs_root_status(root,resolved,&kind,&identity,error);
        if (ok && kind==QA_FS_REGULAR) {
            ok=qa_fs_identity_modified_time(&identity,&time) || fail(error,QA_ERROR_FORMAT,"Q1 configuration has invalid retained modification metadata");
            if (ok && (!newest || time.seconds>modified.seconds ||
                (time.seconds==modified.seconds && time.nanoseconds>modified.nanoseconds))) {
                free(newest); newest=resolved; resolved=NULL; modified=time;
                newest_mount=mount?mount->id:0; newest_root=root;
            }
        }
        free(resolved);
    }
    qa_mount_id read_mount=owner->shared;
    if (ok && newest && newest_mount) {
        read_mount=0;
        for (size_t i=0;i<qa_vfs_mount_count(owner->console);++i) {
            qa_vfs_mount_info info={0};
            if (qa_vfs_mount_at(owner->console,i,&info) && !info.is_archive &&
                info.comparison==QA_ARCHIVE_CASE_INSENSITIVE &&
                qa_fs_root_same_object(newest_root,qa_vfs_mount_root(owner->console,info.id))) {
                read_mount=info.id; break;
            }
        }
        if (!read_mount) ok=qa_vfs_mount_retained(owner->console,qa_catalog_files(owner->catalog),newest_mount,
            QA_ARCHIVE_CASE_INSENSITIVE,false,&read_mount,error);
    }
    bool found=ok && newest && acquire(owner->console,read_mount,newest,out,error);
    free(newest); return found;
}
static bool source_loose(frontend_config_files *owner,const char *path,qa_resource **out,qa_error *error)
{
    for (size_t i=0;i<qa_vfs_mount_count(owner->selected);++i) {
        qa_vfs_mount_info info;
        if (!qa_vfs_mount_at(owner->selected,i,&info)) return fail(error,QA_ERROR_FORMAT,"Source loose search lost its actual row");
        if (info.is_archive) continue;
        if (acquire(owner->selected,info.id,path,out,error)) return true;
        if (error && error->code!=QA_OK) return false;
    }
    return false;
}
bool frontend_config_files_read(void *context,frontend_script_scope scope,const char *name,
    const qa_command_context *source,qa_bytes *out,void **lease,qa_error *error)
{
    frontend_config_files *owner=context;
    if (!owner || !out || !lease || owner->writing || owner->reads==SIZE_MAX || !local(source,error)) return false;
    char *path=qa_vfs_normalize_path(name,error);
    if (!path) return false;
    qa_resource *resource=NULL; bool found=false; qa_error observed={0};
    switch (scope) {
    case FRONTEND_SCRIPT_MOUNTED: found=acquire(owner->selected,0,path,&resource,&observed); break;
    case FRONTEND_SCRIPT_USER:
        if (source->origin==QA_COMMAND_SEAT) {
            char *seat=NULL;
            if (seat_path(source->seat,path,&seat,&observed))
                found=acquire(owner->console,owner->console_writable,seat,&resource,&observed);
            free(seat);
        }
        if (!found && observed.code==QA_OK) found=legacy(owner,path,&resource,&observed);
        if (!found && observed.code==QA_OK && !owner->qw_changed)
            found=acquire(owner->console,owner->writable,path,&resource,&observed);
        if (!found && observed.code==QA_OK) found=acquire(owner->selected,0,path,&resource,&observed);
        break;
    case FRONTEND_SCRIPT_BASE_LOOSE: found=loose(owner,true,path,&resource,&observed); break;
    case FRONTEND_SCRIPT_GAME_LOOSE:
        found=owner->qw_changed?source_loose(owner,path,&resource,&observed):loose(owner,false,path,&resource,&observed); break;
    case FRONTEND_SCRIPT_LOOSE:
        found=owner->qw_changed?source_loose(owner,path,&resource,&observed):loose(owner,false,path,&resource,&observed);
        if (!found && observed.code==QA_OK && owner->base!=owner->product) found=loose(owner,true,path,&resource,&observed);
        break;
    case FRONTEND_SCRIPT_SEAT: {
        char *seat=NULL;
        if (source->origin==QA_COMMAND_SEAT && seat_path(source->seat,path,&seat,&observed))
            found=acquire(owner->console,owner->console_writable,seat,&resource,&observed);
        free(seat); break;
    }
    default: fail(&observed,QA_ERROR_ARGUMENT,"Unknown startup script scope"); break;
    }
    free(path);
    if (!found) { if (error) *error=observed; return false; }
    script_lease *held=malloc(sizeof(*held));
    if (!held) { qa_resource_release(resource); return fail(error,QA_ERROR_MEMORY,"Retaining actual configuration script read"); }
    *held=(script_lease){owner,resource}; ++owner->reads; *out=qa_resource_bytes(resource); *lease=held; return true;
}
void frontend_config_files_release(void *context,void *lease)
{
    script_lease *held=lease; frontend_config_files *owner=context;
    if (!held) return;
    if (held->owner!=owner) return;
    qa_resource_release(held->resource); --owner->reads; free(held);
}
bool frontend_config_files_console_read(frontend_config_files *owner,const char *name,
    const qa_command_context *source,qa_bytes *out,void **lease,qa_error *error)
{
    qa_error observed={0};
    if (source && source->origin==QA_COMMAND_SEAT && frontend_config_files_read(owner,FRONTEND_SCRIPT_SEAT,name,source,out,lease,&observed)) return true;
    if (observed.code!=QA_OK) { if (error) *error=observed; return false; }
    return frontend_config_files_read(owner,FRONTEND_SCRIPT_USER,name,source,out,lease,error);
}
bool frontend_config_files_write_config(frontend_config_files *owner,const char *name,const qa_command_context *source,
    const qa_cvars *cvars,const qa_input_seat *seat,bool controllers,qa_error *error)
{
    if (!owner || owner->writing || !local(source,error)) return false;
    char *path=qa_vfs_normalize_path(name,error); if (!path) return false;
    qa_settings_store store=frontend_config_files_store(owner,false); char *seat_name=NULL;
    if (source->origin==QA_COMMAND_SEAT) {
        if (!seat_path(source->seat,path,&seat_name,error)) { free(path); return false; }
        store=(qa_settings_store){owner->console,owner->console_writable};
    }
    owner->writing=true;
    bool ok=qa_settings_save_config(store,seat_name?seat_name:path,cvars,seat,controllers,error);
    owner->writing=false; free(seat_name); free(path); return ok;
}
bool frontend_config_files_write_config_text(frontend_config_files *owner,const char *name,
    const qa_command_context *source,qa_bytes text,qa_error *error)
{
    if (!owner || owner->writing || (text.size && !text.data) || !local(source,error)) return false;
    char *path=qa_vfs_normalize_path(name,error); if (!path) return false;
    qa_settings_store store=frontend_config_files_store(owner,false); char *seat_name=NULL;
    if (source->origin==QA_COMMAND_SEAT) {
        if (!seat_path(source->seat,path,&seat_name,error)) { free(path); return false; }
        store=(qa_settings_store){owner->console,owner->console_writable};
    }
    owner->writing=true;
    bool ok=qa_settings_write(store,seat_name?seat_name:path,text,error);
    owner->writing=false; free(seat_name); free(path); return ok;
}
bool frontend_config_files_dump(frontend_config_files *owner,const char *name,const qa_command_context *source,
    const qa_console_buffer *buffer,qa_error *error)
{
    if (!owner || !buffer || owner->writing || owner->nonce==UINT64_MAX || !local(source,error)) return false;
    char *path=qa_vfs_normalize_path(name,error); if (!path) return false;
    qa_buffer text={0}; owner->writing=true;
    bool ok=qa_console_buffer_dump(buffer,&text,error) &&
        qa_fs_root_replace(qa_vfs_mount_root(owner->console,owner->console_writable),path,
                           (qa_bytes){text.data,text.size},++owner->nonce,error);
    owner->writing=false; qa_buffer_free(&text); free(path); return ok;
}
static bool names(frontend_config_files *owner,qa_mount_id mount,qa_vfs *view,const char *prefix,qa_vfs_listing *out,qa_error *error)
{
    qa_fs_root *root=qa_vfs_mount_root(view,mount); qa_fs_listing entries={0};
    const char *held=qa_vfs_mount_root_prefix(view,mount);
    char *relative=held?source_child_path(held,prefix,error):NULL;
    bool listed=root && relative && qa_fs_root_list(root,*relative?relative:".",&entries,error);
    free(relative);
    if (!listed) return false;
    bool ok=true;
    for (size_t i=0;ok && i<entries.count;++i) {
        qa_fs_entry *entry=entries.entries+i; size_t a=strlen(prefix),b=strlen(entry->name);
        if (a>SIZE_MAX-b-2) { ok=fail(error,QA_ERROR_MEMORY,"Configuration listing path overflow"); break; }
        char *path=malloc(a+b+2);
        if (!path) { ok=fail(error,QA_ERROR_MEMORY,"Listing configuration paths"); break; }
        if (a) { memcpy(path,prefix,a); path[a]='/'; } memcpy(path+a+(a?1:0),entry->name,b+1);
        if (entry->kind==QA_FS_DIRECTORY) ok=names(owner,mount,view,path,out,error);
        else if (entry->kind==QA_FS_REGULAR && b>=4 &&
            (entry->name[b-3]=='c'||entry->name[b-3]=='C') && (entry->name[b-2]=='f'||entry->name[b-2]=='F') &&
            (entry->name[b-1]=='g'||entry->name[b-1]=='G') && entry->name[b-4]=='.') {
            if (out->count==SIZE_MAX/sizeof(*out->names)) ok=fail(error,QA_ERROR_MEMORY,"Configuration listing overflow");
            char **rows=ok?realloc(out->names,(out->count+1)*sizeof(*rows)):NULL;
            if (ok && !rows) ok=fail(error,QA_ERROR_MEMORY,"Retaining configuration listing");
            if (ok) { out->names=rows; out->names[out->count++]=path; path=NULL; }
        }
        free(path);
    }
    qa_fs_listing_free(&entries); (void)owner; return ok;
}
static bool folded_equal(const char *a,const char *b)
{
    for (;;++a,++b) {
        unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if (x>='A'&&x<='Z') x+='a'-'A';
        if (y>='A'&&y<='Z') y+='a'-'A';
        if (x!=y) return false;
        if (!x) return true;
    }
}
static int compare_names(const void *a,const void *b) { return strcmp(*(const char *const *)a,*(const char *const *)b); }
bool frontend_config_files_list(frontend_config_files *owner,const qa_command_context *source,qa_vfs_listing *out,qa_error *error)
{
    if (!owner || !out || out->names || out->count || owner->writing || !local(source,error)) return false;
    qa_vfs_listing product={0},seat={0}; bool ok=names(owner,owner->writable,owner->console,"",&product,error);
    for (size_t i=0;ok && i<product.count;++i) {
        size_t j=i+1;
        while (j<product.count) {
            if (!folded_equal(product.names[i],product.names[j])) { ++j; continue; }
            free(product.names[i]); product.names[i]=product.names[j];
            memmove(product.names+j,product.names+j+1,(product.count-j-1)*sizeof(*product.names)); --product.count;
        }
    }
    char *prefix=NULL;
    if (ok && source->origin==QA_COMMAND_SEAT) {
        ok=seat_path(source->seat,"",&prefix,error);
        if (ok) { size_t n=strlen(prefix); if (n && prefix[n-1]=='/') prefix[n-1]=0;
            qa_fs_entry_kind kind; ok=qa_fs_root_status(qa_vfs_mount_root(owner->console,owner->console_writable),prefix,&kind,NULL,error);
            if (ok && kind!=QA_FS_MISSING) ok=names(owner,owner->console_writable,owner->console,prefix,&seat,error); }
    }
    for (size_t i=0;ok && i<seat.count;++i) {
        char *name=seat.names[i]+strlen(prefix)+1; size_t j=0;
        while (j<product.count && !folded_equal(product.names[j],name)) ++j;
        char *copy=malloc(strlen(name)+1);
        if (!copy) { ok=fail(error,QA_ERROR_MEMORY,"Retaining seat configuration listing"); break; }
        strcpy(copy,name);
        if (j<product.count) { free(product.names[j]); product.names[j]=copy; }
        else { char **rows=product.count<SIZE_MAX/sizeof(*rows)?realloc(product.names,(product.count+1)*sizeof(*rows)):NULL;
            if (!rows) { free(copy); ok=fail(error,QA_ERROR_MEMORY,"Combining seat configuration listing"); }
            else { product.names=rows; product.names[product.count++]=copy; } }
    }
    free(prefix); qa_vfs_listing_free(&seat);
    if (ok) { if (product.count>1) qsort(product.names,product.count,sizeof(*product.names),compare_names); *out=product; }
    else qa_vfs_listing_free(&product);
    return ok;
}
bool frontend_config_files_visit(const frontend_config_files *owner,const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!owner || !visitor || !visitor->pool || !visitor->catalog || !visitor->view || owner->reads || owner->writing)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration inventory requires its returned actual file owners");
    return visitor->pool(visitor->context,qa_catalog_resources(owner->catalog),error) &&
        visitor->catalog(visitor->context,owner->catalog,error) && visitor->view(visitor->context,qa_catalog_files(owner->catalog),error) &&
        visitor->view(visitor->context,owner->selected,error) &&
        (owner->base_files==owner->selected || visitor->view(visitor->context,owner->base_files,error)) &&
        (!owner->qw_base || visitor->view(visitor->context,owner->qw_base,error)) &&
        visitor->view(visitor->context,owner->console,error);
}
typedef struct files_state {
    uint64_t catalog,selected,base_files,console,writable,base_writable,console_writable,
        shared,loose,base_loose,script_base_user,script_base_loose,nonce,qw_base,qw_family;
    uint32_t product,base,qw_base_product;
    char *qw_name,*qw_home_prefix,*qw_write_child;
    bool qw_changed;
} files_state;
static bool saved_text(qa_source_save_io *io,char **value)
{
    bool present=*value!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?strlen(*value):0;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        *value=malloc(size+1);
        if (!*value) return fail(io->error,QA_ERROR_MEMORY,"Restoring Source directory text");
        (*value)[size]=0;
    }
    return qa_source_save_bytes(io,*value,size) && !memchr(*value,0,size);
}
static bool source_fields(qa_source_save_io *io,files_state *state)
{
    return qa_source_save_u64(io,&state->qw_base) && qa_source_save_u64(io,&state->qw_family) &&
        qa_source_save_u32(io,&state->qw_base_product) && qa_source_save_bool(io,&state->qw_changed) &&
        saved_text(io,&state->qw_name) && saved_text(io,&state->qw_home_prefix) && saved_text(io,&state->qw_write_child) &&
        (state->qw_base?state->qw_family && state->qw_base_product && state->qw_name && state->qw_home_prefix:
            !state->qw_family && !state->qw_base_product && !state->qw_name && !state->qw_home_prefix &&
            !state->qw_write_child && !state->qw_changed);
}
static bool fields(qa_source_save_io *io,files_state *state)
{
    uint8_t magic[4]={'Q','F','C','F'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFCF",4) && qa_source_save_u64(io,&state->catalog) && state->catalog && qa_source_save_u32(io,&state->product) && state->product &&
        qa_source_save_u32(io,&state->base) && state->base && qa_source_save_u64(io,&state->selected) && state->selected &&
        qa_source_save_u64(io,&state->base_files) && state->base_files && qa_source_save_u64(io,&state->console) && state->console &&
        qa_source_save_u64(io,&state->writable) && state->writable && qa_source_save_u64(io,&state->base_writable) && state->base_writable &&
        qa_source_save_u64(io,&state->console_writable) && state->console_writable &&
        qa_source_save_u64(io,&state->shared) && state->shared &&
        qa_source_save_u64(io,&state->loose) && qa_source_save_u64(io,&state->base_loose) &&
        qa_source_save_u64(io,&state->script_base_user) && state->script_base_user &&
        qa_source_save_u64(io,&state->script_base_loose) && qa_source_save_u64(io,&state->nonce) &&
        (state->selected==state->base_files)==(state->product==state->base) && state->console!=state->selected && state->console!=state->base_files &&
        source_fields(io,state);
}
bool frontend_config_files_checkpoint(const frontend_config_files *owner,const qa_application_content_graph *graph,qa_buffer *out,qa_error *error)
{
    if (!owner || !graph || !out || out->data || out->size || owner->reads || owner->writing)
        return fail(error,QA_ERROR_ARGUMENT,"Configuration capture requires its actual held graph");
    files_state state={.catalog=qa_application_content_catalog_id(graph,owner->catalog),
        .selected=qa_application_content_view_id(graph,owner->selected),.base_files=qa_application_content_view_id(graph,owner->base_files),
        .console=qa_application_content_view_id(graph,owner->console),.writable=owner->writable,.base_writable=owner->base_writable,
        .console_writable=owner->console_writable,.shared=owner->shared,.loose=owner->loose,.base_loose=owner->base_loose,
        .script_base_user=owner->script_base_user,.script_base_loose=owner->script_base_loose,.nonce=owner->nonce,
        .product=owner->product,.base=owner->base,.qw_family=owner->qw_family,.qw_base_product=owner->qw_base_product,
        .qw_base=owner->qw_base?qa_application_content_view_id(graph,owner->qw_base):0,.qw_changed=owner->qw_changed,
        .qw_name=owner->qw_name,.qw_home_prefix=owner->qw_home_prefix,.qw_write_child=owner->qw_write_child};
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,error) && fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_config_files_restore(qa_application_content_graph *graph,qa_bytes bytes,frontend_config_files **out,qa_error *error)
{
    if (!graph || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Configuration import requires its empty detached owner");
    files_state state={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    frontend_config_files *owner=ok?calloc(1,sizeof(*owner)):NULL;
    if (ok && !owner) {
        free(state.qw_name); free(state.qw_home_prefix); free(state.qw_write_child);
        return fail(error,QA_ERROR_MEMORY,"Importing configuration file owner");
    }
    if (ok) {
        owner->catalog=qa_application_content_catalog(graph,state.catalog); owner->product=state.product; owner->base=state.base;
        owner->selected=qa_application_content_view(graph,state.selected); owner->base_files=qa_application_content_view(graph,state.base_files);
        owner->console=qa_application_content_view(graph,state.console);
        owner->writable=state.writable; owner->base_writable=state.base_writable; owner->console_writable=state.console_writable;
        owner->shared=state.shared; owner->loose=state.loose; owner->base_loose=state.base_loose;
        owner->script_base_user=state.script_base_user; owner->script_base_loose=state.script_base_loose;
        owner->qw_base=state.qw_base?qa_application_content_view(graph,state.qw_base):NULL;
        owner->qw_family=state.qw_family; owner->qw_base_product=state.qw_base_product;
        owner->qw_name=state.qw_name; owner->qw_home_prefix=state.qw_home_prefix;
        owner->qw_write_child=state.qw_write_child; owner->qw_changed=state.qw_changed;
        const qa_product *product=qa_catalog_product(owner->catalog,owner->product); size_t depth=0;
        qa_product_id ancestor=product?qa_catalog_configuration_base(owner->catalog,product->id):QA_PRODUCT_NONE;
        while (product && ancestor && depth++<qa_catalog_count(owner->catalog)) {
            product=qa_catalog_product(owner->catalog,ancestor);
            ancestor=product?qa_catalog_configuration_base(owner->catalog,product->id):QA_PRODUCT_NONE;
        }
        ok=product && !ancestor && product->id==owner->base && owner->selected && owner->base_files && owner->console &&
            (state.qw_changed || qa_catalog_product_view_current(owner->catalog,owner->product,owner->selected)) &&
            qa_catalog_product_view_current(owner->catalog,owner->base,owner->base_files);
        qa_mount_id ids[]={state.writable,state.base_writable,state.console_writable,state.shared,state.loose,state.base_loose,state.script_base_user,state.script_base_loose};
        for (size_t j=0;ok && j<sizeof(ids)/sizeof(ids[0]);++j) {
            if (!ids[j]) continue;
            qa_vfs_mount_info info={0}; bool found=false;
            for (size_t i=0;i<qa_vfs_mount_count(owner->console);++i)
                if (qa_vfs_mount_at(owner->console,i,&info) && info.id==ids[j] && !info.is_archive && info.writable==(j<4 || j==6)) found=true;
            ok=found;
        }
        ok=ok && (state.writable==state.base_writable)==(owner->product==owner->base) &&
            (owner->product!=owner->base || state.loose==state.base_loose) && state.console_writable!=state.shared &&
            state.console_writable!=state.writable && state.console_writable!=state.base_writable &&
            state.shared!=state.writable && state.shared!=state.base_writable;
        qa_product_id immediate=qa_catalog_configuration_base(owner->catalog,owner->product);
        qa_product_id products[]={owner->product,owner->base,immediate?immediate:owner->product};
        qa_mount_id mounts[]={state.writable,state.base_writable,state.script_base_user};
        qa_mount_id loose_mounts[]={state.loose,state.base_loose,state.script_base_loose};
        for (size_t i=0;ok && i<sizeof(products)/sizeof(products[0]);++i) {
            qa_fs_root *actual=qa_catalog_product_write_root(owner->catalog,products[i]);
            if (actual && !(i==0 && state.qw_write_child)) ok=qa_fs_root_same_object(actual,qa_vfs_mount_root(owner->console,mounts[i]));
            qa_fs_root *loose_root=qa_catalog_product_loose_root(owner->catalog,products[i]);
            if (ok) ok=loose_root?loose_mounts[i] &&
                qa_fs_root_same_object(loose_root,qa_vfs_mount_root(owner->console,loose_mounts[i])):!loose_mounts[i];
        }
        if (ok && state.qw_base) ok=source_current(owner,error);
        /* Borrowed qualification precedes every transfer of destructor ownership. */
        owner->catalog=NULL; owner->selected=owner->base_files=owner->console=owner->qw_base=NULL;
        owner->qw_name=owner->qw_home_prefix=owner->qw_write_child=NULL;
    }
    if (ok) ok=qa_application_content_retain_catalog(graph,state.catalog,&owner->catalog,error) &&
        qa_application_content_claim_view(graph,state.selected,&owner->selected,error);
    if (ok && state.selected==state.base_files) owner->base_files=owner->selected;
    else if (ok) ok=qa_application_content_claim_view(graph,state.base_files,&owner->base_files,error);
    if (ok) ok=qa_application_content_claim_view(graph,state.console,&owner->console,error);
    if (ok && state.qw_base) ok=qa_application_content_claim_view(graph,state.qw_base,&owner->qw_base,error);
    if (ok) { owner->writable=state.writable; owner->base_writable=state.base_writable; owner->console_writable=state.console_writable;
        owner->shared=state.shared; owner->loose=state.loose; owner->base_loose=state.base_loose;
        owner->script_base_user=state.script_base_user; owner->script_base_loose=state.script_base_loose; owner->nonce=state.nonce;
        owner->qw_name=state.qw_name; state.qw_name=NULL; owner->qw_home_prefix=state.qw_home_prefix; state.qw_home_prefix=NULL;
        owner->qw_write_child=state.qw_write_child; state.qw_write_child=NULL; *out=owner; }
    else { frontend_config_files_destroy(owner,NULL); if (!error || error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Configuration continuation leaves actual selected roots"); }
    free(state.qw_name); free(state.qw_home_prefix); free(state.qw_write_child);
    return ok;
}
