#include "zacc.h"

// commit[1] - commit[22] 注释备份在 annotation-bak 中

// commit[25]: 最顶层语法规则更新为函数定义 注释掉 program 重新修改为两层结构
// commit[31]: 把函数作为 Global Object 进行处理
// parse() = (functionDefinition | globalVariable)*
// __program = "{" compoundStamt

// commit[25]: 函数定义 目前只支持 'int' 并且无参  eg. int* funcName() {...}
// functionDefinition = declspec declarator "{" compoundStamt*
// declspec = "int" | "char"
// declarator = "*"* ident typeSuffix

// commit[26]: 含参的函数定义
// commit[27]: 新增对数组变量定义的支持(修改了 typeSuffix 的语法 把结束位置移动到下一层)
// commit[28]: 多维数组 通过递归解析 "[]" 实现  所以需要把语法判断提前
// typeSuffix = ("(" funcFormalParams | "[" num "]" typeSuffix | ε
// funcFormalParams = (formalParam ("," formalParam)*)? ")"
// formalParam = declspec declarator
// __typeSuffix = ("(" ")")?

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

// commit[29]: 新增对 [] 的语法支持  本质上就是对 *(x + y) 的一个语法糖 => x[y]
// __third_class_expr = (+|-|&|*)third_class_expr | primary_class_expr        优先拆分出一个符号作为减法符号
// third_class_expr = (+|-|&|*)third_class_expr | postFix
// postFix = primary_class_expr ("[" expr "]")*

// commit[23]: 添加对零参函数名声明的支持
// Tip: 这里处理参数的语法规则有 ident 重叠 因为不确定是函数声明还是变量 所以要往前看一个字符

// commit[30]: 新增对 "sizeof" 的支持
// Q: 为什么这里的递归解析是 third_expr 而不是 expr 有什么好处吗 或者说必要性
// primary_class_expr = '(' expr ')' | num | ident fun_args? | "sizeof" third_class_expr | str
// __primary_class_expr = '(' expr ')' | num | ident fun_args?

// commit[24]: 函数调用 在 primary_class_expr 前看一个字符确认是函数声明后的定义
// funcall = ident "(" (expr ("," expr)*)? ")"

Object* Local;      // 函数内部变量
Object* Global;     // 全局变量 + 函数定义(因为 C 不能发生函数嵌套 所以一定是全局的层面)

// 当前只有一个匿名 main 函数帧 一旦扫描到一个 var 就检查一下是否已经定义过 如果没有定义过就在链表中新增定义
static Object* findVar(Token* tok) {
    // 查找局部变量
    for (Object* obj = Local; obj != NULL; obj = obj->next) {
        if ((strlen(obj->var_name) == tok->length) &&
            !strncmp(obj->var_name, tok->place, tok->length)) {
                return obj;
            }
    }

    // commit[32]: 真正支持全局变量的查找
    for (Object* obj = Global; obj != NULL; obj = obj->next) {
        if ((strlen(obj->var_name) == tok->length) &&
            !strncmp(obj->var_name, tok->place, tok->length)) {
                return obj;
            } 
    }
    return NULL;
}

// commit[31]: 针对局部变量和全局变量的共同行为，给出一个函数用来复用
static Object* newVariable(char* varName, Type* varType) {
    Object* obj = calloc(1, sizeof(Object));
    obj->var_type = varType;
    obj->var_name = varName;
    return obj;
}

// 对没有定义过的变量名 通过头插法新增到 Local 链表中
static Object* newLocal(char* varName, Type* localVarType) {
    Object* obj = newVariable(varName, localVarType);

    // Object* obj = calloc(1, sizeof(Object));
    // obj->var_type = type;
    // obj->var_name = varName;

    obj->next = Local;      // 每个变量结点其实都是指向了 Local 链表
    Local = obj;

    // commit[31]: 针对局部变量需要特殊声明
    obj->IsLocal = true;
    return obj;
}

// commit[31]: 完全参考 Local 的实现方式
static Object* newGlobal(char* varName, Type* globalVarType) {
    Object* obj = newVariable(varName, globalVarType);
    obj->next = Global;
    Global = obj;
    return obj;
}

// commit[22]: 针对变量声明语句 提取变量名
static char* getVarName(Token* tok) {
    if (tok->token_kind != TOKEN_IDENT)
        tokenErrorAt(tok, "expected a variable name");

    // string.h: 从给定的字符串 token 中复制最多 length 个字符 并返回指针
    return strndup(tok->place, tok->length);
}

