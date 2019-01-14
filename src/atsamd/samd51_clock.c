// Code to setup peripheral clocks on the SAMD51
//
// Copyright (C) 2018-2019  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "compiler.h" // DIV_ROUND_CLOSEST
#include "internal.h" // enable_pclock

// The "generic clock generators" that are configured
#define CLKGEN_MAIN 0
#define CLKGEN_200M 1
#define CLKGEN_32K 2
#define CLKGEN_48M 3
#define CLKGEN_2M 4
#define CLKGEN_100M 5

#define FREQ_MAIN 120000000
#define FREQ_200M 200000000
#define FREQ_32K 32768
#define FREQ_48M 48000000
#define FREQ_2M 2000000
#define FREQ_100M 100000000

// Configure a clock generator using a given source as input
static inline void
gen_clock(uint32_t clkgen_id, uint32_t flags)
{
    GCLK->GENCTRL[clkgen_id].reg = flags | GCLK_GENCTRL_GENEN;
    while (GCLK->SYNCBUSY.reg & GCLK_SYNCBUSY_GENCTRL(clkgen_id))
        ;
}

// Route a peripheral clock to a given clkgen
static inline void
route_pclock(uint32_t pclk_id, uint32_t clkgen_id)
{
    uint32_t val = GCLK_PCHCTRL_GEN(clkgen_id) | GCLK_PCHCTRL_CHEN;
    GCLK->PCHCTRL[pclk_id].reg = val;
    while (GCLK->PCHCTRL[pclk_id].reg != val)
        ;
}

// Enable a peripheral clock and power to that peripheral
void
enable_pclock(uint32_t pclk_id, uint32_t pm_id)
{
    uint32_t clkgen_id = CLKGEN_100M;
    if (pclk_id == TC0_GCLK_ID || pclk_id == TC1_GCLK_ID)
        clkgen_id = CLKGEN_200M;
    else if (pclk_id == USB_GCLK_ID)
        clkgen_id = CLKGEN_48M;
    route_pclock(pclk_id, clkgen_id);
    uint32_t pm_port = pm_id / 32, pm_bit = 1 << (pm_id % 32);
    (&MCLK->APBAMASK.reg)[pm_port] |= pm_bit;
}

// Return the frequency of the given peripheral clock
uint32_t
get_pclock_frequency(uint32_t pclk_id)
{
    if (pclk_id == TC0_GCLK_ID || pclk_id == TC1_GCLK_ID)
        return FREQ_200M;
    else if (pclk_id == USB_GCLK_ID)
        return FREQ_48M;
    return FREQ_100M;
}

