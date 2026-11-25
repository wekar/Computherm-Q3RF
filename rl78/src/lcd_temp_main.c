/* ============================================================================
 *  Q3RF  --  homerseklet-kijelzo   (Renesas R7F0C001G2, RL78-mag)
 *
 *  SZENZOR-KOR -- MERESSEL JAVITVA (2026-11-12, lcd_sensdiag_main.c):
 *     VDD -- RT1 (termisztor) + RV1 (3k trim) -- ANI0/P20 (pin38) --R4 10k-- P144 (pin39)
 *     (a KiCad v2 rajzon az RT1 alja GND-n van -- ez HIBAS, a VDD-re megy!)
 *  Meres: P144 = 0 -> varakozas -> ADC (ref = VDD) -> P144 = 1 (aram 0).
 *    arany  n/1024 = R4 / (R4 + Rs),   Rs = RV1 + RT1
 *    =>     Rs = 10000 * (1024 - n) / n        (VDD-tol fuggetlen)
 *  Mert pont: 23.2 C -> 45.4 %  -> Rs ~ 12.0 k
 *
 *  KEZELES:
 *    - ROOM mod (alap): mert szobahomerseklet "DD.D °C" + ROOM felirat,
 *      10 s-onkent frissul.  Tartomanyon kivul: "--.-"
 *    - barmelyik fel/le gomb -> SET mod: a beallitott (kivant) homerseklet
 *      + SET felirat, az EGESZ kijelzo 2 s-onkent villog (1.5 s be / 0.5 s ki).
 *      Mar az elso nyomas is +-0.5 C-ot lep (nyomva tartva 0.5 s utan
 *      ismetel).  Tartomany 18.0 ... 28.0 C, alap 21.0 C.
 *      5 s gombnyomas nelkul -> vissza ROOM modba.
 *    - a beallitott ertek RAM-ban van: elemcserenel visszaall 21.0-ra.
 *
 *  FUTESVEZERLES (RF, Computherm Q7RF/Q8RF protokoll, firmware/q7_protocol):
 *    - minden meres (10 s) utan: BE ha T <= SP-0.2, KI ha T >= SP+0.2
 *      (hibas/tartomanyon kivuli meresnel KI).  HEAT felirat = futes BE.
 *    - allapotvaltaskor azonnal kuldi (1 adas ~392 ms), kulonben ~60 s-onkent ismetli.
 *    - SET modbol kilepeskor azonnal mer es szabalyoz.
 *    - PAROSITAS: a vevon nyomd az M/A gombot, amig zolden villog, majd
 *      nyomd meg a termosztat LEARN gombjat (hatlap).  Kozben "--.-" latszik.
 *      A kontrollhoz hasonloan minden LEARN-nel UJ veletlen ID-t general
 *      (felengedes pillanata + ADC-zaj), PAIR adas, majd 1 s mulva az allapot.
 *      Az ID az adatflash-ben van (df_save/df_load): elemcserét is tulel,
 *      csak a kovetkezo parositaskor valtozik.  (rl78flash -a torli az adatflash-t!)
 *
 *  ALACSONY FOGYASZTAS: a CPU STOP modban alszik (IE=0 -> nincs interrupt-
 *  vektor; ebredes utan a STOP utani utasitassal folytatja).  Az LCD es a
 *  12 bites intervallum-idozito az fIL-rol (15 kHz) STOP-ban is jar (16-2 tabla).
 *    - INTIT 0.125 s-onkent ebreszt; 80 ebredes = 10 s -> meres (~15 ms aktiv)
 *    - INTKR (KR1 = P32 felso, KR2 = P31 also gomb, lefuto el) ebreszt;
 *      tartalekkent minden 0.125 s-os ebredeskor a gombok szintjet is nezzuk
 *    - meresek kozott az ADC-komparator KI, P144 = 1 (az osztoban nincs aram)
 *
 *  MODELL: 10k NTC, B=3950 (tablazat 5 C-onkent, linearis interpolacio),
 *  RT1 = Rs - RV_OFFSET, ahol RV_OFFSET a trimmer: a 23.2 C / 45.4 % pontbol
 *  Kontroll-termosztattal osszevetve:
 *    1164 ohm -> +0.3 C  (meg a folyton pörgo, panelt melegito firmware-rel!)
 *    1012 ohm -> -0.2 C,  1073 ohm -> -0.3 C  (STOP-os, alacsony fogyasztasu fw)
 *  A STOP-os meresekbol: 1073 + 0.3 C * 482 ohm/C (NTC dR/dT 23 C-on) = 1218 ohm.
 *  NTC igazolva (melegitesre no a T).
 *
 *  LCD: firmware/LCD_MAP.md (belso boost, 4-COM, 1/3 bias).  Const tabla OK
 *  (rl78.ld: .rodata @0x2000, near-tukor).  crt0 nem nullaz -> csak lokalisok.
 * ========================================================================== */
#include <stdint.h>

#define SFR8(a)   (*(volatile uint8_t  *)(a))
#define SFR16(a)  (*(volatile uint16_t *)(a))
/* LCD */
#define PER0     SFR8(0xF00F0u)   /* bit7 RTCEN, bit5 ADCEN */
#define OSMC     SFR8(0xF00F3u)
#define LCDM0    SFR8(0xFFF40u)
#define LCDM1    SFR8(0xFFF41u)
#define LCDC0    SFR8(0xFFF42u)
#define VLCD     SFR8(0xFFF43u)
#define ISCLCD   SFR8(0xF0308u)
#define PFSEG0   SFR8(0xF0300u)
#define PFSEG1   SFR8(0xF0301u)
#define PFSEG2   SFR8(0xF0302u)
#define PFSEG3   SFR8(0xF0303u)
#define PFSEG4   SFR8(0xF0304u)
#define SEGRAM   ((volatile uint8_t *)0xF0400u)
/* portok */
#define P1    SFR8(0xFFF01u)
#define P3    SFR8(0xFFF03u)
#define P5    SFR8(0xFFF05u)
#define P12   SFR8(0xFFF0Cu)
#define P14   SFR8(0xFFF0Eu)
#define PM1   SFR8(0xFFF21u)
#define PM2   SFR8(0xFFF22u)
#define PM3   SFR8(0xFFF23u)
#define PM5   SFR8(0xFFF25u)
#define PM12  SFR8(0xFFF2Cu)
#define PM14  SFR8(0xFFF2Eu)
#define PU3   SFR8(0xF0033u)
#define PU1   SFR8(0xF0031u)
#define RESF  SFR8(0xFFFA8u)   /* reset-ok: 80 TRAP, 10 WDT, 04 RAM-paritas, 02 illeg. hozzaferes, 01 LVD (olvasas torli) */
#define PU12  SFR8(0xF003Cu)
#define PMC14 SFR8(0xF006Eu)      /* 1 1 PMC145 PMC144 PMC143 PMC142 1 1 */
/* ADC (11. fejezet) */
#define ADCR  SFR16(0xFFF1Eu)     /* 10 bites eredmeny a felso 10 biten  */
#define ADM0  SFR8(0xFFF30u)      /* ADCS 0 FR2 FR1 FR0 LV1 LV0 ADCE     */
#define ADS   SFR8(0xFFF31u)
#define ADM1  SFR8(0xFFF32u)      /* ADTMD1 ADTMD0 ADSCM 0 0 0 ADTRS1 0  */
#define ADM2  SFR8(0xF0010u)      /* ADREFP1 ADREFP0 ADREFM 0 ADRCK AWC 0 ADTYP */
#define ADPC  SFR8(0xF0076u)
/* intervallum-idozito, key interrupt, IF/MK (8., 14., 15. fejezet) */
#define ITMC  SFR16(0xFFF90u)     /* RINTE(15) ITCMP11..0                 */
#define KRCTL SFR8(0xFFF34u)      /* KRMD(7) ... KREG(0)                  */
#define KRM0  SFR8(0xFFF37u)      /* KRM03..KRM00                         */
#define IF1L  SFR8(0xFFFE2u)      /* bit7 ITIF                            */
#define IF1H  SFR8(0xFFFE3u)      /* bit0 KRIF                            */
#define MK1L  SFR8(0xFFFE6u)      /* bit7 ITMK                            */
#define MK1H  SFR8(0xFFFE7u)      /* bit0 KRMK                            */
#define MK0L  SFR8(0xFFFE4u)
#define MK0H  SFR8(0xFFFE5u)
#define MK2L  SFR8(0xFFFD4u)
#define MK2H  SFR8(0xFFFD5u)
/* LEARN gomb (P70, pin24, KR0) */
#define P7    SFR8(0xFFF07u)
#define PM7   SFR8(0xFFF27u)
#define PU7   SFR8(0xF0037u)
/* TAU0 csatorna 0 (6. fejezet) -- csak az RF-adas idejere jar (STOP-ban tilos) */
#define TCR00 SFR16(0xF0180u)     /* szamlalo (lefele szamol)             */
#define TMR00 SFR16(0xF0190u)     /* mod                                  */
#define TDR00 SFR16(0xFFF18u)     /* periodus                             */
#define TS0   SFR16(0xF01B2u)     /* csatorna start                       */
#define TT0   SFR16(0xF01B4u)     /* csatorna stop                        */
#define TPS0  SFR16(0xF01B6u)     /* orajel-eloosztok                     */

