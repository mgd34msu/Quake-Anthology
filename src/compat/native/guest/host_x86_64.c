#define _GNU_SOURCE
#include "host_x86_64.h"
#include "qa/binary.h"
#include "qa/pool.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(__x86_64__)
#include <asm/prctl.h>
#include <cpuid.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <errno.h>
#include <unistd.h>
#endif

static bool fail(qa_error *error, qa_status code, uint64_t address, const char *message)
{ qa_error_set(error,code,(size_t)address,"%s",message); return false; }

void guest_host_x86_64_state_free(guest_host_x86_64_state *state)
{ if (state) { qa_buffer_free(&state->xsave); memset(state,0,sizeof(*state)); } }

bool guest_host_x86_64_state_copy(const guest_host_x86_64_state *source,
    guest_host_x86_64_state *out, qa_error *error)
{
    if (!source || !out || (!out->xsave.data && out->xsave.size) || !source->xsave.data || source->xsave.size < 576)
        return fail(error,QA_ERROR_ARGUMENT,0,"hardware state copy needs actual complete source and output storage");
#if defined(__linux__) && defined(__x86_64__)
    void *bytes = out->xsave.data;
    if (bytes) {
        if (out->xsave.size < source->xsave.size || (uintptr_t)bytes % 64)
            return fail(error,QA_ERROR_ARGUMENT,0,"hardware state output needs full aligned XSAVE storage");
    } else if (posix_memalign(&bytes,64,source->xsave.size) != 0)
        return fail(error,QA_ERROR_MEMORY,0,"retaining aligned hardware state");
    memcpy(bytes,source->xsave.data,source->xsave.size); *out = *source; out->xsave.data = bytes; return true;
#else
    return fail(error,QA_ERROR_UNSUPPORTED,0,"hardware state copy platform is unavailable");
#endif
}

bool guest_host_x86_64_capability(guest_host_x86_64_capabilities *out, qa_error *error)
{
#if defined(__linux__) && defined(__x86_64__)
    if (!out) return fail(error,QA_ERROR_ARGUMENT,0,"hardware capability output is required");
    unsigned a,b,c,d;
    if (!__get_cpuid(1,&a,&b,&c,&d) || !(c & bit_XSAVE) || !(c & bit_OSXSAVE))
        return fail(error,QA_ERROR_UNSUPPORTED,0,"host has no OS-owned XSAVE execution context");
    uint32_t low,high; __asm__ volatile("xgetbv" : "=a"(low),"=d"(high) : "c"(0));
    uint64_t mask = ((uint64_t)high<<32)|low, permitted = 0;
    if (syscall(SYS_arch_prctl,ARCH_GET_XCOMP_PERM,&permitted) == 0) mask &= permitted;
    __cpuid_count(0x0d,0,a,b,c,d);
    if ((mask & 3) != 3 || b < 576) return fail(error,QA_ERROR_UNSUPPORTED,0,"host lacks actual x87/SSE standard XSAVE layout");
    uint64_t hardware_features=((uint64_t)d<<32)|a;
    _Alignas(16) uint8_t fxsave[512]; memset(fxsave,0,sizeof(fxsave));
    __asm__ volatile("fxsave64 %0" : "=m"(fxsave));
    uint32_t mxcsr_mask = qa_load_u32le(fxsave+28);
    *out = (guest_host_x86_64_capabilities){.xfeatures=mask,.xsave_bytes=b,.mxcsr_mask=mxcsr_mask ? mxcsr_mask : 0xffbf};
    for (unsigned bit=2;bit<64;++bit) if ((hardware_features>>bit)&1) {
        __cpuid_count(0x0d,bit,a,b,c,d);
        out->components[bit]=(guest_host_x86_64_component){.index=bit,.offset=b,.bytes=a,
            .supervisor=(c&1)!=0,.compact_align64=(c&2)!=0};
    }
    return true;
#else
    (void)out; return fail(error,QA_ERROR_UNSUPPORTED,0,"hardware guest execution requires Linux x86-64");
#endif
}

