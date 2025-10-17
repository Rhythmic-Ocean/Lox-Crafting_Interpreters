package com.craftinginterpreters.lox;

/*
 * class Box {}
fun notMethod(argument) {
  print "called function with " + argument;
}
var box = Box();
box.function = notMethod;
box.function("argument");
 */
//THis is a function field call, not a method call

/*
 * class Box {
  method() {
    print this;
  }
}
 */
//This one is a method call, if u make an instance of Box() and do calls
import java.util.HashMap;
import java.util.Map;

class LoxInstance {
    private LoxClass klass;
    private final Map<String, Object> fields = new HashMap<>();

    LoxInstance(LoxClass klass){
        this.klass = klass;
    }

    Object get(Token name){
        if(fields.containsKey(name.lexeme)){
            return fields.get(name.lexeme);
        }

        LoxFunction method = klass.findMethod(name.lexeme);
        if(method != null) return method.bind(this);

        throw new RuntimeError(name, "Undefined property '" + name.lexeme + "'.");
    }

    @Override
    public String toString(){
        return klass.name + " instance";
    }

    void set(Token name, Object value){
        fields.put(name.lexeme,value);
    }
}
