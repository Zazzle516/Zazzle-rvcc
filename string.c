#include "zacc.h"

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