#define IT_CMP      0x0752u   /* fIL 15 kHz: (1874+1)/15000 = 0.125 s      */
#define MEAS_WAKES  80u       /* 80 x 0.125 s = 10 s meresi periodus       */

/* ---------- kalibracio ---------- */
#define R_SERIES    10000L    /* R4 "01C" = 10.0 k                        */
#define RV_OFFSET   1218L     /* trimmer; 1073 ohm -0.3 C-ot mutatott -> +145 ohm */
/* 10k NTC, B=3950: R(T) ohm, T = -20, -15, ... 70 C */
#define NTC_T0      (-20)
#define NTC_N       19u
static const uint32_t NTC_R[NTC_N] = {
    105385, 77898, 58246, 44026, 33621, 25925, 20175, 15837, 12535, 10000,
    8037, 6506, 5301, 4348, 3588, 2978, 2486, 2086, 1760
};
/* R (ohm) -> T tized C-ban; tartomanyon kivul -1000 */
static int16_t ntc_t10(int32_t r){
    if (r <= 0 || (uint32_t)r > NTC_R[0] || (uint32_t)r < NTC_R[NTC_N-1u]) return -1000;
    uint8_t i = 0u;
    while (i < NTC_N-2u && (uint32_t)r < NTC_R[i+1u]) i++;
    uint32_t hi = NTC_R[i], lo = NTC_R[i+1u];
    int32_t t10 = (int32_t)(NTC_T0 + 5*(int16_t)i) * 10
                + (int32_t)(50u * (hi - (uint32_t)r) / (hi - lo));
    return (int16_t)t10;
}

/* ---------- kijelzo ---------- */
#define SEG_D1_ABCD 33u
#define SEG_D1_FGE  34u
#define SEG_D2_ABCD  1u
#define SEG_D2_FGE   0u
#define SEG_D3_ABCD  3u
#define SEG_D3_FGE   2u
#define SEG_ICON5    5u   /* bit0 ROOM, bit1 SET */
#define ICON_ROOM    0x01u
#define ICON_SET     0x02u
#define SEG_UNIT     6u
#define SEG_DEGDP    7u
#define UNIT_C       0x0Bu
/* 7-szegmens kod: also nibble = abcd-SEG, felso nibble = fge-SEG */
static const uint8_t DIGIT[10] = {0x5F,0x06,0x6B,0x2F,0x36,0x3D,0x7D,0x07,0x7F,0x3F};
#define CH_BLANK 0x00u
#define CH_DASH  0x20u        /* csak g */

#define LCDM0_BOOST 0x40u
#define LCDM0_VAL   0x4Du
#define VLCD_VAL    0x04u
#define LCDC0_VAL   0x05u

#define BUSY 900u
static void delay_ms(uint16_t ms){
    volatile uint16_t n;
    while (ms--) for (n=0;n<BUSY;n++) { }
}

static void put_code(uint8_t seg_abcd, uint8_t seg_fge, uint8_t code){
    SEGRAM[seg_abcd] = (uint8_t)(code & 0x0Fu);
    SEGRAM[seg_fge]  = (uint8_t)(code >> 4);
}

/* v10 = tized egysegben (-99 ... 999); kivul -> "--.-" */
static void show10(int16_t v10){
    uint8_t c1, c2, c3;
    if (v10 < -99 || v10 > 999){
        c1 = c2 = c3 = CH_DASH;
    } else {
        uint8_t neg = (uint8_t)(v10 < 0);
        uint16_t a = (uint16_t)(neg ? -v10 : v10);
        uint8_t d3 = (uint8_t)(a % 10u); a /= 10u;
        uint8_t d2 = (uint8_t)(a % 10u); a /= 10u;
        uint8_t d1 = (uint8_t)a;
        c1 = neg ? CH_DASH : (d1 ? DIGIT[d1] : CH_BLANK);
        c2 = DIGIT[d2];
        c3 = DIGIT[d3];
    }
    put_code(SEG_D1_ABCD, SEG_D1_FGE, c1);
    put_code(SEG_D2_ABCD, SEG_D2_FGE, c2);
    put_code(SEG_D3_ABCD, SEG_D3_FGE, c3);
}

/* ---------- portok + LCD ---------- */
static void port_config(void){
    PFSEG0 = 0xF0u;  PFSEG1 = 0x00u;  PFSEG2 = 0x00u;  PFSEG3 = 0x00u;
    PFSEG4 = 0x06u;                     /* SEG33, SEG34; SEG35 (P144) = port */
    PM1  = 0x1Fu;  P1  = 0x00u;
    PM5  = 0xFEu;  P5  = 0x00u;
    P14  = 0x10u;                       /* P144 = 1 (nyugalmi, aram nelkul)   */
    PMC14 = 0xE3u;                      /* P142,P143,P144 digitalis           */
    PM14 = 0xE1u;                       /* P142,P143 SEG-kimenet, P144 kimenet (SENS_DRV),
                                           P141 kimenet (RF DATA, alapbol 0 = vivo KI) */
    PM7 |= 0x01u;  PU7 |= 0x01u;        /* LEARN gomb P70 bemenet + felhuzo    */
    PU12 = 0x00u;  P12 = 0x00u;  PM12 = 0xFFu;
    ISCLCD = 0x00u;
    PM3 |= 0x06u;  PU3 |= 0x06u;        /* gombok P31 (also), P32 (felso)     */
    PM2 |= 0x01u;                       /* P20 = ANI0 bemenet                  */
    ADPC = 0x02u;                       /* ANI0 analog, ANI1 (P21) digitalis   */
}

static void lcd_init_boost(void){
    OSMC   = 0x10u;
    LCDM1  = 0x00u;
    LCDM0  = LCDM0_BOOST;
    port_config();
    VLCD   = VLCD_VAL;
    delay_ms(10);
    LCDM1  = 0x01u;
    LCDM1  = 0x21u;
    delay_ms(700);
    LCDM0  = LCDM0_VAL;
    LCDC0  = LCDC0_VAL;
    for (uint8_t i=0;i<8u;i++) SEGRAM[i] = 0x00u;
    SEGRAM[33] = 0x00u; SEGRAM[34] = 0x00u;
    LCDM1  = 0x61u;
    LCDM1  = 0xE1u;
}

/* ---------- ADC (11.7 beallitasi sorrend) ---------- */
static void adc_init(void){
    ADM0 = 0x00u;            /* ADCS=ADCE=0                                   */
    ADM0 = 0x0Cu;            /* FR=001 (fCLK/32), LV=10 (alacsony fesz. mod 1,
                                1.6 V-tol) -> konverzio ~76 us @8MHz           */
    ADM1 = 0x20u;            /* szoftveres trigger, EGYSZERI konverzio        */
    ADM2 = 0x00u;            /* +ref = VDD, -ref = VSS, 10 bit                 */
    ADS  = 0x00u;            /* ANI0                                          */
}

static uint16_t adc_read(void){
    ADM0 = 0x8Du;                          /* ADCS=1: start                   */
    while (ADM0 & 0x80u) { }               /* egyszeri modban magatol 0 lesz  */
    return (uint16_t)(ADCR >> 6);          /* 0..1023                         */
}

