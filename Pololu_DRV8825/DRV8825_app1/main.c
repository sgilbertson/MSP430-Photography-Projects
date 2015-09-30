#include "msp430G2553.h"

/* DRV8825_app1 - Stepper motor driver for a motion time-lapse rig, using Pololu DRV8825 boards.
 *
 * Method:
 * Periodic interrupt occurs at four times the full step rate.
 * ISR toggles output pins to make two squarewaves 90° out of phase.
 *
 * Stepper motor output connections:
 *  - P2.0 = /ENABLE for both drivers
 *  - P2.1 = Motor 1 DIR
 *  - P2.5 = Motor 2 DIR
 *  - P2.3 = Motor 1 STEP
 *  - P2.6 = Motor 2 STEP
 *
 * LEDs, which mirror A+/B+
 *  - P1.0 LED 1 (same as on Launchpad)
 *  - P1.6 LED 2 (same as on Launchpad)
 *
 *  Speed control
 *   - A1 = potentiometer - forward speed (adjust by holding down forward button)
 *   - A2 = potentiometer - reverse speed (adjust by holding down reverse button)
 *
 *  Buttons (active low)
 *   - P1.3 = forward
 *   - P1.4 = reverse
 *   - P1.5 = stop -- can connect in parallel with a microswitch for a stop limit
 *
 */

// Stepper motor connection definitions
#define STEPPER_DISABLE BIT0
#define STEPPER_1_DIR BIT1
#define STEPPER_2_DIR BIT5
#define STEPPER_1_STEP BIT3
#define STEPPER_2_STEP BIT6
#define STEPPER_ALL_OUTPUTS (STEPPER_DISABLE + STEPPER_1_DIR + STEPPER_2_DIR + STEPPER_1_STEP + STEPPER_2_STEP)
#define STEPPER_ALL_STEP_BITS (STEPPER_1_STEP + STEPPER_2_STEP)

#define STEPPER_DIR P2DIR
#define STEPPER_OUT P2OUT

// Button port/bit definitions
#define BUTTON_PORT P1IN
#define BUTTON_FORWARD BIT3
#define BUTTON_REVERSE BIT4
#define BUTTON_STOP BIT5
#define BUTTON_ALL_BITS (BUTTON_FORWARD|BUTTON_REVERSE|BUTTON_STOP)

typedef enum
{
	RUNSTATE_OFF,
	RUNSTATE_FORWARD,
	RUNSTATE_REVERSE
} RunningState;


// LEDs
#define     LED1                  BIT0
#define     LED2                  BIT6
#define     LED_ALL               (LED1|LED2)
#define     LED_DIR               P1DIR
#define     LED_OUT               P1OUT

#define APPROX_CLOCK_HZ 1107754
#define MICROSTEP_RATIO 32	// DRV8825 does a 32-point sinewave for micro-stepping
#define MIN_STEP_HZ ((unsigned short)((APPROX_CLOCK_HZ+32767)/65535))
#define MAX_STEP_HZ (MICROSTEP_RATIO*300)	// experimentally derived for a particular 0.312mA motor

