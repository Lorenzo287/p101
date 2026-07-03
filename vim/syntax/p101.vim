" Vim syntax file
" Language: Programma 101 key-chord listings

if exists("b:current_syntax")
  finish
endif

syn case match

" Metadata/directives.
syn match p101Meta /^\s*\.\%(decimals\|set\)\>/

" Operands and key prefixes.
syn match p101Operand /\c\<[MAR]\>/
syn match p101Operand /\c\<[BCDEF]\>/
syn match p101Operand /\c\<[bcdef]\>/
syn match p101Operand /\c\<[A-F]\//
syn match p101Operand /^\s*\/\ze\s*[#><VWYZ]/
syn match p101Operand /\/\ze[#><VWYZ]/

" Reference points and jumps, compact or spaced.
syn match p101Reference /^\s*[ABEF]\/\?[VWYZ]\>/
syn match p101Reference /^\s*[ABEF]\/\?\s\+[VWYZ]\>/
syn match p101Jump /^\s*\%([VWYZ]\|[CDR]\/\?[VWYZ]\|\/[VWYZ]\)\>/
syn match p101Jump /^\s*\%([CDR]\/\?\|\/\)\s\+[VWYZ]\>/

" Literal constant entry. A/ <M starts the sequence; R/F continue it; D/E end it.
syn match p101Literal /^\s*[RFDE]\s*\/\?\s*\%(><A\|><\|<M\|>A\|[S<>+\-x:#*]\)\ze\%(\s\|;\|$\)/ contained
syn region p101LiteralSeq matchgroup=p101Literal start=/^\s*A\s*\/\s*\%(<M\|<\)\ze\%(\s\|;\|$\)/ end=/^\s*[DE]\s*\/\?\s*\%(><A\|><\|<M\|>A\|[S<>+\-x:#*]\)\ze\%(\s\|;\|$\)/ contains=p101Literal,p101Comment keepend

" Operators. Works for both D< and D < because operands are matched separately.
syn match p101Operator /sqrt/
syn match p101Operator /><A\|>A\|<M\|><\|[<>+\-x:#*S]/

" Special service or non-register chords.
syn match p101Service /^\s*\%(S\|RS\)\ze\%(\s\|;\|$\)/
syn match p101Special /^\s*\/\s*#\ze\%(\s\|;\|$\)/
syn match p101Special /^\s*\/\s*\%(><A\|><\)\ze\%(\s\|;\|$\)/
syn match p101Special /^\s*A\s*\%(><A\|><\)\ze\%(\s\|;\|$\)/

" Signed numbers are not P101 + or - operations.
syn match p101Number /[-+]\=\d\+\%([.,]\d\+\)\=/

" Comments last. # is always the P101 print key.
syn match p101Comment /;.*/ contains=NONE

hi def link p101Meta PreProc
hi def link p101Number Number
hi def link p101Operand Identifier
hi def link p101Reference Label
hi def link p101Jump Statement
hi def link p101Literal Constant
hi def link p101Service Special
hi def link p101Special Special
" hi def link p101Operator Operator
hi def link p101Operator Normal
hi def link p101Comment Comment

syn sync fromstart

let b:current_syntax = "p101"
