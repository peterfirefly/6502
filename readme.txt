6502 emulator, quick 2-day hack
---

Just the basics.  Documented instructions only.  Emulated at instruction level.
No clock counting.  No interrupts.  No reset.  No support for the SO ("Set Overflow")
pin.
A real 6502 generates extra memory reads now and then (which are then just thrown
away).  I am not going to emulate that.  A "RTS" instruction generates no less
than *three* extra memory reads, for example!



Spurious memory read/writes
---
All 6502's that I know of perform spurious reads and writes.  RMW instructions
on NMOS 6502 perform a write back with an incorrect value before writing the
correct value in the subsequent cycle.  Lots of addressing modes cause one or
more extra reads.  Lots of instructions perform extra reads.

Precisely *which* reads/writes depends on the version.  The phony writes for
RMW instructions was apparently considered bad enough that it was fixed in
65C02.



Variants
---
It turns out there are many variants of the 6502.  The very early ones had a
bug in the ROR instruction so it was omitted from the datasheet.  All NMOS versions
had a bug in JMP (indir) if the vector is at the end of a page: JMP ($12FF) reads
the target addr from $12FF and $1200 instead of from $12FF and $1300.  This is
corrected in the CMOS version.
All NMOS versions set the N/Z/V flags in decimal mode based on the *binary*
operation, not on the *decimal* operation.  Why?  Because the ALU works in binary
and the result is corrected afterwards.  The CMOS version spends an extra cycle
to set the flags based on the decimal result.
All NMOS versions do an extra dummy write for RMW instructions which may be a
problem for hardware registers.
There are also bugs regarding indexed addressing when crossing a page and regarding
the simultaneous execution of BRK and acknowledgement of an interrupt.   Again,
this is fixed in the CMOS version.

The CMOS version added a few instructions:
  INC/DEC               (for the accumulator -- the NMOS version can only inc/dec X/Y/mem)
  STZ  addr             (store zero)
  PHX/PHY/PLX/PLY       (push/pull X/Y)
  BRA  rel              (unconditional relative jump)

  + more BIT addr modes + TSB/TRB (test-and-set bits, test-and-reset bits)
  + STP/WAI (stop CPU until reset/interrupt -- power-saving)

Then there all the different CMOS clones... And all the extended versions.

I am just going to implement the documented instructions of the NMOS 6502 but
without the page crossing bugs.  I am not going to implement reset, NMI, IRQ,
or the SO (set overflow) pin.



Decoding
---
I know that the instruction set is ridiculously simple.  There are no multi-byte
opcodes.  Some instructions have one or two immediate bytes.  Many opcodes are
unassigned.

There isn't much useful structure in the opcode map.  Just use a big switch
and get it over with.


Addresses as 2 bytes
---
Not really a 16-bit CPU.  Does 16-bit addressing with PC and SP and various
addr modes but it is really done with 8-bit registers.  This means there are a
couple of wrap-around bugs on at least some 6502 versions.

Which ones are they?  Are they relevant?



Evaluation order
---
The exact order sometimes matter -- JSR reads the low byte of the new addr,
then it stores the return address and then it reads the high byte of the new
addr.  This matters if executing from the stack.


Addressing modes
---
Accumulator
  A
Immediate
  #     1-byte immediate
zeropage
  zpg   1-byte immediate is the address of the operand
indexed zeropage
  zpg,X
  zpg,Y
Absolute
  abs   2-byte address
indexed absolute
  abs,X         addr = imm16 + X
  abs,Y         addr = imm16 + Y
indirect
  jmp (ind)     jump to address at [imm16]

  (ind,X)       addr = [imm16 + X]
  (ind),Y       addr = [

Relative

[FIXME]



Flags
---
N/Z/C are easy.
  N/Z for almost any kind of data transfer
  C for ADC/SBC, CMP/CPX/CPY, ROL/ROR, LSR
V is harder.  ADC.  BIT.  SO pin.  SBC.

There are 0 16-bit operations, so doing 8-bit add/sub with extra precision is
very cheap.  So that's what I will do for ADC/SBC, CMP/CPX/CPY to evaluate the
true unsigned and signed results and get C and V.


Decimal mode
---
ADC/SBC work in BCD in decimal mode -- but what if their inputs aren't proper
BCD values?

(There is no ADD/SUB, btw.)

http://www.righto.com/2013/08/reverse-engineering-8085s-decimal.html



Testing
---
http://rubbermallet.org/fake6502.c
https://github.com/Klaus2m5/6502_65C02_functional_tests/tree/master


-------------------------------------------------------------------------------

2020-12-11  Friday
2h or so.  Outline of the code.  Trying to understand addressing modes and flags,
the two things that always did my head in with the 6502.

Have done nothing on 6502 disassembler.
Have done nothing on 6502 assembler.

Todo:
 - understand addr modes
 x understand flags
 x understand push/pop, jsr/rts, brk
 - understand reset
 - flags code
 - code the instructions
 - run a little test program

2020-12-13  Sunday
1h or so.  BRK, RTI, Bxx, JMP (indir), JMP abs, F_PHYSICAL.  cpu_adc().  ADC #.
Pretty much understood flags.  My biggest problem is still the addressing modes.

2020-12-14  Monday
Printed out manual.  Realized I had misunderstood BRK/IRQ/NMI and PHP.  Turns
out PHP also sets B.  Turns out that all the flag pushing sets bit 5.
Read 20% of the manual.  Coded SBC #/abs, BIT abs, EOR/ORA/AND #/abs,
CMP/CPX/CPY #/abs.  2h so far.
Still need ASL/LSR, ROL/ROR + addr modes.
Taking a short break.
1h work later.  Almost all addr modes implemented -- only indirect indexed left.

<long break -- low blood sugar, shopping, dürüm, etc>

1h for a bit of reading, double-checking ASL/LSR and implementing ROL/ROR.

Addr modes:
 * accumulator  ROL     A
 * immediate    LDA     #12
 * implied      CLC

 * relative     BNE     L1
 * absolute     JMP     $1234
 * zero-page    LDA     $23
 * indirect:    JMP     ($1234)

 * absolute indexed     STA     $1000,Y ; $1000 + Y
 * zero-page indexed    STA     $C0,X   ; ($C0 + X) & 0xFF
 * indexed indirect     LDA     ($20,X) ; addr = memw[($20 + X) & 0xFF], X-only
 * indirect indexed     LDA     ($86),Y ; addr = memw[$86] + Y , Y-only


Todo:
 * understand addr modes
 * ASL
 * LSR
 * ROL/ROR
 - decimal [1 hour?]

 - short test loop [10 minutes?]
 - AllSuite [1h?]
 - ehbasic [1h?]


2020-12-15  Tuesday
About 1h sketching out a disassembler.  Disassembler is 0.5K code + 0.5K data.
The 6502 is *tiny*!
Todo: fill out the tables and test the disassembler on a binary blob.
1h musings about assembler.  Think I have a spec now.

Decimal (40% down)
http://www.zimmers.net/anonftp/pub/cbm/documents/chipdata/64doc

https://stackoverflow.com/questions/29193303/6502-emulation-proper-way-to-implement-adc-and-sbc

http://rubbermallet.org/fake6502.c

https://github.com/Klaus2m5/6502_65C02_functional_tests/tree/master

Todo:
 - decimal
 - short test loop
 - AllSuite
 - ehbasic

 - disasm
 - asm
