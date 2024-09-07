#include "zacc.h"

// commit[1] - commit[22] 注释备份在 annotation-bak 中
// commit[42]: checkout 查看注释

// commit[52]: 支持结构体标签的解析
// 本质上和 TY_INT 没什么区分  只是 TY_INT 可以用字面量去提前定义  而结构体不能
    // struct t {...} x;
    //        t       x;
    //       int      x;
// 结构体声明和变量声明是同一个语法层次

// commit[54]: 支持联合体的类型模板存储  因为语法和 struct 一样  所以可以复用
typedef struct TagScope TagScope;
struct TagScope {
    // 将函数可能定义的多个结构体类型模板作为链表存储
    TagScope* next;

    char* tagName;
    // 利用 Type 记录该结构体的一些元数据  比如成员 对齐值 大小...
    Type* tagType;
};

/* commit[44]: 类似于 Antlr4 对变量域通过 [[], [], ..] 的管理  这里使用 Scope 链表管理 */
// 每个 Scope 中的变量同样构成了一个 VarInScope 链表存储

// commit[44]: 一个函数中可能有多个代码块  这些代码块中的内容也不能相互影响
// 具体的代码块的层次都是由 Scope 来决定的  VarInScope 只是负责插入正确的 Scope 层次位置
typedef struct VarInScope VarInScope;
struct VarInScope {
    VarInScope* next;

    // Q: 在 findVar() 查找变量通过这里判断  为什么不是 Object 中的 name  目前这个 scopeName 完全就是 Object.name
    // A: 目前猜测有一些匿名变量的考虑  需要结合后面的实现
    char* scopeName;

    // 标记变量作用范围的同时指向该变量
    Object* varList;
};

// 每个 {} 构成一个生效 Scope 范围
typedef struct Scope Scope;
struct Scope {
    Scope* next;

    VarInScope* varScope;

    // 没有标签的结构体实际上是一个匿名结构体  虽然可以定义多个结构体变量 但是无法被复用
    // 结构体实例作为 var 存入 varScope 而 tarScope 是存了一个模板
    TagScope* tagScope;
};


// commit[31]: 把函数作为 Global Object 进行处理
// parse() = (functionDefinition | globalVariable)*

// commit[49]: 支持 struct 语法解析
// commit[54]: 支持 union 语法解析
// declspec = "int" | "char" | structDeclaration | unionDeclaration
// __declspec = "int" | "char" | structDeclaration
// __declspec = "int" | "char"

// __commit[49]: struct StructName { variableDeclaration }
// commit[54]: 针对 struct 和 union 提取出抽象层
// StructOrUnionDecl = ident ? (" {" structMembers)?

// structDeclaration = StructOrUnionDecl
// unionDeclaration = StructOrUnionDecl

// structMembers = (declspec declarator ("," declarator)* ";")*

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

// commit[49]: 支持对结构体成员的访问
// commit[53]: 支持对结构体实例成员的访问  写入左子树结构  注意结构体可能是递归的 x->y->z
// postFix = primary_calss_expr ("[" expr"]" | ("." ident)* | ("->" ident)*)
// __postFix = primary_calss_expr ("[" expr"]" | "." ident)*
// __postFix = primary_class_expr ("[" expr "]")*

// commit[23]: 添加对零参函数名声明的支持
// commit[30]: 新增对 "sizeof" 的支持
// commit[39]: 新增对 ND_GNU_EXPR 的支持
// primary_class_expr = "(" "{" stamt+ "}" ")" |'(' expr ')' | num | ident fun_args? | "sizeof" third_class_expr | str

// commit[24]: 函数调用 在 primary_class_expr 前看一个字符确认是函数声明后的定义
// funcall = ident "(" (expr ("," expr)*)? ")"

Object* Local;      // 函数内部变量
Object* Global;     // 全局变量 + 函数定义(因为 C 不能发生函数嵌套 所以一定是全局的层面)

// 复合字面量: 在代码中直接创建一个结构体或数组的匿名实例
// static: 文件作用域  只能在定义它的文件中访问   对比 type 的非静态定义  很适合需要临时对象的类型
// HEADScope: 指向零初始化的 Scope 结构体指针  每次访问 HEADScope 仍然指向同一块静态分配的空间  并且保持上一次修改后的状态
// 类似于链表操作的头结点  本身不储存实际内容  只是为了方便操作比如头插法  在默认初始化的时候 NODE_KIND 会被默认化为 ND_NUM

