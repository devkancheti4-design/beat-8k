/* avrsim.c -- a small simulator of the ATmega328P's processor (the Arduino Uno's chip), enough to run a bare-metal
   program that only uses the serial port: every instruction avr-gcc emits for this chip, the status flags, 2 KB of
   RAM, and the datasheet's cycle count for each instruction.

     avrsim program.bin [max-seconds-of-chip-time]

   It prints what the program sends to the serial port, and for the program's marks (GPIOR0 written non-zero at the
   start of a problem, zero at its end) the chip time between them at 16 MHz. At the end: the total chip time and the
   deepest the stack went. A program stops it by writing 0xFF to GPIOR1. Anything it cannot run (an unknown
   instruction, an address outside the chip's memory) stops it with an error. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

enum { NOP, MOVW, MULS, MULSU, FMUL, FMULS, FMULSU, CPC, SBC, ADD, CPSE, CP, SUB, ADC, AND, EOR, OR, MOV, CPI, SBCI, SUBI, ORI, ANDI, LDI,
       LDD_Y, LDD_Z, STD_Y, STD_Z, LDS, STS, LD_ZP, LD_MZ, LPM_Z, LPM_ZP, LD_YP, LD_MY, LD_X, LD_XP, LD_MX, POP,
       ST_ZP, ST_MZ, ST_YP, ST_MY, ST_X, ST_XP, ST_MX, PUSH, COM, NEG, SWAP, INC, ASR, LSR, ROR, DEC, BSET, BCLR,
       RET, RETI, LPM0, IJMP, ICALL, JMP, CALL, ADIW, SBIW, CBI, SBIC, SBI, SBIS, MUL, IN, OUT, RJMP, RCALL, BRBS, BRBC, BLD, BST, SBRC, SBRS, BAD };

typedef struct { uint8_t op, d, r, len; int32_t k; } insn;

#define FLASH_WORDS 0x4000
#define RAMEND 0x8FF
static uint16_t flash[FLASH_WORDS];
static insn dec[FLASH_WORDS];
static uint8_t data[RAMEND + 1];
static int fC, fZ, fN, fV, fS, fH, fT, fI;
static uint32_t sp = RAMEND, minsp = RAMEND;
static uint64_t cyc, mark_cyc;
static int mark, stopped, phase;
static uint64_t phase_cyc, phase_tot[16];   /* chip time by phase: a traced program writes the phase to GPIOR2 */
static uint64_t prof[FLASH_WORDS];        /* cycles spent at each word of the program (with AVRSIM_PROFILE=file) */
static uint32_t mark_minsp;

static void die(const char *m, uint32_t pc) { fprintf(stderr, "\nSIMULATOR STOPPED: %s at word %u (0x%04x)\n", m, pc, pc < FLASH_WORDS ? flash[pc] : 0); exit(2); }

