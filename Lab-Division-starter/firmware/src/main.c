/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project. It is intended to
    be used as the starting point for CISC-211 Curiosity Nano Board
    programming projects. After initializing the hardware, it will
    go into a 0.5s loop that calls an assembly function specified in a separate
    .s file. It will print the iteration number and the result of the assembly 
    function call to the serial port.
    As an added bonus, it will toggle the LED on each iteration
    to provide feedback that the code is actually running.
  
    NOTE: PC serial port MUST be set to 115200 rate.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/


// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stdio.h>
#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include <string.h>
#include <malloc.h>
#include <inttypes.h>   // required to print out pointers using PRIXPTR macro
#include "definitions.h"                // SYS function prototypes

/* RTC Time period match values for input clock of 1 KHz */
#define PERIOD_100MS                            102
#define PERIOD_500MS                            512
#define PERIOD_1S                               1024
#define PERIOD_2S                               2048
#define PERIOD_4S                               4096

#define MAX_PRINT_LEN 1000

extern char * nameStrPtr; // so we can print the name string

static volatile bool isRTCExpired = false;
static volatile bool changeTempSamplingRate = false;
static volatile bool isUSARTTxComplete = true;
static uint8_t uartTxBuffer[MAX_PRINT_LEN] = {0};

// STUDENTS: you can try your own inputs by modifying the lines below
// the following array defines pairs of values (dividend, divisor) for test
// inputs to the assembly function
// tc stands for test case
static uint32_t tc[][2] = {
    {         29,          5}, // normal case, no errs
    {          1,          1}, // check for handling 0 as a result for mod
    {         25,          5}, // check for handling 0 as a result for mod
    {          0,          0}, // test with both inputs 0
    { 3000000000,    1000000}, // big numbers! (checks to see if using unsigned compares)
    {    1000000, 3000000000}, // big numbers! (checks to see if using unsigned compares)
    {          0,          5}, // test with a 0 input
    {          5,          0}, // test with a 0 input
    {      65534,         11}, // normal case, no errs
    {          1,          5} // check for correct integer division result
};

static char * pass = "PASS";
static char * fail = "FAIL";

// VB COMMENT:
// The ARM calling convention permits the use of up to 4 registers, r0-r3
// to pass data into a function. Only one value can be returned to the 
// C caller. The assembly language routine stores the return value
// in r0. The C compiler will automatically use it as the function's return
// value.
//
// Function signature
// for this lab, the function takes one arg (amount), and returns the balance
extern uint32_t asmFunc(uint32_t, uint32_t);


extern uint32_t dividend;
extern uint32_t divisor;
extern uint32_t quotient;
extern uint32_t mod;
extern uint32_t we_have_a_problem;


// set this to 0 if using the simulator. BUT note that the simulator
// does NOT support the UART, so there's no way to print output.
#define USING_HW 1

#if USING_HW
static void rtcEventHandler (RTC_TIMER32_INT_MASK intCause, uintptr_t context)
{
    if (intCause & RTC_MODE0_INTENSET_CMP0_Msk)
    {            
        isRTCExpired    = true;
    }
}
static void usartDmaChannelHandler(DMAC_TRANSFER_EVENT event, uintptr_t contextHandle)
{
    if (event == DMAC_TRANSFER_EVENT_COMPLETE)
    {
        isUSARTTxComplete = true;
    }
}
#endif

// print the mem addresses of the global vars at startup
// this is to help the students debug their code
static void printGlobalAddresses(void)
{
    // build the string to be sent out over the serial lines
    snprintf((char*)uartTxBuffer, MAX_PRINT_LEN,
            "========= START_TESTS: %s\r\n"
            "========= GLOBAL VARIABLES MEMORY ADDRESS LIST\r\n"
            "global variable \"dividend\" stored at address:          0x%" PRIXPTR "\r\n"
            "global variable \"divisor\" stored at address:           0x%" PRIXPTR "\r\n"
            "global variable \"quotient\" stored at address:          0x%" PRIXPTR "\r\n"
            "global variable \"mod\" stored at address:               0x%" PRIXPTR "\r\n"
            "global variable \"we_have_a_problem\" stored at address: 0x%" PRIXPTR "\r\n"
            "========= END -- GLOBAL VARIABLES MEMORY ADDRESS LIST\r\n"
            "\r\n",
            nameStrPtr,
            (uintptr_t)(&dividend), 
            (uintptr_t)(&divisor), 
            (uintptr_t)(&quotient), 
            (uintptr_t)(&mod),  
            (uintptr_t)(&we_have_a_problem)
            ); 
    isRTCExpired = false;
    isUSARTTxComplete = false;

#if USING_HW 
    DMAC_ChannelTransfer(DMAC_CHANNEL_0, uartTxBuffer, \
        (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), \
        strlen((const char*)uartTxBuffer));
    // spin here, waiting for timer and UART to complete
    while (isUSARTTxComplete == false); // wait for print to finish
    /* reset it for the next print */
    isUSARTTxComplete = false;
#endif
}


