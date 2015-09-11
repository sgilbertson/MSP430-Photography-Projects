#include "msp430G2553.h"

/* PWM inverter to drive stepper motor for a motion time-lapse rig
 *
 * Method:
 * Periodic interrupt occurs at four times the full step rate.
 * ISR toggles output pins to make two squarewaves 90° out of phase.
 *
 * Stepper motor connections:
 *  - P2.0 = A+
 *  - P2.1 = A-
 *  - P2.5 = B+
 *  - P2.3 = B-
 *
 * LEDs, which mirror A+/B+
 *  - P1.0 LED 1 (same as on Launchpad)
 *  - P1.6 LED 2 (same as on Launchpad)
 *
 *  Speed control
 *   - A1 = potentiometer
 *
 * FUTURE:
 *  - Buttons to control start/stop and direction
 */

// Stepper motor connection definitions
#define STEPPER_A1 BIT0
#define STEPPER_A0 BIT1
#define STEPPER_B1 BIT5
#define STEPPER_B0 BIT3
#define STEPPER_ALL_BITS (STEPPER_A1 + STEPPER_A0 + STEPPER_B1 + STEPPER_B0)
#define STEPPER_DIR P2DIR
#define STEPPER_OUT P2OUT


// LEDs
#define     LED1                  BIT0
#define     LED2                  BIT6
#define     LED_ALL               (LED1|LED2)
#define     LED_DIR               P1DIR
#define     LED_OUT               P1OUT

#define APPROX_CLOCK_HZ 1107754
#define MIN_STEP_HZ ((unsigned short)((APPROX_CLOCK_HZ+32767)/65535))
#define MAX_STEP_HZ 500

volatile short irqcount0 = 0;
volatile unsigned short step_interval = APPROX_CLOCK_HZ / 20; // start at 20Hz, which is 5 steps per second
volatile unsigned short stepperBits = STEPPER_A1 | STEPPER_B1;
volatile unsigned short countPer100ms = 0;
void main(void)
{
	volatile unsigned short lastCountPer100ms = countPer100ms;
	unsigned short nextStepCounter = 0;
	WDTCTL = WDTPW + WDTHOLD;  // Stop WDT

	LED_DIR |= LED_ALL;
	LED_OUT &= ~LED_ALL;
	STEPPER_DIR |= STEPPER_ALL_BITS;
	STEPPER_OUT &= ~STEPPER_ALL_BITS;

	// Set up A/D converter for speed-control potentiometer
	ADC10CTL0 = ADC10SHT_2 + ADC10ON + ADC10IE; // ADC10ON, interrupt enabled
	ADC10CTL1 = INCH_1;                       // input A1
	ADC10AE0 |= 0x02;                         // PA.1 ADC option select

	// Set up a timer interrupt for stepper PWM generation
//	BCSCTL1 |= DIVA_1;                        // ACLK/2
//	BCSCTL3 |= LFXT1S_2;                      // ACLK = VLO

	TA0CCTL0 = CCIE;                             // CCR0 interrupt enabled
	TA0CCR0 = 50000;
	TA0CTL = TASSEL_2 + MC_2;                  // SMCLK, contmode
	
	// Set up another timer interrupt, for general timing operations
	TA1CCTL0 = CCIE;                             // CCR0 interrupt enabled
	TA1CCR0 = 50000;
	TA1CTL = TASSEL_2 + MC_2;                  // SMCLK, contmode

	__enable_interrupt();
	while(1)
	{
		__bis_SR_register(LPM0_bits + GIE);          // LPM0 with interrupts enabled
		// We get here when the A/D conversion ends after each 100ms interrupt
		if (lastCountPer100ms != countPer100ms)
		{
			unsigned short adcValue = ADC10MEM;	// 0..1023
			// The idea here is to set step_interval to 65535 if adcValue is very low, and to some
			// small value if it's high. The small value is selected so that it isn't too fast for the motor.
			unsigned short hz = ((unsigned long)ADC10MEM*(MAX_STEP_HZ-MIN_STEP_HZ))/1023 + MIN_STEP_HZ;
			unsigned long adcReciprocal = ((unsigned long)APPROX_CLOCK_HZ) / hz;
			step_interval = (unsigned short)(adcReciprocal&0xFFFF);

			lastCountPer100ms = countPer100ms;
		}
	}
}

// Timer A0 interrupt service routine
// Fires at 4x the full-step rate, so we can update the step PWM outputs.
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A0 (void)
{
	irqcount0++;
	switch(irqcount0&3)
	{
	case 0:
		stepperBits = STEPPER_A1 | STEPPER_B0;
		break;
	case 1:
		stepperBits = STEPPER_A1 | STEPPER_B1;
		break;
	case 2:
		stepperBits = STEPPER_A0 | STEPPER_B1;
		break;
	default:
	case 3:
		stepperBits = STEPPER_A0 | STEPPER_B0;
		break;
	}
	STEPPER_OUT = (STEPPER_OUT & ~STEPPER_ALL_BITS) | stepperBits;
	// LEDs indicate the A+ and B+ stepper signal states
	LED_OUT = (LED_OUT&~LED_ALL) | ((stepperBits&STEPPER_A1)?LED1:0) | ((stepperBits&STEPPER_B1)?LED2:0);
	/*
	if (LED_OUT | LED1)
		LED_OUT = (LED_OUT ^ LED2) && ~LED1;
	else
		LED_OUT |= LED1;
	*/
	TA0CCR0 += step_interval;                            // Add Offset to CCR0 to set time of next interrupt
}

// Timer A1 interrupt service routine
// Fires at a fixed rate for general timing operations
// The ISR starts an ADC conversion and icrements a counter.
// The ADC interrupt will wake up the main loop
#pragma vector=TIMER1_A0_VECTOR
__interrupt void Timer_A1 (void)
{
	countPer100ms++;
	ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start
	TA1CCR0 += (APPROX_CLOCK_HZ/10);	// enter this ISR ten times per second
}

// ADC10 interrupt service routine (possibly not needed)
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR(void)
{
	__bic_SR_register_on_exit(CPUOFF);	// wake up the main loop
}
