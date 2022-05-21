/* 6502 assembler...

   $xxxx => never zero page, even if $00xx
   $xx   => always zero page
   3-digit hex addrs are not supported

   how do I force absolute when using a label with a low value?  >LBL

   lbl  no colon(!) but have to start in col 1 (as the only thing allowed there)

   lbl = <val>
   .byte $xx, $xx...
   .byte "kjsdfjsdfh"
   .org  $xxxx

   The length of an instruction depends on whether zeropage or abs is used.
   That's easy for backwards references.  Forward references?  Could force
   absolute.  Yes, only used for data (and self-modifying code) so it shouldn't
   be a problem!

   Expressions in defines?  Perhaps.  Forward references in expressions?
   Maybe...  should be doable in a two-pass assembler or with backpatching, as
   long as code/data generation size (and therefore also other labels/defines)
   can't depend on forward references.
   The rule about forward refs => absolute should really be: unknown val in
   first pass => absolute.

   I think the assembler needs a fast way to 1) find a mnemonic and 2) loop
   through addr modes.  #2 is a FAT-like linked list in a 256-byte array.
   #1 is a hash (if unique hash values can be found) => the first mnemonic in
   the link.  I would of course never do something so wasteful on a tiny machine.
   I would probably use a hash of the first two chars + a (short) overflow chain.

   46 mnemonics of 3 chars.  5 bits each.  15 bits in total -- we could actually
   use a 32K lookup table!  It's easy to create at run-time.

   Also, for Apple II compat, perhaps:
     ASC
     DFB (define byte)
     DFS (define storage)
     EQU
     EPZ        EQU for Page Zero
     
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
