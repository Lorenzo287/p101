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

| Source token | Also accepted | Keyboard role |
| ------------ | ------------- | ------------- |
| `<M`         | `<`           | transfer from `M` |
| `>A`         | `>`           | transfer to `A` |
| `><`         | `><A`         | exchange key |
| `#`          |               | print key; with `/`, emits a blank tape line |
| `*`          |               | clear key; in literal mode, digit `9` |
| `x`          |               | multiply key |
| `:`          |               | divide key |
| `sqrt`       |               | square-root key |

## Registers And Context

Registers are `M`, `A`, `R`, `B`, `B/`, `C`, `C/`, `D`, `D/`, `E`, `E/`,
`F`, and `F/`. Lowercase `b` through `f` are accepted as one-letter aliases
for the split registers `B/` through `F/`.

Routine keys are `V`, `W`, `Y`, and `Z`. The same symbol can change meaning
based on the full chord:

| Context | Example |
| ------- | ------- |
| Register prefix | `D +` means add register `D` to `A` |
| Jump or label family | `D V` jumps to `E V`, while `A V` defines label `A V` |
| Special operation | `A ><` means absolute value, `/ ><` extracts the decimal part |
| Literal mode | after `A/ <M`, keys such as `S`, `>A`, and `*` become digits |

## Command Reference

For the forms below, `REG` can be any register listed above. When a register
prefix is optional and omitted, it defaults to `M`; for example, `>A` loads
`M` into `A`.

### Reference Points And Jumps

Let `KEY` be one of `V`, `W`, `Y`, or `Z`. Reference points are labels:
`A KEY`, `A/ KEY`, `B KEY`, `B/ KEY`, `E KEY`, `E/ KEY`, `F KEY`, and
`F/ KEY`. The command-line entry point `--start V` starts at `A V`,
`--start W` starts at `A W`, and so on.

| Jump form | Target label |
| --------- | ------------ |
| `KEY` | `A KEY` |
| `C KEY` | `B KEY` |
| `D KEY` | `E KEY` |
| `R KEY` | `F KEY` |
| `/ KEY` | `A/ KEY`, only if `A > 0` |
| `C/ KEY` | `B/ KEY`, only if `A > 0` |
| `D/ KEY` | `E/ KEY`, only if `A > 0` |
| `R/ KEY` | `F/ KEY`, only if `A > 0` |

### Data Movement And Service Keys

| Form     | Meaning                                             |
| -------- | --------------------------------------------------- |
| `S`      | stop and read the next input value into `M`         |
| `R S`    | exchange `D` and `R`                                |
| `REG <M` | copy `M` into `REG`                                 |
| `REG >A` | copy `REG` into `A`                                 |
| `REG ><` | exchange `REG` and `A`; with `R`, copy `R` into `A` |
| `/ ><`   | copy the decimal part of `A` into `M`               |
| `A ><`   | replace `A` with its absolute value                 |
| `REG *`  | clear `REG`                                         |

With `--input FILE`, `S` reads from the file. If there is no more input, the
program stops.

### Arithmetic And Output

Arithmetic commands first copy `REG` into `M`. Addition, subtraction, and
multiplication store the exact result in `R` and the rounded result in `A`.
Division stores the quotient in `A` and the remainder in `R`. Square root
stores `sqrt(REG)` in `A`, the remainder `REG - A * A` in `R`, and `2 * A` in
`M`.

| Form       | Meaning                 |
| ---------- | ----------------------- |
| `REG +`    | `A + REG -> A`          |
| `REG -`    | `A - REG -> A`          |
| `REG x`    | `A x REG -> A`          |
| `REG :`    | `A / REG -> A`          |
| `REG sqrt` | `sqrt(REG) -> A`        |
| `REG #`    | print `REG`             |
| `/ #`      | print a blank tape line |

## Literal Constants

Generated constants use the original P101 literal-entry mode. `A/ <M` begins
the sequence. The following key chords are decoded as digits and stored in
`M`; the sequence ends at the first `D...` or `E...` chord.

Digits are entered from least significant to most significant. The terminating
`D` or `E` chord contains the leftmost digit and determines the final sign.

| Digit | Literal key | Digit | Literal key |
| ----- | ----------- | ----- | ----------- |
| `0` | `S` | `5` | `-` |
| `1` | `>A` or `>` | `6` | `x` |
| `2` | `<M` or `<` | `7` | `:` |
| `3` | `><A` or `><` | `8` | `#` |
| `4` | `+` | `9` | `*` |

| Prefix      | Meaning in literal mode                                              |
| ----------- | -------------------------------------------------------------------- |
| `R` or `R/` | continue the literal                                                 |
| `F` or `F/` | continue the literal; accepted for original negative-number notation |
| `D` or `D/` | final digit, positive number                                         |
| `E` or `E/` | final digit, negative number                                         |

A slash in the prefix marks the decimal point after that digit in the final
number.

| Constant | Literal sequence |
| -------- | ---------------- |
| `1`      | `A/ <M`<br>`D/ >A` |
| `12.5`   | `A/ <M`<br>`R -`<br>`R/ <M`<br>`D >A` |
| `-12`    | `A/ <M`<br>`F <M`<br>`E >A` |

## Directives

Directives are interpreter conveniences, not P101 keys. Numeric values may
use either `.` or `,` as the decimal separator.

- `.decimals N` or `decimals N`: set decimal precision, from `0` to `15`;
  default is `0`.
- `.set REG VALUE` or `set REG VALUE`: initialize a register before execution.

## CLI Options

- `--start KEY`: select the start routine, one of `V`, `W`, `Y`, or `Z`;
  default is `V`.
- `--input FILE`: read `S` input values from a file.
- `--trace`: print executed instructions to stderr.

## Current Limitations

- No magnetic-card import/export yet.
- No instruction/data sharing inside `D`, `E`, and `F` yet.
- Register capacity checks are not strict enough for real hardware fidelity.
- Square-root remainder behavior is approximate.

## References

- [emulator by Claudio Larini](http://www.claudiolarini.altervista.org/emul2.htm)
- [simulator by Marco Galeotti](http://www.marcogaleotti.com/p101simulator.html)
- [emulator by the University of Amsterdam](https://ub.fnwi.uva.nl/computermuseum/p101emul.html)
