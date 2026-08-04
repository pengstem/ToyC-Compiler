// A Bison parser, made by GNU Bison 3.8.2.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2015, 2018-2021 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.

// DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
// especially those whose name start with YY_ or yy_.  They are
// private implementation details that can be changed or removed.





#include "toycc_parser.hpp"


// Unqualified %code blocks.
#line 35 "src/toycc_parser.yy"

#include <iostream>
#include <stdexcept>
#include <string>

#include "parser_driver.h"

namespace yy {
parser::symbol_type yylex(toycc::ParserDriver& driver);
}

#line 58 "src/toycc_parser.cpp"


#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif


// Whether we are compiled with exception support.
#ifndef YY_EXCEPTIONS
# if defined __GNUC__ && !defined __EXCEPTIONS
#  define YY_EXCEPTIONS 0
# else
#  define YY_EXCEPTIONS 1
# endif
#endif



// Enable debugging if requested.
#if YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << '\n';                       \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yy_stack_print_ ();                \
  } while (false)

#else // !YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YY_USE (Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void> (0)
# define YY_STACK_PRINT()                static_cast<void> (0)

#endif // !YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

namespace yy {
#line 131 "src/toycc_parser.cpp"

  /// Build a parser object.
  parser::parser (toycc::ParserDriver& driver_yyarg)
#if YYDEBUG
    : yydebug_ (false),
      yycdebug_ (&std::cerr),
#else
    :
#endif
      driver (driver_yyarg)
  {}

  parser::~parser ()
  {}

  parser::syntax_error::~syntax_error () YY_NOEXCEPT YY_NOTHROW
  {}

  /*---------.
  | symbol.  |
  `---------*/



  // by_state.
  parser::by_state::by_state () YY_NOEXCEPT
    : state (empty_state)
  {}

  parser::by_state::by_state (const by_state& that) YY_NOEXCEPT
    : state (that.state)
  {}

  void
  parser::by_state::clear () YY_NOEXCEPT
  {
    state = empty_state;
  }

  void
  parser::by_state::move (by_state& that)
  {
    state = that.state;
    that.clear ();
  }

  parser::by_state::by_state (state_type s) YY_NOEXCEPT
    : state (s)
  {}

  parser::symbol_kind_type
  parser::by_state::kind () const YY_NOEXCEPT
  {
    if (state == empty_state)
      return symbol_kind::S_YYEMPTY;
    else
      return YY_CAST (symbol_kind_type, yystos_[+state]);
  }

  parser::stack_symbol_type::stack_symbol_type ()
  {}

