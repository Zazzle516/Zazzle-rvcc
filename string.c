#include "zacc.h"

char* format(char* Fmt, ...) {
    // 将字面量的匿名定义移动到 string.format() 中实现
    char* buffer;
    size_t bufferLen;

    // 创建一个面向字节的流 ???
    FILE* out = open_memstream(&buffer, &bufferLen);

    va_list VA;
    va_start(VA, Fmt);
    vfprintf(out, Fmt, VA);
    va_end(VA);

    fclose(out);
    return buffer;
}