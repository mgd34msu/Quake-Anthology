#define _GNU_SOURCE
#include "host_child.h"
#include "qa/binary.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(__x86_64__)
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
#endif

typedef guest_profile_guard_callback host_callback;
struct guest_host_child {
    qa_native_target target;
    int descriptor;
    int64_t process;
    uint64_t sequence,thread;
    unsigned running,callback_depth;
    bool failed,source_initialized,state_written;
    guest_host_memory *memory;
    guest_host_x86_64_capabilities capability;
    guest_profile_guard_receipt profile;
    guest_profile_cpu_domain domain;
    guest_host_x86_64_state state;
    guest_host_stop fault;
    bool has_fault;
    host_callback *callbacks; size_t callback_count,callback_capacity;
    guest_profile_interest *interests; size_t interest_count,interest_capacity;
};
static bool fail(qa_error *error,qa_status code,uint64_t where,const char *message)
{ qa_error_set(error,code,(size_t)where,"%s",message); return false; }

#if defined(__linux__) && defined(__x86_64__)
enum { HOST_READY=1,HOST_BACKING,HOST_MAP,HOST_CHANGE,HOST_BIND,HOST_UNBIND,
    HOST_RUN,HOST_RESUME,HOST_ABORT,HOST_EXIT,HOST_DROP_BACKING,HOST_STOP,HOST_DONE,HOST_FINISH,HOST_CPU_CLOCK,HOST_INTEREST,HOST_CANCEL };
#define HOST_REPLY UINT32_C(0x80000000)
typedef struct host_packet { uint32_t operation,status; uint64_t sequence; qa_buffer body; int descriptor; } host_packet;
typedef struct host_server_backing { guest_host_backing_view view; int descriptor; } host_server_backing;
typedef struct host_server {
    int descriptor;
    host_server_backing *backings; size_t backing_count,backing_capacity;
    guest_profile_guard_mapping *mappings; size_t mapping_count,mapping_capacity;
    host_callback *callbacks; size_t callback_count,callback_capacity;
    guest_profile_interest *interests; size_t interest_count,interest_capacity;
    uint64_t stop,run_sequence,bypass;
    guest_host_x86_64_capabilities capability;
    guest_host_x86_64_state state;
    guest_profile_guard_receipt profile;
    guest_profile_cpu_domain domain;
    guest_profile_guard_fault profile_fault;
    bool stopped,exit,failed,syscalls;
} host_server;

static bool grow(void **pointer,size_t *capacity,size_t count,size_t bytes,qa_error *error)
{
    if (count<=*capacity) return true;
    if (count>SIZE_MAX/bytes) return fail(error,QA_ERROR_MEMORY,count,"host child table overflow");
    size_t next=*capacity<16?16:*capacity;
    while(next<count) { if(next>SIZE_MAX/2) {next=count;break;} next*=2; }
    if(next>SIZE_MAX/bytes) next=count;
    void *data=realloc(*pointer,next*bytes);
    if(!data) return fail(error,QA_ERROR_MEMORY,count,"retaining host child table");
    *pointer=data;*capacity=next;return true;
}
static bool transfer(int descriptor,void *bytes,size_t count,bool writing,qa_error *error)
{
    for(size_t offset=0;offset<count;) {
        ssize_t amount=writing?send(descriptor,(uint8_t *)bytes+offset,count-offset,MSG_NOSIGNAL):
            recv(descriptor,(uint8_t *)bytes+offset,count-offset,0);
        if(amount<0 && errno==EINTR) continue;
        if(amount<=0) return fail(error,QA_ERROR_FORMAT,offset,"native child channel closed during actual transfer");
        offset+=(size_t)amount;
    }
    return true;
}
static void packet_free(host_packet *packet)
{ qa_buffer_free(&packet->body);if(packet->descriptor>=0) close(packet->descriptor);packet->descriptor=-1; }
static bool packet_send(int descriptor,uint32_t operation,uint32_t status,uint64_t sequence,
    qa_bytes bytes,int passed,qa_error *error)
{
    uint8_t header[32]={0};memcpy(header,"QAHC",4);qa_store_u32le(header+4,1);
    qa_store_u32le(header+8,operation);qa_store_u32le(header+12,status);qa_store_u64le(header+16,sequence);qa_store_u64le(header+24,bytes.size);
    if((!bytes.data && bytes.size) || bytes.size>UINT32_MAX) return fail(error,QA_ERROR_ARGUMENT,sequence,"native child packet extent is invalid");
    struct iovec vector={header,1};struct msghdr message={.msg_iov=&vector,.msg_iovlen=1};
    union {struct cmsghdr alignment;uint8_t bytes[CMSG_SPACE(sizeof(int))];} control;
    if(passed>=0) {
        memset(&control,0,sizeof(control));message.msg_control=control.bytes;message.msg_controllen=sizeof(control.bytes);
        struct cmsghdr *item=CMSG_FIRSTHDR(&message);item->cmsg_level=SOL_SOCKET;item->cmsg_type=SCM_RIGHTS;item->cmsg_len=CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(item),&passed,sizeof(passed));
    }
    ssize_t sent;do{sent=sendmsg(descriptor,&message,MSG_NOSIGNAL);}while(sent<0 && errno==EINTR);
    return (sent==1 && transfer(descriptor,header+1,31,true,error) && transfer(descriptor,(void *)bytes.data,bytes.size,true,error)) ||
        fail(error,QA_ERROR_FORMAT,sequence,"sending actual native child packet failed");
}
static bool packet_receive(int descriptor,host_packet *packet,qa_error *error)
{
    memset(packet,0,sizeof(*packet));packet->descriptor=-1;uint8_t header[32];
    union {struct cmsghdr alignment;uint8_t bytes[CMSG_SPACE(sizeof(int)*4)];} control;
    struct iovec vector={header,1};struct msghdr message={.msg_iov=&vector,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
    ssize_t received;do{received=recvmsg(descriptor,&message,MSG_CMSG_CLOEXEC);}while(received<0 && errno==EINTR);
    bool valid=received==1 && !(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC));
    for(struct cmsghdr *item=CMSG_FIRSTHDR(&message);received>0 && item;item=CMSG_NXTHDR(&message,item)) {
        if(item->cmsg_level!=SOL_SOCKET || item->cmsg_type!=SCM_RIGHTS || item->cmsg_len<CMSG_LEN(sizeof(int))) {valid=false;continue;}
        size_t count=(item->cmsg_len-CMSG_LEN(0))/sizeof(int);int *fds=(int *)CMSG_DATA(item);
        for(size_t i=0;i<count;++i) {if(packet->descriptor<0)packet->descriptor=fds[i];else{close(fds[i]);valid=false;}}
    }
    if(!valid || !transfer(descriptor,header+1,31,false,error) || memcmp(header,"QAHC",4) || qa_load_u32le(header+4)!=1) {
        packet_free(packet);return fail(error,QA_ERROR_FORMAT,0,"native child packet header or descriptor is invalid");
    }
    uint64_t count=qa_load_u64le(header+24);
    if(count>SIZE_MAX || count>UINT32_MAX) {packet_free(packet);return fail(error,QA_ERROR_FORMAT,0,"native child packet byte span is invalid");}
    packet->operation=qa_load_u32le(header+8);packet->status=qa_load_u32le(header+12);packet->sequence=qa_load_u64le(header+16);
    packet->body.data=count?malloc((size_t)count):NULL;packet->body.size=(size_t)count;
    if(count && !packet->body.data) {packet_free(packet);return fail(error,QA_ERROR_MEMORY,0,"retaining native child packet");}
    if(!transfer(descriptor,packet->body.data,packet->body.size,false,error)) {packet_free(packet);return false;}return true;
}
static bool reply_failure(int descriptor,uint32_t operation,uint64_t sequence,const qa_error *error,qa_error *transport_error)
{
    const char *message=error && error->message[0]?error->message:"native child operation failed";
    return packet_send(descriptor,operation|HOST_REPLY,error && error->code!=QA_OK?(uint32_t)error->code:QA_ERROR_ARGUMENT,
        sequence,(qa_bytes){(const uint8_t *)message,strlen(message)},-1,transport_error);
}
static bool state_encode(const guest_host_x86_64_state *state,qa_buffer *out,qa_error *error)
{
    if(!state || !out || out->data || out->size || !state->xsave.data || state->xsave.size<576 || state->xsave.size>SIZE_MAX-188)
        return fail(error,QA_ERROR_ARGUMENT,0,"native hardware transfer needs actual state and empty output");
    out->size=188+state->xsave.size;out->data=malloc(out->size);
    if(!out->data) {out->size=0;return fail(error,QA_ERROR_MEMORY,0,"encoding native hardware transfer");}
    for(size_t i=0;i<16;++i) qa_store_u64le(out->data+i*8,state->registers[i]);
    qa_store_u64le(out->data+128,state->instruction);qa_store_u64le(out->data+136,state->flags);
    qa_store_u64le(out->data+144,state->fs_base);qa_store_u64le(out->data+152,state->gs_base);
    for(size_t i=0;i<6;++i) qa_store_u16le(out->data+160+i*2,state->selectors[i]);
    qa_store_u64le(out->data+172,state->xfeatures);qa_store_u64le(out->data+180,state->xsave.size);
    memcpy(out->data+188,state->xsave.data,state->xsave.size);return true;
}
static bool state_decode(qa_bytes bytes,const guest_host_x86_64_capabilities *capability,
    guest_host_x86_64_state *out,qa_error *error)
{
    if(bytes.size<188 || !out || out->xsave.data) return fail(error,QA_ERROR_FORMAT,0,"native hardware transfer is truncated or output occupied");
    uint64_t count=qa_load_u64le(bytes.data+180);
    if(count!=bytes.size-188 || count>capability->xsave_bytes || count<576) return fail(error,QA_ERROR_FORMAT,0,"native hardware transfer differs from real XSAVE extent");
    void *storage=NULL;if(posix_memalign(&storage,64,(size_t)count)!=0) return fail(error,QA_ERROR_MEMORY,0,"decoding aligned native hardware transfer");
    for(size_t i=0;i<16;++i) out->registers[i]=qa_load_u64le(bytes.data+i*8);
    out->instruction=qa_load_u64le(bytes.data+128);out->flags=qa_load_u64le(bytes.data+136);
    out->fs_base=qa_load_u64le(bytes.data+144);out->gs_base=qa_load_u64le(bytes.data+152);
    for(size_t i=0;i<6;++i) out->selectors[i]=qa_load_u16le(bytes.data+160+i*2);
    out->xfeatures=qa_load_u64le(bytes.data+172);out->xsave=(qa_buffer){storage,(size_t)count};memcpy(storage,bytes.data+188,(size_t)count);
    if(!guest_host_x86_64_state_valid(out,capability,error)) {guest_host_x86_64_state_free(out);return false;}return true;
}
static void mapping_encode(uint8_t bytes[44],const qa_native_guest_mapping *mapping)
{
    qa_store_u64le(bytes,mapping->id);qa_store_u64le(bytes+8,mapping->base);qa_store_u64le(bytes+16,mapping->bytes);
    qa_store_u64le(bytes+24,mapping->backing);qa_store_u64le(bytes+32,mapping->backing_offset);qa_store_u32le(bytes+40,mapping->permissions);
}
static qa_native_guest_mapping mapping_decode(const uint8_t bytes[44])
{
    return (qa_native_guest_mapping){.id=qa_load_u64le(bytes),.base=qa_load_u64le(bytes+8),.bytes=qa_load_u64le(bytes+16),
        .backing=qa_load_u64le(bytes+24),.backing_offset=qa_load_u64le(bytes+32),.permissions=qa_load_u32le(bytes+40)};
}
static bool usable(const guest_host_child *child)
{return child && child->descriptor>=0 && !child->failed && (!child->running || child->callback_depth);}
static bool request(guest_host_child *child,uint32_t operation,qa_bytes bytes,int descriptor,qa_error *error)
{
    if(!usable(child) || child->sequence==UINT64_MAX) return fail(error,QA_ERROR_ARGUMENT,0,"native child request requires its stopped retained owner");
    uint64_t sequence=++child->sequence;host_packet packet;
    if(!packet_send(child->descriptor,operation,0,sequence,bytes,descriptor,error) || !packet_receive(child->descriptor,&packet,error)) {child->failed=true;return false;}
    bool okay=packet.operation==(operation|HOST_REPLY) && packet.sequence==sequence && packet.descriptor<0 && !packet.status && !packet.body.size;
    if(!okay) {
        qa_status status=packet.operation==(operation|HOST_REPLY) && packet.sequence==sequence &&
            packet.status>QA_OK && packet.status<=QA_ERROR_NOT_FOUND ? (qa_status)packet.status : QA_ERROR_FORMAT;
        qa_error_set(error,status,(size_t)sequence,"native child rejected operation: %.*s",
            packet.body.size>INT_MAX?INT_MAX:(int)packet.body.size,packet.body.data?(char *)packet.body.data:"");
        child->failed=true;
    }
    packet_free(&packet);return okay;
}

