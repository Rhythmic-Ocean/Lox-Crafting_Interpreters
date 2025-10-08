package com.craftinginterpreters.lox;

class Token{ //creating a token type that will hold on each lexeme with other important information
  final TokenType type;//actual token type
  final String lexeme;//raw substring of the token
  final Object literal;//the literal value in case of string, numbers etc
  final int line; //note that some advance interpreters also store columns to make it easier for error handling
  //and since we have to constantly monitor the line here it might create a bit of overhead
  //also the fact that 


  Token(TokenType type, String lexeme, Object literal, int line){
    this.type = type;
    this.lexeme = lexeme;
    this.literal = literal;
    this.line = line;
  }

  public String toString(){
    return type + " " + lexeme + " " + literal;
  }

}