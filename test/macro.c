int assert(int expected, int actual, char *code);
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);

// [159] 支持 '#' 和 '/* */' 这样的标识
// 为后续的宏处理进行语法支持 
#

/* */ #

int main() {
  printf("OK\n");
  return 0;
}