static bool interest_change(guest_profile_interest **rows,size_t *count,size_t *capacity,
    const guest_profile_interest *interest,bool remove,qa_error *error)
{
    if(!interest || !interest->id || (!remove && (!interest->address ||
        (interest->kind==GUEST_PROFILE_INTEREST_STORE ? !interest->bytes ||
            interest->bytes>UINT64_MAX-interest->address :
         interest->kind!=GUEST_PROFILE_INTEREST_INSTRUCTION || interest->bytes))))
        return fail(error,QA_ERROR_ARGUMENT,0,"native observation interest is invalid");
    size_t index=*count;
    for(size_t i=0;i<*count;++i)if((*rows)[i].kind==interest->kind && (*rows)[i].id==interest->id)index=i;
    if(remove) {
        if(index==*count)return fail(error,QA_ERROR_NOT_FOUND,interest->id,"native observation interest is absent");
        if((*rows)[index].references>1) {--(*rows)[index].references;return true;}
        memmove(*rows+index,*rows+index+1,(*count-index-1)*sizeof(**rows));--*count;return true;
    }
    if(index!=*count) {
        guest_profile_interest *old=*rows+index;
        if(old->address!=interest->address || old->bytes!=interest->bytes || old->references==SIZE_MAX)
            return fail(error,QA_ERROR_ARGUMENT,interest->id,"native observation interest changes its admitted span");
        ++old->references;return true;
    }
    if(!grow((void **)rows,capacity,*count+1,sizeof(**rows),error))return false;
    (*rows)[*count]=*interest;(*rows)[(*count)++].references=1;return true;
}
static host_server_backing *server_backing(host_server *server,uint64_t id)
{for(size_t i=0;i<server->backing_count;++i) if(server->backings[i].view.id==id)return server->backings+i;return NULL;}
static bool server_profile(host_server *server,guest_profile_guard_operation operation,
    guest_host_x86_64_state *cpu,qa_error *error)
{
    guest_profile_guard_control control={.operation=(uint32_t)operation,.scope=server->run_sequence,
        .entry=cpu?cpu->instruction:0,.stop=server->stop,.fs_base=cpu?cpu->fs_base:0,
        .gs_base=cpu?cpu->gs_base:0,.syscalls=server->syscalls,.fault=&server->profile_fault,
        .interests=server->interests,.interest_count=server->interest_count,.bypass=server->bypass,
        .xsave=cpu?cpu->xsave.data:NULL,.xsave_bytes=cpu?cpu->xsave.size:0};
    if(operation==GUEST_PROFILE_GUARD_ENTER || operation==GUEST_PROFILE_GUARD_RESUME) {
        control.mappings=server->mappings;control.mapping_count=server->mapping_count;
        control.callbacks=server->callbacks;
        control.callback_count=server->callback_count;
        memset(&server->profile_fault,0,sizeof(server->profile_fault));
    }
    bool okay=guest_profile_guard_control_call(&control,error);
    if(okay && operation==GUEST_PROFILE_GUARD_PROBE)server->profile=control.installed;
    return okay;
}
static bool server_dispatch(host_server *,host_packet *,bool *,qa_error *);
static bool server_stop(void *context,int number,void *opaque,guest_host_x86_64_state *state,bool *resume,qa_error *error)
{
    host_server *server=context;siginfo_t *information=opaque;
    if(!server_profile(server,GUEST_PROFILE_GUARD_CAPTURE,state,error) ||
        !guest_profile_cpu_current(&server->domain,&server->capability,state,error))return false;
    guest_host_stop_kind kind=GUEST_HOST_STOP_FAULT;uint64_t id=0,address=(uint64_t)(uintptr_t)information->si_addr;
    bool guarded=server->profile_fault.kind!=GUEST_PROFILE_GUARD_NO_FAULT;
    if(guarded) {
        bool entry=server->profile_fault.kind==GUEST_PROFILE_GUARD_ENTRY;
        bool syscall=server->profile_fault.kind==GUEST_PROFILE_GUARD_SYSCALL;
        bool returned=server->profile_fault.kind==GUEST_PROFILE_GUARD_RETURN;
        bool observed=server->profile_fault.kind==GUEST_PROFILE_GUARD_STORE ||
            server->profile_fault.kind==GUEST_PROFILE_GUARD_BOUNDARY;
        if(server->profile_fault.scope!=server->run_sequence ||
            ((entry || syscall || observed || returned) ? number!=SIGTRAP || state->instruction!=(uint64_t)(uintptr_t)qa_guest_profile_import+1 :
                number!=SIGILL || state->instruction!=(uint64_t)(uintptr_t)qa_guest_profile_fault)) {
            qa_error_set(error,QA_ERROR_FORMAT,(size_t)server->run_sequence,
                "instruction rejection lost its stopped CPU: signal %d at %llx, kind %u, scope %llu/%llu, import %llx",
                number,(unsigned long long)state->instruction,(unsigned)server->profile_fault.kind,
                (unsigned long long)server->profile_fault.scope,(unsigned long long)server->run_sequence,
                (unsigned long long)(uintptr_t)qa_guest_profile_import);
            return false;
        }
        /* Only the observed PC is projected back from the real monitor. All
         * other registers and FP bytes came from the actual signal bridge. */
        state->instruction=observed ? server->profile_fault.continuation : server->profile_fault.instruction;
        if(observed)kind=GUEST_HOST_STOP_OBSERVATION;
        address=server->profile_fault.address;
        if(returned) {
            if(!server->stop || state->instruction!=server->stop || address!=server->stop ||
                server->profile_fault.bytes!=1 || server->profile_fault.access)
                return fail(error,QA_ERROR_FORMAT,state->instruction,"native return lost its admitted stop trap");
            kind=GUEST_HOST_STOP_RETURN;
        }
        if(syscall) {
            if(!server->syscalls || server->profile_fault.bytes!=2 ||
                state->instruction>UINT64_MAX-2 || address!=state->instruction+2)
                return fail(error,QA_ERROR_FORMAT,state->instruction,"syscall lost its actual entered instruction receipt");
            kind=GUEST_HOST_STOP_SYSCALL;
        }
        if(entry) {
            for(size_t i=0;i<server->callback_count;++i) if(server->callbacks[i].address==address) {
                kind=GUEST_HOST_STOP_IMPORT;id=server->callbacks[i].id;break;
            }
            if(!id)return fail(error,QA_ERROR_FORMAT,address,"guarded entry lost its actual bound callback identity");
        }
    }
    if(!guarded && number==SIGTRAP && (information->si_code==TRAP_BRKPT || information->si_code==SI_KERNEL) && state->instruction) {
        uint64_t entry=state->instruction-1;
        if(entry==server->stop) {kind=GUEST_HOST_STOP_RETURN;address=entry;state->instruction=entry;}
        else for(size_t i=0;i<server->callback_count;++i) if(server->callbacks[i].address==entry) {
            kind=GUEST_HOST_STOP_IMPORT;id=server->callbacks[i].id;address=entry;state->instruction=entry;break;
        }
    }
    guest_host_x86_64_state_free(&server->state);
    if(!guest_host_x86_64_state_copy(state,&server->state,error)) return false;
    qa_buffer hardware={0};if(!state_encode(state,&hardware,error)) return false;
    qa_buffer body={.size=84+hardware.size};body.data=malloc(body.size);
    if(!body.data) {qa_buffer_free(&hardware);return fail(error,QA_ERROR_MEMORY,0,"retaining native stop receipt");}
    qa_store_u32le(body.data,(uint32_t)kind);qa_store_u32le(body.data+4,(uint32_t)number);
    qa_store_u32le(body.data+8,(uint32_t)information->si_code);
    uint32_t access=0;
    if(guarded)access=server->profile_fault.access;
    else if(number==SIGSEGV || number==SIGBUS) {
        /* Linux x86 supplies its actual page-fault error in the signal context;
         * si_addr alone cannot distinguish read, write and instruction fetch. */
        extern uint64_t qa_host_bridge_fault_error;
        access=(qa_host_bridge_fault_error&16)?QA_NATIVE_GUEST_EXECUTE:(qa_host_bridge_fault_error&2)?QA_NATIVE_GUEST_WRITE:QA_NATIVE_GUEST_READ;
    }
    qa_store_u32le(body.data+12,access);qa_store_u64le(body.data+16,id);qa_store_u64le(body.data+24,address);
    qa_store_u32le(body.data+32,(uint32_t)server->profile_fault.kind);
    qa_store_u32le(body.data+36,server->profile_fault.access);qa_store_u32le(body.data+40,server->profile_fault.vector);
    qa_store_u64le(body.data+44,server->profile_fault.scope);qa_store_u64le(body.data+52,server->profile_fault.instruction);
    qa_store_u64le(body.data+60,server->profile_fault.address);qa_store_u64le(body.data+68,server->profile_fault.bytes);
    qa_store_u64le(body.data+76,server->profile_fault.continuation);
    memcpy(body.data+84,hardware.data,hardware.size);qa_buffer_free(&hardware);
    bool okay=packet_send(server->descriptor,HOST_STOP,0,server->run_sequence,(qa_bytes){body.data,body.size},-1,error);
    qa_buffer_free(&body);if(!okay)return false;
    if(kind==GUEST_HOST_STOP_FAULT) {server->failed=true;*resume=false;return true;}
    if(kind==GUEST_HOST_STOP_RETURN) {*resume=false;return true;}
    server->stopped=true;
    while(okay) {
        host_packet packet;if(!packet_receive(server->descriptor,&packet,error))return false;
        if(packet.operation==HOST_FINISH) {
            okay=kind==GUEST_HOST_STOP_SYSCALL && server->syscalls &&
                packet.sequence==server->run_sequence && packet.descriptor<0 && !packet.status && !packet.body.size;
            packet_free(&packet);
            if(okay) {*resume=false;server->stopped=false;return true;}
            fail(error,QA_ERROR_FORMAT,server->run_sequence,"program finish lacks its genuine stopped syscall scope");
            break;
        }
        if(packet.operation==HOST_RESUME) {
            guest_host_x86_64_state next={0};
            okay=packet.sequence==server->run_sequence && packet.descriptor<0 &&
                state_decode((qa_bytes){packet.body.data,packet.body.size},&server->capability,&next,error);
            packet_free(&packet);
            if(okay)okay=server_profile(server,GUEST_PROFILE_GUARD_RESUME,&next,error);
            if(okay)okay=guest_profile_cpu_current(&server->domain,&server->capability,&next,error);
            if(okay) {guest_host_x86_64_state_free(state);*state=next;*resume=true;server->stopped=false;return true;}
            guest_host_x86_64_state_free(&next);break;
        }
        if(packet.operation==HOST_CANCEL) {
            okay=packet.sequence==server->run_sequence && packet.descriptor<0 &&
                !packet.status && !packet.body.size;
            packet_free(&packet);
            if(okay) {*resume=false;server->stopped=false;return true;}
            fail(error,QA_ERROR_FORMAT,server->run_sequence,"cancelled invocation lost its stopped source scope");break;
        }
        if(packet.operation==HOST_ABORT) {packet_free(&packet);server->failed=true;*resume=false;server->stopped=false;return true;}
        bool completed=false;okay=server_dispatch(server,&packet,&completed,error);packet_free(&packet);
        if(server->failed || server->exit) {okay=false;break;}
    }
    server->failed=true;server->stopped=false;return false;
}
static bool server_dispatch(host_server *server,host_packet *packet,bool *completed,qa_error *error)
{
    *completed=false;qa_error failure={0};bool okay=true;const uint8_t *data=packet->body.data;size_t bytes=packet->body.size;
    switch(packet->operation) {
    case HOST_CPU_CLOCK: {
        if(bytes!=4 || packet->descriptor>=0 ||
            (qa_load_u32le(data)!=2 && qa_load_u32le(data)!=3)) {
            okay=fail(&failure,QA_ERROR_FORMAT,0,"child CPU clock request is invalid");break;
        }
        struct timespec value;
        clockid_t clock=qa_load_u32le(data)==2?CLOCK_PROCESS_CPUTIME_ID:CLOCK_THREAD_CPUTIME_ID;
        if(clock_gettime(clock,&value)!=0) {
            int code=errno;
            qa_error_set(&failure,QA_ERROR_IO,(size_t)code,"reading actual child CPU clock failed: %s",strerror(code));
            okay=false;break;
        }
        long thread=syscall(SYS_gettid);
        if(thread<=0 || value.tv_sec<0 || value.tv_nsec<0 || value.tv_nsec>=1000000000L) {
            okay=fail(&failure,QA_ERROR_IO,0,"actual child CPU clock identity or value is invalid");break;
        }
        uint8_t reply[28];
        qa_store_u64le(reply,(uint64_t)getpid());qa_store_u64le(reply+8,(uint64_t)thread);
        qa_store_u64le(reply+16,(uint64_t)value.tv_sec);qa_store_u32le(reply+24,(uint32_t)value.tv_nsec);
        return packet_send(server->descriptor,HOST_CPU_CLOCK|HOST_REPLY,0,packet->sequence,(qa_bytes){reply,sizeof(reply)},-1,error);
    }
    case HOST_BACKING: {
        if(bytes!=49 || packet->descriptor<0) {okay=fail(&failure,QA_ERROR_FORMAT,0,"child backing descriptor is absent");break;}
        guest_host_backing_view view={.id=qa_load_u64le(data),.bytes={NULL,(size_t)qa_load_u64le(data+8)},.file=data[16]!=0};
        view.source.bytes=qa_load_u64le(data+17);view.source.offset=qa_load_u64le(data+25);view.source.accessible_bytes=qa_load_u64le(data+33);view.source.capability=qa_load_u64le(data+41);
        if(data[16]>1 || !view.bytes.size || (view.bytes.size&4095) || server_backing(server,view.id) ||
            !guest_host_memory_child_backing(packet->descriptor,&view,&failure) ||
            !grow((void **)&server->backings,&server->backing_capacity,server->backing_count+1,sizeof(*server->backings),&failure)) {okay=false;break;}
        server->backings[server->backing_count++]=(host_server_backing){view,packet->descriptor};packet->descriptor=-1;break;
    }
    case HOST_DROP_BACKING: {
        if(bytes!=8 || packet->descriptor>=0) {okay=fail(&failure,QA_ERROR_FORMAT,0,"child backing release is invalid");break;}
        host_server_backing *backing=server_backing(server,qa_load_u64le(data));
        if(!backing) {okay=fail(&failure,QA_ERROR_NOT_FOUND,0,"child backing is absent");break;}
        for(size_t i=0;i<server->mapping_count;++i) if(server->mappings[i].mapping.backing==backing->view.id) okay=false;
        if(!okay) {fail(&failure,QA_ERROR_ARGUMENT,backing->view.id,"child backing still owns aliases");break;}
        int result=close(backing->descriptor);backing->descriptor=-1;
        if(result!=0) {okay=fail(&failure,QA_ERROR_IO,backing->view.id,"closing actual child backing failed");break;}
        size_t index=(size_t)(backing-server->backings);
        memmove(backing,backing+1,(server->backing_count-index-1)*sizeof(*backing));--server->backing_count;break;
    }
    case HOST_MAP: {
        if(bytes!=44 || packet->descriptor>=0) {okay=fail(&failure,QA_ERROR_FORMAT,0,"child map is invalid");break;}
        qa_native_guest_mapping mapping=mapping_decode(data);host_server_backing *backing=server_backing(server,mapping.backing);
        if(!backing || mapping.backing_offset>backing->view.bytes.size || mapping.bytes>backing->view.bytes.size-mapping.backing_offset) {okay=fail(&failure,QA_ERROR_ARGUMENT,mapping.id,"child map has no actual backing span");break;}
        for(size_t i=0;i<server->mapping_count;++i) if(server->mappings[i].mapping.id==mapping.id) okay=false;
        if(!okay || !grow((void **)&server->mappings,&server->mapping_capacity,server->mapping_count+1,sizeof(*server->mappings),&failure) ||
            !guest_host_memory_child_map(backing->descriptor,&mapping,&failure)) {okay=false;break;}
        server->mappings[server->mapping_count++]=(guest_profile_guard_mapping){mapping,backing->view.file,backing->view.source.accessible_bytes};break;
    }
    case HOST_CHANGE: {
        if(bytes!=49 || data[48]>1 || packet->descriptor>=0) {okay=fail(&failure,QA_ERROR_FORMAT,0,"child mapping change is invalid");break;}
        qa_native_guest_mapping expected=mapping_decode(data);size_t index=server->mapping_count;
        for(size_t i=0;i<server->mapping_count;++i) if(server->mappings[i].mapping.id==expected.id) index=i;
        uint8_t actual[44];if(index<server->mapping_count)mapping_encode(actual,&server->mappings[index].mapping);
        if(index==server->mapping_count || memcmp(actual,data,44)) {okay=fail(&failure,QA_ERROR_ARGUMENT,expected.id,"child mapping witness was replaced");break;}
        uint32_t rights=qa_load_u32le(data+44);
        if(!guest_host_memory_child_change(&expected,rights,data[48]!=0,&failure)) {okay=false;break;}
        if(data[48]) {memmove(server->mappings+index,server->mappings+index+1,(server->mapping_count-index-1)*sizeof(*server->mappings));--server->mapping_count;}
        else server->mappings[index].mapping.permissions=rights;
        break;
    }
    case HOST_INTEREST: {
        if(bytes!=29 || data[28]>1 || packet->descriptor>=0) {okay=false;break;}
        guest_profile_interest interest={(guest_profile_interest_kind)qa_load_u32le(data),
            qa_load_u64le(data+4),qa_load_u64le(data+12),qa_load_u64le(data+20),1};
        okay=interest_change(&server->interests,&server->interest_count,&server->interest_capacity,
            &interest,data[28]!=0,&failure);break;
    }
    case HOST_BIND: {
        if(bytes!=16 || packet->descriptor>=0) {okay=fail(&failure,QA_ERROR_FORMAT,0,"child callback binding is invalid");break;}
        host_callback callback={qa_load_u64le(data),qa_load_u64le(data+8)};
        if(!callback.id || !callback.address) {okay=false;break;}
        for(size_t i=0;i<server->callback_count;++i) if(server->callbacks[i].id==callback.id || server->callbacks[i].address==callback.address) okay=false;
        if(!okay || !grow((void **)&server->callbacks,&server->callback_capacity,server->callback_count+1,sizeof(callback),&failure)) {okay=false;break;}
        server->callbacks[server->callback_count++]=callback;break;
    }
    case HOST_UNBIND: {
        if(bytes!=8 || packet->descriptor>=0) {okay=false;break;}
        size_t index=server->callback_count;for(size_t i=0;i<server->callback_count;++i) if(server->callbacks[i].id==qa_load_u64le(data)) index=i;
        if(index==server->callback_count) {okay=fail(&failure,QA_ERROR_NOT_FOUND,0,"child callback is absent");break;}
        memmove(server->callbacks+index,server->callbacks+index+1,(server->callback_count-index-1)*sizeof(*server->callbacks));--server->callback_count;break;
    }
    case HOST_RUN: {
        if(bytes<17 || data[8]>1 || packet->descriptor>=0 || server->failed) {okay=false;break;}
        guest_host_x86_64_state state={0};
        if(!state_decode((qa_bytes){data+17,bytes-17},&server->capability,&state,&failure)) {okay=false;break;}
        if(!guest_profile_cpu_current(&server->domain,&server->capability,&state,&failure)) {
            guest_host_x86_64_state_free(&state);okay=false;break;
        }
        uint64_t saved_stop=server->stop,saved_sequence=server->run_sequence,saved_bypass=server->bypass;
        bool saved_stopped=server->stopped,saved_syscalls=server->syscalls;
        server->stop=qa_load_u64le(data);server->run_sequence=packet->sequence;
        server->syscalls=data[8]!=0;server->bypass=qa_load_u64le(data+9);
        bool entered=(server->stop || server->syscalls) && server_profile(server,GUEST_PROFILE_GUARD_ENTER,&state,&failure);
        okay=entered && guest_host_x86_64_enter(&state,&failure);
        if(entered) {
            qa_error leave_error={0};
            if(!server_profile(server,GUEST_PROFILE_GUARD_LEAVE,NULL,&leave_error)) {
                if(okay)failure=leave_error;
                okay=false;
            }
        }
        guest_host_x86_64_state_free(&state);
        server->stop=saved_stop;server->run_sequence=saved_sequence;server->stopped=saved_stopped;
        server->syscalls=saved_syscalls;server->bypass=saved_bypass;
        if(!okay) server->failed=true;
        *completed=true;
        if(!okay) return reply_failure(server->descriptor,HOST_DONE,packet->sequence,&failure,error);
        return packet_send(server->descriptor,HOST_DONE,server->failed?QA_ERROR_ARGUMENT:0,packet->sequence,(qa_bytes){0},-1,error);
    }
    case HOST_EXIT: server->exit=true;break;
    default: okay=fail(&failure,QA_ERROR_FORMAT,packet->operation,"child received unknown actual command");break;
    }
    if(!okay) return reply_failure(server->descriptor,packet->operation,packet->sequence,&failure,error);
    return packet_send(server->descriptor,packet->operation|HOST_REPLY,0,packet->sequence,(qa_bytes){0},-1,error);
}

