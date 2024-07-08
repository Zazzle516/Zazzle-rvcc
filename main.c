#include <stdio.h>
#include <stdlib.h>

#include <string.h>         // 提供 memcmp()

#include <stdbool.h>
#include <ctype.h>

#include <stdarg.h>

typedef enum {
    TOKEN_EOF,              // 终结符
    TOKEN_NUM,              // 数字
    TOKEN_OP                // 运算符       在 commit[7] 中新增的比较符也计入 TOKEN_OP 中
} TokenKind;

// 全局变量 记录输入流的起始位置(方便后续找到具体的错误位置)
// 写在最上面 如果只是定义在 main() 上面的话 tokenize() 找不到
static char* InputHEAD;

typedef struct Token Token;
struct Token {
    TokenKind token_kind;
    Token* next;
    int value;          // 只针对数字符号  因为超过 10 的数字大小要额外通过 value 记录

    // 这两个属性忘掉了
    char* place;            // 在 input_ptr 中的位置    从 equal() 层面看 存储的是具体内容
    unsigned int length;        // 该符号在 input_ptr 中所占长度
};

// 有一个在看代码的时候也忽略掉了的 static 修饰 因为都只在当前文件使用 所有都可以用 static 修饰

// 针对报错进行优化 errorHint() 和 errorAt() 是同级的错误信息展示
// errorHint() 不去展示具体的语法错误未知 也就是不能用在具体的代码分析中 只可能能是一开始的参数数量错误
// errorAt() 展示具体的语法错误位置

static void errorHint(char* errorInfo, ...) {
    // 使用 <stdarg.h> 提供的变长参数

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

// FMT VA 在 tokenize() 词法分析中定义的输出
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


// 对错误信息的具体处理     针对 数字/符号 抽象出一层用来调用 errorAt()

static void tokenErrorAt(Token* token, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    // 指明该 Token 的出错位置
    errorAt(token->place, FMT, VA);
}

static void charErrorAt(char* place, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    errorAt(place, FMT, VA);
}

// 判断当前 Token 与指定 target 是否相同
static bool equal(Token* input_token, char* target) {
    // 使用 memcmp 进行存储内容的比较   因为不清楚 Token 的存储类型 不能直接使用 '=='
    return memcmp(input_token->place, target, input_token->length) == 0 && target[input_token->length] == '\0';
}

// 指定 target 去跳过
static Token* skip(Token* input_token, char* target) {
    if (!equal(input_token, target)) {
        // errorHint("expected: %s", target);   针对 Token 进行错误优化
        tokenErrorAt(input_token, "expected: %s", target);
    }

    // 不是根据 target 连续跳过 这个函数是用来跳过负号的(一次就够了 这么写会无法检测到语法错误)
    // while (equal(input_token, target)) {
    //     input_token = input_token.next;
    // }
    return input_token->next;
}

static int getNumber(Token* input_token) {
    if (input_token->token_kind != TOKEN_NUM) {
        // errorHint("expected number");        同理根据 Token 优化错误信息
        tokenErrorAt(input_token, "expected number");
    }
    // 把在 newToken() 中构造好的值取出
    return input_token->value;
}

// 定义一个新的 Token 结点挂载到链表上
static Token* newToken(TokenKind token_kind, char* start, char* end) {
    // 在传参的时候实际上只能看到一段内存空间
    Token* currToken = calloc(1, sizeof(Token));            // 底层语言 C 这种只能用 calloc / malloc 来分配内存
    currToken->token_kind = token_kind;
    currToken->place = start;
    currToken->length = end - start;
    return currToken;

    // 为了编译器的效率 这里分配的空间并没有释放
}

// 引入比较符后修改 isdigit() 的判断

// 比较字符串是否相等   和 equal() 中的 memcmp() 区分
static bool strCmp(char* input_ptr, char* target) {
    return (strncmp(input_ptr, target, strlen(target)) == 0);
}

static int readPunct(char* input_ptr) {
    // 同理通过比较内存的方式   优先判断长度更长的运算符
    if (strCmp(input_ptr, "==") || strCmp(input_ptr, "!=") || strCmp(input_ptr, ">=") || strCmp(input_ptr, "<=")) {
        return 2;
    }
    return (ispunct(*input_ptr) ? 1: 0);      // 单运算符返回 1 否则为 0
}

static Token* tokenize() {
    // 词法分析 根据链表的结构去调用 newToken() 构造
    Token HEAD = {};                 // 声明 HEAD 为空
    Token* currToken = &HEAD;        // 指向 HEAD 类型为 Token 的指针 声明为 currToken 注意是指针哦 通过 '->' 调用
    char* P = InputHEAD;

    while(*P) {
        // 对空格没有使用 while 因为如果在里面写了 continue 的话 while 实际上就没有意义了
        // 如果不写 continue 程序就会继续执行 访问到 errorHint()
        if (isspace(*P)) {
            // 跳过空格 \t \n \v \f \r
            P ++;
            continue;
        }

        // 把数字和操作符分开处理

        // 仅处理数字
        if (isdigit(*P)) {
            // isdigit() 比较特殊 一次只能判断一个字符 如果是一个很大的数字 需要使用 while 循环判断
            currToken->next = newToken(TOKEN_NUM, P, P);
            currToken = currToken->next;

            const char* start = P;
            currToken->value = strtoul(P, &P, 10);      // 因为符号会额外处理 所以这里转换为 无符号数
            currToken->length = P - start;              // 在 newToken() 的时候还无法确定长度 在此时确定后重置

            // 此时 P 通过 strtoul() 指向了非数字字符 空格或者符号
            continue;
        }

        // 仅处理运算符
        int opLen = readPunct(P);
        if (opLen > 0) {
            // 如果是运算符 目前的运算符只有一位 +/-
            currToken->next = newToken(TOKEN_OP, P, P + opLen);
            P += opLen;                 // 根据符号长度更新指向!!!
            currToken = currToken->next;        // 更新指向下一个位置
            continue;
        }

        charErrorAt(P, "Wrong Syntax: %c", *P);
    }

    // 添加终结符
    currToken->next = newToken(TOKEN_EOF, P, P);

    // 对直接存在的结构体调用 "成员变量"
    return HEAD.next;
}

// 语法分析 结合语法规则判断输入是否合法并且生成 AST

// 在引入一元运算符后 涉及到单叉树的构造(本质上是看成负号 和一般意义上的 ++|-- 不同)

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
    ND_LT       // Less Then
}NODE_KIND;

