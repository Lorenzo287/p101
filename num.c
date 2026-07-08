#include "num.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* === Decimal Numbers === */

static struct bigint izero(void) {
    struct bigint z;
    memset(&z,0,sizeof(z));
    return z;
}

static void inorm(struct bigint *a) {
    while(a->len > 0 && a->digit[a->len-1] == 0) a->len--;
    if (a->len == 0) a->sign = 0;
}

static bool izeroq(struct bigint a) {
    return a.sign == 0;
}

static bool ipos(struct bigint a) {
    return a.sign > 0;
}

static bool ineg(struct bigint a) {
    return a.sign < 0;
}

static struct bigint iabsval(struct bigint a) {
    if (a.sign < 0) a.sign = 1;
    return a;
}

static struct bigint inegval(struct bigint a) {
    if (a.sign) a.sign = -a.sign;
    return a;
}

static struct bigint ifromint(int n) {
    struct bigint a = izero();
    unsigned int v = n < 0 ? (unsigned int)(-(n+1)) + 1 : (unsigned int)n;
    if (v == 0) return a;
    a.sign = n < 0 ? -1 : 1;
    while(v) {
        a.digit[a.len++] = (unsigned char)(v%10);
        v /= 10;
    }
    return a;
}

static int icmpabs(struct bigint a, struct bigint b) {
    if (a.len != b.len) return a.len < b.len ? -1 : 1;
    for (int j = a.len-1; j >= 0; j--)
        if (a.digit[j] != b.digit[j])
            return a.digit[j] < b.digit[j] ? -1 : 1;
    return 0;
}

static int icmp(struct bigint a, struct bigint b) {
    if (a.sign != b.sign) return a.sign < b.sign ? -1 : 1;
    if (a.sign == 0) return 0;
    int cmp = icmpabs(a,b);
    return a.sign > 0 ? cmp : -cmp;
}

static int iaddabs(struct bigint a, struct bigint b, struct bigint *out) {
    struct bigint r = izero();
    int carry = 0;
    int len = a.len > b.len ? a.len : b.len;
    for (int j = 0; j < len || carry; j++) {
        if (j == BIG_DIGITS) return 0;
        int sum = carry;
        if (j < a.len) sum += a.digit[j];
        if (j < b.len) sum += b.digit[j];
        r.digit[j] = (unsigned char)(sum%10);
        carry = sum/10;
        r.len = j+1;
    }
    r.sign = r.len ? 1 : 0;
    *out = r;
    return 1;
}

