#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "tokenizer.h"
#include "hashmap.h"
#include "parser.h"
#include <string.h>
#include <stddef.h>
#include "osstream.h"
#include "console.h"
#include "fs.h"
#include <ctype.h>
#include "formatvisit.h"
#ifdef _WIN32
#include <crtdbg.h>

#undef assert
#define assert _ASSERTE
#endif

#include "visit.h"
#include <time.h>

struct defer_statement* defer_statement(struct parser_ctx* ctx, struct error* error);

static int anonymous_struct_count = 0;

///////////////////////////////////////////////////////////////////////////////
void naming_convention_struct_tag(struct parser_ctx* ctx, struct token* token);
void naming_convention_enum_tag(struct parser_ctx* ctx, struct token* token);
void naming_convention_function(struct parser_ctx* ctx, struct token* token);
void naming_convention_enumerator(struct parser_ctx* ctx, struct token* token);
void naming_convention_struct_member(struct parser_ctx* ctx, struct token* token, struct type* type);
void naming_convention_parameter(struct parser_ctx* ctx, struct token* token, struct type* type);
void naming_convention_global_var(struct parser_ctx* ctx, struct token* token, struct type* type);
void naming_convention_local_var(struct parser_ctx* ctx, struct token* token, struct type* type);

///////////////////////////////////////////////////////////////////////////////

/*coisas que vao em hash map possuim um id
assim é possivel usar o mesmo mapa para achar tipos diferentes
*/
enum
{
    TAG_TYPE_NONE,
    TAG_TYPE_ENUN_SPECIFIER,
    TAG_TYPE_STRUCT_OR_UNION_SPECIFIER,
    TAG_TYPE_ENUMERATOR,
    TAG_TYPE_DECLARATOR,
};


#ifdef TEST
int printf_nothing(const char* fmt, ...) { return 0; }
#endif

void scope_list_push(struct scope_list* list, struct scope* pnew)
{
    if (list->tail)
        pnew->scope_level = list->tail->scope_level + 1;

    if (list->head == NULL)
    {
        list->head = pnew;
        list->tail = pnew;
        //pnew->prev = list->tail;
    }
    else
    {
        pnew->previous = list->tail;
        list->tail->next = pnew;
        list->tail = pnew;
    }

    //return pnew;
}

void scope_list_pop(struct scope_list* list)
{


    if (list->head == NULL)
        return;

    struct scope* p = list->tail;
    if (list->head == list->tail)
    {
        list->head = list->tail = NULL;
    }
    else
    {

        list->tail = list->tail->previous;
        if (list->tail == list->head)
        {
            list->tail->next = NULL;
            list->tail->previous = NULL;
        }
    }
    p->next = NULL;
    p->previous = NULL;
    return;

}


void print_item(struct osstream* ss, bool* first, const char* item);



void parser_seterror_with_token(struct parser_ctx* ctx, struct token* p_token, const char* fmt, ...)
{
    ctx->n_errors++;
    int line = 0;
    if (p_token)
    {
        if (p_token->pFile)
        {
            line = p_token->line;
            ctx->printf(WHITE "%s:%d:%d: ",
                p_token->pFile->lexeme,
                p_token->line,
                p_token->col);
        }
    }
    else
    {
        ctx->printf(WHITE "<>");
    }

    char buffer[200] = { 0 };
    va_list args;
    va_start(args, fmt);
    /*int n =*/ vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    ctx->printf(LIGHTRED "error: " WHITE "%s\n", buffer);




    ctx->printf(LIGHTGRAY);

    char nbuffer[20] = { 0 };
    int n = snprintf(nbuffer, sizeof nbuffer, "%d", line);
    ctx->printf(" %s |", nbuffer);

    struct token* prev = p_token;
    while (prev && prev->prev && (prev->prev->type != TK_NEWLINE && prev->prev->type != TK_BEGIN_OF_FILE))
    {
        prev = prev->prev;
    }
    struct token* next = prev;
    while (next && (next->type != TK_NEWLINE && next->type != TK_BEGIN_OF_FILE))
    {
        if (next->flags & TK_FLAG_MACRO_EXPANDED)
        {
            /*
            tokens expandidos da macro nao tem espaco entre
            vamos adicionar para ver melhor
            */
            if (next->flags & TK_FLAG_HAS_SPACE_BEFORE)
            {
                ctx->printf(" ");
            }
        }
        ctx->printf("%s", next->lexeme);
        next = next->next;
    }
    ctx->printf("\n");
    ctx->printf(LIGHTGRAY);
    ctx->printf(" %*s |", n, " ");
    if (p_token)
    {
        for (int i = 1; i < (p_token->col - 1); i++)
        {
            ctx->printf(" ");
        }
    }
    ctx->printf(LIGHTGREEN "^\n");
}


void parser_setwarning_with_token(struct parser_ctx* ctx, struct token* p_token, const char* fmt, ...)
{
    ctx->n_warnings++;
    int line = 0;
    if (p_token)
    {
        if (p_token->pFile)
        {
            line = p_token->line;
            ctx->printf(WHITE "%s:%d:%d: ",
                p_token->pFile->lexeme,
                p_token->line,
                p_token->col);
        }
    }
    else
    {
        ctx->printf(WHITE "<>");
    }

    char buffer[200] = { 0 };
    va_list args;
    va_start(args, fmt);
    /*int n =*/ vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    ctx->printf(LIGHTMAGENTA "warning: " WHITE "%s\n", buffer);




    ctx->printf(LIGHTGRAY);

    char nbuffer[20] = { 0 };
    int n = snprintf(nbuffer, sizeof nbuffer, "%d", line);
    ctx->printf(" %s |", nbuffer);

    struct token* prev = p_token;
    while (prev && prev->prev && (prev->prev->type != TK_NEWLINE && prev->prev->type != TK_BEGIN_OF_FILE))
    {
        prev = prev->prev;
    }
    struct token* next = prev;
    while (next && (next->type != TK_NEWLINE && next->type != TK_BEGIN_OF_FILE))
    {
        if (next->flags & TK_FLAG_MACRO_EXPANDED)
        {
            /*
            tokens expandidos da macro nao tem espaco entre
            vamos adicionar para ver melhor
            */
            if (next->flags & TK_FLAG_HAS_SPACE_BEFORE)
            {
                ctx->printf(" ");
            }
        }
        ctx->printf("%s", next->lexeme);
        next = next->next;
    }
    ctx->printf("\n");
    ctx->printf(LIGHTGRAY);
    ctx->printf(" %*s |", n, " ");
    if (p_token)
    {
        for (int i = 1; i < (p_token->col - 1); i++)
        {
            ctx->printf(" ");
        }
    }
    ctx->printf(LIGHTGREEN "^\n");
}


void parser_set_info_with_token(struct parser_ctx* ctx, struct token* p_token, const char* fmt, ...)
{
    ctx->n_warnings++;
    int line = 0;
    if (p_token)
    {
        if (p_token->pFile)
        {
            line = p_token->line;
            ctx->printf(WHITE "%s:%d:%d: ",
                p_token->pFile->lexeme,
                p_token->line,
                p_token->col);
        }
    }
    else
    {
        ctx->printf(WHITE "<>");
    }

    char buffer[200] = { 0 };
    va_list args;
    va_start(args, fmt);
    /*int n =*/ vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    ctx->printf(LIGHTCYAN "info: " WHITE "%s\n", buffer);




    ctx->printf(LIGHTGRAY);

    char nbuffer[20] = { 0 };
    int n = snprintf(nbuffer, sizeof nbuffer, "%d", line);
    ctx->printf(" %s |", nbuffer);

    struct token* prev = p_token;
    while (prev && prev->prev && (prev->prev->type != TK_NEWLINE && prev->prev->type != TK_BEGIN_OF_FILE))
    {
        prev = prev->prev;
    }
    struct token* next = prev;
    while (next && (next->type != TK_NEWLINE && next->type != TK_BEGIN_OF_FILE))
    {
        if (next->flags & TK_FLAG_MACRO_EXPANDED)
        {
            /*
            tokens expandidos da macro nao tem espaco entre
            vamos adicionar para ver melhor
            */
            if (next->flags & TK_FLAG_HAS_SPACE_BEFORE)
            {
                ctx->printf(" ");
            }
        }
        else
        {
            ctx->printf("%s", next->lexeme);
        }

        next = next->next;
    }
    ctx->printf("\n");
    ctx->printf(LIGHTGRAY);
    ctx->printf(" %*s |", n, " ");
    if (p_token)
    {
        for (int i = 1; i < (p_token->col - 1); i++)
        {
            ctx->printf(" ");
        }
    }
    ctx->printf(LIGHTGREEN "^\n");
}


void print_scope(struct scope_list* e)
{
    printf("--- begin of scope---\n");
    struct scope* p = e->head;
    int level = 0;
    while (p)
    {
        for (int i = 0; i < p->variables.capacity; i++)
        {
            if (p->variables.table[i])
            {
                for (int k = 0; k < level; k++)
                    printf(" ");
                printf("%s\n", p->variables.table[i]->key);
            }
        }

        for (int i = 0; i < p->tags.capacity; i++)
        {
            if (p->tags.table[i])
            {
                for (int k = 0; k < level; k++)
                    printf(" ");
                printf("tag %s\n", p->tags.table[i]->key);
            }
        }

        level++;
        p = p->next;
    }
    printf("--- end of scope---\n");
}


bool first_of_function_specifier_token(struct token* token)
{
    if (token == NULL)
        return false;

    return token->type == TK_KEYWORD_INLINE || token->type == TK_KEYWORD__NORETURN;
}

bool first_is(struct parser_ctx* ctx, enum token_type type)
{
    return ctx->current != NULL && ctx->current->type == type;
}

bool first_of_function_specifier(struct parser_ctx* ctx)
{
    return first_of_function_specifier_token(ctx->current);
}


bool first_of_enum_specifier_token(struct token* token)
{
    if (token == NULL)
        return false;
    return token->type == TK_KEYWORD_ENUM;
}

bool first_of_enum_specifier(struct parser_ctx* ctx)
{
    return first_of_enum_specifier_token(ctx->current);
}

bool first_of_alignment_specifier(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;
    return ctx->current->type == TK_KEYWORD__ALIGNAS;
}

bool first_of_atomic_type_specifier_token(struct token* token)
{
    if (token == NULL)
        return false;

    return token->type == TK_KEYWORD__ATOMIC;
}

bool first_of_atomic_type_specifier(struct parser_ctx* ctx)
{
    return first_of_atomic_type_specifier_token(ctx->current);
}

bool first_of_storage_class_specifier(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return ctx->current->type == TK_KEYWORD_TYPEDEF ||
        ctx->current->type == TK_KEYWORD_CONSTEXPR ||
        ctx->current->type == TK_KEYWORD_EXTERN ||
        ctx->current->type == TK_KEYWORD_STATIC ||
        ctx->current->type == TK_KEYWORD__THREAD_LOCAL ||
        ctx->current->type == TK_KEYWORD_AUTO ||
        ctx->current->type == TK_KEYWORD_REGISTER;
}

bool  first_of_struct_or_union_token(struct token* token)
{
    return token->type == TK_KEYWORD_STRUCT || token->type == TK_KEYWORD_UNION;
}

bool  first_of_struct_or_union(struct parser_ctx* ctx)
{
    return first_of_struct_or_union_token(ctx->current);
}


bool first_of_type_qualifier_token(struct token* p_token)
{
    if (p_token == NULL)
        return false;

    return p_token->type == TK_KEYWORD_CONST ||
        p_token->type == TK_KEYWORD_RESTRICT ||
        p_token->type == TK_KEYWORD_VOLATILE ||
        p_token->type == TK_KEYWORD__ATOMIC;
    //__fastcall
    //__stdcall
}

bool first_of_type_qualifier(struct parser_ctx* ctx)
{
    return first_of_type_qualifier_token(ctx->current);
}


//declaração da macro container_of
#ifndef container_of
#define container_of(ptr , type , member) (type *)( (char *) ptr - offsetof(type , member) )
#endif


struct type_tag_id* find_tag(struct parser_ctx* ctx, const char* lexeme)
{
    struct scope* scope = ctx->scopes.tail;
    while (scope)
    {
        struct type_tag_id* type_id = hashmap_find(&scope->tags, lexeme);
        if (type_id)
        {
            return type_id;
        }
        scope = scope->previous;
    }
    return NULL;
}




struct type_tag_id* find_variables(struct parser_ctx* ctx, const char* lexeme, struct scope** ppscope_opt)
{
    if (ppscope_opt != NULL)
        *ppscope_opt = NULL; //out

    struct scope* scope = ctx->scopes.tail;
    while (scope)
    {
        struct type_tag_id* type_id = hashmap_find(&scope->variables, lexeme);
        if (type_id)
        {
            if (ppscope_opt)
                *ppscope_opt = scope;
            return type_id;
        }
        scope = scope->previous;
    }
    return NULL;
}



struct enum_specifier* find_enum_specifier(struct parser_ctx* ctx, const char* lexeme)
{

    struct enum_specifier* best = NULL;
    struct scope* scope = ctx->scopes.tail;
    while (scope)
    {
        struct type_tag_id* type_id = hashmap_find(&scope->tags, lexeme);
        if (type_id &&
            type_id->type == TAG_TYPE_ENUN_SPECIFIER)
        {

            best = container_of(type_id, struct enum_specifier, type_id);
            if (best->enumerator_list.head != NULL)
                return best; //OK bem completo
            else
            {
                //nao eh completo vamos continuar subindo
            }

        }
        scope = scope->previous;
    }
    return best; //mesmo que nao seja tao completo vamos retornar.    
}

struct struct_or_union_specifier* find_struct_or_union_specifier(struct parser_ctx* ctx, const char* lexeme)
{

    struct struct_or_union_specifier* best = NULL;
    struct scope* scope = ctx->scopes.tail;
    while (scope)
    {
        struct type_tag_id* type_id = hashmap_find(&scope->tags, lexeme);
        if (type_id &&
            type_id->type == TAG_TYPE_STRUCT_OR_UNION_SPECIFIER)
        {

            best = container_of(type_id, struct struct_or_union_specifier, type_id);
            if (best->member_declaration_list.head != NULL)
                return best; //OK bem completo
            else
            {
                //nao eh completo vamos continuar subindo
            }

        }
        scope = scope->previous;
    }
    return best; //mesmo que nao seja tao completo vamos retornar.    
}


struct declarator* find_declarator(struct parser_ctx* ctx, const char* lexeme, struct scope** ppscope_opt)
{
    struct type_tag_id* type_id = find_variables(ctx, lexeme, ppscope_opt);

    if (type_id && type_id->type == TAG_TYPE_DECLARATOR)
        return container_of(type_id, struct declarator, type_id);

    return NULL;
}

struct enumerator* find_enumerator(struct parser_ctx* ctx, const char* lexeme, struct scope** ppscope_opt)
{
    struct type_tag_id* type_id = find_variables(ctx, lexeme, ppscope_opt);

    if (type_id && type_id->type == TAG_TYPE_ENUMERATOR)
        return container_of(type_id, struct enumerator, type_id);

    return NULL;
}

bool first_of_typedef_name(struct parser_ctx* ctx, struct token* p_token)
{
    if (p_token == NULL)
        return false;

    if (p_token->type != TK_IDENTIFIER)
    {
        //nao precisa verificar
        return false;
    }
    if (p_token->flags & TK_FLAG_IDENTIFIER_IS_TYPEDEF)
    {
        //ja foi verificado que eh typedef
        return true;
    }
    if (p_token->flags & TK_FLAG_IDENTIFIER_IS_NOT_TYPEDEF)
    {
        //ja foi verificado que NAO eh typedef
        return false;
    }


    struct declarator* pdeclarator = find_declarator(ctx, p_token->lexeme, NULL);

    //pdeclarator->declaration_specifiers->
    if (pdeclarator &&
        pdeclarator->declaration_specifiers &&
        (pdeclarator->declaration_specifiers->storage_class_specifier_flags & STORAGE_SPECIFIER_TYPEDEF))
    {
        p_token->flags |= TK_FLAG_IDENTIFIER_IS_TYPEDEF;
        return true;
    }
    else
    {
        p_token->flags |= TK_FLAG_IDENTIFIER_IS_NOT_TYPEDEF;
    }
    return false;
}

bool first_of_type_specifier(struct parser_ctx* ctx);
bool first_of_type_specifier_token(struct parser_ctx* ctx, struct token* token);


bool first_of_type_name_ahead(struct parser_ctx* ctx)
{

    if (ctx->current == NULL)
        return false;

    if (ctx->current->type != '(')
        return false;
    struct token* pAhead = parser_look_ahead(ctx);
    return first_of_type_specifier_token(ctx, pAhead) ||
        first_of_type_qualifier_token(pAhead);
}

bool first_of_type_name(struct parser_ctx* ctx)
{
    return first_of_type_specifier(ctx) || first_of_type_qualifier(ctx);
}

bool first_of_pointer(struct parser_ctx* ctx)
{
    return first_is(ctx, '*');
}



bool first_of_type_specifier_token(struct parser_ctx* ctx, struct token* p_token)
{
    if (p_token == NULL)
        return false;

    //if (ctx->)
    return p_token->type == TK_KEYWORD_VOID ||
        p_token->type == TK_KEYWORD_CHAR ||
        p_token->type == TK_KEYWORD_SHORT ||
        p_token->type == TK_KEYWORD_INT ||
        p_token->type == TK_KEYWORD_LONG ||

        //microsoft extension
        p_token->type == TK_KEYWORD__INT8 ||
        p_token->type == TK_KEYWORD__INT16 ||
        p_token->type == TK_KEYWORD__INT32 ||
        p_token->type == TK_KEYWORD__INT64 ||
        //end microsoft

        p_token->type == TK_KEYWORD_FLOAT ||
        p_token->type == TK_KEYWORD_DOUBLE ||
        p_token->type == TK_KEYWORD_SIGNED ||
        p_token->type == TK_KEYWORD_UNSIGNED ||
        p_token->type == TK_KEYWORD__BOOL ||
        p_token->type == TK_KEYWORD__COMPLEX ||
        p_token->type == TK_KEYWORD__DECIMAL32 ||
        p_token->type == TK_KEYWORD__DECIMAL64 ||
        p_token->type == TK_KEYWORD__DECIMAL128 ||
        p_token->type == TK_KEYWORD_TYPEOF || //C23
        p_token->type == TK_KEYWORD_TYPEOF_UNQUAL || //C23
        first_of_atomic_type_specifier_token(p_token) ||
        first_of_struct_or_union_token(p_token) ||
        first_of_enum_specifier_token(p_token) ||
        first_of_typedef_name(ctx, p_token);
}

bool first_of_type_specifier(struct parser_ctx* ctx)
{
    return first_of_type_specifier_token(ctx, ctx->current);
}

bool first_of_type_specifier_qualifier(struct parser_ctx* ctx)
{
    return first_of_type_specifier(ctx) ||
        first_of_type_qualifier(ctx) ||
        first_of_alignment_specifier(ctx);
}

bool first_of_compound_statement(struct parser_ctx* ctx)
{
    return first_is(ctx, '{');
}

bool first_of_jump_statement(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return ctx->current->type == TK_KEYWORD_GOTO ||
        ctx->current->type == TK_KEYWORD_CONTINUE ||
        ctx->current->type == TK_KEYWORD_BREAK ||
        ctx->current->type == TK_KEYWORD_RETURN ||
        ctx->current->type == TK_KEYWORD_THROW /*extension*/;
}

