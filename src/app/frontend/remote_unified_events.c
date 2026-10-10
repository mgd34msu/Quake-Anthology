#include "remote_unified_private.h"
#include "remote_unified_events.h"
#include "remote_unified_save.h"
#include "../application/unified_output.h"
#include "qa/binary.h"
#include "qa/network_unified_frame.h"
#include "qa/network_unified_control.h"
#include "../../network/unified/frame_internal.h"

#include <math.h>

typedef struct unified_event_resource {
    struct unified_event_resource *next;
    qa_string_id id, content, path;
    const qa_resource *resource;
    qa_audio_asset *asset;
    qa_game_family family;
} unified_event_resource;
typedef struct unified_component_owner {
    struct unified_component_owner *next;
    qa_string_id provider, content;
    uint64_t generation;
    bool retired, cancelled;
} unified_component_owner;
typedef struct unified_received_event {
    qa_unified_held delivery;
    struct unified_received_event *next_retained;
    qa_event_lease *lease;
    bool *mirrored;
    bool published;
    unified_component_owner component;
} unified_received_event;
struct frontend_unified_events {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_event_options options;
    qa_strings *strings;
    char *message_line;
    qa_event_ring *records;
    unified_received_event *retained;
    uint64_t presentation_cursor, simulation_cursor;
    size_t presentation_at, simulation_at;
    unified_received_event *delivery;
    unified_event_resource *resources;
    unified_component_owner *components;
    qa_hud *hud;
    uint32_t epoch;
    uint64_t frame, prepared_frame;
    qa_unified_document *prepared_document;
    double seconds, prepared_seconds;
    uint64_t presentation_sequence, simulation_sequence;
    bool has_presentation_sequence, has_simulation_sequence;
    bool has_frame, prepared, busy, owns_audio, families_ready;
};

const qa_unified_document *frontend_unified_events_document(const frontend_unified_events *o)
{ return o && o->delivery ? o->delivery->delivery.document : NULL; }

static unified_received_event *record_begin(frontend_unified_events *o,
    qa_event_transaction *transaction,qa_error *error)
{
    if (!qa_event_ring_begin(o->records,transaction)) {
        frontend_unified_fail(error,QA_ERROR_MEMORY,"Received event storage is full"); return NULL;
    }
    unified_received_event *record=qa_event_ring_alloc(transaction,sizeof(*record),
        _Alignof(unified_received_event),error);
    if (!record) {
        qa_event_ring_abort(transaction);
        frontend_unified_fail(error,QA_ERROR_MEMORY,"Received event storage is full"); return NULL;
    }
    *record=(unified_received_event){0}; return record;
}
static void metadata_retire(frontend_unified_events *o)
{
    uint64_t first=qa_event_ring_first(o->records),next=qa_event_ring_next(o->records);
    while (first<next) {
        const unified_received_event *record=qa_event_ring_at(o->records,first);
        if (record->delivery.document) break;
        o->presentation_cursor=o->simulation_cursor=++first;
        qa_event_ring_retire(o->records,first);
    }
}
static void metadata_commit(frontend_unified_events *o,qa_event_transaction *transaction,
    unified_received_event *record)
{
    uint64_t id=qa_event_ring_commit(transaction,record);
    record->lease=qa_event_ring_retain(o->records,id);
    record->next_retained=o->retained; o->retained=record;
    metadata_retire(o);
}

static bool current(frontend_unified_events *o, qa_error *e)
{
    if (!o || !frontend_unified_media_current(o->media))
        return frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified event media is not current");
    bool snapshot=o->frontend->capture || o->frontend->source_restoring;
    if (!(snapshot?frontend_remote_unified_checkpoint_current(o->replica,e):frontend_remote_unified_current(o->replica,e))) return false;
    return o->epoch == frontend_remote_unified_epoch(o->replica) ||
        frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified event owner changed its admitted epoch");
}
static bool execution_current(frontend_unified_events *o,qa_error *e)
{
    if (!o || !o->families_ready || o->frontend->capture || o->frontend->source_restoring || o->frontend->resource_inventory)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event delivery overlaps private graph capture or restore");
    return current(o,e);
}

