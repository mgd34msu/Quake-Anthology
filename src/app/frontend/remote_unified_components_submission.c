#include "remote_unified_components_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void q3remote_component_admissions_clear(remote_component *row)
{
    if(row->admissions) for(size_t i=0;i<row->admission_count;++i) { free(row->admissions[i].polygons); free(row->admissions[i].lights); }
    free(row->admissions); row->admissions=NULL; row->admission_count=0;
    row->submission_bank=NULL; row->submission_cycle=0; row->submitted=false; row->admissions_ready=false;
}
void frontend_unified_components_submission_release(frontend_unified_components *owner,const qa_q3_source_scene_bank *bank)
{
    if(!owner||!bank) return;
    for(size_t i=0;i<q3remote_component_physical_count(owner);++i) {
        remote_component *row=q3remote_component_physical_at(owner,i);
        if(row&&row->submission_bank==bank) q3remote_component_admissions_clear(row);
    }
}

static bool prepare(remote_component *row,qa_q3_source_scene_bank *bank,uint64_t cycle,qa_error *error)
{
    if(row->submission_bank==bank&&row->submission_cycle==cycle) return true;
    q3remote_component_admissions_clear(row);
    size_t count=0;
    if(!frontend_component_scene_packet_count(row->parent->frontend,row->frontend_identity,row->draw_sequence,&count,error)) return false;
    if(count>SIZE_MAX/sizeof(*row->admissions))
        return q3remote_component_fail(error,QA_ERROR_MEMORY,"Component packet receipt extent overflow");
    row->admissions=count?calloc(count,sizeof(*row->admissions)):NULL;
    if(count&&!row->admissions) return q3remote_component_fail(error,QA_ERROR_MEMORY,"Retaining component packet admissions");
    row->admission_count=count;
    for(size_t i=0;i<count;++i) {
        frontend_component_scene_packet packet;
        if(!q3remote_component_packet_read(row,i,&packet,error)) return false;
        remote_component_packet_admission *receipt=row->admissions+i;
        receipt->source_time=packet.definition.time;
        if(packet.polygon_count>SIZE_MAX/sizeof(*receipt->polygons))
            return q3remote_component_fail(error,QA_ERROR_MEMORY,"Component polygon receipt extent overflow");
        receipt->polygons=packet.polygon_count?calloc(packet.polygon_count,sizeof(*receipt->polygons)):NULL;
        if(packet.polygon_count&&!receipt->polygons)
            return q3remote_component_fail(error,QA_ERROR_MEMORY,"Retaining component polygon admissions");
        receipt->polygon_count=packet.polygon_count;
        if(packet.light_count>SIZE_MAX/sizeof(*receipt->lights))
            return q3remote_component_fail(error,QA_ERROR_MEMORY,"Component light receipt extent overflow");
        receipt->lights=packet.light_count?calloc(packet.light_count,sizeof(*receipt->lights)):NULL;
        if(packet.light_count&&!receipt->lights) return q3remote_component_fail(error,QA_ERROR_MEMORY,"Retaining component light admissions");
        receipt->light_count=packet.light_count;
    }
    row->submission_bank=bank; row->submission_cycle=cycle; return true;
}

