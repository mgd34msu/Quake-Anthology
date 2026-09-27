#include "internal.h"
#include "qa/math.h"
#include "qa/q3_abi.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static int32_t float_word(float value)
{
    int32_t word;
    memcpy(&word,&value,sizeof(word));
    return word;
}
static float word_float(int32_t value)
{
    float word;
    memcpy(&word,&value,sizeof(word));
    return word;
}
static bool write_float(const qa_q3_abi_memory *memory, uint64_t offset, float value, qa_error *error)
{
    uint8_t bytes[4];
    qa_store_u32le(bytes,(uint32_t)float_word(value));
    return memory->write(memory->context,offset,(qa_bytes){bytes,4},error);
}
static bool vector(const qa_q3_abi_memory *memory, uint64_t offset, qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!memory->read(memory->context,offset,bytes,sizeof(bytes),error)) return false;
    *out = qa_v3(qa_load_f32le(bytes),qa_load_f32le(bytes + 4),qa_load_f32le(bytes + 8));
    return true;
}
static bool write_vector(const qa_q3_abi_memory *memory, uint64_t offset, qa_vec3 value, qa_error *error)
{
    return write_float(memory,offset,value.x,error) && write_float(memory,offset + 4,value.y,error) && write_float(memory,offset + 8,value.z,error);
}
static bool no_overlap(uint64_t a, size_t na, uint64_t b, size_t nb, qa_error *error)
{
    return na == 0 || nb == 0 || (a <= b ? na <= b - a : nb <= a - b)
        || qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"overlapping Q3 memcpy/strncpy ranges");
}

static bool memory_call(const qa_q3_abi_memory *memory, const uint64_t *words,
                          int32_t trap, uint64_t *result, qa_error *error)
{
    int32_t count; uint32_t bits = (uint32_t)words[2];
    memcpy(&count,&bits,sizeof(count));
    uint64_t destination, source;
    if (count < 0) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"negative QVM memory operation length");
    if (!memory->span(memory->context,words[0],(size_t)count,&destination,error)) return false;
    *result = 0;
    if (trap == 100) return memory->fill(memory->context,destination,(size_t)count,(uint8_t)words[1],error);
    if (trap == 101) {
        if (!memory->span(memory->context,words[1],(size_t)count,&source,error) || !no_overlap(destination,(size_t)count,source,(size_t)count,error)) return false;
        return memory->copy(memory->context,destination,source,(size_t)count,error);
    }
    if (!memory->span(memory->context,words[1],0,&source,error)) return false;
    size_t copied; bool terminated;
    if (!memory->string_length(memory->context,source,(size_t)count,&copied,&terminated,error)) return false;
    size_t consumed = copied + (terminated ? 1u : 0u);
    if (!no_overlap(destination,(size_t)count,source,consumed,error)) return false;
    if (!memory->copy(memory->context,destination,source,copied,error) ||
        !memory->fill(memory->context,destination + copied,(size_t)count - copied,0,error)) return false;
    *result = words[0];
    return true;
}

static void angle_vectors(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up)
{
    const double to_radians = 0.017453292519943295769236907684886;
    float yaw = (float)(angles.y * to_radians), pitch = (float)(angles.x * to_radians), roll = (float)(angles.z * to_radians);
    float sy = (float)sin(yaw), cy = (float)cos(yaw), sp = (float)sin(pitch), cp = (float)cos(pitch), sr = (float)sin(roll), cr = (float)cos(roll);
    *forward = qa_v3(cp * cy,cp * sy,-sp);
    *right = qa_v3((-sr * sp) * cy + (-cr * -sy),(-sr * sp) * sy + (-cr * cy),-sr * cp);
    *up = qa_v3((cr * sp) * cy + (-sr * -sy),(cr * sp) * sy + (-sr * cy),cr * cp);
}

