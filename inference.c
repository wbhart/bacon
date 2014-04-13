/*

Copyright 2012, 2014 William Hart. All rights reserved.

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

#include "inference.h"

/*
   Change AST tag to L-value version of tag
*/
void to_lvalue(ast_t * a)
{
   if (a->tag == AST_IDENT) a->tag = AST_LIDENT;
   else if (a->tag == AST_SLOT) a->tag = AST_LSLOT;
   else if (a->tag == AST_LOCN) a->tag = AST_LLOCN;
   else if (a->tag == AST_TUPLE) a->tag = AST_LTUPLE;
   else if (a->tag == AST_APPL) a->tag = AST_LAPPL; 
   else
      exception("Invalid L-value in assignment\n");
}

/*
   Annotate AST node for the Lvalue side of an assignment
   given the (already inferred) type of the expression being 
   assigned on the RHS.
*/
void assign_inference(ast_t * a, type_t * b)
{
    int i, j;
    bind_t * bind;
   
    if (a->tag == AST_LIDENT)
    {
       bind = find_symbol(a->sym); /* look up identifier */
       if (!bind) /* identifier doesn't exist */
          bind_symbol(a->sym, b, NULL); /* put new identifier in scope */
       else if (b != bind->type) /* identifier type doesn't match expression type */
             exception("Identifier type doesn't match expression type in assignment\n");
       a->type = b; /* infer type of identifier */
    } else if (a->tag == AST_LTUPLE)
    {
       if (b->tag != TUPLE)
          exception("Attempt to assign non-tuple to tuple\n");
       ast_t * a1 = a->child; /* list of entries in tuple */
       i = ast_count(a1);
       if (i != b->arity) /* check number of entries in tuple of LHS and RHS agree */
          exception("Incorrect number of entries in tuple assignment\n");
       for (j = 0; j < i; j++) /* infer types of each entry in the tuple on the LHS */
       {
          to_lvalue(a1);
          assign_inference(a1, b->args[j]);
          a1 = a1->next;
       }
       a->type = b;
    } else if (a->tag == AST_LSLOT)
    {
       inference(a);
       if (a->type != b)
          exception("Slot type doesn't match expression type in assignment\n");
    } else if (a->tag == AST_LLOCN)
    {
       inference(a);
       if (a->type != b)
          exception("Array entry type doesn't match expression type in assignment\n");
    } else if (a->tag == AST_LAPPL)
    {
       inference(a);
       if (a->type != b)
          exception("Lvalue type doesn't match expression type in assignment\n");
    } else
       exception("Invalid L-value in assignment\n");
}

/* 
   Count the nodes in an AST list.
*/
int ast_count(ast_t * a)
{
   int i = 0;

   while (a != NULL)
   {
      a = a->next;
      i++;
   }

   return i;   
}

/* 
   Do inference for each node of an AST list and return 
   the type of the final node.
*/
type_t * list_inference(ast_t * a)
{
   if (a == NULL)
      return t_nil;

   while (a->next != NULL)
   {
      inference(a);
      a = a->next;
   }
   inference(a);
      
   return a->type;
}

/*
   Given an AST list a, assign the (already inferred) types of
   the elements of that list to a list of args (e.g. for a 
   constructor, function or data type).
*/
int assign_args(type_t ** args, ast_t * a)
{
   int i = 0;

   while (a != NULL)
   {
      args[i] = a->type;
      a = a->next;
      i++;
   }

   return i;
}

/*
   Given an AST list a of AST_SYMBOL nodes, assign the symbol
   names to a list of symbols, e.g. slots in a new data type.
*/
int assign_syms(sym_t ** syms, ast_t * a)
{
   int i = 0;

   while (a != NULL)
   {
      syms[i] = a->child->sym;
      a = a->next;
      i++;
   }

   return i;
}

/*
   Find the index of a slot in a data type t by symbol name sym.
*/
int find_slot(type_t * t, sym_t * sym)
{
   int i;
   
   for (i = 0; i < t->arity; i++)
      if (t->slots[i] == sym)
         break;

   return i;
}

