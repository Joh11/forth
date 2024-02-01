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
   - word name (null terminated)
   - 8 bytes :: codeword
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
typedef int64_t i64;

#define IMMEDIATE_FLAG 0x1

typedef enum
{
    NORMAL_STATE, COMPILE_STATE
} interp_state_t;

struct forth_t;

typedef struct
{
    u8* words;
    u64* stack;
    u64* rstack;

    // constants that dictate the size of the word array, the stack
    // and the return stack
    size_t word_size;
    size_t stack_size;
    size_t rstack_size;

    // pointers to the top of the three stacks (i.e. the next free one)
    u8* here;
    u64* top_stack;
    u64* top_rstack;

    u8* latest; // last word defined (NULL if no word)

    // by default stdin, but can be changed to i.e. read from a file
    // or a string
    FILE* input_stream;
    
    interp_state_t state;

    // to match jonesforth's naming:
    u64* next; // %esi
    u64* current; // %eax
} forth_t;

typedef void prim_t(forth_t*);

void run_word(forth_t* f, u8* word);
u8* find_word(forth_t* f, const char* name);

// stack manipulation

// (return stack)
u64 rpop(forth_t* f) { assert(f->top_rstack > f->rstack); return *(--f->top_rstack); }
void rpush(forth_t* f, u64 p) { *f->top_rstack = p; ++(f->top_rstack); }

// (value stack)
u64 pop(forth_t* f) { assert(f->top_stack > f->stack); return *(--f->top_stack); }
void push(forth_t* f, u64 p) { *f->top_stack = p; ++(f->top_stack); }

void docol(forth_t* f)
{
    rpush(f, cast(u64, f->next));
    f->next = f->current + 1;
    // no need to set f->current, since it will be taken care of by
    // the end of the interpret loop (i.e. NEXT in jonesforth)
}

void doexit(forth_t* f)
{
    f->next = cast(u64*, rpop(f));
}

void lit(forth_t* f)
{
    push(f, *f->next);
    f->next += 1;
}

bool parse_number(const char* txt, i64* num)
{
    i64 n = 0;
    bool negative = false;
    if(strlen(txt) == 0) return false;

    // take care of negative sign
    if(*txt == '-')
    {
	negative = true;
	txt++;
	if(strlen(txt) == 0) return false;
    }

    while(*txt != '\0')
    {
	if(isdigit(*txt))
	{
	    n = 10 * n + (*txt - '0');
	    txt++;
	}
	else return false;
    }

    *num = negative ? -n : n;
    return true;
}

void push42(forth_t* f)
{
    *f->top_stack = 42;
    ++f->top_stack;
}

void mult(forth_t* f)
{
    assert(f->top_stack - f->stack >= 2);
    long a = cast(long, f->top_stack[-2]);
    long b = cast(long, f->top_stack[-1]);

    f->top_stack[-2] = a * b;
    --f->top_stack;
}

void dup(forth_t* f)
{
    assert(f->top_stack - f->stack >= 1);
    long x = cast(long, f->top_stack[-1]);

    *f->top_stack = x;
    ++f->top_stack;
}

void key(forth_t* f)
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

    push(f, c);
}

void word(forth_t* f)
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

    push(f, cast(u64, &buf));
}

void emit(forth_t* f)
{
    assert(f->top_stack - f->stack >= 1);
    assert(f->top_stack[-1] < 256); // only ASCII
    
    char c = cast(char, pop(f));
    putchar(c);
}

u64* codeword(u8* word)
{
    word += 9; // skip link ptr and flag byte
    while(*word) word++; // skip word name
    word++; // null char
    return cast(u64*, word);
}

u8* wordname(u8* word) { return word + 9; }

void semicolon(forth_t* f)
{
    assert(f->state == COMPILE_STATE); // must be in compile mode
    f->state = NORMAL_STATE;

    // put exit at the end to close the word
    *cast(u64**, f->here) = codeword(find_word(f, "exit"));
    f->here += 8;
}