bool guest_host_child_bootstrap(int argc,char *const *argv,bool *handled,int *status,qa_error *error)
{
    if(!handled || !status || argc<0 || (argc && !argv)) return fail(error,QA_ERROR_ARGUMENT,0,"native child bootstrap requires actual startup arguments");
    *handled=argc>=2 && !strcmp(argv[1],"--qa-native-host-child");if(!*handled)return true;
    *status=125;
    if(argc!=4 || strcmp(argv[3],"--qa-native-profile-required") || !argv[2][0])
        return fail(error,QA_ERROR_UNSUPPORTED,0,"native child requires the actual configured instruction monitor");
    uint64_t descriptor=0;
    for(const char *at=argv[2];*at;++at) {if(*at<'0'||*at>'9'||descriptor>((uint64_t)INT_MAX-(unsigned)(*at-'0'))/10)return fail(error,QA_ERROR_ARGUMENT,0,"native child startup descriptor is invalid");descriptor=descriptor*10+(unsigned)(*at-'0');}
    host_server server={.descriptor=(int)descriptor};
    bool okay=server_profile(&server,GUEST_PROFILE_GUARD_PROBE,NULL,error) &&
        guest_host_x86_64_capability(&server.capability,error) && guest_host_x86_64_capture(&server.state,error) &&
        server_profile(&server,GUEST_PROFILE_GUARD_CAPTURE,&server.state,error) &&
        guest_profile_cpu_domain_read(&server.state,&server.capability,&server.domain,error) &&
        guest_host_x86_64_bridge_open(server_stop,&server,error);
    qa_buffer hardware={0},body={0};
    long thread=okay?syscall(SYS_gettid):-1;
    if(okay && thread<=0)okay=fail(error,QA_ERROR_ARGUMENT,0,"reading actual child thread identity failed");
    if(okay) okay=state_encode(&server.state,&hardware,error);
    if(okay) {body.size=44+hardware.size;body.data=malloc(body.size);
        if(!body.data)okay=fail(error,QA_ERROR_MEMORY,body.size,"owning actual child READY receipt");}
    if(okay) {
        qa_store_u64le(body.data,server.capability.xfeatures);qa_store_u32le(body.data+8,server.capability.xsave_bytes);qa_store_u32le(body.data+12,server.capability.mxcsr_mask);
        qa_store_u32le(body.data+16,server.profile.policy);
        qa_store_u64le(body.data+20,(uint64_t)getpid());qa_store_u64le(body.data+28,(uint64_t)getpgrp());
        qa_store_u64le(body.data+36,(uint64_t)thread);
        memcpy(body.data+44,hardware.data,hardware.size);okay=packet_send(server.descriptor,HOST_READY,0,0,(qa_bytes){body.data,body.size},-1,error);
    }
    qa_buffer_free(&hardware);qa_buffer_free(&body);
    if(!okay) {qa_error transport={0};reply_failure(server.descriptor,HOST_READY,0,error,&transport);}
    while(okay && !server.exit) {
        host_packet packet;if(!packet_receive(server.descriptor,&packet,error)){okay=false;break;}
        bool completed=false;okay=server_dispatch(&server,&packet,&completed,error);packet_free(&packet);
    }
    guest_host_x86_64_bridge_close();guest_host_x86_64_state_free(&server.state);
    for(size_t i=0;i<server.backing_count;++i) close(server.backings[i].descriptor);
    free(server.backings);free(server.mappings);free(server.callbacks);free(server.interests);close(server.descriptor);
    *status=okay?0:125;return okay;
}

