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

#include "backend.h"

#define CS_MALLOC "GC_malloc"
#define CS_REALLOC "GC_realloc"
#define CS_MALLOC_ATOMIC "GC_malloc_atomic"

/**********************************************************************

   Hash table for local identifiers that LLVM won't let us serialise

**********************************************************************/

loc_t ** loc_tab;

void loc_tab_init(void)
{
   long i;
   
   loc_tab = (loc_t **) GC_MALLOC(LOC_TAB_SIZE*sizeof(loc_t *));
}

loc_t * new_loc(const char * name, int length, LLVMValueRef llvm_val)
{
   loc_t * loc = (loc_t *) GC_MALLOC(sizeof(loc_t));
   loc->name = (char *) GC_MALLOC(length + 1);
   strcpy(loc->name, name);
   loc->llvm_val = llvm_val;
   return loc;
}

int loc_hash(const char * name, int length)
{
    int hash = (int) name[0];
    int i;
    for (i = 1; i < length; i++)
        hash += (name[i] << ((3*i) % 15));
    return hash % LOC_TAB_SIZE;
}

void loc_insert(const char * name, LLVMValueRef llvm_val)
{
   int length = strlen(name);
   int hash = loc_hash(name, length);
   loc_t * loc;

   while (loc_tab[hash])
   {
       hash++;
       if (hash == LOC_TAB_SIZE)
           hash = 0;
   }

   loc = new_loc(name, length, llvm_val);
   loc_tab[hash] = loc;
}

LLVMValueRef loc_lookup(const char * name)
{
   int length = strlen(name);
   int hash = loc_hash(name, length);
   
   while (loc_tab[hash])
   {
       if (strcmp(loc_tab[hash]->name, name) == 0)
           return loc_tab[hash]->llvm_val;
       hash++;
       if (hash == LOC_TAB_SIZE)
           hash = 0;
   }

   return NULL;
}

/**********************************************************************

   LLVM Jit backend

**********************************************************************/

/* 
   Tell LLVM about some external library functions so we can call them 
   and about some constants we want to use from jit'd code
*/
void llvm_functions(jit_t * jit)
{
   LLVMTypeRef args[2];
   LLVMTypeRef fntype; 
   LLVMTypeRef ret;
   LLVMValueRef fn;

   /* patch in the printf function */
   args[0] = LLVMPointerType(LLVMInt8Type(), 0);
   ret = LLVMWordType();
   fntype = LLVMFunctionType(ret, args, 1, 1);
   fn = LLVMAddFunction(jit->module, "printf", fntype);

   /* patch in the exit function */
   args[0] = LLVMWordType();
   ret = LLVMVoidType();
   fntype = LLVMFunctionType(ret, args, 1, 0);
   fn = LLVMAddFunction(jit->module, "exit", fntype);

   /* patch in the GC_malloc function */
   args[0] = LLVMWordType();
   ret = LLVMPointerType(LLVMInt8Type(), 0);
   fntype = LLVMFunctionType(ret, args, 1, 0);
   fn = LLVMAddFunction(jit->module, CS_MALLOC, fntype);
   LLVMAddFunctionAttr(fn, LLVMNoAliasAttribute);

   /* patch in the GC_realloc function */
   args[0] = LLVMPointerType(LLVMInt8Type(), 0);
   args[1] = LLVMWordType();
   ret = LLVMPointerType(LLVMInt8Type(), 0);
   fntype = LLVMFunctionType(ret, args, 2, 0);
   fn = LLVMAddFunction(jit->module, CS_REALLOC, fntype);
   LLVMAddFunctionAttr(fn, LLVMNoAliasAttribute);

   /* patch in the GC_malloc_atomic function */
   args[0] = LLVMWordType();
   ret = LLVMPointerType(LLVMInt8Type(), 0);
   fntype = LLVMFunctionType(ret, args, 1, 0);
   fn = LLVMAddFunction(jit->module, CS_MALLOC_ATOMIC, fntype);
   LLVMAddFunctionAttr(fn, LLVMNoAliasAttribute);
}

/*
   Initialise the LLVM JIT
*/
jit_t * llvm_init(void)
{
    char * error = NULL;
    
    /* create jit struct */
    jit_t * jit = (jit_t *) GC_MALLOC(sizeof(jit_t));

    /* Jit setup */
    LLVMLinkInJIT();
    LLVMInitializeNativeTarget();

    /* Create module */
    jit->module = LLVMModuleCreateWithName("cesium");

    /* Create JIT engine */
    if (LLVMCreateJITCompilerForModule(&(jit->engine), jit->module, 2, &error) != 0) 
    {   
       fprintf(stderr, "%s\n", error);  
       LLVMDisposeMessage(error);  
       abort();  
    } 
   
    /* Create optimisation pass pipeline */
    jit->pass = LLVMCreateFunctionPassManagerForModule(jit->module);  
    LLVMAddTargetData(LLVMGetExecutionEngineTargetData(jit->engine), jit->pass);  
    LLVMAddAggressiveDCEPass(jit->pass); /* */
    LLVMAddDeadStoreEliminationPass(jit->pass); 
    LLVMAddIndVarSimplifyPass(jit->pass); 
    LLVMAddJumpThreadingPass(jit->pass); 
    LLVMAddLICMPass(jit->pass); 
    LLVMAddLoopDeletionPass(jit->pass); 
    LLVMAddLoopRotatePass(jit->pass); 
    LLVMAddLoopUnrollPass(jit->pass); 
    LLVMAddLoopUnswitchPass(jit->pass);
    LLVMAddMemCpyOptPass(jit->pass); 
    LLVMAddReassociatePass(jit->pass); 
    LLVMAddSCCPPass(jit->pass); 
    LLVMAddScalarReplAggregatesPass(jit->pass); 
    LLVMAddSimplifyLibCallsPass(jit->pass);
    LLVMAddTailCallEliminationPass(jit->pass); 
    LLVMAddDemoteMemoryToRegisterPass(jit->pass); /* */ 
    LLVMAddConstantPropagationPass(jit->pass);  
    LLVMAddInstructionCombiningPass(jit->pass);  
    LLVMAddPromoteMemoryToRegisterPass(jit->pass);  
    LLVMAddGVNPass(jit->pass);  
    LLVMAddCFGSimplificationPass(jit->pass);

    /* patch in some external functions */
    llvm_functions(jit);

    return jit;
}

/*
   If something goes wrong after partially jit'ing something we need
   to clean up.
*/
void llvm_reset(jit_t * jit)
{
    if (jit->function)
       LLVMDeleteFunction(jit->function);
    if (jit->builder)
       LLVMDisposeBuilder(jit->builder);
    jit->function = NULL;
    jit->builder = NULL;
}

/*
   Clean up LLVM on exit from Cesium
*/
void llvm_cleanup(jit_t * jit)
{
    /* Clean up */
    LLVMDisposePassManager(jit->pass);  
    LLVMDisposeExecutionEngine(jit->engine); 
    jit->pass = NULL;
    jit->engine = NULL;
    jit->module = NULL;
}

/* 
   Raise an exception but clean up jit first 
*/
void jit_exception(jit_t * jit, const char * msg)
{
   llvm_reset(jit);
   
   exception(msg);
}

/*
   Return 1 if type is atomic (i.e. contains no pointers)
*/
int is_atomic(type_t * type)
{
   typ_t t = type->tag;
   return (t != ARRAY && t != TUPLE && t != DATA 
        && t != FN);
}

/*
   Return 1 if the type is structured.
*/
int is_structured(type_t * type)
{
   typ_t t = type->tag;
   return (t == ARRAY || t == TUPLE || t == DATA);
}

/* 
   Jit a call to GC_malloc
*/
LLVMValueRef LLVMBuildGCMalloc(jit_t * jit, LLVMTypeRef type, const char * name, int atomic)
{
    LLVMValueRef fn;
    if (atomic)
        fn = LLVMGetNamedFunction(jit->module, CS_MALLOC_ATOMIC);
    else
        fn = LLVMGetNamedFunction(jit->module, CS_MALLOC);
    LLVMValueRef arg[1] = { LLVMSizeOf(type) };
    LLVMValueRef gcmalloc = LLVMBuildCall(jit->builder, fn, arg, 1, "malloc");
    return LLVMBuildPointerCast(jit->builder, gcmalloc, LLVMPointerType(type, 0), name);
}

/* 
   Jit a call to GC_realloc
*/
LLVMValueRef LLVMBuildGCRealloc(jit_t * jit, LLVMValueRef ptr, LLVMTypeRef type, const char * name)
{
    LLVMValueRef fn = LLVMGetNamedFunction(jit->module, CS_REALLOC);
    LLVMValueRef args[2] = { ptr, LLVMSizeOf(type) };
    LLVMValueRef gcrealloc = LLVMBuildCall(jit->builder, fn, args, 2, "realloc");
    return LLVMBuildPointerCast(jit->builder, gcrealloc, LLVMPointerType(type, 0), name);
}

/* 
   Jit a call to GC_malloc to create an array
*/
LLVMValueRef LLVMBuildGCArrayMalloc(jit_t * jit, type_t * t, LLVMValueRef num, const char * name, int atomic)
{
    LLVMValueRef fn;
    if (atomic)
        fn = LLVMGetNamedFunction(jit->module, CS_MALLOC_ATOMIC);
    else
        fn = LLVMGetNamedFunction(jit->module, CS_MALLOC);
    
    LLVMTypeRef type = type_to_llvm(jit, t);
    
    LLVMValueRef size = LLVMSizeOf(type);
    LLVMValueRef arg[1] = { LLVMBuildMul(jit->builder, num, size, "arr_size") };
    LLVMValueRef gcmalloc = LLVMBuildCall(jit->builder, fn, arg, 1, "malloc");
    return LLVMBuildPointerCast(jit->builder, gcmalloc, LLVMPointerType(type, 0), name);
}

/* 
   Jit a call to GC_realloc to reallocate an array
*/
LLVMValueRef LLVMBuildGCArrayRealloc(jit_t * jit, type_t * t, LLVMValueRef arr, LLVMValueRef num, const char * name)
{
    LLVMValueRef fn = LLVMGetNamedFunction(jit->module, CS_REALLOC);
    
    LLVMTypeRef type = type_to_llvm(jit, t);
    
    LLVMValueRef size = LLVMSizeOf(type);
    LLVMValueRef args[2] = { LLVMBuildPointerCast(jit->builder, arr, LLVMPointerType(LLVMInt8Type(), 0), "arr"), 
       LLVMBuildMul(jit->builder, num, size, "arr_size") };
    LLVMValueRef gcrealloc = LLVMBuildCall(jit->builder, fn, args, 2, "realloc");
    return LLVMBuildPointerCast(jit->builder, gcrealloc, LLVMPointerType(type, 0), name);
}