bool guest_host_x86_64_state_valid(const guest_host_x86_64_state *state,
    const guest_host_x86_64_capabilities *capability, qa_error *error)
{
    if (!state || !capability || !state->xsave.data || state->xsave.size < 576 ||
        state->xsave.size > capability->xsave_bytes || (state->xfeatures & ~capability->xfeatures) ||
        (state->flags & 0x202) != 0x202 || (state->flags & ~UINT64_C(0x250fd7)) ||
        (qa_load_u32le(state->xsave.data+24) & ~capability->mxcsr_mask) ||
        (qa_load_u64le(state->xsave.data+512) & ~state->xfeatures) || qa_load_u64le(state->xsave.data+520))
        return fail(error,QA_ERROR_ARGUMENT,0,"hardware user state differs from its actual supported processor context");
    for (size_t i = 528; i < 576; ++i) if (state->xsave.data[i]) return fail(error,QA_ERROR_ARGUMENT,i,"hardware XSAVE header has nonzero reserved state");
#if defined(__linux__) && defined(__x86_64__)
    uint64_t bits = qa_load_u64le(state->xsave.data+512);
    for (unsigned bit = 2; bit < 64; ++bit) if ((bits>>bit)&1) {
        const guest_host_x86_64_component *row=capability->components+bit;
        if (!row->bytes || row->supervisor || row->offset < 576 ||
            row->offset > state->xsave.size || row->bytes > state->xsave.size-row->offset)
            return fail(error,QA_ERROR_UNSUPPORTED,bit,"hardware component is absent from its actual user layout");
    }
#endif
    return true;
}

bool guest_host_x86_64_to_cpu(const guest_host_x86_64_state *state, qa_native_guest_cpu *out, qa_error *error)
{
    if (!state || !out || !state->xsave.data || state->xsave.size < 576)
        return fail(error,QA_ERROR_ARGUMENT,0,"hardware CPU projection requires genuine saved state");
    memset(out,0,sizeof(*out)); memcpy(out->registers,state->registers,sizeof(state->registers));
    out->instruction=state->instruction; out->flags=state->flags;
    for (size_t i = 0; i < 6; ++i) out->segments[i].selector=state->selectors[i];
    out->segments[4].base=state->fs_base; out->segments[5].base=state->gs_base;
    const uint8_t *data=state->xsave.data;
    uint64_t active=qa_load_u64le(data+512);
    out->mxcsr=qa_load_u32le(data+24);
    /* Standard XRSTOR supplies architectural init registers for inactive
     * components; transfer bytes there are not the resumed register values. */
    out->fp_control=0x37f; out->fp_tags=0xffff;
    if (active&1) {
        out->fp_control=qa_load_u16le(data); out->fp_status=qa_load_u16le(data+2); out->fp_opcode=qa_load_u16le(data+6);
        out->fp_instruction=qa_load_u64le(data+8); out->fp_operand=qa_load_u64le(data+16);
        unsigned top=(out->fp_status>>11)&7; out->fp_tags=0;
        for (unsigned i=0;i<8;++i) {
            unsigned physical=(top+i)&7; uint64_t mantissa=qa_load_u64le(data+32+i*16); uint16_t exponent=qa_load_u16le(data+40+i*16);
            out->fp_mantissa[physical]=mantissa; out->fp_exponent[physical]=exponent;
            unsigned tag = !(data[4]&(1u<<physical)) ? 3 : !(exponent&0x7fff) && !mantissa ? 1 :
                (exponent&0x7fff)==0x7fff || !(exponent&0x7fff) || !(mantissa>>63) ? 2 : 0;
            out->fp_tags |= (uint16_t)(tag<<(physical*2));
        }
    }
    if (active&2) for (size_t i=0;i<16;++i) { out->xmm[i][0]=qa_load_u64le(data+160+i*16); out->xmm[i][1]=qa_load_u64le(data+168+i*16); }
    out->xcr0=state->xfeatures; out->xstate_bv=active; return true;
}