bool guest_host_child_create(const guest_host_child_options *options,guest_host_child **out,qa_error *error)
{
    if(!options || !out || *out || !options->executable || !*options->executable ||
        !options->profile_guard || !options->profile_guard->runner || !*options->profile_guard->runner ||
        !options->profile_guard->client || !*options->profile_guard->client || options->target.arch!=QA_NATIVE_ARCH_X86_64 ||
        options->target.pointer_bytes!=8 || !((options->target.os==QA_NATIVE_OS_WINDOWS && options->target.abi==QA_NATIVE_ABI_MICROSOFT_X64) ||
        (options->target.os==QA_NATIVE_OS_LINUX && options->target.abi==QA_NATIVE_ABI_SYSTEM_V_X64)))
        return fail(error,QA_ERROR_UNSUPPORTED,0,"native child needs actual x64 Windows or System V profile and startup executable");
    int channel[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,channel)!=0) return fail(error,QA_ERROR_ARGUMENT,0,"creating actual native child channel failed");
    guest_host_child *child=calloc(1,sizeof(*child));
    if(!child) {close(channel[0]);close(channel[1]);return fail(error,QA_ERROR_MEMORY,0,"creating actual native child controller");}
    child->descriptor=channel[0];child->process=-1;child->target=options->target;
    posix_spawn_file_actions_t actions;int code=posix_spawn_file_actions_init(&actions);bool actions_initialized=code==0;
    posix_spawnattr_t attributes;bool attributes_initialized=false;
    if(!code){code=posix_spawnattr_init(&attributes);attributes_initialized=code==0;}
    if(!code)code=posix_spawnattr_setpgroup(&attributes,0);
    if(!code)code=posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);
    /* Descriptor 3 is private to the re-executed controller. dup2 clears its
     * close-on-exec bit even if the source socket already occupied slot 3. */
    if(!code)code=posix_spawn_file_actions_adddup2(&actions,channel[1],3);
    if(!code && channel[0]!=3)code=posix_spawn_file_actions_addclose(&actions,channel[0]);
    if(!code && channel[1]!=3)code=posix_spawn_file_actions_addclose(&actions,channel[1]);
    char *arguments[]={(char *)options->profile_guard->runner,"-thread_private","-translate_fpu_pc",
        "-disable_traces","-max_elide_jmp","0","-max_elide_call","0","-c",
        (char *)options->profile_guard->client,"--",(char *)options->executable,"--qa-native-host-child","3",
        "--qa-native-profile-required",NULL};pid_t process=0;
    if(!code)code=posix_spawn(&process,options->profile_guard->runner,&actions,&attributes,arguments,environ);
    if(attributes_initialized)posix_spawnattr_destroy(&attributes);
    if(actions_initialized)posix_spawn_file_actions_destroy(&actions);
    close(channel[1]);
    if(code) {close(channel[0]);free(child);return fail(error,QA_ERROR_ARGUMENT,(uint64_t)code,"re-executing actual native controller failed");}
    child->process=process;*out=child;
    host_packet ready;bool okay=guest_host_memory_create(&child->memory,error) && packet_receive(child->descriptor,&ready,error);
    if(okay) {
        if(ready.operation==(HOST_READY|HOST_REPLY) && !ready.sequence && ready.descriptor<0 && ready.status) {
            qa_status status=ready.status>QA_OK && ready.status<=QA_ERROR_NOT_FOUND ? (qa_status)ready.status : QA_ERROR_FORMAT;
            qa_error_set(error,status,0,"native child did not admit its actual source profile: %.*s",
                ready.body.size>INT_MAX?INT_MAX:(int)ready.body.size,ready.body.data?(char *)ready.body.data:"");
        }
        okay=ready.operation==HOST_READY && !ready.sequence && !ready.status && ready.descriptor<0 && ready.body.size>=44 &&
            qa_load_u64le(ready.body.data+20)==(uint64_t)process && qa_load_u64le(ready.body.data+28)==(uint64_t)process &&
            qa_load_u64le(ready.body.data+36)>0 && qa_load_u64le(ready.body.data+36)<=(uint64_t)INT_MAX;
        if(okay) {
            child->capability=(guest_host_x86_64_capabilities){qa_load_u64le(ready.body.data),qa_load_u32le(ready.body.data+8),qa_load_u32le(ready.body.data+12)};
            child->profile=(guest_profile_guard_receipt){qa_load_u32le(ready.body.data+16)};
            child->thread=qa_load_u64le(ready.body.data+36);
            okay=child->profile.policy==GUEST_PROFILE_GUARD_SOURCE_X64 &&
                state_decode((qa_bytes){ready.body.data+44,ready.body.size-44},&child->capability,&child->state,error) &&
                guest_profile_cpu_domain_read(&child->state,&child->capability,&child->domain,error);
        }
        packet_free(&ready);
    }
    if(!okay) {
        child->failed=true;
        if(!error || error->code==QA_OK)fail(error,QA_ERROR_UNSUPPORTED,0,"actual child READY identity, monitor or CPU domain was not admitted");
        return false;
    }return true;
}