static insn decode(uint16_t w, uint16_t next)
{
    insn i; i.op = BAD; i.d = (w >> 4) & 0x1F; i.r = (w & 0xF) | ((w >> 5) & 0x10); i.k = 0; i.len = 1;
    if (w == 0) { i.op = NOP; return i; }
    switch (w & 0xFC00) {
    case 0x0400: i.op = CPC; return i;   case 0x0800: i.op = SBC; return i;   case 0x0C00: i.op = ADD; return i;
    case 0x1000: i.op = CPSE; return i;  case 0x1400: i.op = CP; return i;    case 0x1800: i.op = SUB; return i;
    case 0x1C00: i.op = ADC; return i;   case 0x2000: i.op = AND; return i;   case 0x2400: i.op = EOR; return i;
    case 0x2800: i.op = OR; return i;    case 0x2C00: i.op = MOV; return i;   case 0x9C00: i.op = MUL; return i;
    }
    if ((w & 0xFF00) == 0x0100) { i.op = MOVW; i.d = ((w >> 4) & 0xF) * 2; i.r = (w & 0xF) * 2; return i; }
    if ((w & 0xFF00) == 0x0200) { i.op = MULS; i.d = 16 + ((w >> 4) & 0xF); i.r = 16 + (w & 0xF); return i; }
    if ((w & 0xFF00) == 0x0300) {
        i.d = 16 + ((w >> 4) & 7); i.r = 16 + (w & 7);
        i.op = (w & 0x88) == 0x00 ? MULSU : (w & 0x88) == 0x08 ? FMUL : (w & 0x88) == 0x80 ? FMULS : FMULSU; return i;
    }
    switch (w & 0xF000) {
    case 0x3000: i.op = CPI; break;  case 0x4000: i.op = SBCI; break; case 0x5000: i.op = SUBI; break;
    case 0x6000: i.op = ORI; break;  case 0x7000: i.op = ANDI; break; case 0xE000: i.op = LDI; break;
    case 0xC000: i.op = RJMP; i.k = (int16_t)(w << 4) >> 4; return i;
    case 0xD000: i.op = RCALL; i.k = (int16_t)(w << 4) >> 4; return i;
    }
    if (i.op != BAD) { i.d = 16 + ((w >> 4) & 0xF); i.k = ((w >> 4) & 0xF0) | (w & 0xF); return i; }
    if ((w & 0xD000) == 0x8000) {
        i.k = (w & 7) | ((w >> 7) & 0x18) | ((w >> 8) & 0x20);
        i.op = (w & 0x0200) ? ((w & 8) ? STD_Y : STD_Z) : ((w & 8) ? LDD_Y : LDD_Z); return i;
    }
    if ((w & 0xFE00) == 0x9000 || (w & 0xFE00) == 0x9200) {
        static const uint8_t L[16] = {LDS, LD_ZP, LD_MZ, BAD, LPM_Z, LPM_ZP, BAD, BAD, BAD, LD_YP, LD_MY, BAD, LD_X, LD_XP, LD_MX, POP};
        static const uint8_t S[16] = {STS, ST_ZP, ST_MZ, BAD, BAD, BAD, BAD, BAD, BAD, ST_YP, ST_MY, BAD, ST_X, ST_XP, ST_MX, PUSH};
        i.op = (w & 0x0200) ? S[w & 0xF] : L[w & 0xF];
        if (i.op == LDS || i.op == STS) { i.k = next; i.len = 2; }
        return i;
    }
    if ((w & 0xFE00) == 0x9400) {
        if ((w & 0xFF8F) == 0x9408) { i.op = BSET; i.d = (w >> 4) & 7; return i; }
        if ((w & 0xFF8F) == 0x9488) { i.op = BCLR; i.d = (w >> 4) & 7; return i; }
        if (w == 0x9508) { i.op = RET; return i; }
        if (w == 0x9518) { i.op = RETI; return i; }
        if (w == 0x95C8) { i.op = LPM0; return i; }
        if (w == 0x9409) { i.op = IJMP; return i; }
        if (w == 0x9509) { i.op = ICALL; return i; }
        if (w == 0x9588 || w == 0x95A8) { i.op = NOP; return i; }             /* SLEEP, WDR: nothing to do here */
        if ((w & 0xFE0E) == 0x940C) { i.op = JMP; i.k = ((int32_t)(((w & 0x1F0) >> 3) | (w & 1)) << 16) | next; i.len = 2; return i; }
        if ((w & 0xFE0E) == 0x940E) { i.op = CALL; i.k = ((int32_t)(((w & 0x1F0) >> 3) | (w & 1)) << 16) | next; i.len = 2; return i; }
        switch (w & 0xF) {
        case 0x0: i.op = COM; break; case 0x1: i.op = NEG; break; case 0x2: i.op = SWAP; break; case 0x3: i.op = INC; break;
        case 0x5: i.op = ASR; break; case 0x6: i.op = LSR; break; case 0x7: i.op = ROR; break; case 0xA: i.op = DEC; break;
        }
        return i;
    }
    if ((w & 0xFE00) == 0x9600) { i.op = (w & 0x0100) ? SBIW : ADIW; i.d = 24 + ((w >> 4) & 3) * 2; i.k = (w & 0xF) | ((w >> 2) & 0x30); return i; }
    if ((w & 0xFC00) == 0x9800) {
        static const uint8_t B[4] = {CBI, SBIC, SBI, SBIS};
        i.op = B[(w >> 8) & 3]; i.k = 0x20 + ((w >> 3) & 0x1F); i.d = w & 7; return i;
    }
    if ((w & 0xF000) == 0xB000) { i.op = (w & 0x0800) ? OUT : IN; i.k = 0x20 + ((w & 0xF) | ((w >> 5) & 0x30)); return i; }
    if ((w & 0xF800) == 0xF000) { i.op = (w & 0x0400) ? BRBC : BRBS; i.d = w & 7; i.k = (int8_t)((w >> 3) << 1) >> 1; return i; }
    if ((w & 0xF808) == 0xF800) {
        static const uint8_t B[4] = {BLD, BST, SBRC, SBRS};
        i.op = B[(w >> 9) & 3]; i.r = w & 7; return i;
    }
    return i;
}

