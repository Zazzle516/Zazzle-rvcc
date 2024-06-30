#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    // 首先是对异常状态的判断
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    // 对合法状态进行处理
    printf("  .global main\n");
    printf("main:\n");

    // commit[1]
    // printf("  li a0, %d\n", atoi(argv[1]));

    // commit[2]
    
    // 对输入进行转换 
    char* input_ptr = argv[1];      // 使用指针指向输入的第一个元素     从 index = 1 开始(不要记错
    
    // 有个很严重的运算错误 确实是想到了输入是不定长的所以用指针 但是用于处理的参数使用错了 atol/atoi 只能处理定长的单字符串

    // long first_num = atol(input_ptr++);
    long first_num = strtol(input_ptr, &input_ptr, 10);     // 此时 input_ptr = first operator
    printf("  li a0, %ld\n", first_num);

    // 当输入没有结束的时候 其实写成递归也行
    while(*input_ptr) {
        // 逻辑错误 不应该每次从 li 开始 循环开始后只能出现 addi
        // long first_num = atol(input_ptr++);
        // printf("  li a0, %ld\n", first_num);        

        char operator = *input_ptr;
        input_ptr ++;
        long second_num = 0;

        // 在这个循环中没有显式的写出对非法输入的处理 比如++
        // 隐含在对数字的处理中 如果是合法输入 那么在符号后一定是数字 数字在处理后如果扔未结束必定是符号 回到循环开始
        // 通过内部的 continue 跳过 err 情况    如果执行到 err 那么一定发生了问题
        if (operator == '+') {
            second_num = strtol(input_ptr, &input_ptr, 10);
            printf("  addi a0, a0, %ld\n", second_num);
            continue;
        }

        if (operator == '-') {
            second_num = strtol(input_ptr, &input_ptr, 10);
            printf("  addi a0, a0, -%ld\n", second_num);
            continue;
        }

        // 执行到这里前两个分支匹配失败
        fprintf(stderr, "Wrong syntax input\n");
        return 1;
    }

    // 考虑到终结情况 比如 1-1-1- 转化为 1-1-1-0    虽然功能上没问题 但是应该报错 Warning
    printf("  ret\n");
    return 0;
}