// 宏定义: 创建一个更简洁的接口调用 assert 函数 同时自动生成用于调式输出的代码字符串
// #y: 本质上是把该变量作为宏去处理  也就是展开为字符串  字符串化 stringizing  C 的宏预处理特性之一


/* 
    int main() {
        int a = 1;

        ASSERT(1, a);
        ASSERT(5, 2 + 3);

        return 0;
    }

    // 字符串化的应该是传参本身
    assert(5, a, "a");
    assert(5, 2 + 3, "2 + 3");
*/
#define ASSERT(x, y) assert(x, y, #y)

// [69] 对未定义或未声明的函数报错
void assert(int expected, int actual, char *code);

// commit[60]: 支持函数声明
int printf();

// [107] 为全局变量处理联合体初始化
int strcmp(char *p, char *q);
int memcmp(char *p, char *q, long n);
