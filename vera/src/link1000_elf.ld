/* Disassembly-only variant of link1000.ld: same memory map, ELF output so
 * llvm-objdump can read it.  Not used for shipping images. */

__rc0 = 0x02;
INCLUDE imag-regs.ld

MEMORY {
    zp : ORIGIN = 0x50, LENGTH = 0xC0 - 0x50
    ram (rw) : ORIGIN = 0x1400, LENGTH = 0x8800
}

REGION_ALIAS("c_readonly", ram)
REGION_ALIAS("c_writeable", ram)

SECTIONS {
    . = 0x1400;
    .text : { INCLUDE text-sections.ld } > c_readonly
    INCLUDE zp.ld
    INCLUDE rodata.ld
    INCLUDE data.ld
    INCLUDE bss.ld
    INCLUDE noinit.ld
}

__stack = 0xBE00;