bool guest_host_child_idle(const guest_host_child *child) {return child && !child->failed && !child->running && !child->callback_depth;}
const guest_host_x86_64_capabilities *guest_host_child_capability(const guest_host_child *child) {return child?&child->capability:NULL;}
bool guest_host_child_profile_read(const guest_host_child *child,guest_profile_guard_receipt *out,qa_error *error)
{
    if(!usable(child) || !out || child->profile.policy!=GUEST_PROFILE_GUARD_SOURCE_X64)
        return fail(error,QA_ERROR_ARGUMENT,0,"profile receipt needs its actual stopped native child");
    *out=child->profile;return true;
}
bool guest_host_child_source_domain(const guest_host_child *child,guest_profile_cpu_domain *out,qa_error *error)
{
    if(!usable(child) || !out)return fail(error,QA_ERROR_ARGUMENT,0,"source CPU domain requires its actual stopped child");
    *out=child->domain;return true;
}
bool guest_host_child_process_read(const guest_host_child *child,uint64_t *process,uint64_t *thread,qa_error *error)
{
    if(!usable(child) || !process || !thread || child->process<=0 || !child->thread)
        return fail(error,QA_ERROR_ARGUMENT,0,"native process identity requires its actual stopped child");
    *process=(uint64_t)child->process;*thread=child->thread;return true;
}
bool guest_host_child_cpu_clock_read(guest_host_child *child,int32_t clock,int64_t *seconds,int32_t *nanoseconds,qa_error *error)
{
    if(!usable(child) || !seconds || !nanoseconds || (clock!=2 && clock!=3) ||
        child->process<=0 || !child->thread || child->sequence==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,0,"CPU clock needs its actual stopped child and process/thread clock ID");
    uint8_t data[4];qa_store_u32le(data,(uint32_t)clock);
    uint64_t sequence=++child->sequence;host_packet packet;
    if(!packet_send(child->descriptor,HOST_CPU_CLOCK,0,sequence,(qa_bytes){data,sizeof(data)},-1,error) ||
        !packet_receive(child->descriptor,&packet,error)) {child->failed=true;return false;}
    bool matched=packet.operation==(HOST_CPU_CLOCK|HOST_REPLY) && packet.sequence==sequence && packet.descriptor<0;
    bool okay=matched && !packet.status && packet.body.size==28 &&
        qa_load_u64le(packet.body.data)==(uint64_t)child->process &&
        qa_load_u64le(packet.body.data+8)==child->thread &&
        qa_load_u64le(packet.body.data+16)<=INT64_MAX && qa_load_u32le(packet.body.data+24)<1000000000;
    if(okay) {
        *seconds=(int64_t)qa_load_u64le(packet.body.data+16);
        *nanoseconds=(int32_t)qa_load_u32le(packet.body.data+24);
    } else {
        bool native_failure=matched && packet.status==QA_ERROR_IO;
        qa_error_set(error,native_failure?QA_ERROR_IO:QA_ERROR_FORMAT,(size_t)sequence,
            "actual child CPU clock observation failed: %.*s",
            native_failure?(packet.body.size>INT_MAX?INT_MAX:(int)packet.body.size):0,
            native_failure && packet.body.data?(char *)packet.body.data:"");
        if(!native_failure)child->failed=true;
    }
    packet_free(&packet);return okay;
}
bool guest_host_child_source_initialize(guest_host_child *child,qa_error *error)
{
    if(!guest_host_child_idle(child) || child->sequence || child->source_initialized || child->state_written)
        return fail(error,QA_ERROR_ARGUMENT,0,"source CPU initialization requires the fresh child before any source or mapping transaction");
    if(!guest_profile_cpu_initialize(&child->domain,&child->capability,&child->state,error))return false;
    child->source_initialized=true;return true;
}
bool guest_host_child_destroy(guest_host_child **pointer,qa_error *error)
{
    if(!pointer || !*pointer)return true;
    guest_host_child *child=*pointer;
    if(child->running || child->callback_depth)return fail(error,QA_ERROR_ARGUMENT,0,"native child retirement requires no entered continuation");
    pid_t process=(pid_t)child->process;
    if(process>0) {
        if(kill(-process,SIGKILL)!=0 && errno!=ESRCH)return fail(error,QA_ERROR_ARGUMENT,(uint64_t)process,"stopping actual native child process group failed");
        int status;pid_t waited;do{waited=waitpid(process,&status,0);}while(waited<0 && errno==EINTR);
        if(waited<0 && errno!=ECHILD)return fail(error,QA_ERROR_ARGUMENT,(uint64_t)process,"reaping actual native child failed");
        child->process=-1;
    }
    if(child->descriptor>=0){int result=close(child->descriptor);child->descriptor=-1;if(result!=0)return fail(error,QA_ERROR_ARGUMENT,0,"closing actual native child channel failed");}
    if(!guest_host_memory_destroy(&child->memory,error))return false;
    guest_host_x86_64_state_free(&child->state);guest_host_x86_64_state_free(&child->fault.state);
    free(child->callbacks);free(child->interests);free(child);*pointer=NULL;return true;
}
bool guest_host_child_backing(guest_host_child *child,const guest_host_backing_view *view,qa_error *error)
{
    if(!usable(child) || !guest_host_memory_backing(child->memory,view,error))return false;
    guest_host_backing_view actual;int descriptor;
    if(!guest_host_memory_backing_at(child->memory,guest_host_memory_backing_count(child->memory)-1,&actual,&descriptor,error))return false;
    uint8_t data[49];qa_store_u64le(data,actual.id);qa_store_u64le(data+8,actual.bytes.size);data[16]=actual.file?1:0;
    qa_store_u64le(data+17,actual.source.bytes);qa_store_u64le(data+25,actual.source.offset);qa_store_u64le(data+33,actual.source.accessible_bytes);qa_store_u64le(data+41,actual.source.capability);
    return request(child,HOST_BACKING,(qa_bytes){data,sizeof(data)},descriptor,error);
}
size_t guest_host_child_backing_count(const guest_host_child *child) {return child?guest_host_memory_backing_count(child->memory):0;}
bool guest_host_child_backing_at(const guest_host_child *child,size_t index,guest_host_backing_view *out,qa_error *error)
{
    if(!child || (child->running && !child->callback_depth))return fail(error,QA_ERROR_ARGUMENT,index,"native backing capture requires stopped child");
    int descriptor;return guest_host_memory_backing_at(child->memory,index,out,&descriptor,error);
}
bool guest_host_child_backing_remove(guest_host_child *child,uint64_t id,qa_error *error)
{
    uint8_t data[8];qa_store_u64le(data,id);
    if(!request(child,HOST_DROP_BACKING,(qa_bytes){data,8},-1,error))return false;
    if(!guest_host_memory_remove_backing(child->memory,id,error)){child->failed=true;return false;}return true;
}
bool guest_host_child_map(guest_host_child *child,const qa_native_guest_mapping *mapping,qa_error *error)
{
    if(!usable(child) || !guest_host_memory_map(child->memory,mapping,error))return false;
    uint8_t data[44];mapping_encode(data,mapping);return request(child,HOST_MAP,(qa_bytes){data,sizeof(data)},-1,error);
}
bool guest_host_child_change(guest_host_child *child,const qa_native_guest_mapping *mapping,uint32_t rights,bool remove,qa_error *error)
{
    if(!usable(child) || !mapping)return fail(error,QA_ERROR_ARGUMENT,0,"native map mutation needs actual stopped owner");
    uint8_t data[49];mapping_encode(data,mapping);qa_store_u32le(data+44,rights);data[48]=remove?1:0;
    if(!request(child,HOST_CHANGE,(qa_bytes){data,sizeof(data)},-1,error))return false;
    if(!guest_host_memory_change(child->memory,mapping,rights,remove,error)){child->failed=true;return false;}return true;
}
bool guest_host_child_bind(guest_host_child *child,uint64_t id,uint64_t address,qa_error *error)
{
    if(!usable(child) || !id || !guest_host_memory_check(child->memory,address,1,QA_NATIVE_GUEST_EXECUTE,error))
        return fail(error,QA_ERROR_ARGUMENT,address,"native callback requires its actual owned executable entry");
    for(size_t i=0;i<child->callback_count;++i)if(child->callbacks[i].id==id || child->callbacks[i].address==address)return fail(error,QA_ERROR_ARGUMENT,id,"native callback repeats physical identity");
    if(!grow((void **)&child->callbacks,&child->callback_capacity,child->callback_count+1,sizeof(*child->callbacks),error))return false;
    uint8_t data[16];qa_store_u64le(data,id);qa_store_u64le(data+8,address);
    if(!request(child,HOST_BIND,(qa_bytes){data,16},-1,error))return false;
    child->callbacks[child->callback_count++]=(host_callback){id,address};return true;
}
bool guest_host_child_unbind(guest_host_child *child,uint64_t id,qa_error *error)
{
    if(!guest_host_child_idle(child))return fail(error,QA_ERROR_ARGUMENT,id,"native callback retirement needs idle physical child");
    size_t index=child->callback_count;for(size_t i=0;i<child->callback_count;++i)if(child->callbacks[i].id==id)index=i;
    if(index==child->callback_count)return fail(error,QA_ERROR_NOT_FOUND,id,"native callback is absent");
    uint8_t data[8];qa_store_u64le(data,id);if(!request(child,HOST_UNBIND,(qa_bytes){data,8},-1,error))return false;
    memmove(child->callbacks+index,child->callbacks+index+1,(child->callback_count-index-1)*sizeof(*child->callbacks));--child->callback_count;return true;
}
bool guest_host_child_interest(guest_host_child *child,const guest_profile_interest *interest,
    bool remove,qa_error *error)
{
    if(!usable(child) || !interest)return fail(error,QA_ERROR_ARGUMENT,0,"native observation change requires its stopped owner");
    uint8_t wire[29];qa_store_u32le(wire,(uint32_t)interest->kind);qa_store_u64le(wire+4,interest->id);
    qa_store_u64le(wire+12,interest->address);qa_store_u64le(wire+20,interest->bytes);wire[28]=(uint8_t)remove;
    return request(child,HOST_INTEREST,(qa_bytes){wire,sizeof(wire)},-1,error) &&
        interest_change(&child->interests,&child->interest_count,&child->interest_capacity,interest,remove,error);
}
bool guest_host_child_cpu_read(const guest_host_child *child,guest_host_x86_64_state *out,qa_error *error)
{
    if(!child || (child->running && !child->callback_depth))return fail(error,QA_ERROR_ARGUMENT,0,"hardware CPU read requires its stopped continuation");
    return guest_host_x86_64_state_copy(&child->state,out,error);
}
bool guest_host_child_cpu_write(guest_host_child *child,const guest_host_x86_64_state *source,qa_error *error)
{
    if(!usable(child) || !guest_host_x86_64_state_valid(source,&child->capability,error))return false;
    guest_host_x86_64_state next={0};if(!guest_host_x86_64_state_copy(source,&next,error))return false;
    guest_host_x86_64_state_free(&child->state);child->state=next;child->state_written=true;return true;
}
bool guest_host_child_read(const guest_host_child *child,uint64_t address,void *data,size_t bytes,qa_error *error)
{
    if(!child || (child->running && !child->callback_depth))return fail(error,QA_ERROR_ARGUMENT,address,"native memory read requires stopped physical child");
    return guest_host_memory_read(child->memory,address,data,bytes,error);
}
bool guest_host_child_write(guest_host_child *child,uint64_t address,qa_bytes bytes,qa_error *error)
{
    if(!usable(child))return fail(error,QA_ERROR_ARGUMENT,address,"native memory write requires stopped physical child");
    return guest_host_memory_write(child->memory,address,bytes,error);
}