/* 64 minta osszege (0..65472) = n * 64 */
static uint16_t measure_sum64(void){
    P14 &= (uint8_t)~0x10u;                /* SENS_DRV = 0: oszto aktiv       */
    ADM0 = 0x0Du;                          /* ADCE=1: komparator be           */
    delay_ms(10);                          /* C9 beallas + >=1 us ADC-stabil. */
    uint16_t s = 0u;
    for (uint8_t i=0;i<64u;i++) s = (uint16_t)(s + adc_read());
    ADM0 = 0x0Cu;                          /* ADCE=0: komparator ki           */
    P14 |= 0x10u;                          /* SENS_DRV = 1: nincs aram        */
    return s;
}

/* S = 64-mintas osszeg (0..65472) -> Rs ohm */
static uint32_t sum_to_rs(uint32_t S){
    if (S < 64u) S = 64u;                  /* szakadt szenzor ellen (osztas)  */
    return (uint32_t)R_SERIES * (65536u - S) / S;
}

/* S -> homerseklet tized C-ban (-1000 = hiba / tartomanyon kivul) */
static int16_t temp10(uint32_t S){
    return ntc_t10((int32_t)sum_to_rs(S) - RV_OFFSET);
}

/* ---------- gombok ---------- */
#define KEY_UP 0x01u
#define KEY_DN 0x02u
static uint8_t keys_down(void){
    uint8_t k = P3, r = 0u;
    if ((k & 0x04u) == 0u) r |= KEY_UP;    /* P32 = felso gombok */
    if ((k & 0x02u) == 0u) r |= KEY_DN;    /* P31 = also gombok  */
    return r;
}

#define SP_MIN    180     /* 18.0 C */
#define SP_MAX    280     /* 28.0 C */
#define SP_START  210     /* 21.0 C */

#define LCD_ON()   (LCDM1 = 0xE1u)        /* SCOC=1, LCDON=1             */
#define LCD_OFF()  (LCDM1 = 0x61u)        /* SCOC=1, LCDON=0: ures kep   */

#define SEG_ICON4    4u   /* bit0 hopehely, bit1 A/C, bit2 HEAT */
#define ICON_HEAT    0x04u
static void show_heat(uint8_t heat){
    SEGRAM[SEG_ICON4] = heat ? ICON_HEAT : 0x00u;
}

static void show_room(int16_t t10){
    SEGRAM[SEG_ICON5] = ICON_ROOM;
    SEGRAM[SEG_UNIT]  = UNIT_C;
    show10(t10);
}

/* ---------- RF: Computherm Q3RF/Q7RF protokoll -- a KONTROLL-termosztatrol
 * rogzitve (2026-11-13, rf_capture_main.c, FOLYTATAS.md).
 * P141/pin48 -> 1k -> RF modul DATA, 1 = vivo BE.  Egy adas (~392 ms):
 *   1. lassu eloszo: 25 x (H 440 / L 440 us)
 *   2. 4 x lassu keret: szinkron H 2000 / L 2000, 40 bit PWM
 *        '1' = H 800 / L 400, '0' = H 400 / L 800, az utolso bit utan +400 us L
 *        bajtok: swap(ID_hi) swap(ID_lo) 0x80 SCMD XOR(elso 4)
 *        SCMD: PAIR 0x01, BE 0x80, KI 0x00
 *   3. 4 x gyors (esphome) burst, chip 222 us: 111000111 + 2 x (ID16 + 1000 + FCMD)
 *        '1' = 001, '0' = 011; burstok kozott L 1232 us
 *        FCMD: PAIR 0x00, BE 0xFF, KI 0x0F
 * LEARN a kontrollon: PAIR adas, majd ~1 s mulva (inditastol) az allapot.
 * Az ID-t a kontroll minden LEARN-nel veletlenul valasztja; nalunk fix.     */
/* Sajat ID: minden LEARN-nel UJ, veletlen 16 bites ertek (mint a kontrollnal).
 * RAM-ban el: resetet tul el (magic + ellenorzo), elemcserenel elveszik ->
 * ujra kell parositani.  A .bss-t a crt0 nem nullazza -- itt ez kifejezetten
 * hasznos, mert a reset utan megmarad az ertek.                              */
#define RF_ID_MAGIC 0x51335246UL  /* "Q3RF" */
static uint32_t rf_id_magic __attribute__((section(".noinit")));
static uint16_t rf_id __attribute__((section(".noinit")));
static uint16_t rf_id_chk __attribute__((section(".noinit")));        /* = ~rf_id; .noinit: a crt0 RAM-torlese kihagyja */
static uint32_t ent;              /* entropia-gyujto allapot */
static uint8_t rf_id_valid(void){
    return (uint8_t)(rf_id_magic == RF_ID_MAGIC && rf_id_chk == (uint16_t)~rf_id
                     && rf_id != 0x0000u && rf_id != 0xFFFFu);
}
/* entropia bekeverese (xorshift32 lepesekkel) */
static void ent_mix(uint16_t v){
    ent ^= v;
    for (uint8_t i=0;i<3u;i++){
        ent ^= ent << 13;  ent ^= ent >> 17;  ent ^= ent << 5;
    }
    if (ent == 0u) ent = 0x2545F491UL;
}
#define CMD_PAIR 0u
#define CMD_ON   1u
#define CMD_OFF  2u
#define RF_INV_DEFAULT 0u         /* 0 = DATA aktiv magas (1 = vivo BE) -- merve */

/* polaritas futasidoben (a vivo-teszt mindkettot kiprobalja).
 * .bss-ben van, a crt0 NEM nullazza -> main() az elejen beallitja!       */
static uint8_t rf_inv;

static void rf_level(uint8_t on){
    if ((uint8_t)(on ^ rf_inv)) P14 |= 0x02u; else P14 &= (uint8_t)~0x02u;
}

/* egy szint: beallitas, majd varakozas us mikroszekundumig (abszolut utemezes,
 * fCLK 8 MHz -> 8 tick/us; max ~4000 us az int16 osszehasonlitas miatt) */
static void rf_pulse(uint8_t on, uint16_t us, uint16_t *t){
    rf_level(on);
    *t = (uint16_t)(*t - (uint16_t)(us * 8u));
    while ((int16_t)(TCR00 - *t) > 0) { }
}
static void tau_start(void){
    PER0 |= 0x01u;                       /* TAU0EN                                 */
    TPS0  = 0x0000u;                     /* CK00 = fCLK/1 = 8 MHz                  */
    TMR00 = 0x0000u;                     /* CK00, szoftveres start, intervallum mod */
    TDR00 = 0xFFFFu;                     /* 65536 orajel periodus -> modularis szamlalo */
    TS0   = 0x0001u;                     /* start                                  */
    delay_ms(1);
}
static void tau_stop(void){
    TT0   = 0x0001u;                     /* stop (STOP modban a TAU nem jarhat)    */
    PER0 &= (uint8_t)~0x01u;
}

static uint8_t nswap(uint8_t v){ return (uint8_t)((uint8_t)(v << 4) | (uint8_t)(v >> 4)); }

/* lassu keret: szinkron + 40 bit */
static void rf_slow_frame(const uint8_t *by, uint16_t *t){
    rf_pulse(1u, 2000u, t); rf_pulse(0u, 2000u, t);
    for (uint8_t i=0;i<5u;i++){
        for (uint8_t m=0x80u; m; m >>= 1){
            uint8_t last = (uint8_t)(i == 4u && m == 1u);
            if (by[i] & m){ rf_pulse(1u, 800u, t); rf_pulse(0u, last ? 800u : 400u, t); }
            else          { rf_pulse(1u, 400u, t); rf_pulse(0u, last ? 1200u : 800u, t); }
        }
    }
}

