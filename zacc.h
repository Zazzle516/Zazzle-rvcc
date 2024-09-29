// Zazzle-rvcc:
            // zacc.h   头文件  定义结构体和函数声明
            // type.c       变量类型定义实现
            // tokenize.c   词法解析
            // parse.c      语法解析
            // codeGen.c    后端代码生成
            // main.c       程序入口

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <strings.h>

// 替换声明  在后续的变量声明中省略 struct 关键字
// Q: 为什么 typedef 可以作为一个组合识别到 (struct structName)
// A: 因为 typedef 关键字(非宏处理) 和编译器强绑定  结合 commit[64] 的实现
typedef struct structMember structMember;
typedef struct Type Type;
typedef struct Object Object;
typedef struct Node Node;

/* 词法解析 tokenize() 数据结构和函数声明 */ 

// 字符串
char* format(char* Fmt, ...);

// 在 tokenize() 阶段使用的工具函数
void errorHint(char* errorInfo, ...);
void errorAt(int errorLineNum, char* place, char* FMT, va_list VA);
void charErrorAt(char* place, char* FMT, ...);

// 从词法分析的 IO 的角度分析 Token 的可能性
typedef enum {
    TOKEN_EOF,          // 终结符
    TOKEN_NUM,          // 数字
    TOKEN_OP,           // 运算符
    TOKEN_IDENT,        // 标识

    TOKEN_KEYWORD,      // 相关语法关键字
    TOKEN_STR,          // 字符串字面量
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind token_kind;
    Token* next;
    int LineNum;        // commit[46]: 记录行号 方便报错

    int64_t value;      // commit[57]: 支持 long 类型后需要扩大
    char* place;
    unsigned int length;

    char* strContent;
    Type* tokenType;    // 本身只是记录数字和字符串的
};

Token* tokenizeFile(char* filePath);


/* 语法分析 parse() 分为声明语法和运算语法进行解析 */

/* 声明语法解析的数据结构和函数声明 */

bool equal(Token* input_token, char* target);
Token* skip(Token* input_token, char* target);
bool consume(Token** rest, Token* tok, char* str);
void tokenErrorAt(Token* token, char* FMT, ...);

// 标准类型
typedef enum {
    TY_VOID,            // commit[61]: 支持 void 类型  更新在首位方便初始化
    TY_INT,             // 整数类型变量
    TY_PTR,             // 指针

    TY_FUNC,            // commit[25]: 声明函数签名

    TY_ARRAY_LINER,     // commit[27]: 定义数组变量类型 一维数组

    TY_CHAR,            // commit[33]: 支持 char 类型

    TY_STRUCT,          // commit[49]: 结构体

    TY_UNION,           // commit[54]: 联合体定义

    TY_LONG,            // commit[57]: 基础 long 类型

    TY_SHORT,           // commit[58]: 基础 short 类型

    TY_BOOL,            // commit[72]: 基础 bool 类型

    TY_ENUM,            // commit[74]: 支持枚举类型
} Typekind;

// 自定义类型
struct structMember {
    structMember* next;
    Token* tok;             // commit[86]: 针对结构体成员的零或数组定义不支持

    Type* memberType;
    Token* memberName;
    int offset;             // 记录该成员相对于结构体的偏移量
};

// 针对不同类型额外保存的数据
struct Type {
// 基础信息(匿名)
    Typekind Kind;
    int alignSize;                  // 该类型内存对齐标准
    int BaseSize;                   // commit[27]: 类型 sizeof() 计算的返回值

// 被声明变量的基础信息 (结构体 | 函数 | ...)
    Token* Name;

// 指针 TY_PTR
    Type* Base;                     // 指针基类涉及空间计算

// 函数 TY_FUNC
    Type* ReturnType;               // commit[25]: 根据 Token 保存返回值类型
    Type* formalParamLink;
    Type* formalParamNext;          // commit[26]: 支持函数形参链表结构

// 数组 TY_ARRAY_LINER
    int arrayElemCount;             // commit[27]: 数组的元素个数 newSubPtr()

// 结构体 | 联合体 TY_STRUCT | TY_UNION
    structMember* structMemLink;    // commit[49]: 该结构体的成员变量链表
};

// 使用在 type.c 中定义的全局变量
extern Type* TYINT_GLOBAL;
extern Type* TYCHAR_GLOBAL;
extern Type* TYLONG_GLOBAL;
extern Type* TYSHORT_GLOBAL;
extern Type* TYVOID_GLOBAL;
extern Type* TYBOOL_GLOBAL;

// 判断是否是指针
bool isInteger(Type* TY);

// 构建一个指针类型
Type* newPointerTo(Type* Base);

// 构造函数类型结点
Type* funcType(Type* ReturnType);

// commit[74]: 构造枚举类型
Type* enumType(void);

// commit[27]: 根据数组基类和元素个数定义该数组的元数据
Type* linerArrayType(Type* arrayBaseType, int arrayElemCount);

// commit[88]: 与其说是针对结构体的前向声明  更多是空结构体的基础面板
// 对原本通过 calloc() 创建 TY_STRUCT 的抽象
Type* structBasicDeclType(void);

// 递归性的为节点内的所有节点添加类型
void addType(Node* ND);

// 声明语法解析的输出 => HEADScope
// 可以从 4 个角度判断   (全局 or 局部)   (变量 or 常量)    eg. 函数名也算是常量
struct Object {
    bool IsLocal;
    bool IsFunction;
    bool IsStatic;          // 判断是否是文件域内函数

