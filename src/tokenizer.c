//#include <corecrt.h>
/*
   "string com codigo" se transforma em uma lista ligada de tokens

   ┌───┐   ┌───┐   ┌───┐   ┌───┐
   │   ├──►│   ├──►│   ├──►│   │──► NULL
   └───┘   └───┘   └───┘   └───┘

   ao passar no preprocessaor esta lista é expandida com includes e macros


   ┌───┐                  ┌───┐   ┌───┐   ┌───┐
   │   ├──────┐           │   ├──►│x  ├──►│x  │ ──► NULL
   └───┘      │           └───┘   └───┘   └───┘
            ┌─▼─┐   ┌───┐   ▲
            │   ├───┤   ├───┘
            └───┘   └───┘

  cada item tem um int level que indica o nivel de incluldes
  tambem bmacroexapanded que indica se token foi gerado de expando
  de macro e bfinal se este token eh final o que o parser realmente  ve

  a parte que da dentro do include, para efeito de parser pode ignorar
  todos os espacos. Temo um modo que so coloca os nos finais dentro
  do nivel 1 2. 3..
  ou colocar tudo sempre. #define INCLUDE_ALL 1
  a vantagem de include tudo eh pode colocar uma mensagem de erro
  de um header por ex copiando a linha. a desvantagem eh mais memoria

*/


#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <time.h>
#include "console.h"
#include "hashmap.h"
#include "osstream.h"
#include "fs.h"

#include "error.h"
#include "pre_expressions.h"
#include "tokenizer.h"

#ifdef _WIN32
#include <crtdbg.h>
#include <Windows.h>
#include <debugapi.h>
#undef assert
#define assert _ASSERTE
#endif

//declaração da macro container_of
#ifndef container_of
#define container_of(ptr , type , member) (type *)( (char *) ptr - offsetof(type , member) )
#endif

/*
 Se for 1 inclui todos os ignorados de dentro dos includes
 se for 0 ele faz so resumido e desctart oq nao eh usado.
*/
#define INCLUDE_ALL 1

void preprocessor_ctx_destroy(struct preprocessor_ctx* p)
{
    hashmap_destroy(&p->macros);
}

struct token_list preprocessor(struct preprocessor_ctx* ctx, struct token_list* inputList, int level, struct error* error);


void pre_seterror_with_token(struct preprocessor_ctx* ctx, struct token* p_token, const char* fmt, ...)
{
    ctx->n_errors++;
    //er->code = 1;

    if (p_token)
    {
        ctx->printf(WHITE "%s:%d:%d: ",
            p_token->pFile->lexeme,
            p_token->line,
            p_token->col);
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


    struct token* prev = p_token;
    while (prev && prev->prev && prev->prev->type != TK_NEWLINE)
    {
        prev = prev->prev;
    }
    struct token* next = prev;
    while (next && next->type != TK_NEWLINE)
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
    if (p_token)
    {
        for (int i = 1; i < p_token->col - 1; i++)
        {
            ctx->printf(" ");
        }
        ctx->printf(LIGHTGREEN "^\n");
    }
}
void pre_error_warning_with_token(struct preprocessor_ctx* ctx, struct token* p_token, bool bError)
{
    ctx->n_warnings++;
    //er->code = 1;

    if (p_token)
    {
        ctx->printf(WHITE "%s:%d:%d: ",
            p_token->pFile->lexeme,
            p_token->line,
            p_token->col);
    }
    else
    {
        ctx->printf(WHITE "<>");
    }

    if (bError)
          ctx->printf(LIGHTRED "error: " WHITE );
    else 
        ctx->printf(LIGHTMAGENTA "warning: " WHITE );


    struct token* prev = p_token;
    while (prev && prev->prev && prev->prev->type != TK_NEWLINE)
    {
        prev = prev->prev;
    }
    struct token* next = prev;
    while (next && next->type != TK_NEWLINE)
    {
        ctx->printf("%s", next->lexeme);
        next = next->next;
    }
    ctx->printf("\n");
    
}


struct include_dir* include_dir_add(struct include_dir_list* list, const char* path)
{
    struct include_dir* pNew = calloc(1, sizeof * pNew);
    pNew->path = strdup(path);
    if (list->head == NULL)
    {
        list->head = pNew;
        list->tail = pNew;
    }
    else
    {
        list->tail->next = pNew;
        list->tail = pNew;
    }
    return pNew;
}


const char* find_and_read_include_file(struct preprocessor_ctx* ctx, const char* path, char fullpath[300], bool* bAlreadyIncluded)
{
    snprintf(fullpath, 300, "%s", path);

    if (hashmap_find(&ctx->pragmaOnce, fullpath) != NULL)
    {
        *bAlreadyIncluded = true;
        return NULL;
    }


    char* content = readfile(fullpath);
    if (content == NULL)
    {
        struct include_dir* pCurrent = ctx->include_dir.head;
        while (pCurrent)
        {
            snprintf(fullpath, 300, "%s%s", pCurrent->path, path);

            if (hashmap_find(&ctx->pragmaOnce, fullpath) != NULL)
            {
                *bAlreadyIncluded = true;
                return NULL;
            }

            content = readfile(fullpath);
            if (content != NULL)
                break;
            pCurrent = pCurrent->next;
        }
    }
    return content;
}


struct macro_parameter
{
    const char* name;
    struct macro_parameter* next;
};


struct macro
{
    struct type_tag_id type_id;
    const char* name;
    struct token_list replacementList; /*copia*/
    struct macro_parameter* pParameters;
    bool bIsFunction;
    int usage;

    bool bExpand;
};

/*usado para verificar recursividade*/
struct MacroExpanded
{
    const char* name;
    struct MacroExpanded* pPrevious;
};

void add_macro(struct preprocessor_ctx* ctx, const char* name)
{
    struct macro* pMacro = calloc(1, sizeof * pMacro);
    if (pMacro == NULL)
    {
    }
    pMacro->name = strdup(name);
    hashmap_set(&ctx->macros, name, &pMacro->type_id);
}




struct MacroArgument
{
    const char* name;
    struct token_list tokens;
    struct MacroArgument* next;
};


struct token_list  copy_replacement_list(struct token_list* list);

struct token_list  copy_argument_list_tokens(struct token_list* list)
{
    //Faz uma copia dos tokens fazendo um trim no iniico e fim
    //qualquer espaco coments etcc vira um unico  espaco
    struct token_list r = { 0 };
    struct token* pCurrent = list->head;
    //sai de cima de todos brancos iniciais
    while (pCurrent &&
        (token_is_blank(pCurrent) ||
            pCurrent->type == TK_NEWLINE))
    {
        pCurrent = pCurrent->next;
    }
    //remover flag de espaco antes se tiver
    bool bIsFirst = true;
    bool previousIsBlank = false;
    for (; pCurrent;)
    {
        if (pCurrent && (token_is_blank(pCurrent) ||
            pCurrent->type == TK_NEWLINE))
        {
            if (pCurrent == list->tail)
                break;

            pCurrent = pCurrent->next;
            continue;
        }
        struct token* pAdded = token_list_clone_and_add(&r, pCurrent);
        if (pAdded->flags & TK_FLAG_HAS_NEWLINE_BEFORE)
        {
            pAdded->flags = pAdded->flags & ~TK_FLAG_HAS_NEWLINE_BEFORE;
            pAdded->flags |= TK_FLAG_HAS_SPACE_BEFORE;
        }
        if (bIsFirst)
        {
            pAdded->flags = pAdded->flags & ~TK_FLAG_HAS_SPACE_BEFORE;
            pAdded->flags = pAdded->flags & ~TK_FLAG_HAS_NEWLINE_BEFORE;
            bIsFirst = false;
        }
        remove_line_continuation(pAdded->lexeme);
        previousIsBlank = false;

        if (pCurrent == list->tail)
            break;
        pCurrent = pCurrent->next;

    }
    return r;
}


struct token_list copy_argument_list(struct MacroArgument* pMacroArgument)
{
    //pnew->tokens
    struct token_list list = copy_argument_list_tokens(&pMacroArgument->tokens);
    if (list.head == NULL)
    {
        /*nunca eh vazio..se for ele colocar um TK_PLACEMARKER*/
        struct token* pNew = calloc(1, sizeof * pNew);
        pNew->lexeme = strdup("");
        pNew->type = TK_PLACEMARKER;
        token_list_add(&list, pNew);
    }
    //print_list(&list);
    return list;
}


struct MacroArgumentList
{
    struct token_list tokens;
    struct MacroArgument* head;
    struct MacroArgument* tail;
};

void print_macro_arguments(struct MacroArgumentList* arguments)
{
    struct MacroArgument* pArgument = arguments->head;
    while (pArgument)
    {
        printf("%s:", pArgument->name);
        print_list(&pArgument->tokens);
        pArgument = pArgument->next;
    }
}

struct MacroArgument* find_macro_argument_by_name(struct MacroArgumentList* parameters, const char* name)
{
    /*
    * Os argumentos são coletados na expansão da macro e cada um (exceto ...)
    * é associado a um dos parametros da macro.
    */
    struct MacroArgument* p = parameters->head;
    while (p)
    {
        if (strcmp(p->name, name) == 0)
        {
            return p;
        }
        p = p->next;
    }
    return NULL;
}


void argument_list_add(struct MacroArgumentList* list, struct MacroArgument* pnew)
{
    assert(pnew->next == NULL);
    if (list->head == NULL)
    {
        list->head = pnew;
        list->tail = pnew;
    }
    else
    {
        list->tail->next = pnew;
        list->tail = pnew;
    }
}


void print_macro(struct macro* pMacro)
{
    printf("%s", pMacro->name);
    if (pMacro->bIsFunction)
        printf("(");
    struct macro_parameter* pParameter = pMacro->pParameters;
    while (pParameter)
    {
        if (pMacro->pParameters != pParameter)
            printf(",");
        printf("%s", pParameter->name);
        pParameter = pParameter->next;
    }
    if (pMacro->bIsFunction)
        printf(") ");
    print_list(&pMacro->replacementList);
}

void delete_macro(struct macro* pMacro)
{
    if (pMacro)
    {
        free(pMacro);
    }
}

struct macro* find_macro(struct preprocessor_ctx* ctx, const char* name)
{
    struct type_tag_id* pNode = hashmap_find(&ctx->macros, name);
    if (pNode == NULL)
        return NULL;
    struct macro* pMacro = container_of(pNode, struct macro, type_id);
    return pMacro;
}



void stream_print_line(struct stream* stream)
{
    const char* p = stream->current;
    while ((p - 1) >= stream->source &&
        *(p - 1) != '\n')
    {
        p--;
    }
    while (*p && *(p + 1) != '\n')
    {
        printf("%c", *p);
        p++;
    }
    printf("\n");
    for (int i = 0; i < stream->col - 1; i++)
        printf(" ");
    printf("^\n");
}

void stream_match(struct stream* stream)
{
    /*
    2. Each instance of a backslash character (\) immediately followed by a new-line character is
    deleted, splicing physical source lines to form logical source lines. Only the last backslash on
    any physical source line shall be eligible for being part of such a splice. A source file that is
    not empty shall end in a new-line character, which shall not be immediately preceded by a
    backslash character before any such splicing takes place.
    */
    stream->current++;
    if (stream->current[0] == '\n')
    {
        stream->line++;
        stream->col = 1;
    }
    else
    {
        stream->col++;
    }
    if (stream->current[0] == '\\' && stream->current[1] == '\n')
    {
        stream->current++;
        stream->current++;
        stream->line++;
        stream->col = 1;
    }
}

void print_line(struct token* p)
{
    printf("%s\n", p->pFile->lexeme);
    struct token* prev = p;
    while (prev->prev && prev->prev->type != TK_NEWLINE)
    {
        prev = prev->prev;
    }
    struct token* next = prev;
    while (next && next->type != TK_NEWLINE)
    {
        printf("%s", next->lexeme);
        next = next->next;
    }
    printf("\n");
}

int is_nondigit(struct stream* p)
{
    /*
    nondigit: one of
     _ a b c d e f g h i j k l m
     n o p q r s t u v w x y z
     A B C D E F G H I J K L M
     N O P Q R S T U V W X Y Z
    */
    return (p->current[0] >= 'a' && p->current[0] <= 'z') ||
        (p->current[0] >= 'A' && p->current[0] <= 'Z') ||
        (p->current[0] == '_');
}


enum token_type is_punctuator(struct stream* stream)
{
    //TODO peprformance range?

    enum token_type type = TK_NONE;
    /*
     punctuator: one of
     [ ] ( ) { } . ->
     ++ -- & * + - ~ !
     / % << >> < > <= >= == != ^ | && ||
     ? : :: ; ...
     = *= /= %= += -= <<= >>= &= ^= |=
     , # ##
     <: :> <% %> %: %:%:
    */
    switch (stream->current[0])
    {
    case '\\':
        type = '\\';
        stream_match(stream);
        break;
    case '[':
        type = '[';
        stream_match(stream);
        break;
    case ']':
        type = ']';
        stream_match(stream);
        break;
    case '(':
        type = '(';
        stream_match(stream);
        break;
    case ')':
        type = ')';
        stream_match(stream);
        break;
    case '{':
        type = '{';
        stream_match(stream);
        break;
    case '}':
        type = '}';
        stream_match(stream);
        break;
    case ';':
        type = ';';
        stream_match(stream);
        break;
    case ',':
        type = ',';
        stream_match(stream);
        break;
    case '!':
        type = '!';
        stream_match(stream);
        if (stream->current[0] == '=')
        {
            type = '!=';
            stream_match(stream);
        }
        break;
    case ':':
        type = ':';
        stream_match(stream);
        if (stream->current[0] == ':')
        {
            type = '::';
            stream_match(stream);
        }
        break;
    case '~':
        type = '~';
        stream_match(stream);
        break;
    case '?':
        type = '?';
        stream_match(stream);
        break;
    case '/':
        type = '/';
        stream_match(stream);
        break;
    case '*':
        type = '*';
        stream_match(stream);
        break;
    case '%':
        type = '%';
        stream_match(stream);
        break;
    case '-':
        type = '-';
        stream_match(stream);
        if (stream->current[0] == '>')
        {
            type = '->';
            stream_match(stream);
        }
        else if (stream->current[0] == '-')
        {
            type = '--';
            stream_match(stream);
        }
        else if (stream->current[0] == '=')
        {
            type = '-=';
            stream_match(stream);
        }
        break;
    case '|':
        type = '|';
        stream_match(stream);
        if (stream->current[0] == '|')
        {
            type = '||';
            stream_match(stream);
        }
        else if (stream->current[0] == '=')
        {
            type = '|=';
            stream_match(stream);
        }
        break;
    case '+':
        type = '+';
        stream_match(stream);
        if (stream->current[0] == '+')
        {
            type = '++';
            stream_match(stream);
        }
        else if (stream->current[0] == '=')
        {
            type = '+=';
            stream_match(stream);
        }
        break;
    case '=':
        type = '=';
        stream_match(stream);
        if (stream->current[0] == '=')
        {
            type = '==';
            stream_match(stream);
        }
        break;
    case '^':
        type = '^';
        stream_match(stream);
        if (stream->current[0] == '=')
        {
            type = '^=';
            stream_match(stream);
        }
        break;
    case '&':
        type = '&';
        stream_match(stream);
        if (stream->current[0] == '&')
        {
            type = '&&';
            stream_match(stream);
        }
        else if (stream->current[0] == '=')
        {
            type = '&=';
            stream_match(stream);
        }
        break;
    case '>':
        type = '>';
        stream_match(stream);
        if (stream->current[0] == '>')
        {
            type = '>>';
            stream_match(stream);
        }
        else if (stream->current[0] == '=')
        {
            type = '>=';
            stream_match(stream);
        }

        break;
    case '<':
        type = '<';
        stream_match(stream);
        if (stream->current[0] == '<')
        {
            type = '<<';
            stream_match(stream);
        }
        else if (stream->current[0] == '=')
        {
            type = '<=';
            stream_match(stream);
        }
        break;
    case '#':
        type = '#';
        stream_match(stream);
        if (stream->current[0] == '#')
        {
            type = '##';
            stream_match(stream);
        }
        break;
    case '.':
        type = '.';
        stream_match(stream);
        if (stream->current[0] == '.' && stream->current[1] == '.')
        {
            type = '...';
            stream_match(stream);
            stream_match(stream);
        }
        break;
    }
    return type;
}


struct token* new_token(const char* lexeme_head, const char* lexeme_tail, enum token_type type)
{
    struct token* pNew = calloc(1, sizeof * pNew);
    size_t sz = lexeme_tail - lexeme_head;
    pNew->lexeme = calloc(sz + 1, sizeof(char));
    pNew->type = type;
    strncpy(pNew->lexeme, lexeme_head, sz);
    return pNew;
}

struct token* identifier(struct stream* stream)
{
    const char* start = stream->current;
    stream_match(stream);
    /*
    identifier:
      identifier-nondigit
      identifier identifier-nondigit
      identifier digit

    identifier-nondigit:
      nondigit
      universal-character-name
      other implementation-defined characters
    */
    while (is_nondigit(stream) || is_digit(stream))
    {
        stream_match(stream);
    }

    struct token* pNew = new_token(start, stream->current, TK_IDENTIFIER);


    return pNew;
}




bool first_of_character_constant(struct stream* stream)
{
    return stream->current[0] == '\'' ||
        (stream->current[0] == 'u' && stream->current[1] == '8' && stream->current[2] == '\'') ||
        (stream->current[0] == 'u' && stream->current[1] == '\'') ||
        (stream->current[0] == 'U' && stream->current[1] == '\'') ||
        (stream->current[0] == 'L' && stream->current[1] == '\'');
}

struct token* character_constant(struct stream* stream)
{
    const char* start = stream->current;

    /*encoding_prefix_opt*/
    if (stream->current[0] == 'u')
    {
        stream_match(stream);
        if (stream->current[1] == '8')
            stream_match(stream);
    }
    else if (stream->current[0] == 'U' ||
        stream->current[0] == 'L')
    {
        stream_match(stream);
    }


    stream_match(stream); //"


    while (stream->current[0] != '\'')
    {
        if (stream->current[0] == '\\')
        {
            stream_match(stream);
            stream_match(stream);
        }
        else
            stream_match(stream);
    }
    stream_match(stream);

    if (stream->current - start > 6)
    {
        //warning: character constant too long for its type
    }
    struct token* pNew = new_token(start, stream->current, TK_CHAR_CONSTANT);

    return pNew;
}

bool first_of_string_literal(struct stream* stream)
{
    /*
    string-literal:
    encoding_prefix_opt " s-char-sequenceopt "

    encoding_prefix:
    u8
    u
    U
    L
    */

    return stream->current[0] == '"' ||
        (stream->current[0] == 'u' && stream->current[1] == '8' && stream->current[2] == '"') ||
        (stream->current[0] == 'u' && stream->current[1] == '"') ||
        (stream->current[0] == 'U' && stream->current[1] == '"') ||
        (stream->current[0] == 'L' && stream->current[1] == '"');
}

struct token* string_literal(struct stream* stream, struct error* error)
{
    struct token* pNew = NULL;

    const char* start = stream->current;
    int start_line = stream->line;
    int start_col = stream->col;

    try
    {
        /*encoding_prefix_opt*/
        if (stream->current[0] == 'u')
        {
            stream_match(stream);
            if (stream->current[0] == '8')
                stream_match(stream);
        }
        else if (stream->current[0] == 'U' ||
            stream->current[0] == 'L')
        {
            stream_match(stream);
        }


        stream_match(stream); //"


        while (stream->current[0] != '"')
        {
            if (stream->current[0] == '\0' ||
                stream->current[0] == '\n')
            {
                seterror(error, "%s(%d:%d) missing terminating \" character",
                    stream->source,
                    start_line,
                    start_col);
                throw;
            }

            if (stream->current[0] == '\\')
            {
                stream_match(stream);
                stream_match(stream);
            }
            else
                stream_match(stream);
        }
        stream_match(stream);
        pNew = new_token(start, stream->current, TK_STRING_LITERAL);
    }
    catch
    {
    }

    return pNew;
}

struct token* ppnumber(struct stream* stream)
{
    /*
     pp-number:
      digit
      . digit
      pp-number identifier-continue
      pp-number ’ digit
      pp-number ’ nondigit
      pp-number e sign
      pp-number E sign
      pp-number p sign
      pp-number P sign
      pp-number .
    */

    const char* start = stream->current;
    if (is_digit(stream))
    {
        stream_match(stream);//digit
    }
    else if (stream->current[0] == '.')
    {
        stream_match(stream); //.
        stream_match(stream); //digit
    }
    else
    {
        assert(false);
    }
    for (;;)
    {
        if (is_digit(stream))
        {
            stream_match(stream);//digit
        }
        else if (is_nondigit(stream))
        {
            stream_match(stream);//nondigit
        }
        else if (stream->current[0] == '\'')
        {
            //digit separators c23
            stream_match(stream);
            if (is_digit(stream))
            {
                stream_match(stream);
            }
            else if (is_nondigit(stream))
            {
                stream_match(stream);
            }
            else
            {
                assert(false);
                break;
            }
        }
        else if (stream->current[0] == 'e' ||
            stream->current[0] == 'E' ||
            stream->current[0] == 'p' ||
            stream->current[0] == 'P')
        {
            stream_match(stream);//e E  p P
            stream_match(stream);//sign
        }
        else if (stream->current[0] == '\'')
        {
            //digit separators dentro preprocessador
            stream_match(stream);//'
            stream_match(stream);//.
        }
        else if (stream->current[0] == '.')
        {
            stream_match(stream);//.            
        }
        else
        {
            break;
        }
    }
    struct token* pNew = new_token(start, stream->current, TK_PPNUMBER);
    return pNew;
}

struct token_list embed_tokenizer(const char* filename_opt, int level, enum token_flags addflags, struct error* error)
{
    struct token_list list = { 0 };

    FILE* file = NULL;

    bool bFirst = true;
    int line = 1;
    int col = 1;
    int count = 0;
    try
    {
#ifndef MOCKFILES
        file = fopen(filename_opt, "rb");
        if (file == NULL)
        {
            seterror(error, "file '%s' not found", filename_opt);
            throw;
        }
#else
        /*web versions only text files that are included*/
        const char* textfile = readfile(filename_opt);
        if (textfile == NULL)
        {
            seterror(error, "file '%s' not found", filename_opt);
            throw;
        }

        const char* pch = textfile;
#endif

        unsigned char ch;
#ifndef MOCKFILES
        while (fread(&ch, 1, 1, file))
        {
#else
        while (*pch)
        {
            ch = *pch;
            pch++;
#endif                    
            if (bFirst)
            {
                bFirst = false;
            }
            else
            {
                char b[] = ",";
                struct token* pNew = new_token(b, &b[1], TK_COMMA);
                pNew->flags |= addflags;
                pNew->level = level;
                pNew->pFile = NULL;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);

                if (count > 0 && count % 25 == 0)
                {
                    /*new line*/
                    char newline[] = "\n";
                    struct token* pNew3 = new_token(newline, &newline[1], TK_NEWLINE);
                    pNew3->level = level;
                    pNew3->pFile = NULL;
                    pNew3->line = line;
                    pNew3->col = col;
                    token_list_add(&list, pNew3);
                }
            }

            char buffer[30];
            int c = snprintf(buffer, sizeof buffer, "%d", (int)ch);

            struct token* pNew = new_token(buffer, &buffer[c], TK_PPNUMBER);
            pNew->flags |= addflags;
            pNew->level = level;
            pNew->pFile = NULL;
            pNew->line = line;
            pNew->col = col;
            token_list_add(&list, pNew);

            
            count++;
        }
#ifdef MOCKFILES   
        free(textfile);
#endif
    }
    catch
    {
    }

    /*new line*/
    char newline[] = "\n";
    struct token* pNew = new_token(newline, &newline[1], TK_NEWLINE);
    pNew->level = level;
    pNew->pFile = NULL;
    pNew->line = line;
    pNew->col = col;
    token_list_add(&list, pNew);

    if (file) fclose(file);



    assert(list.head != NULL);
    return list;
}

struct token_list tokenizer(const char* text, const char* filename_opt, int level, enum token_flags addflags, struct error* error)
{
    struct token_list list = { 0 };
    if (text == NULL)
    {
        return list;
    }

    struct stream stream =
    {
        .col = 1,
        .line = 1,
        .source = text,
        .current = text
    };

    try
    {
        struct token* pFirst = NULL;
        if (filename_opt != NULL)
        {
            const char* bof = "";
            pFirst = new_token(bof, bof + 1, TK_BEGIN_OF_FILE);
            pFirst->level = level;
            pFirst->lexeme = strdup(filename_opt);
            token_list_add(&list, pFirst);
        }


        //struct token* pCurrent = pFirst;
        bool bNewLine = true;
        bool bHasSpace = false;
        while (1)
        {
            const int line = stream.line;
            const int col = stream.col;

            if (stream.current[0] == '\0')
            {
                stream_match(&stream);
                break;
            }
            if (is_digit(&stream) ||
                (stream.current[0] == '.' && isdigit(stream.current[0])))
            {
                struct token* pNew = ppnumber(&stream);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;
                continue;
            }

            /*
             Tem que vir antes identifier
            */
            if (first_of_string_literal(&stream))
            {
                struct token* pNew = string_literal(&stream, error);
                if (pNew == NULL)
                    throw;
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);;
                bNewLine = false;
                bHasSpace = false;
                continue;
            }

            if (first_of_character_constant(&stream))
            {
                struct token* pNew = character_constant(&stream);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;
                continue;
            }

            if (is_nondigit(&stream))
            {
                struct token* pNew = identifier(&stream);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;
                continue;
            }
            if (stream.current[0] == ' ' || stream.current[0] == '\t')
            {
                const char* start = stream.current;
                while (stream.current[0] == ' ' ||
                    stream.current[0] == '\t')
                {
                    stream_match(&stream);
                }
                struct token* pNew = new_token(start, stream.current, TK_BLANKS);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                /*bNewLine = false;*/ //deixa assim
                bHasSpace = true;
                continue;
            }
            if (stream.current[0] == '/' &&
                stream.current[1] == '/')
            {
                const char* start = stream.current;
                stream_match(&stream);
                stream_match(&stream);
                //line comment
                while (stream.current[0] != '\n')
                {
                    stream_match(&stream);
                }
                struct token* pNew = new_token(start, stream.current, TK_LINE_COMMENT);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = true;
                bHasSpace = false;
                continue;
            }
            if (stream.current[0] == '/' &&
                stream.current[1] == '*')
            {
                const char* start = stream.current;
                stream_match(&stream);
                stream_match(&stream);
                //line comment
                for (;;)
                {
                    if (stream.current[0] == '*' && stream.current[1] == '/')
                    {
                        stream_match(&stream);
                        stream_match(&stream);
                        break;
                    }
                    else if (stream.current[0] == '\0')
                    {
                        seterror(error, "missing end of comment");
                        break;
                    }
                    else
                    {
                        stream_match(&stream);
                    }
                }
                struct token* pNew = new_token(start, stream.current, TK_COMENT);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;
                continue;
            }
            if (bNewLine && stream.current[0] == '#')
            {
                const char* start = stream.current;
                stream_match(&stream);
                struct token* pNew = new_token(start, stream.current, '#');
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                pNew->type = TK_PREPROCESSOR_LINE;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;
                continue;
            }


            if (stream.current[0] == '\n' || stream.current[0] == '\r')
            {
                if (stream.current[0] == '\r' && stream.current[1] == '\n')
                {
                    stream_match(&stream);
                    stream_match(&stream);
                }
                else
                {
                    stream_match(&stream);
                }
                char  newline[] = "\n";
                struct token* pNew = new_token(newline, newline + 1, TK_NEWLINE);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = true;
                bHasSpace = false;
                continue;
            }
            const char* start = stream.current;
            enum token_type t = is_punctuator(&stream);
            if (t != TK_NONE)
            {

                struct token* pNew = new_token(start, stream.current, t);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;
                continue;
            }
            else
            {
                stream_match(&stream);
                struct token* pNew = new_token(start, stream.current, ANY_OTHER_PP_TOKEN);
                pNew->flags |= bHasSpace ? TK_FLAG_HAS_SPACE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= bNewLine ? TK_FLAG_HAS_NEWLINE_BEFORE : TK_FLAG_NONE;
                pNew->flags |= addflags;

                pNew->level = level;
                pNew->pFile = pFirst;
                pNew->line = line;
                pNew->col = col;
                token_list_add(&list, pNew);
                bNewLine = false;
                bHasSpace = false;

                //            stream_print_line(&stream);
                            //printf("%s (%d, %d) invalid token ? '%c' %d\n",
                              //     filename,
                                //   line,
                                  // col,
                                   //stream.current[0],
                                   //(int)stream.current[0]);

                continue;
            }


            break;
        }
    }
    catch
    {
    }

    assert(list.head != NULL);
    return list;
}


