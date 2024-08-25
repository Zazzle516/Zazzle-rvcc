// 在 commit[8] 中修改了项目结构
// Zazzle-rvcc:
            // zacc.h   头文件  定义结构体和函数声明
            // type.c       变量类型定义实现
            // tokenize.c   词法解析
            // parse.c      语法解析
            // codeGen.c    后端代码生成
            // main.c       主函数  调用

// Tip: 注意里面有哪些函数需要 static 修饰

// 通过定义 _POSIX_C_SOURCE 指定一个值 控制头文件对 POSIX 功能的可见性
// 199309L: 启用 POSIX.1b (实时扩展) 标准
// 199506L: 启用 POSIX.1c (线程扩展) 标准
// 200112L: 启用 POSIX.1-2001 标准
// 200809L: 启用 POSIX.1-2008 标准
#define _POSIX_C_SOURCE 200809L

// Q: 在函数帧中 形参的 Local 更新和函数内部变量的 local 更新的过程

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 提前声明
typedef struct Node Node;
typedef struct Type Type;
typedef struct Object Object;

/* 词法解析 tokenize() 数据结构和函数声明 */ 

typedef enum {
    TOKEN_EOF,          // 终结符
    TOKEN_NUM,          // 数字
    TOKEN_OP,           // 运算符       在 commit[7] 中新增的比较符也计入 TOKEN_OP 中

    TOKEN_IDENT,        // 标识     变量名 | 函数名  commit[10]

    TOKEN_KEYWORD,

    TOKEN_STR,           // 字符串字面量  Tip: 相比字符串变量 可以匿名定义不允许修改 一般存储在只读内存区
} TokenKind;

typedef struct Token Token;
struct Token {
    TokenKind token_kind;
    Token* next;
    int value;                  // 只针对数字符号  因为超过 10 的数字大小要额外通过 value 记录

    char* place;                // 在 input_ptr 中的位置    从 equal() 层面看 存储的是具体内容
    unsigned int length;        // 该符号在 input_ptr 中所占长度

    // commit[34]: 字符串字面量通过定义 char 数组实现 包括 '\0'  类似于 int value 的属性针对数值
    // 实际上 Token 类型只保存了 TOKEN_NUM 和 TOKEN_STR 的具体内容  可以用在两个类型上
    // Q: 必须在 token 的时候判断为 str 否则在解析的时候分不清 IDENT 和 STR
    Type* tokenType;

    // Q: 为什么需要拷贝字符串内容 直接定位到同一个地址不就好了
    // Q: 目前猜测仍然是因为 token 解析的问题 解析的依据是空格 而字符串的内容是不能控制的 所以要提前存起来
    // 需要有空格的字符串测试
    char* strContent;
};

void errorHint(char* errorInfo, ...);
void errorAt(char* place, char* FMT, va_list VA);
void tokenErrorAt(Token* token, char* FMT, ...);
void charErrorAt(char* place, char* FMT, ...);
bool equal(Token* input_token, char* target);
Token* skip(Token* input_token, char* target);
bool consume(Token** rest, Token* tok, char* str);

Token* tokenize(char* P);       // main() 调用声明

/* 语法分析 parse() 数据结构和函数声明 */

// commit[11]: 定义在 Function 中使用的 local 数据
// commit[31]: 融合 Function 和 Object
// Q: 目前没有明白融合的意义
// A: 因为处理全局变量 所以必须要把函数考虑进来
struct Object {
    // commit[31]: 新增对函数和变量的判断
    bool IsLocal;
    bool IsFunction;

    /* 针对全局 包括变量和函数 */
    char* var_name;
    Object* next;
    Type* var_type;

    /* 针对全局变量的部分 */
    // Q: 这里是因为和 str 的实现绑定所以一定是 char* 类型吗  有没有可能代表其他的全局类型
    // A: 在 commit[34] 之前的全局变量都是声明 没有存储具体的内容 从这里开始有了实际内容需要存储
    // 结合 chibicc 的实现来看 是可以储存任意类型的     Q: char* 是怎么实现任意类型的
    char* InitData;

    /* 针对局部变量的部分 */
    int offset;
    