LLVMValueRef AddLocal(jit_t * jit, LLVMTypeRef type, char * name)
{
   LLVMBasicBlockRef second = LLVMGetFirstBasicBlock(jit->function);
   LLVMBasicBlockRef first = LLVMInsertBasicBlock(second, serialise("decl"));
   LLVMBuilderRef builder = LLVMCreateBuilder();
   LLVMPositionBuilderAtEnd(builder, first);
   LLVMValueRef val = LLVMBuildAlloca(builder, type, name);
   LLVMBuildBr(builder, second);
   LLVMDisposeBuilder(builder);
   loc_insert(name, val);
   
   return val;
}

LLVMValueRef create_var(jit_t * jit, sym_t * sym, char * llvm, type_t * t)
{
   LLVMValueRef val;
   
   bind_t * bind = find_symbol(sym);
   if (bind == NULL) /* symbol is not bound yet */
      bind = bind_symbol(sym, t, llvm);
   else
      bind->llvm = llvm;

   LLVMTypeRef type = type_to_llvm(jit, t); /* convert to llvm type */
      
   if (scope_is_global(bind))
   {
      val = LLVMAddGlobal(jit->module, type, llvm);
      LLVMSetInitializer(val, LLVMGetUndef(type));
   } else
      val = AddLocal(jit, type, llvm);
   
   return val;
}

/* 
   Build llvm struct type from ordinary tuple type
*/
LLVMTypeRef tuple_to_llvm(jit_t * jit, type_t * type)
{
    int params = type->arity;
    int i;

    /* get parameter types */
    LLVMTypeRef * args = (LLVMTypeRef *) GC_MALLOC(params*sizeof(LLVMTypeRef));
    for (i = 0; i < params; i++)
        args[i] = type_to_llvm(jit, type->args[i]); 

    /* make LLVM struct type */
    return LLVMStructType(args, params, 1);
}

/* 
   Build llvm struct type from array type 
*/
LLVMTypeRef array_to_llvm(jit_t * jit, type_t * type)
{
    int i;

    /* get parameter types */
    LLVMTypeRef * args = (LLVMTypeRef *) GC_MALLOC(2*sizeof(LLVMTypeRef));
    args[0] = LLVMPointerType(type_to_llvm(jit, type->params[0]), 0); 
    args[1] = LLVMWordType();

    /* make LLVM struct type */
    return LLVMStructType(args, 2, 1);
}

/* 
   Convert a type_t to an LLVMTypeRef 
*/
LLVMTypeRef type_to_llvm(jit_t * jit, type_t * type)
{
   if (type == t_nil)
      return LLVMVoidType();
   else if (type == t_int || type == t_uint)
      return LLVMWordType();
   else if (type == t_double)
      return LLVMDoubleType();
   else if (type == t_char)
      return LLVMInt8Type();
   else if (type == t_string)
      return LLVMPointerType(LLVMInt8Type(), 0);
   else if (type == t_bool)
      return LLVMInt1Type();
   else if (type->tag == TUPLE)
      return tuple_to_llvm(jit, type);
   else if (type->tag == ARRAY)
      return array_to_llvm(jit, type);
   else if (type->tag == DATA)
   {
      LLVMTypeRef t = LLVMGetTypeByName(jit->module, type->llvm);

      bind_t * bind = find_symbol(type->sym);
       
      if (bind != NULL && bind->llvm == NULL) /* type bound but not yet defined in LLVM */
      {
         int i, count = type->arity;
         LLVMTypeRef * types = GC_MALLOC(count*sizeof(LLVMTypeRef));
         for (i = 0; i < count; i++)
            types[i] = type_to_llvm(jit, type->args[i]);
         LLVMStructSetBody(t, types, count, 0);
         bind->llvm = type->llvm;
      }
      
      return t;
   }
   else if (type->tag == PTR || type->tag == REF)
      return LLVMPointerType(type_to_llvm(jit, type->ret), 0);
   else
      jit_exception(jit, "Unknown type in type_to_llvm\n");
}

/* 
   Convert a type_t to an LLVMTypeRef with an extra level of indirection
   for structs
*/
LLVMTypeRef type_to_generic_llvm(jit_t * jit, type_t * type)
{
   if (type->tag == DATA || type->tag == TUPLE || type->tag == ARRAY)
      return LLVMPointerType(type_to_llvm(jit, type), 0);
   else
      return type_to_llvm(jit, type);
}

/*
   Create a return struct for return from jit'ing an AST node
*/
ret_t * ret(int closed, LLVMValueRef val)
{
   ret_t * ret = GC_MALLOC(sizeof(ret_t));
   ret->closed = closed;
   ret->val = val;
   return ret;
}

/*
   Jit a ZZ literal
*/
ret_t * exec_ZZ(jit_t * jit, ast_t * ast)
{
   char * str = ast->sym->name; /* the integer represented as a string */
   fmpz * temp = GC_malloc(sizeof(fmpz));
   LLVMValueRef val;

   fmpz_init(temp);
   fmpz_set_str(temp, str, 10);
   
   LLVMValueRef field[1] = { LLVMConstInt(LLVMWordType(), (slong) *temp, 0) };
   val = LLVMConstNamedStruct(type_to_llvm(jit, t_ZZ), field, 1);
   LLVMValueRef loc = LLVMAddGlobal(jit->module, type_to_llvm(jit, t_ZZ), "__cs_ZZ");
   LLVMSetInitializer(loc, val);

   return ret(0, loc);
}

/*
   Jit an int literal
*/
ret_t * exec_int(jit_t * jit, ast_t * ast)
{
    long num = atol(ast->sym->name);
    
    LLVMValueRef val = LLVMConstInt(LLVMWordType(), num, 1);

    return ret(0, val);
}

/*
   Jit a uint literal
*/
ret_t * exec_uint(jit_t * jit, ast_t * ast)
{
    unsigned long num = strtoul(ast->sym->name, NULL, 10);
    
    LLVMValueRef val = LLVMConstInt(LLVMWordType(), num, 0);

    return ret(0, val);
}

/*
   Jit a double literal
*/
ret_t * exec_double(jit_t * jit, ast_t * ast)
{
    double num = atof(ast->sym->name);
    
    LLVMValueRef val = LLVMConstReal(LLVMDoubleType(), num);

    return ret(0, val);
}

/*
   Jit a char literal
*/
ret_t * exec_char(jit_t * jit, ast_t * ast)
{
    char c = ast->sym->name[0];

    if (c == '\\')
    {
       switch (ast->sym->name[1])
       {
       case '\'':
          c = '\'';
          break;
       case '\"':
          c = '\"';
          break;
       case '\\':
          c = '\\';
          break;
       case '0':
          c = '\0';
          break;
       case 'n':
          c = '\n';
          break;
       case 'r':
          c = '\r';
          break;
       case 't':
          c = '\t';
          break;
       default:
          jit_exception(jit, "Unknown char escape character in exec_char\n");
       }
    }

    LLVMValueRef val = LLVMConstInt(LLVMInt8Type(), c, 0);;

    return ret(0, val);
}

/*
   Jit a string literal
*/
ret_t * exec_string(jit_t * jit, ast_t * ast)
{
    char * str = ast->sym->name;
    
    LLVMValueRef val = LLVMBuildGlobalStringPtr(jit->builder, str, "string");

    return ret(0, val);
}

/*
   Jit a binary operation involving a ZZ
*/
ret_t * exec_binary_data(jit_t * jit, ast_t * ast, int cleanup)
{
    ast_t * expr1 = ast->child;                          
    ast_t * expr2 = expr1->next;                         

    ret_t * ret1 = exec_ast(jit, expr1);               
    ret_t * ret2 = exec_ast(jit, expr2);              

    bind_t * bind = find_symbol(ast->sym);
    type_t * op = find_prototype(bind->type, ast->child);

    if (op == NULL)
       exception("Unable to find binary operation for data type\n");

    if (op->intrinsic)
    {
       char * llvm = serialise("__cs_temp");

       LLVMValueRef val;
       
       if (cleanup)
          val = create_var(jit, sym_lookup(llvm), llvm, op->ret);
       else
          val = AddLocal(jit, type_to_llvm(jit, op->ret), llvm);

       if (requires_constructor(op->ret))
          call_constructors(jit, val, op->ret);

       LLVMValueRef fn = LLVMGetNamedFunction(jit->module, op->llvm);

       LLVMValueRef arg[3] = { val, ret1->val, ret2->val };

       LLVMBuildCall(jit->builder, fn, arg, 3, "");
       
       return ret(0, val);
    } else
       exception("Non intrinsic binary ops involving datatypes not implemented yet\n");
}

/*
   We have a number of binary ops we want to jit and they
   all look the same, so define macros for them.
*/
#define exec_binary(__name, __fop, __iop, __str)              \
__name(jit_t * jit, ast_t * ast, int cleanup)                 \
{                                                             \
    ast_t * expr1 = ast->child;                               \
    ast_t * expr2 = expr1->next;                              \
                                                              \
    if (expr1->type->tag == DATA || expr2->type->tag == DATA) \
       return exec_binary_data(jit, ast, cleanup);            \
                                                              \
    ret_t * ret1 = exec_ast(jit, expr1);                      \
    ret_t * ret2 = exec_ast(jit, expr2);                      \
                                                              \
    LLVMValueRef v1 = ret1->val, v2 = ret2->val, val;         \
                                                              \
    if (expr1->type == t_double)                              \
       val = __fop(jit->builder, v1, v2, __str);              \
    else                                                      \
       val = __iop(jit->builder, v1, v2, __str);              \
                                                              \
    return ret(0, val);                                       \
}

/* 
   Jit add, sub, .... ops 
*/
ret_t * exec_binary(exec_plus, LLVMBuildFAdd, LLVMBuildAdd, "add")

ret_t * exec_binary(exec_minus, LLVMBuildFSub, LLVMBuildSub, "sub")

ret_t * exec_binary(exec_times, LLVMBuildFMul, LLVMBuildMul, "times")

ret_t * exec_binary(exec_div, LLVMBuildFDiv, LLVMBuildSDiv, "div")

ret_t * exec_binary(exec_mod, LLVMBuildFRem, LLVMBuildSRem, "mod")