bool fread2(void* buffer, size_t size, size_t count, FILE * stream, size_t * sz)
{
    *sz = 0;//out
    bool result = false;
    size_t n = fread(buffer, size, count, stream);
    if (n == count)
    {
        *sz = n;
        result = true;
    }
    else if (n < count)
    {
        if (feof(stream))
        {
            *sz = n;
            result = true;
        }
    }
    return result;
}


bool preprocessor_token_ahead_is_identifier(struct token* p, const char* lexeme);
struct token_list group_part(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error);
struct token_list group_opt(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error)
{
    /*
      group:
       group-part
       group group-part
    */
    struct token_list r = { 0 };
    try
    {

        if (token_list_is_empty(inputList))
        {
            return r;
        }
        while (!token_list_is_empty(inputList))
        {

            if (inputList->head->type == TK_PREPROCESSOR_LINE &&
                (preprocessor_token_ahead_is_identifier(inputList->head, "endif") ||
                    preprocessor_token_ahead_is_identifier(inputList->head, "else") ||
                    preprocessor_token_ahead_is_identifier(inputList->head, "elif") ||
                    preprocessor_token_ahead_is_identifier(inputList->head, "elifdef") ||
                    preprocessor_token_ahead_is_identifier(inputList->head, "elifndef")))
            {
                /*follow of group-part*/
                break;
            }
            else
            {
                struct token_list r2 = group_part(ctx, inputList, bActive, level, error);
                token_list_append_list(&r, &r2);
                if (error->code) throw;
            }
        }
    }
    catch
    {
    }

    return r;
}

bool is_parser_token(struct token* p)
{
    return p->type != TK_COMENT &&
        p->type != TK_BLANKS &&
        p->type != TK_LINE_COMMENT &&
        p->type != TK_NEWLINE;
}

bool is_never_final(enum token_type type)
{
    return type == TK_BEGIN_OF_FILE ||
        type == TK_BLANKS ||
        type == TK_LINE_COMMENT ||
        type == TK_COMENT ||
        type == TK_PLACEMARKER ||
        type == TK_NEWLINE;
}



enum token_type is_keyword(const char* text);






struct token* preprocessor_look_ahead_core(struct token* p)
{
    if (p->next == NULL)
    {
        return NULL;
    }
    struct token* pCurrent = p->next;
    if (pCurrent == NULL)
        return NULL;
    while (pCurrent &&
        (pCurrent->type == TK_BLANKS ||
            pCurrent->type == TK_PLACEMARKER ||
            pCurrent->type == TK_LINE_COMMENT ||
            pCurrent->type == TK_COMENT))
    {
        pCurrent = pCurrent->next;
    }
    return pCurrent;
}

bool preprocessor_token_ahead_is(struct token* p, enum token_type t)
{
    struct token* pA = preprocessor_look_ahead_core(p);
    if (pA != NULL && pA->type == t)
        return true;
    return false;
}

bool preprocessor_token_previous_is(struct token* p, enum token_type t)
{
    if (p == NULL)
    {
        return false;
    }
    struct token* pCurrent = p->prev;
    if (pCurrent == NULL)
        return false;
    while (pCurrent &&
        (pCurrent->type == TK_BLANKS ||
            pCurrent->type == TK_LINE_COMMENT ||
            pCurrent->type == TK_PLACEMARKER ||
            pCurrent->type == TK_COMENT))
    {
        pCurrent = pCurrent->prev;
    }
    return (pCurrent && pCurrent->type == t);
}

bool preprocessor_token_ahead_is_identifier(struct token* p, const char* lexeme)
{
    assert(p != NULL);
    struct token* pA = preprocessor_look_ahead_core(p);
    if (pA != NULL && pA->type == TK_IDENTIFIER)
    {
        return strcmp(pA->lexeme, lexeme) == 0;
    }
    return false;
}

void skip_blanks_level(struct token_list* dest, struct token_list* inputList, int level)
{
    while (inputList->head &&
        token_is_blank(inputList->head))
    {
        if (INCLUDE_ALL || level == 0)
            token_list_add(dest, token_list_pop_front(inputList));
        else
            token_list_pop_front(inputList); //deletar
    }
}

void skip_blanks(struct token_list* dest, struct token_list* inputList)
{
    while (token_is_blank(inputList->head))
    {
        token_list_add(dest, token_list_pop_front(inputList));
    }
}

void prematch_level(struct token_list* dest, struct token_list* inputList, int level)
{
    if (INCLUDE_ALL || level == 0)
        token_list_add(dest, token_list_pop_front(inputList));
    else
        token_list_pop_front(inputList);
}

void prematch(struct token_list* dest, struct token_list* inputList)
{
    token_list_add(dest, token_list_pop_front(inputList));
}
struct token_list pp_tokens_opt(struct preprocessor_ctx* ctx, struct token_list* inputList, int level);

struct token_list process_defined(struct preprocessor_ctx* ctx, struct token_list* inputList, struct error* error)
{
    struct token_list r = { 0 };

