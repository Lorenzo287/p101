#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INS 256
#define MAX_LABELS 128
#define MAX_LINE 256
#define MAX_NAME 32
#define STEP_ERROR -1
#define STEP_STOP 0
#define STEP_NEXT 1

__extension__ typedef __int128 i128;

typedef struct decimal {
    i128 coeff;
    int scale;
} decimal;

typedef enum regid {
    REG_M,
    REG_A,
    REG_R,
    REG_B,
    REG_BS,
    REG_C,
    REG_CS,
    REG_D,
    REG_DS,
    REG_E,
    REG_ES,
    REG_F,
    REG_FS,
    REG_COUNT
} regid;

typedef enum opcode {
    OP_MARK,
    OP_INPUT,
    OP_LITERAL_BEGIN,
    OP_LITERAL_DIGIT,
    OP_STORE,
    OP_LOAD,
    OP_SWAP,
    OP_FRAC,
    OP_ABS,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_SQRT,
    OP_CLEAR,
    OP_PRINT,
    OP_NEWLINE,
    OP_GOTO,
    OP_IFPOS,
    OP_RS,
    OP_HALT
} opcode;

typedef struct instruction {
    opcode op;
    regid reg;
    char key[MAX_NAME];
    char target[MAX_NAME];
    int line;
} instruction;

typedef struct label {
    char name[MAX_NAME];
    int pc;
} label;

typedef struct program {
    instruction ins[MAX_INS];
    int inslen;
    label labels[MAX_LABELS];
    int labelslen;
    decimal init[REG_COUNT];
    bool has_init[REG_COUNT];
    int decimals;
} program;

typedef struct machine {
    decimal reg[REG_COUNT];
    int pc;
    int decimals;
    bool trace;
    FILE *input;
} machine;

static i128 pow10_i128(int n) {
    i128 v = 1;
    while (n-- > 0) v *= 10;
    return v;
}

static decimal decimal_new(i128 coeff, int scale) {
    decimal d = {coeff, scale};
    if (d.coeff == 0) {
        d.scale = 0;
        return d;
    }
    while (d.scale > 0 && d.coeff % 10 == 0) {
        d.coeff /= 10;
        d.scale--;
    }
    return d;
}

static decimal decimal_int(int value) {
    return decimal_new(value, 0);
}

static decimal decimal_rescale(decimal d, int scale) {
    if (d.scale < scale)
        d.coeff *= pow10_i128(scale - d.scale);
    else if (d.scale > scale)
        d.coeff /= pow10_i128(d.scale - scale);
    return decimal_new(d.coeff, scale);
}

static decimal decimal_add(decimal a, decimal b) {
    int scale = a.scale > b.scale ? a.scale : b.scale;
    i128 ac = a.coeff * pow10_i128(scale - a.scale);
    i128 bc = b.coeff * pow10_i128(scale - b.scale);
    return decimal_new(ac + bc, scale);
}

static decimal decimal_sub(decimal a, decimal b) {
    int scale = a.scale > b.scale ? a.scale : b.scale;
    i128 ac = a.coeff * pow10_i128(scale - a.scale);
    i128 bc = b.coeff * pow10_i128(scale - b.scale);
    return decimal_new(ac - bc, scale);
}

static decimal decimal_mul(decimal a, decimal b) {
    return decimal_new(a.coeff * b.coeff, a.scale + b.scale);
}

static bool decimal_div(decimal a, decimal b, int scale, decimal *q) {
    if (b.coeff == 0) return false;
    int exp = b.scale + scale - a.scale;
    i128 num = a.coeff, den = b.coeff;
    if (exp >= 0)
        num *= pow10_i128(exp);
    else
        den *= pow10_i128(-exp);
    *q = decimal_new(num / den, scale);
    return true;
}

static long double decimal_told(decimal d) {
    long double value = (long double)d.coeff;
    while (d.scale-- > 0) value /= 10.0L;
    return value;
}

