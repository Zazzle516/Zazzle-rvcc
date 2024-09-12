#include "test.h"

int main() {
  // [62] 修正解析复杂类型声明
  ASSERT(1, ({ char x; sizeof(x); }));
  ASSERT(2, ({ short int x; sizeof(x); }));
  ASSERT(2, ({ int short x; sizeof(x); }));
  ASSERT(4, ({ int x; sizeof(x); }));
  ASSERT(8, ({ long int x; sizeof(x); }));
  ASSERT(8, ({ int long x; sizeof(x); }));

// short 和 long 本质上不是类型声明 而是类型修饰符 用来修饰 int
// 所以平时的 short x; 定义实际上是 short int x; 的简写  short int 才是原本的使用方式

  // [63] 支持long long
  ASSERT(8, ({ long long x; sizeof(x); }));

  printf("OK\n");
  return 0;
}