    try
    {
        while (inputList->head != NULL)
        {
            if (inputList->head->type == TK_IDENTIFIER &&
                strcmp(inputList->head->lexeme, "defined") == 0)
            {
                token_list_pop_front(inputList);
                skip_blanks(&r, inputList);

                bool bHasParentesis = false;
                if (inputList->head->type == '(')
                {
                    token_list_pop_front(inputList);
                    bHasParentesis = true;
                }

                skip_blanks(&r, inputList);



                struct macro* pMacro = find_macro(ctx, inputList->head->lexeme);
                struct token* pNew = token_list_pop_front(inputList);
                pNew->type = TK_PPNUMBER;
                free(pNew->lexeme);
                if (pMacro)
                {
                    pNew->lexeme = strdup("1");
                }
                else
                {
                    pNew->lexeme = strdup("0");
                }

                token_list_add(&r, pNew);

                if (bHasParentesis)
                {
                    if (inputList->head->type != ')')
                    {
                        pre_seterror_with_token(ctx, inputList->head, "missing )");
                        throw;
                    }
                    token_list_pop_front(inputList);
                }


            }
            else if (inputList->head->type == TK_IDENTIFIER &&
                strcmp(inputList->head->lexeme, "__has_include") == 0)
            {
                token_list_pop_front(inputList); //pop __has_include
                skip_blanks(&r, inputList);
                token_list_pop_front(inputList); //pop (
                skip_blanks(&r, inputList);


                char path[100] = { 0 };

                if (inputList->head->type == TK_STRING_LITERAL)
                {
                    strcat(path, inputList->head->lexeme);
                    token_list_pop_front(inputList); //pop "file"
                }
                else
                {
                    token_list_pop_front(inputList); //pop <

                    while (inputList->head->type != '>')
                    {
                        strcat(path, inputList->head->lexeme);
                        token_list_pop_front(inputList); //pop (
                    }
                    token_list_pop_front(inputList); //pop >					
                }

                char fullpath[300] = { 0 };



                bool bAlreadyIncluded = false;
                char* s = find_and_read_include_file(ctx, path, fullpath, &bAlreadyIncluded);


                
                bool bHasInclude = s != NULL;
                free(s);

                struct token* pNew = calloc(1, sizeof * pNew);
                pNew->type = TK_PPNUMBER;
                free(pNew->lexeme);
                pNew->lexeme = strdup(bHasInclude ? "1" : "0");
                pNew->flags |= TK_FLAG_FINAL;

                token_list_add(&r, pNew);
                token_list_pop_front(inputList); //pop )
            }
            else if (inputList->head->type == TK_IDENTIFIER &&
                strcmp(inputList->head->lexeme, "__has_c_attribute") == 0)
            {
                token_list_pop_front(inputList); //pop __has_include
                skip_blanks(&r, inputList);
                token_list_pop_front(inputList); //pop (
                skip_blanks(&r, inputList);


                char path[100] = { 0 };
                while (inputList->head->type != ')')
                {
                    strcat(path, inputList->head->lexeme);
                    token_list_pop_front(inputList); //pop (
                }
                token_list_pop_front(inputList); //pop >					


                //TODO ver se existe criar unit test
                assert(false);
                //bool bAlreadyIncluded = false;
                //char* content = find_and_read_file(ctx, path + 1, path, &bAlreadyIncluded);
                bool bHas_C_Attribute = false;
                //free(content);

                struct token* pNew = calloc(1, sizeof * pNew);
                pNew->type = TK_PPNUMBER;
                free(pNew->lexeme);
                pNew->lexeme = strdup(bHas_C_Attribute ? "1" : "0");
                pNew->flags |= TK_FLAG_FINAL;

                token_list_add(&r, pNew);
                token_list_pop_front(inputList); //pop )
            }
            else
            {
                token_list_add(&r, token_list_pop_front(inputList));
            }
        }
    }
    catch
    {
    }

    return r;
}

struct token_list process_identifiers(struct preprocessor_ctx* ctx, struct token_list* list)
{
    assert(!token_list_is_empty(list));

    struct token_list list2 = { 0 };


    while (list->head != NULL)
    {
        if (list->head->type == TK_IDENTIFIER)
        {

            struct macro* pMacro = find_macro(ctx, list->head->lexeme);
            struct token* pNew = token_list_pop_front(list);
            pNew->type = TK_PPNUMBER;

            if (pMacro)
            {
                free(pNew->lexeme);
                pNew->lexeme = strdup("1");
            }
            else
            {
                /*
                * after all replacements due to macro expansion and
                  evaluations of defined macro expressions, has_include expressions, and has_c_attribute expressions
                  have been performed, all remaining identifiers other than true (including those lexically identical
                  to keywords such as false) are replaced with the pp-number 0, true is replaced with pp-number
                  1, and then each preprocessing token is converted into a token.
                */
                if (strcmp(pNew->lexeme, "true") == 0)
                {
                    pNew->lexeme[0] = '1';
                    pNew->lexeme[1] = '\0';
                }
                else if (strcmp(pNew->lexeme, "false") == 0)
                {
                    pNew->lexeme[0] = '0';
                    pNew->lexeme[1] = '\0';
                }
                else
                {
                    free(pNew->lexeme);
                    pNew->lexeme = strdup("0");
                }
            }
            token_list_add(&list2, pNew);
        }
        else
        {
            token_list_add(&list2, token_list_pop_front(list));
        }
    }
    assert(!token_list_is_empty(&list2));
    return list2;
}

struct token_list IgnorePreprocessorLine(struct token_list* inputList)
{
    struct token_list r = { 0 };
    while (inputList->head->type != TK_NEWLINE)
    {
        token_list_add(&r, token_list_pop_front(inputList));
    }
    return r;
}

//todo passar lista para reotnro
long long preprocessor_constant_expression(struct preprocessor_ctx* ctx,
    struct token_list* outputList,
    struct token_list* inputList,
    int level,
    struct error* error)
{
    ctx->bConditionalInclusion = true;
    struct token_list r = { 0 };
    while (inputList->head && inputList->head->type != TK_NEWLINE)
    {
        token_list_add(&r, token_list_pop_front(inputList));
    }
    *outputList = r;


    struct token_list list1 = copy_replacement_list(&r);
    //printf("\n");
    //print_list(&list1);
    //printf("\n");



    int flags = ctx->flags;
    ctx->flags |= preprocessor_ctx_flags_only_final;

    /*defined X  por exemplo é mantido sem ser expandido*/

    struct token_list list2 = preprocessor(ctx, &list1, 1, error);
    ctx->flags = flags;
    //printf("apos preprocess\n");
    //print_list(&list2);
    //printf("\n");

    /*aonde defined has_c_aatribute sao transformados em constantes*/
    struct token_list list3 = process_defined(ctx, &list2, error);

    //printf("apos remove defined\n");
    //print_list(&list3);
    //printf("\n");

    struct token_list list4 = process_identifiers(ctx, &list3);

    //printf("apos remover identificadores restantes\n");
    //print_list(&list4);
    //printf("\n");

    assert(list4.head != NULL);

    struct preprocessor_ctx pre_ctx = { 0 };
    //struct parser_ctx parser_ctx = { 0 };
    pre_ctx.inputList = list4;
    pre_ctx.current = list4.head;
    //pre_skip_blanks(&parser_ctx);

    long long value = 0;
    if (pre_constant_expression(&pre_ctx, error, &value) != 0)
    {
        assert(false);
        //TODO error
    }

    ctx->bConditionalInclusion = false;
    return value;
}

void match_level(struct token_list* dest, struct token_list* inputList, int level)
{
    if (INCLUDE_ALL || level == 0)
        token_list_add(dest, token_list_pop_front(inputList));
    else
        token_list_pop_front(inputList); //deletar
}


int match_token_level(struct token_list* dest, struct token_list* inputList, enum token_type type, int level,
    struct preprocessor_ctx* ctx, struct error* error)
{
    try
    {
        if (inputList->head == NULL ||
            inputList->head->type != type)
        {
            if (type == TK_NEWLINE && inputList->head == NULL)
            {
                //vou aceitar final de arquivo como substituro do endline
                //exemplo #endif sem quebra de linha
            }
            else
            {
                if (inputList->head)
                    pre_seterror_with_token(ctx, inputList->head, "expected token %s got %s\n", get_token_name(type), get_token_name(inputList->head->type));
                else
                    pre_seterror_with_token(ctx, dest->tail, "expected EOF \n");

                throw;
            }
        }
        if (inputList->head != NULL)
        {
            if (INCLUDE_ALL || level == 0)
                token_list_add(dest, token_list_pop_front(inputList));
            else
                token_list_pop_front(inputList); //deletar
        }
    }
    catch
    {
    }
    return error->code;
}


struct token_list if_group(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, bool* pbIfResult, struct error* error)
{
    *pbIfResult = 0; //out

    struct token_list r = { 0 };
    try
    {
        /*
         if-group:
           # if constant-expression new-line group_opt
           # ifdef identifier new-line group_opt
           # ifndef identifier new-line group_opt
        */
        match_token_level(&r, inputList, TK_PREPROCESSOR_LINE, level, ctx, error);
        skip_blanks_level(&r, inputList, level);
        assert(inputList->head->type == TK_IDENTIFIER);
        if (strcmp(inputList->head->lexeme, "ifdef") == 0)
        {
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //ifdef
            skip_blanks_level(&r, inputList, level);
            if (bActive)
            {
                struct macro* pMacro = find_macro(ctx, inputList->head->lexeme);
                *pbIfResult = (pMacro != NULL) ? 1 : 0;
                //printf("#ifdef %s (%s)\n", inputList->head->lexeme, *pbIfResult ? "true" : "false");
            }
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
            skip_blanks_level(&r, inputList, level);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else if (strcmp(inputList->head->lexeme, "ifndef") == 0)
        {
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //ifndef
            skip_blanks_level(&r, inputList, level);
            if (bActive)
            {
                struct macro* pMacro = find_macro(ctx, inputList->head->lexeme);
                *pbIfResult = (pMacro == NULL) ? 1 : 0;
            }
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
            skip_blanks_level(&r, inputList, level);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else if (strcmp(inputList->head->lexeme, "if") == 0)
        {
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //if
            skip_blanks_level(&r, inputList, level);
            if (bActive)
            {
                struct token_list r0 = { 0 };
                *pbIfResult = preprocessor_constant_expression(ctx, &r0, inputList, level, error);
                token_list_append_list(&r, &r0);
            }
            else
            {
                struct token_list r0 = IgnorePreprocessorLine(inputList);
                token_list_append_list(&r, &r0);
            }
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else
        {

            pre_seterror_with_token(ctx, inputList->head, "unexpected");
            throw;
        }
        struct token_list r2 = group_opt(ctx, inputList, bActive && *pbIfResult, level, error);
        token_list_append_list(&r, &r2);
    }
    catch
    {
    }

    return r;
}

struct token_list elif_group(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, bool* pElifResult, struct error* error)
{
    *pElifResult = 0; //out

    struct token_list r = { 0 };
    /*
     elif-group:
      # elif constant-expression new-line group_opt

      C23
      # elifdef identifier new-line group_opt
      # elifndef identifier new-line group_opt
    */
    match_token_level(&r, inputList, TK_PREPROCESSOR_LINE, level, ctx, error);
    skip_blanks(&r, inputList);
    int result = 0;
    if (strcmp(inputList->head->lexeme, "elif") == 0)
    {
        match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
        skip_blanks(&r, inputList);

        if (bActive)
        {
            struct token_list r0 = { 0 };
            result = preprocessor_constant_expression(ctx, &r0, inputList, level, error);

            token_list_append_list(&r, &r0);


        }
        else
        {
            IgnorePreprocessorLine(inputList);
        }
    }
    else if (strcmp(inputList->head->lexeme, "elifdef") == 0)
    {
        match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
        skip_blanks(&r, inputList);

        if (bActive)
        {
            result = (hashmap_find(&ctx->macros, inputList->head->lexeme) != NULL) ? 1 : 0;
        }
        match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
    }
    else if (strcmp(inputList->head->lexeme, "elifndef") == 0)
    {
        match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
        skip_blanks(&r, inputList);

        if (bActive)
        {
            result = (hashmap_find(&ctx->macros, inputList->head->lexeme) == NULL) ? 1 : 0;
        }
        match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
    }
    *pElifResult = (result != 0);
    skip_blanks(&r, inputList);
    match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
    struct token_list r2 = group_opt(ctx, inputList, bActive && *pElifResult, level, error);
    token_list_append_list(&r, &r2);
    return r;
}

struct token_list elif_groups(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, bool* pbelifResult, struct error* error)
{
    struct token_list r = { 0 };
    /*
    elif-groups:
      elif-group
      elif-groups elif-group
    */
    bool bAlreadyFoundElifTrue = false;
    bool bElifResult = false;
    struct token_list r2 = elif_group(ctx, inputList, bActive, level, &bElifResult, error);
    token_list_append_list(&r, &r2);
    if (bElifResult)
        bAlreadyFoundElifTrue = true;
    if (inputList->head->type == TK_PREPROCESSOR_LINE &&
        preprocessor_token_ahead_is_identifier(inputList->head, "elif") ||
        preprocessor_token_ahead_is_identifier(inputList->head, "elifdef") ||
        preprocessor_token_ahead_is_identifier(inputList->head, "elifndef"))
    {
        /*
          Depois que acha 1 true bAlreadyFoundElifTrue os outros sao false.
        */
        struct token_list r3 = elif_groups(ctx, inputList, bActive && !bAlreadyFoundElifTrue, level, &bElifResult, error);
        token_list_append_list(&r, &r3);
        if (bElifResult)
            bAlreadyFoundElifTrue = true;
    }
    /*
       Se algum dos elifs foi true retorna true
    */
    *pbelifResult = bAlreadyFoundElifTrue;
    return r;
}

struct token_list else_group(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error)
{
    /*
      else-group:
       # else new-line group_opt
    */

    struct token_list r = { 0 };
    match_token_level(&r, inputList, TK_PREPROCESSOR_LINE, level, ctx, error);
    skip_blanks_level(&r, inputList, level);

    match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //else
    skip_blanks_level(&r, inputList, level);
    match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);

    struct token_list r2 = group_opt(ctx, inputList, bActive, level, error);
    token_list_append_list(&r, &r2);

    return r;
}

struct token_list endif_line(struct preprocessor_ctx* ctx, struct token_list* inputList, int level, struct error* error)
{
    /*
     endif-line:
       # endif new-line
    */

    struct token_list r = { 0 };

    match_token_level(&r, inputList, TK_PREPROCESSOR_LINE, level, ctx, error); //#
    skip_blanks_level(&r, inputList, level);
    match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //endif
    skip_blanks_level(&r, inputList, level);
    match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);

    return r;
}

struct token_list if_section(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error)
{
    /*
     if-section:
       if-group elif-groups_opt else-group_opt endif-line
    */

    struct token_list r = { 0 };

    try
    {
        bool bIfResult = false;
        struct token_list r2 = if_group(ctx, inputList, bActive, level, &bIfResult, error);
        if (error->code) throw;

        token_list_append_list(&r, &r2);
        bool bElifResult = false;
        if (inputList->head->type == TK_PREPROCESSOR_LINE &&
            preprocessor_token_ahead_is_identifier(inputList->head, "elif") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "elifdef") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "elifndef"))
        {
            struct token_list r3 = elif_groups(ctx, inputList, bActive && !bIfResult, level, &bElifResult, error);
            token_list_append_list(&r, &r3);
        }
        if (inputList->head->type == TK_PREPROCESSOR_LINE &&
            preprocessor_token_ahead_is_identifier(inputList->head, "else"))
        {
            struct token_list r4 = else_group(ctx, inputList, bActive && !bIfResult && !bElifResult, level, error);
            token_list_append_list(&r, &r4);
        }

        if (error->code) throw;

        struct token_list r5 = endif_line(ctx, inputList, level, error);
        token_list_append_list(&r, &r5);
    }
    catch
    {
    }

    return r;
}

struct token_list identifier_list(struct preprocessor_ctx* ctx, struct macro* pMacro, struct token_list* inputList, int level, struct error* error)
{
    struct token_list r = { 0 };
    /*
      identifier-list:
      identifier
      identifier-list , identifier
    */
    skip_blanks(&r, inputList);
    struct macro_parameter* pMacroParameter = calloc(1, sizeof * pMacroParameter);
    pMacroParameter->name = strdup(inputList->head->lexeme);
    pMacro->pParameters = pMacroParameter;
    match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
    skip_blanks(&r, inputList);
    while (inputList->head->type == ',')
    {
        match_token_level(&r, inputList, ',', level, ctx, error);
        skip_blanks(&r, inputList);
        if (inputList->head->type == '...')
        {
            break;
        }
        pMacroParameter->next = calloc(1, sizeof * pMacroParameter);
        pMacroParameter = pMacroParameter->next;
        pMacroParameter->name = strdup(inputList->head->lexeme);
        match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);
        skip_blanks(&r, inputList);
    }
    return r;
}