static bool vector_call(const qa_q3_abi_memory *memory, const uint64_t *words,
                          int32_t trap, uint64_t *result, qa_error *error)
{
    uint64_t offsets[4] = {0};
    size_t count = trap == 107 ? 3 : trap == 108 ? 4 : 2;
    for (size_t i = 0; i < count; ++i) {
        if (trap == 108 && i > 0 && words[i] == 0) continue;
        if (!memory->span(memory->context,words[i],trap == 107 ? 36u : 12u,&offsets[i],error)) return false;
    }
    *result = 0;
    if (trap == 107) {
        for (uint32_t row = 0; row < 3; ++row) {
            for (uint32_t column = 0; column < 3; ++column) {
                qa_vec3 a; uint8_t values[3][4];
                uint64_t at = offsets[1] + column * 4;
                if (!vector(memory,offsets[0] + row * 12,&a,error)) return false;
                for (size_t k = 0; k < 3; ++k)
                    if (!memory->read(memory->context,at + k * 12,values[k],4,error)) return false;
                qa_vec3 b = qa_v3(qa_load_f32le(values[0]),qa_load_f32le(values[1]),qa_load_f32le(values[2]));
                if (!write_float(memory,offsets[2] + row * 12 + column * 4,qa_vec_dot(a,b),error)) return false;
            }
        }
        return true;
    }
    if (trap == 108) {
        qa_vec3 values[3], angles;
        if (!vector(memory,offsets[0],&angles,error)) return false;
        angle_vectors(angles,&values[0],&values[1],&values[2]);
        for (size_t i = 1; i < 4; ++i)
            if (words[i] != 0 && !write_vector(memory,offsets[i],values[i - 1],error)) return false;
        return true;
    }
    qa_vec3 source;
    if (!vector(memory,offsets[1],&source,error)) return false;
    float denominator = qa_vec_dot(source,source);
    if (denominator == 0) return qa_qvm_error(error,QA_ERROR_ARGUMENT,offsets[1],"QVM perpendicular vector has a zero projection denominator");
    qa_vec3 axis = qa_v3(1,0,0);
    float minimum = 1;
    if (fabsf(source.x) < minimum) { minimum = fabsf(source.x); axis = qa_v3(1,0,0); }
    if (fabsf(source.y) < minimum) { minimum = fabsf(source.y); axis = qa_v3(0,1,0); }
    if (fabsf(source.z) < minimum) axis = qa_v3(0,0,1);
    float inverse = 1.0f / denominator, distance = qa_vec_dot(source,axis) * inverse;
    qa_vec3 projected = qa_vec_sub(axis,qa_vec_scale(qa_vec_scale(source,inverse),distance));
    float length = sqrtf(qa_vec_dot(projected,projected));
    if (length != 0) projected = qa_vec_scale(projected,1.0f / length);
    return write_vector(memory,offsets[0],projected,error);
}

static float snap(float value)
{
    double lower = floor((double)value), fraction = (double)value - lower;
    double integer = fraction < 0.5 ? lower : fraction > 0.5 ? lower + 1
        : fmod(lower,2.0) == 0 ? lower : lower + 1;
    if (!isfinite(integer) || integer < INT32_MIN || integer > INT32_MAX) return (float)INT32_MIN;
    return integer == 0 ? 0.0f : (float)integer;
}

/* Convert a source ABI trap to its modern engine service. Intrinsics keep
 * their source numbers and are selected before this mapping. */
bool qa_qvm_classify_syscall(qa_qvm_role role, qa_qvm_abi abi, int32_t trap, int32_t *canonical, bool *engine, qa_error *error)
{
    if (canonical == NULL || engine == NULL || (unsigned)role > QA_QVM_UI || (unsigned)abi > QA_QVM_Q3_116N)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM syscall classification");
    *canonical = trap;
    if (abi == QA_QVM_Q3_116N) {
        if (role == QA_QVM_GAME) {
            bool bot = (trap >= 200 && trap <= 211) || (trap >= 303 && trap <= 318)
                || (trap >= 400 && trap <= 401) || (trap >= 406 && trap <= 426) || (trap >= 500 && trap <= 572);
            if (!((trap >= 0 && trap <= 40) || (trap >= 100 && trap <= 106) || trap == 110 || trap == 111 || bot || (trap >= 402 && trap <= 405)))
                return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,(size_t)(uint32_t)trap,"unsupported legacy QVM game syscall");
            static const int32_t elementary[21] = {404,402,416,405,406,407,408,417,418,409,410,411,412,413,414,415,419,420,421,422,423};
            if (trap >= 406 && trap <= 426) *canonical = elementary[trap - 406];
            *engine = (trap >= 0 && trap <= 40) || bot;
            return true;
        }
        if (!((trap >= 0 && trap <= 58) || (trap >= 100 && trap <= 106)
            || (role == QA_QVM_CGAME ? trap == 107 || trap == 108 : trap == 110 || trap == 111)))
            return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,(size_t)(uint32_t)trap,"unsupported legacy QVM client syscall");
        if (role == QA_QVM_UI) {
            if (trap >= 46 && trap <= 49) { *engine = false; return true; }
            if (trap >= 50 && trap <= 58) *canonical = trap - 4;
        }
    }
    if (role == QA_QVM_GAME)
        *engine = (trap >= 0 && trap <= 45) || (trap >= 200 && trap <= 211) || (trap >= 300 && trap <= 318)
            || (trap >= 400 && trap <= 423) || (trap >= 500 && trap <= 581);
    else if (role == QA_QVM_CGAME) *engine = (trap >= 0 && trap <= 89) || (trap >= 100 && trap <= 111);
    else *engine = (*canonical >= 0 && *canonical <= 87) || (*canonical >= 100 && *canonical <= 108);
    return true;
}

