#include "source_weapon_resource_history.h"
#include "source_weapon_standalone.h"
#include <stdlib.h>
#include <string.h>

static char *text_copy(const char *text,qa_error *error) {
    if(!text) return NULL;
    size_t size=strlen(text)+1;char *copy=malloc(size);
    if(!copy) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon resource history text");return NULL;}
    memcpy(copy,text,size);return copy;
}
static bool diagnostics_copy(const bot_weapon_resource *source,bot_weapon_resource *out,qa_error *error) {
    if(source->diagnostic_count>SIZE_MAX/sizeof(*out->diagnostics)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Weapon resource history diagnostics exceed native storage");return false;
    }
    if(!source->diagnostic_count) return true;
    out->diagnostics=calloc(source->diagnostic_count,sizeof(*out->diagnostics));
    if(!out->diagnostics) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining weapon resource history diagnostics");return false;}
    out->diagnostic_capacity=source->diagnostic_count;
    for(size_t i=0;i<source->diagnostic_count;++i) {
        const bot_weapon_diagnostic *row=&source->diagnostics[i];
        bot_weapon_diagnostic *copy=&out->diagnostics[out->diagnostic_count++];
        copy->origin=row->origin;copy->value=row->value;
        copy->path=text_copy(row->path,error);
        if(copy->path) copy->message=text_copy(row->message,error);
        if(!copy->path || !copy->message) return false;
        copy->value.location.path=copy->path;copy->value.message=copy->message;
    }
    return true;
}
static bool pending_copy(const bot_weapon_resource *source,bot_weapon_resource *out,qa_error *error) {
    bot_weapon_acquired_source **tail=&out->pending;
    for(const bot_weapon_acquired_source *row=source->pending;row;row=row->next) {
        *tail=calloc(1,sizeof(**tail));
        if(!*tail) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining pending weapon source history");return false;}
        bot_weapon_acquired_source *copy=*tail;copy->owned=true;
        if(row->value.path) {
            copy->value.path=text_copy(row->value.path,error);
            if(!copy->value.path) return false;
        }
        if(row->value.bytes.size) {
            if(!row->value.bytes.data) {qa_error_set(error,QA_ERROR_FORMAT,0,"Pending weapon history has no source bytes");return false;}
            uint8_t *bytes=malloc(row->value.bytes.size);
            if(!bytes) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining acquired weapon source history bytes");return false;}
            memcpy(bytes,row->value.bytes.data,row->value.bytes.size);
            copy->value.bytes=(qa_bytes){bytes,row->value.bytes.size};
        }
        tail=&copy->next;
    }
    return true;
}
bool bot_weapon_resource_clone(const bot_weapon_resource *source,bool rebind,
    bot_weapon_resource **out,qa_error *error) {
    if(!source || source->active || !out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Weapon history requires an idle actual resource and empty output");return false;
    }
    bot_weapon_resource *copy=NULL;
    bool ok=source->services.read?
        bot_weapon_resource_create(source->memory,&source->services,&source->options,&source->host,&copy,error):
        bot_weapon_resource_pure(source->memory,&copy,error);
    if(!ok) return false;
    copy->record=source->record;copy->attempted=source->attempted;
    copy->missing_root=source->missing_root;copy->report_failed=source->report_failed;
    copy->invalid_root_path=source->invalid_root_path;copy->own_failure=source->own_failure;
    copy->report_error=source->report_error;
    if(source->path) {copy->path=text_copy(source->path,error);ok=copy->path!=NULL;}
    if(ok && source->bound && rebind)
        ok=bot_weapon_resource_bind(copy,copy->record,source->path,error);
    else if(ok && source->bound) {
        copy->defined=malloc(source->weapon_count?source->weapon_count:1);
        if(!copy->defined) {qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining captured weapon array membership");ok=false;}
        else {
            if(source->weapon_count) memcpy(copy->defined,source->defined,source->weapon_count);
            copy->weapon_count=source->weapon_count;copy->projectile_count=source->projectile_count;
            copy->defined_count=source->defined_count;copy->bound=true;
        }
    }
    if(ok) ok=diagnostics_copy(source,copy,error) && pending_copy(source,copy,error);
    if(ok && source->reader) {
        qa_script_checkpoint state={0};qa_script_services services=bot_weapon_resource_services(copy);
        ok=qa_script_capture(source->reader,&state,error) && qa_script_restore(&services,&state,&copy->reader,error);
        qa_script_checkpoint_free(&state);
    }
    if(ok) *out=copy;else bot_weapon_resource_destroy(copy);
    return ok;
}