struct token_list replacement_list(struct macro* pMacro, struct token_list* inputList, int level)
{
    struct token_list r = { 0 };
    while (inputList->head->type != TK_NEWLINE)
    {
        match_level(&r, inputList, level);
        if (inputList->head == NULL)
        {
            //terminou define sem quebra de linha
        }
    }
    assert(pMacro->replacementList.head == NULL);
    struct token_list copy = copy_replacement_list(&r);
    token_list_append_list(&pMacro->replacementList, &copy);
    return r;
}

struct token_list pp_tokens_opt(struct preprocessor_ctx* ctx, struct token_list* inputList, int level)
{
    struct token_list r = { 0 };
    while (inputList->head->type != TK_NEWLINE)
    {
        prematch_level(&r, inputList, level);
    }
    return r;
}

struct token_list control_line(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error)
{
    /*
        control-line:
            # include pp-tokens new-line
            # define identifier replacement-list new-line
            # define identifier ( identifier-list_opt ) replacement-list new-line
            # define identifier ( ... ) replacement-list new-line
            # define identifier lparen identifier-list , ... ) replacement-list new-line
            # undef identifier new-line
            # line pp-tokens new-line
            # error pp-tokensopt new-line
            # pragma pp-tokensopt new-line
            # new-line
    */

    struct token_list r = { 0 };

    try
    {

        if (!bActive)
        {
            //se nao esta ativo eh ingorado
            struct token_list r7 = pp_tokens_opt(ctx, inputList, level);
            token_list_append_list(&r, &r7);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
            return r;
        }

#ifdef _WIN32
        //char line[1000] = { 0 };
        //snprintf(line, sizeof line, "%s(%d,%d):\n", inputList->head->pFile->lexeme, inputList->head->line, inputList->head->col);
        //OutputDebugStringA(line);
#endif
        struct token* const ptoken = inputList->head;
        match_token_level(&r, inputList, TK_PREPROCESSOR_LINE, level, ctx, error);
        skip_blanks_level(&r, inputList, level);
        if (strcmp(inputList->head->lexeme, "include") == 0)
        {
            /*
              # include pp-tokens new-line
            */
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //include
            skip_blanks_level(&r, inputList, level);
            char path[100] = { 0 };

            if (inputList->head->type == TK_STRING_LITERAL)
            {
                strcat(path, inputList->head->lexeme);
                prematch_level(&r, inputList, level);
            }
            else
            {
                while (inputList->head->type != '>')
                {
                    strcat(path, inputList->head->lexeme);
                    prematch_level(&r, inputList, level);
                }
                strcat(path, inputList->head->lexeme);
                prematch_level(&r, inputList, level);
            }

            if (inputList->head)
            {
                while (inputList->head->type != TK_NEWLINE)
                {
                    prematch_level(&r, inputList, level);
                }
            }
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);

            char fullpath[300] = { 0 };


            path[strlen(path) - 1] = '\0';

            bool bAlreadyIncluded = false;
            char* content = find_and_read_include_file(ctx, path + 1, fullpath, &bAlreadyIncluded);
            if (content != NULL)
            {
                struct token_list list = tokenizer(content, fullpath, level + 1, TK_FLAG_NONE, error);
                free(content);

                struct token_list list2 = preprocessor(ctx, &list, level + 1, error);
                token_list_append_list(&r, &list2);
            }
            else
            {
                if (!bAlreadyIncluded)
                {
                    pre_seterror_with_token(ctx, r.tail, "file %s not found", path + 1);
                }
                else
                {
                    //pragma once..
                }
            }

        }
        else if (strcmp(inputList->head->lexeme, "embed") == 0)
        {
            struct token_list discard0 = { 0 };
            struct token_list* p_list = &r;
            if (ctx->options.target < LANGUAGE_C2X)
            {
                p_list = &discard0;
                
                free(ptoken->lexeme);
                ptoken->lexeme = strdup(" ");
                
            }

            /*
              C23
              # embed pp-tokens new-line
            */
            match_token_level(p_list, inputList, TK_IDENTIFIER, level, ctx, error); //embed
            skip_blanks_level(p_list, inputList, level);
            char path[100] = { 0 };

            if (inputList->head->type == TK_STRING_LITERAL)
            {
                strcat(path, inputList->head->lexeme);
                prematch_level(p_list, inputList, level);
            }
            else
            {
                while (inputList->head->type != '>')
                {
                    strcat(path, inputList->head->lexeme);
                    prematch_level(p_list, inputList, level);
                }
                strcat(path, inputList->head->lexeme);
                prematch_level(p_list, inputList, level);
            }

            if (inputList->head)
            {
                while (inputList->head->type != TK_NEWLINE)
                {
                    prematch_level(p_list, inputList, level);
                }
            }
            match_token_level(p_list, inputList, TK_NEWLINE, level, ctx, error);



            char fullpath[300] = { 0 };
            path[strlen(path) - 1] = '\0';

            snprintf(fullpath, sizeof(fullpath), "%s", path + 1);
            struct error localerror = { 0 };
            
            int nlevel = level;

            enum token_flags f = 0;
            if (ctx->options.target < LANGUAGE_C2X)
            {
                //we can see it
                f = TK_FLAG_FINAL;
            }
            else
            {
                f = TK_FLAG_FINAL;
                //we cannot see it just like include
                nlevel = nlevel + 1;
            }

            struct token_list list = embed_tokenizer(fullpath, nlevel, f, &localerror);
            if (localerror.code != 0)
            {
                pre_seterror_with_token(ctx, inputList->head, "embed error: %s", localerror.message);
            }

            token_list_append_list(&r, &list);
            token_list_destroy(&discard0);



        }
        else if (strcmp(inputList->head->lexeme, "define") == 0)
        {
            //TODO strcmp nao pode ser usado temos que criar uma funcao especial

            /*
             #de\
             fine A 1

            A
            */

            struct macro* pMacro = calloc(1, sizeof * pMacro);
            if (pMacro == NULL)
            {
                seterror(error, "out of memory");
                throw;
            }

            /*
                # define identifier                           replacement-list new-line
                # define identifier ( identifier-list_opt )    replacement-list new-line
                # define identifier ( ... )                   replacement-list new-line
                # define identifier ( identifier-list , ... ) replacement-list new-line
            */
            //p = preprocessor_match_identifier(p, bActive, level, false, "define");
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //define
            skip_blanks_level(&r, inputList, level);

            // printf("define %s\n%s : %d\n", inputList->head->lexeme, inputList->head->pFile->lexeme, inputList->head->line);


            if (hashmap_find(&ctx->macros, inputList->head->lexeme) != NULL)
            {
                //printf("warning: '%s' macro redefined at %s %d\n",
                  //     inputList->head->lexeme,
                    ///   inputList->head->pFile->lexeme,
                      // inputList->head->line);
            }


            hashmap_set(&ctx->macros, inputList->head->lexeme, &pMacro->type_id);
            pMacro->name = strdup(inputList->head->lexeme);


            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error); //nome da macro
            /*sem skip*/
            //p = preprocessor_match_token(p, bActive, level, false, IDENTIFIER); /*name*/
            if (inputList->head->type == '(')
            {

                pMacro->bIsFunction = true;


                match_token_level(&r, inputList, '(', level, ctx, error); //nome da macro
                skip_blanks_level(&r, inputList, level);
                if (inputList->head->type == '...')
                {
                    struct macro_parameter* pMacroParameter = calloc(1, sizeof * pMacroParameter);
                    pMacroParameter->name = strdup("__VA_ARGS__");
                    pMacro->pParameters = pMacroParameter;

                    // assert(false);
                    match_token_level(&r, inputList, '...', level, ctx, error); //nome da macro
                    skip_blanks_level(&r, inputList, level);
                    match_token_level(&r, inputList, ')', level, ctx, error); //nome da macro
                }
                else if (inputList->head->type == ')')
                {
                    match_token_level(&r, inputList, ')', level, ctx, error);
                    skip_blanks_level(&r, inputList, level);
                }
                else
                {
                    struct token_list r3 = identifier_list(ctx, pMacro, inputList, level, error);
                    token_list_append_list(&r, &r3);
                    skip_blanks_level(&r, inputList, level);
                    if (inputList->head->type == '...')
                    {
                        struct macro_parameter* pMacroParameter = calloc(1, sizeof * pMacroParameter);
                        pMacroParameter->name = strdup("__VA_ARGS__");
                        struct macro_parameter* pLast = pMacro->pParameters;
                        assert(pLast != NULL);
                        while (pLast->next)
                        {
                            pLast = pLast->next;
                        }
                        pLast->next = pMacroParameter;


                        match_token_level(&r, inputList, '...', level, ctx, error);
                    }
                    skip_blanks_level(&r, inputList, level);
                    match_token_level(&r, inputList, ')', level, ctx, error);
                }
            }
            else
            {
                pMacro->bIsFunction = false;
            }
            struct token_list r4 = replacement_list(pMacro, inputList, level);
            token_list_append_list(&r, &r4);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else if (strcmp(inputList->head->lexeme, "undef") == 0)
        {
            /*
             # undef identifier new-line
            */
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//undef
            skip_blanks_level(&r, inputList, level);
            struct type_tag_id* pNode = hashmap_remove(&ctx->macros, inputList->head->lexeme);
            assert(find_macro(ctx, inputList->head->lexeme) == NULL);
            if (pNode)
            {
                struct macro* pMacro = container_of(pNode, struct macro, type_id);
                delete_macro(pMacro);
                match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//undef
            }
            else
            {
                match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//undef
                /*no warnings*/
            }
            skip_blanks_level(&r, inputList, level);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else if (strcmp(inputList->head->lexeme, "line") == 0)
        {
            /*
               # line pp-tokens new-line
            */
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//line
            struct token_list r5 = pp_tokens_opt(ctx, inputList, level);
            token_list_append_list(&r, &r5);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else if (strcmp(inputList->head->lexeme, "error") == 0)
        {
            /*
              # error pp-tokensopt new-line
            */
            ctx->n_warnings++;
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//error
            struct token_list r6 = pp_tokens_opt(ctx, inputList, level);
            pre_error_warning_with_token(ctx, inputList->head, true);
            token_list_append_list(&r, &r6);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);


        }
        else if (strcmp(inputList->head->lexeme, "warning") == 0)
        {
            /*
              # warning pp-tokensopt new-line
            */
            ctx->n_warnings++;
            if (ctx->options.target < LANGUAGE_C2X)
            {
                /*insert comment before #*/
                free(ptoken->lexeme);
                ptoken->lexeme = strdup("//#");                                
            }
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//warning

            struct token_list r6 = pp_tokens_opt(ctx, inputList, level);
            pre_error_warning_with_token(ctx, inputList->head, false);
            token_list_append_list(&r, &r6);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
        else if (strcmp(inputList->head->lexeme, "pragma") == 0)
        {
            /*
              # pragma pp-tokensopt new-line
            */
            match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//pragma
            skip_blanks_level(&r, inputList, level);

            if (inputList->head->type == TK_IDENTIFIER)
            {
                if (strcmp(inputList->head->lexeme, "once") == 0)
                {
                    hashmap_set(&ctx->pragmaOnce, inputList->head->pFile->lexeme, (void*)1);
                    match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//pragma
                }
                else if (strcmp(inputList->head->lexeme, "expand") == 0)
                {
                    match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//pragma
                    skip_blanks_level(&r, inputList, level);

                    struct macro* pMacro = find_macro(ctx, inputList->head->lexeme);
                    if (pMacro)
                    {
                        pMacro->bExpand = true;
                    }

                    match_token_level(&r, inputList, TK_IDENTIFIER, level, ctx, error);//pragma

                }
            }

            struct token_list r7 = pp_tokens_opt(ctx, inputList, level);
            token_list_append_list(&r, &r7);
            match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
        }
    }
    catch
    {
    }

    return r;
}


struct token_list non_directive(struct preprocessor_ctx* ctx, struct token_list* inputList, int level, struct error* error)
{
    /*
      non-directive:
      pp-tokens new-line
     */
    struct token_list r = pp_tokens_opt(ctx, inputList, level);
    skip_blanks_level(&r, inputList, level);
    match_token_level(&r, inputList, TK_NEWLINE, level, ctx, error);
    return r;
}

struct MacroArgumentList collect_macro_arguments(struct preprocessor_ctx* ctx,
    struct macro* pMacro,
    struct token_list* inputList, int level, struct error* error)
{
    struct MacroArgumentList macroArgumentList = { 0 };
    try
    {
        assert(inputList->head->type == TK_IDENTIFIER); //nome da macro

        match_token_level(&macroArgumentList.tokens, inputList, TK_IDENTIFIER, level, ctx, error); //NOME DA MACRO
        if (!pMacro->bIsFunction)
        {
            //se nao eh funcao so faz isso e retorna o nome da macro
            return macroArgumentList;
        }

        struct macro_parameter* pCurrentParameter = pMacro->pParameters;
        int count = 1;
        skip_blanks(&macroArgumentList.tokens, inputList);
        match_token_level(&macroArgumentList.tokens, inputList, '(', level, ctx, error);
        skip_blanks(&macroArgumentList.tokens, inputList);
        if (inputList->head->type == ')')
        {
            if (pMacro->pParameters != NULL)
            {
                struct MacroArgument* pArgument = calloc(1, sizeof(struct MacroArgument));
                pArgument->name = strdup(pCurrentParameter->name);
                argument_list_add(&macroArgumentList, pArgument);
            }
            match_token_level(&macroArgumentList.tokens, inputList, ')', level, ctx, error);
            return macroArgumentList;
        }
        struct MacroArgument* pCurrentArgument = calloc(1, sizeof(struct MacroArgument));
        pCurrentArgument->name = strdup(pCurrentParameter->name);
        while (inputList->head != NULL)
        {
            if (inputList->head->type == '(')
            {
                count++;
                token_list_clone_and_add(&pCurrentArgument->tokens, inputList->head);
                match_token_level(&macroArgumentList.tokens, inputList, '(', level, ctx, error);
            }
            else if (inputList->head->type == ')')
            {
                count--;
                if (count == 0)
                {
                    match_token_level(&macroArgumentList.tokens, inputList, ')', level, ctx, error);
                    argument_list_add(&macroArgumentList, pCurrentArgument);
                    pCurrentParameter = pCurrentParameter->next;

                    if (pCurrentParameter != NULL)
                    {
                        if (strcmp(pCurrentParameter->name, "__VA_ARGS__") == 0)
                        {
                            //adicionamos este argumento como sendo vazio
                            pCurrentArgument = calloc(1, sizeof(struct MacroArgument));
                            pCurrentArgument->name = strdup(pCurrentParameter->name);
                            argument_list_add(&macroArgumentList, pCurrentArgument);
                        }
                        else
                        {
                            //tODO
                            pre_seterror_with_token(ctx, inputList->head, "too few arguments provided to function-like macro invocation\n");
                            error->code = 1;
                            throw;
                        }
                    }


                    break;
                }
                else
                {
                    token_list_clone_and_add(&pCurrentArgument->tokens, inputList->head);
                    match_token_level(&macroArgumentList.tokens, inputList, ')', level, ctx, error);
                }
            }
            else if (count == 1 && inputList->head->type == ',')
            {
                if (strcmp(pCurrentParameter->name, "__VA_ARGS__") == 0)
                {
                    token_list_clone_and_add(&pCurrentArgument->tokens, inputList->head);
                    match_token_level(&macroArgumentList.tokens, inputList, ',', level, ctx, error);
                }
                else //if (count == 1)
                {
                    match_token_level(&macroArgumentList.tokens, inputList, ',', level, ctx, error);
                    argument_list_add(&macroArgumentList, pCurrentArgument);
                    pCurrentArgument = NULL; /*tem mais?*/
                    pCurrentArgument = calloc(1, sizeof(struct MacroArgument));
                    pCurrentParameter = pCurrentParameter->next;
                    if (pCurrentParameter == NULL)
                    {
                        pre_seterror_with_token(ctx, inputList->head, "invalid args");
                        throw;
                    }
                    pCurrentArgument->name = strdup(pCurrentParameter->name);
                }



            }
            else
            {
                token_list_clone_and_add(&pCurrentArgument->tokens, inputList->head);
                prematch_level(&macroArgumentList.tokens, inputList, level);
                //token_list_add(&list, token_list_pop_front(inputList));
            }
        }
    }
    catch
    {
    }

    return macroArgumentList;
}

struct token_list expand_macro(struct preprocessor_ctx* ctx, struct MacroExpanded* pList, struct macro* pMacro, struct MacroArgumentList* arguments, int level, struct error* error);
struct token_list replacement_list_reexamination(struct preprocessor_ctx* ctx, struct MacroExpanded* pList, struct token_list* oldlist, int level, struct error* error);


struct token_list macro_copy_replacement_list(struct preprocessor_ctx* ctx, struct macro* pMacro, struct error* error);

/*#define hash_hash # ## #
#define mkstr(a) # a
#define in_between(a) mkstr(a)
#define join(c, d) in_between(c hash_hash d)

hash_hash

join(x, y)
*/
struct token_list concatenate(struct preprocessor_ctx* ctx, struct token_list* inputList, struct error* error)
{
    //printf("input="); print_list(inputList);

