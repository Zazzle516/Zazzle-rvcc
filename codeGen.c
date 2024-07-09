#include "zacc.h"
// 记录当前的栈深度 用于后续的运算合法性判断

static int StackDepth;

static void push_stack(void) {
    printf("  addi sp, sp, -8\n");
    printf("  sd a0, 0(sp)\n");
    StackDepth++;
}

static void pop_stack(char* reg) {
    printf("  ld %s, 0(sp)\n", reg);
    printf("  addi sp, sp, 8\n");
    StackDepth--;
}

// 根据 AST 和目标后端 RISCV-64 生成代码
static void assemblyGen(Node* AST) {
    switch (AST->node_kind)
    {
    case ND_NUM:
        printf("  li a0, %d\n", AST->val);
        return;
    case ND_NEG:
        // 因为是单叉树所以某种程度上也是终结状态   递归找到最后的数字
        assemblyGen(AST->LHS);
        printf("  neg a0, a0\n");
        return;
    default:
        // +|-|*|/ 其余运算 需要继续向下判断
        break;
    }

    assemblyGen(AST->RHS);
    push_stack();
    assemblyGen(AST->LHS);
    pop_stack("a1");        // 把 RHS 的计算结果弹出    根据根节点情况计算

    // 根据当前根节点的类型完成运算
    switch (AST->node_kind)
    {
    case ND_ADD:
        printf("  add a0, a0, a1\n");
        return;
    case ND_SUB:
        printf("  sub a0, a0, a1\n");
        return;
    case ND_MUL:
        printf("  mul a0, a0, a1\n");
        return;
    case ND_DIV:
        printf("  div a0, a0, a1\n");
        return;

    case ND_EQ:
    case ND_NEQ:
        // 汇编层面通过 xor 比较结果
        printf("  xor a0, a0, a1\n");       // 异或结果储存在 a0 中
        if (AST->node_kind == ND_EQ) 
            printf("  seqz a0, a0\n");                   // 判断结果 == 0
        if (AST->node_kind == ND_NEQ)
            printf("  snez a0, a0\n");                   // 判断结果 > 0
        return;

    case ND_GT:
        printf("  sgt a0, a0, a1\n");
        return;
    case ND_LT:
        printf("  slt a0, a0, a1\n");
        return;

    case ND_GE:
        // 转换为判断是否小于
        printf("  slt a0, a0, a1\n");       // 先判断 > 情况
        printf("  xori a0, a0, 1\n");       // 再判断 == 情况
        return;
    case ND_LE:
        printf("  sgt a0, a0, a1\n");
        printf("  xori a0, a0, 1\n");
        return;
    
    default:
        errorHint("invalid expr");
    }
}

void codeGen(Node* AST) {
    printf("  .global main\n");
    printf("main:\n");
    assemblyGen(AST);
    printf("  ret\n");
    assert(StackDepth==0);
}