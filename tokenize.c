#include "zacc.h"

// commit[136]: 进行 C 关键字的忽略
// auto: 在 C 中所有局部变量默认就是 auto 类型  和 C++ 的自动推导类型区分
// volatile: 该变量的值随时可能被外部修改 eg. 硬件设备 信号 多线程  禁止编译器进行优化
// register: 建议编译器将该变量存储在 cpu 中  提高效率
// _Noreturn: 修饰无返回函数 C11 新特性
// restrict: 通知编译器 指针是访问该对象的唯一手段  允许编译器进行更多优化 (int* restrict)
// __restrict: GNU 扩展
// __restrict__: GNU 扩展

// 全局变量 记录输入流的起始位置(方便后续找到具体的错误位置)
// 写在最上面 如果只是定义在 main() 上面的话 tokenize() 找不到
static char* InputHEAD;

// commit[40]: 记录输入的文件名  用于在错误信息提示时打印错误文件名
static char* currentFileName;

// commit[159]: 判断当前是否在行首
static bool isAtBeginOfLine;

// 极简版错误提示  一般用于系统错误
void errorHint(char* errorInfo, ...) {
    va_list Info;
    va_start(Info, errorInfo);

    fprintf(stderr, errorInfo, Info);
    fprintf(stderr, "\n");

    va_end(Info);
    exit(1);
}

// commit[40]: 记录错误在文件中的位置 并打印错误信息
void errorAt(int errorLineNum, char* place, char* FMT, va_list VA) {

    // 找到 errorLineStart 最开始的位置
    char* errorLineStart = place;
    while (InputHEAD < errorLineStart && errorLineStart[-1] != '\n')
        errorLineStart--;

    // 找到 errorLineStart 结束的位置
    char* errorLineEnd = place;
    while (*errorLineEnd != '\n')
        errorLineEnd++;

    // commit[46]: 在 zacc.h 中添加行号信息后 优化掉这段公共计算
    // 找到整个文件中 errorLine 的行号
    // int errorLine = 1;
    // for (char* P = InputHEAD; P < errorLineStart; P ++)
    //     if (*P == '\n')
    //         errorLine++;

    // 对错误信息进行格式处理
    int charNum = fprintf(stderr, "%s:%d: ", currentFileName, errorLineNum);
    fprintf(stderr, "%.*s\n", (int)(errorLineEnd - errorLineStart), errorLineStart);

    int err_place = place - errorLineStart + charNum;

    // 通过 '%*s' 表示该输出字符串长度可变
    fprintf(stderr, "%*s", err_place, "");

    // 打印错误标志
    fprintf(stderr, "^ ");

    // 打印错误信息
    vfprintf(stderr, FMT, VA);
    fprintf(stderr, "\n");

    // 在这里统一清除变长参数
    va_end(VA);

    exit(1);
}


// 对错误信息的具体处理     针对 数字/符号 抽象出一层用来调用 errorAt()

// 针对 token 的报错  只会在 parse 层被调用
void tokenErrorAt(Token* token, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    // commit[46]: 在 struct token 添加了 lineNUm 属性后通过空间提高了效率
    errorAt(token->LineNum, token->place, FMT, VA);
}

// 针对 char 的报错  只会在 tokenize 层调用
void charErrorAt(char* place, char* FMT, ...) {
    va_list VA;
    va_start(VA, FMT);

    // commit[46]: 因为在 tokenize 层 token 是生成目标  所以通过编译的方式得到错误的字符位置
    int errorLineNum = 1;
    for (char* P = InputHEAD; P < place; P ++)
        if (*P == '\n')
            errorLineNum ++;

    errorAt(errorLineNum, place, FMT, VA);
}

/* 工具函数声明 */
static bool strCmp(char* input_ptr, char* target);
static Token* readIntNumLiteral(char* start);