// Initialize the clocks using an external 32K crystal
static void
clock_init_32k(void)
{
    // Enable external 32Khz crystal and route to CLKGEN_32K
    uint32_t val = (OSC32KCTRL_XOSC32K_ENABLE | OSC32KCTRL_XOSC32K_EN32K
                    | OSC32KCTRL_XOSC32K_CGM_XT | OSC32KCTRL_XOSC32K_XTALEN);
    OSC32KCTRL->XOSC32K.reg = val;
    while (!(OSC32KCTRL->STATUS.reg & OSC32KCTRL_STATUS_XOSC32KRDY))
        ;
    gen_clock(CLKGEN_32K, GCLK_GENCTRL_SRC_XOSC32K);

    // Generate 120Mhz clock on PLL0 (with CLKGEN_32K as reference)
    route_pclock(OSCCTRL_GCLK_ID_FDPLL0, CLKGEN_32K);
    uint32_t mul = DIV_ROUND_CLOSEST(FREQ_MAIN, FREQ_32K);
    OSCCTRL->Dpll[0].DPLLRATIO.reg = OSCCTRL_DPLLRATIO_LDR(mul - 1);
    while (OSCCTRL->Dpll[0].DPLLSYNCBUSY.reg & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO)
        ;
    OSCCTRL->Dpll[0].DPLLCTRLB.reg = (OSCCTRL_DPLLCTRLB_REFCLK_GCLK
                                      | OSCCTRL_DPLLCTRLB_LBYPASS);
    OSCCTRL->Dpll[0].DPLLCTRLA.reg = OSCCTRL_DPLLCTRLA_ENABLE;
    uint32_t mask = OSCCTRL_DPLLSTATUS_CLKRDY | OSCCTRL_DPLLSTATUS_LOCK;
    while ((OSCCTRL->Dpll[0].DPLLSTATUS.reg & mask) != mask)
        ;

    // Switch main clock to 120Mhz PLL0
    gen_clock(CLKGEN_MAIN, GCLK_GENCTRL_SRC_DPLL0);

    // Generate 200Mhz clock on PLL1 (with CLKGEN_32K as reference)
    route_pclock(OSCCTRL_GCLK_ID_FDPLL1, CLKGEN_32K);
    mul = DIV_ROUND_CLOSEST(FREQ_200M, FREQ_32K);
    OSCCTRL->Dpll[1].DPLLRATIO.reg = OSCCTRL_DPLLRATIO_LDR(mul - 1);
    while (OSCCTRL->Dpll[1].DPLLSYNCBUSY.reg & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO)
        ;
    OSCCTRL->Dpll[1].DPLLCTRLB.reg = (OSCCTRL_DPLLCTRLB_REFCLK_GCLK
                                      | OSCCTRL_DPLLCTRLB_LBYPASS);
    OSCCTRL->Dpll[1].DPLLCTRLA.reg = OSCCTRL_DPLLCTRLA_ENABLE;
    mask = OSCCTRL_DPLLSTATUS_CLKRDY | OSCCTRL_DPLLSTATUS_LOCK;
    while ((OSCCTRL->Dpll[1].DPLLSTATUS.reg & mask) != mask)
        ;

    // Produce 100Mhz and 200Mhz clocks from PLL1
    gen_clock(CLKGEN_200M, GCLK_GENCTRL_SRC_DPLL1);
    uint32_t div = DIV_ROUND_CLOSEST(FREQ_200M, FREQ_100M);
    gen_clock(CLKGEN_100M, GCLK_GENCTRL_SRC_DPLL1 | GCLK_GENCTRL_DIV(div));

    // Configure DFLL48M clock (with CLKGEN_32K as reference)
    OSCCTRL->DFLLCTRLA.reg = 0;
    route_pclock(OSCCTRL_GCLK_ID_DFLL48, CLKGEN_32K);
    mul = DIV_ROUND_CLOSEST(FREQ_48M, FREQ_32K);
    OSCCTRL->DFLLMUL.reg = (OSCCTRL_DFLLMUL_CSTEP(31)
                            | OSCCTRL_DFLLMUL_FSTEP(511)
                            | OSCCTRL_DFLLMUL_MUL(mul));
    while (OSCCTRL->DFLLSYNC.reg & OSCCTRL_DFLLSYNC_DFLLMUL)
        ;
    OSCCTRL->DFLLCTRLB.reg = (OSCCTRL_DFLLCTRLB_MODE | OSCCTRL_DFLLCTRLB_QLDIS
                              | OSCCTRL_DFLLCTRLB_WAITLOCK);
    while (OSCCTRL->DFLLSYNC.reg & OSCCTRL_DFLLSYNC_DFLLCTRLB)
        ;
    OSCCTRL->DFLLCTRLA.reg = OSCCTRL_DFLLCTRLA_ENABLE;
    while (!(OSCCTRL->STATUS.reg & OSCCTRL_STATUS_DFLLRDY))
        ;
    gen_clock(CLKGEN_48M, GCLK_GENCTRL_SRC_DFLL);
}

