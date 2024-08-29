#include "zacc.h"

// commit[1] - commit[22] 注释备份在 annotation-bak 中

// commit[31]: 把函数作为 Global Object 进行处理
// parse() = (functionDefinition | globalVariable)*

// declspec = "int" | "char"
// declarator = "*"* ident typeSuffix

// commit[22]: 声明语句的语法定义 支持连续定义
// commit[25]: 函数定义 目前只支持 'int' 并且无参  eg. int* funcName() {...}
// functionDefinition = declspec declarator "{" compoundStamt*

// compoundStamt = (declaration | stamt)* "}"

// commit[26]: 含参的函数定义
// commit[27]: 新增对数组变量定义的支持(修改了 typeSuffix 的语法 把结束位置移动到下一层)
// commit[28]: 多维数组 通过递归解析 "[]" 实现  所以需要把语法判断提前
// typeSuffix = ("(" funcFormalParams | "[" num "]" typeSuffix | ε
// funcFormalParams = (formalParam ("," formalParam)*)? ")"
// formalParam = declspec declarator

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
// third_class_expr = (+|-|&|*)third_class_expr | postFix
// postFix = primary_class_expr ("[" expr "]")*

// commit[23]: 添加对零参函数名声明的支持
// commit[30]: 新增对 "sizeof" 的支持
// commit[39]: 新增对 ND_GNU_EXPR 的支持
// primary_class_expr = "(" "{" stamt+ "}" ")" |'(' expr ')' | num | ident fun_args? | "sizeof" third_class_expr | str

// commit[24]: 函数调用 在 primary_class_expr 前看一个字符确认是函数声明后的定义
// funcall = ident "(" (expr ("," expr)*)? ")"

Object* Local;      // 函数内部变量
Object* Global;     // 全局变量 + 函数定义(因为 C 不能发生函数嵌套 所以一定是全局的层面)

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

// commit[31]: 针对局部变量和全局变量的共同行为 函数复用
static Object* newVariable(char* varName, Type* varType) {
    Object* obj = calloc(1, sizeof(Object));
    obj->var_type = varType;
    obj->var_name = varName;
    return obj;
}

// 未定义的局部变量通过头插法新增到 Local 链表中
static Object* newLocal(char* varName, Type* localVarType) {
    Object* obj = newVariable(varName, localVarType);

    obj->next = Local;
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
    if (tok->token_kind != TOKEN_NUM)
        tokenErrorAt(tok, "need a number for array declaration");
    return tok->value;
}

// commit[33]: 判断当前读取的类型是否符合变量声明的类型
static bool isTypeName(Token* tok) {
    return (equal(tok, "int") || equal(tok, "char"));
}


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

static Node* equality_expr(Token** rest, Token* tok);
static Node* relation_expr(Token** rest, Token* tok);
static Node* first_class_expr(Token** rest, Token* tok);
static Node* second_class_expr(Token** rest, Token* tok);
static Node* third_class_expr(Token** rest, Token* tok);
static Node* preFix(Token** rest, Token* tok);
static Node* primary_class_expr(Token** rest, Token* tok);
static Node* funcall(Token** rest, Token* tok);

// 定义 AST 结点

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

// 定义 AST 的树型结构
static Node* createAST(NODE_KIND node_kind, Node* LHS, Node* RHS, Token* tok) {
    Node* rootNode = createNode(node_kind, tok);
    rootNode->LHS = LHS;
    rootNode->RHS = RHS;
    return rootNode;
}

// 作用于一元运算符的单边树  选择在左子树中
static Node* createSingle(NODE_KIND node_kind, Node* single_side, Token* tok) {
    Node* rootNode = createNode(node_kind, tok);
    rootNode->LHS = single_side;            
    return rootNode;
}

/* 提供对指针的加减运算 */

