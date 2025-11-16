package com.craftinginterpreters.lox;

import static com.craftinginterpreters.lox.TokenType.AND;
import static com.craftinginterpreters.lox.TokenType.BANG;
import static com.craftinginterpreters.lox.TokenType.BANG_EQUAL;
import static com.craftinginterpreters.lox.TokenType.CLASS;
import static com.craftinginterpreters.lox.TokenType.COMMA;
import static com.craftinginterpreters.lox.TokenType.DOT;
import static com.craftinginterpreters.lox.TokenType.ELSE;
import static com.craftinginterpreters.lox.TokenType.EOF;
import static com.craftinginterpreters.lox.TokenType.EQUAL;
import static com.craftinginterpreters.lox.TokenType.EQUAL_EQUAL;
import static com.craftinginterpreters.lox.TokenType.FOR;
import static com.craftinginterpreters.lox.TokenType.FUN;
import static com.craftinginterpreters.lox.TokenType.GREATER;
import static com.craftinginterpreters.lox.TokenType.GREATER_EQUAL;
import static com.craftinginterpreters.lox.TokenType.IDENTIFIER;
import static com.craftinginterpreters.lox.TokenType.IF;
import static com.craftinginterpreters.lox.TokenType.LEFT_BRACE;
import static com.craftinginterpreters.lox.TokenType.LEFT_PAREN;
import static com.craftinginterpreters.lox.TokenType.LESS;
import static com.craftinginterpreters.lox.TokenType.LESS_EQUAL;
import static com.craftinginterpreters.lox.TokenType.MINUS;
import static com.craftinginterpreters.lox.TokenType.NIL;
import static com.craftinginterpreters.lox.TokenType.NUMBER;
import static com.craftinginterpreters.lox.TokenType.OR;
import static com.craftinginterpreters.lox.TokenType.PLUS;
import static com.craftinginterpreters.lox.TokenType.PRINT;
import static com.craftinginterpreters.lox.TokenType.RETURN;
import static com.craftinginterpreters.lox.TokenType.RIGHT_BRACE;
import static com.craftinginterpreters.lox.TokenType.RIGHT_PAREN;
import static com.craftinginterpreters.lox.TokenType.SEMICOLON;
import static com.craftinginterpreters.lox.TokenType.SLASH;
import static com.craftinginterpreters.lox.TokenType.STAR;
import static com.craftinginterpreters.lox.TokenType.STRING;
import static com.craftinginterpreters.lox.TokenType.SUPER;
import static com.craftinginterpreters.lox.TokenType.THIS;
import static com.craftinginterpreters.lox.TokenType.TRUE;
import static com.craftinginterpreters.lox.TokenType.VAR;
import static com.craftinginterpreters.lox.TokenType.WHILE;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;



public class Scanner{
  private final String source;
  private final List<Token> tokens = new ArrayList<>();
  private int start = 0;//start of the token
  private int current = 0;//current scanning point of the said token
  private int line = 1;
  private static final Map<String, TokenType> keywords;

  static{ //this will run only once when the class is initialized
    keywords = new HashMap<>();
    keywords.put("and", AND);
    keywords.put("class", CLASS);
    keywords.put("else", ELSE);
    keywords.put("for", FOR);
    keywords.put("fun", FUN);
    keywords.put("if", IF);
    keywords.put("nil", NIL);
    keywords.put("or", OR);
    keywords.put("print", PRINT);
    keywords.put("return", RETURN);
    keywords.put("super", SUPER);
    keywords.put("this", THIS);
    keywords.put("true", TRUE);
    keywords.put("var", VAR);
    keywords.put("while", WHILE);
  }

  Scanner(String source){
    this.source = source;
  }

  private boolean isAtEnd(){
    return current >= source.length();
  }

  //THIS IS THE METHOD BEING CALLED IN Lox.java
  List<Token> scanTokens(){
    while(!isAtEnd()){
      start = current;//start positin of current token, eg for toke "hello" start = position of "
      scanToken();//takes one token and scans it at a time
    }
    tokens.add(new Token(EOF,"", null, line ));
    return tokens;
  }