static decimal decimal_sqrt(decimal d, int scale) {
    long double value = decimal_told(d);
    if (value < 0) value = -value;
    i128 coeff = (i128)floorl(sqrtl(value) * powl(10.0L, scale) + 1e-18L);
    return decimal_new(coeff, scale);
}

static decimal decimal_frac(decimal d) {
    if (d.scale == 0) return decimal_int(0);
    i128 unit = pow10_i128(d.scale);
    return decimal_new(d.coeff - (d.coeff / unit) * unit, d.scale);
}

static bool decimal_parse(const char *s, decimal *out) {
    bool neg = false;
    bool dot = false;
    bool any = false;
    i128 coeff = 0;
    int scale = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '+' || *s == '-') {
        neg = *s == '-';
        s++;
    }
    while (*s) {
        if (isdigit((unsigned char)*s)) {
            any = true;
            coeff = coeff * 10 + (*s - '0');
            if (dot) scale++;
        } else if ((*s == '.' || *s == ',') && !dot) {
            dot = true;
        } else if (!isspace((unsigned char)*s)) {
            return false;
        }
        s++;
    }
    if (!any) return false;
    *out = decimal_new(neg ? -coeff : coeff, scale);
    return true;
}

static void i128_string(i128 value, char *buf, size_t size) {
    char tmp[80];
    bool neg = value < 0;
    int pos = 0;
    size_t out = 0;
    if (size == 0) return;
    if (neg) value = -value;
    do {
        tmp[pos++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && pos < (int)sizeof(tmp));
    if (neg && out + 1 < size) buf[out++] = '-';
    while (pos && out + 1 < size) buf[out++] = tmp[--pos];
    buf[out] = '\0';
}

static void decimal_string(decimal d, int scale, bool fixed, char *buf,
                           size_t size) {
    char digits[96];
    char tmp[128];
    int pos = 0;
    if (fixed) d = decimal_rescale(d, scale);
    bool neg = d.coeff < 0;
    i128 value = neg ? -d.coeff : d.coeff;
    i128_string(value, digits, sizeof(digits));
    int len = (int)strlen(digits);
    if (neg) tmp[pos++] = '-';
    if (d.scale == 0) {
        for (int i = 0; i < len; i++) tmp[pos++] = digits[i];
    } else {
        int intlen = len - d.scale;
        if (intlen <= 0) {
            tmp[pos++] = '0';
            tmp[pos++] = ',';
            for (int i = 0; i < -intlen; i++) tmp[pos++] = '0';
            for (int i = 0; i < len; i++) tmp[pos++] = digits[i];
        } else {
            for (int i = 0; i < intlen; i++) tmp[pos++] = digits[i];
            tmp[pos++] = ',';
            for (int i = intlen; i < len; i++) tmp[pos++] = digits[i];
        }
    }
    tmp[pos] = '\0';
    if (!fixed) {
        while (pos > 0 && tmp[pos - 1] == '0') tmp[--pos] = '\0';
        if (pos > 0 && tmp[pos - 1] == ',') tmp[--pos] = '\0';
    }
    snprintf(buf, size, "%s", tmp);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
    return s;
}

static bool streq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool parse_reg(const char *s, regid *id) {
    static const regid full[] = {REG_A, REG_B, REG_C, REG_D, REG_E, REG_F, REG_M, REG_R};
    static const regid split_ids[] = {REG_BS, REG_CS, REG_DS, REG_ES, REG_FS};
    char base = (char)toupper((unsigned char)s[0]);
    if (s[0] == '\0') return false;

    bool split = s[1] == '/' && s[2] == '\0';
    if (s[1] == '\0')
        split = s[0] >= 'b' && s[0] <= 'f';
    else if (!split)
        return false;

    if (base >= 'B' && base <= 'F') {
        *id = split ? split_ids[base - 'B'] : full[base - 'A'];
        return true;
    }
    if (split) return false;
    if (base == 'A') *id = REG_A;
    else if (base == 'M') *id = REG_M;
    else if (base == 'R') *id = REG_R;
    else return false;
    return true;
}

static const char *reg_name(regid id) {
    static const char *names[] = {"M", "A",  "R", "B",  "B/", "C", "C/",
                                  "D", "D/", "E", "E/", "F",  "F/"};
    return names[id];
}

static bool is_routine(char c) {
    return c == 'V' || c == 'W' || c == 'Y' || c == 'Z';
}

static bool is_refpoint(const char *s) {
    size_t len = strlen(s);
    if (len == 2) return strchr("ABEF", s[0]) != NULL && is_routine(s[1]);
    if (len == 3)
        return strchr("ABEF", s[0]) != NULL && s[1] == '/' && is_routine(s[2]);
    return false;
}

static bool add_label(program *p, const char *name, int pc, int line) {
    if (p->labelslen == MAX_LABELS) {
        fprintf(stderr, "line %d: too many labels\n", line);
        return false;
    }
    snprintf(p->labels[p->labelslen].name, MAX_NAME, "%s", name);
    p->labels[p->labelslen].pc = pc;
    p->labelslen++;
    return true;
}

static int find_label(const program *p, const char *name) {
    for (int i = 0; i < p->labelslen; i++) {
        if (strcmp(p->labels[i].name, name) == 0) return p->labels[i].pc;
    }
    return -1;
}

static int entry_pc(const program *p, char key) {
    char label[4];
    snprintf(label, sizeof(label), "A%c", key);
    return find_label(p, label);
}

static bool add_instruction(program *p, instruction in) {
    if (p->inslen == MAX_INS) {
        fprintf(stderr, "line %d: too many instructions\n", in.line);
        return false;
    }
    p->ins[p->inslen++] = in;
    return true;
}

static bool split_chord(const char *s, char *prefix, char *op) {
    size_t len = strlen(s);
    size_t plen;
    if (len == 0) return false;
    if (len >= 4 && strcmp(s + len - 4, "sqrt") == 0) {
        plen = len - 4;
        if (plen >= MAX_NAME) return false;
        memcpy(prefix, s, plen);
        prefix[plen] = '\0';
        strcpy(op, "sqrt");
        return true;
    }
    if (len >= 2 && strcmp(s + len - 2, "><") == 0) {
        plen = len - 2;
        if (plen >= MAX_NAME) return false;
        memcpy(prefix, s, plen);
        prefix[plen] = '\0';
        strcpy(op, "><");
        return true;
    }
    if (len >= 3 && strcmp(s + len - 3, "><A") == 0) {
        plen = len - 3;
        if (plen >= MAX_NAME) return false;
        memcpy(prefix, s, plen);
        prefix[plen] = '\0';
        strcpy(op, "><");
        return true;
    }
    if (len >= 2 && strcmp(s + len - 2, "<M") == 0) {
        plen = len - 2;
        if (plen >= MAX_NAME) return false;
        memcpy(prefix, s, plen);
        prefix[plen] = '\0';
        strcpy(op, "<");
        return true;
    }
    if (len >= 2 && strcmp(s + len - 2, ">A") == 0) {
        plen = len - 2;
        if (plen >= MAX_NAME) return false;
        memcpy(prefix, s, plen);
        prefix[plen] = '\0';
        strcpy(op, ">");
        return true;
    }
    if (strchr("S<>+-x:#*", s[len - 1]) == NULL) return false;
    plen = len - 1;
    if (plen >= MAX_NAME) return false;
    memcpy(prefix, s, plen);
    prefix[plen] = '\0';
    op[0] = s[len - 1];
    op[1] = '\0';
    return true;
}

static bool jump_target(const char *origin, bool *conditional, char *target) {
    *conditional = false;
    if (strlen(origin) == 1 && is_routine(origin[0])) {
        snprintf(target, MAX_NAME, "A%c", origin[0]);
        return true;
    }
    if (strlen(origin) == 2 && strchr("CDR", origin[0]) && is_routine(origin[1])) {
        char reg = origin[0], key = origin[1];
        snprintf(target, MAX_NAME, "%c%c",
                 reg == 'C'   ? 'B'
                 : reg == 'D' ? 'E'
                              : 'F',
                 key);
        return true;
    }
    if (strlen(origin) == 2 && origin[0] == '/' && is_routine(origin[1])) {
        *conditional = true;
        snprintf(target, MAX_NAME, "A/%c", origin[1]);
        return true;
    }
    if (strlen(origin) == 3 && strchr("CDR", origin[0]) && origin[1] == '/' &&
        is_routine(origin[2])) {
        *conditional = true;
        char reg = origin[0], key = origin[2];
        snprintf(target, MAX_NAME, "%c/%c",
                 reg == 'C'   ? 'B'
                 : reg == 'D' ? 'E'
                              : 'F',
                 key);
        return true;
    }
    return false;
}

static bool parse_directive(program *p, char **argv, int argc, int line) {
    if (streq(argv[0], ".decimals") || streq(argv[0], "decimals")) {
        if (argc != 2) return false;
        int decimals = atoi(argv[1]);
        if (decimals < 0 || decimals > 15) return false;
        p->decimals = decimals;
        return true;
    }
    if (streq(argv[0], ".set") || streq(argv[0], "set")) {
        regid id;
        decimal value;
        if (argc != 3 || !parse_reg(argv[1], &id) ||
            !decimal_parse(argv[2], &value))
            return false;
        p->init[id] = value;
        p->has_init[id] = true;
        return true;
    }
    fprintf(stderr, "line %d: unknown directive %s\n", line, argv[0]);
    return false;
}

static bool parse_chord(program *p, const char *key, int line) {
    instruction in = {0};
    in.reg = REG_M;
    in.line = line;
    snprintf(in.key, sizeof(in.key), "%s", key);
    if (is_refpoint(key)) {
        if (!add_label(p, key, p->inslen, line)) return false;
        in.op = OP_MARK;
        return add_instruction(p, in);
    }
    bool conditional;
    char target[MAX_NAME];
    if (jump_target(key, &conditional, target)) {
        in.op = conditional ? OP_IFPOS : OP_GOTO;
        snprintf(in.target, sizeof(in.target), "%s", target);
        return add_instruction(p, in);
    }
    if (strcmp(key, "RS") == 0) {
        in.op = OP_RS;
        return add_instruction(p, in);
    }
    char prefix[MAX_NAME], op[MAX_NAME];
    if (!split_chord(key, prefix, op)) {
        fprintf(stderr, "line %d: cannot parse key chord %s\n", line, key);
        return false;
    }
    if (strcmp(prefix, "A/") == 0 && strcmp(op, "<") == 0) {
        in.op = OP_LITERAL_BEGIN;
        return add_instruction(p, in);
    }
    if (strcmp(op, "S") == 0) {
        in.op = prefix[0] == '\0' ? OP_INPUT : OP_LITERAL_DIGIT;
    } else if (strcmp(op, "<") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_STORE;
    } else if (strcmp(op, ">") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_LOAD;
    } else if (strcmp(op, "><") == 0) {
        if (strcmp(prefix, "/") == 0) {
            in.op = OP_FRAC;
        } else if (strcmp(prefix, "A") == 0) {
            in.op = OP_ABS;
        } else {
            if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
            in.op = OP_SWAP;
        }
    } else if (strcmp(op, "+") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_ADD;
    } else if (strcmp(op, "-") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_SUB;
    } else if (strcmp(op, "x") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_MUL;
    } else if (strcmp(op, ":") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_DIV;
    } else if (strcmp(op, "sqrt") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_SQRT;
    } else if (strcmp(op, "#") == 0) {
        if (strcmp(prefix, "/") == 0) {
            in.op = OP_NEWLINE;
        } else {
            if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
            in.op = OP_PRINT;
        }
    } else if (strcmp(op, "*") == 0) {
        if (prefix[0] != '\0' && !parse_reg(prefix, &in.reg)) return false;
        in.op = OP_CLEAR;
    } else {
        return false;
    }
    return add_instruction(p, in);
}

static int split_words(char *s, char **argv, int max) {
    char *ctx = NULL;
    int argc = 0;
    for (char *tok = strtok_r(s, " \t\r\n", &ctx); tok != NULL;
         tok = strtok_r(NULL, " \t\r\n", &ctx)) {
        if (argc == max) return -1;
        argv[argc++] = tok;
    }
    return argc;
}

static bool parse_line(program *p, char *line, int lineno) {
    char *argv[4];
    char *comment = strchr(line, ';');
    if (comment != NULL) *comment = '\0';
    line = trim(line);
    if (line[0] == '\0') return true;
    int argc = split_words(line, argv, 4);
    if (argc <= 0) {
        fprintf(stderr, "line %d: invalid syntax\n", lineno);
        return false;
    }
    if (argv[0][0] == '.' || streq(argv[0], "decimals") || streq(argv[0], "set")) {
        if (!parse_directive(p, argv, argc, lineno)) {
            fprintf(stderr, "line %d: invalid directive\n", lineno);
            return false;
        }
        return true;
    }
    if (argc == 1) return parse_chord(p, argv[0], lineno);
    if (argc == 2) {
        char chord[MAX_NAME * 2];
        snprintf(chord, sizeof(chord), "%s%s", argv[0], argv[1]);
        return parse_chord(p, chord, lineno);
    }
    if (argc == 3 && strcmp(argv[1], "/") == 0) {
        char chord[MAX_NAME * 2];
        snprintf(chord, sizeof(chord), "%s/%s", argv[0], argv[2]);
        return parse_chord(p, chord, lineno);
    }
    {
        fprintf(stderr, "line %d: expected one key chord\n", lineno);
        return false;
    }
}

static bool load_program(const char *path, program *p) {
    char line[MAX_LINE];
    int lineno = 0;
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    memset(p, 0, sizeof(*p));
    p->decimals = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;
        if (!parse_line(p, line, lineno)) {
            fclose(fp);
            return false;
        }
    }
    fclose(fp);
    return true;
}