    struct token_list  r = { 0 };
    //todo juntar tokens mesmo objet macro
    //struct token* pPreviousNonBlank = 0;
    while (inputList->head)
    {
        //printf("r="); print_list(&r);
        //printf("input="); print_list(inputList);

        assert(!(inputList->head->flags & TK_FLAG_HAS_NEWLINE_BEFORE));
        if (inputList->head->type == '##')
        {
            if (r.tail == NULL)
            {
                pre_seterror_with_token(ctx, inputList->head, "missing macro argument (should be checked before)");
                break;
            }
            /*
            * arranca ## do input (sem adicionar)
            */
            token_list_pop_front(inputList);

            struct osstream ss = { 0 };

            /*
            *  Faz uma string com o fim r + começo do input (## ja foi removido)
            */
            if (r.tail->lexeme[0] != '\0')
                ss_fprintf(&ss, "%s", r.tail->lexeme);

            if (inputList->head && inputList->head->lexeme[0] != '\0')
                ss_fprintf(&ss, "%s", inputList->head->lexeme);

            //copiar o level para gerar um novo igual
            int level = inputList->head ? inputList->head->level : 0;

            /*
            * Já paga do input o token usado na concatenacao
            */
            token_list_pop_front(inputList);

            /*
            * Faz um novo token com a string montada
            */
            struct token_list newlist = tokenizer(ss.c_str, NULL, level, TK_FLAG_NONE, error);


            if (newlist.head)
            {
                //flags ficam sendo o mesmo do anterior
                newlist.head->flags = r.tail->flags;
            }

            /*
            * Arranca o anterior do r que foi usado para formar string
            */
            token_list_pop_back(&r);

            /*adiciona novo token no fim do r*/
            token_list_append_list(&r, &newlist);
            if (inputList->head == NULL)
                break;
        }
        else
        {
            prematch(&r, inputList);
        }
    }
    return r;
}

struct token_list replace_vaopt(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bvaargs_was_empty)
{
    /*
    4  If the pp-token sequence that is attributed to the variable arguments is
    the empty pp-token sequence, after argument substitution for the following
    rescan of the replacement list (see 6.10.3.4), the identifier __VA_OPT__
    behaves as if defined as:
    */
    struct token_list r = { 0 };
    try
    {
        while (inputList->head)
        {
            if (inputList->head->type == TK_IDENTIFIER &&
                strcmp(inputList->head->lexeme, "__VA_OPT__") == 0)
            {
                //int flags = inputList->head->flags;
                token_list_pop_front(inputList);
                token_list_pop_front(inputList);

                if (bvaargs_was_empty)
                {
                    //remove tudo
                    int count = 1;
                    for (; inputList->head;)
                    {
                        if (inputList->head->type == '(')
                        {
                            token_list_pop_front(inputList);
                            count++;
                        }
                        else if (inputList->head->type == ')')
                        {
                            count--;
                            token_list_pop_front(inputList);
                            if (count == 0)
                                break;
                        }
                        else
                            token_list_pop_front(inputList);
                    }
                }
                else
                {
                    int count = 1;
                    for (; inputList->head;)
                    {
                        if (inputList->head->type == '(')
                        {
                            prematch(&r, inputList);
                            count++;
                        }
                        else if (inputList->head->type == ')')
                        {
                            count--;

                            if (count == 0)
                            {
                                token_list_pop_front(inputList);
                                break;
                            }
                            prematch(&r, inputList);
                        }
                        else
                            prematch(&r, inputList);
                    }
                }
            }
            else
            {
                prematch(&r, inputList);
            }
        }
    }
    catch
    {
    }
    return r;
}
struct token_list replace_macro_arguments(struct preprocessor_ctx* ctx, struct MacroExpanded* pList, struct token_list* inputList, struct MacroArgumentList* arguments, struct error* error)
{
    struct token_list r = { 0 };
    bool bVarArgsWasEmpty = false;
    bool bVarArgs = false;
    try
    {
        while (inputList->head)
        {
            assert(!(inputList->head->flags & TK_FLAG_HAS_NEWLINE_BEFORE));
            assert(!token_is_blank(inputList->head));
            assert(r.tail == NULL || !token_is_blank(r.tail));
            struct MacroArgument* pArgument = NULL;
            if (inputList->head->type == TK_IDENTIFIER)
            {
                pArgument = find_macro_argument_by_name(arguments, inputList->head->lexeme);
            }
            if (pArgument)
            {
                bool check = false;
                if (strcmp(inputList->head->lexeme, "__VA_ARGS__") == 0)
                {
                    check = true;
                }

                if (r.tail != NULL && r.tail->type == '#')
                {

                    /*
                      deleta nome parametro da lista
                      antes copia flags dele
                    */

                    const enum token_flags flags = inputList->head->flags;
                    token_list_pop_front(inputList);

                    //deleta tambem # do fim
                    while (token_is_blank(r.tail))
                    {
                        token_list_pop_back(&r);
                    }
                    token_list_pop_back(&r);

                    ///----------------------------
                    //transforma tudo em string e coloca no resultado
                    struct token_list argumentlist = copy_argument_list(pArgument);
                    if (check)
                    {
                        bVarArgs = true;
                        bVarArgsWasEmpty = (argumentlist.head == NULL || argumentlist.head->type == TK_PLACEMARKER);
                    }

                    char* s = token_list_join_tokens(&argumentlist, true);
                    if (s == NULL)
                    {
                        pre_seterror_with_token(ctx, inputList->head, "unexpected");
                        throw;
                    }
                    struct token* pNew = calloc(1, sizeof * pNew);
                    pNew->lexeme = s;
                    pNew->type = TK_STRING_LITERAL;
                    pNew->flags = flags;
                    token_list_add(&r, pNew);
                    continue;
                }
                else if (r.tail != NULL && r.tail->type == '##')
                {
                    //estou parametro e anterior era ##
                    token_list_pop_front(inputList);
                    struct token_list argumentlist = copy_argument_list(pArgument);
                    if (check)
                    {
                        bVarArgs = true;
                        bVarArgsWasEmpty = (argumentlist.head == NULL || argumentlist.head->type == TK_PLACEMARKER);
                    }
                    token_list_append_list(&r, &argumentlist);
                }
                else if (inputList->head->next && inputList->head->next->type == '##')
                {
                    //estou no parametro e o da frente eh ##
                    int flags = inputList->head->flags;
                    //tira nome parametro a lista
                    token_list_pop_front(inputList);
                    //passa tudo p resultado
                    struct token_list argumentlist = copy_argument_list(pArgument);
                    if (argumentlist.head != NULL)
                    {
                        argumentlist.head->flags = flags;
                    }
                    if (check)
                    {
                        bVarArgs = true;
                        bVarArgsWasEmpty = (argumentlist.head == NULL || argumentlist.head->type == TK_PLACEMARKER);
                    }

                    token_list_append_list(&r, &argumentlist);
                    // ja passa o ## tambem
                    prematch(&r, inputList);
                }
                else
                {

                    int flags = inputList->head->flags;
                    //remove nome parametro do input
                    token_list_pop_front(inputList);
                    //coloca a expansao no resultado
                    struct token_list argumentlist = copy_argument_list(pArgument);
                    if (argumentlist.head)
                    {
                        //copia os flags do identificador
                        argumentlist.head->flags = flags;
                    }
                    /*depois reescan vai corrigir level*/
                    struct token_list r4 = replacement_list_reexamination(ctx, pList, &argumentlist, 0, error/*por enquanto*/);
                    if (error->code) throw;

                    if (check)
                    {
                        bVarArgs = true;
                        bVarArgsWasEmpty = (r4.head == NULL || r4.head->type == TK_PLACEMARKER);
                    }
                    token_list_append_list(&r, &r4);
                }
            }
            else
            {
                prematch(&r, inputList);
            }
        }
    }
    catch
    {
    }

    if (bVarArgs)
    {
        struct token_list r2 = replace_vaopt(ctx, &r, bVarArgsWasEmpty);
        return r2;
    }
    return r;
}

struct token_list concatenate(struct preprocessor_ctx* ctx, struct token_list* inputList, struct error* error);

bool macro_already_expanded(struct MacroExpanded* pList, const char* name)
{
    struct MacroExpanded* pItem = pList;
    while (pItem)
    {
        if (strcmp(name, pItem->name) == 0)
        {
            return true;
        }
        pItem = pItem->pPrevious;
    }
    return false;
}

struct token_list replacement_list_reexamination(struct preprocessor_ctx* ctx, struct MacroExpanded* pList, struct token_list* oldlist, int level, struct error* error)
{
    struct token_list r = { 0 };
    try
    {
        //replacement_list_reexamination
        /*
        For both object-like and function-like macro invocations, before the replacement list is reexamined
        for more macro names to replace, each instance of a ## preprocessing token in the replacement list
        (not from an argument) is deleted and the preceding preprocessing token is concatenated with the
        following preprocessing token.
        */
        struct token_list newList = concatenate(ctx, oldlist, error);
        while (newList.head != NULL)
        {
            assert(!(newList.head->flags & TK_FLAG_HAS_NEWLINE_BEFORE));
            assert(!token_is_blank(newList.head));
            struct macro* pMacro = NULL;
            if (newList.head->type == TK_IDENTIFIER)
            {
                pMacro = find_macro(ctx, newList.head->lexeme);
                if (pMacro &&
                    pMacro->bIsFunction &&
                    !preprocessor_token_ahead_is(newList.head, '('))
                {
                    pMacro = NULL;
                }

                if (pMacro && macro_already_expanded(pList, newList.head->lexeme))
                {
                    newList.head->type = TK_IDENTIFIER_RECURSIVE_MACRO;
                    pMacro = NULL;
                }


                if (ctx->bConditionalInclusion)
                {
                    /*
                     Quando estamos expandindo em condinonal inclusion o defined macro ou defined (macro)
                     não é expandido e é considerado depois
                    */
                    if (r.tail &&
                        r.tail->type == TK_IDENTIFIER &&
                        strcmp(r.tail->lexeme, "defined") == 0)
                    {
                        pMacro = NULL;
                    }
                    else if (r.tail &&
                        r.tail->type == '(')
                    {
                        struct token* previous = r.tail->prev;
                        if (previous != NULL &&
                            previous->type == TK_IDENTIFIER &&
                            strcmp(previous->lexeme, "defined") == 0)
                        {
                            pMacro = NULL;
                        }
                    }
                }

            }
            if (pMacro)
            {
                int flags = newList.head->flags;
                struct MacroArgumentList arguments = collect_macro_arguments(ctx, pMacro, &newList, level, error);
                if (error->code) throw;


                struct token_list r3 = expand_macro(ctx, pList, pMacro, &arguments, level, error);
                if (error->code) throw;

                if (r3.head)
                {
                    r3.head->flags = flags;
                }
                token_list_append_list_at_beginning(&newList, &r3);
            }
            else
            {
                /*
                aqui eh um bom lugar para setar o level e macro flags
                poq sempre tem a re scann da macro no fim
                */
                newList.head->level = level;
                newList.head->flags |= TK_FLAG_MACRO_EXPANDED;
                assert(!(newList.head->flags & TK_FLAG_HAS_NEWLINE_BEFORE));
                prematch(&r, &newList); //nao era macro
            }
        }
    }
    catch
    {
    }

