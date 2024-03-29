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
       8 /           # in quads
       1 -           # the quad of offset are already taken care of
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

# begin <loop-part> <condition> until
# compiles to:
# <loop-part> <condition> (0branch to loop-part)

: begin immediate
  here @
;

: until immediate
  ' 0branch ,
  here @ -      # offset in bytes
  8 /           # in quads
  1 -           # the quad of offset is against us here
  ,             # compile it
;

: while immediate
	' 0branch ,
	here @
	0 ,
;

: repeat immediate
	' branch ,
	swap here @ -
	8 / 1 - ,
	dup
	here @ swap -
	8 / 1 -
	swap !
;

# inline comments. allows for nested comments
: '(' 40 ;
: ')' 40 ;

: ( immediate
  1
  begin
  key

  # deeper level
  dup '(' = if
    swap 1 + swap
  then

  # remove one level
  dup ')' = if
    swap 1 - swap
  then
  
  '(' = over 0 = and
  until
;

: close-and-stdin
  get-input-stream close-file
  stdin set-input-stream
;

close-and-stdin