// commit[27]: 获取数组定义中括号中的数字
static int getArrayNumber(Token* tok) {
    // Q: 为什么需要这样的获取数字的方式
    // A: 就是数组的结构比较特殊 没什么原因
    if (tok->token_kind != TOKEN_NUM)
        tokenErrorAt(tok, "need a number for array declaration");
    return tok->value;
}

// commit[33]: 判断当前读取的类型是否符合变量声明的类型
static bool isTypeName(Token* tok) {
    return (equal(tok, "int") || equal(tok, "char"));
}

// 定义产生式关系并完成自顶向下的递归调用
// static Node* __program(Token** rest, Token* tok);

// commit[32]: 全局变量和函数定义作为 rank 1 级进行解析
static Token* functionDefinition(Token* tok, Type* funcReturnBaseType);
static Token* gloablDefinition(Token* tok, Type* globalBaseType);

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
static Node* preFix(Token** rest, Token* tok);
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

    // 如果有一个整数一个指针 对 LHS 和 RHS 两种情况分别讨论 核心是对整数结点处理
    // commit[27]: 优化了直接写数字 8 的情况 改为变量 为后续不同类型大小的指针运算准备

    // commit[29]: 在这次的 commit 里面发现了一个隐藏 bug 关于 LHS 和 RHS 左右位置的问题
    // 示例代码通过调换左右子树的顺序让这个问题不是很明显 但是我是分开处理的 所以在处理 (2[x] = 5;) 这个表达式的时候出错
    // 指针部分永远放在左边! 要结合 addType() 来理解   所有的赋值都通过 LHS 来进行
    // 否则叶子结点的类型无法向上传递导致出错
    
    // LHS: int  +  RHS: ptr
    if (isInteger(LHS->node_type) && (!isInteger(RHS->node_type))) {
        Node* newLHS = createAST(ND_MUL, numNode(RHS->node_type->Base->BaseSize, tok), LHS, tok);
        // return createAST(ND_ADD, newLHS，RHS, tok);
        return createAST(ND_ADD, RHS, newLHS, tok);
    }

    // LHS: ptr  +  RHS: int
    if (!isInteger(LHS->node_type) && (isInteger(RHS->node_type))) {
        // commit[28]: 使用 (n - 1) 维数组的大小作为 1 运算
        Node* newRHS = createAST(ND_MUL, RHS, numNode(LHS->node_type->Base->BaseSize, tok), tok);
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
        // 注意除法需要区分左子树和右子树(小心~)
        return createAST(ND_DIV, ND, numNode(LHS->node_type->Base->BaseSize, tok), tok);
    }

    // num - num 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_SUB, LHS, RHS, tok);
    }

    // LHS: ptr  -  RHS: int
    // 对比加法 相反的操作顺序是非法的  (int - ptr) 没有意义
    if (LHS->node_type->Base && isInteger(RHS->node_type)) {
        Node* newRHS = createAST(ND_MUL, numNode(LHS->node_type->Base->BaseSize, tok), RHS, tok);

        // Q: ???   为什么加法不需要减法需要
        addType(newRHS);

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

/* 下面的三个辅助函数只可能发生函数定义与变量定义的混用(因为 funcall 不会有 int 前缀) */
// 函数定义: int* funcName() {...}
// 变量定义: int a, *b = xx;
// 因为混用导致函数嵌套定义的报错发生在 declaration() 的变量解析中

// 返回对类型 'int' 的判断
static Type* declspec(Token** rest, Token* tok) {
    // commit[33]: 对类型分别进行处理
    if (equal(tok, "int")) {
        *rest = skip(tok, "int");
        return TYINT_GLOBAL;
    }
    if (equal(tok, "char")) {
        *rest = skip(tok, "char");
        return TYCHAR_GLOBAL;
    }
    return TYINT_GLOBAL;
}

// commit[27]: 拆分 typeSuffix 到 funcFormalParams 和 typeSuffix 两个语法函数
// Q: 为什么要去进行一个拆分    A: 出于对 TypeSuffix 的结构清晰性

static Type* funcFormalParams(Token** rest, Token* tok, Type* returnType) {
    // commit[27]: 在 typeSuffix() 功能拆分后 该函数只处理函数定义括号内部变量
    // if (equal(tok, "(")) {
    //     tok = tok->next;

    // commit[26]: 利用 Type 结构定义存储形参的链表
    Type HEAD = {};
    Type* Curr = &HEAD;

    while (!equal(tok, ")")) {
        // 多参跳过分隔符
        if (Curr != &HEAD)
            tok = skip(tok, ",");
        // commit[26]: 复用函数定义的规则(因为不可能存在 "=" 所以 != 变量定义)
        Type* formalBaseType = declspec(&tok, tok);
        Type* isPtr = declarator(&tok, tok, formalBaseType);
        
        // Q: 这里的 copyType() 到底干什么了
        // A: 实现 C 的值传递规则 任何变量都会换一个新地址保存
        Curr->formalParamNext = copyType(isPtr);
        Curr = Curr->formalParamNext;
    }

    // Q: 为什么没有在这里声明函数返回值属于哪个函数
    // A: 返回值本身是已经确定的 这里在新建一个函数结点 反向声明该函数拥有什么返回类型
    *rest = skip(tok, ")");

    // 封装函数结点类型
    Type* funcNode = funcType(returnType);
    funcNode->formalParamLink = HEAD.formalParamNext;

    return funcNode;
}

// commit[27]: 原本写在 typeSuffix() 中 解析变量定义的部分 在拆分后不需要了
//     // 变量声明
//     // Q: 这里没有把形参的名字读进去??     A: 在后面的 Base->Name 中更新
//     *rest = tok;
//     return returnType;
// }

// commit[25]: 目前只支持零参函数定义 或者 变量定义
// commit[27]: 拆分两个功能分开处理 更清晰
static Type* typeSuffix(Token** rest, Token* tok, Type* BaseType) {
    if (equal(tok, "(")) {
        // 函数定义处理    BaseType: 函数返回值类型
        return funcFormalParams(rest, tok->next, BaseType);
    }

    if (equal(tok, "[")) {
        // 数组定义处理    BaseType: 数组基类
        int arraySize = getArrayNumber(tok->next);
        // *rest = skip(tok->next->next, "]");

        // commit[28]: 多维数组 递归解析语法
        tok = skip(tok->next->next, "]");
        // 通过递归不断重置 BaseType 保存 (n - 1) 维数组的信息  最终返回 n 维数组信息
        BaseType = typeSuffix(rest, tok, BaseType);
        return linerArrayType(BaseType, arraySize);
    }

    // 变量定义处理    BaseType: 变量类型
    *rest = tok;
    return BaseType;
}

// 判断是否是指针类型 如果是指针类型那么要声明该指针的指向
static Type* declarator(Token** rest, Token* tok, Type* Base) {
    while (consume(&tok, tok, "*")) {
        Base = newPointerTo(Base);
    }
    // 此时的 Base 已经是最终的返回值类型 或 变量类型 因为全部的 (int**...) 已经解析完成了

    // 接下来要读取 funcName | identName 实际上根据 zacc.h 会写入 Type Base 中
    if (tok->token_kind != TOKEN_IDENT) {
        tokenErrorAt(tok, "expected a variable name or a function name");
    }

    // commit[25]: 这里同时有变量声明 | 函数定义两个可能性 所以后续交给 typeSuffix 判断
    // *rest = tok->next;
    // commit[26]: 使用 typeSuffix() 完成对传参的处理
    Base = typeSuffix(rest, tok->next, Base);
    // 如果是函数结点   因为调用 typeSuffix() 传递的是 rest 所以这里 tok 没有更新

    // case1: 函数定义         读取函数名
    // case2: 变量或者传参解析  读取传参变量名
    Base->Name = tok;       // Tip: 这里保留了 Token 的链接

    return Base;
}

// commit[26]: 对多参函数的传参顺序进行构造 添加到 Local 链表
static void createParamVar(Type* param) {
    // Tip: Local 构造是头插法 所以要先递归到最后一个参数插入
    // Q: 如果 param 本身是 NULL 那么会在这里出现 segment fault
    // A: 所以这里不能使用这样的递归    (说起来这类错误我犯了好几次了但是一点意识都没有...
    
    // if (param->formalParamNext)
    //     createParamVar(param->formalParamNext);
    // newLocal(getVarName(param->Name), param);

    if (param) {
        createParamVar(param->formalParamNext);
        newLocal(getVarName(param->Name), param);
    }
}

// commit[32]: 判断当前的语法是函数还是全局变量    区别就是 ";"
static bool GlobalOrFunction(Token* tok) {      // Q: 这里为什么只用 tok 额外传一个 BaseType 也不会怎么样
    bool Global = true;
    bool Function = false;

    if (equal(tok, ";"))
        return Global;

    // Q: 为什么的虚设变量 Dummy 意义是
    // A: 全局变量的声明方式很多 比如数组或者赋值 无法简单的通过 equal(tok, ";") 判定
    // 所以这里进一步针对其他形式的全局声明进行解析 有可能不是函数  但总之后续会二次判断
    Type Dummy = {};        // 初始化隐含了枚举变量的初始化  默认设置为 TY_INT  所以 BaseType 不是完全空的  只是现在碰巧是 INT 不可能解析 CHAR 也许后面会改？
    Type* ty = declarator(&tok, tok, &Dummy);
    if (ty->Kind == TY_FUNC)
        return Function;
    
    // __tokenErrorAt(tok, "Not a Global Variable nor a Function define\n");
    return Global;
}

static char* newUniqueName(void) {
    static int I = 0;
    char* buffer = calloc(1, 20);
    sprintf(buffer, ".L..%d", I++);
    return buffer;
}

static Object* newAnonyGlobalVar(Type* globalType) {
    return newGlobal(newUniqueName(), globalType);
}

// commit[34]: 以全局变量的方式处理字符字面量
static Object* newStringLiteral(char* strContent, Type* strType) {
    Object* strObj = newAnonyGlobalVar(strType);
    strObj->InitData = strContent;
    return strObj;
}

/* 语法规则的递归解析 */

// commit[25]: 解析零参函数定义     int* funcName() {...}
// Q: commit[31] 的修改为什么传递 Token 回去
// A: 因为函数已经作为一种特殊的变量被写入 Global 了
static Token* functionDefinition(Token* tok, Type* funcReturnBaseType) {
    Type* funcType = declarator(&tok, tok, funcReturnBaseType);

    // commit[31]: 构造函数结点
    Object* function = newGlobal(getVarName(funcType->Name), funcType);
    function->IsFunction = true;

    // 初始化函数帧内部变量
    Local = NULL;

    // 初始化函数定义结点
    // __Function* func = calloc(1, sizeof(Function));
    
    // Q: 这里一定要用 funcType->Name 吗   Base->Name = tok;
    // A: 因为在 declarator 中更新了 &tok 所以这个时候 tok 位置指向函数体
    // __func->FuncName = getVarName(funcType->Name);

    // commit[26]: 形参变量和函数内部变量需要两次不同的更新
    
    // 第一次更新 Local: 函数形参
    createParamVar(funcType->formalParamLink);
    function->formalParam = Local;

    tok = skip(tok, "{");
    // 第二次更新 Local: 更新函数内部定义变量
    function->AST = compoundStamt(&tok, tok);

    // 因为更新的 local 是不同区域的 Local 比如参数和函数内变量
    // 虽然它们最后都在同一个链表 Local 里面    但是赋值的变量 (formalParam, local) 是不同的
    function->local = Local;

    // Q: 为什么这里是返回 tok 呢   可能是函数作为一个变量已经存入 Object 中了
    return tok;
}

// commit[32]: 正式在 AST 中加入全局变量的处理
static Token* gloablDefinition(Token* tok, Type* globalBaseType) {
    // Q: 为什么需要 First 这种东西
    // A: 因为全局变量也可能是连续定义的语法结构 初始化默认为只有一个变量定义
    // 同时保证了如果连续的变量定义 第一个变量不会从 ", var" 开始判断
    bool isLast = true;

    while (!consume(&tok, tok, ";")) {
        // 判断为连续的全局变量的定义   通过 skip 进行语法检查
        if (!isLast)
            // 为了正确解析第一个变量
            tok = skip(tok, ",");
        isLast = false;

        // 解析全局变量的类型并写入 Global 链表
        Type* globalType = declarator(&tok, tok, globalBaseType);
        newGlobal(getVarName(globalType->Name), globalType);
    }
    return tok;
}

// 对 Block 中复合语句的判断
static Node* compoundStamt(Token** rest, Token* tok) {
    Node* ND = createNode(ND_BLOCK, tok);

    Node HEAD = {};
    Node* Curr = &HEAD;

    while (!equal(tok, "}")) {
        // commit[22]: 对声明语句和表达式语句分别处理
        // commit[25]: 函数调用是没有返回值的前缀! 所以目前 compoundStamt.declaration 只用于变量定义
        // commit[33]: 在加入新的类型判断之后 Q: ???
        if (isTypeName(tok))
            // Tip: 如果只是变量声明没有赋值 不会新建任何结点 只是更新了 Local
            Curr->next = declaration(&tok, tok);
        else
            Curr->next = stamt(&tok, tok);
        Curr = Curr->next;
        // 在 commit[25] 和 commot[26] 更新之后 Curr 实际上只指向函数体内部的语句
        addType(Curr);
    }
    // 得到 CompoundStamt 内部的语句链表

    ND->Body = HEAD.next;

    *rest = tok->next;
    return ND;
}

// 对变量定义语句的解析
// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node* declaration(Token** rest, Token* tok) {
    // 1. 解析 declspec 类型语法  放在循环外面应用于所有的声明变量
    Type* nd_base_type = declspec(&tok, tok);

    Node HEAD = {};
    Node* Curr = &HEAD;

    // Tip: 计数 至少有一个变量被定义
    // Q: 这个计数的意义是什么  只是为了合法性判断吗
    int variable_count = 0;

    // 2. 如果存在连续声明  需要通过 for-loop 完成
    while(!equal(tok, ";")) {
        // 判断循环是否能合法的继续下去  同时排除掉第一个变量声明子语句
        if ((variable_count ++) > 0)
            // Tip: 函数嵌套定义报错位置
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
    // return primary_class_expr(rest, tok);
    return preFix(rest, tok);
}

// commit[28]: preFix = primary_class_expr ("[" expr "]")*  eg. x[y] 先解析 x 再是 y
static Node* preFix(Token** rest, Token* tok) {
    Node* ND = primary_class_expr(&tok, tok);

    while (equal(tok, "[")) {
        // 需要考虑到多维数组的情况   x[y] | y[x] => *(x + y)
        // Tip: 针对 x[y] 和 y[x] 两种情况  对应 primary 中的 NUM 和 IDENT 判断
        // 但是从执行效率上讲 更推荐 x[y] 的方式
        Token* idxStart = tok;
        Node* idxExpr = expr(&tok, tok->next);
        tok = skip(tok, "]");
        ND = createSingle(ND_DEREF, newPtrAdd(ND, idxExpr, idxStart), idxStart);
    }
    *rest = tok;
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

    // commit[30]: 支持 sizeof
    if (equal(tok, "sizeof")) {
        // Q: 为什么这里调用的是 third_class_expr 而不能用最顶层的 expr()
        // A: 是 sizeof() 语法结构的问题 这个关键字本身就只能处理单边表达式  如果使用双边 expr 会导致 AST 结构问题
        // 比如 {sizeof(**x) + 1} ND_ADD 结点本身在顶层 如果使用 expr 加法结点会被转换为 ND_NUM 进入叶子层
        Node* ND = third_class_expr(rest, tok->next);
        
        // Q: 为什么这里需要调用 addType() 如果注释掉会完全错误
        // A: 如果不去赋予类型的话 在后面的 numNode 使用 node_type 时会发生访问空指针的问题
        addType(ND);
        return numNode(ND->node_type->BaseSize, tok);
    }

    if ((tok->token_kind) == TOKEN_NUM) {
        Node* ND = numNode(tok->value, tok);
        *rest = tok->next;
        return ND;
    }

    if ((tok->token_kind) == TOKEN_STR) {
        Object* strObj = newStringLiteral(tok->strContent, tok->tokenType);
        // Q: 这里为什么是 next
        *rest = tok->next;
        return singleVarNode(strObj, tok);
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

// 读入 token stream 在每个 stamt 中处理一部分 token 结点
Object* parse(Token* tok) {
    // 生成的 AST 只有一个根节点 而不是像前面的 commit 一样的单链表

    // commit[25]: 顶层结构从 program 变成 functionDefinition*
    //Node* AST = program(&tok, tok);

    // commit[25]: 解析层次修改之后 从 parse->program 的两层结构变成 parse->funcDefin->program 三层结构
    // 三层结构会导致 *rest tok 错误    除非换成两层

    // __Function HEAD = {};
    // __Function* Curr = &HEAD;

    // commit[31]: 使用全局变量 Global 来记录函数定义
    Global = NULL;

    while (tok->token_kind !=TOKEN_EOF) {
        Type* BaseType = declspec(&tok, tok);
        // Q: 即使是判断函数还是变量 为什么不在这里把 BaseType 传进去呢
        // commit[32]: 判断全局变量或者函数 进行不同的处理
        if (!GlobalOrFunction(tok)) {
            Type* funcReturnBaseType = copyType(BaseType);
            tok = functionDefinition(tok, funcReturnBaseType);
            continue;
        }

        else {
            Type* globalBaseType = copyType(BaseType);
            tok = gloablDefinition(tok, globalBaseType);
        }
    }
    
    // commit[25]: 在 functionDefinition() 中进行空间分配
    // Function* Func = calloc(1, sizeof(Function));

    // 对 Func->StackSize 的处理在 codeGen() 实现
    // __return HEAD.next;

    return Global;
}