/* gyors (esphome) burst */
#define CHIP_US 222u
static void esp_bit(uint8_t b, uint16_t *t){
    rf_pulse(0u, CHIP_US, t); rf_pulse(b ? 0u : 1u, CHIP_US, t); rf_pulse(1u, CHIP_US, t);
}
static void esp_payload(uint8_t cmd, uint16_t *t){
    for (uint16_t m = 0x8000u; m; m >>= 1) esp_bit((uint8_t)((rf_id & m) != 0u), t);
    esp_bit(1u, t); esp_bit(0u, t); esp_bit(0u, t); esp_bit(0u, t);          /* 1000 */
    for (uint8_t m = 0x80u; m; m >>= 1) esp_bit((uint8_t)((cmd & m) != 0u), t);
}
static void esp_burst(uint8_t cmd, uint16_t *t){
    rf_pulse(1u, 3u*CHIP_US, t); rf_pulse(0u, 3u*CHIP_US, t); rf_pulse(1u, 3u*CHIP_US, t);
    esp_payload(cmd, t);
    esp_payload(cmd, t);
}

/* egy teljes adas (~392 ms) */
static void rf_tx(uint8_t cmd){
    uint8_t scmd = (cmd == CMD_PAIR) ? 0x01u : (cmd == CMD_ON) ? 0x80u : 0x00u;
    uint8_t fcmd = (cmd == CMD_PAIR) ? 0x00u : (cmd == CMD_ON) ? 0xFFu : 0x0Fu;
    uint8_t by[5];
    by[0] = nswap((uint8_t)(rf_id >> 8));
    by[1] = nswap((uint8_t)rf_id);
    by[2] = 0x80u;
    by[3] = scmd;
    by[4] = (uint8_t)(by[0] ^ by[1] ^ by[2] ^ by[3]);

    tau_start();
    uint16_t t = TCR00;
    for (uint8_t i=0;i<25u;i++){ rf_pulse(1u, 440u, &t); rf_pulse(0u, 440u, &t); }
    for (uint8_t i=0;i<4u;i++) rf_slow_frame(by, &t);
    for (uint8_t i=0;i<4u;i++){
        if (i) rf_pulse(0u, 1232u, &t);
        esp_burst(fcmd, &t);
    }
    rf_level(0u);                        /* vivo KI */
    tau_stop();
}

static void rf_state(uint8_t heat){ rf_tx(heat ? CMD_ON : CMD_OFF); }

/* parositas: mint a kontroll LEARN-je -- PAIR, majd 1 s-mal kesobb az allapot */
static void rf_pair(uint8_t heat){
    rf_tx(CMD_PAIR);
    delay_ms(608);                       /* 1000 ms inditastol inditasig */
    rf_state(heat);
}

/* ---------- szabalyozas ---------- */
#define HYST        2       /* +-0.2 C: BE ha T <= SP-0.2, KI ha T >= SP+0.2 */
#define RESEND_MEAS 6u      /* allapot ujrakuldese 6 meresenkent = ~60 s      */

/* uj fut-allapot a mert T alapjan (hiszterezissel); hibas meresnel KI */
static uint8_t control(int16_t t10, int16_t sp, uint8_t heat){
    if (t10 == -1000) return 0u;
    if (t10 <= sp - HYST) return 1u;
    if (t10 >= sp + HYST) return 0u;
    return heat;
}

/* SET mod: aktiv ciklus 10 ms-os lepesekkel, kilepes 5 s tetlenseg utan */
static void set_mode(int16_t *sp){
    SEGRAM[SEG_ICON5] = ICON_SET;
    SEGRAM[SEG_UNIT]  = UNIT_C;
    show10(*sp);
    LCD_ON();
    uint8_t  hold = 0u;
    uint16_t idle = 0u, blink = 0u;
    for (;;){
        delay_ms(10);
        uint8_t k = keys_down();
        if (k == KEY_UP || k == KEY_DN){
            idle = 0u;
            if (hold < 255u) hold++;
            uint8_t step = 0u;
            if (hold == 3u) step = 1u;                              /* 30 ms pergesmentesites */
            else if (hold == 50u) step = 1u;                       /* 0.5 s utan ismetles    */
            else if (hold >= 65u){ step = 1u; hold = 50u; }        /* majd 150 ms-onkent     */
            if (step){
                if (k == KEY_UP && *sp < SP_MAX) *sp = (int16_t)(*sp + 5);
                if (k == KEY_DN && *sp > SP_MIN) *sp = (int16_t)(*sp - 5);
                show10(*sp);
                blink = 0u; LCD_ON();                              /* valtozaskor latszodjon */
            }
        } else {
            hold = 0u;
            if (k == 0u && ++idle >= 500u) break;    /* 5 s tetlenseg */
        }
        if (++blink == 150u) LCD_OFF();              /* 1.5 s be ...  */
        else if (blink >= 200u){ blink = 0u; LCD_ON(); }   /* ... 0.5 s ki = 2 s periodus */
    }
    LCD_ON();
}

/* ---------- ADATFLASH (1 KB, F1000H-F13FFH, 1 blokk) -- az ID tartos tarolasa ----------
 * A Renesas "PFDL T04 pico" konyvtar csak a boot-ROM flash-rutinjait hivja
 * (EFFF8H = parancs, EF004H = allapot, EF008H = lezaras); a parameterek az
 * F0800H-F0881H teruleten.  Ugyanezt csinaljuk sajat kodbol (a konyvtar GNU
 * valtozatanak visszafejtese alapjan, hirakuni45/RL78: data_flash_lib/pfdl.a).
 * FIGYELEM: a ROM-rutin a FFB00H-FFC89H RAM-ot hasznalja -> a .bss FFC90H-tol van
 * (rl78.ld).  Rekord (8 bajtos slotok, hozzafuzve; tele -> blokk-torles):
 *   5A idH idL ~idH ~idL A5 FF FF                                              */
#define DFLCTL     SFR8(0xF0090u)
#define FDL_ADDR   SFR16(0xF0800u)
#define FDL_MODE   SFR8(0xF0801u)
#define FDL_P2     SFR8(0xF0802u)
#define FDL_P3     SFR8(0xF0803u)
#define FDL_PTR    SFR16(0xF0804u)
#define FDL_CNT    SFR16(0xF0806u)
#define FDL_STATE  SFR8(0xF0880u)
#define FDL_CMD    SFR8(0xF0881u)
#define DF_BASE    ((volatile uint8_t *)0xF1000u)
#define DF_SLOTS   128u
#define FDL_BUSY   0xFFu

uint8_t fdl_fw(uint8_t cmd);       /* asm lent */
uint8_t fdl_hdl(void);
void    fdl_end(void);
/* ROM-hivas (EFFF8 / EF004 / EF008) a Renesas PFDL T04 (GNU V1.04, pfdl.o
 * visszafejtve) szerint.  Ket, MERESSEL igazolt hiba javitva (2026-11-25):
 *  1) a ROM a celcimet a DE regiszterbol veszi (a konyvtar DE = 0x1000+idx,
 *     B = darabszam felso bajt, C = parancs, HL = mutato toltessel hiv).
 *     Korabban DE-ben szemet volt -> a ROM a KODFLASH-be irt (0x0000-s szo
 *     serult, TRAP-reset).
 *  2) a T04 az FFE20-FFEFF teruletet hasznalja belso allapotra (R01AN1944),
 *     a GCC pedig ott tartja r8-r31-et (FFEE0-FFEF7) -> a ket tartalmat
 *     cserelgetjuk: a ROM-e a rom_rb-ben el a hivasok kozott.          */
