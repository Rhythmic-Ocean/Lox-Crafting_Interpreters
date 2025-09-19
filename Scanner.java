package com.craftinginterpreters.lox;

import static com.craftinginterpreters.lox.TokenType.DOT;
import static com.craftinginterpreters.lox.TokenType.EOF;
import static com.craftinginterpreters.lox.TokenType.LEFT_BRACE;
import static com.craftinginterpreters.lox.TokenType.LEFT_PAREN;
import static com.craftinginterpreters.lox.TokenType.MINUS;
import static com.craftinginterpreters.lox.TokenType.PLUS;
import static com.craftinginterpreters.lox.TokenType.RIGHT_BRACE;
import static com.craftinginterpreters.lox.TokenType.RIGHT_PAREN;
import static com.craftinginterpreters.lox.TokenType.SEMICOLON;
import static com.craftinginterpreters.lox.TokenType.STAR;
import java.util.ArrayList;
import java.util.List;


public class Scanner{
  private final String source;
  private final List<Token> tokens = new ArrayList<>();
  private int start = 0;//start of the token
  private int current = 0;//current scanning point of the said token
  private int line = 1;

  Scanner(String source){
    this.source = source;
  }

  List<Token> scanTokens(){
    while(!isAtEnd()){
      start = current;
      scanToken();
    }
    tokens.add(new Token(EOF,"", null, line ));
    return tokens;
  }

  private boolean isAtEnd(){
    return current >= source.length();
  }
  private void scanToken(){
    char c = advance();
    switch (c){
      case '('  -> addToken(LEFT_PAREN);
      case ')' ->addToken(RIGHT_PAREN) ;
      case '{' ->addToken(LEFT_BRACE);
      case '}' ->addToken(RIGHT_BRACE); 
      case ',' ->addToken(DOT); 
      case '.' ->addToken(MINUS); 
      case '+' ->addToken(PLUS); 
      case ';' ->addToken(SEMICOLON); 
      case '*' ->addToken(STAR); 
      default -> Lox.error(line, "Unexpected character. "); //we move forward despite the error cuz we don't wannna have the user deal with this problem just to have another one crop up!
      //hasError also gets set so the program won't be executing!!
    }
  }

  private char advance(){
    return source.charAt(current++);
  }

  private void addToken(TokenType type){
    addToken(type, null);
  }

  private void addToken(TokenType type, Object literal){
    String text = source.substring(start, current);
    tokens.add(new Token (type, text, literal, line));
  }
}