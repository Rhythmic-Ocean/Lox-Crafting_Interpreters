package com.craftinginterpreters.lox;

//the caller has higher precedence than even unary, just below primary so it's sloted right before primary
//unary -> ("!"|"-") unary | call;
//call -> primary("("arguments?")")*;

/*
 def get_callback():
    def inner():
        return "Yum! Eating the ice cream scoop 🍦"
    return inner  # return the function itself, not the result yet

# Step 2: Using nested calls
result = get_callback()()  # <-- two calls chained, calls inner() too
print(result)


NOTE: () is the actual function caller!!

//arguments -> expression(","expression)*;
//NOTE: Althought the BNF expression here says expression is mandatory for arguments, it's not cuz in call itself argument is non mandatory
*/

/*
 declaration -> funDecl | varDecl | statement;
 funDecl -> "fun" function;
 function -> IDENTIFIER "(" parameters?")" block;
 */

/*
 statement      → exprStmt
              | forStmt
              | ifStmt
              | printStmt
              | returnStmt
              | whileStmt
              | block ;
   
   returnStmt -> "return" expression? ";";

 */

import static com.craftinginterpreters.lox.TokenType.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

class Parser {
	private static class ParseError extends RuntimeException {

	}

	private final List<Token> tokens;
	private int current = 0;

	Parser(List<Token> tokens) {
		this.tokens = tokens;
	}

	List<Stmt> parse() {
		List<Stmt> statements = new ArrayList<>();
		while (!isAtEnd()) {
			statements.add(declaration());
		}
		return statements;
	}

	private Stmt declaration() {
		try {
			if (match(CLASS))
				return classDeclaration();
			if (match(FUN))
				return function("function");
			if (match(VAR))
				return varDeclaration();// match also moved the position of current one step forward

			return statement();
		} catch (ParseError e) {
			synchronize();
			return null;
		}
	}

	private Stmt classDeclaration() {
		Token name = consume(IDENTIFIER, "Expect class name");
		Expr.Variable superclass = null;
		if (match(LESS)) {
			consume(IDENTIFIER, "Expect superclass name.");
			superclass = new Expr.Variable(previous());
		}
		consume(LEFT_BRACE, "Expect '{' before class body");

		List<Stmt.Function> methods = new ArrayList<>();
		while (!check(RIGHT_BRACE) && !isAtEnd()) {
			methods.add(function("method"));
		}
		consume(RIGHT_BRACE, "Expect '}' after class body.");

		return new Stmt.Class(name, superclass, methods);
	}

	private Stmt statement() {
		if (match(FOR))
			return forStatement();
		if (match(IF))
			return ifStatement();
		if (match(PRINT))
			return printStatement();
		if (match(RETURN))
			return returnStatement();
		if (match(WHILE))
			return whileStatement();
		if (match(LEFT_BRACE))
			return new Stmt.Block(block());

		return expressionStatement();
	}

	private Stmt forStatement() {
		consume(LEFT_PAREN, "Expect '(' after 'for'.");

		Stmt initializer;
		if (match(SEMICOLON)) {
			initializer = null;
		} else if (match(VAR)) {
			initializer = varDeclaration();
		} else {
			initializer = expressionStatement();
		}

		Expr condition = null;
		if (!check(SEMICOLON)) {
			condition = expression();
		}
		consume(SEMICOLON, "Expect ';' after loop condition.");

		Expr increment = null;
		if (!check(RIGHT_PAREN)) {
			increment = expression();
		}
		consume(RIGHT_PAREN, "Expect ')' after for clauses.");

		Stmt body = statement();
		if (increment != null) {
			body = new Stmt.Block(
					Arrays.asList(
							body,
							new Stmt.Expression(increment)));
		}
		if (condition == null)
			condition = new Expr.Literal(true);
		body = new Stmt.While(condition, body);

		if (initializer != null) {
			body = new Stmt.Block(Arrays.asList(initializer, body));
		}

		return body;
	}

	private Stmt ifStatement() {// NOTE doing {} is valid too cuz lox has {} blocks
		consume(LEFT_PAREN, "Expect '(' after 'if'");
		Expr condition = expression();
		consume(RIGHT_PAREN, "Expect ')' after if condition");

		Stmt thenBranch = statement();
		Stmt elseBranch = null;
		if (match(ELSE)) {
			elseBranch = statement();
		}
		return new Stmt.If(condition, thenBranch, elseBranch);
	}