  parser::stack_symbol_type::stack_symbol_type (YY_RVREF (stack_symbol_type) that)
    : super_type (YY_MOVE (that.state))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_INT_LITERAL: // INT_LITERAL
        value.YY_MOVE_OR_COPY< int > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_global_decl: // global_decl
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::ASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_decl: // decl
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::Decl> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_opt_init: // opt_init
      case symbol_kind::S_opt_expr: // opt_expr
      case symbol_kind::S_expr: // expr
      case symbol_kind::S_assign_expr: // assign_expr
      case symbol_kind::S_logical_or_expr: // logical_or_expr
      case symbol_kind::S_logical_and_expr: // logical_and_expr
      case symbol_kind::S_equality_expr: // equality_expr
      case symbol_kind::S_relational_expr: // relational_expr
      case symbol_kind::S_additive_expr: // additive_expr
      case symbol_kind::S_multiplicative_expr: // multiplicative_expr
      case symbol_kind::S_unary_expr: // unary_expr
      case symbol_kind::S_primary_expr: // primary_expr
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::Expr> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_func_def: // func_def
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::FuncDef> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_param: // param
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::FuncParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_stmt: // stmt
      case symbol_kind::S_decl_stmt: // decl_stmt
      case symbol_kind::S_expr_stmt: // expr_stmt
      case symbol_kind::S_if_stmt: // if_stmt
      case symbol_kind::S_while_stmt: // while_stmt
      case symbol_kind::S_return_stmt: // return_stmt
      case symbol_kind::S_break_stmt: // break_stmt
      case symbol_kind::S_continue_stmt: // continue_stmt
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::Stmt> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_IDENT: // IDENT
        value.YY_MOVE_OR_COPY< std::string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_global_decl_list: // global_decl_list
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<toycc::ASTNode>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_opt_arg_list: // opt_arg_list
      case symbol_kind::S_arg_list: // arg_list
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<toycc::Expr>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_opt_param_list: // opt_param_list
      case symbol_kind::S_param_list: // param_list
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<toycc::FuncParam>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_stmt_list: // stmt_list
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<toycc::Stmt>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_decl_specifier: // decl_specifier
        value.YY_MOVE_OR_COPY< toycc::DeclSpec > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

#if 201103L <= YY_CPLUSPLUS
    // that is emptied.
    that.state = empty_state;
#endif
  }

  parser::stack_symbol_type::stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) that)
    : super_type (s)
  {
    switch (that.kind ())
    {
      case symbol_kind::S_INT_LITERAL: // INT_LITERAL
        value.move< int > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_global_decl: // global_decl
        value.move< std::shared_ptr<toycc::ASTNode> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.move< std::shared_ptr<toycc::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_decl: // decl
        value.move< std::shared_ptr<toycc::Decl> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_opt_init: // opt_init
      case symbol_kind::S_opt_expr: // opt_expr
      case symbol_kind::S_expr: // expr
      case symbol_kind::S_assign_expr: // assign_expr
      case symbol_kind::S_logical_or_expr: // logical_or_expr
      case symbol_kind::S_logical_and_expr: // logical_and_expr
      case symbol_kind::S_equality_expr: // equality_expr
      case symbol_kind::S_relational_expr: // relational_expr
      case symbol_kind::S_additive_expr: // additive_expr
      case symbol_kind::S_multiplicative_expr: // multiplicative_expr
      case symbol_kind::S_unary_expr: // unary_expr
      case symbol_kind::S_primary_expr: // primary_expr
        value.move< std::shared_ptr<toycc::Expr> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_func_def: // func_def
        value.move< std::shared_ptr<toycc::FuncDef> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_param: // param
        value.move< std::shared_ptr<toycc::FuncParam> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_stmt: // stmt
      case symbol_kind::S_decl_stmt: // decl_stmt
      case symbol_kind::S_expr_stmt: // expr_stmt
      case symbol_kind::S_if_stmt: // if_stmt
      case symbol_kind::S_while_stmt: // while_stmt
      case symbol_kind::S_return_stmt: // return_stmt
      case symbol_kind::S_break_stmt: // break_stmt
      case symbol_kind::S_continue_stmt: // continue_stmt
        value.move< std::shared_ptr<toycc::Stmt> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_IDENT: // IDENT
        value.move< std::string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_global_decl_list: // global_decl_list
        value.move< std::vector<std::shared_ptr<toycc::ASTNode>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_opt_arg_list: // opt_arg_list
      case symbol_kind::S_arg_list: // arg_list
        value.move< std::vector<std::shared_ptr<toycc::Expr>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_opt_param_list: // opt_param_list
      case symbol_kind::S_param_list: // param_list
        value.move< std::vector<std::shared_ptr<toycc::FuncParam>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_stmt_list: // stmt_list
        value.move< std::vector<std::shared_ptr<toycc::Stmt>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_decl_specifier: // decl_specifier
        value.move< toycc::DeclSpec > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

    // that is emptied.
    that.kind_ = symbol_kind::S_YYEMPTY;
  }

#if YY_CPLUSPLUS < 201103L
  parser::stack_symbol_type&
  parser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_INT_LITERAL: // INT_LITERAL
        value.copy< int > (that.value);
        break;

      case symbol_kind::S_global_decl: // global_decl
        value.copy< std::shared_ptr<toycc::ASTNode> > (that.value);
        break;

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.copy< std::shared_ptr<toycc::Block> > (that.value);
        break;

      case symbol_kind::S_decl: // decl
        value.copy< std::shared_ptr<toycc::Decl> > (that.value);
        break;

      case symbol_kind::S_opt_init: // opt_init
      case symbol_kind::S_opt_expr: // opt_expr
      case symbol_kind::S_expr: // expr
      case symbol_kind::S_assign_expr: // assign_expr
      case symbol_kind::S_logical_or_expr: // logical_or_expr
      case symbol_kind::S_logical_and_expr: // logical_and_expr
      case symbol_kind::S_equality_expr: // equality_expr
      case symbol_kind::S_relational_expr: // relational_expr
      case symbol_kind::S_additive_expr: // additive_expr
      case symbol_kind::S_multiplicative_expr: // multiplicative_expr
      case symbol_kind::S_unary_expr: // unary_expr
      case symbol_kind::S_primary_expr: // primary_expr
        value.copy< std::shared_ptr<toycc::Expr> > (that.value);
        break;

      case symbol_kind::S_func_def: // func_def
        value.copy< std::shared_ptr<toycc::FuncDef> > (that.value);
        break;

      case symbol_kind::S_param: // param
        value.copy< std::shared_ptr<toycc::FuncParam> > (that.value);
        break;

      case symbol_kind::S_stmt: // stmt
      case symbol_kind::S_decl_stmt: // decl_stmt
      case symbol_kind::S_expr_stmt: // expr_stmt
      case symbol_kind::S_if_stmt: // if_stmt
      case symbol_kind::S_while_stmt: // while_stmt
      case symbol_kind::S_return_stmt: // return_stmt
      case symbol_kind::S_break_stmt: // break_stmt
      case symbol_kind::S_continue_stmt: // continue_stmt
        value.copy< std::shared_ptr<toycc::Stmt> > (that.value);
        break;

      case symbol_kind::S_IDENT: // IDENT
        value.copy< std::string > (that.value);
        break;

      case symbol_kind::S_global_decl_list: // global_decl_list
        value.copy< std::vector<std::shared_ptr<toycc::ASTNode>> > (that.value);
        break;

      case symbol_kind::S_opt_arg_list: // opt_arg_list
      case symbol_kind::S_arg_list: // arg_list
        value.copy< std::vector<std::shared_ptr<toycc::Expr>> > (that.value);
        break;

      case symbol_kind::S_opt_param_list: // opt_param_list
      case symbol_kind::S_param_list: // param_list
        value.copy< std::vector<std::shared_ptr<toycc::FuncParam>> > (that.value);
        break;

      case symbol_kind::S_stmt_list: // stmt_list
        value.copy< std::vector<std::shared_ptr<toycc::Stmt>> > (that.value);
        break;

      case symbol_kind::S_decl_specifier: // decl_specifier
        value.copy< toycc::DeclSpec > (that.value);
        break;

      default:
        break;
    }

    return *this;
  }

  parser::stack_symbol_type&
  parser::stack_symbol_type::operator= (stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_INT_LITERAL: // INT_LITERAL
        value.move< int > (that.value);
        break;

      case symbol_kind::S_global_decl: // global_decl
        value.move< std::shared_ptr<toycc::ASTNode> > (that.value);
        break;

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.move< std::shared_ptr<toycc::Block> > (that.value);
        break;

      case symbol_kind::S_decl: // decl
        value.move< std::shared_ptr<toycc::Decl> > (that.value);
        break;

      case symbol_kind::S_opt_init: // opt_init
      case symbol_kind::S_opt_expr: // opt_expr
      case symbol_kind::S_expr: // expr
      case symbol_kind::S_assign_expr: // assign_expr
      case symbol_kind::S_logical_or_expr: // logical_or_expr
      case symbol_kind::S_logical_and_expr: // logical_and_expr
      case symbol_kind::S_equality_expr: // equality_expr
      case symbol_kind::S_relational_expr: // relational_expr
      case symbol_kind::S_additive_expr: // additive_expr
      case symbol_kind::S_multiplicative_expr: // multiplicative_expr
      case symbol_kind::S_unary_expr: // unary_expr
      case symbol_kind::S_primary_expr: // primary_expr
        value.move< std::shared_ptr<toycc::Expr> > (that.value);
        break;

      case symbol_kind::S_func_def: // func_def
        value.move< std::shared_ptr<toycc::FuncDef> > (that.value);
        break;

      case symbol_kind::S_param: // param
        value.move< std::shared_ptr<toycc::FuncParam> > (that.value);
        break;

      case symbol_kind::S_stmt: // stmt
      case symbol_kind::S_decl_stmt: // decl_stmt
      case symbol_kind::S_expr_stmt: // expr_stmt
      case symbol_kind::S_if_stmt: // if_stmt
      case symbol_kind::S_while_stmt: // while_stmt
      case symbol_kind::S_return_stmt: // return_stmt
      case symbol_kind::S_break_stmt: // break_stmt
      case symbol_kind::S_continue_stmt: // continue_stmt
        value.move< std::shared_ptr<toycc::Stmt> > (that.value);
        break;

      case symbol_kind::S_IDENT: // IDENT
        value.move< std::string > (that.value);
        break;

      case symbol_kind::S_global_decl_list: // global_decl_list
        value.move< std::vector<std::shared_ptr<toycc::ASTNode>> > (that.value);
        break;

      case symbol_kind::S_opt_arg_list: // opt_arg_list
      case symbol_kind::S_arg_list: // arg_list
        value.move< std::vector<std::shared_ptr<toycc::Expr>> > (that.value);
        break;

      case symbol_kind::S_opt_param_list: // opt_param_list
      case symbol_kind::S_param_list: // param_list
        value.move< std::vector<std::shared_ptr<toycc::FuncParam>> > (that.value);
        break;

      case symbol_kind::S_stmt_list: // stmt_list
        value.move< std::vector<std::shared_ptr<toycc::Stmt>> > (that.value);
        break;

      case symbol_kind::S_decl_specifier: // decl_specifier
        value.move< toycc::DeclSpec > (that.value);
        break;

      default:
        break;
    }

    // that is emptied.
    that.state = empty_state;
    return *this;
  }