static bool child_run(guest_host_child *child,uint64_t start,uint64_t stop,
    guest_host_import_fn invoke,guest_host_syscall_fn syscall,void *context,const guest_host_observation *observation,bool *program_stopped,
    bool *entered,qa_error *error)
{
    if(program_stopped)*program_stopped=false;
    if(entered)*entered=false;
    uint8_t trap;
    if(!usable(child) || !start || (!stop && !syscall) || !invoke || child->running==UINT_MAX || child->sequence==UINT64_MAX ||
        !guest_host_memory_check(child->memory,start,1,QA_NATIVE_GUEST_EXECUTE,error) ||
        (stop && (!guest_host_memory_check(child->memory,stop,1,QA_NATIVE_GUEST_READ|QA_NATIVE_GUEST_EXECUTE,error) ||
        !guest_host_memory_read(child->memory,stop,&trap,1,error) || trap!=0xcc)))
        return fail(error,QA_ERROR_ARGUMENT,start,"native invocation requires genuine owned return trap and stopped callbacks");
    if(!guest_profile_cpu_current(&child->domain,&child->capability,&child->state,error))return false;
    child->state.instruction=start;qa_buffer hardware={0},body={0};
    if(!state_encode(&child->state,&hardware,error))return false;
    body.size=17+hardware.size;body.data=malloc(body.size);
    if(!body.data){qa_buffer_free(&hardware);return fail(error,QA_ERROR_MEMORY,start,"retaining native invocation packet");}
    qa_store_u64le(body.data,stop);body.data[8]=syscall!=NULL;
    qa_store_u64le(body.data+9,observation?observation->bypass:0);
    memcpy(body.data+17,hardware.data,hardware.size);qa_buffer_free(&hardware);
    uint64_t sequence=++child->sequence;++child->running;
    bool okay=packet_send(child->descriptor,HOST_RUN,0,sequence,(qa_bytes){body.data,body.size},-1,error);qa_buffer_free(&body);
    bool returned=false;
    while(okay) {
        host_packet packet;if(!packet_receive(child->descriptor,&packet,error)){okay=false;break;}
        if(packet.sequence!=sequence || packet.descriptor>=0) {packet_free(&packet);okay=fail(error,QA_ERROR_FORMAT,sequence,"native stop belongs to a different entered continuation");break;}
        if(packet.operation==(HOST_RUN|HOST_REPLY) && packet.status) {
            qa_status status=packet.status>QA_OK && packet.status<=QA_ERROR_NOT_FOUND ? (qa_status)packet.status : QA_ERROR_FORMAT;
            qa_error_set(error,status,(size_t)start,"native invocation was not admitted: %.*s",
                packet.body.size>INT_MAX?INT_MAX:(int)packet.body.size,packet.body.data?(char *)packet.body.data:"");
            packet_free(&packet);okay=false;break;
        }
        if(packet.operation==HOST_DONE || packet.operation==(HOST_DONE|HOST_REPLY)) {
            okay=returned && packet.operation==HOST_DONE && !packet.status && !packet.body.size;
            if(!okay) {
                qa_status status=packet.status>QA_OK && packet.status<=QA_ERROR_NOT_FOUND ? (qa_status)packet.status : QA_ERROR_FORMAT;
                qa_error_set(error,status,(size_t)start,"native execution terminated before actual return: %.*s",
                    packet.body.size>INT_MAX?INT_MAX:(int)packet.body.size,packet.body.data?(char *)packet.body.data:"");
            }
            packet_free(&packet);break;
        }
        if(returned || packet.operation!=HOST_STOP || packet.status || packet.body.size<84) {packet_free(&packet);okay=fail(error,QA_ERROR_FORMAT,sequence,"native execution produced an invalid stopped receipt");break;}
        guest_host_stop receipt={.kind=(guest_host_stop_kind)qa_load_u32le(packet.body.data),.signal_number=(int)qa_load_u32le(packet.body.data+4),
            .signal_code=(int)qa_load_u32le(packet.body.data+8),.access=qa_load_u32le(packet.body.data+12),
            .callback_id=qa_load_u64le(packet.body.data+16),.address=qa_load_u64le(packet.body.data+24),
            .profile={(guest_profile_guard_fault_kind)qa_load_u32le(packet.body.data+32),
                qa_load_u32le(packet.body.data+36),qa_load_u32le(packet.body.data+40),
                qa_load_u64le(packet.body.data+44),qa_load_u64le(packet.body.data+52),
                qa_load_u64le(packet.body.data+60),qa_load_u64le(packet.body.data+68),qa_load_u64le(packet.body.data+76)}};
        okay=receipt.kind<=GUEST_HOST_STOP_OBSERVATION && receipt.profile.kind<=GUEST_PROFILE_GUARD_RETURN &&
            !(receipt.profile.access&~7u) &&
            (receipt.profile.kind==GUEST_PROFILE_GUARD_NO_FAULT ||
                (receipt.profile.scope==sequence &&
                    (receipt.profile.kind==GUEST_PROFILE_GUARD_ENTRY ? receipt.kind==GUEST_HOST_STOP_IMPORT :
                        receipt.profile.kind==GUEST_PROFILE_GUARD_SYSCALL ? receipt.kind==GUEST_HOST_STOP_SYSCALL :
                        receipt.profile.kind==GUEST_PROFILE_GUARD_RETURN ? receipt.kind==GUEST_HOST_STOP_RETURN :
                        (receipt.profile.kind==GUEST_PROFILE_GUARD_STORE || receipt.profile.kind==GUEST_PROFILE_GUARD_BOUNDARY) ?
                            receipt.kind==GUEST_HOST_STOP_OBSERVATION : receipt.kind==GUEST_HOST_STOP_FAULT))) &&
            (receipt.kind!=GUEST_HOST_STOP_SYSCALL || (syscall && receipt.profile.kind==GUEST_PROFILE_GUARD_SYSCALL &&
                receipt.profile.bytes==2 && receipt.profile.instruction<=UINT64_MAX-2 &&
                receipt.address==receipt.profile.instruction+2 && !receipt.callback_id)) &&
            state_decode((qa_bytes){packet.body.data+84,packet.body.size-84},&child->capability,&receipt.state,error) &&
            guest_profile_cpu_current(&child->domain,&child->capability,&receipt.state,error);
        packet_free(&packet);if(!okay){guest_host_x86_64_state_free(&receipt.state);break;}
        guest_host_x86_64_state_free(&child->state);child->state=receipt.state;memset(&receipt.state,0,sizeof(receipt.state));
        if(receipt.kind==GUEST_HOST_STOP_RETURN) {
            returned=receipt.address==stop && child->state.instruction==stop;
            if(returned && entered)*entered=true;
            continue;
        }
        if(receipt.kind==GUEST_HOST_STOP_FAULT) {
            if(entered)*entered=true;
            guest_host_x86_64_state_free(&child->fault.state);child->fault=receipt;
            if(guest_host_x86_64_state_copy(&child->state,&child->fault.state,error))child->has_fault=true;
            qa_error_set(error,receipt.profile.kind==GUEST_PROFILE_GUARD_INSTRUCTION?QA_ERROR_UNSUPPORTED:QA_ERROR_ARGUMENT,
                (size_t)(receipt.profile.kind?receipt.profile.instruction:receipt.address),
                "actual %s stopped its continuation: signal %d/%d at %llx, address %llx, profile %u",
                receipt.profile.kind?"instruction monitor":"native CPU",receipt.signal_number,receipt.signal_code,
                (unsigned long long)child->state.instruction,(unsigned long long)receipt.address,
                (unsigned)receipt.profile.kind);
            /* Drain the actual completed run reply before whole-child teardown. */
            host_packet done;if(packet_receive(child->descriptor,&done,error))packet_free(&done);
            okay=false;break;
        }
        if(receipt.kind==GUEST_HOST_STOP_SYSCALL) {
            if(child->callback_depth==UINT_MAX || child->state.instruction!=receipt.profile.instruction) {
                okay=fail(error,QA_ERROR_FORMAT,receipt.address,"native syscall lost its stopped source CPU");break;
            }
            if(entered)*entered=true;
            guest_host_syscall call={.instruction=child->state.instruction,.next_instruction=receipt.address,
                .number=child->state.registers[0],.arguments={child->state.registers[7],child->state.registers[6],
                    child->state.registers[2],child->state.registers[10],child->state.registers[8],child->state.registers[9]}};
            uint64_t flags=child->state.flags;
            guest_host_x86_64_state entry_state={0};
            if(!guest_host_x86_64_state_copy(&child->state,&entry_state,error)) {
                qa_error ignored={0};packet_send(child->descriptor,HOST_ABORT,0,sequence,(qa_bytes){0},-1,&ignored);
                okay=false;break;
            }
            guest_host_syscall_result result={0};++child->callback_depth;
            okay=syscall(context,child,&call,&result,error);--child->callback_depth;
            if(okay && child->failed)okay=fail(error,QA_ERROR_ARGUMENT,call.instruction,"syscall service lost its actual child owner");
            if(okay)okay=guest_profile_cpu_current(&child->domain,&child->capability,&child->state,error);
            if(okay && result.stop) {
                okay=packet_send(child->descriptor,HOST_FINISH,0,sequence,(qa_bytes){0},-1,error);
                if(okay) {
                    guest_host_x86_64_state_free(&entry_state);
                    returned=true;*program_stopped=true;continue;
                }
            } else if(okay) {
                child->state.registers[0]=(uint64_t)result.value;
                child->state.registers[1]=call.next_instruction;child->state.registers[11]=flags;
                child->state.instruction=call.next_instruction;
                qa_buffer resume={0};okay=state_encode(&child->state,&resume,error);
                if(okay)okay=packet_send(child->descriptor,HOST_RESUME,0,sequence,(qa_bytes){resume.data,resume.size},-1,error);
                qa_buffer_free(&resume);
                if(okay) {guest_host_x86_64_state_free(&entry_state);continue;}
            }
            /* A rejected service has no architectural syscall return. Retain
             * its actual entry CPU, while committed memory/file effects stay
             * with their real owners for terminal teardown. */
            guest_host_x86_64_state_free(&child->state);child->state=entry_state;
            qa_error ignored={0};packet_send(child->descriptor,HOST_ABORT,0,sequence,(qa_bytes){0},-1,&ignored);break;
        }
        bool bound=false;for(size_t i=0;i<child->callback_count;++i)
            if(child->callbacks[i].id==receipt.callback_id && child->callbacks[i].address==receipt.address)bound=true;
        bool observed=receipt.kind==GUEST_HOST_STOP_OBSERVATION;
        if((observed ? !observation || !observation->invoke ||
            child->state.instruction!=receipt.profile.continuation : !bound) || child->callback_depth==UINT_MAX) {okay=fail(error,QA_ERROR_FORMAT,receipt.callback_id,"native stopped callback lost physical registry identity");break;}
        if(entered)*entered=true;
        ++child->callback_depth;uint64_t entry=child->state.instruction;
        okay=observed ? observation->invoke(observation->context,child,&receipt.profile,error) :
            invoke(context,child,receipt.callback_id,&child->state,error);--child->callback_depth;
        if(okay && (child->failed || (!observed && child->state.instruction==entry)))okay=fail(error,QA_ERROR_ARGUMENT,entry,"native callback did not produce its real ABI continuation");
        if(!okay && observation && observation->cancelled &&
            observation->cancelled(observation->context,error)) {
            qa_error transfer_error={0};host_packet done;
            bool cancelled=packet_send(child->descriptor,HOST_CANCEL,0,sequence,(qa_bytes){0},-1,&transfer_error) &&
                packet_receive(child->descriptor,&done,&transfer_error);
            if(cancelled) {
                cancelled=done.operation==HOST_DONE && done.sequence==sequence && !done.status && !done.body.size;
                if(!cancelled)qa_error_set(&transfer_error,QA_ERROR_FORMAT,(size_t)sequence,
                    "cancelled native invocation returned operation %u, sequence %llu, status %u and %zu bytes",
                    done.operation,(unsigned long long)done.sequence,done.status,done.body.size);
                packet_free(&done);
            }
            if(cancelled) { --child->running;return false; }
            if(error)*error=transfer_error;
        }
        if(okay)okay=guest_profile_cpu_current(&child->domain,&child->capability,&child->state,error);
        qa_buffer resume={0};if(okay)okay=state_encode(&child->state,&resume,error);
        if(okay)okay=packet_send(child->descriptor,HOST_RESUME,0,sequence,(qa_bytes){resume.data,resume.size},-1,error);
        qa_buffer_free(&resume);
        if(!okay) {qa_error ignored={0};packet_send(child->descriptor,HOST_ABORT,0,sequence,(qa_bytes){0},-1,&ignored);break;}
    }
    --child->running;if(!okay)child->failed=true;return okay;
}
bool guest_host_child_run(guest_host_child *child,uint64_t start,uint64_t stop,
    guest_host_import_fn invoke,void *context,qa_error *error)
{
    bool entered;
    return guest_host_child_run_receipt(child,start,stop,invoke,context,NULL,&entered,error);
}
bool guest_host_child_run_receipt(guest_host_child *child,uint64_t start,uint64_t stop,
    guest_host_import_fn invoke,void *context,const guest_host_observation *observation,bool *entered,qa_error *error)
{
    if(!entered)return fail(error,QA_ERROR_ARGUMENT,start,"native invocation requires its entered output receipt");
    return child_run(child,start,stop,invoke,NULL,context,observation,NULL,entered,error);
}
bool guest_host_child_run_with_syscalls(guest_host_child *child,uint64_t start,uint64_t stop,
    guest_host_import_fn invoke,guest_host_syscall_fn syscall,void *context,const guest_host_observation *observation,bool *program_stopped,qa_error *error)
{
    if(!syscall || !program_stopped)return fail(error,QA_ERROR_ARGUMENT,start,"program invocation requires its actual syscall and stop owner");
    if(!child || child->target.os!=QA_NATIVE_OS_LINUX || child->target.abi!=QA_NATIVE_ABI_SYSTEM_V_X64)
        return fail(error,QA_ERROR_UNSUPPORTED,start,"Linux syscall invocation requires its actual System V source target");
    return child_run(child,start,stop,invoke,syscall,context,observation,program_stopped,NULL,error);
}
bool guest_host_child_last_fault(const guest_host_child *child,guest_host_stop *out,qa_error *error)
{
    if(!child || !child->has_fault || !out || out->state.xsave.data)return fail(error,QA_ERROR_NOT_FOUND,0,"native owner has no actual retained CPU fault");
    *out=child->fault;memset(&out->state,0,sizeof(out->state));return guest_host_x86_64_state_copy(&child->fault.state,&out->state,error);
}
#else
bool guest_host_child_create(const guest_host_child_options *options,guest_host_child **out,qa_error *error)
{(void)options;(void)out;return fail(error,QA_ERROR_UNSUPPORTED,0,"native child platform is not Linux x86-64");}
bool guest_host_child_bootstrap(int argc,char *const *argv,bool *handled,int *status,qa_error *error)
{(void)argc;(void)argv;(void)error;if(handled)*handled=false;if(status)*status=0;return handled&&status;}
bool guest_host_child_idle(const guest_host_child *child){(void)child;return false;}
const guest_host_x86_64_capabilities *guest_host_child_capability(const guest_host_child *child){(void)child;return NULL;}
bool guest_host_child_profile_read(const guest_host_child *child,guest_profile_guard_receipt *out,qa_error *error)
{(void)child;(void)out;return fail(error,QA_ERROR_UNSUPPORTED,0,"native instruction monitor platform is unavailable");}
bool guest_host_child_source_domain(const guest_host_child *child,guest_profile_cpu_domain *out,qa_error *error)
{(void)child;(void)out;return fail(error,QA_ERROR_UNSUPPORTED,0,"native source CPU domain platform is unavailable");}
bool guest_host_child_process_read(const guest_host_child *child,uint64_t *process,uint64_t *thread,qa_error *error)
{(void)child;(void)process;(void)thread;return fail(error,QA_ERROR_UNSUPPORTED,0,"native child process identity platform is unavailable");}
bool guest_host_child_cpu_clock_read(guest_host_child *child,int32_t clock,int64_t *seconds,int32_t *nanoseconds,qa_error *error)
{(void)child;(void)clock;(void)seconds;(void)nanoseconds;return fail(error,QA_ERROR_UNSUPPORTED,0,"native child CPU clock platform is unavailable");}
bool guest_host_child_source_initialize(guest_host_child *child,qa_error *error)
{(void)child;return fail(error,QA_ERROR_UNSUPPORTED,0,"native source CPU initialization platform is unavailable");}
bool guest_host_child_destroy(guest_host_child **owner,qa_error *error)
{return (!owner || !*owner)||fail(error,QA_ERROR_UNSUPPORTED,0,"native child platform is unavailable");}
#define HOST_UNAVAILABLE(name,args,unused) bool name args {unused;return fail(error,QA_ERROR_UNSUPPORTED,0,"native child platform is unavailable");}
HOST_UNAVAILABLE(guest_host_child_backing,(guest_host_child *c,const guest_host_backing_view *v,qa_error *error),(void)c;(void)v)
size_t guest_host_child_backing_count(const guest_host_child *c){(void)c;return 0;}
HOST_UNAVAILABLE(guest_host_child_backing_at,(const guest_host_child *c,size_t i,guest_host_backing_view *v,qa_error *error),(void)c;(void)i;(void)v)
HOST_UNAVAILABLE(guest_host_child_backing_remove,(guest_host_child *c,uint64_t i,qa_error *error),(void)c;(void)i)
HOST_UNAVAILABLE(guest_host_child_map,(guest_host_child *c,const qa_native_guest_mapping *m,qa_error *error),(void)c;(void)m)
HOST_UNAVAILABLE(guest_host_child_change,(guest_host_child *c,const qa_native_guest_mapping *m,uint32_t p,bool r,qa_error *error),(void)c;(void)m;(void)p;(void)r)
HOST_UNAVAILABLE(guest_host_child_bind,(guest_host_child *c,uint64_t i,uint64_t a,qa_error *error),(void)c;(void)i;(void)a)
HOST_UNAVAILABLE(guest_host_child_unbind,(guest_host_child *c,uint64_t i,qa_error *error),(void)c;(void)i)
HOST_UNAVAILABLE(guest_host_child_interest,(guest_host_child *c,const guest_profile_interest *i,bool r,qa_error *error),(void)c;(void)i;(void)r)
HOST_UNAVAILABLE(guest_host_child_cpu_read,(const guest_host_child *c,guest_host_x86_64_state *s,qa_error *error),(void)c;(void)s)
HOST_UNAVAILABLE(guest_host_child_cpu_write,(guest_host_child *c,const guest_host_x86_64_state *s,qa_error *error),(void)c;(void)s)
HOST_UNAVAILABLE(guest_host_child_read,(const guest_host_child *c,uint64_t a,void *d,size_t n,qa_error *error),(void)c;(void)a;(void)d;(void)n)
HOST_UNAVAILABLE(guest_host_child_write,(guest_host_child *c,uint64_t a,qa_bytes b,qa_error *error),(void)c;(void)a;(void)b)
HOST_UNAVAILABLE(guest_host_child_run,(guest_host_child *c,uint64_t a,uint64_t b,guest_host_import_fn f,void *x,qa_error *error),(void)c;(void)a;(void)b;(void)f;(void)x)
bool guest_host_child_run_receipt(guest_host_child *c,uint64_t a,uint64_t b,guest_host_import_fn f,
    void *x,const guest_host_observation *o,bool *entered,qa_error *error)
{
    (void)c;(void)a;(void)b;(void)f;(void)x;(void)o;
    if(entered)*entered=false;
    return fail(error,QA_ERROR_UNSUPPORTED,0,"native child platform is unavailable");
}
HOST_UNAVAILABLE(guest_host_child_run_with_syscalls,(guest_host_child *c,uint64_t a,uint64_t b,guest_host_import_fn f,guest_host_syscall_fn s,void *x,const guest_host_observation *o,bool *stopped,qa_error *error),(void)c;(void)a;(void)b;(void)f;(void)s;(void)x;(void)o;(void)stopped)
HOST_UNAVAILABLE(guest_host_child_last_fault,(const guest_host_child *c,guest_host_stop *s,qa_error *error),(void)c;(void)s)
#undef HOST_UNAVAILABLE
#endif
