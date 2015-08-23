#include "msp430G2553.h"

/* PWM inverter to drive 24V AC motor for time-lapse rig
 *
 * Connections:
 *  - P2.2 drives one MOSFET
 *  - P2.4 drives the other MOSFET
 *  - P1.0 LED 1 (same as on Launchpad)
 *  - P1.6 LED 2 (same as on Launchpad)
 */

#define     LED1                  BIT0
#define     LED2                  BIT6
#define     LED_DIR               P1DIR
#define     LED_OUT               P1OUT

#define APPROX_CLOCK_HZ 1107754
#define PWM_FREQUENCY 60
#define PWM_PERIOD (APPROX_CLOCK_HZ/50)
#define PWM_25_PERCENT (PWM_PERIOD/4)
#define PWM_75_PERCENT ((PWM_PERIOD*3)/4)

short irqcount = 0;

void main(void)
{
	WDTCTL = WDTPW + WDTHOLD;  // Stop WDT

	LED_DIR |= LED1 + LED2;
	LED_OUT &= ~(LED1 + LED2);

	// Set up a timer interrupt
//	BCSCTL1 |= DIVA_1;                        // ACLK/2
//	BCSCTL3 |= LFXT1S_2;                      // ACLK = VLO

	TA0CCTL0 = CCIE;                             // CCR0 interrupt enabled
	TA0CCR0 = 50000;
	TA0CTL = TASSEL_2 + MC_2;                  // SMCLK, contmode
	
//	TA0CCR0 = 1200;                            //
//	TA0CTL = TASSEL_1 | MC_1;                  // TACLK = SMCLK, Up mode.
//	TA0CCTL1 = CCIE + OUTMOD_3;                // TACCTL1 Capture Compare
//	TA0CCR1 = 600;

	// Two channels of PWM output from Timer1_A3
	P2DIR |= (BIT2 | BIT4);             // P2.2, P2.4 to output
	P2SEL |= (BIT2 | BIT4);             // P2.2 to TA1.1, P2.4 to TA1.2

	TA1CCR0 = PWM_PERIOD-1;             // PWM Period
	TA1CCTL1 = OUTMOD_6;          // CCR1 toggle/set -- output on in high part of triangle
	TA1CCR1 = PWM_75_PERCENT;                // CCR1 PWM duty cycle
	TA1CCTL2 = OUTMOD_2;          // CCR2 toggle/reset -- output on in low part of triangle
	TA1CCR2 = PWM_25_PERCENT;                // CCR2 PWM duty cycle
	TA1CTL = TASSEL_2 + MC_3;     // SMCLK, up/down mode

	__enable_interrupt();
	__bis_SR_register(LPM0_bits + GIE);          // LPM0 with interrupts enabled
}

// Timer A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A0 (void)
{
	irqcount++;
	LED_OUT ^= ((irqcount&4)?LED1:0) | ((irqcount&6)?LED2:0);
	/*
	if (LED_OUT | LED1)
		LED_OUT = (LED_OUT ^ LED2) && ~LED1;
	else
		LED_OUT |= LED1;
	*/
	TA0CCR0 += 50000;                            // Add Offset to CCR0
}
