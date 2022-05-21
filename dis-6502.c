/* 6502 disassembler

   I choose a method very similar to the one I used in the Z80 disassembler.
   There is an array of 256 strings, each string consists of the mnemonic +
   a character that indicates the addr mode used.
   Strictly speaking, it is not one array of 256 strings.  It is a char array
   with all the strings packed together + another array that points into it.

   Or... packed strings + indices (which might even be single byte!) + array of
   addrmodes as single bytes.  The mnemonics are 3 bytes each.

   2 brk,nop
   8 bcc

   4 shift/rol
   6 inc/dec, dey/iny/dex/inx

   3 cmp/cpx/cpy
   7 flag clear/set

   6 lda/ldx/ldy, sta/stx/sty
   4 php/plp, pha/pla

   1 jmp
   3 and/ora/eor
   6 Txx

   2 rts,rti
   1 jsr
   2 adc/sbc
   1 bit

   46 mnemonics of 3 chars = 138, so we can use a single byte to indicate the
   mnemonic.
   how many addr modes?
     implied
     A
     imm                #
     abs

     zero-page
     rel
     abs,X
     abs,Y

     zpg,X
     zpg,Y
     (ind,X)
     (ind),Y
     (ind)
 
   Less than 16, so we can compress that table to 128 bytes instead of 256.

   The tables should of course be machine generated.

   128 + 256 + 138 = 518 bytes data.
 */

#include <stdint.h>
#include <stdio.h>

static const char       mne_s[] = {"BRKORAANDTXATAXPHPPLPCLCJSRRTI"};
static const uint8_t    mne_idx[256] = {
        0, 3, 3, 6, -1,
};

static const uint8_t addrmode[128] = {
 0x0A, 0x00, 0x07,
};

void dis(uint8_t instr[3], uint16_t addr)
{
        printf("%04X:\t", addr);
        if (mne_idx[instr[0]] == 0xFF) {
                printf("\tDB\t$%02X\t; illegal instruction\n", instr[0]);
                return;
        }
        const char      *p = mne_s + mne_idx[instr[0]];
        printf("\t%c%c%c", p[0], p[1], p[2]);

        /* unpack addr mode nibble */
        uint8_t am = addrmode[instr[0] >> 1];
        if (instr[0] & 1)
                am = am & 0xF;
        else
                am = am >> 4;
        if (am)                 /* not 'implied' addr mode? */
                printf("\t");

        uint16_t        word = (instr[2] << 8) + instr[1];
        uint16_t        target = addr + (int16_t)(int8_t) instr[1];
        switch (am) {
        case  1: printf("A");                   break;
        case  2: printf("#$%02X", instr[1]);    break;
        case  3: printf("$%04X", word);         break;
        case  4: printf("$%02X", instr[1]);     break;
        case  5: printf("$%04X\t; +%02X", target, instr[1]);   break;
        case  6: printf("$%04X,X", word);       break;
        case  7: printf("$%04X,Y", word);       break;
        case  8: printf("$%02X,X", instr[1]);   break;
        case  9: printf("$%02X,Y", instr[1]);   break;
        case 10: printf("($%04X,X)", word);     break;
        case 11: printf("($%04X,Y)", word);     break;
        case 12: printf("($%04X)", word);       break;
        }
        printf("\n");
}
