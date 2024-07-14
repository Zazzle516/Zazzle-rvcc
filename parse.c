#include "zacc.h"

// Q: 在使用 Block 后 目标得到的 AST/Function 的结构是什么
// Program -> ND_BLOCK/CompoundStamt    单个结点作为 AST 的 root

// 定义程序开始
// 解析为复合语句
// program = "{" compoundStamt
// compoundStamt = stamt* "}"

// 在添加 exprStamt 后顶层以单叉树方式递归
// 新增 "return" 关键字
// 新增对 compoundStamt 的支持
// stamt = "return" expr ";" | exprStamt | "{" compoundStamt

// exprStamt = expr->;->expr->;->...

// 添加对标识符的支持后新增 ASSIGN 语句
// expr = assign
// assign = equality (= assign)*                        支持递归性赋值

// 在新增比较符后比较符的运算优先
// equality = relation op relation                      op = (!= | ==)
// relation = first_class_expr op first_class_expr      op = (>= | > | < | <=)
// first_class_expr = second_class_expr (+|- second_class_expr)*
// second_class_expr = third_class_expr (*|/ third_class_expr)
// third_class_expr = (+|-)third_class_expr | primary_class_expr        优先拆分出一个符号作为减法符号
// primary_class_expr = '(' | ')' | num

// commit[11]: 语法规则本身没有变化 主要是根据 Object 的修改
// 定义当前函数栈帧使用到的局部变量链表
Object* Local;

// 当前只有一个匿名 main 函数帧 一旦扫描到一个 var 就检查一下是否已经定义过 如果没有定义过就在链表中新增定义
static Object* findVar(Token* tok) {
    // 找到了返回对应 Object 否则返回 NULL
    for (Object* obj = Local; obj != NULL; obj = obj->next) {
        // 优先判断长度 浅浅的提高一下效率
        if ((strlen(obj->var_name) == tok->length) &&
            !strncmp(obj->var_name, tok->place, tok->length)) {
                return obj;
            }
        // Tip: strncmp() 相比 strcmp() 可以比较指定长度的字符串
    }
    return NULL;
}

// 对没有定义过的变量名 通过头插法新增到 Local 链表中
static Object* newLocal(char* varName) {
    Object* obj = calloc(1, sizeof(Object));
    obj->var_name = varName;
    obj->next = Local;      // 每个变量结点其实都是指向了 Local 链表
    Local = obj;
    return obj;
}


// 定义产生式关系并完成自顶向下的递归调用
static Node* program(Token** rest, Token* tok);
static Node* compoundStamt(Token** rest, Token* tok);
static Node* stamt(Token** rest, Token* tok);
static Node* exprStamt(Token** rest, Token* tok);
static Node* expr(Token** rest, Token* tok);

static Node* assign(Token** rest, Token* tok);

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

