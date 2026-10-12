#ifndef CONVERSION_WALK_H
#define CONVERSION_WALK_H

#include <string>
#include <vector>

extern "C" {
#include "ast.h"
#include "symbol_table.h"
#include "target.h"
}

struct ConversionWalk {
  std::vector<std::string> problems;
  int kinds[4] = {0, 0, 0, 0};

  static type_info *resolved(type_info *type) {
    while (type != nullptr && type->kind == TYPE_TYPEDEF && type->symbol != nullptr)
      type = type->symbol->type;
    return type;
  }

  static bool is_kind(type_info *type, type_kind kind) {
    type = resolved(type);
    return type != nullptr && type->kind == kind;
  }

  static bool is_prim(type_info *type, prim_kind prim) {
    type = resolved(type);
    return type != nullptr && type->kind == TYPE_PRIMITIVE && type->prim == prim &&
           !type->is_complex;
  }

  static int arithmetic_code(type_info *type) {
    if (type->kind == TYPE_ENUM)
      return type->symbol != nullptr ? type->symbol->enum_type * 2 : -1;
    if (type->kind == TYPE_PRIMITIVE)
      return type->prim * 2 + type->is_complex;
    return -2;
  }

  static bool same(type_info *a, type_info *b) {
    a = resolved(a);
    b = resolved(b);
    if (a == nullptr || b == nullptr)
      return true;
    if (a->kind == TYPE_POINTER || b->kind == TYPE_POINTER)
      return a->kind == b->kind;
    if (a->kind == TYPE_STRUCT || a->kind == TYPE_UNION)
      return a->kind == b->kind && a->symbol == b->symbol;
    return arithmetic_code(a) == arithmetic_code(b);
  }

  static bool is_value(ast_node *node) {
    type_info *type = resolved(node->expr_type);
    return !node->is_lvalue &&
           (type == nullptr || (type->kind != TYPE_ARRAY && type->kind != TYPE_FUNCTION));
  }

  static bool promoted_integer(type_info *type) {
    type = resolved(type);
    if (type == nullptr || (type->kind != TYPE_PRIMITIVE && type->kind != TYPE_ENUM))
      return false;
    int code = arithmetic_code(type) / 2;
    return code >= PRIM_INT && code <= PRIM_ULLONG;
  }

  void report(ast_node *at, const std::string &what) {
    problems.push_back(std::to_string(at->loc.line) + ":" + std::to_string(at->loc.column) + ": " +
                       what);
  }

  void need_value(ast_node *operand, ast_node *at, const char *what) {
    if (operand != nullptr && operand->expr_type != nullptr && !is_value(operand))
      report(at, std::string(what) + " is not a value");
  }

  void need(ast_node *operand, type_info *type, ast_node *at, const char *what) {
    if (operand == nullptr || operand->expr_type == nullptr || type == nullptr)
      return;
    if (!is_value(operand))
      report(at, std::string(what) + " is not a value");
    else if (!same(operand->expr_type, type))
      report(at, std::string(what) + " does not have the type it works in");
  }

  void need_bool(ast_node *operand, ast_node *at, const char *what) {
    if (operand != nullptr && operand->expr_type != nullptr &&
        (!is_value(operand) || !is_prim(operand->expr_type, PRIM_BOOL)))
      report(at, std::string(what) + " is not a _Bool value");
  }

  void need_index(ast_node *operand, ast_node *at, const char *what) {
    if (operand != nullptr && operand->expr_type != nullptr &&
        (!is_value(operand) || !is_prim(operand->expr_type, target_current()->ptrdiff_type)))
      report(at, std::string(what) + " is not a ptrdiff_t value");
  }

  void check_conversion(ast_node *node) {
    ast_node *operand = node->conversion.operand;
    type_info *from = resolved(operand->expr_type);
    kinds[node->conversion.kind]++;
    switch (node->conversion.kind) {
    case CONVERSION_LVALUE: {
      type_info *to = resolved(node->expr_type);
      if (!operand->is_lvalue || from == nullptr || from->kind == TYPE_ARRAY ||
          from->kind == TYPE_FUNCTION)
        report(node, "an lvalue conversion of something that is not an lvalue");
      else if (to->is_const || to->is_volatile || to->is_restrict)
        report(node, "an lvalue conversion keeps a qualifier");
      break;
    }
    case CONVERSION_ARRAY_TO_POINTER:
      if (!is_kind(from, TYPE_ARRAY) || !is_kind(node->expr_type, TYPE_POINTER))
        report(node, "an array conversion of something that is not an array");
      break;
    case CONVERSION_FUNCTION_TO_POINTER:
      if (!is_kind(from, TYPE_FUNCTION) || !is_kind(node->expr_type, TYPE_POINTER))
        report(node, "a function conversion of something that is not a function");
      break;
    case CONVERSION_VALUE:
      if (!is_value(operand))
        report(node, "a value conversion of something that is not a value");
      break;
    }
  }

