#include "internal.h"

typedef struct expression_value {
    script_eval_value value;
    struct expression_value *previous,*next;
} expression_value;
typedef struct expression_operator {
    const qa_script_token *token;
    int depth,precedence;
    struct expression_operator *previous,*next;
} expression_operator;
static int precedence(uint32_t op) {
    switch (op) {
    case QA_SCRIPT_LOGICAL_NOT:
    case QA_SCRIPT_NOT:
        return 16;
    case QA_SCRIPT_MUL:
    case QA_SCRIPT_DIV:
    case QA_SCRIPT_MOD:
        return 15;
    case QA_SCRIPT_ADD:
    case QA_SCRIPT_SUB:
        return 14;
    case QA_SCRIPT_RSHIFT:
    case QA_SCRIPT_LSHIFT:
        return 13;
    case QA_SCRIPT_GE:
    case QA_SCRIPT_LE:
    case QA_SCRIPT_GT:
    case QA_SCRIPT_LT:
        return 12;
    case QA_SCRIPT_EQ:
    case QA_SCRIPT_NE:
        return 11;
    case QA_SCRIPT_AND:
        return 10;
    case QA_SCRIPT_XOR:
        return 9;
    case QA_SCRIPT_OR:
        return 8;
    case QA_SCRIPT_LOGICAL_AND:
        return 7;
    case QA_SCRIPT_LOGICAL_OR:
        return 6;
    case QA_SCRIPT_QUESTION:
    case QA_SCRIPT_COLON:
        return 5;
    case QA_SCRIPT_INCREMENT:
    case QA_SCRIPT_DECREMENT:
        return 0;
    default:
        return -1;
    }
}
static bool integer_only(uint32_t op) {
    return op == QA_SCRIPT_NOT || op == QA_SCRIPT_MOD || op == QA_SCRIPT_RSHIFT ||
           op == QA_SCRIPT_LSHIFT || op == QA_SCRIPT_AND || op == QA_SCRIPT_OR ||
           op == QA_SCRIPT_XOR;
}
static bool checked(qa_script *s, qa_script_location location, int64_t value, int32_t *out,
                    qa_error *e) {
    if (value < INT32_MIN || value > INT32_MAX)
        return script_fail(s, location, "Expression exceeds signed 32-bit range", e);
    *out = (int32_t)value;
    return true;
}
static bool binary(qa_script *s, const qa_script_token *op, script_eval_value a,
                   script_eval_value b, script_eval_value *out, qa_error *e) {
    script_eval_value v = a;
    int64_t x = a.integer, y = b.integer;
    switch (op->subtype) {
    case QA_SCRIPT_MUL:
        if (!checked(s, op->location, x * y, &v.integer, e))
            return false;
        v.number = a.number * b.number;
        break;
    case QA_SCRIPT_DIV:
    case QA_SCRIPT_MOD:
        if (y == 0 || (op->subtype == QA_SCRIPT_DIV && b.number == 0))
            return script_fail(s, op->location, "Expression division by zero", e);
        if (!checked(s, op->location, x / y, &v.integer, e))
            return false;
        if (op->subtype == QA_SCRIPT_MOD)
            v.integer = (int32_t)(x % y);
        else
            v.number = a.number / b.number;
        break;
    case QA_SCRIPT_ADD:
        if (!checked(s, op->location, x + y, &v.integer, e))
            return false;
        v.number = a.number + b.number;
        break;
    case QA_SCRIPT_SUB:
        if (!checked(s, op->location, x - y, &v.integer, e))
            return false;
        v.number = a.number - b.number;
        break;
    case QA_SCRIPT_LOGICAL_AND:
        v.integer = x != 0 && y != 0;
        v.number = a.number != 0 && b.number != 0;
        break;
    case QA_SCRIPT_LOGICAL_OR:
        v.integer = x != 0 || y != 0;
        v.number = a.number != 0 || b.number != 0;
        break;
    case QA_SCRIPT_GE:
        v.integer = x >= y;
        v.number = a.number >= b.number;
        break;
    case QA_SCRIPT_LE:
        v.integer = x <= y;
        v.number = a.number <= b.number;
        break;
    case QA_SCRIPT_GT:
        v.integer = x > y;
        v.number = a.number > b.number;
        break;
    case QA_SCRIPT_LT:
        v.integer = x < y;
        v.number = a.number < b.number;
        break;
    case QA_SCRIPT_EQ:
        v.integer = x == y;
        v.number = a.number == b.number;
        break;
    case QA_SCRIPT_NE:
        v.integer = x != y;
        v.number = a.number != b.number;
        break;
    case QA_SCRIPT_RSHIFT:
        if (y < 0 || y >= 32)
            return script_fail(s, op->location, "Invalid signed shift count", e);
        v.integer = (int32_t)(x >= 0 ? x / ((int64_t)1 << y) : -1 - ((-1 - x) / ((int64_t)1 << y)));
        break;
    case QA_SCRIPT_LSHIFT:
        if (y < 0 || y >= 32 || x < 0)
            return script_fail(s, op->location, "Invalid signed left shift", e);
        if (!checked(s, op->location, x * ((int64_t)1 << y), &v.integer, e))
            return false;
        break;
    case QA_SCRIPT_AND:
        v.integer = a.integer & b.integer;
        break;
    case QA_SCRIPT_OR:
        v.integer = a.integer | b.integer;
        break;
    case QA_SCRIPT_XOR:
        v.integer = a.integer ^ b.integer;
        break;
    default:
        return script_fail(s, op->location, "Unsupported expression operator", e);
    }
    *out = v;
    return true;
}
static bool evaluate(qa_script *s, const qa_script_token *tokens, size_t count, bool integer_mode,
                       script_eval_value *out, qa_error *e) {
    expression_value values[64],*first_value=NULL,*last_value_cell=NULL;
    expression_operator operators[64],*first_operator=NULL,*last_operator=NULL;
    script_eval_value question={0};size_t value_count=0,operator_count=0;
    int depth = 0;
    bool last_value = false, negative = false, has_question = false;
    for (size_t i = 0; i < count; ++i) {
        const qa_script_token *t = tokens + i;
        script_eval_value value = {0};
        bool add = false;
        if (t->kind == QA_SCRIPT_NAME) {
            if (last_value || negative || !qa_script_token_is(t, "defined"))
                return script_fail(s, t->location, "Unexpected name in expression", e);
            if (++i == count)
                return script_fail(s, t->location, "defined requires a macro name", e);
            bool brace = qa_script_token_is(tokens + i, "(");
            if (brace && ++i == count)
                return script_fail(s, t->location, "defined requires a macro name", e);
            if (tokens[i].kind != QA_SCRIPT_NAME)
                return script_fail(s, t->location, "defined requires a macro name", e);
            script_macro *defined_macro=NULL;
            if(!script_macro_lookup(&s->macros,tokens[i].text,&defined_macro,e)) return false;
            value.integer=defined_macro!=NULL;
            value.number = value.integer;
            add = true;
            if (brace && (++i == count || !qa_script_token_is(tokens + i, ")")))
                return script_fail(s, t->location, "Missing ) after defined", e);
        } else if (t->kind == QA_SCRIPT_NUMBER) {
            if (last_value)
                return script_fail(s, t->location, "Adjacent values in expression", e);
            value = (script_eval_value){t->integer, t->number};
            if (negative) {
                if (!checked(s, t->location, -(int64_t)value.integer, &value.integer, e))
                    return false;
                value.number = -value.number;
            }
            negative = false;
            add = true;
        } else if (t->kind == QA_SCRIPT_PUNCTUATION) {
            if (negative)
                return script_fail(s, t->location, "Misplaced minus sign in expression", e);
            uint32_t op = t->subtype;
            if (op == QA_SCRIPT_OPEN_PAREN) {
                if (depth == INT_MAX)
                    return script_fail(s, t->location, "Expression nesting overflow", e);
                ++depth;
                continue;
            }
            if (op == QA_SCRIPT_CLOSE_PAREN) {
                if (--depth < 0)
                    return script_fail(s, t->location, "Unmatched ) in expression", e);
                continue;
            }
            if (!integer_mode && integer_only(op))
                return script_fail(s, t->location, "Integer operator in floating expression", e);
            if (op == QA_SCRIPT_NOT || op == QA_SCRIPT_LOGICAL_NOT) {
                if (last_value)
                    return script_fail(s, t->location, "Unary operator after expression value", e);
            } else if (op == QA_SCRIPT_SUB && !last_value) {
                negative = true;
                continue;
            } else if (op == QA_SCRIPT_INCREMENT || op == QA_SCRIPT_DECREMENT)
                script_warn(s, t->location, "Increment/decrement in expression");
            else if (!last_value || precedence(op) < 0)
                return script_fail(s, t->location, "Invalid expression operator", e);
            if (operator_count == 64)
                return script_fail(s, t->location, "Expression exceeds 64 source operators", e);
            expression_operator *entry=&operators[operator_count++];
            *entry=(expression_operator){t,depth,precedence(op),last_operator,NULL};
            if(last_operator) last_operator->next=entry;else first_operator=entry;
            last_operator=entry;
            last_value = false;
        } else
            return script_fail(s, t->location, "Invalid expression token", e);
        if (add) {
            if (value_count == 64)
                return script_fail(s, t->location, "Expression exceeds 64 source values", e);
            expression_value *entry=&values[value_count++];
            *entry=(expression_value){value,last_value_cell,NULL};
            if(last_value_cell) last_value_cell->next=entry;else first_value=entry;
            last_value_cell=entry;
            last_value = true;
        }
    }
    if (!last_value || depth != 0)
        return script_fail(s, qa_script_position(s), "Incomplete expression", e);
    while(first_operator) {
        expression_operator *operation=first_operator;expression_value *value=first_value;
        while(operation->next) {
            expression_operator *following=operation->next;
            if(operation->depth>following->depth || (operation->depth==following->depth && operation->precedence>=following->precedence)) break;
            if(operation->token->subtype!=QA_SCRIPT_NOT && operation->token->subtype!=QA_SCRIPT_LOGICAL_NOT) value=value?value->next:NULL;
            if(!value) return script_fail(s,operation->token->location,"Missing expression operand",e);
            operation=following;
        }
        const qa_script_token *op=operation->token;
        if(!value) return script_fail(s,op->location,"Missing expression operand",e);
        if(op->subtype==QA_SCRIPT_LOGICAL_NOT)
            value->value=(script_eval_value){value->value.integer==0,value->value.number==0};
        else if(op->subtype==QA_SCRIPT_NOT) value->value.integer=~value->value.integer;
        else {
            expression_value *following=value->next,*removed=following;
            if(op->subtype==QA_SCRIPT_QUESTION) {
                if(has_question) return script_fail(s,op->location,"Nested source conditional operator",e);
                question=value->value;has_question=true;removed=value;
            } else {
                if(!following) return script_fail(s,op->location,"Missing binary expression operand",e);
                if(op->subtype==QA_SCRIPT_COLON) {
                    if(!has_question) return script_fail(s,op->location,"Conditional : without ?",e);
                    if(integer_mode) value->value.integer=question.integer==0?following->value.integer:value->value.integer;
                    else value->value.number=question.number==0?following->value.number:value->value.number;
                    has_question=false;
                } else if(op->subtype!=QA_SCRIPT_INCREMENT && op->subtype!=QA_SCRIPT_DECREMENT &&
                    !binary(s,op,value->value,following->value,&value->value,e)) return false;
            }
            if(removed->previous) removed->previous->next=removed->next;else first_value=removed->next;
            if(removed->next) removed->next->previous=removed->previous;else last_value_cell=removed->previous;
        }
        if(operation->previous) operation->previous->next=operation->next;else first_operator=operation->next;
        if(operation->next) operation->next->previous=operation->previous;else last_operator=operation->previous;
    }
    if(!first_value) return script_fail(s,qa_script_position(s),"Expression has no result",e);
    *out=first_value->value;return true;
}

bool script_expression(qa_script *source,uint32_t head,bool integer_mode,script_eval_value *out,qa_error *error) {
    qa_script_token *tokens=NULL;size_t count=0,capacity=0,remaining=source->macros.queue_count;
    /* Snapshot the actual chain before reducing it, as the original evaluator
     * does. The projection is local and leaves the real heap untouched. */
    while(head) {
        if(!remaining--) {qa_error_set(error,QA_ERROR_FORMAT,0,"Expression token chain contains a cycle");goto fail;}
        script_token_record *token=script_heap_token(&source->macros,head);
        if(!script_heap_token_bytes(&source->macros,token,error) ||
            !script_grow((void **)&tokens,&capacity,count+1,sizeof(*tokens),error) ||
            !script_token_load(token->record.bytes,token->extent,token->location,token->whitespace,&source->arena,tokens+count,error)) goto fail;
        ++count;head=qa_load_u32le(token->record.bytes+1064);
    }
    bool ok=evaluate(source,tokens,count,integer_mode,out,error);free(tokens);return ok;
fail:
    free(tokens);return false;
}
