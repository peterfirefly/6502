/* gen-6502 -- generate tables for 6502 disassembler */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

char    mne[48][3] = {
"BRK", "ORA", "", "", "", "ORA", "ASL", "",    "PHP", "ORA", "ASL", "", "", "ORA", "ASL", "",
"BPL", "ORA", "", "", "", "ORA", "ASL", "",    "CLC", "ORA", "",    "", "", "ORA", "ASL", "",
"JSR", "AND", "", "", "BIT", "AND", "ROL", "", "PLP", "AND", "ASL", "", "BIT", "AND", "ROL", "",
};

#define A        1
#define IMM      2
#define ABS      3
#define ZPG      4
#define REL      5
#define AX       6
#define AY       7
#define ZX       8
#define ZY       9
#define IX      10
#define IY      11
#define IND     12

uint8_t addrmode[256] = {
        0, IX, 0, 0, 0, ZPG, ZPG, 0,    0, IMM, A, 0, 0, ABS, ABS, 0,

};

int main(int argc, char *argv[])
{
        (void) argc, (void) argv;

        printf("static const char mne[48][3] = {\n");
        for (unsigned i=0; i<48; i++) {
                if (mne[i][0])
                        printf("\"%c%c%c\"", mne[i][0], mne[i][1], mne[i][2]);
                else
                        printf("\"\"   ");
                if (i % 16 == 15)
                        printf(",\n");
                else
                        printf(", ");
                if (i % 16 == 7)
                        printf("  ");
        }
        printf("};\n");
        printf("\n\n");
        printf("#define\t%s\t%2d\n", "A",   A  );
        printf("#define\t%s\t%2d\n", "IMM", IMM);
        printf("#define\t%s\t%2d\n", "ABS", ABS);
        printf("#define\t%s\t%2d\n", "ZPG", ZPG);
        printf("#define\t%s\t%2d\n", "REL", REL);
        printf("#define\t%s\t%2d\n", "AX",  AX );
        printf("#define\t%s\t%2d\n", "AY",  AY );
        printf("#define\t%s\t%2d\n", "ZX",  ZX );
        printf("#define\t%s\t%2d\n", "ZY",  ZY );
        printf("#define\t%s\t%2d\n", "IX",  IX );
        printf("#define\t%s\t%2d\n", "IY",  IY );
        printf("#define\t%s\t%2d\n", "IND", IND);
        printf("\n\n");
        printf("static const uint8_t addrmode[128] = {\n");
        for (unsigned i=0; i < 128; i++) {
                printf("0x%X%X", addrmode[i*2], addrmode[i*2+1]);
                if (i % 16 == 15)
                        printf(",\n");
                else
                        printf(", ");
        }
        printf("};\n");

        return EXIT_SUCCESS;
}