#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INS 120
#define CORE_INS 48
#define OVERFLOW_INS 12
#define MAX_LABELS 128
#define MAX_LINE 256
#define MAX_NAME 32

__extension__ typedef __int128 i128;

enum { P101_ERR = -1, P101_STOP, P101_NEXT };

enum {
    R_M, R_A, R_R, R_B, R_BS, R_C, R_CS,
    R_D, R_DS, R_E, R_ES, R_F, R_FS, R_COUNT
};

enum {
    I_MARK, I_INPUT, I_LIT, I_LITDIG, I_STORE, I_LOAD, I_SWAP,
    I_FRAC, I_ABS, I_ADD, I_SUB, I_MUL, I_DIV, I_SQRT,
    I_CLEAR, I_PRINT, I_NL, I_GOTO, I_IFPOS, I_RS
};

struct num {
    i128 n;
    int scale;
};

struct ins {
    int op, reg, line;
    char key[MAX_NAME], target[MAX_NAME];
};

struct label {
    char name[MAX_NAME];
    int pc;
};

struct prog {
    struct ins ins[MAX_INS];
    int nins;
    struct label labels[MAX_LABELS];
    int nlabels;
    struct num init[R_COUNT];
    int hasinit[R_COUNT];
    int used[R_COUNT];
    int decimals;
};

struct mach {
    struct num reg[R_COUNT];
    int split[5];
    int pc, decimals, trace;
    FILE *input;
};

/* Decimal numbers: integer coefficient plus decimal scale. */
static i128 pow10i(int n) {
    i128 v = 1;
    while(n-- > 0) v *= 10;
    return v;
}

static struct num dnew(i128 n, int scale) {
    struct num d = {n,scale};
    if (d.n == 0) {
        d.scale = 0;
        return d;
    }
    while(d.scale > 0 && d.n % 10 == 0) {
        d.n /= 10;
        d.scale--;
    }
    return d;
}

static struct num dint(int n) {
    return dnew(n,0);
}

static struct num dscale(struct num d, int scale) {
    if (d.scale < scale) d.n *= pow10i(scale-d.scale);
    else if (d.scale > scale) d.n /= pow10i(d.scale-scale);
    return dnew(d.n,scale);
}

static struct num dadd(struct num a, struct num b) {
    int scale = a.scale > b.scale ? a.scale : b.scale;
    i128 an = a.n * pow10i(scale-a.scale);
    i128 bn = b.n * pow10i(scale-b.scale);
    return dnew(an+bn,scale);
}

static struct num dsub(struct num a, struct num b) {
    int scale = a.scale > b.scale ? a.scale : b.scale;
    i128 an = a.n * pow10i(scale-a.scale);
    i128 bn = b.n * pow10i(scale-b.scale);
    return dnew(an-bn,scale);
}

static struct num dmul(struct num a, struct num b) {
    return dnew(a.n*b.n,a.scale+b.scale);
}

static int ddiv(struct num a, struct num b, int scale, struct num *q) {
    int exp;
    i128 num = a.n, den = b.n;
    if (b.n == 0) return 0;
    exp = b.scale + scale - a.scale;
    if (exp >= 0) num *= pow10i(exp);
    else den *= pow10i(-exp);
    *q = dnew(num/den,scale);
    return 1;
}

static i128 isqrt(i128 n) {
    i128 lo = 0, hi = 1, mid;
    if (n <= 0) return 0;
    while(hi <= n/hi) hi *= 2;
    while(lo+1 < hi) {
        mid = lo + (hi-lo)/2;
        if (mid <= n/mid) lo = mid;
        else hi = mid;
    }
    return lo;
}

static struct num dsqrt(struct num d, int scale) {
    i128 n = d.n < 0 ? -d.n : d.n;
    int exp = 2*scale - d.scale;
    if (exp >= 0) n *= pow10i(exp);
    else n /= pow10i(-exp);
    return dnew(isqrt(n),scale);
}

