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
    // 得到的 input_token 中的 input_token.place 并不是 token 的存储位置    而是输入 ptr 的存储位置
    // Q: 在后续的 Token** rest 中会看到??

    // 调用语法分析
    Function* wrapper = parse(input_token);

    // 后端生成代码
    codeGen(wrapper);

    return 0;
}