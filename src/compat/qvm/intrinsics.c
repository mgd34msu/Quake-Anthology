#include "internal.h"
#include "qa/math.h"

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
static bool argument(const qa_qvm_call *call, size_t index, int32_t *out, qa_error *error)
{
    return qa_qvm_call_argument(call,index,out,error);
}
static bool span(qa_qvm *vm, int32_t pointer, size_t length, uint32_t *out, qa_error *error)
{
    qa_bytes bytes;
    if (!qa_qvm_span(vm,pointer,0,length,&bytes,error)) return false;
    *out = (uint32_t)(bytes.data - vm->data);
    return true;
}
static bool write_float(qa_qvm *vm, uint32_t offset, float value, qa_error *error)
{
    uint8_t bytes[4];
    qa_store_u32le(bytes,(uint32_t)float_word(value));
    return qa_qvm_write(vm,offset,(qa_bytes){bytes,4},error);
}
static qa_vec3 vector(const qa_qvm *vm, uint32_t offset)
{
    return qa_v3(qa_load_f32le(vm->data + offset),qa_load_f32le(vm->data + offset + 4),qa_load_f32le(vm->data + offset + 8));
}
static bool write_vector(qa_qvm *vm, uint32_t offset, qa_vec3 value, qa_error *error)
{
    return write_float(vm,offset,value.x,error) && write_float(vm,offset + 4,value.y,error) && write_float(vm,offset + 8,value.z,error);
}
static bool no_overlap(uint32_t a, size_t na, uint32_t b, size_t nb, qa_error *error)
{
    return na == 0 || nb == 0 || (uint64_t)a + na <= b || (uint64_t)b + nb <= a
        || qa_qvm_error(error,QA_ERROR_ARGUMENT,a,"overlapping QVM memcpy/strncpy ranges");
}

static bool memory_call(qa_qvm *vm, const qa_qvm_call *call, int32_t trap, int32_t *result, qa_error *error)
{
    int32_t destination_word, source_word, count;
    uint32_t destination, source;
    if (!argument(call,0,&destination_word,error) || !argument(call,1,&source_word,error) || !argument(call,2,&count,error)) return false;
    if (count < 0) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"negative QVM memory operation length");
    if (!span(vm,destination_word,(size_t)count,&destination,error)) return false;
    *result = 0;
    if (trap == 100) return qa_qvm_fill(vm,destination,(size_t)count,(uint8_t)source_word,error);
    if (trap == 101) {
        if (!span(vm,source_word,(size_t)count,&source,error) || !no_overlap(destination,(size_t)count,source,(size_t)count,error)) return false;
        return qa_qvm_copy(vm,destination,source,(size_t)count,error);
    }
    if (!span(vm,source_word,0,&source,error)) return false;
    size_t available = vm->data_size - source;
    size_t inspected = available < (size_t)count ? available : (size_t)count;
    const uint8_t *end = memchr(vm->data + source,0,inspected);
    size_t copied = end == NULL ? (size_t)count : (size_t)(end - vm->data - source);
    size_t consumed = copied + (end == NULL ? 0u : 1u);
    if (!qa_qvm_raw_range(vm,source,consumed,error) || !no_overlap(destination,(size_t)count,source,consumed,error)) return false;
    if (!qa_qvm_copy(vm,destination,source,copied,error) || !qa_qvm_fill(vm,destination + (uint32_t)copied,(size_t)count - copied,0,error)) return false;
    *result = destination_word;
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

static bool vector_call(qa_qvm *vm, const qa_qvm_call *call, int32_t trap, int32_t *result, qa_error *error)
{
    int32_t words[4] = {0};
    uint32_t offsets[4] = {0};
    size_t count = trap == 107 ? 3 : trap == 108 ? 4 : 2;
    for (size_t i = 0; i < count; ++i) if (!argument(call,i,&words[i],error)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (trap == 108 && i > 0 && words[i] == 0) continue;
        if (!span(vm,words[i],trap == 107 ? 36u : 12u,&offsets[i],error)) return false;
    }
    *result = 0;
    if (trap == 107) {
        for (uint32_t row = 0; row < 3; ++row) {
            for (uint32_t column = 0; column < 3; ++column) {
                qa_vec3 a = vector(vm,offsets[0] + row * 12);
                uint32_t at = offsets[1] + column * 4;
                qa_vec3 b = qa_v3(qa_load_f32le(vm->data + at),qa_load_f32le(vm->data + at + 12),qa_load_f32le(vm->data + at + 24));
                if (!write_float(vm,offsets[2] + row * 12 + column * 4,qa_vec_dot(a,b),error)) return false;
            }
        }
        return true;
    }
    if (trap == 108) {
        qa_vec3 values[3];
        angle_vectors(vector(vm,offsets[0]),&values[0],&values[1],&values[2]);
        for (size_t i = 1; i < 4; ++i)
            if (words[i] != 0 && !write_vector(vm,offsets[i],values[i - 1],error)) return false;
        return true;
    }
    qa_vec3 source = vector(vm,offsets[1]);
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
    return write_vector(vm,offsets[0],projected,error);
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

bool qa_qvm_dispatch(qa_qvm *vm, const qa_qvm_call *call, int32_t trap, int32_t *result, qa_error *error)
{
    int32_t canonical;
    bool engine;
    if (!qa_qvm_classify_syscall(vm->options.role,vm->options.abi,trap,&canonical,&engine,error)) return false;
    if (trap >= 100 && trap <= 102) return memory_call(vm,call,trap,result,error);
    qa_qvm_role math_role = vm->options.abi == QA_QVM_Q3_116N && vm->options.role == QA_QVM_UI ? QA_QVM_GAME : vm->options.role;
    if ((trap >= 103 && trap <= 106) || ((trap == 107 || trap == 108) && math_role != QA_QVM_GAME)
        || (trap == 110 && math_role == QA_QVM_GAME) || (trap == 111 && math_role != QA_QVM_UI)) {
        int32_t a, b = 0;
        if (!argument(call,0,&a,error) || (trap == 105 && !argument(call,1,&b,error))) return false;
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
        *result = float_word(value);
        return true;
    }
    if (vm->options.role == QA_QVM_GAME && trap >= 107 && trap <= 109) return vector_call(vm,call,trap,result,error);
    if ((vm->options.role == QA_QVM_GAME && trap == 42) || (vm->options.role == QA_QVM_CGAME && trap == 71)) {
        int32_t pointer;
        uint32_t offset;
        if (!argument(call,0,&pointer,error) || !span(vm,pointer,12,&offset,error)) return false;
        for (uint32_t i = 0; i < 12; i += 4)
            if (!write_float(vm,offset + i,snap(qa_load_f32le(vm->data + offset + i)),error)) return false;
        *result = 0;
        return true;
    }
    (void)canonical; (void)engine;
    if (vm->options.syscall != NULL) return vm->options.syscall(vm->options.context,call,trap,result,error);
    return qa_qvm_error(error,QA_ERROR_UNSUPPORTED,(size_t)(uint32_t)trap,"QVM engine syscall has no bound host");
}
