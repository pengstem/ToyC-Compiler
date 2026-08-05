%option noyywrap nounput noinput
%option never-interactive
%option prefix="toycc"

%{
#include <string>
#include "parser_driver.h"
#include "toycc_parser.hpp"
%}

%x COMMENT

%%

"/*"              { BEGIN(COMMENT); }
<COMMENT>"*/"     { BEGIN(INITIAL); }
<COMMENT>[^*\n]+  ;
<COMMENT>"*"      ;
<COMMENT>"\n"    ;

"//"[^\n]*       ;

[ \t\r\n]+       ;

"const"           { return yy::parser::token::CONST; }
"int"             { return yy::parser::token::INT; }
"void"            { return yy::parser::token::VOID; }
"return"          { return yy::parser::token::RETURN; }
"if"              { return yy::parser::token::IF; }
"else"            { return yy::parser::token::ELSE; }
"while"           { return yy::parser::token::WHILE; }
"break"           { return yy::parser::token::BREAK; }
"continue"        { return yy::parser::token::CONTINUE; }

"="               { return yy::parser::token::ASSIGN; }
";"               { return yy::parser::token::SEMI; }
","               { return yy::parser::token::COMMA; }
"("               { return yy::parser::token::LPAREN; }
")"               { return yy::parser::token::RPAREN; }
"{"               { return yy::parser::token::LBRACE; }
"}"               { return yy::parser::token::RBRACE; }

"=="              { return yy::parser::token::EQ; }
"!="              { return yy::parser::token::NE; }
"<="              { return yy::parser::token::LE; }
">="              { return yy::parser::token::GE; }
"<"               { return yy::parser::token::LT; }
">"               { return yy::parser::token::GT; }
"+"               { return yy::parser::token::ADD; }
"-"               { return yy::parser::token::SUB; }
"*"               { return yy::parser::token::MUL; }
"/"               { return yy::parser::token::DIV; }
"%"               { return yy::parser::token::MOD; }
"&&"              { return yy::parser::token::AND; }
"||"              { return yy::parser::token::OR; }
"!"               { return yy::parser::token::NOT; }

[a-zA-Z_][a-zA-Z0-9_]* { return yy::parser::token::IDENT; }
[0-9]+            { return yy::parser::token::INT_LITERAL; }

.                 { return yy::parser::token::INVALID; }

%%