/*
   Jit a binary relation involving a ZZ
*/
ret_t * exec_binary_rel_data(jit_t * jit, ast_t * ast)
{
    ast_t * expr1 = ast->child;                          
    ast_t * expr2 = expr1->next;                         

    ret_t * ret1 = exec_ast(jit, expr1);               
    ret_t * ret2 = exec_ast(jit, expr2);              

    bind_t * bind = find_symbol(ast->sym);
    type_t * op = find_prototype(bind->type, ast->child);

    if (op == NULL)
       exception("Unable to find binary relation for datatype\n");

    if (op->intrinsic)
    {
       LLVMValueRef fn = LLVMGetNamedFunction(jit->module, op->llvm);

       LLVMValueRef arg[2] = { ret1->val, ret2->val };

       LLVMValueRef val = LLVMBuildCall(jit->builder, fn, arg, 2, "");

       return ret(0, val);
    } else
       exception("Non intrinsic binary rels involving datatypes not implemented yet\n");
}

/*
   We have a number of binary rels we want to jit and they
   all look the same, so define macros for them.
*/

#define exec_binary_rel(__name, __fop, __frel, __iop, __irel, __str)  \
__name(jit_t * jit, ast_t * ast)                                      \
{                                                                     \
    ast_t * expr1 = ast->child;                                       \
    ast_t * expr2 = expr1->next;                                      \
                                                                      \
    if (expr1->type->tag == DATA || expr2->type->tag == DATA)         \
    return exec_binary_rel_data(jit, ast);                            \
                                                                      \
    ret_t * ret1 = exec_ast(jit, expr1);                              \
    ret_t * ret2 = exec_ast(jit, expr2);                              \
                                                                      \
    LLVMValueRef v1 = ret1->val, v2 = ret2->val, val;                 \
                                                                      \
    if (expr1->type == t_double)                                      \
       val = __fop(jit->builder, __frel, v1, v2, __str);              \
    else                                                              \
       val = __iop(jit->builder, __irel, v1, v2, __str);              \
                                                                      \
    return ret(0, val);                                               \
}

/* 
   Jit eq, neq, lt, gt, .... rels 
*/

ret_t * exec_binary_rel(exec_le, LLVMBuildFCmp, LLVMRealOLE, LLVMBuildICmp, LLVMIntSLE, "le")

ret_t * exec_binary_rel(exec_ge, LLVMBuildFCmp, LLVMRealOGE, LLVMBuildICmp, LLVMIntSGE, "ge")

ret_t * exec_binary_rel(exec_lt, LLVMBuildFCmp, LLVMRealOLT, LLVMBuildICmp, LLVMIntSLT, "lt")

ret_t * exec_binary_rel(exec_gt, LLVMBuildFCmp, LLVMRealOGT, LLVMBuildICmp, LLVMIntSGT, "gt")

ret_t * exec_binary_rel(exec_eq, LLVMBuildFCmp, LLVMRealOEQ, LLVMBuildICmp, LLVMIntEQ, "eq")

ret_t * exec_binary_rel(exec_ne, LLVMBuildFCmp, LLVMRealONE, LLVMBuildICmp, LLVMIntNE, "ne")

/* 
   Dispatch to various binary operations 
*/
ret_t * exec_binop(jit_t * jit, ast_t * ast, int cleanup)
{
    if (ast->sym == sym_lookup("+"))
        return exec_plus(jit, ast, cleanup);

    if (ast->sym == sym_lookup("-"))
        return exec_minus(jit, ast, cleanup);

    if (ast->sym == sym_lookup("*"))
        return exec_times(jit, ast, cleanup);

    if (ast->sym == sym_lookup("/"))
        return exec_div(jit, ast, cleanup);

    if (ast->sym == sym_lookup("%"))
        return exec_mod(jit, ast, cleanup);

    if (ast->sym == sym_lookup("=="))
        return exec_eq(jit, ast);

    if (ast->sym == sym_lookup("!="))
        return exec_ne(jit, ast);

    if (ast->sym == sym_lookup("<="))
        return exec_le(jit, ast);

    if (ast->sym == sym_lookup(">="))
        return exec_ge(jit, ast);

    if (ast->sym == sym_lookup("<"))
        return exec_lt(jit, ast);

    if (ast->sym == sym_lookup(">"))
        return exec_gt(jit, ast);

    jit_exception(jit, "Unknown symbol in binop\n");
}

/*
   Jit a block of statements
*/
ret_t * exec_block(jit_t * jit, ast_t * ast)
{
    ast_t * c = ast->child;
    ret_t * c_ret = ret(0, NULL);
    env_t * scope_save;
   
    if (ast->tag == AST_BLOCK)
    {
       scope_save = current_scope;
       current_scope = ast->env;
    }

    while (c != NULL)
    {
        c_ret = exec_ast(jit, c);
                    
        c = c->next;
    }

    if (ast->tag == AST_BLOCK)
    {
       /*if (find_symbol_in_current_scope(sym_lookup("return")) == NULL)*/
       if (c_ret->closed == 0)
          exec_destructors(jit, NULL);
       current_scope = scope_save;
    }

    return c_ret;
}

/*
   Jit an if..else expression
*/
ret_t * exec_if_else_expr(jit_t * jit, ast_t * ast)
{
    ast_t * exp = ast->child;
    ast_t * con = exp->next;
    ast_t * alt = con->next;
    
    ret_t * exp_ret, * con_ret, * alt_ret;
    LLVMValueRef val;

    LLVMBasicBlockRef i = LLVMAppendBasicBlock(jit->function, "if");
    LLVMBasicBlockRef b1 = LLVMAppendBasicBlock(jit->function, "ifbody");
    LLVMBasicBlockRef b2 = LLVMAppendBasicBlock(jit->function, "elsebody");
    LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "ifend");

    LLVMBuildBr(jit->builder, i);
    LLVMPositionBuilderAtEnd(jit->builder, i);  
    
    exp_ret = exec_ast(jit, exp); /* expression */

    LLVMValueRef tmp = LLVMBuildAlloca(jit->builder, type_to_llvm(jit, ast->type), "ifexpr");
    
    LLVMBuildCondBr(jit->builder, exp_ret->val, b1, b2);
    LLVMPositionBuilderAtEnd(jit->builder, b1); 
   
    con_ret = exec_ast(jit, con); /* stmt1 */
    LLVMBuildStore(jit->builder, con_ret->val, tmp);

    LLVMBuildBr(jit->builder, e);

    LLVMPositionBuilderAtEnd(jit->builder, b2);  

    alt_ret = exec_ast(jit, alt); /* stmt2 */
    LLVMBuildStore(jit->builder, alt_ret->val, tmp);

    LLVMBuildBr(jit->builder, e);

    LLVMPositionBuilderAtEnd(jit->builder, e); 
    
    val = LLVMBuildLoad(jit->builder, tmp, "if_else_tmp");
      
    return ret(0, val);
}

/*
   Jit an if..else statement
*/
ret_t * exec_if_else_stmt(jit_t * jit, ast_t * ast)
{
    ast_t * exp = ast->child;
    ast_t * con = exp->next;
    ast_t * alt = con->next;
    
    ret_t * exp_ret, * con_ret, * alt_ret;

    LLVMBasicBlockRef i = LLVMAppendBasicBlock(jit->function, "if");
    LLVMBasicBlockRef b1 = LLVMAppendBasicBlock(jit->function, "ifbody");
    LLVMBasicBlockRef b2 = LLVMAppendBasicBlock(jit->function, "elsebody");
    LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "ifend");

    LLVMBuildBr(jit->builder, i);
    LLVMPositionBuilderAtEnd(jit->builder, i);  
    
    exp_ret = exec_ast(jit, exp); /* expression */

    LLVMBuildCondBr(jit->builder, exp_ret->val, b1, b2);
    LLVMPositionBuilderAtEnd(jit->builder, b1); 
   
    con_ret = exec_ast(jit, con); /* stmt1 */
    
    if (!con_ret->closed)
       LLVMBuildBr(jit->builder, e);

    LLVMPositionBuilderAtEnd(jit->builder, b2);  

    alt_ret = exec_ast(jit, alt); /* stmt2 */
    
    if (!alt_ret->closed)
       LLVMBuildBr(jit->builder, e);

    if (con_ret->closed && alt_ret->closed)
    {
       LLVMDeleteBasicBlock(e);
       return ret(1, NULL);
    } else
    {
       LLVMPositionBuilderAtEnd(jit->builder, e); 
       return ret(0, NULL);
    }
}

/*
   Jit an if statement
*/
ret_t * exec_if_stmt(jit_t * jit, ast_t * ast)
{
    ast_t * exp = ast->child;
    ast_t * con = exp->next;
    
    ret_t * exp_ret, * con_ret;

    LLVMBasicBlockRef i = LLVMAppendBasicBlock(jit->function, "if");
    LLVMBasicBlockRef b = LLVMAppendBasicBlock(jit->function, "ifbody");
    LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "ifend");

    LLVMBuildBr(jit->builder, i);
    LLVMPositionBuilderAtEnd(jit->builder, i);  
    
    exp_ret = exec_ast(jit, exp); /* expression */

    LLVMBuildCondBr(jit->builder, exp_ret->val, b, e);
    LLVMPositionBuilderAtEnd(jit->builder, b); 
   
    con_ret = exec_ast(jit, con); /* stmt1 */
    
    if (!con_ret->closed)
       LLVMBuildBr(jit->builder, e);

    LLVMPositionBuilderAtEnd(jit->builder, e); 
      
    return ret(0, NULL);
}

/*
   Jit a while statement
*/
ret_t * exec_while_stmt(jit_t * jit, ast_t * ast)
{
    ast_t * exp = ast->child;
    ast_t * con = exp->next;
    
    ret_t * exp_ret, * con_ret;

    LLVMBasicBlockRef breaksave = jit->breakto;

    LLVMBasicBlockRef w = LLVMAppendBasicBlock(jit->function, "while");
    LLVMBasicBlockRef b = LLVMAppendBasicBlock(jit->function, "whilebody");
    LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "whileend");

    LLVMBuildBr(jit->builder, w);
    LLVMPositionBuilderAtEnd(jit->builder, w);  
    
    exp_ret = exec_ast(jit, exp); /* expression */
    
    LLVMBuildCondBr(jit->builder, exp_ret->val, b, e);
    LLVMPositionBuilderAtEnd(jit->builder, b); 
   
    jit->breakto = e;

    con_ret = exec_ast(jit, con); /* stmt1 */
    
    jit->breakto = breaksave;

    if (!con_ret->closed)
        LLVMBuildBr(jit->builder, w);
 
    LLVMPositionBuilderAtEnd(jit->builder, e); 
      
    return ret(0, NULL);
}

/*
   Jit a break statement
*/
ret_t * exec_break(jit_t * jit, ast_t * ast)
{
    if (jit->breakto == NULL)
        jit_exception(jit, "Attempt to break outside loop");

    LLVMBuildBr(jit->builder, jit->breakto);
         
    return ret(1, NULL);
}

ret_t * exec_decl(jit_t * jit, ast_t * ast)
{
   LLVMValueRef val = create_var(jit, ast->sym, serialise(ast->sym->name), ast->type);
   
   return ret(0, val);
}

