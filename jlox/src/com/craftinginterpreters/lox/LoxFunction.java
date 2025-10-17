 package com.craftinginterpreters.lox;

import java.util.List;

public class LoxFunction implements LoxCallable {
    private final Environment closure;
    private final Stmt.Function declaration;
    private final boolean isInitializer;

    LoxFunction(Stmt.Function declaration, Environment closure, boolean isInitializer)
    {
        this.isInitializer = isInitializer;
        this.closure = closure;
        this.declaration = declaration;
    }

    LoxFunction bind(LoxInstance instance){
        Environment environment = new Environment(closure);
        environment.define("this", instance);
        return new LoxFunction(declaration, environment, isInitializer);
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments){//note that a new env is created after each call, not at declaration (now the closure is created at decleartion not at calls)
        Environment environment = new Environment(closure);
        for(int i = 0; i < declaration.params.size(); i++){//binding the parameters and arguments
            environment.define(declaration.params.get(i).lexeme, arguments.get(i));
        }
        try {
            interpreter.executeBlock(declaration.body, environment);//then executing the function's body with the new env (after binding parameters and args)
        } catch (Return returnValue) {
            if(isInitializer)return closure.getAt(0, "this");
            return returnValue.value;
        }
        if (isInitializer) return closure.getAt(0, "this");
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