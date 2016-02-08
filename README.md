# MSP430-Photography-Projects
Personal MSP430 projects, mainly photography-related

Here will place source code for some of my personal MSP430 microcontroller projects, mainly related to photography.

They're not designed for general use, but if you're working with MSP430 processors perhaps you'll find some of the material helpful or interesting.


Pololu DRV8825 Adjustment
=========================

See [https://www.pololu.com/product/2133]

Procedure:
 * Install jumper pulling M0,M1,M2 all low, for full stepping, not micro-stepping
 * Do not connect a motor
 * Clip the red lead of a voltmeter to a small screwdriver
 * Clip the black lead to ground
 * Apply power
 * Adjust the trimpot on the DRV8825 until the voltmeter reads half the motor amps rating

For example, if the motor is rated for 1A, adjust until the meter reads 0.5V.


Sample Motors
-------------

These are some motors I found in nearby surplus stores.

Japan Servo KP35M2-035
 * 37 ohms
 * Internet sources suggest 500mA, implying 0.5*37=18.5V -- tune to 250mV
 * 1.8° per pulse (200 pulses per rev), but testing suggests 4x that -- 1Hz ~50 seconds per rev, 200H ~4rev/sec
 * Runs OK on 12V around 250mA
 * Wdg1 = blue/red, Wdg2 = white/yellow

Litton-Clifton Precision 11-SHBX-51KT H347
 * 16 Ohms
 * 0.312A -- tune to 161mV
 * 0.312*16=5V
 * 1.8° per pulse

Small geared motor ABYJ28-5V or 24BYJ28-5V
 * 10x gear-down
 * Therefore 0.18°/pulse
 * 70 ohms, 5V, so 5/70=71.4mA -- tune to 36mV, or use 5V supply and tune high
 * Loads of torque at 5V up to about 100Hz (5 seconds per rev)
 * Won't start above 150Hz.
 * Wdg1 = blue/yellow, Wdg2 = orange/pink

