#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    // 首先是对异常状态的判断
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    // 对合法状态进行处理
    printf("  .global main\n");
    printf("main:\n");
    printf("  li a0, %d\n", atoi(argv[1]));
    printf("  ret\n");
    return 0;
}