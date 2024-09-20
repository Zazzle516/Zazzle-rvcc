#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>

/* tokenize() 数据结构 & 函数定义 */

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

// 因为不确定后续的 errorHint() 使用可能有多少 %s %d 叭啦叭啦这样的参数  所以用变长参数
static void errorHint(char* errorInfo, ...) {
    // <stdarg.h> 提供的变长参数调用

    // 初始化
    va_list Info;
    va_start(Info, errorInfo);

    // 处理
    fprintf(stderr, errorInfo, Info);
    fprintf(stderr, "\n");

    // 清理
    va_end(Info);
    exit(1);
}

// 定义一个新的 Token 结点挂载到链表上
static Token* newToken(TokenKind token_kind, char* start, char* end) {
    // 在传参的时候实际上只能看到一段内存空间
    Token* currToken = calloc(1, sizeof(Token));
    currToken->token_kind = token_kind;
    currToken->place = start;
    currToken->length = end - start;
    return currToken;

    // 为了编译器的效率 这里分配的空间并没有释放
}

/* 词法分析 */
static Token* tokenize(char* input_ptr) {
    // 使用头插法定义 Token 流  HEAD 作为哨兵 Token 存在
    Token HEAD = {};
    Token* currToken = &HEAD;

    while(*input_ptr) {

        // 跳过空格 \t \n \v \f \r
        if (isspace(*input_ptr)) {
            input_ptr ++;
            continue;
        }

        // 仅处理数字
        if (isdigit(*input_ptr)) {
            // isdigit() 一次只能判断一个字符  所以后面用 strtoul() 处理不定长数字  需要记录断点计算长度
            currToken->next = newToken(TOKEN_NUM, input_ptr, input_ptr);
            currToken = currToken->next;

            const char* start = input_ptr;
            currToken->value = strtoul(input_ptr, &input_ptr, 10);
            currToken->length = input_ptr - start;
            continue;
        }

        // 仅处理运算符
        if (*input_ptr == '+' || *input_ptr == '-') {
            // 如果是运算符 目前的运算符只有一位 +/-  长度固定
            currToken->next = newToken(TOKEN_OP, input_ptr, input_ptr + 1);
            input_ptr ++;
            currToken = currToken->next;
            continue;
        }

        errorHint("Wrong Syntax: %c", *input_ptr);
    }

    // 人为为 Token 流添加终结符
    currToken->next = newToken(TOKEN_EOF, input_ptr, input_ptr);
    return HEAD.next;
}

// 指定 target 去跳过一个 token  包含了合法性判断
static Token* skip(Token* input_token, char* target) {
    if (!equal(input_token, target)) {
        errorHint("expected: %s", target);
    }
    return input_token->next;
}

// 在代码生成阶段加载立即数
static int getNumber(Token* input_token) {
    if (input_token->token_kind != TOKEN_NUM) {
        errorHint("expected number");
    }
    return input_token->value;
}

// 从字节的角度判断当前 Token 与指定 target 是否相同  因为 Token 是结构体
static bool equal(Token* input_token, char* target) {
    return memcmp(input_token->place, target, input_token->length) == 0 && target[input_token->length] == '\0';
}

/* 语法分析 + 代码生成 */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    printf("  .global main\n");
    printf("main:\n");
    
    // 得到指向输入流的指针
    char* input_ptr = argv[1];
    
    // char* => tokenize() => Token Stream
    Token* input_token = tokenize(input_ptr);
    
    int first_num = getNumber(input_token);
    printf("  li a0, %d\n", first_num);

    // 更新 token 的位置 (如果语法正确应该是符号)
    input_token = input_token->next;

    while(input_token->token_kind != TOKEN_EOF) {
        // "+"
        if (equal(input_token, "+")) {
            input_token = input_token->next;
            printf("  addi a0, a0, %d\n", getNumber(input_token));
            input_token = input_token->next;
            continue;
        }

        // "-"
        input_token = skip(input_token, "-");
        printf("  addi a0, a0, -%d\n", getNumber(input_token));
        input_token = input_token->next;
    }

    printf("  ret\n");
    return 0;
}