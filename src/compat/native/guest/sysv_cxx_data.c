#include "sysv_libc_private.h"

enum { CXX_TYPE = 10, CXX_CLASS_VTABLE = 11, CXX_VTABLE = 12 };
bool sysv_name(const char *prefix,const char *name,const char *suffix,char **out,qa_error *error)
{
    if (!prefix || !name || !suffix || !out) return sysv_fail(error,QA_ERROR_ARGUMENT,"C++ symbol requires exact text components");
    size_t a = strlen(prefix), b = strlen(name), c = strlen(suffix);
    if (a > SIZE_MAX-b || a+b > SIZE_MAX-c-1) return sysv_fail(error,QA_ERROR_MEMORY,"C++ symbol extent overflow");
    char *text = malloc(a+b+c+1);
    if (!text) return sysv_fail(error,QA_ERROR_MEMORY,"allocating exact C++ symbol name");
    memcpy(text,prefix,a); memcpy(text+a,name,b); memcpy(text+a+b,suffix,c+1); *out = text; return true;
}
bool sysv_cxx_text(guest_sysv_runtime *r,const char *text,uint64_t *out,qa_error *error)
{
    size_t bytes = strlen(text);
    return bytes < SYSV_MAX_ALLOCATION && sysv_allocate(r,bytes+1,true,out,error) &&
        sysv_write(r,*out,text,bytes+1,error);
}
bool sysv_cxx_data(guest_sysv_runtime *r,const char *name,const char *version,
    size_t bytes,uint64_t *out,qa_error *error)
{
    return sysv_allocate(r,bytes,true,out,error) && sysv_data(r,"libstdc++.so.6",name,version,*out,bytes,error);
}
bool sysv_cxx_unsupported(guest_sysv_runtime *r,const char *name,uint64_t *out,qa_error *error)
{
    const qa_native_value_type pointer = QA_NATIVE_ADDRESS; const char *versions[] = {NULL};
    return sysv_service_add(r,SYSV_RUNTIME,0,0,0,0,"libstdc++.so.6",name,versions,1,
        &pointer,1,QA_NATIVE_VOID,"this C++ virtual operation is not implemented",out,error);
}
bool sysv_cxx_data_install(guest_sysv_runtime *r,qa_error *error)
{
    size_t p = r->target.pointer_bytes;
    const char *names[] = {"N10__cxxabiv117__class_type_infoE","N10__cxxabiv120__si_class_type_infoE","N10__cxxabiv121__vmi_class_type_infoE"};
    const char *forms[] = {"class","si","vmi"};
    uint64_t base,types[3],tables[3],pointer_test,function_test;
    if (!sysv_cxx_data(r,"_ZTISt9type_info","GLIBCXX_3.4",2*p,&base,error)) return false;
    for (size_t i = 0; i < 3; ++i) {
        char *symbol = NULL;
        if (!sysv_name("_ZTI",names[i],"",&symbol,error)) return false;
        bool ok = sysv_cxx_data(r,symbol,"CXXABI_1.3",3*p,types+i,error); free(symbol);
        if (!ok) return false;
    }
    const char *versions[] = {"GLIBCXX_3.4",NULL}; const qa_native_value_type pointer = QA_NATIVE_ADDRESS;
    if (!sysv_service_add(r,SYSV_CXX,10,0,0,0,"libstdc++.so.6","__guest_type_info_is_pointer",versions,2,
        &pointer,1,QA_NATIVE_I32,NULL,&pointer_test,error) ||
        !sysv_service_add(r,SYSV_CXX,10,0,0,0,"libstdc++.so.6","__guest_type_info_is_function",versions,2,
        &pointer,1,QA_NATIVE_I32,NULL,&function_test,error)) return false;
    for (size_t i = 0; i < 3; ++i) {
        char *symbol = NULL;
        if (!sysv_name("_ZTV",names[i],"",&symbol,error)) return false;
        bool ok = sysv_cxx_data(r,symbol,"CXXABI_1.3",11*p,tables+i,error); free(symbol);
        if (!ok || !sysv_put_pointer(r,tables[i]+p,types[i],error)) return false;
        for (size_t method = 0; method < 9; ++method) {
            uint64_t address = method == 2 ? pointer_test : function_test;
            if (method != 2 && method != 3) {
                char suffix[] = "_type_info_virtual_0"; suffix[sizeof(suffix)-2] = (char)('0'+method);
                char *name = NULL;
                if (!sysv_name("__guest_",forms[i],suffix,&name,error)) return false;
                ok = sysv_cxx_unsupported(r,name,&address,error); free(name); if (!ok) return false;
            }
            if (!sysv_put_pointer(r,tables[i]+(method+2)*p,address,error)) return false;
        }
        if (!sysv_object_add(r,CXX_CLASS_VTABLE,forms[i],tables[i]+2*p,9*p,NULL,0,error)) return false;
    }
    uint64_t text;
    if (!sysv_put_pointer(r,base,tables[0]+2*p,error) || !sysv_cxx_text(r,"St9type_info",&text,error) ||
        !sysv_put_pointer(r,base+p,text,error) || !sysv_object_add(r,CXX_TYPE,"St9type_info",base,2*p,NULL,0,error)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!sysv_put_pointer(r,types[i],tables[1]+2*p,error) || !sysv_cxx_text(r,names[i],&text,error) ||
            !sysv_put_pointer(r,types[i]+p,text,error) || !sysv_put_pointer(r,types[i]+2*p,i ? types[0] : base,error) ||
            !sysv_object_add(r,CXX_TYPE,names[i],types[i],3*p,NULL,0,error)) return false;
    return true;
}
bool sysv_cxx_type(guest_sysv_runtime *r,const char *name,const uint64_t *bases,
    const int64_t *offsets,const uint32_t *flags,size_t count,uint64_t *out,qa_error *error)
{
    if (!name || !out || (count && (!bases || !offsets || !flags)) || count > (SYSV_MAX_ALLOCATION-24)/16)
        return sysv_fail(error,QA_ERROR_ARGUMENT,"invalid concrete System V C++ type metadata");
    sysv_object *prior = sysv_object_find(r,CXX_TYPE,name);
    if (prior) { *out = prior->address; return true; }
    const char *form = !count ? "class" : count == 1 && !offsets[0] && flags[0] == 2 ? "si" : "vmi";
    sysv_object *table = sysv_object_find(r,CXX_CLASS_VTABLE,form);
    if (!table) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V concrete type lacks genuine class RTTI table");
    uint64_t vptr = table->address; size_t p = r->target.pointer_bytes;
    size_t bytes = !count ? 2*p : !strcmp(form,"si") ? 3*p : 2*p+8+count*2*p;
    char *symbol = NULL;
    if (!sysv_name("_ZTI",name,"",&symbol,error)) return false;
    uint64_t address,text;
    bool ok = sysv_cxx_data(r,symbol,"GLIBCXX_3.4",bytes,&address,error); free(symbol);
    if (!ok || !sysv_cxx_text(r,name,&text,error) || !sysv_put_pointer(r,address,vptr,error) ||
        !sysv_put_pointer(r,address+p,text,error)) return false;
    if (!strcmp(form,"si")) { if (!sysv_put_pointer(r,address+2*p,bases[0],error)) return false; }
    else if (!strcmp(form,"vmi")) {
        if (!sysv_store(r,address+2*p,4,0,error) || !sysv_store(r,address+2*p+4,4,count,error)) return false;
        for (size_t i = 0; i < count; ++i) {
            uint64_t encoded = (uint64_t)offsets[i]*256 | flags[i];
            if (!sysv_put_pointer(r,address+2*p+8+i*2*p,bases[i],error) ||
                !sysv_store(r,address+3*p+8+i*2*p,p,encoded,error)) return false;
        }
    }
    if (!sysv_object_add(r,CXX_TYPE,name,address,bytes,NULL,0,error)) return false;
    *out = address; return true;
}
bool sysv_cxx_vtable(guest_sysv_runtime *r,const char *name,uint64_t type,
    const uint64_t *entries,size_t count,uint64_t *out,qa_error *error)
{
    size_t p = r->target.pointer_bytes;
    if (!name || !out || (count && !entries) || count > SYSV_MAX_ALLOCATION/p-2) return false;
    char *symbol = NULL; uint64_t table;
    if (!sysv_name("_ZTV",name,"",&symbol,error)) return false;
    bool ok = sysv_cxx_data(r,symbol,"GLIBCXX_3.4",(count+2)*p,&table,error); free(symbol);
    if (!ok || !sysv_put_pointer(r,table+p,type,error)) return false;
    for (size_t i = 0; i < count; ++i) if (!sysv_put_pointer(r,table+(i+2)*p,entries[i],error)) return false;
    if (!sysv_object_add(r,CXX_VTABLE,name,table+2*p,count*p,NULL,0,error)) return false;
    *out = table+2*p; return true;
}