/*
   Jit a tuple assignment statement
*/
ret_t * exec_tuple_assign(jit_t * jit, ast_t * id, ast_t * expr)
{
    ast_t * a1 = id->child;
    ast_t * a2 = expr->child;
    ret_t * id_ret, * dt_ret;
    LLVMValueRef var;

    int i = 0;
    while (a1 != NULL)
    {
       a1 = a1->next;
       i++;
    }

    LLVMValueRef * vals = GC_MALLOC(i*sizeof(LLVMValueRef));
    i = 0;
    while (a2 != NULL)
    {
       vals[i] = exec_ast(jit, a2)->val;
       if (a2->type->tag == DATA)
           vals[i] = LLVMBuildLoad(jit->builder, vals[i], "load");
       a2 = a2->next;
       i++;
    }
    
    i = 0;
    a1 = id->child;
    while (a1 != NULL)
    {
       if (a1->tag == AST_LIDENT)
       {
          bind_t * bind = find_symbol(a1->sym);
          if (bind->llvm == NULL) /* symbol doesn't exist yet */
          {
             id_ret = exec_decl(jit, a1);
             var = id_ret->val;
          } else
          {
             if (scope_is_global(bind))
                var = LLVMGetNamedGlobal(jit->module, bind->llvm);
             else
                var = loc_lookup(bind->llvm);
          }

          LLVMBuildStore(jit->builder, vals[i], var);
       } else if (a1->tag == AST_LSLOT)
       {
          dt_ret = exec_ast(jit, a1);
    
          LLVMBuildStore(jit->builder, vals[i], dt_ret->val);
       } else if (a1->tag == AST_LTUPLE)
          exec_tuple_unpack_val(jit, a1, vals[i], expr->type->args[i]);
       /* TODO: what about LLOCN and LAPPL ?? */
       else 
          jit_exception(jit, "Unknown type in exec_tuple_assign\n");

       a1 = a1->next;
       i++;
    }

    return ret(0, NULL);
}

/*
   Jit a tuple unpack from a value
*/
ret_t * exec_tuple_unpack_val(jit_t * jit, ast_t * ast1, LLVMValueRef val, type_t * type)
{
    int i = type->arity;
    LLVMValueRef * vals = GC_MALLOC(i*sizeof(LLVMValueRef));
    ast_t * a1;
    LLVMValueRef var;
    ret_t * id_ret, * dt_ret;

    for (i = 0; i < type->arity; i++)
    {
       LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
       LLVMValueRef p = LLVMBuildInBoundsGEP(jit->builder, val, index, 2, "tuple");
       LLVMValueRef val = LLVMBuildLoad(jit->builder, p, "entry");
          
       vals[i] = val;
    }

    i = 0;
    a1 = ast1->child;
    while (a1 != NULL)
    {
       if (a1->tag == AST_LIDENT)
       {
          bind_t * bind = find_symbol(a1->sym);
          if (bind->llvm == NULL) /* symbol doesn't exist yet */
          {
             id_ret = exec_decl(jit, a1);
             var = id_ret->val;
          } else
          {
             if (scope_is_global(bind))
                var = LLVMGetNamedGlobal(jit->module, bind->llvm);
             else
                var = loc_lookup(bind->llvm);
          }
          LLVMBuildStore(jit->builder, vals[i], var);
       } else if (a1->tag == AST_LSLOT)
       {
          dt_ret = exec_ast(jit, a1);
    
          LLVMBuildStore(jit->builder, vals[i], dt_ret->val);
       } else if (a1->tag == AST_LTUPLE)
          exec_tuple_unpack_val(jit, a1, vals[i], type->args[i]);
       /* TODO: what about LLOCN and LAPPL ?? */
       else 
          jit_exception(jit, "Unknown type in exec_tuple_unpack_val\n");

       a1 = a1->next;
       i++;
    }

    return ret(0, NULL);
}

/*
   Jit a tuple unpack
*/
ret_t * exec_tuple_unpack(jit_t * jit, ast_t * ast1, ast_t * ast2)
{
    ret_t * a2_ret = exec_ast(jit, ast2);

    return exec_tuple_unpack_val(jit, ast1, a2_ret->val, ast2->type);
}

int requires_destructor(type_t * t)
{
   if (t->tag == DATA)
   {
      int i;

      bind_t * bind = find_symbol(t->sym);
      if (find_finalizer(bind->type) != NULL) /* explicit constructor exists */
         return 1;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_destructor(t->args[i]))
            return 1;

      return 0;
   } else if (t->tag == ARRAY)
      return requires_destructor(t->params[0]);
   else if (t->tag == TUPLE)
   {
      int i;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_destructor(t->args[i]))
            return 1;
   }

   return 0;
}

void call_destructors(jit_t * jit, LLVMValueRef var, type_t * t, LLVMValueRef retval)
{
   if (retval != var)
   {  
      if (t->tag == DATA)
      {
         type_t * fin = find_finalizer(t);
         if (fin) /* there is a finalizer for this type */
         {
            if (TRACE2) printf("data finalizer\n");
            
            LLVMValueRef fn = LLVMGetNamedFunction(jit->module, fin->llvm);

            LLVMValueRef arg[1] = { var };
            LLVMBuildCall(jit->builder, fn, arg, 1, ""); /* call the finalizer */
         } else /* run destructors for individual slots */
         {
            if (TRACE2) printf("data destructor per slot\n");
            
            int i, count = t->arity;
         
            for (i = 0; i < count; i++)
            {
               type_t * arg = t->args[i];
 
               if (requires_destructor(arg))
               {
                  LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
                  LLVMValueRef slot = LLVMBuildInBoundsGEP(jit->builder, var, index, 2, "datatype");

                  call_destructors(jit, slot, arg, NULL);
               }
            }
         }
      } else if (t->tag == ARRAY)
      {
         type_t * type = t->params[0];
         
         LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
         LLVMValueRef arr = LLVMBuildInBoundsGEP(jit->builder, var, indices, 2, "arr");
         arr = LLVMBuildLoad(jit->builder, arr, "array");

         LLVMValueRef indices2[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 1, 0) };
         LLVMValueRef len = LLVMBuildInBoundsGEP(jit->builder, var, indices2, 2, "length");
         len = LLVMBuildLoad(jit->builder, len, "length");

         if (requires_destructor(type))
         {
            if (TRACE2) printf("array destructor\n");
            /* TODO: push following out to function */
            /* temporary local variable i = 0 */
            LLVMValueRef iloc = AddLocal(jit, LLVMWordType(), serialise("i"));
            LLVMBuildStore(jit->builder, LLVMConstInt(LLVMWordType(), 0, 0), iloc); /* i = 0 */

            /* while */
            LLVMBasicBlockRef w = LLVMAppendBasicBlock(jit->function, "while");
            LLVMBasicBlockRef b = LLVMAppendBasicBlock(jit->function, "whilebody");
            LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "whileend");

            LLVMBuildBr(jit->builder, w);
            LLVMPositionBuilderAtEnd(jit->builder, w);  
    
            /* i < len */
            LLVMValueRef ival = LLVMBuildLoad(jit->builder, iloc, "load");
            LLVMValueRef cmpval = LLVMBuildICmp(jit->builder, LLVMIntSLT, ival, len, "lt");
    
            LLVMBuildCondBr(jit->builder, cmpval, b, e);
            LLVMPositionBuilderAtEnd(jit->builder, b); 
   
            /* constructor(arr + i) */
            LLVMValueRef indices3[1] = { ival };
            LLVMValueRef locn = LLVMBuildInBoundsGEP(jit->builder, arr, indices3, 1, "arr_entry");
    
            call_destructors(jit, locn, type, NULL);
            
            /* i++ */
            LLVMValueRef newi = LLVMBuildAdd(jit->builder, ival, LLVMConstInt(LLVMWordType(), 1, 0), "inc");
            LLVMBuildStore(jit->builder, newi, iloc);
    
            LLVMBuildBr(jit->builder, w);
 
            LLVMPositionBuilderAtEnd(jit->builder, e); 
         }
      } else if (t->tag == TUPLE)
      {
         if (TRACE2) printf("tuple destructor\n");
         int i, count = t->arity;

         for (i = 0; i < count; i++)
         {
            type_t * arg = t->args[i];
 
            if (requires_destructor(arg))
            {
               LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
               LLVMValueRef slot = LLVMBuildInBoundsGEP(jit->builder, var, index, 2, "datatype");

               call_destructors(jit, slot, arg, NULL);
            }
         }
      }
   }
}

/*
   Call destructors for everything going out of scope
*/
ret_t * exec_destructors(jit_t * jit, LLVMValueRef retval)
{
    bind_t * bind = current_scope->scope;
    LLVMValueRef var;

    while (bind != NULL)
    {
       type_t * t = bind->type;
      
       if (t->tag == DATA || t->tag == ARRAY || t->tag == TUPLE)
       {
          if (bind->llvm != NULL) /* make sure it has actually been initialised */
          {
             if (scope_is_global(bind))
                var = LLVMGetNamedGlobal(jit->module, bind->llvm);
             else
                var = loc_lookup(bind->llvm);
  
             call_destructors(jit, var, t, retval); 
          }
       }

       bind = bind->next;
    }

    return ret(0, NULL);
}

int requires_assign(type_t * t)
{
   if (t->tag == DATA)
   {
      int i;

      bind_t * bind = find_symbol(t->sym);
      if (find_assignment(bind->type->ret) != NULL) /* explicit assignment operator exists */
         return 1;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_assign(t->args[i]))
            return 1;

      return 0;
   } else if (t->tag == ARRAY)
      return 1;
   else if (t->tag == TUPLE)
   {
      int i;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_assign(t->args[i]))
            return 1;
   }

   return 0;
}

