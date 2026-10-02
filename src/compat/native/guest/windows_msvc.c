#include "internal.h"
#include "windows_msvc.h"
#include "windows_crt.h"
#include <stdio.h>
#include <ctype.h>

#define BUFFER "?$basic_streambuf@DU?$char_traits@D@std@@@std@@"
#define IOS "?$basic_ios@DU?$char_traits@D@std@@@std@@"
#define OSTREAM "?$basic_ostream@DU?$char_traits@D@std@@@std@@"
#define IOSTREAM "?$basic_iostream@DU?$char_traits@D@std@@@std@@"
enum msvc_operation {
    M_INCREF=1, M_DECREF, M_FACET_DTOR, M_FACET_DELETE, M_FACET_CTOR,
    M_LOCALE_INIT, M_GLOBAL, M_LOCK, M_UNLOCK, M_LOCINFO_CTOR, M_LOCINFO_DTOR,
    M_TRUE, M_FALSE, M_LCONV, M_CVTVEC, M_IOS_DTOR, M_IOS_DELETE, M_IOS_CTOR,
    M_RDBUF, M_SETSTATE, M_GOOD, M_OSTREAM_CTOR, M_IOSTREAM_CTOR,
    M_OSTREAM_DTOR, M_IOSTREAM_DTOR, M_OSTREAM_DELETE, M_BUFFER_DTOR, M_BUFFER_DELETE,
    M_BUFFER_LOCK, M_BUFFER_OVERFLOW, M_BUFFER_UNDERFLOW, M_SHOWMANY, M_SYNC,
    M_SETBUF, M_IMBUE, M_EBACK, M_PBASE, M_GPTR, M_PPTR, M_EGPTR, M_EPPTR,
    M_UFLOW, M_PUTC, M_GETN, M_PUTN, M_SPUTN, M_SEEKOFF, M_SEEKPOS,
    M_BUFFER_CTOR, M_FLUSH, M_SUFFIX, M_UNCAUGHT, M_TELLP, M_INTEGER32,
    M_INTEGER64, M_INSERT_BUFFER
};
typedef struct msvc_descriptor {
    const char *name; uint32_t operation; qa_native_value_type result;
    size_t count; qa_native_value_type parameters[5];
} msvc_descriptor;
#define P QA_NATIVE_ADDRESS
#define I QA_NATIVE_I32
#define U QA_NATIVE_U32
#define J QA_NATIVE_I64
#define V QA_NATIVE_VOID
#define D(n,o,r,c,...) {n,o,r,c,{__VA_ARGS__}}
static const msvc_descriptor descriptors[] = {
    D("?_Incref@facet@locale@std@@UEAAXXZ",M_INCREF,V,1,P), D("?_Decref@facet@locale@std@@UEAAPEAV_Facet_base@3@XZ",M_DECREF,P,1,P),
    D("??1facet@locale@std@@MEAA@XZ",M_FACET_DTOR,V,1,P), D("runtime:facet-delete",M_FACET_DELETE,P,2,P,U),
    D("??0facet@locale@std@@IEAA@_K@Z",M_FACET_CTOR,P,2,P,QA_NATIVE_U64), D("?_Init@locale@std@@CAPEAV_Locimp@12@_N@Z",M_LOCALE_INIT,P,1,U),
    D("?_Getgloballocale@locale@std@@CAPEAV_Locimp@12@XZ",M_GLOBAL,P,0,0), D("??0_Lockit@std@@QEAA@H@Z",M_LOCK,P,2,P,I), D("??1_Lockit@std@@QEAA@XZ",M_UNLOCK,V,1,P),
    D("??0_Locinfo@std@@QEAA@PEBD@Z",M_LOCINFO_CTOR,P,2,P,P), D("??1_Locinfo@std@@QEAA@XZ",M_LOCINFO_DTOR,V,1,P),
    D("?_Gettrue@_Locinfo@std@@QEBAPEBDXZ",M_TRUE,P,1,P), D("?_Getfalse@_Locinfo@std@@QEBAPEBDXZ",M_FALSE,P,1,P),
    D("?_Getlconv@_Locinfo@std@@QEBAPEBUlconv@@XZ",M_LCONV,P,1,P), D("?_Getcvt@_Locinfo@std@@QEBA?AU_Cvtvec@@XZ",M_CVTVEC,P,2,P,P),
    D("??1" IOS "UEAA@XZ",M_IOS_DTOR,V,1,P), D("runtime:basic-ios-delete",M_IOS_DELETE,P,2,P,U), D("??0" IOS "IEAA@XZ",M_IOS_CTOR,P,1,P),
    D("?rdbuf@" IOS "QEBAPEAV?$basic_streambuf@DU?$char_traits@D@std@@@2@XZ",M_RDBUF,P,1,P), D("?setstate@" IOS "QEAAXH_N@Z",M_SETSTATE,V,3,P,I,U), D("?good@ios_base@std@@QEBA_NXZ",M_GOOD,U,1,P),
    D("??0" OSTREAM "QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@_N@Z",M_OSTREAM_CTOR,P,4,P,P,U,I),
    D("??0" IOSTREAM "QEAA@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z",M_IOSTREAM_CTOR,P,3,P,P,I),
    D("??1" OSTREAM "UEAA@XZ",M_OSTREAM_DTOR,V,1,P), D("??1" IOSTREAM "UEAA@XZ",M_IOSTREAM_DTOR,V,1,P), D("runtime:ostream-delete",M_OSTREAM_DELETE,P,2,P,U),
    D("??1" BUFFER "UEAA@XZ",M_BUFFER_DTOR,V,1,P), D("runtime:streambuf-delete",M_BUFFER_DELETE,P,2,P,U),
    D("?_Lock@" BUFFER "UEAAXXZ",M_BUFFER_LOCK,V,1,P), D("?_Unlock@" BUFFER "UEAAXXZ",M_BUFFER_LOCK,V,1,P),
    D("runtime:streambuf-overflow",M_BUFFER_OVERFLOW,I,2,P,I), D("runtime:streambuf-pbackfail",M_BUFFER_OVERFLOW,I,2,P,I), D("runtime:streambuf-underflow",M_BUFFER_UNDERFLOW,I,1,P),
    D("?showmanyc@" BUFFER "MEAA_JXZ",M_SHOWMANY,J,1,P), D("?sync@" BUFFER "MEAAHXZ",M_SYNC,I,1,P), D("?setbuf@" BUFFER "MEAAPEAV12@PEAD_J@Z",M_SETBUF,P,3,P,P,J), D("?imbue@" BUFFER "MEAAXAEBVlocale@2@@Z",M_IMBUE,V,2,P,P),
    D("?eback@" BUFFER "IEBAPEADXZ",M_EBACK,P,1,P), D("?pbase@" BUFFER "IEBAPEADXZ",M_PBASE,P,1,P), D("?gptr@" BUFFER "IEBAPEADXZ",M_GPTR,P,1,P), D("?pptr@" BUFFER "IEBAPEADXZ",M_PPTR,P,1,P),
    D("?egptr@" BUFFER "IEBAPEADXZ",M_EGPTR,P,1,P), D("?epptr@" BUFFER "IEBAPEADXZ",M_EPPTR,P,1,P),
    D("?uflow@" BUFFER "MEAAHXZ",M_UFLOW,I,1,P), D("?sputc@" BUFFER "QEAAHD@Z",M_PUTC,I,2,P,I),
    D("?xsgetn@" BUFFER "MEAA_JPEAD_J@Z",M_GETN,J,3,P,P,J), D("?xsputn@" BUFFER "MEAA_JPEBD_J@Z",M_PUTN,J,3,P,P,J), D("?sputn@" BUFFER "QEAA_JPEBD_J@Z",M_SPUTN,J,3,P,P,J),
    D("runtime:streambuf-seekoff",M_SEEKOFF,P,5,P,P,J,I,I), D("runtime:streambuf-seekpos",M_SEEKPOS,P,4,P,P,P,I), D("??0" BUFFER "IEAA@XZ",M_BUFFER_CTOR,P,1,P),
    D("?flush@" OSTREAM "QEAAAEAV12@XZ",M_FLUSH,P,1,P), D("?_Osfx@" OSTREAM "QEAAXXZ",M_SUFFIX,V,1,P), D("?uncaught_exception@std@@YA_NXZ",M_UNCAUGHT,U,0,0),
    D("?tellp@" OSTREAM "QEAA?AV?$fpos@U_Mbstatet@@@2@XZ",M_TELLP,P,2,P,P),
    D("??6" OSTREAM "QEAAAEAV01@H@Z",M_INTEGER32,P,2,P,I), D("??6" OSTREAM "QEAAAEAV01@_J@Z",M_INTEGER64,P,2,P,J),
    D("??6" OSTREAM "QEAAAEAV01@PEAV?$basic_streambuf@DU?$char_traits@D@std@@@1@@Z",M_INSERT_BUFFER,P,2,P,P)
};
#undef D
#undef P
#undef I
#undef U
#undef J
#undef V