// Initialize clocks from factory calibrated internal clock
static void
clock_init_internal(void)
{
#if 0 // XXX - figure out how to do USB SOF updating
    // Temporarily switch main clock to internal 32Khz signal
    gen_clock(CLKGEN_MAIN, GCLK_GENCTRL_SRC_OSCULP32K);

    // Configure DFLL48M clock (with USB 1Khz SOF as reference)
    OSCCTRL->DFLLCTRLA.reg = 0;
    uint32_t mul = DIV_ROUND_CLOSEST(FREQ_48M, 1000);
    OSCCTRL->DFLLMUL.reg = (OSCCTRL_DFLLMUL_CSTEP(7)
                            | OSCCTRL_DFLLMUL_FSTEP(10)
                            | OSCCTRL_DFLLMUL_MUL(mul));
    while (OSCCTRL->DFLLSYNC.reg & OSCCTRL_DFLLSYNC_DFLLMUL)
        ;
    OSCCTRL->DFLLCTRLB.reg = (OSCCTRL_DFLLCTRLB_USBCRM | OSCCTRL_DFLLCTRLB_MODE
                              | OSCCTRL_DFLLCTRLB_CCDIS
                              | OSCCTRL_DFLLCTRLB_WAITLOCK);
    while (OSCCTRL->DFLLSYNC.reg & OSCCTRL_DFLLSYNC_DFLLCTRLB)
        ;
    OSCCTRL->DFLLCTRLA.reg = OSCCTRL_DFLLCTRLA_ENABLE;
    while (OSCCTRL->DFLLSYNC.reg & OSCCTRL_DFLLSYNC_ENABLE)
        ;
#endif

    // Route factory calibrated DFLL48M to CLKGEN_48M
    gen_clock(CLKGEN_48M, GCLK_GENCTRL_SRC_DFLL);

    // Generate CLKGEN_2M (with CLKGEN_48M as reference)
    uint32_t div = DIV_ROUND_CLOSEST(FREQ_48M, FREQ_2M);
    gen_clock(CLKGEN_2M, GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_DIV(div));

    // Generate 120Mhz clock on PLL0 (with CLKGEN_2M as reference)
    route_pclock(OSCCTRL_GCLK_ID_FDPLL0, CLKGEN_2M);
    uint32_t mul = DIV_ROUND_CLOSEST(FREQ_MAIN, FREQ_2M);
    OSCCTRL->Dpll[0].DPLLRATIO.reg = OSCCTRL_DPLLRATIO_LDR(mul - 1);
    while (OSCCTRL->Dpll[0].DPLLSYNCBUSY.reg & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO)
        ;
    OSCCTRL->Dpll[0].DPLLCTRLB.reg = (OSCCTRL_DPLLCTRLB_REFCLK_GCLK
                                      | OSCCTRL_DPLLCTRLB_LBYPASS);
    OSCCTRL->Dpll[0].DPLLCTRLA.reg = OSCCTRL_DPLLCTRLA_ENABLE;
    uint32_t mask = OSCCTRL_DPLLSTATUS_CLKRDY | OSCCTRL_DPLLSTATUS_LOCK;
    while ((OSCCTRL->Dpll[0].DPLLSTATUS.reg & mask) != mask)
        ;

    // Switch main clock to 120Mhz PLL0
    gen_clock(CLKGEN_MAIN, GCLK_GENCTRL_SRC_DPLL0);

    // Generate 200Mhz clock on PLL1 (with CLKGEN_2M as reference)
    route_pclock(OSCCTRL_GCLK_ID_FDPLL1, CLKGEN_2M);
    mul = DIV_ROUND_CLOSEST(FREQ_200M, FREQ_2M);
    OSCCTRL->Dpll[1].DPLLRATIO.reg = OSCCTRL_DPLLRATIO_LDR(mul - 1);
    while (OSCCTRL->Dpll[1].DPLLSYNCBUSY.reg & OSCCTRL_DPLLSYNCBUSY_DPLLRATIO)
        ;
    OSCCTRL->Dpll[1].DPLLCTRLB.reg = (OSCCTRL_DPLLCTRLB_REFCLK_GCLK
                                      | OSCCTRL_DPLLCTRLB_LBYPASS);
    OSCCTRL->Dpll[1].DPLLCTRLA.reg = OSCCTRL_DPLLCTRLA_ENABLE;
    mask = OSCCTRL_DPLLSTATUS_CLKRDY | OSCCTRL_DPLLSTATUS_LOCK;
    while ((OSCCTRL->Dpll[1].DPLLSTATUS.reg & mask) != mask)
        ;

    // Produce 100Mhz and 200Mhz clocks from PLL1
    gen_clock(CLKGEN_200M, GCLK_GENCTRL_SRC_DPLL1);
    div = DIV_ROUND_CLOSEST(FREQ_200M, FREQ_100M);
    gen_clock(CLKGEN_100M, GCLK_GENCTRL_SRC_DPLL1 | GCLK_GENCTRL_DIV(div));
}

void
SystemInit(void)
{
    // Reset GCLK
    GCLK->CTRLA.reg = GCLK_CTRLA_SWRST;
    while (GCLK->SYNCBUSY.reg & GCLK_SYNCBUSY_SWRST)
        ;

    // Init clocks
    if (CONFIG_CLOCK_REF_X32K)
        clock_init_32k();
    else
        clock_init_internal();

    // Enable SAMD51 cache
    CMCC->CTRL.reg = 1;
}
