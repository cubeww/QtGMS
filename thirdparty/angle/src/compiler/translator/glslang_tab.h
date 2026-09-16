/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_GLSLANG_TAB_H_INCLUDED
# define YY_YY_GLSLANG_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif
/* "%code requires" blocks.  */

#define YYLTYPE TSourceLoc
#define YYLTYPE_IS_DECLARED 1


/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    INVARIANT = 258,               /* INVARIANT  */
    HIGH_PRECISION = 259,          /* HIGH_PRECISION  */
    MEDIUM_PRECISION = 260,        /* MEDIUM_PRECISION  */
    LOW_PRECISION = 261,           /* LOW_PRECISION  */
    PRECISION = 262,               /* PRECISION  */
    ATTRIBUTE = 263,               /* ATTRIBUTE  */
    CONST_QUAL = 264,              /* CONST_QUAL  */
    BOOL_TYPE = 265,               /* BOOL_TYPE  */
    FLOAT_TYPE = 266,              /* FLOAT_TYPE  */
    INT_TYPE = 267,                /* INT_TYPE  */
    UINT_TYPE = 268,               /* UINT_TYPE  */
    BREAK = 269,                   /* BREAK  */
    CONTINUE = 270,                /* CONTINUE  */
    DO = 271,                      /* DO  */
    ELSE = 272,                    /* ELSE  */
    FOR = 273,                     /* FOR  */
    IF = 274,                      /* IF  */
    DISCARD = 275,                 /* DISCARD  */
    RETURN = 276,                  /* RETURN  */
    SWITCH = 277,                  /* SWITCH  */
    CASE = 278,                    /* CASE  */
    DEFAULT = 279,                 /* DEFAULT  */
    BVEC2 = 280,                   /* BVEC2  */
    BVEC3 = 281,                   /* BVEC3  */
    BVEC4 = 282,                   /* BVEC4  */
    IVEC2 = 283,                   /* IVEC2  */
    IVEC3 = 284,                   /* IVEC3  */
    IVEC4 = 285,                   /* IVEC4  */
    VEC2 = 286,                    /* VEC2  */
    VEC3 = 287,                    /* VEC3  */
    VEC4 = 288,                    /* VEC4  */
    UVEC2 = 289,                   /* UVEC2  */
    UVEC3 = 290,                   /* UVEC3  */
    UVEC4 = 291,                   /* UVEC4  */
    MATRIX2 = 292,                 /* MATRIX2  */
    MATRIX3 = 293,                 /* MATRIX3  */
    MATRIX4 = 294,                 /* MATRIX4  */
    IN_QUAL = 295,                 /* IN_QUAL  */
    OUT_QUAL = 296,                /* OUT_QUAL  */
    INOUT_QUAL = 297,              /* INOUT_QUAL  */
    UNIFORM = 298,                 /* UNIFORM  */
    VARYING = 299,                 /* VARYING  */
    MATRIX2x3 = 300,               /* MATRIX2x3  */
    MATRIX3x2 = 301,               /* MATRIX3x2  */
    MATRIX2x4 = 302,               /* MATRIX2x4  */
    MATRIX4x2 = 303,               /* MATRIX4x2  */
    MATRIX3x4 = 304,               /* MATRIX3x4  */
    MATRIX4x3 = 305,               /* MATRIX4x3  */
    CENTROID = 306,                /* CENTROID  */
    FLAT = 307,                    /* FLAT  */
    SMOOTH = 308,                  /* SMOOTH  */
    STRUCT = 309,                  /* STRUCT  */
    VOID_TYPE = 310,               /* VOID_TYPE  */
    WHILE = 311,                   /* WHILE  */
    SAMPLER2D = 312,               /* SAMPLER2D  */
    SAMPLERCUBE = 313,             /* SAMPLERCUBE  */
    SAMPLER_EXTERNAL_OES = 314,    /* SAMPLER_EXTERNAL_OES  */
    SAMPLER2DRECT = 315,           /* SAMPLER2DRECT  */
    SAMPLER2DARRAY = 316,          /* SAMPLER2DARRAY  */
    ISAMPLER2D = 317,              /* ISAMPLER2D  */
    ISAMPLER3D = 318,              /* ISAMPLER3D  */
    ISAMPLERCUBE = 319,            /* ISAMPLERCUBE  */
    ISAMPLER2DARRAY = 320,         /* ISAMPLER2DARRAY  */
    USAMPLER2D = 321,              /* USAMPLER2D  */
    USAMPLER3D = 322,              /* USAMPLER3D  */
    USAMPLERCUBE = 323,            /* USAMPLERCUBE  */
    USAMPLER2DARRAY = 324,         /* USAMPLER2DARRAY  */
    SAMPLER3D = 325,               /* SAMPLER3D  */
    SAMPLER3DRECT = 326,           /* SAMPLER3DRECT  */
    SAMPLER2DSHADOW = 327,         /* SAMPLER2DSHADOW  */
    SAMPLERCUBESHADOW = 328,       /* SAMPLERCUBESHADOW  */
    SAMPLER2DARRAYSHADOW = 329,    /* SAMPLER2DARRAYSHADOW  */
    LAYOUT = 330,                  /* LAYOUT  */
    IDENTIFIER = 331,              /* IDENTIFIER  */
    TYPE_NAME = 332,               /* TYPE_NAME  */
    FLOATCONSTANT = 333,           /* FLOATCONSTANT  */
    INTCONSTANT = 334,             /* INTCONSTANT  */
    UINTCONSTANT = 335,            /* UINTCONSTANT  */
    BOOLCONSTANT = 336,            /* BOOLCONSTANT  */
    FIELD_SELECTION = 337,         /* FIELD_SELECTION  */
    LEFT_OP = 338,                 /* LEFT_OP  */
    RIGHT_OP = 339,                /* RIGHT_OP  */
    INC_OP = 340,                  /* INC_OP  */
    DEC_OP = 341,                  /* DEC_OP  */
    LE_OP = 342,                   /* LE_OP  */
    GE_OP = 343,                   /* GE_OP  */
    EQ_OP = 344,                   /* EQ_OP  */
    NE_OP = 345,                   /* NE_OP  */
    AND_OP = 346,                  /* AND_OP  */
    OR_OP = 347,                   /* OR_OP  */
    XOR_OP = 348,                  /* XOR_OP  */
    MUL_ASSIGN = 349,              /* MUL_ASSIGN  */
    DIV_ASSIGN = 350,              /* DIV_ASSIGN  */
    ADD_ASSIGN = 351,              /* ADD_ASSIGN  */
    MOD_ASSIGN = 352,              /* MOD_ASSIGN  */
    LEFT_ASSIGN = 353,             /* LEFT_ASSIGN  */
    RIGHT_ASSIGN = 354,            /* RIGHT_ASSIGN  */
    AND_ASSIGN = 355,              /* AND_ASSIGN  */
    XOR_ASSIGN = 356,              /* XOR_ASSIGN  */
    OR_ASSIGN = 357,               /* OR_ASSIGN  */
    SUB_ASSIGN = 358,              /* SUB_ASSIGN  */
    LEFT_PAREN = 359,              /* LEFT_PAREN  */
    RIGHT_PAREN = 360,             /* RIGHT_PAREN  */
    LEFT_BRACKET = 361,            /* LEFT_BRACKET  */
    RIGHT_BRACKET = 362,           /* RIGHT_BRACKET  */
    LEFT_BRACE = 363,              /* LEFT_BRACE  */
    RIGHT_BRACE = 364,             /* RIGHT_BRACE  */
    DOT = 365,                     /* DOT  */
    COMMA = 366,                   /* COMMA  */
    COLON = 367,                   /* COLON  */
    EQUAL = 368,                   /* EQUAL  */
    SEMICOLON = 369,               /* SEMICOLON  */
    BANG = 370,                    /* BANG  */
    DASH = 371,                    /* DASH  */
    TILDE = 372,                   /* TILDE  */
    PLUS = 373,                    /* PLUS  */
    STAR = 374,                    /* STAR  */
    SLASH = 375,                   /* SLASH  */
    PERCENT = 376,                 /* PERCENT  */
    LEFT_ANGLE = 377,              /* LEFT_ANGLE  */
    RIGHT_ANGLE = 378,             /* RIGHT_ANGLE  */
    VERTICAL_BAR = 379,            /* VERTICAL_BAR  */
    CARET = 380,                   /* CARET  */
    AMPERSAND = 381,               /* AMPERSAND  */
    QUESTION = 382                 /* QUESTION  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{

    struct {
        union {
            TString *string;
            float f;
            int i;
            unsigned int u;
            bool b;
        };
        TSymbol* symbol;
    } lex;
    struct {
        TOperator op;
        union {
            TIntermNode* intermNode;
            TIntermNodePair nodePair;
            TIntermTyped* intermTypedNode;
            TIntermAggregate* intermAggregate;
            TIntermSwitch* intermSwitch;
            TIntermCase* intermCase;
        };
        union {
            TPublicType type;
            TPrecision precision;
            TLayoutQualifier layoutQualifier;
            TQualifier qualifier;
            TFunction* function;
            TParameter param;
            TField* field;
            TFieldList* fieldList;
        };
    } interm;


};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif




int yyparse (TParseContext* context, void *scanner);


#endif /* !YY_YY_GLSLANG_TAB_H_INCLUDED  */