static struct num dfrac(struct num d) {
    i128 unit;
    if (d.scale == 0) return dint(0);
    unit = pow10i(d.scale);
    return dnew(d.n - (d.n/unit)*unit,d.scale);
}

/* Parse user/input decimal text into the normalized coefficient/scale form. */
static int dparse(const char *s, struct num *out) {
    int neg = 0, dot = 0, any = 0, scale = 0;
    i128 n = 0;
    while(isspace((unsigned char)*s)) s++;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    while(*s) {
        if (isdigit((unsigned char)*s)) {
            any = 1;
            n = n*10 + (*s-'0');
            if (dot) scale++;
        } else if ((*s == '.' || *s == ',') && !dot) {
            dot = 1;
        } else if (!isspace((unsigned char)*s)) {
            return 0;
        }
        s++;
    }
    if (!any) return 0;
    *out = dnew(neg ? -n : n,scale);
    return 1;
}

/* Convert an __int128 coefficient to decimal digits. */
static void i128str(i128 v, char *buf, size_t size) {
    char tmp[80];
    int neg = v < 0, pos = 0;
    size_t out = 0;
    if (size == 0) return;
    if (neg) v = -v;
    do {
        tmp[pos++] = (char)('0' + v%10);
        v /= 10;
    } while(v && pos < (int)sizeof(tmp));
    if (neg && out+1 < size) buf[out++] = '-';
    while(pos && out+1 < size) buf[out++] = tmp[--pos];
    buf[out] = '\0';
}

/* Convert a decimal value to the P101 comma-decimal print format. */
static void dstr(struct num d, int scale, int fixed, char *buf, size_t size) {
    char digits[96], tmp[128];
    int pos = 0, len, intlen, j;
    i128 v;
    if (fixed) d = dscale(d,scale);
    if (d.n < 0) {
        tmp[pos++] = '-';
        v = -d.n;
    } else {
        v = d.n;
    }
    i128str(v,digits,sizeof(digits));
    len = (int)strlen(digits);
    if (d.scale == 0) {
        for (j = 0; j < len; j++) tmp[pos++] = digits[j];
    } else {
        intlen = len - d.scale;
        if (intlen <= 0) {
            tmp[pos++] = '0';
            tmp[pos++] = ',';
            for (j = 0; j < -intlen; j++) tmp[pos++] = '0';
            for (j = 0; j < len; j++) tmp[pos++] = digits[j];
        } else {
            for (j = 0; j < intlen; j++) tmp[pos++] = digits[j];
            tmp[pos++] = ',';
            for (; j < len; j++) tmp[pos++] = digits[j];
        }
    }
    tmp[pos] = '\0';
    if (!fixed) {
        while(pos > 0 && tmp[pos-1] == '0') tmp[--pos] = '\0';
        if (pos > 0 && tmp[pos-1] == ',') tmp[--pos] = '\0';
    }
    snprintf(buf,size,"%s",tmp);
}

/* Parser helpers. */
static char *trim(char *s) {
    size_t len;
    while(isspace((unsigned char)*s)) s++;
    len = strlen(s);
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
    return r == R_BS || r == R_CS || r == R_DS || r == R_ES || r == R_FS;
}

static int regcap(int r) {
    if (splitreg(r)) return 11;
    if (r == R_A) return 23;
    if (r == R_R) return 44;
    return 22;
}

static int ndigits(i128 n) {
    int digits = 1;
    if (n < 0) n = -n;
    while(n >= 10) {
        n /= 10;
        digits++;
    }
    return digits;
}

static int stored_digits(struct num d) {
    int digits = ndigits(d.n);
    return d.scale >= digits ? d.scale+1 : digits;
}

static int fitsreg(int r, struct num d) {
    return stored_digits(d) <= regcap(r);
}