static uint8_t sreg_get(void) { return (uint8_t)(fC | fZ << 1 | fN << 2 | fV << 3 | fS << 4 | fH << 5 | fT << 6 | fI << 7); }
static void sreg_set(uint8_t v) { fC = v & 1; fZ = v >> 1 & 1; fN = v >> 2 & 1; fV = v >> 3 & 1; fS = v >> 4 & 1; fH = v >> 5 & 1; fT = v >> 6 & 1; fI = v >> 7 & 1; }

static uint8_t rd(uint32_t a, uint32_t pc)
{
    if (a > RAMEND) die("read outside the chip's memory", pc);
    if (a >= 0x100) return data[a];
    if (a == 0x5D) return (uint8_t)sp;
    if (a == 0x5E) return (uint8_t)(sp >> 8);
    if (a == 0x5F) return sreg_get();
    if (a == 0xC0) return (uint8_t)(data[a] | 0x20);                         /* the serial port is always ready to send */
    return data[a];
}
static void wr(uint32_t a, uint8_t v, uint32_t pc)
{
    if (a > RAMEND) die("write outside the chip's memory", pc);
    if (a >= 0x100) { data[a] = v; return; }
    if (a == 0x5D) { sp = (sp & 0xFF00) | v; if (sp < minsp) minsp = sp; return; }   /* the low byte is written last */
    if (a == 0x5E) { sp = (sp & 0x00FF) | (uint32_t)v << 8; return; }
    if (a == 0x5F) { sreg_set(v); return; }
    data[a] = v;
    if (a == 0xC6) { putchar(v); if (v == '\n') fflush(stdout); }
    else if (a == 0x3E) {
        if (v) { mark = v; mark_cyc = cyc; mark_minsp = minsp; minsp = sp; }
        else if (mark) {
            printf("    [problem %d: %.1f s of chip time at 16 MHz, %llu cycles; stack down to 0x%03X: %u bytes below the top of RAM]\n",
                   mark, (double)(cyc - mark_cyc) / 16e6, (unsigned long long)(cyc - mark_cyc), minsp, RAMEND - minsp);
            if (mark_minsp < minsp) minsp = mark_minsp;
            mark = 0;
        }
    } else if (a == 0x4A && v == 0xFF) stopped = 1;
    else if (a == 0x4B) { phase_tot[phase & 15] += cyc - phase_cyc; phase_cyc = cyc; phase = v; }
}

#define R data
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: avrsim program.bin [max-seconds]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    static uint8_t img[FLASH_WORDS * 2]; memset(img, 0xFF, sizeof img);
    size_t n = fread(img, 1, sizeof img, f); fclose(f);
    for (int i = 0; i < FLASH_WORDS; i++) flash[i] = (uint16_t)(img[2 * i] | img[2 * i + 1] << 8);
    for (int i = 0; i < FLASH_WORDS; i++) dec[i] = decode(flash[i], flash[(i + 1) % FLASH_WORDS]);
    uint64_t maxcyc = argc > 2 ? (uint64_t)(atof(argv[2]) * 16e6) : ~0ULL;
    uint64_t prof_from = getenv("AVRSIM_PROFILE_FROM") ? (uint64_t)(atof(getenv("AVRSIM_PROFILE_FROM")) * 16e6) : 0;
    uint32_t pc = 0;
    fprintf(stderr, "program: %zu bytes of flash\n", n);
