: close-and-stdin
  get-input-stream close-file
  stdin set-input-stream
  ;

close-and-stdin

( I need to wrap this in a word, otherwise one cannot both close the
file, and change the input stream to stdin )

# define comments. Crude implementation: does not care about nested parentheses
: ( immediate
    
  ;