// 定义 AST 的结点结构
typedef struct Node Node;
struct Node {
    NODE_KIND node_kind;
    int val;                // 针对 ND_NUM 记录大小
    Node* LHS;
    Node* RHS;
};

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

// 定义 AST 的树型结构  (本质上仍然是一个结点但是有左右子树的定义)
static Node* createAST(NODE_KIND node_kind, Node* LHS, Node* RHS) {
    Node* rootNode = createNode(node_kind);
    rootNode->LHS = LHS;
    rootNode->RHS = RHS;
    return rootNode;
}

// 作用于一元运算符的单边树
static Node* createSingleNEG(Node* single_side) {
    Node* rootNode = createNode(ND_NEG);
    rootNode->LHS = single_side;            // 定义在左子树中
    return rootNode;
}

// 在新增比较符后比较符的运算优先
// expr = equality          本质上不需要但是结构清晰
// equality = relation op relation                      op = (!= | ==)
// relation = first_class_expr op first_class_expr      op = (>= | > | < | <=)
// first_class_expr = second_class_expr (+|- second_class_expr)*
// second_class_expr = third_class_expr (*|/ third_class_expr)
// third_class_expr = (+|-)third_class_expr | primary_class_expr        优先拆分出一个符号作为 减法符号
// primary_class_expr = '(' | ')' | num

// 定义产生式关系并完成自顶向下的递归调用
static Node* expr(Token** rest, Token* tok);
static Node* equality_expr(Token** rest, Token* tok);       // 针对 (3 < 6 == 5) 需要优先判断 (<)
static Node* relation_expr(Token** rest, Token* tok);
static Node* first_class_expr(Token** rest, Token* tok);
static Node* second_class_expr(Token** rest, Token* tok);
static Node* third_class_expr(Token** rest, Token* tok);
static Node* primary_class_expr(Token** rest, Token* tok);

// Q: rest 是 where and how 起到的作用
// 没什么作用 把 rest 删了不影响函数功能 只是方便跟踪
static Node* expr(Token** rest, Token* tok) {
    return equality_expr(rest, tok);
}

static Node* equality_expr(Token** rest, Token* tok) {
    Node* ND = relation_expr(&tok, tok);
    while(true) {
        // 比较符号后面不可能还是比较符号
        if (equal(tok, "!=")) {
            ND = createAST(ND_NEQ, ND, relation_expr(&tok, tok->next));
            continue;
        }
        if (equal(tok, "==")) {
            ND = createAST(ND_EQ, ND, relation_expr(&tok, tok->next));
            continue;
        }
        *rest = tok;
        return ND;
    }
}