/*
   Given a generic or constructor type t, search through its 
   current list of functions to find one with prototype matching 
   the types given by the (already type inferred) AST parameter 
   list. If one is found, return a pointer to it, else return NULL.

   If t is instead a function type, simply return it if it matches
   the supplied AST parameter type list, otherwise return NULL.
*/
type_t * find_prototype(type_t * t, ast_t * a)
{
   int i, j;
   int c = ast_count(a);
   ast_t * a2;

   if (t->tag == FN)
   {
      a2 = a;

      if (t->arity == c)
      {
         for (j = 0; j < c; j++)
         {
            if (t->args[j] != a2->type)
               break;
            a2 = a2->next;
         }

         if (j == c)
            return t;
      }
   } else /* generic or type constructor */
   {
      for (i = 0; i < t->arity; i++)
      {
         type_t * fn = t->args[i];
         a2 = a;

         if ((fn = find_prototype(fn, a)))
            return fn;
      }
   }
   
   return NULL; /* didn't find an op with that prototype */
}

/*
   Annotate an AST with known types
*/
void inference(ast_t * a)
{
   bind_t * bind;
   type_t * t1, * t2, * f1;
   type_t ** args, ** fns;
   sym_t ** slots;
   ast_t * a1, * a2, * a3, * a4;
   env_t * scope_save;
   int i, j, k;

   switch (a->tag)
   {
   case AST_NONE:
      a->type = t_nil;
      break;
   case AST_INT:
      a->type = t_int;
      break;
   case AST_WORD:
      a->type = t_word;
      break;
   case AST_UWORD:
      a->type = t_uword;
      break;
   case AST_DOUBLE:
      a->type = t_double;
      break;
   case AST_CHAR:
      a->type = t_char;
      break;
   case AST_STRING:
      a->type = t_string;
      break;
   case AST_BINOP:
      a1 = a->child; /* list of arguments to operator */
      list_inference(a1); /* infer types of arguments */
      bind = find_symbol(a->sym); /* look up operator */
      if ((t1 = find_prototype(bind->type, a1))) /* find op with that prototype */
         a->type = t1->ret;
      else
         exception("Operator not found in inference\n");
      break;
   case AST_BLOCK:
      a->env = scope_up(); /* block delineates new scope */
      a->type = list_inference(a->child); /* type is that of last stmt in block */
      scope_down();
      break;
   case AST_IF_ELSE_EXPR:
      a1 = a->child; /* condition */
      a2 = a1->next; /* if block */
      a3 = a2->next; /* else block */
      inference(a1);
      if (a1->type != t_bool)
         exception("Boolean expression expected in if..else expression\n");
      inference(a2);
      inference(a3);
      if (a2->type != a3->type)
         exception("Types not equal in branches of if..else expression\n");
      a->type = a2->type; /* type is that of either branch */
      break;
   case AST_IF_ELSE_STMT:
      a1 = a->child; /* condition */
      a2 = a1->next; /* if block/stmt */
      a3 = a2->next; /* else block/stmt */
      inference(a1);
      if (a1->type != t_bool)
         exception("Boolean expression expected in if..else statement\n");
      inference(a2);
      inference(a3);
      a->type = t_nil;
      break;
   case AST_IF_STMT:
      a1 = a->child; /* condition */
      a2 = a1->next; /* if block/stmt */
      inference(a1);
      if (a1->type != t_bool)
         exception("Boolean expression expected in if statement\n");
      inference(a2);
      a->type = t_nil;
      break;
   case AST_THEN: /* TODO: are these tags needed any more within if/while? */
   case AST_ELSE:
   case AST_DO:
      a1 = a->child; /* block/stmt */
      inference(a1); 
      a->type = a1->type;
      break;
   case AST_ASSIGNMENT:
      a1 = a->child; /* Lvalue */
      a2 = a1->next; /* expression */
      inference(a2);
      assign_inference(a1, a2->type);
      a->type = t_nil; /* TODO: should assignment be an expression? */
      break;
   case AST_WHILE_STMT:
      a1 = a->child; /* condition */
      a2 = a1->next; /* while block/stmt */
      inference(a1);
      if (a1->type != t_bool)
         exception("Boolean expression expected in while statement\n");
      inference(a2);
      a->type = t_nil;
      break;
   case AST_BREAK:
      a->type = t_nil;
      break;
   case AST_DATA_STMT:
      a1 = a->child; /* name of type */
      a2 = a1->next; /* data body */
      a3 = a2->child; /* list of slots with types */
      i = ast_count(a3); /* number of slots */
      args = GC_MALLOC(i*sizeof(type_t *)); /* types of slots */
      slots = GC_MALLOC(i*sizeof(sym_t *)); /* slot names */
      t1 = data_type(i, args, a1->sym, slots, 0, NULL); /* the new type being created */
      f1 = fn_type(t1, i, args); /* the constructor function */
      fns = GC_MALLOC(sizeof(type_t *)); /* one automatic constructor function */
      fns[0] = f1;
      t2 = constructor_type(a1->sym, t1, 1, fns); /* generic constructor */
      bind_symbol(a1->sym, t2, NULL); /* bind new type name to generic constructor */
      inference(a2); /* infer types in data declaration body */
      assign_args(f1->args, a3); /* infer constructor function parameter types */
      assign_args(t1->args, a3); /* infer types of slots in new data type */
      assign_syms(t1->slots, a3); /* fill in slot names in new data type */
      a->type = t_nil;
      break;
   case AST_DATA_BODY:
      a1 = a->child; /* list of slot names with types (data slots) */
      list_inference(a1);
      break;
   case AST_DATA_SLOT:
      a1 = a->child; /* slot name */
      a2 = a1->next; /* slot type name */
      inference(a2);
      a->type = a2->type;
      break;
   case AST_TUPLE_TYPE:
      a1 = a->child; /* list of entries in tuple */
      list_inference(a1);
      i = ast_count(a1);
      args = GC_MALLOC(i*sizeof(type_t *));
      assign_args(args, a->child); /* infer types of each entry in type tuple */
      a->type = tuple_type(i, args); /* return unique tuple type with those type entries */
      break;
   case AST_TYPE_NAME:
      bind = find_symbol(a->sym); /* look up type name */
      if (!bind)
         exception("Type name not found\n");
      else if (bind->type->tag == CONSTRUCTOR)
         a->type = bind->type->ret; /* constructor returns an object of given type */
      else /* TODO: this case shouldn't exist; conversions are just constructors */
         a->type = bind->type; /* for built-in types symbol is bound to type itself */
      break;
   case AST_FN_STMT: /* called initially when AST for fn declaration is first traversed */
      a1 = a->child; /* function name */
      a2 = a1->next; /* param list */
      a3 = a2->next; /* return type */
      /* function body is ignored for this round */
      a->env = scope_up(); /* new scope for function */
      inference(a2);
      inference(a3);
      bind_symbol(sym_lookup("return"), a3->type, NULL); /* return is symbol in fn scope */
      i = ast_count(a2->child);
      args = GC_MALLOC(i*sizeof(type_t *));
      f1 = fn_type(a3->type, i, args); /* new type for function; always distinct */
      assign_args(f1->args, a2->child); /* fill in types of fn arguments */
      f1->ast = a; /* store ast for second inference round when jit'ing body */
      scope_down(); /* back to global scope (where fn will live */
      bind = find_symbol(a1->sym); /* look to see if generic already exists for this fn */
      if (bind != NULL && bind->type->tag == GENERIC) /* insert fn into existing generic */
         generic_insert(bind->type, f1);
      else /* new generic */
      {
         fns = GC_MALLOC(sizeof(type_t *)); /* just this function in generic */
         fns[0] = f1;
         t1 = generic_type(1, fns); /* new generic containing function */
         bind_generic(a1->sym, t1); /* bind function name to generic */
      }   
      a->type = t_nil;
      break;
   case AST_PARAM_BODY:
      a1 = a->child; /* list of parameter names with types */
      list_inference(a1);
      break;
   case AST_PARAM:
      a1 = a->child; /* parameter name */
      a2 = a1->next; /* type name */
      inference(a2);
      bind_symbol(a1->sym, a2->type, NULL); /* bind parameter name to type (in fn scope) */
      a->type = a2->type; 
      break;
   case AST_FN_BODY: /* called on demand when jit'ing function */
      a1 = a->child->next->next->next; /* fn block (body) */ 
      scope_save = current_scope; /* save current scope */
      current_scope = a->env; /* load function scope */
      inference(a1); 
      current_scope = scope_save; /* restore scope */
      break;
   case AST_RETURN:
      a1 = a->child; /* return expression */
      inference(a1);
      bind = find_symbol(sym_lookup("return")); /* look return up in fn scope */
      if (bind == NULL) /* TODO: prevent this at the grammar level */
         exception("Return outside of a function");
      if (bind->type != a1->type) /* check function return type matches return expression */
         exception("Return type does not match prototype in function definition");
      a->type = t_nil;
      break;
   case AST_ARRAY_CONSTRUCTOR:
      a1 = a->child; /* type of array elements */
      a2 = a1->next; /* expression giving number of elements */
      inference(a2);
      inference(a1);
      if (a2->type != t_word)
         exception("Number of elements in an array must be a word\n");
      t1 = array_type(a1->type); /* type of array is parameterised by type of elements */
      a->type = t1;
      break;
   case AST_ARRAY_TYPE:
      a1 = a->child; /* type of array elements */
      inference(a1);
      t1 = array_type(a1->type); /* type of array is parameterised by type of elements */
      a->type = t1;
      break;
   case AST_IDENT:
      bind = find_symbol(a->sym); /* look up identifier */
      if (!bind)
         exception("Symbol not found in expression\n");
      a->type = bind->type;
      break;
   case AST_TUPLE:
      a1 = a->child; /* list of expressions in tuple */
      list_inference(a1);
      i = ast_count(a1);
      args = GC_MALLOC(i*sizeof(type_t *));
      assign_args(args, a1); /* put inferred types of tuple entries into args array */
      t1 = tuple_type(i, args); /* create tuple type (unique for given entry types) */
      a->type = t1;
      break;
   case AST_LSLOT:
   case AST_SLOT:
      a1 = a->child; /* the root of the slot (identifier, locn, appl or another slot) */
      a2 = a1->next; /* slot name */
      inference(a1);
      t1 = a1->type; /* type of the root */
      if (t1->tag != DATA)
         exception("Attempt to access slot in expression which is not a data type\n");
      else
      {
         i = find_slot(t1, a2->sym); /* find slot index in data type */
         if (i == t1->arity) /* not found */
            exception("Slot not found in data type\n");
         a->type = t1->args[i]; /* infer type of slot */
      }
      break;
   case AST_LOCN:
   case AST_LLOCN:
      a1 = a->child; /* the root of the locn (array name, appl, slot or another locn) */
      a2 = a1->next; /* expression giving the index in the array */
      inference(a2);
      if (a2->type != t_word)
         exception("Array index must be of word type\n");
      inference(a1);
      t1 = a1->type; /* type of root */
      if (t1->tag != ARRAY)
         exception("Attempt to dereference expression which is not of array type\n");
      else
         a->type = t1->params[0]; /* type of locn is parameter type of array type */
      break;
   case AST_APPL:
   case AST_LAPPL:
      a1 = a->child; /* the root of the appl (fn name, locn, slot or another appl) */
      a2 = a1->next; /* list of arguments for function application */
      list_inference(a2);
      inference(a1);
      t1 = a1->type; /* type of root */
      if (t1->tag != GENERIC && t1->tag != CONSTRUCTOR)
         exception("Attempt to call something which isn't a function or constructor\n");
      t2 = find_prototype(t1, a2); /* find matching prototype in generic or constructor */
      if (t2 == NULL) 
         exception("Unable to find function prototype matching given argument types\n");
      a->type = t2->ret; /* type of application is return type of function */
      break;
   default:
      exception("Unknown AST tag in inference\n");
   }
}
