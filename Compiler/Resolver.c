#include "Resolver.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>


struct Type *
Resolver_Resolve_Type (struct Type *type, struct Scope *scope)
{
  switch (type->kind)
    {
    case TYPE_DUMMY:
      {
        struct Token token = type->value.dummy;
        const char *name = token.value.s;

        struct Symbol *symbol;

        symbol = Scope_Find (scope, name);

        if (symbol == NULL)
          {
            Diagnostic (token.location, D_ERROR, "undefined type '%s'", name);
            Halt ();
          }

        return Type_Copy (symbol->type);
      }
    case TYPE_FUNCTION:
      {
        struct Type_Function function = type->value.function;

        struct Type **in = calloc (function.in_n, sizeof (struct Type *));

        for (size_t i = 0; i < function.in_n; ++i)
          in[i] = Resolver_Resolve_Type (function.in[i], scope);

        struct Type *out = Resolver_Resolve_Type (function.out, scope);

        struct Type_Function copy ;
        copy = Type_Function_Create (function.in_n, in, out, function.variadic);

        free (in);

        return Type_Create_Function (copy);
      }
    case TYPE_POINTER:
      return Type_Create_Pointer (
          Resolver_Resolve_Type (type->value.base, scope));
    default:
      // Should be unreachable.
      assert (0);
    }
}


void Resolver_Resolve_Program (struct AST *, struct Scope *);

void Resolver_Resolve_Prototype (struct AST *, struct Scope *);

void Resolver_Resolve_Function (struct AST *, struct Scope *);

void Resolver_Resolve_Alias (struct AST *, struct Scope *);

void Resolver_Resolve_Variable (struct AST *, struct Scope *);

void Resolver_Resolve_If (struct AST *, struct Scope *);

void Resolver_Resolve_While (struct AST *, struct Scope *);

void Resolver_Resolve_Unary (struct AST *, struct Scope *);

void Resolver_Resolve_Binary (struct AST *, struct Scope *);

void Resolver_Resolve_Cast (struct AST *, struct Scope *);

void Resolver_Resolve_Compound (struct AST *, struct Scope *);

void Resolver_Resolve_Identifier (struct AST *, struct Scope *);

void Resolver_Resolve_Call (struct AST *, struct Scope *);

void Resolver_Resolve_I64 (struct AST *, struct Scope *);

void Resolver_Resolve_F64 (struct AST *, struct Scope *);


void
Resolver_Resolve_Program (struct AST *ast, struct Scope *scope)
{
  struct Scope *child;

  child = Scope_Create (scope);

  // ast->scope = child;

  struct AST *current = ast->child;

  while (current)
    {
      Resolver_Resolve (current, child);
      current = current->next;
    }

  Scope_Destroy_Type (child);
}


void
Resolver_Resolve_Prototype (struct AST *ast, struct Scope *scope)
{
  AST_Switch_Type (ast, Resolver_Resolve_Type (ast->type, scope));

  // Don't do the type-checking here, just do a "soft" check.
  if (ast->type->kind == TYPE_FUNCTION)
    {
      struct Type_Function function = ast->type->value.function;

      struct AST *current = ast->child->next;

      size_t i = 0;

      while (i < function.in_n)
        {
          if (!current)
            break;
          ++i, current = current->next;
        }

      if (current != NULL)
        {
          Diagnostic (ast->location, D_ERROR,
                      "too many arguments compared to function-prototype");
          Halt ();
        }

      if (i < function.in_n)
        {
          Diagnostic (ast->location, D_ERROR,
                      "not enough arguments compared to function-prototype");
          Halt ();
        }
    }

  const char *name = ast->child->token.value.s;

  struct Symbol *dup = Scope_Find (scope, name);

  if (dup)
    {
      if (!Type_Match (ast->type, dup->type))
        {
          Diagnostic (ast->child->location, D_ERROR,
                      "prototypes of '%s' are not matching", name);
          Halt ();
        }
    }
  else
    Scope_Add (scope, Symbol_Create_Type (name, ast->type));
}


void
Resolver_Resolve_Function (struct AST *ast, struct Scope *scope)
{
  // Resolve prototype.
  Resolver_Resolve (ast->child, scope);

  struct Type_Function function = ast->child->type->value.function;

  struct Scope *child;

  child = Scope_Create (scope);

  // ast->scope = child;

  // First argument
  struct AST *current = ast->child->child->next;

  size_t i = 0;

  while (current)
    {
      if (!current)
        break;

      const char *name = current->token.value.s;

      // Get prototype's arguments' type.
      struct Type *type = function.in[i];

      Scope_Add (child, Symbol_Create_Type (name, type));

      ++i, current = current->next;
    }

  // Resolve body.
  Resolver_Resolve (ast->child->next, child);

  // Cast body to function's return type.
  if (function.out->kind != TYPE_VOID)
    {
      struct AST *cast;

      cast = AST_Create (ast->child->next->location, AST_CAST);

      cast->type = Type_Copy (function.out);

      AST_Append (cast, ast->child->next);

      ast->child->next = cast;
    }

  ast->type = Type_Copy (ast->child->type);

  Scope_Destroy_Type (child);
}