/* Enforce the P101 register digit capacities. */
static int checkfitcap(const char *name, int cap, struct num d) {
    if (stored_digits(d) <= cap) return 1;
    fprintf(stderr,"register %s capacity exceeded (%d digits, limit %d)\n",
            name,stored_digits(d),cap);
    return 0;
}

static int checkfit(int r, struct num d) {
    return checkfitcap(regname(r),regcap(r),d);
}

/* Map program slots after the 48 core instructions into F/f/E/e/D/d. */
static int insreg(int pc) {
    static int order[] = {R_F,R_FS,R_E,R_ES,R_D,R_DS};
    int slot;
    if (pc < CORE_INS) return -1;
    slot = (pc-CORE_INS)/OVERFLOW_INS;
    return slot < (int)(sizeof(order)/sizeof(order[0])) ? order[slot] : -1;
}

/* Mark D/E/F register halves consumed by instruction overflow. */
static int mark_instruction_regs(struct prog *p) {
    int pc, r;
    for (pc = 0; pc < p->nins; pc++) {
        r = insreg(pc);
        if (r >= 0) p->used[r] = 1;
    }
    for (r = 0; r < R_COUNT; r++) {
        if (p->used[r] && p->hasinit[r]) {
            fprintf(stderr,"register %s is occupied by program instructions\n",regname(r));
            return 0;
        }
    }
    return 1;
}

static int regpair(int r) {
    return r >= R_B && r <= R_FS ? (r-R_B)/2 : -1;
}

static int rightreg(int pair) {
    return R_B + pair*2;
}

static int leftreg(int pair) {
    return R_BS + pair*2;
}

static int rightside(int r) {
    int pair = regpair(r);
    return pair >= 0 && r == rightreg(pair);
}

static int wholeaccess(const struct mach *m, int r) {
    int pair = regpair(r);
    return pair >= 0 && rightside(r) && !m->split[pair];
}

/* Runtime register access: B-F overlay whole registers with split halves. */
static int canreadreg(const struct mach *m, const struct prog *p, int r) {
    int pair = regpair(r);
    if (pair >= 0 && wholeaccess(m,r)) {
        if (!p->used[rightreg(pair)] && !p->used[leftreg(pair)]) return 1;
    } else if (!p->used[r]) {
        return 1;
    }
    fprintf(stderr,"register %s is occupied by program instructions\n",regname(r));
    return 0;
}

static int canwritereg(const struct mach *m, const struct prog *p, int r, struct num d) {
    int cap = regcap(r);
    if (!canreadreg(m,p,r)) return 0;
    if (regpair(r) >= 0 && rightside(r) && m->split[regpair(r)])
        cap = 11;
    return checkfitcap(regname(r),cap,d);
}

static int spliterror(int r, struct num d) {
    fprintf(stderr,"cannot split register %s containing %d digits (limit 11)\n",
            regname(r),stored_digits(d));
    return 0;
}

static int makesplit(struct mach *m, int pair) {
    int right = rightreg(pair), left = leftreg(pair);
    if (m->split[pair]) return 1;
    if (!fitsreg(left,m->reg[right])) return spliterror(right,m->reg[right]);
    m->reg[left] = dint(0);
    m->split[pair] = 1;
    return 1;
}

static int getregval(struct mach *m, const struct prog *p, int r, struct num *out) {
    int pair = regpair(r);
    if (!canreadreg(m,p,r)) return 0;
    if (splitreg(r) && !makesplit(m,pair)) return 0;
    *out = m->reg[r];
    return 1;
}

