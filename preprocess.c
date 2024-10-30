#include "zacc.h"

static bool isHash(Token* tok) {
    // 强调是行首哦
    return (tok->atBeginOfLine && equal(tok, "#"));
}

// commit[159]: 目前如果在 # 仍有内容的话会报错
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
        
        // 只在语法层面解析到宏定义  目前没有任何处理  直接跳过
        tok = tok->next;
        if (tok->atBeginOfLine)
            continue;

        tokenErrorAt(tok, "invalid preprocess directive");
    }

    Curr->next = tok;
    return HEAD.next;
}

Token* preprocess(Token* tok) {
    // commit[159]: 处理宏和指示 directive
    tok = processDefine(tok);

    // commit[158]: 将 tokenize 中对 keyword 的标记转移到 preprocess 实现
    convertKeyWord(tok);
    return tok;
}