bool qa_q3_abi_intrinsic_signature(qa_qvm_role role, qa_qvm_abi abi, int32_t trap,
                                    size_t *count, uint32_t *pointers, bool *address_result)
{
    if ((unsigned)role > QA_QVM_UI || (unsigned)abi > QA_QVM_Q3_116N ||
        !count || !pointers || !address_result) return false;
    *count = 0; *pointers = 0; *address_result = false;
    if (trap >= 100 && trap <= 102) {
        *count = 3; *pointers = trap == 100 ? 1u : 3u; *address_result = trap == 102;
        return true;
    }
    qa_qvm_role math_role = abi == QA_QVM_Q3_116N && role == QA_QVM_UI ? QA_QVM_GAME : role;
    if ((trap >= 103 && trap <= 106) || ((trap == 107 || trap == 108) && math_role != QA_QVM_GAME)
        || (trap == 110 && math_role == QA_QVM_GAME) || (trap == 111 && math_role != QA_QVM_UI)) {
        *count = trap == 105 ? 2u : 1u; return true;
    }
    if (role == QA_QVM_GAME && trap >= 107 && trap <= 109) {
        *count = trap == 107 ? 3u : trap == 108 ? 4u : 2u;
        *pointers = (1u << *count) - 1u; return true;
    }
    if ((role == QA_QVM_GAME && trap == 42) || (role == QA_QVM_CGAME && trap == 71)) {
        *count = 1; *pointers = 1; return true;
    }
    return false;
}

bool qa_q3_abi_intrinsic(qa_qvm_role role, qa_qvm_abi abi, int32_t trap,
                          const uint64_t *arguments, size_t argument_count,
                          const qa_q3_abi_memory *memory, uint64_t *result, qa_error *error)
{
    size_t required; uint32_t pointers; bool address_result;
    int32_t canonical; bool engine;
    if (!qa_qvm_classify_syscall(role,abi,trap,&canonical,&engine,error)) return false;
    if (!qa_q3_abi_intrinsic_signature(role,abi,trap,&required,&pointers,&address_result) ||
        !arguments || argument_count != required || !result ||
        (pointers && (!memory || !memory->span || !memory->read || !memory->write ||
                      !memory->copy || !memory->fill || !memory->string_length)))
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid Q3 intrinsic call");
    (void)canonical; (void)engine; (void)address_result;
    if (trap >= 100 && trap <= 102) return memory_call(memory,arguments,trap,result,error);
    qa_qvm_role math_role = abi == QA_QVM_Q3_116N && role == QA_QVM_UI ? QA_QVM_GAME : role;
    if (!pointers) {
        uint32_t bits = (uint32_t)arguments[0]; int32_t a, b = 0;
        memcpy(&a,&bits,sizeof(a));
        if (required > 1) { bits = (uint32_t)arguments[1]; memcpy(&b,&bits,sizeof(b)); }
        float x = word_float(a), y = word_float(b), value = 0;
        switch (trap) {
        case 103: value = (float)sin(x); break;
        case 104: value = (float)cos(x); break;
        case 105: value = (float)atan2(x,y); break;
        case 106: value = (float)sqrt(x); break;
        case 107: case 110: value = floorf(x); break;
        case 108: value = ceilf(x); break;
        case 111:
            value = math_role == QA_QVM_GAME ? ceilf(x) : (float)acos(x);
            if (math_role == QA_QVM_CGAME && ((double)value > 3.14159265358979323846 || (double)value < -3.14159265358979323846)) value = 3.14159265358979323846f;
            break;
        default: break;
        }
        *result = (uint32_t)float_word(value);
        return true;
    }
    if (role == QA_QVM_GAME && trap >= 107 && trap <= 109)
        return vector_call(memory,arguments,trap,result,error);
    uint64_t offset;
    if (!memory->span(memory->context,arguments[0],12,&offset,error)) return false;
    for (uint32_t i = 0; i < 12; i += 4) {
        uint8_t bytes[4];
        if (!memory->read(memory->context,offset + i,bytes,sizeof(bytes),error) ||
            !write_float(memory,offset + i,snap(qa_load_f32le(bytes)),error)) return false;
    }
    *result = 0;
    return true;
}

