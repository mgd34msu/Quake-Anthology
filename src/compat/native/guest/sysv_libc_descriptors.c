#include "sysv_libc_private.h"

static bool version(const sysv_service *s,const char *expected)
{ return !s->version || !strcmp(s->version,expected); }
static bool named(const sysv_service *s,const char *name,uint32_t operation,uint64_t flags)
{ return !strcmp(s->name,name) && s->operation == operation && s->a == flags; }
static bool identity(const sysv_service *s)
{
    bool wide = s->runtime->target.pointer_bytes == 8;
    const char *base = wide ? "GLIBC_2.2.5" : "GLIBC_2.0";
    if (s->group == SYSV_RUNTIME)
        return !s->version && !strncmp(s->name,"__guest_",8) &&
            (!strcmp(s->library,"libstdc++.so.6") || !strcmp(s->library,"libc.so.6"));
    if (s->detail) return false;
    if (s->group == SYSV_FORMAT)
        return !strcmp(s->library,"libc.so.6") &&
            ((named(s,"vsnprintf",1,0) && version(s,base)) ||
             (named(s,"__vsnprintf_chk",2,0) && version(s,"GLIBC_2.3.4")));
    if (s->group == SYSV_LIBC) {
        if (!strcmp(s->library,"ld-linux-x86-64.so.2"))
            return wide && named(s,"__tls_get_addr",2,0) && version(s,"GLIBC_2.3");
        if (!strcmp(s->library,"libm.so.6")) {
            if (named(s,"__atan2_finite",21,0)) return version(s,"GLIBC_2.15");
            if (named(s,"sincos",22,0) || named(s,"sincosf",22,0))
                return version(s,wide ? base : "GLIBC_2.1");
            const char *math[] = {"sin","cos","ceil","floor","sqrt","fabs"};
            if (s->operation != 20 || s->a >= 6 || !version(s,base)) return false;
            const char *name = math[s->a]; size_t n = strlen(name);
            return !strncmp(s->name,name,n) &&
                (s->result.kind == QA_NATIVE_F32 ? !strcmp(s->name+n,"f") : !s->name[n]);
        }
        if (strcmp(s->library,"libc.so.6")) return false;
        const char *names[] = {"__errno_location",NULL,"malloc","calloc","free","realloc",
            NULL,"memset","memcmp","strlen","strcmp",NULL,"strncpy",NULL,NULL,
            "strtok","strtol","time","qsort",NULL,NULL,NULL,"__isnanf","__stack_chk_fail"};
        if (s->operation >= 1 && s->operation <= 24 && names[s->operation-1])
            return named(s,names[s->operation-1],s->operation,0) &&
                version(s,s->operation == 24 ? "GLIBC_2.4" : base);
        if (s->operation == 7) {
            if (s->a) return (!strcmp(s->name,"__memcpy_chk") || !strcmp(s->name,"__memmove_chk")) && version(s,"GLIBC_2.3.4");
            return (!strcmp(s->name,"memcpy") || !strcmp(s->name,"memmove")) &&
                (version(s,base) || (wide && !strcmp(s->name,"memcpy") && version(s,"GLIBC_2.14")));
        }
        if (s->operation == 12) {
            const char *copy[] = {"strcpy","stpcpy","strcat","__strcpy_chk","__stpcpy_chk","__strcat_chk"};
            for (size_t i = 0; i < 6; ++i)
                if (named(s,copy[i],12,(i >= 3 ? 1 : 0)|(i%3 == 2 ? 2 : 0)|(i%3 == 1 ? 4 : 0)))
                    return version(s,i >= 3 ? "GLIBC_2.3.4" : base);
            return false;
        }
        if (s->operation == 14) return version(s,base) &&
            (named(s,"strchr",14,0) || named(s,"strrchr",14,1));
        if (s->operation == 15) return version(s,base) &&
            (named(s,"strstr",15,0) || named(s,"strpbrk",15,1));
        return false;
    }
    if (s->group == SYSV_CXX) {
        if (s->operation <= 2) return !strcmp(s->library,"libc.so.6") &&
            named(s,s->operation == 1 ? "__cxa_atexit" : "__cxa_finalize",s->operation,0) &&
            version(s,wide ? base : "GLIBC_2.1.3");
        if (strcmp(s->library,"libstdc++.so.6")) return false;
        const char *names[] = {"__cxa_guard_acquire","__cxa_guard_release","__cxa_guard_abort",
            NULL,NULL,"__cxa_allocate_exception","__cxa_free_exception"};
        if (s->operation >= 3 && s->operation <= 9 && names[s->operation-3])
            return named(s,names[s->operation-3],s->operation,0) && version(s,"CXXABI_1.3");
        if (s->operation == 6) return version(s,"GLIBCXX_3.4") &&
            (named(s,wide ? "_Znwm" : "_Znwj",6,0) || named(s,wide ? "_Znam" : "_Znaj",6,0));
        if (s->operation == 7) return version(s,"GLIBCXX_3.4") &&
            (named(s,"_ZdlPv",7,0) || named(s,"_ZdaPv",7,0));
        return s->operation == 10 && version(s,"GLIBCXX_3.4") &&
            (named(s,"__guest_type_info_is_pointer",10,0) || named(s,"__guest_type_info_is_function",10,0));
    }
    if (s->group == SYSV_STDIO) {
        if (strcmp(s->library,"libc.so.6")) return false;
        const char *names[] = {"fflush","fputc","fgetc","ungetc","fwrite","fread","fwide","__guest_IO_file_overflow",
            "fopen","fclose","fseek","ftell"};
        if (s->operation < 1 || s->operation > 12) return false;
        bool name = !strcmp(s->name,names[s->operation-1]) ||
            (s->operation == 2 && !strcmp(s->name,"putc")) ||
            (s->operation == 3 && !strcmp(s->name,"getc")) ||
            (s->operation == 3 && !strcmp(s->name,"__uflow")) ||
            (s->operation == 8 && !strcmp(s->name,"__overflow")) ||
            (s->operation == 1 && !strcmp(s->name,"__guest_IO_file_sync")) ||
            (s->operation == 9 && !strcmp(s->name,"fopen64"));
        if ((s->operation == 8 && strcmp(s->name,"__overflow")) ||
            (s->operation == 1 && !strcmp(s->name,"__guest_IO_file_sync"))) return name && !s->version;
        return name && version(s,!wide && (s->operation == 7 || !strcmp(s->name,"fopen64")) ? "GLIBC_2.1" : base);
    }
    if (s->group == SYSV_LOCALE) {
        if (s->operation == 1) return !strcmp(s->library,"libc.so.6") && version(s,"GLIBC_2.3") &&
            (!strcmp(s->name,"__ctype_b_loc") || !strcmp(s->name,"__ctype_tolower_loc") || !strcmp(s->name,"__ctype_toupper_loc"));
        if (strcmp(s->library,"libstdc++.so.6") || !version(s,"GLIBCXX_3.4")) return false;
        const char *names[] = {NULL,"is","toupper","tolower","toupper_range","tolower_range","widen","widen_range","narrow"};
        if (s->operation < 2 || s->operation > 9) return false;
        const char *prefix = s->a ? "__guest_ctype_w_" : "__guest_ctype_c_";
        return !strncmp(s->name,prefix,strlen(prefix)) && !strcmp(s->name+strlen(prefix),names[s->operation-1]);
    }
    if (s->group == SYSV_IOSTREAM) {
        if (strcmp(s->library,"libstdc++.so.6") || !version(s,"GLIBCXX_3.4")) return false;
        if (s->operation <= 2) {
            const char *names[] = {"_ZNSt8ios_base4InitC1Ev","_ZNSt8ios_base4InitC2Ev","_ZNSt8ios_base4InitD1Ev","_ZNSt8ios_base4InitD2Ev"};
            return !s->a && (!strcmp(s->name,names[(s->operation-1)*2]) || !strcmp(s->name,names[(s->operation-1)*2+1]));
        }
        if (s->operation == 3) return !strcmp(s->name,s->a ? "_ZNSt9basic_iosIwSt11char_traitsIwEE5clearESt12_Ios_Iostate" :
            "_ZNSt9basic_iosIcSt11char_traitsIcEE5clearESt12_Ios_Iostate");
        if (s->operation == 4) return !strcmp(s->name,s->a ? "_ZNSt13basic_ostreamIwSt11char_traitsIwEE5flushEv" : "_ZNSo5flushEv");
        const char *prefix = s->a ? "__guest_N9__gnu_cxx18stdio_sync_filebufIwSt11char_traitsIwEEE_" :
            "__guest_N9__gnu_cxx18stdio_sync_filebufIcSt11char_traitsIcEEE_";
        const char *suffixes[] = {"imbue","setbuf","sync","showmanyc","xsgetn","underflow","uflow","pbackfail","xsputn","overflow"};
        return s->operation >= 5 && s->operation <= 14 && !strncmp(s->name,prefix,strlen(prefix)) &&
            !strcmp(s->name+strlen(prefix),suffixes[s->operation-5]);
    }
    return false;
}

