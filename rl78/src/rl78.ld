/* Linker script -- R7F0C001G2 (16 KB flash, 1 KB RAM FFB00H-FFEFFH), Q3RF
 *
 * FONTOS: a const adatot (.rodata) a GCC 16-bites near-cimmel olvassa, ami az
 * F0000H-s bankra mutat.  A flash csak 02000H-tol latszik ott (tukor:
 * 02000H-07FFFH -> F2000H-F7FFFH, kezikonyv 3.1.2).  Ezert a .rodata a
 * 0x2000-0x3FFF tartomanyba kerul (RODATA region) -- igy a const tablak
 * helyesen olvashatok.  A kod (.text) 0x100-0x1FFF.                          */
OUTPUT_ARCH(rl78)
ENTRY(_start)

MEMORY
{
  VEC (r)    : ORIGIN = 0x00000, LENGTH = 0x00080   /* reset/IRQ vektor-tabla */
  OPT (r)    : ORIGIN = 0x000C0, LENGTH = 0x00004   /* option byte 0xC0..0xC3 */
  ROM (rx)   : ORIGIN = 0x00100, LENGTH = 0x03700   /* kod, 0x100..0x37FF (2026-11-25: bovitve) */
  RODATA (r) : ORIGIN = 0x03800, LENGTH = 0x00400   /* const, tukrozott terulet (14. blokk; a 15. az FSW-nek szabad) */
  RAM (rw)   : ORIGIN = 0xFFC90, LENGTH = 0x00150   /* (crt0 a .noinit kivetelevel mindent nullaz) FFB00-FFC89: adatflash ROM-rutin munkaterulete */
}

SECTIONS
{
  .vec          : { KEEP(*(.vec)) }           > VEC
  .option_bytes : { KEEP(*(.option_bytes)) }  > OPT

  .text :
  {
    *(.text) *(.text.*)
  } > ROM

  .rodata :
  {
    *(.rodata) *(.rodata.*)
    *(.frodata) *(.frodata.*)
  } > RODATA

  .noinit (NOLOAD) :
  {
    __noinit_start = .;
    *(.noinit) *(.noinit.*)
    . = ALIGN(2);
    __noinit_end = .;
  } > RAM

  .bss (NOLOAD) :
  {
    *(.bss) *(.bss.*) *(COMMON)
  } > RAM

  /DISCARD/ : { *(.comment) *(.note) *(.note.*) }
}
