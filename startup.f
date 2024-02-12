# define quotient and remainder from divmod
: / divmod drop ;
: % divmod swap drop ;

# control structures
: if immediate
     ' 0branch ,
     here @
     0 ,
     ;

: then immediate
       dup
       here @ swap - # offset in bytes
       8 /           # in 8bytes
       1 -           # the 8bytes of offset are already taken care of
       swap !
;

: else immediate
       ' branch ,
       here @
       0 ,
       swap dup
       # same as then
       here @ swap -
       8 / 1 -
       swap !
;

: t0
  1 if 2 else 3 then 4 5
;

: close-and-stdin
  get-input-stream close-file
  stdin set-input-stream
;

close-and-stdin

# define comments. Crude implementation: does not care about nested parentheses
: ( immediate
    
  ;

: '
    word find-word code-word
;

: ['] immediate
    word find-word code-word
;
