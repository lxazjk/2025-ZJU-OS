#include "syscall.h"

#include "task_manager.h"
#include "stdio.h"
#include "defs.h"
#include "slub.h"
#include "mm.h"


struct ret_info syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    struct ret_info ret;
    switch (syscall_num) {
    case SYS_GETPID: {
        ret.a0 = getpid();
        break;
    }
    case SYS_WRITE: {
        int fd = arg0;
        char* buffer = (char*)arg1;
        int size = arg2;
        if(fd == 1) {
            for(int i = 0; i < size; i++) {
                putchar(buffer[i]);
            }
        }
        ret.a0 = size;
        break;
    }
    case SYS_MMAP: {
        // 创建一个新的 vma 结构体 
        struct vm_area_struct *vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
        
        if (vma == NULL) {
            ret.a0 = -1;
            break;
        }

        // arg0 (addr) 和 arg1 (len)
        vma->vm_start = arg0;
        vma->vm_end = arg0 + arg1;
        vma->vm_flags = arg2; 
        // 标记这块地还没对应物理内存
        vma->mapped = 0;

        // list_add 接受两个参数：要插入的新节点，和把它插到谁后面
        // 这里我们将新节点插入到 current->mm.vm->vm_list (头节点) 的后面
        list_add(&(vma->vm_list), &(current->mm.vm->vm_list));

        // 返回分配的虚拟起始地址
        ret.a0 = arg0;        
        break;
    }
    case SYS_MUNMAP: {
        uint64_t addr = arg0;
        uint64_t len = arg1;
        struct vm_area_struct *vma;
        struct vm_area_struct *target_vma = NULL;

        // 遍历链表查找对应的 vma
        list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
            if (vma->vm_start == addr && vma->vm_end == (addr + len)) {
                target_vma = vma;
                break; // 找到了
            }
        }

        if (target_vma == NULL) {
            ret.a0 = -1;
            break;
        }

        // 如果该区域已经被映射过（分配过物理页），则需要释放物理页并解除映射
        if (target_vma->mapped) {
            // 获取当前进程页表的根物理地址
            // satp 的低 44 位是 PPN，左移 12 位得到物理地址
            uint64_t *pgtbl = (uint64_t *)((current->satp & 0xFFFFFFFFFFF) << 12);
            
            // 逐页遍历
            for (uint64_t va = addr; va < addr + len; va += 4096) { 
                // 获取该虚拟地址对应的页表项值
                uint64_t pte_val = get_pte(pgtbl, va);
                // 检查 PTE 是否有效
                if (pte_val & 0x1) {
                    // 从 PTE 中提取物理页号 (PPN)，位移得到物理地址
                    uint64_t pa = (pte_val >> 10) << 12;
                    // 归还物理内存
                    free_pages(pa);
                    // 使用 create_mapping 将该页映射为空
                    create_mapping(pgtbl, va, 0, 4096, 0);
                }
            }
        }

        // 从链表中删除该 vma 节点
        list_del(&(target_vma->vm_list));

        // 释放 vma 结构体本身占用的内核内存
        kfree(target_vma);

        ret.a0 = 0;
        asm volatile ("sfence.vma");
        break;
    }
    default:
        printf("Unknown syscall! syscall_num = %d\n", syscall_num);
        while(1);
        break;
    }
    return ret;
}