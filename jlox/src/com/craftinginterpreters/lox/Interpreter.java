package com.craftinginterpreters.lox;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

class Interpreter implements Expr.Visitor<Object>, Stmt.Visitor<Void> {

	final Environment globals = new Environment();
	private Environment environment = globals; // the outermost stuff will be sotred here for now.
	private final Map<Expr, Integer> locals = new HashMap<>();

	// note: gloal represents the outermost env while environment represents the
	// cureent env, the latter's value changes while the formal's doesnt'

	Interpreter() {
		globals.define("clock", new LoxCallable() {
			@Override
			public int arity() {
				return 0;
			}

			@Override
			public Object call(Interpreter interpreter, List<Object> arguments) {
				return (double) System.currentTimeMillis() / 1000.0;
			}

			@Override
			public String toString() {
				return "<native fn>";
			}
		});
	}

	void interpret(List<Stmt> statements) {// for list of statements
		try {
			for (Stmt statement : statements) {
				execute(statement);
			}
		} catch (RuntimeError e) {
			Lox.runtimeError((e));
		}
	}

	@Deprecated
	void interpret(Expr expression) {// for expressions, deprecated
		try {
			Object value = evaluate(expression);
			System.out.println(stringify(value));
		} catch (RuntimeError e) {
			Lox.runtimeError(e);
		}
	}

	private void execute(Stmt stmt) {
		stmt.accept(this);
	}

	void resolve(Expr expr, int depth) {
		locals.put(expr, depth);
	}

	@Override
	public Void visitBlockStmt(Stmt.Block stmt) {
		executeBlock(stmt.statements, new Environment(environment));
		return null;
	}

	@Override
	public Void visitClassStmt(Stmt.Class stmt) {

		Object superclass = null;
		if (stmt.superclass != null) {
			superclass = evaluate(stmt.superclass);
			if (!(superclass instanceof LoxClass)) {
				throw new RuntimeError(stmt.superclass.name, "Superclas must be a class.");
			}
		}
		environment.define(stmt.name.lexeme, null);// this allows methods to reference the class
		if(stmt.superclass != null){
			environment.define("super", superclass);
		}

		Map<String, LoxFunction> methods = new HashMap<>();
		for (Stmt.Function method : stmt.methods) {
			LoxFunction function = new LoxFunction(method, environment, method.name.lexeme.equals("init"));
			methods.put(method.name.lexeme, function);
		}
		LoxClass klass = new LoxClass(stmt.name.lexeme, (LoxClass)superclass, methods);
		if(superclass != null){
			environment = environment.enclosing;
		}
		environment.assign(stmt.name, klass);
		return null;
	}

	void executeBlock(List<Stmt> statements, Environment environment) {
		Environment previous = this.environment;
		try {
			this.environment = environment;

			for (Stmt statement : statements) {
				execute(statement);
			}
		} finally {// will be executed no matter the error
			this.environment = previous;
		}
	}

	@Override
	public Void visitExpressionStmt(Stmt.Expression stmt) {
		evaluate(stmt.expression);
		return null;
	}

	@Override
	public Void visitFunctionStmt(Stmt.Function stmt) {// THIS is function decleration.
		LoxFunction function = new LoxFunction(stmt, environment, false);
		environment.define(stmt.name.lexeme, function);
		return null;
	}

	@Override
	public Void visitIfStmt(Stmt.If stmt) {
		if (isTruthy(evaluate(stmt.condition))) {
			execute(stmt.thenBranch);
		} else if (stmt.elseBranch != null) {
			execute(stmt.elseBranch);
		}
		return null;
	}

	@Override
	public Void visitPrintStmt(Stmt.Print stmt) {
		Object value = evaluate(stmt.expression);
		System.out.println(stringify(value));
		return null;
	}

	@Override
	public Void visitReturnStmt(Stmt.Return stmt) {
		Object value = null;
		if (stmt.value != null)
			value = evaluate(stmt.value);

		throw new Return(value);
	}

	@Override
	public Void visitVarStmt(Stmt.Var stmt) {
		Object value = null;// vaue in hashtable
		if (stmt.initializer != null) {
			value = evaluate(stmt.initializer);// assigning corresponding initializer as the initial value
								// for corresponding
								// variable in hash map
		}
		environment.define(stmt.name.lexeme, value);// adding new key value pair in hashmap
		return null;
	}
	// NOTE: If a variable is accessed without being initialized/ assigned a value,
	// then no runtime error is thrown, just a nil value is given

	@Override
	public Object visitAssignExpr(Expr.Assign expr) {
		Object value = evaluate(expr.value);
		Integer distance = locals.get(expr);
		if (distance != null) {
			environment.assignAt(distance, expr.name, value);
		} else {
			globals.assign(expr.name, value);
		}
		return value;
	}

	private Object evaluate(Expr expr) {
		return expr.accept(this);
	}

	@Override
	public Object visitLiteralExpr(Expr.Literal expr) {
		return expr.value;
	}

	@Override
	public Void visitWhileStmt(Stmt.While stmt) {
		while (isTruthy(evaluate(stmt.condition))) {
			execute(stmt.body);
		}
		return null;
	}

	@Override
	public Object visitLogicalExpr(Expr.Logical expr) {
		Object left = evaluate(expr.left);

		if (expr.operator.type == TokenType.OR) {
			if (isTruthy(left))
				return left;
		} else {
			if (!isTruthy(left))
				return left;
		}

		return evaluate(expr.right);
	}