// 一个 HEADScope 只能存储一个函数 但是可以有任意多个代码块
static Scope *HEADScope = &(Scope){};

/* 变量域的操作定义 */

// 块域 {} 执行开始
static void enterScope(void) {
    Scope* newScope = calloc(1, sizeof(Scope));

    newScope->next = HEADScope;
    HEADScope = newScope;
}

// 块域 {} 执行结束
static void leaveScope() {
    HEADScope = HEADScope->next;
}

static VarInScope* pushVarScopeToScope(char* Name, Object* var) {
    VarInScope* newVarScope = calloc(1, sizeof(VarInScope));

    newVarScope->scopeName = Name;
    newVarScope->varList = var;

    newVarScope->next = HEADScope->varScope;
    HEADScope->varScope = newVarScope;

    return newVarScope;
}

// commit[52]: 存入 struct 类型模板
static void pushTagScopeToScope(Token* tok, Type* tagType) {
    TagScope* newTagScope = calloc(1, sizeof(TagScope));

    newTagScope->tagType = tagType;
    newTagScope->tagName = strndup(tok->place, tok->length);

    newTagScope->next = HEADScope->tagScope;
    HEADScope->tagScope = newTagScope;
}

static Object* findVar(Token* tok) {
    // commit[44]: 在嵌套的代码块遍历查找
    for (Scope* currScp = HEADScope; currScp ; currScp = currScp->next) {
        for (VarInScope* currVarScope = currScp->varScope; currVarScope; currVarScope = currVarScope->next) {
            if (equal(tok, currVarScope->scopeName))
                return currVarScope->varList;
        }
    }

    return NULL;
}

// commit[52]: 类似 findVar() 找到目标结构体类型模板
static Type* findStructTag(Token* tok) {
    for (Scope* currScope = HEADScope; currScope; currScope = currScope->next) {
        for (TagScope* currTagScope = currScope->tagScope; currTagScope; currTagScope = currTagScope->next) {
            if (equal(tok, currTagScope->tagName))
                return currTagScope->tagType;
        }
    }
    return NULL;
}

// commit[31]: 针对局部变量和全局变量的共同行为 函数复用
static Object* newVariable(char* varName, Type* varType) {
    Object* obj = calloc(1, sizeof(Object));
    obj->var_type = varType;
    obj->var_name = varName;

    // commit[44]: 压入 newVarScope 中
    pushVarScopeToScope(varName, obj);

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
    return (equal(tok, "int") || equal(tok, "char")) || equal(tok, "struct") || equal(tok, "union");
}

static structMember* getStructMember(Type* structType, Token* tok) {
    // 遍历结构体所有的成员变量返回 .x 的目标变量
    for (structMember* mem = structType->structMemLink; mem; mem = mem->next) {
        if (mem->memberName->length == tok->length &&
            !strncmp(mem->memberName->place, tok->place, tok->length))
            return mem;
    }

    tokenErrorAt(tok, "No such struct member");
    return NULL;
}


static Token* functionDefinition(Token* tok, Type* funcReturnBaseType);
static Token* gloablDefinition(Token* tok, Type* globalBaseType);

static Type* declarator(Token** rest, Token* tok, Type* Base);
static Type* declspec(Token** rest, Token* tok);
static Node* declaration(Token** rest, Token* tok);

static Type* structDeclaration(Token** rest, Token* tok);
static Type* unionDeclaration(Token** rest, Token* tok);
static Type* StructOrUnionDecl(Token** rest, Token* tok);

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


/* AST 结构的辅助函数 */

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

/* 指针的加减运算 */

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

    // commit[49]: struct 整体作为一个自定义类型
    if (equal(tok, "struct")) {
        return structDeclaration(rest, tok->next);
    }

    // commit[54]: 复用完成 union 的解析
    if (equal(tok, "union")) {
        return unionDeclaration(rest, tok->next);
    }

    tokenErrorAt(tok, "unexpected preFix declaration\n");
    return NULL;
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
    // commit[49]: 记录结构体的名称定义 更新 tok
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

    // case1: 函数定义 & 读取函数名
    // case2: 传参解析 & 读取传参变量名
    // case3: 记录成员变量的名称 & 结构体本身的名称
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