void call_assign(jit_t * jit, LLVMValueRef var, LLVMValueRef val, type_t * type)
{
   if (requires_assign(type))
   {
      /* check if we have an assignment operator for type */
      type_t * ass = find_assignment(type);
      if (ass != NULL) /* we have an assignment operator */
      {
         if (TRACE2) printf("data assignment operator\n");

         LLVMValueRef fn = LLVMGetNamedFunction(jit->module, ass->llvm);
         LLVMValueRef arg[2] = { var, val };
               
         LLVMBuildCall(jit->builder, fn, arg, 2, ""); /* call the assignment operator */
      } else if (type->tag == DATA || type->tag == TUPLE)
      {
         if (TRACE2) printf("data/tuple assignment per slot\n");

         /* iterate over fields */
         int i, count = type->arity;

         for (i = 0; i < count; i++)
         {
            type_t * arg = type->args[i];
 
            LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
            LLVMValueRef lslot = LLVMBuildInBoundsGEP(jit->builder, var, index, 2, "datatype");
            LLVMValueRef rslot = LLVMBuildInBoundsGEP(jit->builder, val, index, 2, "datatype");

            call_assign(jit, lslot, rslot, arg);
         }
      } else /* type->tag == ARRAY */
      {
         if (TRACE2) printf("array assignment\n");

         type = type->params[0];
   
         LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 1, 0) };
         LLVMValueRef rlen = LLVMBuildInBoundsGEP(jit->builder, val, indices, 2, "length");
         rlen = LLVMBuildLoad(jit->builder, rlen, "rlength");
      
         LLVMValueRef indices2[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
         LLVMValueRef rarrloc = LLVMBuildInBoundsGEP(jit->builder, val, indices2, 2, "arr");
         LLVMValueRef rarr = LLVMBuildLoad(jit->builder, rarrloc, "rarr");
      
         LLVMValueRef llenloc = LLVMBuildInBoundsGEP(jit->builder, var, indices, 2, "length");
         LLVMValueRef llen = LLVMBuildLoad(jit->builder, llenloc, "llength");
      
         LLVMValueRef larrloc = LLVMBuildInBoundsGEP(jit->builder, var, indices2, 2, "arr");
         LLVMValueRef larr = LLVMBuildLoad(jit->builder, larrloc, "larr");
      
         /* if rlength > llength realloc larr */

         LLVMBasicBlockRef i1 = LLVMAppendBasicBlock(jit->function, "if");
         LLVMBasicBlockRef b1 = LLVMAppendBasicBlock(jit->function, "ifbody");
         LLVMBasicBlockRef e1 = LLVMAppendBasicBlock(jit->function, "ifend");

         LLVMBuildBr(jit->builder, i1);
         LLVMPositionBuilderAtEnd(jit->builder, i1);  

         LLVMValueRef cmp = LLVMBuildICmp(jit->builder, LLVMIntSGT, rlen, llen, "gt");

         LLVMBuildCondBr(jit->builder, cmp, b1, e1);
         LLVMPositionBuilderAtEnd(jit->builder, b1); 
   
         LLVMValueRef larr2 = LLVMBuildGCArrayRealloc(jit, type, larr, rlen, "realloc");
         LLVMBuildStore(jit->builder, larr2, larrloc);

         LLVMBuildStore(jit->builder, rlen, llenloc);

         LLVMBuildBr(jit->builder, e1);
         LLVMPositionBuilderAtEnd(jit->builder, e1); 

         /* TODO: run constructors for new entries */

         /* temporary local variable i = 0 */
         LLVMValueRef iloc = AddLocal(jit, LLVMWordType(), serialise("i"));
         LLVMBuildStore(jit->builder, LLVMConstInt(LLVMWordType(), 0, 0), iloc); /* i = 0 */

         /* while */
         LLVMBasicBlockRef w = LLVMAppendBasicBlock(jit->function, "while");
         LLVMBasicBlockRef b = LLVMAppendBasicBlock(jit->function, "whilebody");
         LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "whileend");

         LLVMBuildBr(jit->builder, w);
         LLVMPositionBuilderAtEnd(jit->builder, w);  
    
         /* i < len */
         llen = LLVMBuildLoad(jit->builder, llenloc, "llength");
         LLVMValueRef ival = LLVMBuildLoad(jit->builder, iloc, "load");
         LLVMValueRef cmpval = LLVMBuildICmp(jit->builder, LLVMIntSLT, ival, rlen, "lt");
    
         LLVMBuildCondBr(jit->builder, cmpval, b, e);
         LLVMPositionBuilderAtEnd(jit->builder, b); 
   
         larr = LLVMBuildLoad(jit->builder, larrloc, "larr");
      
         /* constructor(arr + i) */
         LLVMValueRef indices3[1] = { ival };
         LLVMValueRef rlocn = LLVMBuildInBoundsGEP(jit->builder, rarr, indices3, 1, "rarr_entry");
         LLVMValueRef llocn = LLVMBuildInBoundsGEP(jit->builder, larr, indices3, 1, "larr_entry");
    
         call_assign(jit, llocn, rlocn, type);
    
         /* i++ */
         LLVMValueRef newi = LLVMBuildAdd(jit->builder, ival, LLVMConstInt(LLVMWordType(), 1, 0), "inc");
         LLVMBuildStore(jit->builder, newi, iloc);
    
         LLVMBuildBr(jit->builder, w);
 
         LLVMPositionBuilderAtEnd(jit->builder, e);
      } 
   } else
   {
      /* otherwise just copy entire struct/val */
      val = LLVMBuildLoad(jit->builder, val, "load");
      LLVMBuildStore(jit->builder, val, var);
   }
}

ret_t * exec_initialise_assign(jit_t * jit, ast_t * id, ast_t * expr)
{
   LLVMValueRef var = exec_decl(jit, id)->val;
   LLVMValueRef val;

   if (expr->tag == AST_APPL)
      val = exec_appl(jit, expr, 0)->val; /* don't clean up temp */
   else if (expr->tag == AST_BINOP)
      val = exec_binop(jit, expr, 0)->val; /* don't clean up temp */
   else
      val = exec_ast(jit, expr)->val;

   if (expr->type->tag == DATA) 
   {
      type_t * ty = find_symbol(expr->type->sym)->type;
      type_t * args[1] = { expr->type };
      type_t * constr = find_constructor(ty, args, 1);

      if (constr != NULL)
      {
         if (TRACE2) printf("initialise with constructor\n");
         
         LLVMValueRef fn = LLVMGetNamedFunction(jit->module, constr->llvm);

         LLVMValueRef arg[2] = { var, val };
         LLVMBuildCall(jit->builder, fn, arg, 2, "");

         return ret(0, NULL);
      }
   }

   if (expr->type->tag == DATA || expr->type->tag == ARRAY || expr->type->tag == TUPLE)
   {
      if (expr->tag == AST_ARRAY_CONSTRUCTOR || expr->tag == AST_APPL)
      {
         if (TRACE2) printf("assign array constructor or appl\n");
        
         val = LLVMBuildLoad(jit->builder, val, "load");
         LLVMBuildStore(jit->builder, val, var);
      } else
      {
         if (expr->type->tag == ARRAY) /* initialise left array */
         {
            if (TRACE2) printf("initialise array\n");
            
            type_t * ptype = expr->type->params[0];
            
            LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 1, 0) };
            LLVMValueRef len = LLVMBuildInBoundsGEP(jit->builder, var, indices, 2, "length");
            
            LLVMBuildStore(jit->builder, LLVMConstInt(LLVMWordType(), 0, 0), len);

            LLVMValueRef indices2[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
            LLVMValueRef arr = LLVMBuildInBoundsGEP(jit->builder, var, indices2, 2, "arr");
            
            LLVMBuildStore(jit->builder, LLVMConstNull(type_to_llvm(jit, pointer_type(ptype))), arr);
         }

         call_assign(jit, var, val, expr->type);
      }
   } else
      LLVMBuildStore(jit->builder, val, var);

   return ret(0, NULL);
}

/*
   Jit an assignment statement
*/
ret_t * exec_assignment(jit_t * jit, ast_t * id, ast_t * expr)
{
    char * llvm;
    ret_t * id_ret, * expr_ret;
    LLVMValueRef val, var;

    if (id->tag == AST_LTUPLE)
    {
       if (expr->tag == AST_TUPLE)
          return exec_tuple_assign(jit, id, expr);
       else
          return exec_tuple_unpack(jit, id, expr);
    } 

    if (id->tag == AST_LSLOT || id->tag == AST_LLOCN || id->tag == AST_LAPPL)
       var = exec_ast(jit, id)->val;
    else /* lident */
    {
       bind_t * bind = find_symbol(id->sym);
       
       if (bind == NULL || bind->llvm == NULL) /* symbol doesn't exist or not initialised */
          return exec_initialise_assign(jit, id, expr);
       else
       {
          if (scope_is_global(bind))
             var = LLVMGetNamedGlobal(jit->module, bind->llvm);
          else
             var = loc_lookup(bind->llvm);

          if (bind->type->tag == REF) /* if it is a reference type, deref */
             var = LLVMBuildLoad(jit->builder, var, "deref");
       }
    }
    
    val = exec_ast(jit, expr)->val;
    
    if (id->type->tag == DATA || id->type->tag == TUPLE || id->type->tag == ARRAY)
       call_assign(jit, var, val, id->type);
    else
       LLVMBuildStore(jit->builder, val, var);

    return ret(0, NULL);
}

/*
   Jit access to an identifier
*/
ret_t * exec_place(jit_t * jit, ast_t * ast)
{
    bind_t * bind = find_symbol(ast->sym);
    LLVMValueRef var;

    if (scope_is_global(bind))
       var = LLVMGetNamedGlobal(jit->module, bind->llvm);
    else
       var = loc_lookup(bind->llvm);
    
    return ret(0, var);
}

/*
   Jit load of an identifier
*/
ret_t * exec_ident(jit_t * jit, ast_t * ast)
{
    bind_t * bind = find_symbol(ast->sym);
    LLVMValueRef var;

    if (scope_is_global(bind))
       var = LLVMGetNamedGlobal(jit->module, bind->llvm);
    else
       var = loc_lookup(bind->llvm);
    
    if ((ast->type->tag == DATA || ast->type->tag == TUPLE || ast->type->tag == ARRAY) 
       && bind->type->tag != REF)
       return ret(0, var); /* for structs, return a reference, not the value */

    LLVMValueRef val = LLVMBuildLoad(jit->builder, var, bind->llvm);
    
    return ret(0, val);
}

/*
   Jit a tuple expression
*/
ret_t * exec_tuple(jit_t * jit, ast_t * ast)
{
    int params = ast->type->arity;
    int i;
    int atomic = 1;
    ret_t * p_ret;
    
    LLVMTypeRef tup_type = tuple_to_llvm(jit, ast->type);

    LLVMValueRef val = AddLocal(jit, tup_type, serialise("tuple"));

    ast_t * p = ast->child;
    for (i = 0; i < params; i++)
    {
        p_ret = exec_ast(jit, p);
        if (p->type->tag == DATA)
           p_ret->val = LLVMBuildLoad(jit->builder, p_ret->val, "load");
        
         /* insert value into tuple */
        LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
        LLVMValueRef entry = LLVMBuildInBoundsGEP(jit->builder, val, indices, 2, "tuple");
        LLVMBuildStore(jit->builder, p_ret->val, entry);
        
        p = p->next;
    }

    return ret(0, val);
}

