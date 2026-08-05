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
#line 35 "toycc_parser.yy"

#include <iostream>
#include <stdexcept>
#include <string>

#include "parser_driver.h"

namespace yy {
parser::symbol_type yylex(toycc::ParserDriver& driver);
}

#line 58 "toycc_parser.cpp"


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
#line 131 "toycc_parser.cpp"

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

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.YY_MOVE_OR_COPY< std::shared_ptr<toycc::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_init_declarator: // init_declarator
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
      case symbol_kind::S_global_decl: // global_decl
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<toycc::ASTNode>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_decl: // decl
      case symbol_kind::S_init_declarator_list: // init_declarator_list
        value.YY_MOVE_OR_COPY< std::vector<std::shared_ptr<toycc::Decl>> > (YY_MOVE (that.value));
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

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.move< std::shared_ptr<toycc::Block> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_init_declarator: // init_declarator
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
      case symbol_kind::S_global_decl: // global_decl
        value.move< std::vector<std::shared_ptr<toycc::ASTNode>> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_decl: // decl
      case symbol_kind::S_init_declarator_list: // init_declarator_list
        value.move< std::vector<std::shared_ptr<toycc::Decl>> > (YY_MOVE (that.value));
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

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.copy< std::shared_ptr<toycc::Block> > (that.value);
        break;

      case symbol_kind::S_init_declarator: // init_declarator
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
      case symbol_kind::S_global_decl: // global_decl
        value.copy< std::vector<std::shared_ptr<toycc::ASTNode>> > (that.value);
        break;

      case symbol_kind::S_decl: // decl
      case symbol_kind::S_init_declarator_list: // init_declarator_list
        value.copy< std::vector<std::shared_ptr<toycc::Decl>> > (that.value);
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

      case symbol_kind::S_compound_stmt: // compound_stmt
        value.move< std::shared_ptr<toycc::Block> > (that.value);
        break;

      case symbol_kind::S_init_declarator: // init_declarator
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
      case symbol_kind::S_global_decl: // global_decl
        value.move< std::vector<std::shared_ptr<toycc::ASTNode>> > (that.value);
        break;

      case symbol_kind::S_decl: // decl
      case symbol_kind::S_init_declarator_list: // init_declarator_list
        value.move< std::vector<std::shared_ptr<toycc::Decl>> > (that.value);
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

      case symbol_kind::S_compound_stmt: // compound_stmt
        yylhs.value.emplace< std::shared_ptr<toycc::Block> > ();
        break;

      case symbol_kind::S_init_declarator: // init_declarator
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
      case symbol_kind::S_global_decl: // global_decl
        yylhs.value.emplace< std::vector<std::shared_ptr<toycc::ASTNode>> > ();
        break;

      case symbol_kind::S_decl: // decl
      case symbol_kind::S_init_declarator_list: // init_declarator_list
        yylhs.value.emplace< std::vector<std::shared_ptr<toycc::Decl>> > ();
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
#line 76 "toycc_parser.yy"
                     {
      auto unit = std::make_shared<toycc::CompUnit>();
      unit->globalDecls = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ());
      driver.setLocation(unit.get());
      driver.setAST(std::move(unit));
    }
#line 908 "toycc_parser.cpp"
    break;

  case 3: // global_decl_list: %empty
#line 85 "toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > () = {}; }
#line 914 "toycc_parser.cpp"
    break;

  case 4: // global_decl_list: global_decl_list global_decl
#line 86 "toycc_parser.yy"
                                 {
      for (auto& n : yystack_[0].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ()) {
        yystack_[1].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ().push_back(std::move(n));
      }
      yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > () = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ());
    }
#line 925 "toycc_parser.cpp"
    break;

  case 5: // global_decl: func_def
#line 95 "toycc_parser.yy"
             { yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > () = {}; yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::FuncDef> > ())); }
#line 931 "toycc_parser.cpp"
    break;

  case 6: // global_decl: decl
#line 96 "toycc_parser.yy"
         {
      std::vector<std::shared_ptr<toycc::ASTNode>> nodes;
      for (auto& d : yystack_[0].value.as < std::vector<std::shared_ptr<toycc::Decl>> > ()) {
        nodes.push_back(std::move(d));
      }
      yylhs.value.as < std::vector<std::shared_ptr<toycc::ASTNode>> > () = std::move(nodes);
    }
