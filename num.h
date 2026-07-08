#ifndef P101_NUM_H
#define P101_NUM_H

#include <stdbool.h>
#include <stddef.h>

#define BIG_DIGITS 128  // decimal digits for coefficients and intermediates

struct bigint {
    signed char sign;  // -1, 0, +1
    int len;
    unsigned char digit[BIG_DIGITS];  // lsd first
};

struct num {
    struct bigint n;  // integer coefficient
    int scale;  // decimal places
};

struct num dint(int n);
struct num dabs(struct num d);
bool dzerop(struct num d);
bool dpos(struct num d);
int dstoredigits(struct num d);
int dscale(struct num d, int scale, struct num *out);
int dadd(struct num a, struct num b, struct num *out);
int dsub(struct num a, struct num b, struct num *out);
int dmul(struct num a, struct num b, struct num *out);
int ddiv(struct num a, struct num b, int scale, struct num *q);
int dsqrt(struct num d, int scale, struct num *out);
int dfrac(struct num d, struct num *out);
int dparse(const char *s, struct num *out);
int dstr(struct num d, int scale, bool fixed, char *buf, size_t size);

#endif
