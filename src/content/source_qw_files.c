#include "qa/source_qw_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(qa_error *e,qa_status status,const char *text)
{ qa_error_set(e,status,0,"%s",text); return false; }
static bool fs_equal(const char *a,const char *b,void *opaque)
{ (void)opaque; return qa_archive_paths_equal(a,b,QA_ARCHIVE_CASE_INSENSITIVE); }
char *qa_source_files_child_path(const char *parent,const char *child,qa_error *error)
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
bool qa_source_qw_files_base(const qa_catalog *catalog, qa_product_id selected,
    qa_vfs **files, qa_product_id *base_product, char **directory,
    char **home_prefix, qa_error *error)
{
    const qa_product *product=qa_catalog_product(catalog,selected), *base=product;
    if (!product || product->edition!=QA_EDITION_QUAKEWORLD || !files ||
        !base_product || !directory || *directory || !home_prefix || *home_prefix)
        return fail(error,QA_ERROR_ARGUMENT,"Source base requires its actual QW product");
    size_t depth=0;
    while (base->base) {
        const qa_product *next=qa_catalog_product(catalog,base->base);
        if (!next || ++depth>qa_catalog_count(catalog))
            return fail(error,QA_ERROR_FORMAT,"QuakeWorld Source has invalid fixed-base ancestry");
        if (next->edition!=QA_EDITION_QUAKEWORLD) break;
        base=next;
    }
    const char *leaf=strrchr(product->directory,'/');
    if (!leaf || !leaf[1]) return fail(error,QA_ERROR_FORMAT,"Source family has no logical directory");
    size_t length=(size_t)(leaf-product->directory);
    *directory=malloc(strlen(leaf+1)+1); *home_prefix=malloc(length+1);
    if (!*directory || !*home_prefix) {
        free(*directory); free(*home_prefix); *directory=*home_prefix=NULL;
        return fail(error,QA_ERROR_MEMORY,"Retaining Source family directory");
    }
    strcpy(*directory,leaf+1); memcpy(*home_prefix,product->directory,length); (*home_prefix)[length]=0;
    *base_product=base->id;
    if (*files) return qa_catalog_product_view_current(catalog,base->id,*files) ||
        fail(error,QA_ERROR_FORMAT,"Source base differs from its current product");
    return qa_catalog_open(catalog,base->id,files,error);
}
bool qa_source_qw_files_change(const qa_vfs *fixed, qa_vfs *authority,
    qa_mount_id family_mount, qa_mount_id home_mount, const char *home_prefix,
    const char *directory, qa_vfs **content, qa_mount_id *writable,
    char **write_child, qa_error *error)
{
    if (!fixed || !authority || !directory || !content || *content || !writable ||
        !write_child || *write_child || strstr(directory,"..") || strchr(directory,'/') ||
        strchr(directory,'\\') || strchr(directory,':'))
        return fail(error,QA_ERROR_ARGUMENT,"Source gamedir requires a single filename");
    qa_vfs *base=qa_vfs_clone(fixed,error);
    if (!base) return false;
    *content=base;
    if (!strcmp(directory,"id1") || !strcmp(directory,"qw")) return true;
    qa_fs_root *family=qa_vfs_mount_root(authority,family_mount);
    qa_fs_root *home=qa_vfs_mount_root(authority,home_mount);
    const char *relative_directory=!strcmp(directory,".")?"":directory;
    char *destination=qa_source_files_child_path(home_prefix,relative_directory,error);
    qa_mount_id physical,user;
    bool okay=destination && qa_vfs_mount_child(authority,home,destination,
        QA_ARCHIVE_CASE_INSENSITIVE,true,writable,error);
    if (okay) {
        *write_child=destination; destination=NULL;
    }
    if (okay) okay=*relative_directory?qa_vfs_mount_child(base,family,relative_directory,
        QA_ARCHIVE_CASE_INSENSITIVE,false,&physical,error):
        qa_vfs_mount_retained(base,authority,family_mount,
            QA_ARCHIVE_CASE_INSENSITIVE,false,&physical,error);
    if (okay) okay=qa_vfs_mount_retained(base,authority,*writable,
        QA_ARCHIVE_CASE_INSENSITIVE,true,&user,error);
    free(destination);
    for (size_t i=0;okay;++i) {
        char leaf[64];
        int length=snprintf(leaf,sizeof(leaf),"pak%zu.pak",i);
        if (length<0 || (size_t)length>=sizeof(leaf)) { okay=fail(error,QA_ERROR_FORMAT,"Source package ordinal overflow"); break; }
        char *relative=qa_source_files_child_path(relative_directory,leaf,error),*resolved=NULL;
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
    bool ordered=source_order(base,fixed,&order_error);
    if (okay && !ordered && error) *error=order_error;
    return okay && ordered;
}
