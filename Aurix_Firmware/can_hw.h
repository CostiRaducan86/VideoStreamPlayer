#ifndef CAN_HW_H
#define CAN_HW_H

/******************************************************************************
 * can_hw.h — Diagnostic UART sniffer on ASCLIN9 / P20.7
 *
 * The "CAN" diagnostic bus between ECU and LSM is UART at 1 Mbaud, 8-Odd-2,
 * sent through CAN transceivers (TLE9251V on Aurix kit, TJA1057 on ECU,
 * TCAN1057 on LSM).  The transceivers only provide differential signaling.
 *
 * v7: ASCLIN9 is dedicated to diagnostic UART on P20.7 (DMA channel 0).
 *     LVDS pixel data runs on ASCLIN1/P14.8 (DMA channel 1).
 *     Both run simultaneously.
 *
 * Hardware: KIT_A2G_TC397_5V_TFT
 *   RXD  = P20.7 (ASCLIN9 RXF, via TLE9251V U206)
 *   X202: Pin3=CAN_L, Pin4=CAN_H, Pin2/5=GND
 ******************************************************************************/

#include "Ifx_Types.h"

/* ── Received UART diagnostic frame ── */
typedef struct
{
    uint8   data[72];       /* payload bytes (max ECU frame ~70 bytes)  */
    uint8   len;            /* actual data length                       */
    uint32  timestampUs;    /* STM0-based timestamp in microseconds     */
    uint16  responseDelayUs;   /* time from ECU request end to this response start */
    uint16  interFrameDelayUs; /* time from previous frame end to this frame start */
} DiagUartFrame;

/* ── Diagnostic counters & ASCLIN9 register snapshots ── */
typedef struct
{
    /* Byte/DMA counters */
    volatile uint32 dmaCompletions;  /* DMA buffer completions (each = 2560 B) */
    volatile uint32 totalRxBytes;    /* cumulative received bytes              */
    volatile uint32 missedBuffers;   /* DMA buffers overwritten before read    */

    /* ASCLIN error tracking */
    volatile uint32 framingErrors;   /* ASCLIN FE flag count                   */
    volatile uint32 parityErrors;    /* ASCLIN PE flag count                   */

    /* Status */
    volatile uint32 synced;          /* 1 = bytes flowing on diagnostic bus    */
    volatile uint32 initOk;          /* 1 = ASCLIN9 + DMA initialised OK      */
    volatile uint32 baudrate;        /* configured baudrate (1000000)          */
    volatile uint32 stmFreqHz;       /* STM0 clock frequency for timestamps   */

    /* ASCLIN9 register snapshots (updated each tick, for debugger) */
    volatile uint32 regFlags;        /* ASCLIN9.FLAGS.U                        */
    volatile uint32 regFlagsEn;      /* ASCLIN9.FLAGSENABLE.U                  */
    volatile uint32 regFrameCon;     /* ASCLIN9.FRAMECON.U                     */
    volatile uint32 regBrg;          /* ASCLIN9.BRG.U (baud rate generator)    */
    volatile uint32 regBitCon;       /* ASCLIN9.BITCON.U (bit timing)          */
    volatile uint32 regIocr;         /* ASCLIN9.IOCR.U (pin mux / ALTI)       */
    volatile uint32 regRxFifoCon;    /* ASCLIN9.RXFIFOCON.U (fill level)       */
    volatile uint32 regCsr;          /* ASCLIN9.CSR.U (clock source)           */
    volatile uint32 regDatCon;       /* ASCLIN9.DATCON.U (data length)         */

    /* GPIO / misc diagnostics */
    volatile uint32 pinRxLevel;      /* current P20.7 digital level (0/1)     */
    volatile uint32 rxFifoFill;      /* last sampled RXFIFO fill level        */
    volatile uint32 pollCount;       /* how many times tick() was called      */

    /* Frame parser counters */
    volatile uint32 framesDecoded;   /* complete UART frames extracted        */
    volatile uint32 syncSkips;       /* bytes skipped hunting for 0x80 SYNC   */
    volatile uint32 badDlc;          /* unrecognised DLC/FUN at valid SYNC    */
} DiagUartStats;

extern DiagUartStats g_diagUartStats;

/** Global flag: 1 = diagnostic sniffing active, 0 = idle.
 *  Controlled by PC command FE_CMD_DIAG_SNIFF via Ethernet. */
extern volatile uint8 g_diagSniffEnabled;

/* ── API ── */

/** Initialise ASCLIN9 + DMA for diagnostic UART sniffer. */
void diag_uart_init(void);

/** Poll diagnostic counters. Call from main loop.
 *  Returns TRUE when bytes are flowing (synced). */
boolean diag_uart_tick(void);

/** Check if diagnostic UART has synced (bytes received). */
boolean diag_uart_is_synced(void);

/** Try to receive a parsed diagnostic frame.
 *  Returns TRUE if a frame was available (not yet implemented). */
boolean diag_uart_try_receive(DiagUartFrame *out);

/** Reset all soft counters and parser state (does NOT re-init ASCLIN9/DMA).
 *  Call on sniff-start so a new recording session starts from zero. */
void diag_uart_reset_state(void);

#endif /* CAN_HW_H */
