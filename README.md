#UPDATE: About chp 24 for clox, writing docs for it too. Probably won't be doing that for jlox tho. But might try after this. Anyone stuck in CI's clox might find my comments on source code helpful!
# Lox Bytecode VM Interpreter - clox (C)(IN_PROGRESS)
# Lox Interpreter - jlox (Java)(COMPLETED - JAVADOCS IN PROGRESS)

My implementation of the [Crafting Interpreters](https://craftinginterpreters.com) tree-walk interpreter written in Java.  
Lox is a dynamically typed scripting language with lexical scoping, functions, and classes.

---

## Features
- Full AST parser and interpreter
- Support for variables, control flow, functions and classes (with constructors, methods and inheritence)
- Native `clock()` function implemented in Java
- Clear error reporting and REPL mode

---

## How to Run with a lox file:

```bash
# Compile
javac com/craftinginterpreters/lox/*.java

# Run
java com.craftinginterpreters.lox.Lox <lox_file>
```

## OR to lunch the REPL:
```bash
# Compile
javac com/craftinginterpreters/lox/*.java

# Run
java com.craftinginterpreters.lox.Lox
```
## Example Program:
```bash
fun greet(name) {
  print "Hello, " + name + "!";
}

greet("World");
```
