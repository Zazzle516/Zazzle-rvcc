// 在 commit[8] 中修改了项目结构
// Zazzle-rvcc:
            // zacc.h   头文件  定义结构体和函数声明
            // tokenize.c   词法解析
            // parse.c      语法解析
            // codeGen.c    后端代码生成
            // main.c       主函数  调用

// Tip: 注意里面有哪些函数需要 static 修饰

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 词法解析 tokenize() 数据结构和函数声明

typedef enum {
    TOKEN_EOF,              // 终结符
    TOKEN_NUM,              // 数字
    TOKEN_OP,               // 运算符       在 commit[7] 中新增的比较符也计入 TOKEN_OP 中

    TOKEN_IDENT             // 标识     变量名 | 函数名  commit[10]
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind token_kind;
    Token* next;
    int value;                  // 只针对数字符号  因为超过 10 的数字大小要额外通过 value 记录

    char* place;                // 在 input_ptr 中的位置    从 equal() 层面看 存储的是具体内容
    unsigned int length;        // 该符号在 input_ptr 中所占长度
};

void errorHint(char* errorInfo, ...);
void errorAt(char* place, char* FMT, va_list VA);
void tokenErrorAt(Token* token, char* FMT, ...);
void charErrorAt(char* place, char* FMT, ...);
bool equal(Token* input_token, char* target);
Token* skip(Token* input_token, char* target);

Token* tokenize(char* P);       // main() 调用声明

// 语法分析 parse() 数据结构和函数声明

// 声明 AST 的节点类型
typedef enum {
    ND_NUM,
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NEG,

    // 新增比较运算符
    ND_EQ,
    ND_NEQ,
    ND_GE,      // Greater Equal
    ND_LE,      // Less Equal
    ND_GT,      // Greater Then
    ND_LT,      // Less Then

    // 新增语句分号判断
    ND_STAMT,

    // 新增赋值语句定义
    ND_ASSIGN,
    ND_VAR
}NODE_KIND;

// 定义 AST 的结点结构
typedef struct Node Node;
struct Node {
    NODE_KIND node_kind;
    int val;                // 针对 ND_NUM 记录大小
    Node* LHS;
    Node* RHS;

    // commit[9]: 添加对 ';' 多语句的支持   每个语句 exprStamt 构成一个结点   整体是一个单叉树
    Node* next;

    // commit[10]: 添加对标识符的支持
    char var_name;
};

Node* parse(Token* tok);

// 后端生成 codeGen() 数据结构和函数声明
void codeGen(Node* AST);