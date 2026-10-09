#include "sysv_libc_private.h"
#include "qa/filesystem.h"

enum stdio_operation { ST_FLUSH = 1, ST_PUT, ST_GET, ST_UNGET, ST_WRITE, ST_READ, ST_WIDE, ST_OVERFLOW,
    ST_OPEN, ST_CLOSE, ST_SEEK, ST_TELL, ST_TMPFILE, ST_REWIND };
typedef struct file_layout { size_t size, descriptor, mode, lock, offset, wide, old; } file_layout;
static file_layout layout(const guest_sysv_runtime *r)
{
    return r->target.pointer_bytes == 8 ? (file_layout){216,112,192,136,144,160,120} :
        (file_layout){148,56,104,72,76,88,64};
}
static uint64_t field(const guest_sysv_runtime *r, uint64_t file, size_t index)
{ return file + index * r->target.pointer_bytes; }
static sysv_file *opened(guest_sysv_runtime *r, uint64_t address)
{
    for (size_t i = 0; i < r->open_file_count; ++i)
        if (r->open_files[i].address == address) return r->open_files + i;
    return NULL;
}
bool guest_sysv_file_in_use(const guest_sysv_runtime *r,uint64_t handle)
{
    if (!r || !handle) return false;
    for (size_t i = 0; i < r->open_file_count; ++i)
        if (r->open_files[i].handle == handle) return true;
    return false;
}
static bool descriptor(guest_sysv_runtime *r, uint64_t file, int32_t *out, qa_error *error)
{
    uint64_t flags, value;
    if (!file) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V FILE requires its actual nonnull pointer");
    if (!sysv_unsigned(r, file, 4, &flags, error) || (flags & UINT32_C(0xffff0000)) != UINT32_C(0xfbad0000))
        return sysv_fail(error, QA_ERROR_ARGUMENT, "invalid actual System V FILE magic");
    if (!sysv_unsigned(r, file + layout(r).descriptor, 4, &value, error)) return false;
    uint32_t bits = (uint32_t)value;
    *out = bits <= INT32_MAX ? (int32_t)bits : -(int32_t)(UINT32_MAX - bits) - 1;
    if (*out >= 0 && *out < 3 && r->files[*out] == file) return true;
    sysv_file *entry = opened(r,file);
    return (entry && !entry->closing && entry->descriptor == bits) ||
        sysv_fail(error,QA_ERROR_ARGUMENT,"System V FILE has no live retained descriptor");
}
static bool orient(guest_sysv_runtime *r, uint64_t file, bool wide, bool *accepted, qa_error *error)
{
    uint64_t mode;
    if (!file) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V FILE orientation requires its actual nonnull pointer");
    if (!sysv_unsigned(r, file + layout(r).mode, 4, &mode, error)) return false;
    uint32_t desired = wide ? 1 : UINT32_MAX;
    *accepted = !mode || (uint32_t)mode == desired;
    return mode || sysv_store(r, file + layout(r).mode, 4, desired, error);
}
static bool file_error(guest_sysv_runtime *r, uint64_t file, int code,
    int32_t *out, qa_error *error)
{
    uint64_t flags; *out = -1;
    return sysv_errno(r, code, error) && sysv_unsigned(r, file, 4, &flags, error) &&
        sysv_store(r, file, 4, flags | 0x20, error);
}
static bool create_file(guest_sysv_runtime *r, size_t fd, uint64_t chain, qa_error *error)
{
    file_layout l = layout(r); size_t p = r->target.pointer_bytes;
    uint64_t lock, wide;
    if (!sysv_allocate(r, l.size + p, true, r->files + fd, error) ||
        !sysv_store(r, r->files[fd], 4, UINT32_C(0xfbad0000) | 0x2080 | (fd ? 4 : 8) | (fd == 2 ? 2 : 0), error) ||
        !sysv_put_pointer(r, field(r, r->files[fd], 13), chain, error) ||
        !sysv_store(r, r->files[fd] + l.descriptor, 4, fd, error) ||
        !sysv_store(r, r->files[fd] + l.offset, 8, UINT64_MAX, error) ||
        !sysv_store(r, r->files[fd] + l.old, p, UINT64_MAX, error) ||
        !sysv_allocate(r, p * 2 + 8, true, &lock, error) ||
        !sysv_put_pointer(r, r->files[fd] + l.lock, lock, error) ||
        !sysv_allocate(r, p == 8 ? 312 : 180, true, &wide, error) ||
        !sysv_put_pointer(r, r->files[fd] + l.wide, wide, error)) return false;
    r->wide_files[fd] = wide; return true;
}
bool sysv_stdio_install(guest_sysv_runtime *r, qa_error *error)
{
    size_t p = r->target.pointer_bytes; file_layout l = layout(r);
    const char *base = p == 4 ? "GLIBC_2.0" : "GLIBC_2.2.5";
    const char *io_version = p == 4 ? "GLIBC_2.1" : base;
    const char *versions[] = {base,NULL}, *wide_versions[] = {io_version,NULL}, *unversioned[] = {NULL};
    const char *names[] = {"stdin","stdout","stderr"};
    for (size_t fd = 0; fd < 3; ++fd) {
        uint64_t slot;
        if (!create_file(r, fd, fd ? r->files[fd - 1] : 0, error) ||
            !sysv_allocate(r, p, true, &slot, error) ||
            !sysv_put_pointer(r, slot, r->files[fd], error) ||
            !sysv_data(r, "libc.so.6", names[fd], base, slot, p, error)) return false;
        char object[32]; size_t n = strlen(names[fd]);
        memcpy(object, "_IO_2_1_", 8); memcpy(object + 8, names[fd], n); object[8+n] = '_'; object[9+n] = 0;
        if (!sysv_data(r, "libc.so.6", object, io_version, r->files[fd], l.size + p, error)) return false;
    }
    const qa_native_value_type pointer = QA_NATIVE_ADDRESS, integer = QA_NATIVE_I32, size = sysv_size_type(r);
    const qa_native_value_type ip[] = {integer,pointer}, pi[] = {pointer,integer}, pssp[] = {pointer,size,size,pointer};
    const qa_native_value_type pp[] = {pointer,pointer}, pli[] = {pointer,sysv_signed_type(r),integer};
#define STDIO_ADD(op,name,types,count,result,vers) \
    sysv_service_add(r,SYSV_STDIO,op,0,0,0,"libc.so.6",name,vers,2,types,count,result,NULL,NULL,error)
    if (!STDIO_ADD(ST_FLUSH,"fflush",&pointer,1,integer,versions) ||
        !STDIO_ADD(ST_PUT,"fputc",ip,2,integer,versions) ||
        !STDIO_ADD(ST_PUT,"putc",ip,2,integer,versions) ||
        !STDIO_ADD(ST_GET,"fgetc",&pointer,1,integer,versions) ||
        !STDIO_ADD(ST_GET,"getc",&pointer,1,integer,versions) ||
        !STDIO_ADD(ST_GET,"__uflow",&pointer,1,integer,versions) ||
        !STDIO_ADD(ST_OVERFLOW,"__overflow",pi,2,integer,versions) ||
        !STDIO_ADD(ST_UNGET,"ungetc",ip,2,integer,versions) ||
        !STDIO_ADD(ST_WRITE,"fwrite",pssp,4,size,versions) ||
        !STDIO_ADD(ST_READ,"fread",pssp,4,size,versions) ||
        !STDIO_ADD(ST_WIDE,"fwide",pi,2,integer,wide_versions) ||
        !STDIO_ADD(ST_OPEN,"fopen",pp,2,pointer,wide_versions) ||
        !STDIO_ADD(ST_OPEN,"fopen64",pp,2,pointer,wide_versions) ||
        !STDIO_ADD(ST_CLOSE,"fclose",&pointer,1,integer,wide_versions) ||
        !STDIO_ADD(ST_SEEK,"fseek",pli,3,integer,versions) ||
        !STDIO_ADD(ST_TELL,"ftell",&pointer,1,sysv_signed_type(r),versions) ||
        !STDIO_ADD(ST_TMPFILE,"tmpfile",NULL,0,pointer,wide_versions) ||
        !STDIO_ADD(ST_TMPFILE,"tmpfile64",NULL,0,pointer,wide_versions) ||
        !STDIO_ADD(ST_REWIND,"rewind",&pointer,1,QA_NATIVE_VOID,versions)) return false;
    if (p == 4) {
        const char *legacy[] = {base};
        if (!sysv_service_add(r,SYSV_STDIO,ST_OPEN,0,0,0,"libc.so.6","fopen",legacy,1,
                pp,2,pointer,NULL,NULL,error) ||
            !sysv_service_add(r,SYSV_STDIO,ST_CLOSE,0,0,0,"libc.so.6","fclose",legacy,1,
                &pointer,1,integer,NULL,NULL,error)) return false;
    }
#undef STDIO_ADD
    const char *operations[] = {"finish","overflow","underflow","uflow","pbackfail","xsputn","xsgetn",
        "seekoff","seekpos","setbuf","sync","doallocate","read","write","seek","close","stat","showmanyc","imbue"};
    uint64_t *tables = r->file_tables;
    r->next_file = 3;
    for (size_t w = 0; w < 2; ++w) {
        if (!sysv_allocate(r, 21*p, true, tables+w, error) ||
            !sysv_data(r,"libc.so.6",w ? "_IO_wfile_jumps" : "_IO_file_jumps",io_version,tables[w],21*p,error)) return false;
        for (size_t i = 0; i < 19; ++i) {
            uint64_t address;
            if (w && i == 10) {
                if (!sysv_pointer(r,tables[0]+(i+2)*p,&address,error)) return false;
            } else {
                char name[64]; const char *prefix = w ? "__guest_IO_wfile_" : "__guest_IO_file_";
                size_t n = strlen(prefix), m = strlen(operations[i]); memcpy(name,prefix,n); memcpy(name+n,operations[i],m+1);
                bool implemented = !w && (i == 1 || i == 10);
                uint32_t operation = i == 1 ? ST_OVERFLOW : ST_FLUSH;
                if (!sysv_service_add(r,implemented ? SYSV_STDIO : SYSV_RUNTIME,operation,0,0,0,
                    "libc.so.6",name,unversioned,1,i == 1 && implemented ? pi : &pointer,
                    i == 1 && implemented ? 2 : 1,implemented ? integer : QA_NATIVE_VOID,
                    implemented ? NULL : w ? "this wide FILE virtual operation is not implemented" :
                    "this FILE virtual operation is not implemented",&address,error)) return false;
            }
            if (!sysv_put_pointer(r,tables[w]+(i+2)*p,address,error)) return false;
        }
    }
    for (size_t fd = 0; fd < 3; ++fd)
        if (!sysv_put_pointer(r,r->files[fd]+l.size,tables[0],error) ||
            !sysv_put_pointer(r,r->wide_files[fd]+(p == 8 ? 304 : 176),tables[1],error)) return false;
    return true;
}
bool sysv_stdio_flush(guest_sysv_runtime *r, uint64_t file, int32_t *out, qa_error *error)
{
    int32_t fd; uint64_t base, next;
    if (!descriptor(r,file,&fd,error) || !sysv_pointer(r,field(r,file,4),&base,error) ||
        !sysv_pointer(r,field(r,file,5),&next,error)) return false;
    *out = 0; if (!fd) return true;
    sysv_file *entry = opened(r,file);
    if (!entry && fd != 1 && fd != 2) return file_error(r,file,9,out,error);
    if (entry && !(entry->mode & GUEST_RUNTIME_FILE_WRITE)) return true;
    guest_sysv_stream stream = entry ? (guest_sysv_stream){0} : r->options.bindings.streams[fd];
    if (base && next && next > base) {
        if (!entry && (!stream.id || !stream.write)) return sysv_fail(error,QA_ERROR_UNSUPPORTED,"System V fflush has no actual standard output capability");
        uint64_t length = next-base;
        if (length > SIZE_MAX || !guest_range(r->guest,base,(size_t)length,QA_NATIVE_GUEST_READ,error)) return false;
        uint8_t *bytes = malloc((size_t)length);
        if (!bytes) return sysv_fail(error,QA_ERROR_MEMORY,"holding actual FILE pending output");
        if (!sysv_read(r,base,bytes,(size_t)length,error)) { free(bytes); return false; }
        size_t written = 0;
        while (written < length) {
            size_t completed = 0, remaining = (size_t)length-written;
            if (entry && entry->append) {
                uint64_t end;
                if (!guest_runtime_resources_size(r->options.bindings.resources,entry->handle,&end,error) ||
                    !sysv_current(r,error) || !guest_runtime_resources_seek(r->options.bindings.resources,entry->handle,end,error)) {
                    bool retained = sysv_write(r,base,bytes+written,(size_t)length-written,error) &&
                        sysv_put_pointer(r,field(r,file,5),base+length-written,error);
                    (void)retained;
                    free(bytes); return false;
                }
            }
            bool callback = entry ? guest_runtime_resources_write(r->options.bindings.resources,entry->handle,
                (qa_bytes){bytes+written,remaining},&completed,error) :
                stream.write(stream.context,(qa_bytes){bytes+written,remaining},&completed,error);
            if (!sysv_current(r,error)) { free(bytes); return false; }
            bool valid = completed <= remaining;
            if (valid) written += completed;
            if (!callback || !valid || !completed) {
                bool ok = sysv_write(r,base,bytes+written,(size_t)length-written,error) &&
                    sysv_put_pointer(r,field(r,file,5),base+length-written,error);
                free(bytes);
                if (!ok) return false;
                if (!callback) return false;
                return file_error(r,file,5,out,error);
            }
        }
        free(bytes);
        if (!sysv_put_pointer(r,field(r,file,5),base,error)) return false;
    }
    if (entry || stream.flush) {
        bool ok = entry ? guest_runtime_resources_flush(r->options.bindings.resources,entry->handle,error) :
            stream.flush(stream.context,error);
        if (!sysv_current(r,error)) return false;
        if (!ok) return false;
    }
    return true;
}
static bool output_buffer(guest_sysv_runtime *r, uint64_t file, qa_error *error)
{
    uint64_t base;
    if (!sysv_pointer(r,field(r,file,7),&base,error)) return false;
    sysv_file *entry = opened(r,file);
    uint64_t output;
    if (!sysv_pointer(r,field(r,file,4),&output,error)) return false;
    if (base && output) return true;
    if (entry) {
        uint64_t next,end;
        guest_runtime_file_view view;
        if (!sysv_pointer(r,field(r,file,1),&next,error) || !sysv_pointer(r,field(r,file,2),&end,error) ||
            !guest_runtime_resources_find(r->options.bindings.resources,entry->handle,&view,error)) return false;
        if (next && end > next && (end-next > view.offset ||
            !guest_runtime_resources_seek(r->options.bindings.resources,entry->handle,view.offset-(end-next),error))) return false;
        for (size_t i = 1; i <= 3; ++i) if (!sysv_put_pointer(r,field(r,file,i),0,error)) return false;
    }
    if (!base && !sysv_allocate(r,8192,true,&base,error)) return false;
    if (entry) entry->buffer = base;
    const size_t starts[] = {4,5,7}, ends[] = {6,8};
    for (size_t i = 0; i < 3; ++i) if (!sysv_put_pointer(r,field(r,file,starts[i]),base,error)) return false;
    for (size_t i = 0; i < 2; ++i) if (!sysv_put_pointer(r,field(r,file,ends[i]),base+8192,error)) return false;
    int32_t fd; uint64_t flags;
    if (!descriptor(r,file,&fd,error)) return false;
    return fd != 1 || !r->options.bindings.output_is_terminal ||
        (sysv_unsigned(r,file,4,&flags,error) && sysv_store(r,file,4,flags|0x200,error));
}
static bool put_byte(guest_sysv_runtime *r,uint64_t file,uint8_t byte,int32_t *out,qa_error *error)
{
    uint64_t flags,next,end;
    if (!sysv_unsigned(r,file,4,&flags,error)) return false;
    if (flags & 8) return file_error(r,file,9,out,error);
    if (!output_buffer(r,file,error) || !sysv_pointer(r,field(r,file,5),&next,error) ||
        !sysv_pointer(r,field(r,file,6),&end,error)) return false;
    if (!next || !end) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V FILE has no output buffer cursors");
    if (next >= end) {
        if (!sysv_stdio_flush(r,file,out,error)) return false;
        if (*out) return true;
        if (!sysv_pointer(r,field(r,file,5),&next,error) || !next) return false;
    }
    if (!sysv_store(r,next,1,byte,error) || !sysv_put_pointer(r,field(r,file,5),next+1,error) ||
        !sysv_unsigned(r,file,4,&flags,error)) return false;
    if ((flags & 2) || ((flags & 0x200) && byte == 10)) {
        if (!sysv_stdio_flush(r,file,out,error)) return false;
        if (*out) return true;
    }
    *out = byte; return true;
}
bool sysv_stdio_put(guest_sysv_runtime *r,uint64_t file,bool wide,uint32_t value,int32_t *out,qa_error *error)
{
    int32_t fd; bool accepted;
    if (!descriptor(r,file,&fd,error) || !orient(r,file,wide,&accepted,error)) return false;
    *out = -1; if (!accepted) return true;
    if (wide && value > 127) return file_error(r,file,84,out,error);
    if (!put_byte(r,file,(uint8_t)value,out,error)) return false;
    if (*out >= 0) *out = (int32_t)value;
    return true;
}
bool sysv_stdio_get(guest_sysv_runtime *r,uint64_t file,bool wide,int32_t *out,qa_error *error)
{
    int32_t fd; uint64_t flags,next,end; bool accepted;
    if (!descriptor(r,file,&fd,error) || !sysv_unsigned(r,file,4,&flags,error)) return false;
    sysv_file *entry = opened(r,file);
    if ((!entry && fd) || (flags & 4)) return file_error(r,file,9,out,error);
    if (flags & 0x10) { *out = -1; return true; }
    if (!orient(r,file,wide,&accepted,error)) return false;
    *out = -1; if (!accepted) return true;
    if (entry) {
        uint64_t writing;
        if (!sysv_pointer(r,field(r,file,4),&writing,error)) return false;
        if (writing) {
            int32_t flushed;
            if (!sysv_stdio_flush(r,file,&flushed,error)) return false;
            if (flushed) return true;
            for (size_t i = 4; i <= 6; ++i) if (!sysv_put_pointer(r,field(r,file,i),0,error)) return false;
        }
    }
    if (!sysv_pointer(r,field(r,file,1),&next,error) || !sysv_pointer(r,field(r,file,2),&end,error)) return false;
    if (!next || !end || next >= end) {
        guest_sysv_stream stream = r->options.bindings.streams[0];
        if (!entry && (!stream.id || !stream.read)) return sysv_fail(error,QA_ERROR_UNSUPPORTED,"System V fgetc has no actual standard input capability");
        uint8_t bytes[8192]; size_t count = 0;
        bool callback = entry ? guest_runtime_resources_read(r->options.bindings.resources,entry->handle,
            bytes,sizeof(bytes),&count,error) : stream.read(stream.context,bytes,sizeof(bytes),&count,error);
        if (!sysv_current(r,error)) return false;
        if (count > sizeof(bytes)) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V input capability exceeded requested extent");
        if (!count) return callback && sysv_unsigned(r,file,4,&flags,error) && sysv_store(r,file,4,flags|0x10,error);
        uint64_t buffer;
        if (!sysv_pointer(r,field(r,file,7),&buffer,error) ||
            (!buffer && !sysv_allocate(r,8192,true,&buffer,error)) ||
            !sysv_write(r,buffer,bytes,count,error) ||
            !sysv_put_pointer(r,field(r,file,7),buffer,error) ||
            !sysv_put_pointer(r,field(r,file,8),buffer+8192,error) ||
            !sysv_put_pointer(r,field(r,file,3),buffer,error) ||
            !sysv_put_pointer(r,field(r,file,1),buffer,error) ||
            !sysv_put_pointer(r,field(r,file,2),buffer+count,error)) return false;
        if (entry) entry->buffer = buffer;
        if (!callback) return false;
        next = buffer;
    }
    uint64_t byte;
    if (!sysv_unsigned(r,next,1,&byte,error) || !sysv_put_pointer(r,field(r,file,1),next+1,error)) return false;
    if (wide && byte > 127) return file_error(r,file,84,out,error);
    *out = (int32_t)byte; return true;
}
bool sysv_stdio_unget(guest_sysv_runtime *r,uint64_t file,bool wide,uint32_t value,int32_t *out,qa_error *error)
{
    *out = -1; if (value == UINT32_MAX) return true;
    bool accepted; uint64_t next,base,flags; int32_t fd;
    if (!descriptor(r,file,&fd,error) || !sysv_unsigned(r,file,4,&flags,error)) return false;
    if (flags & 4) return file_error(r,file,9,out,error);
    if (!orient(r,file,wide,&accepted,error)) return false;
    if (!accepted) return true;
    if (!sysv_pointer(r,field(r,file,1),&next,error) || !sysv_pointer(r,field(r,file,3),&base,error)) return false;
    if (!next || !base || next <= base) return true;
    if (!sysv_store(r,next-1,1,value&255,error) || !sysv_put_pointer(r,field(r,file,1),next-1,error) ||
        !sysv_unsigned(r,file,4,&flags,error) || !sysv_store(r,file,4,flags&~UINT64_C(0x10),error)) return false;
    *out = value <= INT32_MAX ? (int32_t)value : -(int32_t)(UINT32_MAX-value)-1; return true;
}
static bool reserve_file(guest_sysv_runtime *r, uint64_t handle, bool *available, qa_error *error)
{
    *available = false;
    for (size_t i = 0; i < r->open_file_count; ++i) if (r->open_files[i].handle == handle) {
        return sysv_errno(r,16,error);
    }
    if (r->next_file > INT32_MAX) return sysv_errno(r,24,error);
    if (!guest_grow((void **)&r->open_files,&r->open_file_capacity,r->open_file_count+1,
        sizeof(*r->open_files),error)) return false;
    *available = true; return true;
}
static bool publish_file(guest_sysv_runtime *r, const guest_runtime_file_view *view,
    uint32_t rights, bool append, uint64_t *out, qa_error *error)
{
    sysv_file *entry = r->open_files+r->open_file_count++;
    *entry = (sysv_file){.handle=view->id,.capability=view->capability,.descriptor=r->next_file++,
        .mode=rights,.append=append};
    file_layout l = layout(r); size_t p = r->target.pointer_bytes;
    uint32_t flags = UINT32_C(0xfbad0000)|0x2000|(append ? 0x1000u : 0u)|
        ((rights & GUEST_RUNTIME_FILE_READ) ? 0u : 4u)|((rights & GUEST_RUNTIME_FILE_WRITE) ? 0u : 8u);
    if (!sysv_allocate(r,l.size+p,true,&entry->address,error) ||
        !sysv_allocate(r,p*2+8,true,&entry->lock,error) ||
        !sysv_allocate(r,p == 8 ? 312 : 180,true,&entry->wide,error) ||
        !sysv_store(r,entry->address,4,flags,error) ||
        !sysv_store(r,entry->address+l.descriptor,4,entry->descriptor,error) ||
        !sysv_store(r,entry->address+l.offset,8,UINT64_MAX,error) ||
        !sysv_store(r,entry->address+l.old,p,UINT64_MAX,error) ||
        !sysv_put_pointer(r,entry->address+l.lock,entry->lock,error) ||
        !sysv_put_pointer(r,entry->address+l.wide,entry->wide,error) ||
        !sysv_put_pointer(r,entry->address+l.size,r->file_tables[0],error) ||
        !sysv_put_pointer(r,entry->wide+(p == 8 ? 304 : 176),r->file_tables[1],error)) {
        r->failed = true; return false;
    }
    *out = entry->address; return true;
}
static bool temporary_file(guest_sysv_runtime *r, uint64_t *out, qa_error *error)
{
    *out = 0;
    if (!r->options.bindings.open_temporary_file) return sysv_errno(r,38,error);
    uint64_t handle = 0; bool acquired = false;
    if (!r->options.bindings.open_temporary_file(r->options.bindings.context, &handle, &acquired, error) ||
        !sysv_current(r,error)) return false;
    if (!acquired || !handle) return sysv_errno(r,5,error);
    guest_runtime_file_view view;
    if (!guest_runtime_resources_find(r->options.bindings.resources,handle,&view,error)) return false;
    bool available;
    if (!reserve_file(r,handle,&available,error)) return false;
    if (!available) return guest_runtime_resources_close(r->options.bindings.resources,handle,error);
    if (!guest_runtime_resources_seek(r->options.bindings.resources,handle,0,error)) return false;
    return publish_file(r, &view, GUEST_RUNTIME_FILE_READ|GUEST_RUNTIME_FILE_WRITE, false, out, error);
}
static bool open_file(guest_sysv_runtime *r,uint64_t name_address,uint64_t mode_address,
    uint64_t *out,qa_error *error)
{
    qa_buffer name = {0}, text = {0}; bool present;
    *out = 0;
    if (!name_address || !mode_address) return sysv_fail(error,QA_ERROR_ARGUMENT,"fopen requires actual path and mode strings");
    if (!sysv_string(r,name_address,SYSV_MAX_STRING,&name,&present,error)) goto failed;
    if (!present) { sysv_fail(error,QA_ERROR_ARGUMENT,"fopen path is not terminated"); goto failed; }
    if (!sysv_string(r,mode_address,16,&text,&present,error)) goto failed;
    if (!present) { sysv_fail(error,QA_ERROR_ARGUMENT,"fopen mode is not terminated"); goto failed; }
    uint8_t *name_bytes = realloc(name.data,name.size+1);
    if (!name_bytes) { sysv_fail(error,QA_ERROR_MEMORY,"holding fopen path"); goto failed; }
    name.data = name_bytes; name.data[name.size] = 0;
    uint8_t *mode_bytes = realloc(text.data,text.size+1);
    if (!mode_bytes) { sysv_fail(error,QA_ERROR_MEMORY,"holding fopen mode"); goto failed; }
    text.data = mode_bytes; text.data[text.size] = 0;
    const char *mode = (const char *)text.data;
    uint32_t rights = mode[0] == 'r' ? GUEST_RUNTIME_FILE_READ : GUEST_RUNTIME_FILE_WRITE;
    bool plus = false, binary = false, append = mode[0] == 'a';
    if (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a') goto invalid;
    for (size_t i = 1; mode[i]; ++i) {
        if (mode[i] == '+' && !plus) { plus = true; rights = GUEST_RUNTIME_FILE_READ|GUEST_RUNTIME_FILE_WRITE; }
        else if (mode[i] == 'b' && !binary) binary = true;
        else goto invalid;
    }
    guest_runtime_file_view view = {0}; bool found = false, fresh = false, in_use = false;
    size_t count = guest_runtime_resources_count(r->options.bindings.resources);
    for (size_t i = 0; i < count; ++i) {
        guest_runtime_file_view candidate;
        if (!guest_runtime_resources_at(r->options.bindings.resources,i,&candidate,error)) goto failed;
        bool standard = false;
        for (size_t j = 0; j < 3; ++j)
            if (candidate.id == r->options.bindings.streams[j].id) standard = true;
        if (standard) continue;
        if (!candidate.closed && candidate.name && !strcmp(candidate.name,(const char *)name.data)) {
            if (guest_sysv_file_in_use(r,candidate.id)) { in_use = true; continue; }
            if (found) { if (!sysv_errno(r,16,error)) goto failed; goto done; }
            view = candidate; found = true;
        }
    }
    if (!found && r->options.bindings.open_file) {
        uint64_t handle = 0; bool acquired = false; qa_error opening = {0};
        uint32_t creation = mode[0] == 'w' ? QA_FS_CREATE_ALWAYS :
            append ? QA_FS_OPEN_ALWAYS : QA_FS_OPEN_EXISTING;
        bool okay = r->options.bindings.open_file(r->options.bindings.context,
            (const char *)name.data,rights,creation,&handle,&acquired,&opening);
        if (!okay) {
            if (!acquired && opening.code == QA_ERROR_NOT_FOUND) {
                if (!sysv_errno(r,2,error)) goto failed;
                goto done;
            }
            if (error) *error = opening;
            goto failed;
        }
        if (!sysv_current(r,error)) goto failed;
        if (!acquired || !handle) { if (!sysv_errno(r,2,error)) goto failed; goto done; }
        if (!guest_runtime_resources_find(r->options.bindings.resources,handle,&view,error)) goto failed;
        if (view.closed || !view.name || strcmp(view.name,(const char *)name.data)) {
            sysv_fail(error,QA_ERROR_ARGUMENT,"fopen returned a different actual registered file"); goto failed;
        }
        for (size_t j = 0; j < 3; ++j) if (handle == r->options.bindings.streams[j].id) {
            sysv_fail(error,QA_ERROR_ARGUMENT,"fopen returned a standard stream capability"); goto failed;
        }
        found = true; fresh = true;
    }
    if (!found || (view.mode & rights) != rights) {
        if (!sysv_errno(r,found ? 13 : in_use ? 16 : 2,error)) goto failed;
        goto done;
    }
    bool available;
    if (!reserve_file(r, view.id, &available, error)) goto failed;
    if (!available) goto done;
    uint64_t start = 0;
    if (mode[0] == 'w' && !fresh && (!guest_runtime_resources_truncate(r->options.bindings.resources,view.id,0,error) ||
        !sysv_current(r,error))) goto failed;
    if (append && !plus && (!guest_runtime_resources_size(r->options.bindings.resources,view.id,&start,error) ||
        !sysv_current(r,error))) goto failed;
    if (!guest_runtime_resources_seek(r->options.bindings.resources,view.id,start,error)) goto failed;
    if (!publish_file(r, &view, rights, append, out, error)) goto failed;
done:
    qa_buffer_free(&name); qa_buffer_free(&text); return true;
invalid:
    if (!sysv_errno(r,22,error)) goto failed;
    goto done;
failed:
    qa_buffer_free(&name); qa_buffer_free(&text); return false;
}
static bool close_file(guest_sysv_runtime *r,uint64_t address,int32_t *out,qa_error *error)
{
    sysv_file *entry = opened(r,address);
    if (!entry) return file_error(r,address,9,out,error);
    if (!entry->closing) {
        if (!sysv_stdio_flush(r,address,out,error)) return false;
        if (*out) return true;
        entry->closing = true;
    }
    if (!guest_runtime_resources_close(r->options.bindings.resources,entry->handle,error) ||
        !sysv_current(r,error)) return false;
    uint64_t *allocations[] = {&entry->buffer,&entry->wide,&entry->lock,&entry->address};
    for (size_t i = 0; i < 4; ++i) if (*allocations[i]) {
        if (!sysv_free(r,*allocations[i],error)) return false;
        *allocations[i] = 0;
    }
    size_t index = (size_t)(entry-r->open_files);
    memmove(entry,entry+1,(r->open_file_count-index-1)*sizeof(*entry));
    --r->open_file_count; *out = 0; return true;
}
static bool position(guest_sysv_runtime *r,uint64_t address,uint64_t *out,qa_error *error)
{
    sysv_file *entry = opened(r,address); guest_runtime_file_view view;
    uint64_t next,end,base,write;
    if (!entry || entry->closing ||
        !guest_runtime_resources_find(r->options.bindings.resources,entry->handle,&view,error) ||
        !sysv_pointer(r,field(r,address,1),&next,error) || !sysv_pointer(r,field(r,address,2),&end,error) ||
        !sysv_pointer(r,field(r,address,4),&base,error) || !sysv_pointer(r,field(r,address,5),&write,error)) return false;
    uint64_t unread = next && end > next ? end-next : 0;
    uint64_t pending = base && write > base ? write-base : 0;
    if (unread > view.offset || pending > UINT64_MAX-(view.offset-unread))
        return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE position exceeds retained capability cursor");
    if (entry->append && pending) {
        uint64_t size;
        if (!guest_runtime_resources_size(r->options.bindings.resources,entry->handle,&size,error) ||
            !sysv_current(r,error) || size > UINT64_MAX-pending) return false;
        *out = size+pending;
    } else *out = view.offset-unread+pending;
    return true;
}
static bool seek_file(guest_sysv_runtime *r,uint64_t address,int64_t delta,int32_t whence,
    int32_t *out,qa_error *error)
{
    sysv_file *entry = opened(r,address); uint64_t start = 0, target, flags;
    if (!entry || entry->closing) return file_error(r,address,29,out,error);
    if (whence == 1) { if (!position(r,address,&start,error)) return false; }
    else if (whence != 0 && whence != 2) return file_error(r,address,22,out,error);
    if (!sysv_stdio_flush(r,address,out,error)) return false;
    if (*out) return true;
    if (whence == 2 && (!guest_runtime_resources_size(r->options.bindings.resources,entry->handle,&start,error) ||
        !sysv_current(r,error))) return false;
    uint64_t magnitude = delta < 0 ? (uint64_t)(-(delta+1))+1 : (uint64_t)delta;
    if ((delta < 0 && magnitude > start) || (delta >= 0 && magnitude > UINT64_MAX-start))
        return file_error(r,address,22,out,error);
    target = delta < 0 ? start-magnitude : start+magnitude;
    if (target > INT64_MAX) return file_error(r,address,75,out,error);
    if (!guest_runtime_resources_seek(r->options.bindings.resources,entry->handle,target,error)) return false;
    for (size_t i = 1; i <= 6; ++i) if (!sysv_put_pointer(r,field(r,address,i),0,error)) return false;
    *out = 0;
    return sysv_unsigned(r,address,4,&flags,error) && sysv_store(r,address,4,flags&~UINT64_C(0x10),error);
}
bool sysv_stdio_retire(guest_sysv_runtime *r,qa_error *error)
{
    while (r->open_file_count) {
        int32_t result;
        if (!close_file(r,r->open_files[0].address,&result,error) || result) return false;
    }
    int32_t result;
    return sysv_stdio_flush(r,r->files[1],&result,error) && !result &&
        sysv_stdio_flush(r,r->files[2],&result,error) && !result;
}
bool sysv_stdio_fields(qa_source_save_io *io,guest_sysv_runtime *r)
{
    bool resources = r->options.bindings.resources != NULL;
    bool opener = r->options.bindings.open_file != NULL;
    if (!qa_source_save_bool(io,&resources) || !qa_source_save_bool(io,&opener) ||
        !qa_source_save_u32(io,&r->next_file) ||
        !qa_source_save_u64(io,r->file_tables) || !qa_source_save_u64(io,r->file_tables+1) ||
        !qa_source_save_count(io,&r->open_file_count,SYSV_MAX_ALLOCATION/sizeof(sysv_file))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        r->file_resources = resources;
        r->file_opener = opener;
        if (io->offset > io->input.size || r->open_file_count > (io->input.size-io->offset)/58)
            return sysv_fail(io->error,QA_ERROR_FORMAT,"truncated System V FILE records");
        if (r->open_file_count) {
            r->open_files = calloc(r->open_file_count,sizeof(*r->open_files));
            if (!r->open_files) return sysv_fail(io->error,QA_ERROR_MEMORY,"decoding System V FILE owners");
        }
        r->open_file_capacity = r->open_file_count;
    }
    for (size_t i = 0; i < r->open_file_count; ++i) {
        sysv_file *f = r->open_files+i;
        uint64_t *fields[] = {&f->address,&f->lock,&f->wide,&f->buffer,&f->handle,&f->capability};
        for (size_t j = 0; j < 6; ++j) if (!qa_source_save_u64(io,fields[j])) return false;
        if (!qa_source_save_u32(io,&f->descriptor) || !qa_source_save_u32(io,&f->mode) ||
            !qa_source_save_bool(io,&f->append) || !qa_source_save_bool(io,&f->closing)) return false;
    }
    return true;
}
static bool heap_is(const guest_sysv_runtime *r,uint64_t address,uint64_t bytes)
{
    for (size_t i = 0; i < r->heap_count; ++i)
        if (r->heap[i].address == address && r->heap[i].bytes == bytes) return true;
    return false;
}
bool sysv_stdio_valid(const guest_sysv_runtime *r,qa_error *error)
{
    size_t p = r->target.pointer_bytes; file_layout l = layout(r);
    if (r->next_file < 3 || r->next_file > (uint32_t)INT32_MAX+1u ||
        !heap_is(r,r->file_tables[0],21*p) || !heap_is(r,r->file_tables[1],21*p) ||
        r->file_tables[0] == r->file_tables[1])
        return sysv_fail(error,QA_ERROR_FORMAT,"invalid System V FILE table or descriptor cursor");
    for (size_t i = 0; i < r->open_file_count; ++i) {
        const sysv_file *f = r->open_files+i;
        if (!f->address || !f->handle || !f->capability || f->descriptor < 3 || f->descriptor >= r->next_file ||
            !f->mode || (f->mode & ~UINT32_C(3)) || (f->append && !(f->mode & GUEST_RUNTIME_FILE_WRITE)) ||
            !heap_is(r,f->address,l.size+p) ||
            (!f->closing && (!f->lock || !f->wide)) ||
            (f->lock && !heap_is(r,f->lock,p*2+8)) ||
            (f->wide && !heap_is(r,f->wide,p == 8 ? 312 : 180)) ||
            (f->buffer && !heap_is(r,f->buffer,8192)))
            return sysv_fail(error,QA_ERROR_FORMAT,"invalid retained System V FILE ownership");
        for (size_t j = 0; j < 3; ++j) if (f->address == r->files[j] || f->wide == r->wide_files[j] ||
            f->handle == r->options.bindings.streams[j].id)
            return sysv_fail(error,QA_ERROR_FORMAT,"dynamic System V FILE aliases a standard stream");
        for (size_t j = 0; j < i; ++j) if (f->address == r->open_files[j].address ||
            f->descriptor == r->open_files[j].descriptor || f->handle == r->open_files[j].handle ||
            f->capability == r->open_files[j].capability)
            return sysv_fail(error,QA_ERROR_FORMAT,"duplicate System V FILE close owner");
        const uint64_t allocations[] = {f->address,f->lock,f->wide,f->buffer};
        for (size_t a = 0; a < 4; ++a) if (allocations[a]) {
            for (size_t b = 0; b < a; ++b) if (allocations[a] == allocations[b])
                return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE allocation has duplicate owners");
            for (size_t j = 0; j < i; ++j) {
                const sysv_file *other = r->open_files+j;
                if (allocations[a] == other->address || allocations[a] == other->lock ||
                    allocations[a] == other->wide || allocations[a] == other->buffer)
                    return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE shares another FILE allocation");
            }
        }
        if (r->options.bindings.resources) {
            guest_runtime_file_view v;
            if (!guest_runtime_resources_find(r->options.bindings.resources,f->handle,&v,error) ||
                v.capability != f->capability || (v.mode & f->mode) != f->mode || (v.closed && !f->closing) ||
                (!v.closed && (!f->lock || !f->wide)))
                return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE differs from its actual opened capability");
        } else if (!r->detached || !r->file_resources)
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE has no process resource owner");
    }
    return true;
}
static bool lower_number(const qa_native_guest *guest,uint64_t address,size_t bytes,
    uint64_t *out,qa_error *error)
{
    uint8_t data[8]; *out = 0;
    if (!qa_native_guest_read(guest,address,data,bytes,error)) return false;
    for (size_t i = 0; i < bytes; ++i) *out |= (uint64_t)data[i] << (i*8);
    return true;
}
bool sysv_stdio_lower_valid(guest_sysv_runtime *r,const qa_native_guest *guest,qa_error *error)
{
    if (!sysv_stdio_valid(r,error)) return false;
    size_t p = r->target.pointer_bytes; file_layout l = layout(r);
    const char *names[] = {"_IO_file_jumps","_IO_wfile_jumps"};
    for (size_t i = 0; i < 2; ++i) {
        guest_runtime_import_key key = {.scope=r->options.scope,.library="libc.so.6",.kind=GUEST_RUNTIME_SYMBOL_NAME,
            .name=names[i],.version=p == 4 ? "GLIBC_2.1" : "GLIBC_2.2.5"};
        guest_runtime_import_view view;
        if (!guest_runtime_imports_find(r->imports,&key,&view,error) || view.kind != GUEST_RUNTIME_IMPORT_DATA ||
            view.address != r->file_tables[i] || view.bytes != 21*p)
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE vtable differs from its actual declaration");
    }
    for (size_t i = 0; i < r->open_file_count; ++i) {
        const sysv_file *f = r->open_files+i;
        if (f->closing) continue;
        uint64_t value;
        if (!lower_number(guest,f->address,4,&value,error) ||
            (value & UINT32_C(0xffff0000)) != UINT32_C(0xfbad0000) ||
            !!(value & 0x1000u) != f->append ||
            (value & 12u) != (((f->mode & 1u) ? 0u : 4u)|((f->mode & 2u) ? 0u : 8u)) ||
            !lower_number(guest,f->address+l.descriptor,4,&value,error) || value != f->descriptor ||
            !lower_number(guest,f->address+l.lock,p,&value,error) || value != f->lock ||
            !lower_number(guest,f->address+l.wide,p,&value,error) || value != f->wide ||
            !lower_number(guest,f->address+l.size,p,&value,error) || value != r->file_tables[0] ||
            !lower_number(guest,f->wide+(p == 8 ? 304 : 176),p,&value,error) || value != r->file_tables[1])
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE RAM differs from its retained ABI owner");
        if (!lower_number(guest,f->address+l.mode,4,&value,error) ||
            (value != 0 && value != 1 && value != UINT32_MAX))
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE has invalid stream orientation");
        uint64_t cursors[9] = {0};
        for (size_t j = 1; j <= 8; ++j) if (!lower_number(guest,field(r,f->address,j),p,cursors+j,error)) return false;
        if (cursors[7] != f->buffer || cursors[8] != (f->buffer ? f->buffer+8192 : 0))
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE buffer differs from its heap owner");
        for (size_t j = 1; j <= 6; ++j) if (cursors[j] &&
            (!f->buffer || cursors[j] < f->buffer || cursors[j] > f->buffer+8192))
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE cursor exceeds actual buffer");
        if ((cursors[1] && (cursors[3] > cursors[1] || cursors[1] > cursors[2])) ||
            (cursors[5] && (cursors[4] > cursors[5] || cursors[5] > cursors[6])))
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE buffer cursors are reversed");
        if ((cursors[1] || cursors[2] || cursors[3]) && (cursors[4] || cursors[5] || cursors[6]))
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE retains conflicting read and write buffers");
        guest_runtime_file_view source;
        if (!guest_runtime_resources_find(r->options.bindings.resources,f->handle,&source,error)) return false;
        uint64_t unread = cursors[1] && cursors[2] > cursors[1] ? cursors[2]-cursors[1] : 0;
        uint64_t pending = cursors[4] && cursors[5] > cursors[4] ? cursors[5]-cursors[4] : 0;
        if (unread > source.offset || pending > UINT64_MAX-source.offset)
            return sysv_fail(error,QA_ERROR_FORMAT,"System V FILE buffer differs from the actual resource position");
    }
    return true;
}
bool sysv_stdio_call(sysv_service *service,const qa_native_value *args,qa_native_value *out,qa_error *error)
{
    guest_sysv_runtime *r = service->runtime; out->type = service->result.kind;
    switch (service->operation) {
    case ST_TMPFILE: return temporary_file(r, &out->as.address, error);
    case ST_REWIND: {
        int32_t result; uint64_t flags;
        return seek_file(r,args[0].as.address,0,0,&result,error) &&
            sysv_unsigned(r,args[0].as.address,4,&flags,error) &&
            sysv_store(r,args[0].as.address,4,flags&~UINT64_C(0x30),error);
    }
    case ST_OPEN: return open_file(r,args[0].as.address,args[1].as.address,&out->as.address,error);
    case ST_CLOSE: return close_file(r,args[0].as.address,&out->as.i32,error);
    case ST_SEEK:
        return seek_file(r,args[0].as.address,r->target.pointer_bytes == 8 ? args[1].as.i64 : args[1].as.i32,
            args[2].as.i32,&out->as.i32,error);
    case ST_TELL: {
        uint64_t value = 0; int32_t fd;
        if (!descriptor(r,args[0].as.address,&fd,error)) return false;
        bool accepted = opened(r,args[0].as.address) != NULL;
        if (accepted && !position(r,args[0].as.address,&value,error)) return false;
        uint64_t maximum = r->target.pointer_bytes == 8 ? (uint64_t)INT64_MAX : (uint64_t)INT32_MAX;
        if (!accepted || value > maximum) {
            if (!sysv_errno(r,accepted ? 75 : 29,error)) return false;
            if (out->type == QA_NATIVE_I64) out->as.i64 = -1; else out->as.i32 = -1;
        } else if (out->type == QA_NATIVE_I64) out->as.i64 = (int64_t)value;
        else out->as.i32 = (int32_t)value;
        return true;
    }
    case ST_FLUSH:
        if (args[0].as.address) return sysv_stdio_flush(r,args[0].as.address,&out->as.i32,error);
        { int32_t first,second;
          if (!sysv_stdio_flush(r,r->files[1],&first,error) || !sysv_stdio_flush(r,r->files[2],&second,error)) return false;
          out->as.i32 = first || second ? -1 : 0;
          for (size_t i = 0; i < r->open_file_count; ++i) if (!r->open_files[i].closing) {
              int32_t result;
              if (!sysv_stdio_flush(r,r->open_files[i].address,&result,error)) return false;
              if (result) out->as.i32 = -1;
          }
          return true; }
    case ST_PUT: return sysv_stdio_put(r,args[1].as.address,false,(uint8_t)args[0].as.i32,&out->as.i32,error);
    case ST_GET: return sysv_stdio_get(r,args[0].as.address,false,&out->as.i32,error);
    case ST_UNGET:
        return (args[1].as.address || sysv_fail(error,QA_ERROR_ARGUMENT,"ungetc requires its actual nonnull FILE")) &&
            sysv_stdio_unget(r,args[1].as.address,false,(uint32_t)args[0].as.i32,&out->as.i32,error);
    case ST_OVERFLOW:
        if (args[1].as.i32 == -1) return sysv_stdio_flush(r,args[0].as.address,&out->as.i32,error);
        return sysv_stdio_put(r,args[0].as.address,false,(uint8_t)args[1].as.i32,&out->as.i32,error);
    case ST_WIDE: {
        uint64_t mode; int32_t fd;
        if (!args[0].as.address) return sysv_fail(error,QA_ERROR_ARGUMENT,"fwide requires its actual nonnull FILE");
        if (!descriptor(r,args[0].as.address,&fd,error)) return false;
        if (!sysv_unsigned(r,args[0].as.address+layout(r).mode,4,&mode,error)) return false;
        if (!mode) {
            int32_t desired = args[1].as.i32 > 0 ? 1 : args[1].as.i32 < 0 ? -1 : 0;
            if (!sysv_store(r,args[0].as.address+layout(r).mode,4,(uint32_t)desired,error)) return false;
            mode = (uint32_t)desired;
        }
        out->as.i32 = (uint32_t)mode <= INT32_MAX ? (int32_t)mode : -(int32_t)(UINT32_MAX-(uint32_t)mode)-1; return true;
    }
    case ST_WRITE: case ST_READ: {
        uint64_t size = sysv_integer(args+1), count = sysv_integer(args+2);
        if (size > SYSV_MAX_ALLOCATION || count > SYSV_MAX_ALLOCATION ||
            (size && count > SYSV_MAX_ALLOCATION/size)) return sysv_fail(error,QA_ERROR_ARGUMENT,"System V stdio item extent exceeds actual supported memory");
        size_t total = (size_t)(size*count), completed = 0; uint8_t *bytes = NULL;
        if (total && (!args[0].as.address || !args[3].as.address))
            return sysv_fail(error,QA_ERROR_ARGUMENT,"System V stdio transfer requires actual nonnull memory and FILE");
        if (total && service->operation == ST_WRITE) {
            bytes = malloc(total);
            if (!bytes) return sysv_fail(error,QA_ERROR_MEMORY,"holding fwrite source bytes");
            if (!sysv_read(r,args[0].as.address,bytes,total,error)) { free(bytes); return false; }
            int32_t fd; bool accepted;
            if (!descriptor(r,args[3].as.address,&fd,error) || !orient(r,args[3].as.address,false,&accepted,error)) { free(bytes); return false; }
            if (!accepted) { free(bytes); if (out->type == QA_NATIVE_U32) out->as.u32 = 0; else out->as.u64 = 0; return true; }
        }
        bool ok = true;
        for (; completed < total; ++completed) {
            int32_t value;
            if (service->operation == ST_WRITE) ok = put_byte(r,args[3].as.address,bytes[completed],&value,error);
            else {
                ok = sysv_stdio_get(r,args[3].as.address,false,&value,error);
                if (ok && value >= 0) ok = sysv_store(r,args[0].as.address+completed,1,(uint8_t)value,error);
            }
            if (!ok || value < 0) break;
        }
        free(bytes); uint64_t items = size ? completed/size : 0;
        if (out->type == QA_NATIVE_U32) out->as.u32 = (uint32_t)items; else out->as.u64 = items;
        return ok;
    }
    default: return sysv_fail(error,QA_ERROR_FORMAT,"unknown retained System V stdio operation");
    }
}
