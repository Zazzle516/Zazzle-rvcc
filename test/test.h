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