%skeleton "lalr1.cc"
%require "3.8"
%defines
%define api.parser.class {parser}
%define api.value.type variant
%define parse.assert
%define parse.error detailed
%define api.token.constructor

%parse-param { toycc::ParserDriver& driver }
%lex-param { toycc::ParserDriver& driver }

%code requires {
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ast_nodes.h"

namespace toycc {
class ParserDriver;

struct DeclSpec {
  bool isConst = false;
  Type varType = Type::INT;
};
} // namespace toycc

namespace yy {
class parser;
}
}

%code {
#include <iostream>
#include <stdexcept>
#include <string>

#include "parser_driver.h"

namespace yy {
parser::symbol_type yylex(toycc::ParserDriver& driver);
}
}

%token <std::string> IDENT
%token <int> INT_LITERAL
%token CONST VOID INT RETURN IF ELSE WHILE BREAK CONTINUE
%token ASSIGN SEMI COMMA LPAREN RPAREN LBRACE RBRACE
%token EQ NE LE GE LT GT ADD SUB MUL DIV MOD AND OR NOT
%token INVALID
%token END

%type <toycc::DeclSpec> decl_specifier
%type <std::vector<std::shared_ptr<toycc::ASTNode>>> global_decl_list
%type <std::shared_ptr<toycc::ASTNode>> global_decl
%type <std::shared_ptr<toycc::Decl>> decl
%type <std::shared_ptr<toycc::FuncDef>> func_def
%type <std::shared_ptr<toycc::FuncParam>> param
%type <std::vector<std::shared_ptr<toycc::FuncParam>>> param_list opt_param_list
%type <std::shared_ptr<toycc::Block>> compound_stmt
%type <std::vector<std::shared_ptr<toycc::Stmt>>> stmt_list
%type <std::shared_ptr<toycc::Stmt>> stmt decl_stmt expr_stmt if_stmt while_stmt return_stmt break_stmt continue_stmt
%type <std::shared_ptr<toycc::Expr>> expr opt_expr assign_expr logical_or_expr logical_and_expr equality_expr relational_expr additive_expr multiplicative_expr unary_expr primary_expr
%type <std::vector<std::shared_ptr<toycc::Expr>>> arg_list opt_arg_list
%type <std::shared_ptr<toycc::Expr>> opt_init

%start compilation_unit

%%

compilation_unit
  : global_decl_list {
      auto unit = std::make_shared<toycc::CompUnit>();
      unit->globalDecls = std::move($1);
      driver.setLocation(unit.get());
      driver.setAST(std::move(unit));
    }
  ;

global_decl_list
  : %empty { $$ = {}; }
  | global_decl_list global_decl {
      $1.push_back(std::move($2));
      $$ = std::move($1);
    }
  ;

global_decl
  : func_def { $$ = std::move($1); }
  | decl { $$ = std::move($1); }
  ;

func_def
  : decl_specifier IDENT LPAREN opt_param_list RPAREN compound_stmt {
      auto func = std::make_shared<toycc::FuncDef>();
      func->retType = $1.varType;
      func->name = std::move($2);
      func->params = std::move($4);
      func->body = std::move($6);
      driver.setLocation(func.get());
      $$ = std::move(func);
    }
  ;

opt_param_list
  : %empty { $$ = {}; }
  | param_list { $$ = std::move($1); }
  ;

param_list
  : param { $$ = {}; $$.push_back(std::move($1)); }
  | param_list COMMA param { $1.push_back(std::move($3)); $$ = std::move($1); }
  ;

param
  : decl_specifier IDENT {
      auto param = std::make_shared<toycc::FuncParam>();
      param->type = $1.varType;
      param->name = std::move($2);
      driver.setLocation(param.get());
      $$ = std::move(param);
    }
  ;

decl_specifier
  : INT { $$ = {.isConst = false, .varType = toycc::Type::INT}; }
  | VOID { $$ = {.isConst = false, .varType = toycc::Type::VOID}; }
  | CONST INT { $$ = {.isConst = true, .varType = toycc::Type::INT}; }
  | CONST VOID { $$ = {.isConst = true, .varType = toycc::Type::VOID}; }
  ;

decl
  : decl_specifier IDENT opt_init SEMI {
      auto decl = std::make_shared<toycc::Decl>();
      decl->isConst = $1.isConst;
      decl->varType = $1.varType;
      decl->name = std::move($2);
      decl->initExpr = std::move($3);
      driver.setLocation(decl.get());
      $$ = std::move(decl);
    }
  ;

opt_init
  : %empty { $$ = nullptr; }
  | ASSIGN expr { $$ = std::move($2); }
  ;

compound_stmt
  : LBRACE stmt_list RBRACE {
      auto block = std::make_shared<toycc::Block>();
      block->stmts = std::move($2);
      driver.setLocation(block.get());
      $$ = std::move(block);
    }
  ;

