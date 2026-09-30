if exists("b:current_syntax")
  finish
endif

syn keyword cathodeKeyword   func if else while for return break continue defer
syn keyword cathodeKeyword   const var struct enum import must export switch
syn keyword cathodeType      int bool string float i8 u8 i16 u16 i32 u32 i64 u64
syn keyword cathodeBoolean   true false null

syn match   cathodeNumber    "\<\d\+\(\.\d\+\)\?\([eE][+-]\?\d\+\)\?\>"
syn match   cathodeNumber    "\<0x\x\+\>"
syn region  cathodeString    start=+"+ skip=+\\"+ end=+"+ contains=cathodeEscape
syn match   cathodeEscape    "\\." contained
syn region  cathodeCString   start=+c"+ skip=+\\"+ end=+"+

syn keyword cathodeTodo      TODO FIXME XXX contained
syn match   cathodeComment   "//.*$" contains=cathodeTodo
syn region  cathodeComment   start="/\*" end="\*/" contains=cathodeTodo

syn match   cathodeFunction  "\<\h\w*\ze\s*("

hi def link cathodeKeyword   Keyword
hi def link cathodeType      Type
hi def link cathodeBoolean   Boolean
hi def link cathodeNumber    Number
hi def link cathodeString    String
hi def link cathodeCString   String
hi def link cathodeEscape    SpecialChar
hi def link cathodeComment   Comment
hi def link cathodeTodo      Todo
hi def link cathodeFunction  Function

let b:current_syntax = "cathode"

