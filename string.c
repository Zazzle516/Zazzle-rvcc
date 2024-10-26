#include "zacc.h"

// Q: 为什么这个参数格式要这么设计   为了后续集中处理删掉所有临时文件
// Q: 目前只会存储 tmpFilePath 这一个参数吧  猜测后面还有更多的
void strArrayPush(StringArray* Arr, char* S) {
    if (!Arr->paramData) {
        // 如果目前没有分配空间  那么初始化空间为 8 个 char* 指针
        Arr->paramData = calloc(8, sizeof(char*));
        Arr->Capacity = 8;
    }

    if (Arr->Capacity == Arr->paramNum) {
        // 如果已经到达存储上限  那么 *2 扩容
        Arr->paramData = realloc(Arr->paramData, sizeof(char*) * (Arr->Capacity * 2));
        Arr->Capacity *= 2;
        for (int I = Arr->paramNum; I < Arr->Capacity; I++)
            Arr->paramData[I] = NULL;
    }

    Arr->paramData[Arr->paramNum++] = S;
}

char* format(char* Fmt, ...) {
    // 将字面量的匿名定义移动到 string.format() 中实现
    char* buffer;
    size_t bufferLen;

    // 内存流: 可动态调整大小的内存缓冲区
    // 系统会自动管理 open_memstream 分配的内存空间  并且保证缓冲区中的字符串以 '\0' 结尾
    // 内存流的操作类似文件 返回值也是 FILE*  可以使用 vfprintf() fwrite() 之类的操作
    FILE* out = open_memstream(&buffer, &bufferLen);

    va_list VA;
    va_start(VA, Fmt);
    vfprintf(out, Fmt, VA);
    va_end(VA);

    fclose(out);
    
    // 出于编译器的效率  没有使用 free(buffer) 释放空间
    return buffer;
}