#include "guest_q3_components_private.h"
#include "unified_output_json.h"

bool application_unified_component_identity_create(const qa_catalog_mod *mod,const qa_product *product,
    const char *instance,qa_unified_component_identity *out,qa_error *e)
{
    if(!mod||!product||!product->key||!product->identity||!mod->id||!instance||!out||
        (mod->runtime!=QA_PROGRAM_QVM&&mod->runtime!=QA_PROGRAM_NATIVE))
        return application_fail(e,QA_ERROR_ARGUMENT,"Component identity lost its retained discovered product");
    application_unified_json module={0};
    char declaration[72]="sha256:",program[72]="sha256:";
    qa_sha256_hex(&mod->declaration_digest,declaration+7); qa_sha256_hex(&mod->program_digest,program+7);
    bool ok=application_unified_json_text(&module,"mod:",e)&&application_unified_json_percent_encoded(&module,product->key,e)&&
        application_unified_json_text(&module,"%2F",e)&&application_unified_json_percent_encoded(&module,mod->id,e)&&
        application_unified_json_append(&module,(qa_bytes){(const uint8_t *)"",1},e);
    qa_unified_component_identity source={.runtime=mod->runtime,.product=(char *)product->key,.id=(char *)mod->id,
        .provider=(char *)instance,.content=(char *)product->identity,.declaration_digest=mod->declaration_digest,
        .module={.id=(char *)module.bytes.data,.artifact_path=(char *)mod->program_path,.digest=program,
            .revision=mod->runtime==QA_PROGRAM_NATIVE?program:declaration}};
    if(ok) ok=qa_unified_component_identity_clone(&source,out,e);
    application_unified_json_dispose(&module); return ok;
}
bool q3components_identity(component_game_row *row,qa_error *e)
{
    bool ok=application_unified_component_identity_create(row->publication.metadata,
        qa_catalog_product(row->provider->product_catalog,row->publication.metadata->product),
        row->publication.descriptor->selection.instance,&row->identity,e);
    if(ok) { row->publication.identity=&row->identity; row->publication.module=&row->identity.module; }
    return ok;
}