stmt_list
  : %empty { $$ = {}; }
  | stmt_list stmt { $1.push_back(std::move($2)); $$ = std::move($1); }
  ;

stmt
  : compound_stmt { $$ = std::move($1); }
  | expr_stmt { $$ = std::move($1); }
  | decl_stmt { $$ = std::move($1); }
  | if_stmt { $$ = std::move($1); }
  | while_stmt { $$ = std::move($1); }
  | return_stmt { $$ = std::move($1); }
  | break_stmt { $$ = std::move($1); }
  | continue_stmt { $$ = std::move($1); }
  ;

decl_stmt
  : decl {
      auto stmt = std::make_shared<toycc::DeclStmt>();
      stmt->decl = std::move($1);
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

expr_stmt
  : opt_expr SEMI {
      auto stmt = std::make_shared<toycc::ExprStmt>();
      stmt->expr = std::move($1);
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

if_stmt
  : IF LPAREN expr RPAREN stmt {
      auto stmt = std::make_shared<toycc::IfStmt>();
      stmt->condition = std::move($3);
      stmt->thenBranch = std::move($5);
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  | IF LPAREN expr RPAREN stmt ELSE stmt {
      auto stmt = std::make_shared<toycc::IfStmt>();
      stmt->condition = std::move($3);
      stmt->thenBranch = std::move($5);
      stmt->elseBranch = std::move($7);
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

while_stmt
  : WHILE LPAREN expr RPAREN stmt {
      auto stmt = std::make_shared<toycc::WhileStmt>();
      stmt->condition = std::move($3);
      stmt->body = std::move($5);
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

return_stmt
  : RETURN opt_expr SEMI {
      auto stmt = std::make_shared<toycc::ReturnStmt>();
      stmt->value = std::move($2);
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

break_stmt
  : BREAK SEMI {
      auto stmt = std::make_shared<toycc::BreakStmt>();
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

continue_stmt
  : CONTINUE SEMI {
      auto stmt = std::make_shared<toycc::ContinueStmt>();
      driver.setLocation(stmt.get());
      $$ = std::move(stmt);
    }
  ;

opt_expr
  : %empty { $$ = nullptr; }
  | expr { $$ = std::move($1); }
  ;

expr
  : assign_expr { $$ = std::move($1); }
  ;

assign_expr
  : logical_or_expr { $$ = std::move($1); }
  | unary_expr ASSIGN assign_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::ASSIGN, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

logical_or_expr
  : logical_and_expr { $$ = std::move($1); }
  | logical_or_expr OR logical_and_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::OR, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

logical_and_expr
  : equality_expr { $$ = std::move($1); }
  | logical_and_expr AND equality_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::AND, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

equality_expr
  : relational_expr { $$ = std::move($1); }
  | equality_expr EQ relational_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::EQ, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | equality_expr NE relational_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::NE, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

relational_expr
  : additive_expr { $$ = std::move($1); }
  | relational_expr LT additive_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::LT, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | relational_expr GT additive_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::GT, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | relational_expr LE additive_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::LE, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | relational_expr GE additive_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::GE, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

additive_expr
  : multiplicative_expr { $$ = std::move($1); }
  | additive_expr ADD multiplicative_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::ADD, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | additive_expr SUB multiplicative_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::SUB, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

multiplicative_expr
  : unary_expr { $$ = std::move($1); }
  | multiplicative_expr MUL unary_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::MUL, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | multiplicative_expr DIV unary_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::DIV, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | multiplicative_expr MOD unary_expr {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::MOD, std::move($1), std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

unary_expr
  : primary_expr { $$ = std::move($1); }
  | ADD unary_expr {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::POS, std::move($2));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | SUB unary_expr {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::NEG, std::move($2));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | NOT unary_expr {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::NOT, std::move($2));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  ;

primary_expr
  : INT_LITERAL {
      auto expr = std::make_shared<toycc::IntLiteral>($1);
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | IDENT {
      auto expr = std::make_shared<toycc::VarExpr>($1);
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | IDENT LPAREN opt_arg_list RPAREN {
      auto expr = std::make_shared<toycc::CallExpr>($1, std::move($3));
      driver.setLocation(expr.get());
      $$ = std::move(expr);
    }
  | LPAREN expr RPAREN { $$ = std::move($2); }
  ;

opt_arg_list
  : %empty { $$ = {}; }
  | arg_list { $$ = std::move($1); }
  ;

arg_list
  : expr { $$ = {}; $$.push_back(std::move($1)); }
  | arg_list COMMA expr { $1.push_back(std::move($3)); $$ = std::move($1); }
  ;

%%

void yy::parser::error(const std::string& msg) {
  driver.setError(msg);
}
