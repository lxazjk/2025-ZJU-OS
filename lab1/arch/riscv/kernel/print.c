#include "defs.h"
#include "print.h"

extern struct sbiret sbi_call(uint64_t ext, uint64_t fid, uint64_t arg0,
                              uint64_t arg1, uint64_t arg2, uint64_t arg3,
                              uint64_t arg4, uint64_t arg5);


#define putchar(ch) sbi_call(1, 0, ch, 0, 0, 0, 0, 0);

int puts(char *str) {
  // TODO
  int i = 0;
  while(*str) {
    putchar(*str++);
  }
  return 1;
}

int put_num(uint64_t n) {
  // TODO
  char str[30];
  int i = 0;
  if(n == 0) {
    putchar('0'); return 1;
  }
  while(n) {
    str[i ++ ] = n % 10 + '0';
    n /= 10;
  }
  for(int j = i - 1; j >= 0; j--) putchar(str[j]);
  return 1;
}