#endif

  template <typename Base>
  void
  parser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);
  }

#if YYDEBUG
  template <typename Base>
  void
  parser::yy_print_ (std::ostream& yyo, const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YY_USE (yyoutput);
    if (yysym.empty ())
      yyo << "empty symbol";
    else
      {
        symbol_kind_type yykind = yysym.kind ();
        yyo << (yykind < YYNTOKENS ? "token" : "nterm")
            << ' ' << yysym.name () << " (";
        YY_USE (yykind);
        yyo << ')';
      }
  }
#endif

  void
  parser::yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym)
  {
    if (m)
      YY_SYMBOL_PRINT (m, sym);
    yystack_.push (YY_MOVE (sym));
  }

  void
  parser::yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym)
  {
#if 201103L <= YY_CPLUSPLUS
    yypush_ (m, stack_symbol_type (s, std::move (sym)));
#else
    stack_symbol_type ss (s, sym);
    yypush_ (m, ss);
#endif
  }

  void
  parser::yypop_ (int n) YY_NOEXCEPT
  {
    yystack_.pop (n);
  }

#if YYDEBUG
  std::ostream&
  parser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  parser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  parser::debug_level_type
  parser::debug_level () const
  {
    return yydebug_;
  }

  void
  parser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YYDEBUG

  parser::state_type
  parser::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - YYNTOKENS] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - YYNTOKENS];
  }

  bool
  parser::yy_pact_value_is_default_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yypact_ninf_;
  }

  bool
  parser::yy_table_value_is_error_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yytable_ninf_;
  }

  int
  parser::operator() ()
  {
    return parse ();
  }

  int
  parser::parse ()
  {
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The return value of parse ().
    int yyresult;

#if YY_EXCEPTIONS
    try
#endif // YY_EXCEPTIONS
      {
    YYCDEBUG << "Starting parse\n";


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, YY_MOVE (yyla));

  /*-----------------------------------------------.
  | yynewstate -- push a new symbol on the stack.  |
  `-----------------------------------------------*/
  yynewstate:
    YYCDEBUG << "Entering state " << int (yystack_[0].state) << '\n';
    YY_STACK_PRINT ();

    // Accept?
    if (yystack_[0].state == yyfinal_)
      YYACCEPT;

    goto yybackup;


  /*-----------.
  | yybackup.  |
  `-----------*/
  yybackup:
    // Try to take a decision without lookahead.
    yyn = yypact_[+yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyla.empty ())
      {
        YYCDEBUG << "Reading a token\n";
#if YY_EXCEPTIONS
        try
#endif // YY_EXCEPTIONS
          {
            symbol_type yylookahead (yylex (driver));
            yyla.move (yylookahead);
          }
#if YY_EXCEPTIONS
        catch (const syntax_error& yyexc)
          {
            YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
            error (yyexc);
            goto yyerrlab1;
          }
#endif // YY_EXCEPTIONS
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    if (yyla.kind () == symbol_kind::S_YYerror)
    {
      // The scanner already issued an error message, process directly
      // to error recovery.  But do not keep the error token as
      // lookahead, it is too special and may lead us to an endless
      // loop in error recovery. */
      yyla.kind_ = symbol_kind::S_YYUNDEF;
      goto yyerrlab1;
    }

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.kind ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.kind ())
      {
        goto yydefault;
      }

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", state_type (yyn), YY_MOVE (yyla));
    goto yynewstate;


  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[+yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;


  /*-----------------------------.
  | yyreduce -- do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_ (yystack_[yylen].state, yyr1_[yyn]);
      /* Variants are always initialized to an empty instance of the
         correct type. The default '$$ = $1' action is NOT applied
         when using variants.  */
      switch (yyr1_[yyn])
    {
      case symbol_kind::S_INT_LITERAL: // INT_LITERAL
        yylhs.value.emplace< int > ();
        break;

      case symbol_kind::S_global_decl: // global_decl
        yylhs.value.emplace< std::shared_ptr<toycc::ASTNode> > ();
        break;

      case symbol_kind::S_compound_stmt: // compound_stmt
        yylhs.value.emplace< std::shared_ptr<toycc::Block> > ();
        break;

      case symbol_kind::S_decl: // decl
        yylhs.value.emplace< std::shared_ptr<toycc::Decl> > ();
        break;

      case symbol_kind::S_opt_init: // opt_init
      case symbol_kind::S_opt_expr: // opt_expr
      case symbol_kind::S_expr: // expr
      case symbol_kind::S_assign_expr: // assign_expr
      case symbol_kind::S_logical_or_expr: // logical_or_expr
      case symbol_kind::S_logical_and_expr: // logical_and_expr
      case symbol_kind::S_equality_expr: // equality_expr
      case symbol_kind::S_relational_expr: // relational_expr
      case symbol_kind::S_additive_expr: // additive_expr
      case symbol_kind::S_multiplicative_expr: // multiplicative_expr
      case symbol_kind::S_unary_expr: // unary_expr
      case symbol_kind::S_primary_expr: // primary_expr
        yylhs.value.emplace< std::shared_ptr<toycc::Expr> > ();
        break;

      case symbol_kind::S_func_def: // func_def
        yylhs.value.emplace< std::shared_ptr<toycc::FuncDef> > ();
        break;

      case symbol_kind::S_param: // param
        yylhs.value.emplace< std::shared_ptr<toycc::FuncParam> > ();
        break;

      case symbol_kind::S_stmt: // stmt
      case symbol_kind::S_decl_stmt: // decl_stmt
      case symbol_kind::S_expr_stmt: // expr_stmt
      case symbol_kind::S_if_stmt: // if_stmt
      case symbol_kind::S_while_stmt: // while_stmt
      case symbol_kind::S_return_stmt: // return_stmt
      case symbol_kind::S_break_stmt: // break_stmt
      case symbol_kind::S_continue_stmt: // continue_stmt
        yylhs.value.emplace< std::shared_ptr<toycc::Stmt> > ();
        break;

      case symbol_kind::S_IDENT: // IDENT
        yylhs.value.emplace< std::string > ();
        break;

      case symbol_kind::S_global_decl_list: // global_decl_list
        yylhs.value.emplace< std::vector<std::shared_ptr<toycc::ASTNode>> > ();
        break;

      case symbol_kind::S_opt_arg_list: // opt_arg_list
      case symbol_kind::S_arg_list: // arg_list
        yylhs.value.emplace< std::vector<std::shared_ptr<toycc::Expr>> > ();
        break;

      case symbol_kind::S_opt_param_list: // opt_param_list
      case symbol_kind::S_param_list: // param_list
        yylhs.value.emplace< std::vector<std::shared_ptr<toycc::FuncParam>> > ();
        break;

      case symbol_kind::S_stmt_list: // stmt_list
        yylhs.value.emplace< std::vector<std::shared_ptr<toycc::Stmt>> > ();
        break;

      case symbol_kind::S_decl_specifier: // decl_specifier
        yylhs.value.emplace< toycc::DeclSpec > ();
        break;

      default:
        break;
    }



      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
#if YY_EXCEPTIONS
      try
#endif // YY_EXCEPTIONS
        {
          switch (yyn)
            {
  case 2: // compilation_unit: global_decl_list
#line 74 "src/toycc_parser.yy"
                     {
      auto unit = std::make_shared<toycc::CompUnit>();
      unit->globalDecls = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ());
      driver.setLocation(unit.get());
      driver.setAST(std::move(unit));
    }
#line 898 "src/toycc_parser.cpp"
    break;

  case 3: // global_decl_list: %empty
#line 83 "src/toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > () = {}; }
#line 904 "src/toycc_parser.cpp"
    break;

  case 4: // global_decl_list: global_decl_list global_decl
#line 84 "src/toycc_parser.yy"
                                 {
      yystack_[1].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::ASTNode> > ()));
      yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > () = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ());
    }