#line 943 "toycc_parser.cpp"
    break;

  case 7: // func_def: decl_specifier IDENT LPAREN opt_param_list RPAREN compound_stmt
#line 106 "toycc_parser.yy"
                                                                    {
      auto func = std::make_shared<toycc::FuncDef>();
      func->retType = yystack_[5].value.as < toycc::DeclSpec > ().varType;
      func->name = std::move(yystack_[4].value.as < std::string > ());
      func->params = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ());
      func->body = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Block> > ());
      driver.setLocation(func.get());
      yylhs.value.as < std::shared_ptr<toycc::FuncDef> > () = std::move(func);
    }
#line 957 "toycc_parser.cpp"
    break;

  case 8: // opt_param_list: %empty
#line 118 "toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = {}; }
#line 963 "toycc_parser.cpp"
    break;

  case 9: // opt_param_list: param_list
#line 119 "toycc_parser.yy"
               { yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ()); }
#line 969 "toycc_parser.cpp"
    break;

  case 10: // param_list: param
#line 123 "toycc_parser.yy"
          { yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = {}; yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::FuncParam> > ())); }
#line 975 "toycc_parser.cpp"
    break;

  case 11: // param_list: param_list COMMA param
#line 124 "toycc_parser.yy"
                           { yystack_[2].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::FuncParam> > ())); yylhs.value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > () = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::FuncParam>> > ()); }
#line 981 "toycc_parser.cpp"
    break;

  case 12: // param: decl_specifier IDENT
#line 128 "toycc_parser.yy"
                         {
      auto param = std::make_shared<toycc::FuncParam>();
      param->type = yystack_[1].value.as < toycc::DeclSpec > ().varType;
      param->name = std::move(yystack_[0].value.as < std::string > ());
      driver.setLocation(param.get());
      yylhs.value.as < std::shared_ptr<toycc::FuncParam> > () = std::move(param);
    }
#line 993 "toycc_parser.cpp"
    break;

  case 13: // decl_specifier: INT
#line 138 "toycc_parser.yy"
        { yylhs.value.as < toycc::DeclSpec > () = {.isConst = false, .varType = toycc::Type::INT}; }
#line 999 "toycc_parser.cpp"
    break;

  case 14: // decl_specifier: VOID
#line 139 "toycc_parser.yy"
         { yylhs.value.as < toycc::DeclSpec > () = {.isConst = false, .varType = toycc::Type::VOID}; }
#line 1005 "toycc_parser.cpp"
    break;

  case 15: // decl_specifier: CONST INT
#line 140 "toycc_parser.yy"
              { yylhs.value.as < toycc::DeclSpec > () = {.isConst = true, .varType = toycc::Type::INT}; }
#line 1011 "toycc_parser.cpp"
    break;

  case 16: // decl_specifier: CONST VOID
#line 141 "toycc_parser.yy"
               { yylhs.value.as < toycc::DeclSpec > () = {.isConst = true, .varType = toycc::Type::VOID}; }
#line 1017 "toycc_parser.cpp"
    break;

  case 17: // decl: decl_specifier init_declarator_list SEMI
#line 145 "toycc_parser.yy"
                                             {
      for (auto& d : yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Decl>> > ()) {
        d->isConst = yystack_[2].value.as < toycc::DeclSpec > ().isConst;
        d->varType = yystack_[2].value.as < toycc::DeclSpec > ().varType;
      }
      yylhs.value.as < std::vector<std::shared_ptr<toycc::Decl>> > () = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Decl>> > ());
    }
#line 1029 "toycc_parser.cpp"
    break;

  case 18: // init_declarator_list: init_declarator
#line 155 "toycc_parser.yy"
                    { yylhs.value.as < std::vector<std::shared_ptr<toycc::Decl>> > () = {}; yylhs.value.as < std::vector<std::shared_ptr<toycc::Decl>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Decl> > ())); }
#line 1035 "toycc_parser.cpp"
    break;

  case 19: // init_declarator_list: init_declarator_list COMMA init_declarator
