# Olivetti Programma 101 Interpreter

This is a small C interpreter for Programma 101-style programs.
The program body uses P101 key-chord notation so listings stay close to the
machine keyboard. 

![](images/Garziera.jpg)
> instructions written by Gastone Garziera.

Options:

```text
--start V|W|Y|Z   Select the routine key, default V
--input FILE      Read S-stop input values from a file
--trace           Print executed instructions to stderr
```

## Program Format

Comments start with `;`, both on their own line and after an instruction.

```text
A V     reference point for routine key V
S       stop/read input into M
D <M    M -> D
D >A    D -> A
D ><    exchange D and A
/ ><    decimal part of A -> M
A ><    absolute value of A
D +     A = A + D
D -     A = A - D
D x     A = A * D
D :     A = A / D
D sqrt  sqrt(D) -> A
D #     print D
D *     clear D
/ #     blank tape line
V       jump to AV
C/ V    if A > 0 jump to B/V
```

The shorter transfer arrows are still accepted, so `D <`, `D >`, `D<`, and
`D>` mean the same thing as `D <M` and `D >A`. Compact forms such as `D<M`,
`D>A`, `D><`, `D><A`, `D+`, and `C/V` are accepted too.

Registers are `M`, `A`, `R`, `B`, `B/`, `C`, `C/`, `D`, `D/`, `E`, `E/`,
`F`, and `F/`. Lowercase `b` through `f` are also accepted as split-register
aliases in directives.

ASCII substitutes:

```text
<M    original "from M" transfer key
>A    original "to A" transfer key
< >   accepted short aliases for <M and >A
><    exchange key
><A   accepted keyboard-labeled alias for ><
#     print key
*     clear key
x     multiply key
:     divide key
sqrt  square-root key
```

Generated constants use the original P101 mode. `A/ <M` begins the literal
sequence; following key chords are decoded as digits until a final `D...` or
`E...` chord:

```text
A/ <M
D/ >A   ; generates +1 in M
```

Digit mapping in literal mode:

```text
S=0 >A=1 <M=2 >< or ><A=3 +=4 -=5 x=6 :=7 #=8 *=9
R/D prefixes are positive, F/E prefixes are negative.
D/E prefixes terminate the literal. A slash in the prefix marks the decimal.
```

## Current Limitations

- No Galeotti/Larini compatibility parser.
- No magnetic-card import/export yet.
- No instruction/data sharing inside `D`, `E`, and `F` yet.
- Register capacity checks are not strict enough for real hardware fidelity.
- Square-root remainder behavior is approximate.

# References
- [emulator by Claudio Larini](http://www.claudiolarini.altervista.org/emul2.htm)
- [simulator by Marco Galeotti](http://www.marcogaleotti.com/p101simulator.html)
- [emulator by the Univeristy of Amsterdam](https://ub.fnwi.uva.nl/computermuseum/p101emul.html)
