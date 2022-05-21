/* Implements the documented instructions of the NMOS 6502, without the page
   crossing bugs and the extra writes of the RMW instructions.
   Flags in decimal mode are based on the binary operation before BCD correction
   (as on the NMOS 6502).  All later versions set the flags based on the decimal
   result (by spending an extra cycle) -- that is NOT what this emulator does.
 */

#include <assert.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/***/

#define ERROR(...)      do {                                            \
                                fprintf(stderr, __VA_ARGS__);           \
                                exit(EXIT_FAILURE);                     \
                        } while (0)


/***/

/* interface between CPU emulator and "real" world */

uint8_t mem[64*1024];

/* event log -- for testing */


uint8_t rd8(uint16_t addr)
{
        return mem[addr];
}


void wr8(uint16_t addr, uint8_t x)
{
        mem[addr] = x;
}


/***/

#define F_N     (1 << 7)        /* negative     */
#define F_V     (1 << 6)        /* overflow     */
#define F_5     (1 << 5)        /* bit 5, stack only, always set when pushed */
#define F_B     (1 << 4)        /* breakpoint, stack only       */
#define F_D     (1 << 3)        /* decimal      */
#define F_I     (1 << 2)        /* interrupt disable    */
#define F_Z     (1 << 1)        /* zero         */
#define F_C     (1 << 0)        /* carry        */

/* only 6 physical bits */
#define F_PHYSICAL      (F_N | F_V | F_D | F_I | F_Z | F_C)


struct {
        uint8_t         a, x, y;
        uint8_t         flags;          /* only 6 bits exist physically */
        uint8_t         sp;
        uint16_t        pc;
} cpu;


void cpu_init(void)
{
        cpu.a = cpu.x = cpu.y = 0;      /* cleared on POWER UP but not on RESET */
        cpu.flags = F_I;        /* RESET doesn't initialize all flags on true 6502 */
        cpu.sp = 0xFD;  /* RESET/POWER UP share PLA entries with BRK/IRQ/NMI, which all push 3 bytes,
                           POWER UP seems to clear SP first before executing RESET sequence.
                           RESET doesn't actually write anything to the stack but it
                           does decrement SP by 3 (the writes are turned into reads).
                         */
        cpu.pc = rd8(0xFFFC) + (rd8(0xFFFD) << 8);
}


static void cpu_push8(uint8_t x)
{
        wr8(0x0100 + cpu.sp, x);
        cpu.sp--;
}

static uint8_t cpu_pop8(void)
{
        cpu.sp++;
        return rd8(0x0100 + cpu.sp);
}

static uint8_t  cpu_fetch8(void)
{
        return rd8(cpu.pc++);
}


static uint16_t cpu_fetch16(void)
{
        uint16_t        addr;
        addr = rd8(cpu.pc++);
        addr = addr + (rd8(cpu.pc++) << 8);
        return addr;
}

/* get addr for "indexed indirect"   (ind,X) */
static uint16_t cpu_ind_x(void)
{
        uint8_t         zpaddr;
        uint16_t        addr;

        zpaddr = (cpu_fetch8() + cpu.x) & 0xFF;
        addr = rd8(zpaddr++);
        addr = addr + (rd8(zpaddr) << 8);
        return addr;
}


/* get addr for "indirect indexed"   (ind),Y */
static uint16_t cpu_ind_y(void)
{
        uint8_t         zpaddr;
        uint16_t        addr;

        zpaddr = cpu_fetch8();
        addr = rd8(zpaddr++);
        addr = addr + (rd8(zpaddr) << 8);
        return addr + cpu.y;
}


/* most writes to A/X/Y */
static void cpu_flags_nz(uint8_t x)
{
        cpu.flags = (cpu.flags &~ (F_N | F_Z))
                | ((x & 0x80) ? F_N : 0)
                | (x ? 0 : F_Z);
}

