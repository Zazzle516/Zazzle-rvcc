// Zazzle-rvcc:
            // zacc.h   头文件  定义结构体和函数声明
            // type.c       变量类型定义实现
            // tokenize.c   词法解析
            // preprocess.c 预处理
            // parse.c      语法解析
            // codeGen.c    后端代码生成
            // main.c       程序入口

#define _POSIX_C_SOURCE 200809L

// 目前 RVCC 自举并不完全支持 #include
// 所以才需要 self.py 声明所需要的库文件
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
#include <sys/types.h>
#include <sys/wait.h>       // 进程间的同步和状态查询
#include <unistd.h>         // 系统调用相关的函数声明  fork() exec() ...
#include <libgen.h>
#include <glob.h>
#include <sys/stat.h>

// 替换声明  在后续的变量声明中省略 struct 关键字
// Q: 为什么 typedef 可以作为一个组合识别到 (struct structName)
// A: 因为 typedef 关键字(非宏处理) 和编译器强绑定  结合 commit[64] 的实现
typedef struct structMember structMember;
typedef struct Type Type;
typedef struct Object Object;
typedef struct Node Node;
typedef struct Relocation Relocation;
typedef struct Token Token;

/* 当前解析的 .c 文件 */
extern char* SingleBaseFile;


/* 工具函数 */
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* 从文件层面定义输入和头文件 */
typedef struct {
    char* fileName;
    int fileNo;
    char* fileContent;
} InputFileArray;

// 获取当前输入文件的文件柄
InputFileArray** getAllIncludeFile(void);

/* 编译器驱动输入参数处理 */

// 定义参数存储格式
typedef struct {
    char** paramData;   // 参数内容
    int Capacity;
    int paramNum;       // 参数个数
} StringArray;

void strArrayPush(StringArray* Arr, char* S);

/* 词法解析 tokenize() 数据结构和函数声明 */ 

// 在 tokenize() 阶段使用的工具函数
void errorHint(char* errorInfo, ...);
void errorAt(char* fileName, char* input, int errorLineNum, char* place, char* FMT, va_list VA);
void charErrorAt(char* place, char* FMT, ...);
char* format(char* Fmt, ...);

// 从词法分析的 IO 的角度分析 Token 的可能性
typedef enum {
    TOKEN_EOF,          // 终结符
    TOKEN_NUM,          // 数字
    TOKEN_OP,           // 运算符
    TOKEN_IDENT,        // 标识

    TOKEN_KEYWORD,      // 相关语法关键字
    TOKEN_STR,          // 字符串字面量
} TokenKind;

struct Token {
    InputFileArray* inputFile;    // 记录当前读取的 token 对应的文件
    TokenKind token_kind;
    Token* next;
    int LineNum;                // commit[46]: 记录行号 方便报错
    bool atBeginOfLine;         // commit[159]: 判断当前是否是行首

    int64_t value;              // commit[57]: 支持 long 类型后需要扩大
    double FloatValue;          // commit[139]: 支持浮点运算
    char* place;
    unsigned int length;

    char* strContent;
    Type* tokenType;            // commit[132]: 在词法解析部分自定义数值后缀
};

void convertKeyWord(Token* tok);
void warnTok(Token* tok, char* Fmt, ...);
Token* tokenizeFile(char* filePath);

/* 预处理函数 preprocess() */

Token* preprocess(Token* tok);

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

    TY_FLOAT,
    TY_DOUBLE,
} Typekind;

// 自定义类型
struct structMember {
    structMember* next;
    Token* tok;             // commit[86]: 针对结构体成员的零或数组定义不支持

    Type* memberType;
    Token* memberName;
    int Align;              // commit[118]: 细化到支持结构体成员本身的对齐
    int offset;             // 从字节角度: 记录该成员相对于结构体的偏移量

    int Idx;                // 从逻辑角度: 记录每个成员的下标  同时也作为成员比较的标志
};

