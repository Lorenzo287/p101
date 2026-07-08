#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "num.h"

#define CORE_INS 48      // instructions stored in p1, p2
#define MAX_INS 120      // extended with D, E, F
#define OVERFLOW_HALF_INS 12  // instruction slots in one D/E/F half-register
#define MAX_LABELS 128

#define MAX_LINE 256
#define MAX_NAME 32
#define MAX_KEY_TOKENS 2
#define MAX_DIRECTIVE_TOKENS 3

static bool clicolor;

/* === Program Model === */

enum { P101_ERR = -1, P101_STOP, P101_NEXT };

enum reg {
    R_M, R_A, R_R,  // input, accumulator, remainder
    R_B, R_b, R_C, R_c,  // data
    R_D, R_d, R_E, R_e, R_F, R_f,  // inst plus data
    R_COUNT
};

enum op {
    I_MARK, I_INPUT, I_LIT, I_LITDIG, I_STORE, I_LOAD, I_SWAP,
    I_FRAC, I_ABS, I_ADD, I_SUB, I_MUL, I_DIV, I_SQRT,
    I_CLEAR, I_PRINT, I_NL, I_GOTO, I_IFPOS, I_RS
};

/* One parsed program instruction. */
struct ins {
    enum op op;
    enum reg reg;
    int line;
    // const literal generation
    int lit_digit;
    bool lit_point, lit_last, lit_neg;
    // jump target
    char target[MAX_NAME];
};

/* A reference point name and the index it points to. */
struct label {
    char name[MAX_NAME];
    int pc;
};

/* Parsed program card plus setup metadata. */
struct prog {
    struct ins ins[MAX_INS];
    int nins;
    struct label labels[MAX_LABELS];
    int nlabels;
    // metadata
    struct num init[R_COUNT];
    bool hasinit[R_COUNT];
    bool instused[R_COUNT];
    int datacap[R_COUNT];
    int decimals;
};

/* Runtime state for one program execution. */
struct machine {
    struct num reg[R_COUNT];
    struct num rs_right, rs_left;
    bool split[5];
    bool rs_saved, rs_split, rs_protected;
    int pc, decimals;
    FILE *input;
};

/* === Text Helpers === */

