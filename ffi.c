/*

Copyright 2014 William Hart. All rights reserved.

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

#include "ffi.h"

type_t * new_foreign_type(sym_t * sym, const char ** fields, type_t ** types, int num_args)
{
   type_t * t;
   
   sym_t ** slots = GC_MALLOC(num_args*sizeof(sym_t *)); /* slot names */
   int i;

   for (i = 0; i < num_args; i++)
      slots[i] = sym_lookup(fields[i]);

   t = data_type(num_args, types, sym, slots, 0, NULL); /* the new type being created */ 
   
   char * llvm = serialise(sym->name);
   
   LLVMTypeRef str = LLVMStructCreateNamed(LLVMGetGlobalContext(), llvm);
                
   t->llvm = llvm;
   
   return t;
}

void new_foreign_function(jit_t * jit, const char * name, type_t * ret, 
                          type_t ** args, int num)
{
   int i;
   LLVMTypeRef * fn_args = GC_malloc(num*sizeof(LLVMTypeRef));
   LLVMTypeRef fn_ret, fn_type;

   for (i = 0; i < num; i++)
      fn_args[i] = type_to_llvm(jit, args[i]);

   fn_ret = type_to_llvm(jit, ret);

   fn_type = LLVMFunctionType(fn_ret, fn_args, num, 0);
   LLVMAddFunction(jit->module, name, fn_type);
}

/******************************************************************************

   Bignum (ZZ) interface - flint fmpz

******************************************************************************/

void ZZ_init(jit_t * jit)
{
   const char * ZZ_fields[1] = { "fmpz" };
   type_t * ZZ_types[1] = { t_int };
   
   sym_t * name = sym_lookup("ZZ");
   t_ZZ = new_foreign_type(name, ZZ_fields, ZZ_types, 1);
   
   type_t * constr = constructor_type(name, t_ZZ, 0, NULL); /* generic constructor */

   bind_t * bind = bind_symbol(name, constr, NULL); /* bind new type name to generic constructor */
   type_to_llvm(jit, t_ZZ);

   type_t * args[3] = { reference_type(t_ZZ), NULL, NULL };

   type_t * f1 = fn_type(t_nil, 1, args); /* the empty constructor function */
   new_foreign_function(jit, "__fmpz_init", t_nil, args, 1);
   f1->llvm = "__fmpz_init";
   new_foreign_function(jit, "__fmpz_clear", t_nil, args, 1);
   
   args[1] = t_uint;
   type_t * f2 = fn_type(t_nil, 2, args); /* the int constructor function */
   new_foreign_function(jit, "__fmpz_init_set_ui", t_nil, args, 2);
   f2->llvm = "__fmpz_init_set_ui";
   
   args[1] = t_string;
   args[2] = t_int;
   type_t * f3 = fn_type(t_nil, 3, args); /* the string constructor function */
   new_foreign_function(jit, "fmpz_set_str", t_nil, args, 3);
   f3->llvm = "fmpz_set_str";
   
   args[1] = reference_type(t_ZZ);
   type_t * f4 = fn_type(t_nil, 2, args); /* the copy constructor */
   new_foreign_function(jit, "__fmpz_init_set", t_nil, args, 2);
   f4->llvm = "__fmpz_init_set";
   
   generic_insert(constr, f1);
   generic_insert(constr, f2);
   generic_insert(constr, f3);
   generic_insert(constr, f4);
   
   type_t * fns[4] = { f1, f2, f3, f4 };

   f1 = fn_type(t_nil, 1, args); /* finaliser */
   f1->llvm = "__fmpz_clear";
   fns[0] = f1;
   t_finalizer = generic_type(1, fns); 
   bind_symbol(sym_lookup("finalizer"), t_finalizer, NULL);

   args[1] = reference_type(t_ZZ);
   args[2] = reference_type(t_ZZ);
   new_foreign_function(jit, "fmpz_add", t_nil, args, 3);
   new_foreign_function(jit, "fmpz_sub", t_nil, args, 3);
   new_foreign_function(jit, "fmpz_mul", t_nil, args, 3);
   new_foreign_function(jit, "fmpz_fdiv_q", t_nil, args, 3);
   new_foreign_function(jit, "fmpz_mod", t_nil, args, 3);
 
   args[0] = reference_type(t_ZZ);
   args[1] = reference_type(t_ZZ);
   
   f1 = fn_type(t_ZZ, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "fmpz_add";
   bind = find_symbol(sym_lookup("+"));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_ZZ, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "fmpz_sub";
   bind = find_symbol(sym_lookup("-"));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_ZZ, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "fmpz_mul";
   bind = find_symbol(sym_lookup("*"));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_ZZ, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "fmpz_fdiv_q";
   bind = find_symbol(sym_lookup("/"));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_ZZ, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "fmpz_mod";
   bind = find_symbol(sym_lookup("%"));
   generic_insert(bind->type, f1);

   new_foreign_function(jit, "fmpz_set", t_nil, args, 2);
   
   f1 = fn_type(t_nil, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "fmpz_set";
   bind = find_symbol(sym_lookup("="));
   generic_insert(bind->type, f1);

   new_foreign_function(jit, "__fmpz_lt", t_bool, args, 2);
   new_foreign_function(jit, "__fmpz_gt", t_bool, args, 2);
   new_foreign_function(jit, "__fmpz_lte", t_bool, args, 2);
   new_foreign_function(jit, "__fmpz_gte", t_bool, args, 2);
   new_foreign_function(jit, "__fmpz_eq", t_bool, args, 2);
   new_foreign_function(jit, "__fmpz_neq", t_bool, args, 2);

   f1 = fn_type(t_bool, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "__fmpz_lt";
   bind = find_symbol(sym_lookup("<"));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_bool, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "__fmpz_gt";
   bind = find_symbol(sym_lookup(">"));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_bool, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "__fmpz_lte";
   bind = find_symbol(sym_lookup("<="));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_bool, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "__fmpz_gte";
   bind = find_symbol(sym_lookup("<="));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_bool, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "__fmpz_eq";
   bind = find_symbol(sym_lookup("=="));
   generic_insert(bind->type, f1);

   f1 = fn_type(t_bool, 2, args);
   f1->intrinsic = 1;
   f1->llvm = "__fmpz_neq";
   bind = find_symbol(sym_lookup("!="));
   generic_insert(bind->type, f1);
}
