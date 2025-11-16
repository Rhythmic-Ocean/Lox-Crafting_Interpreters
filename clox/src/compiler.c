#include <stdio.h>
#include <stdlib.h>

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

typedef void (*ParseFn)(bool canAssign);//function pointers for functions that take no arguments and return nothing

typedef struct{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
}ParseRule;

Parser parser;
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

static void advance(){
    parser.previous = parser.current;
    for(;;){
        parser.current = scanToken();
   
        if(parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

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


static void endCompiler(){
    emitReturn();
}


static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t identifierConstant(Token* name){
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
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
    uint8_t arg = identifierConstant(&name);
    if(canAssign && match(TOKEN_EQUAL)){
        expression();
        emitBytes(OP_SET_GLOBAL, arg);//to set global expr->setter
    }
    else{
        emitBytes(OP_GET_GLOBAL, arg);//to get global expr->getter
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
    return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global){
    emitBytes(OP_DEFINE_GLOBAL, global);//OP_DEFINE_GLOBAL's like OP_CONSTANT but for declaring global variables
}

static ParseRule* getRule(TokenType type){//solely to look up the parser rule
    return &rules[type];
}

static void expression(){
    parsePrecedence(PREC_ASSIGNMENT);

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

static void declaration(){
    if(match(TOKEN_VAR)){
        varDeclaration();
    }
    else{
        statement();
    }

    if(parser.panicMode) synchronize();
}

static void statement(){
    if(match(TOKEN_PRINT)){
        printStatement();
    }
    else{
        expressionStatement();
    }
}

bool compile(const char* source, Chunk* chunk){
    initScanner(source);
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