// 针对不同类型额外保存的数据
struct Type {
// 基础信息(匿名)
    Typekind Kind;
    int BaseSize;                   // commit[27]: 类型 sizeof() 计算的返回值
    int alignSize;                  // 该类型内存对齐标准
    bool IsUnsigned;                // commit[131]: 支持符号定义

// 被声明变量的基础信息 (结构体 | 函数 | ...)
    Token* Name;

// 指针 TY_PTR
    Type* Base;                     // 指针基类涉及空间计算

// 函数 TY_FUNC
    bool IsVariadic;                // commit[127]: 该函数是否接受可变参数长度
    Type* ReturnType;               // commit[25]: 根据 Token 保存返回值类型
    Type* formalParamLink;
    Type* formalParamNext;          // commit[26]: 支持函数形参链表结构
    Token* namePos;                 // commit[138]: 记录函数声明中形参定义的位置  Q: 任意变量都有这个属性吗

// 数组 TY_ARRAY_LINER
    int arrayElemCount;             // commit[27]: 数组的元素个数 newSubPtr()

// 结构体 | 联合体 TY_STRUCT | TY_UNION
    structMember* structMemLink;    // commit[49]: 该结构体的成员变量链表
    bool IsFlexible;                // commit[113]: 判断是否为灵活数组成员
};

// 使用在 type.c 中定义的全局变量
extern Type* TYINT_GLOBAL;
extern Type* TYCHAR_GLOBAL;
extern Type* TYLONG_GLOBAL;
extern Type* TYSHORT_GLOBAL;
extern Type* TYVOID_GLOBAL;
extern Type* TYBOOL_GLOBAL;

extern Type* TY_UNSIGNED_CHAR_GLOBAL;
extern Type* TY_UNSIGNED_SHORT_GLOBAL;
extern Type* TY_UNSIGNED_INT_GLOBAL;
extern Type* TY_UNSIGNED_LONG_GLOBAL;

extern Type* TYFLOAT_GLOBAL;
extern Type* TYDOUBLE_GLOBAL;

// 判断是否是指针
bool isInteger(Type* numType);
bool isFloatNum(Type* numType);
bool isNumeric(Type* numType);

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

// 支持多个全局变量作为 RHS 被调用  
struct Relocation {
    Relocation* next;       // 在 codeGen.emitData 中记录赋值的全局变量信息

    char* globalLabel;      // 作为指针被访问的另一个全局变量的标签
    int labelOffset;        // 针对数组 结构体等复合类型  元素 成员变量相对于 label 的偏移量
    long suffixCalcu;       // 当前全局变量在此基础上的一些后续计算
};


// 声明语法解析的输出 => HEADScope
// 可以从 4 个角度判断   (全局 or 局部)   (变量 or 常量)    eg. 函数名也算是常量
struct Object {
    bool IsLocal;
    bool IsFunction;
    bool IsStatic;          // 判断是否是文件域内函数
    int Align;              // commit[118]: 暂存自定义对齐值  防止覆盖

    /* 全局变量 */
    bool IsFuncOrVarDefine; // RVCC 先支持省略 extern 的函数声明  此时同时支持函数和变量的外部声明  所以复用
    char* var_name;
    Object* next;
    Type* var_type;
    Token* token;           // Q: 对应的终结符 ???

    /*  全局常量 */
    char* InitData;
    Relocation* relocatePtrData;

    Node* AST;
    Object* local;
    int StackSize;
    Object* formalParam;
    // Tip: 编译器无法确认可变参数类型  要等到运行的时候推断  所以该属性并不能写到 Type 中
    Object* VariadicParam;  // commit[128]: 支持可变参数

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
    ND_NULL_EXPR1,              // 处理 ND_COMMA 返回值的占位功能
    ND_NULL_EXPR2,              // 处理无赋值情况的占位
    ND_MEMZERO,                 // 初始化赋值的清零操作

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
    ND_DOWHILE,
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
    double FloatVal;        // commit[139]: 加载浮点数

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

// 针对链表空间分配的封装
Type* copyType(Type* origin);
