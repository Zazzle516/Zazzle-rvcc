#include "zacc.h"

int main(int argc, char* argv[]) {
    // 首先是对异常状态的判断
    if (argc != 2) {
        fprintf(stderr, "%s need two arguments\n", argv[0]);
        return 1;
    }

    // commit[34]: launch.json 不好处理有双引号的输入 所以输入写在程序内

    // commit[36]: 同理写在 main.c 中  不过注意和 test.sh 的区别 这里的测试代码同样写在双引号中
    // 也就是说 虽然这个测试代码的目的是 (\a, x, \n, y)  但为了让编译器软件正常执行 需要对 '\' 进行二次转义
    // char code[] = "int main() { return \"\ax\ny\"[3]; }";
    // char code[] = "int main() { return \"\\ax\\ny\"[3]; }";

    // commit[37]: 测试代码
    // char code[] = "int main() { return \"\\1500\"[0]; }";

    // 得到指向输入流的指针

    char* input_ptr = argv[1];
    // char* input_ptr = code;
    
    // 把输入流进行标签化 得到 TokenStream 链表     此时没有空格
    Token* input_token = tokenize(input_ptr);

    // 调用语法分析
    Object* wrapper = parse(input_token);

    // 后端生成代码
    codeGen(wrapper);

    return 0;
}