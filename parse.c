#include "zacc.h"

// commit[1] - commit[22] 注释备份在 annotation-bak 中

// commit[25]: 最顶层语法规则更新为函数定义
// parse() = functionDefinition*
// __program = "{" compoundStamt

// commit[25]: 函数签名定义 目前只支持 'int' 并且无参  eg. int* funcName() {...}
// functionDefinition = declspec declarator "{" compoundStamt*
// declspec = "int"
// declarator = "*"* ident typeSuffix
// typeSuffix = ("(" ")")?

// compoundStamt = (declaration | stamt)* "}"

// commit[22]: 声明语句的语法定义 支持连续定义
// int a, b; | int a, *b; | int a = 3, b = expr;
// declspec: "int" 类型声明     declarator: "*" 指针声明
// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"

// stamt = "return" expr ";"
//          | exprStamt
//          | "{" compoundStamt
//          | "if" "(" cond-expr ")" stamt ("else" stamt)?
//          | "for" "(" exprStamt expr? ";" expr? ")" "{" stamt
//          | "while" "(" expr ")" stamt

// exprStamt = expr? ";"
// expr = assign
// assign = equality (= assign)*                        支持递归性赋值

// equality = relation op relation                      op = (!= | ==)
// relation = first_class_expr op first_class_expr      op = (>= | > | < | <=)
// first_class_expr = second_class_expr (+|- second_class_expr)*
// second_class_expr = third_class_expr (*|/ third_class_expr)

// third_class_expr = (+|-|&|*)third_class_expr | primary_class_expr        优先拆分出一个符号作为减法符号

// commit[23]: 添加对零参函数名声明的支持
// commit[24]: 添加对有参函数签名的支持
// Tip: 这里处理参数的语法规则有 ident 重叠 因为不确定是函数声明还是变量 所以要往前看一个字符
// primary_class_expr = '(' expr ')' | num | ident fun_args?

// 这里的处理是在 primary_class_expr 前看一个字符确认是函数声明后的定义
// funcall = ident "(" (expr ("," expr)*)? ")"

Object* Local;

// 当前只有一个匿名 main 函数帧 一旦扫描到一个 var 就检查一下是否已经定义过 如果没有定义过就在链表中新增定义
static Object* findVar(Token* tok) {
    for (Object* obj = Local; obj != NULL; obj = obj->next) {
        if ((strlen(obj->var_name) == tok->length) &&
            !strncmp(obj->var_name, tok->place, tok->length)) {
                return obj;
            }
    }
    return NULL;
}

// 对没有定义过的变量名 通过头插法新增到 Local 链表中
static Object* newLocal(char* varName, Type* type) {
    Object* obj = calloc(1, sizeof(Object));
    obj->var_type = type;
    obj->var_name = varName;
    obj->next = Local;      // 每个变量结点其实都是指向了 Local 链表
    Local = obj;
    return obj;
}

// commit[22]: 针对变量声明语句 提取变量名
static char* getVarName(Token* tok) {
    if (tok->token_kind != TOKEN_IDENT)
        tokenErrorAt(tok, "expected a variable name");

    // string.h: 从给定的字符串 token 中复制最多 length 个字符 并返回指针
    return strndup(tok->place, tok->length);
}

// 定义产生式关系并完成自顶向下的递归调用
static Node* program(Token** rest, Token* tok);
static Function* functionDefinition(Token** rest, Token* tok);

static Type* declarator(Token** rest, Token* tok, Type* Base);
static Type* declspec(Token** rest, Token* tok);
static Node* declaration(Token** rest, Token* tok);

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
static Node* funcall(Token** rest, Token* tok);

// 定义 AST 结点  (注意因为 ND_NUM 的定义不同要分开处理)

// 创造新的结点并分配空间
static Node* createNode(NODE_KIND node_kind, Token* tok) {
    Node* newNode = calloc(1, sizeof(Node));
    newNode->node_kind = node_kind;
    newNode->token = tok;
    return newNode;
}

// 针对数字结点的额外的值定义(数字节点相对特殊 但是在 AST 扮演的角色没区别)
static Node* numNode(int val, Token* tok) {
    Node* newNode = createNode(ND_NUM, tok);
    newNode->val = val;
    return newNode;
}

// 针对变量结点的定义
static Node* singleVarNode(Object* var, Token* tok) {
    Node* varNode = createNode(ND_VAR, tok);
    varNode->var = var;
    return varNode;
}

