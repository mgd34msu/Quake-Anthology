#include "internal.h"
#include <stdio.h>

typedef struct search_directory { const char *path; unsigned depth; } search_directory;
const char *catalog_native_parent(qa_catalog *catalog,const char *path,qa_error *error)
{
    const char *slash=strrchr(path,'/'),*back=strrchr(path,'\\');
    if (!slash || (back && back>slash)) slash=back;
    if (!slash) return NULL;
    size_t length=(size_t)(slash-path);
    if (!length || (length==2 && path[1]==':')) ++length;
    char *parent=malloc(length+1);
    if (!parent) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining discovered install ancestry"); return NULL; }
    memcpy(parent,path,length); parent[length]=0;
    const char *result=catalog_string(catalog,parent,error); free(parent); return result;
}
static bool directory_complete(qa_catalog *catalog,const catalog_product *product,const char *path,
    bool *complete,qa_error *error)
{
    *complete=true;
    for (size_t i=0;i<product->required_count;++i) {
        const char *leaf=strrchr(product->required[i],'/'),*file=NULL;
        if (!catalog_physical_path(catalog,path,leaf?leaf+1:product->required[i],&file,error)) return false;
        qa_fs_entry_kind kind=QA_FS_MISSING;
        if (file && !qa_fs_path_status(file,true,&kind,NULL,error)) return false;
        if (kind!=QA_FS_REGULAR) *complete=false;
    }
    return true;
}
bool catalog_product_directory(qa_catalog *catalog,catalog_product *product,const char **out,qa_error *error)
{
    if (product->installed_directory) { *out=product->installed_directory; return true; }
    *out=NULL; const char *first=NULL;
    if (product->view.base) {
        catalog_product *base=catalog->products+product->view.base-1;
        if (base->installed_directory) {
            const char *parent=catalog_native_parent(catalog,base->installed_directory,error);
            if (!parent && error && error->code==QA_ERROR_MEMORY) return false;
            if (parent) {
                const char *leaf=strrchr(product->view.directory,'/'),*candidate=NULL;
                bool ok=catalog_physical_path(catalog,parent,leaf?leaf+1:product->view.directory,&candidate,error);
                if (!ok) return false;
                if (candidate) {
                    bool complete;
                    if (!directory_complete(catalog,product,candidate,&complete,error)) return false;
                    first=candidate;
                    if (complete) { *out=candidate; return true; }
                }
            }
        }
    }
    size_t logical_length=strlen(product->view.directory);
    for (size_t length=logical_length;length;--length) for (size_t i=0;i<catalog->location_count;++i) {
        const catalog_location *location=catalog->locations+i;
        if (strlen(location->logical)!=length || strncmp(product->view.directory,location->logical,length) ||
            (product->view.directory[length] && product->view.directory[length]!='/')) continue;
        const char *tail=product->view.directory+length,*candidate=NULL;
        if (*tail=='/') ++tail;
        if (!catalog_physical_path(catalog,location->path,tail,&candidate,error)) return false;
        if (!candidate) continue;
        if (!first) first=candidate;
        bool complete;
        if (!directory_complete(catalog,product,candidate,&complete,error)) return false;
        if (complete) { *out=candidate; return true; }
    }
    const char *fallback=NULL;
    if (!catalog_physical_path(catalog,catalog->root,product->view.directory,&fallback,error)) return false;
    *out=first?first:fallback; return true;
}
bool catalog_location_matches(const qa_catalog *catalog,const char *logical,const char *path)
{
    for (size_t i=0;i<catalog->location_count;++i) {
        const catalog_location *location=catalog->locations+i;
        size_t length=strlen(location->logical),native=strlen(location->path);
        if (strncmp(logical,location->logical,length) || (logical[length] && logical[length]!='/') ||
            strncmp(path,location->path,native)) continue;
        const char *tail=logical+length,*actual=path+native;
        if (*tail=='/') ++tail;
        if (*tail && native && location->path[native-1]!='/' && location->path[native-1]!='\\') {
            if (*actual!='/' && *actual!='\\') continue;
            ++actual;
        }
        if (catalog_ascii_equal(tail,actual)) return true;
    }
    return false;
}
static bool location_add(qa_catalog *catalog,const char *logical,const char *path,qa_error *error)
{
    for (size_t i=0;i<catalog->location_count;++i)
        if (!strcmp(catalog->locations[i].logical,logical) && !strcmp(catalog->locations[i].path,path)) return true;
    if (!catalog_grow((void **)&catalog->locations,&catalog->location_capacity,catalog->location_count+1,
        sizeof(*catalog->locations),error)) return false;
    const char *name=catalog_string(catalog,logical,error),*native=catalog_string(catalog,path,error);
    if (!name || !native) return false;
    catalog->locations[catalog->location_count++]=(catalog_location){name,native}; return true;
}
static const qa_fs_entry *entry_named(const qa_fs_listing *list,const char *name)
{
    for (size_t i=0;i<list->count;++i)
        if (list->entries[i].kind==QA_FS_REGULAR && catalog_ascii_equal(list->entries[i].name,name))
            return list->entries+i;
    return NULL;
}
static bool archive_has(const qa_archive *archive,const char *path)
{
    const qa_archive_entry *entry=NULL;
    return qa_archive_find_normalized(archive,path,QA_ARCHIVE_CASE_INSENSITIVE,0,&entry,NULL) && entry;
}
static bool recognize(qa_catalog *catalog,const char *path,const qa_fs_listing *listing,qa_error *error)
{
    const char *leaf=strrchr(path,'/');
    const char *back=strrchr(path,'\\'); if (!leaf || (back && back>leaf)) leaf=back;
    leaf=leaf?leaf+1:path;
    const qa_fs_entry *pak=entry_named(listing,"pak0.pak"),*pk3=entry_named(listing,"pak0.pk3");
    int family=-1;
    if (pak && catalog_ascii_equal(leaf,"id1")) family=QA_GAME_Q1;
    else if (pak && catalog_ascii_equal(leaf,"baseq2")) family=QA_GAME_Q2;
    else if (pk3 && (catalog_ascii_equal(leaf,"baseq3") || catalog_ascii_equal(leaf,"demota"))) family=QA_GAME_Q3;
    if (family<0 && (pak || pk3)) {
        const qa_fs_entry *entry=pak?pak:pk3; const char *archive_path=NULL;
        if (!catalog_physical_path(catalog,path,entry->name,&archive_path,error)) return false;
        qa_archive *archive=NULL; qa_error issue={0};
        if (archive_path && qa_archive_open_file(archive_path,pak?QA_ARCHIVE_PAK:QA_ARCHIVE_PK3,&archive,&issue)) {
            if (pak && archive_has(archive,"progs.dat") && archive_has(archive,"maps/e1m1.bsp")) family=QA_GAME_Q1;
            else if (pak && archive_has(archive,"maps/base1.bsp") && archive_has(archive,"pics/colormap.pcx")) family=QA_GAME_Q2;
            else if (pk3 && archive_has(archive,"maps/q3dm1.bsp")) family=QA_GAME_Q3;
            qa_archive_close(archive);
        } else if (issue.code==QA_ERROR_MEMORY) { if (error) *error=issue; return false; }
    }
    if (entry_named(listing,"QuakeEX.kpf") && !location_add(catalog,"q1/rerelease",path,error)) return false;
    if (entry_named(listing,"Q2Game.kpf") && !location_add(catalog,"q2/rerelease",path,error)) return false;
    if (family<0) return true;
    size_t parent_length=(size_t)(leaf-path);
    if (parent_length>1 && (path[parent_length-1]=='/' || path[parent_length-1]=='\\') &&
        !(parent_length==3 && path[1]==':')) --parent_length;
    char *parent=malloc(parent_length+1);
    if (!parent) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining discovered game directory"); return false; }
    memcpy(parent,path,parent_length); parent[parent_length]=0;
    if (!parent_length) { free(parent); parent=NULL; }
    bool rerelease=false;
    if (parent) {
        const char *marker=NULL;
        if (!catalog_physical_path(catalog,parent,family==QA_GAME_Q1?"QuakeEX.kpf":"Q2Game.kpf",&marker,error)) { free(parent); return false; }
        rerelease=family!=QA_GAME_Q3 && marker!=NULL;
        const char *name=strrchr(parent,'/'),*separator=strrchr(parent,'\\');
        if (!name || (separator && separator>name)) name=separator;
        name=name?name+1:parent;
        rerelease|=family!=QA_GAME_Q3 && catalog_ascii_equal(name,"rerelease");
    }
    const char *logical=family==QA_GAME_Q1?(rerelease?"q1/rerelease":"q1"):
        family==QA_GAME_Q2?(rerelease?"q2/rerelease":"q2"):"q3a";
    const char *data=family==QA_GAME_Q1?"id1":family==QA_GAME_Q2?"baseq2":
        catalog_ascii_equal(leaf,"demota")?"demota":"baseq3";
    char directory[64]; snprintf(directory,sizeof(directory),"%s/%s",logical,data);
    bool ok=(!parent || location_add(catalog,logical,parent,error)) && location_add(catalog,directory,path,error);
    free(parent); return ok;
}
static int entry_order(const void *left,const void *right)
{ return strcmp(((const qa_fs_entry *)left)->name,((const qa_fs_entry *)right)->name); }
static bool queue_add(qa_catalog *catalog,search_directory **queue,size_t *count,size_t *capacity,
    const char *path,unsigned depth,qa_error *error)
{
    for (size_t i=0;i<*count;++i) if (!strcmp((*queue)[i].path,path)) return true;
    if (*count==8192) return true;
    if (!catalog_grow((void **)queue,capacity,*count+1,sizeof(**queue),error)) return false;
    const char *owned=catalog_string(catalog,path,error); if (!owned) return false;
    (*queue)[(*count)++]=(search_directory){owned,depth}; return true;
}
bool catalog_discover_locations(qa_catalog *catalog,qa_error *error)
{
    search_directory *queue=NULL; size_t count=0,capacity=0; bool ok=true;
    for (size_t i=0;ok && i<=catalog->install_root_count;++i) {
        const char *root=i?catalog->install_roots[i-1]:catalog->root,*path=NULL; qa_error issue={0};
        if (!catalog_physical_path(catalog,root,"",&path,&issue)) {
            if (issue.code==QA_ERROR_MEMORY) { if (error) *error=issue; ok=false; }
            continue;
        }
        if (path) ok=queue_add(catalog,&queue,&count,&capacity,path,0,error);
    }
    for (size_t at=0;ok && at<count;++at) {
        search_directory current=queue[at]; qa_fs_listing list={0}; qa_error issue={0};
        if (!qa_fs_path_list(current.path,&list,&issue)) {
            if (issue.code==QA_ERROR_MEMORY) { if (error) *error=issue; ok=false; }
            qa_fs_listing_free(&list); continue;
        }
        if (list.count>1) qsort(list.entries,list.count,sizeof(*list.entries),entry_order);
        ok=recognize(catalog,current.path,&list,error);
        if (ok && current.depth<4) for (size_t i=0;ok && i<list.count;++i) {
            const qa_fs_entry *entry=list.entries+i;
            if (entry->kind!=QA_FS_DIRECTORY || entry->name[0]=='.') continue;
            const char *child=NULL;
            if (!catalog_physical_path(catalog,current.path,entry->name,&child,error)) { ok=false; break; }
            if (child) ok=queue_add(catalog,&queue,&count,&capacity,child,current.depth+1,error);
        }
        qa_fs_listing_free(&list);
    }
    free(queue); return ok;
}
