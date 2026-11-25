/* Minimal RL78 startup for R7F0C001G2 -- Q3RF LCD segment test
 * Nincs .data/.bss (csak const a flashben), ezert csak SP + main kell. */

	.section .vec, "a", @progbits      /* reset vektor @ 0x00000 (2 bajt) */
	.global __vectors
__vectors:
	.short _start
	/* 2026-11-25: TELJES vektortabla.  Korabban csak a reset-vektor volt kitoltve,
	 * a tobbi 0xFFFF -> ha barmi megszakitast fogad el (pl. az adatflash ROM-rutin
	 * kozben, nyitott ITMK/KRMK + fuggo ITIF/KRIF), a CPU 0xFFFF-re ugrik -> TRAP.
	 * Minden vektor egy RETI csonkra mutat (BRK @7E: RETB). */
	.rept 62
	.short _isr_stub
	.endr
	.short _brk_stub

	/* User + OCD option bytes @ 0x000C0..0x000C3 (kezikonyv 22. fej.) */
	.section .option_bytes, "a", @progbits
	.byte 0x6E       /* 000C0H  WDT OFF (WDTON=0, WINDOW=100%)        */
	.byte 0x3F       /* 000C1H  LVD RESET mod, VLVD = 1.88 V fel / 1.84 V le (2.09 V-on az RF-adas aramlokese resetelt!)
	                    (VPOC=001, bit4=1, LVIS=11, LVIMDS=11; kezikonyv 22-2 tabla).
	                    Elembehelyezeskor (pergo erintkezo, lassu felfutas) es
	                    merulo elemnel resetben tart, igy a CPU sosem fut az LS-mod
	                    1.8 V-os minimuma alatt (korabban 0xFF = LVD KI: a POR mar
	                    1.51 V-nal elengedte -> elszallo CPU, flash-iras alacsony fesz.-en) */
	.byte 0xAA       /* 000C2H  LS mod, HOCO=8MHz (CMODE=10,FRQSEL=1010)
	                    LS: VDD 1.8-5.5 V @ max 8 MHz -- elemrol merulve is
	                    mukodik (a korabbi HS mod 0xEA csak 2.4 V felett!) */
	.byte 0x04       /* 000C3H  OCD OFF (bit2=1 kotelezo)             */

	.text
	.global _isr_stub
_isr_stub:
	reti
	.global _brk_stub
_brk_stub:
	retb

	.global _start
	.type _start, @function
_start:
	movw	sp, #0xFE20        /* stack top 0xFFE20: az FFE20-FFEDF savot az adatflash ROM-rutin hasznalja! */
	/* 2026-11-25: RAM-torles FFB00-FFEF7 (a 0. regiszterbank kivetelevel), kiveve a .noinit-et (RF-ID
	 * + magic + chk).  Hidegindulaskor (napokig nincs elem) a RAM szemet;
	 * korabban semmit nem nullaztunk -> inicializalatlan globalisok.          */
	movw	hl, #0xFB00
	clrw	ax
.Lclr:
	movw	ax, hl
	cmpw	ax, #__noinit_start
	bc	$.Lwr               /* hl < noinit_start -> torol */
	cmpw	ax, #__noinit_end
	bc	$.Lskip             /* noinit_start <= hl < noinit_end -> kihagy */
.Lwr:
	clrw	ax
	movw	[hl], ax
.Lskip:
	incw	hl
	incw	hl
	movw	ax, hl
	cmpw	ax, #0xFEF8         /* FFEF8-FFEFF = 0. regiszterbank (AX..HL) -> NEM toroljuk */
	bnz	$.Lclr
	call	!!_main            /* main() meghivasa                          */
.Lhang:
	br	!!.Lhang
	.size _start, .-_start
