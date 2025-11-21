//NOTE: delcaring variable means when it's initliazed in the memory
//defining it means when it's available for usage

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

//A local struct for parser
typedef struct{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;//to initiate error recovery
}Parser;

//Enum with precedence in order for pratt parsing
typedef enum{
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISION,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
}Precedence;

//ParseFn is just an alias for a function pointer that returns void and can take argument of type bool
typedef void (*ParseFn)(bool canAssign);

//struct that holds the infix and prefix function for any corresponding precedence
typedef struct{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
}ParseRule;

//just holds the token name and scope depth of a local variable
typedef struct{
    Token name;
    int depth;
} Local;

//a namespace equivalent for local variables, just a names of the locals are stored here tho
//NOTE: Only exists during compile time to emit corresponding bytecodes, DOES NOT pass on to the VM
typedef struct{
    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

/**
 * Returns temporary chunk for the duration of compilation
 * 
 * @return chunk*
 */
static Chunk* currentChunk(){
    return compilingChunk;
}

/**
 * Given the Token producing error and the error message to be delivered, it prints it in proper format at stderr
 * 
 * @param token Token that's at or nearest to error prone location
 * @param message Corresponding string of error message
 * 
 * @return void
 * 
 * NOTE: When at panicMode, simpley returns as we don't wanna report any more error until the program srynconizes after error
 * 
 * If panicMode is not true, toggles it, and prints an appropriate error message
 */
static void errorAt(Token* token, const char* message){
    if(parser.panicMode) return; 
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if(token->type == TOKEN_EOF){
        fprintf(stderr, " at end");
    }
    else{
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ":%s\n", message);
    parser.hadError = true;
}

static void error(const char* message){
    errorAt(&parser.current, message);
}

static void errorAtCurrent(const char* message){
    errorAt(&parser.current, message);
}

/**
 * Advances the parser to next token
 * 
 * This function invokes scanToken() until it encounts a non error token type. 
 * If an TOKEN_ERROR is thrown, it reports it and tries again. 
 * parser.current is assigned the latest token type while parser.previous 
 * holds the one right before.
 * 
 * @return void
 */
static void advance(){
    parser.previous = parser.current;
    for(;;){
    parser.current = scanToken();
   
        if(parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

/**
 * Returns true and 'advances' if the Token being pointe by parser.current is 
 * of the type desired, else error is through at current token with given 
 * message
 * 
 * @param type the desired TokenType
 * @param message a string of error message, in case the type check is negative
 * @return void
 * 
 * Special note: like match but throws error instead of returning false on negative result
 * strict tokentype check
 */
static void consume(TokenType type, const char* message){
    if(parser.current.type == type){
        advance();
        return;
    }
    errorAtCurrent(message);
}
/**
 * Just checks weather token in parser.current's type is equal to the given TokeType
 * 
 * @param type
 * @return bool
 */
static bool check(TokenType type){
    return parser.current.type == type;
}

/**
 * Returns true and 'advances' if the Token being pointed at by the parser.current
 * is of the type desired, else returns false.
 * @param type a TokenType
 * @return bool
 * 
 * Special note: like consume but returns false instead of error type
 * non-strict tokentype check
 */
static bool match(TokenType type){
    if(!check(type)) return false;
    advance();
    return true;
}

/**
 * Emits one byte to current Chunk using the writeChunk() function
 * 
 * @param byte
 * @return void
 */
static void emitByte(uint8_t byte){//writing into the chunk
    writeChunk(currentChunk(), byte, parser.previous.line);

}

/**
 * For emitting 2 bytes at once for some bytecode of type OP_CONSTANT or OP_DEFINE_GLOBAL
 * 
 * @param byte1 usually OP_CONSTANT, OP_DEFINE_GLOBAL or smthing like that
 * @param byte2 usually index of the corresponding value it represents
 */
static void emitBytes(uint8_t byte1, uint8_t byte2){
    emitByte(byte1);
    emitByte(byte2);
}

/**
 * Just invokes emitByte to return a bytecode that's OP_RETURN
 */
static void emitReturn(){
    emitByte(OP_RETURN);
}

/**
 * Given a Value, it invokes addConstant to add it to the ValueArray in the compiler chunk and 
 * return the corresponding index where the constant is stored
 * 
 * @param value the value to tbe stored in the array
 * @return uint8_t the index where it is stored
 */
static uint8_t makeConstant(Value value){
    int constant = addConstant(currentChunk(), value);
    if(constant > UINT8_MAX){
        error("Too many constants in one chunk");
        return 0;
    }
    return (uint8_t)constant;
}

/**
 * Emits OP_CONSTANT followed by the corresponding value to the chunk
 * 
 * @param value
 * @return void
 */
static void emitConstant(Value value){
    emitBytes(OP_CONSTANT, makeConstant(value));
}
/** 
 * Initiates the local namespace for the compilation process
 * 
 * @param compiler
 * 
 * @return void
*/
static void initCompiler(Compiler* compiler){
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
}
/**
 * emits OP_RETURN at the end of compilation to the chunk to indicate end of program
 * 
 * @return void
 */
static void endCompiler(){
    emitReturn();
}
/**
 * beginning the block's scope by increasing the overall compiler scope
 * 
 * @return void
 */
static void beginScope(){
    current->scopeDepth++;
}

/**
 * ending the block's scopeby decreasing the overall compiler's scope plus emitting x numbers of OP_POPs where 
 * x is the number of local variables at that scope
 * This interprets to removing all the value that's in the scope that's in the stack after the block for that scope ends
 * 
 * @return void
 */
static void endScope(){
    current->scopeDepth--;

    while(current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth){
        emitByte(OP_POP);
        current->localCount--;
    }
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

/**
 * Returns the index of the global variable name in the global ValueArray 
 * 
 * @return uint8_t
 */
static uint8_t identifierConstant(Token* name){
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}
/**
 * Returns true or false depending on weather two variable names are equivalent or not
 * 
 * @param a First Token
 * @param b Second Token
 * @return bool
 */
static bool identifiersEqual(Token* a, Token* b){
    if(a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

/**
 * Searching for given variable name in local namespace and returning it's index in current->local if found
 * returns -1 if not found
 * 
 * @param compiler namesapce for local variable
 * 
 */
static int resolveLocal(Compiler* compiler, Token* name){
    for(int i = compiler->localCount - 1; i >= 0; i--){//walking backwards to account for shadowing
        Local* local = &compiler->locals[i];
        if(identifiersEqual(name, &local->name)){
            if(local -> depth == -1){
                error("Can't read local var in it's own initializer!");
            }
            return i;
        }
    }

    return -1;
}
/**
 * Creates i.e adds aka declears the local variable by adding it to the Compiler current and creating a new local identifier.
 * Throws error if there's more than UINT8_COUNT local variable (can be changed)
 * local->depth is set to -1 as the variable has not been 'defined' yet!
 */
static void addLocal(Token name){
    if (current->localCount == UINT8_COUNT){
        error("Too many local variables in the function.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
}

/**
 * if the given variable is global just return.
 * 
 * For Local variable:
 *  It iterates through every local variable in the the Local array FROM BACK at current->local. The loop breaks when it encounters a local with:
 *      local->depth < current scopeDepth --> implies that we are out of current scope, i.e there can't be a variable of same name within the same scope
 *          we can safely define this local
 *      local->depth != 1 :dunno yet!! (as of chp 24)
 * 
 *      BUT if there's a local of same name, within the same scope, then an error is thrown.
 * 
 * if it passes the loop or breaks out of it, addLocal() is called to finally add map it as a local variable!
 * 
 * @return void
 *      
 * NOTE: For global vars, it's mapped in hashtables, which is done at runtime. So you just turn the var names to 
 *      (Value)OBJ_STRING and have it with OP_DEFINE_GLOBAL. Also have the value as Value and store it's index at
 *      ValueArray in the chunk alongside OP_CONST. GLOBAL is decleared like this:
 *      so for var a = 1+1*2;
 *      OP_CONST 
 *      INDEX_OF_1
 *      OP_CONST 
 *      INDEX_OF_1
 *      OP_CONST
 *      INDEX_OF_2
 *      OP_STAR
 *      OP_ADD
 *      OP_DEFINE_GLOBAL 
 *      INDEX_OF_a
 *      And when the vm sees OP_DEFINE_GLOBAL a at the end, it maps a with whatever is the result of var declaration. 
 *      A special note, INDEX_OF_a is usually the lowest in ValueArray, and all these INDEX_OF are uint_8 type, so just numbers
 * 
 * NOTE:
 *  var a = 1;
 *  var a = 1;
 *  A double declearation like this only a problem in local scope.
 *  It's allowed in global scope for clox.
 * 
 * 
 */
static void declareVariable(){
    if(current->scopeDepth == 0) return;

Token* name = &parser.previous;
    for(int i = current->localCount - 1; i>=0; i--){
        Local* local = &current->locals[i];
        if(local->depth != -1 && local->depth < current->scopeDepth){
            break;
        }

        if(identifiersEqual(name, &local->name)){
            error("Already a vairable with this name in scope.");
        }
    }

    addLocal(*name);
}

static void binary(bool canAssign){
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch(operatorType){
        case TOKEN_BANG_EQUAL:      emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:     emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:         emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL:   emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:            emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:      emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_BANG:            emitByte(OP_NOT); break;
        case TOKEN_PLUS:            emitByte(OP_ADD); break;
        case TOKEN_MINUS:           emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:            emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:           emitByte(OP_DIVIDE); break;
        default: return;
    }
}

static void literal(bool canAssign){
    switch(parser.previous.type){
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return;//Unreachable
    }
}

static void grouping(bool canAssign){
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void numbers(bool canAssign){
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign){
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}//+1 and -2 to remove "" and the last \0

/**
 * If resolveLocal returns -1, that means it's GLOBAL variable and corresponding stuffs are emitted
 * else it's local and OP_SET_LOCAL or GOBAL is emitted based on weather the next token is TOKEN_EQUAL or not
 * 
 * NOTE: Local and Global Variable WORK very differently even tho when compiler emits similar things when encountering them (during non declearation phase)
 *       It starts from declearation. For global it emits the corresponding string (variable name)'s index in ValueArray with OP_DEFINE_GLOBAL along 
 *       with the OP_CONSTANTs and the corresponding index for it's values. When vm sees it, it maps the variable name and the value in hashtable. So when you
 *       do OP_SET_GLOBAL or OP_GET_GLOBAL along with args, this args is nothing by the index of variable name in ValueArray which will be used by vm to look up 
 *       the corresponding value in the hash table
 * 
 *       For local variables it's very very different, almost everything is handled in compile time and all that vm does is pop and push around the values. The 'namespace'
 *       maintaining the local variable Compiler compiler is just here in compile time and will not be passed on to vm. Instead, the current->local[] arrays just MIRRORS the 
 *       arrangement of local variable's values. So what you are passing as args is the slot in vm stack where the value of the variable described will live in the vm.stack
 *       NOTE: WHEN YOU PUSH SOMETHING TO VM STACK, YOU ARE PUSHING THE VALUES NOT A BYTECODE!!
 * 
 * @param name the variable name that appears in the expression
 * @param canAssign weather it can be assigned a value or not (to avoid something like a*b = c)
 * 
 * @return void
 * 
 */
static void namedVariable(Token name, bool canAssign){
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if(arg != -1){
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    }
    else{
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if(canAssign && match(TOKEN_EQUAL)){
        expression();
        emitBytes(setOp, (uint8_t)arg);//to set global expr->setter
    }
    else{
        emitBytes(getOp, (uint8_t)arg);//to get global expr->getter
    }

    /*
    NOTE: setters vs getters
    setters->an expression that sets a value, assign and stuff
    getter->getting a value, maybe a field's getting a value from class etc
    */
}


static void variable(bool canAssign){
    namedVariable(parser.previous, canAssign);
    /*
    variable is stored as:
    OP_GET_GLOBAL string
    And we look up the string in hash table
    */
}

static void unary(bool canAssign){
    TokenType operatorType = parser.previous.type;//only - and ! works here

    //compile the operand
    parsePrecedence(PREC_UNARY);

    //emit the operator instruction
    switch(operatorType){
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

/**
 * Array of ParseRule where each index is from TokenType enum. This is how you initialize multiple array in C. Equivalent for TOKEN_LEFT_PAREN would be:
 *      rules[TOKEN_LEFT_PAREN].prefix = grouping;
        rules[TOKEN_LEFT_PAREN].infix = NULL;
        rules[TOKEN_LEFT_PAREN].precedence = PREC_NONE;
 */
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_NONE},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_NONE},
    [TOKEN_GREATER]       = {NULL,     binary,   PREC_NONE},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_NONE},
    [TOKEN_LESS]          = {NULL,     binary,   PREC_NONE},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_NONE},
    [TOKEN_IDENTIFIER]    = {variable,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {numbers,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},

};
/**
 * Core of our pratt parser. For each expression, it gets it's precedence,  
 */
static void parsePrecedence(Precedence precedence){
    advance();
    ParseFn prefixRule = getRule(parser.previous.type) -> prefix;
    if(prefixRule == NULL){
        error("Expect expression.");
        return;
    }
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    //to prevent any other expression operation from happening on the left side
    //like a*b = c+d;
    prefixRule(canAssign);

    while(precedence <= getRule(parser.current.type)->precedence){
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if(canAssign && match(TOKEN_EQUAL)){
        error("Invalid assignment target.");
    }

}
/**
 * To 'declear' both local and global variable. First declears local variable through declearVariable() 
 * On declearVariable it simply adds the local variable's token to compiler
 * For global variable, it uses identifierConstant(), which in turn stores the variable's name as value to chunk's ValueArray 
 * and returns the index where it is stored.
 * 
 * @param errorMessage the error message to be displayed in case of missing variable name
 * 
 * @return uint8_t i.e the index on ValueArray where the global variable's name is stored as OBJ_STRING, returns 0 if it's a local var
 */

static uint8_t parseVariable(const char* errorMessage){
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if(current->scopeDepth > 0) return 0;//exit if in local scope

    return identifierConstant(&parser.previous);
}

/**
 * Initializes aka defines the most recently decleared the variable and gives it it's current scope
 * 
 * @return void
 */

static void markInitialized(){
    current->locals[current->localCount - 1].depth = 
        current->scopeDepth;
}

/**
 * For globals, simply emits OP_DEFINE_GLOBAL, for local calls markInitialized() fnc
 * NOTE: In clox, global variables are "resolved after compile time", i.e we just compile the corresponding global declarations as
 *      Bytecode and store the value in chunks. So at run time as the vm goes thru the chunk, it puts the declaraion in hashtable at
 *      runtime. For local vars, the mapping of variables to it's value is done at compile time using the Compile struct, making local 
 *      variables a little more efficient. 
 * 
 * @return void
 */
static void defineVariable(uint8_t global){
    if(current->scopeDepth > 0){
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);//OP_DEFINE_GLOBAL's like OP_CONSTANT but for declaring global variables
}

/**
 * Returns the entire ParseRule for corresponding operation type
 */
static ParseRule* getRule(TokenType type){//solely to look up the parser rule
    return &rules[type];
}

/**
 * Starting point for expression parsing.
 * NOTE: expressions have SIDE EFFECTS! When expression chunks are done executing in vm stack they have a +1 effect
 *      i.e they leave behind a bytecode in vm stack. That's why it's under expressionStatement, which 'pops' that last
 *      bytecode so for an expressionStatement like this:
 *      1+1;
 *      the resulting answer, 2 which would have otherwise remained in the vm stack pops. So the above expression outputs
 *      nothing. It's different for something like printStatement tho, cuz for:
 *      print 1+1;
 *      OP_PRINT does the popping as well as printing, which results in 2 ebing printed on screen.
 * Calls parsePrecedence with lowest precedence argument 'PREC_ASSIGNMENT', start of pratt parsing execution
 * NOTE: This interpreter uses recursive descent for parsing declaration and statements, and pratt parsing for 
 *      expressions
 * 
 * @return void
 */

static void expression(){
    parsePrecedence(PREC_ASSIGNMENT);

}
/**
 * For blocks, consumes right and left braces, throws error if closing not found
 * 
 * @return void
 */

static void block(){
    while(!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)){
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

/**
 * Declaring both global and local variables. 
 * NOTE: before the variable is defined, it's scope is -1 and in accessible! It is to prevent smthing like:
 *      var a = 1;
 *      {
 *          var a = a;
 *      }
 *      Here if we had not implement -1 scoping, when var a = a happens, the inner a tries to assign itself with
 *      itself which doesn't have a initialzer yet... but having inner a initiaze as outer a's value makes more 
 *      sense, So when assignment happens the compiler parses through Local and ignores the inner a declearing seeing
 *      it's scoping as -1 and has outer a's value as the assignment target!
 * 
 * If a value has been initialized, emits the corresponding value to the chunk through expression(), else emits
 * OP_NIL as initialized value
 * 
 * @return void
 */

static void varDeclaration(){
    uint8_t global = parseVariable("Expect variable name");//global = 0, if the given variable to be decleard is local. Else global is assigned the 
    //corresponding index in ValueArray where the variable name is stored as Obj_String.

    if(match(TOKEN_EQUAL)){
        expression();//initializing var, if no initialization, the compiler simply gives it a nil initialization
    }
    else{
        emitByte(OP_NIL);
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}
/**
 * Parses expression statements (expression + ;), emits OP_POP upon successful parsing or error for semicolen missing
 * 
 * @return void
 * 
 * NOTE: Expression has a SIDE EFFCT! After it's executed it leaves behind a bytecode as a result, that's why expression
 *      statement has a OP_POP at the end to clear out the stack
 * Eg: 1 + 1;
 *      Leaves being 2 as the side effect in vm stack but since we are not supposed to display the result, we simply pop
 *      the result!
 */
static void expressionStatement(){//expressionStatemet is expression followed by a ;. Usually to call a function or evaluate an assignment for it's side effect
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

/**
 * Emits OP_PRINT bytecode to bytecode chunk or throws error if no SEMICOLEN end of statement
 * 
 * @return void
 */
static void printStatement(){
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}
/**
 * To get the parser out of the panic mode, called at declaration().
 * Passes through keywords util EOF, ; or any other declearitive or 
 * statement type tokens
 * 
 * @return void
 */

static void synchronize(){
    parser.panicMode = false;

    while(parser.current.type != TOKEN_EOF){
        if (parser.previous.type == TOKEN_SEMICOLON) return;
    }
    
    switch(parser.current.type){
        case TOKEN_CLASS:
        case TOKEN_FUN:
        case TOKEN_VAR:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
            return;
    }
}

/**
 * First step of the recursive descent. Any declearations made for each token
 * are 'matched' and sent off to be parsed in various functions. If it's not a 
 * declaration, it passes thru to statements().
 * 
 * Also, if the parser is paniking, calls synchronize to get the tokens in a state 
 * where it can parse again
 * 
 * @return void
 */
static void declaration(){
    if(match(TOKEN_VAR)){
        varDeclaration();
    }
    else{
        statement();
    }

    if(parser.panicMode) synchronize();
}

/**
 * Second step of the descent, any statements are made here. The statement's keywords are
 * matched, if the token does not preludes a statements, it's let pass through
 * 
 * @return void
 */
static void statement(){
    if(match(TOKEN_PRINT)){
        printStatement();
    }
    else if (match(TOKEN_LEFT_BRACE)){
        beginScope();//increases current->scopeDepth by +1
        block();
        endScope();//decreases current->scopeDepth by -1
    }
    else{
        expressionStatement();
    }
}

/**
 * Gets the source code and a chunk, turns the source string into bytecode 
 * and stores it in the porovided chunk location
 * 
 * @param source the source code string
 * @param chunk the compiler chunk location
 * @return bool
 */
bool compile(const char* source, Chunk* chunk){
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler);
    compilingChunk = chunk;
    parser.hadError = false;
    parser.panicMode = false;
    advance();

    while(!match(TOKEN_EOF)){
        declaration();
    }
    endCompiler();
    return !parser.hadError;//should return false when error occurs
}