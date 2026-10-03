#include "guest_mod_item_definition.h"
#include "internal.h"
#include "qa/text.h"
#include <stdlib.h>

bool application_mod_item_text(const qa_json_document *d,qa_json_id id,char **out,qa_error *e)
{ qa_buffer b={0}; if(!qa_json_string(d,id,&b,e)) return false; if(memchr(b.data,0,b.size)) {qa_buffer_free(&b);return application_fail(e,QA_ERROR_FORMAT,"Item declaration contains NUL");} *out=(char *)b.data; return true; }
bool application_mod_item_identity(const qa_json_document *d,qa_json_id id,qa_strings *s,qa_item_id *out,qa_error *e)
{
    char *name=NULL; if(!application_mod_item_text(d,id,&name,e)) return false;
    char *colon=strchr(name,':'); bool ok=colon&&colon!=name&&colon[1]&&qa_strings_intern_cstr(s,name,out,e);
    free(name); return ok||application_fail(e,QA_ERROR_FORMAT,"Item identity requires its declared namespace");
}
static bool raw_optional(const qa_json_document *d,qa_json_id id,qa_buffer *out,qa_error *e)
{
    if(id==QA_JSON_NONE) return true;
    qa_bytes bytes=qa_json_source(d,id); out->data=malloc(bytes.size); out->size=bytes.size;
    if(bytes.size&&!out->data) return application_fail(e,QA_ERROR_MEMORY,"Retaining item presentation declaration");
    if(bytes.size) memcpy(out->data,bytes.data,bytes.size);
    return true;
}
static bool resource_path(const qa_json_document *d,qa_json_id id,qa_error *e)
{
    char *path=NULL;if(!application_mod_item_text(d,id,&path,e))return false;
    size_t size=strlen(path);bool ok=size!=0;
    if(size>=2&&((path[0]>='A'&&path[0]<='Z')||(path[0]>='a'&&path[0]<='z'))&&path[1]==':')ok=false;
    size_t start=0;
    for(size_t i=0;ok&&i<=size;++i)if(i==size||path[i]=='/'||path[i]=='\\'){
        size_t length=i-start;
        if(!length||(length==1&&path[start]=='.')||(length==2&&path[start]=='.'&&path[start+1]=='.'))ok=false;
        start=i+1;
    }
    free(path);return ok||application_fail(e,QA_ERROR_FORMAT,"Item media path is not a relative source resource");
}
static bool digest(const qa_json_document *d,qa_json_id id,qa_error *e)
{
    char *value=NULL;if(!application_mod_item_text(d,id,&value,e))return false;
    bool ok=strlen(value)==71&&!memcmp(value,"sha256:",7);
    for(size_t i=7;ok&&i<71;++i)ok=(value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f');
    free(value);return ok||application_fail(e,QA_ERROR_FORMAT,"Held item model digest is not a source SHA256 identity");
}
static bool vector(const qa_json_document *d,qa_json_id id,double out[3],qa_error *e)
{
    const char *names[]={"x","y","z"};
    for(size_t i=0;i<3;++i)if(!qa_json_number(d,qa_json_get(d,id,names[i]),out+i,e)||!isfinite(out[i]))return false;
    return true;
}
static double dot(const double a[3],const double b[3])
{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static bool held(const qa_json_document *d,qa_json_id id,qa_error *e)
{
    if(id==QA_JSON_NONE)return true;
    qa_json_id kind=qa_json_get(d,id,"kind");
    if(qa_json_string_equal(d,kind,"none"))return true;
    if(!qa_json_string_equal(d,kind,"model"))return false;
    qa_json_id model=qa_json_get(d,id,"model"),grip=qa_json_get(d,model,"grip"),
        axes=qa_json_get(d,grip,"axis"),scale=qa_json_get(d,grip,"scale"),
        part=qa_json_get(d,model,"part"),hash=qa_json_get(d,model,"digest"),fallback=qa_json_get(d,model,"fallback");
    uint64_t frame;double origin[3],axis[3][3],sizes[3]={1,1,1};
    if(!resource_path(d,qa_json_get(d,model,"path"),e)||
        !qa_json_u64(d,qa_json_get(d,model,"referenceFrame"),&frame,e)||frame>UINT64_C(9007199254740991)||
        !vector(d,qa_json_get(d,grip,"origin"),origin,e)||qa_json_type(d,axes)!=QA_JSON_ARRAY||qa_json_size(d,axes)!=3||
        (scale!=QA_JSON_NONE&&!vector(d,scale,sizes,e))||sizes[0]==0||sizes[1]==0||sizes[2]==0||
        (hash!=QA_JSON_NONE&&!digest(d,hash,e))||(fallback!=QA_JSON_NONE&&!resource_path(d,fallback,e)))return false;
    for(size_t i=0;i<3;++i)if(!vector(d,qa_json_at(d,axes,i),axis[i],e)||fabs(dot(axis[i],axis[i])-1)>0.001)return false;
    double cross[3]={axis[0][1]*axis[1][2]-axis[0][2]*axis[1][1],
        axis[0][2]*axis[1][0]-axis[0][0]*axis[1][2],axis[0][0]*axis[1][1]-axis[0][1]*axis[1][0]};
    if(fabs(dot(axis[0],axis[1]))>0.001||fabs(dot(axis[0],axis[2]))>0.001||fabs(dot(axis[1],axis[2]))>0.001||dot(cross,axis[2])<0.999)return false;
    if(part!=QA_JSON_NONE){qa_json_id hashes=qa_json_get(d,part,"digests"),vertices=qa_json_get(d,part,"vertices");
        if(qa_json_type(d,hashes)!=QA_JSON_ARRAY||!qa_json_size(d,hashes)||
            qa_json_type(d,vertices)!=QA_JSON_ARRAY||!qa_json_size(d,vertices))return false;
        for(size_t i=0;i<qa_json_size(d,hashes);++i)if(!digest(d,qa_json_at(d,hashes,i),e))return false;
        for(size_t i=0;i<qa_json_size(d,vertices);++i){uint64_t vertex;
            if(!qa_json_u64(d,qa_json_at(d,vertices,i),&vertex,e)||vertex>UINT64_C(9007199254740991))return false;
            for(size_t j=0;j<i;++j){uint64_t prior;if(!qa_json_u64(d,qa_json_at(d,vertices,j),&prior,e)||prior==vertex)return false;}
        }
    }
    return true;
}
static bool icon(const qa_json_document *d,qa_json_id id,qa_error *e)
{
    if(id==QA_JSON_NONE||qa_json_type(d,id)==QA_JSON_NULL)return true;
    qa_json_id kind=qa_json_get(d,id,"kind");bool shader=qa_json_string_equal(d,kind,"shader"),wad=qa_json_string_equal(d,kind,"wad-picture");
    if((!shader&&!wad&&!qa_json_string_equal(d,kind,"image"))||!resource_path(d,qa_json_get(d,id,shader?"name":"path"),e))return false;
    if(wad){char *lump=NULL;if(!application_mod_item_text(d,qa_json_get(d,id,"lump"),&lump,e))return false;
        size_t cursor=0,units=0;uint32_t scalar;qa_bytes bytes={(const uint8_t *)lump,strlen(lump)};
        while(qa_utf8_next(bytes,&cursor,&scalar))units+=scalar>0xffff?2u:1u;
        free(lump);if(!units||units>16)return false;
    }
    return true;
}
bool application_mod_item_definition(const qa_json_document *d,qa_json_id row,qa_strings *strings,
    qa_item_admission *out,qa_buffer *icon_bytes,qa_buffer *held_bytes,qa_json_id actions[2],qa_error *e)
{
    qa_item_definition *definition=&out->definition;
    qa_json_id kind=qa_json_get(d,row,"kind"),admission=qa_json_get(d,row,"admission");
    definition->weapon=qa_json_string_equal(d,kind,"weapon");
    out->replace_primary=qa_json_string_equal(d,admission,"replace-primary");
    if((!definition->weapon&&!qa_json_string_equal(d,kind,"counter"))||
        (!out->replace_primary&&!qa_json_string_equal(d,admission,"add"))||
        !application_mod_item_identity(d,qa_json_get(d,row,"item"),strings,&definition->item,e)||
        !application_mod_item_text(d,qa_json_get(d,row,"label"),(char **)&definition->label,e)||
        !icon(d,qa_json_get(d,row,"icon"),e)||
        (definition->weapon&&!held(d,qa_json_get(d,row,"held"),e))||
        !raw_optional(d,qa_json_get(d,row,"icon"),icon_bytes,e)||
        !raw_optional(d,qa_json_get(d,row,"held"),held_bytes,e)) return false;
    if(definition->weapon) {
        qa_json_id ammo=qa_json_get(d,row,"ammo");
        if(qa_json_type(d,ammo)!=QA_JSON_NULL&&
            !application_mod_item_identity(d,ammo,strings,&definition->ammo,e)) return false;
    }
    static const char *names[]={"use","drop"};
    for(size_t i=0;i<2;++i) {
        actions[i]=qa_json_get(d,qa_json_get(d,row,"actions"),names[i]);
        if(actions[i]!=QA_JSON_NONE) definition->actions|=UINT32_C(1)<<i;
    }
    return true;
}
bool application_mod_pickup_definition(const qa_json_document *d,qa_json_id row,qa_strings *strings,
    uint32_t *id,qa_item_id **offered,size_t *offered_count,qa_pickup_write **writes,size_t *write_count,qa_error *e)
{
    char *name=NULL;
    if(!application_mod_item_text(d,qa_json_get(d,row,"id"),&name,e))return false;
    bool ok=*name&&qa_strings_intern_cstr(strings,name,id,e);free(name);
    qa_json_id offers=qa_json_get(d,row,"offered"),stores=qa_json_get(d,row,"writes");
    if(!ok||qa_json_type(d,offers)!=QA_JSON_ARRAY||!qa_json_size(d,offers)||
        qa_json_type(d,stores)!=QA_JSON_ARRAY||!qa_json_size(d,stores))
        return application_fail(e,QA_ERROR_FORMAT,"Pickup declaration needs its identity, offered items and writes");
    *offered_count=qa_json_size(d,offers);*write_count=qa_json_size(d,stores);
    if(*offered_count>SIZE_MAX/sizeof(**offered)||*write_count>SIZE_MAX/sizeof(**writes))
        return application_fail(e,QA_ERROR_MEMORY,"Pickup declaration size overflows");
    *offered=calloc(*offered_count,sizeof(**offered));*writes=calloc(*write_count,sizeof(**writes));
    if(!*offered||!*writes)return application_fail(e,QA_ERROR_MEMORY,"Owning pickup item and resource declarations");
    for(size_t i=0;i<*offered_count;++i)
        if(!application_mod_item_identity(d,qa_json_at(d,offers,i),strings,*offered+i,e))return false;
    for(size_t i=0;i<*write_count;++i){qa_pickup_write *w=*writes+i;qa_json_id at=qa_json_at(d,stores,i),kind=qa_json_get(d,at,"kind");
        if(qa_json_string_equal(d,kind,"protection")){
            qa_json_id channel=qa_json_get(d,at,"channel");w->resource.kind=QA_PICKUP_PROTECTION;
            if(qa_json_string_equal(d,channel,"regular"))w->resource.channel=QA_PROTECTION_REGULAR;
            else if(qa_json_string_equal(d,channel,"powered"))w->resource.channel=QA_PROTECTION_POWERED;
            else return application_fail(e,QA_ERROR_FORMAT,"Unknown pickup protection channel");
        }else if(qa_json_string_equal(d,kind,"inventory")){
            qa_json_id fields=qa_json_get(d,at,"fields");w->resource.kind=QA_PICKUP_INVENTORY;
            if(qa_json_string_equal(d,fields,"count"))w->fields=QA_PICKUP_COUNT;
            else if(qa_json_string_equal(d,fields,"capacity"))w->fields=QA_PICKUP_CAPACITY;
            else if(qa_json_string_equal(d,fields,"count-and-capacity"))w->fields=QA_PICKUP_COUNT_CAPACITY;
            else return application_fail(e,QA_ERROR_FORMAT,"Unknown pickup inventory dimensions");
            if(!application_mod_item_identity(d,qa_json_get(d,at,"item"),strings,&w->resource.item,e))return false;
        }else return application_fail(e,QA_ERROR_FORMAT,"Unknown pickup resource");
    }
    return true;
}
