#include "CG.h"
#include <llvm-c/Core.h>
#include <llvm-c-14/llvm-c/Types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


struct CG *
CG_Create ()
{
  struct CG *cg;

  cg = calloc (1, sizeof (struct CG));

  cg->context = LLVMContextCreate ();
  cg->module = LLVMModuleCreateWithNameInContext ("Main", cg->context);
  cg->builder = LLVMCreateBuilderInContext (cg->context);

  cg->pass = LLVMCreateFunctionPassManagerForModule (cg->module);
  LLVMAddPromoteMemoryToRegisterPass (cg->pass);
  LLVMInitializeFunctionPassManager (cg->pass);

  cg->function = NULL;

  return cg;
}


void
CG_Destroy (struct CG *cg)
{
  LLVMDisposePassManager (cg->pass);
  LLVMDisposeBuilder (cg->builder);
  LLVMDisposeModule (cg->module);
  LLVMContextDispose (cg->context);
  free (cg);
}


LLVMValueRef
CG_Generate_Alloca (struct CG* cg, const char *name, LLVMTypeRef type)
{
  LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock (cg->function);
  LLVMValueRef first = LLVMGetFirstInstruction (entry);

  LLVMBasicBlockRef bb = LLVMGetInsertBlock (cg->builder);

  if (first)
    LLVMPositionBuilderBefore (cg->builder, first);
  else
    LLVMPositionBuilderAtEnd (cg->builder, entry);

  LLVMValueRef alloca = LLVMBuildAlloca (cg->builder, type, name);

  LLVMPositionBuilderAtEnd (cg->builder, bb);

  return alloca;
}


LLVMValueRef
CG_Generate_LValue(struct CG *cg, struct AST *ast, struct Scope *scope)
{
  switch (ast->kind)
    {
    case AST_IDENTIFIER:
      {
        const char *name = ast->token.value.s;

        struct Symbol *symbol = Scope_Find (scope, name);

        assert (symbol);

        return symbol->value;
      }
    case AST_UNARY:
      switch (ast->token.kind)
        {
        case TOKEN_STAR:
          {
            LLVMValueRef ptr = CG_Generate(cg, ast->child, scope);
            return ptr;
          }
        default:
          return NULL;
        }
    default:
      return NULL;
    }
}