static char *trim(char *s) {
    while(isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while(len && isspace((unsigned char)s[len-1])) s[--len] = '\0';
    return s;
}

static int eq(const char *a, const char *b) {
    while(*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static void error_msg(bool red, const char *fmt, ...) {
    va_list ap;
    if (red) fputs("\033[31m",stderr);
    va_start(ap,fmt);
    vfprintf(stderr,fmt,ap);
    va_end(ap);
    if (red) fputs("\033[0m",stderr);
}

static int parsedecimals(const char *s, int *out) {
    while(isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    char *end;
    long value = strtol(s,&end,10);
    while(isspace((unsigned char)*end)) end++;
    if (*end || value < 0 || value > 15) return 0;
    *out = (int)value;
    return 1;
}

/* === Register Layout And Overlay === */

static int routine(int c) {
    return c == 'V' || c == 'W' || c == 'Y' || c == 'Z';
}

static const char *regname(int r) {
    static const char *name[] = {
        "M","A","R","B","B/","C","C/","D","D/","E","E/","F","F/"
    };
    return name[r];
}

static int splitreg(int r) {
    return r == R_b || r == R_c || r == R_d || r == R_e || r == R_f;
}

static int regcap(int r) {
    if (splitreg(r)) return 11;
    if (r == R_A) return 23;
    if (r == R_R) return 44;
    return 22;
}

static int stored_digits(struct num d) {
    return dstoredigits(d);
}

/* Enforce the P101 register digit capacities. */
static int checkfitcap(const char *name, int cap, struct num d) {
    if (stored_digits(d) <= cap) return 1;
    error_msg(clicolor,"register %s capacity exceeded (%d digits, limit %d)\n",
              name,stored_digits(d),cap);
    return 0;
}

static int checkfit(int r, struct num d) {
    return checkfitcap(regname(r),regcap(r),d);
}

static int runtime_checkfitcap(const char *name, int cap, struct num d) {
    if (stored_digits(d) <= cap) return 1;
    error_msg(true,"register %s capacity exceeded (%d digits, limit %d)\n",
                  name,stored_digits(d),cap);
    return 0;
}

static int runtime_checkfit(int r, struct num d) {
    return runtime_checkfitcap(regname(r),regcap(r),d);
}

/* Map program slots after the 48 core instructions into F/f/E/e/D/d. */
static int insreg(int pc) {
    static int order[] = {R_F,R_f,R_E,R_e,R_D,R_d};
    if (pc < CORE_INS) return -1;
    int slot = (pc-CORE_INS)/OVERFLOW_HALF_INS;
    return slot < (int)(sizeof(order)/sizeof(order[0])) ? order[slot] : -1;
}

/* In a mixed data/instruction half, each leading S reserves one data digit. */
static int halfdatacap(const struct prog *p, int start, int count) {
    int cap = 0;
    for (int j = 0; j < count && start+j < p->nins; j++) {
        if (p->ins[start+j].op != I_INPUT) break;
        cap++;
    }
    return cap > 11 ? 11 : cap;
}

static int hasregdata(const struct prog *p, int r) {
    return !p->instused[r] || p->datacap[r] > 0;
}

/* Mark D/E/F register halves consumed by instruction overflow.
   The first 48 instructions are stored in p1/p2. Later instructions occupy
   F, f, E, e, D, d in 12-instruction chunks.
   Leading S slots reserve numeric digit positions, matching the P101
   technique for sharing one half between data and code. */
static int mark_instruction_regs(struct prog *p) {
    for (int pc = 0; pc < p->nins; pc++) {
        int r = insreg(pc);
        if (r >= 0) p->instused[r] = true;
    }
    for (int r = 0; r < R_COUNT; r++) {
        if (!p->instused[r]) continue;
        int start = -1;
        for (int pc = CORE_INS; pc < p->nins; pc++) {
            if (insreg(pc) == r) {
                start = pc;
                break;
            }
        }
        if (start >= 0) {
            int count = p->nins - start;
            if (count > OVERFLOW_HALF_INS) count = OVERFLOW_HALF_INS;
            p->datacap[r] = halfdatacap(p,start,count);
        }
        if (p->hasinit[r]) {
            if (p->datacap[r] == 0) {
                error_msg(clicolor,"register %s is occupied by program instructions\n",regname(r));
                return 0;
            }
            if (!checkfitcap(regname(r),p->datacap[r],p->init[r]))
                return 0;
        }
    }
    return 1;
}

static int regpair(int r) {
    return r >= R_B && r <= R_f ? (r-R_B)/2 : -1;
}

static int rightreg(int pair) {
    return R_B + pair*2;
}

static int leftreg(int pair) {
    return R_b + pair*2;
}

/* True for B/C/D/E/F, false for their lower-case halves. */
static int rightside(int r) {
    int pair = regpair(r);
    return pair >= 0 && r == rightreg(pair);
}

static int programsplit(const struct prog *p, int pair) {
    return p->instused[rightreg(pair)] || p->instused[leftreg(pair)];
}

static int regdatacap(const struct machine *m, const struct prog *p, int r) {
    int cap = regcap(r);
    /* Once split, the upper-case side is only the 11-digit right half. */
    if (regpair(r) >= 0 && rightside(r) && m->split[regpair(r)])
        cap = 11;
    if (p->instused[r] && p->datacap[r] < cap)
        cap = p->datacap[r];
    return cap;
}

static int wholeaccess(const struct machine *m, int r) {
    int pair = regpair(r);
    return pair >= 0 && rightside(r) && !m->split[pair];
}

/* Runtime register access: B-F overlay 22-digit whole registers with two
   11-digit halves. Program instructions may occupy D/E/F halves. */
static int canreadreg(const struct machine *m, const struct prog *p, int r) {
    int pair = regpair(r);
    if (r == R_R && m->rs_saved) {
        error_msg(true,"register R holds an RS-saved D register pair\n");
        return 0;
    }
    if (pair >= 0 && wholeaccess(m,r)) {
        if (!programsplit(p,pair)) return 1;
    } else if (hasregdata(p,r)) {
        return 1;
    }
    error_msg(true,"register %s is occupied by program instructions\n",regname(r));
    return 0;
}

static int canwritereg(const struct machine *m, const struct prog *p, int r, struct num d) {
    if (r == R_R && m->rs_saved) {
        if (m->rs_protected) {
            error_msg(true,"register R holds an RS-saved D register pair\n");
            return 0;
        }
        return runtime_checkfit(r,d);
    }
    if (!canreadreg(m,p,r)) return 0;
    int cap = regdatacap(m,p,r);
    return runtime_checkfitcap(regname(r),cap,d);
}

static int spliterror(int r, struct num d) {
    error_msg(true,"cannot split register %s containing %d digits (limit 11)\n",
                  regname(r),stored_digits(d));
    return 0;
}

static int makesplit(struct machine *m, int pair) {
    int right = rightreg(pair), left = leftreg(pair);
    if (m->split[pair]) return 1;
    /* Splitting keeps the current whole value in the right half. */
    if (stored_digits(m->reg[right]) > 11) return spliterror(right,m->reg[right]);
    m->reg[left] = dint(0);
    m->split[pair] = true;
    return 1;
}

static int getregval(struct machine *m, const struct prog *p, int r, struct num *out) {
    int pair = regpair(r);
    if (!canreadreg(m,p,r)) return 0;
    if (splitreg(r) && !makesplit(m,pair)) return 0;
    *out = m->reg[r];
    return 1;
}

static int setregval(struct machine *m, const struct prog *p, int r, struct num d) {
    int pair = regpair(r);
    if (!canwritereg(m,p,r,d)) return 0;
    if (pair < 0) {
        if (r == R_R) {
            m->rs_saved = false;
            m->rs_protected = false;
            m->rs_split = false;
        }
        m->reg[r] = d;
        return 1;
    }
    if (splitreg(r)) {
        if (!makesplit(m,pair)) return 0;
        m->reg[r] = d;
        return 1;
    }
    if (m->split[pair]) {
        m->reg[r] = d;
        return 1;
    }
    /* Writing a whole register clears the hidden left half. */
    m->reg[r] = d;
    m->reg[leftreg(pair)] = dint(0);
    return 1;
}

static int clearregval(struct machine *m, const struct prog *p, int r) {
    int pair = regpair(r);
    struct num zero = dint(0);
    if (r == R_M || r == R_R) {
        error_msg(true,"register %s cannot be cleared\n",regname(r));
        return 0;
    }
    if (pair < 0) return setregval(m,p,r,zero);
    if (!canwritereg(m,p,r,zero)) return 0;
    if (splitreg(r)) {
        if (!makesplit(m,pair)) return 0;
        m->reg[r] = zero;
        if (!programsplit(p,pair)) m->split[pair] = false;
        return 1;
    }
    m->reg[r] = zero;
    if (m->split[pair]) {
        if (dzerop(m->reg[leftreg(pair)]) &&
            !programsplit(p,pair))
            m->split[pair] = false;
        return 1;
    }
    m->reg[leftreg(pair)] = zero;
    return 1;
}

static int regid(const char *s) {
    static int full[] = {R_B,R_C,R_D,R_E,R_F};
    static int split[] = {R_b,R_c,R_d,R_e,R_f};
    if (s[0] == '\0') return -1;
    char base = s[0];
    if (s[1] == '\0' && base >= 'b' && base <= 'f') return split[base - 'b'];
    if (s[1] == '/' && s[2] == '\0' && base >= 'B' && base <= 'F') return split[base - 'B'];
    if (s[1] != '\0') return -1;
    if (base >= 'B' && base <= 'F') return full[base - 'B'];
    if (base == 'A') return R_A;
    if (base == 'M') return R_M;
    if (base == 'R') return R_R;
    return -1;
}

static int setreg(const char *prefix, enum reg *r) {
    if (prefix[0] == '\0') {
        *r = R_M;
        return 1;
    }
    int id = regid(prefix);
    if (id < 0) return 0;
    *r = (enum reg)id;
    return 1;
}

/* === Instruction Decoding === */

static int refpoint(const char *s) {
    size_t len = strlen(s);
    if (len == 2) return strchr("ABEF",s[0]) != NULL && routine(s[1]);
    if (len == 3) return strchr("ABEF",s[0]) != NULL && s[1] == '/' && routine(s[2]);
    return 0;
}

static int addlabel(struct prog *p, const char *name, int pc, int line) {
    if (p->nlabels == MAX_LABELS) {
        error_msg(clicolor,"line %d: too many labels\n",line);
        return 0;
    }
    snprintf(p->labels[p->nlabels].name,MAX_NAME,"%s",name);
    p->labels[p->nlabels].pc = pc;
    p->nlabels++;
    return 1;
}

static int findlabel(const struct prog *p, const char *name) {
    for (int j = 0; j < p->nlabels; j++)
        if (!strcmp(p->labels[j].name,name)) return p->labels[j].pc;
    return -1;
}

static int entry(const struct prog *p, int key) {
    char label[4];
    snprintf(label,sizeof(label),"A%c",key);
    return findlabel(p,label);
}

static int addins(struct prog *p, struct ins in) {
    if (p->nins == MAX_INS) {
        error_msg(clicolor,"line %d: too many instructions for P101 memory\n",in.line);
        return 0;
    }
    p->ins[p->nins++] = in;
    return 1;
}

/* Decode an operation suffix while accepting ASCII keyboard aliases. */
static int splitkey(const char *key, char *prefix, char *op) {
    struct { const char *tail, *op; } tail[] = {
        {"sqrt","sqrt"}, {"><","><"}, {"><A","><"}, {"<M","<"}, {">A",">"}, {NULL,NULL}
    };
    size_t len = strlen(key);
    if (len == 0) return 0;
    for (int j = 0; tail[j].tail; j++) {
        size_t tlen = strlen(tail[j].tail);
        if (len >= tlen && !strcmp(key+len-tlen,tail[j].tail)) {
            size_t plen = len-tlen;
            if (plen >= MAX_NAME) return 0;
            memcpy(prefix,key,plen);
            prefix[plen] = '\0';
            snprintf(op,MAX_NAME,"%s",tail[j].op);
            return 1;
        }
    }
    if (!strchr("S<>+-x:#*",key[len-1])) return 0;
    size_t plen = len-1;
    if (plen >= MAX_NAME) return 0;
    memcpy(prefix,key,plen);
    prefix[plen] = '\0';
    op[0] = key[len-1];
    op[1] = '\0';
    return 1;
}

static int litdigit(const char *op) {
    if (!strcmp(op,"><")) return 3;
    if (op[1]) return -1;
    switch(op[0]) {
    case 'S': return 0; case '>': return 1; case '<': return 2;
    case '+': return 4; case '-': return 5; case 'x': return 6;
    case ':': return 7; case '#': return 8; case '*': return 9;
    }
    return -1;
}

static void decode_litdigit(struct ins *in, const char *prefix, const char *op) {
    int digit = litdigit(op);
    size_t len = strlen(prefix);
    if (digit < 0 || (len != 1 && !(len == 2 && prefix[1] == '/'))) return;
    /* R/F continue a literal; D/E terminate it. Slash marks the decimal point. */
    if (!strchr("RFDE",prefix[0])) return;
    in->lit_digit = digit;
    in->lit_point = len == 2;
    in->lit_last = prefix[0] == 'D' || prefix[0] == 'E';
    in->lit_neg = prefix[0] == 'E';
}

/* Translate P101 jump chords into the labels they target. */
static int jumptarget(const char *key, bool *conditional, char *target) {
    size_t len = strlen(key);
    *conditional = false;
    if (len == 1 && routine(key[0])) {
        snprintf(target,MAX_NAME,"A%c",key[0]);
        return 1;
    }
    if (len == 2 && strchr("CDR",key[0]) && routine(key[1])) {
        snprintf(target,MAX_NAME,"%c%c",key[0] == 'C' ? 'B' : key[0] == 'D' ? 'E' : 'F',key[1]);
        return 1;
    }
    if (len == 2 && key[0] == '/' && routine(key[1])) {
        *conditional = true;
        snprintf(target,MAX_NAME,"A/%c",key[1]);
        return 1;
    }
    if (len == 3 && strchr("CDR",key[0]) && key[1] == '/' && routine(key[2])) {
        *conditional = true;
        snprintf(target,MAX_NAME,"%c/%c",key[0] == 'C' ? 'B' : key[0] == 'D' ? 'E' : 'F',key[2]);
        return 1;
    }
    return 0;
}

/* Decode one normalized key chord into an executable instruction. */
static int decodekey(const char *key, int line, struct ins *in) {
    memset(in,0,sizeof(*in));
    in->reg = R_M;
    in->line = line;
    in->lit_digit = -1;
    bool conditional;
    if (jumptarget(key,&conditional,in->target)) {
        in->op = conditional ? I_IFPOS : I_GOTO;
        return 1;
    }
    if (!strcmp(key,"RS")) {
        in->op = I_RS;
        return 1;
    }
    char prefix[MAX_NAME], op[MAX_NAME];
    if (!splitkey(key,prefix,op)) {
        return 0;
    }
    decode_litdigit(in,prefix,op);
    if (!strcmp(prefix,"A/") && !strcmp(op,"<")) {
        in->op = I_LIT;
        return 1;
    }
    if (!strcmp(op,"S")) {
        if (prefix[0] && in->lit_digit < 0) return 0;
        in->op = prefix[0] ? I_LITDIG : I_INPUT;
    } else if (!strcmp(op,"<")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_STORE;
    } else if (!strcmp(op,">")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_LOAD;
    } else if (!strcmp(op,"><")) {
        if (!strcmp(prefix,"/")) in->op = I_FRAC;
        else if (!strcmp(prefix,"A")) in->op = I_ABS;
        else {
            if (!setreg(prefix,&in->reg)) return 0;
            in->op = I_SWAP;
        }
    } else if (!strcmp(op,"+")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_ADD;
    } else if (!strcmp(op,"-")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_SUB;
    } else if (!strcmp(op,"x")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_MUL;
    } else if (!strcmp(op,":")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_DIV;
    } else if (!strcmp(op,"sqrt")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_SQRT;
    } else if (!strcmp(op,"#")) {
        if (!strcmp(prefix,"/")) in->op = I_NL;
        else {
            if (!setreg(prefix,&in->reg)) return 0;
            in->op = I_PRINT;
        }
    } else if (!strcmp(op,"*")) {
        if (!setreg(prefix,&in->reg)) return 0;
        in->op = I_CLEAR;
    } else {
        return 0;
    }
    return 1;
}

/* === Source File Parsing === */

/* Parse setup metadata such as decimal wheel and initial register values. */
static int directive(struct prog *p, char **tokens, int ntok, int line) {
    if (eq(tokens[0],".wheel")) {
        int decimals;
        if (ntok != 2 || !parsedecimals(tokens[1],&decimals)) return 0;
        p->decimals = decimals;
        return 1;
    }
    if (eq(tokens[0],".init")) {
        if (ntok != 3) return 0;
        int r = regid(tokens[1]);
        if (r < 0) return 0;
        struct num value;
        if (!dparse(tokens[2],&value)) return 0;
        if (!checkfit(r,value)) return 0;
        p->init[r] = value;
        p->hasinit[r] = true;
        return 1;
    }
    error_msg(clicolor,"line %d: unknown directive %s\n",line,tokens[0]);
    return 0;
}

/* Parse one normalized key chord into a stored program instruction. */
static int parsekey(struct prog *p, const char *key, int line) {
    if (refpoint(key)) {
        struct ins in;
        memset(&in,0,sizeof(in));
        in.reg = R_M;
        in.line = line;
        in.lit_digit = -1;
        if (!addlabel(p,key,p->nins,line)) return 0;
        in.op = I_MARK;
        return addins(p,in);
    }
    struct ins in;
    if (!decodekey(key,line,&in)) {
        error_msg(clicolor,"line %d: cannot parse key chord %s\n",line,key);
        return 0;
    }
    return addins(p,in);
}

static int tokwords(char *s, char **tokens, int tokmax) {
    int ntok = 0;
    char *t = strtok(s," \t\r\n");
    while(t) {
        if (ntok == tokmax) return -1;
        tokens[ntok++] = t;
        t = strtok(NULL," \t\r\n");
    }
    return ntok;
}

static void upper_routine_key(char *key) {
    if (key[0] && !key[1]) {
        char c = (char)toupper((unsigned char)key[0]);
        if (routine(c)) key[0] = c;
    }
}

static int keyfromline(char *line, char *key, size_t size) {
    char *comment = strchr(line,';');
    if (comment) *comment = '\0';
    line = trim(line);
    if (!line[0]) return 0;
    char *tokens[MAX_KEY_TOKENS];
    int ntok = tokwords(line,tokens,MAX_KEY_TOKENS);
    if (ntok <= 0) return 0;
    if (ntok == 1) {
        snprintf(key,size,"%s",tokens[0]);
        upper_routine_key(key);
        return 1;
    }
    if (ntok == 2) {
        snprintf(key,size,"%s%s",tokens[0],tokens[1]);
        upper_routine_key(key);
        return 1;
    }
    return 0;
}

/* Strip comments, join spaced chords, and dispatch directives/key chords. */
static int parseline(struct prog *p, char *line, int lineno) {
    char *comment = strchr(line,';');
    if (comment) *comment = '\0';
    line = trim(line);
    if (!line[0]) return 1;
    char *tokens[MAX_DIRECTIVE_TOKENS];
    int ntok = tokwords(line,tokens,MAX_DIRECTIVE_TOKENS);
    if (ntok <= 0) {
        error_msg(clicolor,"line %d: invalid syntax\n",lineno);
        return 0;
    }
    if (tokens[0][0] == '.') {
        if (!directive(p,tokens,ntok,lineno)) {
            error_msg(clicolor,"line %d: invalid directive\n",lineno);
            return 0;
        }
        return 1;
    }
    if (ntok == 1) return parsekey(p,tokens[0],lineno);
    if (ntok == 2) {
        char key[MAX_NAME*2];
        snprintf(key,sizeof(key),"%s%s",tokens[0],tokens[1]);
        return parsekey(p,key,lineno);
    }
    error_msg(clicolor,"line %d: expected one key chord\n",lineno);
    return 0;
}

/* Load and validate a source file before execution starts. */
static int load(const char *path, struct prog *p) {
    FILE *fp = fopen(path,"r");
    if (!fp) {
        error_msg(clicolor,"cannot open %s\n",path);
        return 0;
    }
    memset(p,0,sizeof(*p));
    int lineno = 0;
    char line[MAX_LINE];
    while(fgets(line,sizeof(line),fp)) {
        if (!parseline(p,line,++lineno)) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return mark_instruction_regs(p);
}

/* === Instruction Execution === */

/* Execute the original P101 constant-entry digit sequence into M. */
static int literal(struct machine *m, const struct prog *p) {
    char digits[32], number[80];
    bool point[32], neg = false, done = false;
    int pc = m->pc+1, len = 0, out = 0;
    while(pc < p->nins && len < (int)sizeof(digits)-1) {
        struct ins in = p->ins[pc];
        if (in.lit_digit < 0) return 0;
        digits[len] = (char)('0'+in.lit_digit);
        point[len] = (char)in.lit_point;
        if (in.lit_last) {
            neg = in.lit_neg;
            len++;
            done = true;
            break;
        }
        len++;
        pc++;
    }
    if (!done) return 0;
    if (neg) number[out++] = '-';
    for (int j = len-1; j >= 0; j--) {
        number[out++] = digits[j];
        if (point[j]) number[out++] = '.';
    }
    number[out] = '\0';
    struct num value;
    if (!dparse(number,&value) || !setregval(m,p,R_M,value)) return 0;
    m->pc = pc+1;
    return 1;
}

static int numoverflow(const struct machine *m) {
    error_msg(true,"numeric overflow at step %d\n",m->pc+1);
    return 0;
}

/* Shared add/subtract/multiply/divide path with decimal-wheel truncation. */
static int binary(struct machine *m, const struct prog *p, int r, int op) {
    struct num src;
    if (!getregval(m,p,r,&src)) return 0;
    struct num exact, rr, aa;
    switch(op) {
    case I_ADD:
        if (!getregval(m,p,R_A,&exact)) return 0;
        if (!dadd(exact,src,&rr) || !dscale(rr,m->decimals,&aa))
            return numoverflow(m);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_R,rr) ||
            !setregval(m,p,R_A,aa)) return 0;
        return 1;
    case I_SUB:
        if (!getregval(m,p,R_A,&exact)) return 0;
        if (!dsub(exact,src,&rr) || !dscale(rr,m->decimals,&aa))
            return numoverflow(m);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_R,rr) ||
            !setregval(m,p,R_A,aa)) return 0;
        return 1;
    case I_MUL:
        if (!getregval(m,p,R_A,&exact)) return 0;
        if (!dmul(exact,src,&rr) || !dscale(rr,m->decimals,&aa))
            return numoverflow(m);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_R,rr) ||
            !setregval(m,p,R_A,aa)) return 0;
        return 1;
    case I_DIV: {
        struct num q;
        if (!getregval(m,p,R_A,&exact)) return 0;
        if (dzerop(src)) {
            error_msg(true,"division by zero at step %d\n",m->pc+1);
            return 0;
        }
        if (!ddiv(exact,src,m->decimals,&q)) {
            return numoverflow(m);
        }
        struct num product;
        if (!dmul(q,src,&product) || !dsub(exact,product,&rr))
            return numoverflow(m);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_A,q) ||
            !setregval(m,p,R_R,rr)) return 0;
        return 1;
    }
    }
    return 0;
}