#line 156 "toycc_parser.yy"
                                               {
      yystack_[2].value.as < std::vector<std::shared_ptr<toycc::Decl>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Decl> > ()));
      yylhs.value.as < std::vector<std::shared_ptr<toycc::Decl>> > () = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::Decl>> > ());
    }
#line 1044 "toycc_parser.cpp"
    break;

  case 20: // init_declarator: IDENT opt_init
#line 163 "toycc_parser.yy"
                   {
      auto decl = std::make_shared<toycc::Decl>();
      decl->name = std::move(yystack_[1].value.as < std::string > ());
      decl->initExpr = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ());
      driver.setLocation(decl.get());
      yylhs.value.as < std::shared_ptr<toycc::Decl> > () = std::move(decl);
    }
#line 1056 "toycc_parser.cpp"
    break;

  case 21: // opt_init: %empty
#line 173 "toycc_parser.yy"
           { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = nullptr; }
#line 1062 "toycc_parser.cpp"
    break;

  case 22: // opt_init: ASSIGN expr
#line 174 "toycc_parser.yy"
                { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1068 "toycc_parser.cpp"
    break;

  case 23: // compound_stmt: LBRACE stmt_list RBRACE
#line 178 "toycc_parser.yy"
                            {
      auto block = std::make_shared<toycc::Block>();
      block->stmts = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Stmt>> > ());
      driver.setLocation(block.get());
      yylhs.value.as < std::shared_ptr<toycc::Block> > () = std::move(block);
    }
#line 1079 "toycc_parser.cpp"
    break;

  case 24: // stmt_list: %empty
#line 187 "toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::Stmt>> > () = {}; }
#line 1085 "toycc_parser.cpp"
    break;

  case 25: // stmt_list: stmt_list stmt
#line 188 "toycc_parser.yy"
                   { yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Stmt>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ())); yylhs.value.as < std::vector<std::shared_ptr<toycc::Stmt>> > () = std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Stmt>> > ()); }
#line 1091 "toycc_parser.cpp"
    break;

  case 26: // stmt: compound_stmt
#line 192 "toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Block> > ()); }
#line 1097 "toycc_parser.cpp"
    break;

  case 27: // stmt: expr_stmt