bool guest_host_x86_64_from_cpu(const qa_native_guest_cpu *source,
    const guest_host_x86_64_state *retained, guest_host_x86_64_state *out, qa_error *error)
{
    if (!source || !retained || !out || source->fp_code_selector || source->fp_data_selector)
        return fail(error,QA_ERROR_UNSUPPORTED,0,"hardware long-mode state cannot install emulated x87 selectors");
    for (size_t i=0;i<6;++i) if (source->segments[i].selector != retained->selectors[i])
        return fail(error,QA_ERROR_UNSUPPORTED,i,"hardware segment selector differs from actual native profile");
    if (!guest_host_x86_64_state_copy(retained,out,error)) return false;
    memcpy(out->registers,source->registers,sizeof(out->registers)); out->instruction=source->instruction; out->flags=source->flags;
    out->fs_base=source->segments[4].base; out->gs_base=source->segments[5].base;
    uint8_t *data=out->xsave.data; qa_store_u16le(data,source->fp_control); qa_store_u16le(data+2,source->fp_status);
    qa_store_u16le(data+6,source->fp_opcode); qa_store_u64le(data+8,source->fp_instruction); qa_store_u64le(data+16,source->fp_operand);
    qa_store_u32le(data+24,source->mxcsr); data[4]=0;
    unsigned top=(source->fp_status>>11)&7;
    for (unsigned i=0;i<8;++i) {
        unsigned physical=(top+i)&7;
        if (((source->fp_tags>>(physical*2))&3)!=3) data[4]|=(uint8_t)(1u<<physical);
        qa_store_u64le(data+32+i*16,source->fp_mantissa[physical]); qa_store_u16le(data+40+i*16,source->fp_exponent[physical]);
    }
    for (size_t i=0;i<16;++i) { qa_store_u64le(data+160+i*16,source->xmm[i][0]); qa_store_u64le(data+168+i*16,source->xmm[i][1]); }
    qa_store_u64le(data+512,qa_load_u64le(data+512)|3); return true;
}

#if defined(__linux__) && defined(__x86_64__)
typedef struct host_bridge_frame {
    uint64_t registers[16],instruction,flags,fs_base,gs_base;
    void *guest_xsave; uint64_t xfeatures;
    uint64_t host_stack,host_rbx,host_rbp,host_r12,host_r13,host_r14,host_r15;
    void *host_xsave;
    uint64_t host_fs,host_gs,captured_fs,captured_gs;
    struct host_bridge_frame *parent;
    uint64_t host_flags;
    guest_host_x86_64_state state;
    void *signal_stack; size_t signal_stack_bytes;
    stack_t previous_stack;
    sigset_t previous_mask;
    bool okay;
    qa_error failure;
} host_bridge_frame;
_Static_assert(offsetof(host_bridge_frame,guest_xsave)==160,"hardware bridge guest XSAVE offset");
_Static_assert(offsetof(host_bridge_frame,host_stack)==176,"hardware bridge host stack offset");
_Static_assert(offsetof(host_bridge_frame,host_fs)==240,"hardware bridge host FS offset");
_Static_assert(offsetof(host_bridge_frame,captured_fs)==256,"hardware bridge captured FS offset");
_Static_assert(offsetof(host_bridge_frame,host_flags)==280,"hardware bridge host flags offset");
host_bridge_frame *qa_host_bridge_frame;
uint64_t qa_host_bridge_target;
uint64_t qa_host_bridge_fault_error;
extern void qa_host_bridge_enter(void);
extern void qa_host_bridge_return(void);
extern void qa_host_bridge_signal(int,siginfo_t *,void *);
extern void qa_host_bridge_capture(host_bridge_frame *);
static guest_host_x86_64_stop_fn bridge_stop;
static void *bridge_context;
static guest_host_x86_64_capabilities bridge_capability;
static qa_arena bridge_storage;
static qa_pool bridge_workspaces;
static size_t bridge_xsave_stride,bridge_signal_stack_bytes;
static stack_t bridge_previous_stack;
static sigset_t bridge_previous_mask;
static struct sigaction previous_actions[5];
static const int bridge_signals[]={SIGTRAP,SIGSEGV,SIGBUS,SIGILL,SIGFPE};
static size_t installed_actions;
static const int context_registers[]={REG_RAX,REG_RCX,REG_RDX,REG_RBX,REG_RSP,REG_RBP,REG_RSI,REG_RDI,
    REG_R8,REG_R9,REG_R10,REG_R11,REG_R12,REG_R13,REG_R14,REG_R15};

