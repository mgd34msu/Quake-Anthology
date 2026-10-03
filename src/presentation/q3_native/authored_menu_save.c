#include "mission_hud_internal.h"
#include "qa/script_defines_save.h"

static bool integer(qa_source_save_io *io,int *v) { int32_t n=*v; if(!qa_source_save_i32(io,&n))return false; if(io->direction==QA_SOURCE_SAVE_READ)*v=n; return true; }
static bool boolean(qa_source_save_io *io,qboolean *v) { bool b=*v!=0; if(!qa_source_save_bool(io,&b))return false; if(io->direction==QA_SOURCE_SAVE_READ)*v=b?qtrue:qfalse; return true; }
static bool floats(qa_source_save_io *io,float *v,size_t n) { for(size_t i=0;i<n;++i)if(!q3nh_float(io,v+i))return false; return true; }
static bool pooled_string(qa_source_save_io *io,q3menu_context *c,const char **v,bool active)
{
    int32_t offset=-1;
    if(io->direction==QA_SOURCE_SAVE_WRITE&&*v) {
        uintptr_t p=(uintptr_t)*v,base=(uintptr_t)c->string_pool;
        if(p>=base&&p<base+sizeof(c->string_pool))offset=(int32_t)(p-base);
        else if(!**v)offset=-2; else return false;
    }
    if(!qa_source_save_i32(io,&offset)||offset< -2||offset>=(int32_t)sizeof(c->string_pool))return false;
    size_t extent=active?(size_t)c->string_pool_index:sizeof(c->string_pool);
    if(offset>=0&&((size_t)offset>=extent||!memchr(c->string_pool+offset,0,extent-(size_t)offset)))return false;
    if(io->direction==QA_SOURCE_SAVE_READ)*v=offset==-1?NULL:offset==-2?"":c->string_pool+offset;
    return true;
}
static bool string(qa_source_save_io *io,q3menu_context *c,const char **v)
{ return pooled_string(io,c,v,true); }
static bool allocation(qa_source_save_io *io,q3menu_context *c,void **v,int expected)
{
    int32_t index=-1;
    if(io->direction==QA_SOURCE_SAVE_WRITE&&*v) {
        for(uint32_t i=0;i<c->allocation_count;++i)if(*v==c->allocation.bytes+c->allocations[i].offset) { index=(int32_t)i; break; }
        if(index<0)return false;
    }
    if(!qa_source_save_i32(io,&index)||index< -1||index>=(int32_t)c->allocation_count)return false;
    if(index>=0&&expected>=0&&c->allocations[index].kind!=(q3menu_allocation_kind)expected)return false;
    if(io->direction==QA_SOURCE_SAVE_READ)*v=index<0?NULL:c->allocation.bytes+c->allocations[index].offset;
    return true;
}
/* String_Init rewinds the pool; cached asset names retain its old bytes. */
bool q3menu_save_string(qa_source_save_io *io,q3menu_context *c,const char **v)
{ return pooled_string(io,c,v,false); }
static bool menu_ref(qa_source_save_io *io,q3menu_context *c,menuDef_t **v)
{
    int32_t index=-1;
    if(io->direction==QA_SOURCE_SAVE_WRITE&&*v) { for(int i=0;i<MAX_MENUS;++i)if(*v==c->menus+i) { index=i; break; } if(index<0)return false; }
    if(!qa_source_save_i32(io,&index)||index< -1||index>=MAX_MENUS)return false;
    if(io->direction==QA_SOURCE_SAVE_READ)*v=index<0?NULL:c->menus+index;
    return true;
}
static bool handle(qa_source_save_io *io,int *v,const qa_q3_presentation_assets *a,q3p_resource_kind kind)
{ return integer(io,v)&&q3nh_handle(a,*v,kind); }
static bool window(qa_source_save_io *io,q3menu_context *c,windowDef_t *w,const qa_q3_presentation_assets *a)
{
    return floats(io,&w->rect.x,4)&&floats(io,&w->rectClient.x,4)&&string(io,c,&w->name)&&string(io,c,&w->group)&&
        string(io,c,&w->cinematicName)&&integer(io,&w->cinematic)&&w->cinematic>=-2&&w->cinematic<16&&
        integer(io,&w->style)&&integer(io,&w->border)&&integer(io,&w->ownerDraw)&&integer(io,&w->ownerDrawFlags)&&
        q3nh_float(io,&w->borderSize)&&integer(io,&w->flags)&&floats(io,&w->rectEffects.x,4)&&
        floats(io,&w->rectEffects2.x,4)&&integer(io,&w->offsetTime)&&integer(io,&w->nextTime)&&floats(io,w->foreColor,4)&&
        floats(io,w->backColor,4)&&floats(io,w->borderColor,4)&&floats(io,w->outlineColor,4)&&handle(io,&w->background,a,Q3P_SHADER);
}
static bool item(qa_source_save_io *io,q3menu_context *c,itemDef_t *v,const qa_q3_presentation_assets *a)
{
    menuDef_t *parent=v->parent;
    if(!window(io,c,&v->window,a)||!floats(io,&v->textRect.x,4)||!integer(io,&v->type)||v->type<0||v->type>ITEM_TYPE_BIND||
        !integer(io,&v->alignment)||!integer(io,&v->textalignment)||!q3nh_float(io,&v->textalignx)||!q3nh_float(io,&v->textaligny)||
        !q3nh_float(io,&v->textscale)||!integer(io,&v->textStyle)||!string(io,c,&v->text)||!menu_ref(io,c,&parent)||
        !handle(io,&v->asset,a,v->type==ITEM_TYPE_MODEL?Q3P_MODEL:Q3P_SHADER)||!string(io,c,&v->mouseEnterText)||
        !string(io,c,&v->mouseExitText)||!string(io,c,&v->mouseEnter)||!string(io,c,&v->mouseExit)||!string(io,c,&v->action)||
        !string(io,c,&v->onFocus)||!string(io,c,&v->leaveFocus)||!string(io,c,&v->cvar)||!string(io,c,&v->cvarTest)||
        !string(io,c,&v->enableCvar)||!integer(io,&v->cvarFlags)||!handle(io,&v->focusSound,a,Q3P_SOUND)||
        !integer(io,&v->numColors)||v->numColors<0||v->numColors>MAX_COLOR_RANGES)return false;
    if(io->direction==QA_SOURCE_SAVE_READ)v->parent=parent;
    for(unsigned i=0;i<MAX_COLOR_RANGES;++i)if(!floats(io,v->colorRanges[i].color,4)||!q3nh_float(io,&v->colorRanges[i].low)||!q3nh_float(io,&v->colorRanges[i].high))return false;
    int data_kind=v->type==ITEM_TYPE_LISTBOX?Q3MENU_LIST:v->type==ITEM_TYPE_MULTI?Q3MENU_MULTI:
        v->type==ITEM_TYPE_MODEL?Q3MENU_MODEL:
        v->type==ITEM_TYPE_TEXT||v->type==ITEM_TYPE_EDITFIELD||v->type==ITEM_TYPE_NUMERICFIELD||
        v->type==ITEM_TYPE_YESNO||v->type==ITEM_TYPE_BIND||v->type==ITEM_TYPE_SLIDER?Q3MENU_EDIT:-1;
    return q3nh_float(io,&v->special)&&integer(io,&v->cursorPos)&&allocation(io,c,&v->typeData,data_kind);
}
static bool menu(qa_source_save_io *io,q3menu_context *c,menuDef_t *m,const qa_q3_presentation_assets *a)
{
    if(!window(io,c,&m->window,a)||!string(io,c,&m->font)||!boolean(io,&m->fullScreen)||!integer(io,&m->itemCount)||m->itemCount<0||m->itemCount>MAX_MENUITEMS||
        !integer(io,&m->fontIndex)||!integer(io,&m->cursorItem)||!integer(io,&m->fadeCycle)||!q3nh_float(io,&m->fadeClamp)||
        !q3nh_float(io,&m->fadeAmount)||!string(io,c,&m->onOpen)||!string(io,c,&m->onClose)||!string(io,c,&m->onESC)||
        !string(io,c,&m->soundName)||!floats(io,m->focusColor,4)||!floats(io,m->disableColor,4))return false;
    for(unsigned i=0;i<MAX_MENUITEMS;++i) { void *p=m->items[i]; if(!allocation(io,c,&p,Q3MENU_ITEM))return false; if(io->direction==QA_SOURCE_SAVE_READ)m->items[i]=p; }
    for(int i=0;i<m->itemCount;++i)if(!m->items[i]||m->items[i]->parent!=m)return false;
    return true;
}
static bool record(qa_source_save_io *io,q3menu_context *c,q3menu_allocation *r,const qa_q3_presentation_assets *a)
{
    void *p=c->allocation.bytes+r->offset;
    switch(r->kind) {
    case Q3MENU_STRING:{ stringDef_t *v=p; void *next=v->next; if(!allocation(io,c,&next,Q3MENU_STRING)||!string(io,c,&v->str))return false; if(io->direction==QA_SOURCE_SAVE_READ)v->next=next; return v->str!=NULL; }
    case Q3MENU_ITEM:return item(io,c,p,a);
    case Q3MENU_LIST:{ listBoxDef_t *v=p;
        if(!integer(io,&v->startPos)||!integer(io,&v->endPos)||!integer(io,&v->drawPadding)||!integer(io,&v->cursorPos)||
            !q3nh_float(io,&v->elementWidth)||!q3nh_float(io,&v->elementHeight)||!integer(io,&v->elementStyle)||
            !integer(io,&v->numColumns)||v->numColumns<0||v->numColumns>MAX_LB_COLUMNS)return false;
        for(unsigned i=0;i<MAX_LB_COLUMNS;++i)if(!integer(io,&v->columnInfo[i].pos)||!integer(io,&v->columnInfo[i].width)||!integer(io,&v->columnInfo[i].maxChars))return false;
        return string(io,c,&v->doubleClick)&&boolean(io,&v->notselectable); }
    case Q3MENU_EDIT:{ editFieldDef_t *v=p; return q3nh_float(io,&v->minVal)&&q3nh_float(io,&v->maxVal)&&q3nh_float(io,&v->defVal)&&
        q3nh_float(io,&v->range)&&integer(io,&v->maxChars)&&integer(io,&v->maxPaintChars)&&integer(io,&v->paintOffset); }
    case Q3MENU_MULTI:{ multiDef_t *v=p; for(unsigned i=0;i<MAX_MULTI_CVARS;++i)if(!string(io,c,&v->cvarList[i])||!string(io,c,&v->cvarStr[i])||!q3nh_float(io,&v->cvarValue[i]))return false;
        return integer(io,&v->count)&&v->count>=0&&v->count<=MAX_MULTI_CVARS&&boolean(io,&v->strDef); }
    case Q3MENU_MODEL:{ modelDef_t *v=p; return integer(io,&v->angle)&&floats(io,v->origin,3)&&q3nh_float(io,&v->fov_x)&&q3nh_float(io,&v->fov_y)&&integer(io,&v->rotationSpeed); }
    }
    return false;
}
static bool blob(qa_source_save_io *io,qa_buffer *b,size_t max)
{
    size_t size=b->size; if(!qa_source_save_count(io,&size,max))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { if(size>io->input.size-io->offset)return false; b->data=size?malloc(size):NULL; b->size=size; if(size&&!b->data)return false; }
    return qa_source_save_bytes(io,b->data,size);
}
static bool fields(qa_source_save_io *io,q3menu_context *c)
{
    q3n_mission_hud *owner=c->owner; const qa_q3_presentation_assets *a=owner->options.assets;
    uint8_t magic[4]={'Q','3','M','N'}; if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,"Q3MN",4)||!integer(io,&c->alloc_point)||c->alloc_point<0||c->alloc_point>128*1024||!integer(io,&c->out_of_memory)||
        !integer(io,&c->string_pool_index)||c->string_pool_index<0||c->string_pool_index>128*1024||
        !integer(io,&c->string_handle_count)||!qa_source_save_bytes(io,c->string_pool,sizeof(c->string_pool)))return false;
    uint32_t count=c->allocation_count; if(!qa_source_save_u32(io,&count)||count>8192)return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { c->allocation_count=count; c->native_alloc_point=0; }
    static const int sizes[]={sizeof(stringDef_t),sizeof(itemDef_t),sizeof(listBoxDef_t),sizeof(editFieldDef_t),sizeof(multiDef_t),sizeof(modelDef_t)};
    static const uint32_t source_sizes[]={8,540,232,28,392,28}; uint32_t charge=0;
    for(uint32_t i=0;i<count;++i) { q3menu_allocation *r=&c->allocations[i]; uint32_t kind=r->kind;
        if(!qa_source_save_u32(io,&kind)||kind>Q3MENU_MODEL||!qa_source_save_u32(io,&r->source_size)||r->source_size!=source_sizes[kind])return false;
        charge+=(r->source_size+15)&~15u;
        if(io->direction==QA_SOURCE_SAVE_READ) { r->kind=(q3menu_allocation_kind)kind; r->offset=(uint32_t)c->native_alloc_point;
            c->native_alloc_point+=(sizes[kind]+15)&~15; if(c->native_alloc_point>(int)sizeof(c->allocation.bytes))return false; }
    }
    if(charge!=(uint32_t)c->alloc_point)return false;
    for(uint32_t i=0;i<count;++i)if(!record(io,c,&c->allocations[i],a))return false;
    for(unsigned i=0;i<2048;++i) { void *p=c->string_handles[i]; if(!allocation(io,c,&p,Q3MENU_STRING))return false; if(io->direction==QA_SOURCE_SAVE_READ)c->string_handles[i]=p;
        uint32_t traversed=0; for(stringDef_t *node=p;node;node=node->next)if(++traversed>count)return false; }
    if(!integer(io,&c->menu_count)||c->menu_count<0||c->menu_count>MAX_MENUS||!integer(io,&c->open_menu_count)||c->open_menu_count<0||c->open_menu_count>MAX_OPEN_MENUS)return false;
    for(unsigned i=0;i<MAX_MENUS;++i)if(!menu(io,c,c->menus+i,a))return false;
    for(unsigned i=0;i<MAX_OPEN_MENUS;++i)if(!menu_ref(io,c,&c->menu_stack[i]))return false;
    void *capture=c->item_capture,*bind=c->bind_item,*edit=c->edit_item,*scroll=c->scroll.item;
    if(!allocation(io,c,&capture,Q3MENU_ITEM)||!allocation(io,c,&bind,Q3MENU_ITEM)||!allocation(io,c,&edit,Q3MENU_ITEM)||!allocation(io,c,&scroll,Q3MENU_ITEM))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { c->item_capture=capture; c->bind_item=bind; c->edit_item=edit; c->scroll.item=scroll; }
    int kind=q3menu_capture_kind(c); bool capture_data=c->capture_data!=NULL;
    if(!integer(io,&kind)||kind<0||kind>3||!qa_source_save_bool(io,&capture_data)||
        (io->direction==QA_SOURCE_SAVE_WRITE&&capture_data&&c->capture_data!=&c->scroll)||
        !integer(io,&c->scroll.nextScrollTime)||!integer(io,&c->scroll.nextAdjustTime)||!integer(io,&c->scroll.adjustValue)||
        !integer(io,&c->scroll.scrollKey)||!q3nh_float(io,&c->scroll.xStart)||!q3nh_float(io,&c->scroll.yStart)||
        !boolean(io,&c->scroll.scrollDir)||!boolean(io,&c->waiting_for_key)||!boolean(io,&c->editing_field)||
        !boolean(io,&c->debug)||!integer(io,&c->last_list_click_time)||!floats(io,&c->corrected_text_rect.x,4))return false;
    if(kind&&(!scroll||!capture||!capture_data))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { q3menu_capture_restore(c,kind); c->capture_data=capture_data?&c->scroll:NULL; }
    for(unsigned i=0;i<64;++i)if(!integer(io,&c->bindings[i].bind1)||!integer(io,&c->bindings[i].bind2))return false;
    if(!qa_source_save_bytes(io,c->binding_names,sizeof(c->binding_names))||!qa_source_save_bytes(io,c->format_buffers,sizeof(c->format_buffers))||
        !qa_source_save_u32(io,&c->format_cursor))return false;
    for(unsigned i=0;i<2;++i)if(!memchr(c->binding_names[i],0,32))return false;
    for(unsigned i=0;i<8;++i)if(!memchr(c->format_buffers[i],0,4096))return false;
    if(!qa_source_save_bytes(io,c->common_parser.token,sizeof(c->common_parser.token))||
        !qa_source_save_count(io,&c->common_parser.token_length,1024)||!qa_source_save_bytes(io,c->common_parser.name,sizeof(c->common_parser.name))||
        !qa_source_save_count(io,&c->common_parser.name_length,1023)||!qa_source_save_i32(io,&c->common_parser.line)||c->common_parser.line<0)return false;
    if(c->common_parser.token_length>=sizeof(c->common_parser.token)||c->common_parser.token[c->common_parser.token_length]||
        c->common_parser.name[c->common_parser.name_length])return false;
    if(!qa_source_save_bytes(io,c->script_date,sizeof(c->script_date))||!qa_source_save_bytes(io,c->script_time,sizeof(c->script_time))||
        !memchr(c->script_date,0,sizeof(c->script_date))||!memchr(c->script_time,0,sizeof(c->script_time)))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { c->scripts.date=c->script_date; c->scripts.time=c->script_time; }
    qa_buffer definitions={0}; bool ok=true;
    if(io->direction==QA_SOURCE_SAVE_WRITE)ok=qa_script_defines_save_capture(c->global_defines,&definitions,io->error);
    if(ok)ok=blob(io,&definitions,SIZE_MAX);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) { qa_script_defines *restored=NULL; ok=qa_script_defines_save_restore((qa_bytes){definitions.data,definitions.size},&restored,io->error);
        if(ok) { qa_script_defines_release(c->global_defines); c->global_defines=restored; c->script_options.globals=restored; } }
    qa_buffer_free(&definitions); if(!ok)return false;
    for(unsigned i=0;i<64;++i) { bool present=c->sources[i]!=NULL; if(!qa_source_save_bool(io,&present))return false; if(!present)continue;
        qa_script_checkpoint checkpoint={0}; qa_buffer bytes={0};
        if(io->direction==QA_SOURCE_SAVE_WRITE)ok=qa_script_capture(c->sources[i],&checkpoint,io->error)&&qa_script_checkpoint_encode(&checkpoint,&bytes,io->error);
        if(ok)ok=blob(io,&bytes,SIZE_MAX);
        if(ok&&io->direction==QA_SOURCE_SAVE_READ)ok=qa_script_checkpoint_decode((qa_bytes){bytes.data,bytes.size},&checkpoint,io->error)&&qa_script_restore(&c->scripts,&checkpoint,&c->sources[i],io->error);
        qa_script_checkpoint_free(&checkpoint); qa_buffer_free(&bytes); if(!ok)return false;
    }
    return true;
}
bool q3menu_checkpoint(const q3menu_context *borrowed,qa_buffer *out,qa_error *e)
{
    if(!borrowed||!out||out->data||out->size||borrowed->in_handle_key||borrowed->allocation_guard)return false;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e)&&fields(&io,(q3menu_context *)borrowed)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool q3menu_restore(const q3menu_context *basis,qa_bytes bytes,q3menu_context **out,qa_error *e)
{
    if(!basis||!out||*out)return false;
    q3menu_context *c=q3menu_create(basis->display,basis->owner,e); if(!c)return false;
    c->scripts=basis->scripts; c->script_options=basis->script_options; c->random_integer=basis->random_integer;
    c->global_defines=basis->global_defines; qa_script_defines_retain(c->global_defines);
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,NULL,bytes,e)&&fields(&io,c)&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); if(ok)*out=c; else q3menu_destroy(c); return ok;
}
