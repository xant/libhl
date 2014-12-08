/** 
 * \file expression_t.h
 * 
 * \brief  TODO 
 *
 */

#include "bsd_queue.h"
#include "sys/types.h"
#include <stdarg.h>

#ifndef __EXPRESSION_H__
#define __EXPRESSION_H__

#define EXPR_MAX_OPERANDS 32

typedef struct __expression_s expression_t;
typedef struct __expression_operand_s expression_operand_t;
typedef struct __expression_operator_s expression_operator_t;

#define EXPR_OP_NOT                0
#define EXPR_OP_TEST               1
#define EXPR_OP_OR                 2
#define EXPR_OP_AND                3
#define EXPR_OP_XOR                4
#define EXPR_OP_EQ                 5
#define EXPR_OP_NE                 6
#define EXPR_OP_GT                 7
#define EXPR_OP_GE                 8
#define EXPR_OP_LT                 9
#define EXPR_OP_LE                 10 
#define EXPR_OP_SUM                11
#define EXPR_OP_SUB                12
#define EXPR_OP_MUL                13
#define EXPR_OP_DIV                14
#define EXPR_OP_MOD                15
#define EXPR_OP_SIN                16
#define EXPR_OP_ASIN               17
#define EXPR_OP_COS                18
#define EXPR_OP_ACOS               19
#define EXPR_OP_TAN                20
#define EXPR_OP_ATAN               21
#define EXPR_OP_ABS                22
#define EXPR_OP_CHANGE             23 

#define EXPR_OPTYPE_INTEGER      0        //!< int
#define EXPR_OPTYPE_FLOAT        1        //!< float
#define EXPR_OPTYPE_STRING       2        //!< string
#define EXPR_OPTYPE_CALLBACK     3        //!< callback 
#define EXPR_OPTYPE_EXPRESSION   4        //!< sub-expression_t
 
#define EXPR_OPERATOR_LABEL_MAX_SIZE 16
#define EXPR_STRING_OPERAND_MAX_SIZE 1024
#define EXPR_CALLBACK_OPERAND_LABEL_MAX_SIZE 256

typedef double (*expression_callback_t)(void *user);

typedef struct __expression_operand_callback_s expression_operand_callback_t;

void expression_callback_change_value(expression_operand_t *op, double value);

/*! Create a new expression_t entity
 * you can create an expression_t with 0 operands and add them later. 
 * validity of the expression_t will be checked at evaluation time.
 * TODO - a function to check the "syntax" of the expression_t (int of operands and such)
 */
expression_t *expression_create(u_char op, int numOperands, ... );
expression_t *expression_createv(u_char op, int numOperands, va_list operands);
expression_t *expression_create_empty(u_char op);

//! Create a callback operand 
expression_operand_t *callback_operand_create(expression_callback_t cb, char *label, void *user);
//! Create an expression_t operand (aka - a sub expression_t) 
expression_operand_t *expression_operand_create(expression_t *expr);
//! Create a integer operand (mostly for numeric comparison) 
expression_operand_t *integer_operand_create(int num);

expression_operand_t *float_operand_create(double num);

/*! Create a string operand (mostly for string comparison) 
 * Note that inside the same expression_t :
 *   - If used with subexpression operands, null string is 0 anything else is 1.
 *   - If used with string, string comparison routines will be used
 *   - If used with integer operands, integer is converted through sprintf and 
 *      string comparison routines will be used 
 */
expression_operand_t *string_operand_create(char *str);
//! Add an operand to an existing expression_t 
int operand_add(expression_t *expr, expression_operand_t *operand);
//! Evaluate the expression_t. returns 1 if TRUE , 0 if FALSE
double expression_evaluate(expression_t *expr);
//! Release all resources associated to an expression_t 
// (doesn't destroy subexpressions, the caller is responsible for disposing subexpressions if any)
void expression_destroy(expression_t *expr);

//! Release all resources associated to an expression_t and all subexpressions
void expression_destroy_deep(expression_t *expr);


//! Release all resources associated to an operand
void expression_operand_destroy(expression_operand_t *operand);

char *expression_dump(expression_t *expr);

#endif