#line 913 "src/toycc_parser.cpp"
    break;

  case 5: // global_decl: func_def
#line 91 "src/toycc_parser.yy"
             { yylhs.value.as < std::shared_ptr<toycc::ASTNode> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::FuncDef> > ()); }
#line 919 "src/toycc_parser.cpp"
    break;

  case 6: // global_decl: decl
#line 92 "src/toycc_parser.yy"
         { yylhs.value.as < std::shared_ptr<toycc::ASTNode> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Decl> > ()); }
#line 925 "src/toycc_parser.cpp"
    break;

  case 7: // func_def: decl_specifier IDENT LPAREN opt_param_list RPAREN compound_stmt
#line 96 "src/toycc_parser.yy"
                                                                    {
      auto func = std::make_shared<toycc::FuncDef>();
      func->retType = yystack_[5].value.as < toycc::DeclSpec > ().varType;
      func->name = std::move(yystack_[4].value.as < std::string > ());
      func->params = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ());
      func->body = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Block> > ());
      driver.setLocation(func.get());
      yylhs.value.as < std::shared_ptr<toycc::FuncDef> > () = std::move(func);
    }
#line 939 "src/toycc_parser.cpp"
    break;

  case 8: // opt_param_list: %empty
#line 108 "src/toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = {}; }
#line 945 "src/toycc_parser.cpp"
    break;

  case 9: // opt_param_list: param_list
#line 109 "src/toycc_parser.yy"
               { yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ()); }
#line 951 "src/toycc_parser.cpp"
    break;

  case 10: // param_list: param
#line 113 "src/toycc_parser.yy"
          { yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = {}; yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::FuncParam> > ())); }
#line 957 "src/toycc_parser.cpp"
    break;

  case 11: // param_list: param_list COMMA param
#line 114 "src/toycc_parser.yy"
                           { yystack_[2].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::FuncParam> > ())); yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ()); }
#line 963 "src/toycc_parser.cpp"
    break;

  case 12: // param: decl_specifier IDENT
#line 118 "src/toycc_parser.yy"
                         {
      auto param = std::make_shared<toycc::FuncParam>();
      param->type = yystack_[1].value.as < toycc::DeclSpec > ().varType;
      param->name = std::move(yystack_[0].value.as < std::string > ());
      driver.setLocation(param.get());
      yylhs.value.as < std::shared_ptr<toycc::FuncParam> > () = std::move(param);
    }
#line 975 "src/toycc_parser.cpp"
    break;

  case 13: // decl_specifier: INT
#line 128 "src/toycc_parser.yy"
        { yylhs.value.as < toycc::DeclSpec > () = {.isConst = false, .varType = toycc::Type::INT}; }
#line 981 "src/toycc_parser.cpp"
    break;

  case 14: // decl_specifier: VOID
#line 129 "src/toycc_parser.yy"
         { yylhs.value.as < toycc::DeclSpec > () = {.isConst = false, .varType = toycc::Type::VOID}; }
#line 987 "src/toycc_parser.cpp"
    break;

  case 15: // decl_specifier: CONST INT
#line 130 "src/toycc_parser.yy"
              { yylhs.value.as < toycc::DeclSpec > () = {.isConst = true, .varType = toycc::Type::INT}; }
#line 993 "src/toycc_parser.cpp"
    break;

  case 16: // decl_specifier: CONST VOID
#line 131 "src/toycc_parser.yy"
               { yylhs.value.as < toycc::DeclSpec > () = {.isConst = true, .varType = toycc::Type::VOID}; }
#line 999 "src/toycc_parser.cpp"
    break;

  case 17: // decl: decl_specifier IDENT opt_init SEMI
#line 135 "src/toycc_parser.yy"
                                       {
      auto decl = std::make_shared<toycc::Decl>();
      decl->isConst = yystack_[3].value.as < toycc::DeclSpec > ().isConst;
      decl->varType = yystack_[3].value.as < toycc::DeclSpec > ().varType;
      decl->name = std::move(yystack_[2].value.as < std::string > ());
      decl->initExpr = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ());
      driver.setLocation(decl.get());
      yylhs.value.as < std::shared_ptr<toycc::Decl> > () = std::move(decl);
    }
