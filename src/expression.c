#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <sys/types.h>
#include <sys/time.h>

#include "bsd_queue.h"
#include "expression.h"
#include "fbuf.h"
#include <math.h>

// private functions 
static double op_boolean(expression_t *expr);
static double op_change(expression_t *expr);
static double op_arithmetic(expression_t *expr);
static int check_generic(expression_t *expr);
static int check_change(expression_t *expr);
static int check_arithmetic(expression_t *expr);
static int check_division(expression_t *expr);

static int _do_number_boolean(char op, double op1, double op2);
static int _do_str_boolean(char op, char *op1, char *op2);
static double _do_arithmetic(char op, double op1, double op2);

struct __expression_operator_s
{
    u_char op; //! the operation
    int minOperands; //! minimum number of operands
    int maxOperands; //! maximum number of operands (0 if unlimited)
    char label[EXPR_OPERATOR_LABEL_MAX_SIZE];
    double (*eval)(expression_t *expr); //! the operation callback (who do the job)
    int (*check)(expression_t *expr); //! check callback, used to verify integrity of the expression_t
};

struct __expression_operand_callback_s {
    expression_callback_t cb;
    char label[EXPR_CALLBACK_OPERAND_LABEL_MAX_SIZE];
    void *user;
    double lastValue;
    //time_t lastCall;
    struct timeval lastChange;
};

struct __expression_operand_s
{
    u_char type;                          //!< type of operand
    int refcnt;
    union {
        char string[EXPR_STRING_OPERAND_MAX_SIZE];
        int inum;
        double fnum;
        expression_t *expr; //! subexpression
        expression_operand_callback_t callback; //! callback. A user callback which will return a boolean value to use in the expression_t
    };
};

struct __expression_s
{
    expression_operator_t *operation; //! operation
    expression_operand_t **operands; //! NULL-terminated array of operands
    int numOperands;
    struct timeval lastEvaluation;
};


/*
struct __expression_rule_s {
    char *context;
    expression_t *rule;
    TAILQ_ENTRY(__expression_rule_s) rule_list;
} expression_rule_t;
*/

/* Operators' Lookup Table. 
 * NOTE: the index of the operators in the array MUST be the same of their id */
static expression_operator_t operations[] = {
        { EXPR_OP_NOT,             1, 1, "!",                op_boolean,                NULL },
        { EXPR_OP_TEST,            1, 1, "",                 op_boolean,                NULL },
        { EXPR_OP_OR,              2, 0, "OR",               op_boolean,                NULL },
        { EXPR_OP_AND,             2, 0, "AND",              op_boolean,                NULL },
        { EXPR_OP_XOR,             2, 0, "XOR",              op_boolean,                NULL },
        { EXPR_OP_EQ,              2, 2, "==",               op_boolean,                NULL },
        { EXPR_OP_NE,              2, 2, "!=",               op_boolean,                NULL },
        { EXPR_OP_GT,              2, 2, ">",                op_boolean,                NULL },
        { EXPR_OP_GE,              2, 2, ">=",               op_boolean,                NULL },
        { EXPR_OP_LT,              2, 2, "<",                op_boolean,                NULL },
        { EXPR_OP_LE,              2, 2, "<=",               op_boolean,                NULL },
        { EXPR_OP_SUM,             2, 0, "+",                op_arithmetic,        check_arithmetic },
        { EXPR_OP_SUB,             2, 0, "-",                op_arithmetic,        check_arithmetic },
        { EXPR_OP_MUL,             2, 0, "*",                op_arithmetic,        check_arithmetic },
        { EXPR_OP_DIV,             2, 2, "/",                op_arithmetic,        check_division },
        { EXPR_OP_MOD,             2, 2, "%",                op_arithmetic,        check_arithmetic },
        { EXPR_OP_SIN,             1, 1, "SIN",              op_arithmetic,        check_arithmetic },
        { EXPR_OP_ASIN,            1, 1, "ASIN",             op_arithmetic,        check_arithmetic },
        { EXPR_OP_COS,             1, 1, "COS",              op_arithmetic,        check_arithmetic },
        { EXPR_OP_ACOS,            1, 1, "ACOS",             op_arithmetic,        check_arithmetic },
        { EXPR_OP_TAN,             1, 1, "TAN",              op_arithmetic,        check_arithmetic },
        { EXPR_OP_ATAN,            1, 1, "ATAN",             op_arithmetic,        check_arithmetic },
        { EXPR_OP_ABS,             1, 1, "ABS",              op_arithmetic,        check_arithmetic },
        { EXPR_OP_CHANGE,          1, 2, "CHANGE",           op_change,            check_change }
};