#define SKIP(cond) do { if (cond) { cyc += 1 + dec[(pc + 1) & (FLASH_WORDS - 1)].len; pc += 1 + dec[(pc + 1) & (FLASH_WORDS - 1)].len; } else { cyc++; pc++; } } while (0)
#define NZS(res) do { fN = (res) >> 7 & 1; fZ = (uint8_t)(res) == 0; fS = fN ^ fV; } while (0)
#define ADDF(a, b, res) do { uint8_t c_ = (uint8_t)(((a) & (b)) | ((b) & ~(res)) | (~(res) & (a))); fH = c_ >> 3 & 1; fC = c_ >> 7 & 1; \
    fV = (uint8_t)(((a) & (b) & ~(res)) | (~(a) & ~(b) & (res))) >> 7 & 1; NZS(res); } while (0)
#define SUBF(a, b, res, keepz) do { uint8_t c_ = (uint8_t)((~(a) & (b)) | ((b) & (res)) | ((res) & ~(a))); fH = c_ >> 3 & 1; fC = c_ >> 7 & 1; \
    fV = (uint8_t)(((a) & ~(b) & ~(res)) | (~(a) & (b) & (res))) >> 7 & 1; fN = (res) >> 7 & 1; \
    fZ = (keepz) ? ((uint8_t)(res) == 0) & fZ : (uint8_t)(res) == 0; fS = fN ^ fV; } while (0)