static int jump(struct machine *m, const struct prog *p, const char *target) {
    int pc = findlabel(p,target);
    if (pc < 0) {
        error_msg(true,"unknown label %s\n",target);
        return P101_ERR;
    }
    m->pc = pc;
    return P101_NEXT;
}

static int printreg(struct machine *m, const struct prog *p, int r) {
    struct num value;
    char text[128];
    if (!getregval(m,p,r,&value)) return 0;
    if (!dstr(value,m->decimals,r != R_R,text,sizeof(text)))
        return numoverflow(m);
    printf("%s#%s%s\n",regname(r),(regname(r)[1]) ? "" : " ",text);
    return 1;
}

static int nextpc(struct machine *m, bool advance) {
    if (advance) m->pc++;
    return P101_NEXT;
}

static int rsxchg(struct machine *m, const struct prog *p);

static int execins(struct machine *m, const struct prog *p, struct ins in,
                   bool advance) {
    struct num value;
    switch(in.op) {
    case I_MARK:
        return nextpc(m,advance);
    case I_INPUT:
        return P101_STOP;
    case I_LIT:
        if (!literal(m,p)) {
            error_msg(true,"invalid literal sequence at step %d\n",m->pc+1);
            return P101_ERR;
        }
        return P101_NEXT;
    case I_LITDIG:
        error_msg(true,"literal digit outside A/< sequence at step %d\n",m->pc+1);
        return P101_ERR;
    case I_STORE:
        if (!getregval(m,p,R_M,&value) || !setregval(m,p,in.reg,value))
            return P101_ERR;
        return nextpc(m,advance);
    case I_LOAD:
        if (!getregval(m,p,in.reg,&value) || !setregval(m,p,R_A,value))
            return P101_ERR;
        return nextpc(m,advance);
    case I_SWAP: {
        struct num acc;
        if (in.reg == R_R) {
            if (!getregval(m,p,R_R,&value) || !setregval(m,p,R_A,value))
                return P101_ERR;
        } else {
            if (!getregval(m,p,in.reg,&value) || !getregval(m,p,R_A,&acc) ||
                !setregval(m,p,in.reg,acc) || !setregval(m,p,R_A,value))
                return P101_ERR;
        }
        return nextpc(m,advance);
    }
    case I_FRAC:
        if (!getregval(m,p,R_A,&value)) return P101_ERR;
        if (!dfrac(value,&value)) {
            numoverflow(m);
            return P101_ERR;
        }
        if (!setregval(m,p,R_M,value)) return P101_ERR;
        return nextpc(m,advance);
    case I_ABS:
        if (!getregval(m,p,R_A,&value)) return P101_ERR;
        value = dabs(value);
        if (!setregval(m,p,R_A,value)) return P101_ERR;
        return nextpc(m,advance);
    case I_ADD: case I_SUB: case I_MUL: case I_DIV:
        if (!binary(m,p,in.reg,in.op)) return P101_ERR;
        return nextpc(m,advance);
    case I_SQRT: {
        if (!getregval(m,p,in.reg,&value)) return P101_ERR;
        struct num root, square, rem, twice;
        if (!dsqrt(value,m->decimals,&root) ||
            !dmul(root,root,&square) ||
            !dsub(value,square,&rem) ||
            !dmul(dint(2),root,&twice)) {
            numoverflow(m);
            return P101_ERR;
        }
        if (!setregval(m,p,R_M,twice) ||
            !setregval(m,p,R_A,root) || !setregval(m,p,R_R,rem))
            return P101_ERR;
        return nextpc(m,advance);
    }
    case I_CLEAR:
        if (!clearregval(m,p,in.reg)) return P101_ERR;
        return nextpc(m,advance);
    case I_PRINT:
        if (!printreg(m,p,in.reg)) return P101_ERR;
        return nextpc(m,advance);
    case I_NL:
        putchar('\n');
        return nextpc(m,advance);
    case I_GOTO:
        return jump(m,p,in.target);
    case I_IFPOS:
        if (!getregval(m,p,R_A,&value)) return P101_ERR;
        if (!dpos(value)) return nextpc(m,advance);
        return jump(m,p,in.target);
    case I_RS:
        if (!rsxchg(m,p)) return P101_ERR;
        return nextpc(m,advance);
    }
    return P101_ERR;
}

