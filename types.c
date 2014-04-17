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

#include "types.h"

type_t * t_nil;
type_t * t_bool;
type_t * t_ZZ;
type_t * t_int;
type_t * t_uint;
type_t * t_double;
type_t * t_string;
type_t * t_char;

type_node_t * tuple_type_list;

type_node_t * array_type_list;

type_t * new_type(char * name, typ_t tag)
{
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = tag;
   t->sym = sym_lookup(name);
   return t;
}

void types_init(void)
{
   t_nil = new_type("nil", NIL);
   t_bool = new_type("bool", NIL);
   t_ZZ = new_type("ZZ", ZZ);
   t_int = new_type("int", INT);
   t_uint = new_type("uint", UINT);
   t_double = new_type("double", DOUBLE);
   t_string = new_type("string", STRING);
   t_char = new_type("char", CHAR);

   tuple_type_list = NULL;
   array_type_list = NULL;
}

type_t * fn_type(type_t * ret, int arity, type_t ** args)
{
   int i;
   
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = FN;
   t->args = (type_t **) GC_MALLOC(sizeof(type_t *)*arity);
   t->ret = ret;
   t->arity = arity;
   
   for (i = 0; i < arity; i++)
      t->args[i] = args[i];

   return t;
}

type_t * generic_type(int arity, type_t ** args)
{
   int i;
   
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = GENERIC;
   t->args = (type_t **) GC_MALLOC(sizeof(type_t *)*arity);
   t->arity = arity;
   
   for (i = 0; i < arity; i++)
      t->args[i] = args[i];

   return t;
}

type_t * constructor_type(sym_t * sym, type_t * type, int arity, type_t ** args)
{
   int i;
   
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = CONSTRUCTOR;
   t->args = (type_t **) GC_MALLOC(sizeof(type_t *)*arity);
   t->arity = arity;
   t->ret = type;
   t->sym = sym;

   for (i = 0; i < arity; i++)
      t->args[i] = args[i];

   return t;
}

type_t * tuple_type(int arity, type_t ** args)
{
   int i;
   
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = TUPLE;
   t->args = (type_t **) GC_MALLOC(sizeof(type_t *)*arity);
   t->arity = arity;

   for (i = 0; i < arity; i++)
      t->args[i] = args[i];

   type_node_t * s = tuple_type_list;

   /* ensure we return a unique tuple type for given arg types */
   while (s != NULL)
   {
      if (s->type->arity == arity)
      {
         for (i = 0; i < arity; i++)
            if (s->type->args[i] != t->args[i]) break;
         
         if (i == arity)
            return s->type;
      }
      s = s->next;
   }

   s = GC_MALLOC(sizeof(type_node_t));
   s->type = t;
   s->next = tuple_type_list;
   tuple_type_list = s;

   return t;
}

type_t * data_type(int arity, type_t ** args, sym_t * sym, 
                       sym_t ** slots, int num_params, type_t ** params)
{
   int i;
   
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = DATA;
   t->args = (type_t **) GC_MALLOC(sizeof(type_t *)*arity);
   t->slots = (sym_t **) GC_MALLOC(sizeof(sym_t *)*arity);
   t->arity = arity;
   t->num_params = num_params;
   t->params = params;

   for (i = 0; i < arity; i++)
   {
       t->args[i] = args[i];
       t->slots[i] = slots[i];
   }
   
   t->sym = sym;

   return t;
}

type_t * array_type(type_t * el_type)
{
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->num_params = 1;
   t->params = (type_t **) GC_MALLOC(sizeof(type_t *)*t->num_params); /* one param */
   t->tag = ARRAY;
   t->params[0] = el_type;
   
   type_node_t * s = array_type_list;

   /* ensure we return a unique array type for given arg types */
   while (s != NULL)
   {
      if (s->type->params[0] == el_type)
         return s->type;
      
      s = s->next;
   }

   s = GC_MALLOC(sizeof(type_node_t));
   s->type = t;
   s->next = array_type_list;
   array_type_list = s;

   return t;
}

/* TODO: should base be made a parameter type? */
type_t * pointer_type(type_t * base)
{
   int i;
   
   type_t * t = (type_t *) GC_MALLOC(sizeof(type_t));
   t->tag = PTR;
   t->ret = base;
   
   return t;
}


void type_print(type_t * type)
{
   int i;
   
   switch (type->tag)
   {
   case BOOL:
      printf("bool");
      break;
   case ZZ:
      printf("ZZ");
      break;
   case INT:
      printf("int");
      break;
   case UINT:
      printf("uint");
      break;
   case DOUBLE:
      printf("double");
      break;
   case CHAR:
      printf("char");
      break;
   case STRING:
      printf("string");
      break;
   case TUPLE:
      printf("(");
      for (i = 0; i < type->arity - 1; i++)
         type_print(type->args[i]), printf(", ");
      type_print(type->args[i]), printf(")");
      break;
   case NIL:
      printf("nil");
      break;
   case DATA:
      printf("%s", type->sym->name);
      break;
   case ARRAY:
      printf("array[");
      type_print(type->params[0]);
      printf("]\n");
      break;
   case PTR:
      printf("pointer<");
      type_print(type->ret);
      printf(">\n");
      break;
   case FN:
      for (i = 0; i < type->arity - 1; i++)
         type_print(type->args[i]), printf(", ");
      if (type->arity)
         type_print(type->args[i]);
      else
         printf("()");
      printf(" -> ");
      type_print(type->ret);
      break;
   case GENERIC:
      printf("generic\n");
      for (i = 0; i < type->arity - 1; i++)
         type_print(type->args[i]), printf("\n");
      if (type->arity)
         type_print(type->args[i]);
      break;
   default:
      exception("Unknown type in type_print\n");
   }
}