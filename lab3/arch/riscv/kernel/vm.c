#include "vm.h"
#include "sched.h"
#include "stdio.h"
#include "test.h"

extern uint64_t text_start;
extern uint64_t rodata_start;
extern uint64_t data_start;
extern uint64_t _end;
extern uint64_t user_program_start;

uint64_t alloc_page() {
  // 从 &_end 开始分配一个页面，返回该页面的首地址
  // 注意：alloc_page始终返回物理地址
  // set the page to zero
  uint64_t ptr = PHYSICAL_ADDR((uint64_t)(&_end) + PAGE_SIZE * alloc_page_num++);
  for(int i = 0; i < PAGE_SIZE; i++) {
    ((char *)(ptr))[i] = 0;
  }
  return ptr;
}

int alloced_page_num(){
  // 返回已经分配的物理页面的数量
  return alloc_page_num;
}

/*

38        30 29        21 20        12 11                           0
-----------------------------------------------------------------------
|   VPN[2]   |   VPN[1]   |   VPN[0]   |          page offset         |
-----------------------------------------------------------------------
                    Sv39 virtual address
*/

bool check(uint64_t PTE) {
  return !(PTE & PTE_V);
}
uint64_t trans(uint64_t* pgtb) {
  return (((uint64_t)pgtb >> 12) << 10) | PTE_V;
}
uint64_t* back(uint64_t PTE) {
  return (uint64_t*)((PTE >> 10) << 12);
}
void create_mapping(uint64_t *pgtbl, uint64_t va, uint64_t pa, uint64_t sz,
                    int perm) {
  // pgtbl 为根页表的基地址
  // va，pa 分别为需要映射的虚拟、物理地址的基地址
  // sz 为映射的大小，单位为字节
  // perm 为映射的读写权限

  // pgtbl 是根页表的首地址，它已经占用了一页的大小，不需要再分配
  // （调用create_mapping前需要先分配好根页表）

  // 1. 通过 va 得到一级页表项的索引
  // 2. 通过 va 得到二级页表项的索引
  // 3. 通过 va 得到三级页表项的索引
  // 4. 如果一级页表项不存在，分配一个二级页表
  // 5. 设置一级页表项的内容
  // 6. 如果二级页表项不存在，分配一个三级页表
  // 7. 设置二级页表项的内容
  // 8. 设置三级页表项的内容

  // DONE: 请完成你的代码
  while(sz > 0) {
    uint64_t VPN[3] = {(va >> 12) & (0x1ff), (va >> 21) & (0x1ff), (va >> 30) & (0x1ff)};
    uint64_t PTE[3] = {0};
    uint64_t *pgtb2, *pgtb3;
    PTE[2] = pgtbl[VPN[2]];
    if(check(PTE[2])) {
      pgtb2 = (uint64_t*)alloc_page();
      pgtbl[VPN[2]]  = trans(pgtb2);
    } else {
      pgtb2 = back(PTE[2]);
    }
    
    PTE[1] = pgtb2[VPN[1]];
    if(check(PTE[1])) {
      pgtb3 = (uint64_t*)alloc_page();
      pgtb2[VPN[1]] = trans(pgtb3);
    } else {
      pgtb3 = back(PTE[1]);
    }

    pgtb3[VPN[0]] = ((pa >> 12) << 10) | (PTE_V | (perm));

    sz -= PAGE_SIZE;
    va += PAGE_SIZE;
    pa += PAGE_SIZE;
  }
}

void paging_init() { 

  uint64_t *pgtbl = (uint64_t *)alloc_page();
  create_mapping(pgtbl, 0xffffffc000000000, 0x80000000, 16 * 1024 * 1024, PTE_V | PTE_R | PTE_W | PTE_X);
  create_mapping(pgtbl, 0x80000000, 0x80000000, 16 * 1024 * 1024, PTE_V | PTE_R | PTE_W | PTE_X);
  create_mapping(pgtbl, 0x10000000, 0x10000000, 1024 * 1024, PTE_V | PTE_R | PTE_W);
  create_mapping(pgtbl, (uint64_t)&text_start, (uint64_t)&text_start,  (uint64_t)&rodata_start - (uint64_t)&text_start, PTE_R | PTE_X);
  create_mapping(pgtbl, (uint64_t)&rodata_start, (uint64_t)&rodata_start, (uint64_t)&data_start - (uint64_t)&rodata_start, PTE_R);           
  create_mapping(pgtbl, (uint64_t)&data_start, (uint64_t)&data_start, (uint64_t)&user_program_start - (uint64_t)&data_start, PTE_R | PTE_W); 
}