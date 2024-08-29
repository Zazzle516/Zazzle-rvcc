#include "zacc.h"

static char* compilResPath;

static char* compilInputPath;

// commit[42]: 针对 '--help' 选项的声明
static void rvccIntroduce(int Status) {
    // Tip: 从这里能看出来 [-o tmp.s] 是一组参数
    fprintf(stderr, "zacc [ -o <outPutPath> ] <file>\n");
    exit(Status);
}

// commit[42]: 对 ./rvcc 的传参进行解析  结合 test.sh 和 testDriver.sh 判断
static void parseArgs(int argc, char** argv) {
    // Tip: argc 记录所有参数个数包括文件名自己  所以从 I = 1 开始
    // commit[42]: argv 是一个指向字符串指针的指针(二维数组)  每个元素 argv[I] 都是一个指向参数字符串的指针
    for (int I = 1; I < argc; I ++) {

        // testDriver.sh: ./rvcc --help
        if (!strcmp(argv[I], "--help")) {
            rvccIntroduce(0);
        }
        
        // test.sh: ./rvcc -o tmp.s -
        // testDriver.sh: ./rvcc -o "$tmp/out"
        // 区别在于输入的方式 test.sh 采用 stdin 而 testDirver.sh 采用 FILE
        if (!strcmp(argv[I], "-o")) {
            if (!argv[++I])
                // 解析下一个参数 判断输出文件是否定义
                rvccIntroduce(1);

            compilResPath = argv[I];
            continue;
        }

        // 类似 基于省略空格的判断
        if (!strncmp(argv[I], "-o", 2)) {
            // 这里比较的长度 2 是 "-o" 的长度
            // 因为两个参数没有空格 所以在 argv 中是同一个指针
            compilResPath = argv[I] + 2;
            continue;
        }

        // 目前 zacc 不支持的编译参数
        if (argv[I][0] == '-' && argv[I][1] != '\0') {
            errorHint("unknown argument: %s\n", argv[I]);
        }

        // 其他情况  匹配为输入文件
        compilInputPath = argv[I];

        // 最后是固定的输入文件的位置
        if (!compilInputPath)
            errorHint("no input file\n");
    }
}

// commit[42]: 尝试打开输出文件
static FILE* openFile(char* filePath) {
    if (!filePath || strcmp(filePath, "-") == 0) {
        // 如果没有给输出定义输出文件  并且 stdout 判定那么输出到控制台
        // Q: 理论上应该不会被调用
        printf("control table\n");
        return stdout;
    }

    FILE* result = fopen(filePath, "w");
    if (!result)
        // Tip: 这里比较的不是 0 而是 NULL
        errorHint("cannot open output file: %s: %s", filePath, strerror(errno));

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