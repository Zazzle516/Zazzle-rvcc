#include <stdio.h>
#include <stdlib.h>

#include <string.h>         // 提供 memcmp()

#include <stdbool.h>
#include <ctype.h>

#include <stdarg.h>

typedef enum {
    TOKEN_EOF,              // 终结符
    TOKEN_NUM,              // 数字
    TOKEN_OP                // 运算符
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
        if (*P == '+' || *P == '-') {
            // 如果是运算符 目前的运算符只有一位 +/-
            currToken->next = newToken(TOKEN_OP, P, P + 1);
            P ++;
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
    
    int first_num = getNumber(input_token);
    printf("  li a0, %d\n", first_num);

    // 更新 token 的位置(如果语法正确应该是符号)
    input_token = input_token->next;

    while(input_token->token_kind != TOKEN_EOF) {
        // 统一为 addi 处理(因为 RISCV64 只有加法指令)
        if (equal(input_token, "+")) {
            input_token = input_token->next;
            printf("  addi a0, a0, %d\n", getNumber(input_token));      // 如果出现 1++ 这类错误会在 getNumber() 调用 tokenErrorAt()
            input_token = input_token->next;
            continue;
        }

        input_token = skip(input_token, "-");   // 记得要通过 skip 更新
        printf("  addi a0, a0, -%d\n", getNumber(input_token));
        input_token = input_token->next;
    }

    // 考虑到终结情况 比如 1-1-1- 转化为 1-1-1-0    (虽然功能上没问题 但是应该报错 Warning
    printf("  ret\n");
    return 0;
}