bool first_of_selection_statement(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return ctx->current->type == TK_KEYWORD_IF ||
        ctx->current->type == TK_KEYWORD_SWITCH ||
        ctx->current->type == TK_KEYWORD_TRY;
}

bool first_of_iteration_statement(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return
        ctx->current->type == TK_KEYWORD_REPEAT || /*extension*/
        ctx->current->type == TK_KEYWORD_WHILE ||
        ctx->current->type == TK_KEYWORD_DO ||
        ctx->current->type == TK_KEYWORD_FOR;
}


bool first_of_label(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    if (ctx->current->type == TK_IDENTIFIER)
    {
        struct token* next = parser_look_ahead(ctx);
        return next && next->type == ':';
    }
    else if (ctx->current->type == TK_KEYWORD_CASE)
    {
        return true;
    }
    else if (ctx->current->type == TK_KEYWORD_DEFAULT)
    {
        return true;
    }

    return false;
}

bool first_of_declaration_specifier(struct parser_ctx* ctx)
{
    /*
    declaration-specifier:
    storage-class-specifier
    type-specifier-qualifier
    function-specifier
    */
    return first_of_storage_class_specifier(ctx) ||
        first_of_function_specifier(ctx) ||
        first_of_type_specifier_qualifier(ctx);
}



bool first_of_static_assert_declaration(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return ctx->current->type == TK_KEYWORD__STATIC_ASSERT;
}

bool first_of_attribute_specifier(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    if (ctx->current->type != '[')
    {
        return false;
    }
    struct token* p_token = parser_look_ahead(ctx);
    return p_token != NULL && p_token->type == '[';
}

bool first_of_labeled_statement(struct parser_ctx* ctx)
{
    return first_of_label(ctx);
}

bool first_of_designator(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return ctx->current->type == '[' || ctx->current->type == '.';
}

struct token* previous_parser_token(struct token* token)
{
    struct token* r = token->prev;
    while (!(r->flags & TK_FLAG_FINAL))
    {
        r = r->prev;
    }
    return r;
}



enum token_type is_keyword(const char* text)
{
    enum token_type result = 0;
    switch (text[0])
    {
    case 'a':
        if (strcmp("alignof", text) == 0) result = TK_KEYWORD__ALIGNOF;
        else if (strcmp("auto", text) == 0) result = TK_KEYWORD_AUTO;
        else if (strcmp("alignas", text) == 0) result = TK_KEYWORD__ALIGNAS; /*C23 alternate spelling _Alignas*/
        else if (strcmp("alignof", text) == 0) result = TK_KEYWORD__ALIGNAS; /*C23 alternate spelling _Alignof*/
        break;
    case 'b':
        if (strcmp("break", text) == 0) result = TK_KEYWORD_BREAK;
        else if (strcmp("bool", text) == 0) result = TK_KEYWORD__BOOL; /*C23 alternate spelling _Bool*/

        break;
    case 'c':
        if (strcmp("case", text) == 0) result = TK_KEYWORD_CASE;
        else if (strcmp("char", text) == 0) result = TK_KEYWORD_CHAR;
        else if (strcmp("const", text) == 0) result = TK_KEYWORD_CONST;
        else if (strcmp("constexpr", text) == 0) result = TK_KEYWORD_CONSTEXPR;
        else if (strcmp("continue", text) == 0) result = TK_KEYWORD_CONTINUE;
        else if (strcmp("catch", text) == 0) result = TK_KEYWORD_CATCH;
        break;
    case 'd':
        if (strcmp("default", text) == 0) result = TK_KEYWORD_DEFAULT;
        else if (strcmp("do", text) == 0) result = TK_KEYWORD_DO;
        else if (strcmp("defer", text) == 0) result = TK_KEYWORD_DEFER;
        else if (strcmp("double", text) == 0) result = TK_KEYWORD_DOUBLE;
        break;
    case 'e':
        if (strcmp("else", text) == 0) result = TK_KEYWORD_ELSE;
        else if (strcmp("enum", text) == 0) result = TK_KEYWORD_ENUM;
        else if (strcmp("extern", text) == 0) result = TK_KEYWORD_EXTERN;
        break;
    case 'f':
        if (strcmp("float", text) == 0) result = TK_KEYWORD_FLOAT;
        else if (strcmp("for", text) == 0) result = TK_KEYWORD_FOR;
        else if (strcmp("false", text) == 0) result = TK_KEYWORD_FALSE;
        break;
    case 'g':
        if (strcmp("goto", text) == 0) result = TK_KEYWORD_GOTO;
        break;
    case 'i':
        if (strcmp("if", text) == 0) result = TK_KEYWORD_IF;
        else if (strcmp("inline", text) == 0) result = TK_KEYWORD_INLINE;
        else if (strcmp("int", text) == 0) result = TK_KEYWORD_INT;
        break;
    case 'N':
        /*extension NULL alias for nullptr*/
        if (strcmp("NULL", text) == 0) result = TK_KEYWORD_NULLPTR;
        break;
    case 'n':
        if (strcmp("nullptr", text) == 0) result = TK_KEYWORD_NULLPTR;
        break;
    case 'l':
        if (strcmp("long", text) == 0) result = TK_KEYWORD_LONG;
        break;
    case 'r':
        if (strcmp("register", text) == 0) result = TK_KEYWORD_REGISTER;
        else if (strcmp("restrict", text) == 0) result = TK_KEYWORD_RESTRICT;
        else if (strcmp("return", text) == 0) result = TK_KEYWORD_RETURN;
        else if (strcmp("repeat", text) == 0) result = TK_KEYWORD_REPEAT;
        break;
    case 's':
        if (strcmp("short", text) == 0) result = TK_KEYWORD_SHORT;
        else if (strcmp("signed", text) == 0) result = TK_KEYWORD_SIGNED;
        else if (strcmp("sizeof", text) == 0) result = TK_KEYWORD_SIZEOF;
        else if (strcmp("static", text) == 0) result = TK_KEYWORD_STATIC;
        else if (strcmp("struct", text) == 0) result = TK_KEYWORD_STRUCT;
        else if (strcmp("switch", text) == 0) result = TK_KEYWORD_SWITCH;
        else if (strcmp("static_assert", text) == 0) result = TK_KEYWORD__STATIC_ASSERT; /*C23 alternate spelling _Static_assert*/

        break;
    case 't':
        if (strcmp("typedef", text) == 0) result = TK_KEYWORD_TYPEDEF;
        else if (strcmp("typeof", text) == 0) result = TK_KEYWORD_TYPEOF; /*C23*/
        else if (strcmp("typeof_unqual", text) == 0) result = TK_KEYWORD_TYPEOF_UNQUAL; /*C23*/
        else if (strcmp("typeid", text) == 0) result = TK_KEYWORD_TYPEID; /*EXTENSION*/
        else if (strcmp("true", text) == 0) result = TK_KEYWORD_TRUE; /*C23*/
        else if (strcmp("thread_local", text) == 0) result = TK_KEYWORD__THREAD_LOCAL; /*C23 alternate spelling _Thread_local*/
        else if (strcmp("try", text) == 0) result = TK_KEYWORD_TRY;
        else if (strcmp("throw", text) == 0) result = TK_KEYWORD_THROW;
        break;
    case 'u':
        if (strcmp("union", text) == 0) result = TK_KEYWORD_UNION;
        else if (strcmp("unsigned", text) == 0) result = TK_KEYWORD_UNSIGNED;
        break;
    case 'v':
        if (strcmp("void", text) == 0) result = TK_KEYWORD_VOID;
        else if (strcmp("volatile", text) == 0) result = TK_KEYWORD_VOLATILE;
        break;
    case 'w':
        if (strcmp("while", text) == 0) result = TK_KEYWORD_WHILE;
        break;
    case '_':

        //begin microsoft
        if (strcmp("__int8", text) == 0) result = TK_KEYWORD__INT8;
        else if (strcmp("__int16", text) == 0) result = TK_KEYWORD__INT16;
        else if (strcmp("__int32", text) == 0) result = TK_KEYWORD__INT32;
        else if (strcmp("__int64", text) == 0) result = TK_KEYWORD__INT64;
        else if (strcmp("__forceinline", text) == 0) result = TK_KEYWORD_INLINE;
        else if (strcmp("__inline", text) == 0) result = TK_KEYWORD_INLINE;
        else if (strcmp("_asm", text) == 0 || strcmp("__asm", text) == 0) result = TK_KEYWORD__ASM;
        else if (strcmp("__alignof", text) == 0) result = TK_KEYWORD__ALIGNOF;
        //
        //end microsoft
        else if (strcmp("_Hashof", text) == 0) result = TK_KEYWORD_HASHOF;
        else if (strcmp("_Alignas", text) == 0) result = TK_KEYWORD__ALIGNAS;
        else if (strcmp("_Atomic", text) == 0) result = TK_KEYWORD__ATOMIC;
        else if (strcmp("_Bool", text) == 0) result = TK_KEYWORD__BOOL;
        else if (strcmp("_Complex", text) == 0) result = TK_KEYWORD__COMPLEX;
        else if (strcmp("_Decimal128", text) == 0) result = TK_KEYWORD__DECIMAL32;
        else if (strcmp("_Decimal64", text) == 0) result = TK_KEYWORD__DECIMAL64;
        else if (strcmp("_Decimal128", text) == 0) result = TK_KEYWORD__DECIMAL128;
        else if (strcmp("_Generic", text) == 0) result = TK_KEYWORD__GENERIC;
        else if (strcmp("_Imaginary", text) == 0) result = TK_KEYWORD__IMAGINARY;
        else if (strcmp("_Noreturn", text) == 0) result = TK_KEYWORD__NORETURN; /*_Noreturn deprecated C23*/
        else if (strcmp("_Static_assert", text) == 0) result = TK_KEYWORD__STATIC_ASSERT;
        else if (strcmp("_Thread_local", text) == 0) result = TK_KEYWORD__THREAD_LOCAL;
        else if (strcmp("_BitInt", text) == 0) result = TK_KEYWORD__BITINT; /*(C23)*/
        break;
    default:
        break;
    }
    return result;
}


static void token_promote(struct token* token, struct error* error)
{
    if (token->type == TK_IDENTIFIER_RECURSIVE_MACRO)
    {
        //talvez desse para remover antesisso..
        //assim que sai do tetris
        //virou passado
        token->type = TK_IDENTIFIER; /*nao precisamos mais disso*/
    }

    if (token->type == TK_IDENTIFIER)
    {
        enum token_type t = is_keyword(token->lexeme);
        if (t != TK_NONE)
            token->type = t;
    }
    else if (token->type == TK_PPNUMBER)
    {
        token->type = parse_number(token->lexeme, NULL, error);
    }
}

struct token* parser_look_ahead(struct parser_ctx* ctx)
{
    struct token* p = ctx->current->next;
    while (p && !(p->flags & TK_FLAG_FINAL))
    {
        p = p->next;
    }

    if (p)
    {
        struct error error = { 0 };
        token_promote(p, &error);
    }
    return p;
}

bool is_binary_digit(struct stream* stream)
{
    return stream->current[0] >= '0' && stream->current[0] <= '1';
}

bool is_hexadecimal_digit(struct stream* stream)
{
    return (stream->current[0] >= '0' && stream->current[0] <= '9') ||
        (stream->current[0] >= 'a' && stream->current[0] <= 'f') ||
        (stream->current[0] >= 'A' && stream->current[0] <= 'F');
}

bool is_octal_digit(struct stream* stream)
{
    return stream->current[0] >= '0' && stream->current[0] <= '7';
}

void digit_sequence(struct stream* stream)
{
    while (is_digit(stream))
    {
        stream_match(stream);
    }
}

void hexadecimal_digit_sequence(struct stream* stream)
{
    /*
     hexadecimal-digit-sequence:
     hexadecimal-digit
     hexadecimal-digit ’opt hexadecimal-digit
    */

    stream_match(stream);
    while (stream->current[0] == '\'' ||
        is_hexadecimal_digit(stream))
    {
        if (stream->current[0] == '\'')
        {
            stream_match(stream);
            if (!is_hexadecimal_digit(stream))
            {
                //erro
            }
            stream_match(stream);
        }
        else
            stream_match(stream);
    }

}

bool first_of_unsigned_suffix(struct stream* stream)
{
    /*
     unsigned-suffix: one of
       u U
     */
    return (stream->current[0] == 'u' ||
        stream->current[0] == 'U');
}

void unsigned_suffix_opt(struct stream* stream)
{
    /*
   unsigned-suffix: one of
     u U
   */
    if (stream->current[0] == 'u' ||
        stream->current[0] == 'U')
    {
        stream_match(stream);
    }
}

void integer_suffix_opt(struct stream* stream, enum type_specifier_flags* flags_opt)
{
    /*
     integer-suffix:
     unsigned-suffix long-suffixopt
     unsigned-suffix long-long-suffix
     long-suffix unsigned-suffixopt
     long-long-suffix unsigned-suffixopt
    */

    if (/*unsigned-suffix*/
        stream->current[0] == 'U' || stream->current[0] == 'u')
    {
        stream_match(stream);

        if (flags_opt)
        {
            *flags_opt |= TYPE_SPECIFIER_UNSIGNED;
        }

        /*long-suffixopt*/
        if (stream->current[0] == 'l' || stream->current[0] == 'L')
        {
            if (flags_opt)
            {
                *flags_opt = *flags_opt & ~TYPE_SPECIFIER_INT;
                *flags_opt |= TYPE_SPECIFIER_LONG;
            }
            stream_match(stream);
        }

        /*long-long-suffix*/
        if (stream->current[0] == 'l' || stream->current[0] == 'L')
        {
            if (flags_opt)
            {
                *flags_opt = *flags_opt & ~TYPE_SPECIFIER_LONG;
                *flags_opt |= TYPE_SPECIFIER_LONG_LONG;
            }

            stream_match(stream);
        }
    }
    else if ((stream->current[0] == 'l' || stream->current[0] == 'L'))
    {
        if (flags_opt)
        {
            *flags_opt = *flags_opt & ~TYPE_SPECIFIER_INT;
            *flags_opt |= TYPE_SPECIFIER_LONG;
        }

        /*long-suffix*/
        stream_match(stream);

        /*long-long-suffix*/
        if ((stream->current[0] == 'l' || stream->current[0] == 'L'))
        {
            if (flags_opt)
            {
                *flags_opt = *flags_opt & ~TYPE_SPECIFIER_LONG;
                *flags_opt |= TYPE_SPECIFIER_LONG_LONG;
            }
            stream_match(stream);
        }

        if (/*unsigned-suffix*/
            stream->current[0] == 'U' || stream->current[0] == 'u')
        {
            if (flags_opt)
            {
                *flags_opt |= TYPE_SPECIFIER_UNSIGNED;
            }
            stream_match(stream);
        }
    }
}

void exponent_part_opt(struct stream* stream)
{
    /*
    exponent-part:
    e signopt digit-sequence
    E signopt digit-sequence
    */
    if (stream->current[0] == 'e' || stream->current[0] == 'E')
    {
        stream_match(stream);

        if (stream->current[0] == '-' || stream->current[0] == '+')
        {
            stream_match(stream);
        }
        digit_sequence(stream);
    }
}

enum type_specifier_flags floating_suffix_opt(struct stream* stream)
{
    enum type_specifier_flags f = TYPE_SPECIFIER_DOUBLE;


    if (stream->current[0] == 'l' || stream->current[0] == 'L')
    {
        f = TYPE_SPECIFIER_LONG | TYPE_SPECIFIER_DOUBLE;
        stream_match(stream);
    }
    else if (stream->current[0] == 'f' || stream->current[0] == 'F')
    {
        f = TYPE_SPECIFIER_FLOAT;
        stream_match(stream);
    }

    return f;
}

bool is_nonzero_digit(struct stream* stream)
{
    return stream->current[0] >= '1' && stream->current[0] <= '9';
}



enum token_type parse_number_core(struct stream* stream, enum type_specifier_flags* flags_opt, struct error* error)
{
    if (flags_opt)
    {
        *flags_opt = TYPE_SPECIFIER_INT;
    }


    enum token_type type = TK_NONE;
    if (stream->current[0] == '.')
    {


        type = TK_COMPILER_DECIMAL_FLOATING_CONSTANT;
        stream_match(stream);
        digit_sequence(stream);
        exponent_part_opt(stream);
        enum type_specifier_flags f = floating_suffix_opt(stream);
        if (flags_opt)
        {
            *flags_opt = f;
        }
    }
    else if (stream->current[0] == '0' && (stream->current[1] == 'x' || stream->current[1] == 'X'))
    {
        type = TK_COMPILER_HEXADECIMAL_CONSTANT;

        stream_match(stream);
        stream_match(stream);
        while (is_hexadecimal_digit(stream))
        {
            stream_match(stream);
        }
        integer_suffix_opt(stream, flags_opt);

        if (stream->current[0] == '.')
        {
            type = TK_COMPILER_HEXADECIMAL_FLOATING_CONSTANT;
            hexadecimal_digit_sequence(stream);
            enum type_specifier_flags f = floating_suffix_opt(stream);
            if (flags_opt)
            {
                *flags_opt = f;
            }
        }
    }
    else if (stream->current[0] == '0' && (stream->current[1] == 'b' || stream->current[1] == 'B'))
    {
        type = TK_COMPILER_BINARY_CONSTANT;
        stream_match(stream);
        stream_match(stream);
        while (is_binary_digit(stream))
        {
            stream_match(stream);
        }
        integer_suffix_opt(stream, flags_opt);
    }
    else if (stream->current[0] == '0') //octal
    {
        type = TK_COMPILER_OCTAL_CONSTANT;

        stream_match(stream);
        while (is_octal_digit(stream))
        {
            stream_match(stream);
        }
        integer_suffix_opt(stream, flags_opt);

        if (stream->current[0] == '.')
        {
            hexadecimal_digit_sequence(stream);
            enum type_specifier_flags f = floating_suffix_opt(stream);
            if (flags_opt)
            {
                *flags_opt = f;
            }
        }
    }
    else if (is_nonzero_digit(stream)) //decimal
    {
        type = TK_COMPILER_DECIMAL_CONSTANT;

        stream_match(stream);
        while (is_digit(stream))
        {
            stream_match(stream);
        }
        integer_suffix_opt(stream, flags_opt);

        if (stream->current[0] == 'e' || stream->current[0] == 'E')
        {
            exponent_part_opt(stream);
            enum type_specifier_flags f = floating_suffix_opt(stream);
            if (flags_opt)
            {
                *flags_opt = f;
            }
        }
        else if (stream->current[0] == '.')
        {
            stream_match(stream);
            type = TK_COMPILER_DECIMAL_FLOATING_CONSTANT;
            digit_sequence(stream);
            exponent_part_opt(stream);
            enum type_specifier_flags f = floating_suffix_opt(stream);
            if (flags_opt)
            {
                *flags_opt = f;
            }
        }
    }




    return type;
}


enum token_type parse_number(const char* lexeme, enum type_specifier_flags* flags_opt, struct error* error)
{
    struct stream stream = { .source = lexeme, .current = lexeme, .line = 1, .col = 1 };
    return parse_number_core(&stream, flags_opt, error);
}

struct token* parser_skip_blanks(struct parser_ctx* ctx)
{
    while (ctx->current && !(ctx->current->flags & TK_FLAG_FINAL))
    {
        ctx->current = ctx->current->next;
    }

