package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

class Environment {//to store all the variables and it's values!!

    final Environment enclosing;

    Environment() {//for global scope with no enclosing, also enclosing is recursive and we need an eventual end of chain
        this.enclosing = null;
    }

    public Environment(Environment enclosing) {
        this.enclosing = enclosing;
    }

    public final Map<String, Object> values = new HashMap<>();

    Object get(Token name){
        if (values.containsKey(name.lexeme)){
            return values.get(name.lexeme);
        }

        if (enclosing != null) return enclosing.get(name); //also recursive if it's not global and specially if it's a block inside a local
        //and this only goes off if the corresponding var name is not inside the immediate local/block
        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'.");//it's runtime error cuz during compilation we might only know if a certain var was defined, not if it's scope is proper
    }

    void define(String name, Object value){//note we don't check if a key already exists, we just add into it, so a variable's "defination" could be it's redefination
        values.put(name, value);
    }

    void assign(Token name, Object value){
        if(values.containsKey(name.lexeme)){
            values.put(name.lexeme, value);
            return;
        }
        if (enclosing != null) {
            enclosing.assign(name, value);
            return;
        }
        throw new RuntimeError(name, //if key doesn't already exist in the environment, throw runtime error
            "Undefined variable '" + name.lexeme + "'.");
        }


    //NOTE: Methods and fields of a class have dynamic scoping while normal variables have lexical/ static scope
    //eg of dynamic scoping:
    /*
     class Saxophone {
        play() {
            print "Careless Whisper";
        }
    }

    class GolfClub {
        play() {
            print "Fore!";
        }
    }

    fun playIt(thing) {
        thing.play();
      }
     */
    //here unless we know the the type of 'thing' passed to the function playIt we won't know wether it's Saxophone or GolfClub class that's about to come come

}
