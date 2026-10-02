#include "guest_q3_components_private.h"
#include "unified_output_json.h"

static bool encoded(application_unified_json *j,const char *s,qa_error *e)
{
    static const char hex[]="0123456789ABCDEF";
    for(const unsigned char *p=(const unsigned char *)s;*p;++p) {
        bool plain=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-_.!~*'()",*p)!=NULL;
        char escaped[3]={'%',hex[*p>>4],hex[*p&15]};
        if(!application_unified_json_append(j,plain?(qa_bytes){p,1}:(qa_bytes){(const uint8_t *)escaped,3},e)) return false;
    }
    return true;
}
bool q3components_identity(component_game_row *row,qa_error *e)
{
    const qa_catalog_mod *mod=row->publication.metadata;
    const qa_product *product=qa_catalog_product(row->provider->product_catalog,mod->product);
    if(!product||!product->key||!product->identity||!row->publication.descriptor)
        return application_fail(e,QA_ERROR_ARGUMENT,"Component identity lost its retained discovered product");
    application_unified_json module={0},json={0};
    char declaration[72]="sha256:",program[72]="sha256:";
    qa_sha256_hex(&mod->declaration_digest,declaration+7); qa_sha256_hex(&mod->program_digest,program+7);
    bool ok=application_unified_json_text(&module,"mod:",e)&&encoded(&module,product->key,e)&&
        application_unified_json_text(&module,"%2F",e)&&encoded(&module,mod->id,e)&&application_unified_json_append(&module,(qa_bytes){(const uint8_t *)"",1},e);
    if(ok) ok=application_unified_json_text(&json,"{\"selection\":{\"product\":",e)&&application_unified_json_string(&json,product->key,e)&&
        application_unified_json_text(&json,",\"id\":",e)&&application_unified_json_string(&json,mod->id,e)&&
        application_unified_json_text(&json,"},\"source\":{\"provider\":",e)&&application_unified_json_string(&json,row->publication.descriptor->selection.instance,e)&&
        application_unified_json_text(&json,",\"content\":",e)&&application_unified_json_string(&json,product->identity,e)&&
        application_unified_json_text(&json,"},\"declarationDigest\":",e)&&application_unified_json_string(&json,declaration,e)&&
        application_unified_json_text(&json,",\"modules\":[{\"id\":",e)&&application_unified_json_string(&json,(char *)module.bytes.data,e)&&
        application_unified_json_text(&json,",\"artifactPath\":",e)&&application_unified_json_string(&json,mod->program_path,e)&&
        application_unified_json_text(&json,",\"digest\":",e)&&application_unified_json_string(&json,program,e)&&
        application_unified_json_text(&json,",\"revision\":",e)&&application_unified_json_string(&json,declaration,e)&&
        application_unified_json_text(&json,"}],\"providers\":[]}",e)&&qa_unified_document_create(QA_UNIFIED_CHECKPOINT,(qa_bytes){json.bytes.data,json.bytes.size},&row->identity,e);
    if(ok) row->publication.identity=row->identity;
    application_unified_json_dispose(&module); application_unified_json_dispose(&json); return ok;
}