static void result(qa_native_value *out, qa_native_value_type type, uint64_t value)
{
    *out = (qa_native_value){.type = type};
    if (type == QA_NATIVE_ADDRESS) out->as.address = value;
    else if (type == QA_NATIVE_U32) out->as.u32 = (uint32_t)value;
    else if (type == QA_NATIVE_I32) { uint32_t bits = (uint32_t)value; memcpy(&out->as.i32, &bits, 4); }
    else if (type == QA_NATIVE_I64) memcpy(&out->as.i64, &value, 8);
}

bool windows_msvc_descriptors(guest_windows *owner, bool bind, qa_error *error)
{
    if (owner->target.pointer_bytes != 8) return true;
    for (size_t i = 0; i < sizeof(descriptors)/sizeof(*descriptors); ++i) {
        const msvc_descriptor *entry = descriptors + i;
        if (!windows_service_add(owner, UINT64_C(0x57494e0300000000) + i + 1, 3, entry->operation,
            "msvcp140.dll", entry->name, entry->parameters, entry->count, entry->result, GUEST_ABI_DEFAULT, bind, error)) return false;
    }
    return true;
}

static bool table(guest_windows *owner, const char *const *names, size_t count,
    uint64_t *out, qa_error *error)
{
    if (!windows_storage(owner, count * 8, out, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint64_t target;
        if (!guest_windows_resolve(owner, "msvcp140.dll", names[i], &target, error) || !target ||
            !windows_write(owner, *out + i * 8, 8, target, error)) return false;
    }
    return qa_native_guest_protect_range(owner->guest, *out, 4096, QA_NATIVE_GUEST_READ, error);
}

bool windows_msvc_initialize(guest_windows *owner, qa_error *error)
{
    guest_windows_msvc *msvc = owner->msvc;
    /* Data exports exist for both pointer widths, even without x64 streams. */
    if (!windows_storage(owner, 4, &msvc->locale_id_count, error) || !windows_storage(owner, owner->target.pointer_bytes, &msvc->numpunct_id, error)) return false;
    guest_runtime_import_key key = {.library = "msvcp140.dll", .kind = GUEST_RUNTIME_SYMBOL_NAME, .name = "?_Id_cnt@id@locale@std@@0HA"};
    if (!guest_runtime_imports_data(owner->imports, &key, msvc->locale_id_count, 4, error)) return false;
    key.name = "?id@?$numpunct@D@std@@2V0locale@2@A";
    if (!guest_runtime_imports_data(owner->imports, &key, msvc->numpunct_id, owner->target.pointer_bytes, error)) return false;
    if (owner->target.pointer_bytes != 8) return true;
    const char *facet[] = {"runtime:facet-delete","?_Incref@facet@locale@std@@UEAAXXZ","?_Decref@facet@locale@std@@UEAAPEAV_Facet_base@3@XZ"};
    const char *ios[] = {"runtime:basic-ios-delete"}, *output[] = {"runtime:ostream-delete"};
    const char *buffer[] = {"runtime:streambuf-delete","?_Lock@" BUFFER "UEAAXXZ","?_Unlock@" BUFFER "UEAAXXZ",
        "runtime:streambuf-overflow","runtime:streambuf-pbackfail","?showmanyc@" BUFFER "MEAA_JXZ","runtime:streambuf-underflow",
        "?uflow@" BUFFER "MEAAHXZ","?xsgetn@" BUFFER "MEAA_JPEAD_J@Z","?xsputn@" BUFFER "MEAA_JPEBD_J@Z",
        "runtime:streambuf-seekoff","runtime:streambuf-seekpos","?setbuf@" BUFFER "MEAAPEAV12@PEAD_J@Z","?sync@" BUFFER "MEAAHXZ","?imbue@" BUFFER "MEAAXAEBVlocale@2@@Z"};
    const uint16_t c_name[] = {'C'}, true_name[] = {'t','r','u','e'}, false_name[] = {'f','a','l','s','e'};
    uint64_t name;
    if (!windows_storage(owner, 64, &msvc->locks, error) || !table(owner, facet, 3, &msvc->facet_vtable, error) ||
        !windows_storage(owner, 56, &msvc->global_locale, error) || !windows_store_string(owner, c_name, 1, false, &name, error) ||
        !windows_write(owner, msvc->global_locale, 8, msvc->facet_vtable, error) || !windows_write(owner, msvc->global_locale + 8, 4, 2, error) ||
        !windows_write(owner, msvc->global_locale + 32, 4, 63, error) || !windows_write(owner, msvc->global_locale + 40, 8, name, error) ||
        !windows_store_string(owner, true_name, 4, false, &msvc->true_text, error) || !windows_store_string(owner, false_name, 5, false, &msvc->false_text, error) ||
        !table(owner, ios, 1, &msvc->ios_vtable, error) || !table(owner, output, 1, &msvc->ostream_vtable, error) ||
        !table(owner, output, 1, &msvc->iostream_vtable, error) || !table(owner, buffer, 15, &msvc->buffer_vtable, error) ||
        !windows_storage(owner, 8, &msvc->ostream_vbase, error) || !windows_storage(owner, 16, &msvc->iostream_vbase, error) ||
        !windows_write(owner, msvc->ostream_vbase + 4, 4, 16, error) || !windows_write(owner, msvc->iostream_vbase + 4, 4, 32, error) ||
        !windows_write(owner, msvc->iostream_vbase + 12, 4, 16, error) ||
        !qa_native_guest_protect_range(owner->guest, msvc->ostream_vbase, 4096, QA_NATIVE_GUEST_READ, error) ||
        !qa_native_guest_protect_range(owner->guest, msvc->iostream_vbase, 4096, QA_NATIVE_GUEST_READ, error)) return false;
    return true;
}

static bool reference(guest_windows *owner, uint64_t slot, uint64_t *out, qa_error *error)
{
    return windows_read(owner, slot, 8, out, error) && (*out || guest_fail(error, QA_ERROR_ARGUMENT, slot, "missing MSVC object pointer"));
}

static bool signed32(guest_windows *owner, uint64_t at, int32_t *out, qa_error *error)
{
    uint64_t value; if (!windows_read(owner, at, 4, &value, error)) return false;
    uint32_t bits = (uint32_t)value; memcpy(out, &bits, 4); return true;
}

static bool incref(guest_windows *owner, uint64_t facet, qa_error *error)
{
    uint64_t refs; return windows_read(owner, facet + 8, 4, &refs, error) && windows_write(owner, facet + 8, 4, refs + 1, error);
}

static bool decref(guest_windows *owner, uint64_t facet, uint64_t *out, qa_error *error)
{
    uint64_t refs;
    if (!windows_read(owner, facet + 8, 4, &refs, error)) return false;
    if (!refs) return guest_fail(error, QA_ERROR_ARGUMENT, facet, "MSVC facet reference underflow");
    if (!windows_write(owner, facet + 8, 4, refs - 1, error)) return false;
    *out = refs == 1 ? facet : 0; return true;
}

static bool locale_create(guest_windows *owner, uint64_t *out, qa_error *error)
{
    return windows_allocate(owner, 8, 0, out, error) && (*out || guest_fail(error, QA_ERROR_MEMORY, 0, "MSVC locale allocation failed")) &&
        incref(owner, owner->msvc->global_locale, error) && windows_write(owner, *out, 8, owner->msvc->global_locale, error);
}

static bool locale_destroy(guest_windows *owner, uint64_t locale, qa_error *error)
{
    if (!locale) return true;
    uint64_t facet, returned;
    if (!reference(owner, locale, &facet, error)) return false;
    if (facet != owner->msvc->global_locale) return guest_fail(error, QA_ERROR_UNSUPPORTED, facet, "non-C MSVC locale implementation destructor is not implemented");
    if (!decref(owner, facet, &returned, error)) return false;
    if (returned) return guest_fail(error, QA_ERROR_ARGUMENT, facet, "MSVC process locale lost its owner references");
    return windows_free(owner, locale, 0, error);
}

static bool locale_lock(guest_windows *owner, uint64_t object, int32_t kind, bool unlock, qa_error *error)
{
    if (!unlock && !windows_write(owner, object, 4, (uint32_t)kind, error)) return false;
    if (unlock && !signed32(owner, object, &kind, error)) return false;
    if (kind < 0 || kind >= 8) return true;
    uint64_t slot = owner->msvc->locks + (uint32_t)kind * 8, thread, depth;
    if (!windows_read(owner, slot, 4, &thread, error) || !windows_read(owner, slot + 4, 4, &depth, error)) return false;
    if (unlock) {
        if (thread != owner->thread_id || !depth) return guest_fail(error, QA_ERROR_ARGUMENT, slot, "unowned MSVC lock release");
        return windows_write(owner, slot + 4, 4, depth - 1, error) && (depth != 1 || windows_write(owner, slot, 4, 0, error));
    }
    if (thread && thread != owner->thread_id) return guest_fail(error, QA_ERROR_UNSUPPORTED, slot, "MSVC lock contention requires thread scheduling");
    return windows_write(owner, slot, 4, owner->thread_id, error) && windows_write(owner, slot + 4, 4, depth + 1, error);
}

static bool virtual_call(guest_windows *owner, uint64_t object, size_t slot,
    const qa_native_value *extra, size_t count, qa_native_value_type type,
    qa_native_value *out, qa_error *error)
{
    uint64_t vtable, target;
    if (!reference(owner, object, &vtable, error) || !reference(owner, vtable + slot * 8, &target, error)) return false;
    qa_native_value args[6] = {{.type = QA_NATIVE_ADDRESS, .as.address = object}};
    qa_native_value_type types[6] = {QA_NATIVE_ADDRESS};
    for (size_t i = 0; i < count; ++i) { args[i + 1] = extra[i]; types[i + 1] = extra[i].type; }
    return windows_invoke(owner, target, types, count + 1, type, args, false, out, error);
}

static bool virtual_ios(guest_windows *owner, uint64_t object, uint64_t *out, qa_error *error)
{
    uint64_t vbase; int32_t offset;
    if (!reference(owner, object, &vbase, error) || !signed32(owner, vbase + 4, &offset, error)) return false;
    *out = object + (uint64_t)(int64_t)offset; return true;
}

static bool setstate(guest_windows *owner, uint64_t object, uint32_t state, qa_error *error)
{
    uint64_t previous, buffer, mask;
    if (!windows_read(owner, object + 16, 4, &previous, error) || !windows_read(owner, object + 72, 8, &buffer, error)) return false;
    uint32_t value = ((uint32_t)previous | state | (!buffer ? 4 : 0)) & 0x17;
    if (!windows_write(owner, object + 16, 4, value, error) || !windows_read(owner, object + 20, 4, &mask, error)) return false;
    return !(value & mask) || guest_fail(error, QA_ERROR_UNSUPPORTED, object, "MSVC stream exception requires guest C++ exception dispatch");
}

static bool ios_construct(guest_windows *owner, uint64_t object, qa_error *error)
{ return windows_zero(owner, object, 96, error) && windows_write(owner, object, 8, owner->msvc->ios_vtable, error); }

static bool ios_initialize(guest_windows *owner, uint64_t object, uint64_t buffer, qa_error *error)
{
    uint64_t locale;
    return windows_write(owner, object + 8, 8, 0, error) && windows_write(owner, object + 16, 4, buffer ? 0 : 4, error) &&
        windows_write(owner, object + 20, 4, 0, error) && windows_write(owner, object + 24, 4, 0x201, error) &&
        windows_write(owner, object + 32, 8, 6, error) && windows_write(owner, object + 40, 8, 0, error) &&
        windows_write(owner, object + 48, 8, 0, error) && windows_write(owner, object + 56, 8, 0, error) &&
        locale_create(owner, &locale, error) && windows_write(owner, object + 64, 8, locale, error) &&
        windows_write(owner, object + 72, 8, buffer, error) && windows_write(owner, object + 80, 8, 0, error) && windows_write(owner, object + 88, 1, 32, error);
}

static bool ios_destroy(guest_windows *owner, uint64_t object, qa_error *error)
{
    uint64_t standard, node, locale;
    if (!windows_write(owner, object, 8, owner->msvc->ios_vtable, error) || !windows_read(owner, object + 8, 8, &standard, error)) return false;
    if (standard) return guest_fail(error, QA_ERROR_UNSUPPORTED, object, "MSVC standard stream ownership is not implemented");
    if (!windows_read(owner, object + 56, 8, &node, error)) return false;
    while (node) {
        uint64_t callback; int32_t index; qa_native_value ignored;
        if (!reference(owner, node + 16, &callback, error) || !signed32(owner, node + 8, &index, error)) return false;
        const qa_native_value_type types[] = {QA_NATIVE_I32,QA_NATIVE_ADDRESS,QA_NATIVE_I32};
        const qa_native_value args[] = {{.type=QA_NATIVE_I32,.as.i32=0},{.type=QA_NATIVE_ADDRESS,.as.address=object},{.type=QA_NATIVE_I32,.as.i32=index}};
        if (!windows_invoke(owner, callback, types, 3, QA_NATIVE_VOID, args, false, &ignored, error) || !windows_read(owner, node, 8, &node, error)) return false;
    }
    const size_t offsets[] = {48,56};
    for (size_t i = 0; i < 2; ++i) {
        if (!windows_read(owner, object + offsets[i], 8, &node, error)) return false;
        while (node) { uint64_t next; if (!windows_read(owner, node, 8, &next, error) || !windows_free(owner, node, 0, error)) return false; node = next; }
        if (!windows_write(owner, object + offsets[i], 8, 0, error)) return false;
    }
    return windows_read(owner, object + 64, 8, &locale, error) && locale_destroy(owner, locale, error) && windows_write(owner, object + 64, 8, 0, error);
}

static bool buffer_field(guest_windows *owner, uint64_t object, size_t offset, uint64_t *out, qa_error *error)
{
    uint64_t slot; return reference(owner, object + offset, &slot, error) && windows_read(owner, slot, 8, out, error);
}

static bool available(guest_windows *owner, uint64_t object, bool input, int32_t *out, qa_error *error)
{
    uint64_t next, slot;
    if (!buffer_field(owner, object, input ? 56 : 64, &next, error)) return false;
    if (!next) { *out = 0; return true; }
    return reference(owner, object + (input ? 80 : 88), &slot, error) && signed32(owner, slot, out, error);
}

static bool bump(guest_windows *owner, uint64_t object, int32_t amount, bool input, uint64_t *out, qa_error *error)
{
    uint64_t next_slot, next, count_slot; int32_t count;
    if (!reference(owner, object + (input ? 56 : 64), &next_slot, error) || !reference(owner, next_slot, &next, error) ||
        !reference(owner, object + (input ? 80 : 88), &count_slot, error) || !signed32(owner, count_slot, &count, error) ||
        !windows_write(owner, next_slot, 8, next + (uint64_t)(int64_t)amount, error) ||
        !windows_write(owner, count_slot, 4, (uint32_t)count - (uint32_t)amount, error)) return false;
    *out = next; return true;
}

static bool buffer_get(guest_windows *owner, uint64_t object, bool consume, int32_t *out, qa_error *error)
{
    int32_t count; uint64_t address, value;
    if (!available(owner, object, true, &count, error)) return false;
    if (count > 0) {
        if (!(consume ? bump(owner, object, 1, true, &address, error) : buffer_field(owner, object, 56, &address, error)) ||
            !windows_read(owner, address, 1, &value, error)) return false;
        *out = (int32_t)value; return true;
    }
    qa_native_value result;
    if (!virtual_call(owner, object, consume ? 7 : 6, NULL, 0, QA_NATIVE_I32, &result, error)) return false;
    *out = result.as.i32; return true;
}

static bool buffer_put(guest_windows *owner, uint64_t object, int32_t c, int32_t *out, qa_error *error)
{
    int32_t count; uint64_t address;
    if (!available(owner, object, false, &count, error)) return false;
    if (count > 0) {
        if (!bump(owner, object, 1, false, &address, error) || !windows_write(owner, address, 1, (uint8_t)c, error)) return false;
        *out = (uint8_t)c; return true;
    }
    qa_native_value argument = {.type=QA_NATIVE_I32,.as.i32=(uint8_t)c}, result;
    if (!virtual_call(owner, object, 3, &argument, 1, QA_NATIVE_I32, &result, error)) return false;
    *out = result.as.i32; return true;
}

static bool buffer_destroy(guest_windows *owner, uint64_t object, qa_error *error)
{
    uint64_t locale;
    return windows_write(owner, object, 8, owner->msvc->buffer_vtable, error) && windows_read(owner, object + 96, 8, &locale, error) &&
        locale_destroy(owner, locale, error) && windows_write(owner, object + 96, 8, 0, error);
}

static bool unlock_buffer(guest_windows *owner, uint64_t buffer, bool okay, qa_error *error)
{
    if (!buffer) return okay;
    qa_error failure = error ? *error : (qa_error){0}; qa_native_value ignored;
    bool unlocked = virtual_call(owner, buffer, 2, NULL, 0, QA_NATIVE_VOID, &ignored, error);
    if (!okay && error) *error = failure;
    return okay && unlocked;
}

static bool suffix(guest_windows *owner, uint64_t object, qa_error *error)
{
    uint64_t base, state, flags, buffer;
    if (!virtual_ios(owner, object, &base, error) || !windows_read(owner, base + 16, 4, &state, error) ||
        !windows_read(owner, base + 24, 4, &flags, error)) return false;
    if (state || !(flags & 2)) return true;
    if (!windows_read(owner, base + 72, 8, &buffer, error)) return false;
    if (!buffer) return true;
    qa_native_value returned;
    if (!virtual_call(owner, buffer, 13, NULL, 0, QA_NATIVE_I32, &returned, error)) return false;
    return returned.as.i32 != -1 || setstate(owner, base, 4, error);
}

typedef struct flush_link { uint64_t object; const struct flush_link *parent; } flush_link;

static bool flush(guest_windows *owner, uint64_t object, const flush_link *parent, qa_error *error)
{
    for (const flush_link *at = parent; at; at = at->parent)
        if (at->object == object) return guest_fail(error, QA_ERROR_ARGUMENT, object, "MSVC tied streams form a recursive flush cycle");
    uint64_t base, buffer, state, tied; qa_native_value returned;
    if (!virtual_ios(owner, object, &base, error) || !windows_read(owner, base + 72, 8, &buffer, error)) return false;
    if (!buffer) return true;
    if (!virtual_call(owner, buffer, 1, NULL, 0, QA_NATIVE_VOID, &returned, error)) return false;
    bool okay = windows_read(owner, base + 16, 4, &state, error);
    if (okay && !state) {
        okay = windows_read(owner, base + 80, 8, &tied, error);
        flush_link link = {object,parent};
        if (okay && tied && tied != object) okay = flush(owner, tied, &link, error);
        if (okay) okay = windows_read(owner, base + 16, 4, &state, error);
        if (okay && !state) {
            okay = virtual_call(owner, buffer, 13, NULL, 0, QA_NATIVE_I32, &returned, error);
            if (okay && returned.as.i32 == -1) okay = setstate(owner, base, 4, error);
        }
    }
    if (okay) okay = suffix(owner, object, error);
    return unlock_buffer(owner, buffer, okay, error);
}

static bool insert_number(guest_windows *owner, uint64_t object, uint64_t integer,
    unsigned bits, qa_error *error)
{
    uint64_t base, buffer, state, tied; qa_native_value ignored;
    if (!virtual_ios(owner, object, &base, error) || !windows_read(owner, base + 72, 8, &buffer, error)) return false;
    if (buffer && !virtual_call(owner, buffer, 1, NULL, 0, QA_NATIVE_VOID, &ignored, error)) return false;
    bool okay = windows_read(owner, base + 16, 4, &state, error);
    if (okay && !state && buffer) {
        okay = windows_read(owner, base + 80, 8, &tied, error);
        if (okay && tied && tied != object) okay = flush(owner, tied, NULL, error);
        if (okay) okay = windows_read(owner, base + 16, 4, &state, error);
        if (okay && !state) {
            uint64_t locale, implementation, custom, flags, width_bits, fill;
            okay = reference(owner, base + 64, &locale, error) && reference(owner, locale, &implementation, error) &&
                windows_read(owner, implementation + 24, 8, &custom, error);
            if (okay && (implementation != owner->msvc->global_locale || custom))
                okay = guest_fail(error, QA_ERROR_UNSUPPORTED, implementation, "custom MSVC num_put locale requires facet dispatch");
            if (okay) okay = windows_read(owner, base + 24, 4, &flags, error) && windows_read(owner, base + 40, 8, &width_bits, error) && windows_read(owner, base + 88, 1, &fill, error);
            if (okay) {
                int64_t width; memcpy(&width, &width_bits, 8);
                if (width > 0x1000000) okay = guest_fail(error, QA_ERROR_ARGUMENT, base, "MSVC numeric field exceeds source limit");
                else {
                    unsigned radix = (flags & 0xe00) == 0x400 ? 8 : (flags & 0xe00) == 0x800 ? 16 : 10;
                    uint64_t mask = bits == 32 ? UINT32_MAX : UINT64_MAX; integer &= mask;
                    bool negative = radix == 10 && (integer & (UINT64_C(1) << (bits - 1)));
                    uint64_t magnitude = negative ? (UINT64_C(0) - integer) & mask : integer;
                    char reverse[65], digits[65], prefix[3] = {0}; size_t count = 0;
                    do { reverse[count++] = (flags & 4 ? "0123456789ABCDEF" : "0123456789abcdef")[magnitude % radix]; magnitude /= radix; } while (magnitude);
                    for (size_t i = 0; i < count; ++i) digits[i] = reverse[count - i - 1];
                    if (negative) prefix[0] = '-'; else if (radix == 10 && (flags & 0x20)) prefix[0] = '+';
                    if (radix != 10 && integer && (flags & 8)) { prefix[0] = '0'; if (radix == 16) prefix[1] = flags & 4 ? 'X' : 'x'; }
                    size_t prefix_count = strlen(prefix), padding = width > (int64_t)(prefix_count + count) ? (size_t)width - prefix_count - count : 0;
                    size_t total = prefix_count + count + padding;
                    char *bytes = malloc(total ? total : 1);
                    if (!bytes) okay = guest_fail(error, QA_ERROR_MEMORY, 0, "formatting MSVC numeric insertion");
                    else {
                        uint32_t adjustment = (uint32_t)flags & 0x1c0; size_t at = 0;
                        if (adjustment != 0x40 && adjustment != 0x100) { memset(bytes, (uint8_t)fill, padding); at += padding; }
                        memcpy(bytes + at, prefix, prefix_count); at += prefix_count;
                        if (adjustment == 0x100) { memset(bytes + at, (uint8_t)fill, padding); at += padding; }
                        memcpy(bytes + at, digits, count); at += count;
                        if (adjustment == 0x40) memset(bytes + at, (uint8_t)fill, padding);
                        for (size_t i = 0; okay && i < total; ++i) {
                            int32_t written;
                            okay = buffer_put(owner, buffer, (uint8_t)bytes[i], &written, error);
                            if (okay && written == -1) { okay = setstate(owner, base, 4, error); break; }
                        }
                        free(bytes);
                        if (okay) okay = windows_write(owner, base + 40, 8, 0, error);
                    }
                }
            }
        }
    }
    if (okay) okay = setstate(owner, base, 0, error) && suffix(owner, object, error);
    return unlock_buffer(owner, buffer, okay, error);
}

static bool insert_buffer(guest_windows *owner, uint64_t object, uint64_t source, qa_error *error)
{
    uint64_t base, target, state, tied; bool copied = false; uint32_t additional = 0; qa_native_value ignored;
    if (!virtual_ios(owner, object, &base, error) || !windows_read(owner, base + 72, 8, &target, error)) return false;
    if (target && !virtual_call(owner, target, 1, NULL, 0, QA_NATIVE_VOID, &ignored, error)) return false;
    bool okay = windows_read(owner, base + 16, 4, &state, error);
    if (okay && !state && target && source) {
        okay = windows_read(owner, base + 80, 8, &tied, error);
        if (okay && tied && tied != object) okay = flush(owner, tied, NULL, error);
        if (okay) okay = windows_read(owner, base + 16, 4, &state, error);
        if (okay && !state) for (;;) {
            int32_t character, written;
            if (!buffer_get(owner, source, false, &character, error)) { okay = false; break; }
            if (character == -1) break;
            if (!buffer_put(owner, target, character, &written, error)) { okay = false; break; }
            if (written == -1) { additional |= 4; break; }
            if (!buffer_get(owner, source, true, &character, error)) { okay = false; break; }
            copied = true;
        }
    }
    if (okay) okay = windows_write(owner, base + 40, 8, 0, error) &&
        setstate(owner, base, !source ? 4 : additional | (copied ? 0 : 2), error) && suffix(owner, object, error);
    return unlock_buffer(owner, target, okay, error);
}

static bool transfer(guest_windows *owner, uint64_t object, uint64_t data,
    int64_t requested, bool input, qa_native_value *out, qa_error *error)
{
    if (!object || !data) return guest_fail(error, QA_ERROR_ARGUMENT, 0, "MSVC stream transfer requires genuine object and data pointers");
    if (requested <= 0) { result(out, QA_NATIVE_I64, 0); return true; }
    if (requested > 0x10000000) return guest_fail(error, QA_ERROR_ARGUMENT, object, "MSVC stream transfer exceeds source limit");
    size_t copied = 0;
    while (copied < (size_t)requested) {
        int32_t available_count;
        if (!available(owner, object, input, &available_count, error)) return false;
        size_t amount = available_count > 0 ? (size_t)available_count : 0;
        if (amount > (size_t)requested - copied) amount = (size_t)requested - copied;
        if (amount) {
            uint64_t buffer; uint8_t *bytes = malloc(amount);
            if (!bytes) return guest_fail(error, QA_ERROR_MEMORY, 0, "staging MSVC stream transfer");
            bool okay = bump(owner, object, (int32_t)amount, input, &buffer, error) &&
                qa_native_guest_read(owner->guest, input ? buffer : data + copied, bytes, amount, error) &&
                qa_native_guest_write(owner->guest, input ? data + copied : buffer, (qa_bytes){bytes, amount}, error);
            free(bytes); if (!okay) return false; copied += amount;
        } else if (input) {
            int32_t character;
            if (!buffer_get(owner, object, true, &character, error)) return false;
            if (character == -1) break;
            if (!windows_write(owner, data + copied++, 1, (uint8_t)character, error)) return false;
        } else {
            uint64_t character; int32_t written;
            if (!windows_read(owner, data + copied, 1, &character, error) || !buffer_put(owner, object, (int32_t)character, &written, error)) return false;
            if (written == -1) break;
            ++copied;
        }
    }
    result(out, QA_NATIVE_I64, copied); return true;
}

bool windows_msvc_invoke(windows_service *service, const qa_native_value *args,
    size_t count, qa_native_value *out, qa_error *error)
{
    guest_windows *owner = service->owner; guest_windows_msvc *msvc = owner->msvc;
    uint32_t operation = service->operation; uint64_t object = count ? args[0].as.address : 0;
    uint64_t b = count > 1 && args[1].type == QA_NATIVE_ADDRESS ? args[1].as.address : 0, value = 0;
    qa_native_value_type type = service->function.signature.result.kind;
    result(out, type, type == QA_NATIVE_ADDRESS ? object : 0);
    switch (operation) {
    case M_INCREF: return incref(owner, object, error);
    case M_DECREF: if (!decref(owner, object, &value, error)) return false; result(out, type, value); return true;
    case M_FACET_DTOR: return windows_write(owner, object, 8, msvc->facet_vtable, error);
    case M_FACET_DELETE:
        if (object == msvc->global_locale) return guest_fail(error, QA_ERROR_UNSUPPORTED, object, "MSVC process classic/global locale still owns references");
        return windows_write(owner, object, 8, msvc->facet_vtable, error) && (!(args[1].as.u32 & 1) || windows_free(owner, object, 0, error));
    case M_FACET_CTOR: return windows_write(owner, object, 8, msvc->facet_vtable, error) && windows_write(owner, object + 8, 4, args[1].as.u64, error);
    case M_LOCALE_INIT:
        if ((args[0].as.u32 & 255) && !incref(owner, msvc->global_locale, error)) return false;
        result(out, type, msvc->global_locale); return true;
    case M_GLOBAL: result(out, type, msvc->global_locale); return true;
    case M_LOCK: return locale_lock(owner, object, args[1].as.i32, false, error);
    case M_UNLOCK: return locale_lock(owner, object, 0, true, error);
    case M_LOCINFO_CTOR: {
        uint16_t *name = NULL; size_t length;
        if (!b) return guest_fail(error, QA_ERROR_UNSUPPORTED, 0, "MSVC null locale is not implemented");
        if (!windows_string(owner, b, false, &name, &length, error)) return false;
        bool supported = !length || (length == 1 && name[0] == 'C'); free(name);
        if (!supported) return guest_fail(error, QA_ERROR_UNSUPPORTED, b, "non-C MSVC locale is not implemented");
        if (!windows_zero(owner, object, 104, error) || !locale_lock(owner, object, 0, false, error)) return false;
        const uint8_t narrow[] = {'C',0}, wide[] = {'C',0,0,0};
        for (size_t i = 0; i < 2; ++i) {
            uint64_t address; qa_bytes bytes = i ? (qa_bytes){narrow,2} : (qa_bytes){wide,4};
            if (!windows_allocate(owner, bytes.size, 0, &address, error) || !address ||
                !qa_native_guest_write(owner->guest, address, bytes, error) || !windows_write(owner, object + (i ? 88 : 72), 8, address, error)) return false;
        }
        return true;
    }
    case M_LOCINFO_DTOR:
        for (size_t offset = 8; offset <= 88; offset += 16) {
            if (!windows_read(owner, object + offset, 8, &value, error) || !windows_free(owner, value, 0, error) || !windows_write(owner, object + offset, 8, 0, error)) return false;
        }
        return locale_lock(owner, object, 0, true, error);
    case M_TRUE: result(out, type, msvc->true_text); return true;
    case M_FALSE: result(out, type, msvc->false_text); return true;
    case M_LCONV: {
        uint64_t target;
        return guest_windows_resolve(owner, "ucrtbase.dll", "localeconv", &target, error) && target &&
            windows_invoke(owner, target, NULL, 0, QA_NATIVE_ADDRESS, NULL, false, out, error);
    }
    case M_CVTVEC:
        result(out, type, b); return windows_zero(owner, b, 44, error) && windows_write(owner, b + 4, 4, 1, error) && windows_write(owner, b + 8, 4, 1, error);
    case M_IOS_DTOR: case M_IOS_DELETE:
        return ios_destroy(owner, object, error) && (operation != M_IOS_DELETE || !(args[1].as.u32 & 1) || windows_free(owner, object, 0, error));
    case M_IOS_CTOR: return ios_construct(owner, object, error);
    case M_RDBUF: if (!windows_read(owner, object + 72, 8, &value, error)) return false; result(out, type, value); return true;
    case M_SETSTATE: return setstate(owner, object, (uint32_t)args[1].as.i32, error);
    case M_GOOD: if (!windows_read(owner, object + 16, 4, &value, error)) return false; result(out, type, value == 0); return true;
    case M_OSTREAM_CTOR: case M_IOSTREAM_CTOR: {
        bool output = operation == M_OSTREAM_CTOR;
        if (output && args[2].as.u32) return guest_fail(error, QA_ERROR_UNSUPPORTED, object, "MSVC standard stream registration is not implemented");
        int32_t virtual_base = args[output ? 3 : 2].as.i32;
        if (virtual_base) {
            if (!windows_write(owner, object, 8, output ? msvc->ostream_vbase : msvc->iostream_vbase, error) ||
                (!output && !windows_write(owner, object + 16, 8, msvc->iostream_vbase + 8, error)) || !ios_construct(owner, object + (output ? 16 : 32), error)) return false;
        }
        if (!output && !windows_write(owner, object + 8, 8, 0, error)) return false;
        uint64_t base;
        return virtual_ios(owner, object, &base, error) && windows_write(owner, base, 8, output ? msvc->ostream_vtable : msvc->iostream_vtable, error) &&
            windows_write(owner, base - 4, 4, base - object - (output ? 16 : 32), error) && ios_initialize(owner, base, b, error);
    }
    case M_OSTREAM_DTOR: case M_IOSTREAM_DTOR: {
        bool output = operation == M_OSTREAM_DTOR; uint64_t original = object - (output ? 16 : 32), base;
        return virtual_ios(owner, original, &base, error) && windows_write(owner, base, 8, output ? msvc->ostream_vtable : msvc->iostream_vtable, error) &&
            windows_write(owner, base - 4, 4, base - original - (output ? 16 : 32), error);
    }
    case M_OSTREAM_DELETE: return guest_fail(error, QA_ERROR_UNSUPPORTED, object, "standalone MSVC basic_ostream deleting destructor is not implemented");
    case M_BUFFER_DTOR: case M_BUFFER_DELETE:
        return buffer_destroy(owner, object, error) && (operation != M_BUFFER_DELETE || !(args[1].as.u32 & 1) || windows_free(owner, object, 0, error));
    case M_BUFFER_LOCK: case M_IMBUE: case M_SYNC: case M_SHOWMANY: case M_UNCAUGHT: return true;
    case M_BUFFER_OVERFLOW: case M_BUFFER_UNDERFLOW: result(out, type, UINT32_MAX); return true;
    case M_SETBUF: return true;
    case M_EBACK: case M_PBASE: case M_GPTR: case M_PPTR:
        if (!buffer_field(owner, object, operation == M_EBACK ? 24 : operation == M_PBASE ? 32 : operation == M_GPTR ? 56 : 64, &value, error)) return false;
        result(out, type, value); return true;
    case M_EGPTR: case M_EPPTR: {
        bool input = operation == M_EGPTR; uint64_t slot; int32_t available_count;
        if (!buffer_field(owner, object, input ? 56 : 64, &value, error)) return false;
        if (value) { if (!reference(owner, object + (input ? 80 : 88), &slot, error) || !signed32(owner, slot, &available_count, error)) return false; value += (uint64_t)(int64_t)available_count; }
        result(out, type, value); return true;
    }
    case M_UFLOW: {
        qa_native_value returned;
        if (!virtual_call(owner, object, 6, NULL, 0, QA_NATIVE_I32, &returned, error)) return false;
        if (returned.as.i32 == -1) { result(out, type, UINT32_MAX); return true; }
        if (!bump(owner, object, 1, true, &value, error) || !windows_read(owner, value, 1, &value, error)) return false;
        result(out, type, value); return true;
    }
    case M_PUTC: { int32_t written; if (!buffer_put(owner, object, args[1].as.i32, &written, error)) return false; result(out, type, (uint32_t)written); return true; }
    case M_GETN: case M_PUTN: return transfer(owner, object, b, args[2].as.i64, operation == M_GETN, out, error);
    case M_SPUTN: return virtual_call(owner, object, 9, args + 1, 2, QA_NATIVE_I64, out, error);
    case M_SEEKOFF: case M_SEEKPOS:
        result(out, type, b); return windows_zero(owner, b, 24, error) && windows_write(owner, b + 8, 8, UINT64_MAX, error);
    case M_BUFFER_CTOR: {
        if (!windows_zero(owner, object, 104, error) || !windows_write(owner, object, 8, msvc->buffer_vtable, error)) return false;
        const uint32_t slots[] = {24,32,56,64,80,88}, targets[] = {8,16,40,48,72,76};
        for (size_t i = 0; i < 6; ++i) if (!windows_write(owner, object + slots[i], 8, object + targets[i], error)) return false;
        return locale_create(owner, &value, error) && windows_write(owner, object + 96, 8, value, error);
    }
    case M_FLUSH: return flush(owner, object, NULL, error);
    case M_SUFFIX: return suffix(owner, object, error);
    case M_TELLP: {
        uint64_t base, state, buffer;
        if (!virtual_ios(owner, object, &base, error) || !windows_read(owner, base + 16, 4, &state, error)) return false;
        if (state & 6) { result(out, type, b); return windows_zero(owner, b, 24, error) && windows_write(owner, b + 8, 8, UINT64_MAX, error); }
        if (!reference(owner, base + 72, &buffer, error)) return false;
        const qa_native_value extra[] = {{.type=QA_NATIVE_ADDRESS,.as.address=b},{.type=QA_NATIVE_I64,.as.i64=0},{.type=QA_NATIVE_I32,.as.i32=1},{.type=QA_NATIVE_I32,.as.i32=2}};
        return virtual_call(owner, buffer, 10, extra, 4, type, out, error);
    }
    case M_INTEGER32: return insert_number(owner, object, (uint32_t)args[1].as.i32, 32, error);
    case M_INTEGER64: return insert_number(owner, object, (uint64_t)args[1].as.i64, 64, error);
    case M_INSERT_BUFFER: return insert_buffer(owner, object, b, error);
    default: return guest_fail(error, QA_ERROR_ARGUMENT, operation, "invalid MSVC descriptor");
    }
}

static bool table_valid(guest_windows *owner, uint64_t address, const char *const *names,
    size_t count, qa_error *error)
{
    if (!windows_validate_storage(owner,address,count*8,0x57494e,error) ||
        !guest_range(owner->guest,address,count*8,QA_NATIVE_GUEST_READ,error)) return false;
    for (size_t i = 0; i < count; ++i) {
        uint64_t expected = 0, actual;
        for (size_t j = 0; j < owner->service_count; ++j)
            if (owner->services[j]->family == 3 && !strcmp(owner->services[j]->name,names[i])) expected = owner->services[j]->function.address;
        if (!expected || !windows_read(owner,address+i*8,8,&actual,error) || actual != expected)
            return guest_fail(error,QA_ERROR_FORMAT,address+i*8,"MSVC vtable differs from its genuine physical method identities");
    }
    return true;
}

bool windows_msvc_validate(guest_windows *owner, qa_error *error)
{
    guest_windows_msvc *msvc = owner->msvc;
    if (!windows_validate_storage(owner,msvc->locale_id_count,4,0x57494e,error) ||
        !windows_validate_storage(owner,msvc->numpunct_id,owner->target.pointer_bytes,0x57494e,error)) return false;
    guest_runtime_import_key key = {.library="msvcp140.dll",.kind=GUEST_RUNTIME_SYMBOL_NAME,
        .name="?_Id_cnt@id@locale@std@@0HA"};
    guest_runtime_import_view data;
    if (!guest_runtime_imports_find(owner->imports,&key,&data,error)) return false;
    if (data.kind != GUEST_RUNTIME_IMPORT_DATA || data.address != msvc->locale_id_count || data.bytes != 4)
        return guest_fail(error,QA_ERROR_FORMAT,data.address,"MSVC shared locale counter export lost its actual owned address");
    key.name = "?id@?$numpunct@D@std@@2V0locale@2@A";
    if (!guest_runtime_imports_find(owner->imports,&key,&data,error)) return false;
    if (data.kind != GUEST_RUNTIME_IMPORT_DATA || data.address != msvc->numpunct_id || data.bytes != owner->target.pointer_bytes)
        return guest_fail(error,QA_ERROR_FORMAT,data.address,"MSVC numpunct ID export lost its actual owned address");
    if (owner->target.pointer_bytes != 8) return (!msvc->locks && !msvc->facet_vtable && !msvc->global_locale &&
        !msvc->true_text && !msvc->false_text && !msvc->ios_vtable && !msvc->ostream_vtable && !msvc->iostream_vtable &&
        !msvc->buffer_vtable && !msvc->ostream_vbase && !msvc->iostream_vbase) ||
        guest_fail(error,QA_ERROR_FORMAT,0,"i386 Windows continuation has invented x64 MSVC owner state");
    const char *facet[] = {"runtime:facet-delete","?_Incref@facet@locale@std@@UEAAXXZ","?_Decref@facet@locale@std@@UEAAPEAV_Facet_base@3@XZ"};
    const char *ios[] = {"runtime:basic-ios-delete"}, *output[] = {"runtime:ostream-delete"};
    const char *buffer[] = {"runtime:streambuf-delete","?_Lock@" BUFFER "UEAAXXZ","?_Unlock@" BUFFER "UEAAXXZ",
        "runtime:streambuf-overflow","runtime:streambuf-pbackfail","?showmanyc@" BUFFER "MEAA_JXZ","runtime:streambuf-underflow",
        "?uflow@" BUFFER "MEAAHXZ","?xsgetn@" BUFFER "MEAA_JPEAD_J@Z","?xsputn@" BUFFER "MEAA_JPEBD_J@Z",
        "runtime:streambuf-seekoff","runtime:streambuf-seekpos","?setbuf@" BUFFER "MEAAPEAV12@PEAD_J@Z","?sync@" BUFFER "MEAAHXZ","?imbue@" BUFFER "MEAAXAEBVlocale@2@@Z"};
    if (!windows_validate_storage(owner,msvc->locks,64,0x57494e,error) ||
        !windows_validate_storage(owner,msvc->global_locale,56,0x57494e,error) ||
        !windows_validate_storage(owner,msvc->true_text,5,0x57494e,error) ||
        !windows_validate_storage(owner,msvc->false_text,6,0x57494e,error) ||
        !windows_validate_storage(owner,msvc->ostream_vbase,8,0x57494e,error) ||
        !windows_validate_storage(owner,msvc->iostream_vbase,16,0x57494e,error) ||
        !table_valid(owner,msvc->facet_vtable,facet,3,error) || !table_valid(owner,msvc->ios_vtable,ios,1,error) ||
        !table_valid(owner,msvc->ostream_vtable,output,1,error) || !table_valid(owner,msvc->iostream_vtable,output,1,error) ||
        !table_valid(owner,msvc->buffer_vtable,buffer,15,error)) return false;
    uint64_t first, second, third;
    if (!windows_read(owner,msvc->ostream_vbase+4,4,&first,error) || !windows_read(owner,msvc->iostream_vbase+4,4,&second,error) ||
        !windows_read(owner,msvc->iostream_vbase+12,4,&third,error) || first != 16 || second != 32 || third != 16)
        return guest_fail(error,QA_ERROR_FORMAT,0,"MSVC virtual-base table identity differs");
    return true;
}