    if (ctx->current)
    {
        struct error error = { 0 };
        token_promote(ctx->current, &error); //transforma para token de parser
    }

    return ctx->current;
}


struct token* parser_match(struct parser_ctx* ctx)
{
    ctx->previous = ctx->current;
    ctx->current = ctx->current->next;
    parser_skip_blanks(ctx);

    return ctx->current;
}



void parser_match_tk(struct parser_ctx* ctx, enum token_type type, struct error* error)
{
    if (error->code != 0)
        return;
    if (ctx->current == NULL)
    {
        parser_seterror_with_token(ctx, ctx->input_list.tail, "unexpected end of file after");
        error->code = 1;
        return;
    }

    if (ctx->current->type != type)
    {
        parser_seterror_with_token(ctx, ctx->current, "expected %s", get_token_name(type));
        return;
    }
    ctx->current = ctx->current->next;
    parser_skip_blanks(ctx);
    return;
}

void print_declaration_specifiers(struct osstream* ss, struct declaration_specifiers* p_declaration_specifiers)
{
    bool first = true;
    print_type_qualifier_flags(ss, &first, p_declaration_specifiers->type_qualifier_flags);

    if (p_declaration_specifiers->enum_specifier)
    {

        if (p_declaration_specifiers->enum_specifier->tag_token)
            ss_fprintf(ss, "enum %s", p_declaration_specifiers->enum_specifier->tag_token->lexeme);
        else
            assert(false);
    }
    else if (p_declaration_specifiers->struct_or_union_specifier)
    {
        //
        if (p_declaration_specifiers->struct_or_union_specifier->tagName)
            ss_fprintf(ss, "struct %s", p_declaration_specifiers->struct_or_union_specifier->tagName);
        else
            assert(false);
    }
    else if (p_declaration_specifiers->typedef_declarator)
    {
        print_item(ss, &first, p_declaration_specifiers->typedef_declarator->name->lexeme);
    }
    else
    {
        print_type_specifier_flags(ss, &first, p_declaration_specifiers->type_specifier_flags);
    }
}

bool type_specifier_is_integer(enum type_specifier_flags flags)
{
    if ((flags & TYPE_SPECIFIER_CHAR) ||
        (flags & TYPE_SPECIFIER_SHORT) ||
        (flags & TYPE_SPECIFIER_INT) ||
        (flags & TYPE_SPECIFIER_LONG) ||
        (flags & TYPE_SPECIFIER_INT) ||
        (flags & TYPE_SPECIFIER_INT8) ||
        (flags & TYPE_SPECIFIER_INT16) ||
        (flags & TYPE_SPECIFIER_INT32) ||
        (flags & TYPE_SPECIFIER_INT64) ||
        (flags & TYPE_SPECIFIER_LONG_LONG))
    {
        return true;
    }
    return false;
}
int final_specifier(struct parser_ctx* ctx,
    enum type_specifier_flags* flags,
    struct error* error)
{
    ctx;
    if (((*flags) & TYPE_SPECIFIER_UNSIGNED) ||
        ((*flags) & TYPE_SPECIFIER_SIGNED))
    {
        if (!type_specifier_is_integer(*flags))
        {
            //se nao especificou nada vira integer
            (*flags) |= TYPE_SPECIFIER_INT;
        }
    }


    return error->code;
}

int add_specifier(struct parser_ctx* ctx,
    enum type_specifier_flags* flags,
    enum type_specifier_flags newFlag,
    struct error* error)
{
    /*
    * Verifica as combinaçòes possíveis
    */

    if (newFlag & TYPE_SPECIFIER_LONG) //adicionando um long
    {
        if ((*flags) & TYPE_SPECIFIER_LONG_LONG) //ja tinha long long
        {
            parser_seterror_with_token(ctx, ctx->current, "cannot combine with previous 'long long' declaration specifier");
            error->code = 1;

            return 1;
        }
        else if ((*flags) & TYPE_SPECIFIER_LONG) //ja tinha um long
        {
            (*flags) = (*flags) & ~TYPE_SPECIFIER_LONG;
            (*flags) |= TYPE_SPECIFIER_LONG_LONG;
        }
        else //nao tinha nenhum long
        {
            (*flags) = (*flags) & ~TYPE_SPECIFIER_INT;
            (*flags) |= TYPE_SPECIFIER_LONG;
        }
    }
    else
    {
        (*flags) |= newFlag;
    }
    return error->code;
}

struct declaration_specifiers* declaration_specifiers(struct parser_ctx* ctx, struct error* error)
{
    /*
        declaration-specifiers:
          declaration-specifier attribute-specifier-sequence_opt
          declaration-specifier declaration-specifiers
    */

    /*
     Ao fazer parser do segundo o X ja existe mas ele nao deve ser usado
     typedef char X;
     typedef char X;
    */

    struct declaration_specifiers* p_declaration_specifiers = calloc(1, sizeof(struct declaration_specifiers));

    try
    {
        while (error->code == 0 &&
            first_of_declaration_specifier(ctx))
        {
            if (ctx->current->flags & TK_FLAG_IDENTIFIER_IS_TYPEDEF)
            {
                if (p_declaration_specifiers->type_specifier_flags != TYPE_SPECIFIER_NONE)
                {
                    //typedef tem que aparecer sozinho
                    //exemplo Socket eh nome e nao typdef
                    //typedef int Socket;
                    //struct X {int Socket;}; 
                    break;
                }
            }

            struct declaration_specifier* p_declaration_specifier = declaration_specifier(ctx, error);

            if (p_declaration_specifier->type_specifier_qualifier)
            {
                if (p_declaration_specifier->type_specifier_qualifier)
                {
                    if (p_declaration_specifier->type_specifier_qualifier->pType_specifier)
                    {

                        if (add_specifier(ctx,
                            &p_declaration_specifiers->type_specifier_flags,
                            p_declaration_specifier->type_specifier_qualifier->pType_specifier->flags,
                            error) != 0)
                        {
                            throw;
                        }


                        if (p_declaration_specifier->type_specifier_qualifier->pType_specifier->struct_or_union_specifier)
                        {
                            p_declaration_specifiers->struct_or_union_specifier = p_declaration_specifier->type_specifier_qualifier->pType_specifier->struct_or_union_specifier;
                        }
                        else if (p_declaration_specifier->type_specifier_qualifier->pType_specifier->enum_specifier)
                        {
                            p_declaration_specifiers->enum_specifier = p_declaration_specifier->type_specifier_qualifier->pType_specifier->enum_specifier;
                        }
                        else if (p_declaration_specifier->type_specifier_qualifier->pType_specifier->typeof_specifier)
                        {
                            p_declaration_specifiers->typeof_specifier = p_declaration_specifier->type_specifier_qualifier->pType_specifier->typeof_specifier;
                        }
                        else if (p_declaration_specifier->type_specifier_qualifier->pType_specifier->token &&
                            p_declaration_specifier->type_specifier_qualifier->pType_specifier->token->type == TK_IDENTIFIER)
                        {
                            p_declaration_specifiers->typedef_declarator =
                                find_declarator(ctx,
                                    p_declaration_specifier->type_specifier_qualifier->pType_specifier->token->lexeme,
                                    NULL);

                            //p_declaration_specifiers->typedef_declarator = p_declaration_specifier->type_specifier_qualifier->pType_specifier->token->lexeme;
                        }
                    }
                    else if (p_declaration_specifier->type_specifier_qualifier->pType_qualifier)
                    {
                        p_declaration_specifiers->type_qualifier_flags |= p_declaration_specifier->type_specifier_qualifier->pType_qualifier->flags;

                    }
                }
            }
            else if (p_declaration_specifier->storage_class_specifier)
            {
                p_declaration_specifiers->storage_class_specifier_flags |= p_declaration_specifier->storage_class_specifier->flags;
            }

            list_add(p_declaration_specifiers, p_declaration_specifier);
            attribute_specifier_sequence_opt(ctx, error);

            if (ctx->current->type == TK_IDENTIFIER &&
                p_declaration_specifiers->type_specifier_flags != TK_NONE)
            {
                //typedef nao pode aparecer com outro especifier
                //entao ja tem tem algo e vier identifier signfica que acabou 
                //exemplo
                /*
                 typedef char X;
                 typedef char X;
                */
                break;
            }
        }
    }
    catch
    {
    }

    final_specifier(ctx, &p_declaration_specifiers->type_specifier_flags, error);

    return p_declaration_specifiers;
}

struct declaration* declaration_core(struct parser_ctx* ctx, bool canBeFunctionDefinition, bool* is_function_definition, struct error* error)
{
    /*
                                  declaration-specifiers init-declarator-list_opt ;
     attribute-specifier-sequence declaration-specifiers init-declarator-list ;
     static_assert-declaration
     attribute-declaration
 */

    struct declaration* p_declaration = calloc(1, sizeof(struct declaration));

    p_declaration->first_token = ctx->current;

    if (ctx->current->type == ';')
    {
        parser_match_tk(ctx, ';', error);
        //declaracao vazia nao eh erro
        return p_declaration;
    }

    if (first_of_static_assert_declaration(ctx))
    {
        p_declaration->static_assert_declaration = static_assert_declaration(ctx, error);
    }
    else
    {
        attribute_specifier_sequence_opt(ctx, error); //se tem aqui initi nao eh opcional!TODO
        if (first_of_declaration_specifier(ctx))
        {

            p_declaration->declaration_specifiers = declaration_specifiers(ctx, error);
            if (ctx->current->type != ';')
                p_declaration->init_declarator_list = init_declarator_list(ctx, p_declaration->declaration_specifiers, error);


            p_declaration->last_token = ctx->current;

            if (first_is(ctx, '{'))
            {
                if (canBeFunctionDefinition)
                    *is_function_definition = true;
                else
                {
                    assert(false);
                    error->code = 1;
                }
            }
            else
                parser_match_tk(ctx, ';', error);
        }
        else
        {
            parser_seterror_with_token(ctx, ctx->current, "unknown type name '%s'", ctx->current->lexeme);
            error->code = 1;
        }
    }


    return p_declaration;
}

struct declaration* function_definition_or_declaration(struct parser_ctx* ctx, struct error* error)
{

    bool is_function_definition = false;
    struct declaration* p_declaration = declaration_core(ctx, true, &is_function_definition, error);
    if (is_function_definition)
    {
        naming_convention_function(ctx, p_declaration->init_declarator_list.head->declarator->direct_declarator->name);

        ctx->p_current_function_opt = p_declaration;
        //tem que ter 1 so
        //tem 1 que ter  1 cara e ser funcao
        assert(p_declaration->init_declarator_list.head->declarator->direct_declarator->array_function_list.head->function_declarator);

        struct scope* parameters_scope =
            &p_declaration->init_declarator_list.head->declarator->direct_declarator->array_function_list.head->function_declarator->parameters_scope;


        scope_list_push(&ctx->scopes, parameters_scope);


        //o function_prototype_scope era um block_scope
        p_declaration->function_body = function_body(ctx, error);



        struct parameter_declaration* parameter = NULL;

        if (p_declaration->init_declarator_list.head->declarator->direct_declarator->array_function_list.head->function_declarator &&
            p_declaration->init_declarator_list.head->declarator->direct_declarator->array_function_list.head->function_declarator->parameter_type_list_opt &&
            p_declaration->init_declarator_list.head->declarator->direct_declarator->array_function_list.head->function_declarator->parameter_type_list_opt->parameter_list)
        {
            parameter = p_declaration->init_declarator_list.head->declarator->direct_declarator->array_function_list.head->function_declarator->parameter_type_list_opt->parameter_list->head;
        }

        /*parametros nao usados*/
        while (parameter)
        {
            if (parameter->declarator->nUses == 0)
            {
                if (parameter->name &&
                    parameter->name->level == 0 /*direct source*/
                    )
                {

                    ctx->printf(WHITE "%s:%d:%d: ",
                        parameter->name->pFile->lexeme,
                        parameter->name->line,
                        parameter->name->col);

                    ctx->printf(LIGHTMAGENTA "warning: " WHITE "'%s': unreferenced formal parameter\n",
                        parameter->name->lexeme);
                }
            }
            parameter = parameter->next;
        }


        scope_list_pop(&ctx->scopes);
        ctx->p_current_function_opt = NULL;
    }
    else
    {
        struct init_declarator* p = p_declaration->init_declarator_list.head;
        while (p)
        {
            if (p->declarator && p->declarator->name)
            {
                naming_convention_global_var(ctx, p->declarator->name, &p->declarator->type);
            }
            p = p->next;
        }
    }

    return p_declaration;
}

struct declaration* declaration(struct parser_ctx* ctx, struct error* error)
{
    bool is_function_definition;
    return declaration_core(ctx, false, &is_function_definition, error);
}


//(6.7) declaration-specifiers:
//declaration-specifier attribute-specifier-sequenceopt
//declaration-specifier declaration-specifiers



struct declaration_specifier* declaration_specifier(struct parser_ctx* ctx, struct error* error)
{
    //    storage-class-specifier
    //    type-specifier-qualifier
    //    function-specifier
    struct declaration_specifier* pDeclaration_specifier = calloc(1, sizeof * pDeclaration_specifier);
    if (first_of_storage_class_specifier(ctx))
    {
        pDeclaration_specifier->storage_class_specifier = storage_class_specifier(ctx);
    }
    else if (first_of_type_specifier_qualifier(ctx))
    {
        pDeclaration_specifier->type_specifier_qualifier = type_specifier_qualifier(ctx, error);
    }
    else if (first_of_function_specifier(ctx))
    {
        pDeclaration_specifier->function_specifier = function_specifier(ctx, error);
    }
    else
    {
        parser_seterror_with_token(ctx, ctx->current, "unexpected");
    }
    return pDeclaration_specifier;
}


struct init_declarator* init_declarator(struct parser_ctx* ctx,
    struct declaration_specifiers* p_declaration_specifiers,
    struct error* error)
{
    /*
     init-declarator:
       declarator
       declarator = initializer
    */
    struct init_declarator* p_init_declarator = calloc(1, sizeof(struct init_declarator));
    try
    {

        struct token* tkname = 0;
        p_init_declarator->declarator = declarator(ctx,
            NULL,
            p_declaration_specifiers,
            false,
            &tkname,
            error);
        if (tkname == NULL)
        {
            parser_seterror_with_token(ctx, ctx->current, "empty declarator name?? unexpected");
            error->code = 1;
            return p_init_declarator;
        }

        p_init_declarator->declarator->declaration_specifiers = p_declaration_specifiers;
        p_init_declarator->declarator->name = tkname;

        p_init_declarator->declarator->type =
            make_type_using_declarator(ctx, p_init_declarator->declarator);

        if (error->code != 0) throw;
        const char* name = p_init_declarator->declarator->name->lexeme;
        if (name)
        {
            //TODO se ja existe?
            hashmap_set(&ctx->scopes.tail->variables, name, &p_init_declarator->declarator->type_id);
        }
        else
        {
            assert(false);
        }
        if (ctx->current && ctx->current->type == '=')
        {
            parser_match(ctx);
            p_init_declarator->initializer = initializer(ctx, error);
        }
    }
    catch
    {
    }
    return p_init_declarator;
}

struct init_declarator_list init_declarator_list(struct parser_ctx* ctx,
    struct declaration_specifiers* p_declaration_specifiers,
    struct error* error)
{
    /*
    init-declarator-list:
      init-declarator
      init-declarator-list , init-declarator
    */
    struct init_declarator_list init_declarator_list = { 0 };
    list_add(&init_declarator_list, init_declarator(ctx, p_declaration_specifiers, error));
    while (error->code == 0 &&
        ctx->current != NULL && ctx->current->type == ',')
    {
        parser_match(ctx);
        list_add(&init_declarator_list, init_declarator(ctx, p_declaration_specifiers, error));
        if (error->code) break;
    }
    return init_declarator_list;
}

struct storage_class_specifier* storage_class_specifier(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return NULL;

    struct storage_class_specifier* new_storage_class_specifier = calloc(1, sizeof(struct storage_class_specifier));
    if (new_storage_class_specifier == NULL)
        return NULL;

    new_storage_class_specifier->token = ctx->current;
    switch (ctx->current->type)
    {
    case TK_KEYWORD_TYPEDEF:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_TYPEDEF;
        break;
    case TK_KEYWORD_EXTERN:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_EXTERN;
        break;
    case TK_KEYWORD_CONSTEXPR:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_CONSTEXPR;
        break;
    case TK_KEYWORD_STATIC:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_STATIC;
        break;
    case TK_KEYWORD__THREAD_LOCAL:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_THREAD_LOCAL;
        break;
    case TK_KEYWORD_AUTO:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_AUTO;
        break;
    case TK_KEYWORD_REGISTER:
        new_storage_class_specifier->flags = STORAGE_SPECIFIER_REGISTER;
        break;
    default:
        assert(false);
    }

    /*
     TODO
     thread_local may appear with static or extern,
     auto may appear with all the others except typedef138), and
     constexpr may appear with auto, register, or static.
    */

    parser_match(ctx);
    return new_storage_class_specifier;
}

struct typeof_specifier_argument* typeof_specifier_argument(struct parser_ctx* ctx, struct error* error)
{
    struct typeof_specifier_argument* new_typeof_specifier_argument = calloc(1, sizeof(struct typeof_specifier_argument));
    if (new_typeof_specifier_argument)
        return NULL;

    if (first_of_type_name(ctx))
    {
        new_typeof_specifier_argument->type_name = type_name(ctx, error);
    }
    else
    {
        struct expression_ctx ectx = { 0 };
        new_typeof_specifier_argument->expression = expression(ctx, error, &ectx);
    }

    return new_typeof_specifier_argument;
}

bool first_of_typeof_specifier(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;

    return ctx->current->type == TK_KEYWORD_TYPEOF ||
        ctx->current->type == TK_KEYWORD_TYPEOF_UNQUAL;
}

struct typeof_specifier* typeof_specifier(struct parser_ctx* ctx, struct error* error)
{
    struct typeof_specifier* p_typeof_specifier = calloc(1, sizeof(struct typeof_specifier));

    p_typeof_specifier->token = ctx->current;
    parser_match(ctx);
    parser_match_tk(ctx, '(', error);

    p_typeof_specifier->typeof_specifier_argument =
        typeof_specifier_argument(ctx, error);

    p_typeof_specifier->endtoken = ctx->current;
    parser_match_tk(ctx, ')', error);

    return p_typeof_specifier;
}

struct type_specifier* type_specifier(struct parser_ctx* ctx, struct error* error)
{
    /*
     type-specifier:
       void
       char
       short
       int
       long
       float
       double
       signed
       unsigned
       _BitInt ( constant-expression )
       bool                                  C23
       _Complex
       _Decimal32
       _Decimal64
       _Decimal128
       atomic-type-specifier
       struct-or-union-specifier
       enum-specifier
       typedef-name
       typeof-specifier                      C23
    */

    struct type_specifier* pType_specifier = calloc(1, sizeof * pType_specifier);




