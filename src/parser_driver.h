#ifndef TOYCC_PARSER_DRIVER_H
#define TOYCC_PARSER_DRIVER_H

#include "ast_nodes.h"
#include "toycc_parser.hpp"

#include <iosfwd>
#include <memory>
#include <string>
#include <utility>

namespace toycc {

class ParserDriver {
public:
  ParserDriver();
  ~ParserDriver();

  bool parse(const std::string& input);
  bool parseStream(std::istream& input);

  std::shared_ptr<CompUnit> getAST() const {
    return ast;
  }

  void setAST(std::shared_ptr<CompUnit> unit) {
    ast = std::move(unit);
  }

  void setError(const std::string& msg);
  const std::string& getError() const {
    return errorMessage;
  }

  bool hadError() const {
    return !errorMessage.empty();
  }

  void setLocation(ASTNode* node) const;
  void reset();

  yy::parser::symbol_type lex();

  void advanceLine();
  void advanceColumn(int delta = 1);
  void consumeToken(const std::string& text);

  int currentLine() const {
    return line;
  }
  int currentColumn() const {
    return col;
  }

  void setTokenStart(int lineNo, int colNo);
  int tokenStartLine() const {
    return tokenLine;
  }
  int tokenStartColumn() const {
    return tokenCol;
  }

private:
  std::string errorMessage;
  std::shared_ptr<CompUnit> ast;
  std::string input;
  std::string tokenText;
  void* scanBuffer = nullptr;
  int line = 1;
  int col = 1;
  int tokenLine = 1;
  int tokenCol = 1;
  bool parsing = false;
  bool scanInitialized = false;
};

} // namespace toycc

#endif // TOYCC_PARSER_DRIVER_H
