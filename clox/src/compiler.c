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


typedef struct{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;//to initiate error recovery
}Parser;

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

typedef void (*ParseFn)(bool canAssign); //ParseFn is an alias for a function pointer that returns void and takes 'bool canAssign' as argument. 

typedef struct{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
}ParseRule;

typedef struct{
    Token name;
    int depth;
} Local;

typedef struct{
    Local locals[UINT8_COUNT];
    int localCount;
    int scopeDepth;
} Compiler;

Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk;

static Chunk* currentChunk(){
    return compilingChunk;
}

static void errorAt(Token* token, const char* message){
    if(parser.panicMode) return; //while in panic mode, simply ignore any other errors sent our way
    //cuz we want the parser to first get syncronized
    //after we get in sync, ignoring all the prev error, normal error detection starts
    //but hadError will always be true, and the bytecode's never gonna get executed
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if(token->type == TOKEN_EOF){
        fprintf(stderr, " at end");
    }
    else if(token->type == TOKEN_ERROR){

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

static void emitByte(uint8_t byte){//writing into the chunk
    writeChunk(currentChunk(), byte, parser.previous.line);

}

static void emitBytes(uint8_t byte1, uint8_t byte2){
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn(){
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value){
    int constant = addConstant(currentChunk(), value);
    if(constant > UINT8_MAX){
        error("Too many constants in one chunk");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value){
    emitBytes(OP_CONSTANT, makeConstant(value));
}

static void initCompiler(Compiler* compiler){
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    current = compiler;
}

static void endCompiler(){
    emitReturn();
}

static void beginScope(){
    current->scopeDepth++;
}

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

static uint8_t identifierConstant(Token* name){
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b){
    if(a->length!= b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name){
    for(int i = compiler->localCount - 1; i >= 0; i--){//walking backwards to account for shadowing
        Local* local = &compiler->locals[i];
        if(identifiersEqual(name, &local->name)){
            if(local -> depth == -1){
                error("Can't read local var in itls own initializer!");
            }
            return i;
        }
    }

    return -1;
}

static void addLocal(Token name){
    if (current->localCount == UINT8_COUNT){
        error("Too many local variables in the function.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->depth = current->scopeDepth;
}

static void declareVariable(){
    if(current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    for(int i = current->localCount - 1; i>=00; i--){
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


static void namedVariable(Token name, bool canAssign){
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if(arg != -10){
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

ParseRule rules[] = {//array initializers in C. its like:
    //rules[TOKEN_LEFT_PAREN].prefix = grouping;
    //rules[TOKEN_LEFT_PAREN].infix = NULL;
    //rules[TOKEN_LEFT_PAREN].precedence = PREC_NONE;
    //this for everything below!
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


static uint8_t parseVariable(const char* errorMessage){
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if(current->scopeDepth > 0) return 0;//exit if in local scope

    return identifierConstant(&parser.previous);
}

static void markInitialized(){
    current->locals[current->localCount - 1].depth = 
        current->scopeDepth;
}

static void defineVariable(uint8_t global){
    if(current->scopeDepth > 0){
        markInitialized();
        return;
    }

    emitBytes(OP_DEFINE_GLOBAL, global);//OP_DEFINE_GLOBAL's like OP_CONSTANT but for declaring global variables
}

static ParseRule* getRule(TokenType type){//solely to look up the parser rule
    return &rules[type];
}

static void expression(){
    parsePrecedence(PREC_ASSIGNMENT);

}

static void block(){
    while(!check(TOKEN_RIGHT_PAREN) && !check(TOKEN_EOF)){
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect ')' after block.");
}

static void varDeclaration(){
    uint8_t global = parseVariable("Expect variable name");

    if(match(TOKEN_EQUAL)){
        expression();//initializing var, if no initialization, the compiler simply gives it a nil initialization
    }
    else{
        emitByte(OP_NIL);
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
    //the const are stored with OP_CONST
    /*
    so for var a = 1+1*2;
    OP_CONST 1
    OP_CONST 1
    OP_CONST 2
    OP_STAR
    OP_ADD
    OP_DEFINE_GLOBAL a
    
    */
}

static void expressionStatement(){//expressionStatemet is expression followed by a ;. Usually to call a function or evaluate an assignment for it's side effect
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

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
        beginScope();
        block();
        endScope();
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