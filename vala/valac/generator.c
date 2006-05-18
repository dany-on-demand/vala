/* generator.c
 *
 * Copyright (C) 2006  Jürg Billeter, Raffaele Sandrini
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author:
 * 	Jürg Billeter <j@bitron.ch>
 *	Raffaele Sandrini <rasa@gmx.ch>
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "context.h"
#include "generator.h"

ValaCodeGenerator *
vala_code_generator_new (ValaContext *context)
{
	ValaCodeGenerator *generator = g_new0 (ValaCodeGenerator, 1);

	generator->context = context;
	
	return generator;
}

static char *
filename_to_define (const char *filename)
{
	char *define = g_path_get_basename (filename);
	char *p;
	for (p = define; *p != '\0'; p++) {
		if (g_ascii_isalnum (*p)) {
			*p = toupper (*p);
		} else {
			*p = '_';
		}
	}
	
	return define;
}

static char *
get_cname_for_type_reference (ValaTypeReference *type, gboolean constant, ValaLocation *location)
{
	switch (type->symbol->type) {
	case VALA_SYMBOL_TYPE_CLASS:
		return g_strdup_printf ("%s *%s", type->symbol->class->cname, type->array_type ? "*" : "");
	case VALA_SYMBOL_TYPE_STRUCT:
		if (constant && type->array_type) {
			return g_strdup_printf ("const %s %s", type->symbol->struct_->cname, (type->symbol->struct_->reference_type ? "*" : ""));
		}
		return g_strdup_printf ("%s%s %s%s", constant ? "const " : "", type->symbol->struct_->cname, (type->symbol->struct_->reference_type ? "*" : ""), type->array_type ? "*" : "");
	case VALA_SYMBOL_TYPE_ENUM:
		return g_strdup_printf ("%s ", type->symbol->enum_->cname);
	case VALA_SYMBOL_TYPE_VOID:
		return g_strdup ("void");
	default:
		err (location, "internal error: unhandled symbol type %d", type->symbol->type);
		return NULL;
	}
}

static char *
get_cname_for_static_expression_type (ValaExpression *expr, ValaLocation *location)
{
	switch (expr->static_type_symbol->type) {
	case VALA_SYMBOL_TYPE_CLASS:
		return g_strdup_printf ("%s *%s", expr->static_type_symbol->class->cname, expr->array_type ? "*" : "");
	case VALA_SYMBOL_TYPE_STRUCT:
		return g_strdup_printf ("%s %s%s", expr->static_type_symbol->struct_->cname, (expr->static_type_symbol->struct_->reference_type ? "*" : ""), expr->array_type ? "*" : "");
	case VALA_SYMBOL_TYPE_VOID:
		return g_strdup ("void");
	default:
		err (location, "internal error: unhandled symbol type %d", expr->static_type_symbol->type);
		return NULL;
	}
}

static void
vala_code_generator_process_methods1 (ValaCodeGenerator *generator, ValaClass *class)
{
	GList *l;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;

	ValaNamespace *namespace = class->namespace;

	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = class->lower_case_cname;
	char *upper_case = class->upper_case_cname;

	for (l = class->methods; l != NULL; l = l->next) {
		ValaMethod *method = l->data;
		
		char *method_return_type_cname = get_cname_for_type_reference (method->return_type, FALSE, method->location);
		if (method->cname == NULL) {
			method->cname = g_strdup_printf ("%s%s_%s", ns_lower, lower_case, method->name);
		}

		if (namespace->import)
			continue;

		char *parameters;
		GList *parameter_list = NULL;
		if ((method->modifiers & VALA_MODIFIER_STATIC) == 0) {
			if (method->modifiers & VALA_MODIFIER_OVERRIDE) {
				ValaClass *super_class = class->base_class;
				while (super_class != NULL) {
					GList *vml;
					for (vml = super_class->methods; vml != NULL; vml = vml->next) {
						ValaMethod *vmethod = vml->data;
						if (strcmp (vmethod->name, method->name) == 0 && (vmethod->modifiers & (VALA_MODIFIER_ABSTRACT | VALA_MODIFIER_VIRTUAL))) {
							break;
						}
					}
					if (vml != NULL) {
						break;
					}
					super_class = super_class->base_class;
				}
				if (super_class == NULL) {
					err (method->location, "error: no overridable method ´%s´ found", method->name);
				}
				method->virtual_super_class = super_class;
				parameter_list = g_list_append (parameter_list, g_strdup_printf ("%s *base", method->virtual_super_class->cname));
			} else {
				parameter_list = g_list_append (parameter_list, g_strdup_printf ("%s *self", class->cname));
			}
		}
		
		GList *pl;
		for (pl = method->formal_parameters; pl != NULL; pl = pl->next) {
			ValaFormalParameter *param = pl->data;
			char *param_string = g_strdup_printf ("%s%s", get_cname_for_type_reference (param->type, FALSE, param->location), param->name);
			parameter_list = g_list_append (parameter_list, param_string);
		}
		
		if (parameter_list == NULL) {
			method->cparameters = g_strdup ("");
		} else {
			method->cparameters = parameter_list->data;
			GList *sl;
			for (sl = parameter_list->next; sl != NULL; sl = sl->next) {
				method->cparameters = g_strdup_printf ("%s, %s", method->cparameters, sl->data);
				g_free (sl->data);
			}
			g_list_free (parameter_list);
		}
		
		if (method->modifiers & VALA_MODIFIER_PUBLIC) {
			method->cdecl1 = g_strdup (method_return_type_cname);
		} else {
			method->cdecl1 = g_strdup_printf ("static %s", method_return_type_cname);
			fprintf (generator->c_file, "%s %s (%s);\n", method->cdecl1, method->cname, method->cparameters);
		}

		if (strcmp (method->name, "init") == 0) {
			if (method->modifiers & VALA_MODIFIER_STATIC) {
				err (method->location, "error: instance initializer must not be static");
			}
			if (method->formal_parameters != NULL) {
				err (method->location, "error: instance initializer must not have any arguments");
			}
			class->init_method = method;
		} else if (strcmp (method->name, "class_init") == 0) {
			if ((method->modifiers & VALA_MODIFIER_STATIC) == 0) {
				err (method->location, "error: class initializer must be static");
			}
			if (method->formal_parameters != NULL) {
				err (method->location, "error: class initializer must not have any arguments");
			}
			class->class_init_method = method;
		}
	}
	fprintf (generator->c_file, "\n");
}

static void
vala_code_generator_process_ns_method (ValaCodeGenerator *generator, ValaNamespace *namespace, ValaMethod *method)
{
	if (method->cname == NULL) {
		method->cname = g_strdup_printf ("%s%s", namespace->lower_case_cname, method->name);
	}
}

static void
vala_code_generator_process_struct_methods1 (ValaCodeGenerator *generator, ValaStruct *struct_)
{
	GList *l;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;

	ValaNamespace *namespace = struct_->namespace;

	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = struct_->lower_case_cname;
	char *upper_case = struct_->upper_case_cname;

	for (l = struct_->methods; l != NULL; l = l->next) {
		ValaMethod *method = l->data;
		
		char *method_return_type_cname = get_cname_for_type_reference (method->return_type, FALSE, method->location);
		if (method->cname == NULL) {
			method->cname = g_strdup_printf ("%s%s_%s", ns_lower, lower_case, method->name);
		}

		char *parameters;
		GList *parameter_list = NULL;
		if ((method->modifiers & VALA_MODIFIER_STATIC) == 0) {
			parameter_list = g_list_append (parameter_list, g_strdup_printf ("%s *self", struct_->cname));
		}
		
		GList *pl;
		for (pl = method->formal_parameters; pl != NULL; pl = pl->next) {
			ValaFormalParameter *param = pl->data;
			char *param_string = g_strdup_printf ("%s%s", get_cname_for_type_reference (param->type, FALSE, param->location), param->name);
			parameter_list = g_list_append (parameter_list, param_string);
		}
		
		if (parameter_list == NULL) {
			method->cparameters = g_strdup ("");
		} else {
			method->cparameters = parameter_list->data;
			GList *sl;
			for (sl = parameter_list->next; sl != NULL; sl = sl->next) {
				method->cparameters = g_strdup_printf ("%s, %s", method->cparameters, sl->data);
				g_free (sl->data);
			}
			g_list_free (parameter_list);
		}
		
		if (method->modifiers & VALA_MODIFIER_PUBLIC) {
			method->cdecl1 = g_strdup (method_return_type_cname);
		} else {
			method->cdecl1 = g_strdup_printf ("static %s", method_return_type_cname);
			fprintf (generator->c_file, "%s %s (%s);\n", method->cdecl1, method->cname, method->cparameters);
		}
	}
	fprintf (generator->c_file, "\n");
}

static void vala_code_generator_process_expression (ValaCodeGenerator *generator, ValaExpression *expr);

static void
vala_code_generator_process_operation_expression (ValaCodeGenerator *generator, ValaExpression *expr)
{
	char *cop = "";
	if (expr->op.left != NULL) {
		vala_code_generator_process_expression (generator, expr->op.left);
	}
	switch (expr->op.type) {
	case VALA_OP_TYPE_PLUS:
		cop = "+";
		break;
	case VALA_OP_TYPE_MINUS:
		cop = "-";
		break;
	case VALA_OP_TYPE_MUL:
		cop = "*";
		break;
	case VALA_OP_TYPE_DIV:
		cop = "/";
		break;
	case VALA_OP_TYPE_EQ:
		cop = "==";
		break;
	case VALA_OP_TYPE_NE:
		cop = "!=";
		break;
	case VALA_OP_TYPE_LT:
		cop = "<";
		break;
	case VALA_OP_TYPE_GT:
		cop = ">";
		break;
	case VALA_OP_TYPE_LE:
		cop = "<=";
		break;
	case VALA_OP_TYPE_GE:
		cop = ">=";
		break;
	case VALA_OP_TYPE_NEG:
		cop = "!";
		break;
	case VALA_OP_TYPE_AND:
		cop = "&&";
		break;
	case VALA_OP_TYPE_BITWISE_AND:
		cop = "&";
		break;
	case VALA_OP_TYPE_OR:
		cop = "||";
		break;
	case VALA_OP_TYPE_BITWISE_OR:
		cop = "|";
		break;
	}
	fprintf (generator->c_file, " %s ", cop);
	vala_code_generator_process_expression (generator, expr->op.right);
}

static ValaSymbol *
get_inherited_member (ValaSymbol *type, const char *name, ValaLocation *location, gboolean break_on_failure)
{
	ValaSymbol *sym;
	sym = g_hash_table_lookup (type->symbol_table, name);
	if (sym != NULL) {
		if (sym->type == VALA_SYMBOL_TYPE_METHOD && (sym->method->modifiers & VALA_MODIFIER_OVERRIDE)) {
			/* don't return overriden method
			 * return virtual method from corresponding super class */
		} else {
			return sym;
		}
	}
	
	if (type->type != VALA_SYMBOL_TYPE_CLASS || type->class->base_class == NULL) {
		if (break_on_failure) {
			err (location, "error: type member ´%s´ not found", name);
		}
		
		return NULL;
	}
	
	return get_inherited_member (type->class->base_class->symbol, name, location, break_on_failure);
}