// 关键字声明
static bool isKeyWords(Token* input) {
    static char* keywords[] = {"if", "else", "return", "for", "while","sizeof", "static", "goto",
                                "break", "continue", "switch", "case", "default",
                                "char", "struct", "union", "_Bool", "enum",
                                "long", "short", "void", "typedef", "int", "extern",
                                "_Alignof", "_Alignas", "do", "signed", "unsigned",
                                "const", "volatile", "auto", "register", "restrict", "__restrict",
                                "__restrict__", "_Noreturn", "float", "double",
                            };

    for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i ++) {
        if (equal(input, keywords[i]))
            return true;
    }
    return false;
}

// 得到所有 Tokens 后针对 KEYWORD 进行判断
void convertKeyWord(Token* input_token) {
    for (Token* tok = input_token; tok->token_kind != TOKEN_EOF; tok = tok->next) {
        if (isKeyWords(tok)) {
            tok->token_kind = TOKEN_KEYWORD;
        }
    }
}

// 判断当前 Token 与指定 target 是否相同
bool equal(Token* input_token, char* target) {
    return memcmp(input_token->place, target, input_token->length) == 0 && target[input_token->length] == '\0';
}

// 指定 target 去跳过
Token* skip(Token* input_token, char* target) {
    if (!equal(input_token, target)) {
        tokenErrorAt(input_token, "expected: '%s'", target);
    }
    return input_token->next;
}

// commit[22]: comsume 处理数量不确定的 tok
bool consume(Token** rest, Token* tok, char* str) {
    // 可能没有目标 tok 可能有 1 个
    if (equal(tok, str)) {
        *rest = tok->next;
        return true;
    }
    *rest = tok;
    return false;
}

// 定义一个新的 Token 结点挂载到链表上
static Token* newToken(TokenKind token_kind, char* start, char* end) {
    // 在传参的时候实际上只能看到一段内存空间
    Token* currToken = calloc(1, sizeof(Token));
    currToken->token_kind = token_kind;
    currToken->place = start;
    currToken->length = end - start;

    // 记录当前 token 的位置状态并重置到默认 非行首 状态
    // 只有在 tokenize 中读取到 '\n' 后才会覆盖
    currToken->atBeginOfLine = isAtBeginOfLine;
    isAtBeginOfLine = false;
    return currToken;

    // 为了编译器的效率 这里分配的空间并没有释放
}

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

// commit[38]: 判断一位十六进制数字并转换到十进制
static int fromHex(char hexNumber) {
    if (hexNumber >= '0' && hexNumber <= '9')
        return hexNumber - '0';
    if (hexNumber >= 'a' && hexNumber <= 'f')
        return hexNumber - 'a' + 10;
    return hexNumber - 'A' + 10;
}

