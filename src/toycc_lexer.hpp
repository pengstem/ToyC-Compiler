#ifndef TOYCC_LEXER_HPP
#define TOYCC_LEXER_HPP

#include <string>

namespace toycc {
class ToyCCFlexLexer {
public:
  virtual ~ToyCCFlexLexer() = default;
  virtual int yylex() = 0;
  virtual std::string YYText() const = 0;
};
} // namespace toycc

#endif // TOYCC_LEXER_HPP