bool guest_host_x86_64_capture(guest_host_x86_64_state *out, qa_error *error)
{
    guest_host_x86_64_capabilities capability;
    if (!out || out->xsave.data || !guest_host_x86_64_capability(&capability,error)) return false;
    host_bridge_frame frame={0}; void *data=NULL;
    if (posix_memalign(&data,64,capability.xsave_bytes)!=0) return fail(error,QA_ERROR_MEMORY,0,"capturing real hardware floating context");
    memset(data,0,capability.xsave_bytes); frame.guest_xsave=data; frame.xfeatures=capability.xfeatures;
    qa_host_bridge_capture(&frame);
    memset(out,0,sizeof(*out)); memcpy(out->registers,frame.registers,sizeof(frame.registers));
    out->instruction=frame.instruction; out->flags=frame.flags; out->xfeatures=capability.xfeatures;
    out->xsave=(qa_buffer){data,capability.xsave_bytes};
    if (syscall(SYS_arch_prctl,ARCH_GET_FS,&out->fs_base)!=0 || syscall(SYS_arch_prctl,ARCH_GET_GS,&out->gs_base)!=0) {
        guest_host_x86_64_state_free(out); return fail(error,QA_ERROR_ARGUMENT,0,"capturing actual hardware thread bases failed");
    }
    __asm__ volatile("movw %%cs,%0; movw %%ds,%1; movw %%es,%2; movw %%ss,%3; movw %%fs,%4; movw %%gs,%5"
        : "=r"(out->selectors[0]),"=r"(out->selectors[1]),"=r"(out->selectors[2]),"=r"(out->selectors[3]),"=r"(out->selectors[4]),"=r"(out->selectors[5]));
    return true;
}