    /* 全局变量 */
    bool IsFuncDefinition;
    char* var_name;
    Object* next;
    Type* var_type;

    /*  全局常量 */
    char* InitData;

    Node* AST;
    Object* local;
    int StackSize;
    Object* formalParam;

    /* 局部变量 */
    int offset;
};

Object* parse(Token* tok);


/* 运算语法解析的数据结构和函数声明 */


// 结合两个方面理解  满足两个条件写入这里
// case1: 从 parse 输入的角度理解  可以把 TOKEN 细分到什么结点  数字 运算符 标识 关键字
// case2: 从 parse 输出的角度理解  一定对应着可以翻译到汇编语句的操作
typedef enum {
// TK_NUM——————————————加载立即数
    ND_NUM,

// TOKEN_OP————————————执行运算操作 | RW变量 | 定义代码生成段落
    ND_ADD,
    ND_SUB,
    ND_MUL,
    ND_DIV,
    ND_NEG,
    ND_MOD,

    ND_ASSIGN,

    ND_EQ,
    ND_NEQ,
    ND_GE,                      // Greater Equal
    ND_LE,                      // Less Equal
    ND_GT,                      // Greater Then
    ND_LT,                      // Less Then
    ND_NOT,                     // commit[81]: 非运算
    ND_BITNOT,                  // commit[82]: 按位取反
    ND_BITAND,                  // 按位与
    ND_BITOR,                   // 按位或
    ND_BITXOR,                  // 按位异或
    ND_LOGAND,
    ND_LOGOR,

    ND_STAMT,                   // 分号判断语句独立

    ND_BLOCK,                   // 花括号判断代码块

    ND_ADDR,                    // &: 取地址
    ND_DEREF,                   // *: 根据地址取内容

    // commit[39]: 对 GNU-C 的语句表达式的扩展支持
    // 大多数时候用在宏定义中 ({expr1, expr2, ...}) 语法结构会被写入 Body 返回最后一个表达式结果
    ND_GNU_EXPR,

    ND_COMMA,                   // commit[48]: 支持 "," 计算

    ND_STRUCT_MEMEBER,          // commit[49]: 支持结构体访问运算

    ND_TYPE_CAST,               // commit[67]: 支持强制类型转换

    ND_SHL,                     // shift left 左移运算
    ND_SHR,

    ND_TERNARY,                 // commit[95]: 三目运算符

// TOKEN_IDENT—————————读写变量
    ND_VAR,
    ND_FUNCALL,                 // 需要结合符号一起判断

// TOKEN_KEYWORD———————特殊的控制语句执行
    ND_RETURN,
    ND_IF,
    ND_FOR,
    ND_GOTO,
    ND_LABEL,
    ND_SWITCH,
    ND_CASE,                    // default 作为一种特殊 case
} NODE_KIND;

// 根据结点具体的操作类型保存额外数据
struct Node {
// 基本运算结点结构
    NODE_KIND node_kind;    // 声明该 NODE 结点是什么操作
    Node* LHS;
    Node* RHS;

// ND_TYPE_CAST | Basiclly everyNode
    Type* node_type;        // 定义操作结点的返回值类型 | 强转类型

// ND_NUM
    int64_t val;            // commit[57]: 同理在 long 支持后扩容

// ND_STAMT
    Node* next;             // commit[9]: 每个完整语句通过一个 Node 表示

// ND_VAR
    Object* var;            // commit[11]: 更新为 Object 的存储方式

// ND_BLOCK
    Node* Body;             // COMMIT[13]: 对 CompoundStamt 的 Block 支持

// ND_RETURN | ND_IF | ND_FOR
    Node* If_BLOCK;
    Node* Else_BLOCK;
    Node* Cond_Block;
    Node* For_Init;
    Node* Inc;
    char* BreakLabel;       // commit[91]: 新增对循环体中 break 的支持
    char* ContinueLabel;    // commit[92]: 新增对循环体中 continue 支持

// ND_SWITCH | ND_CASE
    Node* caseNext;         // 针对当前的 switch 所有的 case 语句作为链表存储
    Node* defaultCase;      // default 作为特殊的 case 语句存在

// ND_FUNCALL
    char* FuncName;
    Node* Func_Args;        // Tip: 参数可能是计算式 eg. &param
    Type* definedFuncType;  // 与被调用函数的函数定义类型保持一致

// ND_STRUCT_MEMEBER
    structMember* structTargetMember;   // commit[49]: 储存结构体访问的成员变量

    Token* token;           // 对报错位置的声明支持

// ND_GOTO  ND_LABEL
    char* gotoLabel;
    char* gotoUniqueLabel;
    Node* gotoNext;         // 依赖函数中的 GOTOs 和 Labels 实现
};

// commit[68]: 将 commit[67] 中 parse 定义的类型转换函数重声明
Node* newCastNode(Node* lastNode, Type* currTypeTarget);

/* 后端生成 codeGen() 数据结构和函数声明 */
void codeGen(Object* Prog, FILE* result);

/* 工具函数 */

// commit[56]: 显示源文件的报错位置 __FILE__: 报错文件名    __LINE__: 报错行号
#define unreachable() \
    errorHint("internal error at %s:%d", __FILE__, __LINE__)

int alignTo(int realTotal, int aimAlign);

// ???
Type* copyType(Type* origin);