static Node* relation_expr(Token** rest, Token* tok) {
    Node* ND = first_class_expr(&tok, tok);

    while(true) {
        if (equal(tok, ">=")) {
            ND = createAST(ND_GE, ND, first_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "<=")) {
            ND = createAST(ND_LE, ND, first_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "<")) {
            ND = createAST(ND_LT, ND, first_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, ">")) {
            ND = createAST(ND_GT, ND, first_class_expr(&tok, tok->next));
            continue;
        }
        *rest = tok;
        return ND;
    }
}

static Node* first_class_expr(Token** rest, Token* tok) {
    // 处理最左表达式
    Node* ND = second_class_expr(&tok, tok);

    while(true) {
        // 如果仍然有表达式的话
        if (equal(tok, "+")) {
            // 构建 ADD 根节点 (注意使用的是 tok->next) 并且
            ND = createAST(ND_ADD, ND, second_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "-")) {
            // 同理建立 SUB 根节点
            ND = createAST(ND_SUB, ND, second_class_expr(&tok, tok->next));
            continue;
        }

        // 表达式解析结束   退出
        *rest = tok;        // rest 所指向的指针 = tok  rest 仍然维持一个 3 层的指针结构
        return ND;
    }
}

static Node* second_class_expr(Token** rest, Token* tok) {
    Node* ND = third_class_expr(&tok, tok);

    while(true) {
        if (equal(tok, "*")) {
            ND = createAST(ND_MUL, ND, third_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "/")) {
            ND = createAST(ND_DIV, ND, third_class_expr(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return ND;
    }
}

static Node* third_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "+")) {
        // 正数跳过
        return third_class_expr(rest, tok->next);
    }

    if (equal(tok, "-")) {
        // 作为负数标志记录结点
        Node* ND = createSingleNEG(third_class_expr(rest, tok->next));
        return ND;
    }

    // 直到找到一个非运算符
    return primary_class_expr(rest, tok);
}

static Node* primary_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "(")) {
        // 递归调用顶层表达式处理
        Node* ND = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return ND;
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value);
        *rest = tok->next;
        return ND;
    }

    // 错误处理
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
    switch (AST->node_kind)
    {
    case ND_NUM:
        printf("  li a0, %d\n", AST->val);
        return;
    case ND_NEG:
        // 因为是单叉树所以某种程度上也是终结状态   递归找到最后的数字
        codeGen(AST->LHS);
        printf("  neg a0, a0\n");
        return;
    default:
        // +|-|*|/ 其余运算 需要继续向下判断
        break;
    }

    codeGen(AST->RHS);
    push_stack();
    codeGen(AST->LHS);
    pop_stack("a1");        // 把 RHS 的计算结果弹出    根据根节点情况计算

    // 根据当前根节点的类型完成运算
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

    case ND_EQ:
    case ND_NEQ:
        // 汇编层面通过 xor 比较结果
        printf("  xor a0, a0, a1\n");       // 异或结果储存在 a0 中
        if (AST->node_kind == ND_EQ) 
            printf("  seqz a0, a0\n");                   // 判断结果 == 0
        if (AST->node_kind == ND_NEQ)
            printf("  snez a0, a0\n");                   // 判断结果 > 0
        return;

    case ND_GT:
        printf("  sgt a0, a0, a1\n");
        return;
    case ND_LT:
        printf("  slt a0, a0, a1\n");
        return;

    case ND_GE:
        // 转换为判断是否小于
        printf("  slt a0, a0, a1\n");       // 先判断 > 情况
        printf("  xori a0, a0, 1\n");       // 再判断 == 情况
        return;
    case ND_LE:
        printf("  sgt a0, a0, a1\n");
        printf("  xori a0, a0, 1\n");
        return;
    
    default:
        errorHint("invalid expr");
    }
}

int main(int argc, char* argv[]) {
    // 首先是对异常状态的判断
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    // 对合法状态进行处理
    printf("  .global main\n");
    printf("main:\n");
    
    // 得到指向输入流的指针
    char* input_ptr = argv[1];
    InputHEAD = argv[1];
    
    // 把输入流进行标签化 得到 TokenStream 链表     此时没有空格
    Token* input_token = tokenize();

    // 调用语法分析
    Node* currentAST = expr(&input_token, input_token);

    // 注意这里是在语法分析结束后再检查词法的结束正确性
    if (input_token->token_kind != TOKEN_EOF) {
        // 因为在 first_class_expr() 执行之前 input_token 仍然指向第一个 token
        // 在 first_class_expr() 执行之后 此时指向最后一个 Token_EOF
        // 不放在这个位置会报错
        tokenErrorAt(input_token, "extra Token");
    }

    // 后端生成代码
    codeGen(currentAST);

    printf("  ret\n");

    // 通过栈对运算合法性判断
    if (StackDepth != 0) {
        errorHint("Wrong syntax");
    }
    return 0;
}