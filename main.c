#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    // 合法化判断完成词法分析 + 语法分析
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    // 通过字符串指针获取用户输入
    char* input_ptr = argv[1];

    // 进入代码生成阶段
    printf("  .global main\n");
    printf("main:\n");

    // 执行 commit[1] 加载立即数的第一个操作
    // Tip: 数字是不定长的  需要 strtol() 库函数解析  转换到十进制表示
    long first_num = strtol(input_ptr, &input_ptr, 10);
    printf("  li a0, %ld\n", first_num);

    // 如果输入合法  此时指针指向第一个运算符
    while(*input_ptr) {

        char operator = *input_ptr;
        input_ptr ++;
        long second_num = 0;

        if (operator == '+') {
            second_num = strtol(input_ptr, &input_ptr, 10);
            printf("  addi a0, a0, %ld\n", second_num);
            continue;
        }

        if (operator == '-') {
            second_num = strtol(input_ptr, &input_ptr, 10);
            // Tip: RISCV 中没有减法  所以 +(负数) 替换
            printf("  addi a0, a0, -%ld\n", second_num);
            continue;
        }

        // 判断到输入不合法报错
        fprintf(stderr, "Wrong syntax input\n");
        return 1;
    }

    printf("  ret\n");
    return 0;
}