static int getdpair(struct machine *m, const struct prog *p,
                    struct num *right, struct num *left, bool *split) {
    int pair = regpair(R_D);
    *split = m->split[pair];
    if (!getregval(m,p,R_D,right)) return 0;
    if (*split) {
        if (!getregval(m,p,R_d,left)) return 0;
    } else {
        *left = dint(0);
    }
    return 1;
}

static int setdpair(struct machine *m, const struct prog *p,
                    struct num right, struct num left, bool split) {
    int pair = regpair(R_D);
    if (split) {
        if (!setregval(m,p,R_d,left) || !setregval(m,p,R_D,right)) return 0;
        return 1;
    }
    if (m->split[pair] && !clearregval(m,p,R_d)) return 0;
    return setregval(m,p,R_D,right);
}

static int rsxchg(struct machine *m, const struct prog *p) {
    struct num d_right, d_left, r_value;
    bool d_split;
    if (!getdpair(m,p,&d_right,&d_left,&d_split)) return 0;
    if (m->rs_saved) {
        struct num saved_right = m->rs_right, saved_left = m->rs_left;
        bool saved_split = m->rs_split;
        if (!setdpair(m,p,saved_right,saved_left,saved_split)) return 0;
        m->rs_right = d_right;
        m->rs_left = d_left;
        m->rs_split = d_split;
        m->rs_protected = false;
        return 1;
    }
    if (!getregval(m,p,R_R,&r_value)) return 0;
    m->rs_right = d_right;
    m->rs_left = d_left;
    m->rs_split = d_split;
    m->rs_saved = true;
    m->rs_protected = true;
    if (!setdpair(m,p,r_value,dint(0),false)) return 0;
    return 1;
}