#line 1013 "src/toycc_parser.cpp"
    break;

  case 18: // opt_init: %empty
#line 147 "src/toycc_parser.yy"
           { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = nullptr; }
#line 1019 "src/toycc_parser.cpp"
    break;

  case 19: // opt_init: ASSIGN expr
#line 148 "src/toycc_parser.yy"
                { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1025 "src/toycc_parser.cpp"
    break;

  case 20: // compound_stmt: LBRACE stmt_list RBRACE
#line 152 "src/toycc_parser.yy"
                            {
      auto block = std::make_shared<toycc::Block>();
      block->stmts = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Stmt>> > ());
      driver.setLocation(block.get());
      yylhs.value.as < std::shared_ptr<toycc::Block> > () = std::move(block);
    }
#line 1036 "src/toycc_parser.cpp"
    break;

  case 21: // stmt_list: %empty
#line 161 "src/toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::Stmt>> > () = {}; }
#line 1042 "src/toycc_parser.cpp"
    break;

  case 22: // stmt_list: stmt_list stmt
#line 162 "src/toycc_parser.yy"
                   { yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Stmt>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ())); yylhs.value.as < std::vector<std::shared_ptr<toycc::Stmt>> > () = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Stmt>> > ()); }
#line 1048 "src/toycc_parser.cpp"
    break;

  case 23: // stmt: compound_stmt
#line 166 "src/toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Block> > ()); }
#line 1054 "src/toycc_parser.cpp"
    break;

  case 24: // stmt: expr_stmt