static bool read_input(machine *m, decimal *out) {
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), m->input) != NULL) {
        char *s = trim(line);
        if (s[0] == '\0') continue;
        if (decimal_parse(s, out)) return true;
        fprintf(stderr, "invalid input: %s\n", s);
        return false;
    }
    return false;
}

static int literal_digit(const char *op) {
    if (strcmp(op, "S") == 0) return 0;
    if (strcmp(op, ">") == 0) return 1;
    if (strcmp(op, "<") == 0) return 2;
    if (strcmp(op, "><") == 0) return 3;
    if (strcmp(op, "+") == 0) return 4;
    if (strcmp(op, "-") == 0) return 5;
    if (strcmp(op, "x") == 0) return 6;
    if (strcmp(op, ":") == 0) return 7;
    if (strcmp(op, "#") == 0) return 8;
    if (strcmp(op, "*") == 0) return 9;
    return -1;
}

static bool execute_literal(machine *m, const program *p) {
    char digits[32];
    bool point[32];
    char prefix[MAX_NAME];
    char op[MAX_NAME];
    bool neg = false;
    int pc = m->pc + 1;
    int len = 0;
    while (pc < p->inslen && len < (int)sizeof(digits) - 1) {
        if (!split_chord(p->ins[pc].key, prefix, op)) return false;
        int digit = literal_digit(op);
        if (digit < 0 || prefix[0] == '\0') return false;
        digits[len] = (char)('0' + digit);
        point[len] = strchr(prefix, '/') != NULL;
        char base = prefix[0];
        if (base == 'D' || base == 'E') {
            neg = base == 'E';
            len++;
            break;
        }
        if (base != 'R' && base != 'F') return false;
        len++;
        pc++;
    }
    if (len == 0 || pc >= p->inslen) return false;
    char number[80];
    int out = 0;
    if (neg) number[out++] = '-';
    for (int i = len - 1; i >= 0; i--) {
        number[out++] = digits[i];
        if (point[i]) number[out++] = '.';
    }
    number[out] = '\0';
    if (!decimal_parse(number, &m->reg[REG_M])) return false;
    m->pc = pc + 1;
    return true;
}

