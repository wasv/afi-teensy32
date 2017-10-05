
/*
 * File:		hw_trig_test.c
 * Purpose:		Demo adc triggered by PDB module
 */

#include "common.h"
#include "adc_demo.h"
#include "hw_trig_test.h"
#include  "adc16.h"

// Global filtered output for ADC1 used by ISR and by demo code print routine
uint32 exponentially_filtered_result1 = 0 ;

extern tADC_Config Master_Adc_Config;

static int8_t cycle_flags=0;

volatile static unsigned result0A,result0B;


// **********************************************************
//
// The folowing code demonstrates the hardware trigger capability of the ADC.
// For the ADC there are two hardware trigger channels, therefore the two status&control "SC1" registers
// and two result registers are tested.
//
// [Why the ADC need two sets of some registers?  So that while one is in use, the other set can be used.
// It alows a ping-pong approach that will be demonstrated here.]
//
// A total of two triggering signals are generated from the PDB hardware
// and the code causes every one of those to be routed to the correct ADC trigger channel.

// The PDB is set in continuous, software triggered mode , counting from 0 to 0xFFFF
// from a  frequency slow enough to allow a very long cycle during which
// there is printed something about the two ADC trigger events.

// A PDB interrupt is setup at the beginning of each PDB cycle ( PDB_IDLY = 0 ).
// It is set to signal by toggling "PIN", at the beginning of each cycle.
// This will be visible as an LED.
// These two PDB delays will trigger two triggers on ADC0.
// The ADC, for each "trigger-SC1x-result" group, will generate an end of conversion interrupt and set a bit
// in a global variable to flag execution of that trigger.
// Background code will check for all the two triggers and conversions to end,
// Then the printf on a single line the two results from adc is shown.
// The effect of an exponential filter is shown on subsequent lines.
// Both the filtered, and unfiltered conversion results are displayed.
// The filter is a simple exponential filter that decays an impulse exponentially.

// Upon the execution of each trigger, the ADC interrupt 
// will signal the "A" trigger with a high on a dedicated pin
// and a "B" trigger via a low level on same pin.

// Again, the ADC has a set of A registers and a set of B registers
// for those items that benefit from ping-ponging (double buffering).

// "PIN1" will be used by ADC0 isr.
// By resetting both marking pins in the initial PDB and having
// "A" trigger happen before "B"  the sequences can be seen.


