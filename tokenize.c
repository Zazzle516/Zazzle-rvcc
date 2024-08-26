#include "zacc.h"

// 全局变量 记录输入流的起始位置(方便后续找到具体的错误位置)
// 写在最上面 如果只是定义在 main() 上面的话 tokenize() 找不到
static char* InputHEAD;

// 针对报错进行优化 errorHint() 和 errorAt() 是同级的错误信息展示
// errorHint() 不去展示具体的语法错误未知 也就是不能用在具体的代码分析中 只可能能是一开始的参数数量错误
// errorAt() 展示具体的语法错误位置

void errorHint(char* errorInfo, ...) {
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
void errorAt(char* place, char* FMT, va_list VA) {
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

void tokenErrorAt(Token* token, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    // 指明该 Token 的出错位置
    errorAt(token->place, FMT, VA);
}

void charErrorAt(char* place, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    errorAt(place, FMT, VA);
}

// 判断是否是 keywords
// 如果写在一起的话是一个二重循环了 不太好理解
// commit[30]: 新增对 sizeof 的支持
static bool isKeyWords(Token* input) {
    static char* keywords[] = {"if", "else", "return", "for", "while", "sizeof", "char"};

    // Tip: 注意这里的下标      sizeof() 得到的是该对象的总空间
    for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i ++) {
        if (equal(input, keywords[i]))
            return true;
    }
    return false;
}

// 得到所有 Tokens 后针对 KEYWORD 进行判断
static void convertKeyWord(Token* input_token) {
    // 直到 commit[34] 才发现这里写错了...
    // 不过从 input_token 改了之后才发现速度提升了这么快  这个 keyWord 功能为什么提速这么快
    // 没有发现在 parser 中用到 所以是怎么提速的???
    for (Token* tok = input_token; tok->token_kind != TOKEN_EOF; tok = tok->next) {
        if (isKeyWords(tok)) {
            tok->token_kind = TOKEN_KEYWORD;
        }
    }
}

// 判断当前 Token 与指定 target 是否相同
bool equal(Token* input_token, char* target) {
    // 使用 memcmp 进行存储内容的比较   因为不清楚 Token 的存储类型 不能直接使用 '=='
    return memcmp(input_token->place, target, input_token->length) == 0 && target[input_token->length] == '\0';
}

// 指定 target 去跳过
Token* skip(Token* input_token, char* target) {
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

// commit[22]: 
// Q: 消耗掉指定 Token  目前是用在 DEREF 中 不能用 skip 代替的区别是什么
// A: skip() 用在准确跳过一个目标 tok 而 comsume 面对的是数量不确定的 tok*
bool consume(Token** rest, Token* tok, char* str) {
    // 可能没有目标 tok 可能有 1 个
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
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

// commit[11] 在支持任意变量名后新增判断

// 变量名首位判断 [a-zA-Z_]
static bool isIdentIndex1(char input_ptr) {
    if ((input_ptr >= 'a' && input_ptr <= 'z') || (input_ptr >= 'A' && input_ptr <= 'Z') || input_ptr == '_') {
        return true;
    }
    return false;
}

// 变量名其余位置判断 [a-zA-Z_0-9]
static bool isIdentIndex(char input) {
    return (isIdentIndex1(input) || (input >= '0' && input <= '9'));
}

// commit[36]: 在含有转义字符的情况处理字符串 读取到真正的结束符 '"' 返回位置
static char* readStringLiteralEnd(char* strPos) {
    char* strStart = strPos;

    for (; *strPos != '"'; strPos++) {
        if (*strPos == '\n' || *strPos == '\0')
            charErrorAt(strStart, "unclosed string literal\n");
        if (*strPos == '\\')
            // 这里的 '\\' 是为了在 C 程序中正确表达 '\' 而是用转义
            // 结合 for-loop 本身的 strPos++ 这里通过转义字符 '\' 跳过字符 '"' 或者其他想表示特殊含义的字符
            strPos++;
    }
    // 指向结束字符串的右双引号
    return strPos;
}

// commit[36]: 处理字符串中的转义字符
static int readEscapeChar(char* escapeChar) {
    switch (*escapeChar)
    {
    case 'a':
        return '\a';        // 响铃
    case 'b':
        return '\b';        // 退格
    case 't':
        return '\t';        // 制表
    case 'n':
        return '\n';        // 换行
    case 'v':
        return '\v';        // 垂直制表符
    case 'f':
        return '\f';        // 换页
    case 'r':
        return '\r';        // 回车
    case 'e':
        return 27;          // 转义符 (GNU扩展 长见识了..
    default:
        return *escapeChar;
    }
}

// commit[34]: 读取字符字面量 目前不支持 "" 的转义 以全局的方式处理
// commit[36]: 在支持转义字符之后 注意转义字符在长度计算的参与
static Token* readStringLiteral(char* start) {
    // 注意在 readStringLiteralEnd 中直接从 strPos 判断 直接传 start 会被视为结束符
    char* strEnd = readStringLiteralEnd(start + 1);

    // Q: 这里定义的 buffer 是在干什么
    // 存储经过转义后的合法字符串 理论上还需要 - 1 但是没有 这里是预留给 '\0' 结束符
    // 转义后的合法字符串长度一定小于 (strEnd - start) 但是直接分配可能的最大值不会出错
    char* buffer = calloc(1, strEnd - start);

    // 初始化转义后合法字符串的长度 (转义符 \) + (后续字符 ?) 的转义结果统一记为长度 1
    // 这里的长度一定是 小于等于 strEnd - start - 1 的  对应 buffer 存储内容的真实长度
    int realStrLen = 0;

    // 将转义结果结果写入 buffer
    for (char* strPos = start + 1; strPos < strEnd; ) {
        if (*strPos == '\\') {
            // 读取当时被跳过的被转义字符 并返回特殊含义
            buffer[realStrLen++] = readEscapeChar(strPos + 1);
            strPos += 2;
        }
        else {
            buffer[realStrLen++] = *strPos++;
        }
    }
    
    // Q: commit[36]: 这里修改的参数是什么意义
    // A: 看待字符串的角度不同

    // 词法: 包含两个双引号 不用考虑 '\0'
    Token* strToken = newToken(TOKEN_STR, start, strEnd + 1);
    // 语法: 必须是一个处理后的合法字符串 最后要有一个 \0
    strToken->tokenType = linerArrayType(TYCHAR_GLOBAL, realStrLen + 1);
    strToken->strContent = buffer;

    // 因为全局处理 所以需要通过 token 传递内容到 object 不然 token 的判断逻辑无法处理 str 和 ident
    return strToken;
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

Token* tokenize(char* P) {
    // 词法分析 根据链表的结构去调用 newToken() 构造
    Token HEAD = {};                 // 声明 HEAD 为空
    Token* currToken = &HEAD;        // 指向 HEAD 类型为 Token 的指针 声明为 currToken 注意是指针哦 通过 '->' 调用
    InputHEAD = P;

    // 此时因为写在了新文件中 所以无法使用全局变量 InputHEAD 就没什么用了 和我 rebase 掉的那个错误是一致的
    // 就是初始化一下   不知道会不会在后面用到

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

        // 对单个字符进行处理 commit[10]    通过 ASCII 编码进行判断
        // commit[11] 通过循环完成对变量名的获取
        if (isIdentIndex1(*P)) {
            char* start = P;
            do {
                P++;
            } while (isIdentIndex(*P));
            
            // 当时这里因为判断条件的限制   只能分辨小写字符    如果是大写的就判断不了了
            currToken->next = newToken(TOKEN_IDENT, start, P);
            // P += length;            目前是单个字符 长度已知为 1 直接 ++ 就好
            currToken = currToken->next;
            continue;
        }

        // commit[34]: 单独处理字符串 否则会作为变量进行处理
        if (*P == '"') {
            currToken->next = readStringLiteral(P);
            currToken = currToken->next;
            P += currToken->length;
            continue;
        }

        // 仅处理运算符
        int opLen = readPunct(P);
        if (opLen) {
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

    // 对完整的 Token 流进行判断 提取关键字
    // 目前看这个 KEYWORD 没有任何作用 可能要到很后期才能体现出来 现在直接注释掉也 ok
    convertKeyWord(HEAD.next);

    // 对直接存在的结构体调用 "成员变量"
    return HEAD.next;
}