volatile short irqcount0 = 0;
volatile unsigned short step_interval = APPROX_CLOCK_HZ / (MICROSTEP_RATIO*20); // start at 20Hz, which is 5 steps per second
volatile unsigned short stepperBits = STEPPER_DISABLE | STEPPER_2_DIR;
volatile unsigned short countPer100ms = 0;
volatile unsigned short countPerButtonStateChange = 0;
volatile RunningState runningState = RUNSTATE_OFF;
int adc_buffer[10] = {0};
void main(void)
{
	volatile unsigned short lastCountPer100ms = countPer100ms;
	volatile unsigned short lastCountPerButtonStateChange = countPerButtonStateChange;
	unsigned short forwardStepInterval = step_interval;
	unsigned short reverseStepInterval = step_interval;
	RunningState adjustingState = RUNSTATE_OFF;
	RunningState nextAdjustingState = RUNSTATE_OFF;
	WDTCTL = WDTPW + WDTHOLD;  // Stop WDT

	LED_DIR |= LED_ALL;
	LED_OUT &= ~LED_ALL;
	STEPPER_DIR |= STEPPER_ALL_OUTPUTS;
	STEPPER_OUT &= ~STEPPER_ALL_OUTPUTS;

	// Set up A/D converter for speed-control potentiometer
	ADC10CTL0 = ADC10SHT_2 + ADC10ON + MSC; // ADC10 enabled, multiple conversions, 16 x ADC10CLKs, interrupts disabled
	ADC10CTL1 = CONSEQ_3 + INCH_2;                       // DTC transfer, input A2,A1
	ADC10AE0 |= (BIT1 + BIT2);                         // PA.1 & PA.2 ADC option select (not digital I/O)
	ADC10DTC1 = 2;	// 2 conversions

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

	// Buttons are inputs
	P1DIR &= ~BUTTON_ALL_BITS;
	// Enbable an interrupt whenever the state of a button changes
	P1IE |= BUTTON_ALL_BITS;
	P1IES = 0;	// interrupt on falling edges -- gets adjusted in ISR
	P1IFG &= ~BUTTON_ALL_BITS;

	__enable_interrupt();
	while(1)
	{
		__bis_SR_register(LPM0_bits + GIE);          // LPM0 with interrupts enabled
		// We get here when the A/D conversion ends after each 100ms interrupt...
		if (lastCountPer100ms != countPer100ms)
		{
			if (adjustingState != RUNSTATE_OFF)
			{
				int adChannel = ((adjustingState == RUNSTATE_FORWARD) ? 0 : 1);
				unsigned short adcValue = adc_buffer[adChannel];	// 0..1023
				// The idea here is to set step_interval to 65535 if adcValue is very low, and to some
				// small value if it's high. The small value is selected so that it isn't too fast for the motor.
				unsigned short hz = ((unsigned long)adcValue*(MAX_STEP_HZ-MIN_STEP_HZ))/1023 + MIN_STEP_HZ;
				unsigned long adcReciprocal = ((unsigned long)APPROX_CLOCK_HZ) / hz;
				step_interval = (unsigned short)(adcReciprocal&0xFFFF);
				if (adjustingState == RUNSTATE_FORWARD)
					forwardStepInterval = step_interval;
				else
					reverseStepInterval = step_interval;
			}
			else if (runningState == RUNSTATE_FORWARD)
				step_interval = forwardStepInterval;
			else
				step_interval = reverseStepInterval;
			adjustingState = nextAdjustingState;	// button must be held down >100ms to adjust speed
			lastCountPer100ms = countPer100ms;
			// Start the next conversion, with transfers going to adc_buffer
			ADC10CTL0 &= ~ENC;				// Disable Conversion
		    while (ADC10CTL1 & BUSY);		// Wait if ADC10 busy
		    ADC10SA = (int)&adc_buffer[0];
			ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start for next interval
		}
		// ... or when a button input has changed state ...
		if (lastCountPerButtonStateChange != countPerButtonStateChange)
		{
			if (!(BUTTON_PORT & BUTTON_STOP))
			{
				nextAdjustingState = adjustingState = RUNSTATE_OFF;
				runningState = RUNSTATE_OFF;
				STEPPER_OUT |= STEPPER_DISABLE;
			}
			else if (!(BUTTON_PORT & BUTTON_FORWARD))
			{
				nextAdjustingState = RUNSTATE_FORWARD;
				runningState = RUNSTATE_FORWARD;
				STEPPER_OUT |= STEPPER_1_DIR;
				STEPPER_OUT &= ~STEPPER_DISABLE;
				ADC10CTL1 = INCH_1;                       // input A1
			}
			else if (!(BUTTON_PORT & BUTTON_REVERSE))
			{
				nextAdjustingState = RUNSTATE_REVERSE;
				runningState = RUNSTATE_REVERSE;
				STEPPER_OUT &= ~STEPPER_1_DIR;
				STEPPER_OUT &= ~STEPPER_DISABLE;
				ADC10CTL1 = INCH_2;                       // input A2
			}
			else
			{
				nextAdjustingState = adjustingState = RUNSTATE_OFF;
			}
			lastCountPerButtonStateChange = countPerButtonStateChange;
		}
	}
}

// Port 1 interrupt service routine -- fires when a button is pressed or released
#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void)
{
	countPerButtonStateChange++;
	P1IFG &= ~BUTTON_ALL_BITS;	// clear the interrupt condition
	P1IES = BUTTON_PORT & BUTTON_ALL_BITS;	// look for opposite transitions
	__bic_SR_register_on_exit(CPUOFF);	// wake up the main loop
}

// Timer A0 interrupt service routine
// Updates the step PWM outputs.
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A0 (void)
{
	irqcount0++;
	switch(irqcount0&8)	// Slow down the output by a factor of 8, to make up for timer being too fast for good time-lapse motion
	{
	case 0:
		stepperBits = STEPPER_1_STEP;
		break;
	default:
	case 1:
		stepperBits = 0;
		break;
	}
	if (runningState != RUNSTATE_OFF)
	{
		STEPPER_OUT = (STEPPER_OUT & ~STEPPER_ALL_STEP_BITS) | stepperBits;
	}
	// LEDs indicate the A+ and B+ stepper signal states
	LED_OUT = (LED_OUT&~LED_ALL) | ((stepperBits&STEPPER_1_STEP)?LED1:0) | ((stepperBits&STEPPER_2_DIR)?LED2:0);
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
	TA1CCR0 += (APPROX_CLOCK_HZ/10);	// enter this ISR ten times per second
	__bic_SR_register_on_exit(CPUOFF);	// wake up the main loop
}