static int setregval(struct mach *m, const struct prog *p, int r, struct num d) {
    int pair = regpair(r);
    if (!canwritereg(m,p,r,d)) return 0;
    if (pair < 0) {
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
    m->reg[r] = d;
    m->reg[leftreg(pair)] = dint(0);
    return 1;
}

static int clearregval(struct mach *m, const struct prog *p, int r) {
    int pair = regpair(r);
    struct num zero = dint(0);
    if (pair < 0) return setregval(m,p,r,zero);
    if (!canwritereg(m,p,r,zero)) return 0;
    if (splitreg(r)) {
        if (!makesplit(m,pair)) return 0;
        m->reg[r] = zero;
        if (!p->used[rightreg(pair)]) m->split[pair] = 0;
        return 1;
    }
    m->reg[r] = zero;
    if (m->split[pair]) {
        if (m->reg[leftreg(pair)].n == 0 &&
            !p->used[rightreg(pair)] && !p->used[leftreg(pair)])
            m->split[pair] = 0;
        return 1;
    }
    m->reg[leftreg(pair)] = zero;
    return 1;
}

static int regid(const char *s) {
    static int full[] = {R_A,R_B,R_C,R_D,R_E,R_F,R_M,R_R};
    static int split[] = {R_BS,R_CS,R_DS,R_ES,R_FS};
    int isplit;
    char base;
    if (s[0] == '\0') return -1;
    base = (char)toupper((unsigned char)s[0]);
    isplit = s[1] == '/' && s[2] == '\0';
    if (s[1] == '\0') isplit = s[0] >= 'b' && s[0] <= 'f';
    else if (!isplit) return -1;
    if (base >= 'B' && base <= 'F') return isplit ? split[base-'B'] : full[base-'A'];
    if (isplit) return -1;
    if (base == 'A') return R_A;
    if (base == 'M') return R_M;
    if (base == 'R') return R_R;
    return -1;
}

static int setreg(const char *prefix, int *r) {
    int id;
    if (prefix[0] == '\0') {
        *r = R_M;
        return 1;
    }
    id = regid(prefix);
    if (id < 0) return 0;
    *r = id;
    return 1;
}

static int refpoint(const char *s) {
    size_t len = strlen(s);
    if (len == 2) return strchr("ABEF",s[0]) != NULL && routine(s[1]);
    if (len == 3) return strchr("ABEF",s[0]) != NULL && s[1] == '/' && routine(s[2]);
    return 0;
}

static int addlabel(struct prog *p, const char *name, int pc, int line) {
    if (p->nlabels == MAX_LABELS) {
        fprintf(stderr,"line %d: too many labels\n",line);
        return 0;
    }
    snprintf(p->labels[p->nlabels].name,MAX_NAME,"%s",name);
    p->labels[p->nlabels].pc = pc;
    p->nlabels++;
    return 1;
}

static int findlabel(const struct prog *p, const char *name) {
    int j;
    for (j = 0; j < p->nlabels; j++)
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
        fprintf(stderr,"line %d: too many instructions for P101 memory\n",in.line);
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
    size_t len = strlen(key), plen;
    int j;
    if (len == 0) return 0;
    for (j = 0; tail[j].tail; j++) {
        size_t tlen = strlen(tail[j].tail);
        if (len >= tlen && !strcmp(key+len-tlen,tail[j].tail)) {
            plen = len-tlen;
            if (plen >= MAX_NAME) return 0;
            memcpy(prefix,key,plen);
            prefix[plen] = '\0';
            snprintf(op,MAX_NAME,"%s",tail[j].op);
            return 1;
        }
    }
    if (!strchr("S<>+-x:#*",key[len-1])) return 0;
    plen = len-1;
    if (plen >= MAX_NAME) return 0;
    memcpy(prefix,key,plen);
    prefix[plen] = '\0';
    op[0] = key[len-1];
    op[1] = '\0';
    return 1;
}

/* Translate P101 jump chords into the labels they target. */
static int jumptarget(const char *key, int *conditional, char *target) {
    size_t len = strlen(key);
    *conditional = 0;
    if (len == 1 && routine(key[0])) {
        snprintf(target,MAX_NAME,"A%c",key[0]);
        return 1;
    }
    if (len == 2 && strchr("CDR",key[0]) && routine(key[1])) {
        snprintf(target,MAX_NAME,"%c%c",key[0] == 'C' ? 'B' : key[0] == 'D' ? 'E' : 'F',key[1]);
        return 1;
    }
    if (len == 2 && key[0] == '/' && routine(key[1])) {
        *conditional = 1;
        snprintf(target,MAX_NAME,"A/%c",key[1]);
        return 1;
    }
    if (len == 3 && strchr("CDR",key[0]) && key[1] == '/' && routine(key[2])) {
        *conditional = 1;
        snprintf(target,MAX_NAME,"%c/%c",key[0] == 'C' ? 'B' : key[0] == 'D' ? 'E' : 'F',key[2]);
        return 1;
    }
    return 0;
}

/* Parse interpreter-only metadata such as decimal wheel and initial values. */
static int directive(struct prog *p, char **argv, int argc, int line) {
    if (eq(argv[0],".decimals") || eq(argv[0],"decimals")) {
        int decimals;
        if (argc != 2) return 0;
        decimals = atoi(argv[1]);
        if (decimals < 0 || decimals > 15) return 0;
        p->decimals = decimals;
        return 1;
    }
    if (eq(argv[0],".set") || eq(argv[0],"set")) {
        int r;
        struct num value;
        if (argc != 3 || (r = regid(argv[1])) < 0 || !dparse(argv[2],&value)) return 0;
        if (!checkfit(r,value)) return 0;
        p->init[r] = value;
        p->hasinit[r] = 1;
        return 1;
    }
    fprintf(stderr,"line %d: unknown directive %s\n",line,argv[0]);
    return 0;
}

/* Parse one normalized key chord into an executable instruction. */
static int parsekey(struct prog *p, const char *key, int line) {
    struct ins in;
    int conditional;
    char prefix[MAX_NAME], op[MAX_NAME];
    memset(&in,0,sizeof(in));
    in.reg = R_M;
    in.line = line;
    snprintf(in.key,sizeof(in.key),"%s",key);
    if (refpoint(key)) {
        if (!addlabel(p,key,p->nins,line)) return 0;
        in.op = I_MARK;
        return addins(p,in);
    }
    if (jumptarget(key,&conditional,in.target)) {
        in.op = conditional ? I_IFPOS : I_GOTO;
        return addins(p,in);
    }
    if (!strcmp(key,"RS")) {
        in.op = I_RS;
        return addins(p,in);
    }
    if (!splitkey(key,prefix,op)) {
        fprintf(stderr,"line %d: cannot parse key chord %s\n",line,key);
        return 0;
    }
    if (!strcmp(prefix,"A/") && !strcmp(op,"<")) {
        in.op = I_LIT;
        return addins(p,in);
    }
    if (!strcmp(op,"S")) {
        in.op = prefix[0] ? I_LITDIG : I_INPUT;
    } else if (!strcmp(op,"<")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_STORE;
    } else if (!strcmp(op,">")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_LOAD;
    } else if (!strcmp(op,"><")) {
        if (!strcmp(prefix,"/")) in.op = I_FRAC;
        else if (!strcmp(prefix,"A")) in.op = I_ABS;
        else {
            if (!setreg(prefix,&in.reg)) return 0;
            in.op = I_SWAP;
        }
    } else if (!strcmp(op,"+")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_ADD;
    } else if (!strcmp(op,"-")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_SUB;
    } else if (!strcmp(op,"x")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_MUL;
    } else if (!strcmp(op,":")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_DIV;
    } else if (!strcmp(op,"sqrt")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_SQRT;
    } else if (!strcmp(op,"#")) {
        if (!strcmp(prefix,"/")) in.op = I_NL;
        else {
            if (!setreg(prefix,&in.reg)) return 0;
            in.op = I_PRINT;
        }
    } else if (!strcmp(op,"*")) {
        if (!setreg(prefix,&in.reg)) return 0;
        in.op = I_CLEAR;
    } else {
        return 0;
    }
    return addins(p,in);
}

static int words(char *s, char **argv, int max) {
    int argc = 0;
    char *t = strtok(s," \t\r\n");
    while(t) {
        if (argc == max) return -1;
        argv[argc++] = t;
        t = strtok(NULL," \t\r\n");
    }
    return argc;
}

/* Strip comments, join spaced chords, and dispatch directives/key chords. */
static int parseline(struct prog *p, char *line, int lineno) {
    char *argv[4], *comment;
    char key[MAX_NAME*2];
    int argc;
    comment = strchr(line,';');
    if (comment) *comment = '\0';
    line = trim(line);
    if (!line[0]) return 1;
    argc = words(line,argv,4);
    if (argc <= 0) {
        fprintf(stderr,"line %d: invalid syntax\n",lineno);
        return 0;
    }
    if (argv[0][0] == '.' || eq(argv[0],"decimals") || eq(argv[0],"set")) {
        if (!directive(p,argv,argc,lineno)) {
            fprintf(stderr,"line %d: invalid directive\n",lineno);
            return 0;
        }
        return 1;
    }
    if (argc == 1) return parsekey(p,argv[0],lineno);
    if (argc == 2) {
        snprintf(key,sizeof(key),"%s%s",argv[0],argv[1]);
        return parsekey(p,key,lineno);
    }
    if (argc == 3 && !strcmp(argv[1],"/")) {
        snprintf(key,sizeof(key),"%s/%s",argv[0],argv[2]);
        return parsekey(p,key,lineno);
    }
    fprintf(stderr,"line %d: expected one key chord\n",lineno);
    return 0;
}

/* Load and validate a source file before execution starts. */
static int load(const char *path, struct prog *p) {
    char line[MAX_LINE];
    int lineno = 0;
    FILE *fp = fopen(path,"r");
    if (!fp) {
        fprintf(stderr,"cannot open %s\n",path);
        return 0;
    }
    memset(p,0,sizeof(*p));
    while(fgets(line,sizeof(line),fp)) {
        if (!parseline(p,line,++lineno)) {
            fclose(fp);
            return 0;
        }
    }
    fclose(fp);
    return mark_instruction_regs(p);
}

/* Execution. */
static int readnum(struct mach *m, struct num *out) {
    char line[MAX_LINE], *s;
    while(fgets(line,sizeof(line),m->input)) {
        s = trim(line);
        if (!s[0]) continue;
        if (dparse(s,out)) return 1;
        fprintf(stderr,"invalid input: %s\n",s);
        return 0;
    }
    return 0;
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

/* Execute the original P101 constant-entry digit sequence into M. */
static int literal(struct mach *m, const struct prog *p) {
    char digits[32], point[32], number[80], prefix[MAX_NAME], op[MAX_NAME];
    struct num value;
    int pc = m->pc+1, len = 0, out = 0, neg = 0, done = 0, j, digit;
    while(pc < p->nins && len < (int)sizeof(digits)-1) {
        if (!splitkey(p->ins[pc].key,prefix,op)) return 0;
        digit = litdigit(op);
        if (digit < 0 || prefix[0] == '\0') return 0;
        digits[len] = (char)('0'+digit);
        point[len] = strchr(prefix,'/') != NULL;
        if (prefix[0] == 'D' || prefix[0] == 'E') {
            neg = prefix[0] == 'E';
            len++;
            done = 1;
            break;
        }
        if (prefix[0] != 'R' && prefix[0] != 'F') return 0;
        len++;
        pc++;
    }
    if (!done) return 0;
    if (neg) number[out++] = '-';
    for (j = len-1; j >= 0; j--) {
        number[out++] = digits[j];
        if (point[j]) number[out++] = '.';
    }
    number[out] = '\0';
    if (!dparse(number,&value) || !setregval(m,p,R_M,value)) return 0;
    m->pc = pc+1;
    return 1;
}

/* Shared add/subtract/multiply/divide path with decimal-wheel truncation. */
static int binary(struct mach *m, const struct prog *p, int r, int op) {
    struct num q, exact, src, rr, aa;
    if (!getregval(m,p,r,&src)) return 0;
    switch(op) {
    case I_ADD:
        if (!getregval(m,p,R_A,&exact)) return 0;
        rr = dadd(exact,src);
        aa = dscale(rr,m->decimals);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_R,rr) ||
            !setregval(m,p,R_A,aa)) return 0;
        return 1;
    case I_SUB:
        if (!getregval(m,p,R_A,&exact)) return 0;
        rr = dsub(exact,src);
        aa = dscale(rr,m->decimals);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_R,rr) ||
            !setregval(m,p,R_A,aa)) return 0;
        return 1;
    case I_MUL:
        if (!getregval(m,p,R_A,&exact)) return 0;
        rr = dmul(exact,src);
        aa = dscale(rr,m->decimals);
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_R,rr) ||
            !setregval(m,p,R_A,aa)) return 0;
        return 1;
    case I_DIV:
        if (!getregval(m,p,R_A,&exact)) return 0;
        if (!ddiv(exact,src,m->decimals,&q)) {
            fprintf(stderr,"division by zero at step %d\n",m->pc+1);
            return 0;
        }
        rr = dsub(exact,dmul(q,src));
        if (!setregval(m,p,R_M,src) || !setregval(m,p,R_A,q) ||
            !setregval(m,p,R_R,rr)) return 0;
        return 1;
    }
    return 0;
}