/*
   Jit a type declaration. Not much we can do except set up 
   the empty struct.
*/
ret_t * exec_data_stmt(jit_t * jit, ast_t * ast)
{
   sym_t * sym = ast->child->sym;
   bind_t * bind;
   char * llvm = serialise(sym->name);
   
   LLVMStructCreateNamed(LLVMGetGlobalContext(), llvm);

   bind = find_symbol(sym);
   bind->type->ret->llvm = llvm;
   
   return ret(0, NULL);
}

/*
   Jit a list of function parameters making allocas for them
*/
ret_t * exec_fnparams(jit_t * jit, ast_t * ast)
{
   ast_t * p = ast->child;
   int i = 0;

   while (p != NULL)
   {
      bind_t * bind = find_symbol(p->child->sym);
      LLVMValueRef param, palloca;

      p->type = bind->type;
      
      param = LLVMGetParam(jit->function, i);
              
      palloca = LLVMBuildAlloca(jit->builder, type_to_llvm(jit, p->type), p->child->sym->name);
      LLVMBuildStore(jit->builder, param, palloca);
       
      bind->llvm = serialise(p->child->sym->name);
      loc_insert(bind->llvm, palloca);
         
      i++;
      p = p->next;
   }

   return ret(0, NULL);
}

/*
   Jit a function definition, given its type.
*/
ret_t * exec_fndef(jit_t * jit, ast_t * ast, type_t * type)
{
   int i, last_return = 0, params = type->arity;
   ast_t * fn_params = ast->child->next;
   ast_t * p = fn_params->next->next;
   ret_t * r;

   LLVMTypeRef * args = (LLVMTypeRef *) GC_MALLOC(params*sizeof(LLVMTypeRef));
   LLVMTypeRef llvm_ret, fn_type;
   LLVMValueRef fn_save;

   sym_t * sym = ast->child->sym;
   char * llvm;
   
   env_t * scope_save;
   LLVMBuilderRef build_save;
   LLVMBasicBlockRef entry;

   /* get llvm parameter types */
   for (i = 0; i < params; i++)
      args[i] = type_to_llvm(jit, type->args[i]); 

   /* get llvm return type */
   llvm_ret = type_to_llvm(jit, type->ret); 

   /* get llvm function type */
   fn_type = LLVMFunctionType(llvm_ret, args, params, 0);
    
   /* serialise function name */
   llvm = serialise(sym->name);
   
   /* make llvm function object */
   fn_save = jit->function;
   jit->function = LLVMAddFunction(jit->module, llvm, fn_type);
   type->llvm = llvm; /* store serialised name in type */
   
   /* set nocapture on all structured params */
   for (i = 0; i < params; i++)
   {
      type_t * t = type->args[i];
      if (is_structured(t))
         LLVMAddAttribute(LLVMGetParam(jit->function, i), LLVMNoCaptureAttribute);
   }

   /* set noalias on all structured return values */
   if (is_structured(type->ret))
      LLVMAddFunctionAttr(jit->function, LLVMNoAliasAttribute);
   
   /* enter function scope */
   scope_save = current_scope;
   current_scope = ast->env;

   /* setup jit builder */
   build_save = jit->builder;
   jit->builder = LLVMCreateBuilder();

   /* first basic block */
   entry = LLVMAppendBasicBlock(jit->function, "entry");
   LLVMPositionBuilderAtEnd(jit->builder, entry);
    
   /* make allocas for the function parameters */
   exec_fnparams(jit, fn_params);
   
   /* jit the statements in the function body */
   r = exec_ast(jit, p);
   
   if (!r->closed) /* no return */
   {
      if (type->ret == t_nil)
         LLVMBuildRetVoid(jit->builder);
      else
         jit_exception(jit, "Function does not return value at end of block");
   }

   /* run the pass manager on the jit'd function */
   LLVMRunFunctionPassManager(jit->pass, jit->function); 
    
   /* clean up */
   LLVMDisposeBuilder(jit->builder);  
   jit->builder = build_save;
   jit->function = fn_save;    
   current_scope = scope_save;
   
   return ret(0, NULL);
}

int requires_constructor(type_t * t)
{
   if (t->tag == DATA)
   {
      int i;

      bind_t * bind = find_symbol(t->sym);
      if (find_constructor(bind->type, NULL, 0) != NULL) /* explicit constructor exists */
         return 1;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_constructor(t->args[i]))
            return 1;

      return 0;
   } else if (t->tag == ARRAY)
      return requires_constructor(t->params[0]);
   else if (t->tag == TUPLE)
   {
      int i;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_constructor(t->args[i]))
            return 1;
   }

   return 0;
}

void call_constructors(jit_t * jit, LLVMValueRef locn, type_t * type)
{
   if (type->tag == DATA)
   {
      bind_t * bind = find_symbol(type->sym);
      type_t * constr = find_constructor(bind->type, NULL, 0);
   
      if (constr) /* we have a constructor to call */
      {
         if (TRACE2) printf("data constructor\n");
      
         /* get constructor function */
         LLVMValueRef confn = LLVMGetNamedFunction(jit->module, constr->llvm);
      
         LLVMValueRef args[1] = { locn };
      
         LLVMBuildCall(jit->builder, confn, args, 1, "");
      } else /* check the fields to see if they need constructing */
      {
         if (TRACE2) printf("data constructor per slot\n");
         
         int i, count = type->arity;

         for (i = 0; i < count; i++)
         {
            type_t * arg = type->args[i];
 
            if (requires_constructor(arg))
            {
               LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
               LLVMValueRef slot = LLVMBuildInBoundsGEP(jit->builder, locn, index, 2, "datatype");

               call_constructors(jit, slot, arg);
            }
         }
      }
   } else if (type->tag == TUPLE)
   {
      if (TRACE2) printf("tuple constructor\n");

      int i, count = type->arity;

      for (i = 0; i < count; i++)
      {
         type_t * arg = type->args[i];
 
         if (requires_constructor(arg))
         {
            LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
            LLVMValueRef slot = LLVMBuildInBoundsGEP(jit->builder, locn, index, 2, "datatype");

            call_constructors(jit, slot, arg);
         }
      }
   } else if (type->tag == ARRAY)
   {
      type = type->params[0];
   
      LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 1, 0) };
      LLVMValueRef len = LLVMBuildInBoundsGEP(jit->builder, locn, indices, 2, "length");
      len = LLVMBuildLoad(jit->builder, len, "length");
      
      LLVMValueRef indices2[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
      LLVMValueRef arr = LLVMBuildInBoundsGEP(jit->builder, locn, indices2, 2, "arr");
      arr = LLVMBuildLoad(jit->builder, arr, "arr");
      
      if (requires_constructor(type))
      {
         if (TRACE2) printf("array constructor\n");

         /* temporary local variable i = 0 */
         LLVMValueRef iloc = AddLocal(jit, LLVMWordType(), serialise("i"));
         LLVMBuildStore(jit->builder, LLVMConstInt(LLVMWordType(), 0, 0), iloc); /* i = 0 */

         /* while */
         LLVMBasicBlockRef w = LLVMAppendBasicBlock(jit->function, "while");
         LLVMBasicBlockRef b = LLVMAppendBasicBlock(jit->function, "whilebody");
         LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "whileend");

         LLVMBuildBr(jit->builder, w);
         LLVMPositionBuilderAtEnd(jit->builder, w);  
    
         /* i < len */
         LLVMValueRef ival = LLVMBuildLoad(jit->builder, iloc, "load");
         LLVMValueRef cmpval = LLVMBuildICmp(jit->builder, LLVMIntSLT, ival, len, "lt");
    
         LLVMBuildCondBr(jit->builder, cmpval, b, e);
         LLVMPositionBuilderAtEnd(jit->builder, b); 
   
         /* constructor(arr + i) */
         LLVMValueRef indices[1] = { ival };
         LLVMValueRef locn = LLVMBuildInBoundsGEP(jit->builder, arr, indices, 1, "arr_entry");
    
         call_constructors(jit, locn, type);
         
         /* i++ */
         LLVMValueRef newi = LLVMBuildAdd(jit->builder, ival, LLVMConstInt(LLVMWordType(), 1, 0), "inc");
         LLVMBuildStore(jit->builder, newi, iloc);
    
         LLVMBuildBr(jit->builder, w);
 
         LLVMPositionBuilderAtEnd(jit->builder, e); 
      }
   }
}

/* 
   Jit an array constructor application
*/
ret_t * exec_array_constructor(jit_t * jit, ast_t * ast)
{
   ast_t * expr = ast->child->next;

   ret_t * r = exec_ast(jit, expr);
   
   LLVMTypeRef struct_ty = array_to_llvm(jit, ast->type);
   LLVMValueRef val = AddLocal(jit, struct_ty, "array_s");

   /* insert length into array struct */
   LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 1, 0) };
   LLVMValueRef entry = LLVMBuildInBoundsGEP(jit->builder, val, indices, 2, "length");
   LLVMBuildStore(jit->builder, r->val, entry);
    
   /* create array */
   int atomic = is_atomic(ast->type->params[0]);
   LLVMValueRef arr = LLVMBuildGCArrayMalloc(jit, ast->type->params[0], r->val, "arr", atomic);

   LLVMValueRef indices2[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
   entry = LLVMBuildInBoundsGEP(jit->builder, val, indices2, 2, "array");
   LLVMBuildStore(jit->builder, arr, entry);
   
   /* constructors for array elements */
   call_constructors(jit, val, ast->type);
   
   return ret(0, val);   
}

int requires_copy_construct(type_t * t)
{
   if (t->tag == DATA)
   {
      int i;

      bind_t * bind = find_symbol(t->sym);
      if (find_copy_cons(bind->type) != NULL) /* explicit copy constructor exists */
         return 1;

      for (i = 0; i < t->arity; i++) /* otherwise, recursively check slots */
         if (requires_copy_construct(t->args[i]))
            return 1;

      return 0;
   } else if (t->tag == ARRAY) /* arrays always need copying */
   {
      return 1;
   } else if (t->tag == TUPLE)
   {
      int i;

      for (i = 0; i < t->arity; i++) /* recursively check slots */
         if (requires_copy_construct(t->args[i]))
            return 1;
   }

   return 0;
}