static int
check_generic(expression_t *expr)
{
    if(expr->numOperands < expr->operation->minOperands ||
        (expr->operation->maxOperands && expr->numOperands > expr->operation->maxOperands)) 
        return 0;
    return 1;
}

static int
check_change(expression_t *expr)
{
    double test;
    if(expr->numOperands == 0 || expr->numOperands > 2) 
        return 0;

    if(expr->operands[0]->type != EXPR_OPTYPE_CALLBACK)
        return 0;

    if (expr->numOperands == 2) {
        expression_operand_t *op = expr->operands[1];
        switch(op->type) {
            case EXPR_OPTYPE_STRING:
                if(sscanf(op->string, "%lf", &test) != 1) {
                    fprintf(stderr, "op_arithmetic() : accepts only numeric values (no strings)\n");
                    return 0;
                }
                break;
        }
    }
    return 1;
}

static int
check_arithmetic(expression_t *expr)
{
    int offset = 0;
    double test;
    expression_operand_t **operands = expr->operands;

    while(operands[offset] != NULL) {
        expression_operand_t *op = operands[offset];
        switch(op->type) {
            case EXPR_OPTYPE_STRING:
                if(sscanf(op->string, "%lf", &test) != 1) {
                    fprintf(stderr, "op_arithmetic() : accepts only numeric values (no strings)\n");
                    return 0;
                }
                break;
        }
        offset++;
    }
    return 1;
}

static int
check_division(expression_t *expr)
{
    double test;
    expression_operand_t *op1 = expr->operands[1];

    if(!check_arithmetic(expr))
        return 0;

    if(!op1)
        return 0;

    switch(op1->type) {
        case EXPR_OPTYPE_STRING:
            if(sscanf(op1->string, "%lf", &test) != 1) {
                fprintf(stderr, "op_arithmetic() : accepts only numeric values (no strings)\n");
                return 0;
            }
            break;
        case EXPR_OPTYPE_INTEGER:
            test = (double)op1->inum;
            break;
        case EXPR_OPTYPE_FLOAT:
            test = op1->fnum;
            break;
        case EXPR_OPTYPE_CALLBACK:
            test = op1->callback.cb(op1->callback.user); 
            expression_callback_change_value(op1, test);
            //op1->callback.lastCall = time(NULL);
            break;
        case EXPR_OPTYPE_EXPRESSION:
            // evaluate sub expression_t (recursion happens here)
            test = expression_evaluate(op1->expr);
            if(test == -1) // abort if we have an invalid subexpression as divisor
                return 0;
            break;
    }
    if(test == 0) // division is not valid if the divisor is 0
        return 0;
    return 1;
}

static double
_do_arithmetic(char op, double op1, double op2)
{
    switch(op) {
        case EXPR_OP_SUM: // true if one isn't a null string
            return op1 + op2;
        case EXPR_OP_SUB:
            return op1 - op2;
        case EXPR_OP_MUL:
            return op1 * op2;
        case EXPR_OP_DIV:
            return op1 / op2;
        case EXPR_OP_MOD:
            return (int)op1 % (int)op2;
        case EXPR_OP_SIN:
            return sin(op1);
        case EXPR_OP_ASIN:
            return asin(op1);
        case EXPR_OP_COS:
            return cos(op1);
        case EXPR_OP_ACOS:
            return acos(op1);
        case EXPR_OP_TAN:
            return tan(op1);
        case EXPR_OP_ATAN:
            return atan(op1);
        case EXPR_OP_ABS:
            return abs(op1);
    }
    return 0;
}