uint16_t rom_rb[12];             /* a ROM FFEE0-FFEF7 allapota a hivasok kozott */
uint8_t  fdl_req[8] __attribute__((aligned(2)));  /* pfdl_request_t: idx16, data16, cnt16, cmd8 */
uint8_t fdl_call(uint8_t which, uint8_t cmd);
uint8_t fdl_fw(uint8_t cmd){ return fdl_call(0u, cmd); }
uint8_t fdl_hdl(void){ return fdl_call(1u, 0u); }
void    fdl_end(void){ (void)fdl_call(2u, 0u); }
__asm__(
"\t.text\n"
".global _fdl_call\n"
"_fdl_call:\n"
"\tpush ax\n"
"\tpush bc\n"
"\tpush de\n"
"\tpush hl\n"
"\tmovw hl, #0xfee0\n"
"\tmov b, #12\n"
".Lrc_s1:\n"
"\tmovw ax, [hl]\n"
"\tpush ax\n"
"\tincw hl\n"
"\tincw hl\n"
"\tdec b\n"
"\tbnz $.Lrc_s1\n"
"\tmovw hl, #0xfee0\n"
"\tmovw de, #_rom_rb\n"
"\tmov b, #12\n"
".Lrc_s2:\n"
"\tmovw ax, [de]\n"
"\tmovw [hl], ax\n"
"\tincw hl\n"
"\tincw hl\n"
"\tincw de\n"
"\tincw de\n"
"\tdec b\n"
"\tbnz $.Lrc_s2\n"
"\tmov a, [sp+36]\n"
"\tcmp0 a\n"
"\tbnz $.Lrc_n0\n"
"\tmov a, [sp+38]\n"
"\tmov c, a\n"
"\tcmp0 a\n"
"\tbnz $.Lrc_exe\n"
"\tmovw ax, !0xf0804\n"
"\tmovw hl, ax\n"
"\tbr $.Lrc_go\n"
".Lrc_exe:\n"
"\tmovw hl, #_fdl_req\n"
"\tmovw de, #0x1000\n"
"\tcmp a, #3\n"
"\tbz $.Lrc_er\n"
/* PFDL_Execute (T04) pontosan: DE = F0800 (cim), BC = F0806 (darab),
 * A = 0, X = darab also bajt, C = parancs.  (Korabban X = cim also bajt,
 * A = darab felso bajt volt -> az iras 0x10-zel tert vissza.)            */
"\tmov b, a\n"
"\tmovw ax, !0xf0800\n"
"\tmovw de, ax\n"
"\tmov a, b\n"
"\tcmp a, #4\n"
"\tmovw ax, !0xf0806\n"
"\tmovw bc, ax\n"
"\tmov a, #0\n"
"\tmov c, !0xf0881\n"
"\tbnz $.Lrc_go\n"
/* WRITE: a ROM EF87B-s irorutinja hibas (5 bajt nullazva) -> javitott masolat */
"\tcall !!_rom_wr_fix\n"
"\tbr $.Lrc_done\n"
".Lrc_er:\n"
/* erase: AX = F0800 (blokk-cim), DE = 0x1000, C = parancs (mint a T04) */
"\tmovw ax, !0xf0800\n"
"\tmov c, !0xf0881\n"
".Lrc_go:\n"
"\tcall !!0xEFFF8\n"
"\tbr $.Lrc_done\n"
".Lrc_n0:\n"
"\tcmp a, #1\n"
"\tbnz $.Lrc_n1\n"
"\tcall !!0xEF004\n"
"\tbr $.Lrc_done\n"
".Lrc_n1:\n"
"\tcall !!0xEF008\n"
".Lrc_done:\n"
"\tmovw hl, #0xfee0\n"
"\tmovw de, #_rom_rb\n"
"\tmov b, #12\n"
".Lrc_s3:\n"
"\tmovw ax, [hl]\n"
"\tmovw [de], ax\n"
"\tincw hl\n"
"\tincw hl\n"
"\tincw de\n"
"\tincw de\n"
"\tdec b\n"
"\tbnz $.Lrc_s3\n"
"\tmovw hl, #0xfef6\n"
"\tmov b, #12\n"
".Lrc_s4:\n"
"\tpop ax\n"
"\tmovw [hl], ax\n"
"\tdecw hl\n"
"\tdecw hl\n"
"\tdec b\n"
"\tbnz $.Lrc_s4\n"
"\tmov a, c\n"
"\tmov r8, a\n"
"\tpop hl\n"
"\tpop de\n"
"\tpop bc\n"
"\tpop ax\n"
"\tret\n"
);

/* JAVITOTT ADATFLASH-IRAS (2026-11-26, ROM-visszafejtes: firmware/rom_dump/).
 * Az R7F0C001 ROM-janak iro-handlere (EF87B) hibas: az elso 5 bajtja
 * "FE 00 00 00 00" a helyes "FE BE 00 AD 24" helyett, vagyis hianyzik
 *     call EF93C        ; tartomany (ffe5a..60), darab (ffe2e), ffe87.0 = ADATFLASH
 *     movw ax, ffe24    ; forras-mutato
 * -> az iras kodflashkent az FSW-hez merodik (0x10), vagy elavult cimre ir.
 * Ez a rutin a ROM EF7D6-os diszpecserenek pro-/epilogusat bajtra masolja
 * (F00A1 = 1: a ROM munkaterulete FFE20.. -> F0800.. atkapcsolva), kozte a
 * HELYES iro-torzzsel, a ROM tobbi rutinjat valtozatlanul hivja.
 * Belepes: mint az EFFF8-nal (C = 4, a parameterek F0800..F0807-ben).
 * Kilepes: C = statusz (0xFF = busy -> EF004 handlerrel pollozni).            */
__asm__(
"\t.text\n"
".global _rom_wr_fix\n"
"_rom_wr_fix:\n"
"\t.byte 0x61,0xDD\n"                 /* push psw                */
"\t.byte 0xE5,0xA1,0x00\n"            /* oneb !0xf00a1           */
"\t.byte 0xC1,0xC3,0xC5,0xC7\n"       /* push ax,bc,de,hl        */
"\t.byte 0x8E,0xFC,0x70\n"            /* mov a,cs ; mov x,a      */
"\t.byte 0x8E,0xFD,0xC1\n"            /* mov a,es ; push ax      */
/* FONTOS: a ROM saddr-munkaterulete (ffe20..) csak ROM-bol futva latszik
 * F0800..-ra athelyezve; innen (kodflashbol) a KOZVETLEN F08xx cimek kellenek:
 * ffe28=F0808, ffe2a=F080A, ffe2b=F080B, ffe24=F0804, ffe34=F0814,
 * ffe86..89=F0866..69, ffe88=F0868.  (saddr-ral a valodi FFE2x-et irtuk ->
 * forras-mutato = 0 -> 0x00 bajtok kerultek az adatflashbe.)               */
"\t.byte 0xAE,0xF8\n"                 /* movw ax,sp              */
"\t.byte 0xBF,0x08,0x08\n"            /* movw !F0808,ax          */
"\t.byte 0x41,0x0E\n"                 /* mov es,#14              */
"\t.byte 0xCE,0xFC,0x0E\n"            /* mov cs,#14              */
"\t.byte 0x62\n"                      /* mov a,c                 */
"\t.byte 0x9F,0x0A,0x08\n"            /* mov !F080A,a            */
"\t.byte 0xF5,0x66,0x08,0xF5,0x67,0x08,0xF5,0x68,0x08,0xF5,0x69,0x08\n" /* clrb !F0866..69 */
"\t.byte 0xCF,0x0B,0x08,0x00\n"       /* mov !F080B,#0           */
/* --- javitott iro-torzs --- */
"\t.byte 0xFC,0x3C,0xF9,0x0E\n"       /* call !!0xEF93C          */
"\t.byte 0xAF,0x04,0x08\n"            /* movw ax,!F0804 (ffe24)  */
"\t.byte 0xBF,0x14,0x08\n"            /* movw !F0814,ax (ffe34)  */
"\t.byte 0xFC,0x7B,0xFA,0x0E\n"       /* call !!0xEFA7B (vedelem) */
"\t.byte 0xDC,0x0C\n"                 /* bc $+12 (hiba: kihagy)  */
"\t.byte 0xFC,0x94,0xFD,0x0E\n"       /* call !!0xEFD94          */
"\t.byte 0x71,0x20,0x68,0x08\n"       /* set1 !F0868.2 (ffe88.2) */
"\t.byte 0xFC,0xD7,0xFC,0x0E\n"       /* call !!0xEFCD7 (inditas) */
/* --- epilogus (ROM EF826-EF83B, kozvetlen cimekkel) --- */
"\t.byte 0xAF,0x08,0x08,0xBE,0xF8\n"  /* movw ax,!F0808 ; movw sp,ax */
"\t.byte 0xC0,0x9E,0xFD\n"            /* pop ax ; mov es,a       */
"\t.byte 0x60,0x9E,0xFC\n"            /* mov a,x ; mov cs,a      */
"\t.byte 0xC6,0xC4,0xC2,0xC0\n"       /* pop hl,de,bc,ax         */
"\t.byte 0xF9,0x0B,0x08\n"            /* mov c,!F080B            */
"\t.byte 0xF5,0xA1,0x00\n"            /* clrb !0xf00a1           */
"\t.byte 0x61,0xCD\n"                 /* pop psw                 */
"\t.byte 0xD7\n"                      /* ret                     */
);

