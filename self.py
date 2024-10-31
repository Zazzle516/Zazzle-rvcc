#!/usr/bin/python3

# 正则表达式
import re
import sys

# Q: 为什么存在重复声明 ???

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

// 定义在 sys/stat.h 中的结构体  存储文件的各种属性信息
// 核心函数 stat() 获取文件的状态信息
struct stat {
    char _[512];
};

// glob_t 定义在 glob.h 中的结构体  用于存储匹配结果
// glob.h 库很简单  只是对文件名匹配的支持  核心函数只有 glob() globfree()
typedef struct {
    size_t g1_pathc;    // 匹配的文件路径数量 eg. argc
    char** g1_pathv;    // 匹配到的路径名列表 eg. argv
    size_t g1_offs;

    char _[512];
} glob_t;

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
int close(int fd);

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

// Tip: 这两个函数都是直接修改参数  并没有分配新的内存

// POSIX: libgen.h
// 提取文件路径中的文件名  如果路径为空返回 . 如果没有路径部分返回整个路径
char* basename(char* path);

// POSIX: libgen.h 获取文件路径中的目录部分
char* dirname(char* path);

// stdio.h
// 在目标字符串 s 中搜索字符 c 的最后一次出现  返回指向该位置的指针  找不到返回 NULL
// strchr(): 返回第一次出现的位置
char* strrchr(char* s, int c);

// POSIX: unisted.h
// 对参数文件删除文件名链接  成功返回 0 本质上就是引用 -1 当引用为零时 文件被真正删除
// Tip: unlink() 只能处理文件  rmdir() 处理目录
int unlink(char* pathname);

// POSIX: 创建一个唯一的临时文件  参数通过 XXX 指定文件名模板  返回 fd 进行读写操作
// Tip: 模板至少以 6 个 XXXXXX 结尾
int mkstemp(char* template);

// POSIX: unisted.h 创建新进程
int fork(void);

// POSIX: unisted.h
// 替换当前进程的内存空间 加载新程序 从新程序的 main() 函数开始执行 原有的执行状态不会被保留。
int execvp(char* file, char** argv);

// POSIX: unisted.h 立刻终止当前进程  不执行清理函数
// Tip: 通常用于中止 fork() 创建的子进程 防止共享的 fd 被影响
// 子进程刷新了文件缓冲区或关闭了文件描述符 父进程的行为可能会受到干扰 通过不清理 将后续交给父进程处理
void _exit(int code);

// POSIX: unisted.h 父进程等待子进程的终止并获取终止状态
int wait(int* wstatus);

// POSIX: stdlib.h
// 在程序退出前通过回调函数指针执行一些自定义的清除工作  LIFO
// 可以多次调用 atexit() 来注册多个清理函数
int atexit(void (*)(void));

// POSIX: glob.h
// 提供了文件名模式匹配（通配符匹配）的功能，通常用于查找符合指定模式的文件或路径

// pattern: 要匹配的模式字符串 eg. *.txt
// flags: 控制匹配行为的标志
// errfunc: 处理读取目录错误的回调函数 可以为 NULL
// pglob: 指向 glob_t 类型的指针 存储匹配结果
// return: 0 表示成功
int glob(char* pattern, int flags, void* errfn, glob_t* pglob);

// 释放 glob 存储的空间
void globfree(glob_t* pglob);

// POSIX: sys/stat.h
// pathname: 文件路径 相对或绝对路径
// statbuf: 通过该文件路径查询到的文件参数存储位置
// return: 0 表示成功
int stat(char* pathname, struct stat* statbuf);
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