static int
_do_str_boolean(char op, char *op1, char *op2)
{
    switch(op) {
        case EXPR_OP_OR: // true if one isn't a null string
            return (op1[0] || op2[0]);
        case EXPR_OP_AND:
            return (op1[0] && op2[0]);
        case EXPR_OP_XOR:
            return (op1[0] ^ op2[0]);
        case EXPR_OP_EQ:
            return (!strcmp(op1, op2));
        case EXPR_OP_NE:
            return (strcmp(op1, op2));
        case EXPR_OP_GT:
            return ((strcmp(op1, op2) < 0)?1:0);
        case EXPR_OP_GE:
            return ((strcmp(op1, op2) <= 0)?1:0);
        case EXPR_OP_LT:
            return ((strcmp(op1, op2) > 0)?1:0);
        case EXPR_OP_LE:
            return ((strcmp(op1, op2) >= 0)?1:0);
    }
    return 0;
}

static int
_do_number_boolean(char op, double op1, double op2)
{
    switch(op) {
        case EXPR_OP_OR:
            return (op1 || op2);
        case EXPR_OP_AND:
            return (op1 && op2);
        case EXPR_OP_XOR:
            return ((uint64_t)op1  ^ (uint64_t)op2);
        case EXPR_OP_EQ:
            return (op1 == op2);
        case EXPR_OP_NE:
            return (op1 != op2);
        case EXPR_OP_GT:
            return (op1  > op2);
        case EXPR_OP_GE:
            return (op1  >= op2);
        case EXPR_OP_LT:
            return (op1  < op2);
        case EXPR_OP_LE:
            return (op1  <= op2);
    }
    return 0;
}

static double
op_change(expression_t *expr)
{
    double period = 1.0;
    struct timeval now;

    gettimeofday(&now, NULL);

    expression_operand_t *op1 = expr->operands[0];

    if(expr->numOperands == 2) {
        expression_operand_t *op2 = expr->operands[1];
        switch(op2->type) {
            case EXPR_OPTYPE_INTEGER:
                    period = (double)op2->inum;
                    break;
            case EXPR_OPTYPE_FLOAT:
                    period = op2->fnum;
                    break;
            case EXPR_OPTYPE_STRING:
                    if(sscanf(op2->string, "%lf", &period) != 1)  {
                            fprintf(stderr, "'%s' string doesn't represent an integer", op2->string);
                            return 0; 
                    }
                    break;
            case EXPR_OPTYPE_CALLBACK:
                    period = op2->callback.cb(op2->callback.user);
                    break;
            case EXPR_OPTYPE_EXPRESSION:
                    period = expression_evaluate(op2->expr);
                    break;
            default:
                    return 0;
        }
    }

    double diff = (now.tv_sec - op1->callback.lastChange.tv_sec) + ((now.tv_usec - op1->callback.lastChange.tv_usec) / 1e6);
    return (double)(diff <= period);
}

// returns the result of the arithmetic expression_t (expanding subexpressions)
static double 
op_arithmetic(expression_t *expr)
{
    double num1 = 0;
    expression_operand_t *op1;
    double res = -1;
    int offset = 0;
    char op = expr->operation->op;
    expression_operand_t **operands = expr->operands;

    // iterate over the NULL-terminated array of operands
    while(operands[offset] != NULL) {
        op1 = operands[offset];
        switch(op1->type) {
            case EXPR_OPTYPE_CALLBACK:
                num1 = op1->callback.cb(op1->callback.user);
                expression_callback_change_value(op1, num1);
                //op1->callback.lastCall = time(NULL);
            break;
            case EXPR_OPTYPE_EXPRESSION:
                // evaluate sub expression_t (recursion happens here)
                num1 = expression_evaluate(op1->expr);
                if(num1 < 0) 
                        return -1;
                break;
            case EXPR_OPTYPE_INTEGER:
                num1 = (double)op1->inum;
                break;
            case EXPR_OPTYPE_FLOAT:
                num1 = (double)op1->fnum;
                break;
            case EXPR_OPTYPE_STRING:
                if(sscanf(op1->string, "%lf", &num1) != 1) {
                    fprintf(stderr, "op_arithmetic() : accepts only numeric values (no strings)\n");
                    return -1;
                }
                break;
            default:
                fprintf(stderr, "op_boolean() : Uknown operand type: 0x%02x\n", op1->type);
                return -1;
        }

        if(offset == 0) { 
            if (expr->operation->minOperands == 1 && expr->operation->maxOperands == 1)
                res = _do_arithmetic(op, num1, 0);
            else
                res = num1;
        } else {
            res = _do_arithmetic(op, (double)res, num1);
        }

        offset++;
    }
    
    return res;
}


