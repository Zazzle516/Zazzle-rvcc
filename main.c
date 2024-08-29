#include "zacc.h"

static char* compilResPath;

static char* compilInputPath;

// commit[42]: 针对 '--help' 选项的声明
static void usage(int Status) {
    // Tip: 从这里能看出来 [-o tmp.s] 是一组参数
    fprintf(stderr, "zacc [ -o <path> ] <file>\n");
    exit(Status);
}

// commit[42]: 对 ./rvcc 的两个参数进行解析  并将输出结果传递到文件(结合 test.sh 来理解)
static void parseArgs(int argc, char** argv) {
    // Tip: argc 记录所有参数个数包括文件名自己  所以从 I = 1 开始
    for (int I = 1; I < argc; I ++) {

        // 解析 '--help' 参数
        if (!strcmp(argv[I], "--help"))
            usage(0);
        
        // 结合 test.sh [-o tmp.s] 是一组参数
        // Q: 它虽然是一组参数  但是在 argv 的存储具体是什么样呢
        if (!strcmp(argv[I], "-o")) {
            if (!argv[++I])
                // Q: 这里是认为没有传入路径吗
                usage(1);

            compilResPath = argv[I];
            continue;
        }

        // 类似 解析 [-otmp.s] 参数 (没有空格分隔)
        if (!strncmp(argv[I], "-o", 2)) {
            compilResPath = argv[I] + 2;
            continue;
        }

        if (argv[I][0] == '-' && argv[I][1] != '\0')
            error("unknown argument: %s\n", argv[I]);

        // 其他情况  匹配为输入文件
        compilInputPath = argv[I];

        if (!compilInputPath)
            error("no input file\n");
    }
}

// commit[42]: 尝试打开写入文件
static FILE* openFile(char* filePath) {
    if (!filePath || strcmp(filePath, "-") == 0)
        // 如果没有给输出定义输出文件  那么输出到控制台
        return stdout;

    FILE* result = fopen(filePath, "w");
    if (!result)
        // Tip: 这里比较的不是 0 而是 NULL
        error("cannot open output file: %s: %s", filePath, strerror(errno));

    return result;
}


int main(int argc, char* argv[]) {
    // 首先是对异常状态的判断
    // if (argc != 2) {
    //     fprintf(stderr, "%s need two arguments\n", argv[0]);
    //     return 1;
    // }

    // commit[34]: launch.json 不好处理有双引号的输入 所以输入写在程序内
    // char code[] = "int main() { return \"\\a123456\"[0]; }";

    // commit[36]: 同理写在 main.c 中  不过注意和 test.sh 的区别 这里的测试代码同样写在双引号中
    // 也就是说 虽然这个测试代码的目的是 (\a, x, \n, y)  但为了让编译器软件正常执行 需要对 '\' 进行二次转义
    // char code[] = "int main() { return \"\ax\ny\"[3]; }";
    // char code[] = "int main() { return \"\\ax\\ny\"[3]; }";

    // commit[37]: 测试代码
    // char code[] = "int main() { return \"\\1500\"[0]; }";

    // 得到指向输入流的指针

    // char* input_ptr = argv[1];
    // char* input_ptr = code;

    // commit[42]: 解析参数
    parseArgs(argc, argv);
    
    // 把输入流进行标签化 得到 TokenStream 链表     此时没有空格
    // commit[40]: 从给定的文件路径中读取
    Token* input_token = tokenizeFile(compilInputPath);

    // 调用语法分析
    Object* wrapper = parse(input_token);

    // 后端生成代码
    FILE* result = openFile(compilResPath);
    codeGen(wrapper, result);

    return 0;
}