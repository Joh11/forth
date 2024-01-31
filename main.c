#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>

/* 
   Anatomy of a forth word, stored in memory (in this order):
   - 8 bytes :: pointer to the next word (or NULL if this is the next free word)
   - 1 byte  :: some flags, to deal with immediate (not sure for now)
   - 8 bytes :: codeword
   - word name (null terminated)
   - if forth word, nullptr terminated array of pointers to other words

a possible solution:
- a field "codeword", with type u8*
- for C primitives: just a function pointer to the actual code to run
- for forth words: a function pointer to the interpreting function,
  which takes as input an array of pointers to forth words. It runs
  all of them in sequence, until it reaches a null pointer (the end of
  the array, equivalent to the NEXT macro in jonesforth)
*/

#define cast(type, val) ((type)(val))
typedef uint8_t u8;
typedef uint64_t u64;

#define IMMEDIATE_FLAG 0x1

typedef enum
{
    NORMAL_STATE, COMPILE_STATE
} interp_state_t;

typedef struct
{
    u8* words;
    u64* stack;

    // constants that dictate the size of the word array and the stack
    size_t word_size;
    size_t stack_size;

    // pointers to the top of the two stacks (i.e. the next free one)
    u8* top_word;
    u64* top_stack;

    // by default stdin, but can be changed to i.e. read from a file
    // or a string
    FILE* input_stream;
    
    interp_state_t state;
} forth_t;

typedef void prim_t(forth_t*, u8*);

void run_word(forth_t* f, u8* word);
u8* find_word(forth_t* f, const char* name);
    
void push42(forth_t* f, u8* code)
{
    *f->top_stack = 42;
    ++f->top_stack;
}

void mult(forth_t* f, u8* code)
{
    assert(f->top_stack - f->stack >= 2);
    long a = cast(long, f->top_stack[-2]);
    long b = cast(long, f->top_stack[-1]);

    f->top_stack[-2] = a * b;
    --f->top_stack;
}

void dup(forth_t* f, u8* code)
{
    assert(f->top_stack - f->stack >= 1);
    long x = cast(long, f->top_stack[-1]);

    *f->top_stack = x;
    ++f->top_stack;
}

void key(forth_t* f, u8* code)
{
    char c = fgetc(f->input_stream);
    if(c == EOF)
    {
	printf("[failure in getchar]\n");
	if(feof(f->input_stream))
	    printf("[due to end of file]\n");
	else
	    printf("[due to something else]\n");

	return;
    }
    
    *f->top_stack = c;
    ++f->top_stack;
}

void word(forth_t* f, u8* code)
{
    static char buf[64];
    memset(buf, 0, 64);
    
    char c;
    while(isspace(c = fgetc(f->input_stream))); // skip whitespace
    size_t n = 0;
    for(; n < 64; ++n)
    {
	buf[n] = c;
	c = fgetc(f->input_stream);
	if(isspace(c) || c == EOF) break;
    }
    
    if(c == EOF)
    {
	printf("[failure in getchar]\n");
	if(feof(f->input_stream))
	    printf("[due to end of file]\n");
	else
	    printf("[due to something else]\n");

	return;
    }
    
    *f->top_stack = cast(u64, &buf);
    ++f->top_stack;
}

void emit(forth_t* f, u8* code)
{
    assert(f->top_stack - f->stack >= 1);
    assert(f->top_stack[-1] < 256); // only ASCII
    char c = cast(char, f->top_stack[-1]);
    putchar(c);
    
    --f->top_stack;
}

void interpret(forth_t* f, u8* code)
{
    code += 17;
    // find the start of the instructions first (TODO store it to make
    // it faster)
    while(*code) ++code; // find null char
    ++code;

    // this is an array of pointers to words
    u8** ps = cast(u8**, code);

    while(*ps)
    {
	run_word(f, *ps);
	++ps;
    }
}

void semicolon(forth_t* f, u8* code)
{
    assert(f->state == COMPILE_STATE); // must be in compile mode
    f->state = NORMAL_STATE;

    // this is stupid, but I need to go over the whole dict to find
    // the actual word we are compiling
    u8* w = f->words;
    while(*w) w = *cast(u8**, w);
    
    *cast(u8**, w) = f->top_word;
    f->top_word = w;
}

void colon(forth_t* f, u8* code)
{
    // consume the next word
    run_word(f, find_word(f, "word"));
    const char* name = cast(const char*, f->top_stack[-1]);
    --f->top_stack;
    
    size_t namelen = strlen(name) + 1; // include null char
    size_t wordlen = 8 + 1 + 8 + namelen;
    
    assert(f->state == NORMAL_STATE); // must be in normal mode
    f->state = COMPILE_STATE;
    
    *cast(u64*, f->top_word + 9) = cast(u64, interpret);
    strcpy(f->top_word + 17, name);
    
    f->top_word += wordlen;
}

void printstack(forth_t* f)
{
    printf("stack: ");
    u64* p = f->stack;
    for(; p < f->top_stack - 1 ; p++)
    {
	printf("%ld ", *p);
    }
    if(p < f->top_stack) printf("%ld", *p);
    printf("\n");
}

void printstack_prim(forth_t* f, u8* code)
{
    printstack(f);
}

void printwords(forth_t* f, u8* code)
{
    printf("words: ");
    u8* p = f->words;
    while(*p)
    {
	printf("%s", cast(const char*, p + 17));
	p = *cast(u8**, p);
	if(*p) printf(" ");
    }
    printf("\n");
}

