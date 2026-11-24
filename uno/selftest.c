/* selftest.c -- the same arithmetic run on the simulated ATmega328P and on this computer; the printed checksums must
   be equal. It exercises what the solver leans on: 8/16/32/64-bit integer arithmetic (add, subtract, multiply,
   divide, remainder, shifts, comparisons, overflow checks) and single-precision floating point. */
#include <stdint.h>
#ifdef __AVR__
#include <avr/io.h>
static void put(char c) { while (!(UCSR0A & _BV(UDRE0))) ; UDR0 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif
static void hex32(uint32_t v) { for (int8_t i = 28; i >= 0; i -= 4) put("0123456789abcdef"[(v >> i) & 15]); }
static void line(const char *name, uint32_t v) { while (*name) put(*name++); put(' '); hex32(v); put('\n'); }

static uint32_t seed = 12345;
static uint32_t rnd(void) { seed = seed * 1664525ul + 1013904223ul; return seed; }
static uint32_t mix(uint32_t h, uint32_t v) { h ^= v; h *= 16777619ul; return h ^ (h >> 13); }

int main(void)
{
#ifdef __AVR__
    UBRR0H = 0; UBRR0L = 16; UCSR0A = _BV(U2X0); UCSR0B = _BV(TXEN0); UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
#endif
    uint32_t h8 = 1, h16 = 1, h32 = 1, h64 = 1, hf = 1, hov = 1;
    for (uint16_t i = 0; i < 4000; i++) {
        uint32_t a = rnd(), b = rnd();
        int8_t a8 = (int8_t)a, b8 = (int8_t)b; uint8_t ua8 = (uint8_t)(a >> 8), ub8 = (uint8_t)(b >> 8);
        h8 = mix(h8, (uint8_t)(a8 + b8)); h8 = mix(h8, (uint8_t)(a8 - b8)); h8 = mix(h8, (uint16_t)((int16_t)a8 * b8)); h8 = mix(h8, (uint16_t)((uint16_t)ua8 * ub8));
        h8 = mix(h8, (uint8_t)(a8 >> (b & 7))); h8 = mix(h8, (uint8_t)(ua8 >> (b & 7))); h8 = mix(h8, (a8 < b8) | (ua8 < ub8) << 1 | (a8 == b8) << 2);
        if (b8) { h8 = mix(h8, (uint8_t)(a8 / b8)); h8 = mix(h8, (uint8_t)(a8 % b8)); }
        if (ub8) { h8 = mix(h8, ua8 / ub8); h8 = mix(h8, ua8 % ub8); }
        int16_t a16 = (int16_t)a, b16 = (int16_t)(b >> 3); uint16_t ua16 = (uint16_t)(a >> 16), ub16 = (uint16_t)(b >> 13);
        h16 = mix(h16, (uint16_t)(a16 + b16)); h16 = mix(h16, (uint16_t)(a16 - b16)); h16 = mix(h16, (uint32_t)((int32_t)a16 * b16)); h16 = mix(h16, (uint32_t)ua16 * ub16);
        h16 = mix(h16, (uint16_t)(a16 >> (b & 15))); h16 = mix(h16, (uint16_t)(ua16 << (b & 15))); h16 = mix(h16, (a16 < b16) | (ua16 < ub16) << 1);
        if (b16) { h16 = mix(h16, (uint16_t)(a16 / b16)); h16 = mix(h16, (uint16_t)(a16 % b16)); }
        if (ub16) { h16 = mix(h16, ua16 / ub16); h16 = mix(h16, ua16 % ub16); }
        int32_t sa = (int32_t)a >> (i & 15), sb = (int32_t)b >> ((i >> 4) & 15);
        h32 = mix(h32, (uint32_t)sa + (uint32_t)sb); h32 = mix(h32, (uint32_t)sa - (uint32_t)sb); h32 = mix(h32, (uint32_t)sa * (uint32_t)sb);
        h32 = mix(h32, (uint32_t)(sa >> (b & 31))); h32 = mix(h32, a << (b & 31)); h32 = mix(h32, a >> (b & 31)); h32 = mix(h32, (sa < sb) | (a < b) << 1);
        h32 = mix(h32, sa & sb); h32 = mix(h32, sa | sb); h32 = mix(h32, sa ^ sb); h32 = mix(h32, (uint32_t)-sa);
        if (sb && !(sa == INT32_MIN && sb == -1)) { h32 = mix(h32, (uint32_t)(sa / sb)); h32 = mix(h32, (uint32_t)(sa % sb)); }
        if (b) { h32 = mix(h32, a / b); h32 = mix(h32, a % b); }
        int64_t wa = (int64_t)sa * sb, wb = ((int64_t)(int32_t)rnd() << 20) ^ b;
        h64 = mix(h64, (uint32_t)wa); h64 = mix(h64, (uint32_t)(wa >> 32));
        uint64_t s64 = (uint64_t)wa + (uint64_t)wb, m64 = (uint64_t)wa * (uint64_t)wb, x64 = (uint64_t)wa - (uint64_t)wb;
        h64 = mix(h64, (uint32_t)s64); h64 = mix(h64, (uint32_t)(s64 >> 32)); h64 = mix(h64, (uint32_t)m64); h64 = mix(h64, (uint32_t)(m64 >> 32));
        h64 = mix(h64, (uint32_t)x64); h64 = mix(h64, (uint32_t)(x64 >> 32)); h64 = mix(h64, (wa < wb) | (wa == wb) << 1 | ((uint64_t)wa < (uint64_t)wb) << 2);
        h64 = mix(h64, (uint32_t)((uint64_t)wb >> (a & 63))); h64 = mix(h64, (uint32_t)(((uint64_t)wb << (a & 63)) >> 32)); h64 = mix(h64, (uint32_t)(wb >> (b & 63)));
        if (wb && !(wa == INT64_MIN && wb == -1)) { int64_t q = wa / wb, r = wa % wb; h64 = mix(h64, (uint32_t)q); h64 = mix(h64, (uint32_t)(q >> 32)); h64 = mix(h64, (uint32_t)r); h64 = mix(h64, (uint32_t)(r >> 32)); }
        int32_t o32; int64_t o64;
        hov = mix(hov, (uint32_t)__builtin_mul_overflow(sa, sb, &o32)); hov = mix(hov, (uint32_t)o32);
        hov = mix(hov, (uint32_t)__builtin_add_overflow(wa, wb, &o64)); hov = mix(hov, (uint32_t)(o64 >> 32));
        hov = mix(hov, (uint32_t)__builtin_popcountl(a)); hov = mix(hov, a ? (uint32_t)__builtin_ctzl(a) : 99);
        float fa = (float)(sa >> 8) * 0.03125f, fb = (float)((sb >> 10) | 1) * 0.0625f;
        union { float f; uint32_t u; } u;
        u.f = fa + fb; hf = mix(hf, u.u); u.f = fa - fb; hf = mix(hf, u.u); u.f = fa * fb; hf = mix(hf, u.u); u.f = fa / fb; hf = mix(hf, u.u);
        u.f = (float)sa; hf = mix(hf, u.u); u.f = (float)a; hf = mix(hf, u.u); hf = mix(hf, (uint32_t)(int32_t)(fa * 3.5f)); hf = mix(hf, (fa < fb) | (fa == fb) << 1 | (fa > 0) << 2);
        u.f = fa * fb + fa / (fb + 100.0f) - fb * 0.3f; hf = mix(hf, u.u);
    }
    line("int8 ", h8); line("int16", h16); line("int32", h32); line("int64", h64); line("ovf  ", hov); line("float", hf);
#ifdef __AVR__
    GPIOR1 = 0xFF; for (;;) ;
#endif
    return 0;
}
