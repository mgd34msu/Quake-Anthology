#include "internal.h"
#include "qa/ui_saves.h"
#include "qa/source_save.h"
#include <stdio.h>
#include <time.h>

typedef struct save_page { struct qa_ui_saves *owner; qa_ui_id id; } save_page;
struct qa_ui_saves {
    qa_ui *ui;
    qa_ui_saves_menus menus;
    qa_ui_saves_service service;
    save_page pages[4];
    size_t registered, page;
    char draft[49], message[256], lines[2][64], labels[5][256], details[5][256], page_label[64];
    char *overwrite_id;
    qa_ui_control controls[20];
    bool busy;
};
enum { SLOT = 1, NEW = 10, PREVIOUS, NEXT, REFRESH, CANCEL, NAME, WRITE, OVERWRITE, INFO, DETAIL = 30 };
static double now(const qa_ui_saves *o) { return o->ui->time_ms; }
static bool busy(const qa_ui_saves *o) { return o->busy || (o->service.busy && o->service.busy(o->service.context)); }
static const char *reason(const qa_ui_saves *o, bool saving) {
    return o->service.unavailable ? o->service.unavailable(o->service.context, saving) : NULL;
}
static qa_scene_rect_f row(unsigned index) { return (qa_scene_rect_f){64, 92 + (float)index * 28, 512, 28}; }
static qa_scene_rect_f startup_row(unsigned index) { return (qa_scene_rect_f){64, 118 + (float)index * 34, 512, 30}; }
static bool list(qa_ui_saves *o, const qa_ui_save_entry **entries, size_t *count, const char **message, qa_error *e) {
    *entries = NULL; *count = 0; *message = NULL;
    return o->service.list(o->service.context, entries, count, message, e) && (!*count || *entries);
}
static bool run(qa_ui_saves *o, bool refresh, bool saving, const char *id, qa_error *e) {
    if (busy(o)) return true;
    o->busy = true;
    snprintf(o->message, sizeof(o->message), "Working...");
    qa_error local = {0};
    bool ok = refresh ? o->service.refresh(o->service.context, &local) :
        saving ? o->service.save(o->service.context, o->draft, o->overwrite_id, &local) :
            o->service.load(o->service.context, id, &local);
    snprintf(o->message, sizeof(o->message), "%s", ok ? "" : local.message);
    o->busy = false;
    if (!ok && local.code == QA_ERROR_MEMORY) { if (e) *e = local; return false; }
    if (ok && saving) return qa_ui_close(o->ui, now(o), e);
    return true;
}
static bool action(void *context, uint32_t seat, qa_ui_id control, const qa_ui_action *event, qa_error *e) {
    save_page *p = context; qa_ui_saves *o = p->owner; (void)seat;
    if (control == NAME && event->kind == QA_UI_CHANGE_TEXT) {
        snprintf(o->draft, sizeof(o->draft), "%s", event->value.text ? event->value.text : ""); return true;
    }
    if (event->kind != QA_UI_ACTIVATE && !(control == NAME && event->kind == QA_UI_SUBMIT)) return true;
    if (busy(o)) return true;
    if (control == CANCEL) return qa_ui_close(o->ui, now(o), e);
    if (control == REFRESH) return run(o, true, false, NULL, e);
    if (control == WRITE || control == OVERWRITE || control == NAME) return run(o, false, true, NULL, e);
    const qa_ui_save_entry *entries; size_t count; const char *status;
    if (!list(o, &entries, &count, &status, e)) return false;
    size_t pages = count / 5 + (count % 5 != 0); if (!pages) pages = 1;
    if (control == PREVIOUS) { o->page = (o->page + pages - 1) % pages; return true; }
    if (control == NEXT) { o->page = (o->page + 1) % pages; return true; }
    if (control == NEW) {
        free(o->overwrite_id); o->overwrite_id = NULL;
        size_t index = 1;
        for (;;) {
            snprintf(o->draft, sizeof(o->draft), "Save %03zu", index);
            size_t found = 0; while (found < count && strcmp(entries[found].label, o->draft)) ++found;
            if (found == count) break;
            ++index;
        }
        o->message[0] = 0; return qa_ui_open(o->ui, o->menus.name, now(o), e);
    }
    if (control < SLOT || control >= SLOT + 5) return true;
    size_t index = o->page * 5 + (size_t)(control - SLOT);
    if (index >= count) return ui_fail(e, "Save row left its actual listing");
    if (entries[index].unavailable && !(p->id == o->menus.load && entries[index].requires_product)) {
        snprintf(o->message, sizeof(o->message), "%s", entries[index].unavailable); return true;
    }
    if (p->id == o->menus.load) return run(o, false, false, entries[index].id, e);
    size_t length = strlen(entries[index].id); char *selected = malloc(length + 1);
    if (!selected) return ui_fail(e, "Retaining selected overwrite slot");
    memcpy(selected, entries[index].id, length + 1); free(o->overwrite_id); o->overwrite_id = selected;
    snprintf(o->draft, sizeof(o->draft), "%s", entries[index].label);
    o->message[0] = 0; return qa_ui_open(o->ui, o->menus.overwrite, now(o), e);
}
static qa_ui_control button(save_page *p, qa_ui_id id, const char *label, qa_scene_rect_f rect, bool enabled) {
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect=rect,
        .enabled=enabled && !busy(p->owner),.visible=true,.context=p,.action=action};
}
static void info(qa_ui_saves *o, const char *text) {
    o->lines[0][0] = o->lines[1][0] = 0; size_t line = 0, length = 0;
    for (const char *cursor = text; cursor && *cursor && line < 2;) {
        while (*cursor == ' ' || *cursor == '\n' || *cursor == '\t') ++cursor;
        const char *word = cursor; while (*cursor && *cursor != ' ' && *cursor != '\n' && *cursor != '\t') ++cursor;
        size_t size = (size_t)(cursor - word); if (!size) break;
        if (length && length + size + 1 > 48) { ++line; length = 0; if (line == 2) break; }
        if (length) o->lines[line][length++] = ' ';
        if (size > sizeof(o->lines[line]) - length - 1) size = sizeof(o->lines[line]) - length - 1;
        memcpy(o->lines[line] + length, word, size); length += size; o->lines[line][length] = 0;
    }
}
static qa_ui_control text(qa_ui_id id,const char *label,float x,float y,float scale,float width,bool accent) {
    qa_ui_control value={.id=id,.kind=QA_UI_TEXT,.label=label,.rect={x,y,width,22},.visible=true};
    value.value.text=(qa_ui_text_style){.scale=scale,.fit_width=width,.accent=accent,.source=true,.overlay=true};return value;
}
static bool factory(void *context, uint32_t seat, qa_ui_menu *out, qa_error *e) {
    save_page *p = context; qa_ui_saves *o = p->owner; (void)seat; size_t n = 0;
    bool saving = p->id != o->menus.load; const char *title;
    if (p->id == o->menus.name) {
        title = "New saved game";
        o->controls[n] = button(p,NAME,"Name",row(1),true); o->controls[n].kind=QA_UI_FIELD;
        o->controls[n].value.field.text=o->draft;o->controls[n++].value.field.maximum=48;
        o->controls[n++] = button(p,WRITE,"Save game",row(3),true);
    } else if (p->id == o->menus.overwrite) {
        title = "Overwrite saved game?";
        o->controls[n++] = button(p,INFO,o->draft,row(1),false);
        o->controls[n++] = button(p,OVERWRITE,"Overwrite this saved game",row(3),true);
    } else {
        title = saving ? "Save game" : "Load Game";
        const qa_ui_save_entry *entries; size_t count; const char *status;
        if (!list(o,&entries,&count,&status,e)) return false;
        size_t pages = count/5+(count%5!=0); if (!pages) pages=1; if(o->page>=pages)o->page=pages-1;
        if (saving) o->controls[n++] = button(p,NEW,"New saved game",row(0),reason(o,true)==NULL);
        for(size_t i=0;i<5 && o->page*5+i<count;++i) {
            const qa_ui_save_entry *entry=entries+o->page*5+i;
            snprintf(o->labels[i],sizeof(o->labels[i]),saving?"%s - %s":"%s  -  %s",entry->label,saving && entry->unavailable?"unavailable":entry->map);
            snprintf(o->details[i],sizeof(o->details[i]),"%s",entry->game?entry->game:"");
            if(entry->saved_at_ms>0) {
                time_t seconds=(time_t)(entry->saved_at_ms/1000);struct tm *stamp=localtime(&seconds);char date[96]={0};
                if(stamp && strftime(date,sizeof(date),"%x %X",stamp))snprintf(o->details[i],sizeof(o->details[i]),"%s  -  %s",entry->game?entry->game:"",date);
            }
            qa_scene_rect_f rect=saving?row((unsigned)i+1):(qa_scene_rect_f){64,118+(float)i*46,512,42};
            o->controls[n++]=button(p,SLOT+i,o->labels[i],rect,reason(o,saving)==NULL && (saving || !entry->unavailable || entry->requires_product));
        }
        if(pages>1) {
            if(saving)o->controls[n++]=button(p,PREVIOUS,"Previous page",row(6),true);
            if(saving)snprintf(o->page_label,sizeof(o->page_label),"Next page (%zu/%zu)",o->page+1,pages);
            else snprintf(o->page_label,sizeof(o->page_label),"Older saves");
            o->controls[n++]=button(p,NEXT,o->page_label,saving?row(7):startup_row(7),true);
        }
        o->controls[n++]=button(p,REFRESH,"Refresh",saving?row(8):startup_row(8),true);
        if(!saving) {
            if(!count) {
                o->controls[n++]=text(DETAIL,"No saved games",64,138,2.5f,512,false);
                o->controls[n++]=text(DETAIL+1,"Your saved games will appear here.",64,174,1.8f,512,false);
                o->controls[n-2].value.text.overlay=false;o->controls[n-1].value.text.overlay=false;
            }
            for(size_t i=0;i<5 && o->page*5+i<count;++i) {
                const qa_ui_save_entry *entry=entries+o->page*5+i;
                o->controls[n++]=text(DETAIL+2+i,o->details[i],74,144+(float)i*46,1.6f,486,false);
                if(entry->unavailable && ui_inside((qa_scene_rect_f){64,118+(float)i*46,512,42},o->ui->cursor))
                    o->controls[n++]=text(DETAIL+8,entry->unavailable,64,360,1.6f,512,true);
            }
            if(status && *status)o->controls[n++]=text(DETAIL+9,status,64,390,1.5f,512,false);
            if(o->service.load_choice) { bool present=false;if(!o->service.load_choice(o->service.context,o->controls+n,&present,e))return false;if(present)++n; }
        }
        if(!o->message[0])info(o,reason(o,saving)?reason(o,saving):saving?(status && *status?status:!count?"No saved games yet.":""):"");
    }
    if(o->message[0] || p->id==o->menus.name || p->id==o->menus.overwrite)info(o,o->message);
    if(saving)for(unsigned i=0;i<2;++i)if(o->lines[i][0])o->controls[n++]=button(p,INFO+i+1,o->lines[i],row(9+i),false);
    if(!saving && o->message[0])o->controls[n++]=text(DETAIL+10,o->message,64,450,1.5f,512,true);
    o->controls[n++]=button(p,CANCEL,saving?"Cancel":"Back",saving?row(11):startup_row(9),true);
    *out=(qa_ui_menu){.id=p->id,.title=title,.controls=o->controls,.count=n,.fullscreen=true};return true;
}
static bool open_page(void *context,uint32_t seat,qa_error *e) {
    save_page *p=context; (void)seat;
    if(p->id!=p->owner->menus.load && p->id!=p->owner->menus.save)return true;
    p->owner->page=0; return run(p->owner,true,false,NULL,e);
}
bool qa_ui_saves_create(qa_ui *ui,qa_ui_saves_menus menus,const qa_ui_saves_service *service,qa_ui_saves **out,qa_error *e) {
    if(!ui || !service || !service->list || !service->refresh || !service->save || !service->load || !out || *out)
        return ui_fail(e,"Saved game menus require their actual service owner");
    qa_ui_saves *o=calloc(1,sizeof(*o));if(!o)return ui_fail(e,"Allocating saved game menus");
    o->ui=ui;o->menus=menus;o->service=*service;snprintf(o->draft,sizeof(o->draft),"Save 001");
    qa_ui_id ids[4]={menus.load,menus.save,menus.name,menus.overwrite};
    for(size_t i=0;i<4;++i) {
        o->pages[i]=(save_page){o,ids[i]};
        if(!qa_ui_register(ui,&(qa_ui_menu_registration){.id=ids[i],.context=o->pages+i,.factory=factory,.open=open_page},e)) {
            qa_error cleanup={0};qa_ui_saves_destroy(&o,0,&cleanup);return false;
        }
        ++o->registered;
    }
    *out=o;return true;
}
bool qa_ui_saves_destroy(qa_ui_saves **slot,double time_ms,qa_error *e) {
    qa_ui_saves *o=slot?*slot:NULL;if(!o)return true;
    while(o->registered) { if(!qa_ui_unregister(o->ui,o->pages[o->registered-1].id,time_ms,e))return false;--o->registered; }
    free(o->overwrite_id);free(o);*slot=NULL;return true;
}
static bool saved_text(qa_source_save_io *io,char *value,size_t capacity) {
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(value):0;
    if(!qa_source_save_count(io,&length,capacity-1) || !qa_source_save_bytes(io,value,length) ||
        memchr(value,0,length))return false;
    value[length]=0;return true;
}
static bool saved_fields(qa_source_save_io *io,qa_ui_saves *saved,const qa_ui_saves *owner) {
    uint8_t magic[4]={'Q','S','M','S'};uint32_t seat=owner->ui->options.seat;
    qa_ui_id ids[4]={owner->menus.load,owner->menus.save,owner->menus.name,owner->menus.overwrite};
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QSMS",4) ||
        !qa_source_save_u32(io,&seat) || seat!=owner->ui->options.seat)return false;
    for(size_t i=0;i<4;++i) { uint64_t id=ids[i];if(!qa_source_save_u64(io,&id) || id!=ids[i])return false; }
    return qa_source_save_count(io,&saved->page,SIZE_MAX/5) &&
        saved_text(io,saved->draft,sizeof(saved->draft)) &&
        saved_text(io,saved->message,sizeof(saved->message)) &&
        qa_source_save_owned_text(io,&saved->overwrite_id) && (!saved->overwrite_id || *saved->overwrite_id);
}
bool qa_ui_saves_checkpoint(const qa_ui_saves *owner,qa_buffer *out,qa_error *e) {
    if(!owner || !out || out->data || out->size || !qa_ui_idle(owner->ui) || owner->busy)
        return ui_fail(e,"Saved game checkpoint requires its idle menu owner");
    qa_ui_saves saved=*owner;qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) && saved_fields(&io,&saved,owner) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if(!ok && e && e->code==QA_OK)qa_error_set(e,QA_ERROR_FORMAT,0,"Saved game page leaves its actual menu owner");
    return ok;
}
bool qa_ui_saves_restore(qa_ui_saves *owner,qa_bytes bytes,qa_error *e) {
    if(!owner || !qa_ui_idle(owner->ui) || owner->busy)return ui_fail(e,"Saved game restore requires its idle menu owner");
    qa_ui_saves saved={0};qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && saved_fields(&io,&saved,owner) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) { free(saved.overwrite_id);if(e && e->code==QA_OK)qa_error_set(e,QA_ERROR_FORMAT,0,"Saved game page leaves its actual menu owner");return false; }
    owner->page=saved.page;memcpy(owner->draft,saved.draft,sizeof(owner->draft));memcpy(owner->message,saved.message,sizeof(owner->message));
    free(owner->overwrite_id);owner->overwrite_id=saved.overwrite_id;owner->lines[0][0]=owner->lines[1][0]=0;return true;
}
