#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>

// 全局变量
static char* InputHEAD;

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
        if (ispunct(*input_ptr)) {
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


/* parser() 数据结构 & 函数定义 */

// Tip: 此时 parse() 不完整  只有计算语句

// 结合两个方面理解  满足两个条件写入 NODE_KIND
// case1: 从 parse 输入的角度理解  可以把 TOKEN 细分到什么结点  数字 运算符
// case2: 从 parse 输出的角度理解  一定对应着可以翻译到汇编语句的操作
typedef enum {
// TOKEN_NUM——————————————加载立即数
    ND_NUM,

// TOKEN_OP———————————————执行运算操作
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV
}NODE_KIND;

// 根据结点具体的操作类型保存额外数据
typedef struct Node Node;
struct Node {
// 基本运算结点结构
    NODE_KIND node_kind;
    Node* LHS;
    Node* RHS;

// ND_NUM
    int val;

};

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


// 定义 AST 结点  (注意因为 ND_NUM 的定义不同要分开处理)

// 创造新的结点并分配空间
static Node* createNode(NODE_KIND node_kind) {
    Node* newNode = calloc(1, sizeof(Node));
    newNode->node_kind = node_kind;
    return newNode;
}

// 针对数字结点的额外的值定义
static Node* numNode(int val) {
    Node* newNode = createNode(ND_NUM);
    newNode->val = val;
    return newNode;
}

// 创建有左右子树的结点
static Node* createAST(NODE_KIND node_kind, Node* LHS, Node* RHS) {
    Node* rootNode = createNode(node_kind);
    rootNode->LHS = LHS;
    rootNode->RHS = RHS;
    return rootNode;
}

// 定义产生式关系并完成自顶向下的递归调用  每一个函数都是一个语法规则  优先级依次升高

static Node* first_class_expr(Token** rest, Token* tok);
static Node* second_class_expr(Token** rest, Token* tok);
static Node* primary_class_expr(Token** rest, Token* tok);

// Q: rest 起到了什么作用

// 加减运算     first_class_expr = first_class_expr (+|- first_class_expr)*
static Node* first_class_expr(Token** rest, Token* tok) {
    Node* ND = second_class_expr(&tok, tok);

    while(true) {
        if (equal(tok, "+")) {
            ND = createAST(ND_ADD, ND, second_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "-")) {
            ND = createAST(ND_SUB, ND, second_class_expr(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 乘除运算     second_class_expr = second_class_expr (*|/ second_class_expr)
static Node* second_class_expr(Token** rest, Token* tok) {
    Node* ND = primary_class_expr(&tok, tok);

    while(true) {
        if (equal(tok, "*")) {
            ND = createAST(ND_MUL, ND, primary_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "/")) {
            ND = createAST(ND_DIV, ND, primary_class_expr(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 括号表达式 | 立即数      primary_class_expr = '(' | ')' | num
static Node* primary_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "(")) {
        Node* ND = first_class_expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return ND;
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value);
        *rest = tok->next;
        return ND;
    }

    tokenErrorAt(tok, "expected an expr");
    return NULL;
}

// 后端生成 生成指定 ISA 的汇编代码

// 记录当前的栈深度 用于后续的运算合法性判断
static int StackDepth;

static void push_stack(void) {
    printf("  addi sp, sp, -8\n");
    printf("  sd a0, 0(sp)\n");
    StackDepth++;
}

static void pop_stack(char* reg) {
    printf("  ld %s, 0(sp)\n", reg);
    printf("  addi sp, sp, 8\n");
    StackDepth--;
}

// 根据 AST 和目标后端 RISCV-64 生成代码
static void codeGen(Node* AST) {
    if (AST->node_kind == ND_NUM) {
        printf("  li a0, %d\n", AST->val);
        return;
    }

    codeGen(AST->RHS);
    push_stack();
    codeGen(AST->LHS);
    pop_stack("a1");        // 把 LHS 的计算结果弹出

    switch (AST->node_kind)
    {
    case ND_ADD:
        printf("  add a0, a0, a1\n");
        return;
    case ND_SUB:
        printf("  sub a0, a0, a1\n");
        return;
    case ND_MUL:
        printf("  mul a0, a0, a1\n");
        return;
    case ND_DIV:
        printf("  div a0, a0, a1\n");
        return;
    default:
        errorHint("invalid expr");
    }
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    printf("  .global main\n");
    printf("main:\n");
    
    char* input_ptr = argv[1];
    InputHEAD = argv[1];

    Token* input_token = tokenize(input_ptr);

    // 在 parse() 结束后检查 token Stream 是否结束
    Node* currentAST = first_class_expr(&input_token, input_token);
    if (input_token->token_kind != TOKEN_EOF) {
        tokenErrorAt(input_token, "extra Token");
    }

    codeGen(currentAST);
    printf("  ret\n");

    if (StackDepth != 0) {
        errorHint("Wrong syntax");
    }
    return 0;
}