  void check_unary(ast_node *node) {
    token_type op = node->unary_op.op.type;
    ast_node *operand = node->unary_op.operand;
    if (op == TOKEN_PLUS || op == TOKEN_MINUS || op == TOKEN_TILDE)
      need(operand, node->expr_type, node, "a unary operand");
    else if (op == TOKEN_NOT)
      need_bool(operand, node, "the operand of '!'");
    else if (op == TOKEN_STAR)
      need_value(operand, node, "the operand of '*'");
    else if ((op == TOKEN_PLUS_PLUS || op == TOKEN_MINUS_MINUS) &&
             node->unary_op.computation_type == nullptr)
      report(node, "an increment without a computation type");
  }

  void check_binary(ast_node *node) {
    token_type op = node->binary_op.op.type;
    ast_node *left = node->binary_op.left;
    ast_node *right = node->binary_op.right;
    if (left->expr_type == nullptr || right->expr_type == nullptr)
      return;
    bool left_pointer = is_kind(left->expr_type, TYPE_POINTER);
    bool right_pointer = is_kind(right->expr_type, TYPE_POINTER);
    bool comparison = op == TOKEN_LT || op == TOKEN_GT || op == TOKEN_LTE || op == TOKEN_GTE ||
                      op == TOKEN_EQ || op == TOKEN_NEQ;
    if (op == TOKEN_COMMA) {
      need_value(right, node, "the right operand of ','");
    } else if (op == TOKEN_AND || op == TOKEN_OR) {
      need_bool(left, node, "the left operand of a logical operator");
      need_bool(right, node, "the right operand of a logical operator");
    } else if (op == TOKEN_LSHIFT || op == TOKEN_RSHIFT) {
      need(left, node->expr_type, node, "a shifted value");
      if (!is_value(right) || !promoted_integer(right->expr_type))
        report(node, "a shift count is not a promoted value");
    } else if (left_pointer || right_pointer) {
      need_value(left, node, "a pointer operation's left operand");
      need_value(right, node, "a pointer operation's right operand");
      if ((op == TOKEN_PLUS || op == TOKEN_MINUS) && left_pointer != right_pointer)
        need_index(left_pointer ? right : left, node, "a pointer's integer operand");
      if (comparison && left_pointer != right_pointer)
        report(node, "a pointer is compared with something that is not a pointer");
    } else {
      need(left, right->expr_type, node, "an arithmetic operand");
      need_value(right, node, "an arithmetic operand");
      if (!comparison)
        need(left, node->expr_type, node, "an arithmetic operand");
    }
  }

  void check_assignment(ast_node *node) {
    ast_node *right = node->assignment.right;
    token_type op = node->assignment.op.type;
    type_info *computation = node->assignment.computation_type;
    if (!node->assignment.left->is_lvalue)
      report(node, "an assignment's left operand is not an lvalue");
    if (op == TOKEN_ASSIGN)
      need(right, node->expr_type, node, "an assigned value");
    else if (computation == nullptr)
      report(node, "a compound assignment without a computation type");
    else if (op == TOKEN_LSHIFT_ASSIGN || op == TOKEN_RSHIFT_ASSIGN)
      need_value(right, node, "a shift count");
    else if (is_kind(computation, TYPE_POINTER))
      need_index(right, node, "a pointer's integer operand");
    else
      need(right, computation, node, "a compound assignment's right operand");
  }