static void isubabs(struct bigint a, struct bigint b, struct bigint *out) {
    struct bigint r = izero();
    int borrow = 0;
    for (int j = 0; j < a.len; j++) {
        int diff = a.digit[j] - borrow - (j < b.len ? b.digit[j] : 0);
        if (diff < 0) {
            diff += 10;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r.digit[j] = (unsigned char)diff;
        r.len = j+1;
    }
    r.sign = 1;
    inorm(&r);
    *out = r;
}

static int iadd(struct bigint a, struct bigint b, struct bigint *out) {
    if (a.sign == 0) {
        *out = b;
        return 1;
    }
    if (b.sign == 0) {
        *out = a;
        return 1;
    }
    if (a.sign == b.sign) {
        if (!iaddabs(iabsval(a),iabsval(b),out)) return 0;
        out->sign = a.sign;
        return 1;
    }
    int cmp = icmpabs(a,b);
    if (cmp == 0) {
        *out = izero();
        return 1;
    }
    if (cmp > 0) {
        isubabs(iabsval(a),iabsval(b),out);
        out->sign = a.sign;
    } else {
        isubabs(iabsval(b),iabsval(a),out);
        out->sign = b.sign;
    }
    return 1;
}

static int isub(struct bigint a, struct bigint b, struct bigint *out) {
    return iadd(a,inegval(b),out);
}

static int imulsmall(struct bigint a, int m, struct bigint *out) {
    if (a.sign == 0 || m == 0) {
        *out = izero();
        return 1;
    }
    struct bigint r = izero();
    int carry = 0;
    for (int j = 0; j < a.len || carry; j++) {
        if (j == BIG_DIGITS) return 0;
        int prod = carry + (j < a.len ? a.digit[j] * m : 0);
        r.digit[j] = (unsigned char)(prod%10);
        carry = prod/10;
        r.len = j+1;
    }
    r.sign = a.sign;
    inorm(&r);
    *out = r;
    return 1;
}

static int iaddsmall(struct bigint a, int v, struct bigint *out) {
    return iadd(a,ifromint(v),out);
}

static int imul(struct bigint a, struct bigint b, struct bigint *out) {
    if (a.sign == 0 || b.sign == 0) {
        *out = izero();
        return 1;
    }
    struct bigint r = izero();
    for (int j = 0; j < a.len; j++) {
        int carry = 0;
        for (int k = 0; k < b.len || carry; k++) {
            int pos = j+k;
            if (pos == BIG_DIGITS) return 0;
            int sum = r.digit[pos] + carry;
            if (k < b.len) sum += a.digit[j] * b.digit[k];
            r.digit[pos] = (unsigned char)(sum%10);
            carry = sum/10;
            if (pos+1 > r.len) r.len = pos+1;
        }
    }
    r.sign = a.sign * b.sign;
    inorm(&r);
    *out = r;
    return 1;
}

static int imul10n(struct bigint a, int n, struct bigint *out) {
    if (a.sign == 0) {
        *out = a;
        return 1;
    }
    if (a.len+n > BIG_DIGITS) return 0;
    struct bigint r = izero();
    r.sign = a.sign;
    r.len = a.len+n;
    for (int j = 0; j < a.len; j++) r.digit[j+n] = a.digit[j];
    *out = r;
    return 1;
}

static void idiv10n(struct bigint *a, int n) {
    if (n <= 0 || a->sign == 0) return;
    if (n >= a->len) {
        *a = izero();
        return;
    }
    for (int j = 0; j < a->len-n; j++) a->digit[j] = a->digit[j+n];
    memset(a->digit+a->len-n,0,(size_t)n);
    a->len -= n;
    inorm(a);
}

static int idivsmall(struct bigint a, int d, struct bigint *out) {
    struct bigint r = izero();
    int carry = 0;
    r.sign = a.sign;
    r.len = a.len;
    for (int j = a.len-1; j >= 0; j--) {
        int cur = carry*10 + a.digit[j];
        r.digit[j] = (unsigned char)(cur/d);
        carry = cur%d;
    }
    inorm(&r);
    *out = r;
    return 1;
}

static int idivabs(struct bigint a, struct bigint b, struct bigint *out) {
    if (b.sign == 0) return 0;
    a = iabsval(a);
    b = iabsval(b);
    if (icmpabs(a,b) < 0) {
        *out = izero();
        return 1;
    }
    struct bigint q = izero(), rem = izero();
    q.len = a.len;
    q.sign = 1;
    for (int j = a.len-1; j >= 0; j--) {
        if (!imulsmall(rem,10,&rem) || !iaddsmall(rem,a.digit[j],&rem))
            return 0;
        while(icmpabs(rem,b) >= 0) {
            isubabs(rem,b,&rem);
            q.digit[j]++;
        }
    }
    inorm(&q);
    *out = q;
    return 1;
}

static int idiv(struct bigint a, struct bigint b, struct bigint *out) {
    if (b.sign == 0) return 0;
    int sign = a.sign * b.sign;
    if (!idivabs(a,b,out)) return 0;
    if (out->sign) out->sign = sign;
    return 1;
}

static int isqrt(struct bigint n, struct bigint *out) {
    if (n.sign <= 0) {
        *out = izero();
        return 1;
    }
    struct bigint lo = izero(), hi = ifromint(1), one = ifromint(1);
    while(1) {
        struct bigint square;
        if (!imul(hi,hi,&square) || icmp(square,n) > 0) break;
        lo = hi;
        if (!imulsmall(hi,2,&hi)) break;
    }
    while(1) {
        struct bigint diff;
        if (!isub(hi,lo,&diff)) return 0;
        if (icmp(diff,one) <= 0) break;
        struct bigint sum, mid, square;
        if (!iadd(lo,hi,&sum) || !idivsmall(sum,2,&mid)) return 0;
        if (!imul(mid,mid,&square) || icmp(square,n) > 0) hi = mid;
        else lo = mid;
    }
    *out = lo;
    return 1;
}

static struct num dnew(struct bigint n, int scale) {
    struct num d = {n,scale};
    if (izeroq(d.n)) {
        d.scale = 0;
        return d;
    }
    while(d.scale > 0 && d.n.digit[0] == 0) {
        idiv10n(&d.n,1);
        d.scale--;
    }
    return d;
}

static int ndigits(struct bigint n) {
    return n.len ? n.len : 1;
}

struct num dint(int n) {
    return dnew(ifromint(n),0);
}

struct num dabs(struct num d) {
    d.n = iabsval(d.n);
    return d;
}

bool dzerop(struct num d) {
    return izeroq(d.n);
}

bool dpos(struct num d) {
    return ipos(d.n);
}

int dstoredigits(struct num d) {
    int digits = ndigits(d.n);
    return d.scale >= digits ? d.scale+1 : digits;
}

int dscale(struct num d, int scale, struct num *out) {
    if (d.scale < scale) {
        if (!imul10n(d.n,scale-d.scale,&d.n)) return 0;
    } else if (d.scale > scale) {
        idiv10n(&d.n,d.scale-scale);
    }
    *out = dnew(d.n,scale);
    return 1;
}

int dadd(struct num a, struct num b, struct num *out) {
    int scale = a.scale > b.scale ? a.scale : b.scale;
    struct bigint an, bn, sum;
    if (!imul10n(a.n,scale-a.scale,&an) ||
        !imul10n(b.n,scale-b.scale,&bn) ||
        !iadd(an,bn,&sum)) return 0;
    *out = dnew(sum,scale);
    return 1;
}

int dsub(struct num a, struct num b, struct num *out) {
    int scale = a.scale > b.scale ? a.scale : b.scale;
    struct bigint an, bn, diff;
    if (!imul10n(a.n,scale-a.scale,&an) ||
        !imul10n(b.n,scale-b.scale,&bn) ||
        !isub(an,bn,&diff)) return 0;
    *out = dnew(diff,scale);
    return 1;
}

int dmul(struct num a, struct num b, struct num *out) {
    struct bigint product;
    if (!imul(a.n,b.n,&product)) return 0;
    *out = dnew(product,a.scale+b.scale);
    return 1;
}

int ddiv(struct num a, struct num b, int scale, struct num *q) {
    struct bigint num = a.n, den = b.n;
    if (izeroq(b.n)) return 0;
    int exp = b.scale + scale - a.scale;
    if (exp >= 0) {
        if (!imul10n(num,exp,&num)) return 0;
    } else {
        if (!imul10n(den,-exp,&den)) return 0;
    }
    struct bigint quotient;
    if (!idiv(num,den,&quotient)) return 0;
    *q = dnew(quotient,scale);
    return 1;
}

int dsqrt(struct num d, int scale, struct num *out) {
    struct bigint n = iabsval(d.n);
    int exp = 2*scale - d.scale;
    if (exp >= 0) {
        if (!imul10n(n,exp,&n)) return 0;
    } else {
        idiv10n(&n,-exp);
    }
    struct bigint root;
    if (!isqrt(n,&root)) return 0;
    *out = dnew(root,scale);
    return 1;
}

int dfrac(struct num d, struct num *out) {
    if (d.scale == 0) {
        *out = dint(0);
        return 1;
    }
    struct bigint whole = d.n, shifted, frac;
    idiv10n(&whole,d.scale);
    if (!imul10n(whole,d.scale,&shifted) || !isub(d.n,shifted,&frac))
        return 0;
    *out = dnew(frac,d.scale);
    return 1;
}

/* Parse user/input decimal text into the normalized coefficient/scale form. */
int dparse(const char *s, struct num *out) {
    bool neg = false, dot = false, any = false;
    int scale = 0;
    struct bigint n = izero();
    while(isspace((unsigned char)*s)) s++;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    while(*s) {
        if (isdigit((unsigned char)*s)) {
            any = true;
            if (!imulsmall(n,10,&n) || !iaddsmall(n,*s-'0',&n)) return 0;
            if (dot) scale++;
        } else if ((*s == '.' || *s == ',') && !dot) {
            dot = true;
        } else if (isspace((unsigned char)*s)) {
            break;
        } else {
            return 0;
        }
        s++;
    }
    while(isspace((unsigned char)*s)) s++;
    if (*s) return 0;
    if (!any) return 0;
    if (neg) n = inegval(n);
    *out = dnew(n,scale);
    return 1;
}

static void istrabs(struct bigint v, char *buf, size_t size) {
    if (size == 0) return;
    v = iabsval(v);
    if (v.sign == 0) {
        snprintf(buf,size,"0");
        return;
    }
    size_t pos = 0;
    for (int j = v.len-1; j >= 0 && pos+1 < size; j--)
        buf[pos++] = (char)('0'+v.digit[j]);
    buf[pos] = '\0';
}

/* Convert a decimal value to the P101 comma-decimal print format. */
int dstr(struct num d, int scale, bool fixed, char *buf, size_t size) {
    int print_scale = d.scale;
    struct bigint v = d.n;
    if (fixed && scale < 0) return 0;
    if (fixed) {
        print_scale = scale;
        if (d.scale < scale) {
            if (!imul10n(v,scale-d.scale,&v)) return 0;
        } else if (d.scale > scale) {
            idiv10n(&v,d.scale-scale);
        }
    }
    if (print_scale < 0 || print_scale > BIG_DIGITS) return 0;

    char tmp[BIG_DIGITS+8];
    int pos = 0;
    if (ineg(v)) {
        tmp[pos++] = '-';
    }

    char digits[BIG_DIGITS+1];
    istrabs(v,digits,sizeof(digits));
    int len = (int)strlen(digits);
    if (print_scale == 0) {
        for (int j = 0; j < len; j++) tmp[pos++] = digits[j];
    } else {
        int intlen = len - print_scale;
        if (intlen <= 0) {
            tmp[pos++] = '0';
            tmp[pos++] = ',';
            for (int j = 0; j < -intlen; j++) tmp[pos++] = '0';
            for (int j = 0; j < len; j++) tmp[pos++] = digits[j];
        } else {
            for (int j = 0; j < intlen; j++) tmp[pos++] = digits[j];
            tmp[pos++] = ',';
            for (int j = intlen; j < len; j++) tmp[pos++] = digits[j];
        }
    }
    tmp[pos] = '\0';
    if (!fixed) {
        while(pos > 0 && tmp[pos-1] == '0') tmp[--pos] = '\0';
        if (pos > 0 && tmp[pos-1] == ',') tmp[--pos] = '\0';
    }
    snprintf(buf,size,"%s",tmp);
    return 1;
}