u8* push_primitive_word(forth_t* f, const char* name, u8 flags, prim_t* primitive)
{
    size_t namelen = strlen(name) + 1; // include null char
    size_t wordlen = 8 + 1 + 8 + namelen;

    *cast(u8**, f->top_word) = f->top_word + wordlen; // next word
    f->top_word[8] = flags; // flags
    *cast(u64*, f->top_word + 9) = cast(u64, primitive);
    strcpy(f->top_word + 17, name);

    u8* word = f->top_word;
    f->top_word += wordlen;
    return word;
}

u8* push_forth_word(forth_t* f, const char* name, u8 flags, u8** words)
{
    size_t namelen = strlen(name) + 1; // include null char
    // compute the number of words
    size_t nwords = 0;
    while(words[nwords]) nwords++;
    
    size_t wordlen = 8 + 1 + 8 + namelen + nwords * 8;
    
    *cast(u8**, f->top_word) = f->top_word + wordlen; // next word
    f->top_word[8] = 0; // flags
    *cast(u64*, f->top_word + 9) = cast(u64, interpret);
    strcpy(f->top_word + 17, name);

    size_t n = 0;
    for(; n < nwords ; ++n)
	cast(u8**, f->top_word + 20)[n] = words[n];
    (cast(u8**, f->top_word + 20))[n] = NULL;

    u8* word = f->top_word;
    f->top_word += wordlen;
    return word;
}

bool is_immediate_word(u8* word)
{
    return word[8] & IMMEDIATE_FLAG;
}

forth_t* new_forth()
{
    const size_t word_size = 65536;
    const size_t stack_size = 16384;

    forth_t* f = malloc(sizeof(forth_t));
    f->word_size = word_size;
    f->stack_size = stack_size;

    f->words = malloc(word_size * sizeof(u8));
    memset(f->words, 0, word_size * sizeof(u8));
    f->stack = malloc(stack_size * sizeof(u64));
    memset(f->stack, 0, stack_size * sizeof(u64));

    f->top_word = f->words;
    f->top_stack = f->stack;

    // by default, read from stdin
    f->input_stream = stdin;

    push_primitive_word(f, "42", 0, push42);
    push_primitive_word(f, "*", 0, mult);
    push_primitive_word(f, "dup", 0, dup);
    push_primitive_word(f, "key", 0, key);
    push_primitive_word(f, "emit", 0, emit);
    push_primitive_word(f, "word", 0, word);
    push_primitive_word(f, ":", 0, colon);
    push_primitive_word(f, ";", IMMEDIATE_FLAG, semicolon);

    push_primitive_word(f, ".s", 0, printstack_prim);
    push_primitive_word(f, ".w", 0, printwords);
    
    // push_forth_word(f, "sq", 0, (u8*[]){find_word(f, "dup"), find_word(f, "*"), NULL});
    
    return f;
}

void free_forth(forth_t* f)
{
    free(f->words);
    free(f->stack);
    free(f);
}

void run_word(forth_t* f, u8* word)
{    
    void (*p)(forth_t*, u8*) = *cast(void (**)(forth_t*, u8*), word + 9);
    p(f, word);
}

u8* find_word(forth_t* f, const char* name)
{
    u8* word = f->words;
    while(*cast(u8**, word) != NULL && strcmp(name, word + 17) != 0)
	word = *cast(u8**, word);

    return *cast(u8**, word) ? word : NULL;
}

void repl(forth_t* f)
{
    u8* word = find_word(f, "word");

    while(1)
    {
	run_word(f, word);
	const char* wordstring = cast(const char*, f->top_stack[-1]);
	--f->top_stack;
	
	if(f->state == NORMAL_STATE)
	{
	    u8* next = find_word(f, wordstring);
	    assert(next);

	    run_word(f, next);
	}
	else if(f->state == COMPILE_STATE)
	{
	    // TODO deal with numbers first
	    u8* next = find_word(f, wordstring);
	    if(!next) printf("failed to find %s", wordstring);
	    assert(next);

	    if(is_immediate_word(next))
		run_word(f, next);
	    else
	    {
		*cast(u8**, f->top_word) = next;
		f->top_word += 8;
	    }
	}
	else assert(0); // should not happen
    }
}

int main()
{
    forth_t* f = new_forth();

    /* run_word(f, find_word(f, "42")); */
    /* printstack(f); */
    
    /* run_word(f, find_word(f, "dup")); */
    /* printstack(f); */

    /* run_word(f, find_word(f, "*")); */
    /* printstack(f); */

    /* run_word(f, find_word(f, "sq")); */
    /* printstack(f); */

    /* run_word(f, find_word(f, "key")); */
    /* printstack(f); */
    /* run_word(f, find_word(f, "emit")); */
    /* printstack(f); */

    repl(f);
    
    run_word(f, find_word(f, "word"));
    printstack(f);
    printf("word read: %s\n", (char*)f->top_stack[-1]);

    const char* txt = "je suis un test";
    FILE* s = fmemopen(txt, strlen(txt) + 1, "r");

    f->input_stream = s;
    run_word(f, find_word(f, "word"));
    printstack(f);
    printf("word read: %s\n", (char*)f->top_stack[-1]);
    fclose(s);
    
    free_forth(f);
    return 0;
}
