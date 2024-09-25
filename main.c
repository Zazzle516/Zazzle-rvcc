#include "zacc.h"

static char* compilResPath;

static char* compilInputPath;

// commit[42]: 无效参数提示
static void rvccIntroduce(int Status) {
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
        return stdout;
    }

    FILE* result = fopen(filePath, "w");
    if (!result)
        // Tip: 这里比较的不是 0 而是 NULL
        errorHint("cannot open output file: %s: %s", filePath, strerror(errno));

    return result;
}


int main(int argc, char* argv[]) {
    // commit[42]: 解析参数
    parseArgs(argc, argv);
    
    // commit[40]: 从给定的文件路径中读取
    Token* input_token = tokenizeFile(compilInputPath);

    // 调用语法分析
    Object* wrapper = parse(input_token);

    // 后端生成代码
    FILE* result = openFile(compilResPath);

    // commit[47]: 为汇编文件添加调试信息  参考 DWARF 索引
    // .file 1 "fileName"  定义数字 1 对应的文件名的映射 建立索引
    fprintf(result, ".file 1 \"%s\"\n", compilInputPath);

    codeGen(wrapper, result);

    return 0;
}