/* === Cards And Operator Stops === */

static int cardreg(int r) {
    return r >= R_D && r <= R_f;
}

static void set_used_splits(struct machine *m, const struct prog *p, bool card_only) {
    int first = card_only ? 2 : 0;
    for (int j = first; j < 5; j++)
        if (p->instused[rightreg(j)] || p->instused[leftreg(j)] || p->hasinit[leftreg(j)])
            m->split[j] = true;
}

static int apply_initial_regs(struct machine *m, const struct prog *p, bool card_only) {
    for (int j = 0; j < R_COUNT; j++)
        if (p->hasinit[j] && (!card_only || cardreg(j)) &&
            !setregval(m,p,j,p->init[j])) return 0;
    return 1;
}

static char *commandarg(char *s, const char *cmd) {
    char *p = s;
    while(*cmd && *p &&
          toupper((unsigned char)*p) == toupper((unsigned char)*cmd)) {
        p++;
        cmd++;
    }
    if (*cmd || (*p && !isspace((unsigned char)*p))) return NULL;
    while(isspace((unsigned char)*p)) p++;
    return p;
}

static int loadcard(struct machine *m, struct prog *p, const char *path) {
    struct prog next;
    struct num zero = dint(0);
    bool old_color_errors = clicolor;
    clicolor = true;
    int ok = load(path,&next);
    clicolor = old_color_errors;
    if (!ok) return 0;
    *p = next;
    /* New cards replace the program-bearing D/E/F registers only. */
    for (int j = R_D; j <= R_f; j++) m->reg[j] = zero;
    for (int j = 2; j < 5; j++) m->split[j] = false;
    set_used_splits(m,p,true);
    return apply_initial_regs(m,p,true);
}