    return r;
}

/*
  Faz a comparação ignorando a continuacao da linha
  TODO fazer uma revisão geral aonde se usa strcmp em lexeme
  e trocar por esta.
*/
int lexeme_cmp(const char* s1, const char* s2)
{
    while (*s1 && *s2)
    {

        while ((s1[0] == '\\' && s1[1] == '\n'))
        {
            s1++;
            s1++;
        }


        while (s2[0] == '\\' && s2[1] == '\n')
        {
            s2++;
            s2++;
        }

        if (*s1 != *s2)
            break;

        s1++;
        s2++;
    }

    while ((s1[0] == '\\' && s1[1] == '\n'))
    {
        s1++;
        s1++;
    }


    while (s2[0] == '\\' && s2[1] == '\n')
    {
        s2++;
        s2++;
    }

    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void remove_line_continuation(char* s)
{
    char* pread = s;
    char* pwrite = s;
    while (*pread)
    {
        if (pread[0] == '\\' && pread[1] == '\n')
        {
            pread++;
            pread++;
        }
        else
        {
            *pwrite = *pread;
            pread++;
            pwrite++;
        }
    }
    *pwrite = *pread;
}

struct token_list  copy_replacement_list(struct token_list* list)
{
    //Faz uma copia dos tokens fazendo um trim no iniico e fim
    //qualquer espaco coments etcc vira um unico  espaco
    struct token_list r = { 0 };
    struct token* pCurrent = list->head;
    //sai de cima de todos brancos iniciais
    while (pCurrent && token_is_blank(pCurrent))
    {
        pCurrent = pCurrent->next;
    }
    //remover flag de espaco antes se tiver
    bool bIsFirst = true;
    bool previousIsBlank = false;
    for (; pCurrent;)
    {
        if (pCurrent && token_is_blank(pCurrent))
        {
            if (pCurrent == list->tail)
                break;

            pCurrent = pCurrent->next;
            continue;
        }
        struct token* pAdded = token_list_clone_and_add(&r, pCurrent);
        if (pAdded->flags & TK_FLAG_HAS_NEWLINE_BEFORE)
        {
            pAdded->flags = pAdded->flags & ~TK_FLAG_HAS_NEWLINE_BEFORE;
            pAdded->flags |= TK_FLAG_HAS_SPACE_BEFORE;
        }
        if (bIsFirst)
        {
            pAdded->flags = pAdded->flags & ~TK_FLAG_HAS_SPACE_BEFORE;
            pAdded->flags = pAdded->flags & ~TK_FLAG_HAS_NEWLINE_BEFORE;
            bIsFirst = false;
        }
        remove_line_continuation(pAdded->lexeme);
        previousIsBlank = false;

        if (pCurrent == list->tail)
            break;
        pCurrent = pCurrent->next;

    }
    return r;
}



struct token_list macro_copy_replacement_list(struct preprocessor_ctx* ctx, struct macro* pMacro, struct error* error)
{
    /*macros de conteudo dinamico*/
    if (strcmp(pMacro->name, "__LINE__") == 0)
    {
        struct token_list r = tokenizer("1", "", 0, TK_FLAG_NONE, error);
        token_list_pop_front(&r);
        r.head->flags = 0;
        return r;
    }
    else if (strcmp(pMacro->name, "__FILE__") == 0)
    {
        struct token_list r = tokenizer("\"file\"", "", 0, TK_FLAG_NONE, error);
        token_list_pop_front(&r);
        r.head->flags = 0;
        return r;
    }
    else if (strcmp(pMacro->name, "__COUNT__") == 0)
    {
        assert(false);//TODO
        struct token_list r = tokenizer("1", "", 0, TK_FLAG_NONE, error);
        token_list_pop_front(&r);
        r.head->flags = 0;
        return r;
    }

    return copy_replacement_list(&pMacro->replacementList);
}

void print_literal2(const char* s);



/*
    Se p for macro expande completamente e retorna o ponteiro
    para o primeiro item da expansao
    caso contrario, se p nao for macro, retorna null.
*/
struct token_list expand_macro(struct preprocessor_ctx* ctx, struct MacroExpanded* pList, struct macro* pMacro, struct MacroArgumentList* arguments, int level, struct error* error)
{
    pMacro->usage++;

    //printf("\nexpanding ");
    //print_macro(pMacro);
    //print_macro_arguments(arguments);
    //printf("\n");
    struct token_list r = { 0 };
    try
    {
        assert(!macro_already_expanded(pList, pMacro->name));
        struct MacroExpanded macro;
        macro.name = pMacro->name;
        macro.pPrevious = pList;
        if (pMacro->bIsFunction)
        {
            struct token_list copy = macro_copy_replacement_list(ctx, pMacro, error);
            struct token_list copy2 = replace_macro_arguments(ctx, &macro, &copy, arguments, error);
            if (error->code) throw;

            struct token_list r2 = replacement_list_reexamination(ctx, &macro, &copy2, level, error);
            if (error->code) throw;

            token_list_append_list(&r, &r2);
        }
        else
        {
            struct token_list copy = macro_copy_replacement_list(ctx, pMacro, error);
            struct token_list r3 = replacement_list_reexamination(ctx, &macro, &copy, level, error);
            if (error->code) throw;

            token_list_append_list(&r, &r3);
        }
    }
    catch
    {
    }

    //printf("result=");
    //print_list(&r);
    return r;
}
void print_token(struct token* p_token);

struct token_list text_line(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error)
{
    /*
          text-line:
          pp-tokens_opt new-line
        */
    struct token_list r = { 0 };
    try
    {
        while (inputList->head &&
            inputList->head->type != TK_PREPROCESSOR_LINE)
        {
            struct macro* pMacro = NULL;
            struct token* start_token = inputList->head;
            //assert(start_token->pFile != NULL);

            if (bActive && inputList->head->type == TK_IDENTIFIER)
            {


                pMacro = find_macro(ctx, inputList->head->lexeme);
                if (pMacro &&
                    pMacro->bIsFunction &&
                    !preprocessor_token_ahead_is(inputList->head, '('))
                {
                    pMacro = NULL;
                }

                if (ctx->bConditionalInclusion)
                {
                    /*
                     Quando estamos expandindo em condinonal inclusion o defined macro ou defined (macro)
                     não é expandido e é considerado depois
                    */

                    if (r.tail &&
                        r.tail->type == TK_IDENTIFIER &&
                        strcmp(r.tail->lexeme, "defined") == 0)
                    {
                        pMacro = NULL;
                    }
                    else if (r.tail &&
                        r.tail->type == '(')
                    {
                        struct token* previous = r.tail->prev;
                        if (previous != NULL &&
                            previous->type == TK_IDENTIFIER &&
                            strcmp(previous->lexeme, "defined") == 0)
                        {
                            pMacro = NULL;
                        }
                    }
                }
            }
            if (pMacro)
            {
#ifdef _WIN32
                if (inputList->head->pFile)
                {
                    //char line[1000] = { 0 };
                    //snprintf(line, sizeof line, "%s(%d,%d):\n", inputList->head->pFile->lexeme, inputList->head->line, inputList->head->col);
                    //OutputDebugStringA(line);
                }
#endif

                //efeito tetris
                //#define f(a) a
                //#define F g
                //F(1)
                //quero deixar F(g) na saida.
                //e toda parte de dentro escondida no caso  1
                //F(1)`a` acho que vou imprimir desta forma ou so fundo diferente
                //
                enum token_flags flags = inputList->head->flags;
                struct MacroArgumentList arguments = collect_macro_arguments(ctx, pMacro, inputList, level, error);
                if (error->code) throw;


                struct token_list startMacro = expand_macro(ctx, NULL, pMacro, &arguments, level, error);
                if (startMacro.head)
                {
                    startMacro.head->flags |= flags;
                }

                if (pMacro->bExpand)
                {
                    //Esconde a macro e os argumentos
                    for (struct token* current = arguments.tokens.head;
                        current != arguments.tokens.tail->next;
                        current = current->next)
                    {
                        current->flags |= TK_FLAG_HIDE;
                    }

                    //mostra a expansao da macro
                    /*teste de expandir so algumas macros*/
                    for (struct token* current = startMacro.head;
                        current != startMacro.tail->next;
                        current = current->next)
                    {
                        current->flags = current->flags & ~TK_FLAG_MACRO_EXPANDED;
                    }
                }

                //seta nos tokens expandidos da onde eles vieram
                token_list_set_file(&startMacro, start_token->pFile, start_token->line, start_token->col);

                token_list_append_list_at_beginning(inputList, &startMacro);

                if (ctx->flags & preprocessor_ctx_flags_only_final)
                {
                }
                else
                {
                    if (level == 0 || INCLUDE_ALL)
                        token_list_append_list(&r, &arguments.tokens);
                }

                //print_tokens(r.head);
                while (pMacro)
                {
                    pMacro = NULL;
                    if (inputList->head->type == TK_IDENTIFIER)
                    {
                        pMacro = find_macro(ctx, inputList->head->lexeme);
                        if (pMacro && pMacro->bIsFunction &&
                            !preprocessor_token_ahead_is(inputList->head, '('))
                        {
                            pMacro = NULL;
                        }
                        if (pMacro)
                        {
                            // printf("tetris\n");
                            int flags2 = inputList->head->flags;
                            struct MacroArgumentList arguments2 = collect_macro_arguments(ctx, pMacro, inputList, level, error);
                            if (error->code) throw;

                            if (ctx->flags & preprocessor_ctx_flags_only_final)
                            {
                            }
                            else
                            {
                                if (level == 0 || INCLUDE_ALL)
                                {
                                    token_list_append_list(&r, &arguments2.tokens);
                                }
                            }


                            struct token_list r3 = expand_macro(ctx, NULL, pMacro, &arguments2, level, error);
                            if (error->code) throw;

                            //seta nos tokens expandidos da onde eles vieram
                            token_list_set_file(&r3, start_token->pFile, start_token->line, start_token->col);

                            if (r3.head)
                            {
                                r3.head->flags = flags2;
                            }
                            token_list_append_list_at_beginning(inputList, &r3);
                        }
                    }
                }
                continue;
                //saiu tetris...
                //entao tudo foi expandido desde a primeiroa
            }
            else
            {
                bool blanks = token_is_blank(inputList->head) || inputList->head->type == TK_NEWLINE;
                bool bFinal = bActive && !is_never_final(inputList->head->type);

                if (ctx->flags & preprocessor_ctx_flags_only_final)
                {
                    if (bFinal)
                    {
                        prematch(&r, inputList);
                        r.tail->flags |= TK_FLAG_FINAL;
                        //token_promote(r.tail);
                    }
                    else
                    {
                        token_list_pop_front(inputList);//todo deletar
                    }
                }
                else
                {
                    if (blanks)
                    {
                        if (level == 0 || INCLUDE_ALL)
                        {
                            prematch(&r, inputList);
                        }
                        else
                            token_list_pop_front(inputList);//todo deletar
                    }
                    else
                    {
                        if (level == 0 || INCLUDE_ALL)
                        {
                            prematch(&r, inputList);
                            if (bFinal)
                            {
                                // if (strcmp(r.tail->lexeme, "_CRT_STDIO_INLINE") == 0)
                                 //{
                                   //  printf("");
                                 //}

                                r.tail->flags |= TK_FLAG_FINAL;
                                //token_promote(r.tail);

                            }
                        }
                        else
                        {
                            if (bFinal)
                            {
                                //if (strcmp(r.tail->lexeme, "_CRT_STDIO_INLINE") == 0)
                                //{
                                 //   printf("");
                                //}

                                prematch(&r, inputList);
                                r.tail->flags |= TK_FLAG_FINAL;
                                //token_promote(r.tail);
                            }
                            else
                            {
                                token_list_pop_front(inputList);//todo deletar
                            }
                        }
                    }
                }


            }
        }
    }
    catch
    {
    }

    return r;
}

struct token_list group_part(struct preprocessor_ctx* ctx, struct token_list* inputList, bool bActive, int level, struct error* error)
{
    /*
    group-part:
     if-section
     control-line
     text-line
     # non-directive
    */

    if (inputList->head->type == TK_PREPROCESSOR_LINE)
    {
        if (preprocessor_token_ahead_is_identifier(inputList->head, "if") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "ifdef") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "ifndef"))
        {
            return if_section(ctx, inputList, bActive, level, error);
        }
        else if (preprocessor_token_ahead_is_identifier(inputList->head, "include") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "embed") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "define") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "undef") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "warning") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "line") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "error") ||
            preprocessor_token_ahead_is_identifier(inputList->head, "pragma"))
        {
            return control_line(ctx, inputList, bActive, level, error);
        }
        else
        {
            //aqui vou consumir o # dentro para ficar simetrico
            return non_directive(ctx, inputList, level, error);
        }
    }
    return text_line(ctx, inputList, bActive, level, error);
}


struct token_list preprocessor(struct preprocessor_ctx* ctx, struct token_list* inputList, int level, struct error* error)
{
    struct token_list r = { 0 };
    if (inputList->head == NULL)
    {
        return r;
    }

    if (inputList->head->type == TK_BEGIN_OF_FILE)
    {
        prematch_level(&r, inputList, 1); //sempre coloca
    }

    struct token_list g = group_opt(ctx, inputList, true /*active*/, level, error);
    token_list_append_list(&r, &g);
    return r;
}


void mark_macros_as_used(struct hash_map* pMap)
{
    /*
     *  Objetivo era alertar macros nao usadas...
     */

    if (pMap->table != NULL)
    {
        for (int i = 0; i < pMap->capacity; i++)
        {
            struct map_entry* pentry = pMap->table[i];

            while (pentry != NULL)
            {
                struct macro* pMacro = container_of(pentry->p, struct macro, type_id);
                pMacro->usage = 1;
                pentry = pentry->next;
            }
        }
    }
}

void check_unused_macros(struct hash_map* pMap)
{
    /*
     *  Objetivo era alertar macros nao usadas...
     */

    if (pMap->table != NULL)
    {
        for (int i = 0; i < pMap->capacity; i++)
        {
            struct map_entry* pentry = pMap->table[i];

            while (pentry != NULL)
            {
                struct macro* pMacro = container_of(pentry->p, struct macro, type_id);
                if (pMacro->usage == 0)
                {
                    //TODO adicionar conceito meu codigo , codigo de outros nao vou colocar erro
                    printf("%s not used\n", pMacro->name);
                }
                pentry = pentry->next;
            }
        }
    }
}

void add_standard_macros(struct preprocessor_ctx* ctx, struct error* error)
{

    static char mon[][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);

    char dataStr[100] = { 0 };
    snprintf(dataStr, sizeof dataStr, "#define __DATE__ \"%s %2d %d\"\n", mon[tm->tm_mon], tm->tm_mday, tm->tm_year + 1900);
    struct token_list l1 = tokenizer(dataStr, "__DATE__ macro inclusion", 0, TK_FLAG_NONE, error);
    preprocessor(ctx, &l1, 0, error);

    char timeStr[100] = { 0 };
    snprintf(timeStr, sizeof timeStr, "#define __TIME__ \"%02d:%02d:%02d\"\n", tm->tm_hour, tm->tm_min, tm->tm_sec);
    struct token_list l2 = tokenizer(timeStr, "__TIME__ macro inclusion", 0, TK_FLAG_NONE, error);
    preprocessor(ctx, &l2, 0, error);

    //token_list_destroy(&l2);

    const char* pre_defined_macros_text =
        "#define __FILE__\n"
        "#define __LINE__\n"
        "#define __COUNT__\n"
        "#define _CONSOLE\n"
        //"#define _MSC_VER 1200\n"        
        "#define _WINDOWS\n"
        "#define _M_IX86\n"
        "#define _X86_\n"
        "#define __fastcall\n"
        "#define __stdcall\n"
        "#define _WIN32\n"
        "#define __cdecl\n"
        "#define __pragma(a)\n"
        "#define __declspec(a)\n"
        "#define __crt_va_start(X) \n"
        "#define __builtin_offsetof(type, member) 0\n"; //como nao defini msver ele pensa que eh gcc aqui

    struct token_list l = tokenizer(pre_defined_macros_text, "standard macros inclusion", 0, TK_FLAG_NONE, error);
    struct token_list l3 = preprocessor(ctx, &l, 0, error);

    //nao quer ver warning de nao usado nestas macros padrao
    mark_macros_as_used(&ctx->macros);
    token_list_destroy(&l);
    token_list_destroy(&l3);
}




const char* get_token_name(enum token_type tk)
{
    switch (tk)
    {
    case TK_NONE: return "NONE";
    case TK_NEWLINE: return "NEWLINE";
    case TK_WHITE_SPACE: return "SPACE";
    case TK_EXCLAMATION_MARK: return "TK_EXCLAMATION_MARK";
    case TK_QUOTATION_MARK: return "TK_QUOTATION_MARK";
    case TK_NUMBER_SIGN: return "TK_NUMBER_SIGN";
    case TK_DOLLAR_SIGN: return "TK_DOLLAR_SIGN";
    case TK_PERCENT_SIGN: return "TK_PERCENT_SIGN";
    case TK_AMPERSAND: return "TK_AMPERSAND";
    case TK_APOSTROPHE: return "TK_APOSTROPHE";
    case TK_LEFT_PARENTHESIS: return "TK_LEFT_PARENTHESIS";
    case TK_RIGHT_PARENTHESIS: return "TK_RIGHT_PARENTHESIS";
    case TK_ASTERISK: return "TK_ASTERISK";
    case TK_PLUS_SIGN: return "TK_PLUS_SIGN";
    case TK_COMMA: return "TK_COMMA";
    case TK_HYPHEN_MINUS: return "TK_HYPHEN_MINUS";
    case TK_FULL_STOP: return "TK_FULL_STOP";
    case TK_SOLIDUS: return "TK_SOLIDUS";
    case TK_COLON: return "TK_COLON";
    case TK_SEMICOLON: return "TK_SEMICOLON";
    case TK_LESS_THAN_SIGN: return "TK_LESS_THAN_SIGN";
    case TK_EQUALS_SIGN: return "TK_EQUALS_SIGN";
    case TK_GREATER_THAN_SIGN: return "TK_GREATER_THAN_SIGN";
    case TK_QUESTION_MARK: return "TK_QUESTION_MARK";
    case TK_COMMERCIAL_AT: return "TK_COMMERCIAL_AT";
    case TK_LEFT_SQUARE_BRACKET: return "TK_LEFT_SQUARE_BRACKET";
    case TK_REVERSE_SOLIDUS: return "TK_REVERSE_SOLIDUS";
    case TK_RIGHT_SQUARE_BRACKET: return "TK_RIGHT_SQUARE_BRACKET";
    case TK_CIRCUMFLEX_ACCENT: return "TK_CIRCUMFLEX_ACCENT";
    case TK_FLOW_LINE: return "TK_FLOW_LINE";
    case TK_GRAVE_ACCENT: return "TK_GRAVE_ACCENT";
    case TK_LEFT_CURLY_BRACKET: return "TK_LEFT_CURLY_BRACKET";
    case TK_VERTICAL_LINE: return "TK_VERTICAL_LINE";
    case TK_RIGHT_CURLY_BRACKET: return "TK_RIGHT_CURLY_BRACKET";
    case TK_TILDE: return "TK_TILDE";
    case TK_PREPROCESSOR_LINE: return "TK_PREPROCESSOR_LINE";
    case TK_STRING_LITERAL: return "TK_STRING_LITERAL";
    case TK_LINE_COMMENT: return "TK_LINE_COMMENT";
    case TK_COMENT: return "TK_COMENT";
    case TK_PPNUMBER: return "TK_PPNUMBER";
    case ANY_OTHER_PP_TOKEN: return "ANY_OTHER_PP_TOKEN";
    case TK_COMPILER_DECIMAL_CONSTANT: return "TK_COMPILER_DECIMAL_CONSTANT";
    case TK_COMPILER_OCTAL_CONSTANT: return "TK_COMPILER_OCTAL_CONSTANT";
    case TK_COMPILER_HEXADECIMAL_CONSTANT: return "TK_COMPILER_HEXADECIMAL_CONSTANT";
    case TK_COMPILER_BINARY_CONSTANT: return "TK_COMPILER_BINARY_CONSTANT";
    case TK_COMPILER_DECIMAL_FLOATING_CONSTANT: return "TK_COMPILER_DECIMAL_FLOATING_CONSTANT";
    case TK_COMPILER_HEXADECIMAL_FLOATING_CONSTANT: return "TK_COMPILER_HEXADECIMAL_FLOATING_CONSTANT";

    case TK_PLACEMARKER: return "TK_PLACEMARKER";
    case TK_BLANKS: return "TK_BLANKS";
    case TK_PLUSPLUS: return "TK_PLUSPLUS";
    case TK_MINUSMINUS: return "TK_MINUSMINUS";
    case TK_ARROW: return "TK_ARROW";
    case TK_SHIFTLEFT: return "TK_SHIFTLEFT";
    case TK_SHIFTRIGHT: return "TK_SHIFTRIGHT";
    case TK_LOGICAL_OPERATOR_OR: return "TK_LOGICAL_OPERATOR_OR";
    case TK_LOGICAL_OPERATOR_AND: return "TK_LOGICAL_OPERATOR_AND";
    case TK_MACRO_CONCATENATE_OPERATOR: return "TK_MACRO_CONCATENATE_OPERATOR";
    case TK_IDENTIFIER: return "TK_IDENTIFIER";
    case TK_IDENTIFIER_RECURSIVE_MACRO: return "TK_IDENTIFIER_RECURSIVE_MACRO";
    case TK_BEGIN_OF_FILE: return "TK_BEGIN_OF_FILE";
    case TK_KEYWORD_AUTO: return "TK_KEYWORD_AUTO";
    case TK_KEYWORD_BREAK: return "TK_KEYWORD_BREAK";
    case TK_KEYWORD_CASE: return "TK_KEYWORD_CASE";
    case TK_KEYWORD_CONSTEXPR: return "TK_KEYWORD_CONSTEXPR";
    case TK_KEYWORD_CHAR: return "TK_KEYWORD_CHAR";
    case TK_KEYWORD_CONST: return "TK_KEYWORD_CONST";
    case TK_KEYWORD_CONTINUE: return "TK_KEYWORD_CONTINUE";
    case TK_KEYWORD_DEFAULT: return "TK_KEYWORD_DEFAULT";
    case TK_KEYWORD_DO: return "TK_KEYWORD_DO";
    case TK_KEYWORD_DOUBLE: return "TK_KEYWORD_DOUBLE";
    case TK_KEYWORD_ELSE: return "TK_KEYWORD_ELSE";
    case TK_KEYWORD_ENUM: return "TK_KEYWORD_ENUM";
    case TK_KEYWORD_EXTERN: return "TK_KEYWORD_EXTERN";
    case TK_KEYWORD_FLOAT: return "TK_KEYWORD_FLOAT";
    case TK_KEYWORD_FOR: return "TK_KEYWORD_FOR";
    case TK_KEYWORD_GOTO: return "TK_KEYWORD_GOTO";
    case TK_KEYWORD_IF: return "TK_KEYWORD_IF";
    case TK_KEYWORD_INLINE: return "TK_KEYWORD_INLINE";
    case TK_KEYWORD_INT: return "TK_KEYWORD_INT";
    case TK_KEYWORD_LONG: return "TK_KEYWORD_LONG";
    case TK_KEYWORD__INT8: return "TK_KEYWORD__INT8";
    case TK_KEYWORD__INT16: return "TK_KEYWORD__INT16";
    case TK_KEYWORD__INT32: return "TK_KEYWORD__INT32";
    case TK_KEYWORD__INT64: return "TK_KEYWORD__INT64";
    case TK_KEYWORD_REGISTER: return "TK_KEYWORD_REGISTER";
    case TK_KEYWORD_RESTRICT: return "TK_KEYWORD_RESTRICT";
    case TK_KEYWORD_RETURN: return "TK_KEYWORD_RETURN";
    case TK_KEYWORD_SHORT: return "TK_KEYWORD_SHORT";
    case TK_KEYWORD_SIGNED: return "TK_KEYWORD_SIGNED";
    case TK_KEYWORD_SIZEOF: return "TK_KEYWORD_SIZEOF";
    case TK_KEYWORD_HASHOF: return "TK_KEYWORD_HASHOF";
    case TK_KEYWORD_STATIC: return "TK_KEYWORD_STATIC";
    case TK_KEYWORD_STRUCT: return "TK_KEYWORD_STRUCT";
    case TK_KEYWORD_SWITCH: return "TK_KEYWORD_SWITCH";
    case TK_KEYWORD_TYPEDEF: return "TK_KEYWORD_TYPEDEF";
    case TK_KEYWORD_UNION: return "TK_KEYWORD_UNION";
    case TK_KEYWORD_UNSIGNED: return "TK_KEYWORD_UNSIGNED";
    case TK_KEYWORD_VOID: return "TK_KEYWORD_VOID";
    case TK_KEYWORD_VOLATILE: return "TK_KEYWORD_VOLATILE";
    case TK_KEYWORD_WHILE: return "TK_KEYWORD_WHILE";
    case TK_KEYWORD__ALIGNAS: return "TK_KEYWORD__ALIGNAS";
    case TK_KEYWORD__ALIGNOF: return "TK_KEYWORD__ALIGNOF";
    case TK_KEYWORD__ATOMIC: return "TK_KEYWORD__ATOMIC";
    case TK_KEYWORD__BOOL: return "TK_KEYWORD__BOOL";
    case TK_KEYWORD__COMPLEX: return "TK_KEYWORD__COMPLEX";
    case TK_KEYWORD__DECIMAL128: return "TK_KEYWORD__DECIMAL128";
    case TK_KEYWORD__DECIMAL32: return "TK_KEYWORD__DECIMAL32";
    case TK_KEYWORD__DECIMAL64: return "TK_KEYWORD__DECIMAL64";
    case TK_KEYWORD__GENERIC: return "TK_KEYWORD__GENERIC";
    case TK_KEYWORD__IMAGINARY: return "TK_KEYWORD__IMAGINARY";
    case TK_KEYWORD__NORETURN: return "TK_KEYWORD__NORETURN";
    case TK_KEYWORD__STATIC_ASSERT: return "TK_KEYWORD__STATIC_ASSERT";
    case TK_KEYWORD__THREAD_LOCAL: return "TK_KEYWORD__THREAD_LOCAL";
    case TK_KEYWORD_TYPEOF: return "TK_KEYWORD_TYPEOF";
    case TK_KEYWORD_TYPEID: return "TK_KEYWORD_TYPEID";

    case TK_KEYWORD_TRUE: return "TK_KEYWORD_TRUE";
    case TK_KEYWORD_FALSE: return "TK_KEYWORD_FALSE";
    case TK_KEYWORD_NULL: return "TK_KEYWORD_NULL";
    case TK_KEYWORD_DEFER: return "TK_KEYWORD_DEFER";
    }
    assert(false);
    return "";
};


