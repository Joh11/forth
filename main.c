#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

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

void push42(forth_t* f, u64* code)
{
    *f->top_stack = 42;
    ++f->top_stack;
}

void printstack(forth_t* f)
{
    for(u64* p = f->stack ; p < f->top_stack ; p++)
    {
	printf("%ld - ", *p);
    }
    printf("\n");
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

    // first function called 42, thus need 8 + 1 + 8 + 3 = 20 bytes
    *cast(u8**, f->top_word) = f->top_word + 20; // next word
    f->top_word[8] = 0; // flags
    *cast(u64*, f->top_word + 9) = cast(u64, push42);
    f->top_word[17] = '4';
    f->top_word[18] = '2';
    f->top_word[19] = '\0';
    
    f->top_word += 20;
    
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
    // debug data
    printf("current word: %p\n", word);
    printf("next word: %p\n", *cast(u64**, word));
    printf("word name: %s\n", word + 17);

    void (*p)(forth_t*, u8*) = *cast(void (**)(forth_t*, u8*), word + 9);
    p(f, word);
}

int main()
{
    forth_t* f = new_forth();

    printstack(f);

    run_word(f, f->words);
    
    printstack(f);
    
    free_forth(f);
    return 0;
}