/* a ROM-rutin szavanként olvassa -> PAROS cimre kell (paratlanon elakad!) */
static uint8_t fdl_desc[2] __attribute__((aligned(2)));
static uint8_t df_rec[8]   __attribute__((aligned(2)));

static void df_read_enable(void){
    DFLCTL |= 0x01u;
    __asm__ volatile ("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop");   /* >= 720 ns (LS) */
}
static uint8_t df_open(void){
    df_read_enable();
    fdl_desc[0] = 8u;                     /* fCLK = 8 MHz                       */
    fdl_desc[1] = 1u;                     /* wide-voltage mod (LS mod, 000C2H)  */
    FDL_PTR  = (uint16_t)(uintptr_t)fdl_desc;
    FDL_MODE = fdl_desc[1];
    FDL_P3   = 1u;
    uint8_t st = fdl_fw(0u);
    FDL_STATE = 4u;
    return st;
}
static void df_close(void){
    fdl_end();
    DFLCTL &= (uint8_t)~0x01u;
    FDL_STATE = 0u;
}
extern uint8_t fdl_req[8];
/* cmd: 3 = blokk-torles (idx = blokk), 4 = iras, 6 = belso verify, 8 = blank check
 * (idx = bajt-offset) -- a Renesas R01AN1362 minta (r_pfdl.c) sorrendje szerint */
static uint8_t df_exec(uint8_t cmd, uint16_t idx, uint8_t *data, uint16_t cnt){
    FDL_P3  = 1u;
    FDL_PTR = (uint16_t)(uintptr_t)data;
    FDL_P2  = 15u;
    FDL_CMD = cmd;
    fdl_req[0] = (uint8_t)idx;  fdl_req[1] = (uint8_t)(idx >> 8);
    fdl_req[2] = (uint8_t)(uintptr_t)data;  fdl_req[3] = (uint8_t)((uintptr_t)data >> 8);
    fdl_req[4] = (uint8_t)cnt;  fdl_req[5] = (uint8_t)(cnt >> 8);
    fdl_req[6] = cmd;  fdl_req[7] = 0u;
    if (cmd == 3u){ FDL_CNT = 0x400u; FDL_ADDR = (uint16_t)(0x1000u + ((idx & 0xFFu) << 10)); }
    else          { FDL_ADDR = (uint16_t)(0x1000u + idx); FDL_CNT = cnt; }
    uint8_t st = fdl_fw(cmd);
    while (st == FDL_BUSY) st = fdl_hdl();
    return st;
}
static uint8_t df_slot_blank(uint8_t k){
    volatile uint8_t *p = DF_BASE + ((uint16_t)k << 3);
    for (uint8_t i=0;i<8u;i++) if (p[i] != 0xFFu) return 0u;
    return 1u;
}
static uint8_t df_slot_id(uint8_t k, uint16_t *id){
    volatile uint8_t *p = DF_BASE + ((uint16_t)k << 3);
    if (p[0] != 0x5Au || p[5] != 0xA5u) return 0u;
    if ((uint8_t)(p[1] ^ p[3]) != 0xFFu || (uint8_t)(p[2] ^ p[4]) != 0xFFu) return 0u;
    *id = (uint16_t)(((uint16_t)p[1] << 8) | p[2]);
    return (uint8_t)(*id != 0x0000u && *id != 0xFFFFu);
}
/* utolso ervenyes rekord (DFLEN-nel olvasva); 1 = talalt */
static uint8_t df_load(uint16_t *id){
    uint8_t found = 0u;
    df_read_enable();
    for (uint8_t k=0;k<DF_SLOTS;k++){
        if (df_slot_blank(k)) break;
        uint16_t v;
        if (df_slot_id(k, &v)){ *id = v; found = 1u; }
    }
    DFLCTL &= (uint8_t)~0x01u;
    return found;
}
/* uj rekord hozzafuzese (tele -> torles); 0 = OK, kulonben hibakod */
static uint8_t df_save(uint16_t id){
    /* minden megszakitas maszkolva a ROM-hivasok idejere (a Renesas-minta
     * teljes vektortablaval fut; nalunk IE=0, de a ROM-rutin kozben elfogadott
     * megszakitas TRAP-et okozott) -- utana visszaallitjuk                   */
    uint8_t m0l = MK0L, m0h = MK0H, m1l = MK1L, m1h = MK1H, m2l = MK2L, m2h = MK2H;
    __asm__ volatile ("di");
    MK0L = 0xFFu; MK0H = 0xFFu; MK1L = 0xFFu; MK1H = 0xFFu; MK2L = 0xFFu; MK2H = 0xFFu;
    uint8_t st = df_open();
    if (st == 0u){
        uint8_t k = 0u;
        while (k < DF_SLOTS && !df_slot_blank(k)) k++;
        if (k < DF_SLOTS){
            /* blank check a ROM-mal is (margin): ha nem ures -> torles */
            uint8_t bc = df_exec(8u, (uint16_t)((uint16_t)k << 3), df_rec, 8u);
            if (bc != 0u) k = DF_SLOTS;
        }
        if (k >= DF_SLOTS){ st = df_exec(3u, 0u, df_rec, 0u); k = 0u; }
        if (st == 0u){
            df_rec[0] = 0x5Au;  df_rec[1] = (uint8_t)(id >> 8);  df_rec[2] = (uint8_t)id;
            df_rec[3] = (uint8_t)~df_rec[1];  df_rec[4] = (uint8_t)~df_rec[2];
            df_rec[5] = 0xA5u;  df_rec[6] = 0xFFu;  df_rec[7] = 0xFFu;
            st = df_exec(4u, (uint16_t)((uint16_t)k << 3), df_rec, 8u);
            if (st == 0u) st = df_exec(6u, (uint16_t)((uint16_t)k << 3), df_rec, 8u);  /* iverify */
            uint16_t v = 0u;
            if (st == 0u && !(df_slot_id(k, &v) && v == id)) st = 0x80u;   /* visszaolvasas */
        }
    }
    df_close();
    __asm__ volatile ("di");             /* ha a ROM EI-t hagyott volna        */
    MK0L = m0l; MK0H = m0h; MK1L = m1l; MK1H = m1h; MK2L = m2l; MK2H = m2h;
    return st;
}

/* ---------- ESP32-KAPCSOLAT: UART0 (P12 = TxD0 pin44, P11 = RxD0 pin45), 38400 8N1 ----------
 * Ebresztes: az ESP32 a P30/KR3 (TP1, pin21) labat >= 1 ms-ra lehuzza, majd
 * 5 ms-on belul kuldi a parancsot.  Nyugalomban az ESP32 MAGASAN tartja
 * (P30 = 0 tartosan -> nincs ESP32 / nincs tapja -> nem adunk neki).
 * Sorok (NMEA-szeru, CS = a '$' es '*' kozotti karakterek XOR-ja, 2 hex):
 *   ESP32 -> RL78:  $Q*CS              lekerdezes
 *                   $P,<sp10>*CS       beallitott ertek (180..280, 0,5 C-os lepes)
 *                   $L*CS              parositas (uj ID + PAIR adas)
 *   RL78 -> ESP32:  $S,<t10>,<sp10>,<heat>,<ID hex4>*CS     allapot
 *                   (t10 = -1000: hibas meres) -- minden parancs utan es
 *                   minden meres utan (10 s), ha az ESP32 jelen van
 *                   $E,<kod>*CS        hiba (1 = checksum/formatum, 2 = ismeretlen parancs)
 * Egy RF-adas ~0,4 s (parositas ~1,4 s, SP-allitas 2 s kijelzes): az ESP32
 * valasz nelkul 3 s mulva ujraprobalja.                                      */