#line 193 "toycc_parser.yy"
              { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1103 "toycc_parser.cpp"
    break;

  case 28: // stmt: decl_stmt
#line 194 "toycc_parser.yy"
              { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1109 "toycc_parser.cpp"
    break;

  case 29: // stmt: if_stmt
#line 195 "toycc_parser.yy"
            { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1115 "toycc_parser.cpp"
    break;

  case 30: // stmt: while_stmt
#line 196 "toycc_parser.yy"
               { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1121 "toycc_parser.cpp"
    break;

  case 31: // stmt: return_stmt
#line 197 "toycc_parser.yy"
                { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1127 "toycc_parser.cpp"
    break;

  case 32: // stmt: break_stmt
#line 198 "toycc_parser.yy"
               { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1133 "toycc_parser.cpp"
    break;

  case 33: // stmt: continue_stmt
#line 199 "toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ()); }
#line 1139 "toycc_parser.cpp"
    break;

  case 34: // decl_stmt: decl
#line 203 "toycc_parser.yy"
         {
      auto stmt = std::make_shared<toycc::DeclStmt>();
      stmt->decls = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::Decl>> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1150 "toycc_parser.cpp"
    break;

  case 35: // expr_stmt: opt_expr SEMI
#line 212 "toycc_parser.yy"
                  {
      auto stmt = std::make_shared<toycc::ExprStmt>();
      stmt->expr = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1161 "toycc_parser.cpp"
    break;

  case 36: // if_stmt: IF LPAREN expr RPAREN stmt
#line 221 "toycc_parser.yy"
                               {
      auto stmt = std::make_shared<toycc::IfStmt>();
      stmt->condition = std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ());
      stmt->thenBranch = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1173 "toycc_parser.cpp"
    break;

  case 37: // if_stmt: IF LPAREN expr RPAREN stmt ELSE stmt
#line 228 "toycc_parser.yy"
                                         {
      auto stmt = std::make_shared<toycc::IfStmt>();
      stmt->condition = std::move(yystack_[4].value.as < std::shared_ptr<toycc::Expr> > ());
      stmt->thenBranch = std::move(yystack_[2].value.as < std::shared_ptr<toycc::Stmt> > ());
      stmt->elseBranch = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1186 "toycc_parser.cpp"
    break;

  case 38: // while_stmt: WHILE LPAREN expr RPAREN stmt
#line 239 "toycc_parser.yy"
                                  {
      auto stmt = std::make_shared<toycc::WhileStmt>();
      stmt->condition = std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ());
      stmt->body = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Stmt> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1198 "toycc_parser.cpp"
    break;

  case 39: // return_stmt: RETURN opt_expr SEMI
#line 249 "toycc_parser.yy"
                         {
      auto stmt = std::make_shared<toycc::ReturnStmt>();
      stmt->value = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ());
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1209 "toycc_parser.cpp"
    break;

  case 40: // break_stmt: BREAK SEMI
#line 258 "toycc_parser.yy"
               {
      auto stmt = std::make_shared<toycc::BreakStmt>();
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1219 "toycc_parser.cpp"
    break;

  case 41: // continue_stmt: CONTINUE SEMI
#line 266 "toycc_parser.yy"
                  {
      auto stmt = std::make_shared<toycc::ContinueStmt>();
      driver.setLocation(stmt.get());
      yylhs.value.as < std::shared_ptr<toycc::Stmt> > () = std::move(stmt);
    }
#line 1229 "toycc_parser.cpp"
    break;

  case 42: // opt_expr: %empty
#line 274 "toycc_parser.yy"
           { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = nullptr; }
#line 1235 "toycc_parser.cpp"
    break;

  case 43: // opt_expr: expr
#line 275 "toycc_parser.yy"
         { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1241 "toycc_parser.cpp"
    break;

  case 44: // expr: assign_expr
#line 279 "toycc_parser.yy"
                { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1247 "toycc_parser.cpp"
    break;

  case 45: // assign_expr: logical_or_expr
#line 283 "toycc_parser.yy"
                    { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1253 "toycc_parser.cpp"
    break;

  case 46: // assign_expr: unary_expr ASSIGN assign_expr
#line 284 "toycc_parser.yy"
                                  {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::ASSIGN, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1263 "toycc_parser.cpp"
    break;

  case 47: // logical_or_expr: logical_and_expr
#line 292 "toycc_parser.yy"
                     { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1269 "toycc_parser.cpp"
    break;

  case 48: // logical_or_expr: logical_or_expr OR logical_and_expr
#line 293 "toycc_parser.yy"
                                        {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::OR, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1279 "toycc_parser.cpp"
    break;

  case 49: // logical_and_expr: equality_expr
#line 301 "toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1285 "toycc_parser.cpp"
    break;

  case 50: // logical_and_expr: logical_and_expr AND equality_expr
#line 302 "toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::AND, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1295 "toycc_parser.cpp"
    break;

  case 51: // equality_expr: relational_expr
#line 310 "toycc_parser.yy"
                    { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1301 "toycc_parser.cpp"
    break;

  case 52: // equality_expr: equality_expr EQ relational_expr
#line 311 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::EQ, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1311 "toycc_parser.cpp"
    break;

  case 53: // equality_expr: equality_expr NE relational_expr
#line 316 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::NE, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1321 "toycc_parser.cpp"
    break;

  case 54: // relational_expr: additive_expr
#line 324 "toycc_parser.yy"
                  { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1327 "toycc_parser.cpp"
    break;

  case 55: // relational_expr: relational_expr LT additive_expr
#line 325 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::LT, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1337 "toycc_parser.cpp"
    break;

  case 56: // relational_expr: relational_expr GT additive_expr
#line 330 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::GT, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1347 "toycc_parser.cpp"
    break;

  case 57: // relational_expr: relational_expr LE additive_expr
#line 335 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::LE, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1357 "toycc_parser.cpp"
    break;

  case 58: // relational_expr: relational_expr GE additive_expr
#line 340 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::GE, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1367 "toycc_parser.cpp"
    break;

  case 59: // additive_expr: multiplicative_expr
#line 348 "toycc_parser.yy"
                        { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1373 "toycc_parser.cpp"
    break;

  case 60: // additive_expr: additive_expr ADD multiplicative_expr
#line 349 "toycc_parser.yy"
                                          {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::ADD, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1383 "toycc_parser.cpp"
    break;

  case 61: // additive_expr: additive_expr SUB multiplicative_expr
#line 354 "toycc_parser.yy"
                                          {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::SUB, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1393 "toycc_parser.cpp"
    break;

  case 62: // multiplicative_expr: unary_expr
#line 362 "toycc_parser.yy"
               { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1399 "toycc_parser.cpp"
    break;

  case 63: // multiplicative_expr: multiplicative_expr MUL unary_expr
#line 363 "toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::MUL, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1409 "toycc_parser.cpp"
    break;

  case 64: // multiplicative_expr: multiplicative_expr DIV unary_expr
#line 368 "toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::DIV, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1419 "toycc_parser.cpp"
    break;

  case 65: // multiplicative_expr: multiplicative_expr MOD unary_expr
#line 373 "toycc_parser.yy"
                                       {
      auto expr = std::make_shared<toycc::BinaryExpr>(toycc::BinOp::MOD, std::move(yystack_[2].value.as < std::shared_ptr<toycc::Expr> > ()), std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1429 "toycc_parser.cpp"
    break;

  case 66: // unary_expr: primary_expr
#line 381 "toycc_parser.yy"
                 { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1435 "toycc_parser.cpp"
    break;

  case 67: // unary_expr: ADD unary_expr
#line 382 "toycc_parser.yy"
                   {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::POS, std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1445 "toycc_parser.cpp"
    break;

  case 68: // unary_expr: SUB unary_expr
#line 387 "toycc_parser.yy"
                   {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::NEG, std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1455 "toycc_parser.cpp"
    break;

  case 69: // unary_expr: NOT unary_expr
#line 392 "toycc_parser.yy"
                   {
      auto expr = std::make_shared<toycc::UnaryExpr>(toycc::UnaryOp::NOT, std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1465 "toycc_parser.cpp"
    break;

  case 70: // primary_expr: INT_LITERAL
#line 400 "toycc_parser.yy"
                {
      auto expr = std::make_shared<toycc::IntLiteral>(yystack_[0].value.as < int > ());
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1475 "toycc_parser.cpp"
    break;

  case 71: // primary_expr: IDENT
#line 405 "toycc_parser.yy"
          {
      auto expr = std::make_shared<toycc::VarExpr>(yystack_[0].value.as < std::string > ());
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1485 "toycc_parser.cpp"
    break;

  case 72: // primary_expr: IDENT LPAREN opt_arg_list RPAREN
#line 410 "toycc_parser.yy"
                                     {
      auto expr = std::make_shared<toycc::CallExpr>(yystack_[3].value.as < std::string > (), std::move(yystack_[1].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ()));
      driver.setLocation(expr.get());
      yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(expr);
    }
#line 1495 "toycc_parser.cpp"
    break;

  case 73: // primary_expr: LPAREN expr RPAREN
#line 415 "toycc_parser.yy"
                       { yylhs.value.as < std::shared_ptr<toycc::Expr> > () = std::move(yystack_[1].value.as < std::shared_ptr<toycc::Expr> > ()); }
#line 1501 "toycc_parser.cpp"
    break;

  case 74: // opt_arg_list: %empty
#line 419 "toycc_parser.yy"
           { yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = {}; }
#line 1507 "toycc_parser.cpp"
    break;

  case 75: // opt_arg_list: arg_list
#line 420 "toycc_parser.yy"
             { yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = std::move(yystack_[0].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ()); }
#line 1513 "toycc_parser.cpp"
    break;

  case 76: // arg_list: expr
#line 424 "toycc_parser.yy"
         { yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = {}; yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ())); }
#line 1519 "toycc_parser.cpp"
    break;

  case 77: // arg_list: arg_list COMMA expr
#line 425 "toycc_parser.yy"
                        { yystack_[2].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ().push_back(std::move(yystack_[0].value.as < std::shared_ptr<toycc::Expr> > ())); yylhs.value.as < std::vector<std::shared_ptr<toycc::Expr>> > () = std::move(yystack_[2].value.as < std::vector<std::shared_ptr<toycc::Expr>> > ()); }
#line 1525 "toycc_parser.cpp"
    break;


#line 1529 "toycc_parser.cpp"

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
  "init_declarator_list", "init_declarator", "opt_init", "compound_stmt",
  "stmt_list", "stmt", "decl_stmt", "expr_stmt", "if_stmt", "while_stmt",
  "return_stmt", "break_stmt", "continue_stmt", "opt_expr", "expr",
  "assign_expr", "logical_or_expr", "logical_and_expr", "equality_expr",
  "relational_expr", "additive_expr", "multiplicative_expr", "unary_expr",
  "primary_expr", "opt_arg_list", "arg_list", YY_NULLPTR
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


  const signed char parser::yypact_ninf_ = -112;

  const signed char parser::yytable_ninf_ = -1;

  const signed char
  parser::yypact_[] =
  {
    -112,    11,    48,  -112,     8,  -112,  -112,  -112,  -112,    38,
    -112,  -112,  -112,    -4,   -13,  -112,    83,    48,  -112,  -112,
      46,    40,  -112,    83,    83,    83,    83,  -112,  -112,    52,
      49,     4,    -6,    33,    34,    75,  -112,    73,    76,  -112,
      90,    80,  -112,    83,    79,  -112,  -112,  -112,    83,    83,
      83,    83,    83,    83,    83,    83,    83,    83,    83,    83,
      83,    83,    82,    48,  -112,  -112,    84,    87,  -112,    49,
    -112,     4,    -6,    -6,    33,    33,    33,    33,    34,    34,
    -112,  -112,  -112,  -112,  -112,  -112,  -112,  -112,    83,    39,
    -112,    83,    89,    91,    92,    94,  -112,    46,  -112,  -112,
    -112,  -112,  -112,  -112,  -112,  -112,  -112,  -112,    97,  -112,
      98,    83,    83,  -112,  -112,  -112,  -112,    86,    96,    71,
      71,   105,  -112,    71,  -112
  };

  const signed char
  parser::yydefact_[] =
  {
       3,     0,     2,     1,     0,    14,    13,     4,     5,     0,
       6,    16,    15,    21,     0,    18,     0,     8,    20,    17,
       0,    71,    70,     0,     0,     0,     0,    22,    44,    45,
      47,    49,    51,    54,    59,    62,    66,     0,     9,    10,
       0,    21,    19,    74,     0,    67,    68,    69,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    12,    76,     0,    75,    73,    48,
      62,    50,    52,    53,    57,    58,    55,    56,    60,    61,
      63,    64,    65,    46,    24,     7,    11,    72,     0,    42,
      77,    42,     0,     0,     0,     0,    23,     0,    34,    26,
      25,    28,    27,    29,    30,    31,    32,    33,     0,    43,
       0,     0,     0,    40,    41,    35,    39,     0,     0,    42,
      42,    36,    38,    42,    37
  };

  const signed char
  parser::yypgoto_[] =
  {
    -112,  -112,  -112,  -112,  -112,  -112,  -112,    53,    -1,   116,
    -112,    99,  -112,    58,  -112,  -111,  -112,  -112,  -112,  -112,
    -112,  -112,  -112,    30,   -16,    61,  -112,    77,    74,    18,
     -31,    14,   -20,  -112,  -112,  -112
  };

  const signed char
  parser::yydefgoto_[] =
  {
       0,     1,     2,     7,     8,    37,    38,    39,    97,    98,
      14,    15,    18,    99,    89,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    66,    67
  };

  const signed char
  parser::yytable_[] =
  {
      27,     9,    19,    20,    45,    46,    47,    44,   121,   122,
      16,     3,   124,    17,    11,    12,    40,    52,    53,    54,
      55,    74,    75,    76,    77,    50,    51,    65,    70,    70,
      70,    70,    70,    70,    70,    70,    70,    70,    80,    81,
      82,    13,    21,    22,     4,     5,     6,    91,    92,    41,
      93,    94,    95,     4,     5,     6,    23,    43,    84,    96,
      56,    57,    40,    58,    59,    60,    24,    25,    72,    73,
      78,    79,    90,    26,    21,    22,     4,     5,     6,    91,
      92,    49,    93,    94,    95,    48,    21,    22,    23,    61,
      84,    62,    63,    64,    16,   117,   118,    68,    24,    25,
      23,    84,    87,    88,   119,    26,   111,   113,   112,   114,
      24,    25,   115,   116,   120,   123,    86,    26,    10,    42,
      85,   110,    83,    71,     0,    69
  };

  const signed char
  parser::yycheck_[] =
  {
      16,     2,    15,    16,    24,    25,    26,    23,   119,   120,
      14,     0,   123,    17,     6,     7,    17,    23,    24,    25,
      26,    52,    53,    54,    55,    21,    22,    43,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,     3,     3,     4,     5,     6,     7,     8,     9,     3,
      11,    12,    13,     5,     6,     7,    17,    17,    19,    20,
      27,    28,    63,    29,    30,    31,    27,    28,    50,    51,
      56,    57,    88,    34,     3,     4,     5,     6,     7,     8,
       9,    32,    11,    12,    13,    33,     3,     4,    17,    14,
      19,    18,    16,     3,    14,   111,   112,    18,    27,    28,
      17,    19,    18,    16,    18,    34,    17,    15,    17,    15,
      27,    28,    15,    15,    18,    10,    63,    34,     2,    20,
      62,    91,    61,    49,    -1,    48
  };

  const signed char
  parser::yystos_[] =
  {
       0,    38,    39,     0,     5,     6,     7,    40,    41,    45,
      46,     6,     7,     3,    47,    48,    14,    17,    49,    15,
      16,     3,     4,    17,    27,    28,    34,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    42,    43,    44,
      45,     3,    48,    17,    61,    69,    69,    69,    33,    32,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    14,    18,    16,     3,    61,    71,    72,    18,    64,
      69,    65,    66,    66,    67,    67,    67,    67,    68,    68,
      69,    69,    69,    62,    19,    50,    44,    18,    16,    51,
      61,     8,     9,    11,    12,    13,    20,    45,    46,    50,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      60,    17,    17,    15,    15,    15,    15,    61,    61,    18,
      18,    52,    52,    10,    52
  };

  const signed char
  parser::yyr1_[] =
  {
       0,    37,    38,    39,    39,    40,    40,    41,    42,    42,
      43,    43,    44,    45,    45,    45,    45,    46,    47,    47,
      48,    49,    49,    50,    51,    51,    52,    52,    52,    52,
      52,    52,    52,    52,    53,    54,    55,    55,    56,    57,
      58,    59,    60,    60,    61,    62,    62,    63,    63,    64,
      64,    65,    65,    65,    66,    66,    66,    66,    66,    67,
      67,    67,    68,    68,    68,    68,    69,    69,    69,    69,
      70,    70,    70,    70,    71,    71,    72,    72
  };

  const signed char
  parser::yyr2_[] =
  {
       0,     2,     1,     0,     2,     1,     1,     6,     0,     1,
       1,     3,     2,     1,     1,     2,     2,     3,     1,     3,
       2,     0,     2,     3,     0,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     5,     7,     5,     3,
       2,     2,     0,     1,     1,     1,     3,     1,     3,     1,
       3,     1,     3,     3,     1,     3,     3,     3,     3,     1,
       3,     3,     1,     3,     3,     3,     1,     2,     2,     2,
       1,     1,     4,     3,     0,     1,     1,     3
  };




#if YYDEBUG
  const short
  parser::yyrline_[] =
  {
       0,    76,    76,    85,    86,    95,    96,   106,   118,   119,
     123,   124,   128,   138,   139,   140,   141,   145,   155,   156,
     163,   173,   174,   178,   187,   188,   192,   193,   194,   195,
     196,   197,   198,   199,   203,   212,   221,   228,   239,   249,
     258,   266,   274,   275,   279,   283,   284,   292,   293,   301,
     302,   310,   311,   316,   324,   325,   330,   335,   340,   348,
     349,   354,   362,   363,   368,   373,   381,   382,   387,   392,
     400,   405,   410,   415,   419,   420,   424,   425
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
#line 2037 "toycc_parser.cpp"

#line 428 "toycc_parser.yy"


void yy::parser::error(const std::string& msg) {
  driver.setError(msg);
}