void print_literal(const char* s)
{
    if (s == NULL)
    {
        printf("\"");
        printf("\"");
        return;
    }
    printf("\"");
    while (*s)
    {
        switch (*s)
        {
        case '\n':
            printf("\\n");
            break;
        default:
            printf("%c", *s);
        }
        s++;
    }
    printf("\"");
}





const char* get_code_as_we_see_plusmacros(struct token_list* list)
{
    struct osstream ss = { 0 };
    struct token* pCurrent = list->head;
    while (pCurrent)
    {
        if (pCurrent->level == 0 &&
            pCurrent->type != TK_BEGIN_OF_FILE)
        {
            if (pCurrent->flags & TK_FLAG_MACRO_EXPANDED)
                ss_fprintf(&ss, LIGHTCYAN);
            else
                ss_fprintf(&ss, WHITE);
            ss_fprintf(&ss, "%s", pCurrent->lexeme);
            ss_fprintf(&ss, RESET);
        }
        pCurrent = pCurrent->next;
    }
    return ss.c_str;
}

const char* get_code_as_we_see(struct token_list* list, bool removeComments)
{
    struct osstream ss = { 0 };
    struct token* pCurrent = list->head;
    while (pCurrent != list->tail->next)
    {
        if (pCurrent->level == 0 &&
            !(pCurrent->flags & TK_FLAG_MACRO_EXPANDED) &&
            !(pCurrent->flags & TK_FLAG_HIDE) &&
            pCurrent->type != TK_BEGIN_OF_FILE)
        {
            if ((pCurrent->flags & TK_FLAG_HAS_SPACE_BEFORE) &&
                (pCurrent->prev != NULL && pCurrent->prev->type != TK_BLANKS))
            {
                //se uma macro expandida for mostrada ele nao tem espacos entao inserimos
                ss_fprintf(&ss, " ");
            }

            if (removeComments)
            {
                if (pCurrent->type == TK_LINE_COMMENT)
                    ss_fprintf(&ss, "\n");
                else if (pCurrent->type == TK_COMENT)
                    ss_fprintf(&ss, " ");
                else
                    ss_fprintf(&ss, "%s", pCurrent->lexeme);
            }
            else
            {
                ss_fprintf(&ss, "%s", pCurrent->lexeme);
            }
        }
        pCurrent = pCurrent->next;
    }
    return ss.c_str;
}


const char* get_code_as_compiler_see(struct token_list* list)
{
    struct osstream ss = { 0 };
    struct token* pCurrent = list->head;
    while (pCurrent != list->tail->next)
    {
        if (!(pCurrent->flags & TK_FLAG_HIDE) &&
            pCurrent->type != TK_BEGIN_OF_FILE &&
            (pCurrent->flags & TK_FLAG_FINAL))
        {
            if (pCurrent->flags & TK_FLAG_HAS_SPACE_BEFORE)
                ss_fprintf(&ss, " ");

            if (pCurrent->flags & TK_FLAG_HAS_NEWLINE_BEFORE)
                ss_fprintf(&ss, "\n");

            if (pCurrent->type == TK_LINE_COMMENT)
                ss_fprintf(&ss, "\n");
            else if (pCurrent->type == TK_COMENT)
                ss_fprintf(&ss, " ");
            else
                ss_fprintf(&ss, "%s", pCurrent->lexeme);
        }
        pCurrent = pCurrent->next;
    }
    return ss.c_str;
}

const char* print_preprocessed_to_string2(struct token* p_token)
{
    /*
    * No nivel > 0 (ou seja dentro dos includes)
    * Esta funcao imprime os tokens como o compilador ve
    * e insere um espaco ou quebra de linha para poder representar
    * a separacao entre os tokens.

    * Ja no nivel 0 (arquivo principal) ele imprime espacos comentarios
    * etc.. e insere espacos na expancao da macro.
    */

    if (p_token == NULL)
        return strdup("(null)");

    struct osstream ss = { 0 };
    struct token* pCurrent = p_token;
    while (pCurrent)
    {

        //Nós ignorados a line continuation e ela pode aparecer em qualquer parte
        //dos lexemes.
        //inves de remover poderia so pular ao imprimir
        remove_line_continuation(pCurrent->lexeme);

        if (pCurrent->flags & TK_FLAG_FINAL)
        {
            if (pCurrent->level > 0)
            {
                //nos niveis de include nos podemos estar ignorando todos
                //os espacos. neste caso eh preciso incluilos para nao juntar os tokens

                if ((pCurrent->flags & TK_FLAG_HAS_NEWLINE_BEFORE))
                    ss_fprintf(&ss, "\n");
                else if ((pCurrent->flags & TK_FLAG_HAS_SPACE_BEFORE))
                    ss_fprintf(&ss, " ");
            }
            else
            {
                /*
                  no nivel 0 nos imprimimos os espacos.. porem no caso das macros
                  eh preciso colocar um espaco pq ele nao existe.
                */
                if (pCurrent->flags & TK_FLAG_MACRO_EXPANDED)
                {
                    if ((pCurrent->flags & TK_FLAG_HAS_SPACE_BEFORE))
                        ss_fprintf(&ss, " ");
                }
            }

            //}

            if (pCurrent->lexeme[0] != '\0')
            {
                ss_fprintf(&ss, "%s", pCurrent->lexeme);
            }

            pCurrent = pCurrent->next;
        }
        else
        {
            if (pCurrent->level == 0)
            {
                if (pCurrent->type == TK_BLANKS ||
                    pCurrent->type == TK_NEWLINE)
                {
                    ss_fprintf(&ss, "%s", pCurrent->lexeme);
                }
            }


            pCurrent = pCurrent->next;
        }
    }
    return ss.c_str; //MOVED
}

const char* print_preprocessed_to_string(struct token* p_token)
{
    /*
    * Esta funcao imprime os tokens como o compilador ve
    * e insere um espaco ou quebra de linha para poder representar
    * a separacao entre os tokens.
    */

    struct osstream ss = { 0 };
    struct token* pCurrent = p_token;

    /*
    * Ignora tudo o que é espaço no início
    */
    while (!(pCurrent->flags & TK_FLAG_FINAL) ||
        pCurrent->type == TK_BLANKS ||
        pCurrent->type == TK_COMENT ||
        pCurrent->type == TK_LINE_COMMENT ||
        pCurrent->type == TK_NEWLINE ||
        pCurrent->type == TK_PREPROCESSOR_LINE)
    {
        pCurrent = pCurrent->next;
        if (pCurrent == NULL)
            return ss.c_str;
    }

    bool first = true;
    while (pCurrent)
    {
        assert(pCurrent->pFile != NULL);
        if (pCurrent->flags & TK_FLAG_FINAL)
        {
            if (!first && pCurrent->flags & TK_FLAG_HAS_NEWLINE_BEFORE)
                ss_fprintf(&ss, "\n");
            else if (!first && pCurrent->flags & TK_FLAG_HAS_SPACE_BEFORE)
                ss_fprintf(&ss, " ");
            if (pCurrent->lexeme[0] != '\0')
                ss_fprintf(&ss, "%s", pCurrent->lexeme);
            first = false;
            pCurrent = pCurrent->next;
        }
        else
        {
            pCurrent = pCurrent->next;
        }
    }
    return ss.c_str;
}

void print_preprocessed(struct token* p_token)
{
    const char* s = print_preprocessed_to_string(p_token);
    if (s)
    {
        printf("%s", s);
        free((void*)s);
    }
}


#ifdef TEST
#include "unit_test.h"


void print_asserts(struct token* p_token)
{
    struct token* pCurrent = p_token;
    printf("struct { const char* lexeme; enum token_type token; int bActive; int bFinal; } result[] = { \n");
    while (pCurrent)
    {
        printf("{ %-20s, %d, ", get_token_name(pCurrent->type), (pCurrent->flags & TK_FLAG_FINAL));
        print_literal(pCurrent->lexeme);
        printf("},\n");
        pCurrent = pCurrent->next;
    }
    printf("}\n");
}

void show_all(struct token* p_token)
{
    struct token* pCurrent = p_token;
    while (pCurrent)
    {
        if (pCurrent->flags & TK_FLAG_FINAL)
        {
            if (pCurrent->level == 0)
                printf(WHITE);
            else
                printf(BROWN);
        }
        else
        {
            if (pCurrent->level == 0)
                printf(LIGHTGRAY);
            else
                printf(BLACK);
        }
        printf("%s", pCurrent->lexeme);
        printf(RESET);
        pCurrent = pCurrent->next;
    }
}





void print_preprocessed_to_file(struct token* p_token, const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (f)
    {
        const char* s = print_preprocessed_to_string(p_token);
        if (s)
        {
            fprintf(f, "%s", s);
            free((void*)s);
        }
        fclose(f);
    }
}

void show_visible(struct token* p_token)
{
    printf(WHITE "visible used   / " LIGHTGRAY "visible ignored\n" RESET);
    struct token* pCurrent = p_token;
    while (pCurrent)
    {
        if (pCurrent->level == 0)
        {
            if (pCurrent->flags & TK_FLAG_FINAL)
                printf(WHITE);
            else
                printf(LIGHTGRAY);
        }
        else
        {
            if (pCurrent->level == 0)
                printf(BLACK);
            else
                printf(BLACK);
        }
        printf("%s", pCurrent->lexeme);
        printf(RESET);
        pCurrent = pCurrent->next;
    }
}

void show_visible_and_invisible(struct token* p_token)
{
    printf(LIGHTGREEN "visible used   / " LIGHTGRAY "visible ignored\n" RESET);
    printf(LIGHTBLUE  "invisible used / " BROWN     "invisible ignored\n" RESET);
    struct token* pCurrent = p_token;
    while (pCurrent)
    {
        if (pCurrent->level == 0)
        {
            if (pCurrent->flags & TK_FLAG_FINAL)
                printf(LIGHTGREEN);
            else
                printf(LIGHTGRAY);
        }
        else
        {
            if (pCurrent->flags & TK_FLAG_FINAL)
                printf(LIGHTBLUE);
            else
                printf(BROWN);
        }
        printf("%s", pCurrent->lexeme);
        printf(RESET);
        pCurrent = pCurrent->next;
    }
}