#define SPS0   SFR16(0xF0126u)
#define SMR00  SFR16(0xF0110u)
#define SMR01  SFR16(0xF0112u)
#define SCR00  SFR16(0xF0118u)
#define SCR01  SFR16(0xF011Au)
#define SSR00  SFR16(0xF0100u)
#define SSR01  SFR16(0xF0102u)
#define SIR01  SFR16(0xF010Au)
#define SS0    SFR16(0xF0122u)
#define ST0    SFR16(0xF0124u)
#define SO0    SFR16(0xF0128u)
#define SOE0   SFR16(0xF012Au)
#define SOL0   SFR16(0xF0134u)
#define SDR00  SFR16(0xFFF10u)
#define SDR01  SFR16(0xFFF12u)
#define TXD0   SFR8(0xFFF10u)
#define RXD0   SFR8(0xFFF12u)
#define NFEN0  SFR8(0xF0070u)
/* A TxD0 (P12) csak akkor lesz kimenet, ha mar jott ervenyes sor az ESP32-tol:
 * ESP32 nelkul (vagy bent hagyott jumperrel) a lab Hi-Z marad, nem terhel. */
static uint8_t esp_on;
#define ESP_PRESENT() (esp_on && (P3 & 0x01u))

static void uart0_init(void){
    PER0 |= 0x04u;                       /* SAU0EN                                  */
    SPS0  = 0x0000u;                     /* CK00 = fCLK = 8 MHz                      */
    ST0   = 0x0003u;
    SMR00 = 0x0022u;                     /* UART adas                                */
    SCR00 = 0x8097u;                     /* TXE, LSB elol, 1 stop, 8 bit, nincs paritas */
    SDR00 = (uint16_t)(103u << 9);       /* 8 MHz / (2 * 104) = 38462 baud           */
    SMR01 = 0x0122u;                     /* UART vetel, RxD lefuto el inditja        */
    SCR01 = 0x4097u;                     /* RXE                                      */
    SDR01 = (uint16_t)(103u << 9);
    NFEN0 |= 0x01u;                      /* RxD0 zajszuro                            */
    SIR01 = 0x0007u;
    SOL0 &= (uint16_t)~0x0001u;
    SO0  |= 0x0001u;
    SOE0 |= 0x0001u;
    SS0   = 0x0003u;                     /* P12 egyelore bemenet (Hi-Z), lasd esp_enable */
    /* Belso felhuzok a SAJAT VDD-rol: az ESP32 TX-e open-drain, az RX-en nincs
     * ESP-oldali felhuzo -> az ESP32 nem taplalja vissza az RL78-at (elem nelkul
     * 1,8 V volt a VDD-n -> LVD-kuszobon billego chip -> serult kodflash).
     * A felhuzo csak bemeneti modban el: P12-n az esp_enable() utan hatastalan. */
    PU1  |= 0x06u;                       /* P11 (RxD0), P12 (TxD0, amig Hi-Z)        */
}
static void esp_enable(void){
    if (esp_on) return;
    P1   |= 0x04u;                       /* P12 nyugalmi magas                       */
    PM1  &= (uint8_t)~0x04u;             /* P12 kimenet (TxD0)                       */
    esp_on = 1u;
}
static void uart0_putc(uint8_t c){
    while (SSR00 & 0x0020u) { }          /* BFF: puffer foglalt                      */
    TXD0 = c;
}
static void uart0_flush(void){ while (SSR00 & 0x0060u) { } }   /* BFF | TSF */

static uint8_t esp_cs;
static void esp_out(uint8_t c){ esp_cs ^= c; uart0_putc(c); }
static void esp_begin(uint8_t type){ uart0_putc('$'); esp_cs = 0u; esp_out(type); }
static void esp_num(int16_t v){
    char b[6]; uint8_t i = 0u;
    uint16_t u;
    esp_out(',');
    if (v < 0){ esp_out('-'); u = (uint16_t)(-v); } else u = (uint16_t)v;
    do { b[i++] = (char)('0' + (uint8_t)(u % 10u)); u = (uint16_t)(u / 10u); } while (u && i < 5u);
    while (i) esp_out((uint8_t)b[--i]);
}
static uint8_t hexd(uint8_t v){ v &= 15u; return (uint8_t)(v < 10u ? (uint8_t)('0' + v) : (uint8_t)('A' + v - 10u)); }
static void esp_end(void){
    uint8_t c = esp_cs;
    uart0_putc('*'); uart0_putc(hexd((uint8_t)(c >> 4))); uart0_putc(hexd(c));
    uart0_putc('\r'); uart0_putc('\n');
    uart0_flush();
}
static void esp_status(int16_t t10, int16_t sp, uint8_t heat){
    if (!ESP_PRESENT()) return;
    esp_begin('S'); esp_num(t10); esp_num(sp); esp_num((int16_t)heat);
    esp_out(',');
    esp_out(hexd((uint8_t)(rf_id >> 12))); esp_out(hexd((uint8_t)(rf_id >> 8)));
    esp_out(hexd((uint8_t)(rf_id >> 4)));  esp_out(hexd((uint8_t)rf_id));
    esp_end();
}
static void esp_error(uint8_t code){
    if (!ESP_PRESENT()) return;
    esp_begin('E'); esp_num((int16_t)code); esp_end();
}

/* egy sor vetele '$'-tol '*CS'-ig, timeout_ms-on belul.
 * Visszaad: 1..ESP_LINE_MAX = a '$' es '*' kozotti hossz (ellenorzott CS),
 * 0 = timeout, 0xFF = hibas sor                                              */
#define ESP_LINE_MAX 24u
static uint8_t esp_line[ESP_LINE_MAX];
static uint8_t hexv(uint8_t c){
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10u);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10u);
    return 0xFFu;
}
static uint8_t esp_read_line(uint16_t timeout_ms){
    uint8_t st = 0u, n = 0u, cs = 0u, h = 0u, rx = 0u;   /* st: 0 = '$'-ra var, 1 = adat, 2/3 = CS */
    uint32_t left = (uint32_t)timeout_ms * 8000u;          /* 8 MHz tick */
    SIR01 = 0x0007u;
    tau_start();
    uint16_t last = TCR00;
    for (;;){
        uint16_t now = TCR00;
        uint16_t d = (uint16_t)(last - now);  last = now;
        if (d >= left) break;
        left -= d;
        uint8_t ssr = (uint8_t)SSR01;                 /* egyszer olvasva, 8 biten */
        if (ssr & 0x07u){ SIR01 = 0x0007u; st = 0u; continue; }   /* keret/tulcsordulas */
        if (!(ssr & 0x20u)) continue;
        uint8_t c = RXD0;
        if (c == '$'){ st = 1u; n = 0u; cs = 0u; continue; }
        if (st == 1u){
            if (c == '*'){ st = 2u; continue; }
            if (n >= ESP_LINE_MAX){ st = 0u; continue; }
            esp_line[n++] = c;  cs ^= c;
        } else if (st == 2u){
            h = hexv(c);  st = 3u;
        } else if (st == 3u){
            uint8_t l = hexv(c);
            rx = (h != 0xFFu && l != 0xFFu && (uint8_t)((h << 4) | l) == cs && n) ? n : 0xFFu;
            break;
        }
    }
    tau_stop();
    return rx;
}
/* tizedes szam a sorban pos-tol; *ok = 0, ha nincs szam */
static int16_t esp_parse_num(uint8_t pos, uint8_t n, uint8_t *ok){
    int16_t v = 0; uint8_t neg = 0u, any = 0u;
    if (pos < n && esp_line[pos] == '-'){ neg = 1u; pos++; }
    while (pos < n && esp_line[pos] >= '0' && esp_line[pos] <= '9' && v < 3000){
        v = (int16_t)(v * 10 + (esp_line[pos] - '0')); pos++; any = 1u;
    }
    *ok = (uint8_t)(any && pos == n);
    return neg ? (int16_t)-v : v;
}

/* ADC-zaj (az NTC-oszto nyers mintainak also bitjei) az entropiaba */
static void ent_adc(void){
    P14 &= (uint8_t)~0x10u;
    ADM0 = 0x0Du;
    delay_ms(2);
    for (uint8_t i=0;i<32u;i++) ent_mix(adc_read());
    ADM0 = 0x0Cu;
    P14 |= 0x10u;
}
/* uj ID: az elozotol kulonbozo, nem 0000/FFFF */
static void rf_new_id(void){
    ent_adc();
    uint16_t old = rf_id_valid() ? rf_id : 0u;
    uint16_t id;
    do {
        ent_mix(0x9E37u);
        id = (uint16_t)((uint16_t)(ent >> 16) ^ (uint16_t)ent);
    } while (id == 0x0000u || id == 0xFFFFu || id == old);
    rf_id = id;  rf_id_chk = (uint16_t)~id;  rf_id_magic = RF_ID_MAGIC;
}

