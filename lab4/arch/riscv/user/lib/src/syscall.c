#include "syscall.h"


struct ret_info u_syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, \
                uint64_t arg3, uint64_t arg4, uint64_t arg5){
    struct ret_info ret;
    // DONE: 完成系统调用，将syscall_num放在a7中，将参数放在a0-a5中，触发ecall，将返回值放在ret中
    asm volatile(
        // 传参
        "mv a7, %[syscall_num]\n"
        "mv a0, %[arg0]\n"
        "mv a1, %[arg1]\n"
        "mv a2, %[arg2]\n"
        "mv a3, %[arg3]\n"
        "mv a4, %[arg4]\n"
        "mv a5, %[arg5]\n"
        // 触发 environment call 异常
        "ecall\n"
        // 返回值放在 a0 和 a1 中
        "mv %[ret_a0], a0\n"
        "mv %[ret_a1], a1\n"
        : [ret_a0] "=r" (ret.a0), [ret_a1] "=r" (ret.a1)
        : [syscall_num] "r" (syscall_num), [arg0] "r" (arg0), [arg1] "r" (arg1), [arg2] "r" (arg2), [arg3] "r" (arg3), [arg4] "r" (arg4), [arg5] "r" (arg5)
        : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
    );

                  
    return ret;
}