// 定义 AST 的树型结构  (本质上仍然是一个结点但是有左右子树的定义)
static Node* createAST(NODE_KIND node_kind, Node* LHS, Node* RHS, Token* tok) {
    Node* rootNode = createNode(node_kind, tok);
    rootNode->LHS = LHS;
    rootNode->RHS = RHS;
    return rootNode;
}

// 在引入一元运算符后 涉及到单叉树的构造(本质上是看成负号 和一般意义上的 ++|-- 不同)

// 作用于一元运算符的单边树
static Node* createSingle(NODE_KIND node_kind, Node* single_side, Token* tok) {
    Node* rootNode = createNode(node_kind, tok);
    rootNode->LHS = single_side;            // 定义在左子树中
    return rootNode;
}


/* 提供对指针的加减运算 */

// commit[21]: 新增对指针的加法运算支持
static Node* newPtrAdd(Node* LHS, Node* RHS, Token* tok) {
    addType(LHS);
    addType(RHS);

    // 根据 LHS 和 RHS 的类型进行不同的计算
    // Tip: ptr + ptr 是非法运算
    if ((!isInteger(LHS->node_type)) && (!isInteger(RHS->node_type))) {
        tokenErrorAt(tok, "can't add two pointers");
    }

    // 如果都是整数 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_ADD, LHS, RHS, tok);
    }

    // 如果有一个整数一个指针 对 LHS 和 RHS 两种情况分别讨论
    // LHS: int  +  RHS: ptr
    if (isInteger(LHS->node_type) && (!isInteger(RHS->node_type))) {
        Node* newLHS = createAST(ND_MUL, numNode(8, tok), RHS, tok);
        return createAST(ND_ADD, newLHS, RHS, tok);
    }

    // RHS: int  +  LHS: ptr
    if (!isInteger(LHS->node_type) && (isInteger(RHS->node_type))) {
        Node* newRHS = createAST(ND_MUL, numNode(8, tok), RHS, tok);
        return createAST(ND_ADD, LHS, newRHS, tok);
    }

    tokenErrorAt(tok, "Unable to parse add operation\n");
    return NULL;
}

// commit[21]: 新增对指针的减法运算支持
// Q: 目前还不知道在 (ptr - int) 中额外进行的类型赋予的意义  unsolved 现在和视频版本保持一致
static Node* newPtrSub(Node* LHS, Node* RHS, Token* tok) {
    addType(LHS);
    addType(RHS);

    // Tip: ptr - ptr 是合法运算!
    if (LHS->node_type->Base && RHS->node_type->Base) {
        // 先得到指针相减的结果
        Node* ND = createAST(ND_SUB, LHS, RHS, tok);

        // 通过在 zacc.h 中的 extern 访问
        // Q: 相比于直接 ND->node_type->Kind = TY_INT; 的优势是什么??
        // A: 目前还不知道 也许后面类型多起来会体现吧
        ND->node_type = TYINT_GLOBAL;

        // 挂载到 LHS: LHS / RHS(8)
        return createAST(ND_DIV, ND, numNode(8, tok), tok);
    }

    // num - num 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_SUB, LHS, RHS, tok);
    }

    // LHS: ptr  -  RHS: int
    // 对比加法 相反的操作顺序是非法的  (int - ptr) 没有意义
    if (LHS->node_type->Base && isInteger(RHS->node_type)) {
        Node* newRHS = createAST(ND_MUL, numNode(8, tok), RHS, tok);

        // Q: ???   为什么加法不需要减法需要
        addType(newRHS);

        // Q: ???
        Node* ND = createAST(ND_SUB, LHS, newRHS, tok);

        // Q: 最后要声明该结点是指针类型 ???
        // Q: 对比加法没有进行额外的声明        那加法最后的结点类型是什么呢
        // 即使省去这一步操作   在 CompoundStamt().addType(Curr) 应该也可以设置为正确类型的
        ND->node_type = LHS->node_type;
        return ND;
    }

    tokenErrorAt(tok, "Unable to parse sub operation\n");
    return NULL;
}

// 函数定义: int* funcName() {...}
// 函数调用: int a = funcName();

// 返回对类型 'int' 的判断
static Type* declspec(Token** rest, Token* tok) {
    *rest = skip(tok, "int");
    return TYINT_GLOBAL;
}

