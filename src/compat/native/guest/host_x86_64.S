#if defined(__linux__) && defined(__x86_64__)
    .text
    .globl qa_host_bridge_capture
    .type qa_host_bridge_capture,@function
qa_host_bridge_capture:
    movq %rax,0(%rdi)
    movq %rcx,8(%rdi)
    movq %rdx,16(%rdi)
    movq %rbx,24(%rdi)
    leaq 8(%rsp),%rax
    movq %rax,32(%rdi)
    movq %rbp,40(%rdi)
    movq %rsi,48(%rdi)
    movq %rdi,56(%rdi)
    movq %r8,64(%rdi)
    movq %r9,72(%rdi)
    movq %r10,80(%rdi)
    movq %r11,88(%rdi)
    movq %r12,96(%rdi)
    movq %r13,104(%rdi)
    movq %r14,112(%rdi)
    movq %r15,120(%rdi)
    movq (%rsp),%rax
    movq %rax,128(%rdi)
    pushfq
    popq %rax
    movq %rax,136(%rdi)
    movq 168(%rdi),%rax
    movq %rax,%rdx
    shrq $32,%rdx
    movq 160(%rdi),%rdi
    xsave64 (%rdi)
    ret
    .size qa_host_bridge_capture,.-qa_host_bridge_capture
    .globl qa_host_bridge_enter
    .type qa_host_bridge_enter,@function
qa_host_bridge_enter:
    movq qa_host_bridge_frame(%rip),%r10
    movq %rsp,176(%r10)
    movq %rbx,184(%r10)
    movq %rbp,192(%r10)
    movq %r12,200(%r10)
    movq %r13,208(%r10)
    movq %r14,216(%r10)
    movq %r15,224(%r10)
    pushfq
    popq 280(%r10)
    movq 168(%r10),%rax
    movq %rax,%rdx
    shrq $32,%rdx
    movq 232(%r10),%rdi
    xsave64 (%rdi)
    movq 160(%r10),%rdi
    xrstor64 (%rdi)
    movl $158,%eax
    movl $0x1002,%edi
    movq 144(%r10),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    movl $158,%eax
    movl $0x1001,%edi
    movq 152(%r10),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    pushq 136(%r10)
    popfq
    movq 0(%r10),%rax
    movq 8(%r10),%rcx
    movq 16(%r10),%rdx
    movq 24(%r10),%rbx
    movq 40(%r10),%rbp
    movq 48(%r10),%rsi
    movq 56(%r10),%rdi
    movq 64(%r10),%r8
    movq 72(%r10),%r9
    movq 88(%r10),%r11
    movq 96(%r10),%r12
    movq 104(%r10),%r13
    movq 112(%r10),%r14
    movq 120(%r10),%r15
    movq 32(%r10),%rsp
    movq 80(%r10),%r10
    jmp *qa_host_bridge_target(%rip)
    .size qa_host_bridge_enter,.-qa_host_bridge_enter

    .globl qa_host_bridge_return
    .type qa_host_bridge_return,@function
qa_host_bridge_return:
    movq qa_host_bridge_frame(%rip),%r10
    movq 168(%r10),%rax
    movq %rax,%rdx
    shrq $32,%rdx
    movq 232(%r10),%rdi
    xrstor64 (%rdi)
    movq 176(%r10),%rsp
    movq 184(%r10),%rbx
    movq 192(%r10),%rbp
    movq 200(%r10),%r12
    movq 208(%r10),%r13
    movq 216(%r10),%r14
    movq 224(%r10),%r15
    cld
    ret
    .size qa_host_bridge_return,.-qa_host_bridge_return

    .globl qa_host_bridge_signal
    .type qa_host_bridge_signal,@function
qa_host_bridge_signal:
    pushq %r12
    pushq %rdi
    pushq %rsi
    pushq %rdx
    subq $8,%rsp
    movq qa_host_bridge_frame(%rip),%r12
    testq %r12,%r12
    jz .Lhost_fatal
    movl $158,%eax
    movl $0x1003,%edi
    leaq 256(%r12),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    movl $158,%eax
    movl $0x1004,%edi
    leaq 264(%r12),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    movl $158,%eax
    movl $0x1002,%edi
    movq 240(%r12),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    movl $158,%eax
    movl $0x1001,%edi
    movq 248(%r12),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    pushq 280(%r12)
    popfq
    movq 168(%r12),%rax
    movq %rax,%rdx
    shrq $32,%rdx
    movq 232(%r12),%rdi
    xrstor64 (%rdi)
    movq 24(%rsp),%rdi
    movq 16(%rsp),%rsi
    movq 8(%rsp),%rdx
    call qa_host_bridge_dispatch
    movl $158,%eax
    movl $0x1002,%edi
    movq 256(%r12),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    movl $158,%eax
    movl $0x1001,%edi
    movq 264(%r12),%rsi
    syscall
    testq %rax,%rax
    js .Lhost_fatal
    addq $8,%rsp
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r12
    ret
.Lhost_fatal:
    movl $231,%eax
    movl $125,%edi
    syscall
    ud2
    .size qa_host_bridge_signal,.-qa_host_bridge_signal
    .section .note.GNU-stack,"",@progbits
#endif
