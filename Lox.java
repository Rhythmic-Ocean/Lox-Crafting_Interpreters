package com.craftinginterpreters.lox;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class Lox{
    static boolean hadError = false;
    public static void main(String[] args) throws IOException{
        if (args.length > 1){
            System.out.println("Usage jlox: [Script]");
        }

        if (args.length == 1){
            runFile(args[0]);
        }
        else{
            runPrompt();
        }
    }

    private static void runFile(String str) throws IOException{
        byte[] bytes = Files.readAllBytes(Paths.get(str));
        run(new String(bytes, Charset.defaultCharset()));
        if (hadError) System.exit(65);
    }


    private static void runPrompt() throws IOException{
        InputStreamReader input = new InputStreamReader(System.in); //converting incoming inputstream to utf-8
        BufferedReader buff = new BufferedReader(input);//store everything from inputstreamreader to buffer for easy access

        for(;;){
            System.out.println("> ");
            String r = buff.readLine();//reading one line at a time from buffer for further processing
            if (r == null) break;
            run(r);
            hadError = false; //so error in one place doesn't stop the entire game
        }
    }

    private static void run (String source) throws IOException{
        Scanner scan = new Scanner(source);
        List<Token> tokens = scan.scanTokens();
        for(Token tok : tokens){
            System.out.println(tok);
        }
    }

    static void error(int line, String message){
        report(line, "", message);
    }

    private static void report(int line, String where, String message){
        System.err.println( "[line: " + line + "] Error " + where + ": "+ message);
        hadError = true;
    }
}