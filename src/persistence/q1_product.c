#include "qa/q1_save_product.h"
#include "qa/qc_text_save.h"
#include <stdlib.h>
#include <string.h>

static unsigned char lower(unsigned char c)
{ return c>='A' && c<='Z'?(unsigned char)(c+'a'-'A'):c; }
static bool equal(const char *a,size_t an,const char *b,size_t bn)
{
    if (an!=bn) return false;
    for (size_t i=0;i<an;++i) if (lower((unsigned char)a[i])!=lower((unsigned char)b[i])) return false;
    return true;
}
static bool failure(qa_error *error,const char *message)
{ qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message); return false; }
static const char *basename(const char *text)
{
    const char *base=text;
    for (const char *p=text;*p;++p) if (*p=='/' || *p=='\\') base=p+1;
    return base;
}
static bool game_directory(const qa_q1_save_data *save,const char **game,size_t *size,qa_error *error)
{
    *game=NULL; *size=0;
    if (save->version==5) return true;
    if (save->version!=6 || !save->game_directories) return failure(error,"Invalid source save version/directories");
    const char *start=save->game_directories;
    for (const char *p=start;;++p) {
        unsigned char c=(unsigned char)*p;
        if (!c || c==';') {
            if (p!=start) { *game=start; *size=(size_t)(p-start); }
            if (!c) break;
            start=p+1;
        } else if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))
            return failure(error,"Invalid source save game directories");
    }
    return *game!=NULL || failure(error,"Source save has no game directory");
}
bool qa_q1_save_select_product(const qa_catalog *catalog,const qa_q1_save_data *save,
    const char *path,const qa_product **out,qa_error *error)
{
    if (!catalog || !path || !out || !qa_q1_save_singleplayer(save,error)) return false;
    const char *game=NULL; size_t game_size=0;
    if (!game_directory(save,&game,&game_size,error)) return false;
    size_t map_size=strlen(save->map);
    if (map_size>SIZE_MAX-10) return failure(error,"Source map path overflows");
    char *map=malloc(map_size+10);
    if (!map) { qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing source save map selection"); return false; }
    memcpy(map,"maps/",5); memcpy(map+5,save->map,map_size); memcpy(map+5+map_size,".bsp",5);
    const char *leaf=basename(path),*parent=path; size_t parent_size=0;
    if (leaf!=path) {
        const char *end=leaf-1;
        while (end>path && (end[-1]=='/' || end[-1]=='\\')) --end;
        parent=end;
        while (parent>path && parent[-1]!='/' && parent[-1]!='\\') --parent;
        parent_size=(size_t)(end-parent);
    }
    const qa_product *only=NULL,*contextual=NULL;
    qa_error mismatch={0};
    size_t count=0,context_count=0;
    for (size_t i=0;i<qa_catalog_count(catalog);++i) {
        const qa_product *product=qa_catalog_at(catalog,i);
        if (!product || product->family!=QA_GAME_Q1 || product->availability!=QA_CONTENT_INSTALLED) continue;
        if ((save->version==5 && product->edition!=QA_EDITION_CLASSIC) ||
            (save->version==6 && product->edition!=QA_EDITION_RERELEASE)) continue;
        if (game) {
            if (!product->directory) continue;
            const char *directory=basename(product->directory);
            if (!equal(directory,strlen(directory),game,game_size)) continue;
        }
        size_t maps_count=0; const qa_catalog_map *maps=qa_catalog_maps(catalog,product->id,&maps_count);
        bool found=false;
        for (size_t j=0;j<maps_count;++j)
            if (maps[j].path && equal(maps[j].path,strlen(maps[j].path),map,map_size+9)) { found=true; break; }
        if (!found) continue;
        qa_vfs *view=NULL; qa_qc_program *program=NULL; qa_error observed={0};
        bool compatible=qa_catalog_open(catalog,product->id,&view,&observed) &&
            qa_qc_program_load_vfs(view,"progs.dat",&program,&observed) &&
            qa_qc_text_program_ready(program,save,&observed);
        qa_qc_program_destroy(program); qa_vfs_destroy(view);
        if (!compatible) {
            if (observed.code==QA_ERROR_MEMORY) { free(map);if (error) *error=observed;return false; }
            mismatch=observed;continue;
        }
        ++count; only=product;
        if ((product->key && equal(product->key,strlen(product->key),parent,parent_size)) ||
            (product->directory && equal(basename(product->directory),strlen(basename(product->directory)),parent,parent_size)))
            { ++context_count; contextual=product; }
    }
    free(map);
    if (context_count==1) { *out=contextual; return true; }
    if (count==1) { *out=only; return true; }
    if (!count && mismatch.code!=QA_OK) { if (error) *error=mismatch;return false; }
    return failure(error,count?"Original save matches several installed games; its content does not identify one":
        "Required original save game content is not installed");
}