// commit[25]: 目前只支持零参函数定义 或者 变量定义     但是没有办法对函数嵌套进行语法报错
// 如果是函数定义的话 一定会提前进入 functionDefinition() 执行
// 并且 C 不支持函数嵌套定义 所以 declaration() 只处理 funcall()
static Type* typeSuffix(Token** rest, Token* tok, Type* returnType) {
    // Q: 为什么没有在这里声明函数返回值属于哪个函数
    // A: 在 declarator() 中的 {Base->Name = tok;} 声明
    if (equal(tok, "(")) {
        // 前看一个字符 判断是变量还是函数调用
        *rest = skip(tok->next, ")");
        return funcType(returnType);
    }

    // 变量声明
    *rest = tok;
    return returnType;
}

// 判断是否是指针类型 如果是指针类型那么要声明该指针的指向
static Type* declarator(Token** rest, Token* tok, Type* Base) {
    while (consume(&tok, tok, "*")) {
        Base = newPointerTo(Base);
    }
    // 此时的 Base 已经是最终的返回值类型 因为全部的 (int**...) 已经解析完成了

    // 接下来要读取 funcName | identName 实际上根据 zacc.h 会写入 Type Base 中
    if (tok->token_kind != TOKEN_IDENT) {
        tokenErrorAt(tok, "expected a variable name or a function name");
    }

    // commit[25]: 这里同时有变量声明 | 函数定义两个可能性 所以后续交给 typeSuffix 判断
    //*rest = tok->next;
    Base = typeSuffix(rest, tok->next, Base);

    // 这个时候的 Base 已经表示函数结点了
    Base->Name = tok;       // Tip: 这里保留了 Token 的链接

    return Base;
}


/* 语法规则的递归解析 */

// Q: 能不能修改为返回 Node* 类型
// commit[25]: 解析零参函数定义     int* funcName() {...}
static Function* functionDefinition(Token** rest, Token* tok) {
    Type* returnBaseType = declspec(&tok, tok);
    Type* isPtr = declarator(&tok, tok, returnBaseType);

    // 初始化函数帧变量
    Local = NULL;

    // 初始化函数定义结点
    Function* func = calloc(1, sizeof(Function));
    
    // Q: 这里一定要用 isPtr->Name 吗   因为这个时候 tok 的位置应该正好指向 funcName
    // 因为在 typeSuffix 中更新了 *rest 所以这个时候 tok 位置在函数体
    func->FuncName = getVarName(isPtr->Name);
    func->AST = program(&tok, tok);
    func->local = Local;

    return func;
}


static Node* program(Token** rest, Token* tok) {
    // 新增语法规则: 要求最外层语句必须有 "{}" 包裹
    tok = skip(tok, "{");
    Node* ND = compoundStamt(&tok, tok);

    // commit[25]: 无效
    // 注意这里是在语法分析结束后再检查词法的结束正确性
    // if (tok->token_kind != TOKEN_EOF) {
    //     tokenErrorAt(tok, "extra Token");
    // }
    *rest = tok;
    return ND;
}

// 对 Block 中复合语句的判断
static Node* compoundStamt(Token** rest, Token* tok) {
    Node* ND = createNode(ND_BLOCK, tok);

    Node HEAD = {};
    Node* Curr = &HEAD;

    while (!equal(tok, "}")) {
        // commit[22]: 对声明语句和表达式语句分别处理
        if (equal(tok, "int")) 
            Curr->next = declaration(&tok, tok);
        else
            Curr->next = stamt(&tok, tok);
        Curr = Curr->next;

        addType(Curr);
    }
    // 得到 CompoundStamt 内部的语句链表

    ND->Body = HEAD.next;

    *rest = tok->next;
    return ND;
}