  void check_call(ast_node *node) {
    ast_node *callee = node->function_call.callable;
    need_value(callee, node, "the called function");
    type_info *fn = resolved(callee->expr_type);
    fn = fn != nullptr && fn->kind == TYPE_POINTER ? resolved(fn->ptr_to) : nullptr;
    if (fn == nullptr)
      return;
    for (int i = 0; i < node->function_call.arg_count; i++) {
      ast_node *argument = node->function_call.arguments[i];
      if (fn->has_prototype && i < fn->param_count) {
        type_info *parameter = resolved(fn->param_types[i]);
        if (parameter != nullptr &&
            (parameter->kind == TYPE_ARRAY || parameter->kind == TYPE_FUNCTION))
          need_value(argument, node, "an argument");
        else
          need(argument, parameter, node, "an argument");
      } else {
        need_value(argument, node, "an argument");
        type_info *type = resolved(argument->expr_type);
        if (type != nullptr && type->kind == TYPE_PRIMITIVE && !type->is_complex &&
            (type->prim == PRIM_FLOAT || (type->prim >= PRIM_BOOL && type->prim < PRIM_INT)))
          report(node, "an argument without a prototype is not promoted");
      }
    }
  }

  void check_condition(ast_node *condition, ast_node *at) {
    need_bool(condition, at, "a condition");
  }

  void walk_type(type_info *type, type_info *returns) {
    for (; type != nullptr; type = type->ptr_to) {
      ast_node *size = type->array_size_expr;
      if (type->kind == TYPE_ARRAY && type->is_vla && size != nullptr &&
          size->expr_type != nullptr &&
          (!is_value(size) || !is_prim(size->expr_type, target_current()->size_type)))
        report(size, "a variable array size is not a size_t value");
      walk(size, returns);
      for (int i = 0; i < type->param_count; i++) {
        if (type->param_definitions != nullptr)
          walk(type->param_definitions[i], returns);
      }
    }
  }