// 针对赋值变量(单字符)结点的定义
static Node* singleVarNode(Object* var) {
    Node* varNode = createNode(ND_VAR);
    varNode->var = var;
    return varNode;
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

// 针对 return 结点的定义
static Node* __returnNode(Token* tok) {
    // 注意这里是单叉树哦!  读到最后一个参数后直接返回  RETURN 作为单叉树的叶子结点
    return createSingle(ND_RETURN, expr(&tok, tok->next));
}

// 在不同函数(这里指语法规则函数) 跳转的传参是值传递    (Token* tok) 在传参之后是一个内容相同但是地址不同的拷贝变量
// 所以通过 *rest 记录
// 首先 rest 永远指向上一个语法规则传参的变量的地址   比如 program(rest) -> parse.&tok
// 也可以认为 rest 保留了上一个语法规则的执行断点   所以在调用了新的语法规则(比如新的语法函数) 需要使用 (&tok) 传参更新
// 在递归返回的时候通过 (*rest = tok;) 更新上一个语法规则中的执行环境   在上一个语法规则中通过 (&tok) 读取新的处理位置
// 核心: 通过 (Token** rest) 这个结构保持了全局中对处理状态的更新

static Node* program(Token** rest, Token* tok) {
    // 新增语法规则: 要求最外层语句必须有 "{}" 包裹
    tok = skip(tok, "{");
    Node* ND = compoundStamt(&tok, tok);

    // 注意这里是在语法分析结束后再检查词法的结束正确性
    if (tok->token_kind != TOKEN_EOF) {
        // 因为在 first_class_expr() 执行之前 input_token 仍然指向第一个 token
        // 在 first_class_expr() 执行之后 此时指向最后一个 Token_EOF
        // 不放在这个位置会报错
        tokenErrorAt(tok, "extra Token");
    }

    return ND;
}

static Node* compoundStamt(Token** rest, Token* tok) {
    Node HEAD = {};
    Node* Curr = &HEAD;

    while (!equal(tok, "}")) {
        Curr->next = stamt(&tok, tok);
        Curr = Curr->next;
    }
    // 得到 CompoundStamt 内部的语句链表

    // 新定义一个 ND_BLOCK 指向该链表
    Node* ND = createNode(ND_BLOCK);
    ND->Body = HEAD.next;

    *rest = tok->next;          // 根据 stamt() 的执行更新 tok 

    // Q: 没有更新 ND->var/Local
    // A: 目前的 parse() 不支持变量的作用域访问 全部都是全局变量的形式存在
    return ND;
}

static Node* stamt(Token** rest, Token* tok) {
    // 新增 "return" 关键字的判断后进行修改
    // Q: 是否要考虑提前结束 parse() 的优化     => 直接返回 不去向下递归
    if (equal(tok, "return")) {
        // 在 debug 的时候发现这里不能写成单纯的函数调用 因为 tok 的位置回到 stamt() 的时候并没有更新所以会报错
        // Node* retNode = returnNode(tok);
        // 因为有两个值在变化 Node tok  所以没办法用一个函数调用去处理
        Node* retNode = createSingle(ND_RETURN, expr(&tok, tok->next));
        *rest = skip(tok, ";");
        return retNode;
    }

    if (equal(tok, "{")) {
        // Q: 为什么不是 &tok
        // A: 因为没有 *rest = tok; 的更新      同时把更新放在了 compoundStamt()
        // Node* ND = compoundStamt(rest, tok->next);

        Node* ND = compoundStamt(&tok, tok->next);
        *rest = tok;

        return ND;
    }
    
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
    return assign(rest, tok);
}

// 赋值语句
static Node* assign(Token** rest, Token* tok) {
    Node* ND = equality_expr(&tok, tok);

    // 支持类似于 'a = b = 1;' 这样的递归性赋值
    if (equal(tok, "=")) {
        ND = createAST(ND_ASSIGN, ND, assign(&tok, tok->next));
    }
    *rest = tok;
    return ND;
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

    if ((tok->token_kind) == TOKEN_IDENT) {
        // 先检查是否已经定义过 根据结果执行
        Object* varObj = findVar(tok);
        if (!varObj) {
            // 已经定义过 => 已经在 Local 链表中添加过 => slip 直接声明结点就可以
            // 没有定义过 => 先添加到 Local 链表中

            // strndup() 复制指定长度的字符
            varObj = newLocal(strndup(tok->place, tok->length));
        }
        // 初始化变量结点
        Node* ND = singleVarNode(varObj);       // Tip: 变量结点指向的是链表 Local 对应结点的位置
        *rest = tok->next;
        return ND;
    }

    // 错误处理
    tokenErrorAt(tok, "expected an expr");
    return NULL;
}

// 读入 token stream 在每个 stamt 中处理一部分 token 结点
Function* parse(Token* tok) {
    // 以分号为间隔 每个表达式构成了一个结点 类似于 Token 的方式使用链表存储
    // Node HEAD = {};
    // Node* Curr = &HEAD;

    // while(tok->token_kind != TOKEN_EOF) {
    //     Curr->next = program(&tok, tok);      // 在解析中把 tok 指针位置更新到 ';' 的下一个位置
    //     Curr = Curr->next;                  // 更新下一个 Node 的存储位置
    // }

    // 在更新用 Block 的方式后 在 program() 调用    通过递归的方式完成对语句的循环
    // 生成的 AST 只有一个根节点 而不是像前面的 commit 一样的单链表
    Node* AST = program(&tok, tok);

    Function* Func = calloc(1, sizeof(Function));
    Func->AST = AST;
    Func->local = Local;

    // 对 Func->StackSize 的处理在 codeGen() 实现
    return Func;
}