// returns -1 on error 0 if FALSE 1 if TRUE
static double 
op_boolean(expression_t *expr)
{
    double num1 = 0;
    char str1[EXPR_STRING_OPERAND_MAX_SIZE];
    char str_prev[EXPR_STRING_OPERAND_MAX_SIZE];
    expression_operand_t *op1;
    double res = 0;
    int offset = 0;
    int is_integer = 0;
    char op = expr->operation->op;
    expression_operand_t **operands = expr->operands;

    // iterate over the NULL-terminated array of operands
    while(operands[offset] != NULL) {
        op1 = operands[offset];
        switch(op1->type) {
            case EXPR_OPTYPE_CALLBACK:
                num1 = op1->callback.cb(op1->callback.user);
                expression_callback_change_value(op1, num1);
                //op1->callback.lastCall = time(NULL);
                sprintf(str1, "%lf", num1);
                break;
            case EXPR_OPTYPE_EXPRESSION:
                // evaluate sub expression_t (recursion happens here)
                num1 = expression_evaluate(op1->expr);
                sprintf(str1, "%lf", num1);
                break;
            case EXPR_OPTYPE_INTEGER:
                num1 = (double)op1->inum;
                sprintf(str1, "%lf", num1);
                break;
            case EXPR_OPTYPE_FLOAT:
                num1 = op1->fnum;
                sprintf(str1, "%lf", num1);
                break;
            case EXPR_OPTYPE_STRING:
                snprintf(str1, sizeof(str1), "%s", op1->string);
                if(sscanf(str1, "%lf", &num1) != 1) 
                    num1 = str1[0]?1:0;
                else
                    is_integer = 1;
                break;
            default:
                fprintf(stderr, "op_boolean() : Uknown operand type: 0x%02x\n", op1->type);
                return 0;
        }

        if(op == EXPR_OP_TEST) 
            return (num1?1:0);
        else if(op == EXPR_OP_NOT)
            return (num1?0:1);

        if(offset == 0) {
            res = num1;
            strcpy(str_prev, str1);                // save the string counterpart of the 'res' variable
        } else {
            if(op1->type == EXPR_OPTYPE_STRING && !is_integer) {
                res = (double)_do_str_boolean(op, str_prev, str1);        
                sprintf(str_prev, "%f", res);
            } else {
                res = _do_number_boolean(op, res, num1);
                sprintf(str_prev, "%f", res);
            }
        }

        offset++;
    }
    
    return res;
}

double
expression_evaluate(expression_t *expr)
{
    double res = -1;

    if(!expr)
        return -1;

    // execute generic checks (number of operands and such)
    if(!check_generic(expr))
        return -1;

    // execute extra checks if we have to 
    if(expr->operation->check)
        if(!expr->operation->check(expr))
            return -1;

    res = expr->operation->eval(expr);

#ifdef __DEBUG_EXPRESSION
    expression_dump(expr);
    printf("  ==  %d \n", res);
#endif
    return res;
}


expression_t *
expression_create_empty(u_char op)
{
    expression_t *newexpr = NULL;
    int operationsNum = sizeof(operations)/sizeof(expression_operator_t);

    if (op >= operationsNum) {
        fprintf(stderr, "at_createExpression() : Invalid operation id 0x%x\n", op);
        return NULL;
    }
    newexpr = calloc(1, sizeof(expression_t));
    if(!newexpr)
        return NULL;

    newexpr->operation = calloc(1, sizeof(expression_operator_t));
    if(!newexpr->operation) {
        free(newexpr);
        return NULL;
    }

    memcpy(newexpr->operation, &operations[op], sizeof(expression_operator_t));

    return newexpr;
}

