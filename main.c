#include "zacc.h"

int main(int argc, char* argv[]) {
    // 首先是对异常状态的判断
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }
    
    // 得到指向输入流的指针
    char* input_ptr = argv[1];
    
    // 把输入流进行标签化 得到 TokenStream 链表     此时没有空格
    Token* input_token = tokenize(input_ptr);

    // 调用语法分析
    Node* currentAST = parse(input_token);

    // 后端生成代码
    codeGen(currentAST);

    return 0;
}