#define XR (R[26] | R[27] << 8)
#define YR (R[28] | R[29] << 8)
#define ZR (R[30] | R[31] << 8)
#define SETX(v) do { R[26] = (uint8_t)(v); R[27] = (uint8_t)((v) >> 8); } while (0)
#define SETY(v) do { R[28] = (uint8_t)(v); R[29] = (uint8_t)((v) >> 8); } while (0)
#define SETZ(v) do { R[30] = (uint8_t)(v); R[31] = (uint8_t)((v) >> 8); } while (0)
#define PUSHB(v) do { if (sp > RAMEND || sp < 0x100) die("stack outside RAM", pc); data[sp] = (uint8_t)(v); sp--; if (sp < minsp) minsp = sp; } while (0)
#define POPB() (sp++, (sp > RAMEND ? (die("stack outside RAM", pc), 0) : data[sp]))
    while (!stopped && cyc < maxcyc) {
        if (pc >= FLASH_WORDS) die("program counter outside flash", pc);
        const insn *in = &dec[pc];
        uint32_t d = in->d, r = in->r; uint8_t a, b, res; uint32_t t, u;
        uint32_t pc0 = pc; uint64_t c0 = cyc;
        switch (in->op) {
        case NOP: pc++; cyc++; break;
        case MOVW: R[d] = R[r]; R[d + 1] = R[r + 1]; pc++; cyc++; break;
        case MULS: t = (uint16_t)((int8_t)R[d] * (int8_t)R[r]); R[0] = (uint8_t)t; R[1] = (uint8_t)(t >> 8); fC = t >> 15 & 1; fZ = (t & 0xFFFF) == 0; pc++; cyc += 2; break;
        case MULSU: t = (uint16_t)((int8_t)R[d] * (uint8_t)R[r]); R[0] = (uint8_t)t; R[1] = (uint8_t)(t >> 8); fC = t >> 15 & 1; fZ = (t & 0xFFFF) == 0; pc++; cyc += 2; break;
        case FMUL: t = (uint16_t)(R[d] * R[r]); fC = t >> 15 & 1; t = (uint16_t)(t << 1); R[0] = (uint8_t)t; R[1] = (uint8_t)(t >> 8); fZ = t == 0; pc++; cyc += 2; break;
        case FMULS: t = (uint16_t)((int8_t)R[d] * (int8_t)R[r]); fC = t >> 15 & 1; t = (uint16_t)(t << 1); R[0] = (uint8_t)t; R[1] = (uint8_t)(t >> 8); fZ = t == 0; pc++; cyc += 2; break;
        case FMULSU: t = (uint16_t)((int8_t)R[d] * (uint8_t)R[r]); fC = t >> 15 & 1; t = (uint16_t)(t << 1); R[0] = (uint8_t)t; R[1] = (uint8_t)(t >> 8); fZ = t == 0; pc++; cyc += 2; break;
        case MUL: t = (uint16_t)(R[d] * R[r]); R[0] = (uint8_t)t; R[1] = (uint8_t)(t >> 8); fC = t >> 15 & 1; fZ = t == 0; pc++; cyc += 2; break;
        case CPC: a = R[d]; b = R[r]; res = (uint8_t)(a - b - fC); SUBF(a, b, res, 1); pc++; cyc++; break;
        case SBC: a = R[d]; b = R[r]; res = (uint8_t)(a - b - fC); SUBF(a, b, res, 1); R[d] = res; pc++; cyc++; break;
        case ADD: a = R[d]; b = R[r]; res = (uint8_t)(a + b); ADDF(a, b, res); R[d] = res; pc++; cyc++; break;
        case ADC: a = R[d]; b = R[r]; res = (uint8_t)(a + b + fC); ADDF(a, b, res); R[d] = res; pc++; cyc++; break;
        case CPSE: SKIP(R[d] == R[r]); break;
        case CP: a = R[d]; b = R[r]; res = (uint8_t)(a - b); SUBF(a, b, res, 0); pc++; cyc++; break;
        case SUB: a = R[d]; b = R[r]; res = (uint8_t)(a - b); SUBF(a, b, res, 0); R[d] = res; pc++; cyc++; break;
        case AND: res = R[d] & R[r]; fV = 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case EOR: res = R[d] ^ R[r]; fV = 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case OR: res = R[d] | R[r]; fV = 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case MOV: R[d] = R[r]; pc++; cyc++; break;
        case CPI: a = R[d]; b = (uint8_t)in->k; res = (uint8_t)(a - b); SUBF(a, b, res, 0); pc++; cyc++; break;
        case SBCI: a = R[d]; b = (uint8_t)in->k; res = (uint8_t)(a - b - fC); SUBF(a, b, res, 1); R[d] = res; pc++; cyc++; break;
        case SUBI: a = R[d]; b = (uint8_t)in->k; res = (uint8_t)(a - b); SUBF(a, b, res, 0); R[d] = res; pc++; cyc++; break;
        case ORI: res = R[d] | (uint8_t)in->k; fV = 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case ANDI: res = R[d] & (uint8_t)in->k; fV = 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case LDI: R[d] = (uint8_t)in->k; pc++; cyc++; break;
        case LDD_Y: R[d] = rd(YR + in->k, pc); pc++; cyc += 2; break;
        case LDD_Z: R[d] = rd(ZR + in->k, pc); pc++; cyc += 2; break;
        case STD_Y: wr(YR + in->k, R[d], pc); pc++; cyc += 2; break;
        case STD_Z: wr(ZR + in->k, R[d], pc); pc++; cyc += 2; break;
        case LDS: R[d] = rd((uint32_t)in->k, pc); pc += 2; cyc += 2; break;
        case STS: wr((uint32_t)in->k, R[d], pc); pc += 2; cyc += 2; break;
        case LD_ZP: t = ZR; R[d] = rd(t, pc); SETZ(t + 1); pc++; cyc += 2; break;
        case LD_MZ: t = (ZR - 1) & 0xFFFF; SETZ(t); R[d] = rd(t, pc); pc++; cyc += 2; break;
        case LD_YP: t = YR; R[d] = rd(t, pc); SETY(t + 1); pc++; cyc += 2; break;
        case LD_MY: t = (YR - 1) & 0xFFFF; SETY(t); R[d] = rd(t, pc); pc++; cyc += 2; break;
        case LD_X: R[d] = rd(XR, pc); pc++; cyc += 2; break;
        case LD_XP: t = XR; R[d] = rd(t, pc); SETX(t + 1); pc++; cyc += 2; break;
        case LD_MX: t = (XR - 1) & 0xFFFF; SETX(t); R[d] = rd(t, pc); pc++; cyc += 2; break;
        case ST_ZP: t = ZR; wr(t, R[d], pc); SETZ(t + 1); pc++; cyc += 2; break;
        case ST_MZ: t = (ZR - 1) & 0xFFFF; SETZ(t); wr(t, R[d], pc); pc++; cyc += 2; break;
        case ST_YP: t = YR; wr(t, R[d], pc); SETY(t + 1); pc++; cyc += 2; break;
        case ST_MY: t = (YR - 1) & 0xFFFF; SETY(t); wr(t, R[d], pc); pc++; cyc += 2; break;
        case ST_X: wr(XR, R[d], pc); pc++; cyc += 2; break;
        case ST_XP: t = XR; wr(t, R[d], pc); SETX(t + 1); pc++; cyc += 2; break;
        case ST_MX: t = (XR - 1) & 0xFFFF; SETX(t); wr(t, R[d], pc); pc++; cyc += 2; break;
        case LPM_Z: t = ZR; R[d] = (uint8_t)(flash[(t >> 1) & (FLASH_WORDS - 1)] >> ((t & 1) * 8)); pc++; cyc += 3; break;
        case LPM_ZP: t = ZR; R[d] = (uint8_t)(flash[(t >> 1) & (FLASH_WORDS - 1)] >> ((t & 1) * 8)); SETZ(t + 1); pc++; cyc += 3; break;
        case LPM0: t = ZR; R[0] = (uint8_t)(flash[(t >> 1) & (FLASH_WORDS - 1)] >> ((t & 1) * 8)); pc++; cyc += 3; break;
        case POP: R[d] = POPB(); pc++; cyc += 2; break;
        case PUSH: PUSHB(R[d]); pc++; cyc += 2; break;
        case COM: res = (uint8_t)~R[d]; fC = 1; fV = 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case NEG: a = R[d]; res = (uint8_t)(0 - a); fH = (res | a) >> 3 & 1; fV = res == 0x80; fC = res != 0; NZS(res); R[d] = res; pc++; cyc++; break;
        case SWAP: R[d] = (uint8_t)(R[d] << 4 | R[d] >> 4); pc++; cyc++; break;
        case INC: res = (uint8_t)(R[d] + 1); fV = res == 0x80; NZS(res); R[d] = res; pc++; cyc++; break;
        case DEC: res = (uint8_t)(R[d] - 1); fV = res == 0x7F; NZS(res); R[d] = res; pc++; cyc++; break;
        case ASR: a = R[d]; res = (uint8_t)((a >> 1) | (a & 0x80)); fC = a & 1; fN = res >> 7; fZ = res == 0; fV = fN ^ fC; fS = fN ^ fV; R[d] = res; pc++; cyc++; break;
        case LSR: a = R[d]; res = a >> 1; fC = a & 1; fN = 0; fZ = res == 0; fV = fC; fS = fV; R[d] = res; pc++; cyc++; break;
        case ROR: a = R[d]; res = (uint8_t)((a >> 1) | (fC << 7)); fC = a & 1; fN = res >> 7; fZ = res == 0; fV = fN ^ fC; fS = fN ^ fV; R[d] = res; pc++; cyc++; break;
        case BSET: sreg_set((uint8_t)(sreg_get() | 1u << d)); pc++; cyc++; break;
        case BCLR: sreg_set((uint8_t)(sreg_get() & ~(1u << d))); pc++; cyc++; break;
        case RET: case RETI: t = POPB(); t = t << 8 | POPB(); pc = t; cyc += 4; break;
        case IJMP: pc = ZR; cyc += 2; break;
        case ICALL: t = pc + 1; PUSHB(t); PUSHB(t >> 8); pc = ZR; cyc += 3; break;
        case JMP: pc = (uint32_t)in->k; cyc += 3; break;
        case CALL: t = pc + 2; PUSHB(t); PUSHB(t >> 8); pc = (uint32_t)in->k; cyc += 4; break;
        case RJMP: pc = (pc + 1 + (uint32_t)in->k) & (FLASH_WORDS - 1); cyc += 2; break;
        case RCALL: t = pc + 1; PUSHB(t); PUSHB(t >> 8); pc = (pc + 1 + (uint32_t)in->k) & (FLASH_WORDS - 1); cyc += 3; break;
        case ADIW: t = R[d] | R[d + 1] << 8; u = (t + (uint32_t)in->k) & 0xFFFF; fV = (~t & u) >> 15 & 1; fC = (~u & t) >> 15 & 1; fN = u >> 15; fZ = u == 0; fS = fN ^ fV;
            R[d] = (uint8_t)u; R[d + 1] = (uint8_t)(u >> 8); pc++; cyc += 2; break;
        case SBIW: t = R[d] | R[d + 1] << 8; u = (t - (uint32_t)in->k) & 0xFFFF; fV = (t & ~u) >> 15 & 1; fC = (u & ~t) >> 15 & 1; fN = u >> 15; fZ = u == 0; fS = fN ^ fV;
            R[d] = (uint8_t)u; R[d + 1] = (uint8_t)(u >> 8); pc++; cyc += 2; break;
        case CBI: wr((uint32_t)in->k, (uint8_t)(rd((uint32_t)in->k, pc) & ~(1u << d)), pc); pc++; cyc += 2; break;
        case SBI: wr((uint32_t)in->k, (uint8_t)(rd((uint32_t)in->k, pc) | 1u << d), pc); pc++; cyc += 2; break;
        case SBIC: SKIP(!(rd((uint32_t)in->k, pc) >> d & 1)); break;
        case SBIS: SKIP(rd((uint32_t)in->k, pc) >> d & 1); break;
        case IN: R[d] = rd((uint32_t)in->k, pc); pc++; cyc++; break;
        case OUT: wr((uint32_t)in->k, R[d], pc); pc++; cyc++; break;
        case BRBS: if (sreg_get() >> d & 1) { pc = pc + 1 + (uint32_t)in->k; cyc += 2; } else { pc++; cyc++; } break;
        case BRBC: if (!(sreg_get() >> d & 1)) { pc = pc + 1 + (uint32_t)in->k; cyc += 2; } else { pc++; cyc++; } break;
        case BLD: R[d] = (uint8_t)((R[d] & ~(1u << r)) | (uint32_t)fT << r); pc++; cyc++; break;
        case BST: fT = R[d] >> r & 1; pc++; cyc++; break;
        case SBRC: SKIP(!(R[d] >> r & 1)); break;
        case SBRS: SKIP(R[d] >> r & 1); break;
        default: die("unknown instruction", pc);
        }
        if (c0 >= prof_from) prof[pc0] += cyc - c0;
    }
    if (getenv("AVRSIM_PROFILE")) { FILE *pf = fopen(getenv("AVRSIM_PROFILE"), "w"); for (int i = 0; i < FLASH_WORDS; i++) if (prof[i]) fprintf(pf, "%d %llu\n", i * 2, (unsigned long long)prof[i]); fclose(pf); }
    fflush(stdout);
    phase_tot[phase & 15] += cyc - phase_cyc;
    for (int i = 1; i < 16; i++) if (phase_tot[i]) fprintf(stderr, "phase %d: %.1f s\n", i, (double)phase_tot[i] / 16e6);
    fprintf(stderr, "%s after %.1f s of chip time at 16 MHz (%llu cycles). Deepest stack: 0x%03X, %u bytes below the top of RAM (0x8FF).\n",
            stopped ? "program finished" : "time limit reached", (double)cyc / 16e6, (unsigned long long)cyc, minsp, RAMEND - minsp);
    return stopped ? 0 : 3;
}