	@Override
	public Object visitSetExpr(Expr.Set expr) {
		Object object = evaluate(expr.object);
		if (!(object instanceof LoxInstance)) {
			throw new RuntimeError(expr.name, "Only instances have fields.");
		}

		Object value = evaluate(expr.value);
		((LoxInstance) object).set(expr.name, value);
		return value;
	}

	@Override
	public Object visitSuperExpr(Expr.Super expr){
		int distance = locals.get(expr);
		LoxClass superclass = (LoxClass)environment.getAt(distance, "super");
		LoxInstance object = (LoxInstance)environment.getAt(distance - 1, "this");
		LoxFunction method = superclass.findMethod(expr.method.lexeme);
		if(method == null){
			throw new RuntimeError(expr.method, "Undefined property '" + expr.method.lexeme + "'."); 
		}
		return method.bind(object);
	}

	@Override
	public Object visitThisExpr(Expr.This expr) {
		return lookUpVariable(expr.keyword, expr);
	}

	@Override
	public Object visitGroupingExpr(Expr.Grouping expr) {
		return evaluate(expr.expression);
	}

	@Override
	public Object visitUnaryExpr(Expr.Unary expr) {
		Object right = evaluate(expr.right);

		switch (expr.operator.type) {
			case BANG:
				return !isTruthy(right);
			case MINUS:
				checkNumberOperand(expr.operator, right);
				return -(double) right;
		}

		// Unreachable.
		return null;
	}

	@Override
	public Object visitVariableExpr(Expr.Variable expr) {
		return lookUpVariable(expr.name, expr);
	}

	private Object lookUpVariable(Token name, Expr expr) {
		Integer distance = locals.get(expr);
		if (distance != null) {// note only locals are in the map, globals are treated specially and only
					// stored in global stuff
			return environment.getAt(distance, name.lexeme);
		} else {
			return globals.get(name);
		}
	}

	private boolean isTruthy(Object object) {
		if (object == null)
			return false;
		if (object instanceof Boolean)
			return (boolean) object;
		return true;
	}

	@Override
	public Object visitBinaryExpr(Expr.Binary expr) {
		Object left = evaluate(expr.left);
		Object right = evaluate(expr.right);

		switch (expr.operator.type) {
			case GREATER:
				checkNumberOperands(expr.operator, left, right);
				return (double) left > (double) right;
			case GREATER_EQUAL:
				checkNumberOperands(expr.operator, left, right);
				return (double) left >= (double) right;
			case LESS:
				checkNumberOperands(expr.operator, left, right);
				return (double) left < (double) right;
			case LESS_EQUAL:
				checkNumberOperands(expr.operator, left, right);
				return (double) left <= (double) right;
			case BANG_EQUAL:
				return !isEqual(left, right);
			case EQUAL_EQUAL:
				return isEqual(left, right);
			case MINUS:
				checkNumberOperands(expr.operator, left, right);
				return (double) left - (double) right;
			case PLUS:
				if (left instanceof Double && right instanceof Double) {
					return (double) left + (double) right;
				}

				if (left instanceof String && right instanceof String) {
					return (String) left + (String) right;
				}

				throw new RuntimeError(expr.operator,
						"Operands must be between two numbers or two strings.");

			case SLASH:
				checkNumberOperands(expr.operator, left, right);
				return (double) left / (double) right;
			case STAR:
				checkNumberOperands(expr.operator, left, right);
				return (double) left * (double) right;
		}

		// Unreachable.
		return null;
	}

	@Override
	public Object visitCallExpr(Expr.Call expr) {

		Object callee = evaluate(expr.callee);

		List<Object> arguments = new ArrayList<>();
		for (Expr argument : expr.arguments) {
			arguments.add(evaluate(argument));
		}

		if (!(callee instanceof LoxCallable)) {
			throw new RuntimeError(expr.paren, "Can only call functions and classes.");
		}
		LoxCallable function = (LoxCallable) callee;// if we don't implement this interface on the potential
								// calls callee
								// can belong to, it's gonna throw
								// java.lang.ClassCastException
		if (arguments.size() != function.arity()) {
			throw new RuntimeError(expr.paren,
					"Expected " + function.arity() + " argument but got " + arguments.size() + ".");
		}
		return function.call(this, arguments);
	}

	@Override
	public Object visitGetExpr(Expr.Get expr) {
		Object object = evaluate(expr.object);
		if (object instanceof LoxInstance) {
			return ((LoxInstance) object).get(expr.name);
		}

		throw new RuntimeError(expr.name, "Only instances have properties.");
	}

	private boolean isEqual(Object a, Object b) {
		if (a == null && b == null)
			return true;
		if (a == null)
			return false;

		return a.equals(b);
	}

	private void checkNumberOperand(Token operator, Object operand) {
		if (operand instanceof Double)
			return;
		throw new RuntimeError(operator, "Operand must be a number.");
	}

	private void checkNumberOperands(Token operator, Object left, Object right) {
		if (left instanceof Double && right instanceof Double)
			return;

		throw new RuntimeError(operator, "Operands must be numbers.");
	}

	private String stringify(Object object) {
		if (object == null)
			return "nil";

		if (object instanceof Double) {
			String text = object.toString();
			if (text.endsWith(".0")) {
				text = text.substring(0, text.length() - 2);
			}
			return text;
		}

		return object.toString();
	}

}