/********************************************************************/
uint8_t Hw_Trig_Test(void)
{
  // Notes:

  //      PDB settings : continous mode, started by sotware trigger.
  //      This means that once the software "pulls the trigger" by setting a certain bit, the PDB starts counting
  //      and handing out four triggers per cycle of its counter.

  //      PDB settings: CH0_DLY0, CH0_DLY1
  //      set to different values to distinguish effect on ADC0_Rx register
  //      need to provide 2 different voltages to convert at two ADC0 channels
  //      PDB counter clock prescaled to allow time for printf's and slow down things to they are visible, each trigger.

  //      Using adiclk= BUS ,  and adidiv/4 to get  12,25MHz on Tower demonstration.
  //      visibility of PDB start trigger is obtained by generating a toggling edge on
  //      GPIOxx with PDBisr set to trigger immediatly at zero value of PDB counter.

  //      Conversion end of the ADC and channel within the ADC ( A,B ) will be done by
  //      toggling second GPIO pin inside ADCisr  ( this pin is also reset by PDB isr )



// GPIO PIN to low voltage .. this macro sets the PIN low.
 PIN_LOW

// Initialize PIN1 and PIN2 GPIO outputs
 Init_Gpio2();

// Disable ADC and PDB interrupts
 disable_irq(INT_ADC0-16) ;   // not ready for this interrupt yet. Plug vector first.
 disable_irq(INT_PDB0-16) ;    // not ready for this interrupt yet. Plug vector first.

// Dynamic interrupt vector modification whilst those interruts are disabled
 __VECTOR_RAM[INT_ADC0] = (uint32)adc0_isr;  // plug isr into vector table in case not there already
 __VECTOR_RAM[INT_PDB0] = (uint32)pdb_isr;   // plug isr into vector table in case not there already

// The System Integration Module largely determines the role of the different ball map locations on Kinetis.
// When an external pin is used, the System Integration Module should be consulted and invoked as needed.
// System integration module registers start with SIM_

// Turn on the ADC0 clock as well as the PDB clocks to test ADC triggered by PDB
 SIM_SCGC6 |= (SIM_SCGC6_ADC0_MASK );
 SIM_SCGC6 |= SIM_SCGC6_PDB_MASK ;
 PMC_REGSC |= PMC_REGSC_BGBE_MASK ;               // enable the PMC bandgap

// Configure System Integration Module for defaults as far as ADC
 SIM_SOPT7 &= ~(SIM_SOPT7_ADC0ALTTRGEN_MASK  | // selects PDB not ALT trigger
                SIM_SOPT7_ADC0PRETRGSEL_MASK );

 SIM_SOPT7 = SIM_SOPT7_ADC0TRGSEL(0);       // applies only in case of ALT trigger, in which case
                                             // PDB external pin input trigger for ADC 

/////////////////////////////////////////////////////////////////////////////////////////
//PDB configured below



// Configure the Peripheral Delay Block (PDB):
// enable PDB, pdb counter clock = busclock / 20 , continous triggers, sw trigger , and use prescaler too
 PDB0_SC =  PDB_SC_CONT_MASK       // Contintuous, rather than one-shot, mode
         | PDB_SC_PDBEN_MASK      // PDB enabled
         | PDB_SC_PDBIE_MASK      // PDB Interrupt Enable
         | PDB_SC_PRESCALER(0x5)  // Slow down the period of the PDB for testing
         | PDB_SC_TRGSEL(0xf)     // Trigger source is Software Trigger to be invoked in this file
         | PDB_SC_MULT(2);        // Multiplication factor 20 for the prescale divider for the counter clock
                                  // the software trigger, PDB_SC_SWTRIG_MASK is not triggered at this time.

 PDB0_IDLY = 0x0000;   // need to trigger interrupt every counter reset which happens when modulus reached

 PDB0_MOD = 0xffff;    // largest period possible with the slections above, so slow you can see each conversion.

// channel 0 pretrigger 0 and 1 enabled and delayed
 PDB0_CH0C1 = PDB_C1_EN(0x01)
           | PDB_C1_TOS(0x01)
           | PDB_C1_EN(0x02)
           | PDB_C1_TOS(0x02) ;

 PDB0_CH0DLY0 = ADC0_DLYA ;
 PDB0_CH0DLY1 = ADC0_DLYB ;

 PDB0_SC =  PDB_SC_CONT_MASK        // Contintuous, rather than one-shot, mode
         | PDB_SC_PDBEN_MASK       // PDB enabled
         | PDB_SC_PDBIE_MASK       // PDB Interrupt Enable
         | PDB_SC_PRESCALER(0x5)   // Slow down the period of the PDB for testing
         | PDB_SC_TRGSEL(0xf)      // Trigger source is Software Trigger to be invoked in this file
         | PDB_SC_MULT(2)          // Multiplication factor 20 for the prescale divider for the counter clock
         | PDB_SC_LDOK_MASK;       // Need to ok the loading or it will not load certain regsiters!
                                   // the software trigger, PDB_SC_SWTRIG_MASK is not triggered at this time.

//PDB configured above
/////////////////////////////////////////////////////////////////////////////////////////
//ADC configured below

// setup the initial ADC default configuration
 Master_Adc_Config.CONFIG1  = ADLPC_NORMAL
                            | ADC_CFG1_ADIV(ADIV_4)
                            | ADLSMP_LONG
                            | ADC_CFG1_MODE(MODE_16)
                            | ADC_CFG1_ADICLK(ADICLK_BUS);
 Master_Adc_Config.CONFIG2  = MUXSEL_ADCA
                            | ADACKEN_DISABLED
                            | ADHSC_HISPEED
                            | ADC_CFG2_ADLSTS(ADLSTS_20) ;
 Master_Adc_Config.COMPARE1 = 0x1234u ;                 // can be anything
 Master_Adc_Config.COMPARE2 = 0x5678u ;                 // can be anything
                                                        // since not using
                                                        // compare feature
 Master_Adc_Config.STATUS2  = ADTRG_HW
                            | ACFE_DISABLED
                            | ACFGT_GREATER
                            | ACREN_ENABLED
                            | DMAEN_DISABLED
                            | ADC_SC2_REFSEL(REFSEL_EXT);

 Master_Adc_Config.STATUS3  = CAL_OFF
                            | ADCO_SINGLE
                            | AVGE_ENABLED
                            | ADC_SC3_AVGS(AVGS_32);

 Master_Adc_Config.STATUS1A = AIEN_OFF | DIFF_SINGLE | ADC_SC1_ADCH(31);
 Master_Adc_Config.STATUS1B = AIEN_OFF | DIFF_SINGLE | ADC_SC1_ADCH(31);

// Configure ADC as it will be used, but becuase ADC_SC1_ADCH is 31,
// the ADC will be inactive.  Channel 31 is just disable function.
// There really is no channel 31.

 ADC_Config_Alt(ADC0_BASE_PTR, &Master_Adc_Config);  // config ADC

// Calibrate the ADC in the configuration in which it will be used:
 ADC_Cal(ADC0_BASE_PTR);                    // do the calibration

// The structure still has the desired configuration.  So restore it.
// Why restore it?  The calibration makes some adjustments to the
// configuration of the ADC.  The are now undone:

// config the ADC again to desired conditions
 ADC_Config_Alt(ADC0_BASE_PTR, &Master_Adc_Config);


// *****************************************************************************
//      ADC0 using the PDB trigger in ping pong
// *****************************************************************************

// use interrupts, single ended mode, and real channel numbers now:

 Master_Adc_Config.STATUS1A = AIEN_ON | DIFF_SINGLE | ADC_SC1_ADCH(ADC0_CHANA);
 Master_Adc_Config.STATUS1B = AIEN_ON | DIFF_SINGLE | ADC_SC1_ADCH(ADC0_CHANB);
 ADC_Config_Alt(ADC0_BASE_PTR, &Master_Adc_Config);  // config ADC0

 // ADC0_CHANA POT channel set the same as the following for demo
 // ADC0_CHANB POT channel set the same as the above for demo

 // The potentiometer is wired to channel 21 on ADC0.  That is the one used
 // to calculate the change of the potentiometer below.


 while(char_present()) in_char();                     // flush terminal buffer

 printf ("\n\n\n");
 printf("********************************************************\n");
 printf("* Running ADC0 HARDWARE TRIGGER by PDB                 *\n");
 
#ifdef FREEDOM
 printf("* ADC0 A,B is the Light Sensor.                        *\n");
#else
 printf("* ADC0 A,B is the POT.   Vary the POT setting.         *\n");
#endif
 
 printf("* Hit any key to exit                                  *\n");
 printf("********************************************************\n");
 printf ("\n\n");

// Enable the ADC and PDB interrupts in NVIC
 enable_irq(INT_ADC0-16) ;   // ready for this interrupt.
 enable_irq(INT_PDB0-16) ;    // ready for this interrupt.

// In case previous test did not end with interrupts enabled, enable used ones.
 EnableInterrupts ;

 cycle_flags=0;
 PDB0_SC |= PDB_SC_SWTRIG_MASK ;    // kick off the PDB  - just once

 //The system is now working!!!!  The PDB is *continuously* triggering ADC
 // conversions.  Now, to display the results!  The line above
 // was the SOFTWARE TRIGGER...

 // The demo will continue as long as no character is pressed on the terminal.

 while(!char_present()) // as long as no operater intervention, keep running this:
 {
  while( cycle_flags != ( ADC0A_DONE | ADC0B_DONE ));  // wait for one complete cycle
  printf("R0A=%6d  R0B=%6d   POT=%6d\r",result0A,result0B,exponentially_filtered_result1);
 }

// disable the PDB

  PDB0_SC = 0 ;

// Disable the ADC and PDB interrupts in NVIC
  disable_irq(INT_ADC0-16) ;   // through with this interrupt.
  disable_irq(INT_PDB0-16) ;    // through with this interrupt.


 printf ("\n\n\n");
 printf("********************************************************\n");
 printf("* Demonstration ended at operator request              *\n");
 printf("* ADC0      TRIGGER DEMO COMPLETE                      *\n");
 printf("********************************************************\n");
 printf ("\n\n");


return 0;
}