// commit[139]: 词法解析浮点数常量
static Token* readNumber(char* start) {
    // strchr: 查找第一个匹配的字符，并返回该字符在字符串中的位置
    // 反向判断当前指向的字符是否是 ".eEfF" 中的一个 从而判断是否属于浮点数

    // strtod: 将字符串转换为双精度浮点数
    // 如果整个字符串成功转换 end 会指向结尾的 '\0'  如果失败  返回 0.0 并指向 start 本身

    // 解析 "." 前面的数字(不一定正确  但是允许进制声明
    // 此时 start 的位置指向数字后半的开始位置
    Token* tok = readIntNumLiteral(start);
    if (!strchr(".eEfF", start[tok->length])) 
        return tok;

    char* End;
    double Val = strtod(start, &End);
    Type* floatSuffix;
    if (*End == 'f' || *End == 'F') {
        floatSuffix = TYFLOAT_GLOBAL;
        End++;
    }

    else if (*End == 'l' ||*End == 'L') {
        floatSuffix = TYDOUBLE_GLOBAL;
        End++;
    }

    else {
        floatSuffix = TYDOUBLE_GLOBAL;  // 如果没有后缀  默认 double
    }

    // 覆盖前半判断出的类型
    tok = newToken(TOKEN_NUM, start, End);
    tok->FloatValue = Val;
    tok->tokenType = floatSuffix;
    return tok;
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

// commit[36]: 处理字符串中的转义字符  所有转义结果都必须能被 ASCII 表示
static int readEscapeChar(char** newPos, char* escapeChar) {
    // commit[37]: 针对八进制数字返回十进制结果
    // 被转义的八进制数字至多三个数字  并且每个数字小于等于 7 通过减 '0' 得到真实的整数值
    if ('0' <= *escapeChar && *escapeChar <= '7') {
        int octNumber = *escapeChar - '0';
        escapeChar++;
        if ('0' <= *escapeChar && *escapeChar <= '7') {
            // C1 在处理后要左移三位为 C2 的计算留出空间    再通过减 '0' 来计算数值
            octNumber = (octNumber << 3) + (*escapeChar - '0');
            escapeChar++;
            if ('0' <= *escapeChar && *escapeChar <= '7') {
                octNumber = (octNumber << 3) + (*escapeChar - '0');
                escapeChar++;
            }
        }

        // 需要通过 指向 escapeChar 的指针的指针来记录解析位置并返回
        // 此时 escapeChar 已经指向八进制数字的下一个字符
        *newPos = escapeChar;
        return octNumber;
    }

    // commit[38]: 针对十六进制返回十进制结果
    // 被转义的十六进制数字对长度没有限制 在字母表示部分的大小写都需要处理
    if (*escapeChar == 'x') {
        // 'x' 作为十六进制数字标志会被跳过
        escapeChar++;

        // Tip: isxdigit() 标准 C 库提供判断十六进制数字
        if (!isxdigit(*escapeChar))
            charErrorAt(escapeChar, "invalied hex escape sequence\n");

        int hexNumber = 0;
        // Tip: 因为没有对十六进制数字长度有所限制  如果被转义的十六进制 它的超过了 ASCII 表示范围  执行结果是不可预测的
        for (; isxdigit(*escapeChar); escapeChar++) {
            hexNumber = (hexNumber << 4) + fromHex(*escapeChar);
        }
        *newPos = escapeChar;
        return hexNumber;
    }

    // 同理因为要返回 2 个内容  所以通过 char** 来更新解析位置
    // case1: 被转义的内容  通过 switch 完成
    // case2: 更新 input_ptr 的指向
    *newPos = escapeChar + 1;

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
            // 类似 rest 同理通过 &strPos 更新解析位置
            buffer[realStrLen++] = readEscapeChar(&strPos, strPos + 1);

            // commit[37]: 更新数字进制后被转义的长度不确定
            // strPos += 2;
        }
        else {
            buffer[realStrLen++] = *strPos++;
        }
    }
    
    // Q: commit[36]: 这里修改的参数是什么意义
    // A: 看待字符串的角度不同  从词法的角度 字符本身没有意义

    // 词法: 包含两个双引号 不用考虑 '\0'
    Token* strToken = newToken(TOKEN_STR, start, strEnd + 1);
    // 语法: 必须是一个处理后的合法字符串 最后要有一个 \0
    strToken->tokenType = linerArrayType(TYCHAR_GLOBAL, realStrLen + 1);
    strToken->strContent = buffer;

    // 因为全局处理 所以需要通过 token 传递内容到 object 不然 token 的判断逻辑无法处理 str 和 ident
    return strToken;
}

// commit[73]: 读取字符字面量
static Token* readCharLiteral(char* start) {
    char* P = start + 1;    // 记录单引号后面的断点

    if (*P == '\0')
        charErrorAt(start, "unclosed char literal");

    // Tip: 是字符! 一个字符!  所以没有循环判断  只读一个之后就可以判断是否结束了
    char C;
    if (*P == '\\')
        C = readEscapeChar(&P, P + 1);
    else
        C = *P++;

    // 在字符串中查找指定的字符，并返回一个指向该字符第一次出现位置的指针
    char* end = strchr(P, '\'');
    if (!end)
        // 空的字符报错位置 ''
        charErrorAt(P, "unclosed char literal");

    // 通过 ASCII 的方式进行存储
    Token* tok = newToken(TOKEN_NUM, start, end + 1);
    tok->value = C;

    // commit[132]: 针对字符字面量默认使用 INT 类型  如果有自定义后续会覆盖
    tok->tokenType = TYINT_GLOBAL;
    return tok;
}

