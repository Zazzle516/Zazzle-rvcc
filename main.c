#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    // 合法化判断完成词法分析 + 语法分析
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    // 进入代码生成阶段
    printf("  .global main\n");
    printf("main:\n");
    printf("  li a0, %d\n", atoi(argv[1]));
    printf("  ret\n");
    return 0;
}