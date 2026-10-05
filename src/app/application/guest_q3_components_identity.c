#include "guest_q3_components_private.h"
#include "unified_output_json.h"

bool application_q3_component_identity_create(const qa_catalog_mod *mod,const qa_product *product,
    const char *instance,qa_unified_document **out,qa_error *e)
{
    if(!mod||!product||!product->key||!product->identity||!instance||!out||*out)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component identity lost its retained discovered product");
    application_unified_json module={0},json={0};
    char declaration[72]="sha256:",program[72]="sha256:";
    qa_sha256_hex(&mod->declaration_digest,declaration+7); qa_sha256_hex(&mod->program_digest,program+7);
    bool ok=application_unified_json_text(&module,"mod:",e)&&application_unified_json_percent_encoded(&module,product->key,e)&&
        application_unified_json_text(&module,"%2F",e)&&application_unified_json_percent_encoded(&module,mod->id,e)&&application_unified_json_append(&module,(qa_bytes){(const uint8_t *)"",1},e);
    if(ok) ok=application_unified_json_text(&json,"{\"selection\":{\"product\":",e)&&application_unified_json_string(&json,product->key,e)&&
        application_unified_json_text(&json,",\"id\":",e)&&application_unified_json_string(&json,mod->id,e)&&
        application_unified_json_text(&json,"},\"source\":{\"provider\":",e)&&application_unified_json_string(&json,instance,e)&&
        application_unified_json_text(&json,",\"content\":",e)&&application_unified_json_string(&json,product->identity,e)&&
        application_unified_json_text(&json,"},\"declarationDigest\":",e)&&application_unified_json_string(&json,declaration,e)&&
        application_unified_json_text(&json,",\"modules\":[{\"id\":",e)&&application_unified_json_string(&json,(char *)module.bytes.data,e)&&
        application_unified_json_text(&json,",\"artifactPath\":",e)&&application_unified_json_string(&json,mod->program_path,e)&&
        application_unified_json_text(&json,",\"digest\":",e)&&application_unified_json_string(&json,program,e)&&
        application_unified_json_text(&json,",\"revision\":",e)&&application_unified_json_string(&json,declaration,e)&&
        application_unified_json_text(&json,"}],\"providers\":[]}",e)&&qa_unified_document_create(QA_UNIFIED_CHECKPOINT,(qa_bytes){json.bytes.data,json.bytes.size},out,e);
    application_unified_json_dispose(&module); application_unified_json_dispose(&json); return ok;
}
bool q3components_identity(component_game_row *row,qa_error *e)
{
    bool ok=application_q3_component_identity_create(row->publication.metadata,
        qa_catalog_product(row->provider->product_catalog,row->publication.metadata->product),
        row->publication.descriptor->selection.instance,&row->identity,e);
    if(ok) {
        const qa_json_document *j=qa_unified_document_json(row->identity);
        qa_json_id module=qa_json_at(j,qa_json_get(j,qa_unified_document_root(row->identity),"modules"),0);
        static const char *const names[]={"id","artifactPath","digest","revision"};
        char **fields[]={&row->module.id,&row->module.artifact_path,&row->module.digest,&row->module.revision};
        for(size_t i=0;ok && i<4;++i) {
            qa_buffer text={0};
            ok=qa_json_string(j,qa_json_get(j,module,names[i]),&text,e);
            if(ok) *fields[i]=(char *)text.data;
            else qa_buffer_free(&text);
        }
        if(ok) { row->publication.identity=row->identity; row->publication.module=&row->module; }
    }
    return ok;
}