// commit[80]: 处理 二进制 八进制 十六进制的数字字面量
static Token* readIntNumLiteral(char* start) {
    char* P = start;    // 记录起始位置  用于后面的长度计算
    int Base = 10;      // 默认十进制

    // Tip: 因为同样参与了浮点数前半的判断  所以可能无法正确解析  会在 readNumber 中覆盖

    // strncasecmp: 比较两个字符串的前 n 个字符，而不区分大小写
    // isalnum: 判断一个字符是否是字母或数字

    // 处理不同进制前缀
    // Tip: strtoul() 会根据 Base 解析所有合法的数字  直到遇到不合法数字存在 *P 中
    // 如果被解析对象本身非法  则返回 0 但是 0 本身也返回 0  面对这种情况需要提前对被解析对象合法化判断  防止后面混淆
    if (!strncasecmp(P, "0x", 2) && isxdigit(P[2])) {
        P += 2;
        Base = 16;
    }

    else if (!strncasecmp(P, "0b", 2) && (P[2] == '0' || P[2] == '1')) {
        P += 2;
        Base = 2;
    }

    else if (*P == '0') {
        Base = 8;
    }

    // 处理真正的数字阶段 + 真正的合法性判断
    int64_t numVal = strtoul(P, &P, Base);

    // commtiit[132]: L 后缀表示 LONG   U 后缀表示 Unsigned
    bool Suffix_L = false;
    bool Suffix_U = false;

    // 组合后缀 (ll | u) | (l | u) | (ll) | (l) | (u)
    if (strCmp(P, "LLU") || strCmp(P, "LLu") || strCmp(P, "llU") || strCmp(P, "llu") ||
        strCmp(P, "ULL") || strCmp(P, "Ull") || strCmp(P, "uLL") || strCmp(P, "ull")) {
            P += 3;
            Suffix_L = Suffix_U = true;
    }

    else if (!strncasecmp(P, "lu", 2) || !strncasecmp(P, "ul", 2)) {
        P += 2;
        Suffix_L = Suffix_U = true;
    }

    else if (strCmp(P, "LL") || strCmp(P, "ll")) {
        P += 2;
        Suffix_L = true;
    }

    else if (*P == 'L' || *P == 'l') {
        P += 1;
        Suffix_L = true;
    }

    else if (*P == 'U' || *P == 'u') {
        P += 1;
        Suffix_U = true;
    }

    // 在后缀匹配完成后不应该还有数字  这里进行语法检查
    // commit[139]: 如果是浮点数  此时是 '.' 不能进行报错判定
    // if (isalnum(*P))
    //     charErrorAt(P, "invalid digit");

    Type* numType;
    if (Base == 10) {   // 为什么十进制和其他进制分开讨论呢  都是移位其他进制却可以放在一起
        if (Suffix_L && Suffix_U)
            numType = TY_UNSIGNED_LONG_GLOBAL;
        else if (Suffix_L)
            numType = TYLONG_GLOBAL;
        else if (Suffix_U)
            numType = (numVal >> 32) ? TY_UNSIGNED_LONG_GLOBAL : TY_UNSIGNED_INT_GLOBAL;
        else
            // 没有任何后缀的情况  根据大小默认有符号
            numType = (numVal >> 31) ? TYLONG_GLOBAL : TYINT_GLOBAL;
    }

    else {
        if (Suffix_L && Suffix_U)
            numType = TY_UNSIGNED_LONG_GLOBAL;
        else if (Suffix_L)
            numType = (numVal >> 63) ? TY_UNSIGNED_LONG_GLOBAL : TYLONG_GLOBAL;
        else if (Suffix_U)
            numType = (numVal >> 32) ? TY_UNSIGNED_LONG_GLOBAL : TY_UNSIGNED_INT_GLOBAL;
        
        // 无后缀情况(虽然很奇怪...  确实改变了解析方式但是保证了数字的安全
        // case1: 符号位为 1：说明这个数是负数，所以需要将它解释为 unsigned long（避免溢出）
        // case2: 符号位为 0：说明这个数是非负数，可以直接使用 long 类型
        else if (numVal >> 63)
            numType = TY_UNSIGNED_LONG_GLOBAL;
        else if (numVal >> 32)
            numType = TYLONG_GLOBAL;
        else if (numVal >> 31)
            numType = TY_UNSIGNED_INT_GLOBAL;
        else
            numType = TYINT_GLOBAL;
    }

    Token* tok = newToken(TOKEN_NUM, start, P);
    tok->value = numVal;
    tok->tokenType = numType;
    return tok;
}