void qa_host_bridge_dispatch(int number, siginfo_t *information, void *opaque)
{
    host_bridge_frame *frame=qa_host_bridge_frame; ucontext_t *context=opaque;
    if (!frame || !bridge_stop || !context->uc_mcontext.fpregs) _exit(125);
    qa_host_bridge_fault_error=(uint64_t)context->uc_mcontext.gregs[REG_ERR];
    uint8_t *fp=(uint8_t *)context->uc_mcontext.fpregs;
    uint32_t magic=qa_load_u32le(fp+464), size=magic==0x46505853 ? qa_load_u32le(fp+480) : 512;
    uint32_t extended=magic==0x46505853 ? qa_load_u32le(fp+468) : 512;
    if (size<512 || size>bridge_capability.xsave_bytes || (magic==0x46505853 &&
        (extended<size+4 || qa_load_u32le(fp+extended-4)!=0x46505845))) _exit(125);
    for (size_t i=0;i<16;++i) frame->state.registers[i]=(uint64_t)context->uc_mcontext.gregs[context_registers[i]];
    frame->state.instruction=(uint64_t)context->uc_mcontext.gregs[REG_RIP]; frame->state.flags=(uint64_t)context->uc_mcontext.gregs[REG_EFL];
    frame->state.fs_base=frame->captured_fs; frame->state.gs_base=frame->captured_gs;
    uint64_t selectors=(uint64_t)context->uc_mcontext.gregs[REG_CSGSFS];
    frame->state.selectors[0]=(uint16_t)selectors; frame->state.selectors[4]=(uint16_t)(selectors>>32);
    frame->state.selectors[5]=(uint16_t)(selectors>>16); frame->state.selectors[3]=(uint16_t)(selectors>>48);
    __asm__ volatile("movw %%ds,%0; movw %%es,%1" : "=r"(frame->state.selectors[1]),"=r"(frame->state.selectors[2]));
    memset(frame->state.xsave.data,0,frame->state.xsave.size); memcpy(frame->state.xsave.data,fp,size);
    memset(frame->state.xsave.data+464,0,48);
    frame->state.xfeatures=magic==0x46505853 ? qa_load_u64le(fp+472) : 3;
    if (size==512) qa_store_u64le(frame->state.xsave.data+512,3);
    uint16_t original_selectors[6]; memcpy(original_selectors,frame->state.selectors,sizeof(original_selectors));
    uint64_t signal_features=frame->state.xfeatures;
    bool resume=false;
    frame->okay=bridge_stop(bridge_context,number,information,&frame->state,&resume,&frame->failure);
    if (frame->okay && resume && !guest_host_x86_64_state_valid(&frame->state,&bridge_capability,&frame->failure)) frame->okay=false;
    if (frame->okay && resume && ((frame->state.xfeatures&~signal_features) ||
        memcmp(original_selectors,frame->state.selectors,sizeof(original_selectors))))
        frame->okay=fail(&frame->failure,QA_ERROR_UNSUPPORTED,0,"resumed hardware state exceeds its actual signal or selector context");
    for (unsigned bit=2; frame->okay && resume && bit<64; ++bit) if ((qa_load_u64le(frame->state.xsave.data+512)>>bit)&1) {
        const guest_host_x86_64_component *component=bridge_capability.components+bit;
        if (component->offset>size || component->bytes>size-component->offset)
            frame->okay=fail(&frame->failure,QA_ERROR_UNSUPPORTED,bit,"resumed hardware component exceeds its actual kernel signal extent");
    }
    if (frame->okay && resume) {
        for (size_t i=0;i<16;++i) context->uc_mcontext.gregs[context_registers[i]]=(greg_t)frame->state.registers[i];
        context->uc_mcontext.gregs[REG_RIP]=(greg_t)frame->state.instruction; context->uc_mcontext.gregs[REG_EFL]=(greg_t)frame->state.flags;
        /* Kernel software words and trailer remain its genuine signal frame. */
        memcpy(fp,frame->state.xsave.data,464);
        if (size>512) memcpy(fp+512,frame->state.xsave.data+512,size-512);
        frame->captured_fs=frame->state.fs_base; frame->captured_gs=frame->state.gs_base;
    } else {
        context->uc_mcontext.gregs[REG_RIP]=(greg_t)(uintptr_t)qa_host_bridge_return;
        context->uc_mcontext.gregs[REG_EFL]=(greg_t)frame->host_flags;
        frame->captured_fs=frame->host_fs; frame->captured_gs=frame->host_gs;
    }
}