void
Resolver_Resolve_Alias (struct AST *ast, struct Scope *scope)
{
  const char *name = ast->token.value.s;
  struct Type *type = Resolver_Resolve_Type (ast->type, scope);

  AST_Switch_Type (ast, type);

  Scope_Add (scope, Symbol_Create_Type (name, type));
}


void
Resolver_Resolve_Variable (struct AST *ast, struct Scope *scope)
{
  if (ast->child)
    Resolver_Resolve (ast->child, scope);

  if (ast->type)
    {
      AST_Switch_Type (ast, Resolver_Resolve_Type (ast->type, scope));

      if (ast->child)
        {
          struct AST *cast;

          cast = AST_Create (ast->child->location, AST_CAST);

          cast->type = Type_Copy (ast->type);

          AST_Append (cast, ast->child);

          ast->child = cast;
        }
    }
  else
    {
      ast->type = Type_Copy (ast->child->type);
    }

  const char *name = ast->token.value.s;

  struct Symbol *dup;

  dup = Scope_Find_Shallow (scope, name);

  if (dup)
    {
      Diagnostic (ast->location, D_ERROR, "redefinition of identifier '%s'",
                  name);
      Halt ();
    }

  Scope_Add (scope, Symbol_Create_Type (name, ast->type));
}


void
Resolver_Resolve_If (struct AST *ast, struct Scope *scope)
{
  Resolver_Resolve (ast->child, scope);
  Resolver_Resolve (ast->child->next, scope);

  if (ast->child->next->next)
    Resolver_Resolve (ast->child->next->next, scope);

  struct AST *cast;

  cast = AST_Create (ast->child->location, AST_CAST);

  cast->type = Type_Create (TYPE_BOOL);

  AST_Append (cast, ast->child);

  ast->child = cast;

  ast->child->next = ast->child->child->next;
  ast->child->child->next = NULL;
}


void
Resolver_Resolve_While (struct AST *ast, struct Scope *scope)
{
  Resolver_Resolve (ast->child, scope);
  Resolver_Resolve (ast->child->next, scope);

  struct AST *cast;

  cast = AST_Create (ast->child->location, AST_CAST);

  cast->type = Type_Create (TYPE_BOOL);

  AST_Append (cast, ast->child);

  ast->child = cast;

  ast->child->next = ast->child->child->next;
  ast->child->child->next = NULL;
}


void
Resolver_Resolve_Unary (struct AST *ast, struct Scope *scope)
{
  Resolver_Resolve (ast->child, scope);

  struct Type *type = NULL;

  switch (ast->token.kind)
    {
    case TOKEN_AMPERSAND:
      type = Type_Create_Pointer (Type_Copy (ast->child->type));
      break;
    case TOKEN_STAR:
      if (ast->child->type->kind != TYPE_POINTER)
        {
          Diagnostic (ast->location, D_ERROR, "cannot dereference right-value");
          Halt ();
        }
      type = Type_Copy (ast->child->type->value.base);
      break;
    default:
      type = Type_Copy (ast->child->type);
      break;
    }

  AST_Switch_Type (ast, type);
}


void
Resolver_Resolve_Binary (struct AST *ast, struct Scope *scope)
{
  Resolver_Resolve (ast->child, scope);
  Resolver_Resolve (ast->child->next, scope);

  struct AST *cast;

  cast = AST_Create (ast->child->next->location, AST_CAST);

  cast->type = Type_Copy (ast->child->type);

  AST_Append (cast, ast->child->next);

  ast->child->next = cast;

  switch (ast->token.kind)
    {
    case TOKEN_EQUALS:
    case TOKEN_PLUS:
    case TOKEN_MINUS:
    case TOKEN_STAR:
    case TOKEN_SLASH:
      ast->type = Type_Copy (ast->child->type);
      break;
    case TOKEN_LT:
    case TOKEN_LE:
    case TOKEN_GT:
    case TOKEN_GE:
      ast->type = Type_Create (TYPE_BOOL);
      break;
    default:
      Diagnostic (ast->location, D_ERROR, "undefined operator '%s'",
                  Token_Kind_String (ast->token.kind));
      Halt ();
    }
}


