#include "sysv_libc_private.h"

enum iostream_operation { IO_INIT = 1, IO_DONE, IO_CLEAR, IO_FLUSH, IO_IMBUE,
    IO_SETBUF, IO_SYNC, IO_SHOW, IO_GETN, IO_UNDER, IO_UFLOW, IO_PBACK, IO_PUTN, IO_OVER };
enum { IO_STREAM = 30, IO_TABLE = 31 };
typedef struct ios_layout {
    size_t size,precision,flags,exceptions,state,local_words,word_size,words,
        locale,tie,buffer,ctype,num_put,num_get;
} ios_layout;
static ios_layout layout(guest_sysv_runtime *r,bool wide)
{
    return r->target.pointer_bytes == 8 ? (ios_layout){264,8,24,28,32,64,192,200,208,216,232,240,248,256} :
        (ios_layout){wide ? 140 : 136,4,12,16,20,36,100,104,108,112,
            wide ? 124 : 120,wide ? 128 : 124,wide ? 132 : 128,wide ? 136 : 132};
}
static bool function(guest_sysv_runtime *r,const char *name,uint32_t operation,bool wide,
    const qa_native_value_type *types,size_t count,qa_native_value_type result,uint64_t *out,qa_error *error)
{
    const char *versions[] = {"GLIBCXX_3.4",NULL};
    return sysv_service_add(r,SYSV_IOSTREAM,operation,wide,0,0,"libstdc++.so.6",name,versions,2,
        types,count,result,NULL,out,error);
}
static bool locale_retain(guest_sysv_runtime *r,qa_error *error)
{
    uint64_t value;
    return sysv_unsigned(r,r->classic_locale,4,&value,error) && sysv_store(r,r->classic_locale,4,(uint32_t)value+1u,error);
}
static bool registered_address(guest_sysv_runtime *r,const char *name,uint64_t *out,qa_error *error)
{
    size_t count = guest_runtime_imports_count(r->imports); *out = 0;
    for (size_t i = 0; i < count; ++i) {
        guest_runtime_import_view view;
        if (!guest_runtime_imports_at(r->imports,i,&view,error)) return false;
        if (view.key.kind == GUEST_RUNTIME_SYMBOL_NAME && view.key.scope == r->options.scope &&
            !strcmp(view.key.library,"libstdc++.so.6") && !strcmp(view.key.name,name) &&
            sysv_equal_text(view.key.version,"GLIBCXX_3.4")) { *out = view.address; return true; }
    }
    return true;
}
static bool buffer(guest_sysv_runtime *r,uint64_t file,bool wide,uint64_t *out,qa_error *error)
{
    size_t p = r->target.pointer_bytes; char *name = NULL,*base_name = NULL,*symbol = NULL;
    if (!sysv_name("N9__gnu_cxx18stdio_sync_filebufI",wide ? "wSt11char_traitsIwEEE" : "cSt11char_traitsIcEEE","",&name,error) ||
        !sysv_name("St15basic_streambufI",wide ? "wSt11char_traitsIwEE" : "cSt11char_traitsIcEE","",&base_name,error) ||
        !sysv_name("_ZTV",name,"",&symbol,error)) { free(name); free(base_name); free(symbol); return false; }
    uint64_t parent,type,table; int64_t offset = 0; uint32_t flags = 2;
    bool ok = sysv_allocate(r,10*p,true,out,error) && sysv_cxx_type(r,base_name,NULL,NULL,NULL,0,&parent,error) &&
        sysv_cxx_type(r,name,&parent,&offset,&flags,1,&type,error) && registered_address(r,symbol,&table,error);
    if (ok && !table) {
        const char *suffixes[] = {"destructor","deleting_destructor","imbue","setbuf","seekoff","seekpos",
            "sync","showmanyc","xsgetn","underflow","uflow","pbackfail","xsputn","overflow"};
        const uint32_t operations[] = {0,0,IO_IMBUE,IO_SETBUF,0,0,IO_SYNC,IO_SHOW,IO_GETN,IO_UNDER,IO_UFLOW,IO_PBACK,IO_PUTN,IO_OVER};
        uint64_t entries[14]; qa_native_value_type pointer = QA_NATIVE_ADDRESS, signed_size = sysv_signed_type(r);
        qa_native_value_type character = wide ? QA_NATIVE_U32 : QA_NATIVE_I32;
        const qa_native_value_type pp[] = {pointer,pointer}, pps[] = {pointer,pointer,signed_size}, pc[] = {pointer,character};
        for (size_t i = 0; i < 14 && ok; ++i) {
            char *prefix = NULL,*method = NULL;
            ok = sysv_name("__guest_",name,"_",&prefix,error) && sysv_name(prefix,suffixes[i],"",&method,error);
            if (ok) {
                const qa_native_value_type *types = i == 2 ? pp : i == 3 || i == 8 || i == 12 ? pps : i == 11 || i == 13 ? pc : &pointer;
                size_t count = i == 2 || i == 11 || i == 13 ? 2 : i == 3 || i == 8 || i == 12 ? 3 : 1;
                qa_native_value_type result = i == 2 ? QA_NATIVE_VOID : i == 3 ? pointer : i == 6 ? QA_NATIVE_I32 :
                    i == 7 || i == 8 || i == 12 ? signed_size : character;
                ok = operations[i] ? function(r,method,operations[i],wide,types,count,result,entries+i,error) :
                    sysv_cxx_unsupported(r,method,entries+i,error);
            }
            free(prefix); free(method);
        }
        uint64_t address_point;
        if (ok) ok = sysv_cxx_vtable(r,name,type,entries,14,&address_point,error);
        if (ok) table = address_point-2*p;
    }
    if (ok) ok = sysv_put_pointer(r,*out,table+2*p,error) && locale_retain(r,error) &&
        sysv_put_pointer(r,*out+7*p,r->classic_locale,error) && sysv_put_pointer(r,*out+8*p,file,error) &&
        sysv_store(r,*out+9*p,4,UINT32_MAX,error);
    free(name); free(base_name); free(symbol); return ok;
}
static bool construct(guest_sysv_runtime *r,const sysv_object *stream,uint64_t buf,qa_error *error)
{
    bool wide = stream->values[1] != 0, input = stream->values[2] != 0;
    uint64_t address = stream->address,ios = stream->values[0]; size_t p = r->target.pointer_bytes;
    ios_layout l = layout(r,wide); uint64_t ios_base,basic_ios,type;
    char *basic_name = NULL;
    if (!sysv_name("St9basic_iosI",wide ? "wSt11char_traitsIwEE" : "cSt11char_traitsIcEE","",&basic_name,error)) return false;
    int64_t offset = 0; uint32_t flags = 2;
    bool ok = sysv_cxx_type(r,"St8ios_base",NULL,NULL,NULL,0,&ios_base,error) &&
        sysv_cxx_type(r,basic_name,&ios_base,&offset,&flags,1,&basic_ios,error);
    free(basic_name); if (!ok) return false;
    const char *name = wide ? input ? "St13basic_istreamIwSt11char_traitsIwEE" : "St13basic_ostreamIwSt11char_traitsIwEE" : input ? "Si" : "So";
    offset = -(int64_t)(3*p); flags = 3;
    if (!sysv_cxx_type(r,name,&basic_ios,&offset,&flags,1,&type,error)) return false;
    char *symbol = NULL; uint64_t table;
    if (!sysv_name("_ZTV",name,"",&symbol,error)) return false;
    ok = registered_address(r,symbol,&table,error);
    size_t prefix = (input ? 2 : 1)*p;
    if (ok && !table) {
        ok = sysv_cxx_data(r,symbol,"GLIBCXX_3.4",10*p,&table,error) &&
            sysv_store(r,table,p,prefix,error) && sysv_put_pointer(r,table+2*p,type,error) &&
            sysv_store(r,table+5*p,p,UINT64_C(0)-prefix,error) &&
            sysv_store(r,table+6*p,p,UINT64_C(0)-prefix,error) && sysv_put_pointer(r,table+7*p,type,error);
        const char *suffixes[] = {"_destructor","_deleting_destructor","_virtual_destructor","_virtual_deleting_destructor"};
        const size_t slots[] = {3,4,8,9};
        for (size_t i = 0; i < 4 && ok; ++i) {
            char *method = NULL; uint64_t target;
            ok = sysv_name("__guest_",name,suffixes[i],&method,error) && sysv_cxx_unsupported(r,method,&target,error) &&
                sysv_put_pointer(r,table+slots[i]*p,target,error); free(method);
        }
    }
    free(symbol); if (!ok) return false;
    sysv_object *locale = sysv_object_find(r,20,"classic");
    if (!locale) return sysv_fail(error,QA_ERROR_ARGUMENT,"iostream construction lacks genuine retained classic locale");
    uint64_t ctype = locale->values[wide ? 1 : 0],num_put = locale->values[wide ? 3 : 2],num_get = locale->values[wide ? 5 : 4];
    return sysv_put_pointer(r,address,table+3*p,error) && sysv_put_pointer(r,ios,table+8*p,error) &&
        sysv_store(r,ios+l.precision,p,6,error) && sysv_store(r,ios+l.flags,4,0x1002,error) &&
        sysv_store(r,ios+l.word_size,4,8,error) && sysv_put_pointer(r,ios+l.words,ios+l.local_words,error) &&
        locale_retain(r,error) && sysv_put_pointer(r,ios+l.locale,r->classic_locale,error) &&
        sysv_put_pointer(r,ios+l.buffer,buf,error) && sysv_put_pointer(r,ios+l.ctype,ctype,error) &&
        sysv_put_pointer(r,ios+l.num_put,num_put,error) && sysv_put_pointer(r,ios+l.num_get,num_get,error);
}
static bool initialize(guest_sysv_runtime *r,qa_error *error)
{
    uint64_t previous;
    if (!sysv_unsigned(r,r->iostream_refcount,4,&previous,error) || !sysv_store(r,r->iostream_refcount,4,(uint32_t)previous+1u,error)) return false;
    if (previous) return true;
    if (!sysv_store(r,r->iostream_sync,1,1,error)) return false;
    const char *names[] = {"cin","cout","cerr","clog","wcin","wcout","wcerr","wclog"};
    for (size_t w = 0; w < 2; ++w) {
        uint64_t buffers[3];
        for (size_t i = 0; i < 3; ++i) if (!buffer(r,r->files[i],w != 0,buffers+i,error)) return false;
        for (size_t i = 0; i < 4; ++i) {
            sysv_object *row = sysv_object_find(r,IO_STREAM,names[w*4+i]);
            if (!row) return sysv_fail(error,QA_ERROR_ARGUMENT,"iostream initialization lost its retained stream");
            sysv_object copy = *row;
            if (!construct(r,&copy,buffers[i == 0 ? 0 : i == 1 ? 1 : 2],error)) return false;
        }
        sysv_object *out = sysv_object_find(r,IO_STREAM,names[w*4+1]);
        uint64_t output = out->address; ios_layout l = layout(r,w != 0);
        for (size_t i = 0; i < 3; i += 2) {
            sysv_object *row = sysv_object_find(r,IO_STREAM,names[w*4+i]); uint64_t ios = row->values[0];
            if (!sysv_put_pointer(r,ios+l.tie,output,error) || (i && !sysv_store(r,ios+l.flags,4,0x3002,error))) return false;
        }
    }
    return sysv_unsigned(r,r->iostream_refcount,4,&previous,error) && sysv_store(r,r->iostream_refcount,4,(uint32_t)previous+1u,error);
}
static bool clear(guest_sysv_runtime *r,uint64_t ios,bool wide,uint32_t state,qa_error *error)
{
    ios_layout l = layout(r,wide); uint64_t buf,exceptions;
    if (!sysv_pointer(r,ios+l.buffer,&buf,error)) return false;
    if (!buf) state |= 1;
    if (!sysv_store(r,ios+l.state,4,state,error) || !sysv_unsigned(r,ios+l.exceptions,4,&exceptions,error)) return false;
    return !(state&exceptions) || sysv_fail(error,QA_ERROR_UNSUPPORTED,"System V guest iostream exception propagation is not implemented");
}
static bool flush(guest_sysv_runtime *r,uint64_t stream,bool wide,size_t depth,qa_error *error)
{
    size_t p = r->target.pointer_bytes; uint64_t vptr,offset,ios,state,tie,buf,sync;
    if (depth >= 4096) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V tied stream recursion exceeds supported depth");
    if (!sysv_pointer(r,stream,&vptr,error) || vptr < 3*p || !sysv_unsigned(r,vptr-3*p,p,&offset,error))
        return sysv_fail(error,QA_ERROR_ARGUMENT,"System V guest stream is unconstructed");
    if (p == 4) offset = (uint64_t)(int64_t)((uint32_t)offset <= INT32_MAX ? (int32_t)offset : -(int32_t)(UINT32_MAX-(uint32_t)offset)-1);
    ios = stream+offset; ios_layout l = layout(r,wide);
    if (!sysv_unsigned(r,ios+l.state,4,&state,error)) return false;
    if (state) return true;
    if (!sysv_pointer(r,ios+l.tie,&tie,error) || (tie && !flush(r,tie,wide,depth+1,error)) ||
        !sysv_pointer(r,ios+l.buffer,&buf,error)) return false;
    if (!buf) return true;
    if (!sysv_pointer(r,buf,&vptr,error) || !vptr || !sysv_pointer(r,vptr+6*p,&sync,error) || !sync)
        return sysv_fail(error,QA_ERROR_ARGUMENT,"System V guest stream buffer has no actual sync method");
    const qa_native_value_type pointer = QA_NATIVE_ADDRESS;
    qa_native_value argument = {.type = pointer,.as.address = buf};
    for (size_t i = 0; i < 2; ++i) {
        qa_native_value result = {.type = QA_NATIVE_I32};
        if (!sysv_invoke(r,sync,&pointer,1,QA_NATIVE_I32,&argument,&result,error)) return false;
        if (result.as.i32 == -1 && (!sysv_unsigned(r,ios+l.state,4,&state,error) || !clear(r,ios,wide,(uint32_t)state|1,error))) return false;
        if (i) break;
        if (!sysv_unsigned(r,ios+l.flags,4,&state,error)) return false;
        if (!(state&0x2000)) break;
    }
    return true;
}
bool sysv_iostream_install(guest_sysv_runtime *r,qa_error *error)
{
    if (!sysv_stdio_install(r,error) || !sysv_locale_install(r,error) ||
        !sysv_cxx_data(r,"_ZNSt8ios_base4Init11_S_refcountE","GLIBCXX_3.4",4,&r->iostream_refcount,error) ||
        !sysv_cxx_data(r,"_ZNSt8ios_base4Init20_S_synced_with_stdioE","GLIBCXX_3.4",1,&r->iostream_sync,error) ||
        !sysv_store(r,r->iostream_sync,1,1,error)) return false;
    const char *names[] = {"cin","cout","cerr","clog","wcin","wcout","wcerr","wclog"};
    size_t p = r->target.pointer_bytes;
    for (size_t i = 0; i < 8; ++i) {
        bool wide = i >= 4,input = i%4 == 0; size_t prefix = (input ? 2 : 1)*p;
        char symbol[16]; memcpy(symbol,"_ZSt",4); size_t n = strlen(names[i]); symbol[4] = (char)('0'+n); memcpy(symbol+5,names[i],n+1);
        uint64_t address;
        if (!sysv_cxx_data(r,symbol,"GLIBCXX_3.4",prefix+layout(r,wide).size,&address,error)) return false;
        uint64_t values[] = {address+prefix,wide,input};
        if (!sysv_object_add(r,IO_STREAM,names[i],address,prefix+layout(r,wide).size,values,3,error)) return false;
    }
    const qa_native_value_type pointer = QA_NATIVE_ADDRESS, pi[] = {pointer,QA_NATIVE_I32};
    const char *constructors[] = {"_ZNSt8ios_base4InitC1Ev","_ZNSt8ios_base4InitC2Ev","_ZNSt8ios_base4InitD1Ev","_ZNSt8ios_base4InitD2Ev"};
    for (size_t i = 0; i < 4; ++i)
        if (!function(r,constructors[i],i < 2 ? IO_INIT : IO_DONE,false,&pointer,1,QA_NATIVE_VOID,NULL,error)) return false;
    for (size_t w = 0; w < 2; ++w) {
        const char *clear_name = w ? "_ZNSt9basic_iosIwSt11char_traitsIwEE5clearESt12_Ios_Iostate" : "_ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate";
        const char *flush_name = w ? "_ZNSt13basic_ostreamIwSt11char_traitsIwEE5flushEv" : "_ZNSo5flushEv";
        if (!function(r,clear_name,IO_CLEAR,w != 0,pi,2,QA_NATIVE_VOID,NULL,error) ||
            !function(r,flush_name,IO_FLUSH,w != 0,&pointer,1,pointer,NULL,error)) return false;
    }
    return true;
}
static int64_t signed_value(const qa_native_value *value)
{ return value->type == QA_NATIVE_I32 ? value->as.i32 : value->as.i64; }
static void integer_result(qa_native_value *out,int64_t value)
{
    if (out->type == QA_NATIVE_U32) out->as.u32 = (uint32_t)value;
    else if (out->type == QA_NATIVE_I32) out->as.i32 = (int32_t)value;
    else out->as.i64 = value;
}
bool sysv_iostream_call(sysv_service *service,const qa_native_value *args,qa_native_value *out,qa_error *error)
{
    guest_sysv_runtime *r = service->runtime; bool wide = service->a != 0; size_t p = r->target.pointer_bytes;
    out->type = service->result.kind; uint64_t object = args[0].as.address;
    switch (service->operation) {
    case IO_INIT: return initialize(r,error);
    case IO_DONE: {
        uint64_t previous;
        if (!sysv_unsigned(r,r->iostream_refcount,4,&previous,error) || !sysv_store(r,r->iostream_refcount,4,(uint32_t)previous-1u,error)) return false;
        if (previous != 2) return true;
        const char *names[] = {"cout","cerr","clog","wcout","wcerr","wclog"};
        for (size_t i = 0; i < 6; ++i) {
            sysv_object *stream = sysv_object_find(r,IO_STREAM,names[i]);
            if (!stream || !flush(r,stream->address,i >= 3,0,error)) return false;
        }
        return true;
    }
    case IO_CLEAR: return (object || sysv_fail(error,QA_ERROR_ARGUMENT,"basic_ios clear requires its actual object")) &&
        clear(r,object,wide,(uint32_t)args[1].as.i32,error);
    case IO_FLUSH: out->as.address = object;
        return (object || sysv_fail(error,QA_ERROR_ARGUMENT,"iostream flush requires its actual object")) && flush(r,object,wide,0,error);
    case IO_IMBUE: return true;
    case IO_SETBUF: out->as.address = object; return object || sysv_fail(error,QA_ERROR_ARGUMENT,"streambuf setbuf requires its actual object");
    case IO_SHOW: integer_result(out,0); return true;
    default: break;
    }
    if (!object) return sysv_fail(error,QA_ERROR_ARGUMENT,"synchronized stream operation requires its actual buffer object");
    uint64_t file;
    if (!sysv_pointer(r,object+8*p,&file,error) || !file) return sysv_fail(error,QA_ERROR_ARGUMENT,"synchronized stream buffer has no actual FILE");
    int32_t value = -1;
    if (service->operation == IO_SYNC) return sysv_stdio_flush(r,file,&out->as.i32,error);
    if (service->operation == IO_UNDER) {
        if (!sysv_stdio_get(r,file,wide,&value,error) || !sysv_stdio_unget(r,file,wide,(uint32_t)value,&value,error)) return false;
        integer_result(out,value); return true;
    }
    if (service->operation == IO_UFLOW) {
        if (!sysv_stdio_get(r,file,wide,&value,error) || !sysv_store(r,object+9*p,4,(uint32_t)value,error)) return false;
        integer_result(out,value); return true;
    }
    if (service->operation == IO_PBACK) {
        uint32_t supplied = (uint32_t)sysv_integer(args+1);
        if (supplied == UINT32_MAX) { uint64_t previous;
            if (!sysv_unsigned(r,object+9*p,4,&previous,error)) return false;
            supplied = (uint32_t)previous; }
        if (!sysv_stdio_unget(r,file,wide,supplied,&value,error) || !sysv_store(r,object+9*p,4,UINT32_MAX,error)) return false;
        integer_result(out,value); return true;
    }
    if (service->operation == IO_OVER) {
        uint32_t supplied = (uint32_t)sysv_integer(args+1);
        bool ok = supplied == UINT32_MAX ? sysv_stdio_flush(r,file,&value,error) : sysv_stdio_put(r,file,wide,supplied,&value,error);
        integer_result(out,value); return ok;
    }
    if (service->operation == IO_GETN || service->operation == IO_PUTN) {
        int64_t count = signed_value(args+2), completed = 0;
        uint64_t memory = args[1].as.address; size_t width = wide ? 4 : 1;
        if (!memory) return sysv_fail(error,QA_ERROR_ARGUMENT,"synchronized stream transfer requires its actual nonnull memory");
        for (; completed < count; ++completed) {
            if ((uint64_t)completed > (UINT64_MAX-memory)/width)
                return sysv_fail(error,QA_ERROR_ARGUMENT,"synchronized stream memory cursor exceeds the target address domain");
            if (service->operation == IO_GETN) {
                if (!sysv_pointer(r,object+8*p,&file,error) || !file || !sysv_stdio_get(r,file,wide,&value,error)) return false;
                if (value < 0) break;
                if (!sysv_store(r,memory+(uint64_t)completed*width,width,(uint32_t)value,error)) return false;
            } else {
                uint64_t input;
                if (!sysv_unsigned(r,memory+(uint64_t)completed*width,width,&input,error) ||
                    !sysv_stdio_put(r,file,wide,(uint32_t)input,&value,error)) return false;
                if (value < 0) break;
            }
        }
        if (service->operation == IO_GETN) {
            uint64_t last = UINT32_MAX;
            if (completed && !sysv_unsigned(r,memory+(uint64_t)(completed-1)*width,width,&last,error)) return false;
            if (!sysv_store(r,object+9*p,4,last,error)) return false;
        }
        integer_result(out,completed); return true;
    }
    return sysv_fail(error,QA_ERROR_FORMAT,"unknown retained System V iostream operation");
}
