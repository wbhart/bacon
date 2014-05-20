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

#include "environment.h"

env_t * current_scope;

void scope_init(void)
{
   current_scope = (env_t *) GC_MALLOC(sizeof(env_t));
}

void intrinsics_init(void)
{
   int i;

   type_t ** args = GC_MALLOC(2*sizeof(type_t *));
   type_t ** fns = GC_MALLOC(3*sizeof(type_t *));
   
   type_t * type_list[3] = { t_int, t_uint, t_double };

   for (i = 0; i < 3; i++)
   {
      args[1] = args[0] = type_list[i];
      
      fns[i] = fn_type(type_list[i], 2, args);
      fns[i]->intrinsic = 1;
   }

   args[1] = args[0] = type_list[3];
      
   bind_generic(sym_lookup("+"), generic_type(3, fns)); 
   bind_generic(sym_lookup("-"), generic_type(3, fns));
   bind_generic(sym_lookup("*"), generic_type(3, fns));
   bind_generic(sym_lookup("/"), generic_type(3, fns));
   bind_generic(sym_lookup("%"), generic_type(3, fns));

   for (i = 0; i < 3; i++)
   {
      args[1] = args[0] = type_list[i];
      
      fns[i] = fn_type(t_bool, 2, args);
      fns[i]->intrinsic = 1;
   }

   bind_generic(sym_lookup("=="), generic_type(3, fns));
   bind_generic(sym_lookup("!="), generic_type(3, fns));
   bind_generic(sym_lookup("<="), generic_type(3, fns));
   bind_generic(sym_lookup(">="), generic_type(3, fns));
   bind_generic(sym_lookup("<"), generic_type(3, fns));
   bind_generic(sym_lookup(">"), generic_type(3, fns));

   for (i = 0; i < 3; i++)
   {
      args[1] = args[0] = type_list[i];
      
      fns[i] = fn_type(t_nil, 2, args);
      fns[i]->intrinsic = 1;
   }

   t_assignment = generic_type(3, fns);
   bind_generic(sym_lookup("="), t_assignment);
   
   bind_symbol(sym_lookup("nil"), t_nil, NULL);
   bind_symbol(sym_lookup("int"), t_int, NULL);
   bind_symbol(sym_lookup("uint"), t_uint, NULL);
   bind_symbol(sym_lookup("bool"), t_bool, NULL);
   bind_symbol(sym_lookup("double"), t_double, NULL);
   bind_symbol(sym_lookup("string"), t_string, NULL);
   bind_symbol(sym_lookup("char"), t_char, NULL);
}

bind_t * bind_generic(sym_t * sym, type_t * type)
{
   bind_t * scope = current_scope->scope;
   bind_t * b = (bind_t *) GC_MALLOC(sizeof(bind_t));
   b->sym = sym;
   b->type = type;
   b->next = scope;
   current_scope->scope = b;
   return b;
}

void generic_insert(type_t * gen, type_t * fn)
{
   gen->args = GC_REALLOC(gen->args, (gen->arity + 1)*sizeof(type_t *));
   gen->args[gen->arity++] = fn;
}

bind_t * bind_symbol(sym_t * sym, type_t * type, char * llvm)
{
   bind_t * scope = current_scope->scope;
   bind_t * b = (bind_t *) GC_MALLOC(sizeof(bind_t));
   b->sym = sym;
   b->type = type;
   b->llvm = llvm;
   b->next = scope;
   current_scope->scope = b;
   return b;
}

bind_t * find_symbol(sym_t * sym)
{
   env_t * s = current_scope;
   bind_t * b;

   while (s != NULL)
   {
      b = s->scope;
 
      while (b != NULL)
      {
         if (b->sym == sym)
            return b;
         b = b->next;
      }
      
      s = s->next;
   }

   return NULL;
}

bind_t * find_symbol_in_current_scope(sym_t * sym)
{
   bind_t * b = current_scope->scope;
 
   while (b != NULL)
   {
      if (b->sym == sym)
         return b;
      b = b->next;
   }

   return NULL;
}

env_t * scope_up(void)
{
   env_t * env = (env_t *) GC_MALLOC(sizeof(env_t));
   env->next = current_scope;
   current_scope = env;
   return current_scope;
}

void scope_down(void)
{
   current_scope = current_scope->next;
}

int scope_is_global(bind_t * bind)
{
   env_t * s = current_scope;
   while (s->next != NULL)
      s = s->next;

   bind_t * b = s->scope;
   while (b != NULL)
   {
      if (b == bind)
         return 1;
      b = b->next;
   }
   return 0;
}