/*
   Call the copy constructor for the given type to make a copy
*/
LLVMValueRef copy_construct(jit_t * jit, LLVMValueRef var, LLVMValueRef val, type_t * t)
{
   if (requires_copy_construct(t))
   {
      if (t->tag == DATA)
      {
         bind_t * bind = find_symbol(t->sym);
         type_t * copy_cons = find_copy_cons(bind->type);
   
         if (copy_cons != NULL) /* see if a copy constructor exists */
         {
            if (TRACE2) printf("data copy constructor\n");
            
            LLVMValueRef fn = LLVMGetNamedFunction(jit->module, copy_cons->llvm);
      
            LLVMValueRef vals[2] = { var, val };

            /* call copy constructor */
            LLVMBuildCall(jit->builder, fn, vals, 2, "");

            return LLVMBuildLoad(jit->builder, var, "call-by-value");
         } 
      }
         
      if (t->tag == DATA || t->tag == TUPLE) /* copy construct per slot */
      { 
         if (TRACE2) printf("data/tuple copy construct per slot\n");

         /* iterate over fields */
         int i, count = t->arity;

         for (i = 0; i < count; i++)
         {
            type_t * arg = t->args[i];
 
            LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
            LLVMValueRef lslot = LLVMBuildInBoundsGEP(jit->builder, var, index, 2, "datatype");
            LLVMValueRef rslot = LLVMBuildInBoundsGEP(jit->builder, val, index, 2, "datatype");

            copy_construct(jit, lslot, rslot, arg);
         }
      } else /* t->tag == ARRAY */
      {
         if (TRACE2) printf("array copy constructor\n");

         t = t->params[0];
   
         LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 1, 0) };
         LLVMValueRef rlen = LLVMBuildInBoundsGEP(jit->builder, val, indices, 2, "length");
         rlen = LLVMBuildLoad(jit->builder, rlen, "rlength");
      
         LLVMValueRef indices2[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
         LLVMValueRef rarrloc = LLVMBuildInBoundsGEP(jit->builder, val, indices2, 2, "arr");
         LLVMValueRef rarr = LLVMBuildLoad(jit->builder, rarrloc, "rarr");
      
         LLVMValueRef llenloc = LLVMBuildInBoundsGEP(jit->builder, var, indices, 2, "length");
            
         LLVMValueRef larrloc = LLVMBuildInBoundsGEP(jit->builder, var, indices2, 2, "arr");
            
         int atomic = is_atomic(t);
         LLVMValueRef larr = LLVMBuildGCArrayMalloc(jit, t, rlen, "realloc", atomic);
         LLVMBuildStore(jit->builder, larr, larrloc);

         LLVMBuildStore(jit->builder, rlen, llenloc);

         /* TODO: run constructors for new entries */

         /* temporary local variable i = 0 */
         LLVMValueRef iloc = AddLocal(jit, LLVMWordType(), serialise("i"));
         LLVMBuildStore(jit->builder, LLVMConstInt(LLVMWordType(), 0, 0), iloc); /* i = 0 */

         /* while */
         LLVMBasicBlockRef w = LLVMAppendBasicBlock(jit->function, "while");
         LLVMBasicBlockRef b = LLVMAppendBasicBlock(jit->function, "whilebody");
         LLVMBasicBlockRef e = LLVMAppendBasicBlock(jit->function, "whileend");

         LLVMBuildBr(jit->builder, w);
         LLVMPositionBuilderAtEnd(jit->builder, w);  
    
         /* i < len */
         LLVMValueRef llen = LLVMBuildLoad(jit->builder, llenloc, "llength");
         LLVMValueRef ival = LLVMBuildLoad(jit->builder, iloc, "load");
         LLVMValueRef cmpval = LLVMBuildICmp(jit->builder, LLVMIntSLT, ival, rlen, "lt");
    
         LLVMBuildCondBr(jit->builder, cmpval, b, e);
         LLVMPositionBuilderAtEnd(jit->builder, b); 
   
         larr = LLVMBuildLoad(jit->builder, larrloc, "larr");
      
         /* constructor(arr + i) */
         LLVMValueRef indices3[1] = { ival };
         LLVMValueRef rlocn = LLVMBuildInBoundsGEP(jit->builder, rarr, indices3, 1, "rarr_entry");
         LLVMValueRef llocn = LLVMBuildInBoundsGEP(jit->builder, larr, indices3, 1, "larr_entry");
    
         copy_construct(jit, llocn, rlocn, t);
    
         /* i++ */
         LLVMValueRef newi = LLVMBuildAdd(jit->builder, ival, LLVMConstInt(LLVMWordType(), 1, 0), "inc");
         LLVMBuildStore(jit->builder, newi, iloc);
    
         LLVMBuildBr(jit->builder, w);
 
         LLVMPositionBuilderAtEnd(jit->builder, e);
      }
   } else
   {
      /* otherwise just copy entire struct/val */
      val = LLVMBuildLoad(jit->builder, val, "load");
      LLVMBuildStore(jit->builder, val, var);
   }

   return LLVMBuildLoad(jit->builder, var, "call-by-value");
}

void call_swap(jit_t * jit, ast_t * id, ast_t * exp)
{
   ast_t * exp2 = exp->next;

   if (exp->type->tag == ARRAY || exp->type->tag == DATA)
   {
      LLVMValueRef loc1 = exec_ast(jit, exp)->val;
      LLVMValueRef loc2 = exec_ast(jit, exp2)->val;
      
      LLVMValueRef temp = AddLocal(jit, type_to_llvm(jit, exp->type), serialise("__cs_temp"));

      LLVMValueRef val = LLVMBuildLoad(jit->builder, loc1, "load");
      LLVMBuildStore(jit->builder, val, temp);
   
      val = LLVMBuildLoad(jit->builder, loc2, "load");
      LLVMBuildStore(jit->builder, val, loc1);
   
      val = LLVMBuildLoad(jit->builder, temp, "load");
      LLVMBuildStore(jit->builder, val, loc2);
   }
}

/* 
   Jit a function application or type constructor application
*/
ret_t * exec_appl(jit_t * jit, ast_t * ast, int cleanup)
{
   ast_t * id = ast->child;
   ast_t * exp = id->next;
   
   if (id->sym == sym_lookup("swap") 
      && (exp->type->tag == ARRAY || exp->type->tag == DATA))
   {
      call_swap(jit, id, exp);
      return ret(0, NULL);
   }

   bind_t * bind = find_symbol(id->sym);
   type_t * fn;
   typ_t tag = bind->type->tag;

   LLVMValueRef val;

   int i, count;
   
   if (tag == CONSTRUCTOR) fn = bind->type->ret;
   else fn = find_prototype(bind->type, exp);
   
   count = fn->arity;
   
   LLVMValueRef * vals = GC_MALLOC(count*sizeof(LLVMValueRef));
   
   i = 0;
   while (exp != NULL)
   {
      ret_t * r = exec_ast(jit, exp);
      vals[i] = r->val;
      if (tag != CONSTRUCTOR &&
          ((exp->type->tag == DATA && fn->args[i]->tag == DATA) /* call by value not reference */
       || (exp->type->tag == TUPLE && fn->args[i]->tag == TUPLE)
       || (exp->type->tag == ARRAY && fn->args[i]->tag == ARRAY)))
      {
         char * name = serialise("temp");
         LLVMValueRef var = create_var(jit, sym_lookup(name), name, exp->type);

         vals[i] = copy_construct(jit, var, vals[i], exp->type);
      } else if (tag == CONSTRUCTOR &&
         (exp->type->tag == DATA || exp->type->tag == TUPLE || exp->type->tag == ARRAY))
            vals[i] = LLVMBuildLoad(jit->builder, vals[i], "load");

      i++;
      exp = exp->next;
   }
   
   if (tag == GENERIC || tag == FN) /* calling an actual function */
   {
      ret_t * r;
      LLVMValueRef f; /* jit'd function */

      if (fn->llvm == NULL) /* function not yet jit'd */
      {
         inference(fn->ast);
         r = exec_fndef(jit, fn->ast, fn);
      }
      
      f = LLVMGetNamedFunction(jit->module, fn->llvm);
      
      /* call function */
      val = LLVMBuildCall(jit->builder, f, vals, count, "");
   } else /* tag == CONSTRUCTOR */
   {                
      LLVMTypeRef t = LLVMGetTypeByName(jit->module, fn->llvm);
   
      if (bind->llvm == NULL) /* type not yet defined in LLVM */
      {
         LLVMTypeRef * types = GC_MALLOC(count*sizeof(LLVMTypeRef));
         for (i = 0; i < count; i++)
            types[i] = type_to_llvm(jit, fn->args[i]);
         LLVMStructSetBody(t, types, count, 0);
         bind->llvm = fn->llvm;
      }

      char * name = serialise("__cs_data");

      /* cleanup up (using destructors) or not */
      if (cleanup)
         val = create_var(jit, sym_lookup(name), name, fn);
      else
         val = AddLocal(jit, t, name);

      for (i = 0; i < count; i++)
      {
         /* insert value into datatype */
         LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
         LLVMValueRef entry = LLVMBuildInBoundsGEP(jit->builder, val, indices, 2, fn->sym->name);
         LLVMBuildStore(jit->builder, vals[i], entry);
      } 

      return ret(0, val);
   }

   if (ast->type->tag == DATA || ast->type->tag == TUPLE || ast->type->tag == ARRAY)
   {
      LLVMTypeRef type = type_to_llvm(jit, ast->type);
      LLVMValueRef temp;
      char * name = serialise("__cs_data");

      /* cleanup up (using destructors) or not */
      if (cleanup)
         temp = create_var(jit, sym_lookup(name), name, ast->type);
      else
         temp = AddLocal(jit, type, name);

      LLVMBuildStore(jit->builder, val, temp);

      return ret(0, temp);
   } else
      return ret(0, val);
}

/*
   Jit a load from a slot
*/
ret_t * exec_slot(jit_t * jit, ast_t * ast)
{
   /* TODO: combine with lslot */
   
   ast_t * dt = ast->child;
   ast_t * slot = dt->next;
   ret_t * r;

   r = exec_ast(jit, dt);

   type_t * type = dt->type;
   int i;

   for (i = 0; i < type->arity; i++)
      if (type->slots[i] == slot->sym)
            break;

   LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
   LLVMValueRef val = LLVMBuildInBoundsGEP(jit->builder, r->val, index, 2, "datatype");

   if (type->args[i]->tag != DATA && type->args[i]->tag != TUPLE && type->args[i]->tag != ARRAY)
      val = LLVMBuildLoad(jit->builder, val, slot->sym->name);

   return ret(0, val);
}

/*
   Jit access to a slot
*/
ret_t * exec_lslot(jit_t * jit, ast_t * ast)
{
   ast_t * dt = ast->child;
   ast_t * slot = dt->next;

   ret_t * r;

   r = exec_ast(jit, dt);

   type_t * type = dt->type;
   int i;

   for (i = 0; i < type->arity; i++)
      if (type->slots[i] == slot->sym)
            break;

   LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
   LLVMValueRef val = LLVMBuildInBoundsGEP(jit->builder, r->val, index, 2, "datatype");
   
   return ret(0, val);
}