/* parositas: uj ID, mentes az adatflash-be, PAIR + allapot adas ("-1-" latszik) */
static void do_pairing(uint8_t heat){
    put_code(SEG_D1_ABCD, SEG_D1_FGE, CH_DASH);
    put_code(SEG_D2_ABCD, SEG_D2_FGE, DIGIT[1]);
    put_code(SEG_D3_ABCD, SEG_D3_FGE, CH_DASH);
    rf_new_id();                         /* minden parositas elott UJ ID */
    rf_pair(heat);                       /* ELOBB az RF-adas, utana a flash-iras */
    uint8_t st = df_save(rf_id);         /* tartosan (elemcsere) */
    if (st != 0u){                       /* hiba: kod 2 s-ig (pl. "12.8" = 0x80 = visszaolvasasi hiba) */
        show10((int16_t)st);
        delay_ms(2000);
    }
}

int main(void){
    rf_inv = RF_INV_DEFAULT;
    esp_on = 0u;
    delay_ms(300);                         /* boot-ablak a programozonak      */
    PER0 |= 0xA0u;                         /* RTCEN (LCD + int.idozito) + ADCEN */
    lcd_init_boost();
    adc_init();
    /* ID: adatflash-bol (elemcseret is tulel; a LEARN-parositas menti).
     * Ha ott nincs (pl. rl78flash -a torolte): a .noinit RAM-ban meg ervenyes
     * ID (meleg reset / flasheles utan), kulonben uj veletlen -> parositas kell. */
    {
        uint16_t id;
        if (df_load(&id)){ rf_id = id;  rf_id_chk = (uint16_t)~id;  rf_id_magic = RF_ID_MAGIC; }
        else if (!rf_id_valid()) rf_new_id();
    }
    SEGRAM[SEG_DEGDP] = 0x01u;             /* ° + tizedespont                 */

    /* ebresztesi forrasok (IE=0 marad -> nincs vektor, csak STOP-feloldas) */
    ITMC  = (uint16_t)(0x8000u | IT_CMP);  /* RINTE=1, 0.125 s                */
    KRCTL = 0x00u;                         /* KREG=0: lefuto el, KRMD=0 (0x01 = felfuto volt!) */
    PU3  |= 0x01u;                         /* P30 (ESP32-ebresztes) felhuzo          */
    PM3  |= 0x01u;
    uart0_init();
    esp_enable();                          /* TxD0 kimenet (csak ESP32 jelenleteben ad) */
    KRM0  = 0x0Fu;                         /* KR0 (P70 LEARN), KR1 (P32), KR2 (P31), KR3 (P30 = ESP32) */
    delay_ms(2);
    IF1L &= (uint8_t)~0x80u;  IF1H &= (uint8_t)~0x01u;
    MK1L &= (uint8_t)~0x80u;  MK1H &= (uint8_t)~0x01u;   /* ITMK=0, KRMK=0 */

    int16_t  sp   = SP_START;
    uint32_t filt = (uint32_t)measure_sum64() << 1;     /* simitott S*2     */
    int16_t  t10  = temp10(filt >> 1);
    uint8_t  heat = control(t10, sp, 0u);
    uint8_t  wakes = 0u, resend = 0u;
    show_room(t10);
    show_heat(heat);
    rf_state(heat);             /* indulaskor szinkron */
    esp_status(t10, sp, heat);

    for (;;){
        IF1L &= (uint8_t)~0x80u;           /* ITIF torles (kulonben a STOP azonnal kilepne) */
        if (!(IF1H & 0x01u)){              /* KR-el ebren erkezett? akkor NEM toroljuk, */
            ST0 = 0x0003u;                 /* hanem STOP nelkul feldolgozzuk.           */
            __asm__ volatile ("stop");     /* SAU STOP elott le (STOP-ban nem mukodik), */
            __asm__ volatile ("nop");
            SS0 = 0x0003u;                 /* ... ebredes utan ujra indul               */
        }
        uint8_t kr = (uint8_t)(IF1H & 0x01u);   /* KRIF atveve, torolve: ami ezutan  */
        IF1H &= (uint8_t)~0x01u;                /* jon, a kovetkezo korben latszik   */

        uint8_t do_meas = 0u;

        if ((P7 & 0x01u) == 0u){           /* LEARN: parositas a vevovel */
            delay_ms(30);
            if ((P7 & 0x01u) == 0u){
                /* mennyi ideig tartjak nyomva? (max 3 s merve)
                 * 5 ms-os lepesek a TAU-val; a felengedes PONTOS pillanata
                 * (8 MHz-es szamlalo) az entropia fo forrasa.                 */
                tau_start();
                uint16_t held = 0u, mark = TCR00;
                while ((P7 & 0x01u) == 0u && held < 600u){
                    if ((uint16_t)(mark - TCR00) >= 40000u){ mark = (uint16_t)(mark - 40000u); held++; }
                }
                uint16_t rel = TCR00;
                tau_stop();
                ent_mix(rel);  ent_mix(held);

                do_pairing(heat);        /* "-1-" latszik, amig ad */
                while ((P7 & 0x01u) == 0u) delay_ms(10);
                show_room(t10);
                esp_status(t10, sp, heat);
            }
            IF1H &= (uint8_t)~0x01u;       /* a LEARN-gomb sajat KR-eleit eldobjuk */
            continue;
        }
        /* ESP32-parancs: KR-ebresztes, de se gomb, se LEARN -> P30 (ESP32) volt */
        if (kr && keys_down() == 0u && (P7 & 0x01u)){
            uint8_t n = esp_read_line(60u);
            esp_enable();
            if (n == 0xFFu){
                esp_error(1u);
                continue;
            }
            if (n != 0u){
                uint8_t c = esp_line[0], ok = 0u;
                kr = 0u;                                   /* az ebresztest az ESP32 hasznalta el */
                if (c == 'Q' && n == 1u){
                    esp_status(t10, sp, heat);
                } else if (c == 'P' && n >= 3u && esp_line[1] == ','){
                    int16_t v = esp_parse_num(2u, n, &ok);
                    if (!ok){ esp_error(1u); continue; }
                    if (v < SP_MIN) v = SP_MIN;
                    if (v > SP_MAX) v = SP_MAX;
                    v = (int16_t)(((v + 2) / 5) * 5);          /* 0,5 C-os lepes */
                    sp = v;
                    esp_status(t10, sp, heat);                 /* AZONNALI nyugta, a 2 s elott */
                    SEGRAM[SEG_ICON5] = ICON_SET;  show10(sp);  /* 2 s visszajelzes */
                    delay_ms(2000);
                    do_meas = 1u;                              /* azonnali szabalyozas + allapot */
                } else if (c == 'L' && n == 1u){
                    do_pairing(heat);
                    show_room(t10);
                    esp_status(t10, sp, heat);
                } else {
                    esp_error(2u);
                }
                if (!do_meas) continue;
            }
            /* n == 0: nem jott sor -> gomb-prelles lehetett, lent kezeljuk */
        }
        if (kr || keys_down()){                          /* fel/le gomb (interrupt vagy szint) */
            delay_ms(30);
            if (keys_down()){
                set_mode(&sp);
                IF1H &= (uint8_t)~0x01u;   /* a gombnyomasok sajat KR-eleit eldobjuk */
                do_meas = 1u;              /* visszateres: friss meres + szabalyozas */
            } else continue;
        }
        if (IF1L & 0x80u){                 /* 0.125 s tick */
            if (++wakes >= MEAS_WAKES) do_meas = 1u;
        }
        if (do_meas){
            wakes = 0u;
            uint16_t s = measure_sum64();
            filt = filt - (filt >> 1) + s;              /* IIR 1/2 (~20 s idoallando) */
            t10  = temp10((filt + 1u) >> 1);
            show_room(t10);
            uint8_t want = control(t10, sp, heat);
            if (want != heat){                          /* valtas: azonnal */
                heat = want;
                show_heat(heat);
                rf_state(heat);
                resend = 0u;
            } else if (++resend >= RESEND_MEAS){        /* ~60 s-onkent ismetles */
                resend = 0u;
                rf_state(heat);
            }
            esp_status(t10, sp, heat);
        }
    }
}
