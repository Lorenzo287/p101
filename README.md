The Olivetti Programma 101 is one of the first programmable calculators,
but don't let the name fool you: the instruction set with jumps and registers
makes it a proper stored-program computer.
The design, form factor, storage and programming capabilities put it
way ahead of its time, and contributed to the well-deserved fame of
**first desktop personal computer**.

This project is a small C interpreter for P101-style programs.
The syntax of the programs uses P101 key-chord notation so listings stay close to the
machine original feel, only a couple of keys were replaced for ease of use on
modern keyboards.

![](images/Garziera.jpg)

> Instructions by Gastone Garziera.

## Example Program

This program computes N!. The default routine is `V`, but this starts from
`Z`, so run it with `--start Z`.

```p101
A  Z    ; start label
   S    ; stop and wait for user input,
        ; then store it into M
D  <M   ; M -> D
   >A   ; M -> A
A  W    ; loop label
A/ <M   ; generate constant 1 into M
D/ >A
M  -    ; A - M -> A
/  V    ; cond jump
D  #    ; print D
   Z    ; goto start
A/ V    ; cond jump label
D  ><   ; swap A D
D  x    ; A x D -> A
D  ><   ; swap A D
   W    ; goto loop
```

## Program Format

A program is a list of P101 key chords or interpreter directives, one per
line. Comments start with `;`, both on their own line and after an instruction.

Whitespace is only for readability. The examples in this README use spaced
key chords, but compact forms such as `D<M`, `D>`, `D><`, `C/V`, and
`A/<M` are accepted too.

## Keyboard Substitutes

The original keyboard uses a few symbols that are awkward to type in plain
source files. Program files use the ASCII substitutes below.

![Olivetti Programma 101 keyboard layout](images/P101-KB-layout.jpg)

| Source token  | Original Key   | Keyboard role                               |
| ------------- | -------------- | ------------------------------------------- |
| `<M` or `<`   | M arrow up     | transfer from `M`                           |
| `>A` or `>`   | A arrow down   | transfer to `A`                             |
| `><` or `><A` | A double arrow | exchange key                                |
| `#`           | rhombus        | print key; with `/` emits a blank tape line |
| `*`           | *              | clear key; in literal mode, digit `9`       |
| `x`           | x              | multiply key                                |
| `:`           | :              | divide key                                  |
| `sqrt`        | sqrt symbol    | square-root key                             |

## Registers And Context

Registers are `M`, `A`, `R`, `B`, `B/`, `C`, `C/`, `D`, `D/`, `E`, `E/`,
`F`, and `F/`. Lowercase `b` through `f` are accepted as one-letter aliases
for the split registers `B/` through `F/`.

`B` through `F` follow the hardware overlay. Each starts as a 22-digit whole
register. A slash access splits it into two 11-digit halves:
`B/` is the left half and `B` is the right half. Splitting a whole register
that currently holds more than 11 digits is an error.

The clear key controls transitions between the two views. `B/ *` clears the
left half and makes `B` available again as a whole register, keeping the right
half as the current value. `B *` clears the whole register when `B` is unsplit,
or the right half when it is split. The same rule applies to `C`, `D`, `E`,
and `F`.

The interpreter enforces the P101 internal program layout: 48 core instruction
slots, then overflow into `F`, `F/`, `E`, `E/`, `D`, and `D/`, for 120
instructions total. Overflow halves can reserve data space the same way as the
hardware: leading `S` slots in that half reserve one numeric digit each, up to
the normal 11-digit split-register capacity. The executable part of that half
should start after those reserved slots, usually at an `A` reference point that
the program jumps to. If no leading `S` slots are reserved, that half is
instruction-only and cannot be used for data.

Routine keys are `V`, `W`, `Y`, and `Z`. The same symbol can change meaning
based on the full chord. `D +` uses `D` as a register prefix and adds `D` to
`A`. `D V` is a jump to `E V`, while `A V` defines a reference point. `A ><`
means absolute value, but `/ ><` extracts the decimal part of `A`. After
`A/ <M`, keys such as `S`, `>A`, and `*` are literal digits until the
terminating `D...` or `E...` chord.

## Command Reference

For the forms below, `REG` can be any register listed above. When a register
prefix is optional and omitted, it defaults to `M`; for example, `>A` loads
`M` into `A`.

### Reference Points And Jumps

Let `KEY` be one of `V`, `W`, `Y`, or `Z`. Reference points are labels:
`A KEY`, `A/ KEY`, `B KEY`, `B/ KEY`, `E KEY`, `E/ KEY`, `F KEY`, and
`F/ KEY`. The command-line entry point `--start V` starts at `A V`,
`--start W` starts at `A W`, and so on. `--start` also accepts full
unconditional origins such as `C W`/`CW`, and direct reference points such as
`A W`/`AW`.

