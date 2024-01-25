#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

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
} forth_t;

typedef void prim_t(forth_t*, u8*);

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

void printstack(forth_t* f)
{
    printf("~ ");
    for(u64* p = f->stack ; p < f->top_stack ; p++)
    {
	printf("%ld - ", *p);
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

    // add two test functions: one that puts 42 on the stack, and one
    // that prints the top element
    push_primitive_word(f, "42", 0, push42);
    push_primitive_word(f, "*", 0, mult);
    
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

int main()
{
    forth_t* f = new_forth();

    printstack(f);

    run_word(f, find_word(f, "42"));
    printstack(f);
    
    run_word(f, find_word(f, "42"));
    printstack(f);

    run_word(f, find_word(f, "*"));
    printstack(f);
    
    free_forth(f);
    return 0;
}