    //typeof (expression)
    switch (ctx->current->type)
    {
    case TK_KEYWORD_VOID:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_VOID;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_CHAR:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_CHAR;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_SHORT:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_SHORT;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_INT:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_INT;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

        //microsoft
    case TK_KEYWORD__INT8:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_INT8;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD__INT16:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_INT16;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;
    case TK_KEYWORD__INT32:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_INT32;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;
    case TK_KEYWORD__INT64:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_INT64;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;
        //end microsoft

    case TK_KEYWORD_LONG:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_LONG;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_FLOAT:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_FLOAT;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_DOUBLE:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_DOUBLE;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_SIGNED:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_SIGNED;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD_UNSIGNED:

        pType_specifier->flags = TYPE_SPECIFIER_UNSIGNED;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD__BOOL:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_BOOL;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD__COMPLEX:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_COMPLEX;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD__DECIMAL32:
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_DECIMAL32;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD__DECIMAL64:

        pType_specifier->flags = TYPE_SPECIFIER_DECIMAL64;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;

    case TK_KEYWORD__DECIMAL128:
        pType_specifier->flags = TYPE_SPECIFIER_DECIMAL128;
        pType_specifier->token = ctx->current;
        parser_match(ctx);
        return pType_specifier;


    }

    if (first_of_typeof_specifier(ctx))
    {
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_TYPEOF;
        pType_specifier->typeof_specifier = typeof_specifier(ctx, error);
    }
    else if (first_of_atomic_type_specifier(ctx))
    {
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_ATOMIC;
        pType_specifier->atomic_type_specifier = atomic_type_specifier(ctx, error);
    }
    else if (first_of_struct_or_union(ctx))
    {
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_STRUCT_OR_UNION;
        pType_specifier->struct_or_union_specifier = struct_or_union_specifier(ctx, error);
    }
    else if (first_of_enum_specifier(ctx))
    {
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_ENUM;
        pType_specifier->enum_specifier = enum_specifier(ctx, error);
    }
    else if (ctx->current->type == TK_IDENTIFIER)
    {
        pType_specifier->token = ctx->current;
        pType_specifier->flags = TYPE_SPECIFIER_TYPEDEF;

        pType_specifier->typedef_declarator =
            find_declarator(ctx, ctx->current->lexeme, NULL);

        //Ser chegou aqui já tem que exitir (reaprovecitar?)
        assert(pType_specifier->typedef_declarator != NULL);

        parser_match(ctx);
    }
    return pType_specifier;
}

struct struct_or_union_specifier* struct_or_union_specifier(struct parser_ctx* ctx, struct error* error)
{
    struct struct_or_union_specifier* pStruct_or_union_specifier = calloc(1, sizeof * pStruct_or_union_specifier);
    pStruct_or_union_specifier->type_id.type = TAG_TYPE_STRUCT_OR_UNION_SPECIFIER;

    if (ctx->current->type == TK_KEYWORD_STRUCT ||
        ctx->current->type == TK_KEYWORD_UNION)
    {
        pStruct_or_union_specifier->first = ctx->current;
        parser_match(ctx);
    }
    else
    {
        assert(false);
    }

    pStruct_or_union_specifier->attribute_specifier_sequence =
        attribute_specifier_sequence_opt(ctx, error);

    struct struct_or_union_specifier* pPreviousTagInThisScope = NULL;

    if (ctx->current->type == TK_IDENTIFIER)
    {
        pStruct_or_union_specifier->tagtoken = ctx->current;
        /*
         Structure, union, and enumeration tags have scope that begins just after the
         appearance of the tag in a type specifier that declares the tag.
        */

        snprintf(pStruct_or_union_specifier->tagName, sizeof pStruct_or_union_specifier->tagName, "%s", ctx->current->lexeme);

        struct type_tag_id* tag_type_id = hashmap_find(&ctx->scopes.tail->tags, ctx->current->lexeme);
        if (tag_type_id)
        {
            /*este tag já existe neste escopo*/
            if (tag_type_id->type == TAG_TYPE_STRUCT_OR_UNION_SPECIFIER)
            {
                pPreviousTagInThisScope = container_of(tag_type_id, struct struct_or_union_specifier, type_id);
                pStruct_or_union_specifier->complete_struct_or_union_specifier = pPreviousTagInThisScope;
            }
            else
            {
                parser_seterror_with_token(ctx, ctx->current, "use of '%s' with tag type that does not match previous declaration.", ctx->current->lexeme);
            }
        }
        else
        {
            /*ok neste escopo nao tinha este tag..vamos escopos para cima*/
            struct struct_or_union_specifier* pOther = find_struct_or_union_specifier(ctx, ctx->current->lexeme);
            if (pOther == NULL)
            {
                pStruct_or_union_specifier->scope_level = ctx->scopes.tail->scope_level;
                /*nenhum escopo tinha este tag vamos adicionar no escopo local*/
                hashmap_set(&ctx->scopes.tail->tags, ctx->current->lexeme, &pStruct_or_union_specifier->type_id);
            }
            else
            {
                /*achou a tag em um escopo mais a cima*/
                pStruct_or_union_specifier->complete_struct_or_union_specifier = pOther;
            }
        }

        parser_match(ctx);
    }
    else
    {
        /*struct sem tag, neste caso vou inventar um tag "oculto" e adicionar no escopo atual*/
        snprintf(pStruct_or_union_specifier->tagName, sizeof pStruct_or_union_specifier->tagName, "_anonymous_struct_%d", anonymous_struct_count);
        anonymous_struct_count++;
        pStruct_or_union_specifier->bAnonymousTag = true;
        pStruct_or_union_specifier->scope_level = ctx->scopes.tail->scope_level;
        hashmap_set(&ctx->scopes.tail->tags, pStruct_or_union_specifier->tagName, &pStruct_or_union_specifier->type_id);
    }



    if (ctx->current->type == '{')
    {
        if (pStruct_or_union_specifier->tagtoken)
            naming_convention_struct_tag(ctx, pStruct_or_union_specifier->tagtoken);

        struct token* first = ctx->current;
        parser_match(ctx);
        pStruct_or_union_specifier->member_declaration_list = member_declaration_list(ctx, error);
        pStruct_or_union_specifier->member_declaration_list.first = first;
        pStruct_or_union_specifier->last = ctx->current;
        pStruct_or_union_specifier->member_declaration_list.last = ctx->current;
        parser_match_tk(ctx, '}', error);

    }
    else
    {
        pStruct_or_union_specifier->last = ctx->current;
    }

    if (pPreviousTagInThisScope)
    {
        if (pPreviousTagInThisScope->member_declaration_list.head == NULL &&
            pStruct_or_union_specifier->member_declaration_list.head != NULL)
        {
            /*
              Temos uma versao mais completa deste tag neste escopo. Vamos ficar com ela.
            */
            hashmap_set(&ctx->scopes.tail->tags, pStruct_or_union_specifier->tagName, &pStruct_or_union_specifier->type_id);
        }
    }

    return pStruct_or_union_specifier;
}

struct member_declarator* member_declarator(struct parser_ctx* ctx,
    struct specifier_qualifier_list* p_specifier_qualifier_list,
    struct error* error)
{
    /*
    member-declarator:
     declarator
     declaratoropt : constant-expression
    */
    struct member_declarator* p_member_declarator = calloc(1, sizeof(struct member_declarator));
    //struct declarator* pdeclarator = calloc(1, sizeof * pdeclarator);
    p_member_declarator->declarator = declarator(ctx, p_specifier_qualifier_list, /*declaration_specifiers*/NULL, false, &p_member_declarator->name, error);
    p_member_declarator->declarator->specifier_qualifier_list = p_specifier_qualifier_list;

    p_member_declarator->declarator->type =
        make_type_using_declarator(ctx, p_member_declarator->declarator);

    if (p_member_declarator->name)
        naming_convention_struct_member(ctx, p_member_declarator->name, &p_member_declarator->declarator->type);

    if (ctx->current->type == ':')
    {
        parser_match(ctx);
        struct expression_ctx ectx = { 0 };
        p_member_declarator->constant_expression = constant_expression(ctx, error, &ectx);
    }
    return p_member_declarator;
}

struct member_declarator_list* member_declarator_list(struct parser_ctx* ctx,
    struct specifier_qualifier_list* p_specifier_qualifier_list,
    struct error* error)
{
    struct member_declarator_list* p_member_declarator_list = calloc(1, sizeof(struct member_declarator_list));
    list_add(p_member_declarator_list, member_declarator(ctx, p_specifier_qualifier_list, error));
    while (error->code == 0 &&
        ctx->current->type == ',')
    {
        parser_match(ctx);
        list_add(p_member_declarator_list, member_declarator(ctx, p_specifier_qualifier_list, error));
    }
    return p_member_declarator_list;
}


struct member_declaration_list member_declaration_list(struct parser_ctx* ctx, struct error* error)
{
    struct member_declaration_list list = { 0 };
    //member_declaration
    //member_declaration_list member_declaration


    list_add(&list, member_declaration(ctx, error));

    while (error->code == 0 &&
        ctx->current->type != '}')
    {
        list_add(&list, member_declaration(ctx, error));
    }
    return list;
}

struct member_declaration* member_declaration(struct parser_ctx* ctx, struct error* error)
{
    struct member_declaration* p_member_declaration = calloc(1, sizeof(struct member_declaration));
    //attribute_specifier_sequence_opt specifier_qualifier_list member_declarator_list_opt ';'
    //static_assert_declaration
    if (ctx->current->type == TK_KEYWORD__STATIC_ASSERT)
    {
        p_member_declaration->p_static_assert_declaration = static_assert_declaration(ctx, error);
    }
    else
    {
        attribute_specifier_sequence_opt(ctx, error);
        p_member_declaration->specifier_qualifier_list = specifier_qualifier_list(ctx, error);
        if (ctx->current->type != ';')
        {
            p_member_declaration->member_declarator_list_opt = member_declarator_list(ctx, p_member_declaration->specifier_qualifier_list, error);
        }
        parser_match_tk(ctx, ';', error);
    }
    return p_member_declaration;
}

struct member_declarator* find_member_declarator(struct member_declaration_list* list, const char* name)
{
    struct member_declaration* p_member_declaration = list->head;
    while (p_member_declaration)
    {
        struct member_declarator* p_member_declarator = NULL;

        if (p_member_declaration->member_declarator_list_opt)
        {
            p_member_declarator = p_member_declaration->member_declarator_list_opt->head;

            while (p_member_declarator)
            {
                if (strcmp(p_member_declarator->name->lexeme, name) == 0)
                {
                    return p_member_declarator;
                }
                p_member_declarator = p_member_declarator->next;
            }
        }
        else
        {
            /*
             struct X {
                union  {
                  unsigned char       Byte[16];
                  unsigned short      Word[8];
                  };
            };

            struct X* a;
            a.Byte[0] & 0xe0;
            */

            if (p_member_declaration->specifier_qualifier_list->struct_or_union_specifier)
            {
                struct member_declaration_list* list =
                    &p_member_declaration->specifier_qualifier_list->struct_or_union_specifier->member_declaration_list;
                struct member_declarator* p = find_member_declarator(list, name);
                if (p)
                {
                    return p;
                }
            }
        }

        p_member_declaration = p_member_declaration->next;
    }
    return NULL;
}

void print_item(struct osstream* ss, bool* first, const char* item)
{
    if (!(*first))
        ss_fprintf(ss, " ");
    ss_fprintf(ss, "%s", item);
    *first = false;

}

//todo trocar tudo p escrever em strings
bool print_type_specifier_flags(struct osstream* ss, bool* first, enum type_specifier_flags e_type_specifier_flags)
{


    if (e_type_specifier_flags & TYPE_SPECIFIER_VOID)
        print_item(ss, first, "void");


    if (e_type_specifier_flags & TYPE_SPECIFIER_UNSIGNED)
        print_item(ss, first, "unsigned");

    if (e_type_specifier_flags & TYPE_SPECIFIER_INT)
        print_item(ss, first, "int");

    if (e_type_specifier_flags & TYPE_SPECIFIER_SHORT)
        print_item(ss, first, "short");

    if (e_type_specifier_flags & TYPE_SPECIFIER_LONG)
        print_item(ss, first, "long");

    if (e_type_specifier_flags & TYPE_SPECIFIER_LONG_LONG)
        print_item(ss, first, "long long");


    if (e_type_specifier_flags & TYPE_SPECIFIER_CHAR)
        print_item(ss, first, "char");

    if (e_type_specifier_flags & TYPE_SPECIFIER_DOUBLE)
        print_item(ss, first, "double");

    if (e_type_specifier_flags & TYPE_SPECIFIER_FLOAT)
        print_item(ss, first, "float");

    if (e_type_specifier_flags & TYPE_SPECIFIER_SIGNED)
        print_item(ss, first, "signed");

    if (e_type_specifier_flags & TYPE_SPECIFIER_BOOL)
        print_item(ss, first, "_Bool");

    if (e_type_specifier_flags & TYPE_SPECIFIER_COMPLEX)
        print_item(ss, first, "_Complex");

    if (e_type_specifier_flags & TYPE_SPECIFIER_DECIMAL32)
        print_item(ss, first, "_Decimal32");

    if (e_type_specifier_flags & TYPE_SPECIFIER_DECIMAL64)
        print_item(ss, first, "_Decimal64");

    if (e_type_specifier_flags & TYPE_SPECIFIER_DECIMAL128)
        print_item(ss, first, "_Decimal128");
    //if (e_type_specifier_flags & type_specifier_Actomi)
      //  print_item(&first, "_Decimal128");
//        TYPE_SPECIFIER_ATOMIC = 1 << 14,

    return first;
}

void print_type_qualifier_flags(struct osstream* ss, bool* first, enum type_qualifier_flags e_type_qualifier_flags)
{

    if (e_type_qualifier_flags & TYPE_QUALIFIER_CONST)
        print_item(ss, first, "const");

    if (e_type_qualifier_flags & TYPE_QUALIFIER_RESTRICT)
        print_item(ss, first, "restrict");

    if (e_type_qualifier_flags & TYPE_QUALIFIER_VOLATILE)
        print_item(ss, first, "volatile");

}

void print_specifier_qualifier_list(struct osstream* ss, bool* first, struct specifier_qualifier_list* p_specifier_qualifier_list)
{

    print_type_qualifier_flags(ss, first, p_specifier_qualifier_list->type_qualifier_flags);

    if (p_specifier_qualifier_list->enum_specifier)
    {

        //TODO
        assert(false);

    }
    else if (p_specifier_qualifier_list->struct_or_union_specifier)
    {
        if (p_specifier_qualifier_list->struct_or_union_specifier->tagName)
            ss_fprintf(ss, "struct %s", p_specifier_qualifier_list->struct_or_union_specifier->tagName);
        else
        {
            assert(false);
        }
    }
    else if (p_specifier_qualifier_list->typedef_declarator)
    {
        print_item(ss, first, p_specifier_qualifier_list->typedef_declarator->name->lexeme);
    }
    else
    {
        print_type_specifier_flags(ss, first, p_specifier_qualifier_list->type_specifier_flags);
    }
}



struct specifier_qualifier_list* specifier_qualifier_list(struct parser_ctx* ctx, struct error* error)
{
    struct specifier_qualifier_list* p_specifier_qualifier_list = calloc(1, sizeof(struct specifier_qualifier_list));
    /*
      type_specifier_qualifier attribute_specifier_sequence_opt
      type_specifier_qualifier specifier_qualifier_list
    */
    try
    {
        while (error->code == 0 && ctx->current != NULL &&
            (first_of_type_specifier(ctx) ||
                first_of_type_qualifier(ctx)))
        {

            if (ctx->current->flags & TK_FLAG_IDENTIFIER_IS_TYPEDEF)
            {
                if (p_specifier_qualifier_list->type_specifier_flags != TYPE_SPECIFIER_NONE)
                {
                    //typedef tem que aparecer sozinho
                    //exemplo Socket eh nome e nao typdef
                    //typedef int Socket;
                    //struct X {int Socket;}; 
                    break;
                }
            }

            struct type_specifier_qualifier* p_type_specifier_qualifier = type_specifier_qualifier(ctx, error);

            if (p_type_specifier_qualifier->pType_specifier)
            {
                if (add_specifier(ctx,
                    &p_specifier_qualifier_list->type_specifier_flags,
                    p_type_specifier_qualifier->pType_specifier->flags,
                    error) != 0)
                {
                    throw;
                }

                if (p_type_specifier_qualifier->pType_specifier->struct_or_union_specifier)
                {
                    p_specifier_qualifier_list->struct_or_union_specifier = p_type_specifier_qualifier->pType_specifier->struct_or_union_specifier;
                }
                else if (p_type_specifier_qualifier->pType_specifier->enum_specifier)
                {
                    p_specifier_qualifier_list->enum_specifier = p_type_specifier_qualifier->pType_specifier->enum_specifier;
                }
                else if (p_type_specifier_qualifier->pType_specifier->typeof_specifier)
                {
                    p_specifier_qualifier_list->typeof_specifier = p_type_specifier_qualifier->pType_specifier->typeof_specifier;
                }
                else if (p_type_specifier_qualifier->pType_specifier->token->type == TK_IDENTIFIER)
                {
                    p_specifier_qualifier_list->typedef_declarator =
                        find_declarator(ctx,
                            p_type_specifier_qualifier->pType_specifier->token->lexeme,
                            NULL);
                }

            }
            else if (p_type_specifier_qualifier->pType_qualifier)
            {
                p_specifier_qualifier_list->type_qualifier_flags |= p_type_specifier_qualifier->pType_qualifier->flags;
            }

            list_add(p_specifier_qualifier_list, p_type_specifier_qualifier);
            attribute_specifier_sequence_opt(ctx, error);
        }
    }
    catch
    {
    }

    final_specifier(ctx, &p_specifier_qualifier_list->type_specifier_flags, error);
    return p_specifier_qualifier_list;
}

struct type_specifier_qualifier* type_specifier_qualifier(struct parser_ctx* ctx, struct error* error)
{
    struct type_specifier_qualifier* type_specifier_qualifier = calloc(1, sizeof * type_specifier_qualifier);
    //type_specifier
    //type_qualifier
    //alignment_specifier
    if (first_of_type_specifier(ctx))
    {
        type_specifier_qualifier->pType_specifier = type_specifier(ctx, error);
    }
    else if (first_of_type_qualifier(ctx))
    {
        type_specifier_qualifier->pType_qualifier = type_qualifier(ctx, error);
    }
    else if (first_of_alignment_specifier(ctx))
    {
        type_specifier_qualifier->pAlignment_specifier = alignment_specifier(ctx, error);
    }
    else
    {
        assert(false);
    }
    return type_specifier_qualifier;
}


