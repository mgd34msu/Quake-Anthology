#include "renderer_registries.h"
#include "qa/material_source_scratch.h"
#include "qa/render_controls.h"
#include "qa/q3_source_scene_bank.h"

struct frontend_renderer_registries {
    qa_frontend *frontend;
    qa_q3_presentation_assets **rows;
    size_t count,capacity;
};
static bool reserve(frontend_renderer_registries *owner,size_t count,qa_error *error)
{
    if (count<=owner->capacity) return true;
    if (count>SIZE_MAX/sizeof(*owner->rows))
        return frontend_fail(error,QA_ERROR_MEMORY,"Retained registry roster exceeds its actual allocation");
    size_t capacity=owner->capacity?owner->capacity:8;
    while (capacity<count) {
        if (capacity>SIZE_MAX/2) { capacity=count; break; }
        capacity*=2;
    }
    if (capacity>SIZE_MAX/sizeof(*owner->rows)) capacity=count;
    void *rows=realloc(owner->rows,capacity*sizeof(*owner->rows));
    if (!rows) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining physical Source registry rows");
    owner->rows=rows; owner->capacity=capacity; return true;
}
static bool contains(const frontend_renderer_registries *owner,const qa_q3_presentation_assets *assets)
{
    for (size_t i=0;i<owner->count;++i) if (owner->rows[i]==assets) return true;
    return false;
}
static bool include(frontend_renderer_registries *owner,qa_q3_presentation_assets *assets,qa_error *error)
{
    frontend_renderer_registries chain={0};
    bool okay=assets!=NULL;
    for (qa_q3_presentation_assets *row=assets;okay && row;row=qa_q3_assets_parent(row)) {
        if (contains(&chain,row) || !qa_q3_assets_idle(row)) {
            okay=frontend_fail(error,QA_ERROR_ARGUMENT,"Retained registry parent chain is cyclic or entered");
            break;
        }
        if (chain.count==SIZE_MAX || !reserve(&chain,chain.count+1,error)) { okay=false; break; }
        chain.rows[chain.count++]=row;
    }
    while (okay && chain.count) {
        qa_q3_presentation_assets *row=chain.rows[--chain.count];
        if (contains(owner,row)) continue;
        if (owner->count==SIZE_MAX || !reserve(owner,owner->count+1,error) ||
            !qa_q3_assets_retain(row,error)) { okay=false; break; }
        owner->rows[owner->count++]=row;
    }
    free(chain.rows); return okay;
}
bool frontend_renderer_registries_idle(const frontend_renderer_registries *owner)
{
    if (owner) for (size_t i=0;i<owner->count;++i)
        if (owner->rows[i] && !qa_q3_assets_idle(owner->rows[i])) return false;
    return true;
}
bool frontend_renderer_registries_destroy(frontend_renderer_registries **slot,qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_renderer_registries *owner=*slot;
    if (!owner->frontend || owner->frontend->capture || owner->frontend->resource_inventory ||
        !frontend_renderer_registries_idle(owner))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained Source registry custody remains borrowed");
    for (size_t i=owner->count;i>0;--i) qa_q3_assets_release(owner->rows[i-1]);
    free(owner->rows); free(owner); *slot=NULL; return true;
}
bool frontend_renderer_registries_include(qa_frontend *f,qa_q3_presentation_assets *assets,qa_error *error)
{
    if (!f || !assets || f->capture || f->resource_inventory || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Registry custody requires returned actual Source allocations");
    if (!f->renderer_registries) {
        f->renderer_registries=calloc(1,sizeof(*f->renderer_registries));
        if (!f->renderer_registries) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining Source registry custody");
        f->renderer_registries->frontend=f;
    }
    return f->renderer_registries->frontend==f && include(f->renderer_registries,assets,error);
}
bool frontend_renderer_registries_refresh(qa_frontend *f,qa_error *error)
{
    if (!f || f->capture || f->resource_inventory || f->source_restoring || (f->cpu && f->gl) ||
        !frontend_renderer_registries_idle(f->renderer_registries))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Registry refresh requires returned physical Source custody");
    frontend_renderer_registries *next=calloc(1,sizeof(*next));
    if (!next) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual Source registry roots");
    next->frontend=f;
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    bool okay=true;
    if (controls) {
        const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
        const qa_q3_source_scene_bank *bank=NULL;
        okay=source && qa_material_source_scene_bank_metadata(source,&bank,error);
        for (size_t i=0;okay && i<qa_q3_source_scene_bank_registry_count(bank);++i)
            okay=include(next,qa_q3_source_scene_bank_registry_at(bank,i),error);
    }
    if (okay) {
        frontend_renderer_registries *prior=f->renderer_registries;
        if (!frontend_renderer_registries_destroy(&prior,error)) okay=false;
        else { f->renderer_registries=next; next=NULL; }
    }
    qa_error cleanup={0};
    if (next && !frontend_renderer_registries_destroy(&next,okay?error:&cleanup)) return false;
    return okay;
}
size_t frontend_renderer_registries_count(const qa_frontend *f)
{ return f && f->renderer_registries?f->renderer_registries->count:0; }
bool frontend_renderer_registries_at(const qa_frontend *f,size_t ordinal,qa_q3_presentation_assets **out,qa_error *error)
{
    const frontend_renderer_registries *owner=f?f->renderer_registries:NULL;
    if (!out || !owner || owner->frontend!=f || ordinal>=owner->count ||
        (!owner->rows[ordinal] && !f->source_restoring))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained registry ordinal has no actual allocation");
    *out=owner->rows[ordinal]; return true;
}
bool frontend_renderer_registries_restore_prefix(qa_frontend *f,size_t count,qa_error *error)
{
    if (!f || !f->source_restoring || f->capture || f->resource_inventory || f->renderer_registries ||
        count>SIZE_MAX/sizeof(qa_q3_presentation_assets *))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Registry prefix requires its isolated empty custody owner");
    frontend_renderer_registries *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating restored registry custody");
    owner->frontend=f; f->renderer_registries=owner;
    if (count) {
        owner->rows=calloc(count,sizeof(*owner->rows));
        if (!owner->rows) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating genuine registry import destinations");
    }
    owner->count=owner->capacity=count; return true;
}
bool frontend_renderer_registries_restore_adopt(qa_frontend *f,size_t ordinal,
    qa_q3_presentation_assets **owned,qa_error *error)
{
    frontend_renderer_registries *owner=f?f->renderer_registries:NULL;
    if (!f || !f->source_restoring || f->capture || f->resource_inventory || !owner || owner->frontend!=f ||
        ordinal>=owner->count || owner->rows[ordinal] || !owned || !*owned ||
        !qa_q3_assets_idle(*owned) || contains(owner,*owned))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Registry import must transfer its exact genuine created allocation");
    owner->rows[ordinal]=*owned; *owned=NULL; return true;
}
