#define _GNU_SOURCE // needed for fmemopen (string -> FILE*)

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>

/* 
   An adaptation of JONESFORTH, written in C.  

   I differ from usual FORTH conventions by using null-terminated
   strings, # for comments, and more verbose words (e.g. code-word
   instead of >CFA)

   Anatomy of a forth word, stored in memory (in this order):
   - 8 bytes :: pointer to the next word (or NULL if this is the next free word)
   - 1 byte  :: some flags, to deal with immediate (not sure for now)
   - word name (null terminated), and some padding to ensure 8-bytes
     alignment
   - 8 bytes :: codeword
   - if forth word, nullptr terminated array of pointers to other words

a solution:
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

u64 stack_size(forth_t* f) { return f->top_stack - f->stack; }
u64 rstack_size(forth_t* f) { return f->top_rstack - f->rstack; }

// (return stack)
u64 rpop(forth_t* f) { assert(rstack_size(f) > 0); return *(--f->top_rstack); }
void rpush(forth_t* f, u64 p) { assert(rstack_size(f) < f->rstack_size);
    *f->top_rstack = p; ++(f->top_rstack); }

// (value stack)
u64 pop(forth_t* f) { assert(stack_size(f) > 0); return *(--f->top_stack); }
void push(forth_t* f, u64 p) { assert(stack_size(f) < f->stack_size);
    *f->top_stack = p; ++(f->top_stack); }

void docol(forth_t* f)
{
    rpush(f, cast(u64, f->next));
    f->next = f->current + 1;
    // no need to set f->current, since it will be taken care of by
    // the end of the interpret loop (i.e. NEXT in jonesforth)
}

void doexit(forth_t* f) { f->next = cast(u64*, rpop(f)); }

void lit(forth_t* f)
{
    push(f, *f->next);
    f->next += 1;
}

void branch(forth_t* f)
{
    i64 offset = *f->next; f->next++;
    f->next += offset;
}

void zero_branch(forth_t* f)
{
    i64 offset = *f->next; f->next++;
    if(pop(f) == 0) f->next += offset;
}

void is_compiling(forth_t* f) { push(f, f->state); }

void set_immediate_mode(forth_t* f) { f->state = NORMAL_STATE; }
void set_compile_mode(forth_t* f) { f->state = COMPILE_STATE; }

void doerror(forth_t* f) { exit(EXIT_FAILURE); }

void dorun_word(forth_t* f) { f->next = cast(u64*, pop(f)); }

void dostack_size(forth_t* f) { push(f, stack_size(f)); }

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

void doparse_number(forth_t* f)
{
    const char* txt = cast(const char*, pop(f));
    push(f, 0);

    push(f, parse_number(txt, cast(i64*, f->top_stack - 1)));
}

// some arithmetic stuff

void add(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a + b;
}

void mult(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a * b;
}

void sub(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a - b;
}

void divmod(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    f->top_stack[-2] = a / b;
    f->top_stack[-1] = a % b;
}

void eq(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a == b;
}

void lt(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a < b;
}

void gt(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a > b;
}

void leq(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a <= b;
}

void geq(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a >= b;
}

// logical stuff

void donot(forth_t* f) { push(f, !pop(f)); }
void doand(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a && b;
}

void door(forth_t* f)
{
    assert(stack_size(f) >= 2);
    i64 a = cast(i64, f->top_stack[-2]);
    i64 b = cast(i64, f->top_stack[-1]);

    pop(f);
    f->top_stack[-1] = a || b;
}

// stack manipulation

void drop(forth_t* f) { pop(f); }

void swap(forth_t* f)
{
    assert(stack_size(f) >= 2);
    u64 a = pop(f);
    u64 b = pop(f);
    push(f, a);
    push(f, b);
}

void dup(forth_t* f)
{
    assert(f->top_stack - f->stack >= 1);
    u64 x = cast(u64, f->top_stack[-1]);

    *f->top_stack = x;
    ++f->top_stack;
}

void over(forth_t* f)
{
    assert(f->top_stack - f->stack >= 2);
    u64 x = cast(u64, f->top_stack[-2]);
    push(f, x);
}


// IO stuff

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

    // skip comments before the word
    while(c == '#')
    {
	while(true)
	{
	    c = fgetc(f->input_stream);
	    if(c == '\n' || c == EOF) break;
	}
	while(isspace(c = fgetc(f->input_stream))); // skip whitespace again
	// if(c != EOF) c = fgetc(f->input_stream);
    }
    
    for(; n < 64; ++n)
    {
	buf[n] = c;
	c = fgetc(f->input_stream);
	if(isspace(c) || c == EOF || c == '#') break;
    }

    // skip comments after the word
    if(c == '#')
    {
	while(true)
	{
	    c = fgetc(f->input_stream);
	    if(c == '\n' || c == EOF) break;
	}
    }
    
    if(n == 0 && c == EOF)
    {
	printf("[failure in getchar]\n");
	if(feof(f->input_stream))
	    printf("[due to end of file]\n");
	else
	    printf("[due to something else]\n");

	return;
    }
    
    push(f, cast(u64, buf));
}

void emit(forth_t* f)
{
    assert(f->top_stack - f->stack >= 1);
    assert(f->top_stack[-1] < 256); // only ASCII
    
    char c = cast(char, pop(f));
    putchar(c);
}

void tell(forth_t* f) { printf("%s", cast(const char*, pop(f))); }

void dofind_word(forth_t* f)
{
    push(f, cast(u64, find_word(f, cast(const char*, pop(f)))));
}

void dostdin(forth_t* f) { push(f, cast(u64, stdin)); }

void set_input_stream(forth_t* f) { f->input_stream = cast(FILE*, pop(f)); }
void get_input_stream(forth_t* f) { push(f, cast(u64, f->input_stream)); }

void open_read_file(forth_t* f)
{
    assert(stack_size(f) >= 1);
    push(f, cast(u64, fopen(cast(const char*, pop(f)), "r")));
}

void close_file(forth_t* f)
{
    assert(stack_size(f) >= 1);
    fclose(cast(FILE*, pop(f)));
}

u64* codeword(u8* word)
{
    u8* start = word;
    
    word += 9; // skip link ptr and flag byte
    
    while(*word) word++; // skip word name
    word++; // null char
    
    // now align to 8bytes boundary
    while((word - start) % 8 != 0) word++;
    
    return cast(u64*, word);
}

void docodeword(forth_t* f)
{
    push(f, cast(u64, codeword(cast(u8*, pop(f)))));
}

u8* wordname(u8* word) { return word + 9; }
u8* wordtag(u8* word) { return word + 8; }

void semicolon(forth_t* f)
{
    assert(f->state == COMPILE_STATE); // must be in compile mode
    f->state = NORMAL_STATE;

    // put exit at the end to close the word
    *cast(u64**, f->here) = codeword(find_word(f, "exit"));
    f->here += 8;
}

void here(forth_t* f) { push(f, cast(u64, &f->here)); }
void latest(forth_t* f) { push(f, cast(u64, &f->latest)); }

void fetch(forth_t* f) { push(f, *cast(u64*, pop(f))); }
void store(forth_t* f)
{
    u64 addr = pop(f);
    u64 val = pop(f);
    *cast(u64*, addr) = val;
}


void colon(forth_t* f)
{
    // consume the next word
    run_word(f, find_word(f, "word"));
    const char* name = cast(const char*, pop(f));
    
    size_t namelen = strlen(name) + 1; // include null char
    // align to 8 bytes boundary, + 1 because flag already misaligns
    if((namelen + 1) % 8 != 0) namelen += 8 - ((namelen + 1) % 8);
    size_t wordlen = 8 + 1 + 8 + namelen;
    
    assert(f->state == NORMAL_STATE); // must be in normal mode
    f->state = COMPILE_STATE;

    *cast(u8**, f->here) = f->latest;
    f->here[9] = 0; // flags

    // TODO maybe do it smarter (also do it for the 3 other functions
    // dealing with namelen)
    memset(wordname(f->here), 0, namelen);
    strcpy(cast(char*, wordname(f->here)), name);
    
    *codeword(f->here) = cast(u64, docol);

    f->latest = f->here;
    f->here += wordlen;
}

void comma(forth_t* f)
{
    *cast(u64*, f->here) = pop(f);
    f->here += 8; // because here is a u8*, not u64* !
}

void tick(forth_t* f)
{
    push(f, *f->next);
    f->next += 1;
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
    // align to 8 bytes boundary, + 1 because flag already misaligns
    if((namelen + 1) % 8 != 0) namelen += 8 - ((namelen + 1) % 8);
    // TODO clean before strcpy
    size_t wordlen = 8 + 1 + namelen + 8;

    *cast(u8**, f->here) = f->latest; // next word
    f->here[8] = flags; // flags
    
    memset(f->here + 9, 0, namelen);
    strcpy(cast(char*, f->here + 9), name);
    
    *cast(u64*, f->here + 9 + namelen) = cast(u64, primitive);

    f->latest = f->here;
    f->here += wordlen;
    return f->latest;
}

u8* push_forth_word(forth_t* f, const char* name, u8 flags, u8** words)
{
    size_t namelen = strlen(name) + 1; // include null char
    // align to 8 bytes boundary, + 1 because flag already misaligns
    if((namelen + 1) % 8 != 0) namelen += 8 - ((namelen + 1) % 8);
    // TODO clean before strcpy
    // compute the number of words
    size_t nwords = 0;
    while(words[nwords]) nwords++;
    
    size_t wordlen = 8 + 1 + 8 + namelen + nwords * 8;
    
    *cast(u8**, f->here) = f->latest; // next word
    f->here[8] = flags;
    memset(f->here + 9, 0, namelen);
    strcpy(cast(char*, f->here + 9), name);
    
    *cast(u64*, f->here + 9 + namelen) = cast(u64, docol);

    size_t n = 0;
    for(; n < nwords ; ++n)
	cast(u64*, f->here + 9 + namelen + 8)[n] = cast(u64, codeword(words[n])); // to link to codeword
    (cast(u64*, f->here + 9 + namelen + 8))[n] = cast(u64, codeword(find_word(f, "exit")));
    // link to codeword of exit, TODO make it better
    
    f->latest = f->here;
    f->here += wordlen + 8; // need +8 for exit
    return f->latest;
}

// same, but does not look for codewords; just put the content
// straight in the body of the word; no need for exit though
u8* push_forth_word_raw(forth_t* f, const char* name, u8 flags, u64* words)
{
    size_t namelen = strlen(name) + 1; // include null char
    // align to 8 bytes boundary, + 1 because flag already misaligns
    if((namelen + 1) % 8 != 0) namelen += 8 - ((namelen + 1) % 8);
    // TODO clean before strcpy
    
    // compute the number of words
    size_t nwords = 0;
    while(words[nwords]) nwords++;
    
    size_t wordlen = 8 + 1 + 8 + namelen + nwords * 8;
    
    *cast(u8**, f->here) = f->latest; // next word
    f->here[8] = flags;
    
    memset(f->here + 9, 0, namelen);
    strcpy(cast(char*, f->here + 9), name);
    
    *cast(u64*, f->here + 9 + namelen) = cast(u64, docol);

    size_t n = 0;
    for(; n < nwords ; ++n)
	cast(u64*, f->here + 9 + namelen + 8)[n] = words[n]; // to link to codeword
    (cast(u64*, f->here + 9 + namelen + 8))[n] = cast(u64, codeword(find_word(f, "exit")));
    // link to codeword of exit, TODO make it better
    
    f->latest = f->here;
    f->here += wordlen + 8; // need +8 for exit
    return f->latest;
}

bool is_immediate_word(u8* word)
{
    return word[8] & IMMEDIATE_FLAG;
}

void immediate(forth_t* f)
{
    assert(f->latest);
    *wordtag(f->latest) |= IMMEDIATE_FLAG;
}

void dumpwords(forth_t* f)
{
    u8* latest = f->latest;
    u64 exitcw = cast(u64, codeword(find_word(f, "exit")));

    while(latest)
    {
	const char* name = cast(char*, wordname(latest));
	u64* cw = codeword(latest);
	
	printf("found%s word %s at %p (cw at %p)\n", is_immediate_word(latest) ? " immediate" : "",
	       name, latest, cw);
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

    // stack manipulation
    push_primitive_word(f, "stack-size", 0, dostack_size);
    push_primitive_word(f, "dup", 0, dup);
    push_primitive_word(f, "over", 0, over);
    push_primitive_word(f, "drop", 0, drop);
    push_primitive_word(f, "swap", 0, swap);
    
    // arithmetic stuff
    push_primitive_word(f, "+", 0, add);
    push_primitive_word(f, "*", 0, mult);
    push_primitive_word(f, "-", 0, sub);
    push_primitive_word(f, "divmod", 0, divmod);
    push_primitive_word(f, "=", 0, eq);
    push_primitive_word(f, "<", 0, lt);
    push_primitive_word(f, ">", 0, gt);
    push_primitive_word(f, "<=", 0, leq);
    push_primitive_word(f, ">=", 0, geq);

    // logical stuff
    push_primitive_word(f, "not", 0, donot);
    push_primitive_word(f, "and", 0, doand);
    push_primitive_word(f, "or", 0, door);
    
    push_primitive_word(f, "docol", 0, docol);
    push_primitive_word(f, "exit", 0, doexit);
    push_primitive_word(f, "is-compiling", 0, is_compiling);
    push_primitive_word(f, "[", IMMEDIATE_FLAG, set_immediate_mode);
    push_primitive_word(f, "]", 0, set_compile_mode);
    push_primitive_word(f, "error", 0, doerror);
    push_primitive_word(f, "run-word", 0, dorun_word);
    push_primitive_word(f, "code-word", 0, docodeword);
        
    push_primitive_word(f, "key", 0, key);
    push_primitive_word(f, "emit", 0, emit);
    push_primitive_word(f, "word", 0, word);
    push_primitive_word(f, "tell", 0, tell);
    push_primitive_word(f, "parse-number", 0, doparse_number);
    push_primitive_word(f, "find-word", 0, dofind_word);
    push_primitive_word(f, ":", 0, colon);
    push_primitive_word(f, ";", IMMEDIATE_FLAG, semicolon);
    push_primitive_word(f, ",", 0, comma);
    push_primitive_word(f, "'", 0, tick);
    push_primitive_word(f, "here", 0, here);
    push_primitive_word(f, "latest", 0, latest);
    push_primitive_word(f, "@", 0, fetch);
    push_primitive_word(f, "!", 0, store);
    push_primitive_word(f, "lit", 0, lit);
    push_primitive_word(f, "branch", 0, branch);
    push_primitive_word(f, "0branch", 0, zero_branch);
    push_primitive_word(f, "immediate", IMMEDIATE_FLAG, immediate);
    push_primitive_word(f, "stdin", 0, dostdin);
    push_primitive_word(f, "set-input-stream", 0, set_input_stream);
    push_primitive_word(f, "get-input-stream", 0, get_input_stream);
    push_primitive_word(f, "close-file", 0, close_file);
    push_primitive_word(f, "open-read-file", 0, open_read_file);

    push_primitive_word(f, ".s", 0, printstack);
    push_primitive_word(f, ".w", 0, printwords);
    push_primitive_word(f, ".d", 0, dumpwords);
    
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
	if(strcmp(cast(char*, wordname(latest)), name) == 0)
	    return latest;
	
	latest = *cast(u8**, latest);
    }
    return NULL;
}

void repl(forth_t* f)
{
    u8* word = find_word(f, "word"); assert(word);
    u8* lit = find_word(f, "lit"); assert(lit);

    // startup script (will take care of closing itself)
    f->input_stream = fopen("startup.f", "r");
    assert(f->input_stream);
    
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
		u8* next = find_word(f, wordstring);
		if(!next) printf("failed to find %s\n", wordstring);
		assert(next);
		
		if(is_immediate_word(next))
		{
		    run_word(f, next);
		}
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