static void
vala_code_generator_find_static_type_of_expression (ValaCodeGenerator *generator, ValaExpression *expr)
{
	ValaSymbol *sym = NULL, *sym2 = NULL;
	
	if (expr->static_type_symbol != NULL)
		return;
		
	switch (expr->type) {
	case VALA_EXPRESSION_TYPE_ASSIGNMENT:
		break;
	case VALA_EXPRESSION_TYPE_CAST:
		expr->static_type_symbol = expr->cast.type->symbol;
		break;
	case VALA_EXPRESSION_TYPE_ELEMENT_ACCESS:
		vala_code_generator_find_static_type_of_expression (generator, expr->element_access.array);
		if (!expr->element_access.array->array_type) {
			err (expr->element_access.index->location, "error: expression preceding indexer is not an array");
		}
		expr->static_type_symbol = expr->element_access.array->static_type_symbol;
		break;
	case VALA_EXPRESSION_TYPE_INVOCATION:
		vala_code_generator_find_static_type_of_expression (generator, expr->invocation.call);
		expr->static_type_symbol = expr->invocation.call->static_type_symbol->method->return_type->symbol;
		break;
	case VALA_EXPRESSION_TYPE_IS:
		expr->static_type_symbol = g_hash_table_lookup (generator->context->root->symbol_table, "bool");
		break;
	case VALA_EXPRESSION_TYPE_MEMBER_ACCESS:
		vala_code_generator_find_static_type_of_expression (generator, expr->member_access.left);
		sym = expr->member_access.left->static_type_symbol;
		if (sym != NULL && sym->type == VALA_SYMBOL_TYPE_CLASS) {
			expr->static_type_symbol = get_inherited_member (sym, expr->member_access.right, expr->member_access.left->location, TRUE);
			if (expr->static_type_symbol->type == VALA_SYMBOL_TYPE_FIELD) {
				expr->field = expr->static_type_symbol->field;
				expr->array_type = expr->static_type_symbol->field->declaration_statement->variable_declaration->type->array_type;
				expr->static_type_symbol = expr->static_type_symbol->field->declaration_statement->variable_declaration->type->symbol;
			} else if (expr->static_type_symbol->type == VALA_SYMBOL_TYPE_PROPERTY) {
				expr->property = expr->static_type_symbol->property;
				expr->array_type = expr->static_type_symbol->property->return_type->array_type;
				expr->static_type_symbol = expr->static_type_symbol->property->return_type->symbol;
			}
		} else if (sym != NULL && sym->type == VALA_SYMBOL_TYPE_STRUCT) {
			expr->static_type_symbol = g_hash_table_lookup (sym->symbol_table, expr->member_access.right);
			if (expr->static_type_symbol == NULL) {
				err (expr->member_access.left->location, "error: struct member ´%s´ not found", expr->member_access.right);
			}
			if (expr->static_type_symbol->type == VALA_SYMBOL_TYPE_FIELD) {
				expr->field = expr->static_type_symbol->field;
				expr->array_type = expr->static_type_symbol->field->declaration_statement->variable_declaration->type->array_type;
				expr->static_type_symbol = expr->static_type_symbol->field->declaration_statement->variable_declaration->type->symbol;
			}
		} else if (sym != NULL && sym->type == VALA_SYMBOL_TYPE_ENUM) {
			expr->static_symbol = g_hash_table_lookup (sym->symbol_table, expr->member_access.right);
			if (expr->static_symbol == NULL) {
				err (expr->member_access.left->location, "error: enum member ´%s´ not found", expr->member_access.right);
			}

			expr->static_type_symbol = g_hash_table_lookup (generator->context->root->symbol_table, "int");
		} else if (sym != NULL && sym->type == VALA_SYMBOL_TYPE_NAMESPACE) {
			expr->static_type_symbol = g_hash_table_lookup (sym->symbol_table, expr->member_access.right);
			if (expr->static_type_symbol == NULL) {
				err (expr->member_access.left->location, "error: namespace member ´%s´ not found", expr->member_access.right);
			}
		} else {
			err (expr->member_access.left->location, "error: specified symbol type %d can't be used for member access", sym->type);
		}
		break;
	case VALA_EXPRESSION_TYPE_OBJECT_CREATION:
		expr->static_type_symbol = expr->object_creation.type->symbol;
		break;
	case VALA_EXPRESSION_TYPE_OPERATION:
		switch (expr->op.type) {
		case VALA_OP_TYPE_PLUS:
		case VALA_OP_TYPE_MINUS:
			if (expr->op.left != NULL) {
				/* required for pointer arithmetic */
				vala_code_generator_find_static_type_of_expression (generator, expr->op.left);
				expr->static_type_symbol = expr->op.left->static_type_symbol;
			}
			break;
		}
		break;
	case VALA_EXPRESSION_TYPE_PARENTHESIZED:
		vala_code_generator_find_static_type_of_expression (generator, expr->inner);
		expr->static_type_symbol = expr->inner->static_type_symbol;
		break;
	case VALA_EXPRESSION_TYPE_LITERAL_INTEGER:
		break;
	case VALA_EXPRESSION_TYPE_LITERAL_STRING:
		expr->static_type_symbol = g_hash_table_lookup (generator->context->root->symbol_table, "string");
		break;
	case VALA_EXPRESSION_TYPE_SIMPLE_NAME:
		if (expr->static_type_symbol == NULL) {
			/* local variable */
			if (generator->sym != NULL) {
				sym = g_hash_table_lookup (generator->sym->symbol_table, expr->str);
			
				if (sym != NULL) {
					expr->static_type_symbol = sym->typeref->symbol;
					expr->array_type = sym->typeref->array_type;
				}
			}
		}
		
		if (expr->static_type_symbol == NULL) {
			/* member of this */
			expr->static_type_symbol = get_inherited_member (generator->class->symbol, expr->str, expr->location, FALSE);
		}

		if (expr->static_type_symbol == NULL) {
			/* member of the current namespace */
			expr->static_type_symbol = g_hash_table_lookup (generator->class->namespace->symbol->symbol_table, expr->str);
		}

		if (expr->static_type_symbol == NULL) {
			/* member of the root namespace */
			expr->static_type_symbol = g_hash_table_lookup (generator->context->root->symbol_table, expr->str);
		}

		if (expr->static_type_symbol == NULL) {
			/* member of a namespace specified by a using directive */

			gboolean found = FALSE;
			GList *l;
			for (l = generator->class->namespace->source_file->using_directives; l != NULL; l = l->next) {
				ValaSymbol *ns_symbol = g_hash_table_lookup (generator->context->root->symbol_table, l->data);
				if (ns_symbol == NULL) {
					err (expr->location, "error: namespace ´%s´ specified by using directive not found", l->data);
				}
				expr->static_type_symbol = g_hash_table_lookup (ns_symbol->symbol_table, expr->str);
				if (expr->static_type_symbol != NULL) {
					if (found) {
						err (expr->location, "error: symbol ´%s´ ambiguous", expr->str);
					}
					found = TRUE;
				}
			}
		}

		if (expr->static_type_symbol != NULL) {
			if (expr->static_type_symbol->type == VALA_SYMBOL_TYPE_FIELD) {
				expr->field = expr->static_type_symbol->field;
				expr->array_type = expr->static_type_symbol->field->declaration_statement->variable_declaration->type->array_type;
				expr->static_type_symbol = expr->static_type_symbol->field->declaration_statement->variable_declaration->type->symbol;
			} else if (expr->static_type_symbol->type == VALA_SYMBOL_TYPE_PROPERTY) {
				expr->property = expr->static_type_symbol->property;
				expr->array_type = expr->static_type_symbol->property->return_type->array_type;
				expr->static_type_symbol = expr->static_type_symbol->property->return_type->symbol;
			}
		}

		if (expr->static_type_symbol == NULL) {
			err (expr->location, "error: symbol ´%s´ not found", expr->str);
		}
		break;
	case VALA_EXPRESSION_TYPE_THIS_ACCESS:
		expr->static_type_symbol = generator->sym->stmt->method->method->class->symbol;
		break;
	}
}

