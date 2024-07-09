#include "zacc.h"

// 在添加 exprStamt 后顶层以单叉树方式递归
// stamt = exprStamt*
// exprStamt = expr->;->expr->;->...

// 在新增比较符后比较符的运算优先
// expr = equality          本质上不需要但是结构清晰
// equality = relation op relation                      op = (!= | ==)
// relation = first_class_expr op first_class_expr      op = (>= | > | < | <=)
// first_class_expr = second_class_expr (+|- second_class_expr)*
// second_class_expr = third_class_expr (*|/ third_class_expr)
// third_class_expr = (+|-)third_class_expr | primary_class_expr        优先拆分出一个符号作为 减法符号
// primary_class_expr = '(' | ')' | num

// 定义产生式关系并完成自顶向下的递归调用
static Node* stamt(Token** rest, Token* tok);
static Node* exprStamt(Token** rest, Token* tok);
static Node* expr(Token** rest, Token* tok);
static Node* equality_expr(Token** rest, Token* tok);       // 针对 (3 < 6 == 5) 需要优先判断 (<)
static Node* relation_expr(Token** rest, Token* tok);
static Node* first_class_expr(Token** rest, Token* tok);
static Node* second_class_expr(Token** rest, Token* tok);
static Node* third_class_expr(Token** rest, Token* tok);
static Node* primary_class_expr(Token** rest, Token* tok);

// 定义 AST 结点  (注意因为 ND_NUM 的定义不同要分开处理)

// 创造新的结点并分配空间
static Node* createNode(NODE_KIND node_kind) {
    Node* newNode = calloc(1, sizeof(Node));
    newNode->node_kind = node_kind;
    return newNode;
}

// 针对数字结点的额外的值定义
static Node* numNode(int val) {
    Node* newNode = createNode(ND_NUM);
    newNode->val = val;
    return newNode;
}

// 定义 AST 的树型结构  (本质上仍然是一个结点但是有左右子树的定义)
static Node* createAST(NODE_KIND node_kind, Node* LHS, Node* RHS) {
    Node* rootNode = createNode(node_kind);
    rootNode->LHS = LHS;
    rootNode->RHS = RHS;
    return rootNode;
}

// 在引入一元运算符后 涉及到单叉树的构造(本质上是看成负号 和一般意义上的 ++|-- 不同)

// 作用于一元运算符的单边树
static Node* createSingle(NODE_KIND node_kind, Node* single_side) {
    Node* rootNode = createNode(node_kind);
    rootNode->LHS = single_side;            // 定义在左子树中
    return rootNode;
}

// Q: rest 是 where and how 起到的作用
// 没什么作用 把 rest 删了不影响函数功能 只是方便跟踪

static Node* stamt(Token** rest, Token* tok) {
    return exprStamt(rest, tok);
}

static Node* exprStamt(Token** rest, Token* tok) {
    // 根据分号构建单叉树
    Node* ND = createSingle(ND_STAMT, expr(&tok, tok));
    // 虽然没用到 rest 但是要更新
    // 后面的语法解析式找不到 ';' 对应的处理规则 会递归回到 exprStamt 进行跳过
    *rest = skip(tok, ";");
    return ND;
}

static Node* expr(Token** rest, Token* tok) {
    return equality_expr(rest, tok);
}

// 相等判断
static Node* equality_expr(Token** rest, Token* tok) {
    Node* ND = relation_expr(&tok, tok);
    while(true) {
        // 比较符号后面不可能还是比较符号
        if (equal(tok, "!=")) {
            ND = createAST(ND_NEQ, ND, relation_expr(&tok, tok->next));
            continue;
        }
        if (equal(tok, "==")) {
            ND = createAST(ND_EQ, ND, relation_expr(&tok, tok->next));
            continue;
        }
        *rest = tok;
        return ND;
    }
}

// 大小判断
static Node* relation_expr(Token** rest, Token* tok) {
    Node* ND = first_class_expr(&tok, tok);

    while(true) {
        if (equal(tok, ">=")) {
            ND = createAST(ND_GE, ND, first_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "<=")) {
            ND = createAST(ND_LE, ND, first_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "<")) {
            ND = createAST(ND_LT, ND, first_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, ">")) {
            ND = createAST(ND_GT, ND, first_class_expr(&tok, tok->next));
            continue;
        }
        *rest = tok;
        return ND;
    }
}

// 加减运算判断
static Node* first_class_expr(Token** rest, Token* tok) {
    // 处理最左表达式
    Node* ND = second_class_expr(&tok, tok);

    while(true) {
        // 如果仍然有表达式的话
        if (equal(tok, "+")) {
            // 构建 ADD 根节点 (注意使用的是 tok->next) 并且
            ND = createAST(ND_ADD, ND, second_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "-")) {
            // 同理建立 SUB 根节点
            ND = createAST(ND_SUB, ND, second_class_expr(&tok, tok->next));
            continue;
        }

        // 表达式解析结束   退出
        *rest = tok;        // rest 所指向的指针 = tok  rest 仍然维持一个 3 层的指针结构
        return ND;
    }
}

// 乘除运算判断
static Node* second_class_expr(Token** rest, Token* tok) {
    Node* ND = third_class_expr(&tok, tok);

    while(true) {
        if (equal(tok, "*")) {
            ND = createAST(ND_MUL, ND, third_class_expr(&tok, tok->next));
            continue;
        }

        if (equal(tok, "/")) {
            ND = createAST(ND_DIV, ND, third_class_expr(&tok, tok->next));
            continue;
        }

        *rest = tok;
        return ND;
    }
}

// 对一元运算符的单边递归
static Node* third_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "+")) {
        // 正数跳过
        return third_class_expr(rest, tok->next);
    }

    if (equal(tok, "-")) {
        // 作为负数标志记录结点
        Node* ND = createSingle(ND_NEG, third_class_expr(rest, tok->next));
        return ND;
    }

    // 直到找到一个非运算符
    return primary_class_expr(rest, tok);
}

// 判断子表达式或者数字
static Node* primary_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "(")) {
        // 递归调用顶层表达式处理
        Node* ND = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return ND;
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value);
        *rest = tok->next;
        return ND;
    }

    // 错误处理
    tokenErrorAt(tok, "expected an expr");
    return NULL;
}

// 读入 token stream 在每个 stamt 中处理一部分 token 结点
Node* parse(Token* tok) {
    // 以分号为间隔 每个表达式构成了一个结点 类似于 Token 的方式使用链表存储
    Node HEAD = {};
    Node* Curr = &HEAD;

    while(tok->token_kind != TOKEN_EOF) {
        Curr->next = stamt(&tok, tok);      // 在解析中把 tok 指针位置更新到 ';' 的下一个位置
        Curr = Curr->next;                  // 更新下一个 Node 的存储位置
    }

    // 注意这里是在语法分析结束后再检查词法的结束正确性
    if (tok->token_kind != TOKEN_EOF) {
        // 因为在 first_class_expr() 执行之前 input_token 仍然指向第一个 token
        // 在 first_class_expr() 执行之后 此时指向最后一个 Token_EOF
        // 不放在这个位置会报错
        tokenErrorAt(tok, "extra Token");
    }
    return HEAD.next;
}