static int resume_stop(struct machine *m, bool can_resume) {
    if (!can_resume) {
        error_msg(true,"no stopped program location to resume; select a routine key\n");
        return P101_ERR;
    }
    m->pc++;
    return P101_NEXT;
}

static int operator_stop(struct machine *m, struct prog *p) {
    char line[MAX_LINE], copy[MAX_LINE], key[MAX_NAME*2], *s, *comment, *arg;
    bool can_resume = m->pc >= 0 && m->pc < p->nins;
	printf("S  ");
    while(fgets(line,sizeof(line),m->input)) {
        comment = strchr(line,';');
        if (comment) *comment = '\0';
        s = trim(line);
        if (!s[0]) continue;
        arg = commandarg(s,"CARD");
        if (arg) {
            if (!arg[0]) {
                error_msg(true,"CARD requires a program path\n");
                return P101_ERR;
            }
            if (!loadcard(m,p,arg)) return P101_ERR;
            m->pc = -1;
            can_resume = false;
            continue;
        }
        if (eq(s,"START")) return resume_stop(m,can_resume);
        struct num value;
        if (dparse(s,&value)) {
            if (!setregval(m,p,R_M,value)) return P101_ERR;
            return resume_stop(m,can_resume);
        }
        snprintf(copy,sizeof(copy),"%s",s);
        if (!keyfromline(copy,key,sizeof(key))) {
            error_msg(true,"invalid input: %s\n",s);
            return P101_ERR;
        }
        if (refpoint(key)) {
            return jump(m,p,key);
        }
        struct ins in;
        if (!decodekey(key,0,&in)) {
            error_msg(true,"invalid input: %s\n",s);
            return P101_ERR;
        }
        if (in.op == I_INPUT) return resume_stop(m,can_resume);
        if (in.op == I_GOTO) return jump(m,p,in.target);
        if (in.op == I_IFPOS) {
            if (!getregval(m,p,R_A,&value)) return P101_ERR;
            if (dpos(value)) return jump(m,p,in.target);
            return resume_stop(m,can_resume);
        }
        error_msg(true,"invalid operator input: %s\n",s);
        return P101_ERR;
    }
    return P101_STOP;
}

