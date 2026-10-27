/* Apple IIe Time Pilot IIvera — custom linker script.
 *
 * Loads at $1400 instead of $2000, giving up to 33,280 bytes ($1400-$95FF).
 * $1000-$12FF is ProDOS QUIT code and $1300-$13FF is a BASIC.SYSTEM scratch
 * area; $1400 is the first genuinely free address under ProDOS 2.4.3 +
 * BASIC.SYSTEM. The game itself never calls MLI QUIT, so its help-page buffer
 * at $0E00 (1,440 bytes, ending at $139F) may safely overlap those two regions;
 * see vera/AGENTS.md §2.3. Do not add a quit path without moving that buffer.
 *
 * ProDOS BRUN transfers control to the load address ($1400), so entry point
 * must be at $1400.
 *
 * The three page-aligned 1 KB ProDOS OPEN buffers live at $7000-$7BFF (see
 * src/disk.c), so the payload must end below $7000.  The assert at the end of
 * this file fails the link if it ever grows into them.
 */

/* Provide imaginary (zero page) registers.  Apple II zero page $00-$01 is
 * reserved; we start imaginary registers at $02. */
__rc0 = 0x02;
INCLUDE imag-regs.ld   /* __rc31 = 0x20 */

MEMORY {
    /* Zero page variables: $50-$BF.
     * $00-$01: Apple II Monitor
     * $02-$21: LLVM-MOS imaginary registers (__rc0 to __rc31)
     * $22-$3F: Applesoft / Monitor reserved
     * $40-$4F: ProDOS MLI internal workspace (reserved for MLI calls)
     * $50-$BF: Safe for C zero page variables (112 bytes)
     * $C0-$FF: Apple II Hardware / Monitor / System
     */
    zp : ORIGIN = 0x50, LENGTH = 0xC0 - 0x50
    /* Main program RAM: $1400-$9BFF (34,816 bytes, leaves 8.5KB below $BE00 stack). */
    ram (rw) : ORIGIN = 0x1400, LENGTH = 0x8800
}

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

/* Entry point at $1400 (ProDOS BRUN jumps here). */
SECTIONS {
    . = 0x1400;
    .text : { INCLUDE text-sections.ld } > c_readonly
    INCLUDE zp.ld
    INCLUDE rodata.ld
    INCLUDE data.ld
    INCLUDE bss.ld
    INCLUDE noinit.ld
}

/* Soft stack grows down from $BE00 (same as stock apple2e). */
__stack = 0xBE00;

/* The ProDOS OPEN buffers at $7000-$7BFF are not allocated by this region, so
 * guard the boundary here instead of failing at run time. */
ASSERT(__heap_start < 0x7000,
       "payload overflows the ProDOS I/O buffers at $7000")

OUTPUT_FORMAT {
    /* ProDOS BIN header: 2-byte load address ($1400) + 2-byte length. */
    SHORT(0x1400)
    SHORT(__data_end - 0x1400)
    TRIM(ram)
}
