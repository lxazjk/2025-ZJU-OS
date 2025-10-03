#ifndef PRINT_ONLY
#include "defs.h"
#include "clock.h"
#include "print.h"


void handler_s(uint64_t cause) {
  if (cause == 0x8000000000000005) {
    // supervisor timer interrupt
    // 设置下一个时钟中断，打印当前的中断数目。
    puts("Supervisor timer interrupt:");
    put_num(ticks);
    puts("\n");
    clock_set_next_event();
  }
}
#endif