Unconditional jumps are written as `KEY`, `C KEY`, `D KEY`, or `R KEY`;
they target `A KEY`, `B KEY`, `E KEY`, and `F KEY` respectively.

Conditional jumps use the slash forms `/ KEY`, `C/ KEY`, `D/ KEY`, and
`R/ KEY`. They jump only when `A > 0`, targeting `A/ KEY`, `B/ KEY`,
`E/ KEY`, and `F/ KEY` respectively. If `A <= 0`, execution continues with
the next instruction.

### Data Movement And Service Keys

`S` stops and releases the operator transcript. A bare numeric input value is
stored in `M` and execution continues after `S`. `START` or `S` restarts
without entering a new number. Routine and jump origins such as `W`, `C W`, or
`D Z` resume from the selected reference point.

`CARD FILE` loads another program card, preserving `M`, `A`, `R`, `B`, `C`,
and the decimal-wheel setting while refreshing `D`, `E`, `F`, and their splits
from the new card. Use `R S` before the card load, and again at the start of
the next card, to carry `D` and `D/` through `R` using the P101 chaining
convention. Select a routine key afterward to continue. With `--input FILE`,
input items are read from the file; EOF stops the program.

`REG <M` copies `M` into `REG`; `REG >A` copies `REG` into `A`; and
`REG ><` exchanges `REG` and `A`. The special form `R ><` copies `R` into
`A` instead of exchanging. `R S` exchanges `D` plus `D/` with `R`; when this
is used for chaining, `R` cannot be used again until another `R S` restores the
saved pair.

`REG *` clears a register, except `M` and `R`, which the P101 clear key cannot
clear. `/ ><` copies the decimal part of `A` into `M`. `A ><` replaces `A`
with its absolute value.

### Arithmetic And Output

Arithmetic commands first copy `REG` into `M`. Addition, subtraction, and
multiplication store the exact result in `R` and the rounded result in `A`.
Division stores the quotient in `A` and the remainder in `R`. Square root
stores `sqrt(REG)` in `A`, the remainder `REG - A * A` in `R`, and `2 * A` in
`M`.

The arithmetic forms are `REG +`, `REG -`, `REG x`, `REG :`, and
`REG sqrt`. `REG #` prints a register, while `/ #` prints a blank tape line.

## Literal Constants

Generated constants use the original P101 literal-entry mode. `A/ <M` begins
the sequence. The following key chords are decoded as digits and stored in
`M`; the sequence ends at the first `D...` or `E...` chord.

Digits are entered from least significant to most significant. The terminating
`D` or `E` chord contains the leftmost digit and determines the final sign.

| Digit | Literal key   | Digit | Literal key |
| ----- | ------------- | ----- | ----------- |
| `0`   | `S`           | `5`   | `-`         |
| `1`   | `>A` or `>`   | `6`   | `x`         |
| `2`   | `<M` or `<`   | `7`   | `:`         |
| `3`   | `><A` or `><` | `8`   | `#`         |
| `4`   | `+`           | `9`   | `*`         |

`R` and `R/` continue the literal. `F` and `F/` also continue it, and are
accepted for the original negative-number notation. `D` or `D/` terminates
the sequence as a positive number; `E` or `E/` terminates it as a negative
number. A slash in the prefix marks the decimal point after that digit in the
final number.

Examples:

```p101
; 1
A/ <M
D/ >A
```

```p101
; 12.5
A/ <M
R  -
R/ <M
D  >A
```

```p101
; -12
A/ <M
F  <M
E  >A
```

## Directives

Directives are interpreter conveniences, not P101 keys. Numeric values may
use either `.` or `,` as the decimal separator.

- `.wheel N`: set decimal precision, from `0` to `15`; default is `0`. This
  models setting the decimal wheel before running.
- `.init REG VALUE`: initialize a register before execution.

## CLI Options

- `--start ORIGIN`: select the start origin or reference point; default is
  `V`. Examples: `W`, `CW`, `AW`.
- `--input FILE`: read operator transcript items from a file.

## Chaining Example

`examples/chaining_card1.p101` and
`examples/chaining_card2.p101` demonstrate card chaining and the
special `R S` handoff. Card 1 reads `x` and `y`, stores them in `D` and `D/`,
executes `R S` to save that pair in `R`, then stops. The operator transcript
loads card 2 and presses `V`; card 2 starts with `R S`, restoring `D` and `D/`,
then prints `(x + y)^2`.

```text
3
4
CARD examples/chaining_card2.p101
V
```

Run it with:

```sh
p101 --input examples/chaining.input examples/chaining_card1.p101
```

The sample prints `A 49`.

## References

- [emulator by Claudio Larini](http://www.claudiolarini.altervista.org/emul2.htm)
- [simulator by Marco Galeotti](http://www.marcogaleotti.com/p101simulator.html)
- [emulator by the University of Amsterdam](https://ub.fnwi.uva.nl/computermuseum/p101emul.html)
