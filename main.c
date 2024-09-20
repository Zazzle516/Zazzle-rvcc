#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>

/* 工具函数 */

static void errorHint(char* errorInfo, ...) {
    va_list Info;
    va_start(Info, errorInfo);

    fprintf(stderr, errorInfo, Info);
    fprintf(stderr, "\n");

    va_end(Info);
    exit(1);
}

// 针对输入的不合法情况进行提示
static void errorAt(char* place, char* FMT, va_list VA) {
    // 打印出错输入(目前的输入都是一行)
    fprintf(stderr, "%s\n", InputHEAD);

    // 计算出错位置 并通过空格跳过
    int err_place = place - InputHEAD;

    // 通过 '%*s' 表示该输出字符串长度可变
    fprintf(stderr, "%*s", err_place, "");

    // 打印错误标志
    fprintf(stderr, "^ ");

    // 打印错误信息
    fprintf(stderr, FMT, VA);
    fprintf(stderr, "\n");

    // 在这里统一清除变长参数
    va_end(VA);

    exit(1);
}


/* tokenize() 数据结构 & 函数定义 */

// 全局变量 记录输入流的起始位置 (方便后续找到具体的错误位置)
static char* InputHEAD;

typedef enum {
    TOKEN_EOF,              // 终结符
    TOKEN_NUM,              // 数字
    TOKEN_OP                // 运算符
} TokenKind;

typedef struct Token Token;
struct Token {
    // 从 tokenize() 输出的角度
    TokenKind token_kind;
    Token* next;

    // 从 tokenize() 输入的角度
    char* place;                // 在 input_ptr 中的位置
    unsigned int length;        // 在 input_ptr 中的长度
    int value;
};

// 在 tokenize() 阶段使用的报错函数
static void charErrorAt(char* place, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    errorAt(place, FMT, VA);
}

// 定义一个新的 Token 结点挂载到链表上
static Token* newToken(TokenKind token_kind, char* start, char* end) {
    Token* currToken = calloc(1, sizeof(Token));
    currToken->token_kind = token_kind;
    currToken->place = start;
    currToken->length = end - start;
    return currToken;
}


static Token* tokenize(char* input_ptr) {
    Token HEAD = {};
    Token* currToken = &HEAD;
    char* P = InputHEAD;

    while(*input_ptr) {
        if (isspace(*input_ptr)) {
            input_ptr ++;
            continue;
        }

        // 仅处理数字
        if (isdigit(*input_ptr)) {
            currToken->next = newToken(TOKEN_NUM, input_ptr, input_ptr);
            currToken = currToken->next;

            const char* start = input_ptr;
            currToken->value = strtoul(input_ptr, &input_ptr, 10);
            currToken->length = input_ptr - start;
            continue;
        }

        // 仅处理运算符
        if (*input_ptr == '+' || *input_ptr == '-') {
            currToken->next = newToken(TOKEN_OP, input_ptr, input_ptr + 1);
            input_ptr ++;
            currToken = currToken->next;
            continue;
        }

        charErrorAt(input_ptr, "Wrong Syntax: %c", *input_ptr);
    }

    // 添加终结符
    currToken->next = newToken(TOKEN_EOF, input_ptr, input_ptr);

    // 对直接存在的结构体调用 "成员变量"
    return HEAD.next;
}


/* 语法分析 + 代码生成 */

// 在 parse() 阶段使用的报错函数
static void tokenErrorAt(Token* token, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    // 指明该 Token 的出错位置
    errorAt(token->place, FMT, VA);
}

// 从字节的角度判断当前 Token 与指定 target 是否相同
static bool equal(Token* input_token, char* target) {
    return memcmp(input_token->place, target, input_token->length) == 0 && target[input_token->length] == '\0';
}

// 指定 target 去跳过一个 token  包含了合法性判断
static Token* skip(Token* input_token, char* target) {
    if (!equal(input_token, target)) {
        tokenErrorAt(input_token, "expected: %s", target);
    }
    return input_token->next;
}

// 在代码生成阶段加载立即数
static int getNumber(Token* input_token) {
    if (input_token->token_kind != TOKEN_NUM) {
        // errorHint("expected number");        同理根据 Token 优化错误信息
        tokenErrorAt(input_token, "expected number");
    }
    // 把在 newToken() 中构造好的值取出
    return input_token->value;
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    printf("  .global main\n");
    printf("main:\n");
    
    // 让报错函数可以访问到用户输入
    char* input_ptr = argv[1];
    InputHEAD = argv[1];
    
    Token* input_token = tokenize(input_ptr);
    
    int first_num = getNumber(input_token);
    printf("  li a0, %d\n", first_num);

    // 更新 token 的位置(如果语法正确应该是符号)
    input_token = input_token->next;

    while(input_token->token_kind != TOKEN_EOF) {
        if (equal(input_token, "+")) {
            input_token = input_token->next;
            printf("  addi a0, a0, %d\n", getNumber(input_token));
            input_token = input_token->next;
            continue;
        }

        input_token = skip(input_token, "-");
        printf("  addi a0, a0, -%d\n", getNumber(input_token));
        input_token = input_token->next;
    }

    printf("  ret\n");
    return 0;
}