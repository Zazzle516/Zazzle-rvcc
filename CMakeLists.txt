cmake_minimum_required( VERSION 3.10.0)

project(rvcc C)

add_executable( rvcc
    main.c
    type.c
    string.c
    tokenize.c
    parse.c
    codeGen.c
)

target_compile_options(rvcc PRIVATE -std=c11 -g -fno-common)