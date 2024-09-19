#include "test.h"

int main() {
  // [67] 支持类型转换
  ASSERT(131585, (int)8590066177);
  ASSERT(513, (short)8590066177);
  ASSERT(1, (char)8590066177);
  ASSERT(1, (long)1);
  ASSERT(0, (long)&*(int *)0);    // error
  ASSERT(513, ({ int x=512; *(char *)&x=1; x; }));
  ASSERT(5, ({ int x=5; long y=(long)&x; *(int*)y; }));

  (void)1;  // 无返回值  无法进行对比输出测试  只要通过语法解析就可以

  printf("OK\n");
  return 0;
}
