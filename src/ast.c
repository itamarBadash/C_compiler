#include "ast.h"
#include <stdlib.h>

ast_node* create_ast_node(ast_node_type type) {
  ast_node *node = (ast_node*)calloc(1, sizeof(ast_node));
  if (node != NULL) {
    node->type = type;
  }
  return node;
}

void free_ast(ast_node *node) {
  if (node == NULL) {
    return;
  }

  switch (node->type) {
    case AST_NODE_TYPE_BINARY_OP:
      if (node->binary_op.op.value != NULL) {
        free(node->binary_op.op.value);
      }
      free_ast(node->binary_op.left);
      free_ast(node->binary_op.right);
      break;

    case AST_NODE_TYPE_UNARY_OP:
      if (node->unary_op.op.value != NULL) {
        free(node->unary_op.op.value);
      }
      free_ast(node->unary_op.operand);
      break;

    case AST_NODE_TYPE_ASSIGNMENT:
      if (node->assignment.op.value != NULL) {
        free(node->assignment.op.value);
      }
      free_ast(node->assignment.left);
      free_ast(node->assignment.right);
      break;

    case AST_NODE_TYPE_TERNARY:
      free_ast(node->ternary.condition);
      free_ast(node->ternary.true_branch);
      free_ast(node->ternary.false_branch);
      break;

    case AST_NODE_TYPE_IF:
      free_ast(node->if_stmt.condition);
      free_ast(node->if_stmt.then_branch);
      free_ast(node->if_stmt.else_branch);
      break;

    case AST_NODE_TYPE_WHILE:
      free_ast(node->while_stmt.condition);
      free_ast(node->while_stmt.body);
      break;

    case AST_NODE_TYPE_FOR:
      free_ast(node->for_stmt.init);
      free_ast(node->for_stmt.condition);
      free_ast(node->for_stmt.increment);
      free_ast(node->for_stmt.body);
      break;

    case AST_NODE_TYPE_BLOCK:
      if (node->block.statements != NULL) {
        for (int i = 0; i < node->block.count; i++) {
          free_ast(node->block.statements[i]);
        }
        free(node->block.statements);
      }
      break;

    case AST_NODE_TYPE_FUNCTION_CALL:
      free_ast(node->function_call.callable);
      if (node->function_call.arguments != NULL) {
        for (int i = 0; i < node->function_call.arg_count; i++) {
          free_ast(node->function_call.arguments[i]);
        }
        free(node->function_call.arguments);
      }
      break;

    case AST_NODE_TYPE_FUNCTION_DEF:
      if (node->function_def.name != NULL) {
        free(node->function_def.name);
      }
      if (node->function_def.return_type != NULL) {
        free_type_info(node->function_def.return_type);
      }
      if (node->function_def.parameters != NULL) {
        for (int i = 0; i < node->function_def.param_count; i++) {
          free(node->function_def.parameters[i]);
        }
        free(node->function_def.parameters);
      }
      if (node->function_def.param_types != NULL) {
        for (int i = 0; i < node->function_def.param_count; i++) {
          free_type_info(node->function_def.param_types[i]);
        }
        free(node->function_def.param_types);
      }
      free_ast(node->function_def.body);
      break;

    case AST_NODE_TYPE_RETURN:
      free_ast(node->return_stmt.return_value);
      break;

    case AST_NODE_TYPE_ARRAY_SUBSCRIPT:
      free_ast(node->array_subscript.left);
      free_ast(node->array_subscript.index);
      break;

    case AST_NODE_TYPE_MEMBER_ACCESS:
      free_ast(node->member_access.left);
      if (node->member_access.member_name != NULL) {
        free(node->member_access.member_name);
      }
      break;

    case AST_NODE_TYPE_VAR_DECL:
      if (node->var_decl.type != NULL) {
        free_type_info(node->var_decl.type);
      }
      if (node->var_decl.var_name != NULL) {
        free(node->var_decl.var_name);
      }
      free_ast(node->var_decl.init_value);
      break;
    case AST_NODE_TYPE_PROGRAM:
      if (node->program.declarations != NULL) {
        for (int i = 0; i < node->program.count; i++) {
          free_ast(node->program.declarations[i]);
        }
        free(node->program.declarations);
      }
      break;

    case AST_NODE_TYPE_NUMBER:
    case AST_NODE_TYPE_IDENTIFIER:
    case AST_NODE_TYPE_STRING:
    case AST_NODE_TYPE_CHAR_LITERAL:
      if (node->tok.value != NULL) {
        free(node->tok.value);
      }
      break;

    case AST_NODE_TYPE_DO_WHILE:
      free_ast(node->do_while_stmt.body);
      free_ast(node->do_while_stmt.condition);
      break;
      
    case AST_NODE_TYPE_SWITCH:
      free_ast(node->switch_stmt.condition);
      free_ast(node->switch_stmt.body);
      break;
      
    case AST_NODE_TYPE_CASE:
      free_ast(node->case_stmt.value);
      free_ast(node->case_stmt.body);
      break;
      
    case AST_NODE_TYPE_DEFAULT:
      free_ast(node->default_stmt.body);
      break;
      
    case AST_NODE_TYPE_BREAK:
    case AST_NODE_TYPE_CONTINUE:
      break;

    case AST_NODE_TYPE_STRUCT_DEF:
      if (node->struct_def.tag_name) free(node->struct_def.tag_name);
      if (node->struct_def.members) {
        for (int i = 0; i < node->struct_def.member_count; i++) {
          free_ast(node->struct_def.members[i]);
        }
        free(node->struct_def.members);
      }
      break;

    case AST_NODE_TYPE_ENUM_DEF:
      if (node->enum_def.tag_name) free(node->enum_def.tag_name);
      if (node->enum_def.enumerators) {
        for (int i = 0; i < node->enum_def.enumerator_count; i++) {
          free(node->enum_def.enumerators[i]);
          if (node->enum_def.values && node->enum_def.values[i]) {
            free_ast(node->enum_def.values[i]);
          }
        }
        free(node->enum_def.enumerators);
        if (node->enum_def.values) {
          free(node->enum_def.values);
        }
      }
      break;

    case AST_NODE_TYPE_CAST:
      if (node->cast_expr.type) free_type_info(node->cast_expr.type);
      if (node->cast_expr.operand) free_ast(node->cast_expr.operand);
      break;

    case AST_NODE_TYPE_INIT_LIST:
      if (node->init_list.items) {
        for (int i = 0; i < node->init_list.count; i++) {
          if (node->init_list.items[i].member_name) {
              free(node->init_list.items[i].member_name);
          }
          if (node->init_list.items[i].index) {
              free_ast(node->init_list.items[i].index);
          }
          if (node->init_list.items[i].value) {
              free_ast(node->init_list.items[i].value);
          }
        }
        free(node->init_list.items);
      }
      break;
      
    case AST_NODE_TYPE_COMPOUND_LITERAL:
      if (node->compound_literal.type) free_type_info(node->compound_literal.type);
      if (node->compound_literal.init_list) free_ast(node->compound_literal.init_list);
      break;
      
    case AST_NODE_TYPE_DESIGNATOR:
      if (node->designator.member_name) free(node->designator.member_name);
      if (node->designator.index) free_ast(node->designator.index);
      break;

    case AST_NODE_TYPE_GOTO:
      if (node->goto_stmt.label_name) free(node->goto_stmt.label_name);
      break;

    case AST_NODE_TYPE_LABEL:
      if (node->label_stmt.label_name) free(node->label_stmt.label_name);
      free_ast(node->label_stmt.statement);
      break;

    case AST_NODE_TYPE_UNKNOWN:
    default:
      break;
  }

  free(node);
}

type_info* create_type_info(type_kind kind) {
  type_info *type = (type_info*)calloc(1, sizeof(type_info));
  if (type) {
    type->kind = kind;
    type->array_size = -1;
  }
  return type;
}

void free_type_info(type_info *type) {
  if (!type) return;
  if (type->tag_name) {
    free(type->tag_name);
  }
  if (type->ptr_to) {
    free_type_info(type->ptr_to);
  }
  if (type->param_types) {
    for (int i = 0; i < type->param_count; i++) {
      free_type_info(type->param_types[i]);
    }
    free(type->param_types);
  }
  if (type->array_size_expr) {
      free_ast(type->array_size_expr);
  }
  if (type->base_type.value) {
    free(type->base_type.value);
  }
  free(type);
}