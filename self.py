#!/usr/bin/python3

# 正则表达式
import re
import sys

print("""
typedef signed char     int8_t;
typedef short           int16_t;
typedef int             int32_t;
typedef long            int64_t;
typedef unsigned long   size_t;

typedef unsigned char   uint8_t;
typedef unsigned short  uint16_t;
typedef unsigned int    uint32_t;
typedef unsigned long   uint64_t;

typedef struct FILE FILE;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

struct stat {
    char _[512];
};

typedef void* va_list;
static void va_end(va_list ap) {}
extern void* __va_area__;

void* malloc(long size);
void* calloc(long nmemb, long size);
void* realloc(void* buf, long size);

int* __errno_location();
char* strerror(int errnum);

FILE* fopen(char* pathname, char* mold);
FILE* fclose(FILE* fp);

FILE* open_memstream(char** ptr, size_t* sizeloc);
long fread(void* ptr, long size, long nmemb, FILE* stream);
size_t fwrite(void* ptr, size_t size, size_t nmemb, FILE* stream);
int fflush(FILE* stream);
int fputc(int c, FILE* stream);
int feof(FILE* stream);

static void assert() {}

int strcmp(char* s1, char* s2);
int strncmp(char* p, char* q, long n);
int strncasecmp(char* s1, char* s2, long n);
char* strdup(char* p);
char* strndup(char* p, long n);

int printf(char* fmt, ...);
int sprintf(char* buf, char* fmt, ...);
int fprintf(FILE* fp, char* fmt, ...);
int vfprintf(FILE* fp, char* fmt, va_list ap);

long strlen(char* p);

int memcmp(char* s1, char* s2, long n);
void* memcpy(char* dest, char* src, long n);

int isspace(int c);
int ispunct(int c);
int isdigit(int c);
int isxdigit(int c);
int isprint(int);

char* strstr(char* haystack, char* needle);
char* strchr(char* s, int c);
double strtod(char* nptr, char** endptr);
long strtoul(char* nptr, char** endptr, int base);

void exit(int code);
double log2(double);
""")

for Path in sys.argv[1:]:
    with open(Path) as File:
        fileContent = File.read()

        # 使用指定的替换内容替换字符串中与正则表达式匹配的部分，可以指定替换的次数
        # count: 替换的次数，默认全部替换
        # \b: 单词边界 确保匹配的是一个完整单词  前后都需要添加
        # re.sub(pattern, repl, string, count=0, flags=0)

        # 把行连续处理为一行
        # eg. {int a = \ 5;} =>  {int a = 5;}
        fileContent = re.sub(r'\\\n', '', fileContent)

        # 删除所有的注释行和预处理指令
        # 所有的宏处理中的函数调用都预先通过 self.py 处理  目前 rvcc 不支持宏预处理
        # \s*: 匹配 0 或 多个空白符
        # #.*: 匹配所有宏定义
        # flags=re.MULTILINE  使正则表达式在多行模式下工作  通过 '^' 匹配每一行的开头
        fileContent = re.sub(r'^\s*#.*', '', fileContent, flags=re.MULTILINE)

        # 删除内容中双引号之间的换行符和空白字符的部分
        fileContent = re.sub(r'"\n\s*"', '', fileContent)

        # 把所有的 'bool' 替换为 rvcc 可以解析的 _Bool 写法
        fileContent = re.sub(r'\bbool\b', '_Bool', fileContent)

        # 把所有的 'errno' 替换为 *__errno_location()
        # errno: 局部线程概念  表示最近一次系统调用或者库函数发生的错误
        # errno: 本质是一个语法糖  在 POSIX 标准下会被解释为一个宏  __errno_location()
        # 因为要在多线程环境下保证每个线程有自己的 errno 所以不能单纯的解释为一个全局变量
        fileContent = re.sub(r'\berrno\b', '*__errno_location()', fileContent)

        # 把 true 重写为 1
        fileContent = re.sub(r'\btrue\b', '1', fileContent)

        # 把 false 重写为 0
        fileContent = re.sub(r'\bfalse\b', '0', fileContent)

        # 把 NULL 重写为 0
        fileContent = re.sub(r'\bNULL\b', '0', fileContent)

# Q: 后面的不太懂...
        # 把 va_list 替换为 va_area
        fileContent = re.sub(r'\bva_list ([a-zA-Z]+)[ ]*;', 'va_list \\1 = __va_area__;', fileContent)

        # 删除 va_start
        fileContent = re.sub(r'\bva_start\(([^)]*), ([^)]*)\);', '', fileContent)

        # 替换 unreachable
        fileContent = re.sub(r'\bunreachable\(\)', 'errorHint("unreachable")', fileContent)

        fileContent = re.sub(r'\bMIN\(([^)]*), ([^)]*)\)', '((\\1)<(\\2)?(\\1):(\\2))', fileContent)

        print(fileContent)