expression_t *
expression_createv(u_char op, int numOperands, va_list operands)
{
    int i;
    expression_operand_t *operand;
    expression_t *newexpr = expression_create_empty(op);
   
    if (numOperands < operations[op].minOperands ||
        (operations[op].maxOperands && numOperands > operations[op].maxOperands))
    {
        fprintf(stderr, "at_createExpression() : Invalid number of operands %d for operation %s\n", numOperands, operations[op].label);
        free(newexpr);
        return NULL;
    }

    if(!newexpr->operation->eval) { // invalid operation
        fprintf(stderr, "at_createExpression() : Uknown operation 0x%x\n", op);
        free(newexpr);
        return NULL;
    }
    
    /* be sure to create a NULL entry in the operations array (just for safety ... 
     * if someone would ever try to evaluate an empty expression_t) */
    newexpr->operands = calloc(1, sizeof(expression_operand_t *));
    newexpr->operands[0] = NULL;

    for(i = 0; i < numOperands; i++) {
        if((operand = va_arg(operands, expression_operand_t *)) != NULL) {
            if(!operand_add(newexpr, operand)) {
                /* TODO - Error Messages */
                if(newexpr->operands)
                    free(newexpr->operands);
                free(newexpr);
                return NULL;
            }
        }
    }
    return newexpr;

}

expression_t *
expression_create(u_char op, int numOperands, ...)
{
    expression_t *expr;
    va_list operands;
    // add operands if any ... they could be added later calling operand_add
    va_start(operands, numOperands);
    expr = expression_createv(op, numOperands, operands);
    va_end(operands);
    return expr;
}

int
operand_add(expression_t *expr, expression_operand_t *operand) {
    if(operations[expr->operation->op].maxOperands > 0 &&
            operations[expr->operation->op].maxOperands == expr->numOperands)
    {
        fprintf(stderr, "operand_add() : Max operands reached\n");
        return 0;
    }
    expr->numOperands++;
    expr->operands = realloc(expr->operands, 
        sizeof(expression_operand_t *) * (expr->numOperands + 1) );
    if(!expr->operands)
        return 0;

    expr->operands[expr->numOperands-1] = operand;
    operand->refcnt++;
    expr->operands[expr->numOperands] = NULL; // be sure to NULL-terminate the operands array

    return 1;
}

void
expression_destroy_deep(expression_t *expr)
{
    int i;
    for(i = 0; i < expr->numOperands; i++) {
        if(expr->operands[i]) {
            if (expr->operands[i]->type == EXPR_OPTYPE_EXPRESSION)
                expression_destroy_deep(expr->operands[i]->expr);
            expression_operand_destroy(expr->operands[i]);
        }
    }
    free(expr->operands);
    free(expr->operation);
    free(expr);
}

void
expression_destroy(expression_t *expr)
{
    int i;
    for(i = 0; i < expr->numOperands; i++) {
        if(expr->operands[i])
            expression_operand_destroy(expr->operands[i]);
    }
    free(expr->operands);
    free(expr->operation);
    free(expr);
}


void
expression_operand_destroy(expression_operand_t *operand)
{
    if (operand->refcnt > 1) {
        operand->refcnt--;
        return;
    } 
    free(operand);
}

expression_operand_t
*callback_operand_create(expression_callback_t cb, char *label, void *user)
{
    expression_operand_t *newoperand = calloc(1, sizeof(expression_operand_t));
    if(newoperand) {
        newoperand->type = EXPR_OPTYPE_CALLBACK;
        newoperand->callback.cb = cb;
        newoperand->callback.user = user;
        if (label)
            snprintf(newoperand->callback.label, sizeof(newoperand->callback.label), "%s", label);
    }
    return newoperand;
}

expression_operand_t
*expression_operand_create(expression_t *expr)
{
    expression_operand_t *newoperand = calloc(1, sizeof(expression_operand_t));
    if(newoperand) {
        newoperand->type = EXPR_OPTYPE_EXPRESSION;
        newoperand->expr = expr;
    }
    return newoperand;
}