	private Stmt printStatement() {
		Expr value = expression();
		consume(SEMICOLON, "Expect ';' after value.");
		return new Stmt.Print(value);
	}

	private Stmt returnStatement() {
		Token keyword = previous();
		Expr value = null;
		if (!check(SEMICOLON)) {
			value = expression();
		}
		consume(SEMICOLON, "Expect ';' after return value");
		return new Stmt.Return(keyword, value);
	}

	private Stmt whileStatement() {
		consume(LEFT_PAREN, "Expect '(' after 'while'");
		Expr condition = expression();
		consume(RIGHT_PAREN, "Expect ')'after while");

		Stmt bdy = statement();
		return new Stmt.While(condition, bdy);
	}

	private Stmt expressionStatement() {
		Expr expr = expression();
		consume(SEMICOLON, "Expect';' after value.");
		return new Stmt.Expression(expr);
	}

	private Stmt.Function function(String kind) {
		Token name = consume(IDENTIFIER, "Expect " + kind + " name."); // kind is for later method's error
										// message
		consume(LEFT_PAREN, "Expect '(' after " + kind + " name.");
		List<Token> parameters = new ArrayList<>();
		if (!check(RIGHT_PAREN)) {// handles the zero parameter case
			do {
				if (parameters.size() >= 255) {
					error(peek(), "Can't have more than 255 parameters.");
				}
				parameters.add(consume(IDENTIFIER, "Expect parameter name."));
			} while (match(COMMA));
		}
		consume(RIGHT_PAREN, "Expect ')' after parameters.");

		consume(LEFT_BRACE, "Expect '{' before " + kind + " body.");
		List<Stmt> body = block();
		return new Stmt.Function(name, parameters, body);
	}

	private List<Stmt> block() {// note that the left paren is already consumed before this gets executed.
		List<Stmt> statements = new ArrayList<>();

		while (!check(RIGHT_BRACE) && !isAtEnd()) {
			statements.add(declaration());
		}

		consume(RIGHT_BRACE, "Expect '}' after block.");
		return statements;
	}

	private Stmt varDeclaration() {// this one is just for var declaration, the other match(IDENTIFIER) is for
					// other usages like printing or other assigning of variables
		Token name = consume(IDENTIFIER, "Expect variable name.");

		Expr initializer = null;
		if (match(EQUAL)) {
			initializer = expression();// should be a valid expression otherwise it error will be thrown at
							// primary
		}

		consume(SEMICOLON, "Expect ';' after variable declaration.");
		return new Stmt.Var(name, initializer);
	}

	private Expr expression() {
		return assignment();
	}

	private Expr assignment() {
		Expr expr = or();
		if (match(EQUAL)) {
			Token equals = previous();
			Expr value = assignment();

			if (expr instanceof Expr.Variable) {
				Token name = ((Expr.Variable) expr).name;// despite already checking for Expr being of
										// Variable class,
										// java needs explicit typecast to
										// access vars inside of the
										// object
				return new Expr.Assign(name, value);
			} else if (expr instanceof Expr.Get) {
				Expr.Get get = (Expr.Get) expr;
				return new Expr.Set(get.object, get.name, value);
			}
			error(equals, "Invalid assignment target.");// sends an error of the left side is not of
									// variable type
			// even a+b a+b is r value will be Expr.Binary
			// (a) = 3 will also be error cuz (a) is Expr.Grouping
			// also the error is not thrown cuz the parser knows exactly what's the problem
			// so need to go to panic mode
		}

		return expr;
	}

	private Expr or() {
		Expr expr = and();

		while (match(OR)) {
			Token operator = previous();
			Expr right = and();
			expr = new Expr.Logical(expr, operator, right);
		}
		return expr;
	}

	private Expr and() {
		Expr expr = equality();

		while (match(AND)) {
			Token operator = previous();
			Expr right = equality();
			expr = new Expr.Logical(expr, operator, right);
		}
		return expr;
	}

	private Expr equality() {
		Expr expr = comparision();
		while (match(BANG_EQUAL, EQUAL_EQUAL)) {
			Token operator = previous();
			Expr right = comparision();
			expr = new Expr.Binary(expr, operator, right);
		}
		return expr;
	}