static int jump(struct mach *m, const struct prog *p, const char *target) {
    int pc = findlabel(p,target);
    if (pc < 0) {
        fprintf(stderr,"unknown label %s\n",target);
        return P101_ERR;
    }
    m->pc = pc;
    return P101_NEXT;
}

/* Execute the instruction at the current program counter. */
static int step(struct mach *m, const struct prog *p) {
    struct ins in = p->ins[m->pc];
    struct num tmp, tmp2, tmp3;
    char value[128];
    if (m->trace) fprintf(stderr,"%03d %s\n",m->pc+1,in.key);
    switch(in.op) {
    case I_MARK:
        m->pc++; return P101_NEXT;
    case I_INPUT:
        if (!readnum(m,&tmp)) return P101_STOP;
        if (!setregval(m,p,R_M,tmp)) return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_LIT:
        if (!literal(m,p)) {
            fprintf(stderr,"invalid literal sequence at step %d\n",m->pc+1);
            return P101_ERR;
        }
        return P101_NEXT;
    case I_LITDIG:
        fprintf(stderr,"literal digit outside A/< sequence at step %d\n",m->pc+1);
        return P101_ERR;
    case I_STORE:
        if (!getregval(m,p,R_M,&tmp) || !setregval(m,p,in.reg,tmp))
            return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_LOAD:
        if (!getregval(m,p,in.reg,&tmp) || !setregval(m,p,R_A,tmp))
            return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_SWAP:
        if (in.reg == R_R) {
            if (!getregval(m,p,R_R,&tmp) || !setregval(m,p,R_A,tmp))
                return P101_ERR;
        } else {
            if (!getregval(m,p,in.reg,&tmp) || !getregval(m,p,R_A,&tmp2) ||
                !setregval(m,p,in.reg,tmp2) || !setregval(m,p,R_A,tmp))
                return P101_ERR;
        }
        m->pc++; return P101_NEXT;
    case I_FRAC:
        if (!getregval(m,p,R_A,&tmp)) return P101_ERR;
        tmp = dfrac(tmp);
        if (!setregval(m,p,R_M,tmp)) return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_ABS:
        if (!getregval(m,p,R_A,&tmp)) return P101_ERR;
        if (tmp.n < 0) tmp.n = -tmp.n;
        if (!setregval(m,p,R_A,tmp)) return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_ADD: case I_SUB: case I_MUL: case I_DIV:
        if (!binary(m,p,in.reg,in.op)) return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_SQRT:
        if (!getregval(m,p,in.reg,&tmp)) return P101_ERR;
        tmp2 = dsqrt(tmp,m->decimals);
        tmp3 = dsub(tmp,dmul(tmp2,tmp2));
        if (!setregval(m,p,R_M,dmul(dint(2),tmp2)) ||
            !setregval(m,p,R_A,tmp2) || !setregval(m,p,R_R,tmp3))
            return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_CLEAR:
        if (!clearregval(m,p,in.reg)) return P101_ERR;
        m->pc++; return P101_NEXT;
    case I_PRINT:
        if (!getregval(m,p,in.reg,&tmp)) return P101_ERR;
        dstr(tmp,m->decimals,in.reg != R_R,value,sizeof(value));
        printf("%s\t%s\n",regname(in.reg),value);
        m->pc++; return P101_NEXT;
    case I_NL:
        putchar('\n'); m->pc++; return P101_NEXT;
    case I_GOTO:
        return jump(m,p,in.target);
    case I_IFPOS:
        if (!getregval(m,p,R_A,&tmp)) return P101_ERR;
        if (tmp.n <= 0) {
            m->pc++;
            return P101_NEXT;
        }
        return jump(m,p,in.target);
    case I_RS:
        if (!getregval(m,p,R_D,&tmp) || !getregval(m,p,R_R,&tmp2) ||
            !setregval(m,p,R_D,tmp2) || !setregval(m,p,R_R,tmp))
            return P101_ERR;
        m->pc++; return P101_NEXT;
    }
    return P101_ERR;
}

