#include "sysv_libc_private.h"

enum cxx_operation { CX_ATEXIT = 1, CX_FINALIZE, CX_GUARD, CX_RELEASE, CX_ABORT,
    CX_NEW, CX_DELETE, CX_EXCEPTION, CX_FREE_EXCEPTION, CX_ZERO };
bool sysv_cxx_install(guest_sysv_runtime *r, qa_error *error)
{
    const char *cxa[] = {"CXXABI_1.3",NULL}, *old[] = {"GLIBCXX_3.4",NULL};
    const char *libc[] = {r->target.pointer_bytes == 4 ? "GLIBC_2.1.3" : "GLIBC_2.2.5",NULL};
    const qa_native_value_type p = QA_NATIVE_ADDRESS, size = sysv_size_type(r), ppp[] = {p,p,p};
    const char *names[] = {"__cxa_atexit","__cxa_finalize","__cxa_guard_acquire","__cxa_guard_release",
        "__cxa_guard_abort",r->target.pointer_bytes == 4 ? "_Znwj" : "_Znwm",
        r->target.pointer_bytes == 4 ? "_Znaj" : "_Znam","_ZdlPv","_ZdaPv",
        "__cxa_allocate_exception","__cxa_free_exception"};
    const uint32_t operations[] = {CX_ATEXIT,CX_FINALIZE,CX_GUARD,CX_RELEASE,CX_ABORT,
        CX_NEW,CX_NEW,CX_DELETE,CX_DELETE,CX_EXCEPTION,CX_FREE_EXCEPTION};
    for (size_t i = 0; i < 11; ++i) {
        bool allocation = i == 5 || i == 6 || i == 9;
        qa_native_value_type result = i == 0 || i == 2 ? QA_NATIVE_I32 : allocation ? p : QA_NATIVE_VOID;
        const char *const *versions = i < 2 ? libc : i >= 5 && i <= 8 ? old : cxa;
        if (!sysv_service_add(r,SYSV_CXX,operations[i],0,0,0,i < 2 ? "libc.so.6" : "libstdc++.so.6",
            names[i],versions,2,i == 0 ? ppp : allocation ? &size : &p,i == 0 ? 3 : 1,result,NULL,NULL,error)) return false;
    }
    return sysv_cxx_data_install(r,error);
}
bool sysv_cxx_call(sysv_service *service,const qa_native_value *args,qa_native_value *out,qa_error *error)
{
    guest_sysv_runtime *r = service->runtime; out->type = service->result.kind;
    uint64_t a = service->parameter_count ? sysv_integer(args) : 0;
    if ((service->operation == CX_GUARD || service->operation == CX_RELEASE || service->operation == CX_ABORT) &&
        (!a || a == UINT64_MAX)) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V static guard pointer is null or overflowing");
    switch (service->operation) {
    case CX_ATEXIT:
        out->as.i32 = 0; return guest_sysv_destructor(r,a,args[1].as.address,args[2].as.address,error);
    case CX_FINALIZE: return guest_sysv_finalize_destructors(r,a,error);
    case CX_GUARD: {
        uint64_t initialized,pending;
        if (!sysv_unsigned(r,a,1,&initialized,error)) return false;
        out->as.i32 = 0; if (initialized) return true;
        if (!sysv_unsigned(r,a+1,1,&pending,error)) return false;
        if (pending) return sysv_fail(error,QA_ERROR_UNSUPPORTED,"System V recursive local static initialization");
        out->as.i32 = 1; return sysv_store(r,a+1,1,1,error);
    }
    case CX_RELEASE: return sysv_store(r,a,1,1,error) && sysv_store(r,a+1,1,0,error);
    case CX_ABORT: return sysv_store(r,a+1,1,0,error);
    case CX_NEW: return sysv_allocate(r,a,true,&out->as.address,error);
    case CX_DELETE: return sysv_free(r,a,error);
    case CX_EXCEPTION: {
        uint64_t header = r->target.pointer_bytes == 4 ? 96 : 128, allocation;
        if (a > SYSV_MAX_ALLOCATION-header) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V exception allocation extent exceeds supported memory");
        if (!sysv_allocate(r,a+header,true,&allocation,error)) return false;
        out->as.address = allocation+header; return true;
    }
    case CX_FREE_EXCEPTION: {
        uint64_t header = r->target.pointer_bytes == 4 ? 96 : 128;
        if (a < header) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V exception pointer has no allocation header");
        return sysv_free(r,a-header,error);
    }
    case CX_ZERO: out->as.i32 = 0; return true;
    default: return sysv_fail(error,QA_ERROR_FORMAT,"unknown retained System V C++ operation");
    }
}