// commit[21]: 新增对指针的加法运算支持
static Node* newPtrAdd(Node* LHS, Node* RHS, Token* tok) {
    addType(LHS);
    addType(RHS);

    // 根据 LHS 和 RHS 的类型进行不同的计算
    
    // ptr + ptr 非法运算
    if ((!isInteger(LHS->node_type)) && (!isInteger(RHS->node_type))) {
        tokenErrorAt(tok, "can't add two pointers");
    }

    // int + int 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_ADD, LHS, RHS, tok);
    }

    // 指针必须在 LHS 分支传递  结合 addType() 来理解  所有的赋值都通过 LHS 来进行 (左值右值)
    // 否则叶子结点的类型无法向上传递导致出错
    
    // LHS: int  +  RHS: ptr
    if (isInteger(LHS->node_type) && (!isInteger(RHS->node_type))) {
        Node* newLHS = createAST(ND_MUL, numNode(RHS->node_type->Base->BaseSize, tok), LHS, tok);
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

    // ptr - ptr 合法运算  得到数组元素个数
    if (LHS->node_type->Base && RHS->node_type->Base) {
        Node* ND = createAST(ND_SUB, LHS, RHS, tok);
        ND->node_type = TYINT_GLOBAL;

        // 挂载到 LHS: LHS / RHS(8)     除法需要区分左右子树
        return createAST(ND_DIV, ND, numNode(LHS->node_type->Base->BaseSize, tok), tok);
    }

    // num - num 正常计算
    if (isInteger(LHS->node_type) && isInteger(RHS->node_type)) {
        return createAST(ND_SUB, LHS, RHS, tok);
    }

    // LHS: ptr  -  RHS: int   
    if (LHS->node_type->Base && isInteger(RHS->node_type)) {
        Node* newRHS = createAST(ND_MUL, numNode(LHS->node_type->Base->BaseSize, tok), RHS, tok);

        // Q: ???   为什么加法不需要减法需要
        addType(newRHS);

        Node* ND = createAST(ND_SUB, LHS, newRHS, tok);

        // Q: 最后要声明该结点类型 ???  在 compoundStamt() 中不可以吗
        ND->node_type = LHS->node_type;
        return ND;
    }

    // LHS: int  -  RHS: ptr 没有意义
    tokenErrorAt(tok, "Unable to parse sub operation\n");
    return NULL;
}


/* 下面的三个辅助函数可能发生函数定义与变量定义的混用(因为 funcall 不会有 int 前缀) */
// 函数定义: int* funcName() {...}
// 变量定义: int a, *b = xx;
// 因为混用导致函数嵌套定义的报错发生在 declaration() 的变量解析中


// 类型前缀判断
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

// 解析函数传参
static Type* funcFormalParams(Token** rest, Token* tok, Type* returnType) {
    // commit[26]: 利用 Type 结构定义存储形参的链表
    Type HEAD = {};
    Type* Curr = &HEAD;

    while (!equal(tok, ")")) {
        // 多参跳过分隔符
        if (Curr != &HEAD)
            tok = skip(tok, ",");
        // commit[26]: 复用函数定义的规则
        Type* formalBaseType = declspec(&tok, tok);
        Type* isPtr = declarator(&tok, tok, formalBaseType);
        
        Curr->formalParamNext = copyType(isPtr);
        Curr = Curr->formalParamNext;
    }

    *rest = skip(tok, ")");

    // 封装函数结点类型
    Type* funcNode = funcType(returnType);
    funcNode->formalParamLink = HEAD.formalParamNext;

    return funcNode;
}

// commit[27]: 处理定义语法的后缀
static Type* typeSuffix(Token** rest, Token* tok, Type* BaseType) {
    if (equal(tok, "(")) {
        // 函数定义处理    BaseType: 函数返回值类型
        return funcFormalParams(rest, tok->next, BaseType);
    }

    if (equal(tok, "[")) {
        // 数组定义处理    BaseType: 数组基类
        int arraySize = getArrayNumber(tok->next);

        tok = skip(tok->next->next, "]");
        // 通过递归不断重置 BaseType 保存 (n - 1) 维数组的信息  最终返回 n 维数组信息
        BaseType = typeSuffix(rest, tok, BaseType);
        return linerArrayType(BaseType, arraySize);
    }

    // 变量定义处理    BaseType: 变量类型
    *rest = tok;
    return BaseType;
}

// 对类型进行完整判断
static Type* declarator(Token** rest, Token* tok, Type* Base) {
    while (consume(&tok, tok, "*")) {
        Base = newPointerTo(Base);
    }
    // 此时的 Base 已经是最终的返回值类型 或 变量类型 因为全部的 (int**...) 已经解析完成了

    // 接下来要读取 funcName | identName
    if (tok->token_kind != TOKEN_IDENT) {
        tokenErrorAt(tok, "expected a variable name or a function name");
    }

    // commit[25]: 这里同时有变量声明 | 函数定义两个可能性 所以后续交给 typeSuffix 判断
    // 调用 typeSuffix() 传递的是 rest 所以这里 tok 没有更新
    Base = typeSuffix(rest, tok->next, Base);

    // case1: 函数定义         读取函数名
    // case2: 变量或者传参解析  读取传参变量名
    Base->Name = tok;

    return Base;
}

// commit[26]: 对多参函数的传参顺序进行构造 添加到 Local 链表
static void createParamVar(Type* param) {
    // Tip: param 本身可能是 NULL
    if (param) {
        createParamVar(param->formalParamNext);
        newLocal(getVarName(param->Name), param);
    }
}