static bool actor(frontend_unified_events *o,qa_actor_id source,qa_actor_id *out,qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(frontend_remote_unified_frame(o->replica));
    return frontend_remote_unified_source_actor(o->replica,frame,source,
        o->frontend->capture || o->frontend->source_restoring,out,e);
}
static unified_component_owner *component_find(const frontend_unified_events *o,const char *provider,uint64_t generation)
{
    qa_string_id id=qa_strings_find(o->strings,(qa_bytes){(const uint8_t *)provider,strlen(provider)});
    for (unified_component_owner *c=o->components;c;c=c->next)
        if (c->generation==generation && c->provider==id) return c;
    return NULL;
}
static bool component_read(frontend_unified_events *o,const qa_source_owner *owner,const char *content,
    unified_component_owner *out,qa_error *e)
{
    qa_vfs *files; const qa_product *product;
    bool okay=owner && owner->provider && *owner->provider && owner->generation && content && *content &&
        qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&product,e);
    if (okay) okay=qa_strings_intern_cstr(o->strings,content,&out->content,e) &&
        qa_strings_intern_cstr(o->strings,owner->provider,&out->provider,e);
    if (okay) out->generation=owner->generation;
    else { if (!e || e->code==QA_OK)
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Component presentation identity is outside its admitted recipe"); }
    return okay;
}
bool frontend_unified_events_component_current(const frontend_unified_events *o,const qa_source_owner *owner,
    const char *content,bool *active,qa_error *e)
{
    if (!o || !owner || !owner->provider || !owner->generation || !content || !active)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component qualification needs its retained owner and content");
    unified_component_owner *c=component_find(o,owner->provider,owner->generation);
    if (!c || c->content!=qa_strings_find(o->strings,(qa_bytes){(const uint8_t *)content,strlen(content)}))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Component presentation token has no matching reliable content admission");
    *active=!c->retired && !c->cancelled; return true;
}
bool frontend_unified_events_component_admit(frontend_unified_events *o,const qa_source_owner *owner,const char *content,qa_error *e)
{ bool created; return frontend_unified_events_component_admit_created(o,owner,content,&created,e); }
bool frontend_unified_events_component_admit_created(frontend_unified_events *o,const qa_source_owner *owner,const char *content,
    bool *created,qa_error *e)
{
    if (!o || !created || o->busy || o->prepared || !current(o,e)) return false;
    *created=false;
    unified_component_owner candidate={0};
    if (!component_read(o,owner,content,&candidate,e)) return false;
    unified_component_owner *c=component_find(o,owner->provider,owner->generation);
    if (c) {
        bool okay=c->content==candidate.content ||
            frontend_unified_fail(e,QA_ERROR_FORMAT,"Reliable component token changed its admitted content");
        if (okay && c->cancelled) { c->cancelled=false; *created=true; }
        return okay;
    }
    qa_event_transaction transaction;
    unified_received_event *record=record_begin(o,&transaction,e);
    if (!record) return false;
    record->component=candidate; c=&record->component;
    metadata_commit(o,&transaction,record);
    c->next=o->components; o->components=c; *created=true; return true;
}
bool frontend_unified_events_component_cancel(frontend_unified_events *o,const qa_source_owner *owner,qa_error *e)
{
    if (!o || o->busy || o->prepared || !owner || !owner->provider || !owner->generation)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component cancellation needs returned retained event storage");
    unified_component_owner *c=component_find(o,owner->provider,owner->generation);
    if (!c || c->retired) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Component cancellation has no unpublished admission");
    c->cancelled=true; return true;
}
bool frontend_unified_events_component_retire(frontend_unified_events *o,const qa_source_owner *owner,qa_error *e)
{
    if (!o || o->busy || o->prepared || !owner || !owner->provider || !owner->generation)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component retirement needs returned retained event storage");
    unified_component_owner *c=component_find(o,owner->provider,owner->generation);
    if (!c) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Retiring component has no reliable presentation admission");
    c->retired=true; c->cancelled=false; return true;
}
static uint64_t clock_ns(double seconds)
{
    long double n=(long double)seconds*1e9L;
    return n<=0?0:n>=(long double)UINT64_MAX?UINT64_MAX:(uint64_t)n;
}
static frontend_unified_events *allocate(qa_frontend *f,frontend_remote_unified *r,
    frontend_unified_media *m,const frontend_unified_event_options *opts,qa_error *e)
{
    if (!f || !r || !m || !opts || !opts->audio_owner || opts->audio_owner==QA_AUDIO_NO_OWNER ||
        !opts->audio_actor || !opts->presentation_validate || !opts->simulation_validate || !frontend_remote_unified_domain_read(r)) {
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified events need their actual media and audio route"); return NULL;
    }
    const qa_unified_limits *limits=qa_unified_session_limits(r->session);
    frontend_unified_events *o=calloc(1,sizeof(*o)+limits->message_bytes+2);
    if (!o) { frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating private unified event ledger"); return NULL; }
    o->frontend=f; o->replica=r; o->media=m; o->options=*opts;
    o->strings=r->strings;
    o->message_line=(char *)(o+1);
    o->records=qa_event_ring_create(limits->queued_reliable_bytes,16384,
        r->options.identity_capacity, e);
    if (!o->records) { free(o); return NULL; }
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(r);
    o->hud=f->seats[domain->physical_seat].hud;
    o->epoch=frontend_remote_unified_epoch(r);
    o->presentation_cursor=o->simulation_cursor=qa_event_ring_first(o->records);
    o->families_ready=true;
    return o;
}
bool frontend_unified_events_create(qa_frontend *f,frontend_remote_unified *r,
    frontend_unified_media *m,const frontend_unified_event_options *opts,frontend_unified_events **out,qa_error *e)
{
    if (!out || *out) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event output must be empty");
    frontend_unified_events *o=allocate(f,r,m,opts,e);
    if (!o) return false;
    if (!current(o,e)) { qa_event_ring_destroy(&o->records); free(o); return false; }
    o->owns_audio=true; *out=o; return true;
}
static bool resource_read_typed(frontend_unified_events *o, const qa_unified_resource_declaration *row,
    unified_event_resource *r, qa_error *e)
{
    const qa_unified_resource_state *key=&row->resource;
    qa_launch_resource held; qa_vfs *files; const qa_vfs_acquisition *opening;
    const qa_product *product=NULL; qa_vfs *actual;
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    bool okay=qa_executable_recipe_find_resource(recipe,key->content,key->path,key->byte_length,&held,&files,&opening) &&
        qa_executable_recipe_content_read(recipe,key->content,&actual,&product) && actual==files;
    if (okay) {
        okay=qa_strings_intern_cstr(o->strings,key->content,&r->content,e) &&
            qa_strings_intern_cstr(o->strings,key->path,&r->path,e) &&
            qa_strings_intern_cstr(o->strings,row->identity,&r->id,e);
        if (okay) {
            r->resource=held.resource;
            r->family=product->family==QA_GAME_Q1?QA_GAME_Q1:product->family==QA_GAME_Q2?QA_GAME_Q2:QA_GAME_Q3;
        }
    }
    if (!okay) { if (!e || e->code==QA_OK)
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Declared Source resource is outside its actual admitted recipe"); }
    return okay;
}
static bool declare_resources(frontend_unified_events *o, const qa_unified_resources_control *resources, qa_error *e)
{
    qa_event_transaction transaction={0};
    unified_received_event *record=NULL;
    unified_event_resource *head=NULL,**tail=&head;
    bool okay=true;
    for (size_t i=0;okay && i<resources->count;++i) {
        unified_event_resource value={0};
        okay=resource_read_typed(o,resources->values+i,&value,e);
        if (!okay) break;
        unified_event_resource *same=o->resources;
        while (same && same->id!=value.id) same=same->next;
        if (!same) {
            same=head;
            while (same && same->id!=value.id) same=same->next;
        }
        if (same) {
            okay=(same->resource==value.resource && same->content==value.content && same->path==value.path) ||
                frontend_unified_fail(e,QA_ERROR_FORMAT,"Source resource reference changed its admitted declaration");
            continue;
        }
        if (!record) {
            record=record_begin(o,&transaction,e);
            if (!record) { okay=false; break; }
        }
        unified_event_resource *r=qa_event_ring_alloc(&transaction,sizeof(*r),
            _Alignof(unified_event_resource),e);
        if (!r) { okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Received resource storage is full"); break; }
        *r=value; *tail=r; tail=&r->next;
    }
    if (!okay || !head) { qa_event_ring_abort(&transaction); return okay; }
    metadata_commit(o,&transaction,record);
    *tail=o->resources; o->resources=head;
    return true;
}
static unified_event_resource *resource_find(frontend_unified_events *o,const char *id)
{
    qa_string_id key=qa_strings_find(o->strings,(qa_bytes){(const uint8_t *)id,strlen(id)});
    for (unified_event_resource *r=o->resources;r;r=r->next)
        if (r->id==key) return r;
    return NULL;
}
static bool rows_valid(frontend_unified_events *o,const qa_unified_frame_events *events,bool children,qa_error *e)
{
    if (events->epoch!=o->epoch)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event control changed its source epoch");
    for (size_t i=0;i<events->presentation_count;++i) {
        const qa_unified_presentation_event *row=events->presentation+i;
        if (children && row->recipient.registry) {
            qa_actor_id received,viewer; uint32_t number;
            if (!actor(o,row->recipient,&received,e) ||
                !frontend_remote_unified_player(o->replica,&viewer,&number) || !qa_actor_id_equal(received,viewer))
                return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified presentation changed its admitted full actor recipient");
        }
        if (row->owner.provider) {
            unified_component_owner *c=component_find(o,row->owner.provider,row->owner.generation);
            if (c && qa_strings_find(o->strings,(qa_bytes){(const uint8_t *)row->content,strlen(row->content)})!=c->content)
                return frontend_unified_fail(e,QA_ERROR_FORMAT,"Component event changed its reliable content identity");
        }
        if (children && !o->options.presentation_validate(o->options.context,row,e)) return false;
    }
    for (size_t i=0;i<events->simulation_count;++i) {
        const qa_unified_simulation_event *row=events->simulation+i;
        if (row->private_audience && (row->client_slot!=o->replica->wire_client.slot ||
            row->client_generation!=o->replica->wire_client.generation))
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified simulation event changed its admitted wire client");
        if (row->payload.kind==QA_UNIFIED_SIMULATION_SOUND) {
            qa_actor_id actual;
            if (!resource_find(o,row->payload.value.sound.resource) ||
                (children && !actor(o,row->payload.value.sound.actor,&actual,e)))
                return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified sound has no declared resource or actual source actor");
            continue;
        }
        if (row->payload.kind==QA_UNIFIED_SIMULATION_DAMAGE) continue;
        if (row->payload.kind==QA_UNIFIED_SIMULATION_MESSAGE &&
            (row->payload.value.message.kind==QA_UNIFIED_MESSAGE_PRINT ||
             row->payload.value.message.kind==QA_UNIFIED_MESSAGE_CENTER_PRINT)) continue;
        if (children && !o->options.simulation_validate(o->options.context,row,e)) return false;
    }
    return true;
}
static bool record_finish(frontend_unified_events *o,qa_event_transaction *transaction,
    unified_received_event *record,qa_unified_document *document,qa_error *error)
{
    qa_unified_frame_events *events=(qa_unified_frame_events *)qa_unified_document_events(document);
    if (events->presentation_count) {
        record->mirrored=qa_event_ring_alloc(transaction,
            events->presentation_count*sizeof(*record->mirrored),_Alignof(bool),error);
        if (!record->mirrored) return false;
        memset(record->mirrored,0,events->presentation_count*sizeof(*record->mirrored));
    }
    if (!qa_unified_document_retain(document,&record->delivery.document,error)) return false;
    uint64_t id=qa_event_ring_commit(transaction,record);
    events->received=qa_event_ring_retain(o->records,id);
    return true;
}

bool frontend_unified_events_decode(frontend_unified_events *o,qa_bytes bytes,
    qa_unified_held **out,bool *ready,qa_error *error)
{
    *ready=false;
    if (!o) return true;
    qa_event_transaction transaction;
    unified_received_event *record=record_begin(o,&transaction,error);
    qa_unified_document *document=NULL;
    if (record) {
        record->delivery.wire.data=qa_event_ring_alloc(&transaction,bytes.size,1,error);
        if (record->delivery.wire.data) {
            memcpy(record->delivery.wire.data,bytes.data,bytes.size);
            record->delivery.wire.size=bytes.size;
        }
    }
    bool okay=record && record->delivery.wire.data && qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT,bytes,o->strings,NULL,
        &transaction,&document,error) && record_finish(o,&transaction,record,document,error);
    if (!okay) {
        qa_unified_document_destroy(document);
        qa_event_ring_abort(&transaction);
        if (transaction.blocked) { if (error) *error=(qa_error){0}; return true; }
        return false;
    }
    record->delivery.event_lease=qa_unified_document_events(document)->received;
    qa_event_lease_retain(record->delivery.event_lease);
    *out=&record->delivery; *ready=true;
    return true;
}

bool frontend_unified_events_control(frontend_unified_events *o,const qa_unified_document *doc,qa_error *e)
{
    if (!o || o->busy || o->prepared || !doc || qa_unified_document_type(doc)!=QA_UNIFIED_CONTROL_DOCUMENT ||
        !execution_current(o,e)) return false;
    const qa_unified_frame_events *events=qa_unified_document_events(doc);
    if (events) {
        /* Reliable events precede their FRAME. Source actor aliases and family
         * owners are qualified when that actual committed frame releases them. */
        if (!rows_valid(o,events,false,e) || !current(o,e)) return false;
        if (events->received) {
            unified_received_event *record=(unified_received_event *)qa_event_lease_record(events->received);
            record->published=true;
            return true;
        }
        /* Cold restored controls enter the same store before client use. */
        qa_event_transaction transaction;
        unified_received_event *record=record_begin(o,&transaction,e);
        qa_unified_frame_events *copy=record ? qa_event_ring_alloc(&transaction,sizeof(*copy),
            _Alignof(qa_unified_frame_events),e) : NULL;
        qa_unified_document *document=NULL;
        bool copied=false;
        if (copy) {
            memset(copy,0,sizeof(*copy));
            copied=qa_unified_record_clone_alloc(&qa_unified_events_layout,events,copy,
                qa_event_ring_alloc,&transaction,(qa_bytes){0},e);
            copy->strings=o->strings; copy->page_owned=true; qa_strings_retain(o->strings);
            copied=copied && qa_unified_document_create_events(&copy,&transaction,&document,e) &&
                record_finish(o,&transaction,record,document,e);
        }
        if (!copied) {
            qa_unified_frame_events_destroy(copy);
            qa_unified_document_destroy(document); qa_event_ring_abort(&transaction);
            return false;
        }
        record->published=true;
        qa_unified_document_destroy(document);
        return true;
    }
    const qa_unified_control *control=qa_unified_document_control(doc);
    if (control) {
        if (control->epoch!=o->epoch)
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified setup control changed its Source epoch");
        if (control->kind==QA_UNIFIED_CONTROL_RESOURCES) return declare_resources(o,&control->value.resources,e);
        return true;
    }
    uint32_t epoch;
    return (qa_unified_document_epoch(doc,&epoch,e) && epoch==o->epoch) ||
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified setup control changed its Source epoch");
}

static bool frame_values(const qa_unified_document *document,uint32_t *epoch,uint64_t *number,
    double *seconds,qa_error *error)
{
    const qa_unified_frame *frame=qa_unified_document_frame(document);
    if (!frame) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified event frame has no actual typed Source observation");
    *epoch=frame->epoch; *number=frame->world->source.number;
    *seconds=frame->world->presentation_seconds;
    return true;
}
bool frontend_unified_events_frame_prepare(frontend_unified_events *o,const qa_unified_document *doc,qa_error *e)
{
    if (!o || o->busy || o->prepared || !doc || qa_unified_document_type(doc)!=QA_UNIFIED_FRAME_DOCUMENT ||
        !execution_current(o,e)) return false;
    uint32_t epoch=0; uint64_t number=0; double seconds=0;
    if (!frame_values(doc,&epoch,&number,&seconds,e) || epoch!=o->epoch) return false;
    if (o->has_frame && number<=o->frame) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event publication must advance its actual frame");
    if (!qa_unified_document_retain(doc,&o->prepared_document,e)) return false;
    o->prepared_frame=number; o->prepared_seconds=seconds; o->prepared=true; return true;
}
void frontend_unified_events_frame_commit(frontend_unified_events *o)
{
    if (!o || !o->prepared || o->busy) return;
    o->frame=o->prepared_frame; o->seconds=o->prepared_seconds; o->has_frame=true; o->prepared=false;
    qa_unified_document_destroy(o->prepared_document);o->prepared_document=NULL;
}
bool frontend_unified_events_frame_ready(frontend_unified_events *o,const qa_unified_document *doc,qa_error *e)
{
    if (!o || o->busy || !o->prepared || !doc ||
        qa_unified_document_type(doc)!=QA_UNIFIED_FRAME_DOCUMENT ||
        (o->hud && !qa_hud_idle(o->hud)) || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified events have no returned prepared frame");
    uint32_t epoch=0; uint64_t number=0; double seconds=0;
    if (!frame_values(doc,&epoch,&number,&seconds,e)) return false;
    return (epoch==o->epoch && number==o->prepared_frame && seconds==o->prepared_seconds) ||
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified prepared event frame differs from publication");
}
void frontend_unified_events_frame_abort(frontend_unified_events *o)
{ if (o && !o->busy) {o->prepared=false;qa_unified_document_destroy(o->prepared_document);o->prepared_document=NULL;} }
static bool mirrored(frontend_unified_events *o,uint64_t sequence)
{
    const qa_unified_frame_events *events=qa_unified_document_events(o->delivery->delivery.document);
    for (size_t i=0;i<events->presentation_count;++i)
        if (events->presentation[i].sequence==sequence) return o->delivery->mirrored[i];
    return false;
}
static bool message(frontend_unified_events *o,const char *text_value,qa_error *e)
{
    if (!current(o,e)) return false;
    /* Decoded text fits the negotiated message envelope. Keep the newline in
     * the same print call so notify, wrapping and redirects keep their policy. */
    size_t length=strlen(text_value);
    memcpy(o->message_line,text_value,length);
    o->message_line[length]='\n'; o->message_line[length+1]=0;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_console_emit(domain->console,&domain->command_context,o->message_line);
    return true;
}
static bool sound(frontend_unified_events *o,const qa_unified_sound_event *event,double ms,qa_error *e)
{
    unified_event_resource *r=resource_find(o,event->resource);
    if (!r) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified sound precedes its actual resource declaration");
    qa_actor_id actual;
    if (!actor(o,event->actor,&actual,e)) return false;
    uint64_t audio=QA_AUDIO_NO_ACTOR;
    if (actual.registry && !o->options.audio_actor(o->options.context,actual,&audio,e)) return false;
    if (!r->asset) {
        qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *bank;
        if (!frontend_unified_media_bank(o->media,qa_strings_cstr(o->strings,r->content),&images,&materials,&fonts,&bank,e) ||
            !qa_audio_bank_register(bank,qa_strings_cstr(o->strings,r->path),r->family,&r->asset,e)) return false;
        if (!r->asset) return frontend_unified_fail(e,QA_ERROR_NOT_FOUND,"Declared Unified sound is absent from its actual bank");
        const qa_resource *resource=qa_audio_asset_resource(r->asset);
        if (resource!=r->resource ||
            qa_resource_bytes(resource).size!=qa_resource_bytes(r->resource).size)
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified sound bank differs from its declared resource authority");
    }
    if (!o->frontend->audio) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified sound has no actual output engine");
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_audio_play play={.sample=qa_audio_asset_sample(r->asset),.asset=r->asset,
        .resource_id=qa_resource_id(r->resource),.family=r->family,
        .actor=audio,.owner=o->options.audio_owner,.audience=domain->physical_seat,.origin_kind=QA_AUDIO_FIXED,
        .origin=event->origin,.channel=event->channel,.volume=event->volume,.attenuation=event->attenuation,
        .delay_seconds=event->delay_seconds,
        .has_server_time=true,.server_milliseconds=ms};
    int32_t signed_tick;
    return qa_audio_source_milliseconds(ms,&signed_tick,e) && current(o,e) &&
        qa_audio_engine_play(o->frontend->audio,&play,signed_tick,e);
}
static bool apply_presentation(frontend_unified_events *o,const qa_unified_presentation_event *row,
    bool *mirrors,qa_error *e)
{
    *mirrors=false;
    if (!o->options.presentation)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Unified source presentation consumer is not installed");
    if (!o->options.presentation(o->options.context,row,mirrors,e)) return false;
    if (row->payload.kind==QA_UNIFIED_PRESENTATION_OWNER) {
        const qa_unified_owner_event *event=&row->payload.value.owner;
        unified_component_owner *c=component_find(o,event->owner.provider,event->owner.generation);
        if (c) { c->retired=event->kind==QA_UNIFIED_OWNER_RETIRED; c->cancelled=false; }
    }
    return true;
}
static bool apply_simulation(frontend_unified_events *o,const qa_unified_simulation_event *row,qa_error *e)
{
    double ms=row->milliseconds?row->time:row->time*1000;
    if (!isfinite(ms)) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified event source time overflow");
    if (row->payload.kind==QA_UNIFIED_SIMULATION_SOUND) return sound(o,&row->payload.value.sound,ms,e);
    /* The server committed these mutations before publishing FRAME state.
     * A client consumes the outcome without applying game damage again. */
    if (row->payload.kind==QA_UNIFIED_SIMULATION_DAMAGE) return true;
    if (row->payload.kind==QA_UNIFIED_SIMULATION_MESSAGE) {
        if (row->payload.linked_presentation && mirrored(o,row->payload.source_presentation_sequence)) return true;
        qa_unified_message_kind kind=row->payload.value.message.kind;
        if (kind==QA_UNIFIED_MESSAGE_PRINT || kind==QA_UNIFIED_MESSAGE_CENTER_PRINT)
            return message(o,row->payload.value.message.text,e);
    }
    if (!o->options.simulation)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Unified simulation media consumer is not installed");
    return o->options.simulation(o->options.context,row,e);
}
bool frontend_unified_events_enter(frontend_unified_events *o,qa_error *e)
{
    if (!o || o->busy || o->prepared || !execution_current(o,e)) return false;
    o->busy=true; bool okay=true;
    while (okay && !o->replica->retired) {
        metadata_retire(o);
        uint64_t id=qa_event_ring_first(o->records);
        if (id==qa_event_ring_next(o->records)) break;
        unified_received_event *record=(unified_received_event *)qa_event_ring_at(o->records,id);
        const qa_unified_frame_events *events=qa_unified_document_events(record->delivery.document);
        if (!record->published || !o->has_frame || events->frame>o->frame) break;
        if (!rows_valid(o,events,true,e)) { okay=false; break; }
        o->delivery=record;
        while (okay && !o->replica->retired && o->presentation_cursor==id &&
            o->presentation_at<events->presentation_count) {
            const qa_unified_presentation_event *row=events->presentation+o->presentation_at;
            if (!o->has_presentation_sequence || row->sequence>o->presentation_sequence) {
                bool mirrors;
                okay=current(o,e) && apply_presentation(o,row,&mirrors,e);
                if (okay) { record->mirrored[o->presentation_at]=mirrors;
                    o->presentation_sequence=row->sequence; o->has_presentation_sequence=true; }
            }
            if (okay) ++o->presentation_at;
        }
        if (o->presentation_cursor==id && o->presentation_at==events->presentation_count) {
            ++o->presentation_cursor; o->presentation_at=0;
        }
        while (okay && !o->replica->retired && o->simulation_cursor==id &&
            o->simulation_at<events->simulation_count) {
            const qa_unified_simulation_event *row=events->simulation+o->simulation_at;
            if (!o->has_simulation_sequence || row->sequence>o->simulation_sequence) {
                okay=current(o,e) && apply_simulation(o,row,e);
                if (okay) { o->simulation_sequence=row->sequence; o->has_simulation_sequence=true; }
            }
            if (okay) ++o->simulation_at;
        }
        if (o->simulation_cursor==id && o->simulation_at==events->simulation_count) {
            ++o->simulation_cursor; o->simulation_at=0;
        }
        o->delivery=NULL;
        if (o->presentation_cursor>id && o->simulation_cursor>id) {
            qa_unified_document_destroy(record->delivery.document);
            qa_event_ring_retire(o->records,o->presentation_cursor<o->simulation_cursor?
                o->presentation_cursor:o->simulation_cursor);
        }
    }
    o->delivery=NULL; o->busy=false; return okay;
}
bool frontend_unified_events_draw(frontend_unified_events *o,qa_scene_rect viewport,bool center_owned,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !frame || o->busy || !o->has_frame || !execution_current(o,e)) return false;
    qa_actor_id player; uint32_t source;
    if (!frontend_remote_unified_player(o->replica,&player,&source)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    o->busy=true;
    bool okay=qa_hud_draw_messages(o->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,
        .time_ns=clock_ns(o->seconds),.viewport=viewport,.safe_area=viewport,.scale=1,.visible=true,
        .center_owned=center_owned},frame,e);
    o->busy=false; return okay;
}
bool frontend_unified_events_checkpoint_ready(const frontend_unified_events *o)
{ return !o || (!o->busy && (!o->hud || qa_hud_idle(o->hud))); }
bool frontend_unified_events_idle(const frontend_unified_events *o)
{ return (!o || !o->prepared) && frontend_unified_events_checkpoint_ready(o); }
bool frontend_unified_events_destroy(frontend_unified_events **slot,qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_events *o=*slot;
    if (!frontend_unified_events_idle(o)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified event owner still has a live callback");
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (o->owns_audio && o->frontend->audio && !qa_audio_engine_stop_owner(o->frontend->audio,o->options.audio_owner,d->physical_seat,e)) return false;
    for (uint64_t id=qa_event_ring_first(o->records);id<qa_event_ring_next(o->records);++id) {
        const unified_received_event *record=qa_event_ring_at(o->records,id);
        qa_unified_document_destroy(record->delivery.document);
    }
    for (unified_event_resource *r=o->resources;r;r=r->next) qa_audio_asset_release(r->asset);
    while (o->retained) {
        unified_received_event *record=o->retained; o->retained=record->next_retained;
        qa_event_lease_release(record->lease);
    }
    qa_event_ring_destroy(&o->records);
    free(o); *slot=NULL; return true;
}

uint64_t frontend_unified_events_audio_owner(const frontend_unified_events *o)
{ return o?o->options.audio_owner:QA_AUDIO_NO_OWNER; }
bool frontend_unified_events_audio_actor(frontend_unified_events *o,qa_actor_id actor_id,uint64_t *id,qa_error *e)
{
    if (!o || !id) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified audio identity needs its actual CLIENT owner");
    if (!actor_id.registry) { *id=QA_AUDIO_NO_ACTOR; return true; }
    qa_saved_actor_id wire;
    if (!frontend_remote_unified_wire_actor(o->replica,actor_id,&wire))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified audio actor is outside its private received ledger");
    return o->options.audio_actor(o->options.context,actor_id,id,e);
}
bool frontend_unified_events_sound_path(frontend_unified_events *o,const char *content,const char *path,
    qa_actor_id actor_id,qa_vec3 origin,double ms,int32_t channel,float volume,float attenuation,double delay,qa_error *e)
{
    if (!o || !content || !path || !isfinite(ms) || !isfinite(delay) || !execution_current(o,e)) return false;
    qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *bank;
    qa_vfs *files; const qa_product *product;
    if (!qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&product,e) ||
        !frontend_unified_media_bank(o->media,content,&images,&materials,&fonts,&bank,e)) return false;
    qa_game_family family=product->family==QA_GAME_Q1?QA_GAME_Q1:product->family==QA_GAME_Q2?QA_GAME_Q2:QA_GAME_Q3;
    qa_audio_asset *asset=NULL;
    if (!qa_audio_bank_register(bank,path,family,&asset,e)) return false;
    if (!asset) return true; /* Retains the genuine bank's optional-miss policy. */
    uint64_t audio;
    bool okay=frontend_unified_events_audio_actor(o,actor_id,&audio,e) && current(o,e);
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (okay && !o->frontend->audio) okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified CLIENT sound has no output engine");
    if (okay) {
        int32_t signed_tick;
        qa_audio_play play={.sample=qa_audio_asset_sample(asset),.asset=asset,
            .resource_id=qa_resource_id(qa_audio_asset_resource(asset)),.name=path,.family=family,
            .actor=audio,.owner=o->options.audio_owner,.audience=d->physical_seat,.origin_kind=QA_AUDIO_FIXED,
            .origin=origin,.channel=channel,.volume=volume,.attenuation=attenuation,.delay_seconds=delay,
            .has_server_time=true,.server_milliseconds=ms};
        okay=qa_audio_source_milliseconds(ms,&signed_tick,e) &&
            qa_audio_engine_play(o->frontend->audio,&play,signed_tick,e);
    }
    qa_audio_asset_release(asset); return okay;
}
bool frontend_unified_events_sound_mirrored(frontend_unified_events *o,const qa_unified_presentation_event *row,
    bool *out,qa_error *e)
{
    if (!o || !row || !out) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Sound linkage needs its actual delivery record");
    *out=false;
    qa_unified_sound_event emitted={0};
    switch (row->payload.kind) {
    case QA_UNIFIED_PRESENTATION_BUILTIN: {
        const qa_builtin_event *source=&row->payload.value.builtin;
        if (source->kind!=QA_BUILTIN_SOUND ||
            (source->family==QA_GAME_Q2 && (source->flags==1 || source->flags==2))) return true;
        bool ambient=source->family==QA_GAME_Q1 && (source->flags & 1u)!=0;
        emitted=(qa_unified_sound_event){.resource=(char *)qa_strings_cstr(o->strings,source->resource),
            .actor=ambient?(qa_actor_id){0}:source->actor,.origin=source->origin,
            .channel=ambient?0:source->channel,.volume=source->volume,.attenuation=source->attenuation};
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q2_PROTOCOL: {
        const qa_unified_q2_protocol_event *source=&row->payload.value.q2_protocol;
        if (source->kind!=QA_Q2_SVC_SOUND) return true;
        emitted=(qa_unified_sound_event){.resource=source->resource,.actor=source->actor,.origin=source->origin,
            .channel=source->channel,.volume=source->volume,.attenuation=source->attenuation,
            .delay_seconds=source->delay_seconds};
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q3: {
        const qa_unified_q3_event *source=&row->payload.value.q3;
        if (source->kind!=QA_UNIFIED_Q3_SOUND || source->loop) return true;
        emitted=(qa_unified_sound_event){.resource=source->resource,.actor=source->actor,.origin=source->origin,
            .channel=source->channel,.volume=source->volume,.attenuation=1};
        break;
    }
    default: return true;
    }
    const qa_unified_frame_events *events=o->delivery?qa_unified_document_events(o->delivery->delivery.document):NULL;
    if (!o->busy || !events || o->presentation_at>=events->presentation_count ||
        events->presentation+o->presentation_at!=row)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Sound linkage is outside its actual retained presentation delivery");
    for (size_t i=0;i<events->simulation_count;++i) {
        const qa_unified_simulation_event *simulation=events->simulation+i;
        if (simulation->payload.kind!=QA_UNIFIED_SIMULATION_SOUND) continue;
        const qa_unified_sound_event *sound_event=&simulation->payload.value.sound;
        unified_event_resource *resource=resource_find(o,sound_event->resource);
        if (!resource) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Linked sound lost its declared resource");
        if (resource->content!=qa_strings_find(o->strings,(qa_bytes){(const uint8_t *)row->content,strlen(row->content)})) continue;
        const char *path=qa_strings_cstr(o->strings,resource->path);
        bool same_path=!strcmp(path,emitted.resource) ||
            (!strncmp(path,"sound/",6) && !strcmp(path+6,emitted.resource)) ||
            (emitted.resource[0]=='#' && !strcmp(path,emitted.resource+1));
        double seconds=simulation->milliseconds?simulation->time/1000:simulation->time;
        if (same_path && seconds==row->seconds && sound_event->channel==emitted.channel &&
            sound_event->volume==emitted.volume && sound_event->attenuation==emitted.attenuation &&
            sound_event->delay_seconds==emitted.delay_seconds &&
            sound_event->origin.x==emitted.origin.x && sound_event->origin.y==emitted.origin.y &&
            sound_event->origin.z==emitted.origin.z && qa_actor_id_equal(sound_event->actor,emitted.actor)) {
            *out=true; return true;
        }
    }
    return true;
}
bool frontend_unified_events_sound_loop_path(frontend_unified_events *o,const char *content,const char *path,
    qa_actor_id actor_id,qa_vec3 origin,qa_vec3 velocity,double ms,int32_t channel,
    float volume,float attenuation,int32_t frame_number,bool persistent,qa_error *e)
{
    if (!o || !content || !path || !isfinite(ms) || !qa_vec_finite(origin) || !qa_vec_finite(velocity) ||
        !isfinite(volume) || !isfinite(attenuation) || !execution_current(o,e)) return false;
    qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *bank;
    qa_vfs *files; const qa_product *product;
    if (!qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&product,e) ||
        !frontend_unified_media_bank(o->media,content,&images,&materials,&fonts,&bank,e)) return false;
    qa_game_family family=product->family==QA_GAME_Q1?QA_GAME_Q1:product->family==QA_GAME_Q2?QA_GAME_Q2:QA_GAME_Q3;
    qa_audio_asset *asset=NULL;
    if (!qa_audio_bank_register(bank,path,family,&asset,e)) return false;
    if (!asset) return true;
    uint64_t audio; bool okay=frontend_unified_events_audio_actor(o,actor_id,&audio,e) && current(o,e);
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (okay && !o->frontend->audio) okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified loop has no output engine");
    if (okay) {
        qa_audio_loop loop={.sound={.sample=qa_audio_asset_sample(asset),.asset=asset,
            .resource_id=qa_resource_id(qa_audio_asset_resource(asset)),.name=path,.family=family,
            .actor=audio,.owner=o->options.audio_owner,.audience=d->physical_seat,.origin_kind=QA_AUDIO_FIXED,
            .origin=origin,.channel=channel,.volume=volume,.attenuation=attenuation,
            .has_server_time=true,.server_milliseconds=ms},.velocity=velocity,.frame_number=frame_number,.persistent=persistent};
        okay=qa_audio_engine_loop(o->frontend->audio,&loop,e);
    }
    qa_audio_asset_release(asset); return okay;
}
bool frontend_unified_events_sound_stop_loop(frontend_unified_events *o,qa_actor_id actor_id,qa_error *e)
{
    uint64_t audio;
    if (!o || !o->frontend->audio || !current(o,e) || !frontend_unified_events_audio_actor(o,actor_id,&audio,e)) return false;
    return qa_audio_engine_stop_loop(o->frontend->audio,audio,o->options.audio_owner,
        frontend_remote_unified_domain_read(o->replica)->physical_seat,e);
}