/* Validate the scalar ABI against the actual dispatcher operation before any
 * decoded record can reach argument indexing. No service installer runs here. */
bool sysv_service_valid(const sysv_service *s,qa_error *error)
{
    qa_native_value_type p = QA_NATIVE_ADDRESS,z = sysv_size_type(s->runtime),signed_size = sysv_signed_type(s->runtime);
    qa_native_value_type parameters[8] = {p,p,p,p,p,p,p,p},result = QA_NATIVE_VOID;
    size_t count = 0; bool valid = !s->b && !s->c;
    switch (s->group) {
    case SYSV_RUNTIME: count = 1; valid = valid && s->detail && !s->a; break;
    case SYSV_FORMAT:
        valid = valid && !s->a && (s->operation == 1 || s->operation == 2);
        count = s->operation == 1 ? 4 : 6; result = QA_NATIVE_I32; parameters[1] = z;
        if (s->operation == 2) { parameters[2] = QA_NATIVE_I32; parameters[3] = z; }
        break;
    case SYSV_CXX:
        valid = valid && !s->a && s->operation >= 1 && s->operation <= 10;
        count = s->operation == 1 ? 3 : 1;
        if (s->operation == 1 || s->operation == 3 || s->operation == 10) result = QA_NATIVE_I32;
        if (s->operation == 6 || s->operation == 8) { result = p; parameters[0] = z; }
        break;
    case SYSV_STDIO:
        valid = valid && !s->a && s->operation >= 1 && s->operation <= 12; result = QA_NATIVE_I32;
        count = s->operation == 1 || s->operation == 3 ? 1 : s->operation == 5 || s->operation == 6 ? 4 : 2;
        if (s->operation == 2 || s->operation == 4) parameters[0] = QA_NATIVE_I32;
        if (s->operation == 7 || s->operation == 8) parameters[1] = QA_NATIVE_I32;
        if (count == 4) { parameters[1] = z; parameters[2] = z; result = z; }
        if (s->operation == 9) { count = 2; result = p; }
        if (s->operation == 10 || s->operation == 12) count = 1;
        if (s->operation == 11) { count = 3; parameters[1] = signed_size; parameters[2] = QA_NATIVE_I32; }
        if (s->operation == 12) result = signed_size;
        break;
    case SYSV_LOCALE: {
        valid = valid && s->operation >= 1 && s->operation <= 9;
        if (s->operation == 1) { result = p; count = 0; valid = valid && s->a; break; }
        valid = valid && s->a <= 1;
        qa_native_value_type character = s->a ? QA_NATIVE_U32 : QA_NATIVE_I32;
        if (s->operation == 2) { count = 3; parameters[1] = parameters[2] = QA_NATIVE_U32; result = QA_NATIVE_I32; valid = valid && s->a; }
        else if (s->operation == 5 || s->operation == 6 || s->operation == 8) { count = s->operation == 8 ? 4 : 3; result = p; }
        else { count = s->operation == 9 ? 3 : 2; parameters[1] = s->operation == 7 ? QA_NATIVE_I32 : character;
            result = s->operation == 9 ? QA_NATIVE_I32 : character; if (count == 3) parameters[2] = QA_NATIVE_I32; }
        break;
    }
    case SYSV_IOSTREAM:
        valid = valid && s->a <= 1 && s->operation >= 1 && s->operation <= 14;
        count = s->operation == 3 || s->operation == 5 || s->operation == 12 || s->operation == 14 ? 2 :
            s->operation == 6 || s->operation == 9 || s->operation == 13 ? 3 : 1;
        if (s->operation == 3) parameters[1] = QA_NATIVE_I32;
        if (s->operation == 6 || s->operation == 9 || s->operation == 13) parameters[2] = signed_size;
        if (s->operation == 12 || s->operation == 14) parameters[1] = s->a ? QA_NATIVE_U32 : QA_NATIVE_I32;
        if (s->operation == 4 || s->operation == 6) result = p;
        else if (s->operation == 7) result = QA_NATIVE_I32;
        else if (s->operation == 8 || s->operation == 9 || s->operation == 13) result = signed_size;
        else if (s->operation >= 10) result = s->a ? QA_NATIVE_U32 : QA_NATIVE_I32;
        break;
    case SYSV_LIBC:
        valid = valid && s->operation >= 1 && s->operation <= 24;
        switch (s->operation) {
        case 1: result = p; break;
        case 2: count = 1; result = p; valid = valid && s->runtime->target.pointer_bytes == 8; break;
        case 3: count = 1; parameters[0] = z; result = p; break;
        case 4: count = 2; parameters[0] = parameters[1] = z; result = p; break;
        case 5: count = 1; break;
        case 6: count = 2; parameters[1] = z; result = p; break;
        case 7: count = s->a ? 4 : 3; parameters[2] = parameters[3] = z; result = p; valid = valid && s->a <= 1; break;
        case 8: count = 3; parameters[1] = QA_NATIVE_I32; parameters[2] = z; result = p; break;
        case 9: count = 3; parameters[2] = z; result = QA_NATIVE_I32; break;
        case 10: count = 1; result = z; break;
        case 11: count = 2; result = QA_NATIVE_I32; break;
        case 12: count = (s->a&1) ? 3 : 2; parameters[2] = z; result = p;
            valid = valid && s->a <= 7 && (s->a&6) != 6; break;
        case 13: count = 3; parameters[2] = z; result = p; break;
        case 14: count = 2; parameters[1] = QA_NATIVE_I32; result = p; valid = valid && s->a <= 1; break;
        case 15: count = 2; result = p; valid = valid && s->a <= 1; break;
        case 16: count = 2; result = p; break;
        case 17: count = 3; parameters[2] = QA_NATIVE_I32; result = signed_size; break;
        case 18: count = 1; result = signed_size; break;
        case 19: count = 4; parameters[1] = parameters[2] = z; break;
        case 20: count = 1; result = s->result.kind; parameters[0] = result;
            valid = valid && s->a < 6 && (result == QA_NATIVE_F32 || result == QA_NATIVE_F64); break;
        case 21: count = 2; parameters[0] = parameters[1] = QA_NATIVE_F64; result = QA_NATIVE_F64; break;
        case 22: count = 3; parameters[0] = s->parameters[0].kind;
            valid = valid && (parameters[0] == QA_NATIVE_F32 || parameters[0] == QA_NATIVE_F64); break;
        case 23: count = 1; parameters[0] = QA_NATIVE_F32; result = QA_NATIVE_I32; break;
        default: break;
        }
        if (s->operation != 7 && s->operation != 12 && s->operation != 14 && s->operation != 15 && s->operation != 20)
            valid = valid && !s->a;
        break;
    default: valid = false; break;
    }
    valid = valid && count == s->parameter_count && result == s->result.kind && identity(s);
    for (size_t i = 0; i < count && valid; ++i) valid = parameters[i] == s->parameters[i].kind;
    return valid || sysv_fail(error,QA_ERROR_FORMAT,"saved System V service operation differs from its actual scalar ABI");
}
