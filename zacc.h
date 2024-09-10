// 在 commit[8] 中修改了项目结构
// Zazzle-rvcc:
            // zacc.h   头文件  定义结构体和函数声明
            // type.c       变量类型定义实现
            // tokenize.c   词法解析
            // parse.c      语法解析
            // codeGen.c    后端代码生成
            // main.c       主函数  调用

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

// 提前声明
typedef struct Node Node;
typedef struct Type Type;
typedef struct Object Object;
typedef struct structMember structMember;


// 字符串
char* format(char* Fmt, ...);

/* 词法解析 tokenize() 数据结构和函数声明 */ 

typedef enum {
    TOKEN_EOF,          // 终结符
    TOKEN_NUM,          // 数字
    TOKEN_OP,           // 运算符

    TOKEN_IDENT,        // 标识

    TOKEN_KEYWORD,

    TOKEN_STR,          // 字符串字面量
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind token_kind;
    Token* next;

    // commit[57]: 支持 long 类型后需要扩大
    int64_t value;
    // int value;

    char* place;
    unsigned int length;

    Type* tokenType;

    char* strContent;

    // commit[46]: 记录行号 方便报错
    int LineNum;
};

void errorHint(char* errorInfo, ...);
void errorAt(int errorLineNum, char* place, char* FMT, va_list VA);
void tokenErrorAt(Token* token, char* FMT, ...);
void charErrorAt(char* place, char* FMT, ...);
bool equal(Token* input_token, char* target);
Token* skip(Token* input_token, char* target);
bool consume(Token** rest, Token* tok, char* str);

// commit[56]: 显示源文件的报错位置 __FILE__: 报错文件名    __LINE__: 报错行号
#define unreachable() \
    errorHint("internal error at %s:%d", __FILE__, __LINE__)

Token* tokenizeFile(char* filePath);

/* 语法分析 parse() 数据结构和函数声明 */

struct Object {
    // commit[31]: 新增对函数和变量的判断
    bool IsLocal;
    bool IsFunction;
    bool IsFuncDefinition;      // 判断函数声明

    /* 针对全局 包括变量和函数 */
    char* var_name;
    Object* next;
    Type* var_type;

    /* 针对全局变量的部分 */
    char* InitData;

    /* 针对局部变量的部分 */
    int offset;
    
    /* 针对函数的部分 */
    Node* AST;
    Object* local;
    int StackSize;
    Object* formalParam;
};

// 声明 AST 的节点类型   和构造 AST 相关所以一定是和某种运算操作
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
    ND_VAR,

    // 针对关键字定义结点
    ND_RETURN,
    ND_IF,
    ND_FOR,         // commit[17]: 复用了 for-label

    // commit[20]: 新增对 '*' 和 '&' 的支持
    ND_ADDR,        // &: 取地址
    ND_DEREF,       // *: 根据地址取内容

    // 复合代码块
    ND_BLOCK,

    // commit[23]: 支持无参函数调用(函数内部可以调用函数)
    ND_FUNCALL,

    // commit[39]: 对 GNU-C 的语句表达式的扩展支持
    // 大多数时候用在宏定义中 ({expr1, expr2, ...}) 语法结构会被写入 Body 返回最后一个表达式结果
    ND_GNU_EXPR,

    // commit[48]: 支持 "," 表达式
    ND_COMMA,

    // commit[49]: 支持结构体访问运算
    ND_STRUCT_MEMEBER,

} NODE_KIND;

// 定义函数内部 AST 结构
struct Node {
    NODE_KIND node_kind;

    // commit[57]: 同理在 long 支持后扩容
    int64_t val;                // 针对 ND_NUM 记录大小
    Node* LHS;
    Node* RHS;

    // commit[18]: 支持 parse.c 和 codeGen.c 使用 tokenize.c 中的报错函数
    Token* token;

    // commit[9]: 添加对 ';' 多语句的支持   每个语句 exprStamt 构成一个结点   整体是一个单叉树
    Node* next;

    // commit[10]: 添加对标识符的支持
    // char var_name;
    Object* var;            // commit[11]: 更新为 Object 的存储方式

    // COMMIT[13]: 对 CompoundStamt 的 Block 支持
    Node* Body;

    // commit[21]: 对类型的支持
    Type* node_type;

    // commit[15]: 对 IF-stamt 的支持
    // commit[16]: 因为在汇编中循环需要复用 if-stamt 所以定义在一起
    Node* If_BLOCK;
    Node* Else_BLOCK;
    Node* Cond_Block;
    Node* For_Init;     // for-init
    Node* Inc;          // for-increase

    // commit[25]: 表明该结点属于哪个函数定义
    char* FuncName;

    // 因为参数本身可能是计算式 比如 ND_ADD 之类的 所以用 Node 类型
    Node* Func_Args;

    // commit[49]: 储存结构体访问的成员变量  一个 ND_STRUCT_MEMEBER 每次只能访问一个
    structMember* structTargetMember;
};


Object* parse(Token* tok);


/* 类型定义 */
typedef enum {
    TY_INT,         // 整数类型变量
    TY_PTR,         // 指针

    // commit[25]: 声明函数签名
    TY_FUNC,

    // commit[27]: 定义数组变量类型 一维数组
    TY_ARRAY_LINER,

    // commit[33]: 支持 char 类型
    TY_CHAR,

    // commit[49]: 结构体
    TY_STRUCT,

    // commit[54]: 联合体定义
    TY_UNION,

    // commit[57]: 基础 long 类型
    TY_LONG,

    // commit[58]: 基础 short 类型
    TY_SHORT,
} Typekind;


struct Type {
    Typekind Kind;
    Type* Base;         // 如果当前类型是指针 必须声明指针基类 后续涉及空间计算
    int alignSize;      // 结构中单个最大的成员对齐目标  maxSingleOffset

    // commit[27]: 对 *任何类型* 的空间计算 等于 sizeof() 的返回值
    int BaseSize;

    // commit[22]: 在 declaration().LHS 构造中用到了
    Token* Name;

    // commit[25]: 根据 Token 保存返回值类型  作用在函数定义中
    Type* ReturnType;

    // commit[26]: 支持 Function.formalParam 定义
    Type* formalParamLink;
    Type* formalParamNext;

    // commit[27]: 记录数组的元素个数(在 newSubPtr() 有具体使用)
    int arrayElemCount;

    // commit[49]: 该结构体的成员变量链表
    structMember* structMemLink;
};


/* 定义结构体的结构 */
struct structMember {
    structMember* next;

    Type* memberType;
    Token* memberName;

    int offset;             // 记录该成员相对于结构体的偏移量
};


// 使用在 type.c 中定义的全局变量
extern Type* TYINT_GLOBAL;
extern Type* TYCHAR_GLOBAL;
extern Type* TYLONG_GLOBAL;
extern Type* TYSHORT_GLOBAL;

// 判断变量类型
bool isInteger(Type* TY);

// 构建一个指针类型
Type* newPointerTo(Type* Base);

// 递归性的为节点内的所有节点添加类型
void addType(Node* ND);

// 定义函数签名
Type* funcType(Type* ReturnType);

// commit[26]: 复制 Type 属性
Type* copyType(Type* origin);

// commit[27]: 根据数组基类和元素个数定义该数组的元数据
Type* linerArrayType(Type* arrayBaseType, int arrayElemCount);

/* 后端生成 codeGen() 数据结构和函数声明 */
void codeGen(Object* Prog, FILE* result);

int alignTo(int realTotal, int aimAlign);