// commit[32]: 判断当前的语法是函数还是全局变量
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

// commit[49]: 以链表的形式存储所有成员变量
static void structMembers(Token** rest, Token* tok, Type* structType) {
    structMember HEAD = {};
    structMember* Curr = &HEAD;

    while (!equal(tok, "}")) {
        // 支持结构体的递归执行
        Type* memberType = declspec(&tok, tok);

        // 类似于变量 declaration 成员变量也可能是连续定义的
        int First = true;
        while (!consume(&tok, tok, ";")) {
            if (!First)
                tok = skip(tok, ",");
            First = false;

            // 结构体中的每一个成员都作为 struct structMember 存储
            structMember* newStructMember = calloc(1, sizeof(structMember));

            // 使用 declarator 支持复杂的成员定义
            newStructMember->memberType = declarator(&tok, tok, memberType);
            // 后续对成员变量的访问通过 structName 判断
            newStructMember->memberName = newStructMember->memberType->Name;

            // 此时的成员变量是顺序存在 structType 中  因为涉及后续的 Offset 的 maxOffset  所以必须是顺序的
            Curr->next = newStructMember;
            Curr = Curr->next;
        }
    }

    *rest = tok->next;
    structType->structMemLink = HEAD.next;
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

    // Q: 这里结合函数本身定义的 {} 同样会在 compoundStamt() 中声明 是无效的
    // 截至 commit[44] 即使是注释掉也 ok  看看后面是否一定需要
    enterScope();

    // 第一次更新 Local: 函数形参
    createParamVar(funcType->formalParamLink);
    function->formalParam = Local;

    tok = skip(tok, "{");
    // 第二次更新 Local: 更新函数内部定义变量
    function->AST = compoundStamt(&tok, tok);
    function->local = Local;

    // commit[44]: 函数访问结束 退出
    leaveScope();

    return tok;
}