/* for CMP/CPX/CPY */
static void cpu_flags_nzc(uint16_t x)
{
        cpu.flags = (cpu.flags &~ (F_N | F_Z | F_C))
                | ((x & 0x80) ? F_N : 0)
                | ((x & 0xFF) ? 0 : F_Z)
                | ((x >> 8) ? 0 : F_C); /* note that this is negated, just like for SBC */
}

static void cpu_bit(uint8_t x)
{
        cpu.flags = (cpu.flags &~ (F_N | F_Z | F_V))
                | ((x & 0x80) ? F_N : 0)
                | ((cpu.a & x) ? 0 : F_Z)
                | ((x & 0x40) ? F_V : 0);
}

static void cpu_adc(uint8_t x)
{
        /* carry iff true unsigned result doesn't fit in 8 bits (hi != 0) */
        uint16_t u = (uint16_t) cpu.a + (uint16_t) x + (uint16_t) !!(cpu.flags & F_C);
        /* overflow iff true signed result doesn't fit in 8 bits */
        int16_t  s = (int16_t)(int8_t) cpu.a + (int16_t)(int8_t) x + (int16_t) !!(cpu.flags & F_C);

        /* decimal mode: http://www.righto.com/2013/08/reverse-engineering-8085s-decimal.html */

        cpu.a = u;
        cpu.flags = (cpu.flags &~ (F_N | F_Z | F_V | F_C))
                | ((cpu.a & 0x80) ? F_N : 0)
                | ( cpu.a ? 0 : F_Z)
                | ((u >> 8) ? F_C : 0)
                | (((s > 127) || (s < -128)) ? F_V : 0);
}

static void cpu_sbc(uint8_t x)
{
        /* SBC subtracts the *negated* carry flag -- it also sets the carry flag
           in a negated way (it is set when there was no borrow).

           This is because they cheaped out when they implemented subtraction.

           The operand gets negated in two-complement form and added normally
           to the accumulator -- but two-complement negation involves a bitwise
           not + the addition of 1.  A true SBC would require a two-complement
           negation (which includes the addition of 1) AND the subtraction of
           the carry flag.  They cheat by doing the bitwise not and then adding
           the carry flag just like a normal ADC.  This makes the logic circuit
           slightly simpler and shorter at the expense of making the assembler
           code more confusing.
         */

        /* carry iff true unsigned result doesn't fit in 8 bits (hi != 0) */
        uint16_t u = (uint16_t) cpu.a - (uint16_t) x - (uint16_t) !(cpu.flags & F_C);
        /* overflow iff true signed result doesn't fit in 8 bits */
        int16_t  s = (int16_t)(int8_t) cpu.a - (int16_t)(int8_t) x - (int16_t) !(cpu.flags & F_C);

        /* FIXME decimal */
        cpu.a = u;
        cpu.flags = (cpu.flags &~ (F_N | F_Z | F_V | F_C))
                | ((cpu.a & 0x80) ? F_N : 0)
                | ( cpu.a ? 0 : F_Z)
                | ((u >> 8) ? 0 : F_C)
                | (((s > 127) || (s < -128)) ? F_V : 0);
}

/* 9-bit rotate left through carry */
static uint8_t cpu_rol(uint8_t x)
{
        bool    cy;

        cy = x & 0x80;
        x = (x << 1) | !!(cpu.flags & F_C);
        cpu.flags = (cpu.flags &~ F_C) | (cy ? F_C : 0);
        cpu_flags_nz(x);
        return x;
}

/* 9-bit rotate right through carry */
static uint8_t cpu_ror(uint8_t x)
{
        bool    cy;

        cy = x & 0x01;
        x = ((cpu.flags & F_C) ? 0x80 : 0x00) | (x >> 1);
        cpu.flags = (cpu.flags &~ F_C) | (cy ? F_C : 0);
        cpu_flags_nz(x);
        return x;
}