#line 167 "src/toycc_parser.yy"
              { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1060 "src/toycc_parser.cpp"
    break;

  case 25: // stmt: decl_stmt
#line 168 "src/toycc_parser.yy"
              { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1066 "src/toycc_parser.cpp"
    break;

  case 26: // stmt: if_stmt
#line 169 "src/toycc_parser.yy"
            { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1072 "src/toycc_parser.cpp"
    break;

  case 27: // stmt: while_stmt
#line 170 "src/toycc_parser.yy"
               { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1078 "src/toycc_parser.cpp"
    break;

  case 28: // stmt: return_stmt
#line 171 "src/toycc_parser.yy"
                { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1084 "src/toycc_parser.cpp"
    break;

  case 29: // stmt: break_stmt
#line 172 "src/toycc_parser.yy"
               { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1090 "src/toycc_parser.cpp"
    break;

  case 30: // stmt: continue_stmt
#line 173 "src/toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1096 "src/toycc_parser.cpp"
    break;

  case 31: // decl_stmt: decl
#line 177 "src/toycc_parser.yy"
         {
      auto stmt = std::make_shared<toycc::DeclStmt>();
      stmt->decl = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Decl> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1107 "src/toycc_parser.cpp"
    break;

  case 32: // expr_stmt: opt_expr SEMI
#line 186 "src/toycc_parser.yy"
                  {
      auto stmt = std::make_shared<toycc::ExprStmt>();
      stmt->expr = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1118 "src/toycc_parser.cpp"
    break;

  case 33: // if_stmt: IF LPAREN expr RPAREN stmt
#line 195 "src/toycc_parser.yy"
                               {
      auto stmt = std::make_shared<toycc::IfStmt>();
      stmt->condition = std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ());
      stmt->thenBranch = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1130 "src/toycc_parser.cpp"
    break;

  case 34: // if_stmt: IF LPAREN expr RPAREN stmt ELSE stmt
#line 202 "src/toycc_parser.yy"
                                         {
      auto stmt = std::make_shared<toycc::IfStmt>();
      stmt->condition = std::move(yystack_[4].value.as < std::shared_ptr<toycc::Expr> > ());
      stmt->thenBranch = std::move(yystack_[2].value.as < std::shared_ptr<toycc::Stmt> > ());
      stmt->elseBranch = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1143 "src/toycc_parser.cpp"
    break;

  case 35: // while_stmt: WHILE LPAREN expr RPAREN stmt
#line 213 "src/toycc_parser.yy"
                                  {
      auto stmt = std::make_shared<toycc::WhileStmt>();
      stmt->condition = std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ());
      stmt->body = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1155 "src/toycc_parser.cpp"
    break;

  case 36: // return_stmt: RETURN opt_expr SEMI
#line 223 "src/toycc_parser.yy"
                         {
      auto stmt = std::make_shared<toycc::ReturnStmt>();
      stmt->value = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1166 "src/toycc_parser.cpp"
    break;

  case 37: // break_stmt: BREAK SEMI
#line 232 "src/toycc_parser.yy"
               {
      auto stmt = std::make_shared<toycc::BreakStmt>();
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1176 "src/toycc_parser.cpp"
    break;

  case 38: // continue_stmt: CONTINUE SEMI
#line 240 "src/toycc_parser.yy"
                  {
      auto stmt = std::make_shared<toycc::ContinueStmt>();
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1186 "src/toycc_parser.cpp"
    break;

  case 39: // opt_expr: %empty
#line 248 "src/toycc_parser.yy"
           { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = nullptr; }
#line 1192 "src/toycc_parser.cpp"
    break;

  case 40: // opt_expr: expr
#line 249 "src/toycc_parser.yy"
         { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1198 "src/toycc_parser.cpp"
    break;

  case 41: // expr: assign_expr
#line 253 "src/toycc_parser.yy"
                { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1204 "src/toycc_parser.cpp"
    break;

  case 42: // assign_expr: logical_or_expr
#line 257 "src/toycc_parser.yy"
                    { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1210 "src/toycc_parser.cpp"
    break;

  case 43: // assign_expr: unary_expr ASSIGN assign_expr
#line 258 "src/toycc_parser.yy"
                                  {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::ASSIGN, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1220 "src/toycc_parser.cpp"
    break;

  case 44: // logical_or_expr: logical_and_expr
#line 266 "src/toycc_parser.yy"
                     { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1226 "src/toycc_parser.cpp"
    break;

  case 45: // logical_or_expr: logical_or_expr OR logical_and_expr
#line 267 "src/toycc_parser.yy"
                                        {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::OR, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1236 "src/toycc_parser.cpp"
    break;

  case 46: // logical_and_expr: equality_expr
#line 275 "src/toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1242 "src/toycc_parser.cpp"
    break;

  case 47: // logical_and_expr: logical_and_expr AND equality_expr
#line 276 "src/toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::AND, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1252 "src/toycc_parser.cpp"
    break;

  case 48: // equality_expr: relational_expr
#line 284 "src/toycc_parser.yy"
                    { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1258 "src/toycc_parser.cpp"
    break;

  case 49: // equality_expr: equality_expr EQ relational_expr
#line 285 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::EQ, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1268 "src/toycc_parser.cpp"
    break;

  case 50: // equality_expr: equality_expr NE relational_expr
#line 290 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::NE, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1278 "src/toycc_parser.cpp"
    break;

  case 51: // relational_expr: additive_expr
#line 298 "src/toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1284 "src/toycc_parser.cpp"
    break;

  case 52: // relational_expr: relational_expr LT additive_expr
#line 299 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::LT, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1294 "src/toycc_parser.cpp"
    break;

  case 53: // relational_expr: relational_expr GT additive_expr
#line 304 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::GT, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1304 "src/toycc_parser.cpp"
    break;

  case 54: // relational_expr: relational_expr LE additive_expr
#line 309 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::LE, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1314 "src/toycc_parser.cpp"
    break;

  case 55: // relational_expr: relational_expr GE additive_expr
#line 314 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::GE, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1324 "src/toycc_parser.cpp"
    break;

  case 56: // additive_expr: multiplicative_expr
#line 322 "src/toycc_parser.yy"
                        { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1330 "src/toycc_parser.cpp"
    break;

  case 57: // additive_expr: additive_expr ADD multiplicative_expr
#line 323 "src/toycc_parser.yy"
                                          {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::ADD, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1340 "src/toycc_parser.cpp"
    break;

  case 58: // additive_expr: additive_expr SUB multiplicative_expr
#line 328 "src/toycc_parser.yy"
                                          {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::SUB, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1350 "src/toycc_parser.cpp"
    break;

  case 59: // multiplicative_expr: unary_expr
#line 336 "src/toycc_parser.yy"
               { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1356 "src/toycc_parser.cpp"
    break;

  case 60: // multiplicative_expr: multiplicative_expr MUL unary_expr
#line 337 "src/toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::MUL, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1366 "src/toycc_parser.cpp"
    break;

  case 61: // multiplicative_expr: multiplicative_expr DIV unary_expr
#line 342 "src/toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::DIV, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1376 "src/toycc_parser.cpp"
    break;

  case 62: // multiplicative_expr: multiplicative_expr MOD unary_expr
#line 347 "src/toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::MOD, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1386 "src/toycc_parser.cpp"
    break;

  case 63: // unary_expr: primary_expr
#line 355 "src/toycc_parser.yy"
                 { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1392 "src/toycc_parser.cpp"
    break;

  case 64: // unary_expr: ADD unary_expr
#line 356 "src/toycc_parser.yy"
                   {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::POS, std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1402 "src/toycc_parser.cpp"
    break;

  case 65: // unary_expr: SUB unary_expr
#line 361 "src/toycc_parser.yy"
                   {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::NEG, std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1412 "src/toycc_parser.cpp"
    break;

  case 66: // unary_expr: NOT unary_expr
#line 366 "src/toycc_parser.yy"
                   {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::NOT, std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1422 "src/toycc_parser.cpp"
    break;

  case 67: // primary_expr: INT_LITERAL
#line 374 "src/toycc_parser.yy"
                {
      auto expr = std::make_shared<toycc::IntLiteral>(yystack_[0].value.as < int > ());
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1432 "src/toycc_parser.cpp"
    break;

  case 68: // primary_expr: IDENT
#line 379 "src/toycc_parser.yy"
          {
      auto expr = std::make_shared<toycc::VarExpr>(yystack_[0].value.as < std::string > ());
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1442 "src/toycc_parser.cpp"
    break;

  case 69: // primary_expr: IDENT LPAREN opt_arg_list RPAREN
#line 384 "src/toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::CallExpr>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1452 "src/toycc_parser.cpp"
    break;

  case 70: // primary_expr: LPAREN expr RPAREN
#line 389 "src/toycc_parser.yy"
                       { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1458 "src/toycc_parser.cpp"
    break;

  case 71: // opt_arg_list: %empty
#line 393 "src/toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = {}; }
#line 1464 "src/toycc_parser.cpp"
    break;

  case 72: // opt_arg_list: arg_list
#line 394 "src/toycc_parser.yy"
             { yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ()); }
#line 1470 "src/toycc_parser.cpp"
    break;

  case 73: // arg_list: expr
#line 398 "src/toycc_parser.yy"
         { yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = {}; yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ())); }
#line 1476 "src/toycc_parser.cpp"
    break;

  case 74: // arg_list: arg_list COMMA expr
#line 399 "src/toycc_parser.yy"
                        { yystack_[2].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ())); yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ()); }
#line 1482 "src/toycc_parser.cpp"
    break;


#line 1486 "src/toycc_parser.cpp"

            default:
              break;
            }
        }
#if YY_EXCEPTIONS
      catch (const syntax_error& yyexc)
        {
          YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
          error (yyexc);
          YYERROR;
        }
#endif // YY_EXCEPTIONS
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, YY_MOVE (yylhs));
    }
    goto yynewstate;


  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        context yyctx (*this, yyla);
        std::string msg = yysyntax_error_ (yyctx);
        error (YY_MOVE (msg));
      }


    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.kind () == symbol_kind::S_YYEOF)
          YYABORT;
        else if (!yyla.empty ())
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyla.clear ();
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:
    /* Pacify compilers when the user code never invokes YYERROR and
       the label yyerrorlab therefore never appears in user code.  */
    if (false)
      YYERROR;

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    YY_STACK_PRINT ();
    goto yyerrlab1;


  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    // Pop stack until we find a state that shifts the error token.
    for (;;)
      {
        yyn = yypact_[+yystack_[0].state];
        if (!yy_pact_value_is_default_ (yyn))
          {
            yyn += symbol_kind::S_YYerror;
            if (0 <= yyn && yyn <= yylast_
                && yycheck_[yyn] == symbol_kind::S_YYerror)
              {
                yyn = yytable_[yyn];
                if (0 < yyn)
                  break;
              }
          }

        // Pop the current state because it cannot handle the error token.
        if (yystack_.size () == 1)
          YYABORT;

        yy_destroy_ ("Error: popping", yystack_[0]);
        yypop_ ();
        YY_STACK_PRINT ();
      }
    {
      stack_symbol_type error_token;


      // Shift the error token.
      error_token.state = state_type (yyn);
      yypush_ ("Shifting", YY_MOVE (error_token));
    }
    goto yynewstate;


  /*-------------------------------------.
  | yyacceptlab -- YYACCEPT comes here.  |
  `-------------------------------------*/
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;


  /*-----------------------------------.
  | yyabortlab -- YYABORT comes here.  |
  `-----------------------------------*/
  yyabortlab:
    yyresult = 1;
    goto yyreturn;


  /*-----------------------------------------------------.
  | yyreturn -- parsing is finished, return the result.  |
  `-----------------------------------------------------*/
  yyreturn:
    if (!yyla.empty ())
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    YY_STACK_PRINT ();
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
#if YY_EXCEPTIONS
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack\n";
        // Do not try to display the values of the reclaimed symbols,
        // as their printers might throw an exception.
        if (!yyla.empty ())
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
#endif // YY_EXCEPTIONS
  }

  void
  parser::error (const syntax_error& yyexc)
  {
    error (yyexc.what ());
  }

  const char *
  parser::symbol_name (symbol_kind_type yysymbol)
  {
    static const char *const yy_sname[] =
    {
    "end of file", "error", "invalid token", "IDENT", "INT_LITERAL",
  "CONST", "VOID", "INT", "RETURN", "IF", "ELSE", "WHILE", "BREAK",
  "CONTINUE", "ASSIGN", "SEMI", "COMMA", "LPAREN", "RPAREN", "LBRACE",
  "RBRACE", "EQ", "NE", "LE", "GE", "LT", "GT", "ADD", "SUB", "MUL", "DIV",
  "MOD", "AND", "OR", "NOT", "INVALID", "END", "$accept",
  "compilation_unit", "global_decl_list", "global_decl", "func_def",
  "opt_param_list", "param_list", "param", "decl_specifier", "decl",
  "opt_init", "compound_stmt", "stmt_list", "stmt", "decl_stmt",
  "expr_stmt", "if_stmt", "while_stmt", "return_stmt", "break_stmt",
  "continue_stmt", "opt_expr", "expr", "assign_expr", "logical_or_expr",
  "logical_and_expr", "equality_expr", "relational_expr", "additive_expr",
  "multiplicative_expr", "unary_expr", "primary_expr", "opt_arg_list",
  "arg_list", YY_NULLPTR
    };
    return yy_sname[yysymbol];
  }



  // parser::context.
  parser::context::context (const parser& yyparser, const symbol_type& yyla)
    : yyparser_ (yyparser)
    , yyla_ (yyla)
  {}

  int
  parser::context::expected_tokens (symbol_kind_type yyarg[], int yyargn) const
  {
    // Actual number of expected tokens
    int yycount = 0;

    const int yyn = yypact_[+yyparser_.yystack_[0].state];
    if (!yy_pact_value_is_default_ (yyn))
      {
        /* Start YYX at -YYN if negative to avoid negative indexes in
           YYCHECK.  In other words, skip the first -YYN actions for
           this state because they are default actions.  */
        const int yyxbegin = yyn < 0 ? -yyn : 0;
        // Stay within bounds of both yycheck and yytname.
        const int yychecklim = yylast_ - yyn + 1;
        const int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
        for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
          if (yycheck_[yyx + yyn] == yyx && yyx != symbol_kind::S_YYerror
              && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
            {
              if (!yyarg)
                ++yycount;
              else if (yycount == yyargn)
                return 0;
              else
                yyarg[yycount++] = YY_CAST (symbol_kind_type, yyx);
            }
      }

    if (yyarg && yycount == 0 && 0 < yyargn)
      yyarg[0] = symbol_kind::S_YYEMPTY;
    return yycount;
  }






  int
  parser::yy_syntax_error_arguments_ (const context& yyctx,
                                                 symbol_kind_type yyarg[], int yyargn) const
  {
    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yyla) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yyla.  (However, yyla is currently not documented for users.)
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state merging
         (from LALR or IELR) and default reductions corrupt the expected
         token list.  However, the list is correct for canonical LR with
         one exception: it will still contain any token that will not be
         accepted due to an error action in a later state.
    */

    if (!yyctx.lookahead ().empty ())
      {
        if (yyarg)
          yyarg[0] = yyctx.token ();
        int yyn = yyctx.expected_tokens (yyarg ? yyarg + 1 : yyarg, yyargn - 1);
        return yyn + 1;
      }
    return 0;
  }

  // Generate an error message.
  std::string
  parser::yysyntax_error_ (const context& yyctx) const
  {
    // Its maximum.
    enum { YYARGS_MAX = 5 };
    // Arguments of yyformat.
    symbol_kind_type yyarg[YYARGS_MAX];
    int yycount = yy_syntax_error_arguments_ (yyctx, yyarg, YYARGS_MAX);

    char const* yyformat = YY_NULLPTR;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
      default: // Avoid compiler warnings.
        YYCASE_ (0, YY_("syntax error"));
        YYCASE_ (1, YY_("syntax error, unexpected %s"));
        YYCASE_ (2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_ (3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_ (4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_ (5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    std::string yyres;
    // Argument number.
    std::ptrdiff_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += symbol_name (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;
  }


  const signed char parser::yypact_ninf_ = -110;

  const signed char parser::yytable_ninf_ = -1;

  const signed char
  parser::yypact_[] =
  {
    -110,     9,    45,  -110,     6,  -110,  -110,  -110,  -110,    20,
    -110,  -110,  -110,    -6,    80,    45,    23,    61,  -110,    80,
      80,    80,    80,  -110,  -110,    13,    50,    40,    -8,    38,
      29,    72,  -110,    73,    74,  -110,    51,  -110,    80,    76,
    -110,  -110,  -110,    80,    80,    80,    80,    80,    80,    80,
      80,    80,    80,    80,    80,    80,    80,    79,    45,  -110,
    -110,    81,    84,  -110,    50,  -110,    40,    -8,    -8,    38,
      38,    38,    38,    29,    29,  -110,  -110,  -110,  -110,  -110,
    -110,  -110,  -110,    80,    36,  -110,    80,    86,    87,    90,
      91,  -110,    98,  -110,  -110,  -110,  -110,  -110,  -110,  -110,
    -110,  -110,  -110,    94,  -110,    95,    80,    80,  -110,  -110,
      97,  -110,  -110,    99,   100,    68,    68,   102,  -110,    68,
    -110
  };

  const signed char
  parser::yydefact_[] =
  {
       3,     0,     2,     1,     0,    14,    13,     4,     5,     0,
       6,    16,    15,    18,     0,     8,     0,    68,    67,     0,
       0,     0,     0,    19,    41,    42,    44,    46,    48,    51,
      56,    59,    63,     0,     9,    10,     0,    17,    71,     0,
      64,    65,    66,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    12,
      73,     0,    72,    70,    45,    59,    47,    49,    50,    54,
      55,    52,    53,    57,    58,    60,    61,    62,    43,    21,
       7,    11,    69,     0,    39,    74,    39,     0,     0,     0,
       0,    20,     0,    31,    23,    22,    25,    24,    26,    27,
      28,    29,    30,     0,    40,     0,     0,     0,    37,    38,
      18,    32,    36,     0,     0,    39,    39,    33,    35,    39,
      34
  };

  const signed char
  parser::yypgoto_[] =
  {
    -110,  -110,  -110,  -110,  -110,  -110,  -110,    55,    -1,   113,
    -110,    59,  -110,  -109,  -110,  -110,  -110,  -110,  -110,  -110,
    -110,    33,   -14,    64,  -110,    78,    82,    22,   -28,    37,
     -18,  -110,  -110,  -110
  };

  const signed char
  parser::yydefgoto_[] =
  {
       0,     1,     2,     7,     8,    33,    34,    35,    92,    93,
      16,    94,    84,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    61,    62
  };

  const signed char
  parser::yytable_[] =
  {
      23,     9,    40,    41,    42,    39,   117,   118,    14,     3,
     120,    15,    11,    12,    36,    47,    48,    49,    50,    69,
      70,    71,    72,    13,    60,    65,    65,    65,    65,    65,
      65,    65,    65,    65,    65,    75,    76,    77,    37,    17,
      18,     4,     5,     6,    86,    87,    43,    88,    89,    90,
       4,     5,     6,    19,    59,    79,    91,    36,    53,    54,
      55,    45,    46,    20,    21,    51,    52,    67,    68,    85,
      22,    17,    18,     4,     5,     6,    86,    87,    38,    88,
      89,    90,    44,    17,    18,    19,    56,    79,    73,    74,
      58,    57,   113,   114,    63,    20,    21,    19,    79,    82,
      83,   110,    22,   106,   107,   108,   109,    20,    21,   111,
     112,    14,   119,    81,    22,    10,    80,   115,   116,   105,
      78,    64,     0,     0,     0,     0,    66
  };

  const signed char
  parser::yycheck_[] =
  {
      14,     2,    20,    21,    22,    19,   115,   116,    14,     0,
     119,    17,     6,     7,    15,    23,    24,    25,    26,    47,
      48,    49,    50,     3,    38,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    15,     3,
       4,     5,     6,     7,     8,     9,    33,    11,    12,    13,
       5,     6,     7,    17,     3,    19,    20,    58,    29,    30,
      31,    21,    22,    27,    28,    27,    28,    45,    46,    83,
      34,     3,     4,     5,     6,     7,     8,     9,    17,    11,
      12,    13,    32,     3,     4,    17,    14,    19,    51,    52,
      16,    18,   106,   107,    18,    27,    28,    17,    19,    18,
      16,     3,    34,    17,    17,    15,    15,    27,    28,    15,
      15,    14,    10,    58,    34,     2,    57,    18,    18,    86,
      56,    43,    -1,    -1,    -1,    -1,    44
  };

  const signed char
  parser::yystos_[] =
  {
       0,    38,    39,     0,     5,     6,     7,    40,    41,    45,
      46,     6,     7,     3,    14,    17,    47,     3,     4,    17,
      27,    28,    34,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    42,    43,    44,    45,    15,    17,    59,
      67,    67,    67,    33,    32,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    14,    18,    16,     3,
      59,    69,    70,    18,    62,    67,    63,    64,    64,    65,
      65,    65,    65,    66,    66,    67,    67,    67,    60,    19,
      48,    44,    18,    16,    49,    59,     8,     9,    11,    12,
      13,    20,    45,    46,    48,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    58,    17,    17,    15,    15,
       3,    15,    15,    59,    59,    18,    18,    50,    50,    10,
      50
  };

  const signed char
  parser::yyr1_[] =
  {
       0,    37,    38,    39,    39,    40,    40,    41,    42,    42,
      43,    43,    44,    45,    45,    45,    45,    46,    47,    47,
      48,    49,    49,    50,    50,    50,    50,    50,    50,    50,
      50,    51,    52,    53,    53,    54,    55,    56,    57,    58,
      58,    59,    60,    60,    61,    61,    62,    62,    63,    63,
      63,    64,    64,    64,    64,    64,    65,    65,    65,    66,
      66,    66,    66,    67,    67,    67,    67,    68,    68,    68,
      68,    69,    69,    70,    70
  };

  const signed char
  parser::yyr2_[] =
  {
       0,     2,     1,     0,     2,     1,     1,     6,     0,     1,
       1,     3,     2,     1,     1,     2,     2,     4,     0,     2,
       3,     0,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     5,     7,     5,     3,     2,     2,     0,
       1,     1,     1,     3,     1,     3,     1,     3,     1,     3,
       3,     1,     3,     3,     3,     3,     1,     3,     3,     1,
       3,     3,     3,     1,     2,     2,     2,     1,     1,     4,
       3,     0,     1,     1,     3
  };




#if YYDEBUG
  const short
  parser::yyrline_[] =
  {
       0,    74,    74,    83,    84,    91,    92,    96,   108,   109,
     113,   114,   118,   128,   129,   130,   131,   135,   147,   148,
     152,   161,   162,   166,   167,   168,   169,   170,   171,   172,
     173,   177,   186,   195,   202,   213,   223,   232,   240,   248,
     249,   253,   257,   258,   266,   267,   275,   276,   284,   285,
     290,   298,   299,   304,   309,   314,   322,   323,   328,   336,
     337,   342,   347,   355,   356,   361,   366,   374,   379,   384,
     389,   393,   394,   398,   399
  };

  void
  parser::yy_stack_print_ () const
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << int (i->state);
    *yycdebug_ << '\n';
  }

  void
  parser::yy_reduce_print_ (int yyrule) const
  {
    int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):\n";
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YYDEBUG


} // yy
#line 1994 "src/toycc_parser.cpp"

#line 402 "src/toycc_parser.yy"


void yy::parser::error(const std::string& msg) {
  driver.setError(msg);
}