/* Initialize machine state and run until stop, error, or end of program. */
static int run(const struct prog *p, int start, FILE *input, int trace) {
    struct mach m;
    int j, rc;
    memset(&m,0,sizeof(m));
    m.decimals = p->decimals;
    m.input = input;
    m.trace = trace;
    for (j = 0; j < 5; j++)
        if (p->used[rightreg(j)] || p->used[leftreg(j)]) m.split[j] = 1;
    for (j = 0; j < R_COUNT; j++)
        if (p->hasinit[j] && !setregval(&m,p,j,p->init[j])) return 0;
    m.pc = entry(p,start);
    if (m.pc < 0) {
        fprintf(stderr,"no entry point for %c\n",start);
        return 0;
    }
    while(m.pc >= 0 && m.pc < p->nins) {
        rc = step(&m,p);
        if (rc == P101_STOP) return 1;
        if (rc == P101_ERR) return 0;
    }
    return 1;
}

static void usage(FILE *fp) {
    fprintf(fp,"Usage: p101 [--start V|W|Y|Z] [--input FILE] [--trace] program.p101\n");
}

int main(int argc, char **argv) {
    const char *path = NULL, *input_path = NULL;
    int j, start = 'V', trace = 0, ok;
    FILE *input = stdin;
    struct prog p;
    for (j = 1; j < argc; j++) {
        if (!strcmp(argv[j],"--start") && j+1 < argc) {
            start = argv[++j][0];
        } else if (!strcmp(argv[j],"--input") && j+1 < argc) {
            input_path = argv[++j];
        } else if (!strcmp(argv[j],"--trace")) {
            trace = 1;
        } else if (!strcmp(argv[j],"-h") || !strcmp(argv[j],"--help")) {
            usage(stdout);
            return 0;
        } else if (!path) {
            path = argv[j];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (!path || !strchr("VWYZ",start)) {
        usage(stderr);
        return 2;
    }
    if (input_path) {
        input = fopen(input_path,"r");
        if (!input) {
            fprintf(stderr,"cannot open %s\n",input_path);
            return 1;
        }
    }
    ok = load(path,&p) && run(&p,start,input,trace);
    if (input != stdin) fclose(input);
    return ok ? 0 : 1;
}