expression_operand_t
*integer_operand_create(int num)
{
    expression_operand_t *newoperand = calloc(1, sizeof(expression_operand_t));
    if(newoperand) {
        newoperand->type = EXPR_OPTYPE_INTEGER;
        newoperand->inum = num;
    }
    return newoperand;
}

expression_operand_t
*float_operand_create(double num)
{
    expression_operand_t *newoperand = calloc(1, sizeof(expression_operand_t));
    if(newoperand) {
        newoperand->type = EXPR_OPTYPE_FLOAT;
        newoperand->fnum = num;
    }
    return newoperand;
}

expression_operand_t
*string_operand_create(char *str)
{
    expression_operand_t *newoperand = calloc(1, sizeof(expression_operand_t));
    if(newoperand) {
        newoperand->type = EXPR_OPTYPE_STRING;
        snprintf(newoperand->string, sizeof(newoperand->string), "%s", str);
    }
    return newoperand;
}

void expression_callback_change_value(expression_operand_t *op, double value)
{
    if (op->type != EXPR_OPTYPE_CALLBACK)
        return;

    if (op->callback.lastValue != value)
        gettimeofday(&op->callback.lastChange, NULL);
    op->callback.lastValue = value;
}

static inline void
expression_dump_internal(expression_t *expr, fbuf_t *buf)
{
    int i = 0;
    expression_operand_t *operand;
    expression_t *subexpr;

    while (expr->operands[i]) {
        operand = expr->operands[i];
        if (expr->operation->minOperands == 1 && expr->operation->maxOperands == 1)
        {
            if (expr->operation->op >= EXPR_OP_SIN && expr->operation->op <= EXPR_OP_ATAN) {
                fbuf_printf(buf, " %s(", expr->operation->label);
            } else {
                fbuf_printf(buf, " %s ", expr->operation->label);
            }
        }
        switch(operand->type) {
            case EXPR_OPTYPE_EXPRESSION:
                subexpr = operand->expr;
                if (subexpr->numOperands > 1)
                    fbuf_printf(buf, "( ");

                expression_dump_internal(operand->expr, buf);

                if(subexpr->numOperands > 1)
                    fbuf_printf(buf, " )");
                break;
            case EXPR_OPTYPE_INTEGER:
                if (expr->operation->op >= EXPR_OP_SIN && expr->operation->op <= EXPR_OP_ATAN) {
                    fbuf_printf(buf, "%lfπ", operand->inum/3.14);
                } else {
                    fbuf_printf(buf, "%d", operand->inum);
                }
                break;
            case EXPR_OPTYPE_FLOAT:
                if (expr->operation->op >= EXPR_OP_SIN && expr->operation->op <= EXPR_OP_ATAN) {
                    fbuf_printf(buf, "%lfπ", operand->fnum/3.14);
                } else {
                    fbuf_printf(buf, "%lf", operand->fnum);
                }
                break;
            case EXPR_OPTYPE_STRING:
                fbuf_printf(buf, "%s", operand->string);
                break;
            case EXPR_OPTYPE_CALLBACK:
                if (operand->callback.label)
                    fbuf_printf(buf, "callback:%s", operand->callback.label);
                else
                    fbuf_printf(buf, "callback:0x%p", operand->callback.cb);
                break;
        }
        if(expr->operands[++i] && expr->operation->op != EXPR_OP_NOT) {
            if((expr->operation->minOperands > 1 && expr->operation->maxOperands != 1) || 
               expr->operation->op == EXPR_OP_CHANGE)
            {
                fbuf_printf(buf, " %s ", expr->operation->label);
            }
        } else if (i == 0 && expr->operation->op == EXPR_OP_CHANGE) {
            fbuf_printf(buf, " %s 1", expr->operation->label);
        }
    }

    if(expr->operation->minOperands == 1 && expr->operation->maxOperands == 1 &&
       expr->operation->op >= EXPR_OP_SIN && expr->operation->op <= EXPR_OP_ATAN)
    {
        fbuf_add(buf, ")");
    }
}

char *expression_dump(expression_t *expr) {
    fbuf_t buf = FBUF_STATIC_INITIALIZER;
    expression_dump_internal(expr, &buf);
    return fbuf_data(&buf);
}