static bool admit(remote_component *row,const frontend_component_scene_packet *packet,
    remote_component_packet_admission *receipt,qa_error *error)
{
    qa_q3_source_scene_bank *bank=row->submission_bank;
    qa_q3_source_scene_membership membership;
    if(!qa_q3_source_scene_bank_membership(bank,&membership))
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component packet lost its actual scene bank");
    if(!receipt->entity_started) { receipt->first_entity=membership.entities; receipt->entity_started=true; }
    if(receipt->entity_scanned<packet->entity_count&&
        (receipt->first_entity>membership.entities||receipt->entity_count!=membership.entities-receipt->first_entity))
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Partial component entity range was interrupted by another admission");
    while(receipt->entity_scanned<packet->entity_count) {
        if(!qa_q3_source_scene_bank_entity_capacity(bank)) { receipt->entity_scanned=packet->entity_count; break; }
        uint32_t ordinal=0; bool accepted=false;
        if(!packet->entities||!qa_q3_source_scene_bank_entity(bank,row->assets,packet->entities+receipt->entity_scanned,&ordinal,&accepted,error)) return false;
        if(accepted) ++receipt->entity_count;
        ++receipt->entity_scanned;
    }
    if(receipt->polygon_count!=packet->polygon_count)
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component polygon receipt changed its actual output extent");
    for(size_t i=0;i<packet->polygon_count;++i) {
        remote_component_polygon_admission *saved=receipt->polygons+i;
        if(saved->reached) continue;
        const qa_q3_scene_polygon *polygon=packet->polygons+i;
        if(!polygon->count||!polygon->shader||!qa_q3_source_scene_bank_poly_capacity(bank,polygon->count)) { saved->reached=true; continue; }
        if(polygon->first>packet->vertex_count||polygon->count>packet->vertex_count-polygon->first||
            !packet->vertices||polygon->count>SIZE_MAX/sizeof(qa_q3_poly_vertex))
            return q3remote_component_fail(error,QA_ERROR_FORMAT,"Component polygon leaves its retained vertex span");
        qa_q3_poly_vertex *raw=malloc(polygon->count*sizeof(*raw));
        if(!raw) return q3remote_component_fail(error,QA_ERROR_MEMORY,"Retaining actual component polygon vertices");
        for(size_t j=0;j<polygon->count;++j) {
            const qa_scene_vertex *vertex=packet->vertices+polygon->first+j;
            raw[j]=(qa_q3_poly_vertex){.position=vertex->position,.texcoord=vertex->texcoord,
                .color={(uint8_t)lroundf(vertex->color.x*255.0f),(uint8_t)lroundf(vertex->color.y*255.0f),
                    (uint8_t)lroundf(vertex->color.z*255.0f),(uint8_t)lroundf(vertex->color.w*255.0f)}};
        }
        bool accepted=false;
        bool okay=qa_q3_source_scene_bank_membership(bank,&membership)&&
            qa_q3_source_scene_bank_poly(bank,row->assets,polygon->shader,raw,polygon->count,&polygon->fog,&accepted,error);
        free(raw); if(!okay) return false;
        saved->ordinal=membership.polygons; saved->admitted=accepted; saved->reached=true;
    }
    return true;
}

static bool draw(remote_component *row,remote_component_packet_admission *receipt,qa_q3_presentation *presentation,
    const qa_q3_scene_options *options,qa_scene_frame *frame,bool reflected,qa_error *error)
{
    qa_q3_source_scene_membership membership;
    if(!qa_q3_source_scene_bank_membership(row->submission_bank,&membership)||
        receipt->first_entity>membership.entities||receipt->entity_count>membership.entities-receipt->first_entity)
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component replay lost its actual entity range");
    for(size_t i=reflected?0:receipt->entity_emitted;i<receipt->entity_count;++i) {
        uint32_t ordinal=receipt->first_entity+(uint32_t)i; qa_q3_source_entity_cell cell;
        if(!qa_q3_source_scene_bank_entity_read(row->submission_bank,ordinal,&cell)||cell.assets!=row->assets)
            return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component entity lost its actual registry cell");
        if(!qa_q3_presentation_source_component_entity(presentation,cell.assets,&cell.value,receipt->source_time,options,ordinal,frame,error)) return false;
        if(!reflected) ++receipt->entity_emitted;
    }
    for(size_t i=0;i<receipt->polygon_count;++i) {
        remote_component_polygon_admission *saved=receipt->polygons+i;
        if(!saved->admitted||(!reflected&&saved->emitted)) continue;
        qa_q3_source_polygon_cell cell; const qa_q3_poly_vertex *raw=NULL;
        if(saved->ordinal>=membership.polygons||!qa_q3_source_scene_bank_poly_read(row->submission_bank,saved->ordinal,&cell,&raw)||
            cell.assets!=row->assets||(cell.count&&sizeof(qa_scene_vertex)>SIZE_MAX/cell.count))
            return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component polygon lost its actual registry cell");
        qa_scene_vertex *vertices=qa_arena_alloc(&frame->storage,(size_t)cell.count*sizeof(*vertices),_Alignof(qa_scene_vertex),error);
        if(!vertices) return false;
        for(uint32_t j=0;j<cell.count;++j) vertices[j]=(qa_scene_vertex){.position=raw[j].position,.texcoord=raw[j].texcoord,
            .color={(float)raw[j].color[0]/255.0f,(float)raw[j].color[1]/255.0f,(float)raw[j].color[2]/255.0f,(float)raw[j].color[3]/255.0f}};
        if(!qa_q3_presentation_source_component_poly(presentation,cell.assets,cell.shader,vertices,cell.count,&cell.fog,
            receipt->source_time,options,frame,error)) return false;
        if(!reflected) saved->emitted=true;
    }
    return true;
}

