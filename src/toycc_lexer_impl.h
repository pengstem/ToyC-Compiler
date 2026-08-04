#ifndef TOYCC_LEXER_IMPL_H
#define TOYCC_LEXER_IMPL_H

#include <string>

namespace toycc {

class ToyCCFlexLexer {
public:
  virtual ~ToyCCFlexLexer() = default;
  virtual int yylex() = 0;
  virtual std::string YYText() const = 0;
  virtual void setInput(const std::string& input) = 0;
};

} // namespace toycc

#endif // TOYCC_LEXER_IMPL_H
