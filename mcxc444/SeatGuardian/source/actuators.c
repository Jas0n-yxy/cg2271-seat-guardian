/*
 * actuators.c - Onboard RGB LED on TPM0 PWM, plus the buzzer.
 *
 * LED PWM reused from our Lab 2 code (as02_2.c): TPM0 clocked from the 8 MHz
 * MCGIRCLK, prescaler 16, centre-aligned, MOD 1000 -> 250 Hz, low-true output
 * because the onboard LEDs are active low.
 *
 * Buzzer: GPIO on/off for an ACTIVE buzzer (Week 10 TODO: confirm the module;
 * a PASSIVE buzzer needs a PWM tone on TPM2_CH0 instead).
 */
#include "board.h"
#include "config.h"
#include "actuators.h"

#define RED_CH		4
#define GREEN_CH	5
#define BLUE_CH		2
#define PWM_MOD		1000

// Configure the MCG Internal Reference Clock (8 MHz) for the TPM
static void setMCGIRClk(void)
{
	MCG->C1 &= ~MCG_C1_CLKS_MASK;
	MCG->C1 |= MCG_C1_IRCLKEN_MASK;		// enable LIRC (MCGIRCLK output)
	MCG->C2 |= MCG_C2_IRCS_MASK;		// IRCS = 1 -> 8 MHz IRC
	MCG->SC = (MCG->SC & ~MCG_SC_FCRDIV_MASK) | MCG_SC_FCRDIV(0);
	MCG->MC = (MCG->MC & ~MCG_MC_LIRC_DIV2_MASK) | MCG_MC_LIRC_DIV2(0);
}

static void initLedPWM(void)
{
	setMCGIRClk();

	SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK | SIM_SCGC5_PORTE_MASK;

	SIM->SOPT2 &= ~SIM_SOPT2_TPMSRC_MASK;
	SIM->SOPT2 |= SIM_SOPT2_TPMSRC(3);		// TPM clock = MCGIRCLK

	PORTE->PCR[RED_PIN]   = (PORTE->PCR[RED_PIN]   & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(3);
	PORTD->PCR[GREEN_PIN] = (PORTD->PCR[GREEN_PIN] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(4);
	PORTE->PCR[BLUE_PIN]  = (PORTE->PCR[BLUE_PIN]  & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(3);

	TPM0->SC &= ~TPM_SC_CMOD_MASK;
	TPM0->SC &= ~TPM_SC_PS_MASK;
	TPM0->SC |= TPM_SC_PS(4);			// 8 MHz / 16 = 500 kHz
	TPM0->SC |= TPM_SC_CPWMS_MASK;		// centre-aligned
	TPM0->CNT = 0;
	TPM0->MOD = PWM_MOD;				// 500 kHz / (2 x 1000) = 250 Hz

	// Low-true PWM: MSB=1, ELSA=1
	TPM0->CONTROLS[RED_CH].CnSC   = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSA_MASK;
	TPM0->CONTROLS[GREEN_CH].CnSC = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSA_MASK;
	TPM0->CONTROLS[BLUE_CH].CnSC  = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSA_MASK;

	TPM0->SC |= TPM_SC_CMOD(1);			// start
}

static void initBuzzer(void)
{
	SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
	PORTB->PCR[BUZZER_PIN] = (PORTB->PCR[BUZZER_PIN] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(1);
	GPIOB->PDDR |= (1 << BUZZER_PIN);
	buzzer_off();
}

void actuators_init(void)
{
	initLedPWM();
	led_set_rgb(0, 0, 0);
	initBuzzer();
}

static int clampPercent(int p)
{
	if (p < 0) return 0;
	if (p > 100) return 100;
	return p;
}

void led_set_rgb(int red, int green, int blue)
{
	TPM0->CONTROLS[RED_CH].CnV   = clampPercent(red)   * PWM_MOD / 100;
	TPM0->CONTROLS[GREEN_CH].CnV = clampPercent(green) * PWM_MOD / 100;
	TPM0->CONTROLS[BLUE_CH].CnV  = clampPercent(blue)  * PWM_MOD / 100;
}

void buzzer_on(void)
{
	GPIOB->PSOR = (1 << BUZZER_PIN);
}

void buzzer_off(void)
{
	GPIOB->PCOR = (1 << BUZZER_PIN);
}