static void
vala_code_generator_process_assignment (ValaCodeGenerator *generator, ValaExpression *expr)
{
	vala_code_generator_find_static_type_of_expression (generator, expr->assignment.left);
	
	if (expr->assignment.left->property != NULL) {
		fprintf (generator->c_file, "g_object_set (");
		switch (expr->assignment.left->type) {
		case VALA_EXPRESSION_TYPE_SIMPLE_NAME:
			fprintf (generator->c_file, "self");
			break;
		case VALA_EXPRESSION_TYPE_MEMBER_ACCESS:
			vala_code_generator_process_expression (generator, expr->assignment.left->member_access.left);
			break;
		default:
			break;
		}
		fprintf (generator->c_file, ", \"%s\", ", expr->assignment.left->property->name);

		vala_code_generator_process_expression (generator, expr->assignment.right);
		fprintf (generator->c_file, ", NULL);");
		return;
	}
	
	vala_code_generator_process_expression (generator, expr->assignment.left);
	fprintf (generator->c_file, " = ");
	vala_code_generator_process_expression (generator, expr->assignment.right);
}

static void
vala_code_generator_process_invocation (ValaCodeGenerator *generator, ValaExpression *expr)
{
	GList *l;
	ValaMethod *method = NULL;
	gboolean first = TRUE;
	
	vala_code_generator_find_static_type_of_expression (generator, expr->invocation.call);
	method = expr->invocation.call->static_type_symbol->method;
	switch (expr->invocation.call->type) {
	case VALA_EXPRESSION_TYPE_MEMBER_ACCESS:
		expr->invocation.instance = expr->invocation.call->member_access.left;
		break;
	}
	
	if (method->returns_modified_pointer) {
		if (expr->static_type_symbol->type != VALA_SYMBOL_TYPE_VOID) {
			err (method->location, "error: ReturnsModifiedPointer declared on a method with non-void return type");
		}
		
		if (expr->invocation.instance != NULL) {
			vala_code_generator_process_expression (generator, expr->invocation.instance);
		} else {
			fprintf (generator->c_file, "self");
		}
		fprintf (generator->c_file, " = ");
	}
	
	vala_code_generator_process_expression (generator, expr->invocation.call);
	fprintf (generator->c_file, " (");
	if (!method->instance_last && (method->modifiers & VALA_MODIFIER_STATIC) == 0) {
		if (expr->invocation.instance != NULL) {
			if (!method->is_struct_method && expr->invocation.instance->static_type_symbol != method->class->symbol) {
				fprintf (generator->c_file, "%s%s(", method->class->namespace->upper_case_cname, method->class->upper_case_cname);
			}
			vala_code_generator_process_expression (generator, expr->invocation.instance);
			if (!method->is_struct_method && expr->invocation.instance->static_type_symbol != method->class->symbol) {
				fprintf (generator->c_file, ")");
			}
		} else {
			if (!method->is_struct_method && generator->class->symbol != method->class->symbol) {
				fprintf (generator->c_file, "%s%s(", method->class->namespace->upper_case_cname, method->class->upper_case_cname);
			}
			fprintf (generator->c_file, "self");
			if (!method->is_struct_method && generator->class->symbol != method->class->symbol) {
				fprintf (generator->c_file, ")");
			}
		}
		first = FALSE;
	}
	for (l = expr->invocation.argument_list; l != NULL; l = l->next) {
		if (!first) {
			fprintf (generator->c_file, ", ");
		} else {
			first = FALSE;
		}
		vala_code_generator_process_expression (generator, l->data);
	}
	if (method->instance_last && (method->modifiers & VALA_MODIFIER_STATIC) == 0) {
		if (!first) {
			fprintf (generator->c_file, ", ");
		}
		if (expr->invocation.instance != NULL) {
			vala_code_generator_process_expression (generator, expr->invocation.instance);
		} else {
			fprintf (generator->c_file, "self");
		}
	}
	fprintf (generator->c_file, ")");
}

static void
vala_code_generator_process_literal (ValaCodeGenerator *generator, ValaExpression *expr)
{
	switch (expr->type) {
	case VALA_EXPRESSION_TYPE_LITERAL_BOOLEAN:
		fprintf (generator->c_file, "%s", expr->num ? "TRUE" : "FALSE");
		break;
	case VALA_EXPRESSION_TYPE_LITERAL_NULL:
		fprintf (generator->c_file, "NULL");
		break;
	default:
		fprintf (generator->c_file, "%s", expr->str);
		break;
	}
}

static void
vala_code_generator_process_cast (ValaCodeGenerator *generator, ValaExpression *expr)
{
	if (expr->cast.type->symbol->type == VALA_SYMBOL_TYPE_CLASS) {
		ValaClass *cl = expr->cast.type->symbol->class;
		fprintf (generator->c_file, "%s%s (", cl->namespace->upper_case_cname, cl->upper_case_cname);
		vala_code_generator_process_expression (generator, expr->cast.inner);
		fprintf (generator->c_file, ")");
	} else {
		fprintf (generator->c_file, "(%s) ", get_cname_for_type_reference (expr->cast.type, FALSE, expr->location));
		vala_code_generator_process_expression (generator, expr->cast.inner);
	}
}

static void
vala_code_generator_process_is (ValaCodeGenerator *generator, ValaExpression *expr)
{
	if (expr->is.type->symbol->type == VALA_SYMBOL_TYPE_CLASS) {
		ValaClass *cl = expr->is.type->symbol->class;
		fprintf (generator->c_file, "%sIS_%s (", cl->namespace->upper_case_cname, cl->upper_case_cname);
		vala_code_generator_process_expression (generator, expr->is.expr);
		fprintf (generator->c_file, ")");
	} else {
		err (expr->location, "error: type check on non-class");
	}
}

static void
vala_code_generator_process_element_access (ValaCodeGenerator *generator, ValaExpression *expr)
{
	vala_code_generator_process_expression (generator, expr->element_access.array);
	fprintf (generator->c_file, "[");
	vala_code_generator_process_expression (generator, expr->element_access.index);
	fprintf (generator->c_file, "]");
}

static void
vala_code_generator_process_member_access (ValaCodeGenerator *generator, ValaExpression *expr)
{
	ValaSymbol *sym = expr->static_type_symbol;
	if (sym->type == VALA_SYMBOL_TYPE_METHOD) {
		ValaMethod *method = sym->method;
		fprintf (generator->c_file, "%s", method->cname);
	} else if (expr->static_symbol != NULL && expr->static_symbol->type == VALA_SYMBOL_TYPE_ENUM_VALUE) {
		fprintf (generator->c_file, "%s", expr->static_symbol->enum_value->cname);
	} else if (expr->property != NULL) {
		fprintf (generator->c_file, "%s%s_get_%s (", expr->property->class->namespace->lower_case_cname, expr->property->class->lower_case_cname, expr->property->name);
		vala_code_generator_process_expression (generator, expr->member_access.left);
		fprintf (generator->c_file, ")");
	} else {
		if (expr->field != NULL && !expr->field->is_struct_field) {
			fprintf (generator->c_file, "%s%s(", expr->field->class->namespace->upper_case_cname, expr->field->class->upper_case_cname);
		}
		vala_code_generator_process_expression (generator, expr->member_access.left);
		if (expr->field != NULL) {
			if (!expr->field->is_struct_field) {
				fprintf (generator->c_file, ")");
			}
			fprintf (generator->c_file, "->%s", expr->member_access.right);
		}
	}
}

static void
vala_code_generator_process_object_creation_expression (ValaCodeGenerator *generator, ValaExpression *expr)
{
	GList *l;
	
	fprintf (generator->c_file, "g_object_new (%sTYPE_%s", expr->object_creation.type->symbol->class->namespace->upper_case_cname, expr->object_creation.type->symbol->class->upper_case_cname);

	for (l = expr->object_creation.named_argument_list; l != NULL; l = l->next) {
		ValaNamedArgument *arg = l->data;
		fprintf (generator->c_file, ", \"%s\", ", arg->name);
		vala_code_generator_process_expression (generator, arg->expression);
	}

	fprintf (generator->c_file, ", NULL)");
}

static void
vala_code_generator_process_parenthesized_expression (ValaCodeGenerator *generator, ValaExpression *expr)
{
	fprintf (generator->c_file, "(");
	vala_code_generator_process_expression (generator, expr->inner);
	fprintf (generator->c_file, ")");
}

static void
vala_code_generator_process_postfix_expression (ValaCodeGenerator *generator, ValaExpression *expr)
{
	vala_code_generator_process_expression (generator, expr->postfix.inner);
	fprintf (generator->c_file, "%s", expr->postfix.cop);
}