// commit[32]: 判断当前的语法是函数还是全局变量    区别就是 ";"
static bool GlobalOrFunction(Token* tok) {
    bool Global = true;
    bool Function = false;

    if (equal(tok, ";"))
        return Global;

    // 这里进一步针对其他 TYPE 进行解析 数组或者其他什么  二次判断

    // 初始化隐含了枚举变量的初始化  默认设置为 TY_INT  所以 BaseType 不是完全空的
    Type Dummy = {};
    Type* ty = declarator(&tok, tok, &Dummy);
    if (ty->Kind == TY_FUNC)
        return Function;
    
    return Global;
}

static char* newUniqueName(void) {
    static int I = 0;
    return format(".L..%d", I++);
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

// Q: commit[31] 的修改为什么传递 Token 回去
// A: 因为函数已经作为一种特殊的变量被写入 Global 了
static Token* functionDefinition(Token* tok, Type* funcReturnBaseType) {
    Type* funcType = declarator(&tok, tok, funcReturnBaseType);

    // commit[31]: 构造函数结点
    Object* function = newGlobal(getVarName(funcType->Name), funcType);
    function->IsFunction = true;

    // 初始化函数帧内部变量
    Local = NULL;
    
    // 第一次更新 Local: 函数形参
    createParamVar(funcType->formalParamLink);
    function->formalParam = Local;

    tok = skip(tok, "{");
    // 第二次更新 Local: 更新函数内部定义变量
    function->AST = compoundStamt(&tok, tok);
    function->local = Local;

    return tok;
}

// commit[32]: 正式在 AST 中加入全局变量的处理
static Token* gloablDefinition(Token* tok, Type* globalBaseType) {
    // 如果连续的变量定义 第一个变量不会从 ", var" 开始判断
    bool isLast = true;

    while (!consume(&tok, tok, ";")) {
        // 判断是否为连续的全局变量的定义  同时正确解析第一个变量
        if (!isLast)
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
        // 针对定义语句和表达式语句分别处理
        if (isTypeName(tok))
            // Tip: 如果只是变量声明没有赋值 不会新建任何结点 只是更新了 Local
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

// 对变量定义语句的解析
// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node* declaration(Token** rest, Token* tok) {
    // 1. 解析 declspec 类型语法  放在循环外面应用于所有的声明变量
    Type* nd_base_type = declspec(&tok, tok);

    Node HEAD = {};
    Node* Curr = &HEAD;

    // 连续定义的合法性判断
    int variable_count = 0;
    // 2. 如果存在连续声明  需要通过 for-loop 完成
    while(!equal(tok, ";")) {
        if ((variable_count ++) > 0)
            // Tip: 函数嵌套定义报错位置
            tok = skip(tok, ",");

        Type* isPtr = declarator(&tok, tok, nd_base_type);
        Object* var = newLocal(getVarName(isPtr->Name), isPtr);

        // 3. 如果存在赋值那么解析赋值部分
        if (!equal(tok, "=")) {
            continue;
        }

        // 通过递归下降的方式构造
        Node* LHS = singleVarNode(var, isPtr->Name);
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

// commit[14]: 新增对空语句的支持   同时支持两种空语句形式 {} | ;
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
    Node* ND = second_class_expr(&tok, tok);

    while(true) {
        Token* start = tok;

        if (equal(tok, "+")) {
            ND = newPtrAdd(ND, second_class_expr(&tok, tok->next), start);
            continue;
        }

        if (equal(tok, "-")) {
            ND = newPtrSub(ND, second_class_expr(&tok, tok->next), start);
            continue;
        }

        *rest = tok;
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
    // commit[39]: 因为 GNU 的语句表达式包含 "(" 和 "{"  要优先判定
    if (equal(tok, "(") && equal(tok->next, "{")) {
        Node* ND = createNode(ND_GNU_EXPR, tok);

        ND->Body = compoundStamt(&tok, tok->next->next)->Body;
        *rest = skip(tok, ")");

        return ND;
    }

    if (equal(tok, "(")) {
        // 递归调用顶层表达式处理
        Node* ND = expr(&tok, tok->next);
        *rest = skip(tok, ")");
        return ND;
    }

    // commit[30]: 支持 sizeof
    if (equal(tok, "sizeof")) {
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
        *rest = tok->next;
        // 利用人为定义的名称 + 字符串内容 构造一个变量结点插入 AST 中
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
        Node* ND = singleVarNode(varObj, tok);
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

// 解析全局变量和函数定义
Object* parse(Token* tok) {
    // commit[31]: 使用全局变量 Global 来记录函数定义
    Global = NULL;

    while (tok->token_kind !=TOKEN_EOF) {
        Type* BaseType = declspec(&tok, tok);

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

    return Global;
}
