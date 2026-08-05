#include "parser_driver.h"

#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

struct yy_buffer_state;
using YY_BUFFER_STATE = yy_buffer_state*;

int toycclex(void);
void toyccrestart(YY_BUFFER_STATE);
void toycc_delete_buffer(YY_BUFFER_STATE);
YY_BUFFER_STATE toycc_scan_string(const char*);
extern char* toycctext;

namespace toycc {

ParserDriver::ParserDriver() = default;
ParserDriver::~ParserDriver() = default;

bool ParserDriver::parse(const std::string& inputText) {
  input = inputText;
  line = 1;
  col = 1;
  tokenLine = 1;
  tokenCol = 1;
  errorMessage.clear();
  ast.reset();
  parsing = true;
  scanInitialized = false;

  if (scanBuffer != nullptr) {
    toycc_delete_buffer(reinterpret_cast<YY_BUFFER_STATE>(scanBuffer));
    scanBuffer = nullptr;
  }
  scanBuffer = reinterpret_cast<void*>(toycc_scan_string(input.c_str()));

  yy::parser parser(*this);
  const int status = parser.parse();
  if (status != 0 || hadError()) {
    return false;
  }
  return true;
}

bool ParserDriver::parseStream(std::istream& inputStream) {
  std::ostringstream oss;
  oss << inputStream.rdbuf();
  return parse(oss.str());
}

void ParserDriver::setError(const std::string& msg) {
  if (!errorMessage.empty()) {
    return;
  }
  errorMessage = msg;
}

void ParserDriver::reset() {
  ast.reset();
  errorMessage.clear();
  input.clear();
  line = 1;
  col = 1;
  tokenLine = 1;
  tokenCol = 1;
  parsing = false;
  scanInitialized = false;
}

void ParserDriver::setLocation(ASTNode* node) const {
  if (!node) {
    return;
  }
  node->line = tokenLine;
  node->col = tokenCol;
}

void ParserDriver::advanceLine() {
  line += 1;
  col = 1;
}

void ParserDriver::advanceColumn(int delta) {
  col += delta;
}

void ParserDriver::consumeToken(const std::string& text) {
  (void) text;
}

void ParserDriver::setTokenStart(int lineNo, int colNo) {
  tokenLine = lineNo;
  tokenCol = colNo;
}

yy::parser::symbol_type ParserDriver::lex() {
  const int token = toycclex();
  if (token == 0) {
    return yy::parser::make_YYEOF();
  }

  tokenText = toycctext ? std::string(toycctext) : std::string();

  switch (token) {
  case yy::parser::token::IDENT:
    return yy::parser::make_IDENT(tokenText);
  case yy::parser::token::INT_LITERAL:
    return yy::parser::make_INT_LITERAL(std::stoi(tokenText));
  case yy::parser::token::CONST:
    return yy::parser::make_CONST();
  case yy::parser::token::VOID:
    return yy::parser::make_VOID();
  case yy::parser::token::INT:
    return yy::parser::make_INT();
  case yy::parser::token::RETURN:
    return yy::parser::make_RETURN();
  case yy::parser::token::IF:
    return yy::parser::make_IF();
  case yy::parser::token::ELSE:
    return yy::parser::make_ELSE();
  case yy::parser::token::WHILE:
    return yy::parser::make_WHILE();
  case yy::parser::token::BREAK:
    return yy::parser::make_BREAK();
  case yy::parser::token::CONTINUE:
    return yy::parser::make_CONTINUE();
  case yy::parser::token::ASSIGN:
    return yy::parser::make_ASSIGN();
  case yy::parser::token::SEMI:
    return yy::parser::make_SEMI();
  case yy::parser::token::COMMA:
    return yy::parser::make_COMMA();
  case yy::parser::token::LPAREN:
    return yy::parser::make_LPAREN();
  case yy::parser::token::RPAREN:
    return yy::parser::make_RPAREN();
  case yy::parser::token::LBRACE:
    return yy::parser::make_LBRACE();
  case yy::parser::token::RBRACE:
    return yy::parser::make_RBRACE();
  case yy::parser::token::EQ:
    return yy::parser::make_EQ();
  case yy::parser::token::NE:
    return yy::parser::make_NE();
  case yy::parser::token::LE:
    return yy::parser::make_LE();
  case yy::parser::token::GE:
    return yy::parser::make_GE();
  case yy::parser::token::LT:
    return yy::parser::make_LT();
  case yy::parser::token::GT:
    return yy::parser::make_GT();
  case yy::parser::token::ADD:
    return yy::parser::make_ADD();
  case yy::parser::token::SUB:
    return yy::parser::make_SUB();
  case yy::parser::token::MUL:
    return yy::parser::make_MUL();
  case yy::parser::token::DIV:
    return yy::parser::make_DIV();
  case yy::parser::token::MOD:
    return yy::parser::make_MOD();
  case yy::parser::token::AND:
    return yy::parser::make_AND();
  case yy::parser::token::OR:
    return yy::parser::make_OR();
  case yy::parser::token::NOT:
    return yy::parser::make_NOT();
  default:
    return yy::parser::make_INVALID();
  }
}

} // namespace toycc

namespace yy {
parser::symbol_type yylex(toycc::ParserDriver& driver) {
  return driver.lex();
}
} // namespace yy