void
Resolver_Resolve_Cast (struct AST *ast, struct Scope *scope)
{
  AST_Switch_Type (ast, Resolver_Resolve_Type (ast->type, scope));
  Resolver_Resolve (ast->child, scope);
}


void
Resolver_Resolve_Compound (struct AST *ast, struct Scope *scope)
{
  struct Scope *child;

  child = Scope_Create (scope);

  // ast->scope = child;

  struct AST *current = ast->child;

  struct Type *type = NULL;

  while (current)
    {
      Resolver_Resolve (current, child);

      if (!current->next && ast->state == 0)
        {
          type = Type_Copy (current->type);
        }

      current = current->next;
    }

  if (type == NULL)
    type = Type_Create (TYPE_VOID);

  ast->type = type;

  Scope_Destroy_Type (child);
}


void
Resolver_Resolve_Identifier (struct AST *ast, struct Scope *scope)
{
  const char *name = ast->token.value.s;

  struct Symbol *symbol;

  symbol = Scope_Find (scope, name);

  if (!symbol)
    {
      Diagnostic (ast->location, D_ERROR, "undefined identifier '%s'", name);
      Halt ();
    }

  ast->type = Type_Copy (symbol->type);
}


void
Resolver_Resolve_Call (struct AST *ast, struct Scope *scope)
{
  Resolver_Resolve (ast->child, scope);

  struct Type_Function function = ast->child->type->value.function;

  if (ast->child->type->kind != TYPE_FUNCTION)
    {
      Diagnostic (ast->location, D_ERROR, "call to '%s' type",
                  Type_Kind_String (ast->child->type->kind));
      Halt ();
    }

  struct AST *current = ast->child->next;
  struct AST *new_head = NULL;
  struct AST *last_cast = NULL;

  size_t i = 0;

  while (i < function.in_n)
    {
      if (!current)
        break;

      struct AST *next = current->next;

      struct Type *type = function.in[i];

      Resolver_Resolve (current, scope);

      struct AST *cast;

      cast = AST_Create (current->location, AST_CAST);

      cast->type = Type_Copy (type);

      cast->child = current;
      cast->next = NULL;

      current->next = NULL;

      if (!new_head)
        {
          new_head = cast;
        }
      else
        {
          last_cast->next = cast;
        }

      last_cast = cast;

      ++i;
      current = next;
    }

  if (current != NULL)
    {
      Diagnostic (ast->location, D_ERROR, "too many arguments supplied");
      Halt ();
    }

  if (i < function.in_n)
    {
      Diagnostic (ast->location, D_ERROR, "not enough arguments supplied");
      Halt ();
    }

  ast->child->next = new_head;

  ast->type = Type_Copy (function.out);
}


void
Resolver_Resolve_I64 (struct AST *ast, struct Scope *scope)
{
  (void)scope;
  ast->type = Type_Create (TYPE_I64);
}


void
Resolver_Resolve_F64 (struct AST *ast, struct Scope *scope)
{
  (void)scope;
  ast->type = Type_Create (TYPE_F64);
}


void
Resolver_Resolve (struct AST *ast, struct Scope *scope)
{
  switch (ast->kind)
    {
    case AST_PROGRAM:
      Resolver_Resolve_Program (ast, scope);
      break;

    case AST_PROTOTYPE:
      Resolver_Resolve_Prototype (ast, scope);
      break;
    case AST_FUNCTION:
      Resolver_Resolve_Function (ast, scope);
      break;

    case AST_ALIAS:
      Resolver_Resolve_Alias (ast, scope);
      break;

    case AST_VARIABLE:
      Resolver_Resolve_Variable (ast, scope);
      break;
    case AST_IF:
      Resolver_Resolve_If (ast, scope);
      break;
    case AST_WHILE:
      Resolver_Resolve_While (ast, scope);
      break;

    case AST_UNARY:
      Resolver_Resolve_Unary (ast, scope);
      break;
    case AST_BINARY:
      Resolver_Resolve_Binary (ast, scope);
      break;
    case AST_CAST:
      Resolver_Resolve_Cast (ast, scope);
      break;

    case AST_COMPOUND:
      Resolver_Resolve_Compound (ast, scope);
      break;
    case AST_IDENTIFIER:
      Resolver_Resolve_Identifier (ast, scope);
      break;
    case AST_CALL:
      Resolver_Resolve_Call (ast, scope);
      break;
    case AST_I64:
      Resolver_Resolve_I64 (ast, scope);
      break;
    case AST_F64:
      Resolver_Resolve_F64 (ast, scope);
      break;
    }
}