static void
vala_code_generator_process_simple_name (ValaCodeGenerator *generator, ValaExpression *expr)
{
	if (expr->ref_variable || expr->out_variable) {
		fprintf (generator->c_file, "&");
	}
	
	if (expr->field != NULL) {
		ValaField *field = expr->field;

		if (field->class != NULL) {
			ValaClass *class = field->class;
			char *ns_upper = class->namespace->upper_case_cname;
			char *class_upper = class->upper_case_cname;
			
			if ((field->modifiers & (VALA_MODIFIER_STATIC | VALA_MODIFIER_PRIVATE)) == (VALA_MODIFIER_STATIC | VALA_MODIFIER_PRIVATE)) {
				fprintf (generator->c_file, "%s", expr->str);
			} else if ((field->modifiers & VALA_MODIFIER_STATIC) != 0) {
				fprintf (generator->c_file, "%s%s_GET_CLASS(self)->%s", ns_upper, class_upper, expr->str);
			} else if ((field->modifiers & VALA_MODIFIER_PRIVATE) != 0) {
				fprintf (generator->c_file, "self->priv->%s", expr->str);
			} else if ((field->modifiers & VALA_MODIFIER_PUBLIC) != 0) {
				fprintf (generator->c_file, "%s%s(self)->%s", ns_upper, class_upper, expr->str);
			}

			return;
		} else if (field->namespace != NULL) {
			ValaNamespace *ns = field->namespace;
			
			if (field->cname != NULL) {
				fprintf (generator->c_file, "%s", field->cname);
			} else {
				fprintf (generator->c_file, "%s%s", ns->lower_case_cname, expr->str);
			}

			return;
		}
	} else if (expr->property != NULL) {
		fprintf (generator->c_file, "%s%s_get_%s (self)", expr->property->class->namespace->lower_case_cname, expr->property->class->lower_case_cname, expr->property->name);
		return;
	}

	switch (expr->static_type_symbol->type) {
	case VALA_SYMBOL_TYPE_METHOD:
		fprintf (generator->c_file, "%s", expr->static_type_symbol->method->cname);
		break;
	default:
		fprintf (generator->c_file, "%s", expr->str);
		break;
	}
}

static void
vala_code_generator_process_struct_or_array_initializer (ValaCodeGenerator *generator, ValaExpression *expr)
{
	GList *l;
	gboolean first = TRUE;
	
	fprintf (generator->c_file, "{ ");
	
	for (l = expr->list; l != NULL; l = l->next) {
		if (!first) {
			fprintf (generator->c_file, ", ");
		} else {
			first = FALSE;
		}
		vala_code_generator_process_expression (generator, l->data);
	}

	fprintf (generator->c_file, " }");
}

static void
vala_code_generator_process_this_access (ValaCodeGenerator *generator, ValaExpression *expr)
{
	fprintf (generator->c_file, "self");
}