static int step(machine *m, const program *p) {
    instruction in = p->ins[m->pc];
    if (m->trace) fprintf(stderr, "%03d %s\n", m->pc + 1, in.key);
    switch (in.op) {
    case OP_MARK:
        m->pc++; return STEP_NEXT;
    case OP_INPUT:
        if (!read_input(m, &m->reg[REG_M])) return STEP_STOP;
        m->pc++; return STEP_NEXT;
    case OP_LITERAL_BEGIN:
        if (!execute_literal(m, p)) {
            fprintf(stderr, "invalid literal sequence at step %d\n", m->pc + 1);
            return STEP_ERROR;
        }
        return STEP_NEXT;
    case OP_LITERAL_DIGIT:
        fprintf(stderr, "literal digit outside A/< sequence at step %d\n",
                m->pc + 1);
        return STEP_ERROR;
    case OP_STORE:
        m->reg[in.reg] = m->reg[REG_M]; m->pc++; return STEP_NEXT;
    case OP_LOAD:
        m->reg[REG_A] = m->reg[in.reg]; m->pc++; return STEP_NEXT;
    case OP_SWAP:
        if (in.reg == REG_R) {
            m->reg[REG_A] = m->reg[REG_R];
        } else {
            decimal tmp = m->reg[in.reg];
            m->reg[in.reg] = m->reg[REG_A];
            m->reg[REG_A] = tmp;
        }
        m->pc++; return STEP_NEXT;
    case OP_FRAC:
        m->reg[REG_M] = decimal_frac(m->reg[REG_A]); m->pc++; return STEP_NEXT;
    case OP_ABS:
        if (m->reg[REG_A].coeff < 0) m->reg[REG_A].coeff = -m->reg[REG_A].coeff;
        m->pc++; return STEP_NEXT;
    case OP_ADD:
        m->reg[REG_M] = m->reg[in.reg];
        m->reg[REG_R] = decimal_add(m->reg[REG_A], m->reg[REG_M]);
        m->reg[REG_A] = decimal_rescale(m->reg[REG_R], m->decimals);
        m->pc++;
        return STEP_NEXT;
    case OP_SUB:
        m->reg[REG_M] = m->reg[in.reg];
        m->reg[REG_R] = decimal_sub(m->reg[REG_A], m->reg[REG_M]);
        m->reg[REG_A] = decimal_rescale(m->reg[REG_R], m->decimals);
        m->pc++;
        return STEP_NEXT;
    case OP_MUL:
        m->reg[REG_M] = m->reg[in.reg];
        m->reg[REG_R] = decimal_mul(m->reg[REG_A], m->reg[REG_M]);
        m->reg[REG_A] = decimal_rescale(m->reg[REG_R], m->decimals);
        m->pc++;
        return STEP_NEXT;
    case OP_DIV:
        m->reg[REG_M] = m->reg[in.reg];
        decimal exact = m->reg[REG_A];
        decimal q;
        if (!decimal_div(exact, m->reg[REG_M], m->decimals, &q)) {
            fprintf(stderr, "division by zero at step %d\n", m->pc + 1);
            return STEP_ERROR;
        }
        m->reg[REG_A] = q;
        m->reg[REG_R] = decimal_sub(exact, decimal_mul(q, m->reg[REG_M]));
        m->pc++;
        return STEP_NEXT;
    case OP_SQRT:
        m->reg[REG_M] = m->reg[in.reg];
        m->reg[REG_A] = decimal_sqrt(m->reg[REG_M], m->decimals);
        m->reg[REG_R] =
            decimal_sub(m->reg[REG_M], decimal_mul(m->reg[REG_A], m->reg[REG_A]));
        m->reg[REG_M] = decimal_mul(decimal_int(2), m->reg[REG_A]);
        m->pc++;
        return STEP_NEXT;
    case OP_CLEAR:
        m->reg[in.reg] = decimal_int(0);
        m->pc++;
        return STEP_NEXT;
    case OP_PRINT: {
        char value[128];
        decimal_string(m->reg[in.reg], m->decimals, in.reg != REG_R, value,
                       sizeof(value));
        printf("%s\t%s\n", reg_name(in.reg), value);
        m->pc++;
        return STEP_NEXT;
    }
    case OP_NEWLINE:
        putchar('\n'); m->pc++; return STEP_NEXT;
    case OP_GOTO: {
        int target = find_label(p, in.target);
        if (target < 0) {
            fprintf(stderr, "unknown label %s\n", in.target);
            return STEP_ERROR;
        }
        m->pc = target;
        return STEP_NEXT;
    }
    case OP_IFPOS: {
        if (m->reg[REG_A].coeff <= 0) {
            m->pc++;
            return STEP_NEXT;
        }
        int target = find_label(p, in.target);
        if (target < 0) {
            fprintf(stderr, "unknown label %s\n", in.target);
            return STEP_ERROR;
        }
        m->pc = target;
        return STEP_NEXT;
    }
    case OP_RS: {
        decimal tmp = m->reg[REG_D];
        m->reg[REG_D] = m->reg[REG_R];
        m->reg[REG_R] = tmp;
        m->pc++; return STEP_NEXT;
    }
    case OP_HALT:
        return STEP_STOP;
    }
    return STEP_ERROR;
}