  void walk(ast_node *node, type_info *returns) {
    if (node == nullptr)
      return;
    switch (node->type) {
    case AST_NODE_TYPE_PROGRAM:
      for (int i = 0; i < node->program.count; i++)
        walk(node->program.declarations[i], returns);
      break;
    case AST_NODE_TYPE_BLOCK:
    case AST_NODE_TYPE_DECL_GROUP:
      for (int i = 0; i < node->block.count; i++)
        walk(node->block.statements[i], returns);
      break;
    case AST_NODE_TYPE_CONVERSION:
      check_conversion(node);
      walk(node->conversion.operand, returns);
      break;
    case AST_NODE_TYPE_UNARY_OP:
      if (node->expr_type != nullptr)
        check_unary(node);
      walk(node->unary_op.operand, returns);
      break;
    case AST_NODE_TYPE_BINARY_OP:
      if (node->expr_type != nullptr)
        check_binary(node);
      walk(node->binary_op.left, returns);
      walk(node->binary_op.right, returns);
      break;
    case AST_NODE_TYPE_ASSIGNMENT:
      if (node->expr_type != nullptr)
        check_assignment(node);
      walk(node->assignment.left, returns);
      walk(node->assignment.right, returns);
      break;
    case AST_NODE_TYPE_TERNARY:
      if (node->expr_type != nullptr) {
        check_condition(node->ternary.condition, node);
        if (!is_prim(node->expr_type, PRIM_VOID)) {
          need(node->ternary.true_branch, node->expr_type, node, "a branch of '?:'");
          need(node->ternary.false_branch, node->expr_type, node, "a branch of '?:'");
        }
      }
      walk(node->ternary.condition, returns);
      walk(node->ternary.true_branch, returns);
      walk(node->ternary.false_branch, returns);
      break;
    case AST_NODE_TYPE_IF:
      check_condition(node->if_stmt.condition, node);
      walk(node->if_stmt.condition, returns);
      walk(node->if_stmt.then_branch, returns);
      walk(node->if_stmt.else_branch, returns);
      break;
    case AST_NODE_TYPE_WHILE:
      check_condition(node->while_stmt.condition, node);
      walk(node->while_stmt.condition, returns);
      walk(node->while_stmt.body, returns);
      break;
    case AST_NODE_TYPE_DO_WHILE:
      check_condition(node->do_while_stmt.condition, node);
      walk(node->do_while_stmt.body, returns);
      walk(node->do_while_stmt.condition, returns);
      break;
    case AST_NODE_TYPE_FOR:
      check_condition(node->for_stmt.condition, node);
      walk(node->for_stmt.init, returns);
      walk(node->for_stmt.condition, returns);
      walk(node->for_stmt.increment, returns);
      walk(node->for_stmt.body, returns);
      break;
    case AST_NODE_TYPE_SWITCH: {
      ast_node *condition = node->switch_stmt.condition;
      if (condition->expr_type != nullptr &&
          (!is_value(condition) || !promoted_integer(condition->expr_type)))
        report(node, "a switch's controlling expression is not a promoted value");
      walk(condition, returns);
      walk(node->switch_stmt.body, returns);
      break;
    }
    case AST_NODE_TYPE_CASE:
      walk(node->case_stmt.value, returns);
      walk(node->case_stmt.body, returns);
      break;
    case AST_NODE_TYPE_DEFAULT:
      walk(node->default_stmt.body, returns);
      break;
    case AST_NODE_TYPE_LABEL:
      walk(node->label_stmt.statement, returns);
      break;
    case AST_NODE_TYPE_RETURN:
      if (returns != nullptr && !is_prim(returns, PRIM_VOID))
        need(node->return_stmt.return_value, returns, node, "a returned value");
      walk(node->return_stmt.return_value, returns);
      break;
    case AST_NODE_TYPE_FUNCTION_DEF:
      walk_type(node->function_def.type, returns);
      walk(node->function_def.body, node->function_def.type->ptr_to);
      break;
    case AST_NODE_TYPE_VAR_DECL: {
      ast_node *init = node->var_decl.init_value;
      if (init != nullptr && init->type != AST_NODE_TYPE_INIT_LIST &&
          !is_kind(node->var_decl.type, TYPE_ARRAY))
        need(init, node->var_decl.type, node, "an initializer");
      walk_type(node->var_decl.type, returns);
      walk(init, returns);
      break;
    }
    case AST_NODE_TYPE_FUNCTION_CALL:
      if (node->expr_type != nullptr)
        check_call(node);
      walk(node->function_call.callable, returns);
      for (int i = 0; i < node->function_call.arg_count; i++)
        walk(node->function_call.arguments[i], returns);
      break;
    case AST_NODE_TYPE_ARRAY_SUBSCRIPT: {
      ast_node *left = node->array_subscript.left;
      ast_node *index = node->array_subscript.index;
      if (node->expr_type != nullptr) {
        need_value(left, node, "a subscripted operand");
        need_value(index, node, "a subscripted operand");
        need_index(is_kind(left->expr_type, TYPE_POINTER) ? index : left, node, "a subscript");
      }
      walk(left, returns);
      walk(index, returns);
      break;
    }
    case AST_NODE_TYPE_MEMBER_ACCESS:
      if (node->expr_type != nullptr && node->member_access.is_pointer)
        need_value(node->member_access.left, node, "the left operand of '->'");
      walk(node->member_access.left, returns);
      break;
    case AST_NODE_TYPE_CAST:
      if (node->expr_type != nullptr && !is_prim(node->expr_type, PRIM_VOID))
        need_value(node->cast_expr.operand, node, "a cast's operand");
      walk(node->cast_expr.definition, returns);
      walk_type(node->cast_expr.type, returns);
      walk(node->cast_expr.operand, returns);
      break;
    case AST_NODE_TYPE_INIT_LIST:
      for (int i = 0; i < node->init_list.count; i++) {
        walk(node->init_list.items[i].index, returns);
        walk(node->init_list.items[i].value, returns);
      }
      break;
    case AST_NODE_TYPE_COMPOUND_LITERAL:
      walk(node->compound_literal.definition, returns);
      walk_type(node->compound_literal.type, returns);
      walk(node->compound_literal.init_list, returns);
      break;
    case AST_NODE_TYPE_STRUCT_DEF:
      for (int i = 0; i < node->struct_def.member_count; i++)
        walk(node->struct_def.members[i], returns);
      break;
    case AST_NODE_TYPE_ENUM_DEF:
      for (int i = 0; node->enum_def.values != nullptr && i < node->enum_def.enumerator_count; i++)
        walk(node->enum_def.values[i], returns);
      break;
    case AST_NODE_TYPE_BUILTIN:
      for (int i = 0; i < node->builtin.arg_count; i++)
        walk(node->builtin.args[i], returns);
      for (int i = 0; i < node->builtin.step_count; i++)
        walk(node->builtin.steps[i].index, returns);
      break;
    default:
      break;
    }
  }
};

static ConversionWalk walk_conversions(ast_node *program) {
  ConversionWalk walk;
  walk.walk(program, nullptr);
  return walk;
}

#endif