// 对声明语句的解析
// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node* declaration(Token** rest, Token* tok) {
    // 1. 解析 declspec 类型语法  放在循环外面应用于所有的声明变量
    Type* nd_base_type = declspec(&tok, tok);

    Node HEAD = {};
    Node* Curr = &HEAD;

    // Tip: 计数
    // Q: 这个计数的意义是什么  只是为了合法性判断吗
    int variable_count = 0;

    // 2. 如果存在连续声明  需要通过 for-loop 完成
    while(!equal(tok, ";")) {
        // 判断循环是否能合法的继续下去  同时排除掉第一个变量声明子语句
        if ((variable_count ++) > 0)
            tok = skip(tok, ",");

        Type* isPtr = declarator(&tok, tok, nd_base_type);
        Object* var = newLocal(getVarName(isPtr->Name), isPtr);     // 通过 Name 链接的 Token 保留上一个解析位置 进行传递

        // 3. 如果存在赋值那么解析赋值部分
        if (!equal(tok, "=")) {
            continue;
        }

        // 通过递归下降的方式构造
        Node* LHS = singleVarNode(var, isPtr->Name);    // 利用到 Name 到 Token 的链接
        Node* RHS = assign(&tok, tok->next);

        Node* ND_ROOT = createAST(ND_ASSIGN, LHS, RHS, tok);

        Curr->next = createSingle(ND_STAMT, ND_ROOT, tok);

        // 更新链表指向下一个声明语句
        Curr = Curr->next;
    }

    // 4. 构造 Block 返回结果
    Node* multi_declara = createNode(ND_BLOCK, tok);
    multi_declara->Body = HEAD.next;
    *rest = tok->next;
    return multi_declara;
}

// 对表达式语句的解析
static Node* stamt(Token** rest, Token* tok) {
    if (equal(tok, "return")) {
        // Node* retNode = createSingle(ND_RETURN, expr(&tok, tok->next));
        Node* retNode = createNode(ND_RETURN, tok);
        retNode->LHS = expr(&tok, tok->next);

        *rest = skip(tok, ";");
        return retNode;
    }

    if (equal(tok, "{")) {
        Node* ND = compoundStamt(&tok, tok->next);
        *rest = tok;

        return ND;
    }

    if (equal(tok, "if")) {
        // {if (a) {a = 1;} else {a = 2;}}
        Node* ND = createNode(ND_IF, tok);

        tok = skip(tok->next, "(");
        ND->Cond_Block = expr(&tok, tok);
        *rest = tok;

        ND->If_BLOCK = stamt(&tok, tok->next);
        *rest = tok;

        if (equal(tok, "else")) {
            ND->Else_BLOCK = stamt(&tok, tok->next);
            *rest = tok;
        }

        return ND;
    }
    
    if (equal(tok, "for")) {
        // for (i = 0; i < 10; i = i + 1)
        Node* ND = createNode(ND_FOR, tok);

        tok = skip(tok->next, "(");
        ND->For_Init = exprStamt(&tok, tok);
        *rest = tok;

        if (!equal(tok, ";"))
            ND->Cond_Block = expr(&tok, tok);

        // 无论是否为空 都需要处理分号
        tok = skip(tok, ";");
        *rest = tok;

        // 对 operation 处理同理        注意没有第三个分号 直接就是右括号了
        if (!equal(tok, ")")) {
            ND->Inc = expr(&tok, tok);
        }
        tok = skip(tok, ")");
        *rest = tok;

        // 循环体
        ND->If_BLOCK = stamt(&tok, tok);
        *rest = tok;
        return ND;
    }

    if (equal(tok, "while")) {
        // while (i < 10) {...}
        Node* ND = createNode(ND_FOR, tok);              // 复用标签

        // while 循环某种程度上可以看成简化版的 for-loop
        tok = skip(tok->next, "(");
        ND->Cond_Block = expr(&tok, tok);
        tok = skip(tok, ")");
        *rest = tok;

        ND->If_BLOCK = stamt(&tok, tok);
        *rest = tok;
        return ND;
    }
    return exprStamt(rest, tok);
}

// commit[14]: 新增对空语句的支持   同时支持两种空语句形式
static Node* exprStamt(Token** rest, Token* tok) {
    // 如果第一个字符是 ";" 那么认为是空语句
    if (equal(tok, ";")) {
        *rest = tok->next;
        return createNode(ND_BLOCK, tok);
    }

    // 分析有效表达式并根据分号构建单叉树
    Node* ND = createNode(ND_STAMT, tok);
    ND->LHS = expr(&tok, tok);

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
        ND = createAST(ND_ASSIGN, ND, assign(&tok, tok->next), tok);
    }
    *rest = tok;
    return ND;
}