struct enum_specifier* enum_specifier(struct parser_ctx* ctx, struct error* error)
{
    /*
        enum-specifier:

        enum attribute-specifier-sequenceopt identifieropt enum-type-specifieropt
        { enumerator-list }

        enum attribute-specifier-sequenceopt identifieropt enum-type-specifieropt
        { enumerator-list , }
        enum identifier enum-type-specifiero
    */
    struct enum_specifier* p_enum_specifier = NULL;
    try
    {
        p_enum_specifier = calloc(1, sizeof * p_enum_specifier);
        p_enum_specifier->type_id.type = TAG_TYPE_ENUN_SPECIFIER;

        parser_match_tk(ctx, TK_KEYWORD_ENUM, error);

        attribute_specifier_sequence_opt(ctx, error);
        struct enum_specifier* pPreviousTagInThisScope = NULL;
        bool bHasIdentifier = false;
        if (ctx->current->type == TK_IDENTIFIER)
        {
            bHasIdentifier = true;
            p_enum_specifier->tag_token = ctx->current;
            parser_match(ctx);
        }

        if (ctx->current->type == ':')
        {
            /*C23*/
            parser_match(ctx);
            p_enum_specifier->type_specifier_qualifier = type_specifier_qualifier(ctx, error);
        }

        if (ctx->current->type == '{')
        {
            if (p_enum_specifier->tag_token)
                naming_convention_enum_tag(ctx, p_enum_specifier->tag_token);

            /*TODO redeclaration?*/
            /*adicionar no escopo*/
            if (p_enum_specifier->tag_token)
            {
                hashmap_set(&ctx->scopes.tail->tags, p_enum_specifier->tag_token->lexeme, &p_enum_specifier->type_id);
            }

            /*self*/
            p_enum_specifier->complete_enum_specifier = p_enum_specifier;

            parser_match_tk(ctx, '{', error);
            p_enum_specifier->enumerator_list = enumerator_list(ctx, error);
            if (ctx->current->type == ',')
            {
                parser_match(ctx);
            }
            parser_match_tk(ctx, '}', error);
        }
        else
        {
            if (!bHasIdentifier)
            {
                parser_seterror_with_token(ctx, ctx->current, "missing enum tag name");
                error->code = 1;
                throw;
            }


            /*searches for this tag in the current scope*/
            struct type_tag_id* tag_type_id = hashmap_find(&ctx->scopes.tail->tags, p_enum_specifier->tag_token->lexeme);
            if (tag_type_id)
            {
                /*we have this tag at this scope*/
                if (tag_type_id->type == TAG_TYPE_ENUN_SPECIFIER)
                {
                    pPreviousTagInThisScope = container_of(tag_type_id, struct enum_specifier, type_id);
                    p_enum_specifier->complete_enum_specifier = pPreviousTagInThisScope;
                }
                else
                {
                    parser_seterror_with_token(ctx, ctx->current, "use of '%s' with tag type that does not match previous declaration.", ctx->current->lexeme);
                    throw;
                }
            }
            else
            {
                struct enum_specifier* pOther = find_enum_specifier(ctx, p_enum_specifier->tag_token->lexeme);
                /*ok neste escopo nao tinha este tag..vamos escopos para cima*/
                if (pOther == NULL)
                {
                    hashmap_set(&ctx->scopes.tail->tags, p_enum_specifier->tag_token->lexeme, &p_enum_specifier->type_id);
                }
                else
                {
                    /*achou a tag em um escopo mais a cima*/
                    p_enum_specifier->complete_enum_specifier = pOther;
                }
            }


        }
    }
    catch
    {}
    return p_enum_specifier;
}

struct enumerator_list enumerator_list(struct parser_ctx* ctx, struct error* error)
{
    struct enumerator_list enumeratorlist = { 0 };
    try
    {

        //enumerator
        //enumerator_list ',' enumerator
        list_add(&enumeratorlist, enumerator(ctx, error));
        if (error->code != 0) throw;

        while (ctx->current != NULL &&
            ctx->current->type == ',')
        {
            parser_match(ctx);  /*pode ter uma , vazia no fim*/

            if (ctx->current->type != '}')
                list_add(&enumeratorlist, enumerator(ctx, error));

            if (error->code != 0) throw;
        }
    }
    catch
    {
    }

    return enumeratorlist;
}

struct enumerator* enumerator(struct parser_ctx* ctx, struct error* error)
{
    //TODO VALUE
    struct enumerator* p_enumerator = calloc(1, sizeof(struct enumerator));
    p_enumerator->type_id.type = TAG_TYPE_ENUMERATOR;

    //enumeration_constant attribute_specifier_sequence_opt
    //enumeration_constant attribute_specifier_sequence_opt '=' constant_expression
    struct token* name = ctx->current;

    naming_convention_enumerator(ctx, name);


    parser_match_tk(ctx, TK_IDENTIFIER, error);
    attribute_specifier_sequence_opt(ctx, error);


    p_enumerator->token = name;
    hashmap_set(&ctx->scopes.tail->variables, p_enumerator->token->lexeme, &p_enumerator->type_id);

    if (ctx->current->type == '=')
    {
        parser_match(ctx);
        struct expression_ctx ectx = { .bConstantExpressionRequired = true };
        p_enumerator->constant_expression_opt = constant_expression(ctx, error, &ectx);
        p_enumerator->value = p_enumerator->constant_expression_opt->constant_value;
    }

    return p_enumerator;
}




struct alignment_specifier* alignment_specifier(struct parser_ctx* ctx, struct error* error)
{
    struct alignment_specifier* pAlignment_specifier = calloc(1, sizeof * pAlignment_specifier);
    pAlignment_specifier->token = ctx->current;
    parser_match_tk(ctx, TK_KEYWORD__ALIGNAS, error);
    parser_match_tk(ctx, '(', error);
    if (first_of_type_name(ctx))
    {

        type_name(ctx, error);

    }
    else
    {
        struct expression_ctx ectx = { .bConstantExpressionRequired = true };
        constant_expression(ctx, error, &ectx);
    }
    parser_match_tk(ctx, ')', error);
    return pAlignment_specifier;
}



struct atomic_type_specifier* atomic_type_specifier(struct parser_ctx* ctx, struct error* error)
{
    //'_Atomic' '(' type_name ')'
    struct atomic_type_specifier* p = calloc(1, sizeof * p);
    p->token = ctx->current;
    parser_match_tk(ctx, TK_KEYWORD__ATOMIC, error);
    parser_match_tk(ctx, '(', error);
    type_name(ctx, error);
    parser_match_tk(ctx, ')', error);
    return p;
}


struct type_qualifier* type_qualifier(struct parser_ctx* ctx, struct error* error)
{
    if (error->code) return NULL;
    struct type_qualifier* pType_qualifier = calloc(1, sizeof * pType_qualifier);
    switch (ctx->current->type)
    {
    case TK_KEYWORD_CONST:
        pType_qualifier->flags = TYPE_QUALIFIER_CONST;
        break;
    case TK_KEYWORD_RESTRICT:
        pType_qualifier->flags = TYPE_QUALIFIER_RESTRICT;
        break;
    case TK_KEYWORD_VOLATILE:
        pType_qualifier->flags = TYPE_QUALIFIER_VOLATILE;
        break;
    case TK_KEYWORD__ATOMIC:
        pType_qualifier->flags = TYPE_QUALIFIER__ATOMIC;
        break;
    }
    //'const'
    //'restrict'
    //'volatile'
    //'_Atomic'
    parser_match(ctx);
    return pType_qualifier;
}
//

struct type_qualifier* type_qualifier_opt(struct parser_ctx* ctx, struct error* error)
{
    if (first_of_type_qualifier(ctx))
    {
        return type_qualifier(ctx, error);
    }
    return NULL;
}


struct function_specifier* function_specifier(struct parser_ctx* ctx, struct error* error)
{
    if (error->code) return NULL;
    struct function_specifier* p = calloc(1, sizeof * p);
    p->token = ctx->current;
    parser_match(ctx);
    //'inline'
    //'_Noreturn'
    return p;
}


struct declarator* declarator(struct parser_ctx* ctx,
    struct specifier_qualifier_list* p_specifier_qualifier_list,
    struct declaration_specifiers* p_declaration_specifiers,
    bool bAbstractAcceptable,
    struct token** pptokenName,
    struct error* error)
{
    /*
      declarator:
      pointer_opt direct-declarator
    */
    struct declarator* p_declarator = calloc(1, sizeof(struct declarator));
    p_declarator->type_id.type = TAG_TYPE_DECLARATOR;
    p_declarator->pointer = pointer_opt(ctx, error);
    p_declarator->direct_declarator = direct_declarator(ctx, p_specifier_qualifier_list, p_declaration_specifiers, bAbstractAcceptable, pptokenName, error);


    return p_declarator;
}

const char* declarator_get_name(struct declarator* p_declarator)
{
    if (p_declarator->direct_declarator)
        return p_declarator->direct_declarator->name->lexeme;
    assert(false);
    return NULL;
}

bool declarator_is_function(struct declarator* p_declarator)
{
    return (p_declarator->direct_declarator &&
        p_declarator->direct_declarator->array_function_list.head &&
        p_declarator->direct_declarator->array_function_list.head->function_declarator != NULL);

}

struct direct_declarator* direct_declarator(struct parser_ctx* ctx,
    struct specifier_qualifier_list* p_specifier_qualifier_list,
    struct declaration_specifiers* p_declaration_specifiers,
    bool bAbstractAcceptable,
    struct token** pptokenName,
    struct error* error)
{
    /*
    direct-declarator:
     identifier attribute-specifier-sequenceopt
     ( declarator )

     array-declarator attribute-specifier-sequenceopt
     function-declarator attribute-specifier-sequenceopt
    */
    struct direct_declarator* p_direct_declarator = calloc(1, sizeof(struct direct_declarator));
    try
    {
        if (ctx->current == NULL)
        {
            return p_direct_declarator;
        }

        struct token* p_token_ahead = parser_look_ahead(ctx);
        if (ctx->current->type == TK_IDENTIFIER)
        {
            struct scope* pscope = NULL;
            struct declarator* pdeclarator =
                find_declarator(ctx, ctx->current->lexeme, &pscope);
            if (pdeclarator)
            {



                if (pscope == ctx->scopes.tail)
                {
                    if (pscope->scope_level != 0)
                    {
                        if (declarator_is_function(pdeclarator))
                        {
                            //redeclaracao de algo q era funcao agora nao eh nao eh problema
                        }
                        else
                        {
                            //TODO tem que ver se esta dentro struct dai nao pode dar erro
                            //parser_seterror_with_token(ctx, ctx->current, "redefinition of '%s'", ctx->current->lexeme);
                        }
                    }
                    else
                    {
                        //global aceita redefinicao!
                        //TODO ver seh eh o mesmo                        
                    }
                }
                else
                {
                    if (pscope->scope_level != 0)
                    {
                        if (pdeclarator->direct_declarator &&
                            pdeclarator->direct_declarator->name &&
                            pdeclarator->direct_declarator->name->pFile)
                        {
                            //TODO ver se esta dentro de struct
                            //printf("warning '%s' at line %d hides previous definition %d\n",
                              //  ctx->current->lexeme,
                                //ctx->current->line,                                
                                //pdeclarator->direct_declarator->name->line);
                        }
                    }

                    //parser_seterror_with_token(ctx, ctx->current, "redefinition of '%s'", ctx->current->lexeme);
                }
            }

            p_direct_declarator->name = ctx->current;
            if (pptokenName != NULL)
            {
                *pptokenName = ctx->current;
            }
            else
            {
                seterror(error, "unexpected name on declarator (abstract?)");
                throw;
            }

            parser_match(ctx);
            attribute_specifier_sequence_opt(ctx, error);
        }
        else if (ctx->current->type == '(')
        {
            struct token* ahead = parser_look_ahead(ctx);

            if (!first_of_type_specifier_token(ctx, p_token_ahead) &&
                !first_of_type_qualifier_token(p_token_ahead) &&
                ahead->type != ')' &&
                ahead->type != '...')
            {
                //look ahead para nao confundir (declarator) com parametros funcao ex void (int)
                //or function int ()

                parser_match(ctx);

                p_direct_declarator->declarator = declarator(ctx,
                    p_specifier_qualifier_list,
                    p_declaration_specifiers,
                    bAbstractAcceptable,
                    pptokenName,
                    error);


                parser_match(ctx); //)
            }
        }


        while (error->code == 0 && ctx->current != NULL &&
            (ctx->current->type == '[' || ctx->current->type == '('))
        {
            struct array_function* p_array_function = calloc(1, sizeof(struct array_function));
            list_add(&p_direct_declarator->array_function_list, p_array_function);
            if (ctx->current->type == '[')
            {
                p_array_function->array_declarator = array_declarator(ctx, error);
            }
            else if (ctx->current->type == '(')
            {

                p_array_function->function_declarator = function_declarator(ctx, error);
            }
        }
    }
    catch
    {
    }

    return p_direct_declarator;
}


struct array_declarator* array_declarator(struct parser_ctx* ctx, struct error* error)
{
    //direct_declarator '['          type_qualifier_list_opt           assignment_expression_opt ']'
    //direct_declarator '[' 'static' type_qualifier_list_opt           assignment_expression     ']'
    //direct_declarator '['          type_qualifier_list      'static' assignment_expression     ']'
    //direct_declarator '['          type_qualifier_list_opt  '*'           ']'

    struct array_declarator* p_array_declarator = NULL;
    try
    {
        if (error->code != 0)
            throw;

        p_array_declarator = calloc(1, sizeof * p_array_declarator);
        if (p_array_declarator == NULL)
            throw;

        parser_match_tk(ctx, '[', error);
        if (error->code != 0) throw;

        bool bIsStatic = false;
        if (ctx->current->type == TK_KEYWORD_STATIC)
        {
            parser_match(ctx);
            bIsStatic = true;
        }

        if (first_of_type_qualifier(ctx))
        {
            p_array_declarator->type_qualifier_list_opt = type_qualifier_list(ctx, error);
        }

        if (!bIsStatic)
        {
            if (ctx->current->type == TK_KEYWORD_STATIC)
            {
                parser_match(ctx);
                bIsStatic = true;
            }
        }

        if (bIsStatic)
        {
            //tem que ter..
            struct expression_ctx ectx = { 0 };
            p_array_declarator->assignment_expression = assignment_expression(ctx, error, &ectx);
            if (error->code != 0) throw;

            p_array_declarator->constant_size = p_array_declarator->assignment_expression->constant_value;
        }
        else
        {
            //opcional
            if (ctx->current->type == '*')
            {
                parser_match(ctx);
            }
            else if (ctx->current->type != ']')
            {
                struct expression_ctx ectx = { 0 };
                p_array_declarator->assignment_expression = assignment_expression(ctx, error, &ectx);
                if (error->code != 0) throw;

                p_array_declarator->constant_size = p_array_declarator->assignment_expression->constant_value;
            }

        }

        parser_match_tk(ctx, ']', error);
        if (error->code != 0) throw;
    }
    catch
    {
        if (p_array_declarator)
        {
        }
    }



    return p_array_declarator;
}


struct function_declarator* function_declarator(struct parser_ctx* ctx, struct error* error)
{
    struct function_declarator* p_function_declarator = calloc(1, sizeof(struct function_declarator));
    //faz um push da funcion_scope_declarator_list que esta vivendo mais em cima
    //eh feito o pop mais em cima tb.. aqui dentro do decide usar.
    //ctx->funcion_scope_declarator_list->outer_scope = ctx->current_scope;
    //ctx->current_scope = ctx->funcion_scope_declarator_list;
    //direct_declarator '(' parameter_type_list_opt ')'



    p_function_declarator->parameters_scope.scope_level = ctx->scopes.tail->scope_level + 1;

    scope_list_push(&ctx->scopes, &p_function_declarator->parameters_scope);

    //print_scope(&ctx->scopes);

    parser_match_tk(ctx, '(', error);
    if (error->code == 0 &&
        ctx->current->type != ')')
    {
        p_function_declarator->parameter_type_list_opt = parameter_type_list(ctx, error);
    }
    parser_match_tk(ctx, ')', error);

    //print_scope(&ctx->scopes);

    scope_list_pop(&ctx->scopes);

    //print_scope(&ctx->scopes);


    return p_function_declarator;
}


struct pointer* pointer_opt(struct parser_ctx* ctx, struct error* error)
{
    struct pointer* p = NULL;
    while (error->code == 0 && ctx->current != NULL &&
        ctx->current->type == '*')
    {
        struct pointer* p_pointer = calloc(1, sizeof(struct pointer));
        p = p_pointer;
        parser_match(ctx);
        attribute_specifier_sequence_opt(ctx, error);
        if (first_of_type_qualifier(ctx))
            p_pointer->type_qualifier_list_opt = type_qualifier_list(ctx, error);
        while (error->code == 0 && ctx->current != NULL &&
            ctx->current->type == '*')
        {
            p_pointer->pointer = pointer_opt(ctx, error);
        }
    }
    //'*' attribute_specifier_sequence_opt type_qualifier_list_opt
    //'*' attribute_specifier_sequence_opt type_qualifier_list_opt pointer
    return p;
}


struct type_qualifier_list* type_qualifier_list(struct parser_ctx* ctx, struct error* error)
{
    struct type_qualifier_list* p_type_qualifier_list = calloc(1, sizeof(struct type_qualifier_list));
    //type_qualifier
    //type_qualifier_list type_qualifier


    struct type_qualifier* p_type_qualifier = type_qualifier(ctx, error);
    p_type_qualifier_list->flags |= p_type_qualifier->flags;
    list_add(p_type_qualifier_list, p_type_qualifier);
    p_type_qualifier = NULL;

    while (error->code == 0 && ctx->current != NULL &&
        first_of_type_qualifier(ctx))
    {
        p_type_qualifier = type_qualifier(ctx, error);
        p_type_qualifier_list->flags |= p_type_qualifier->flags;
        list_add(p_type_qualifier_list, p_type_qualifier);
    }
    return p_type_qualifier_list;
}


struct parameter_type_list* parameter_type_list(struct parser_ctx* ctx, struct error* error)
{
    struct parameter_type_list* p_parameter_type_list = calloc(1, sizeof(struct parameter_type_list));
    //parameter_list
    //parameter_list ',' '...'
    p_parameter_type_list->parameter_list = parameter_list(ctx, error);
    /*ja esta saindo com a virgula consumida do parameter_list para evitar ahead*/
    if (ctx->current->type == '...')
    {
        parser_match(ctx);
        //parser_match_tk(ctx, '...', error);
        p_parameter_type_list->bVarArgs = true;
    }
    return p_parameter_type_list;
}


struct parameter_list* parameter_list(struct parser_ctx* ctx, struct error* error)
{
    /*
      parameter_list
      parameter_declaration
      parameter_list ',' parameter_declaration
    */
    struct parameter_list* p_parameter_list = calloc(1, sizeof(struct parameter_list));
    list_add(p_parameter_list, parameter_declaration(ctx, error));
    while (error->code == 0 && ctx->current != NULL &&
        ctx->current->type == ',')
    {
        parser_match(ctx);
        if (ctx->current->type == '...')
        {
            //follow
            break;
        }
        list_add(p_parameter_list, parameter_declaration(ctx, error));
        if (error->code) break;
    }
    return p_parameter_list;
}


struct parameter_declaration* parameter_declaration(struct parser_ctx* ctx, struct error* error)
{
    struct parameter_declaration* p_parameter_declaration = calloc(1, sizeof(struct parameter_declaration));
    //attribute_specifier_sequence_opt declaration_specifiers declarator
    //attribute_specifier_sequence_opt declaration_specifiers abstract_declarator_opt
    attribute_specifier_sequence_opt(ctx, error);
    p_parameter_declaration->declaration_specifiers = declaration_specifiers(ctx, error);

    //talvez no ctx colocar um flag que esta em argumentos
    //TODO se tiver uma struct tag novo...
    //warning: declaration of 'struct X' will not be visible outside of this function [-Wvisibility]

    p_parameter_declaration->declarator = declarator(ctx,
        /*specifier_qualifier_list*/NULL,
        p_parameter_declaration->declaration_specifiers,
        true/*can be abstract*/,
        &p_parameter_declaration->name,
        error);
    p_parameter_declaration->declarator->declaration_specifiers = p_parameter_declaration->declaration_specifiers;