	private Expr comparision() {
		Expr expr = term();

		while (match(GREATER, GREATER_EQUAL, LESS, LESS_EQUAL)) {
			Token operator = previous();
			Expr right = term();
			expr = new Expr.Binary(expr, operator, right);
		}
		return expr;
	}

	private Expr term() {
		Expr expr = factor();

		while (match(MINUS, PLUS)) {
			Token operator = previous();
			Expr right = factor();
			expr = new Expr.Binary(expr, operator, right);
		}

		return expr;
	}

	private Expr factor() {
		Expr expr = unary();

		while (match(SLASH, STAR)) {
			Token operator = previous();
			Expr right = unary();
			expr = new Expr.Binary(expr, operator, right);
		}

		return expr;
	}

	private Expr unary() {
		if (match(BANG, MINUS)) {
			Token operator = previous();
			Expr right = unary();
			return new Expr.Unary(operator, right);
		}
		return call();
	}

	private Expr call() {
		Expr expr = primary();// will most likely be an identifier?

		while (true) {
			if (match(LEFT_PAREN)) {// checks for () if it matches, it's a function call!
				expr = finishCall(expr);
			} else if (match(DOT)) {
				Token name = consume(IDENTIFIER, "Expect property name after '.'");
				expr = new Expr.Get(expr, name); // here expr might be an instance
			} else {
				break;
			}
		}
		return expr;
	}

	private Expr finishCall(Expr callee) {
		List<Expr> arguments = new ArrayList<>();
		if (!check(RIGHT_PAREN)) {
			do {
				if (arguments.size() >= 255) {
					error(peek(), "Can't have more than 255 arguments.");
				}
				arguments.add(expression());
			} while (match(COMMA));
		}
		Token paren = consume(RIGHT_PAREN, "Expect ')' after arguments.");

		return new Expr.Call(callee, paren, arguments);
	}

	private Expr primary() {
		if (match(FALSE))
			return new Expr.Literal(false);
		if (match(TRUE))
			return new Expr.Literal(true);
		if (match(NIL))
			return new Expr.Literal(null);

		if (match(NUMBER, STRING)) {
			return new Expr.Literal(previous().literal);
		}

		if(match(SUPER)){
			Token keyword = previous();
			consume(DOT, "Expect '.' after 'super'.");
			Token method = consume(IDENTIFIER, "Expect superclass method name.");
			return new Expr.Super(keyword, method);
		}

		if (match(THIS))
			return new Expr.This(previous());
		if (match(IDENTIFIER)) {
			return new Expr.Variable(previous());
		}

		if (match(LEFT_PAREN)) {
			Expr expr = expression();
			consume(RIGHT_PAREN, "Expect ')' after expression.");
			return new Expr.Grouping(expr);
		}

		throw error(peek(), "Expect expression.");
	}

	private boolean match(TokenType... types) {
		for (TokenType type : types) {
			if (check(type)) {
				advance();
				return true;
			}
		}
		return false;
	}

	private Token advance() {
		if (!isAtEnd())
			current++;
		return previous();
	}

	private boolean isAtEnd() {
		return peek().type == EOF;
	}

	private Token peek() {
		return tokens.get(current);
	}

	private Token previous() {
		return tokens.get(current - 1);
	}

	private Token consume(TokenType type, String message) {
		if (check(type))
			return advance();

		throw error(peek(), message);// after we get an error, we throw it and in the catch we try discarding
						// the
						// entire statement until we reach it's end.
		// and starting next statement we do normal parsing
		// but for now we just panic and unwind all the way to the top as we don't have
		// statements yet
	}

	private ParseError error(Token token, String message) {
		Lox.error(token, message);
		return new ParseError();
	}

	private void synchronize() {
		advance();

		while (!isAtEnd()) {
			if (previous().type == SEMICOLON)
				return;

			switch (peek().type) {/// this too cuz either of these spots in cases is a good place for the
						/// parser
						/// to resume it's parsing.
				case CLASS:
				case FUN:
				case VAR:
				case FOR:
				case IF:
				case WHILE:
				case PRINT:
				case RETURN:
					return;
			}
			advance();
		}
	}

	private boolean check(TokenType type) {
		if (isAtEnd())
			return false;
		return peek().type == type;
	}
}