// return failure count. A return value of 0 means everything passed.
static int testResult(int testNum, 
                      uint32_t r0returnedValue, 
                      int32_t *passCount,
                      int32_t *failCount)
{
    // for this lab, each test case corresponds to a single pass or fail
    // But for future labs, one test case may have multiple pass/fail criteria
    // So I'm setting it up this way so it'll work for future labs, too --VB
    *failCount = 0;
    *passCount = 0;
    char *errCheck = "OOPS";
    char *quotientCheck = "OOPS";
    char *dividendCheck = "OOPS";
    char *divisorCheck = "OOPS";
    char *modCheck = "OOPS";
    char *addrCheck = "OOPS";
    // static char *s2 = "OOPS";
    // static bool firstTime = true;
    uint32_t expectedR0 = (uint32_t)&quotient;
    uint32_t myErr = 0;
    uint32_t myDiv = 0;
    uint32_t myMod = 0;
    if ((tc[testNum][0] == 0) || (tc[testNum][1] == 0))
    {
        myErr = 1;
    }
    else
    {
        myDiv = tc[testNum][0] / tc[testNum][1];
        myMod = tc[testNum][0] % tc[testNum][1];
    }

    // Check we_have_a_problem
    if(myErr == we_have_a_problem)
    {
        *passCount += 1;
        errCheck = pass;
    }
    else
    {
        *failCount += 1;
        errCheck = fail;
    }

    // Check that returned value was set to quotient's address
    if(expectedR0 == r0returnedValue)
    {
        *passCount += 1;
        addrCheck = pass;
    }
    else
    {
        *failCount += 1;
        addrCheck = fail;
    }        

    // Check mem value of dividend
    if(dividend == tc[testNum][0])
    {
        *passCount += 1;
        dividendCheck = pass;
    }
    else
    {
        *failCount += 1;
        dividendCheck = fail;
    }
    
    // Check mem value of divisor
    if(divisor == tc[testNum][1])
    {
        *passCount += 1;
        divisorCheck = pass;
    }
    else
    {
        *failCount += 1;
        divisorCheck = fail;
    }
    
    // Check calculation of quotient
    if(quotient == myDiv)
    {
        *passCount += 1;
        quotientCheck = pass;
    }
    else
    {
        *failCount += 1;
        quotientCheck = fail;
    }
    
    // Check calculation of modulus
    if(myMod == mod)
    {
        *passCount += 1;
        modCheck = pass;
    }
    else
    {
        *failCount += 1;
        modCheck = fail;
    }
    
           
    // build the string to be sent out over the serial lines
    snprintf((char*)uartTxBuffer, MAX_PRINT_LEN,
            "========= Test Number: %d =========\r\n"
            "test case INPUT: dividend:  %11lu\r\n"
            "test case INPUT: divisor:   %11lu\r\n"
            "error check pass/fail:         %s\r\n"
            "addr check pass/fail:          %s\r\n"
            "dividend mem value pass/fail:  %s\r\n"
            "divisor mem value pass/fail:   %s\r\n"
            "quotient mem value pass/fail:  %s\r\n"
            "modulus mem value pass/fail:   %s\r\n"
            "debug values                  expected        actual\r\n"
            "dividend:.................%11lu   %11lu\r\n"
            "divisor:..................%11lu   %11lu\r\n"
            "quotient:.................%11lu   %11lu\r\n"
            "mod:......................%11lu   %11lu\r\n"
            "we_have_a_problem:........%11lu   %11lu\r\n"
            "expected R0 value (dec):..%11lu   %11lu\r\n"
            "expected R0 value (hex):.. 0x%08" PRIXPTR "    0x%08" PRIXPTR "\r\n"
            "\r\n",
            testNum,
            tc[testNum][0],
            tc[testNum][1],
            errCheck, addrCheck, dividendCheck, divisorCheck, quotientCheck, modCheck,
            tc[testNum][0], dividend,
            tc[testNum][1], divisor,
            myDiv, quotient,
            myMod, mod,
            myErr, we_have_a_problem,
            expectedR0, r0returnedValue,
            (uintptr_t)(expectedR0), (uintptr_t) r0returnedValue
            );

#if USING_HW 
    // send the string over the serial bus using the UART
    DMAC_ChannelTransfer(DMAC_CHANNEL_0, uartTxBuffer, \
        (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), \
        strlen((const char*)uartTxBuffer));
#endif

    return *failCount;
    
}



// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************
int main ( void )
{
    
 
#if USING_HW
    /* Initialize all modules */
    SYS_Initialize ( NULL );
    DMAC_ChannelCallbackRegister(DMAC_CHANNEL_0, usartDmaChannelHandler, 0);
    RTC_Timer32CallbackRegister(rtcEventHandler, 0);
    RTC_Timer32Compare0Set(PERIOD_100MS);
    RTC_Timer32CounterSet(0);
    RTC_Timer32Start();
#else // using the simulator
    isRTCExpired = true;
    isUSARTTxComplete = true;
#endif //SIMULATOR
    
    printGlobalAddresses();

    // initialize all the variables
    int32_t passCount = 0;
    int32_t failCount = 0;
    int32_t totalPassCount = 0;
    int32_t totalFailCount = 0;
    // int32_t x1 = sizeof(tc);
    // int32_t x2 = sizeof(tc[0]);
    uint32_t numTestCases = sizeof(tc)/sizeof(tc[0]);
    
    // Loop forever
    while ( true )
    {
        // Do the tests
        for (int testCase = 0; testCase < numTestCases; ++testCase)
        {
            // Toggle the LED to show we're running a new test case
            LED0_Toggle();

            // reset the state variables for the timer and serial port funcs
            isRTCExpired = false;
            isUSARTTxComplete = false;
            
            // force bad values to detect mem not written
            mod = dividend = quotient = divisor = 0xDEADBEEF;
            we_have_a_problem = 0XD000000D;
            
            // set the dividend in r0
            uint32_t myDividend = tc[testCase][0];
            // set the divisor in r1
            uint32_t myDivisor  = tc[testCase][1];

            // STUDENTS:
            // !!!! THIS IS WHERE YOUR ASSEMBLY LANGUAGE PROGRAM GETS CALLED!!!!
            // Call our assembly function defined in file asmFunc.s
            uint32_t quotAddr = asmFunc(myDividend, myDivisor);
            
            // test the result and see if it passed
            failCount = testResult(testCase,quotAddr,
                                   &passCount,&failCount);
            totalPassCount = totalPassCount + passCount;
            totalFailCount = totalFailCount + failCount;

#if USING_HW
            // spin here until the UART has completed transmission
            // and the timer has expired
            //while  (false == isUSARTTxComplete ); 
            while ((isRTCExpired == false) ||
                   (isUSARTTxComplete == false));
#endif

        } // for each test case
        
        // When all test cases are complete, print the pass/fail statistics
        // Keep looping so that students can see code is still running.
        // We do this in case there are very few tests and they don't have the
        // terminal hooked up in time.
        uint32_t idleCount = 1;
        uint32_t totalTests = totalPassCount + totalFailCount;
        bool firstTime = true;
        // Only send FINI the first time
        char *finiText = "FINI!!!!!\r\n";
        while(true)      // post-test forever loop
        {
            isRTCExpired = false;
            isUSARTTxComplete = false;
            
            uint32_t numPointsMax = 40;
            uint32_t pointsScored = numPointsMax * totalPassCount / totalTests;
            
            snprintf((char*)uartTxBuffer, MAX_PRINT_LEN,
                    "========= %s: ALL TESTS COMPLETE, Post-test Idle Cycle Number: %ld\r\n"
                    "Summary of tests: %ld of %ld tests passed\r\n"
                    "Final score for test cases: %ld of %ld points\r\n"
                    "%s"
                    "\r\n",
                    nameStrPtr,
                    idleCount, 
                    totalPassCount, totalTests,
                    pointsScored, numPointsMax,
                    finiText); 

#if USING_HW 
            DMAC_ChannelTransfer(DMAC_CHANNEL_0, uartTxBuffer, \
                (const void *)&(SERCOM5_REGS->USART_INT.SERCOM_DATA), \
                strlen((const char*)uartTxBuffer));
            // spin here, waiting for timer and UART to complete
            LED0_Toggle();
            ++idleCount;

            while ((isRTCExpired == false) ||
                   (isUSARTTxComplete == false));

            // STUDENTS:
            // UNCOMMENT THE NEXT LINE IF YOU WANT YOUR CODE TO STOP AFTER THE LAST TEST CASE!
            // exit(0);
            
            // slow down the blink rate after the tests have been executed
            if (firstTime == true)
            {
                finiText = "";
                firstTime = false; // only execute this section once
                RTC_Timer32Compare0Set(PERIOD_4S); // set blink period to 4sec
                RTC_Timer32CounterSet(0); // reset timer to start at 0
            }
#endif
        } // end - post-test forever loop
        
        // Should never get here...
        break;
    } // while ...
            
    /* Execution should not come here during normal operation */
    return ( EXIT_FAILURE );
}
/*******************************************************************************
 End of File
*/