    p_parameter_declaration->declarator->type =
        make_type_using_declarator(ctx, p_parameter_declaration->declarator);

    if (p_parameter_declaration->name)
        naming_convention_parameter(ctx, p_parameter_declaration->name, &p_parameter_declaration->declarator->type);

    //coloca o pametro no escpo atual que deve apontar para escopo paramtros
    // da funcao .
    // 
    //assert ctx->current_scope->variables parametrosd
    if (p_parameter_declaration->name)
    {
        //parametro void nao te name 
        hashmap_set(&ctx->scopes.tail->variables,
            p_parameter_declaration->name->lexeme,
            &p_parameter_declaration->declarator->type_id);
        //print_scope(ctx->current_scope);
    }
    return p_parameter_declaration;
}


struct specifier_qualifier_list* copy(struct declaration_specifiers* p_declaration_specifiers)
{
    struct specifier_qualifier_list* p_specifier_qualifier_list = calloc(1, sizeof(struct specifier_qualifier_list));

    p_specifier_qualifier_list->type_qualifier_flags = p_declaration_specifiers->type_qualifier_flags;
    p_specifier_qualifier_list->type_specifier_flags = p_declaration_specifiers->type_specifier_flags;

    struct declaration_specifier* p_declaration_specifier =
        p_declaration_specifiers->head;

    while (p_declaration_specifier)
    {
        if (p_declaration_specifier->type_specifier_qualifier)
        {
            struct specifier_qualifier* p_specifier_qualifier = calloc(1, sizeof(struct specifier_qualifier));

            if (p_declaration_specifier->type_specifier_qualifier->pType_qualifier)
            {
                struct type_qualifier* p_type_qualifier = calloc(1, sizeof(struct type_qualifier));

                p_type_qualifier->flags = p_declaration_specifier->type_specifier_qualifier->pType_qualifier->flags;


                p_type_qualifier->token = p_declaration_specifier->type_specifier_qualifier->pType_qualifier->token;
                p_specifier_qualifier->type_qualifier = p_type_qualifier;
            }
            else if (p_declaration_specifier->type_specifier_qualifier->pType_specifier)
            {
                struct type_specifier* p_type_specifier = calloc(1, sizeof(struct type_specifier));

                p_type_specifier->flags = p_declaration_specifier->type_specifier_qualifier->pType_specifier->flags;

                //todo
                assert(p_declaration_specifier->type_specifier_qualifier->pType_specifier->struct_or_union_specifier == NULL);

                p_type_specifier->token = p_declaration_specifier->type_specifier_qualifier->pType_specifier->token;
                p_specifier_qualifier->type_specifier = p_type_specifier;
            }

            list_add(p_specifier_qualifier_list, p_specifier_qualifier);
        }
        p_declaration_specifier = p_declaration_specifier->next;
    }
    return p_specifier_qualifier_list;

}


void print_declarator(struct osstream* ss, struct declarator* p_declarator, bool bAbstract);

void print_direct_declarator(struct osstream* ss, struct direct_declarator* p_direct_declarator, bool bAbstract)
{
    if (p_direct_declarator->declarator)
    {
        ss_fprintf(ss, "(");
        print_declarator(ss, p_direct_declarator->declarator, bAbstract);
        ss_fprintf(ss, ")");
    }

    if (p_direct_declarator->name && !bAbstract)
    {
        //Se bAbstract for true é pedido para nao imprimir o nome do indentificador
        ss_fprintf(ss, "%s", p_direct_declarator->name->lexeme);
    }

    struct array_function* p_array_function =
        p_direct_declarator->array_function_list.head;

    while (p_array_function)
    {
        if (p_array_function->function_declarator)
        {
            ss_fprintf(ss, "(");
            struct parameter_declaration* p_parameter_declaration =
                p_array_function->function_declarator->parameter_type_list_opt ?
                p_array_function->function_declarator->parameter_type_list_opt->parameter_list->head : NULL;

            while (p_parameter_declaration)
            {
                if (p_parameter_declaration != p_array_function->function_declarator->parameter_type_list_opt->parameter_list->head)
                    ss_fprintf(ss, ",");

                print_declaration_specifiers(ss, p_parameter_declaration->declaration_specifiers);
                ss_fprintf(ss, " ");
                print_declarator(ss, p_parameter_declaration->declarator, bAbstract);

                p_parameter_declaration = p_parameter_declaration->next;
            }
            //... TODO
            ss_fprintf(ss, ")");
        }
        else if (p_array_function->array_declarator)
        {
            ss_fprintf(ss, "[]");
        }
        p_array_function = p_array_function->next;
    }

}



void print_declarator(struct osstream* ss, struct declarator* p_declarator, bool bAbstract)
{
    bool first = true;
    if (p_declarator->pointer)
    {
        struct pointer* p = p_declarator->pointer;
        while (p)
        {
            if (p->type_qualifier_list_opt)
            {
                print_type_qualifier_flags(ss, &first, p->type_qualifier_list_opt->flags);
            }
            ss_fprintf(ss, "*");
            p = p->pointer;
        }
    }
    print_direct_declarator(ss, p_declarator->direct_declarator, bAbstract);

}

void print_type_name(struct osstream* ss, struct type_name* p)
{
    bool first = true;
    print_specifier_qualifier_list(ss, &first, p->specifier_qualifier_list);
    print_declarator(ss, p->declarator, true);
}

struct type_name* type_name(struct parser_ctx* ctx, struct error* error)
{
    struct type_name* p_type_name = calloc(1, sizeof(struct type_name));

    p_type_name->first = ctx->current;


    p_type_name->specifier_qualifier_list = specifier_qualifier_list(ctx, error);
    p_type_name->declarator = declarator(ctx,
        p_type_name->specifier_qualifier_list,//??
        /*declaration_specifiers*/ NULL,
        true /*DEVE SER TODO*/,
        NULL,
        error);

    p_type_name->last = ctx->current->prev;

    p_type_name->declarator->specifier_qualifier_list = p_type_name->specifier_qualifier_list;
    return p_type_name;
}

struct braced_initializer* braced_initializer(struct parser_ctx* ctx, struct error* error)
{
    /*
     { }
     { initializer-list }
     { initializer-list , }
    */

    struct braced_initializer* p_bracket_initializer_list = calloc(1, sizeof(struct braced_initializer));
    p_bracket_initializer_list->first = ctx->current;
    parser_match_tk(ctx, '{', error);
    if (ctx->current->type != '}')
    {
        p_bracket_initializer_list->initializer_list = initializer_list(ctx, error);
    }
    parser_match_tk(ctx, '}', error);
    return p_bracket_initializer_list;
}



struct initializer* initializer(struct parser_ctx* ctx, struct error* error)
{
    /*
    initializer:
      assignment-expression
      braced-initializer
    */

    struct initializer* p_initializer = calloc(1, sizeof(struct initializer));

    p_initializer->first_token = ctx->current;

    if (ctx->current->type == '{')
    {
        p_initializer->braced_initializer = braced_initializer(ctx, error);
    }
    else
    {
        struct expression_ctx ectx = { 0 };
        p_initializer->assignment_expression = assignment_expression(ctx, error, &ectx);
    }
    return p_initializer;
}


struct initializer_list* initializer_list(struct parser_ctx* ctx, struct error* error)
{
    /*
    initializer-list:
       designation opt initializer
       initializer-list , designation opt initializer
    */


    struct initializer_list* p_initializer_list = calloc(1, sizeof(struct initializer_list));

    p_initializer_list->first_token = ctx->current;

    struct designation* p_designation = NULL;
    if (first_of_designator(ctx))
    {
        p_designation = designation(ctx, error);
    }
    struct initializer* p_initializer = initializer(ctx, error);
    p_initializer->designation = p_designation;
    list_add(p_initializer_list, p_initializer);
    while (error->code == 0 && ctx->current != NULL &&
        ctx->current->type == ',')
    {
        parser_match(ctx);
        if (ctx->current->type == '}')
            break; //follow

        struct designation* p_designation2 = NULL;
        if (first_of_designator(ctx))
        {
            p_designation2 = designation(ctx, error);
        }
        struct initializer* p_initializer2 = initializer(ctx, error);
        p_initializer2->designation = p_designation;
        list_add(p_initializer_list, p_initializer2);
    }
    return p_initializer_list;
}


struct designation* designation(struct parser_ctx* ctx, struct error* error)
{
    //designator_list '='
    struct designation* p_designation = calloc(1, sizeof(struct designation));
    designator_list(ctx, error);
    parser_match_tk(ctx, '=', error);
    return p_designation;
}

struct designator_list* designator_list(struct parser_ctx* ctx, struct error* error)
{
    //designator
    //designator_list designator
    struct designator_list* p_designator_list = calloc(1, sizeof(struct designator_list));
    list_add(p_designator_list, designator(ctx, error));
    while (error->code == 0 && ctx->current != NULL &&
        first_of_designator(ctx))
    {
        list_add(p_designator_list, designator(ctx, error));
    }
    return p_designator_list;
}


struct designator* designator(struct parser_ctx* ctx, struct error* error)
{
    //'[' constant_expression ']'
    //'.' identifier
    struct designator* p_designator = calloc(1, sizeof(struct designator));
    if (ctx->current->type == '[')
    {
        parser_match_tk(ctx, '[', error);
        struct expression_ctx ectx = { 0 };
        constant_expression(ctx, error, &ectx);
        parser_match_tk(ctx, ']', error);
    }
    else if (ctx->current->type == '.')
    {
        parser_match(ctx);
        parser_match_tk(ctx, TK_IDENTIFIER, error);
    }
    return p_designator;
}





struct static_assert_declaration* static_assert_declaration(struct parser_ctx* ctx, struct error* error)
{

    /*
     static_assert-declaration:
      "static_assert" ( constant-expression , string-literal ) ;
      "static_assert" ( constant-expression ) ;
    */

    struct static_assert_declaration* p_static_assert_declaration = calloc(1, sizeof(struct static_assert_declaration));
    try
    {
        p_static_assert_declaration->first_token = ctx->current;
        struct token* position = ctx->current;
        parser_match_tk(ctx, TK_KEYWORD__STATIC_ASSERT, error);
        parser_match_tk(ctx, '(', error);
        struct expression_ctx ectx = { .bConstantExpressionRequired = true };
        p_static_assert_declaration->constant_expression = constant_expression(ctx, error, &ectx);

        if (error->code != 0)
            throw;

        if (ctx->current->type == ',')
        {
            parser_match(ctx);
            p_static_assert_declaration->string_literal_opt = ctx->current;
            parser_match_tk(ctx, TK_STRING_LITERAL, error);
        }

        parser_match_tk(ctx, ')', error);
        p_static_assert_declaration->last_token = ctx->current;
        parser_match_tk(ctx, ';', error);

        if (p_static_assert_declaration->constant_expression->constant_value == 0)
        {
            if (p_static_assert_declaration->string_literal_opt)
            {
                parser_seterror_with_token(ctx, position, "_Static_assert failed %s\n",
                    p_static_assert_declaration->string_literal_opt->lexeme);
            }
            else
            {
                parser_seterror_with_token(ctx, position, "_Static_assert failed");
            }
            error->code = 1;
        }
    }
    catch
    {}
    return p_static_assert_declaration;
}


struct attribute_specifier_sequence* attribute_specifier_sequence_opt(struct parser_ctx* ctx, struct error* error)
{
    //attribute_specifier_sequence_opt attribute_specifier
    struct attribute_specifier_sequence* p_attribute_specifier_sequence =
        calloc(1, sizeof(struct attribute_specifier_sequence));

    while (error->code == 0 &&
        ctx->current != NULL &&
        first_of_attribute_specifier(ctx))
    {
        list_add(p_attribute_specifier_sequence, attribute_specifier(ctx, error));
    }
    return p_attribute_specifier_sequence;
}

struct attribute_specifier_sequence* attribute_specifier_sequence(struct parser_ctx* ctx, struct error* error)
{
    //attribute_specifier_sequence_opt attribute_specifier
    struct attribute_specifier_sequence* p_attribute_specifier_sequence = calloc(1, sizeof(struct attribute_specifier_sequence));
    while (error->code == 0 && ctx->current != NULL &&
        first_of_attribute_specifier(ctx))
    {
        list_add(p_attribute_specifier_sequence, attribute_specifier(ctx, error));
    }
    return p_attribute_specifier_sequence;
}


struct attribute_specifier* attribute_specifier(struct parser_ctx* ctx, struct error* error)
{
    struct attribute_specifier* p_attribute_specifier = calloc(1, sizeof(struct attribute_specifier));

    p_attribute_specifier->first = ctx->current;



    //'[' '[' attribute_list ']' ']'
    parser_match_tk(ctx, '[', error);
    parser_match_tk(ctx, '[', error);
    p_attribute_specifier->attribute_list = attribute_list(ctx, error);
    parser_match_tk(ctx, ']', error);
    p_attribute_specifier->last = ctx->current;
    parser_match_tk(ctx, ']', error);
    return p_attribute_specifier;
}



struct attribute_list* attribute_list(struct parser_ctx* ctx, struct error* error)
{
    struct attribute_list* p_attribute_list = calloc(1, sizeof(struct attribute_list));
    //attribute_opt
    //attribute_list ',' attribute_opt
    while (error->code == 0 && ctx->current != NULL && (
        first_of_attribute(ctx) ||
        ctx->current->type == ','))
    {
        if (first_of_attribute(ctx))
        {
            list_add(p_attribute_list, attribute(ctx, error));
        }
        if (ctx->current->type == ',')
        {
            parser_match(ctx);
        }
    }
    return p_attribute_list;
}

bool first_of_attribute(struct parser_ctx* ctx)
{
    if (ctx->current == NULL)
        return false;
    return ctx->current->type == TK_IDENTIFIER;
}

struct attribute* attribute(struct parser_ctx* ctx, struct error* error)
{
    struct attribute* p_attribute = calloc(1, sizeof(struct attribute));
    //attribute_token attribute_argument_clause_opt
    p_attribute->attribute_token = attribute_token(ctx, error);
    if (ctx->current->type == '(') //first
    {
        p_attribute->attribute_argument_clause = attribute_argument_clause(ctx, error);
    }
    return p_attribute;
}


struct attribute_token* attribute_token(struct parser_ctx* ctx, struct error* error)
{
    struct attribute_token* p_attribute_token = calloc(1, sizeof(struct attribute_token));
    //standard_attribute
    //attribute_prefixed_token
    bool bStandardAtt = strcmp(ctx->current->lexeme, "deprecated") == 0 ||
        strcmp(ctx->current->lexeme, "fallthrough") == 0 ||
        strcmp(ctx->current->lexeme, "maybe_unused") == 0 ||
        strcmp(ctx->current->lexeme, "nodiscard") == 0;
    parser_match_tk(ctx, TK_IDENTIFIER, error);
    if (ctx->current->type == '::')
    {
        parser_match(ctx);
        parser_match_tk(ctx, TK_IDENTIFIER, error);
    }
    else
    {
        if (!bStandardAtt)
        {
            printf("warning not std att\n");
        }
    }
    return p_attribute_token;
}



struct attribute_argument_clause* attribute_argument_clause(struct parser_ctx* ctx, struct error* error)
{
    struct attribute_argument_clause* p_attribute_argument_clause = calloc(1, sizeof(struct attribute_argument_clause));
    //'(' balanced_token_sequence_opt ')'
    parser_match_tk(ctx, '(', error);
    balanced_token_sequence_opt(ctx, error);
    parser_match_tk(ctx, ')', error);
    return p_attribute_argument_clause;
}


struct balanced_token_sequence* balanced_token_sequence_opt(struct parser_ctx* ctx, struct error* error)
{
    struct balanced_token_sequence* p_balanced_token_sequence = calloc(1, sizeof(struct balanced_token_sequence));
    //balanced_token
    //balanced_token_sequence balanced_token
    int count1 = 0;
    int count2 = 0;
    int count3 = 0;
    for (; ctx->current;)
    {
        if (ctx->current->type == '(')
            count1++;
        else if (ctx->current->type == '[')
            count2++;
        else if (ctx->current->type == '{')
            count3++;
        else if (ctx->current->type == ')')
        {
            if (count1 == 0)
            {
                //parser_match(ctx);
                break;
            }
            count1--;
        }
        else if (ctx->current->type == '[')
            count2--;
        else if (ctx->current->type == '{')
            count3--;
        parser_match(ctx);
    }
    if (count2 != 0)
    {
        parser_seterror_with_token(ctx, ctx->current, "expected ']' before ')'");
        error->code = 1;
    }
    if (count3 != 0)
    {
        parser_seterror_with_token(ctx, ctx->current, "expected '}' before ')'");
        error->code = 1;
    }
    return p_balanced_token_sequence;
}


struct statement* statement(struct parser_ctx* ctx, struct error* error)
{
    struct statement* p_statement = calloc(1, sizeof(struct statement));
    if (first_of_labeled_statement(ctx))
    {
        p_statement->labeled_statement = labeled_statement(ctx, error);
    }
    else
    {
        p_statement->unlabeled_statement = unlabeled_statement(ctx, error);
    }
    //labeled_statement
    //unlabeled_statement
    return p_statement;
}

struct primary_block* primary_block(struct parser_ctx* ctx, struct error* error)
{
    struct primary_block* p_primary_block = calloc(1, sizeof(struct primary_block));
    if (first_of_compound_statement(ctx))
    {
        p_primary_block->compound_statement = compound_statement(ctx, error);
    }
    else if (first_of_selection_statement(ctx))
    {
        p_primary_block->selection_statement = selection_statement(ctx, error);
    }
    else if (first_of_iteration_statement(ctx))
    {
        p_primary_block->iteration_statement = iteration_statement(ctx, error);
    }
    else if (ctx->current->type == TK_KEYWORD_DEFER)
    {
        p_primary_block->defer_statement = defer_statement(ctx, error);
    }
    else
    {
        seterror(error, "unexpected");
    }
    p_primary_block->last = previous_parser_token(ctx->current);
    return p_primary_block;
}

struct secondary_block* secondary_block(struct parser_ctx* ctx, struct error* error)
{
    struct secondary_block* p_secondary_block = calloc(1, sizeof(struct secondary_block));
    p_secondary_block->first = ctx->current;


    p_secondary_block->statement = statement(ctx, error);

    p_secondary_block->last = previous_parser_token(ctx->current);

    return p_secondary_block;
}

bool first_of_primary_block(struct parser_ctx* ctx)
{
    if (first_of_compound_statement(ctx) ||
        first_of_selection_statement(ctx) ||
        first_of_iteration_statement(ctx) ||
        ctx->current->type == TK_KEYWORD_DEFER /*extension*/
        )
    {
        return true;
    }
    return false;
}

struct unlabeled_statement* unlabeled_statement(struct parser_ctx* ctx, struct error* error)
{
    struct unlabeled_statement* p_unlabeled_statement = calloc(1, sizeof(struct unlabeled_statement));