/*
   Jit an array access
*/
ret_t * exec_locn(jit_t * jit, ast_t * ast)
{
    /* TODO: combine with exec_llocn below */
   ast_t * id = ast->child;
    ast_t * expr = id->next;
    
    if (ast->sym == NULL) /* need some kind of name */
        ast->sym = sym_lookup("__cs_none");
    
    ret_t * r = exec_ast(jit, id);
    ret_t * s = exec_ast(jit, expr);
    
    /* get array from datatype */
    LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
    LLVMValueRef val = LLVMBuildInBoundsGEP(jit->builder, r->val, indices, 2, "arr");
    val = LLVMBuildLoad(jit->builder, val, "array");
    
    /* get location within array */
    LLVMValueRef indices2[1] = { s->val };
    val = LLVMBuildInBoundsGEP(jit->builder, val, indices2, 1, "arr_entry");
    
    /* load value if not a data type */
    if (ast->type->tag != DATA && ast->type->tag != TUPLE && ast->tag != ARRAY)
       val = LLVMBuildLoad(jit->builder, val, "entry");
    
    return ret(0, val);
}

/*
   Jit access to an array location
*/
ret_t * exec_llocn(jit_t * jit, ast_t * ast)
{
    ast_t * id = ast->child;
    ast_t * expr = id->next;
    
    if (ast->sym == NULL) /* need some kind of name */
        ast->sym = sym_lookup("__cs_none");
    
    ret_t * r = exec_ast(jit, id);
    ret_t * s = exec_ast(jit, expr);
    
    /* get array from datatype */
    LLVMValueRef indices[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), 0, 0) };
    LLVMValueRef val = LLVMBuildInBoundsGEP(jit->builder, r->val, indices, 2, "arr");
    val = LLVMBuildLoad(jit->builder, val, "array");
    
    /* get location within array */
    LLVMValueRef indices2[1] = { s->val };
    val = LLVMBuildInBoundsGEP(jit->builder, val, indices2, 1, "arr_entry");
    
    return ret(0, val);
}

/*
   Jit a return statement
*/
ret_t * exec_return(jit_t * jit, ast_t * ast)
{
    ast_t * p = ast->child;
    ret_t * r;

    if (p != ast_nil)
        r = exec_ast(jit, p);
    else
        r = ret(0, NULL);

    env_t * scope_save = current_scope;
    
    while (current_scope->next->next != NULL) /* while we haven't reached global scope */
    {
       exec_destructors(jit, r->val);
       
       scope_down();
    }
    
    if (p->type->tag == DATA || p->type->tag == TUPLE || p->type->tag == ARRAY)
       r->val = LLVMBuildLoad(jit->builder, r->val, "datatype");

    if (p != ast_nil)
        LLVMBuildRet(jit->builder, r->val);    
    else
        LLVMBuildRetVoid(jit->builder);
     
    current_scope = scope_save; /* we might have other statements following the return */

    return ret(1, NULL);
}
/*
   Jit a function statement. We don't jit the
   function until it is actually called the first time.
*/
ret_t * exec_fn_stmt(jit_t * jit, ast_t * ast)
{
   ast->tag = AST_FN_BODY; /* needed for type inference of body */
   return ret(0, NULL);
}

/*
   As we traverse the ast we dispatch on ast tag to various jit 
   functions defined above
*/
ret_t * exec_ast(jit_t * jit, ast_t * ast)
{
    switch (ast->tag)
    {
    case AST_ZZ:
        return exec_ZZ(jit, ast);
    case AST_INT:
        return exec_int(jit, ast);
    case AST_UINT:
        return exec_uint(jit, ast);
    case AST_DOUBLE:
        return exec_double(jit, ast);
    case AST_CHAR:
        return exec_char(jit, ast);
    case AST_STRING:
        return exec_string(jit, ast);
    case AST_TUPLE:
        return exec_tuple(jit, ast);
    case AST_BINOP:
        return exec_binop(jit, ast, 1); /* by default, cleanup */
    case AST_IF_ELSE_EXPR:
        return exec_if_else_expr(jit, ast);
    case AST_IF_ELSE_STMT:
        return exec_if_else_stmt(jit, ast);
    case AST_IF_STMT:
        return exec_if_stmt(jit, ast);
    case AST_WHILE_STMT:
        return exec_while_stmt(jit, ast);
    case AST_BREAK:
        return exec_break(jit, ast);
    case AST_BLOCK:
    case AST_THEN:
    case AST_ELSE:
    case AST_DO:
        return exec_block(jit, ast);
    case AST_DATA_STMT:
        return exec_data_stmt(jit, ast);
    case AST_ASSIGNMENT:
        return exec_assignment(jit, ast->child, ast->child->next);
    case AST_IDENT:
        return exec_ident(jit, ast);
    case AST_APPL:
        return exec_appl(jit, ast, 1); /* by default, cleanup */
    case AST_ARRAY_CONSTRUCTOR:
        return exec_array_constructor(jit, ast);
    case AST_SLOT:
        return exec_slot(jit, ast);
    case AST_LSLOT:
        return exec_lslot(jit, ast);
    case AST_LOCN:
        return exec_locn(jit, ast);
    case AST_LLOCN:
        return exec_llocn(jit, ast);
    case AST_FN_STMT:
        return exec_fn_stmt(jit, ast);
    case AST_RETURN:
        return exec_return(jit, ast);
    default:
        jit_exception(jit, "Unknown AST tag in exec_ast\n");
    }
}

/* 
   Jit a return
*/
void exec_ret(jit_t * jit, ast_t * ast, LLVMValueRef val)
{
   if (ast->type == t_nil)
      LLVMBuildRetVoid(jit->builder);
   else
      LLVMBuildRet(jit->builder, val);
}

/*
   Print the given entry of a struct
*/
void print_struct_entry(jit_t * jit, type_t * type, int i, LLVMGenericValueRef val)
{
   type_t * t = type->args[i];
   LLVMTypeRef lt = type_to_generic_llvm(jit, t);
   LLVMTypeRef ltype = type_to_generic_llvm(jit, type);
   LLVMGenericValueRef gen_val;
   
   LLVMBuilderRef builder = LLVMCreateBuilder();
   LLVMTypeRef args[1] = { ltype };
   LLVMTypeRef fn_type = LLVMFunctionType(lt, args, 1, 0);
   LLVMValueRef function = LLVMAddFunction(jit->module, "exec2", fn_type);
   LLVMBasicBlockRef entry = LLVMAppendBasicBlock(function, "entry");
   LLVMPositionBuilderAtEnd(builder, entry);
   
   LLVMValueRef obj = LLVMGetParam(function, 0);
   LLVMValueRef index[2] = { LLVMConstInt(LLVMInt32Type(), 0, 0), LLVMConstInt(LLVMInt32Type(), i, 0) };
   LLVMValueRef res = LLVMBuildInBoundsGEP(builder, obj, index, 2, "tuple");
   if (t->tag != DATA && t->tag != TUPLE)
      res = LLVMBuildLoad(builder, res, "entry");
    
   if (t == t_nil)
      LLVMBuildRetVoid(builder);
   else
      LLVMBuildRet(builder, res);

   LLVMRunFunctionPassManager(jit->pass, function);
   LLVMGenericValueRef exec_args[1] = { val };
   gen_val = LLVMRunFunction(jit->engine, function, 1, exec_args);
   
   LLVMDeleteFunction(function);
   LLVMDisposeBuilder(builder);

   print_gen(jit, t, gen_val);
}

/*
   Print special characters in character format
*/
int print_special(char c)
{
   switch (c)
   {
   case '\'':
      printf("'\\''");
      return 1;
   case '\"':
      printf("'\\\"'");
      return 1;
   case '\\':
      printf("'\\\\'");
      return 1;
   case '\0':
      printf("'\\0'");
      return 1;
   case '\n':
      printf("'\\n'");
      return 1;
   case '\r':
      printf("'\\r'");
      return 1;
   case '\t':
      printf("'\\t'");
      return 1;
   default:
      return 0;
   }
}

/*
   Print the generic return value from exec
*/
void print_gen(jit_t * jit, type_t * type, LLVMGenericValueRef gen_val)
{
   int i, res;
   
   if (type == t_nil)
      printf("none");
   else if (type == t_int)
      printf("%ldi", (long) LLVMGenericValueToInt(gen_val, 1));
   else if (type == t_uint)
      printf("%luu", (unsigned long) LLVMGenericValueToInt(gen_val, 0));
   else if (type == t_double)
      printf("%lg", (double) LLVMGenericValueToFloat(LLVMDoubleType(), gen_val));
   else if (type == t_char)
   {
      char c = (char) LLVMGenericValueToInt(gen_val, 0);
      if (!print_special(c))
         printf("'%c'", c);
   }
   else if (type == t_string)
      printf("\"%s\"", (char *) LLVMGenericValueToPointer(gen_val));
   else if (type == t_bool)
   {
      if (LLVMGenericValueToInt(gen_val, 0))
         printf("true");
      else
         printf("false");
   } else if (type->tag == TUPLE)
   {
      printf("(");
      for (i = 0; i < type->arity - 1; i++)
          print_struct_entry(jit, type, i, gen_val), printf(", ");
      print_struct_entry(jit, type, i, gen_val);
      if (type->arity == 1)
         printf(",");
      printf(")");
   } else if (type->tag == DATA)
   {
      if (type == t_ZZ)
         fmpz_print((fmpz *) LLVMGenericValueToPointer(gen_val));
      else
      {
         printf("%s(", type->sym->name);
         for (i = 0; i < type->arity - 1; i++)
             print_struct_entry(jit, type, i, gen_val), printf(", ");
         print_struct_entry(jit, type, i, gen_val);
         printf(")");
      }
   } else if (type->tag == ARRAY)
   {
      printf("array");
   } else
      exception("Unknown type in print_gen\n");
}

/* 
   We start traversing the ast to do jit'ing 
*/
void exec_root(jit_t * jit, ast_t * ast)
{
    LLVMGenericValueRef gen_val;
    ret_t * ret;

    /* Traverse the ast jit'ing everything, then run the jit'd code */
    START_EXEC(type_to_generic_llvm(jit, ast->type));
         
    /* jit the ast */
    ret = exec_ast(jit, ast);
    
    /* jit the return statement for the exec function */
    if (ast->type->tag == DATA || ast->type->tag == TUPLE) /* data types must be returned as GC'd heap objects */
    {
       LLVMTypeRef type = type_to_llvm(jit, ast->type);
       LLVMValueRef val = LLVMBuildLoad(jit->builder, ret->val, "val");
       int atomic = is_atomic(ast->type);
       ret->val = LLVMBuildGCMalloc(jit, type, "data", atomic);
       LLVMBuildStore(jit->builder, val, ret->val);
    }

    exec_ret(jit, ast, ret->val);
    
    /* get the generic return value from exec */
    END_EXEC(gen_val);

    /* print the resulting value */
    print_gen(jit, ast->type, gen_val), printf("\n");
}

