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

// FIFO 機能のON/OFF
//#define NOFIFO

// USBUARTのパケットサイズ
#define     UART_TX_QUEUE_SIZE      (64)

// USBUARTのTXキューバッファ
uint8       uartTxQueue[UART_TX_QUEUE_SIZE];    // TXキュー
uint8       uartTxCount = 0;                    // TXキューに存在するデータ数
CYBIT       uartZlpRequired = 0;                // 要ZLPフラグ
uint8       uartTxReject = 0;                   // 送信不可回数

#ifdef NOFIFO
    
// 1バイトを送信する関数
static void putch_sub(const int16 ch) {
    // FIFOを使わない時は、PutChar()をそのまま使う
    USBUART_PutChar(ch);
}

#else // define(NOFIFO)

// 1バイトを送信する関数
static void putch_sub(const int16 ch) {
    uint8 state;
    for (;;) {
        // 送信キューが空くまで待つ
        state = CyEnterCriticalSection();
        if (uartTxCount < UART_TX_QUEUE_SIZE) break;
        CyExitCriticalSection(state);
    }
    // 送信キューに一文字入れる
    uartTxQueue[uartTxCount++] = ch;
    CyExitCriticalSection(state);
}

// 送信側割り込みサービス制御
void uartTxIsr(void) {
    uint8 state = CyEnterCriticalSection();
    if ((uartTxCount > 0) || uartZlpRequired) {
        // バッファにデータが存在する、または、ZLPが必要な時にパケットを送る
        if (USBUART_CDCIsReady()) {
            // 送信可能なら - パケットを送る
            USBUART_PutData(uartTxQueue, uartTxCount);
            // バッファをクリアする
            uartZlpRequired = (uartTxCount == UART_TX_QUEUE_SIZE);
            uartTxCount = 0;
            uartTxReject = 0;
        } else if (++uartTxReject > 4) {
            // 送信不可が続いたら - バッファのデータを棄てる
            uartTxCount = 0;
            uartTxReject = 0;
        } else {
            // 次回に期待
        }
    }
    CyExitCriticalSection(state);
}

#endif // define(NOFIFO)

// USBUARTに一文字送る
void putch(const int16 ch) {
    if (ch == '\n') {
        // LFをCRLFに変換する
        putch_sub('\r');
    }
    putch_sub(ch);
}

// USBUARTに文字列を送り込む
void putstr(const char *s) {
    // 行末まで表示する
    while (*s) {
        putch(*s++);
    }
}

// 32-bit十進数表
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

// 32-bit数値の十進表示 - ZERO SUPPRESS は省略。
void putdec32(uint32 num, const uint8 nDigits) {
    uint8       i;
    uint8       k;
    CYBIT       show = 0;

    // 表示すべき桁数
    i = sizeof pow10_32 / sizeof pow10_32[0];
    while (--i > 0) {             // 一の位まで表示する
        // i桁目の数値を得る
        for (k = 0; num >= pow10_32[i]; k++) {
            num -= pow10_32[i];
        }
        // 表示すべきか判断する
        show = show || (i <= nDigits) || (k != 0);
        // 必要なら表示する
        if (show) {
            putch(k + '0');     // 着目桁の表示
        }
    }
}

#ifndef NOFIFO
    
// 周期的にUSBUARTの送受信を監視する
CY_ISR(int_uartQueue_isr) {
    uartTxIsr();
}

#endif // !define(NOFIFO)

int main(void) {
    uint32 nLine = 0;           // 行番号
    
    CyGlobalIntEnable;                          // 割り込みの有効化    
    USBUART_Start(0, USBUART_5V_OPERATION);     // 動作電圧5VにてUSBFSコンポーネントを初期化

#ifndef NOFIFO
    
    int_uartQueue_StartEx(int_uartQueue_isr);   // 周期タイマを起動する

#endif // !define(NOFIFO)

    for(;;) {
        // 初期化終了まで待機
        while (USBUART_GetConfiguration() == 0);

        USBUART_IsConfigurationChanged();       // CHANGEフラグを確実にクリアする
        USBUART_CDC_Init();                     // CDC機能を起動する

        for (;;) {
            // 設定が変更されたら、再初期化をおこなう
            if (USBUART_IsConfigurationChanged()) {
                break;
            }

            // CDC-IN : ホストにメッセージを送る
            putdec32(nLine++, 7);
            putstr(" - HELLO WORLD HELLO WORLD HELLO WORLD HELLO WORLD\n");
            
            // CDC-Control : 制御コマンドは無視する
            (void)USBUART_IsLineChanged();
        }
    }
}

/* [] END OF FILE */