  //after switch case here is done, the function returns back to scanTokens() checks if we are at the end and continues executing
  private void scanToken(){
    char c = advance();//returns the current char and THEN increments
    switch (c){
      //very unique, just scan one char and can be exactly categorized to what it is!
      case '(' ->addToken(LEFT_PAREN);
      case ')' ->addToken(RIGHT_PAREN) ;
      case '{' ->addToken(LEFT_BRACE);
      case '}' ->addToken(RIGHT_BRACE); 
      case '.' ->addToken(DOT); 
      case '-' ->addToken(MINUS); 
      case '+' ->addToken(PLUS); 
      case ';' ->addToken(SEMICOLON); 
      case ',' ->addToken(COMMA);
      case '*' ->addToken(STAR); //we don't have / cuz // means comment so we need to take care of it later
      
      //need to go thru at least 2 chars to determine what it exactly means. eg for ! we have to check if there's = right after or not!!
      //NOTE: lox does not support ! = cuz space will be a char too!
      case '!' -> addToken(match('=') ? BANG_EQUAL: BANG);
      case '=' -> addToken(match('=') ? EQUAL_EQUAL : EQUAL);
      case '<' -> addToken(match('=') ? LESS_EQUAL: LESS);
      case '>' -> addToken(match('=') ? GREATER_EQUAL: GREATER);


      //we need to be careful about / or // and the latter is for comments
      case '/' -> {
                    if(match('/')){
                      while (peek() != '\n' &&  !isAtEnd()) advance();
                    }else if(match('*')){
                      multi_line();
                    }
                      else addToken(SLASH);
                }

      //these we just ignore!!
      case ' ' -> {}
      case '\r'-> {}
      case '\t' -> {}
      case '\n' -> {
        line++;
      }
      //for string

      //start of a string
      case '"' -> string();

      //for digits, reserved words and vars
      default -> {
        //for digits
          if (isDigit(c)){
            number();
          }
          //for reserved words and identifiers
          else if(isAlpha(c)){
            identifier();
          }
          else{
            //if None of the string is recognized, throw error!!
            Lox.error(line, "Unexpected character. ");
          }
       } //we move forward despite the error cuz we don't wannna have the user deal with this problem just to have another one crop up!
      //hasError also gets set so the program won't be executing!!
    }
  }

  //this is a lookahead, we only have one character of lookaheahead rn
  private char peek(){ 
    if (isAtEnd()) return '\0'; // /0 is a fake char unlike /n and we check for /0 here cuz if source.charAt() scans EOF it will thorw a IOE exception!!
    return source.charAt(current);
  }

  private char advance(){
    return source.charAt(current++); //dis is post increment, current increases after advance() returns the current char
  }

  private void addToken(TokenType type){
    addToken(type, null);
  }

  private void addToken(TokenType type, Object literal){
    String text = source.substring(start, current);
    tokens.add(new Token (type, text, literal, line));
  }

private boolean isAlpha(char c){
  return (c>= 'a' && c<= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

private void identifier(){
  while(isAlphaNumeric(peek())) advance();

  String text = source.substring(start, current);
  TokenType type = keywords.get(text);
  if (type == null) type = IDENTIFIER;

  addToken(type); //if it's a simple identifier it's literal it null
}

public boolean isAlphaNumeric(char c){
  return isAlpha(c) || isDigit(c);
}

  private void string(){
    while (peek() != '"' && !isAtEnd()){//both cannot happen at the same time, if the loop ends due to isAtEnd() the error "Unterminated String" is thrown
      if (peek() == '\n'){ //allows multi line strings
        line ++;
      }
      advance();
    }
    if (isAtEnd()){//only if the above loop ends due to isAtEnd trigger
      Lox.error(line, "Unterminated string");
      return;
    }

    advance(); //peek does not consume the char only advance does. After getting '"' we did not consume it so we need this extra advance()

    String val = source.substring(start + 1, current -1);
    addToken(STRING, val); //the val stuff is the literal
  }

  private boolean match(char expected){
    if (source.charAt(current) != expected) return false;
    if (isAtEnd()) return false;
    current ++;
    return true;
  }

  private boolean isDigit(char c){
    if (c >= '0' && c <= '9'){
      return true;
    }

    return  false;
  }

  private void number()
  {
    while (isDigit(peek())) advance();

    if (peek() == '.' && isDigit(peekNext())) 
    {
      advance();
      while (isDigit(peek())) advance();
    }

    addToken(NUMBER, Double.parseDouble(source.substring(start, current)));

  }

  private char peekNext(){
    if(current + 1 > source.length()) return '\0';
    return source.charAt(current + 1);
  }
  private void multi_line(){
      while (peek() != '*' || peekNext() != '/')
      {
        if(isAtEnd()) {
          Lox.error(line, "Unterminated comment");
          return;
        }
        advance();
      } 
      advance();
      advance();

  }

}