/* run one instruction */
void cpu_execute(void)
{
        uint8_t opcode = cpu_fetch8();

        /* internal tmp values for some instructions */
        uint16_t        addr;
        uint8_t         tmp8;

        /* this switch is implemented via a jump table in .rodata in recent
           gcc/clang versions.  It is 1k (gcc, 4-byte entries) or 2k (clang,
           8-byte entries).
         */
        switch (opcode) {
        /* 00 */
        case 0x00:      /* BRK          */
                cpu_push8(cpu.pc >> 8);
                cpu_push8(cpu.pc & 0xFF);
                cpu_push8(cpu.flags | F_B | F_5);
                addr = rd8(0xFFFE);
                addr = addr + (rd8(0xFFFF) << 8);
                cpu.flags |= F_I;       /* disable interrupts */
                cpu.pc = addr;
                break;
        case 0x01:      /* ORA  (ind,X) */
                cpu.a |= rd8(cpu_ind_x());
                cpu_flags_nz(cpu.a);
                break;
        case 0x05:      /* ORA  zpg     */
                cpu.a |= rd8(cpu_fetch8());
                cpu_flags_nz(cpu.a);
                break;
        case 0x06:      /* ASL  zpg     */
                addr = cpu_fetch8();
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x80) ? F_C : 0);
                tmp8 = tmp8 << 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;
        case 0x08:      /* PHP          */
                cpu_push8(cpu.flags | F_B | F_5);
                break;
        case 0x09:      /* ORA  #       */
                cpu.a |= cpu_fetch8();
                cpu_flags_nz(cpu.a);
                break;
        case 0x0A:      /* ASL  A       */
                cpu.flags = (cpu.flags &~ F_C) | ((cpu.a & 0x80) ? F_C : 0);
                cpu.a = cpu.a << 1;
                cpu_flags_nz(cpu.a);
                break;
        case 0x0D:      /* ORA  abs     */
                cpu.a |= rd8(cpu_fetch16());
                cpu_flags_nz(cpu.a);
                break;
        case 0x0E:      /* ASL  abs     */
                addr = cpu_fetch16();
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x80) ? F_C : 0);
                tmp8 = tmp8 << 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* 10 */
        case 0x10:      /* BPL  rel     */
                addr = (int16_t)(int8_t) cpu_fetch8();
                if ((cpu.flags & F_N) == 0)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0x11:      /* ORA  (ind),Y */
                cpu.a |= rd8(cpu_ind_y());
                cpu_flags_nz(cpu.a);
                break;
        case 0x15:      /* ORA  zpg,X   */
                cpu.a |= rd8((cpu_fetch8() + cpu.x) & 0xFF);
                cpu_flags_nz(cpu.a);
                break;
        case 0x16:      /* ASL  zpg,X   */
                addr = (cpu_fetch8() + cpu.x) & 0xFF;
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x80) ? F_C : 0);
                tmp8 = tmp8 << 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;
        case 0x18:      /* CLC          */
                cpu.flags &= ~F_C;
                break;
        case 0x19:      /* ORA  abs,Y   */
                cpu.a |= rd8(cpu_fetch16() + cpu.y);
                cpu_flags_nz(cpu.a);
                break;
        case 0x1D:      /* ORA  abs,X   */
                cpu.a |= rd8(cpu_fetch16() + cpu.x);
                cpu_flags_nz(cpu.a);
                break;
        case 0x1E:      /* ASL  abs,X   */
                addr = cpu_fetch16() + cpu.x;
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x80) ? F_C : 0);
                tmp8 = tmp8 << 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* 20 */
        case 0x20:      /* JSR  abs     */
                /* Yes, the saved return address points to the *last* byte of
                   the instruction!
                   RTS will pop that value and increment it.

                   Yes, the target address is fetched *around* the push of the
                   return address.
                 */
                addr = cpu_fetch8();
                cpu_push8(cpu.pc >> 8);
                cpu_push8(cpu.pc & 0xFF);
                addr = addr | (cpu_fetch8() << 8);
                cpu.pc = addr;
                break;
        case 0x21:      /* AND  (ind,X) */
                cpu.a &= rd8(cpu_ind_x());
                cpu_flags_nz(cpu.a);
                break;
        case 0x24:      /* BIT  zpg     */
                cpu_bit(rd8(cpu_fetch8()));
                break;
        case 0x25:      /* AND  zpg     */
                cpu.a &= rd8(cpu_fetch8());
                cpu_flags_nz(cpu.a);
                break;
        case 0x26:      /* ROL  zpg     */
                addr = cpu_fetch8();
                wr8(addr, cpu_rol(rd8(addr)));
                break;
        case 0x28:      /* PLP          */
                cpu.flags = cpu_pop8() & F_PHYSICAL;
                break;
        case 0x29:      /* AND  #       */
                cpu.a &= cpu_fetch8();
                cpu_flags_nz(cpu.a);
                break;
        case 0x2A:      /* ROL  A       */
                cpu.a = cpu_rol(cpu.a);
                break;
        case 0x2C:      /* BIT  abs     */
                cpu_bit(rd8(cpu_fetch16()));
                break;
        case 0x2D:      /* AND  abs     */
                cpu.a &= rd8(cpu_fetch16());
                cpu_flags_nz(cpu.a);
                break;
        case 0x2E:      /* ROL  abs     */
                addr = cpu_fetch16();
                wr8(addr, cpu_rol(rd8(addr)));
                break;

        /* 30 */
        case 0x30:      /* BMI  rel     */
                addr = (int16_t)(int8_t) cpu_fetch8();
                if (cpu.flags & F_N)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0x31:      /* AND  (ind),Y */
                cpu.a &= rd8(cpu_ind_y());
                cpu_flags_nz(cpu.a);
                break;
        case 0x35:      /* AND  zpg,X   */
                cpu.a &= rd8((cpu_fetch8() + cpu.x) & 0xFF);
                cpu_flags_nz(cpu.a);
                break;
        case 0x36:      /* ROL  zpg,X   */
                addr = (cpu_fetch8() + cpu.x) & 0xFF;
                wr8(addr, cpu_rol(rd8(addr)));
                break;
        case 0x38:      /* SEC          */
                cpu.flags |= F_C;
                break;
        case 0x39:      /* AND  abs,Y   */
                cpu.a &= rd8(cpu_fetch16() + cpu.y);
                cpu_flags_nz(cpu.a);
                break;
        case 0x3D:      /* AND  abs,X   */
                cpu.a &= rd8(cpu_fetch16() + cpu.x);
                cpu_flags_nz(cpu.a);
                break;
        case 0x3E:      /* ROL  abs,X   */
                addr = cpu_fetch16() + cpu.x;
                wr8(addr, cpu_rol(rd8(addr)));
                break;

        /* 40 */
        case 0x40:      /* RTI          */
                cpu.flags = cpu_pop8() & F_PHYSICAL;
                addr = cpu_pop8();
                addr = addr + (cpu_pop8() << 8);
                cpu.pc = addr;
                break;
        case 0x41:      /* EOR  (ind,X) */
                cpu.a ^= rd8(cpu_ind_x());
                cpu_flags_nz(cpu.a);
                break;
        case 0x45:      /* EOR  zpg     */
                cpu.a ^= rd8(cpu_fetch8());
                cpu_flags_nz(cpu.a);
                break;
        case 0x46:      /* LSR  zpg     */
                addr = cpu_fetch8();
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x01) ? F_C : 0);
                tmp8 = tmp8 >> 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        case 0x48:      /* PHA          */
                cpu_push8(cpu.a);
                break;
        case 0x49:      /* EOR  #       */
                cpu.a ^= cpu_fetch8();
                cpu_flags_nz(cpu.a);
                break;
        case 0x4A:      /* LSR  A       */
                cpu.flags = (cpu.flags &~ F_C) | ((cpu.a & 0x01) ? F_C : 0);
                cpu.a = cpu.a >> 1;
                cpu_flags_nz(cpu.a);
                break;
        case 0x4C:      /* JMP  abs     */
                cpu.pc = cpu_fetch16();
                break;
        case 0x4D:      /* EOR  abs     */
                cpu.a ^= rd8(cpu_fetch16());
                cpu_flags_nz(cpu.a);
                break;
        case 0x4E:      /* LSR  abs     */
                addr = cpu_fetch16();
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x01) ? F_C : 0);
                tmp8 = tmp8 >> 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* 50 */
        case 0x50:      /* BVC  rel     */
                addr = (int16_t)(int8_t) cpu_fetch8();
                if ((cpu.flags & F_V) == 0)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0x51:      /* EOR  (ind),Y */
                cpu.a ^= rd8(cpu_ind_y());
                cpu_flags_nz(cpu.a);
                break;
        case 0x55:      /* EOR  zpg,X   */
                cpu.a ^= rd8((cpu_fetch8() + cpu.x) & 0xFF);
                cpu_flags_nz(cpu.a);
                break;
        case 0x56:      /* LSR  zpg,X   */
                addr = (cpu_fetch8() + cpu.x) & 0xFF;
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x01) ? F_C : 0);
                tmp8 = tmp8 >> 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;
        case 0x58:      /* CLI          */
                cpu.flags &= ~F_I;
                break;
        case 0x59:      /* EOR  abs,Y   */
                cpu.a ^= rd8(cpu_fetch16() + cpu.y);
                cpu_flags_nz(cpu.a);
                break;
        case 0x5D:      /* EOR  abs,X   */
                cpu.a ^= rd8(cpu_fetch16() + cpu.x);
                cpu_flags_nz(cpu.a);
                break;
        case 0x5E:      /* LSR  abs,X   */
                addr = cpu_fetch16() + cpu.x;
                tmp8 = rd8(addr);
                cpu.flags = (cpu.flags &~ F_C) | ((tmp8 & 0x01) ? F_C : 0);
                tmp8 = tmp8 >> 1;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* 60 */
        case 0x60:      /* RTS          */
                addr = cpu_pop8();
                addr = addr | (cpu_pop8() << 8);
                cpu.pc = addr+1;
                break;
        case 0x61:      /* ADC  (ind,X) */
                cpu_adc(rd8(cpu_ind_x()));
                break;
        case 0x65:      /* ADC  zpg     */
                cpu_adc(rd8(cpu_fetch8()));
                break;
        case 0x66:      /* ROR  zpg     */
                addr = cpu_fetch8();
                wr8(addr, cpu_ror(rd8(addr)));
                break;
        case 0x68:      /* PLA          */
                cpu.a = cpu_pop8();
                break;
        case 0x69:      /* ADC  #       */
                cpu_adc(cpu_fetch8());
                break;
        case 0x6A:      /* ROR  A       */
                cpu.a = cpu_ror(cpu.a);
                break;
        case 0x6C:      /* JMP  (ind)   */
                addr = cpu_fetch16();
                cpu.pc = rd8(addr);
                cpu.pc = cpu.pc + (rd8(addr+1) << 8);
                break;
        case 0x6D:      /* ADC  abs     */
                cpu_adc(rd8(cpu_fetch16()));
                break;
        case 0x6E:      /* ROR  abs     */
                addr = cpu_fetch16();
                wr8(addr, cpu_ror(rd8(addr)));
                break;

        /* 70 */
        case 0x70:      /* BVS  rel     */
                addr = (int16_t)(int8_t) rd8(cpu.pc++);
                if (cpu.flags & F_N)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0x71:      /* ADC  (ind),Y */
                cpu_adc(rd8(cpu_ind_y()));
                break;
        case 0x75:      /* ADC  zpg,X   */
                cpu_adc(rd8((cpu_fetch8() + cpu.x) & 0xFF));
                break;
        case 0x76:      /* ROR  zpg,X   */
                addr = (cpu_fetch8() + cpu.x) & 0xFF;
                wr8(addr, cpu_ror(rd8(addr)));
                break;

        case 0x78:      /* SEI          */
                cpu.flags |= F_I;
                break;
        case 0x79:      /* ADC  abs,Y   */
                cpu_adc(rd8(cpu_fetch16() + cpu.y));
                break;
        case 0x7D:      /* ADC  abs,X   */
                cpu_adc(rd8(cpu_fetch16() + cpu.x));
                break;
        case 0x7E:      /* ROR  abs,X   */
                addr = cpu_fetch16() + cpu.x;
                wr8(addr, cpu_ror(rd8(addr)));
                break;

        /* 80 */
        case 0x81:      /* STA  (ind,X) */
                wr8(cpu_ind_x(), cpu.a);
                break;
        case 0x84:      /* STY  zpg     */
                wr8(cpu_fetch8(), cpu.y);
                break;
        case 0x85:      /* STA  zpg     */
                wr8(cpu_fetch8(), cpu.a);
                break;
        case 0x86:      /* STX  zpg     */
                wr8(cpu_fetch8(), cpu.x);
                break;
        case 0x88:      /* DEY          */
                cpu.y--;
                cpu_flags_nz(cpu.y);
                break;
        case 0x8A:      /* TXA          */
                cpu.a = cpu.x;
                cpu_flags_nz(cpu.a);
                break;
        case 0x8C:      /* STY  abs     */
                wr8(cpu_fetch16(), cpu.y);
                break;
        case 0x8D:      /* STA  abs     */
                wr8(cpu_fetch16(), cpu.a);
                break;
        case 0x8E:      /* STX  abs     */
                wr8(cpu_fetch16(), cpu.x);
                break;

        /* 90 */
        case 0x90:      /* BCC  rel     */
                addr = (int16_t)(int8_t) cpu_fetch8();
                if ((cpu.flags & F_C) == 0)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0x91:      /* STA  (ind),Y */
                wr8(cpu_ind_y(), cpu.a);
                break;
        case 0x94:      /* STY  zpg,X   */
                wr8((cpu_fetch8() + cpu.x) & 0xFF, cpu.y);
                break;
        case 0x95:      /* STA  zpg,X   */
                wr8((cpu_fetch8() + cpu.x) & 0xFF, cpu.a);
                break;
        case 0x96:      /* STX  zpg,Y   */
                wr8((cpu_fetch8() + cpu.y) & 0xFF, cpu.x);
                break;

        case 0x98:      /* TYA          */
                cpu.a = cpu.y;
                cpu_flags_nz(cpu.a);
                break;
        case 0x99:      /* STA  abs,Y   */
                wr8(cpu_fetch16() + cpu.y, cpu.a);
                break;
        case 0x9A:      /* TXS          */
                cpu.sp = cpu.x; /* no flags affected */
                break;
        case 0x9D:      /* STA  abs,X   */
                wr8(cpu_fetch16() + cpu.x, cpu.a);
                break;

        /* A0 */
        case 0xA0:      /* LDY  #       */
                cpu.y = cpu_fetch8();
                cpu_flags_nz(cpu.y);
                break;
        case 0xA1:      /* LDA  (ind,X) */
                cpu.a = rd8(cpu_ind_x());
                cpu_flags_nz(cpu.a);
                break;
        case 0xA2:      /* LDX  #       */
                cpu.x = cpu_fetch8();
                cpu_flags_nz(cpu.x);
                break;
        case 0xA4:      /* LDY  zpg     */
                cpu.y = rd8(cpu_fetch8());
                cpu_flags_nz(cpu.y);
                break;
        case 0xA5:      /* LDA  zpg     */
                cpu.a = rd8(cpu_fetch8());
                cpu_flags_nz(cpu.a);
                break;
        case 0xA6:      /* LDX  zpg     */
                cpu.x = rd8(cpu_fetch8());
                cpu_flags_nz(cpu.x);
                break;

        case 0xA8:      /* TAY          */
                cpu.y = cpu.a;
                cpu_flags_nz(cpu.y);
                break;
        case 0xA9:      /* LDA  #       */
                cpu.a = cpu_fetch8();
                break;
        case 0xAA:      /* TAX          */
                cpu.x = cpu.a;
                cpu_flags_nz(cpu.x);
                break;
        case 0xAB:      /* --           */
        case 0xAC:      /* LDY  abs     */
                cpu.y = rd8(cpu_fetch16());
                cpu_flags_nz(cpu.y);
                break;
        case 0xAD:      /* LDA  abs     */
                cpu.a = rd8(cpu_fetch16());
                cpu_flags_nz(cpu.a);
                break;
        case 0xAE:      /* LDX  abs     */
                cpu.x = rd8(cpu_fetch16());
                cpu_flags_nz(cpu.x);
                break;

        /* B0 */
        case 0xB0:      /* BCS  rel     */
                addr = (int16_t)(int8_t) cpu_fetch8();
                if (cpu.flags & F_C)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0xB1:      /* LDA  (ind),Y */
                cpu.a = rd8(cpu_ind_y());
                cpu_flags_nz(cpu.a);
                break;
        case 0xB4:      /* LDY  zpg,X   */
                cpu.y = rd8((cpu_fetch8() + cpu.x) & 0xFF);
                cpu_flags_nz(cpu.y);
                break;
        case 0xB5:      /* LDA  zpg,X   */
                cpu.a = rd8((cpu_fetch8() + cpu.x) & 0xFF);
                cpu_flags_nz(cpu.a);
                break;
        case 0xB6:      /* LDX  zpg,Y   */
                cpu.x = rd8((cpu_fetch8() + cpu.y) & 0xFF);
                cpu_flags_nz(cpu.x);
                break;

        case 0xB8:      /* CLV          */
                cpu.flags &= ~F_V;
                break;
        case 0xB9:      /* LDA  abs,Y   */
                cpu.a = rd8(cpu_fetch16() + cpu.y);
                cpu_flags_nz(cpu.a);
                break;
        case 0xBA:      /* TSX          */
                cpu.x = cpu.sp;
                cpu_flags_nz(cpu.x);
                break;
        case 0xBC:      /* LDY  abs,X   */
                cpu.y = rd8(cpu_fetch16() + cpu.x);
                cpu_flags_nz(cpu.y);
                break;
        case 0xBD:      /* LDA  abs,X   */
                cpu.a = rd8(cpu_fetch16() + cpu.x);
                cpu_flags_nz(cpu.a);
                break;
        case 0xBE:      /* LDX  abs,Y   */
                cpu.x = rd8(cpu_fetch16() + cpu.y);
                cpu_flags_nz(cpu.x);
                break;

        /* C0 */
        case 0xC0:      /* CPY  #       */
                cpu_flags_nzc((uint16_t)cpu.y - (uint16_t)cpu_fetch8());
                break;
        case 0xC1:      /* CMP  (ind,X) */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8(cpu_ind_x()));
                break;
        case 0xC4:      /* CPY  zpg     */
                cpu_flags_nzc((uint16_t)cpu.y - (uint16_t)rd8(cpu_fetch8()));
                break;
        case 0xC5:      /* CMP  zpg     */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8(cpu_fetch8()));
                break;
        case 0xC6:      /* DEC  zpg     */
                addr = cpu_fetch8();
                tmp8 = rd8(addr);
                tmp8--;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        case 0xC8:      /* INY          */
                cpu.y++;
                cpu_flags_nz(cpu.y);
                break;
        case 0xC9:      /* CMP  #       */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)cpu_fetch8());
                break;
        case 0xCA:      /* DEX          */
                cpu.x--;
                cpu_flags_nz(cpu.x);
                break;
        case 0xCC:      /* CPY  abs     */
                cpu_flags_nzc((uint16_t)cpu.y - (uint16_t)rd8(cpu_fetch16()));
                break;
        case 0xCD:      /* CMP  abs     */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8(cpu_fetch16()));
                break;
        case 0xCE:      /* DEC  abs     */
                addr = cpu_fetch16();
                tmp8 = rd8(addr);
                tmp8--;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* D0 */
        case 0xD0:      /* BNE  rel     */
                addr = (int16_t)(int8_t) cpu_fetch8();
                if ((cpu.flags & F_Z) == 0)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0xD1:      /* CMP  (ind),Y */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8(cpu_ind_y()));
                break;
        case 0xD5:      /* CMP  zpg,X   */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8((cpu_fetch8() + cpu.x) & 0xFF));
                break;
        case 0xD6:      /* DEC  zpg,X   */
                addr = (cpu_fetch8() + cpu.x) & 0xFF;
                tmp8 = rd8(addr);
                tmp8--;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        case 0xD8:      /* CLD          */
                cpu.flags &= ~F_C;
                break;
        case 0xD9:      /* CMP  abs,Y   */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8(cpu_fetch16() + cpu.y));
                break;
        case 0xDD:      /* CMP  abs,X   */
                cpu_flags_nzc((uint16_t)cpu.a - (uint16_t)rd8(cpu_fetch16() + cpu.x));
                break;
        case 0xDE:      /* DEC  abs,X   */
                addr = cpu_fetch16() + cpu.x;
                tmp8 = rd8(addr);
                tmp8--;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* E0 */
        case 0xE0:      /* CPX  #       */
                cpu_flags_nzc((uint16_t)cpu.x - (uint16_t)cpu_fetch8());
                break;
        case 0xE1:      /* SBC  (ind,X) */
                cpu_sbc(rd8(cpu_ind_x()));
                break;
        case 0xE4:      /* CPX  zpg     */
                cpu_flags_nzc((uint16_t)cpu.x - (uint16_t)rd8(cpu_fetch8()));
                break;
        case 0xE5:      /* SBC  zpg     */
                cpu_sbc(rd8(cpu_fetch8()));
                break;
        case 0xE6:      /* INC  zpg     */
                addr = cpu_fetch8();
                tmp8 = rd8(addr);
                tmp8++;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        case 0xE8:      /* INX          */
                cpu.x++;
                cpu_flags_nz(cpu.x);
                break;
        case 0xE9:      /* SBC  #       */
                cpu_sbc(cpu_fetch8());
                break;
        case 0xEA:      /* NOP          */
                break;
        case 0xEC:      /* CPX  abs     */
                cpu_flags_nzc((uint16_t)cpu.x - (uint16_t)cpu_fetch8());
                break;
        case 0xED:      /* SBC  abs     */
                cpu_sbc(rd8(cpu_fetch16()));
                break;
        case 0xEE:      /* INC  abs     */
                addr = cpu_fetch16();
                tmp8 = rd8(addr);
                tmp8++;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        /* F0 */
        case 0xF0:      /* BEQ  rel     */
                addr = (int16_t)(int8_t)cpu_fetch8();
                if (cpu.flags & F_Z)
                        cpu.pc = cpu.pc + addr;
                break;
        case 0xF1:      /* SBC  (ind),Y */
                cpu_sbc(rd8(cpu_ind_y()));
                break;
        case 0xF5:      /* SBC  zpg,X   */
                cpu_sbc(rd8((cpu_fetch8() + cpu.x) & 0xFF));
                break;
        case 0xF6:      /* INC  zpg,X   */
                addr = (cpu_fetch8() + cpu.x) & 0xFF;
                tmp8 = rd8(addr);
                tmp8++;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        case 0xF8:      /* SED          */
                cpu.flags |= F_D;
                break;
        case 0xF9:      /* SBC  abs,Y   */
                cpu_sbc(rd8(cpu_fetch16() + cpu.y));
                break;
        case 0xFD:      /* SBC  abs,X   */
                cpu_sbc(rd8(cpu_fetch16() + cpu.x));
                break;
        case 0xFE:      /* INC  abs,X   */
                addr = cpu_fetch16() + cpu.x;
                tmp8 = rd8(addr);
                tmp8++;
                wr8(addr, tmp8);
                cpu_flags_nz(tmp8);
                break;

        default:
                ERROR("Undefined opcode %02X\n", opcode);
        }
}


int main(int argc, char *argv[])
{
        (void) argc, (void) argv;
        return EXIT_SUCCESS;
}