// 比较字符串是否相等   和 equal() 中的 memcmp() 区分
static bool strCmp(char* input_ptr, char* target) {
    return (strncmp(input_ptr, target, strlen(target)) == 0);
}

static int readPunct(char* input_ptr) {
    // 同理通过比较内存的方式   优先判断长度更长的运算符

    // commit[53]: 类似于关键字判断 虽然目前这些符号本身是等长的  但是后面可能不一定
    // Tip: commit[94] 移位运算的复合运算符判断 ">>=" "<<=" 必须在最前面  否则在判断的时候会提前进入 ">>|<<" 的判断
    static char* keyPunct[] = {"<<=", ">>=", "...",
                               "==", "!=", "<=", ">=", "->",
                               "+=", "-=", "*=", "/=", "++", "--",
                               "%=", "^=", "|=", "&=", "&&", "||",
                               ">>", "<<"};

    for (int I = 0; I < sizeof(keyPunct) / sizeof(*keyPunct); ++I) {
        if (strCmp(input_ptr, keyPunct[I]))
            return strlen(keyPunct[I]);
    }

    return (ispunct(*input_ptr) ? 1: 0);
}

// commit[46]: 计算 token 的行号
static void calcuLineNum(Token* tok) {
    int lineNum = 1;        // 预测下一次赋值的行号  lineNum 提前 P 一行 ??
    char* P = InputHEAD;

    while (*P) {
        // Tip: 如果是个很长的单词 需要完全走过一遍才能进入下一个 token 效率不高但是实现简单
        if (P == tok->place) {
            tok->LineNum = lineNum;
            tok = tok->next;
        }

        if (*P == '\n')
            lineNum++;
        P++;
    }
}