    if (first_of_primary_block(ctx))
    {
        p_unlabeled_statement->primary_block = primary_block(ctx, error);
    }
    else if (first_of_jump_statement(ctx))
    {
        p_unlabeled_statement->jump_statement = jump_statement(ctx, error);
    }
    else
    {
        p_unlabeled_statement->expression_statement = expression_statement(ctx, error);
    }
    //expression_statement
    //attribute_specifier_sequence_opt compound_statement
    //attribute_specifier_sequence_opt selection_statement
    //attribute_specifier_sequence_opt iteration_statement
    //attribute_specifier_sequence_opt jump_statement
    return p_unlabeled_statement;
}
struct label* label(struct parser_ctx* ctx, struct error* error)
{
    struct label* p_label = calloc(1, sizeof(struct label));
    if (ctx->current->type == TK_IDENTIFIER)
    {
        p_label->name = ctx->current;
        parser_match(ctx);
        parser_match_tk(ctx, ':', error);
    }
    else if (ctx->current->type == TK_KEYWORD_CASE)
    {
        parser_match(ctx);
        struct expression_ctx ectx = { .bConstantExpressionRequired = true };
        constant_expression(ctx, error, &ectx);
        parser_match_tk(ctx, ':', error);
    }
    else if (ctx->current->type == TK_KEYWORD_DEFAULT)
    {
        parser_match(ctx);
        parser_match_tk(ctx, ':', error);
    }
    //attribute_specifier_sequence_opt identifier ':'
    //attribute_specifier_sequence_opt 'case' constant_expression ':'
    //attribute_specifier_sequence_opt 'default' ':'
    return p_label;
}

struct labeled_statement* labeled_statement(struct parser_ctx* ctx, struct error* error)
{
    struct labeled_statement* p_labeled_statement = calloc(1, sizeof(struct labeled_statement));
    //label statement
    p_labeled_statement->label = label(ctx, error);
    p_labeled_statement->statement = statement(ctx, error);
    return p_labeled_statement;
}

struct compound_statement* compound_statement(struct parser_ctx* ctx, struct error* error)
{
    //'{' block_item_list_opt '}'
    struct compound_statement* p_compound_statement = calloc(1, sizeof(struct compound_statement));
    struct scope block_scope = { .variables.capacity = 10 };
    scope_list_push(&ctx->scopes, &block_scope);

    p_compound_statement->first = ctx->current;
    parser_match_tk(ctx, '{', error);

    if (ctx->current->type != '}')
    {
        p_compound_statement->block_item_list = block_item_list(ctx, error);
    }

    p_compound_statement->last = ctx->current;
    parser_match_tk(ctx, '}', error);

    //TODO ver quem nao foi usado.

    for (int i = 0; i < block_scope.variables.capacity; i++)
    {
        if (block_scope.variables.table == NULL)
            continue;
        struct map_entry* entry = block_scope.variables.table[i];
        while (entry)
        {

            struct declarator* p_declarator =
                p_declarator = container_of(entry->p, struct declarator, type_id);

            if (p_declarator)
            {
                if (p_declarator->nUses == 0)
                {
                    //setwarning_with_token(ctx, p_declarator->name, )
                    ctx->n_warnings++;
                    if (p_declarator->name && p_declarator->name->pFile)
                    {
                        ctx->printf(WHITE "%s:%d:%d: ",
                            p_declarator->name->pFile->lexeme,
                            p_declarator->name->line,
                            p_declarator->name->col);

                        ctx->printf(LIGHTMAGENTA "warning: " WHITE "'%s': unreferenced declarator\n",
                            p_declarator->name->lexeme);
                    }
                }
            }

            entry = entry->next;
        }
    }

    scope_list_pop(&ctx->scopes);

    return p_compound_statement;
}

struct block_item_list block_item_list(struct parser_ctx* ctx, struct error* error)
{
    /*
      block_item_list:
      block_item
      block_item_list block_item
    */
    struct block_item_list block_item_list = { 0 };
    list_add(&block_item_list, block_item(ctx, error));
    while (error->code == 0 && ctx->current != NULL &&
        ctx->current->type != '}') //follow
    {
        list_add(&block_item_list, block_item(ctx, error));
    }
    return block_item_list;
}

struct block_item* block_item(struct parser_ctx* ctx, struct error* error)
{
    //   declaration
    //     unlabeled_statement
    //   label
    struct block_item* p_block_item = calloc(1, sizeof(struct block_item));
    attribute_specifier_sequence_opt(ctx, error);

    p_block_item->first_token = ctx->current;

    if (ctx->current->type == TK_KEYWORD__ASM)
    {  /*
    asm-block:
    __asm assembly-instruction ;opt
    __asm { assembly-instruction-list } ;opt

assembly-instruction-list:
    assembly-instruction ;opt
    assembly-instruction ; assembly-instruction-list ;opt
    */

        parser_match(ctx);
        if (ctx->current->type == '{')
        {
            parser_match(ctx);
            while (error->code == 0 &&
                ctx->current->type != '}')
            {
                parser_match(ctx);
            }
            parser_match(ctx);
        }
        else
        {
            while (ctx->current->type != TK_NEWLINE)
            {
                ctx->current = ctx->current->next;
            }
            parser_match(ctx);

        }
        if (ctx->current->type == ';')
            parser_match(ctx);
    }
    else if (first_of_declaration_specifier(ctx) ||
        first_of_static_assert_declaration(ctx))
    {
        p_block_item->declaration = declaration(ctx, error);
        struct init_declarator* p = p_block_item->declaration->init_declarator_list.head;
        while (p)
        {
            if (p->declarator && p->declarator->name)
            {
                naming_convention_local_var(ctx, p->declarator->name, &p->declarator->type);
            }
            p = p->next;
        }
    }
    else if (first_of_label(ctx))
    {
        //so identifier confunde com expression
        p_block_item->label = label(ctx, error);
    }
    else
    {
        p_block_item->unlabeled_statement = unlabeled_statement(ctx, error);
    }
    /*
                                           declaration-specifiers init-declarator-list_opt;
              attribute-specifier-sequence declaration-specifiers init-declarator-list;
              static_assert-declaration attribute_declaration
    */
    /*
    unlabeled-statement:
     expression-statement
     attribute-specifier-sequenceopt compound-statement
     attribute-specifier-sequenceopt selection-statement
     attribute-specifier-sequenceopt iteration-statement
     attribute-specifier-sequenceopt jump-statement

    label:
    attribute-specifier-sequenceopt identifier :
    attribute-specifier-sequenceopt case constant-expression :
    attribute-specifier-sequenceopt default :
    */
    return p_block_item;
}



struct selection_statement* selection_statement(struct parser_ctx* ctx, struct error* error)
{
    /*
    init-statement:
    expression-statement
    simple-declaration
    */
    /*
       'if' '(' init_statement_opt expression ')' statement
       'if' '(' init_statement_opt expression ')' statement 'else' statement
       'switch' '(' expression ')' statement
    */
    /*
       'if' '(' expression ')' statement
       'if' '(' expression ')' statement 'else' statement
       'switch' '(' expression ')' statement
    */
    struct selection_statement* p_selection_statement = calloc(1, sizeof(struct selection_statement));

    p_selection_statement->first_token = ctx->current;

    struct scope if_scope = { 0 };
    scope_list_push(&ctx->scopes, &if_scope); //variaveis decladas no if

    if (ctx->current->type == TK_KEYWORD_IF)
    {
        parser_match(ctx);
        parser_match_tk(ctx, '(', error);
        if (first_of_declaration_specifier(ctx))
        {
            struct declaration_specifiers* p_declaration_specifiers = declaration_specifiers(ctx, error);
            struct init_declarator_list list = init_declarator_list(ctx, p_declaration_specifiers, error);
            p_selection_statement->init_declarator = list.head; //only one
            parser_match_tk(ctx, ';', error);
        }

        struct expression_ctx ectx = { 0 };
        p_selection_statement->expression = expression(ctx, error, &ectx);

        //if (ctx->current->type == ';')
        //{
          //  p_selection_statement->expression = expression(ctx, error, &ectx);
        //}
        parser_match_tk(ctx, ')', error);
        p_selection_statement->secondary_block = secondary_block(ctx, error);
        if (ctx->current->type == TK_KEYWORD_ELSE)
        {
            p_selection_statement->else_catch_token = ctx->current;
            parser_match(ctx);
            p_selection_statement->else_secondary_block = secondary_block(ctx, error);
        }
        else
        {
            p_selection_statement->else_catch_token = previous_parser_token(ctx->current);
        }
    }
    else if (ctx->current->type == TK_KEYWORD_SWITCH)
    {
        parser_match(ctx);
        parser_match_tk(ctx, '(', error);
        struct expression_ctx ectx = { 0 };
        p_selection_statement->expression = expression(ctx, error, &ectx);
        parser_match_tk(ctx, ')', error);
        p_selection_statement->secondary_block = secondary_block(ctx, error);
    }
    else if (ctx->current->type == TK_KEYWORD_TRY)
    {
        ctx->try_catch_block_index++;

        /*facilita geração de código*/
        p_selection_statement->try_catch_block_index = ctx->try_catch_block_index;

        parser_match(ctx);
        p_selection_statement->secondary_block = secondary_block(ctx, error);
        ctx->try_catch_block_index--;

        if (ctx->current->type == TK_KEYWORD_CATCH)
        {
            p_selection_statement->else_catch_token = ctx->current;
            parser_match(ctx);

            p_selection_statement->else_secondary_block = secondary_block(ctx, error);
        }
        else
        {
            p_selection_statement->else_catch_token = previous_parser_token(ctx->current);
        }

    }

    p_selection_statement->last_token = ctx->current->prev;

    scope_list_pop(&ctx->scopes);

    return p_selection_statement;
}

struct defer_statement* defer_statement(struct parser_ctx* ctx, struct error* error)
{
    struct defer_statement* p_defer_statement = calloc(1, sizeof(struct defer_statement));
    if (ctx->current->type == TK_KEYWORD_DEFER)
    {
        p_defer_statement->firsttoken = ctx->current;
        parser_match(ctx);
        p_defer_statement->secondary_block = secondary_block(ctx, error);
        p_defer_statement->lasttoken = previous_parser_token(ctx->current);
    }
    return p_defer_statement;
}

struct iteration_statement* iteration_statement(struct parser_ctx* ctx, struct error* error)
{
    /*
    iteration-statement:
      while ( expression ) statement
      do statement while ( expression ) ;
      for ( expressionopt ; expressionopt ; expressionopt ) statement
      for ( declaration expressionopt ; expressionopt ) statement
    */
    struct iteration_statement* p_iteration_statement = calloc(1, sizeof(struct iteration_statement));
    p_iteration_statement->token = ctx->current;
    if (ctx->current->type == TK_KEYWORD_DO)
    {
        parser_match(ctx);
        p_iteration_statement->secondary_block = secondary_block(ctx, error);
        parser_match_tk(ctx, TK_KEYWORD_WHILE, error);
        parser_match_tk(ctx, '(', error);
        struct expression_ctx ectx = { 0 };
        p_iteration_statement->expression1 = expression(ctx, error, &ectx);
        parser_match_tk(ctx, ')', error);
        parser_match_tk(ctx, ';', error);
    }
    else if (ctx->current->type == TK_KEYWORD_REPEAT)
    {
        parser_match(ctx);
        p_iteration_statement->secondary_block = secondary_block(ctx, error);
    }
    else if (ctx->current->type == TK_KEYWORD_WHILE)
    {
        parser_match(ctx);
        parser_match_tk(ctx, '(', error);
        struct expression_ctx ectx = { 0 };
        p_iteration_statement->expression1 = expression(ctx, error, &ectx);
        parser_match_tk(ctx, ')', error);
        p_iteration_statement->secondary_block = secondary_block(ctx, error);
    }
    else if (ctx->current->type == TK_KEYWORD_FOR)
    {
        struct expression_ctx ectx = { 0 };
        parser_match(ctx);
        parser_match_tk(ctx, '(', error);
        if (first_of_declaration_specifier(ctx))
        {
            struct scope for_scope = { 0 };
            scope_list_push(&ctx->scopes, &for_scope);

            declaration(ctx, error);
            if (ctx->current->type != ';')
            {
                p_iteration_statement->expression1 = expression(ctx, error, &ectx);
            }
            parser_match_tk(ctx, ';', error);
            if (ctx->current->type != ')')
                p_iteration_statement->expression2 = expression(ctx, error, &ectx);

            parser_match_tk(ctx, ')', error);

            p_iteration_statement->secondary_block = secondary_block(ctx, error);

            scope_list_pop(&ctx->scopes);
        }
        else
        {
            if (ctx->current->type != ';')
                expression(ctx, error, &ectx);
            parser_match_tk(ctx, ';', error);
            if (ctx->current->type != ';')
                expression(ctx, error, &ectx);
            parser_match_tk(ctx, ';', error);
            if (ctx->current->type != ')')
                p_iteration_statement->expression1 = expression(ctx, error, &ectx);
            parser_match_tk(ctx, ')', error);

            p_iteration_statement->secondary_block = secondary_block(ctx, error);
        }
    }
    return p_iteration_statement;
}
struct jump_statement* jump_statement(struct parser_ctx* ctx, struct error* error)
{
    /*
      jump-statement:
            goto identifier ;
            continue ;
            break ;
            return expressionopt ;
    */

    /*
       throw; (extension)
    */

    struct jump_statement* p_jump_statement = calloc(1, sizeof(struct jump_statement));

    p_jump_statement->token = ctx->current;

    if (ctx->current->type == TK_KEYWORD_GOTO)
    {
        parser_match(ctx);
        p_jump_statement->label = ctx->current;
        parser_match_tk(ctx, TK_IDENTIFIER, error);
    }
    else if (ctx->current->type == TK_KEYWORD_CONTINUE)
    {
        parser_match(ctx);
    }
    else if (ctx->current->type == TK_KEYWORD_BREAK)
    {
        parser_match(ctx);
    }
    else if (ctx->current->type == TK_KEYWORD_THROW)
    {
        if (ctx->try_catch_block_index == 0)
        {
            error->code = 1;
            parser_seterror_with_token(ctx, ctx->current, "throw must be inside try blocks");
        }

        /*helps code generation*/
        p_jump_statement->try_catch_block_index = ctx->try_catch_block_index;

        /*é preciso estar dentro de um block try catch*/
        parser_match(ctx);
    }
    else if (ctx->current->type == TK_KEYWORD_RETURN)
    {
        parser_match(ctx);
        if (ctx->current->type != ';')
        {
            struct expression_ctx ectx = { 0 };
            p_jump_statement->expression = expression(ctx, error, &ectx);

            /*
            * Check is return type is compatible with function return
            */
            if (!type_is_compatible(&ctx->p_current_function_opt->init_declarator_list.head->declarator->type,
                &p_jump_statement->expression->type))
            {
                parser_seterror_with_token(ctx, p_jump_statement->expression->first, "return type is incompatible");
            }
        }
    }
    else
    {
        assert(false);
    }
    p_jump_statement->lasttoken = ctx->current;
    parser_match_tk(ctx, ';', error);
    return p_jump_statement;
}

struct expression_statement* expression_statement(struct parser_ctx* ctx, struct error* error)
{
    struct expression_statement* p_expression_statement = calloc(1, sizeof(struct expression_statement));
    /*
     expression-statement:
       expression_opt ;
       attribute-specifier-sequence expression ;
    */
    if (ctx->current->type != ';')
    {
        p_expression_statement->first_token = ctx->current;
        struct expression_ctx ectx = { 0 };
        p_expression_statement->expression = expression(ctx, error, &ectx);
    }
    parser_match_tk(ctx, ';', error);
    return p_expression_statement;
}

void declaration_list_destroy(struct declaration_list* list)
{

}

struct declaration_list translation_unit(struct parser_ctx* ctx, struct error* error)
{
    struct declaration_list declaration_list = { 0 };
    /*
      translation_unit:
      external_declaration
      translation_unit external_declaration
    */
    while (error->code == 0 &&
        ctx->current != NULL)
    {
        list_add(&declaration_list, external_declaration(ctx, error));
    }
    return declaration_list;
}


struct declaration* external_declaration(struct parser_ctx* ctx, struct error* error)
{
    /*
     function_definition
     declaration
     */
    return function_definition_or_declaration(ctx, error);
}

struct compound_statement* function_body(struct parser_ctx* ctx, struct error* error)
{
    return compound_statement(ctx, error);
}


struct declaration_list parse(struct options* options, struct token_list* list, struct error* error)
{

    anonymous_struct_count = 0;

    struct scope file_scope = { 0 };
    struct parser_ctx ctx = { .input_language = options->input , .check_naming_conventions = options->check_naming_conventions };
#ifdef TEST
    ctx.printf = printf_nothing;
#else
    ctx.printf = printf;
#endif


    scope_list_push(&ctx.scopes, &file_scope);
    ctx.input_list = *list;
    ctx.current = ctx.input_list.head;
    parser_skip_blanks(&ctx);

    struct declaration_list l = translation_unit(&ctx, error);
    if (ctx.n_errors > 0)
        error->code = 1;

    return l;
}

void print_direct_declarator_type(struct osstream* ss, struct direct_declarator_type* type);

void print_declarator_type(struct osstream* ss, struct declarator_type* p_declarator_type)
{
    if (p_declarator_type == NULL)
    {
        return;
    }

    bool first = false;
    struct pointer_type* pointer = p_declarator_type->pointers.head;
    while (pointer)
    {
        ss_fprintf(ss, "*");
        print_type_qualifier_flags(ss, &first, pointer->type_qualifier_flags);

        pointer = pointer->next;
    }

    if (p_declarator_type->direct_declarator_type)
    {
        print_direct_declarator_type(ss, p_declarator_type->direct_declarator_type);
    }


}

void print_direct_declarator_type(struct osstream* ss, struct direct_declarator_type* p_direct_declarator_type)
{

    if (p_direct_declarator_type->declarator_opt)
    {
        ss_fprintf(ss, "(");
        print_declarator_type(ss, p_direct_declarator_type->declarator_opt);
        ss_fprintf(ss, ")");
    }

    struct array_function_type* p_array_function_type =
        p_direct_declarator_type->array_function_type_list.head;
    for (; p_array_function_type; p_array_function_type = p_array_function_type->next)
    {
        if (p_array_function_type->bIsArray)
        {
            ss_fprintf(ss, "[%d]", p_array_function_type->array_size);
        }
        else if (p_array_function_type->bIsFunction)
        {
            ss_fprintf(ss, "(");
            struct type* param = p_array_function_type->params.head;
            while (param)
            {
                if (param != p_array_function_type->params.head)
                    ss_fprintf(ss, ",");
                print_type(ss, param);
                param = param->next;
            }
            if (p_array_function_type->bVarArg)
                ss_fprintf(ss, ",...");

            ss_fprintf(ss, ")");
        }
    }

}

void print_type(struct osstream* ss, struct type* type)
{
    bool first = true;
    print_type_qualifier_flags(ss, &first, type->type_qualifier_flags);

    if (type->type_specifier_flags & TYPE_SPECIFIER_STRUCT_OR_UNION)
    {
        print_item(ss, &first, "struct ");
        ss_fprintf(ss, "%s", type->struct_or_union_specifier->tagName);
    }
    else if (type->type_specifier_flags & TYPE_SPECIFIER_ENUM)
    {
        print_item(ss, &first, "enum ");
        if (type->enum_specifier->tag_token)
            ss_fprintf(ss, "%s", type->enum_specifier->tag_token->lexeme);

    }
    else if (type->type_specifier_flags & TYPE_SPECIFIER_TYPEDEF)
    {
        assert(false);
    }
    else
    {
        print_type_specifier_flags(ss, &first, type->type_specifier_flags);
    }
    print_declarator_type(ss, type->declarator_type);

}

