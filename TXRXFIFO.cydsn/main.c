/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include "project.h"

// Uncomment when Disable the FIFO function.
//#define NOFIFO

// The packet size of the USBUART
// is used for the FIFO buffer size too.
#define     UART_TX_QUEUE_SIZE      (64)
#define     UART_RX_QUEUE_SIZE      (64)

// TX buffer declaration
uint8       uartTxQueue[UART_TX_QUEUE_SIZE];    // Queue buffer for TX
uint8       uartTxCount = 0;                    // Number of data bytes contained in the TX buffer
CYBIT       uartZlpRequired = 0;                // Flag to indicate the ZLP is required
uint8       uartTxReject = 0;                   // The count of trial rejected by the TX endpoint

// RX buffer declaration
uint8       uartRxQueue[UART_RX_QUEUE_SIZE];    // Queue buffer for RX
uint8       uartRxCount = 0;                    // Number of data bytes contained in the TX buffer
uint8       uartRxIndex = 0;                    // Index on RX buffer to get a byte
CYBIT       uartRxCRDetect = 0;                 // CR detection flag

#ifdef NOFIFO
    
// Function to send one byte to USBUART
static void putch_sub(const int16 ch) {
    // PutChar() function is used if no FIFO used
    USBUART_PutChar(ch);
}

#else // define(NOFIFO)

// Function to send one byte to USBUART
static void putch_sub(const int16 ch) {
    uint8 state;
    for (;;) {
        // Wait until the TX buffer is EMPTY
        state = CyEnterCriticalSection();
        if (uartTxCount < UART_TX_QUEUE_SIZE) break;
        CyExitCriticalSection(state);
    }
    // Store one byte into the TX buffer
    uartTxQueue[uartTxCount++] = ch;
    CyExitCriticalSection(state);
}

// TX side Interrupt Service Routine
void uartTxIsr(void) {
    uint8 state = CyEnterCriticalSection();
    if ((uartTxCount > 0) || uartZlpRequired) {
        // Send a packet if the TX buffer has any data or an ZLP packet is required.
        if (USBUART_CDCIsReady()) {
            // Send a packet if the USBUART accepted.
            USBUART_PutData(uartTxQueue, uartTxCount);
            // Clear the buffer
            uartZlpRequired = (uartTxCount == UART_TX_QUEUE_SIZE);
            uartTxCount = 0;
            uartTxReject = 0;
        } else if (++uartTxReject > 4) {
            // Discard the TX buffer content if USBUART does not accept four times.
            uartTxCount = 0;
            uartTxReject = 0;
        } else {
            // Expect next time
        }
    }
    CyExitCriticalSection(state);
}

#endif // define(NOFIFO)

// Send one character to USBUART
void putch(const int16 ch) {
    if (ch == '\n') {
        // Convert LF to CRLF
        putch_sub('\r');
    }
    putch_sub(ch);
}

// Send a character string to USBUART
void putstr(const char *s) {
    // Send characters to the end of line
    while (*s) {
        putch(*s++);
    }
}

// 32-bit power of ten table
static const uint32 CYCODE pow10_32[] = {
    0L,
    1L,
    10L,
    100L,
    1000L,
    10000L,
    100000L,
    1000000L,
    10000000L,
    100000000L,
    1000000000L,
};

// Show 32-bit decimal value
// Not supporting ZERO SUPPRESS feature.
void putdec32(uint32 num, const uint8 nDigits) {
    uint8       i;
    uint8       k;
    CYBIT       show = 0;

    // Number of digits to be shown
    i = sizeof pow10_32 / sizeof pow10_32[0];
    while (--i > 0) {             // Show until last digit
        // Get the i-th digit value
        for (k = 0; num >= pow10_32[i]; k++) {
            num -= pow10_32[i];
        }
        // Specify if the digit should be shown or not.
        show = show || (i <= nDigits) || (k != 0);
        // Show the digit if required.
        if (show) {
            putch(k + '0');
        }
    }
}

#ifdef NOFIFO

// Functio to receive one byte
int16 getch_sub(void) {
    int16 ch = -1;
    uint8 state = CyEnterCriticalSection();

    if (uartRxIndex >= uartRxCount) {
        // RX buffer is empty
        if (USBUART_DataIsReady()) {
            // and data arrives at USBUART
            // receive data to RX buffer
            uartRxCount = USBUART_GetAll(uartRxQueue);
            uartRxIndex = 0;
        }
    }
    if (uartRxIndex < uartRxCount) {
        // If any characters left in RX buffer
        // get a byte from RX buffer
        ch = uartRxQueue[uartRxIndex++];
    }
    CyExitCriticalSection(state);
    return ch;
}
    
#else // define(NOFIFO)

// RX side Interrupt Service Routine
void uartRxIsr(void) {
    uint8 state = CyEnterCriticalSection();
    if (uartRxIndex >= uartRxCount) {
        // If RX buffer is EMPTY
        if (USBUART_DataIsReady()) {
            // and data arrives at USBUART
            // receive data to RX buffer
            uartRxCount = USBUART_GetAll(uartRxQueue);
            uartRxIndex = 0;
        }
    }
    CyExitCriticalSection(state);
}

// Function to get one byte from USBUART
int16 getch_sub(void) {
    int16 ch = -1;
    uint8 state = CyEnterCriticalSection();
    
    if (uartRxIndex < uartRxCount) {
        // If any characters left in RX buffer
        // get a byte from RX buffer
        ch = uartRxQueue[uartRxIndex++];
    }
    CyExitCriticalSection(state);
    return ch;
}

#endif // define(NOFIFO)

// Get one character from USBUART
int16 getch(void) {
    int16 ch = getch_sub();
    if (uartRxCRDetect && ch == '\n') {
        uartRxCRDetect = 0;
        ch = getch_sub();
    } else if (ch == '\r') {
        ch = '\n';
        uartRxCRDetect = 1;
    }
    return ch;
}

#ifndef NOFIFO
    
// Periodically check the TX and RX of USBUART
CY_ISR(int_uartQueue_isr) {
    uartTxIsr();
    uartRxIsr();
}

#endif // !define(NOFIFO)

int main(void) {
    uint32 nLine = 0;           // Line number
    uint32 nChars = 0;          // Number of characters
    
    CyGlobalIntEnable;                          // Enable interrupts
    USBUART_Start(0, USBUART_5V_OPERATION);     // Initialize USBFS using 5V power supply

#ifndef NOFIFO
    
    int_uartQueue_StartEx(int_uartQueue_isr);   // Initialize the periodic timer

#endif // !define(NOFIFO)

    for(;;) {
        // Wait for initialization completed
        while (USBUART_GetConfiguration() == 0);

        USBUART_IsConfigurationChanged();       // Ensure to clear the CHANGE flag
        USBUART_CDC_Init();                     // Initialize the CDC feature

        for (;;) {
            // Re-initialize if the configuration is changed
            if (USBUART_IsConfigurationChanged()) {
                break;
            }

            // CDC-OUT : Show the number of characters in a received line.
            {
                int16 ch = getch();
                if (ch >= 0) {
                    nChars++;
                    if (ch == '\n') {
                        putdec32(nLine, 7);
                        putstr(" - ");
                        putdec32(nChars, 7);
                        putstr("\n");
                        nLine++;
                        nChars = 0;
                    }
                }
            }
            
            // CDC-Control : Ignore all control commands
            (void)USBUART_IsLineChanged();
        }
    }
}

/* [] END OF FILE */
