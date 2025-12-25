#include "defs.h"
#include "sched.h"
#include "test.h"
#include "syscall.h"
#include "stdio.h"
#include "task_manager.h"
#include "mm.h"
#include "vm.h"
#include "riscv.h"

void handler_s(uint64_t cause, uint64_t epc, uint64_t sp) {
  // interrupt
  if (cause >> 63 == 1) {
    // supervisor timer interrupt
    if (cause == 0x8000000000000005) {
      asm volatile("ecall");
      ticks++;
      if (ticks % 10 == 0) {
        do_timer();
      }
    }
  }
  // exception
  else if (cause >> 63 == 0) {
    // instruction page fault
    // instruction page fault
  if (cause == 0xc || cause == 0xd || cause == 0xf) {
    uint64_t stval;
    uint64_t* sp_ptr = (uint64_t*)(sp); // sp_ptr 指向栈上的 TrapContext，sp_ptr[16] 是 sepc

    // TODO: 
    // 1. get the faulting address from stval register
    // 使用内联汇编读取 CSR 寄存器 stval (S-mode Trap Value)，它保存了导致异常的虚拟地址
    asm volatile ("csrr %0, stval" : "=r"(stval));
    
    printf("Page fault! epc = 0x%016lx, stval = 0x%016lx, cause = %d\n", epc, stval, cause);

    struct vm_area_struct* vma;
    // 遍历当前进程的所有虚拟内存区域
    list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
      // 检查 stval 是否在 [vm_start, vm_end) 区间内
      if (stval >= vma->vm_start && stval < vma->vm_end) {
        
        // 0xc (12): Instruction Page Fault -> 需要可执行权限 (PTE_X)
        // 0xd (13): Load Page Fault        -> 需要可读权限 (PTE_R)
        // 0xf (15): Store/AMO Page Fault   -> 需要可写权限 (PTE_W)
        
        int permission_pass = 0;
        
        if (cause == 12 && (vma->vm_flags & PTE_X)) {
            permission_pass = 1;
        } else if (cause == 13 && (vma->vm_flags & PTE_R)) {
            permission_pass = 1;
        } else if (cause == 15 && (vma->vm_flags & PTE_W)) {
            permission_pass = 1;
        }

        if (permission_pass) {
            // 4. if valid, allocate physical pages, map it, mark mapped, and return
            
            // 计算当前进程页表的根物理地址
            uint64_t *pgtbl = (uint64_t *)((current->satp & 0xFFFFFFFFFFF) << 12);
            
            // 必须按页对齐分配。将故障地址向下取整到最近的 4KB 边界
            uint64_t va_start = stval & ~(0xFFF); 
            
            // 分配 1 个物理页
            uint64_t pa = (uint64_t)alloc_pages(1);
            
            if (pa == 0) {
                printf("Error: Out of memory in page fault handler\n");
                break; 
            }

            // 建立映射
            create_mapping(pgtbl, va_start, pa, 4096, vma->vm_flags | PTE_V | PTE_U);
            
            // 标记该区域已被映射（
            vma->mapped = 1;
            
            asm volatile ("sfence.vma");

            // 不要执行下面的 sp_ptr[16] += 4。
            return; 

        } else {
            printf("Permission denied! Cause: %d, VM Flags: 0x%lx\n", cause, vma->vm_flags);
            break; 
        }
      }
    }
    
    // 6. if faulting address is not in any vm area (loop finished)
    // OR permission check failed (break from loop)
    printf("Unhandled Page Fault at 0x%lx. Skipping instruction.\n", stval);
    sp_ptr[16] += 4; // 跳过当前指令 (sepc = sepc + 4)
    return;
  }
    // syscall from user mode
    else if (cause == 0x8) {
      // 根据我们规定的接口规范，从a7中读取系统调用号，然后从a0~a5读取参数，调用对应的系统调用处理函数，最后把返回值保存在a0~a1中。
      // 注意读取和修改的应该是保存在栈上的值，而不是寄存器中的值，因为寄存器上的值可能被更改。

      // 1. 从 a7 中读取系统调用号
      // 2. 从 a0 ~ a5 中读取系统调用参数
      // 2. 调用syscall()，并把返回值保存到 a0,a1 中
      // 3. sepc += 4，注意应该修改栈上的sepc，而不是sepc寄存器
      
      // 提示，可以用(uint64_t*)(sp)得到一个数组
      
      uint64_t* sp_ptr = (uint64_t*)(sp);
      uint64_t syscall_num = sp_ptr[11];
      uint64_t arg0 = sp_ptr[4], arg1 = sp_ptr[5], arg2 = sp_ptr[6], arg3 = sp_ptr[7], arg4 = sp_ptr[8], arg5 = sp_ptr[9];

      struct ret_info ret = syscall(syscall_num, arg0, arg1, arg2, arg3, arg4, arg5);
      sp_ptr[4] = ret.a0;
      sp_ptr[5] = ret.a1;
      sp_ptr[16] += 4;
      
    }
    else {
      printf("Unknown exception! epc = 0x%016lx\n", epc);
      while (1);
    }
  }
  return;
}