// 相等判断
static Node* equality_expr(Token** rest, Token* tok) {
    Node* ND = relation_expr(&tok, tok);
    while(true) {
        // 因为不确定后面的表达式执行是否正确 所以提前保存目前为止正确执行的位置
        Token* start = tok;

        // 比较符号后面不可能还是比较符号
        if (equal(tok, "!=")) {
            ND = createAST(ND_NEQ, ND, relation_expr(&tok, tok->next), start);
            continue;
        }
        if (equal(tok, "==")) {
            ND = createAST(ND_EQ, ND, relation_expr(&tok, tok->next), start);
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
        Token* start = tok;
        if (equal(tok, ">=")) {
            ND = createAST(ND_GE, ND, first_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<=")) {
            ND = createAST(ND_LE, ND, first_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "<")) {
            ND = createAST(ND_LT, ND, first_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, ">")) {
            ND = createAST(ND_GT, ND, first_class_expr(&tok, tok->next), start);
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
        Token* start = tok;

        // 如果仍然有表达式的话
        if (equal(tok, "+")) {
            // 构建 ADD 根节点 (注意使用的是 tok->next) 并且
            ND = newPtrAdd(ND, second_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            // 同理建立 SUB 根节点
            ND = newPtrSub(ND, second_class_expr(&tok, tok->next), start);
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
        Token* start = tok;

        if (equal(tok, "*")) {
            ND = createAST(ND_MUL, ND, third_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "/")) {
            ND = createAST(ND_DIV, ND, third_class_expr(&tok, tok->next), start);
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
        Node* ND = createSingle(ND_NEG, third_class_expr(rest, tok->next), tok);
        return ND;
    }

    if (equal(tok, "&")) {
        Node* ND = createSingle(ND_ADDR, third_class_expr(rest, tok->next), tok);
        return ND;
    }

    if (equal(tok, "*")) {
        Node* ND = createSingle(ND_DEREF, third_class_expr(rest, tok->next), tok);
        return ND;
    }

    // 直到找到一个非运算符
    return primary_class_expr(rest, tok);
}

// commit[24]: 处理含参函数调用
static Node* funcall(Token** rest, Token* tok) {
    // funcall = ident "(" (expr ("," expr)*)? ")"

    // 1. 处理 ident
    Node* ND = createNode(ND_FUNCALL, tok);
    ND->FuncName = getVarName(tok);

    // 将至多 6 个参数表达式通过链表存储
    Node HEAD = {};
    Node* Curr = &HEAD;

    // Tip: 上面的 getVarName() 并不会更改 tok 的位置 需要跳过 'ident('
    tok = tok->next->next;

    // 2. 通过循环读取全部链表表达式
    while (!equal(tok, ")"))
    {
        // 针对多个参数的情况 跳过分割符 其中第一个参数没有 "," 分割
        if (Curr != &HEAD)
            tok = skip(tok, ",");
        Curr->next = expr(&tok, tok);
        Curr = Curr->next;
    }

    *rest = skip(tok, ")");

    ND->Func_Args = HEAD.next;
    return ND;
}

// 判断子表达式或者数字
// commit[23]: 支持对零参函数的声明
static Node* primary_class_expr(Token** rest, Token* tok) {
    if (equal(tok, "(")) {
        // 递归调用顶层表达式处理
        Node* ND = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return ND;
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value, tok);
        *rest = tok->next;
        return ND;
    }

    // indet args?  args = "()"
    if ((tok->token_kind) == TOKEN_IDENT) {
        // 提前一个 token 判断是否是函数声明
        if (equal(tok->next, "(")) {
            // 本质上是在内部执行 不需要更新 tok 位置
            return funcall(rest, tok);
        }

        // 先检查变量是否已经定义过 根据结果执行
        Object* varObj = findVar(tok);
        if (!varObj) {
            tokenErrorAt(tok, "undefined variable");
        }
        // 初始化变量结点
        Node* ND = singleVarNode(varObj, tok);       // Tip: 变量结点指向的是链表 Local 对应结点的位置
        *rest = tok->next;
        return ND;
    }

    // 错误处理
    tokenErrorAt(tok, "expected an expr");
    return NULL;
}

// 读入 token stream 在每个 stamt 中处理一部分 token 结点
Function* parse(Token* tok) {
    // 生成的 AST 只有一个根节点 而不是像前面的 commit 一样的单链表

    // commit[25]: 顶层结构从 program 变成 functionDefinition*
    //Node* AST = program(&tok, tok);

    Function HEAD = {};
    Function* Curr = &HEAD;

    while (tok->token_kind !=TOKEN_EOF) {
        Curr->next = functionDefinition(&tok, tok);
        Curr = Curr->next;
    }
    
    // commit[25]: 在 functionDefinition() 中进行空间分配
    // Function* Func = calloc(1, sizeof(Function));

    // 对 Func->StackSize 的处理在 codeGen() 实现
    return HEAD.next;
}