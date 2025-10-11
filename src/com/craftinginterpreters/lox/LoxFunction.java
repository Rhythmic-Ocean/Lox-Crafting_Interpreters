 package com.craftinginterpreters.lox;

import java.util.List;

public class LoxFunction implements LoxCallable {
    private final Environment closure;
    private final Stmt.Function declaration;

    LoxFunction(Stmt.Function declaration, Environment closure) {
        this.closure = closure;
        this.declaration = declaration;
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments){//note that a new env is created after each call, not at declaration. 
        Environment environment = new Environment(closure);
        for(int i = 0; i < declaration.params.size(); i++){//binding the parameters and arguments
            environment.define(declaration.params.get(i).lexeme, arguments.get(i));
        }
        try {
            interpreter.executeBlock(declaration.body, environment);//then executing the function's body with the new env (after binding parameters and args)
        } catch (Return returnValue) {
            return returnValue.value;
        }
        return null;
    }

    @Override
    public int arity(){
        return declaration.params.size();
    }

    @Override //output if the use decides to print the function value without passing any vars
    public String toString(){
        return "<fn >" + declaration.name.lexeme + ">";
    }
}