void colon(forth_t* f)
{
    // consume the next word
    run_word(f, find_word(f, "word"));
    const char* name = cast(const char*, pop(f));
    
    size_t namelen = strlen(name) + 1; // include null char
    size_t wordlen = 8 + 1 + 8 + namelen;
    
    assert(f->state == NORMAL_STATE); // must be in normal mode
    f->state = COMPILE_STATE;

    *cast(u8**, f->here) = f->latest;
    f->here[9] = 0; // flags
    strcpy(wordname(f->here), name);
    *codeword(f->here) = cast(u64, docol);

    f->latest = f->here;
    f->here += wordlen;
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

void printwords(forth_t* f)
{
    u8* latest = f->latest;
    printf("words: ");
    while(latest)
    {
	printf("%s", wordname(latest));
	printf(" ");

	latest = *cast(u8**, latest);
    }
    printf("\n");
}

u8* push_primitive_word(forth_t* f, const char* name, u8 flags, prim_t* primitive)
{
    size_t namelen = strlen(name) + 1; // include null char
    size_t wordlen = 8 + 1 + namelen + 8;

    *cast(u8**, f->here) = f->latest; // next word
    f->here[8] = flags; // flags
    strcpy(f->here + 9, name);
    *cast(u64*, f->here + 9 + namelen) = cast(u64, primitive);

    f->latest = f->here;
    f->here += wordlen;
    return f->latest;
}

u8* push_forth_word(forth_t* f, const char* name, u8 flags, u8** words)
{
    size_t namelen = strlen(name) + 1; // include null char
    // compute the number of words
    size_t nwords = 0;
    while(words[nwords]) nwords++;
    
    size_t wordlen = 8 + 1 + 8 + namelen + nwords * 8;
    
    *cast(u8**, f->here) = f->latest; // next word
    f->here[8] = flags;
    strcpy(f->here + 9, name);
    *cast(u64*, f->here + 9 + namelen) = cast(u64, docol);

    size_t n = 0;
    for(; n < nwords ; ++n)
	cast(u64*, f->here + 9 + namelen + 8)[n] = cast(u64, codeword(words[n])); // to link to codeword
    (cast(u64*, f->here + 9 + namelen + 8))[n] = cast(u64, codeword(find_word(f, "exit")));
    // link to codeword of exit, TODO make it better
    
    f->latest = f->here;
    f->here += wordlen;
    return f->latest;
}

bool is_immediate_word(u8* word)
{
    return word[8] & IMMEDIATE_FLAG;
}

void dumpwords(forth_t* f)
{
    u8* latest = f->latest;
    u64 exitcw = cast(u64, codeword(find_word(f, "exit")));

    while(latest)
    {
	const char* name = wordname(latest);
	u64* cw = codeword(latest);
	
	printf("found word %s at %p (cw at %p)\n", name, latest, cw);
	if(*cw == cast(u64, docol) && strcmp(name, "docol") != 0)
	{
	    printf("forth word, consisting of: \n");

	    // now also print the content
	    size_t n = 1;
	    while(cw[n] != exitcw)
	    {
		printf("  %p\n", cast(u64*, cw[n]));
		++n;
	    }
	}
	else
	    printf("primitive word\n");
	
	
	printf("\n");
	latest = *cast(u8**, latest);
    }
}

forth_t* new_forth()
{
    const size_t word_size = 65536;
    const size_t stack_size = 16384;
    const size_t rstack_size = 256;

    forth_t* f = malloc(sizeof(forth_t));
    f->word_size = word_size;
    f->stack_size = stack_size;
    f->rstack_size = rstack_size;

    f->words = malloc(word_size * sizeof(u8));
    memset(f->words, 0, word_size * sizeof(u8));
    f->stack = malloc(stack_size * sizeof(u64));
    memset(f->stack, 0, stack_size * sizeof(u64));
    f->rstack = malloc(rstack_size * sizeof(u64));
    memset(f->rstack, 0, rstack_size * sizeof(u64));


    f->here = f->words;
    f->latest = NULL;

    f->top_stack = f->stack;
    f->top_rstack = f->rstack;

    // by default, read from stdin
    f->input_stream = stdin;

    push_primitive_word(f, "42", 0, push42);
    push_primitive_word(f, "*", 0, mult);
    push_primitive_word(f, "docol", 0, docol);
    push_primitive_word(f, "exit", 0, doexit);
    push_primitive_word(f, "dup", 0, dup);
    push_primitive_word(f, "key", 0, key);
    push_primitive_word(f, "emit", 0, emit);
    push_primitive_word(f, "word", 0, word);
    push_primitive_word(f, ":", 0, colon);
    push_primitive_word(f, ";", IMMEDIATE_FLAG, semicolon);
    push_primitive_word(f, "lit", 0, lit);

    push_primitive_word(f, ".s", 0, printstack);
    push_primitive_word(f, ".w", 0, printwords);
    push_primitive_word(f, ".d", 0, dumpwords);
    
    // push_forth_word(f, "sq", 0, (u8*[]){find_word(f, "dup"), find_word(f, "*"), NULL});
    
    return f;
}

void free_forth(forth_t* f)
{
    free(f->words);
    free(f->stack);
    free(f);
}

u8* find_word(forth_t* f, const char* name)
{
    u8* latest = f->latest;
    while(latest)
    {
	if(strcmp(wordname(latest), name) == 0)
	    return latest;
	
	latest = *cast(u8**, latest);
    }
    return NULL;
}

void repl(forth_t* f)
{
    u8* word = find_word(f, "word"); assert(word);
    u8* lit = find_word(f, "lit"); assert(lit);

    while(true)
    {
	run_word(f, word);
	const char* wordstring = cast(const char*, pop(f));
	
	if(f->state == NORMAL_STATE)
	{
	    // see if we can parse a number;
	    i64 num;
	    if(parse_number(wordstring, &num))
	    {
		push(f, cast(u64, num));
	    }
	    else
	    {
		u8* next = find_word(f, wordstring);
		assert(next);
		
		run_word(f, next);
	    }
	}
	else if(f->state == COMPILE_STATE)
	{
	    // see if we can parse a number
	    i64 num;
	    if(parse_number(wordstring, &num))
	    {
		// put LIT, then the number
		*cast(u64**, f->here) = codeword(lit);
		f->here += 8;
		*cast(i64*, f->here) = num;
		f->here += 8;
	    }
	    else
	    {
		// TODO deal with numbers first
		u8* next = find_word(f, wordstring);
		if(!next) printf("failed to find %s", wordstring);
		assert(next);
		
		if(is_immediate_word(next))
		    run_word(f, next);
		else
		{
		    *cast(u64**, f->here) = codeword(next);
		    f->here += 8;
		}
	    }
	}
	else assert(false); // should not happen
    }
}

void run_word(forth_t* f, u8* word)
{
    f->current = codeword(word);
    f->next = NULL;

    void (*p)(forth_t*) = NULL;
    while(true)
    {
	p = *cast(void (**)(forth_t*), f->current);
	p(f);

	if(!f->next) break;
	
	f->current = *cast(u64**, f->next);
	f->next += 1;
    }
}

int main()
{
    forth_t* f = new_forth();
    
    repl(f);
    
    free_forth(f);
    return 0;
}