static int startpoint(const struct prog *p, const char *text) {
    char line[MAX_LINE], key[MAX_NAME*2];
    snprintf(line,sizeof(line),"%s",text);
    if (!keyfromline(line,key,sizeof(key))) {
        error_msg(true,"invalid start origin %s\n",text);
        return -1;
    }
    if (!key[1] && routine(key[0])) {
        int pc = entry(p,key[0]);
        if (pc < 0) error_msg(true,"no entry point for %c\n",key[0]);
        return pc;
    }
    if (refpoint(key)) {
        int pc = findlabel(p,key);
        if (pc < 0) error_msg(true,"unknown label %s\n",key);
        return pc;
    }
    struct ins in;
    if (!decodekey(key,0,&in) || in.op != I_GOTO) {
        error_msg(true,"--start requires an unconditional origin or reference point\n");
        return -1;
    }
    int pc = findlabel(p,in.target);
    if (pc < 0) error_msg(true,"unknown label %s\n",in.target);
    return pc;
}

/* Execute the instruction at the current program counter. */
static int step(struct machine *m, struct prog *p) {
    struct ins in = p->ins[m->pc];
    if (in.op == I_INPUT) return operator_stop(m,p);
    return execins(m,p,in,true);
}

/* Initialize machine state and run until stop, error, or end of program. */
static int run(struct prog *p, const char *start, FILE *input) {
    struct machine m;
    memset(&m,0,sizeof(m));
    m.decimals = p->decimals;
    m.input = input;
    set_used_splits(&m,p,false);
    if (!apply_initial_regs(&m,p,false)) return 0;
    m.pc = startpoint(p,start);
    if (m.pc < 0) {
        return 0;
    }
    while(m.pc >= 0 && m.pc < p->nins) {
        int rc = step(&m,p);
        if (rc == P101_STOP) return 1;
        if (rc == P101_ERR) return 0;
    }
    return 1;
}

/* === Command Line === */

static void usage(FILE *fp) {
    fprintf(fp,"Usage: p101 [--start ORIGIN] [--input FILE] program.p101\n");
}

int main(int argc, char **argv) {
    const char *prog_path = NULL, *input_path = NULL;
    const char *start = "V";
    for (int j = 1; j < argc; j++) {
        if (!strcmp(argv[j],"--start") && j+1 < argc) {
            start = argv[++j];
        } else if (!strcmp(argv[j],"--input") && j+1 < argc) {
            input_path = argv[++j];
        } else if (!strcmp(argv[j],"-h") || !strcmp(argv[j],"--help")) {
            usage(stdout);
            return 0;
        } else if (!prog_path) {
            prog_path = argv[j];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (!prog_path) {
        usage(stderr);
        return 2;
    }
    FILE *input = stdin;
    if (input_path) {
        input = fopen(input_path,"r");
        if (!input) {
            fprintf(stderr,"cannot open %s\n",input_path);
            return 1;
        }
    }
    struct prog p;
    int ok = load(prog_path,&p) && run(&p,start,input);
    if (input != stdin) fclose(input);
    return ok ? 0 : 1;
}