static bool submit(frontend_unified_components *owner,qa_q3_presentation *presentation,qa_q3_source_scene_bank *bank,
    const qa_q3_scene_options *options,qa_scene_frame *frame,bool reflected,qa_error *error)
{
    uint64_t cycle=0;
    if(!frontend_unified_components_current(owner)||!frontend_unified_components_idle(owner)||!presentation||!options||!frame||
        !qa_q3_source_scene_bank_cycle(bank,&cycle)||options->world.view.mirror!=reflected)
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component submission requires its actual recipient and scene cycle");
    owner->busy=true; bool okay=true;
    for(size_t i=0;okay&&i<owner->count;++i) {
        remote_component *row=owner->rows[i]; if(!row->frame) continue;
        if(!row->advanced||row->draw_sequence!=frame->sequence) { okay=q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component submission lacks its completed physical frame"); break; }
        if(reflected) {
            if(!row->admissions_ready||row->submission_bank!=bank||row->submission_cycle!=cycle) {
                okay=q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component reflection lacks its original physical admissions"); break;
            }
        } else if(!prepare(row,bank,cycle,error)) { okay=false; break; }
        for(size_t j=0;okay&&j<row->admission_count;++j) {
            remote_component_packet_admission *receipt=row->admissions+j;
            if(!reflected) {
                frontend_component_scene_packet packet;
                okay=q3remote_component_packet_read(row,j,&packet,error)&&admit(row,&packet,receipt,error);
            }
            if(okay) okay=draw(row,receipt,presentation,options,frame,reflected,error);
        }
        if(okay&&!reflected) { row->submitted=true; row->admissions_ready=true; }
    }
    owner->busy=false; return okay;
}
bool frontend_unified_components_submit(frontend_unified_components *owner,qa_q3_presentation *presentation,
    qa_q3_source_scene_bank *bank,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{ return submit(owner,presentation,bank,options,frame,false,error); }
bool frontend_unified_components_submit_reflected(frontend_unified_components *owner,qa_q3_presentation *presentation,
    qa_q3_source_scene_bank *bank,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{ return submit(owner,presentation,bank,options,frame,true,error); }

bool frontend_unified_components_prepare_submission(frontend_unified_components *owner,qa_q3_source_scene_bank *bank,
    qa_scene_frame *frame,qa_error *error)
{
    uint64_t cycle=0;
    if(!frontend_unified_components_current(owner)||!frontend_unified_components_idle(owner)||!frame||
        !qa_q3_source_scene_bank_cycle(bank,&cycle))
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component admission requires its actual returned frame and bank");
    owner->busy=true; bool okay=true;
    for(size_t i=0;okay&&i<owner->count;++i) {
        remote_component *row=owner->rows[i]; if(!row->frame) continue;
        if(!row->advanced||row->draw_sequence!=frame->sequence) { okay=q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component admission lacks its completed frame"); break; }
        okay=prepare(row,bank,cycle,error);
        for(size_t j=0;okay&&j<row->admission_count;++j) {
            frontend_component_scene_packet packet;
            okay=q3remote_component_packet_read(row,j,&packet,error)&&admit(row,&packet,row->admissions+j,error);
        }
        if(okay) row->admissions_ready=true;
    }
    owner->busy=false; return okay;
}

bool frontend_unified_components_lights_bank(frontend_unified_components *owner,qa_q3_source_scene_bank *bank,
    const qa_scene_light **out,size_t *count,qa_error *error)
{
    uint64_t cycle=0;
    if(!out||!count||!frontend_unified_components_current(owner)||!frontend_unified_components_idle(owner)||
        !qa_q3_source_scene_bank_cycle(bank,&cycle))
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component lights require their actual scene cycle");
    qa_scene_light joined[QA_Q3_SOURCE_LIGHT_CAPACITY];
    owner->busy=true; bool okay=true; size_t used=0;
    for(size_t i=0;okay&&i<owner->count;++i) {
        remote_component *row=owner->rows[i]; if(!row->frame) continue;
        okay=prepare(row,bank,cycle,error);
        for(size_t j=0;okay&&j<row->admission_count;++j) {
            frontend_component_scene_packet packet;
            okay=q3remote_component_packet_read(row,j,&packet,error); if(!okay) break;
            remote_component_packet_admission *receipt=row->admissions+j;
            if(receipt->light_count!=packet.light_count) { okay=q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component light extent changed"); break; }
            for(size_t k=0;okay&&k<receipt->light_count;++k) {
                remote_component_light_admission *saved=receipt->lights+k;
                if(!saved->reached) {
                    qa_q3_source_scene_membership membership;
                    okay=qa_q3_source_scene_bank_membership(bank,&membership)&&
                        qa_q3_source_scene_bank_light(bank,row->assets,packet.lights+k,&saved->admitted,error);
                    if(!okay) break;
                    saved->ordinal=membership.lights; saved->reached=true;
                }
                if(saved->admitted) {
                    qa_q3_source_light_cell cell;
                    if(used>=QA_Q3_SOURCE_LIGHT_CAPACITY||!qa_q3_source_scene_bank_light_read(bank,saved->ordinal,&cell)||cell.assets!=row->assets) {
                        okay=q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component light lost its actual bank cell"); break;
                    }
                    joined[used++]=cell.value;
                }
            }
        }
    }
    owner->busy=false;
    if(!okay) return false;
    memcpy(owner->lights,joined,used*sizeof(*joined));
    owner->light_count=used; *out=owner->lights; *count=used; return true;
}

bool frontend_unified_components_submission_restore(frontend_unified_components *owner,qa_q3_source_scene_bank *bank,qa_error *error)
{
    uint64_t cycle=0; qa_q3_source_scene_membership membership;
    if(!frontend_unified_components_retained_current(owner)||
        !qa_q3_source_scene_bank_cycle(bank,&cycle)||!qa_q3_source_scene_bank_membership(bank,&membership))
        return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Component admission import requires its actual restored scene bank");
    for(size_t i=0;i<owner->count;++i) {
        remote_component *row=owner->rows[i]; if(!row->submission_cycle) continue;
        if(row->acquired||(row->scene&&!application_q3_scene_idle(row->scene))||
            (row->frontend.owner&&!row->frontend.idle(row->frontend.owner)))
            return q3remote_component_fail(error,QA_ERROR_ARGUMENT,"Saved component admission retains an entered renderer");
        size_t count=0;
        if(!row->advanced||row->submission_cycle!=cycle||!row->frontend.owner||
            !frontend_component_scene_packet_count(owner->frontend,row->frontend_identity,row->draw_sequence,&count,error)||count!=row->admission_count)
            return error&&error->code!=QA_OK?false:q3remote_component_fail(error,QA_ERROR_FORMAT,"Saved component admission lost its actual private packet cycle");
        for(size_t j=0;j<count;++j) {
            frontend_component_scene_packet packet; remote_component_packet_admission *receipt=row->admissions+j;
            if(!q3remote_component_packet_read(row,j,&packet,error)) return false;
            if(receipt->source_time!=packet.definition.time||receipt->polygon_count!=packet.polygon_count||
                receipt->light_count!=packet.light_count||receipt->entity_scanned>packet.entity_count||
                (row->admissions_ready&&(!receipt->entity_started||receipt->entity_scanned!=packet.entity_count))||
                receipt->first_entity>membership.entities||receipt->entity_count>membership.entities-receipt->first_entity)
                return q3remote_component_fail(error,QA_ERROR_FORMAT,"Saved component receipt changed its genuine packet extent");
            for(size_t k=0;k<receipt->entity_count;++k) {
                qa_q3_source_entity_cell cell;
                if(!qa_q3_source_scene_bank_entity_read(bank,receipt->first_entity+(uint32_t)k,&cell)||cell.assets!=row->assets)
                    return q3remote_component_fail(error,QA_ERROR_FORMAT,"Saved component entity lost its genuine registry cell");
            }
            for(size_t k=0;k<receipt->polygon_count;++k) {
                remote_component_polygon_admission *saved=receipt->polygons+k;
                if(!saved->admitted) continue;
                qa_q3_source_polygon_cell cell; const qa_q3_poly_vertex *vertices=NULL;
                if(saved->ordinal>=membership.polygons||!qa_q3_source_scene_bank_poly_read(bank,saved->ordinal,&cell,&vertices)||
                    cell.assets!=row->assets||cell.shader!=packet.polygons[k].shader||cell.count!=packet.polygons[k].count)
                    return q3remote_component_fail(error,QA_ERROR_FORMAT,"Saved component polygon lost its genuine registry cell");
            }
            for(size_t k=0;k<receipt->light_count;++k) {
                remote_component_light_admission *saved=receipt->lights+k;
                if(!saved->admitted) continue;
                qa_q3_source_light_cell cell;
                if(saved->ordinal>=membership.lights||!qa_q3_source_scene_bank_light_read(bank,saved->ordinal,&cell)||cell.assets!=row->assets)
                    return q3remote_component_fail(error,QA_ERROR_FORMAT,"Saved component light lost its genuine registry cell");
            }
        }
    }
    for(size_t i=0;i<owner->count;++i) if(owner->rows[i]->submission_cycle) owner->rows[i]->submission_bank=bank;
    return true;
}