// commit[32]: 正式在 AST 中加入全局变量的处理
static Token* gloablDefinition(Token* tok, Type* globalBaseType) {
    // 如果连续的变量定义 第一个变量不会从 ", var" 开始判断
    bool isLast = true;

    // Tip: 全局的结构体标签不会进入该分支  如果有匿名实例 会进入 newGlobal() 声明
    while (!consume(&tok, tok, ";")) {
        // 判断是否为连续的全局变量的定义  同时正确解析第一个变量
        if (!isLast)
            // 目前的语法不支持全局变量的赋值  还没有办法接入 ND_ASSIGN 语法
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

    // commit[44]: 每次进入一个新的 {} 范围执行 enter
    enterScope();

    while (!equal(tok, "}")) {
        // 针对定义语句和表达式语句分别处理
        if (isTypeName(tok))
            Curr->next = declaration(&tok, tok);
        else
            Curr->next = stamt(&tok, tok);

        Curr = Curr->next;
        addType(Curr);
    }
    // 得到 CompoundStamt 内部的语句链表

    // commit[44]: 同理 退出域
    leaveScope();

    ND->Body = HEAD.next;

    *rest = tok->next;
    return ND;
}

// 对变量定义语句的解析
// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node* declaration(Token** rest, Token* tok) {
    // 1. 解析 declspec 类型语法  放在循环外面应用于所有的声明变量
    // commit[49]: 在 declspec 中完成对结构体的解析
    Type* nd_base_type = declspec(&tok, tok);
    
    // commit[49]: 处理结构体的定义匿名实例变量名

    Node HEAD = {};
    Node* Curr = &HEAD;

    // 连续定义的合法性判断
    int variable_count = 0;
    // 2. 如果存在连续声明  需要通过 for-loop 完成
    while(!equal(tok, ";")) {
        if ((variable_count ++) > 0)
            // Tip: 函数嵌套定义报错位置
            tok = skip(tok, ",");

        // 单纯的变量声明不会写入 AST 结构  只是更新了 Local | Global 链表
        Type* isPtr = declarator(&tok, tok, nd_base_type);
        Object* var = newLocal(getVarName(isPtr->Name), isPtr);

        // 3. 如果存在赋值那么解析赋值部分
        if (!equal(tok, "=")) {
            continue;
        }

        // 通过递归下降的方式构造
        Node* LHS = singleVarNode(var, isPtr->Name);
        Node* RHS = assign(&tok, tok->next);

        // Tip: int a, b = 2;  这样的赋值语句 a 是不会被赋值 2 的
        // ND_ASSIGN->LHS: 存储变量的链表 var(b)->var(a)  但是只有 var(b) 会在 codeGen() 中被赋值
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

// commit[50]: 新增结构体的对齐
static Type* structDeclaration(Token** rest, Token* tok) {
    Type* structType = StructOrUnionDecl(rest, tok);
    structType->Kind = TY_STRUCT;

    int totalOffset = 0;
    // int-char-int => 0-8-9-16-24
    for (structMember* newStructMem = structType->structMemLink; newStructMem; newStructMem = newStructMem->next) {
        // 确定当前变量的起始位置  判断是否会对齐  => 判断 realTotal 是否为 aimAlign 的倍数
        // realTotal < 1.0 * aimAlign => 由小变大  填充字节  反之相反
        totalOffset = alignTo(totalOffset, newStructMem->memberType->alignSize);
        newStructMem->offset = totalOffset;

        // 为下一个变量计算起始位置准备
        totalOffset += newStructMem->memberType->BaseSize;

        // 判断是否更新结构体对齐最大值  本质上 structType->alignSize = maxSingleOffset
        if (structType->alignSize < newStructMem->memberType->alignSize)
            structType->alignSize = newStructMem->memberType->alignSize;
    }

    // Q: 为什么这里需要额外一次对齐
    // A: 比如 int-char 这种情况  最后的 char 也要单独留出 8B 否则 9B 中另外的 7B 读取会出问题
    structType->BaseSize = alignTo(totalOffset, structType->alignSize);
    // __structType->BaseSize = Offset;

    return structType;
}

// commit[54]: 针对 union 的偏移量计算
static Type* unionDeclaration(Token** rest, Token* tok) {
    Type* unionType = StructOrUnionDecl(rest, tok);
    unionType->Kind = TY_UNION;

    // 相比于 struct, union 的偏移量设置为成员中最大的
    // 并且由于空间共用  所有成员的偏移量都是 0
    for (structMember* newUnionMem = unionType->structMemLink; newUnionMem; newUnionMem = newUnionMem->next) {
        if (unionType->alignSize < newUnionMem->memberType->alignSize)
            // 根据成员的最大对齐值更新 union 的对齐值
            unionType->alignSize = newUnionMem->memberType->alignSize;

        if (unionType->BaseSize < newUnionMem->memberType->BaseSize)
            unionType->BaseSize = newUnionMem->memberType->BaseSize;
    }

    unionType->BaseSize = alignTo(unionType->BaseSize, unionType->alignSize);

    return unionType;
}

// commit[54]: 基于 union 和 struct 共同的语法结构复用
static Type* StructOrUnionDecl(Token** rest, Token* tok) {
    // tok = skip(tok, "{");
    // commit[52]: 增加结构体标签的判断后 tok->label 而不是 "{"
    Token* newTag = NULL;

    if (tok->token_kind == TOKEN_IDENT) {
        // 存在记录结构体标签的 token
        newTag = tok;
        tok = tok->next;
    }

    // Tip: 结构体定义和使用结构体标签定义变量是相同的语法前缀  分叉点在 "{..}"  所以这里进行一次判断
    if ((newTag != NULL) && !equal(tok, "{")) {
        // 判断能否找到使用的结构体标签
        Type* tagType = findStructTag(newTag);
        if (tagType == NULL)
            tokenErrorAt(newTag, "unknown struct type name");

        *rest = tok;
        return tagType;
    }

    // 如果进入这里 说明这是一个结构体定义 "{...}"
    // 分配记录结构体元数据的空间
    Type* structType = calloc(1, sizeof(Type));
    structType->Kind = TY_STRUCT;       // 虽然在后面被覆盖了  但是这里真的不删吗

    // 解析结构体成员
    // Tip: 这里在 commit[52] 添加结构体标签更新后 tok 要更新为 next
    structMembers(rest, tok->next, structType);

    // 结构体对齐初始化 1B
    // Q: 如果改 0: 会发生 Floating point exception (core dumped)
    // A: 在 test/struct.c 中有一个空结构体测试  导致在 BaseSize 更新中除零异常  但是空结构体在语法上是合法的
    structType->alignSize = 1;

    // 原本的成员偏移量计算由 structDeclaration() 和 unionDeclaration() 处理

    // Q: 为什么在这里进行一次 newTag 判断
    // A: 比如匿名结构体  没有办法复用也就不需要压栈
    // 实际上使用结构体标签定义变量的时候  newTag 也会被赋值存在  所以要在判断为变量定义的时候即使退出  不然就会走到这里
    if (newTag)
        pushTagScopeToScope(newTag, structType);

    return structType;
}

// commit[49]: 构造访问结构体成员的 AST
// commit[54]: union 的访问结构和 struct 相同
static Node* structRef(Node* VAR_STRUCT_UNION, Token* tok) {
    // 判断该变量是结构体的合法性  比如说指针的情况  判断指针所指向的地址存储的的是否是结构体
    // 所以 addType().ND_DEREF 的处理是把 Base 的类型提取出来  而 addType().ND_VAR 是直接提取 var_type
    addType(VAR_STRUCT_UNION);
    
    if (VAR_STRUCT_UNION->node_type->Kind != TY_STRUCT && VAR_STRUCT_UNION->node_type->Kind != TY_UNION)
        tokenErrorAt(VAR_STRUCT_UNION->token, "not a struct or a union");

    // 把 x.a 拆成两个结点构成的单叉树  所以 codeGen() 都是根据 ND_STRUCT_MEMEBER 去实现
    Node* ND = createSingle(ND_STRUCT_MEMEBER, VAR_STRUCT_UNION, tok);
    ND->structTargetMember = getStructMember(VAR_STRUCT_UNION->node_type, tok);

    return ND;
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
    // commit[48]: 针对 ND_COMMA 构造 Haffman 结构
    // 因为每次递归 expr() 后 ND 会更新 不会记录上一层的 ND 所以不是镜像的 Haffman 树
    Node* ND = assign(&tok, tok);

    if (equal(tok, ",")) {
        ND = createAST(ND_COMMA, ND, expr(rest, tok->next), tok);
        return ND;
    }

    *rest = tok;
    return ND;
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
// commit[53]: 递归性支持结构体实例成员的访问
static Node* preFix(Token** rest, Token* tok) {
    Node* ND = primary_class_expr(&tok, tok);

    while (true) {
        if (equal(tok, "[")) {
            Token* idxStart = tok;
            Node* idxExpr = expr(&tok, tok->next);
            tok = skip(tok, "]");
            ND = createSingle(ND_DEREF, newPtrAdd(ND, idxExpr, idxStart), idxStart);
            continue;
        }

        // 访问结构体成员: 使用 "->" 的变量是指针  结构体实例使用 "."

        if (equal(tok, ".")) {
            ND = structRef(ND, tok->next);
            tok = tok->next->next;
            continue;
        }

        if (equal(tok, "->")) {
            // 本质是把 structInstancePtr->member 解析为 *(&structInstance + memberOffset)
            // 相比于结构体实例直接访问  结构体要先通过指针指向的地址找到实例的首地址  所以多了一层 DEREF 结构  然后同理计算偏移量
            ND = createSingle(ND_DEREF, ND, tok);
            ND = structRef(ND, tok->next);

            tok = tok->next->next;

            // 通过 continue 支持 a->b->c 的递归访问
            continue;
        }

        *rest = tok;
        return ND;
    }
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
        // 本质上还是要把 Type 记录的元数据传递上来
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
    // 在测试 commit[48] 突发了报错  经过 3h 的排查找到是 funcall 的问题(吐血...

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

        // 问题就出现在 expr() 和 assign() 的层次调用上  在 commit[24] 实现 funcall() 的时候
        // expr() 和 assign() 调用层次没有区别所以测试 Ok 但是在 expr() 支持 ND_COMMA 后层次不同  所以报错
        // Curr->next = expr(&tok, tok);
        Curr->next = assign(&tok, tok);

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
