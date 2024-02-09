: close-and-stdin
  get-input-stream close-file
  stdin set-input-stream
;

: /
  divmod drop
;

: if immediate
     ' 0branch ,
     here @
     0 ,
     ;

: then immediate
       dup
       here @ swap - # offset in bytes
       8 / # in 8bytes
       1 - # the 8bytes of offset are already taken care of
       swap !
;

: t0
  0 0branch [ 2 , ] 3 4 5
;

: t1
  0 if 3 then 4 5
;

close-and-stdin





: t2
  ' 0branch
;


( I need to wrap this in a word, otherwise one cannot both close the
file, and change the input stream to stdin )

# define comments. Crude implementation: does not care about nested parentheses
: ( immediate
    
  ;

: '
    word find-word code-word
;

: ['] immediate
    word find-word code-word
;