    /* 针对函数的部分 */
    Node* AST;
    Object* local;
    int StackSize;
    Object* formalParam;
};

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
} NODE_KIND;

// 定义 AST 的结点结构
// commit[25]: 因为 C 语言不支持函数嵌套 所以函数本身一定是作为最顶层语法结构出现
// 下面的 struct Node 是针对 Func 内部语句的定义 它只需要知道自己属于哪个 Func 就 ok 了
// Q: 为什么没有给函数返回值一个 Node 结点
// A: 因为 struct Node 从定义上只能处理函数内部的结点 而返回值定义在函数签名中 无法处理
struct Node {
    NODE_KIND node_kind;
    int val;                // 针对 ND_NUM 记录大小
    Node* LHS;
    Node* RHS;

    // commit[18]: 支持 parse.c 和 codeGen.c 使用 tokenize.c 中的报错函数
    // 完成语法错误的提示   因为 tokenErrorAt 使用的是 token 类型的传参    所以需要在 AST 中跟踪 token-stream 的执行
    // 储存结点对应的终结符
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

    // commit[23]: 支持函数名声明
    // commit[25]: 表明该结点属于哪个函数定义
    char* FuncName;

    // commit[24]: 支持参数结点声明
    // commit[25]: 支持当前函数中*内部*函数调用的参数存储 函数返回会通过 ND_ASSIGN 完成
    Node* Func_Args;    // 因为参数本身可能是计算式 比如 ND_ADD 之类的 所以用 Node 类型

    // Tip: 因为一开始我把 Func_Args 的定义写到 NODE_KIND 里面了
    // 参数本身是需要内存去存储的 而且根据传参的方式不同 可能是 ND_VAR ND_STAMT... 之类的多种类型
};
// 视频有提到 因为不同的 ND_KIND 不一定使用到全部的属性 可以用 struct union 进行优化

// commit[11]: 用 Function 结构体包裹 AST 携带数据之类的其他内容
// commit[31]: 弃用 但暂时保留记录
typedef struct Function Function;
struct Function {
    Node* AST;
    Object* local;

    // 目前只在栈上分配函数帧的空间需求(如果需要的空间非常大 要考虑从堆上动态分配空间)
    // 现在只会报 Stack Overflow 的错误
    int StackSize;

    // commit[25]: 定义函数名 每个函数目前作为独立语句处理
    char* FuncName;

    // commit[25]: 对应在 codeGen() 中也是独立拥有函数栈帧 通过循环处理每一个 FuncNode
    Function* next;

    // commit[26]: 当前函数的形参链表
    // Q: 链表结构是 Type 提供的但是为什么这里写 Object 呢
    // A: 参数列表最终会挂载到 Object 中    因为 Local 是 Object 类型
    Object* formalParam;
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
} Typekind;

// Q: 为什么把函数返回值定义在 Type 中
// A: 因为 Node 处理的是函数本身 所以 Type 需要在 Node 外部 链接到 Token 读取函数的返回值类型
struct Type {
    Typekind Kind;      // <int, ptr>
    Type* Base;         // 如果当前类型是指针 必须声明指针基类 后续涉及空间计算

    // commit[27]: 对 *任何类型* 的空间计算 等于 sizeof() 的返回值
    int BaseSize;

    // commit[22]: 在 declaration().LHS 构造中用到了
    // 某种程度上是为了匹配 parse() 的语法位置报错才有这个属性吧
    Token* Name;        // 存储当前类型的名称 <int> <double> 链接到 Token Stream

    // commit[25]: 根据 Token 保存返回值类型  作用在函数定义中
    Type* ReturnType;

    // commit[26]: 支持 Function.formalParam 定义
    // Q: 形参链表结构 Object.next 和 Type.formalParamNext 都有   具体是哪个在生效
    Type* formalParamLink;
    Type* formalParamNext;

    // commit[27]: 记录数组的元素个数(在 newSubPtr() 有具体使用)
    int arrayElemCount;
};

// 使用在 type.c 中定义的全局变量 (用于对 int 类型的判断)
extern Type* TYINT_GLOBAL;
extern Type* TYCHAR_GLOBAL;

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
void codeGen(Object* Global);