bool guest_host_x86_64_bridge_open(guest_host_x86_64_stop_fn stop, void *context,
    size_t scratch_pages, qa_error *error)
{
    if (!stop || bridge_stop || !guest_host_x86_64_capability(&bridge_capability,error)) return false;
    uint64_t shadow_stack=0;
    if(syscall(SYS_arch_prctl,0x5005,&shadow_stack)==0 && shadow_stack)
        return fail(error,QA_ERROR_UNSUPPORTED,shadow_stack,"host shadow-stack state is outside the actual native user transfer profile");
    size_t signal_bytes=(size_t)SIGSTKSZ,xsave_bytes=bridge_capability.xsave_bytes;
    bridge_xsave_stride=(xsave_bytes+63)&~(size_t)63;
    if(signal_bytes>SIZE_MAX-xsave_bytes-65536 ||
        signal_bytes+xsave_bytes+65536>SIZE_MAX-2*bridge_xsave_stride)
        return fail(error,QA_ERROR_MEMORY,0,"native bridge workspace size overflow");
    bridge_signal_stack_bytes=signal_bytes+xsave_bytes+65536;
    if(sigaltstack(NULL,&bridge_previous_stack)!=0 ||
        sigprocmask(SIG_SETMASK,NULL,&bridge_previous_mask)!=0)
        return fail(error,QA_ERROR_ARGUMENT,0,"reading actual child controller signal state failed");
    if(!qa_pool_reserve_pages(&bridge_workspaces,&bridge_storage,scratch_pages,
        2*bridge_xsave_stride+bridge_signal_stack_bytes,64,error)) {
        qa_arena_destroy(&bridge_storage);return false;
    }
    struct sigaction action={0}; action.sa_sigaction=qa_host_bridge_signal; action.sa_flags=SA_SIGINFO|SA_ONSTACK;
    sigfillset(&action.sa_mask);
    for (installed_actions=0;installed_actions<5;++installed_actions)
        if (sigaction(bridge_signals[installed_actions],&action,previous_actions+installed_actions)!=0) {
            while(installed_actions) { --installed_actions; sigaction(bridge_signals[installed_actions],previous_actions+installed_actions,NULL); }
            qa_arena_destroy(&bridge_storage);bridge_workspaces=(qa_pool){0};
            return fail(error,QA_ERROR_ARGUMENT,0,"installing actual child signal bridge failed");
        }
    bridge_stop=stop; bridge_context=context; return true;
}
void guest_host_x86_64_bridge_close(void)
{
    if (qa_host_bridge_frame) return;
    /* A failed per-entry restoration retains its lease until the controller's
     * original signal state is restored; the OS may still reference its stack. */
    if(bridge_workspaces.active && (sigprocmask(SIG_SETMASK,&bridge_previous_mask,NULL)!=0 ||
        sigaltstack(&bridge_previous_stack,NULL)!=0)) return;
    while(installed_actions) { --installed_actions; sigaction(bridge_signals[installed_actions],previous_actions+installed_actions,NULL); }
    bridge_stop=NULL; bridge_context=NULL;
    qa_arena_destroy(&bridge_storage);bridge_workspaces=(qa_pool){0};
}
bool guest_host_x86_64_enter(const guest_host_x86_64_state *source, qa_error *error)
{
    if (!bridge_stop || !guest_host_x86_64_state_valid(source,&bridge_capability,error)) return false;
    uint16_t selectors[6];
    __asm__ volatile("movw %%cs,%0; movw %%ds,%1; movw %%es,%2; movw %%ss,%3; movw %%fs,%4; movw %%gs,%5"
        : "=r"(selectors[0]),"=r"(selectors[1]),"=r"(selectors[2]),"=r"(selectors[3]),"=r"(selectors[4]),"=r"(selectors[5]));
    if (selectors[4] || selectors[5] || memcmp(selectors,source->selectors,sizeof(selectors)))
        return fail(error,QA_ERROR_UNSUPPORTED,0,"native entry selectors differ from the actual flat host profile");
    if (source->flags&UINT64_C(0x10100))
        return fail(error,QA_ERROR_UNSUPPORTED,0,"native entry cannot install trap or resume flags through its user entry bridge");
    if ((qa_load_u64le(source->xsave.data+512)>>9)&1) {
        if (qa_load_u32le(source->xsave.data+bridge_capability.components[9].offset)&3)
            return fail(error,QA_ERROR_UNSUPPORTED,0,"native entry protection key zero denies the actual bridge storage");
    }
    size_t slot;
    uint8_t *workspace=qa_pool_take(&bridge_workspaces,&slot);
    if(!workspace)return fail(error,QA_ERROR_MEMORY,0,"native bridge workspaces are exhausted");
    host_bridge_frame frame={.parent=qa_host_bridge_frame,.okay=true,
        .host_xsave=workspace+bridge_xsave_stride,
        .signal_stack=workspace+2*bridge_xsave_stride,
        .signal_stack_bytes=bridge_signal_stack_bytes};
    frame.state.xsave=(qa_buffer){workspace,bridge_capability.xsave_bytes};
    memset(frame.state.xsave.data,0,frame.state.xsave.size);
    if (!guest_host_x86_64_state_copy(source,&frame.state,error)) {
        qa_pool_release(&bridge_workspaces,slot);return false;
    }
    frame.state.xsave.size=bridge_capability.xsave_bytes;
    memset(frame.host_xsave,0,bridge_capability.xsave_bytes);
    memcpy(frame.registers,source->registers,sizeof(frame.registers)); frame.instruction=source->instruction; frame.flags=source->flags;
    frame.fs_base=source->fs_base; frame.gs_base=source->gs_base; frame.guest_xsave=frame.state.xsave.data; frame.xfeatures=bridge_capability.xfeatures;
    bool okay=syscall(SYS_arch_prctl,ARCH_GET_FS,&frame.host_fs)==0 && syscall(SYS_arch_prctl,ARCH_GET_GS,&frame.host_gs)==0;
    stack_t signal_stack={.ss_sp=frame.signal_stack,.ss_size=frame.signal_stack_bytes,.ss_flags=(int)(1u<<31)};
    bool stack_installed=false,mask_saved=false;
    if (okay) { okay=sigaltstack(&signal_stack,&frame.previous_stack)==0; stack_installed=okay; }
    sigset_t allowed;
    if (okay) { okay=sigprocmask(SIG_SETMASK,NULL,&frame.previous_mask)==0; mask_saved=okay; }
    if (okay) {
        allowed=frame.previous_mask; for(size_t i=0;i<5;++i) sigdelset(&allowed,bridge_signals[i]);
        okay=sigprocmask(SIG_SETMASK,&allowed,NULL)==0;
    }
    if (okay) { qa_host_bridge_frame=&frame; qa_host_bridge_target=frame.instruction; qa_host_bridge_enter(); qa_host_bridge_frame=frame.parent; okay=frame.okay; if (!okay && error) *error=frame.failure; }
    else qa_error_set(error,QA_ERROR_ARGUMENT,0,
        "preparing actual child stack or thread bases failed: %s",strerror(errno));
    bool restored=true;
    if (mask_saved && sigprocmask(SIG_SETMASK,&frame.previous_mask,NULL)!=0) {
        restored=false;okay=fail(error,QA_ERROR_ARGUMENT,0,"restoring child controller signal mask failed");
    }
    if (stack_installed && sigaltstack(&frame.previous_stack,NULL)!=0) {
        restored=false;okay=fail(error,QA_ERROR_ARGUMENT,0,"restoring child controller signal stack failed");
    }
    if(restored)qa_pool_release(&bridge_workspaces,slot);
    return okay;
}
#else
bool guest_host_x86_64_capture(guest_host_x86_64_state *out,qa_error *error)
{ (void)out; return fail(error,QA_ERROR_UNSUPPORTED,0,"hardware state capture platform is unavailable"); }
bool guest_host_x86_64_bridge_open(guest_host_x86_64_stop_fn stop,void *context,size_t scratch_pages,qa_error *error)
{ (void)stop;(void)context;(void)scratch_pages; return fail(error,QA_ERROR_UNSUPPORTED,0,"native child signal bridge platform is unavailable"); }
void guest_host_x86_64_bridge_close(void) { }
bool guest_host_x86_64_enter(const guest_host_x86_64_state *state,qa_error *error)
{ (void)state; return fail(error,QA_ERROR_UNSUPPORTED,0,"native child assembly platform is unavailable"); }
#endif