int test_preprossessor_input_output(const char* input, const char* output)
{
    struct error error = { 0 };
    struct token_list list = tokenizer(input, "source", 0, TK_FLAG_NONE, &error);

    struct preprocessor_ctx ctx = { 0 };
    ctx.printf = printf;

    struct token_list r = preprocessor(&ctx, &list, 0, &error);
    const char* s = print_preprocessed_to_string(r.head);
    if (strcmp(s, output) != 0)
    {
        printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        printf("expected\n%s", output);
        printf("HAS\n%s", s);
        printf("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        print_tokens(r.head);
        printf("TEST 0 FAILED\n");
        return 1;
    }
    free((void*)s);
    return 0;
}

char* normalize_line_end(char* input)
{
    if (input == NULL)
        return NULL;
    char* pWrite = input;
    const char* p = input;
    while (*p)
    {
        if (p[0] == '\r' && p[1] == '\n')
        {
            *pWrite = '\n';
            p++;
            p++;
            pWrite++;
        }
        else
        {
            *pWrite = *p;
            p++;
            pWrite++;
        }
    }
    *pWrite = 0;
    return input;
}

static int printf_nothing(char const* const _Format, ...) { return 0; }

int test_preprocessor_in_out(const char* input, const char* output, struct error* error_opt)
{
    int res = 0;
    struct error error = { 0 };
    struct token_list list = tokenizer(input, "source", 0, TK_FLAG_NONE, &error);

    struct preprocessor_ctx ctx = { 0 };
    ctx.printf = printf_nothing;

    struct token_list r = preprocessor(&ctx, &list, 0, &error);
    const char* result = print_preprocessed_to_string(r.head);
    if (result == NULL)
    {
        result = strdup("");
    }
    if (strcmp(result, output) != 0)
    {
        /*
        printf("FAILED\n");
        printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        printf("assert\n");
        printf("%s`", output);
        printf("\nGOT\n");
        printf("%s`", result);
        printf("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
        print_tokens(r.head);

        */
        res = 1;
    }

    if (error_opt)
        *error_opt = error;

    return res;
}

int test_preprocessor_in_out_using_file(const char* fileName)
{
    int res = 0;
    const char* input = normalize_line_end(readfile(fileName));
    char* output = 0;
    if (input)
    {
        char* pos = strstr(input, "\n---");
        if (pos)
        {
            *pos = 0;
            //anda ate sair ---
            pos++;
            while (*pos != '\n')
            {
                pos++;
            }
            pos++; //skip \n
            output = pos;
            /*optional*/
            pos = strstr(output, "\n---");
            if (pos)
                *pos = 0;
        }
        res = test_preprocessor_in_out(input, output, NULL);
        free((void*)input);
    }
    return res;
}

void test_lexeme_cmp()
{
    assert(lexeme_cmp("a", "\\\na") == 0);
    assert(lexeme_cmp("a", "a\\\n") == 0);
    assert(lexeme_cmp("\\\na", "a") == 0);
    assert(lexeme_cmp("a\\\n", "a") == 0);
    assert(lexeme_cmp("a\\\nb", "ab") == 0);
    assert(lexeme_cmp("define", "define") == 0);
    assert(lexeme_cmp("de\\\nfine", "define") == 0);
}

void token_list_pop_front_test()
{
    struct error error = { 0 };
    struct token_list list = { 0 };
    token_list_pop_front(&list);

    list = tokenizer("a", NULL, 0, TK_FLAG_NONE, &error);
    token_list_pop_front(&list);

    list = tokenizer("a,", NULL, 0, TK_FLAG_NONE, &error);
    token_list_pop_front(&list);

    list = tokenizer("a,b", NULL, 0, TK_FLAG_NONE, &error);
    token_list_pop_front(&list);
}

void token_list_pop_back_test()
{
    struct error error = { 0 };
    struct token_list list = { 0 };
    token_list_pop_back(&list);

    /*pop back quando so tem 1*/
    token_list_clear(&list);
    list = tokenizer("a", NULL, 0, TK_FLAG_NONE, &error);
    token_list_pop_back(&list);
    assert(list.head == NULL && list.tail == NULL);


    /*
    * pop bacl com 2
    */
    token_list_clear(&list);
    list = tokenizer("a,", NULL, 0, TK_FLAG_NONE, &error);
    token_list_pop_back(&list);

    assert(strcmp(list.head->lexeme, "a") == 0);

    assert(list.head != NULL &&
        list.head->prev == NULL &&
        list.head->next == NULL &&
        list.tail->prev == NULL &&
        list.tail->next == NULL &&
        list.tail == list.head);

    /*
    * pop back com 3
    */
    list = tokenizer("a,b", NULL, 0, TK_FLAG_NONE, &error);
    token_list_pop_back(&list);
    assert(strcmp(list.head->lexeme, "a") == 0);
    assert(strcmp(list.head->next->lexeme, ",") == 0);
    assert(strcmp(list.tail->lexeme, ",") == 0);
    assert(strcmp(list.tail->prev->lexeme, "a") == 0);
    assert(list.head->prev == NULL);
    assert(list.tail->next == NULL);
}

int token_list_append_list_test()
{
    struct error error = { 0 };

    struct token_list source = { 0 };
    struct token_list dest = tokenizer("a", NULL, 0, TK_FLAG_NONE, &error);
    token_list_append_list(&dest, &source);
    assert(strcmp(dest.head->lexeme, "a") == 0);


    token_list_clear(&source);
    token_list_clear(&dest);
    dest = tokenizer("a", NULL, 0, TK_FLAG_NONE, &error);
    token_list_append_list(&dest, &source);

    assert(strcmp(dest.head->lexeme, "a") == 0);

    token_list_clear(&source);
    token_list_clear(&dest);
    source = tokenizer("a,", NULL, 0, TK_FLAG_NONE, &error);
    dest = tokenizer("1", NULL, 0, TK_FLAG_NONE, &error);
    token_list_append_list(&dest, &source);
    assert(strcmp(dest.head->lexeme, "1") == 0);
    assert(strcmp(dest.tail->lexeme, ",") == 0);
    assert(dest.tail->next == NULL);
    assert(dest.head->next->next == dest.tail);
    assert(dest.tail->prev->prev == dest.head);

    return 0;
}

void test_collect()
{
    const char* input =
        "#define F(A, B) A ## B\n"
        "F(a \n, b)";

    const char* output =
        "ab"
        ;

    struct error error = { 0 };
    assert(test_preprocessor_in_out(input, output, &error) == 0);

}

void test_va_opt_0()
{
    const char* input =
        "#define F(...)  f(0 __VA_OPT__(,) __VA_ARGS__)\n"
        "F(a, b, c)";
    const char* output =
        "f(0, a, b, c)";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void test_va_opt_1()
{
    const char* input =
        "#define F(...)  f(0 __VA_OPT__(,) __VA_ARGS__)\n"
        "F()";
    const char* output =
        "f(0)";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void test_va_opt()
{
    //TODO esta falando um  monte de casos ainda ...
    // //http://www.open-std.org/jtc1/sc22/wg14/www/docs/n2856.htm
    // 
    //demstra que primerio
    //tem que expandir varargs
    //para depois concluir se era vazio ou nao
    //
    const char* input =
        "#define F(...)  f(0 __VA_OPT__(,) __VA_ARGS__)\n"
        "#define EMPTY\n"
        "F(EMPTY)";
    const char* output =
        "f(0)";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}


void test_empty_va_args()
{
    const char* input = "#define M(a, ...) a, __VA_ARGS__\n"
        "M(1)\n";
    const char* output =
        "1,";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void test_va_args_single()
{
    const char* input =
        "#define F(...) __VA_ARGS__\n"
        "F(1, 2)";
    const char* output =
        "1, 2";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void test_va_args_extra_args()
{
    const char* input =
        "#define F(a, ...) a __VA_ARGS__\n"
        "F(0, 1, 2)";
    const char* output =
        "0 1, 2";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}


void test_empty_va_args_empty()
{
    const char* input =
        "#define F(...) a __VA_ARGS__\n"
        "F()";
    const char* output =
        "a";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void test_defined()
{
    const char* input =
        "#if defined X || defined (X)\n"
        "A\n"
        "#else\n"
        "B\n"
        "#endif\n";
    const char* output =
        "B";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void testline()
{
    const char* input =
        "#define M \\\n"
        "        a\\\n"
        "        b\n"
        "M";
    const char* output =
        "a b";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void ifelse()
{
    const char* input =
        "#if 1\n"
        "A\n"
        "#else\n"
        "B\n"
        "#endif\n";
    const char* output =
        "A";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void T1()
{
    const char* input =
        "#define f(a) f(x * (a))\n"
        "f(2 * (0, 1))";
    const char* output =
        "f(x * (2 * (0, 1)))";
    //se f tivesse 2 parametros
    //error: too few arguments provided to function-like macro invocation
    //se f nao tivesse nenhum ou menus
    //too many arguments provided to function-like macro invocation
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

int EXAMPLE5()
{
    /*
    //EXAMPLE 5 To illustrate the rules for placemarker preprocessing tokens, the sequence

    //const char* input =
    //"#define t(x,y,z) x ## y ## z\n"
    //"int j[] = {t(+1,2,3), t(,4,5), t(6,,7), t(8,9,),t(10,,), t(,11,), t(,,12), t(,,) };";

    //const char* output =
      //  "int j[] = {+123, 45, 67, 89,10, 11, 12, };";

    const char* input =
        "#define t(x,y,z) x ## y ## z\n"
        "t(+1,2,3)";

    const char* output =
        "int j[] = {+123, 45, 67, 89,10, 11, 12, };";

    //se f tivesse 2 parametros
    //error: too few arguments provided to function-like macro invocation

    //se f nao tivesse nenhum ou menus
    //too many arguments provided to function-like macro invocation
    //test_preprocessor_in_out(input, output);
    */
    return 0;
}

void recursivetest1()
{
    //acho que este vai sero caso que precisa do hidden set.
    const char* input =
        "#define x 2\n"
        "#define f(a) f(x * (a))\n"
        "#define z z[0]\n"
        "f(f(z))";
    //resultado gcc da
    //const char* output =
    //  "f(2 * (f(2 * (z[0]))))";
    const char* output =
        "f(2 * (f(z[0])))";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void rectest()
{
    const char* input =
        "#define x 2\n"
        "#define f(a) f(x * (a))\n"
        "#define g f\n"
        "#define z z[0]\n"
        "f(y + 1) + f(f(z)) % t(t(g)(0) + t)(1);";
    //GCC
    //const char* output =
    //  "f(2 * (y + 1)) + f(2 * (f(2 * (z[0])))) % t(t(f)(0) + t)(1);";
    const char* output =
        "f(2 * (y + 1)) + f(2 * (f(z[0]))) % t(t(f)(0) + t)(1);";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void emptycall()
{
    const char* input =
        "#define F(x) x\n"
        "F()"
        ;
    const char* output =
        ""
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void semiempty()
{
    const char* input =
        "#define F(x,y) x ## y\n"
        "F(1,)"
        ;
    const char* output =
        "1"
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void calling_one_arg_with_empty_arg()
{
    const char* input =
        "#define F(a) # a\n"
        "F()"
        ;
    const char* output =
        "\"\""
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}


void test_argument_with_parentesis()
{
    const char* input =
        "#define F(a, b) a ## b\n"
        "F((1, 2, 3),4)"
        ;
    const char* output =
        "(1, 2, 3)4"
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void two_empty_arguments()
{
    const char* input =
        "#define F(a, b) a ## b\n"
        "F(,)\n"
        ;
    const char* output =
        ""
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void simple_object_macro()
{
    const char* input = "#define B b\n"
        "#define M a B\n"
        "M\n"
        "c\n";
    const char* output =
        "a b\n"
        "c";
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}


void test_one_file()
{
    assert(test_preprocessor_in_out_using_file("tests/pre_debug.c") == 0);
}

void test2()
{
    const char* input =
        "#define F(a, b) 1 a ## b 4\n"
        "F(  2  ,  3 )"
        ;
    const char* output =
        "1 23 4"
        ;

    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}


void test3()
{
#if 0
    const char* input =
        "#define F(a, b) 1 a ## 3 4\n"
        "F(  2   )"
        ;
    const char* output =
        "1 23 4"
        ;
#endif
    //este erro falta parametro b
    //too few arguments provided to function - like macro invocation
    //test_preprocessor_in_out(input, output, NULL);
}


void tetris()
{
    const char* input =
        "#define D(a) a\n"
        "#define C(a) a\n"
        "#define F(a) a\n"
        "#define M F\n"
        "M(F)(C)(D)e"
        ;
    const char* output =
        "De"
        ;
    struct error error = { 0 };
    struct token_list list = tokenizer(input, "source", 0, TK_FLAG_NONE, &error);

    struct preprocessor_ctx ctx = { 0 };
    ctx.printf = printf;

    struct token_list r = preprocessor(&ctx, &list, 0, &error);

    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void recursive_macro_expansion()
{
    const char* input =
        "#define A 3 4 B\n"
        "#define B 1 2 A\n"
        "B";
    const char* output =
        "1 2 3 4 B"
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void empty_and_no_args()
{
    const char* input =
        "#define F() 1\n"
        "F()";
    const char* output =
        "1"
        ;
    assert(test_preprocessor_in_out(input, output, NULL) == 0);
}

void test4()
{
    const char* input =
        "#define F(a, b) 1 2 ## a 4\n"
        "F(  3   )"
        ;
    const char* output =
        "1 23 4"
        ;

    struct error error = { 0 };
    test_preprocessor_in_out(input, output, &error);

    //esperado um erro (falta mensagem)
    //too few arguments provided to function-like macro invocation F (3)
    //engracado msc eh warning  warning C4003: not enough actual parameters for macro 'F'
    assert(error.code != 0);
}

void test_string()
{
    const char* input =
        "#define M(a, b) a # b\n"
        "M(A, \"B\")"
        ;
    const char* output =
        "A \"\\\"B\\\"\""
        ;

    struct error error = { 0 };
    test_preprocessor_in_out(input, output, &error);

    assert(error.code == 0);
}

void test6()
{
    /*

    #define Y
    #define X defined (Y)

    #if X
    #warning !
    #endif
    */
}

void testerror()
{
    /*
    const char* input =
        "#define F(a) #b\n"
        "F(1)\n"
        ;
    const char* output =
        ""
        ;
    //tem que dar error
    test_preprocessor_in_out(input, output, NULL);
    */
}

int test_preprocessor_expression(const char* expr, long long expected)
{
    struct error error = { 0 };
    struct preprocessor_ctx ctx = { 0 };
    ctx.printf = printf;
    struct token_list r = { 0 };
    struct token_list input = tokenizer(expr, "", 0, TK_FLAG_NONE, &error);

    long long result = preprocessor_constant_expression(&ctx, &r, &input, 0, &error);
    return result == expected ? 0 : 1;
}

int test_expression()
{

    //TODO preprocessador eh sempre long long.. signed passadno maior
    //deve dar erro

    if (test_preprocessor_expression("true", true) != 0)
        return __LINE__;

    if (test_preprocessor_expression("false", false) != 0)
        return __LINE__;


    if (test_preprocessor_expression("'A'", 'A') != 0)
        return __LINE__;

    if (test_preprocessor_expression("'ab'", 'ab') != 0)
        return __LINE__;

    if (test_preprocessor_expression("1+2", 1 + 2) != 0)
        return __LINE__;

    if (test_preprocessor_expression("1 + 2 * 3 / 2 ^ 2 & 4 | 3 % 6 >> 2 << 5 - 4 + !7",
        1 + 2 * 3 / 2 ^ 2 & 4 | 3 % 6 >> 2 << 5 - 4 + !7) != 0)
        return __LINE__;

    if (test_preprocessor_expression("1ull + 2l * 3ll",
        1ull + 2l * 3ll) != 0)
        return __LINE__;


    return 0;
}

int test_concatenation_o()
{
    const char* input =
        "# define F(t1, t2, t3) *i_##t1##_j k\n"
        "F(A, B, C)\n";

    const char* output =
        "*i_A_j k"
        ;

    struct error error = { 0 };
    return test_preprocessor_in_out(input, output, &error);
}

int test_concatenation()
{
    const char* input =
        "#define F(t1, t2, t3) i##j##k\n"
        "F(A, B, C)\n";

    const char* output =
        "ijk"
        ;

    struct error error = { 0 };
    return test_preprocessor_in_out(input, output, &error);


}

void bad_test()
{
    struct error error = { 0 };
    struct token_list list = tokenizer("0xfe-BAD(3)", "source", 0, TK_FLAG_NONE, &error);

    const char* input = "#define BAD(x) ((x) & 0xff)\n"
        "0xfe-BAD(3);";
    const char* output =
        "0xfe-BAD(3);"
        ;

    return test_preprocessor_in_out(input, output, &error);

}
/*
#define A0
#define B0
#define A1(x) x B##x(
#define B1(x) x A##x(
A1(1)1)1)1)1)0))
*/
int test_spaces()
{
    const char* input =
        "#define throw A B\n"
        "throw\n"
        ;
    const char* output =
        "A B"
        ;

    struct error error = { 0 };
    test_preprocessor_in_out(input, output, &error);

    return error.code;
}

int test_stringfy()
{
    const char* input =
        "#define M(T) #T\n"
        "M(unsigned   int)\n"
        ;
    const char* output =
        "\"unsigned int\""
        ;

    struct error error = { 0 };
    test_preprocessor_in_out(input, output, &error);

    return error.code;
}


int test_tokens()
{
    const char* input =
        "L\"s1\" u8\"s2\""
        ;
    struct error error = { 0 };
    struct token_list list
        = tokenizer(input, "", 0, TK_FLAG_NONE, &error);

    if (list.head->next->type != TK_STRING_LITERAL)
    {
        return __LINE__;
    }

    if (list.head->next->next->next->type != TK_STRING_LITERAL)
    {
        return __LINE__;
    }

    return error.code;
}

int test_predefined_macros()
{
    const char* input =
        "__LINE__ __FILE__"
        ;
    const char* output =
        "1 \"source\""
        ;

    struct error error = { 0 };
    struct token_list list = tokenizer(input, "", 0, TK_FLAG_NONE, &error);

    struct preprocessor_ctx prectx = { 0 };
    prectx.macros.capacity = 5000;
    add_standard_macros(&prectx, &error);
    struct token_list list2 = preprocessor(&prectx, &list, 0, &error);


    const char* result = print_preprocessed_to_string(list2.head);
    if (result == NULL)
    {
        result = strdup("");
    }
    if (strcmp(result, output) != 0)
    {
    }


    return 0;
}

int test_utf8()
{
    struct error error = { 0 };
    const char* input =
        "u8\"maçã\"";
    struct token_list list = tokenizer(input, "source", 0, TK_FLAG_NONE, &error);
    if (strcmp(list.head->next->lexeme, u8"u8\"maçã\"") != 0)
        return __LINE__;
    token_list_destroy(&list);
    return 0;
}


int test_line_continuation()
{
    struct error error = { 0 };

    const char* input =
        "#define A B \\\n"
        "C\n"
        "A";

    const char* output =
        "1 \"source\""
        ;


    struct token_list list = tokenizer(input, "", 0, TK_FLAG_NONE, &error);

    struct preprocessor_ctx prectx = { 0 };
    prectx.macros.capacity = 5000;

    struct token_list list2 = preprocessor(&prectx, &list, 0, &error);

    const char* result = print_preprocessed_to_string(list2.head);
    if (result == NULL)
    {
        result = strdup("");
    }
    if (strcmp(result, output) != 0)
    {
    }


    return 0;
}

#endif