LLVMValueRef CG_Generate_Program (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Prototype (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Function (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Alias (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Variable (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_If (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_While (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Unary (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Binary (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Cast (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Compound (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Identifier (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_Call (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_I64 (struct CG *, struct AST *, struct Scope *);

LLVMValueRef CG_Generate_F64 (struct CG *, struct AST *, struct Scope *);


LLVMValueRef
CG_Generate_Program (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  struct Scope *child;

  child = Scope_Create (scope);

  struct AST *current = ast->child;

  while (current)
    {
      CG_Generate (cg, current, child);
      current = current->next;
    }

  Scope_Destroy_Value (child);

  return NULL;
}


LLVMValueRef
CG_Generate_Prototype (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  const char *name = ast->child->token.value.s;

  LLVMTypeRef type = Type_As_LLVM (ast->type, cg->context);

  LLVMValueRef function = LLVMAddFunction (cg->module, name, type);

  LLVMValueRef argument = LLVMGetFirstParam (function);

  struct AST *current = ast->child->next;

  while (argument != NULL && current != NULL)
    {
      LLVMSetValueName (argument, current->token.value.s);
      argument = LLVMGetNextParam (argument);
      current = current->next;
    }

  return function;
}


LLVMValueRef
CG_Generate_Function (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  const char *name = ast->child->child->token.value.s;
  LLVMValueRef function = LLVMGetNamedFunction (cg->module, name);

  if (!function)
    function = CG_Generate (cg, ast->child, scope);

  cg->function = function;

  assert (function);

  struct Scope *child;

  child = Scope_Create (scope);

  LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext (cg->context, function,
                                                        "entry");
  LLVMPositionBuilderAtEnd (cg->builder, bb);

  size_t i = 0;

  for (LLVMValueRef argument = LLVMGetFirstParam (function); argument != NULL;
       argument = LLVMGetNextParam (argument), ++i)
    {
      const char *name = LLVMGetValueName (argument);
      LLVMTypeRef type = Type_As_LLVM (ast->type->value.function.in[i],
                                       cg->context);

      LLVMValueRef alloca = CG_Generate_Alloca (cg, name, type);

      LLVMBuildStore (cg->builder, argument, alloca);

      Scope_Add (child, Symbol_Create_Value (name, alloca));
    }

  LLVMValueRef out = CG_Generate (cg, ast->child->next, child);

  if (ast->type->value.function.out->kind != TYPE_VOID)
    LLVMBuildRet (cg->builder, out);
  else
    LLVMBuildRetVoid (cg->builder);

  LLVMRunFunctionPassManager (cg->pass, function);

  Scope_Destroy_Value (child);
  cg->function = NULL;

  return function;
}


LLVMValueRef
CG_Generate_Variable (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  const char *name = ast->token.value.s;
  LLVMTypeRef type = Type_As_LLVM (ast->type, cg->context);
  LLVMValueRef alloca = CG_Generate_Alloca (cg, name, type);

  if (ast->child)
    {
      LLVMValueRef value = CG_Generate (cg, ast->child, scope);
      LLVMBuildStore (cg->builder, value, alloca);
    }

  Scope_Add (scope, Symbol_Create_Value (name, alloca));

  return alloca;
}


LLVMValueRef
CG_Generate_While (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  LLVMValueRef fn = cg->function;

  LLVMBasicBlockRef cond_block = LLVMAppendBasicBlock (fn, "while.cond");
  LLVMBasicBlockRef body_block = LLVMAppendBasicBlock (fn, "while.body");
  LLVMBasicBlockRef end_block = LLVMAppendBasicBlock (fn, "while.end");

  LLVMBuildBr (cg->builder, cond_block);

  LLVMPositionBuilderAtEnd (cg->builder, cond_block);
  LLVMValueRef cond_value = CG_Generate (cg, ast->child, scope);

  LLVMBuildCondBr (cg->builder, cond_value, body_block, end_block);
  cond_block = LLVMGetInsertBlock (cg->builder);

  LLVMPositionBuilderAtEnd (cg->builder, body_block);
  LLVMValueRef body_value = CG_Generate (cg, ast->child->next, scope);

  LLVMBuildBr (cg->builder, cond_block);
  body_block = LLVMGetInsertBlock (cg->builder);

  LLVMPositionBuilderAtEnd (cg->builder, end_block);

  return NULL;
}

LLVMValueRef
CG_Generate_Unary (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  enum Token_Kind operator = ast->token.kind;

  if (operator == TOKEN_AMPERSAND)
    return CG_Generate_LValue (cg, ast->child, scope);

  LLVMValueRef value = CG_Generate (cg, ast->child, scope);

  struct Type *t1 = ast->child->type;
  enum Type_Kind k1 = t1->kind;

  if (operator == TOKEN_STAR)
    {
      LLVMTypeRef type = Type_As_LLVM (t1->value.base, cg->context);
      return LLVMBuildLoad2 (cg->builder, type, value, "deref");
    }

  if (Type_Kind_Is_Integer (k1))
    switch (operator)
      {
      case TOKEN_PLUS:
        return value;
      case TOKEN_MINUS:
        return LLVMBuildNeg (cg->builder, value, "neg");
      default:
        assert (0);
      }

  if (Type_Kind_Is_Float (k1))
    switch (operator)
      {
      case TOKEN_PLUS:
        return value;
      case TOKEN_MINUS:
        return LLVMBuildFNeg (cg->builder, value, "fneg");
      default:
        assert (0);
      }

  return NULL;

}

LLVMValueRef
CG_Generate_Binary (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  enum Token_Kind operator = ast->token.kind;

  if (operator == TOKEN_EQUALS)
    {
      LLVMValueRef ptr = CG_Generate_LValue (cg, ast->child, scope);

      LLVMValueRef right = CG_Generate (cg, ast->child->next, scope);
      LLVMBuildStore (cg->builder, right, ptr);

      return right;
    }

  LLVMValueRef left = CG_Generate (cg, ast->child, scope);
  LLVMValueRef right = CG_Generate (cg, ast->child->next, scope);

  struct Type *t1 = ast->child->type;
  struct Type *t2 = ast->child->next->type;

  enum Type_Kind k1 = t1->kind;
  enum Type_Kind k2 = t2->kind;

  int signedness = Type_Kind_Is_Signed (k1);

  if (k1 == TYPE_POINTER && Type_Kind_Is_Integer (k2))
    {
      switch (operator)
        {
        case TOKEN_PLUS:
          {
            LLVMValueRef indices[] = { right };

            // Handle *Void as bytes.
            LLVMTypeRef type =
                t1->value.base->kind == TYPE_VOID
                ? LLVMInt8Type()
                : Type_As_LLVM (t1->value.base, cg->context);

            return LLVMBuildGEP2 (cg->builder, type, left, indices, 1, "gep");
          }
        default:
          Diagnostic (ast->child->location, D_ERROR, "unsupported operand");
          Halt ();
          break;
        }
    }

  if (Type_Kind_Is_Integer (k1))
    switch (operator)
      {
      case TOKEN_PLUS:
        return LLVMBuildAdd (cg->builder, left, right, "add");
      case TOKEN_MINUS:
        return LLVMBuildSub (cg->builder, left, right, "sub");
      case TOKEN_STAR:
        return LLVMBuildMul (cg->builder, left, right, "mul");
      case TOKEN_SLASH:
        return signedness ? LLVMBuildSDiv (cg->builder, left, right, "sdiv")
                          : LLVMBuildUDiv (cg->builder, left, right, "udiv");
      case TOKEN_LT:
        return LLVMBuildICmp (cg->builder, signedness ? LLVMIntSLT : LLVMIntULT,
                              left, right, "ilt");
      case TOKEN_LE:
        return LLVMBuildICmp (cg->builder, signedness ? LLVMIntSLE : LLVMIntULE,
                              left, right, "ile");
      case TOKEN_GT:
        return LLVMBuildICmp (cg->builder, signedness ? LLVMIntSGT : LLVMIntUGT,
                              left, right, "igt");
      case TOKEN_GE:
        return LLVMBuildICmp (cg->builder, signedness ? LLVMIntSGE : LLVMIntUGE,
                              left, right, "ige");
      default:
        assert (0);
      }

  if (Type_Kind_Is_Float (k1))
    switch (operator)
      {
      case TOKEN_PLUS:
        return LLVMBuildFAdd (cg->builder, left, right, "fadd");
      case TOKEN_MINUS:
        return LLVMBuildFSub (cg->builder, left, right, "fsub");
      case TOKEN_STAR:
        return LLVMBuildFMul (cg->builder, left, right, "fmul");
      case TOKEN_SLASH:
        return LLVMBuildFDiv (cg->builder, left, right, "fdiv");

      case TOKEN_LT:
        return LLVMBuildFCmp (cg->builder, LLVMRealOLT, left, right, "flt");
      case TOKEN_LE:
        return LLVMBuildFCmp (cg->builder, LLVMRealOLE, left, right, "fle");
      case TOKEN_GT:
        return LLVMBuildFCmp (cg->builder, LLVMRealOGT, left, right, "fgt");
      case TOKEN_GE:
        return LLVMBuildFCmp (cg->builder, LLVMRealOGE, left, right, "fge");
      default:
        assert (0);
      }

  return NULL;
}


LLVMValueRef
CG_Generate_Cast (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  struct Type *t1 = ast->child->type;
  struct Type *t2 = ast->type;

  LLVMValueRef value = CG_Generate (cg, ast->child, scope);

  // No need to cast when types match!
  if (Type_Match (t1, t2))
    return value;

  enum Type_Kind k1 = t1->kind;
  enum Type_Kind k2 = t2->kind;

  LLVMTypeRef type = Type_As_LLVM (t2, cg->context);

  // Integer <-> Integer
  if (Type_Kind_Is_Integer (k1) && Type_Kind_Is_Integer (k2))
    {
      int w1 = Type_Kind_Width (k1);
      int w2 = Type_Kind_Width (k2);

      if (w1 < w2)
        return Type_Kind_Is_Signed (k1)
          ? LLVMBuildSExt (cg->builder, value, type, "sext")
          : LLVMBuildZExt (cg->builder, value, type, "zext");
      else if (w1 > w2)
        return LLVMBuildTrunc (cg->builder, value, type, "trunc");
      else
        return LLVMBuildBitCast (cg->builder, value, type, "bitcast");
    }

  // Integer -> Float
  if (Type_Kind_Is_Integer (k1) && Type_Kind_Is_Float (k2))
    return Type_Kind_Is_Signed (k1)
      ? LLVMBuildSIToFP (cg->builder, value, type, "sitofp")
      : LLVMBuildUIToFP (cg->builder, value, type, "uitofp");

  // Float -> Integer
  if (Type_Kind_Is_Float (k1) && Type_Kind_Is_Integer (k2))
    return Type_Kind_Is_Signed (k2)
      ? LLVMBuildFPToSI (cg->builder, value, type, "fptosi")
      : LLVMBuildFPToUI (cg->builder, value, type, "fptoui");

  // Float <-> Float
  if (Type_Kind_Is_Float (k1) && Type_Kind_Is_Float (k2))
    {
      int w1 = Type_Kind_Width (k1);
      int w2 = Type_Kind_Width (k2);

      if (w1 < w2)
        return LLVMBuildFPExt (cg->builder, value, type, "fpext");
      else
        return LLVMBuildFPTrunc (cg->builder, value, type, "fptrunc");
    }

  if (k1 == TYPE_BOOL)
    {
      if (Type_Kind_Is_Integer (k2))
        return LLVMBuildZExt (cg->builder, value, type, "zext");
      if (Type_Kind_Is_Float (k2))
        return LLVMBuildUIToFP (cg->builder, value, type, "booltof");
    }

  if (k2 == TYPE_BOOL)
    {
      if (Type_Kind_Is_Integer (k1))
        return LLVMBuildICmp(cg->builder, LLVMIntNE, value, LLVMConstInt(Type_As_LLVM (t1, cg->context), 0, 0), "int_to_bool");
      if (Type_Kind_Is_Float (k1))
        return LLVMBuildFCmp(cg->builder, LLVMRealUNE, value, LLVMConstReal(Type_As_LLVM (t1, cg->context), 0.0), "fp_to_bool");
    }

  if (k1 == TYPE_POINTER && k2 == TYPE_POINTER)
    return LLVMBuildBitCast(cg->builder, value, type, "bitcast");

  if (k1 == TYPE_POINTER && Type_Kind_Is_Integer (k2))
    return LLVMBuildPtrToInt (cg->builder, value, type, "ptrtoint");

  assert (0);

  // printf ("IMPLEMENT CASTING HERE\n");
  // assert (0);
}


LLVMValueRef
CG_Generate_Compound (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  struct AST *current = ast->child;

  struct Scope *child = Scope_Create (scope);

  LLVMValueRef result;

  while (current)
    {
      result = CG_Generate (cg, current, child);
      current = current->next;
    }

  Scope_Destroy_Value (child);

  return result;
}


LLVMValueRef
CG_Generate_Identifier (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  const char *name = ast->token.value.s;

  struct Symbol *symbol = Scope_Find (scope, name);

  assert (symbol);

  LLVMValueRef value = symbol->value;

  LLVMTypeRef type = LLVMGetElementType (LLVMTypeOf (value));

  return LLVMBuildLoad2 (cg->builder, type, value, name);
}


LLVMValueRef
CG_Generate_Call (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  const char *name = ast->child->token.value.s;

  struct Type_Function type_f = ast->child->type->value.function;

  LLVMValueRef function = LLVMGetNamedFunction (cg->module, name);

  assert (function);

  LLVMTypeRef type = LLVMGetElementType (LLVMTypeOf (function));

  LLVMValueRef arguments[type_f.in_n];

  struct AST *current = ast->child->next;

  for (size_t i = 0; i < type_f.in_n; ++i)
    {
      arguments[i] = CG_Generate (cg, current, scope);
      current = current->next;
    }

  return LLVMBuildCall2 (cg->builder, type, function, arguments, type_f.in_n,
                         "call");
}


LLVMValueRef
CG_Generate_I64 (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  return LLVMConstInt(Type_As_LLVM (ast->type, cg->context),
                      ast->token.value.i64, 0);
}


LLVMValueRef
CG_Generate_F64 (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  return LLVMConstReal(Type_As_LLVM (ast->type, cg->context),
                       ast->token.value.f64);
}


LLVMValueRef
CG_Generate (struct CG *cg, struct AST *ast, struct Scope *scope)
{
  // printf ("CG_Generate (%s)\n", AST_Kind_String (ast->kind));
  switch (ast->kind)
    {
    case AST_PROGRAM:
      return CG_Generate_Program (cg, ast, scope);

    case AST_PROTOTYPE:
      return CG_Generate_Prototype (cg, ast, scope);
    case AST_FUNCTION:
      return CG_Generate_Function (cg, ast, scope);

    case AST_ALIAS:
      return NULL;

    case AST_VARIABLE:
      return CG_Generate_Variable (cg, ast, scope);
    case AST_IF:
      return NULL;
    case AST_WHILE:
      return CG_Generate_While (cg, ast, scope);

    case AST_UNARY:
      return CG_Generate_Unary (cg, ast, scope);
    case AST_BINARY:
      return CG_Generate_Binary (cg, ast, scope);
    case AST_CAST:
      return CG_Generate_Cast (cg, ast, scope);

    case AST_COMPOUND:
      return CG_Generate_Compound (cg, ast, scope);
    case AST_IDENTIFIER:
      return CG_Generate_Identifier (cg, ast, scope);
    case AST_CALL:
      return CG_Generate_Call (cg, ast, scope);
    case AST_I64:
      return CG_Generate_I64 (cg, ast, scope);
    case AST_F64:
      return CG_Generate_F64 (cg, ast, scope);
    }
}


