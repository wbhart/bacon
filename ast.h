/*

Copyright 2012 William Hart. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY William Hart ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL William Hart OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "gc.h"

#include <llvm-c/Core.h>  
#include <llvm-c/Analysis.h>  
#include <llvm-c/ExecutionEngine.h>  
#include <llvm-c/Target.h>  
#include <llvm-c/Transforms/Scalar.h> 

#include "symbol.h"
#include "types.h"
#include "environment.h"

#ifndef AST_H
#define AST_H

#ifdef __cplusplus
 extern "C" {
#endif

typedef enum
{
   AST_NONE, 
   AST_INT, AST_WORD, AST_UWORD, AST_DOUBLE, 
   AST_CHAR, AST_STRING,
   AST_BINOP, 
   AST_BLOCK, AST_IF_ELSE_EXPR, 
   AST_IF_ELSE_STMT, AST_IF_STMT, AST_THEN, AST_ELSE, 
   AST_ASSIGNMENT, AST_WHILE_STMT, AST_DO, AST_BREAK,
   AST_DATA_STMT, AST_DATA_BODY, AST_DATA_SLOT,
   AST_TUPLE_TYPE, AST_TYPE_NAME,
   AST_FN_STMT, AST_PARAM_BODY, AST_PARAM, 
   AST_RETURN, 
   AST_ARRAY_CONSTRUCTOR, AST_ARRAY_TYPE,
   AST_IDENT, AST_TUPLE, AST_SLOT, AST_LOCN, AST_APPL,
   AST_LIDENT, AST_LTUPLE, AST_LSLOT, AST_LLOCN, AST_LAPPL,
   AST_FN_BODY
} tag_t;

typedef struct ast_t
{
   tag_t tag;
   struct ast_t * child;
   struct ast_t * next;
   sym_t * sym;
   type_t * type;
   env_t * env;
} ast_t;

extern ast_t * root;

extern ast_t * ast_nil;

ast_t * new_ast();

void ast_init();

void ast_print(ast_t * ast, int indent);

ast_t * ast0(tag_t tag);

ast_t * ast1(tag_t tag, ast_t * a1);

ast_t * ast2(tag_t tag, ast_t * a1, ast_t * a2);

ast_t * ast3(tag_t tag, ast_t * a1, ast_t * a2, ast_t * a3);

ast_t * ast4(tag_t tag, ast_t * a1, ast_t * a2, ast_t * a3, ast_t * a4);

ast_t * ast_binop(sym_t * sym, ast_t * a1, ast_t * a2);

ast_t * ast_symbol(tag_t tag, sym_t * sym);

#ifdef __cplusplus
}
#endif

#endif