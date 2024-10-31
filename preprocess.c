#include "zacc.h"

static bool isHash(Token* tok) {
    // 强调是行首哦
    return (tok->atBeginOfLine && equal(tok, "#"));
}

// commit[160]: 完成对 .h 文件内容的拷贝
static Token* copyToken(Token* tok) {
    Token* newTok = calloc(1, sizeof(Token));
    *newTok = *tok;
    newTok->next = NULL;
    return newTok;
}

// 把头文件的内容拼接到现文件的 Token Stream 中
static Token* append(Token* tokHEAD, Token* tokCurr) {
    if (!tokHEAD || tokHEAD->token_kind == TOKEN_EOF)
        return tokCurr;
    
    Token HEAD = {};
    Token* Curr = &HEAD;

    for (; tokHEAD && tokHEAD->token_kind != TOKEN_EOF; tokHEAD = tokHEAD->next)
        // 为什么一定要 copy 呢  直接把指针链过去不行吗  ???
        Curr = Curr->next = copyToken(tokHEAD);

    Curr->next = tokCurr;
    return HEAD.next;
}

// commit[160]: 支持对头文件的解析
static Token* processDefine(Token* tok) {
    Token HEAD = {};
    Token* Curr = &HEAD;

    while (tok->token_kind != TOKEN_EOF) {
        if (!isHash(tok)) {
            Curr->next = tok;
            Curr = Curr->next;
            tok = tok->next;
            continue;
        }

        tok = tok->next;
        if (equal(tok, "include")) {
            // 以字符串的格式解析包含的头文件  本身对头文件的处理就是直接复制进来就好了
            // 通过 tok 重新赋值跳过 include 语法
            tok = tok->next;
            if (tok->token_kind != TOKEN_STR)
                tokenErrorAt(tok, "expected a filename");
            
            // 取出在字符双引号中的头文件名称  拼装成完整的可以解析的路径
            // Tip: 在前面的路径选择中 是以当前文件所处的路径为基准的
            // 在 tokenize 会把 "xx" 解析成一个完整的 TOKEN_STR  所以会把 "xx" 全部跳过替换为头文件对应的内容
            char* path = format("%s/%s", dirname(strdup(tok->inputFile->fileName)), tok->strContent);
            Token* tokNext = tokenizeFile(path);
            if (!tokNext)
                tokenErrorAt(tok, "%s", strerror(errno));

            tok = append(tokNext, tok->next);
            continue;
        }

        if (tok->atBeginOfLine)
            continue;

        tokenErrorAt(tok, "invalid preprocess directive");
    }

    Curr->next = tok;
    return HEAD.next;
}

Token* preprocess(Token* tok) {
    // commit[159]: 处理宏
    tok = processDefine(tok);

    // commit[158]: 将 tokenize 中对 keyword 的标记转移到 preprocess 实现
    convertKeyWord(tok);
    return tok;
}