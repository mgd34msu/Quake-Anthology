#include "startup_server_browser.h"
#include "menu_fonts.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include "qa/ui_preferences.h"
#include <math.h>
#include <stdio.h>
#include <unicode/ucol.h>

typedef enum browser_family { BROWSER_Q1, BROWSER_QW, BROWSER_Q2, BROWSER_Q3, BROWSER_FAMILIES } browser_family;
typedef enum browser_sort {
    BROWSER_PING_LOW, BROWSER_PING_HIGH, BROWSER_NAME_AZ, BROWSER_NAME_ZA,
    BROWSER_MAP_AZ, BROWSER_MAP_ZA, BROWSER_PLAYERS_MOST, BROWSER_PLAYERS_FEWEST, BROWSER_SORTS
} browser_sort;
typedef struct browser_draft {
    char addresses[BROWSER_FAMILIES][256], masters[BROWSER_FAMILIES][2049];
    char filter[256], selected_remote[256], status[512];
    qa_net_protocol_id selected_protocol;
    qa_net_address selected_address;
    uint32_t family, sort;
    size_t page, details_page;
    bool favorites_only, hide_empty, hide_full, selected, defaults_loaded;
} browser_draft;
typedef struct browser_row {
    qa_server_entry entry;
    char address[256];
    qa_buffer search;
    bool has_ping;
} browser_row;
typedef struct browser_menu_context {
    frontend_startup_server_browser *owner;
    qa_ui_id id;
} browser_menu_context;
struct frontend_startup_server_browser {
    frontend_seat *seat;
    qa_ui *ui;
    qa_input_seat *input;
    qa_seat_console *console;
    frontend_startup_server_browser_menus menus;
    browser_menu_context contexts[3];
    UCollator *base, *standard;
    browser_draft draft;
    browser_row *rows;
    size_t row_count, row_capacity, unfiltered_count, filtered_count;
    size_t *order, *scratch;
    qa_ui_control controls[20];
    char labels[20][2304];
    qa_arena measurements;
    char **details;
    size_t detail_count, detail_capacity;
    uint32_t registered;
    bool busy, retiring;
    const void *status_network;
    uint64_t status_receipt;
};
static bool fail(qa_error *e,qa_status code,const char *message)
{ frontend_fail(e,code,message); return false; }
static double now(const frontend_startup_server_browser *o)
{ return (double)o->seat->frontend->time_ns/1000000.0; }
static bool bound(const frontend_startup_server_browser *o)
{
    const frontend_seat *s=o?o->seat:NULL; const qa_frontend *f=s?s->frontend:NULL;
    return f && f->seats && s->id<f->options.seats && s==f->seats+s->id &&
        s->ui==o->ui && s->input==o->input && s->console==o->console;
}
static bool read(frontend_startup_server_browser *o,frontend_network_menu_view *out,qa_error *e)
{
    return (bound(o) && !o->retiring && frontend_network_menu_read(o->seat->frontend,o->seat->id,out,e) &&
        out->input==o->input && out->console==o->console) ||
        fail(e,QA_ERROR_ARGUMENT,"Server browser lost its actual physical seat and Network owner");
}
static qa_net_protocol_id family_protocol(uint32_t family)
{
    static const qa_net_protocol kinds[BROWSER_FAMILIES]={QA_NET_NQ15,QA_NET_QW28,QA_NET_Q2_34,QA_NET_Q3_68};
    return (qa_net_protocol_id){.kind=kinds[family]};
}
static bool same_protocol(qa_net_protocol_id a,qa_net_protocol_id b)
{ return a.kind==b.kind && a.revision==b.revision && a.flags==b.flags; }
static bool selected(const browser_draft *draft,const browser_row *row)
{
    return draft->selected && qa_net_address_equal(&draft->selected_address,&row->entry.address,true);
}
static bool load_defaults(frontend_startup_server_browser *o,const frontend_network_menu_view *view,qa_error *e)
{
    if(o->draft.defaults_loaded)return true;
    browser_draft draft=o->draft;
    for(unsigned i=0;i<BROWSER_FAMILIES;++i) {
        frontend_network_menu_preferences preferences;
        if(!frontend_network_menu_preferences_read(o->seat->frontend,view,family_protocol(i),&preferences,e))return false;
        if(preferences.direct_count>16)return fail(e,QA_ERROR_FORMAT,"Server browser preferences lost their actual direct extent");
        memcpy(draft.masters[i],preferences.master,sizeof(draft.masters[i]));
        if(preferences.direct_count)memcpy(draft.addresses[i],preferences.direct[0].remote,sizeof(draft.addresses[i]));
    }
    draft.defaults_loaded=true; o->draft=draft; return true;
}
static bool collate(UCollator *collator,const char *a,const char *b,int *out,qa_error *e)
{
    UErrorCode status=U_ZERO_ERROR;
    UCollationResult value=ucol_strcollUTF8(collator,a,-1,b,-1,&status);
    if(U_FAILURE(status)) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Collating server text: %s",u_errorName(status)); return false;
    }
    *out=value<0?-1:value>0; return true;
}
static bool compare(frontend_startup_server_browser *o,size_t left,size_t right,int *out,qa_error *e)
{
    const browser_row *a=o->rows+left,*b=o->rows+right; int value=0;
    if(a->entry.available!=b->entry.available) { *out=a->entry.available?-1:1; return true; }
    switch(o->draft.sort) {
    case BROWSER_PING_LOW: case BROWSER_PING_HIGH:
        if(a->has_ping!=b->has_ping) { *out=a->has_ping?-1:1; return true; }
        value=a->entry.ping_ns<b->entry.ping_ns?-1:a->entry.ping_ns>b->entry.ping_ns;
        if(o->draft.sort==BROWSER_PING_HIGH)value=-value;
        break;
    case BROWSER_NAME_AZ: case BROWSER_NAME_ZA:
        if(!collate(o->base,a->entry.available?a->entry.name:"",b->entry.available?b->entry.name:"",&value,e))return false;
        if(o->draft.sort==BROWSER_NAME_ZA)value=-value;
        break;
    case BROWSER_MAP_AZ: case BROWSER_MAP_ZA:
        if(!collate(o->base,a->entry.available?a->entry.map:"",b->entry.available?b->entry.map:"",&value,e))return false;
        if(o->draft.sort==BROWSER_MAP_ZA)value=-value;
        break;
    case BROWSER_PLAYERS_MOST: case BROWSER_PLAYERS_FEWEST:
        { uint32_t left_players=a->entry.available?a->entry.players:0,right_players=b->entry.available?b->entry.players:0;
        value=left_players<right_players?-1:left_players>right_players; }
        if(o->draft.sort==BROWSER_PLAYERS_MOST)value=-value;
        break;
    default:return fail(e,QA_ERROR_FORMAT,"Server browser has an invalid retained sort choice");
    }
    if(!value && !collate(o->standard,a->address,b->address,&value,e))return false;
    *out=value; return true;
}
static bool sort_rows(frontend_startup_server_browser *o,size_t count,qa_error *e)
{
    for(size_t width=1;width<count;) {
        for(size_t start=0;start<count;) {
            size_t middle=count-start<width?count:start+width;
            size_t end=count-middle<width?count:middle+width;
            size_t a=start,b=middle,target=start;
            while(a<middle && b<end) {
                int compared;
                if(!compare(o,o->order[a],o->order[b],&compared,e))return false;
                o->scratch[target++]=compared<=0?o->order[a++]:o->order[b++];
            }
            while(a<middle)o->scratch[target++]=o->order[a++];
            while(b<end)o->scratch[target++]=o->order[b++];
            start=end;
        }
        memcpy(o->order,o->scratch,count*sizeof(*o->order));
        if(width>count/2)break;
        width*=2;
    }
    return true;
}
static bool trim_lower(const char *text,qa_buffer *out,qa_error *e)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)}; size_t cursor=0,begin=bytes.size,end=0; uint32_t code;
    while(cursor<bytes.size) {
        size_t before=cursor;
        if(!qa_utf8_next(bytes,&cursor,&code))break;
        if(!qa_unicode_whitespace(code)) { if(begin==bytes.size)begin=before; end=cursor; }
    }
    if(begin==bytes.size)begin=end=0;
    return qa_utf8_lower((qa_bytes){bytes.data+begin,end-begin},out,e);
}
static bool contains(qa_bytes text,qa_bytes query)
{
    if(!query.size)return true;
    if(query.size>text.size)return false;
    for(size_t i=0;i<=text.size-query.size;++i)
        if(!memcmp(text.data+i,query.data,query.size))return true;
    return false;
}
static bool included(const browser_draft *draft,const browser_row *row,qa_bytes filter)
{
    const qa_server_entry *entry=&row->entry;
    return (!draft->favorites_only || (entry->sources&QA_SERVER_FAVORITE)) &&
        (!entry->available || !entry->maximum_players ||
            ((!draft->hide_empty || entry->players>0) && (!draft->hide_full || entry->players<entry->maximum_players))) &&
        contains((qa_bytes){row->search.data,row->search.size},filter);
}
static bool draft_text(qa_source_save_io *io,char *text,size_t capacity)
{
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(text):0;
    if(!qa_source_save_count(io,&length,capacity-1) || !qa_source_save_bytes(io,text,length))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(memchr(text,0,length))return false;
        text[length]=0;
    }
    return qa_utf8_valid((qa_bytes){(const uint8_t *)text,length});
}
static bool draft_fields(qa_source_save_io *io,const frontend_startup_server_browser *o,browser_draft *draft)
{
    uint8_t magic[4]={'Q','S','B','R'}; uint32_t physical=o->seat->id;
    uint64_t menus[3]={o->menus.browser,o->menus.options,o->menus.details};
    if(!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QSBR",sizeof(magic)) ||
       !qa_source_save_u32(io,&physical) || physical!=o->seat->id)return false;
    const qa_ui_id ids[3]={o->menus.browser,o->menus.options,o->menus.details};
    for(unsigned i=0;i<3;++i)if(!qa_source_save_u64(io,menus+i) || menus[i]!=ids[i])return false;
    if(!qa_source_save_u32(io,&draft->family) || draft->family>=BROWSER_FAMILIES ||
       !qa_source_save_u32(io,&draft->sort) || draft->sort>=BROWSER_SORTS ||
       !qa_source_save_count(io,&draft->page,SIZE_MAX) || !qa_source_save_count(io,&draft->details_page,SIZE_MAX) ||
       !qa_source_save_bool(io,&draft->favorites_only) || !qa_source_save_bool(io,&draft->hide_empty) ||
       !qa_source_save_bool(io,&draft->hide_full) || !qa_source_save_bool(io,&draft->selected) ||
       !qa_source_save_bool(io,&draft->defaults_loaded))return false;
    for(unsigned i=0;i<BROWSER_FAMILIES;++i)
        if(!draft_text(io,draft->addresses[i],sizeof(draft->addresses[i])) ||
           !draft_text(io,draft->masters[i],sizeof(draft->masters[i])))return false;
    if(!draft_text(io,draft->filter,sizeof(draft->filter)) ||
       !draft_text(io,draft->selected_remote,sizeof(draft->selected_remote)) ||
       !draft_text(io,draft->status,sizeof(draft->status)))return false;
    if(!draft->selected)return true;
    uint32_t kind=draft->selected_protocol.kind,address_kind=draft->selected_address.kind;
    if(!qa_source_save_u32(io,&kind) || kind>QA_NET_UNIFIED_1 ||
       !qa_source_save_u32(io,&draft->selected_protocol.revision) ||
       !qa_source_save_u32(io,&draft->selected_protocol.flags) ||
       !qa_source_save_u32(io,&address_kind) || address_kind>QA_NET_IPV6 ||
       !qa_source_save_u16(io,&draft->selected_address.port) || !draft->selected_address.port)return false;
    draft->selected_protocol.kind=(qa_net_protocol)kind;
    draft->selected_address.kind=(qa_net_address_kind)address_kind;
    if(address_kind==QA_NET_IPV4) {
        if(!qa_source_save_bytes(io,draft->selected_address.host.ipv4,4))return false;
    } else if(!qa_source_save_bytes(io,draft->selected_address.host.ipv6.bytes,16) ||
        !qa_source_save_u32(io,&draft->selected_address.host.ipv6.scope))return false;
    qa_net_protocol_id family=family_protocol(draft->family);
    return qa_net_protocol_valid(draft->selected_protocol,io->error) &&
        (same_protocol(family,draft->selected_protocol) ||
         (draft->family==BROWSER_Q2 && draft->selected_protocol.kind==QA_NET_Q2KEX_2023 &&
          !draft->selected_protocol.revision && !draft->selected_protocol.flags));
}
bool frontend_startup_server_browser_idle(const frontend_startup_server_browser *o)
{ return !o || !o->busy; }
bool frontend_startup_server_browser_checkpoint(const frontend_startup_server_browser *o,qa_buffer *out,qa_error *e)
{
    if(!bound(o) || o->busy || o->retiring || !out || out->data || out->size || o->registered!=3)
        return fail(e,QA_ERROR_ARGUMENT,"Server browser capture requires its returned registered physical child");
    browser_draft copy=o->draft; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && draft_fields(&io,o,&copy) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_startup_server_browser_restore(frontend_startup_server_browser *o,qa_bytes bytes,qa_error *e)
{
    if(!bound(o) || o->busy || o->retiring || o->registered!=3 ||
       !o->seat->frontend->source_restoring || o->seat->frontend->capture)
        return fail(e,QA_ERROR_ARGUMENT,"Server browser import requires its actual returned restoring seat");
    browser_draft draft={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && draft_fields(&io,o,&draft) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay)o->draft=draft;
    else if(!e || e->code==QA_OK)fail(e,QA_ERROR_FORMAT,"Invalid server browser private draft continuation");
    return okay;
}
static bool browser_factory(void *,uint32_t,qa_ui_menu *,qa_error *);
static bool options_factory(void *,uint32_t,qa_ui_menu *,qa_error *);
static bool details_factory(void *,uint32_t,qa_ui_menu *,qa_error *);
static void clear_rows(frontend_startup_server_browser *o)
{
    for(size_t i=0;i<o->row_count;++i)qa_buffer_free(&o->rows[i].search);
    o->row_count=0;
}
static void clear_details(frontend_startup_server_browser *o)
{
    for(size_t i=0;i<o->detail_count;++i)free(o->details[i]);
    free(o->details); o->details=NULL; o->detail_count=o->detail_capacity=0;
}
bool frontend_startup_server_browser_destroy(frontend_startup_server_browser **out,qa_error *e)
{
    if(!out)return fail(e,QA_ERROR_ARGUMENT,"Server browser retirement requires its retained child slot");
    frontend_startup_server_browser *o=*out;
    if(!o)return true;
    if(!bound(o) || o->busy || !qa_ui_idle(o->ui))
        return fail(e,QA_ERROR_ARGUMENT,"Server browser retirement requires its returned physical UI owner");
    o->retiring=true;
    while(o->registered) {
        qa_ui_id id=o->contexts[o->registered-1].id;
        if(!qa_ui_unregister(o->ui,id,now(o),e))return false;
        --o->registered;
    }
    clear_rows(o); clear_details(o);
    free(o->rows); free(o->order); free(o->scratch);
    qa_arena_destroy(&o->measurements);
    if(o->base)ucol_close(o->base);
    if(o->standard)ucol_close(o->standard);
    free(o); *out=NULL; return true;
}
bool frontend_startup_server_browser_create(frontend_seat *seat,
    const frontend_startup_server_browser_menus *menus,frontend_startup_server_browser **out,qa_error *e)
{
    qa_frontend *f=seat?seat->frontend:NULL;
    if(!f || !f->seats || seat->id>=f->options.seats || seat!=f->seats+seat->id ||
       !seat->ui || !seat->input || !seat->console || !qa_ui_idle(seat->ui) || !out || *out || !menus ||
       !menus->browser || !menus->options || !menus->details || menus->browser==menus->options ||
       menus->browser==menus->details || menus->options==menus->details)
        return fail(e,QA_ERROR_ARGUMENT,"Server browser requires its actual physical UI and distinct reserved menu IDs");
    frontend_startup_server_browser *o=calloc(1,sizeof(*o));
    if(!o)return fail(e,QA_ERROR_MEMORY,"Retaining the physical startup server browser");
    o->seat=seat; o->ui=seat->ui; o->input=seat->input; o->console=seat->console; o->menus=*menus;
    qa_arena_init(&o->measurements,4096);
    *out=o;
    UErrorCode status=U_ZERO_ERROR; o->base=ucol_open(NULL,&status);
    if(U_SUCCESS(status) && o->base) {
        ucol_setStrength(o->base,UCOL_PRIMARY); status=U_ZERO_ERROR;
        o->standard=ucol_open(NULL,&status);
    }
    if(U_FAILURE(status) || !o->base || !o->standard) {
        qa_error_set(e,QA_ERROR_MEMORY,0,"Opening server browser collation: %s",u_errorName(status)); return false;
    }
    static const unsigned ports[BROWSER_FAMILIES]={26000,27500,27910,27960};
    for(unsigned i=0;i<BROWSER_FAMILIES;++i)
        snprintf(o->draft.addresses[i],sizeof(o->draft.addresses[i]),"localhost:%u",ports[i]);
    const qa_ui_id ids[3]={menus->browser,menus->options,menus->details};
    const qa_ui_menu_factory factories[3]={browser_factory,options_factory,details_factory};
    for(unsigned i=0;i<3;++i) {
        o->contexts[i]=(browser_menu_context){.owner=o,.id=ids[i]};
        qa_ui_menu_registration registration={.id=ids[i],.context=o->contexts+i,.factory=factories[i]};
        if(!qa_ui_register(o->ui,&registration,e))return false;
        ++o->registered;
    }
    return true;
}
bool frontend_startup_server_browser_open(frontend_startup_server_browser *o,qa_error *e)
{
    if(!bound(o) || o->busy || o->retiring || o->registered!=3)
        return fail(e,QA_ERROR_ARGUMENT,"Server browser opening requires its returned registered physical child");
    return qa_ui_open(o->ui,o->menus.browser,now(o),e);
}
static bool refresh(frontend_startup_server_browser *,const frontend_network_menu_view *,qa_error *);
static bool build_details(frontend_startup_server_browser *,const frontend_network_menu_view *,qa_error *);
static bool select_row(frontend_startup_server_browser *,const frontend_network_menu_view *,size_t,qa_error *);
static bool append_detail(frontend_startup_server_browser *o,const char *text,size_t length,qa_error *e)
{
    if(o->detail_count==o->detail_capacity) {
        size_t capacity=o->detail_capacity?o->detail_capacity*2:16;
        if(capacity<o->detail_capacity || capacity>SIZE_MAX/sizeof(*o->details))
            return fail(e,QA_ERROR_MEMORY,"Server detail rows exceed their retained extent");
        char **rows=realloc(o->details,capacity*sizeof(*rows));
        if(!rows)return fail(e,QA_ERROR_MEMORY,"Retaining server detail rows");
        o->details=rows; o->detail_capacity=capacity;
    }
    char *copy=malloc(length+1);
    if(!copy)return fail(e,QA_ERROR_MEMORY,"Retaining an actual server detail line");
    if(length)memcpy(copy,text,length);
    copy[length]=0; o->details[o->detail_count++]=copy; return true;
}
static bool detail_line(frontend_startup_server_browser *o,const char *text,qa_error *e)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)}; size_t cursor=0,used=0,units=0;
    char line[257]; uint32_t code;
    while(qa_utf8_next(bytes,&cursor,&code)) {
        if(code<32 || code==127)code=' ';
        size_t amount=code>0xffff?2:1;
        if(amount==2 && units==63) {
            used+=qa_utf8_encode(0xfffd,line+used);
            if(!append_detail(o,line,used,e))return false;
            used=qa_utf8_encode(0xfffd,line); units=1; continue;
        }
        if(units+amount>64) {
            if(!append_detail(o,line,used,e))return false;
            used=units=0;
        }
        used+=qa_utf8_encode(code,line+used); units+=amount;
    }
    return (!used && bytes.size) || append_detail(o,line,used,e);
}
static bool bytes_add(qa_buffer *out,qa_bytes bytes,bool strip_colors,qa_error *e)
{
    if(bytes.size>SIZE_MAX-out->size-2)return fail(e,QA_ERROR_MEMORY,"Server text exceeds its retained extent");
    uint8_t *data=realloc(out->data,out->size+bytes.size+2);
    if(!data)return fail(e,QA_ERROR_MEMORY,"Retaining actual server text");
    out->data=data;
    for(size_t i=0;i<bytes.size;++i) {
        if(strip_colors && bytes.data[i]=='^' && i+1<bytes.size && bytes.data[i+1]>='0' && bytes.data[i+1]<='9') { ++i; continue; }
        data[out->size++]=bytes.data[i];
    }
    data[out->size++]=' '; data[out->size]=0; return true;
}
static bool text_add(qa_buffer *out,const char *text,qa_error *e)
{ return bytes_add(out,(qa_bytes){(const uint8_t *)text,strlen(text)},false,e); }
static bool key_is(const qa_buffer *key,const char *name)
{ size_t size=strlen(name); return key->size==size && !memcmp(key->data,name,size); }
static bool row_search(frontend_startup_server_browser *o,const frontend_network_menu_view *view,
    browser_row *row,qa_error *e)
{
    qa_buffer raw={0}; qa_server_browser_details details={0}; bool okay=true;
    if(row->entry.available) {
        okay=frontend_network_menu_details_read(o->seat->frontend,view,&row->entry,&details,e);
        if(okay)okay=text_add(&raw,row->entry.name,e) && text_add(&raw,row->entry.map,e);
        const qa_buffer *game=NULL;
        for(size_t i=0;okay && i<details.rule_count;++i) {
            const qa_server_browser_rule *rule=details.rules+i;
            if(key_is(&rule->name,"gamedir")) { game=&rule->value; break; }
            if(!game && key_is(&rule->name,"game"))game=&rule->value;
        }
        if(okay && game)okay=bytes_add(&raw,(qa_bytes){game->data,game->size},false,e);
        for(size_t i=0;okay && i<details.player_count;++i)
            okay=bytes_add(&raw,(qa_bytes){details.players[i].name.data,details.players[i].name.size},true,e);
        if(okay)okay=frontend_network_menu_details_current(o->seat->frontend,view,&details);
    }
    if(okay)okay=text_add(&raw,row->address,e) && qa_utf8_lower((qa_bytes){raw.data,raw.size},&row->search,e);
    qa_buffer_free(&raw); qa_server_browser_details_free(&details); return okay;
}
static bool refresh(frontend_startup_server_browser *o,const frontend_network_menu_view *view,qa_error *e)
{
    if(!load_defaults(o,view,e))return false;
    qa_net_protocol_id protocols[2]={family_protocol(o->draft.family),{.kind=QA_NET_Q2KEX_2023}};
    size_t counts[2]={0},total=0,lanes=o->draft.family==BROWSER_Q2?2:1;
    for(size_t lane=0;lane<lanes;++lane) {
        if(!frontend_network_menu_rows(o->seat->frontend,view,protocols[lane],NULL,0,counts+lane,e))return false;
        if(counts[lane]>SIZE_MAX-total)return fail(e,QA_ERROR_MEMORY,"Server inventory exceeds its retained extent");
        total+=counts[lane];
    }
    clear_rows(o);
    if(total>o->row_capacity) {
        if(total>SIZE_MAX/sizeof(*o->rows) || total>SIZE_MAX/sizeof(*o->order))
            return fail(e,QA_ERROR_MEMORY,"Server inventory exceeds its native extent");
        browser_row *rows=calloc(total,sizeof(*rows)); size_t *order=malloc(total*sizeof(*order)),*scratch=malloc(total*sizeof(*scratch));
        if(!rows || !order || !scratch) { free(rows); free(order); free(scratch); return fail(e,QA_ERROR_MEMORY,"Retaining actual server inventory"); }
        free(o->rows); free(o->order); free(o->scratch);
        o->rows=rows; o->order=order; o->scratch=scratch; o->row_capacity=total;
    }
    for(size_t lane=0;lane<lanes;++lane) {
        qa_server_entry *entries=counts[lane]?malloc(counts[lane]*sizeof(*entries)):NULL;
        if(counts[lane] && !entries)return fail(e,QA_ERROR_MEMORY,"Reading actual protocol server rows");
        size_t count=0; bool okay=frontend_network_menu_rows(o->seat->frontend,view,protocols[lane],entries,counts[lane],&count,e);
        if(okay && count>counts[lane])okay=fail(e,QA_ERROR_ARGUMENT,"Server inventory changed during observation");
        for(size_t i=0;okay && i<count;++i) {
            size_t target=0;
            while(target<o->row_count && !qa_net_address_equal(&o->rows[target].entry.address,&entries[i].address,true))++target;
            if(target==o->row_count) { o->rows[target]=(browser_row){.entry=entries[i]}; ++o->row_count; }
            else {
                browser_row *row=o->rows+target; uint32_t sources=row->entry.sources|entries[i].sources;
                if((entries[i].available && !row->entry.available) ||
                   (entries[i].available==row->entry.available && entries[i].updated_ns>row->entry.updated_ns))row->entry=entries[i];
                row->entry.sources=sources;
            }
        }
        free(entries); if(!okay)return false;
    }
    qa_buffer filter={0}; bool okay=trim_lower(o->draft.filter,&filter,e); size_t used=0;
    for(size_t i=0;okay && i<o->row_count;++i) {
        browser_row *row=o->rows+i; row->has_ping=row->entry.has_ping;
        okay=qa_net_address_format(&row->entry.address,row->address,sizeof(row->address),e) && row_search(o,view,row,e);
        if(okay && selected(&o->draft,row))o->draft.selected_protocol=row->entry.protocol;
        if(okay && included(&o->draft,row,(qa_bytes){filter.data,filter.size}))o->order[used++]=i;
    }
    qa_buffer_free(&filter);
    if(okay)okay=sort_rows(o,used,e) && frontend_network_menu_current(o->seat->frontend,view);
    if(okay) {
        frontend_network_menu_status status;
        okay=frontend_network_menu_status_read(o->seat->frontend,view,family_protocol(o->draft.family),&status,e);
        if(okay) {
            if(status.receipt && (o->status_network!=view->network || status.receipt!=o->status_receipt))
                snprintf(o->draft.status,sizeof(o->draft.status),"%s",status.text);
            o->status_network=view->network; o->status_receipt=status.receipt;
            o->unfiltered_count=o->row_count; o->filtered_count=used;
        }
    }
    return okay;
}
static bool select_row(frontend_startup_server_browser *o,const frontend_network_menu_view *view,size_t index,qa_error *e)
{
    if(o->draft.page>SIZE_MAX/3 || index>=3 || o->draft.page*3>=o->filtered_count ||
       index>=o->filtered_count-o->draft.page*3)return fail(e,QA_ERROR_ARGUMENT,"Selected server is outside the actual displayed page");
    qa_net_address address=o->rows[o->order[o->draft.page*3+index]].entry.address;
    if(!refresh(o,view,e))return false;
    const browser_row *row=NULL;
    for(size_t i=0;i<o->row_count;++i)if(qa_net_address_equal(&address,&o->rows[i].entry.address,true)) { row=o->rows+i; break; }
    if(!row)return true;
    char remote[256]; bool direct;
    if(!frontend_network_menu_direct_read(o->seat->frontend,view,&row->entry,remote,&direct,e))return false;
    if(!direct)snprintf(remote,sizeof(remote),"%s",row->address);
    o->draft.selected=true; o->draft.selected_protocol=row->entry.protocol; o->draft.selected_address=row->entry.address;
    snprintf(o->draft.selected_remote,sizeof(o->draft.selected_remote),"%s",remote);
    snprintf(o->draft.addresses[o->draft.family],sizeof(o->draft.addresses[0]),"%s",remote);
    return true;
}
static bool counted_detail(frontend_startup_server_browser *o,const qa_buffer *name,const char *middle,
    const qa_buffer *value,qa_error *e)
{
    qa_buffer text={0}; bool okay=bytes_add(&text,(qa_bytes){name->data,name->size},false,e);
    if(okay && text.size) --text.size;
    if(okay)okay=text_add(&text,middle,e);
    if(okay && text.size) --text.size;
    if(okay)okay=bytes_add(&text,(qa_bytes){value->data,value->size},false,e);
    if(okay && text.size)text.data[--text.size]=0;
    for(size_t i=0;i<text.size;++i)if(text.data[i]<32 || text.data[i]==127)text.data[i]=' ';
    if(okay)okay=detail_line(o,(const char *)text.data,e);
    qa_buffer_free(&text); return okay;
}
static bool build_details(frontend_startup_server_browser *o,const frontend_network_menu_view *view,qa_error *e)
{
    clear_details(o);
    if(!refresh(o,view,e))return false;
    const browser_row *row=NULL;
    for(size_t i=0;i<o->row_count;++i)if(selected(&o->draft,o->rows+i)) { row=o->rows+i; break; }
    if(!row)return detail_line(o,"Select a server first.",e);
    if(!row->entry.available)return detail_line(o,row->address,e) && detail_line(o,"No status response yet.",e);
    qa_server_browser_details details={0}; size_t *rules=NULL;
    bool okay=frontend_network_menu_details_read(o->seat->frontend,view,&row->entry,&details,e);
    char line[1200];
    if(okay)okay=detail_line(o,details.entry.name,e) && detail_line(o,row->address,e);
    snprintf(line,sizeof(line),"Map: %s  Players: %u/%u",details.entry.map,details.entry.players,details.entry.maximum_players);
    if(okay)okay=detail_line(o,line,e);
    for(size_t i=0;okay && i<details.player_count;++i) {
        char tail[96]; snprintf(tail,sizeof(tail),"  score %d  ping %d",details.players[i].score,details.players[i].ping);
        qa_buffer suffix={(uint8_t *)tail,strlen(tail)};
        okay=counted_detail(o,&details.players[i].name,"",&suffix,e);
    }
    if(okay && details.rule_count) {
        rules=malloc(details.rule_count*sizeof(*rules));
        if(!rules)okay=fail(e,QA_ERROR_MEMORY,"Sorting actual server rule rows");
    }
    for(size_t i=0;okay && i<details.rule_count;++i) {
        size_t at=i;
        while(at) {
            const qa_buffer *a=&details.rules[rules[at-1]].name,*b=&details.rules[i].name;
            if(a->size>INT32_MAX || b->size>INT32_MAX) { okay=fail(e,QA_ERROR_FORMAT,"Server rule exceeds ICU text extent"); break; }
            UErrorCode status=U_ZERO_ERROR;
            UCollationResult order=ucol_strcollUTF8(o->standard,(const char *)a->data,(int32_t)a->size,(const char *)b->data,(int32_t)b->size,&status);
            if(U_FAILURE(status)) { okay=fail(e,QA_ERROR_FORMAT,"Collating actual server rule names"); break; }
            if(order<=0)break;
            rules[at]=rules[at-1]; --at;
        }
        rules[at]=i;
    }
    for(size_t i=0;okay && i<details.rule_count;++i) {
        const qa_server_browser_rule *rule=details.rules+rules[i];
        okay=counted_detail(o,&rule->name,": ",&rule->value,e);
    }
    if(okay)okay=frontend_network_menu_details_current(o->seat->frontend,view,&details);
    free(rules); qa_server_browser_details_free(&details); return okay;
}
static bool trim_copy(const char *text,char *out,size_t capacity,qa_error *e)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)}; size_t cursor=0,begin=bytes.size,end=0; uint32_t code;
    while(cursor<bytes.size) {
        size_t before=cursor;
        if(!qa_utf8_next(bytes,&cursor,&code))break;
        if(!qa_unicode_whitespace(code)) { if(begin==bytes.size)begin=before; end=cursor; }
    }
    if(begin==bytes.size)begin=end=0;
    size_t length=end-begin;
    if(length>=capacity)return fail(e,QA_ERROR_ARGUMENT,"Server address exceeds its authored field length");
    if(length)memcpy(out,bytes.data+begin,length);
    out[length]=0; return true;
}
static bool field_copy(char *out,size_t capacity,const qa_ui_action *event,qa_error *e)
{
    const char *text=event->value.text?event->value.text:""; size_t length=strlen(text);
    if(length>=capacity || !qa_utf8_valid((qa_bytes){(const uint8_t *)text,length}))
        return fail(e,QA_ERROR_ARGUMENT,"Server browser field exceeds its actual UTF-8 control extent");
    memcpy(out,text,length+1); return true;
}
static bool execute(browser_menu_context *context,const frontend_network_menu_view *view,
    qa_ui_id control,const qa_ui_action *event,qa_error *e)
{
    frontend_startup_server_browser *o=context->owner; browser_draft *d=&o->draft;
    qa_frontend *f=o->seat->frontend; qa_net_protocol_id protocol=family_protocol(d->family);
    if(context->id==o->menus.browser) {
        if(control==1 && event->kind==QA_UI_SELECT) {
            if(event->value.row>=BROWSER_FAMILIES)return fail(e,QA_ERROR_ARGUMENT,"Unknown browser game family");
            d->family=(uint32_t)event->value.row; d->page=0; d->selected=false; return true;
        }
        if(control==2 && event->kind==QA_UI_CHANGE_TEXT)
            return field_copy(d->addresses[d->family],sizeof(d->addresses[0]),event,e);
        if(control==6 && event->kind==QA_UI_CHANGE_TEXT) {
            if(!field_copy(d->filter,sizeof(d->filter),event,e))return false;
            d->page=0; return true;
        }
        if((control==2 && event->kind==QA_UI_SUBMIT) ||
           (control==3 && event->kind==QA_UI_ACTIVATE)) {
            char remote[256];
            if(!trim_copy(d->addresses[d->family],remote,sizeof(remote),e))return false;
            if(d->selected && !strcmp(remote,d->selected_remote))protocol=d->selected_protocol;
            if(!frontend_network_menu_query(f,view,protocol,remote,e))return false;
            snprintf(d->status,sizeof(d->status),"Query sent"); return true;
        }
        if(event->kind!=QA_UI_ACTIVATE)return true;
        switch(control) {
        case 4: {
            qa_error kex_failure={0};
            if(d->family==BROWSER_Q2)
                (void)frontend_network_menu_scan(f,view,(qa_net_protocol_id){.kind=QA_NET_Q2KEX_2023},&kex_failure);
            if(!frontend_network_menu_scan(f,view,protocol,e))return false;
            snprintf(d->status,sizeof(d->status),"%s",kex_failure.code!=QA_OK?kex_failure.message:"Searching local network...");
            return true;
        }
        case 5: {
            char remote[256]; bool added;
            if(!trim_copy(d->addresses[d->family],remote,sizeof(remote),e))return false;
            if(d->selected && !strcmp(remote,d->selected_remote))protocol=d->selected_protocol;
            if(!frontend_network_menu_favorite(f,view,protocol,remote,&added,e))return false;
            snprintf(d->status,sizeof(d->status),"Favorite %s",added?"added":"removed"); return true;
        }
        case 7:return qa_ui_open(o->ui,o->menus.options,now(o),e);
        case 8: case 9: case 10:
            return select_row(o,view,(size_t)(control-8),e);
        case 11: {
            size_t pages=o->filtered_count/3+(o->filtered_count%3!=0);
            if(!pages)pages=1;
            d->page=(d->page+1)%pages; return true;
        }
        case 12:d->favorites_only=!d->favorites_only; d->page=0; return true;
        case 13: {
            char remote[256]; frontend_network_menu_connection connection;
            if(!trim_copy(d->addresses[d->family],remote,sizeof(remote),e))return false;
            if(d->selected && !strcmp(remote,d->selected_remote))protocol=d->selected_protocol;
            if(!frontend_network_menu_connection_read(f,view,protocol,remote,&connection,e))return false;
            if(!connection.available)return fail(e,QA_ERROR_UNSUPPORTED,connection.reason);
            if(!frontend_network_menu_connect(f,view,&connection,e))return false;
            snprintf(d->status,sizeof(d->status),"Connecting..."); return true;
        }
        case 14:d->page=d->details_page=0; return qa_ui_open(o->ui,o->menus.details,now(o),e);
        case 15:return qa_ui_close(o->ui,now(o),e);
        default:return true;
        }
    }
    if(context->id==o->menus.options) {
        if(control==1 && event->kind==QA_UI_SELECT) {
            if(event->value.row>=BROWSER_SORTS)return fail(e,QA_ERROR_ARGUMENT,"Unknown browser sort order");
            d->sort=(uint32_t)event->value.row; d->page=0; return true;
        }
        if((control==2 || control==3) && event->kind==QA_UI_CHANGE_NUMBER) {
            if(control==2)d->hide_empty=event->value.number!=0;
            else d->hide_full=event->value.number!=0;
            d->page=0; return true;
        }
        if(control==4 && event->kind==QA_UI_CHANGE_TEXT)
            return field_copy(d->masters[d->family],sizeof(d->masters[0]),event,e);
        if(control==5 && event->kind==QA_UI_ACTIVATE) {
            char remote[2049];
            if(!trim_copy(d->masters[d->family],remote,sizeof(remote),e) ||
               !frontend_network_menu_master(f,view,protocol,remote,e))return false;
            snprintf(d->status,sizeof(d->status),"Querying master..."); return true;
        }
        return control!=6 || event->kind!=QA_UI_ACTIVATE || qa_ui_close(o->ui,now(o),e);
    }
    if(context->id==o->menus.details && event->kind==QA_UI_ACTIVATE) {
        if(control==9) {
            size_t pages=o->detail_count/8+(o->detail_count%8!=0);
            if(!pages)pages=1;
            d->page=d->details_page=(d->details_page+1)%pages; return true;
        }
        if(control==10)return qa_ui_close(o->ui,now(o),e);
    }
    return true;
}
static bool action(void *user,uint32_t seat,qa_ui_id control,const qa_ui_action *event,qa_error *e)
{
    browser_menu_context *context=user; frontend_startup_server_browser *o=context?context->owner:NULL;
    frontend_network_menu_view view;
    if(!o || !event || seat!=o->seat->id || o->busy || !read(o,&view,e))return false;
    o->busy=true; qa_error failure={0}; bool okay=execute(context,&view,control,event,&failure);
    o->busy=false;
    if(!bound(o) || !frontend_network_menu_current(o->seat->frontend,&view))
        return fail(e,QA_ERROR_ARGUMENT,"Server browser action expired its actual Network owner");
    if(!okay)snprintf(o->draft.status,sizeof(o->draft.status),"%s",failure.message[0]?failure.message:"Server operation failed");
    frontend_network_menu_status status;
    if(!frontend_network_menu_status_read(o->seat->frontend,&view,family_protocol(o->draft.family),&status,e))return false;
    o->status_network=view.network; o->status_receipt=status.receipt;
    return true;
}
static qa_ui_control button(browser_menu_context *context,qa_ui_id id,const char *label,
    float x,float y,float width,bool enabled)
{
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect={x,y,width,30},
        .enabled=enabled,.visible=true,.context=context,.action=action};
}
static qa_ui_control field(browser_menu_context *context,qa_ui_id id,const char *label,
    const char *text,size_t maximum,float x,float y,float width)
{
    qa_ui_control control=button(context,id,label,x,y,width,true); control.kind=QA_UI_FIELD;
    control.value.field.text=text; control.value.field.maximum=maximum; return control;
}
static bool fit(frontend_startup_server_browser *o,char *text,size_t capacity,float width,float scale,qa_error *e)
{
    qa_ui_preferences preferences;
    if(!qa_ui_preferences_read(qa_application_cvars(o->seat->frontend->application),qa_application_ui_preference_handles(o->seat->frontend->application),o->seat->id,&preferences,e))return false;
    qa_font_selection fonts;
    if(!frontend_menu_font_selection(o->seat->frontend,o->seat->id,preferences.typeface==QA_UI_TYPEFACE_BOLD,&fonts,e))return false;
    qa_font_layout_options options={.scale=scale*preferences.text_scale,.color={1,1,1,1},
        .color_codes=QA_FONT_COLOR_LITERAL,.alignment=QA_FONT_ALIGN_LEFT};
    size_t length=strlen(text),end=length;
    for(;;) {
        qa_arena_reset(&o->measurements); qa_font_layout layout;
        options.text=(qa_bytes){(const uint8_t *)text,strlen(text)};
        if(!qa_font_layout_build(&fonts,&options,&o->measurements,&layout,e))return false;
        if(layout.width<=width || !end)return true;
        --end;
        while(end && ((unsigned char)text[end]&0xc0)==0x80)--end;
        if(end>capacity-4)end=capacity-4;
        memcpy(text+end,"...",4);
    }
}
static bool options_factory(void *user,uint32_t seat,qa_ui_menu *out,qa_error *e)
{
    browser_menu_context *context=user; frontend_startup_server_browser *o=context?context->owner:NULL;
    frontend_network_menu_view view;
    if(!o || !out || seat!=o->seat->id || context->id!=o->menus.options || !read(o,&view,e) || !load_defaults(o,&view,e))return false;
    static const char *sorts[]={"Ping: lowest first","Ping: highest first","Name: A to Z","Name: Z to A",
        "Map: A to Z","Map: Z to A","Players: most first","Players: fewest first"};
    bool was_busy=o->busy; o->busy=true;
    o->controls[0]=button(context,1,"Sort",64,118,512,true); o->controls[0].kind=QA_UI_CHOICE;
    o->controls[0].value.choice.labels=sorts; o->controls[0].value.choice.count=BROWSER_SORTS;
    o->controls[0].value.choice.selected=o->draft.sort;
    o->controls[1]=button(context,2,"Hide empty",64,152,512,true); o->controls[1].kind=QA_UI_TOGGLE;
    o->controls[1].value.checked=o->draft.hide_empty;
    o->controls[2]=button(context,3,"Hide full",64,186,512,true); o->controls[2].kind=QA_UI_TOGGLE;
    o->controls[2].value.checked=o->draft.hide_full;
    o->controls[3]=field(context,4,"Master address or HTTP list",o->draft.masters[o->draft.family],2048,64,226,512);
    o->controls[4]=button(context,5,"Find Internet servers",64,288,512,true);
    o->controls[5]=button(context,6,"Back",64,356,512,true);
    *out=(qa_ui_menu){.id=context->id,.title="Server filters",.source_title=true,.controls=o->controls,.count=6};
    bool okay=frontend_network_menu_current(o->seat->frontend,&view); o->busy=was_busy; return okay;
}
static bool browser_factory(void *user,uint32_t seat,qa_ui_menu *out,qa_error *e)
{
    browser_menu_context *context=user; frontend_startup_server_browser *o=context?context->owner:NULL;
    frontend_network_menu_view view;
    if(!o || !out || seat!=o->seat->id || context->id!=o->menus.browser || !read(o,&view,e))return false;
    bool was_busy=o->busy; o->busy=true;
    if(!refresh(o,&view,e)) { o->busy=was_busy; return false; }
    static const char *families[]={"Quake","QuakeWorld","Quake II","Quake III Arena"};
    size_t pages=o->filtered_count/3+(o->filtered_count%3!=0); if(!pages)pages=1;
    if(o->draft.page>=pages)o->draft.page=pages-1;
    size_t count=0;
    o->controls[count]=button(context,1,"Game",64,108,512,true); o->controls[count].kind=QA_UI_CHOICE;
    o->controls[count].value.choice.labels=families; o->controls[count].value.choice.count=BROWSER_FAMILIES;
    o->controls[count++].value.choice.selected=o->draft.family;
    o->controls[count++]=field(context,2,"Address",o->draft.addresses[o->draft.family],255,64,144,512);
    o->controls[count++]=button(context,3,"Query",64,180,160,true);
    o->controls[count++]=button(context,4,"Find LAN",240,180,160,true);
    o->controls[count++]=button(context,5,"Favorite",416,180,160,true);
    o->controls[count++]=field(context,6,"Filter",o->draft.filter,255,64,216,320);
    o->controls[count++]=button(context,7,o->draft.hide_empty||o->draft.hide_full?"Filters on":"Sort/filter",400,216,176,true);
    size_t start=o->draft.page*3;
    for(size_t i=0;i<3 && i<o->filtered_count-start;++i) {
        const browser_row *row=o->rows+o->order[start+i]; char players[48],ping[48];
        if(row->entry.available)snprintf(players,sizeof(players),"%u/%u",row->entry.players,row->entry.maximum_players);
        else snprintf(players,sizeof(players),"?");
        if(row->has_ping)snprintf(ping,sizeof(ping),"%.0fms",floor((double)row->entry.ping_ns/1000000.0+0.5));
        else ping[0]=0;
        snprintf(o->labels[i],sizeof(o->labels[i]),"%s%s  %s  %s",row->entry.sources&QA_SERVER_FAVORITE?"* ":"",
            row->entry.available && row->entry.name[0]?row->entry.name:row->address,players,ping);
        if(!fit(o,o->labels[i],sizeof(o->labels[i]),490,2.6f,e)) { o->busy=was_busy; return false; }
        o->controls[count++]=button(context,8+i,o->labels[i],64,252+(float)i*34,512,true);
    }
    snprintf(o->labels[3],sizeof(o->labels[3]),"Page %zu/%zu",o->draft.page+1,pages);
    o->controls[count++]=button(context,11,o->labels[3],64,358,240,true);
    o->controls[count++]=button(context,12,o->draft.favorites_only?"Favorites only":"All servers",320,358,256,true);
    o->controls[count++]=button(context,13,"Connect",64,396,160,true);
    o->controls[count++]=button(context,14,"Details",240,396,160,true);
    o->controls[count++]=button(context,15,"Back",416,396,160,true);
    if(!o->filtered_count) {
        o->controls[count++]=(qa_ui_control){.id=16,.kind=QA_UI_TEXT,.label=o->unfiltered_count?
            "No servers match these filters.":"No servers yet. Enter an address or find LAN.",
            .rect={64,430,512,0},.visible=true,.value.text={.scale=1.5f,.source=true}};
    }
    const browser_row *selection=NULL;
    for(size_t i=0;i<o->filtered_count;++i)
        if(selected(&o->draft,o->rows+o->order[i])) { selection=o->rows+o->order[i]; break; }
    if(selection && selection->entry.available)
        snprintf(o->labels[5],sizeof(o->labels[5]),"%s — %s",selection->entry.map,o->draft.status);
    else snprintf(o->labels[5],sizeof(o->labels[5]),"%s",o->draft.status);
    o->controls[count++]=(qa_ui_control){.id=17,.kind=QA_UI_TEXT,.label=o->labels[5],
        .rect={64,450,512,0},.visible=true,.value.text={.scale=1.8f,.fit_width=512,.source=true}};
    *out=(qa_ui_menu){.id=context->id,.title="Find servers",.source_title=true,.controls=o->controls,.count=count};
    bool okay=frontend_network_menu_current(o->seat->frontend,&view); o->busy=was_busy; return okay;
}
static bool details_factory(void *user,uint32_t seat,qa_ui_menu *out,qa_error *e)
{
    browser_menu_context *context=user; frontend_startup_server_browser *o=context?context->owner:NULL;
    frontend_network_menu_view view;
    if(!o || !out || seat!=o->seat->id || context->id!=o->menus.details || !read(o,&view,e))return false;
    bool was_busy=o->busy; o->busy=true;
    if(!build_details(o,&view,e)) { o->busy=was_busy; return false; }
    size_t pages=o->detail_count/8+(o->detail_count%8!=0); if(!pages)pages=1;
    if(o->draft.details_page>=pages)o->draft.details_page=pages-1;
    o->draft.page=o->draft.details_page;
    size_t count=0,start=o->draft.details_page*8;
    for(size_t i=0;i<8 && i<o->detail_count-start;++i)
        o->controls[count++]=button(context,i+1,o->details[start+i],64,118+(float)i*34,512,false);
    snprintf(o->labels[4],sizeof(o->labels[4]),"Page %zu/%zu",o->draft.details_page+1,pages);
    o->controls[count++]=button(context,9,o->labels[4],64,390,512,true);
    o->controls[count++]=button(context,10,"Back",64,424,512,true);
    *out=(qa_ui_menu){.id=context->id,.title="Server details",.source_title=true,.controls=o->controls,.count=count};
    bool okay=frontend_network_menu_current(o->seat->frontend,&view); o->busy=was_busy; return okay;
}