/******************************************************************************
* pdb_isr(void)
*
* use to signal PDB counter has restarted counting
*
* In:  n/a
* Out: n/a
******************************************************************************/
void pdb_isr(void)
{
  //printf("\npdb interrupt\n");
 PIN_TOGGLE                     // do this asap - show start of PDB cycle
 PDB0_SC &= ~PDB_SC_PDBIF_MASK ;  // clear interrupt mask
 PIN1_LOW
 cycle_flags = 0;
 return;
}

/******************************************************************************
* adc0_isr(void)
*
* use to signal ADC0 end of conversion
* In:  n/a
* Out: exponentially filtered potentiometer reading!
* The ADC0 is used to sample the potentiometer on the A side and the B side:
* ping-pong.  That reading is filtered for an agregate of ADC0 readings: exponentially_filtered_result1
* thus the filtered POT output is available for display.
******************************************************************************/

void adc0_isr(void)
{
   //printf("\nadc interrupt\n");
 if (( ADC0_SC1A & ADC_SC1_COCO_MASK ) == ADC_SC1_COCO_MASK)
 {  // check which of the two conversions just triggered
  PIN1_HIGH                     // do this asap
  result0A = ADC0_RA;           // this will clear the COCO bit that is also the interrupt flag
  // Begin exponential filter code for Potentiometer setting for demonstration of filter effect
    exponentially_filtered_result1 += result0A;
    exponentially_filtered_result1 /= 2 ;
    // Spikes are attenuated 6dB, 12dB, 24dB, .. and so on untill they die out.
    // End exponential filter code..  add f*sample, divide by (f+1).. f is 1 for this case.
  cycle_flags |= ADC0A_DONE ;   // mark this step done
 }
 else if (( ADC0_SC1B & ADC_SC1_COCO_MASK ) == ADC_SC1_COCO_MASK)
 {
  PIN1_LOW
  result0B = ADC0_RB;
  // Begin exponential filter code for Potentiometer setting for demonstration of filter effect
    exponentially_filtered_result1 += result0B;
    exponentially_filtered_result1 /= 2 ;
    // Spikes are attenuated 6dB, 12dB, 24dB, .. and so on untill they die out.
    // End exponential filter code..  add f*sample, divide by (f+1).. f is 1 for this case.
  
  cycle_flags |= ADC0B_DONE ;
 }
 return;
}


//******************************************************************************
// setup additional two output pins to indirectly observe adc status changes
//
//******************************************************************************

void Init_Gpio2(void){

  // setup PTC8 and PTC9 for output 

  PORTC_PCR8 = PORT_PCR_MUX(1) ;        // select GPIO function
  GPIOC_PCOR =  0x01 << 8 ;              // initial out low
  GPIOC_PDDR |= 0x01 << 8 ;             // output enable NOTE OR

  PORTC_PCR9 = PORT_PCR_MUX(1) ;        // select GPIO function
  GPIOC_PCOR =  0x01 << 9 ;              // initial out low
  GPIOC_PDDR |= 0x01 << 9 ;             // output enable NOTE OR

  }