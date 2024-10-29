#include "zacc.h"

Token* preprocess(Token* tok) {
    // 将 tokenize 中对 keyword 的标记转移到 preprocess 实现
    convertKeyWord(tok);
    return tok;
}