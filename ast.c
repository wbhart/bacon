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

#include "ast.h"

ast_t * root;

ast_t * ast_nil;

ast_t * new_ast()
{
   ast_t * ast = GC_MALLOC(sizeof(ast_t));
      
   return ast;
}

void ast_init()
{
    ast_nil = new_ast();
    ast_nil->tag = AST_NONE;
}

ast_t * ast0(tag_t tag)
{
   ast_t * ast = new_ast();
   ast->tag = tag;
   return ast;
}

ast_t * ast1(tag_t tag, ast_t * a1)
{
   ast_t * ast = new_ast();
   ast->tag = tag;
   ast->child = a1;
   return ast;
}

ast_t * ast2(tag_t tag, ast_t * a1, ast_t * a2)
{
   ast_t * ast = new_ast();
   ast->tag = tag;
   ast->child = a1;
   a1->next = a2;
   return ast;
}

ast_t * ast3(tag_t tag, ast_t * a1, ast_t * a2, ast_t * a3)
{
   ast_t * ast = new_ast();
   ast->tag = tag;
   ast->child = a1;
   a1->next = a2;
   a2->next = a3;
   return ast;
}

ast_t * ast4(tag_t tag, ast_t * a1, ast_t * a2, ast_t * a3, ast_t * a4)
{
   ast_t * ast = new_ast();
   ast->tag = tag;
   ast->child = a1;
   a1->next = a2;
   a2->next = a3;
   a3->next = a4;
   return ast;
}

ast_t * ast_binop(sym_t * sym, ast_t * a1, ast_t * a2)
{
   ast_t * ast = new_ast();
   ast->tag = AST_BINOP;
   ast->child = a1;
   ast->child->next = a2;
   ast->sym = sym;
   return ast;
}

ast_t * ast_symbol(tag_t tag, sym_t * sym)
{
   ast_t * ast = new_ast();
   ast->tag = tag;
   ast->sym = sym;
}

void ast_print(ast_t * ast, int indent)
{
   int i;
   ast_t * a;
   
   for (i = 0; i < indent; i++)
      printf(" ");
   printf("~ ");

   switch (ast->tag)
   {
      case AST_NONE:
         printf("none\n");
         break;
      case AST_INT:
      case AST_WORD:
      case AST_UWORD:
      case AST_DOUBLE:
      case AST_CHAR:
      case AST_STRING:
         printf("%s\n", ast->sym->name);
         break;
      case AST_TUPLE:
      case AST_LTUPLE:
         printf("tuple\n");
         break;
      case AST_BINOP:
         printf("%s\n", ast->sym->name);
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent + 3);
         break;
      case AST_LIDENT:
      case AST_IDENT:
         printf("%s\n", ast->sym->name);
         break;
      case AST_IF_ELSE_EXPR:
      case AST_IF_ELSE_STMT:
         printf("if\n");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent);
         ast_print(ast->child->next->next, indent);
         break;
      case AST_IF_STMT:
         printf("if\n");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent);
         break;
      case AST_WHILE_STMT:
         printf("while\n");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent);
         break;
      case AST_BREAK:
         printf("break\n");
         break;
      case AST_BLOCK:
         printf("block\n");
         a = ast->child;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         break;
      case AST_THEN:
         printf("then\n");
         a = ast->child;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         break;
      case AST_ELSE:
         printf("else\n");
         a = ast->child;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         break;
      case AST_DO:
         printf("do\n");
         a = ast->child;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         break;
      case AST_DATA_SLOT:
         printf("%s\n", ast->child->sym->name);
         break;
      case AST_TYPE_NAME:
         printf("type_name\n");
         break;
      case AST_TUPLE_TYPE:
         printf("tuple_type\n");
         break;
      case AST_DATA_STMT:
         printf("data %s\n", ast->child->sym->name);
         a = ast->child->next->child;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         break;
      case AST_ASSIGNMENT:
         printf("assign\n");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent + 3);
         break;
      case AST_APPL:
      case AST_LAPPL:
         printf("appl\n");
         a = ast->child->next;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         break;
      case AST_ARRAY_CONSTRUCTOR:
         printf("array_constructor");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent + 3);
         break;
      case AST_SLOT:
      case AST_LSLOT:
         printf("slot\n");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent + 3);
         break;
      case AST_LOCN:
      case AST_LLOCN:
         printf("location\n");
         ast_print(ast->child, indent + 3);
         ast_print(ast->child->next, indent + 3);
         break;
      case AST_PARAM:
         printf("%s\n", ast->child->sym->name);
         break;
      case AST_FN_STMT:
         printf("fn %s\n", ast->child->sym->name);
         a = ast->child->next->child;
         while (a != NULL)
         {
            ast_print(a, indent + 3);
            a = a->next;
         }
         a = ast->child->next->next;
         ast_print(a, indent + 3);
         break;
      case AST_RETURN:
         printf("return\n");
         ast_print(ast->child, indent + 3);
         break;
      default:
         printf("%d\n", ast->tag);
         exception("invalid AST tag in ast_print\n");
   }
}