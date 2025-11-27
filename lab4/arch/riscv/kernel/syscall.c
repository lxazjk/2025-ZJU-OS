#include "syscall.h"

#include "task_manager.h"
#include "stdio.h"
#include "defs.h"


struct ret_info syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    // DONE: implement syscall function
    struct ret_info ret;
    ret.a0 = 0;
    ret.a1 = 0;
    
    switch (syscall_num) {
    case SYS_WRITE: {
        // arg0: fd, arg1: buf, arg2: count
        unsigned int fd = (unsigned int) arg0;
        char *buf = (char *) arg1;
        size_t count = (size_t) arg2;

        if (fd == 1) {
            for (size_t i = 0; i < count; i++) {
                putchar(buf[i]);
            }
            ret.a0 = count; // 返回 written 的字节数
        } else {
            ret.a0 = -1;
        }
        break;
    }
    case SYS_GETPID: {
        ret.a0 = getpid();
        break;
    }
    default:
        printf("Unknown syscall! syscall_num = %d\n", syscall_num);
        while(1);
        break;
    }
    return ret;
}