int fill_options(struct options* options, int argc, char** argv, struct preprocessor_ctx* prectx, struct error* error)
{
    /*first loop used to collect options*/
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
            continue;

        if (strcmp(argv[i], "-E") == 0)
        {
            options->bPreprocessOnly = true;
            continue;
        }
        if (strcmp(argv[i], "-r") == 0)
        {
            options->bRemoveComments = true;
            continue;
        }
        if (strcmp(argv[i], "-rm") == 0)
        {
            options->bRemoveMacros = true;
            continue;
        }
        if (strcmp(argv[i], "-n") == 0)
        {
            options->check_naming_conventions = true;
            continue;
        }
        if (strcmp(argv[i], "-fi") == 0)
        {
            options->format_input= true;
            continue;
        }
        if (strcmp(argv[i], "-fo") == 0)
        {
            options->format_ouput= true;
            continue;
        }
        //
        if (strcmp(argv[i], "-target=c99") == 0)
        {
            options->target = LANGUAGE_C99;
            continue;
        }
        if (strcmp(argv[i], "-target=c11") == 0)
        {
            options->target = LANGUAGE_C11;
            continue;
        }
        if (strcmp(argv[i], "-target=c2x") == 0)
        {
            options->target = LANGUAGE_C2X;
            continue;
        }
        if (strcmp(argv[i], "-target=cxx") == 0)
        {
            options->target = LANGUAGE_CXX;
            continue;
        }
        //
        if (strcmp(argv[i], "-std=c99") == 0)
        {
            options->input = LANGUAGE_C99;
            continue;
        }
        if (strcmp(argv[i], "-std=c11") == 0)
        {
            options->input = LANGUAGE_C11;
            continue;
        }
        if (strcmp(argv[i], "-std=c2x") == 0)
        {
            options->input = LANGUAGE_C2X;
            continue;
        }
        if (strcmp(argv[i], "-std=cxx") == 0)
        {
            options->input = LANGUAGE_CXX;
            continue;
        }
        //
        if (argv[i][1] == 'I')
        {
            include_dir_add(&prectx->include_dir, argv[i] + 2);
            continue;
        }
        if (argv[i][1] == 'D')
        {
            char buffer[200];
            snprintf(buffer, sizeof buffer, "#define %s \n", argv[i] + 2);

            struct token_list l1 = tokenizer(buffer, "", 0, TK_FLAG_NONE, error);
            preprocessor(prectx, &l1, 0, error);
            token_list_clear(&l1);
            continue;
        }
    }
    return error->code;
}

#ifdef _WIN32
unsigned long __stdcall GetEnvironmentVariableA(
    const char* lpName,
    char* lpBuffer,
    unsigned long nSize
);
#endif

void append_msvc_include_dir(struct preprocessor_ctx* prectx)
{
#ifdef _WIN32
    /*
     Para ver as variaveis de ambiente pode-se digitar set no windows
    */

    //char env[2000];
    //int n = GetEnvironmentVariableA("INCLUDE", env, sizeof(env));

    char env[] = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.31.31103/ATLMFC/include;"
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.31.31103/include;"
        "C:/Program Files (x86)/Windows Kits/NETFXSDK/4.8/include/um;"
        "C:/Program Files (x86)/Windows Kits/10/include/10.0.19041.0/ucrt;"
        "C:/Program Files (x86)/Windows Kits/10/include/10.0.19041.0/shared;"
        "C:/Program Files (x86)/Windows Kits/10/include/10.0.19041.0/um;"
        "C:/Program Files (x86)/Windows Kits/10/include/10.0.19041.0/winrt;"
        "C:/Program Files (x86)/Windows Kits/10/include/10.0.19041.0/cppwinrt";
    int n = strlen(env);

    if (n > 0 && n < sizeof(env))
    {
        const char* p = env;

        for (;;)
        {
            if (*p == '\0')
            {
                break;
            }
            char fileNameLocal[500] = { 0 };
            int count = 0;
            while (*p != '\0' && *p != ';')
            {
                fileNameLocal[count] = *p;
                p++;
                count++;
            }
            fileNameLocal[count] = 0;
            if (count > 0)
            {
                //printf("%s\n", fileNameLocal);
                strcat(fileNameLocal, "/");

                include_dir_add(&prectx->include_dir, fileNameLocal);
            }
            if (*p == '\0')
            {
                break;
            }
            p++;
        }
    }
#endif
}

const char* format_code(struct options* options,
    const char* content,
    struct error* error)
{
    struct ast ast = { 0 };
    char* s = NULL;


    struct preprocessor_ctx prectx = { 0 };

#ifdef TEST
    prectx.printf = printf_nothing;
#else
    prectx.printf = printf;
#endif


    prectx.macros.capacity = 5000;
    add_standard_macros(&prectx, error);


    try
    {
        prectx.options = *options;
        append_msvc_include_dir(&prectx);


        struct token_list tokens = tokenizer(content, "", 0, TK_FLAG_NONE, error);
        if (error->code != 0)
            throw;

        ast.token_list = preprocessor(&prectx, &tokens, 0, error);
        if (error->code != 0)
            throw;

        ast.declaration_list = parse(&options, &ast.token_list, error);
        if (error->code != 0)
        {
            throw;
        }
        struct format_visit_ctx visit_ctx = { 0 };
        visit_ctx.ast = ast;
        format_visit(&visit_ctx, error);

        if (options->bRemoveMacros)
            s = get_code_as_compiler_see(&visit_ctx.ast.token_list);
        else
            s = get_code_as_we_see(&visit_ctx.ast.token_list, options->bRemoveComments);

    }
    catch
    {

    }


    ast_destroy(&ast);
    preprocessor_ctx_destroy(&prectx);
    return s;
}

int compile_one_file(const char* file_name,
    int argc,
    char** argv,
    struct error* error)
{
    clock_t start_clock = clock();

    struct preprocessor_ctx prectx = { 0 };

#ifdef TEST
    prectx.printf = printf_nothing;
#else
    prectx.printf = printf;
#endif


    prectx.macros.capacity = 5000;
    add_standard_macros(&prectx, error);


    //int no_files = 0;
    struct ast ast = { 0 };

    struct options options = { .input = LANGUAGE_CXX };

    char* s = NULL;

    try
    {
        if (fill_options(&options, argc, argv, &prectx, error) != 0)
        {
            throw;
        }
        prectx.options = options;
        append_msvc_include_dir(&prectx);

        //printf("%-20s ", file_name);

        char* content = readfile(file_name);
        if (content == NULL)
        {
            seterror(error, "file not found '%s'\n", file_name);
            throw;
        }
        //printf(".");//1

        struct token_list tokens = tokenizer(content, file_name, 0, TK_FLAG_NONE, error);
        if (error->code != 0)
            throw;
        //printf(".");//2
        ast.token_list = preprocessor(&prectx, &tokens, 0, error);
        if (error->code != 0)
            throw;

        //_splitpath

        char path[200] = { 0 };
        snprintf(path, sizeof path, "./out/%s", file_name);

        mkdir("./out", 0777);


        if (options.bPreprocessOnly)
        {
            const char* s = print_preprocessed_to_string2(ast.token_list.head);
            printf("%s", s);
            free(s);
        }
        else
        {

            ast.declaration_list = parse(&options, &ast.token_list, error);
            if (error->code != 0)
            {
                throw;
            }

            if (options.format_input)
            {
                /*format input source before transformation*/
                struct format_visit_ctx visit_ctx = { 0 };
                visit_ctx.ast = ast;
                format_visit(&visit_ctx, error);
            }

            struct visit_ctx visit_ctx = { 0 };
            visit_ctx.target = options.target;
            visit_ctx.ast = ast;
            visit(&visit_ctx, error);

            if (options.bRemoveMacros)
                s = get_code_as_compiler_see(&visit_ctx.ast.token_list);
            else
                s = get_code_as_we_see(&visit_ctx.ast.token_list, options.bRemoveComments);

            if (options.format_ouput)
            {
                /*re-parser ouput and format*/
                const char* s2 = format_code(&options, s, error);
                free(s);
                s = s2;
            }

            FILE* out = fopen(path, "w");
            if (out)
            {
                fprintf(out, "%s", s);
                fclose(out);
                //printf("%-30s ", path);
            }
            else
            {
                seterror(error, "cannot open output file");
                throw;
            }



            clock_t end_clock_file = clock();
            double cpu_time_used_file = ((double)(end_clock_file - start_clock)) / CLOCKS_PER_SEC;

            //printf("OK %f sec\n", cpu_time_used_file);

        }
    }
    catch
    {
        //printf("Error %s\n", error->message);
    }


    free(s);
    ast_destroy(&ast);
    preprocessor_ctx_destroy(&prectx);

    return error->code;
}

int compile(int argc, char** argv, struct error* error)
{
    int hasErrors = 0;
    clock_t begin_clock = clock();
    int no_files = 0;

    /*second loop to compile each file*/
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
            continue;
        no_files++;
        clearerror(error);
        compile_one_file(argv[i], argc, argv, error);
        if (error->code != 0)
            hasErrors = true;
    }

    /*tempo total da compilacao*/
    clock_t end_clock = clock();
    double cpu_time_used = ((double)(end_clock - begin_clock)) / CLOCKS_PER_SEC;
    printf("Total %d files %f seconds\n", no_files, cpu_time_used);
    return hasErrors;
}


struct ast get_ast(struct options* options, const char* fileName, const char* source, struct error* error)
{
    struct ast ast = { 0 };

    struct token_list list = tokenizer(source, fileName, 0, TK_FLAG_NONE, error);
    if (error->code != 0)
        return ast;

    struct preprocessor_ctx prectx = { 0 };
    prectx.options = *options;

#ifdef TEST
    prectx.printf = printf_nothing;
#else
    prectx.printf = printf;
#endif

    prectx.macros.capacity = 5000;

    ast.token_list = preprocessor(&prectx, &list, 0, error);
    if (error->code != 0)
        return ast;

    ast.declaration_list = parse(&options, &ast.token_list, error);
    return ast;
}

/*
* dada uma string s produz o argv modificando a string de entrada
* return argc
*/
int strtoargv(char* s, int n, const char* argv[/*n*/])
{
    int argvc = 0;
    char* p = s;
    while (*p)
    {
        while (*p == ' ')
            p++;
        if (*p == 0)
            break;
        argv[argvc] = p;
        argvc++;
        while (*p != ' ' && *p != '\0')
            p++;
        if (*p == 0)
            break;
        *p = 0;
        p++;
        if (argvc >= n)
            break;/*nao tem mais lugares*/
    }
    return argvc;
}

char* compile_source(const char* pszoptions, const char* content)
{
    const char* argv[100] = { 0 };
    char string[200] = { 0 };
    snprintf(string, sizeof string, "exepath %s", pszoptions);

    const int argc = strtoargv(string, 10, argv);

    char* s = NULL;
    struct error error = { 0 };
    struct preprocessor_ctx prectx = { 0 };
#ifdef TEST
    prectx.printf = printf_nothing;
#else
    prectx.printf = printf;
#endif

    struct options options = { .input = LANGUAGE_CXX };
    //printf("options '%s'\n", pszoptions);
    try
    {
        if (fill_options(&options, argc, argv, &prectx, &error) != 0)
        {
            throw;
        }

        prectx.options = options;


        if (options.bPreprocessOnly)
        {

            struct token_list tokens = tokenizer(content, "source", 0, TK_FLAG_NONE, &error);
            if (error.code == 0)
            {
                struct token_list token_list = preprocessor(&prectx, &tokens, 0, &error);
                if (error.code == 0)
                {

                    s = print_preprocessed_to_string2(token_list.head);
                }
            }
            preprocessor_ctx_destroy(&prectx);


        }
        else
        {
            struct visit_ctx visit_ctx = { 0 };
            visit_ctx.target = options.target;
            struct ast ast = get_ast(&options, "source", content, &error);
            visit_ctx.ast = ast;
            visit(&visit_ctx, &error);

            if (options.bRemoveMacros)
            {
                s = get_code_as_compiler_see(&visit_ctx.ast.token_list);
            }
            else
            {
                s = get_code_as_we_see(&visit_ctx.ast.token_list, options.bRemoveComments);
            }
            if (options.format_ouput)
            {
                struct error error = { 0 };
                
                /*re-parser ouput and format*/
                const char* s2 = format_code(&options, s, &error);
                free(s);
                s = s2;
            }
        }

        if (error.code)
        {
            free(s);
            s = strdup(error.message);
        }
    }
    catch
    {
    }

    return s;
}


/*Função exportada para web*/
char* CompileText(const char* pszoptions, const char* content)
{
    return  compile_source(pszoptions, content);
}

void ast_destroy(struct ast* ast)
{
    token_list_destroy(&ast->token_list);
    declaration_list_destroy(&ast->declaration_list);
}

static bool is_all_upper(const char* text)
{
    const char* p = text;
    while (*p)
    {
        if (*p != toupper(*p))
        {
            return false;
        }
        p++;
    }
    return true;
}

static bool is_snake_case(const char* text)
{
    if (text == NULL)
        return true;

    if (!(*text >= 'a' && *text <= 'z'))
    {
        return false;
    }

    while (*text)
    {
        if ((*text >= 'a' && *text <= 'z') ||
            *text == '_' ||
            (*text >= '0' && *text <= '9'))

        {
            //ok
        }
        else
            return false;
        text++;
    }

    return true;
}

static bool is_camel_case(const char* text)
{
    if (text == NULL)
        return true;

    if (!(*text >= 'a' && *text <= 'z'))
    {
        return false;
    }

    while (*text)
    {
        if ((*text >= 'a' && *text <= 'z') ||
            (*text >= 'A' && *text <= 'Z') ||
            (*text >= '0' && *text <= '9'))
        {
            //ok
        }
        else
            return false;
        text++;
    }

    return true;
}

static bool is_pascal_case(const char* text)
{
    if (text == NULL)
        return true;

    if (!(text[0] >= 'A' && text[0] <= 'Z'))
    {
        /*first letter uppepr case*/
        return false;
    }

    while (*text)
    {
        if ((*text >= 'a' && *text <= 'z') ||
            (*text >= 'A' && *text <= 'Z') ||
            (*text >= '0' && *text <= '9'))
        {
            //ok
        }
        else
            return false;
        text++;
    }

    return true;
}
/*
 * This naming conventions are not ready yet...
 * but not dificult to implement.maybe options to choose style
 */
void naming_convention_struct_tag(struct parser_ctx* ctx, struct token* token)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!is_snake_case(token->lexeme)) {
        parser_set_info_with_token(ctx, token, "use snake_case for struct/union tags");
    }
}

void naming_convention_enum_tag(struct parser_ctx* ctx, struct token* token)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!is_snake_case(token->lexeme)) {
        parser_set_info_with_token(ctx, token, "use snake_case for enum tags");
    }
}

void naming_convention_function(struct parser_ctx* ctx, struct token* token)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;


    if (!is_snake_case(token->lexeme)) {
        parser_set_info_with_token(ctx, token, "use snake_case for functions");
    }
}
void naming_convention_global_var(struct parser_ctx* ctx, struct token* token, struct type* type)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!type_is_function_or_function_pointer(type))
    {
        if (!is_snake_case(token->lexeme)) {
            parser_set_info_with_token(ctx, token, "use snake_case global variables");
        }
    }
}

void naming_convention_local_var(struct parser_ctx* ctx, struct token* token, struct type* type)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!type_is_function_or_function_pointer(type))
    {
        if (!is_snake_case(token->lexeme)) {
            parser_set_info_with_token(ctx, token, "use snake_case for local variables");
        }
    }
}

void naming_convention_enumerator(struct parser_ctx* ctx, struct token* token)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!is_all_upper(token->lexeme)) {
        parser_set_info_with_token(ctx, token, "use UPPERCASE for enumerators");
    }
}

void naming_convention_struct_member(struct parser_ctx* ctx, struct token* token, struct type* type)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!is_snake_case(token->lexeme)) {
        parser_set_info_with_token(ctx, token, "use snake_case for struct members");
    }
}

void naming_convention_parameter(struct parser_ctx* ctx, struct token* token, struct type* type)
{
    if (!ctx->check_naming_conventions || token->level != 0)
        return;

    if (!is_snake_case(token->lexeme)) {
        parser_set_info_with_token(ctx, token, "use snake_case for arguments");
    }
}

#ifdef TEST
#include "unit_test.h"

void parser_specifier_test()
{
    const char* source = "long long long i;";
    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    struct ast ast = get_ast(&options, "source", source, &error);
    assert(error.code != 0); //esperado erro    
}

void take_address_type_test()
{
    const char* source =
        "void F(char(*p)[10])"
        "{"
        "    (*p)[0] = 'a';"
        "}";
    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    struct ast ast = get_ast(&options, "source", source, &error);

    assert(error.code == 0);
}

void parser_scope_test()
{
    const char* source = "void f() {int i; char i;}";
    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    struct ast ast = get_ast(&options, "source", source, &error);
    assert(error.code != 0); //tem que dar erro
}

void parser_tag_test()
{
    //mudou tipo do tag no mesmo escopo
    const char* source = "enum E { A }; struct E { int i; };";
    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    struct ast ast = get_ast(&options, "source", source, &error);
    assert(error.code != 0); //tem que dar erro        
}

void string_concatenation_test()
{
    const char* source = " \"part1\" \"part2\"";
    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    struct ast ast = get_ast(&options, "source", source, &error);
    assert(error.code == 0);
}

void test_digit_separator()
{
    char* result = compile_source("-std=C99", "int i = 1'000;");
    assert(strcmp(result, "int i = 1000;") == 0);
    free(result);
}

void test_lit()
{
    //_Static_assert(1 == 2, "");
    char* result = compile_source("-std=C99", "char * s = u8\"maçã\";");
    assert(strcmp(result, "char * s = \"ma\\xc3\\xa7\\xc3\\xa3\";") == 0);
    free(result);
}

void type_test2()
{
    char* src =
        "int a[10]; "
        "struct X* F() { return 0; }"
        " static_assert(typeid(*F()) == typeid(struct X));"
        " static_assert(typeid(&a) == typeid(int (*)[10]));"
        ;

    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    get_ast(&options, "source", src, &error);
    assert(error.code == 0);

}

void type_test3()
{
    char* src =
        "int i;"
        "int (*f)(void);"
        " static_assert(typeid(&i) == typeid(int *));"
        " static_assert(typeid(&f) == typeid(int (**)(void)));"
        ;

    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C99 };
    get_ast(&options, "source", src, &error);
    assert(error.code == 0);

}

void expand_test()
{
    char* src =
        "typedef int A[2];"
        "typedef A *B [1];"
        "static_assert(typeid(B) == typeid(int (*[1])[2]);";
    ;

    struct error error = { 0 };
    struct options options = { .input = LANGUAGE_C2X };
    get_ast(&options, "source", src, &error);
    assert(error.code == 0);
    clearerror(&error);


    char* src2 =
        "typedef char* A;"
        "typedef const A* B; "
        "static_assert(typeid(B) == typeid(char * const *);";

    get_ast(&options, "source", src2, &error);
    assert(error.code == 0);
    clearerror(&error);



    char* src3 =
        "typedef char* T1;"
        "typedef T1(*f[3])(int); "
        "static_assert(typeid(f) == typeid(char* (* [3])(int)));";
    //char* (* [3])(int)



    get_ast(&options, "source", src3, &error);
    assert(error.code == 0);
    clearerror(&error);


    //https://godbolt.org/z/WbK9zP7zM
    }




#endif