static bool vm_span(void *context, uint64_t raw, size_t size, uint64_t *address, qa_error *error)
{
    qa_qvm *vm = context; qa_bytes bytes;
    if (!qa_qvm_span(vm,(int32_t)(uint32_t)raw,0,size,&bytes,error)) return false;
    *address = (uint64_t)(bytes.data - vm->data); return true;
}
static bool vm_read(void *context, uint64_t address, void *out, size_t size, qa_error *error)
{
    qa_qvm *vm = context;
    if (address > UINT32_MAX || !qa_qvm_raw_range(vm,(uint32_t)address,size,error)) return false;
    if (size) memcpy(out,vm->data + (size_t)address,size);
    return true;
}
static bool vm_write(void *context, uint64_t address, qa_bytes bytes, qa_error *error)
{
    return address <= UINT32_MAX && qa_qvm_write(context,(uint32_t)address,bytes,error);
}
static bool vm_copy(void *context, uint64_t destination, uint64_t source, size_t size, qa_error *error)
{
    return destination <= UINT32_MAX && source <= UINT32_MAX &&
           qa_qvm_copy(context,(uint32_t)destination,(uint32_t)source,size,error);
}
static bool vm_fill(void *context, uint64_t address, size_t size, uint8_t value, qa_error *error)
{
    return address <= UINT32_MAX && qa_qvm_fill(context,(uint32_t)address,size,value,error);
}
static bool vm_string_length(void *context, uint64_t address, size_t limit,
                               size_t *length, bool *terminated, qa_error *error)
{
    qa_qvm *vm = context;
    if (address > UINT32_MAX || !qa_qvm_raw_range(vm,(uint32_t)address,0,error)) return false;
    size_t available = vm->data_size - (size_t)address;
    size_t inspected = available < limit ? available : limit;
    const uint8_t *start = vm->data + (size_t)address, *end = memchr(start,0,inspected);
    *terminated = end != NULL;
    *length = end ? (size_t)(end - start) : limit;
    return qa_qvm_raw_range(vm,(uint32_t)address,*length + (*terminated ? 1u : 0u),error);
}

bool qa_qvm_dispatch(qa_qvm *vm, const qa_qvm_call *call, int32_t trap, int32_t *result, qa_error *error)
{
    size_t count; uint32_t pointers; bool address_result;
    if (qa_q3_abi_intrinsic_signature(vm->options.role,vm->options.abi,trap,&count,&pointers,&address_result)) {
        uint64_t arguments[4];
        for (size_t i = 0; i < count; ++i) {
            int32_t word;
            if (!qa_qvm_call_argument(call,i,&word,error)) return false;
            arguments[i] = (uint32_t)word;
        }
        qa_q3_abi_memory memory = {vm,vm_span,vm_read,vm_write,vm_copy,vm_fill,vm_string_length};
        uint64_t value;
        if (!qa_q3_abi_intrinsic(vm->options.role,vm->options.abi,trap,arguments,count,&memory,&value,error)) return false;
        uint32_t word = (uint32_t)value; memcpy(result,&word,sizeof(word)); return true;
    }
    int32_t canonical; bool engine;
    if (!qa_qvm_classify_syscall(vm->options.role,vm->options.abi,trap,&canonical,&engine,error)) return false;
    (void)canonical; (void)engine;
    if (vm->options.syscall != NULL) return vm->options.syscall(vm->options.context,call,trap,result,error);
    return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,(size_t)(uint32_t)trap,"QVM engine syscall has no bound host");
}