static void
vala_code_generator_process_expression (ValaCodeGenerator *generator, ValaExpression *expr)
{
	vala_code_generator_find_static_type_of_expression (generator, expr);

	switch (expr->type) {
	case VALA_EXPRESSION_TYPE_ASSIGNMENT:
		vala_code_generator_process_assignment (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_CAST:
		vala_code_generator_process_cast (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_ELEMENT_ACCESS:
		vala_code_generator_process_element_access (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_INVOCATION:
		vala_code_generator_process_invocation (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_IS:
		vala_code_generator_process_is (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_MEMBER_ACCESS:
		vala_code_generator_process_member_access (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_OBJECT_CREATION:
		vala_code_generator_process_object_creation_expression (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_OPERATION:
		vala_code_generator_process_operation_expression (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_PARENTHESIZED:
		vala_code_generator_process_parenthesized_expression (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_POSTFIX:
		vala_code_generator_process_postfix_expression (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_LITERAL_BOOLEAN:
	case VALA_EXPRESSION_TYPE_LITERAL_CHARACTER:
	case VALA_EXPRESSION_TYPE_LITERAL_INTEGER:
	case VALA_EXPRESSION_TYPE_LITERAL_NULL:
	case VALA_EXPRESSION_TYPE_LITERAL_STRING:
		vala_code_generator_process_literal (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_SIMPLE_NAME:
		vala_code_generator_process_simple_name (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_STRUCT_OR_ARRAY_INITIALIZER:
		vala_code_generator_process_struct_or_array_initializer (generator, expr);
		break;
	case VALA_EXPRESSION_TYPE_THIS_ACCESS:
		vala_code_generator_process_this_access (generator, expr);
		break;
	}
}

static void
vala_code_generator_process_variable_declaration (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	ValaTypeReference *type = stmt->variable_declaration->type;
	ValaExpression *expr = stmt->variable_declaration->declarator->initializer;
	if (type->type_name == NULL) {
		/* var type: use type inference */
		g_assert (expr != NULL);

		vala_code_generator_find_static_type_of_expression (generator, expr);
		type->symbol = expr->static_type_symbol;
	}

	char *decl_string = get_cname_for_type_reference (type, FALSE, stmt->location);

	fprintf (generator->c_file, "\t%s%s", decl_string, stmt->variable_declaration->declarator->name);
	
	if (expr != NULL) {
		fprintf (generator->c_file, " = ");
		vala_code_generator_process_expression (generator, expr);
	}

	fprintf (generator->c_file, ";\n");

	ValaSymbol *sym = vala_symbol_new (VALA_SYMBOL_TYPE_LOCAL_VARIABLE);
	g_hash_table_insert (generator->sym->symbol_table, stmt->variable_declaration->declarator->name, sym);
	sym->typeref = stmt->variable_declaration->type;
}

static void vala_code_generator_process_statement (ValaCodeGenerator *generator, ValaStatement *stmt);

static void
vala_code_generator_process_block (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	GList *l;
	
	fprintf (generator->c_file, "{\n");
	
	for (l = stmt->block.statements; l != NULL; l = l->next) {
		vala_code_generator_process_statement (generator, l->data);
	}
	
	fprintf (generator->c_file, "}\n");
}

static void
vala_code_generator_process_statement_expression_list (ValaCodeGenerator *generator, GList *list)
{
	GList *l;
	gboolean first = TRUE;
	
	for (l = list; l != NULL; l = l->next) {
		if (!first) {
			fprintf (generator->c_file, ", ");
		} else {
			first = FALSE;
		}
		vala_code_generator_process_expression (generator, l->data);
	}
}

static void
vala_code_generator_process_while_statement (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	fprintf (generator->c_file, "\twhile (");
	vala_code_generator_process_expression (generator, stmt->while_stmt.condition);
	fprintf (generator->c_file, ")\n");
	vala_code_generator_process_statement (generator, stmt->while_stmt.loop);
}

static void
vala_code_generator_process_for_statement (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	fprintf (generator->c_file, "\tfor (");
	vala_code_generator_process_statement_expression_list (generator, stmt->for_stmt.initializer);
	fprintf (generator->c_file, "; ");
	vala_code_generator_process_expression (generator, stmt->for_stmt.condition);
	fprintf (generator->c_file, "; ");
	vala_code_generator_process_statement_expression_list (generator, stmt->for_stmt.iterator);
	fprintf (generator->c_file, ")\n");
	vala_code_generator_process_statement (generator, stmt->for_stmt.loop);
}

static void
vala_code_generator_process_foreach_statement (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	vala_code_generator_find_static_type_of_expression (generator, stmt->foreach_stmt.container);
	
	if (stmt->foreach_stmt.container->array_type) {
		fprintf (generator->c_file, "\t%s%s_it;\n", get_cname_for_static_expression_type (stmt->foreach_stmt.container, stmt->location), stmt->foreach_stmt.name);
		fprintf (generator->c_file, "\tfor (");
		fprintf (generator->c_file, "%s_it = ", stmt->foreach_stmt.name, stmt->foreach_stmt.name);
		vala_code_generator_process_expression (generator, stmt->foreach_stmt.container);
		fprintf (generator->c_file, "; ");
		fprintf (generator->c_file, "*%s_it != NULL", stmt->foreach_stmt.name);
		fprintf (generator->c_file, "; ");
		fprintf (generator->c_file, "%s_it++", stmt->foreach_stmt.name);
		fprintf (generator->c_file, ") {\n");
		fprintf (generator->c_file, "\t\t%s%s = *%s_it;\n", get_cname_for_type_reference (stmt->foreach_stmt.type, FALSE, stmt->location), stmt->foreach_stmt.name, stmt->foreach_stmt.name);
	} else {
		fprintf (generator->c_file, "\tGList *%s_it;\n", stmt->foreach_stmt.name);
		fprintf (generator->c_file, "\tfor (");
		fprintf (generator->c_file, "%s_it = ", stmt->foreach_stmt.name);
		vala_code_generator_process_expression (generator, stmt->foreach_stmt.container);
		fprintf (generator->c_file, "; ");
		fprintf (generator->c_file, "%s_it != NULL", stmt->foreach_stmt.name);
		fprintf (generator->c_file, "; ");
		fprintf (generator->c_file, "%s_it = %s_it->next", stmt->foreach_stmt.name, stmt->foreach_stmt.name);
		fprintf (generator->c_file, ") {\n");
		fprintf (generator->c_file, "\t%s%s = %s_it->data;\n", get_cname_for_type_reference (stmt->foreach_stmt.type, FALSE, stmt->location), stmt->foreach_stmt.name, stmt->foreach_stmt.name);
	}

	ValaSymbol *sym = vala_symbol_new (VALA_SYMBOL_TYPE_LOCAL_VARIABLE);
	g_hash_table_insert (generator->sym->symbol_table, stmt->foreach_stmt.name, sym);
	sym->typeref = stmt->foreach_stmt.type;

	vala_code_generator_process_statement (generator, stmt->foreach_stmt.loop);

	fprintf (generator->c_file, "}\n");
}

static void
vala_code_generator_process_if_statement (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	GList *l;
	
	fprintf (generator->c_file, "\tif (");
	vala_code_generator_process_expression (generator, stmt->if_stmt.condition);
	fprintf (generator->c_file, ")\n");
	vala_code_generator_process_statement (generator, stmt->if_stmt.true_stmt);
	if (stmt->if_stmt.false_stmt != NULL) {
		fprintf (generator->c_file, "\telse ");
		vala_code_generator_process_statement (generator, stmt->if_stmt.false_stmt);
	}
}

static void
vala_code_generator_process_return_statement (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	GList *l;
	
	fprintf (generator->c_file, "\treturn ");
	if (stmt->expr != NULL) {
		vala_code_generator_process_expression (generator, stmt->expr);
	}
	fprintf (generator->c_file, ";\n");
}

static void
vala_code_generator_process_statement (ValaCodeGenerator *generator, ValaStatement *stmt)
{
	switch (stmt->type) {
	case VALA_STATEMENT_TYPE_BLOCK:
		vala_code_generator_process_block (generator, stmt);
		break;
	case VALA_STATEMENT_TYPE_EXPRESSION:
		fprintf (generator->c_file, "\t");
		vala_code_generator_process_expression (generator, stmt->expr);
		fprintf (generator->c_file, ";\n");
		break;
	case VALA_STATEMENT_TYPE_WHILE:
		vala_code_generator_process_while_statement (generator, stmt);
		break;
	case VALA_STATEMENT_TYPE_FOR:
		vala_code_generator_process_for_statement (generator, stmt);
		break;
	case VALA_STATEMENT_TYPE_FOREACH:
		vala_code_generator_process_foreach_statement (generator, stmt);
		break;
	case VALA_STATEMENT_TYPE_IF:
		vala_code_generator_process_if_statement (generator, stmt);
		break;
	case VALA_STATEMENT_TYPE_RETURN:
		vala_code_generator_process_return_statement (generator, stmt);
		break;
	case VALA_STATEMENT_TYPE_VARIABLE_DECLARATION:
		vala_code_generator_process_variable_declaration (generator, stmt);
		break;
	default:
		fprintf (generator->c_file, "\t;\n");
	}
}

static GList*
get_fields_by_flag (ValaClass *class, ValaModifierFlags flag)
{
	GList *l,*ret = NULL;
	ValaField *field;
	
	for (l = class->fields; l != NULL; l = l->next) {
		field = l->data;
		if (field->modifiers == flag)
			ret = g_list_prepend (ret, field);
	}
	
	return g_list_reverse (ret);
}

static void
vala_code_generator_process_constants (ValaCodeGenerator *generator, ValaClass *class)
{
	GList *l;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;

	ValaNamespace *namespace = class->namespace;

	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = class->lower_case_cname;
	char *upper_case = class->upper_case_cname;

	for (l = class->constants; l != NULL; l = l->next) {
		ValaConstant *constant = l->data;
		
		ValaStatement* stmt = constant->declaration_statement;
		ValaTypeReference *type = stmt->variable_declaration->type;
		ValaExpression *expr = stmt->variable_declaration->declarator->initializer;
		if (type->type_name == NULL) {
			/* var type: use type inference */
			g_assert (expr != NULL);

			vala_code_generator_find_static_type_of_expression (generator, expr);
			type->symbol = expr->static_type_symbol;
		}

		char *decl_string = get_cname_for_type_reference (type, TRUE, stmt->location);

		fprintf (generator->c_file, "%s%s%s", decl_string, stmt->variable_declaration->declarator->name, type->array_type ? "[]" : "");
		
		if (expr != NULL) {
			fprintf (generator->c_file, " = ");
			vala_code_generator_process_expression (generator, expr);
		}

		fprintf (generator->c_file, ";\n");
		
		fprintf (generator->c_file, "\n");
	}
}

static void
vala_code_generator_process_methods2 (ValaCodeGenerator *generator, ValaClass *class)
{
	GList *l;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;

	ValaNamespace *namespace = class->namespace;

	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = class->lower_case_cname;
	char *upper_case = class->upper_case_cname;

	for (l = class->methods; l != NULL; l = l->next) {
		ValaMethod *method = l->data;
		
		if (strcmp (method->name, "init") == 0 || strcmp (method->name, "class_init") == 0) {
			continue;
		}

		if ((method->modifiers & VALA_MODIFIER_PUBLIC) && (method->modifiers & VALA_MODIFIER_OVERRIDE) == 0) {
			fprintf (generator->h_file, "%s %s (%s);\n", method->cdecl1, method->cname, method->cparameters);
		}

		if ((method->modifiers & VALA_MODIFIER_ABSTRACT) == 0 && method->body != NULL) {
			if ((method->modifiers & (VALA_MODIFIER_VIRTUAL | VALA_MODIFIER_OVERRIDE)) == 0) {
				fprintf (generator->c_file, "%s\n", method->cdecl1);
				fprintf (generator->c_file, "%s (%s)\n", method->cname, method->cparameters);
			} else {
				fprintf (generator->c_file, "static %s\n", method->cdecl1);
				fprintf (generator->c_file, "%s%s_real_%s (%s)\n", ns_lower, lower_case, method->name, method->cparameters);
			}
			
			if (method->modifiers & VALA_MODIFIER_OVERRIDE) {
				fprintf (generator->c_file, "{\n");
				fprintf (generator->c_file, "\t%s *self = %s%s(base);\n", class->cname, class->namespace->upper_case_cname, class->upper_case_cname);
			}

			generator->sym = vala_symbol_new (VALA_SYMBOL_TYPE_BLOCK);
			generator->sym->stmt = method->body;

			ValaSymbol *sym;
			GList *pl;

			for (pl = method->formal_parameters; pl != NULL; pl = pl->next) {
				ValaFormalParameter *param = pl->data;
				
				sym = vala_symbol_new (VALA_SYMBOL_TYPE_LOCAL_VARIABLE);
				g_hash_table_insert (generator->sym->symbol_table, param->name, sym);
				sym->typeref = param->type;
			}

			vala_code_generator_process_block (generator, method->body);

			if (method->modifiers & VALA_MODIFIER_OVERRIDE) {
				fprintf (generator->c_file, "}\n");
			}
		}
		
		fprintf (generator->c_file, "\n");

		if (method->modifiers & (VALA_MODIFIER_ABSTRACT | VALA_MODIFIER_VIRTUAL)) {
			fprintf (generator->c_file, "%s\n", method->cdecl1);
			fprintf (generator->c_file, "%s (%s)\n", method->cname, method->cparameters);
			fprintf (generator->c_file, "{\n");
			fprintf (generator->c_file, "\t");
			if (method->return_type->symbol->type != VALA_SYMBOL_TYPE_VOID) {
				fprintf (generator->c_file, "return ");
			}
			fprintf (generator->c_file, "%s%s_GET_CLASS (self)->%s (self", ns_upper, upper_case, method->name);
			
			GList *pl;
			
			for (pl = method->formal_parameters; pl != NULL; pl = pl->next) {
				ValaFormalParameter *param = pl->data;
				
				fprintf (generator->c_file, ", %s", param->name);
			}
			
			fprintf (generator->c_file, ");\n");
			fprintf (generator->c_file, "}\n");
			fprintf (generator->c_file, "\n");
		}

		if ((method->modifiers & VALA_MODIFIER_STATIC) && strcmp (method->name, "main") == 0 && strcmp (method->return_type->type_name, "int") == 0) {
			if (g_list_length (method->formal_parameters) == 2) {
				/* main method */
				
				fprintf (generator->c_file, "int\n");
				fprintf (generator->c_file, "main (int argc, char **argv)\n");
				fprintf (generator->c_file, "{\n");
				fprintf (generator->c_file, "\tg_type_init ();\n");
				fprintf (generator->c_file, "\treturn %s (argc, argv);\n", method->cname);
				fprintf (generator->c_file, "}\n");
				fprintf (generator->c_file, "\n");
			}
		}
	}
	fprintf (generator->h_file, "\n");
	
	/* properties */
	if (class->properties != NULL) {
		fprintf (generator->c_file, "enum {\n");
		fprintf (generator->c_file, "\t%s%s_DUMMY_PROPERTY,\n", ns_upper, upper_case);
		for (l = class->properties; l != NULL; l = l->next) {
			ValaProperty *prop = l->data;
			fprintf (generator->c_file, "\t%s%s_%s,\n", ns_upper, upper_case, g_ascii_strup (prop->name, -1));
		}
		fprintf (generator->c_file, "};\n");

		/* getter / setter */
		for (l = class->properties; l != NULL; l = l->next) {
			ValaProperty *prop = l->data;

			/* getter */
			if (prop->get_statement != NULL) {
				fprintf (generator->h_file, "%s %s%s_get_%s (%s *self);\n", get_cname_for_type_reference (prop->return_type, FALSE, prop->location), ns_lower, lower_case, prop->name, class->cname);
				fprintf (generator->c_file, "%s\n", get_cname_for_type_reference (prop->return_type, FALSE, prop->location));
				fprintf (generator->c_file, "%s%s_get_%s (%s *self)\n", ns_lower, lower_case, prop->name, class->cname);

				generator->sym = vala_symbol_new (VALA_SYMBOL_TYPE_BLOCK);
				generator->sym->stmt = prop->get_statement;

				vala_code_generator_process_statement (generator, prop->get_statement);
			}

			/* setter */
			if (prop->set_statement != NULL) {
				fprintf (generator->h_file, "void %s%s_set_%s (%s *self, %svalue);\n", ns_lower, lower_case, prop->name, class->cname, get_cname_for_type_reference (prop->return_type, FALSE, prop->location));
				fprintf (generator->c_file, "void\n");
				fprintf (generator->c_file, "%s%s_set_%s (%s *self, %svalue)\n", ns_lower, lower_case, prop->name, class->cname, get_cname_for_type_reference (prop->return_type, FALSE, prop->location));

				generator->sym = vala_symbol_new (VALA_SYMBOL_TYPE_BLOCK);
				generator->sym->stmt = prop->set_statement;

				ValaSymbol *sym = vala_symbol_new (VALA_SYMBOL_TYPE_LOCAL_VARIABLE);
				g_hash_table_insert (generator->sym->symbol_table, "value", sym);
				sym->typeref = prop->return_type;

				vala_code_generator_process_statement (generator, prop->set_statement);
			}
		}

		/* override get_property */
		fprintf (generator->c_file, "static void\n");
		fprintf (generator->c_file, "%s%s_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)\n", ns_lower, lower_case);
		fprintf (generator->c_file, "{\n");
		fprintf (generator->c_file, "\t%s *self = (%s *) object;\n", class->cname, class->cname);
		fprintf (generator->c_file, "\tswitch (property_id) {\n");
		for (l = class->properties; l != NULL; l = l->next) {
			ValaProperty *prop = l->data;
			if (prop->get_statement == NULL) {
				continue;
			}
			fprintf (generator->c_file, "\tcase %s%s_%s:\n", ns_upper, upper_case, g_ascii_strup (prop->name, -1));

			if (strcmp (prop->return_type->type_name, "string") == 0) {
				fprintf (generator->c_file, "\t\tg_value_set_string");
			} else if (prop->return_type->symbol->type == VALA_SYMBOL_TYPE_ENUM || strcmp (prop->return_type->type_name, "int") == 0) {
				fprintf (generator->c_file, "\t\tg_value_set_int");
			} else if (strcmp (prop->return_type->type_name, "bool") == 0) {
				fprintf (generator->c_file, "\t\tg_value_set_boolean");
			} else if (prop->return_type->symbol->type == VALA_SYMBOL_TYPE_CLASS) {
				fprintf (generator->c_file, "\t\tg_value_set_object");
			} else {
				fprintf (generator->c_file, "\t\tg_value_set_pointer");
			}
			fprintf (generator->c_file, " (value, %s%s_get_%s (self));\n", ns_lower, lower_case, prop->name);
			fprintf (generator->c_file, "\t\tbreak;\n");
		}
		fprintf (generator->c_file, "\tdefault:\n");
		fprintf (generator->c_file, "\t\tG_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);\n");
		fprintf (generator->c_file, "\t\tbreak;\n");
		fprintf (generator->c_file, "\t}\n");
		fprintf (generator->c_file, "}\n");

		/* override set_property */
		fprintf (generator->c_file, "static void\n");
		fprintf (generator->c_file, "%s%s_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)\n", ns_lower, lower_case);
		fprintf (generator->c_file, "{\n");
		fprintf (generator->c_file, "\t%s *self = (%s *) object;\n", class->cname, class->cname);
		fprintf (generator->c_file, "\tswitch (property_id) {\n");
		for (l = class->properties; l != NULL; l = l->next) {
			ValaProperty *prop = l->data;
			if (prop->set_statement == NULL) {
				continue;
			}
			fprintf (generator->c_file, "\tcase %s%s_%s:\n", ns_upper, upper_case, g_ascii_strup (prop->name, -1));

			fprintf (generator->c_file, "\t%s%s_set_%s (self, ", ns_lower, lower_case, prop->name);
			if (strcmp (prop->return_type->type_name, "string") == 0) {
				fprintf (generator->c_file, "g_value_dup_string (value)");
			} else if (prop->return_type->symbol->type == VALA_SYMBOL_TYPE_ENUM || strcmp (prop->return_type->type_name, "int") == 0) {
				fprintf (generator->c_file, "g_value_get_int (value)");
			} else if (strcmp (prop->return_type->type_name, "bool") == 0) {
				fprintf (generator->c_file, "g_value_get_boolean (value)");
			} else if (prop->return_type->symbol->type == VALA_SYMBOL_TYPE_CLASS) {
				fprintf (generator->c_file, "g_value_get_object (value)");
			} else {
				fprintf (generator->c_file, "g_value_get_pointer (value)");
			}
			fprintf (generator->c_file, ");\n");
			fprintf (generator->c_file, "\t\tbreak;\n");
		}
		fprintf (generator->c_file, "\tdefault:\n");
		fprintf (generator->c_file, "\t\tG_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);\n");
		fprintf (generator->c_file, "\t\tbreak;\n");
		fprintf (generator->c_file, "\t}\n");
		fprintf (generator->c_file, "}\n");
	}

	/* constructors */
	fprintf (generator->c_file, "static void\n");
	fprintf (generator->c_file, "%s%s_init (%s%s *self)\n", ns_lower, lower_case, namespace->name, class->name);
	fprintf (generator->c_file, "{\n");

	if (class->has_private_fields) {
		fprintf (generator->c_file, "\tself->priv = %s%s_GET_PRIVATE (self);\n", ns_upper, upper_case);
	}
	
	/* initialize all fields */
	for (l = class->fields; l != NULL; l = l->next) {
		ValaField *field = l->data;
		ValaExpression *expr = field->declaration_statement->variable_declaration->declarator->initializer;
		
		if ((field->modifiers & VALA_MODIFIER_STATIC) != 0 || expr == NULL) {
			continue;
		} else if ((field->modifiers & VALA_MODIFIER_PUBLIC) != 0) {
			fprintf (generator->c_file, "\tself->%s = ", field->declaration_statement->variable_declaration->declarator->name);
		} else if ((field->modifiers & VALA_MODIFIER_PRIVATE) != 0) {
			fprintf (generator->c_file, "\tself->priv->%s = ", field->declaration_statement->variable_declaration->declarator->name);
		}
		vala_code_generator_process_expression (generator, expr);
		fprintf (generator->c_file, ";\n");		
	}

	if (class->init_method != NULL) {
		generator->sym = vala_symbol_new (VALA_SYMBOL_TYPE_BLOCK);
		generator->sym->stmt = class->init_method->body;

		vala_code_generator_process_block (generator, class->init_method->body);
	}

	fprintf (generator->c_file, "}\n");
	fprintf (generator->c_file, "\n");

	fprintf (generator->c_file, "static void\n");
	fprintf (generator->c_file, "%s%s_class_init (%s%sClass *klass)\n", ns_lower, lower_case, namespace->name, class->name);
	fprintf (generator->c_file, "{\n");
	if (class->has_private_fields) {
		fprintf (generator->c_file, "\tg_type_class_add_private (klass, sizeof (%sPrivate));\n", g_strdup_printf ("%s%s", class->namespace->name, class->name));
	}
	
	/* initialize all static fields */
	for (l = class->fields; l != NULL; l = l->next) {
		ValaField *field = l->data;
		ValaExpression *expr = field->declaration_statement->variable_declaration->declarator->initializer;
		
		if ((field->modifiers & VALA_MODIFIER_STATIC) == 0 || expr == NULL) {
			continue;
		} else if ((field->modifiers & VALA_MODIFIER_PUBLIC) != 0) {
			fprintf (generator->c_file, "\tklass->%s = ", field->declaration_statement->variable_declaration->declarator->name);
		} else if ((field->modifiers & VALA_MODIFIER_PRIVATE) != 0) {
			/* no private support for now */
			continue;
		}
		vala_code_generator_process_expression (generator, expr);
		fprintf (generator->c_file, ";\n");		
	}

	/* chain virtual functions */
	for (l = class->methods; l != NULL; l = l->next) {
		ValaMethod *method = l->data;
		
		if (method->modifiers & (VALA_MODIFIER_VIRTUAL | VALA_MODIFIER_OVERRIDE)) {
			fprintf (generator->c_file, "\t");
			if (method->modifiers & VALA_MODIFIER_OVERRIDE) {
				fprintf (generator->c_file, "%s%s_CLASS (klass)", method->virtual_super_class->namespace->upper_case_cname, method->virtual_super_class->upper_case_cname);
			} else {
				fprintf (generator->c_file, "klass");
			}
			fprintf (generator->c_file, "->%s = %s%s_real_%s;\n", method->name, ns_lower, lower_case, method->name);
		}
	}
	
	if (class->properties != NULL) {
		fprintf (generator->c_file, "\tG_OBJECT_CLASS(klass)->set_property = %s%s_set_property;\n", ns_lower, lower_case);
		fprintf (generator->c_file, "\tG_OBJECT_CLASS(klass)->get_property = %s%s_get_property;\n", ns_lower, lower_case);
		for (l = class->properties; l != NULL; l = l->next) {
			ValaProperty *prop = l->data;
			fprintf (generator->c_file, "\tg_object_class_install_property (G_OBJECT_CLASS(klass), %s%s_%s, ", ns_upper, upper_case, g_ascii_strup (prop->name, -1));
			
			/* paramspec */
			if (strcmp (prop->return_type->type_name, "string") == 0) {
				fprintf (generator->c_file, "g_param_spec_string");
				fprintf (generator->c_file, " (\"%s\", \"foo\", \"bar\", NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE)", prop->name);
			} else if (prop->return_type->symbol->type == VALA_SYMBOL_TYPE_ENUM || strcmp (prop->return_type->type_name, "int") == 0) {
				fprintf (generator->c_file, "g_param_spec_int");
				fprintf (generator->c_file, " (\"%s\", \"foo\", \"bar\", G_MININT, G_MAXINT, 0, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE)", prop->name);
			} else if (strcmp (prop->return_type->type_name, "bool") == 0) {
				fprintf (generator->c_file, "g_param_spec_boolean");
				fprintf (generator->c_file, " (\"%s\", \"foo\", \"bar\", FALSE, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE)", prop->name);
			} else if (prop->return_type->symbol->type == VALA_SYMBOL_TYPE_CLASS) {
				fprintf (generator->c_file, "g_param_spec_object");
				fprintf (generator->c_file, " (\"%s\", \"foo\", \"bar\", %sTYPE_%s, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE)", prop->name, prop->return_type->symbol->class->namespace->upper_case_cname, prop->return_type->symbol->class->upper_case_cname);
			} else {
				fprintf (generator->c_file, "g_param_spec_pointer");
				fprintf (generator->c_file, " (\"%s\", \"foo\", \"bar\", G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE)", prop->name);
			}
			
			fprintf (generator->c_file, ");\n");
		}
	}

	if (class->class_init_method != NULL) {
		generator->sym = vala_symbol_new (VALA_SYMBOL_TYPE_BLOCK);
		generator->sym->stmt = class->class_init_method->body;

		vala_code_generator_process_block (generator, class->class_init_method->body);
	}

	fprintf (generator->c_file, "}\n");
	fprintf (generator->c_file, "\n");
}

static void
vala_code_generator_process_virtual_method_pointers (ValaCodeGenerator *generator, ValaClass *class)
{
	GList *l;
	gboolean first = TRUE;

	char *ns_lower;
	char *ns_upper;

	ValaNamespace *namespace = class->namespace;

	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = class->lower_case_cname;
	char *upper_case = class->upper_case_cname;

	for (l = class->methods; l != NULL; l = l->next) {
		ValaMethod *method = l->data;

		if ((method->modifiers & (VALA_MODIFIER_ABSTRACT | VALA_MODIFIER_VIRTUAL)) == 0) {
			continue;
		}
		
		if (first) {
			fprintf (generator->h_file, "\n");
			fprintf (generator->h_file, "\t/* virtual methods */\n");
		} else {
			first = FALSE;
		}

		fprintf (generator->h_file, "\t%s(*%s) (%s);\n", get_cname_for_type_reference (method->return_type, FALSE, method->location), method->name, method->cparameters);
	}
}

static void
vala_code_generator_process_class1 (ValaCodeGenerator *generator, ValaClass *class)
{
	ValaNamespace *namespace = class->namespace;
	
	generator->class = class;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;
	GList *l;

	camel_case = class->cname;
	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = class->lower_case_cname;
	char *upper_case = class->upper_case_cname;

	/* type macros */
	fprintf (generator->h_file, "#define %sTYPE_%s\t(%s%s_get_type ())\n", ns_upper, upper_case, ns_lower, lower_case);
	fprintf (generator->h_file, "#define %s%s(obj)\t(G_TYPE_CHECK_INSTANCE_CAST ((obj), %sTYPE_%s, %s))\n", ns_upper, upper_case, ns_upper, upper_case, camel_case);
	fprintf (generator->h_file, "#define %s%s_CLASS(klass)\t(G_TYPE_CHECK_CLASS_CAST ((klass), %sTYPE_%s, %sClass))\n", ns_upper, upper_case, ns_upper, upper_case, camel_case);
	fprintf (generator->h_file, "#define %sIS_%s(obj)\t(G_TYPE_CHECK_INSTANCE_TYPE ((obj), %sTYPE_%s))\n", ns_upper, upper_case, ns_upper, upper_case);
	fprintf (generator->h_file, "#define %sIS_%s_CLASS(klass)\t(G_TYPE_CHECK_CLASS_TYPE ((klass), %sTYPE_%s))\n", ns_upper, upper_case, ns_upper, upper_case);
	fprintf (generator->h_file, "#define %s%s_GET_CLASS(obj)\t(G_TYPE_INSTANCE_GET_CLASS ((obj), %sTYPE_%s, %sClass))\n", ns_upper, upper_case, ns_upper, upper_case, camel_case);
	fprintf (generator->h_file, "\n");

	/* structs */
	fprintf (generator->h_file, "#ifndef _TYPE_%s%s\n", class->namespace->upper_case_cname, class->upper_case_cname);
	fprintf (generator->h_file, "#define _TYPE_%s%s\n", class->namespace->upper_case_cname, class->upper_case_cname);
	fprintf (generator->h_file, "typedef struct _%s %s;\n", camel_case, camel_case);
	fprintf (generator->h_file, "typedef struct _%sClass %sClass;\n", camel_case, camel_case);
	fprintf (generator->h_file, "#endif\n");
	fprintf (generator->h_file, "typedef struct _%sPrivate %sPrivate;\n", camel_case, camel_case);
	fprintf (generator->h_file, "\n");
	
	vala_code_generator_process_methods1 (generator, class);
	
	/* private structure */
	fprintf (generator->c_file, "struct _%sPrivate {\n", camel_case);
	/* private fields */
	for (l = get_fields_by_flag (class, VALA_MODIFIER_PRIVATE); l != NULL; l = l->next) {
		class->has_private_fields = TRUE;
		ValaField *field = l->data;		
		ValaTypeReference *type = field->declaration_statement->variable_declaration->type;
		
		fprintf (generator->c_file, "\t%s%s;\n", get_cname_for_type_reference (type, FALSE, field->declaration_statement->location),field->declaration_statement->variable_declaration->declarator->name);
	}
	if (!class->has_private_fields) {
		/* silence gcc */
		fprintf (generator->c_file, "\tint dummy;\n");
	}
	fprintf (generator->c_file, "};\n", camel_case);
	fprintf (generator->c_file, "\n");
	/* get private macro */
	fprintf (generator->c_file, "#define %s%s_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), %sTYPE_%s, %sPrivate))\n\n", ns_upper, upper_case, ns_upper, upper_case, camel_case);

	/* private static fields */
	for (l = get_fields_by_flag (class, VALA_MODIFIER_PRIVATE | VALA_MODIFIER_STATIC); l != NULL; l = l->next) {
		ValaField *field = l->data;
		ValaTypeReference *type = field->declaration_statement->variable_declaration->type;
		
		fprintf (generator->c_file, "static %s%s;\n", get_cname_for_type_reference (type, FALSE, field->declaration_statement->location),field->declaration_statement->variable_declaration->declarator->name);
	}
	fprintf (generator->c_file, "\n");
}

static void
vala_code_generator_process_struct1 (ValaCodeGenerator *generator, ValaStruct *struct_)
{
	ValaNamespace *namespace = struct_->namespace;
	
	generator->struct_ = struct_;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;
	GList *l;

	camel_case = struct_->cname;
	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = struct_->lower_case_cname;
	char *upper_case = struct_->upper_case_cname;

	/* structs */
	fprintf (generator->h_file, "typedef struct _%s %s;\n", camel_case, camel_case);
	fprintf (generator->h_file, "\n");
	
	vala_code_generator_process_struct_methods1 (generator, struct_);
}

static void
vala_code_generator_process_enum1 (ValaCodeGenerator *generator, ValaEnum *enum_)
{
	ValaNamespace *namespace = enum_->namespace;
	
	char *camel_case;
	char *ns_upper;
	GList *l;

	camel_case = enum_->cname;
	ns_upper = namespace->upper_case_cname;
	
	char *upper_case = enum_->upper_case_cname;

	fprintf (generator->h_file, "typedef enum {\n");
	for (l = enum_->values; l != NULL; l = l->next) {
		ValaEnumValue *val = l->data;

		fprintf (generator->h_file, "\t%s,\n", val->cname);
	}
		
	fprintf (generator->h_file, "} %s;\n", camel_case);
	fprintf (generator->h_file, "\n");
}

static void
vala_code_generator_process_class2 (ValaCodeGenerator *generator, ValaClass *class)
{
	ValaNamespace *namespace = class->namespace;
	
	generator->class = class;

	char *camel_case;
	char *ns_lower;
	char *ns_upper;
	GList *l;

	camel_case = g_strdup_printf ("%s%s", namespace->name, class->name);
	ns_lower = namespace->lower_case_cname;
	ns_upper = namespace->upper_case_cname;
	
	char *lower_case = class->lower_case_cname;
	char *upper_case = class->upper_case_cname;
	
	/* structs */
	fprintf (generator->h_file, "struct _%s {\n", camel_case);
	fprintf (generator->h_file, "\t%s parent;\n", class->base_class->cname);
	fprintf (generator->h_file, "\t%sPrivate *priv;\n", camel_case);
	/* public fields */
	for (l = get_fields_by_flag (class, VALA_MODIFIER_PUBLIC); l != NULL; l = l->next) {
		ValaField *field = l->data;		
		ValaTypeReference *type = field->declaration_statement->variable_declaration->type;
		
		fprintf (generator->h_file, "\t%s%s;\n", get_cname_for_type_reference (type, FALSE, field->declaration_statement->location),field->declaration_statement->variable_declaration->declarator->name);
	}
		
	fprintf (generator->h_file, "};\n");
	fprintf (generator->h_file, "\n");

	fprintf (generator->h_file, "struct _%sClass {\n", camel_case);
	fprintf (generator->h_file, "\t%sClass parent;\n", class->base_class->cname);
	/* public static fields */
	for (l = get_fields_by_flag (class, VALA_MODIFIER_PUBLIC | VALA_MODIFIER_STATIC); l != NULL; l = l->next) {
		ValaField *field = l->data;		
		ValaTypeReference *type = field->declaration_statement->variable_declaration->type;
		
		fprintf (generator->h_file, "\t%s%s;\n", get_cname_for_type_reference (type, FALSE, field->declaration_statement->location),field->declaration_statement->variable_declaration->declarator->name);
	}
	
	vala_code_generator_process_virtual_method_pointers (generator, class);
	
	fprintf (generator->h_file, "};\n");
	fprintf (generator->h_file, "\n");

	/* function declarations */
	fprintf (generator->h_file, "GType %s%s_get_type () G_GNUC_CONST;\n", ns_lower, lower_case);
	fprintf (generator->h_file, "\n");
	
	vala_code_generator_process_constants (generator, class);
	
	vala_code_generator_process_methods2 (generator, class);
	
	/* type initialization function */
	fprintf (generator->c_file, "GType\n");
	fprintf (generator->c_file, "%s%s_get_type ()\n", ns_lower, lower_case);
	fprintf (generator->c_file, "{\n");
	fprintf (generator->c_file, "\tstatic GType g_define_type_id = 0;\n");
	fprintf (generator->c_file, "\tif (G_UNLIKELY (g_define_type_id == 0)) {\n");
	fprintf (generator->c_file, "\t\tstatic const GTypeInfo g_define_type_info = {\n");
	fprintf (generator->c_file, "\t\t\tsizeof (%sClass),\n", camel_case);
	fprintf (generator->c_file, "\t\t\t(GBaseInitFunc) NULL,\n");
	fprintf (generator->c_file, "\t\t\t(GBaseFinalizeFunc) NULL,\n");
	fprintf (generator->c_file, "\t\t\t(GClassInitFunc) %s%s_class_init,\n", ns_lower, lower_case);
	fprintf (generator->c_file, "\t\t\t(GClassFinalizeFunc) NULL,\n");
	fprintf (generator->c_file, "\t\t\tNULL, /* class_data */\n");
	fprintf (generator->c_file, "\t\t\tsizeof (%s),\n", camel_case);
	fprintf (generator->c_file, "\t\t\t0, /* n_preallocs */\n");
	fprintf (generator->c_file, "\t\t\t(GInstanceInitFunc) %s%s_init,\n", ns_lower, lower_case);
	fprintf (generator->c_file, "\t\t};\n");
	
	fprintf (generator->c_file, "\t\tg_define_type_id = g_type_register_static (%sTYPE_%s, \"%s\", &g_define_type_info, 0);\n", class->base_class->namespace->upper_case_cname, class->base_class->upper_case_cname, camel_case);

	/* FIXME: add interfaces */
	fprintf (generator->c_file, "\t}\n");
	fprintf (generator->c_file, "\treturn g_define_type_id;\n");
	fprintf (generator->c_file, "}\n");
	fprintf (generator->c_file, "\n");
}

static void
vala_code_generator_process_namespace1 (ValaCodeGenerator *generator, ValaNamespace *namespace)
{
	GList *l;
	for (l = namespace->classes; l != NULL; l = l->next) {
		vala_code_generator_process_class1 (generator, l->data);
	}
	for (l = namespace->structs; l != NULL; l = l->next) {
		vala_code_generator_process_struct1 (generator, l->data);
	}
	for (l = namespace->enums; l != NULL; l = l->next) {
		vala_code_generator_process_enum1 (generator, l->data);
	}
	for (l = namespace->methods; l != NULL; l = l->next) {
		vala_code_generator_process_ns_method (generator, namespace, l->data);
	}
}

static void
vala_code_generator_process_namespace2 (ValaCodeGenerator *generator, ValaNamespace *namespace)
{
	GList *l;
	for (l = namespace->classes; l != NULL; l = l->next) {
		vala_code_generator_process_class2 (generator, l->data);
	}
}

static void
vala_code_generator_process_dep_type (ValaCodeGenerator *generator, FILE *f, ValaSymbol *dep_type, GList **dep_files)
{
	ValaSourceFile *dep_file;
	ValaNamespace *namespace;
	if (dep_type->type == VALA_SYMBOL_TYPE_CLASS) {
		namespace = dep_type->class->namespace;
	} else if (dep_type->type == VALA_SYMBOL_TYPE_STRUCT) {
		namespace = dep_type->struct_->namespace;
	} else if (dep_type->type == VALA_SYMBOL_TYPE_ENUM) {
		namespace = dep_type->enum_->namespace;
	} else {
		err (NULL, "internal error: dependant type is neither class nor struct");
	}
	if (namespace->name == NULL || strlen (namespace->name) == 0) {
		/* might be global namespace of imported library, don't use stub header */
		/* FIXME: mark source file as appropriate so non-imported types in global namespace work */
		return;
	}
	if (namespace->import) {
		/* imported namespace, don't use stub header */
		/* FIXME: include real header instead */
		
		if (namespace->include_filename != NULL) {
			fprintf (f, "#include <%s>\n", namespace->include_filename);
		}
		
		return;
	}
	dep_file = namespace->source_file;
	
	if (dep_file == NULL) {
		/* type without source file, ignore */
		return;
	}
	
	GList *fl;
	for (fl = *dep_files; fl != NULL; fl = fl->next) {
		if (fl->data == dep_file) {
			dep_file = NULL;
			break;
		}
	}
	if (dep_file == NULL) {
		/* file already included, ignore */
		return;
	}
	
	*dep_files = g_list_prepend (*dep_files, dep_file);

	char *dep_basename = g_strdup (dep_file->filename);
	dep_basename[strlen (dep_basename) - strlen (".vala")] = '\0';
	
	fprintf (f, "#include <%s.h>\n", dep_basename);
}

static void
vala_code_generator_process_source_file (ValaCodeGenerator *generator, ValaSourceFile *source_file)
{
	char *basename = g_strdup (source_file->filename);
	basename[strlen (basename) - strlen (".vala")] = '\0';
	
	/* FIXME: use output directory */
	
	char *c_filename = g_strdup_printf ("%s.c", basename);
	char *h_filename = g_strdup_printf ("%s.h", basename);
	
	char *header_define = filename_to_define (h_filename);
	
	/*
	 * FIXME: (optionally) skip source file if c_file and h_file already
	 * exist and their mtime is >= mtime of source_file
	 * => reduces unnecessary rebuilds
	 *
	 * to be really safe, ensure that output would be identical
	 */
	
	generator->c_file = fopen (c_filename, "w");
	generator->h_file = fopen (h_filename, "w");
	
	fprintf (generator->h_file, "#ifndef __%s__\n", header_define);
	fprintf (generator->h_file, "#define __%s__\n", header_define);
	fprintf (generator->h_file, "\n");

	fprintf (generator->h_file, "#include <stdio.h>\n");
	fprintf (generator->h_file, "#include <glib-object.h>\n");
	fprintf (generator->h_file, "\n");
	
	fprintf (generator->h_file, "G_BEGIN_DECLS\n");
	fprintf (generator->h_file, "\n");

	/* FIXME: fix leak */
	fprintf (generator->c_file, "#include \"%s\"\n", g_path_get_basename (h_filename));
	fprintf (generator->c_file, "\n");
	
	vala_code_generator_process_namespace1 (generator, source_file->root_namespace);
	GList *l;

	for (l = source_file->namespaces; l != NULL; l = l->next) {
		ValaNamespace *ns = l->data;
		
		vala_code_generator_process_namespace1 (generator, ns);
	}

	fprintf (generator->h_file, "G_END_DECLS\n");
	fprintf (generator->h_file, "\n");

	/* FIXME: add include directives for base class and other depending classes */
	/* use <> notation, assume layout in package include directory is the
	 * same as in the source directory. compiler should accept parameter to
	 * specify include root directory if something different than current
	 * working directory is needed */
	 
	GList *dep_files = NULL;
	
	for (l = source_file->namespaces; l != NULL; l = l->next) {
		ValaNamespace *ns = l->data;
		
		if (ns->import)
			continue;
			
		GList *cl;
		for (cl = ns->classes; cl != NULL; cl = cl->next) {
			ValaClass *c = cl->data;
			if (c->base_class != NULL) {
				vala_code_generator_process_dep_type (generator, generator->h_file, c->base_class->symbol, &dep_files);
			}
		}
	}

	for (l = source_file->dep_types; l != NULL; l = l->next) {
		ValaSymbol *sym = l->data;
		if (sym->type == VALA_SYMBOL_TYPE_CLASS && !sym->class->namespace->import) {
			ValaClass *class = sym->class;
			fprintf (generator->h_file, "#ifndef _TYPE_%s%s\n", class->namespace->upper_case_cname, class->upper_case_cname);
			fprintf (generator->h_file, "#define _TYPE_%s%s\n", class->namespace->upper_case_cname, class->upper_case_cname);
			fprintf (generator->h_file, "typedef struct _%s %s;\n", class->cname, class->cname);
			fprintf (generator->h_file, "typedef struct _%sClass %sClass;\n", class->cname, class->cname);
			fprintf (generator->h_file, "#endif\n");
		} else if (sym->type == VALA_SYMBOL_TYPE_ENUM) {
			vala_code_generator_process_dep_type (generator, generator->h_file, l->data, &dep_files);
		}
		vala_code_generator_process_dep_type (generator, generator->c_file, l->data, &dep_files);
	}
	fprintf (generator->h_file, "\n");

	fprintf (generator->h_file, "G_BEGIN_DECLS\n");
	fprintf (generator->h_file, "\n");

	vala_code_generator_process_namespace2 (generator, source_file->root_namespace);

	for (l = source_file->namespaces; l != NULL; l = l->next) {
		ValaNamespace *ns = l->data;
		
		if (!ns->import) {
			vala_code_generator_process_namespace2 (generator, ns);
		}
	}

	fprintf (generator->h_file, "G_END_DECLS\n");
	fprintf (generator->h_file, "\n");

	fprintf (generator->h_file, "#endif /* __%s__ */\n", header_define);
	
	fclose (generator->c_file);
	fclose (generator->h_file);
	
	generator->c_file = NULL;
	generator->h_file = NULL;
}

void
vala_code_generator_run (ValaCodeGenerator *generator)
{
	ValaSourceFile *source_file;
	
	GList *l;
	
	for (l = generator->context->source_files; l != NULL; l = l->next) {
		vala_code_generator_process_source_file (generator, l->data);
	}
}

void
vala_code_generator_free (ValaCodeGenerator *generator)
{
	g_free (generator);
}