Token* tokenize(char* fileName, char* P) {
    Token HEAD = {};
    Token* currToken = &HEAD;
    InputHEAD = P;

    // commit[40]: 更新当前读取的文件路径 + 文件名
    currentFileName = fileName;

    // 此时因为写在了新文件中 所以无法使用全局变量 InputHEAD 就没什么用了 和我 rebase 掉的那个错误是一致的
    // 就是初始化一下   不知道会不会在后面用到

    isAtBeginOfLine = true;

    while(*P) {
        // commit[43]: 跳过行注释
        if (strCmp(P, "//")) {
            P += 2;
            while (*P != '\n')
                P++;
            continue;
        }

        // commit[43]: 跳过块注释
        if (strCmp(P, "/*")) {
            char* rightClose = strstr(P + 2, "*/");  // strstr() 查找子串首次出现的位置
            if (!rightClose)
                charErrorAt(P, "unclosed block annotations\n");

            P = rightClose + 2;     // 更新指向块注释右侧后的字符
            continue;
        }

        // commit[73]: 通过单引号判断字符字面量  Tip: 单引号本身的判断需要 \ 转义
        if (*P == '\'') {
            currToken->next = readCharLiteral(P);
            currToken = currToken->next;

            // 虽然 commit[73] 只解析一个字符  但是字符本身可能是转义的  长度不一定为 1
            P += currToken->length;
            continue;
        }

        // commit[159]: 判断当前是否到了行首
        if (*P == '\n') {
            P ++;
            isAtBeginOfLine = true;
            continue;
        }

        if (isspace(*P)) {
            // 跳过空格 \t \n \v \f \r
            P ++;
            continue;
        }

        // 仅处理数字
        // commit[139]: 新增对浮点数判断
        if (isdigit(*P) || (*P == '.' && isdigit(P[1]))) {
            // commit[80]: 抽象在 readIntNumLiteral() 实现
            currToken->next = readNumber(P);

            currToken = currToken->next;
            P += currToken->length;     // 类似 根据具体长度进行更新  包括符号
            continue;
        }

        // commit[11] 通过循环完成对变量名的获取
        if (isIdentIndex1(*P)) {
            char* start = P;
            do {
                P++;
            } while (isIdentIndex(*P));
            
            // 当时这里因为判断条件的限制   只能分辨小写字符    如果是大写的就判断不了了
            // 这里无论是变量名称还是类型  都会是 TOKEN_IDENT 定义
            currToken->next = newToken(TOKEN_IDENT, start, P);
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
            P += opLen;
            currToken = currToken->next;
            continue;
        }

        charErrorAt(P, "Wrong Syntax: %c", *P);
    }

    // 添加终结符
    currToken->next = newToken(TOKEN_EOF, P, P);

    // commit[46]: 为每个 Token 分配行号
    calcuLineNum(HEAD.next);

    return HEAD.next;
}

// commit[40]: 根据文件路径读取文件内容
static char* readFile(char* filePath) {
    FILE* Fp;

    // 文件读取方式判断
    if (strcmp(filePath, "-") == 0) {
        // Q: 如果文件名是 '-' 那么从输入中读取
        // A: 结合 test.sh 来判断 (./rvcc -) 作为第一个参数否定了文件读取
        Fp = stdin;
    }

    else {
        Fp = fopen(filePath, "r");
        if (!Fp)
            // errno: 全局的错误编号  根据标准库设置
            // 通过 strerror() 把编号转换为一串描述性语言
            errorHint("cannot open %s: %s", filePath, strerror(errno));
    }

    /* 无论是 stdin 或者 FILE 都是写入文件的 */

    // 准备好 Fp 读取后写入的空间
    char* fileBuffer;
    size_t fileBufferLength;
    FILE* fileContent = open_memstream(&fileBuffer, &fileBufferLength);

    // 开始读取
    // Q: 这里使用两个缓冲区进行完成文件流的读写  有什么好处吗
    // A: 好处基本是批量处理数据和减少 fwrite() 之类的系统调用
    // 二级缓冲区的大小也可以手动调整  优化使用场景之类的
    while(true) {
        char fileBufferBack[4096];
        int N = fread(fileBufferBack, 1, sizeof(fileBufferBack), Fp);

        if (N == 0)
            break;
        fwrite(fileBufferBack, 1, N, fileContent);
    }

    // 读取结束  关掉  <读文件>
    // 针对 Fp 的类型 (stdin | FILE) 判断   stdin 本身是由系统启动的 不应该由程序随意关闭
    if (Fp != stdin)
        fclose(Fp);

    // fwrite() 为了效率通常都是先写到一个缓冲区的 只有显式调用 fclose() 或者 fflush() 之后才会写入文件
    // 刷新 <写文件>
    fflush(fileContent);
    // Q: 这个长度是怎么记录的
    // A: 以 byte 计数  累计到最后一个有效字节的 <下一个可写入位置>
    if (fileBufferLength == 0 || fileBuffer[fileBufferLength - 1] != '\n')
        fputc('\n', fileContent);
    
    // 写入结束  关掉 <写文件>
    fputc('\0', fileContent);
    fclose(fileContent);

    return fileBuffer;
}

Token* tokenizeFile(char* filePath) {
    return tokenize(filePath, readFile(filePath));
}