static bool run_program(const program *p, char start, FILE *input, bool trace) {
    machine m;
    memset(&m, 0, sizeof(m));
    m.decimals = p->decimals;
    m.input = input;
    m.trace = trace;
    for (int i = 0; i < REG_COUNT; i++) {
        if (p->has_init[i]) m.reg[i] = p->init[i];
    }
    m.pc = entry_pc(p, start);
    if (m.pc < 0) {
        fprintf(stderr, "no entry point for %c\n", start);
        return false;
    }
    while (m.pc >= 0 && m.pc < p->inslen) {
        int rc = step(&m, p);
        if (rc == STEP_STOP) return true;
        if (rc == STEP_ERROR) return false;
    }
    return true;
}

int main(int argc, char **argv) {
    const char *path = NULL;
    const char *input_path = NULL;
    char start = 'V';
    bool trace = false;
    FILE *input = stdin;
    program p;
    bool ok;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            start = argv[++i][0];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: p101 [--start V|W|Y|Z] [--input FILE] [--trace] program.p101\n");
            return 0;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "Usage: p101 [--start V|W|Y|Z] [--input FILE] [--trace] program.p101\n");
            return 2;
        }
    }
    if (path == NULL || strchr("VWYZ", start) == NULL) {
        fprintf(stderr, "Usage: p101 [--start V|W|Y|Z] [--input FILE] [--trace] program.p101\n");
        return 2;
    }
    if (input_path != NULL) {
        input = fopen(input_path, "r");
        if (input == NULL) {
            fprintf(stderr, "cannot open %s\n", input_path);
            return 1;
        }
    }
    ok = load_program(path, &p) && run_program(&p, start, input, trace);
    if (input != stdin) fclose(input);
    return ok ? 0 : 1;
}
