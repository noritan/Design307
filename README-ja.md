---
title: USBUARTのTXにFIFOを装備する
tags: PSoC5 USBUART FIFO
author: noritan_org
slide: false
---
# USBUARTにFIFOを装備する

[PSoC Advent Calendar 2016]の12日目と19日目の合併記事です。

**PSoC 5LP**のUSBインターフェイスを使うには、**USBUART**コンポーネントを使った仮想COMポートを利用するのが簡単です。
ところが、**USBUART**コンポーネントは、**UART**など他のシリアルインターフェイスが持っているような**FIFO**を持っていません。

この記事では、**USBUART**で**FIFO**を使う方法をご紹介します。

## USBUARTのTXにFIFOを装備する

まずは、**TX**側に**FIFO**を装備します。

![回路図1](./images/schematic1.png)


### USBのややこしい所

**USBUART**には、1バイトのデータを送るための**PutChar()** メソッドが定義されています。
このメソッドを使うと、エンドポイントの送信が終わったのを確認してから、エンドポイントバッファに1バイトのデータを入れます。
そして、ホストから取り込み要求が来た時にバッファのデータを送信します。
エンドポイントバッファは64バイトの大きさがあるのですが、**PutChar()** メソッドを呼び出した時には1バイトしか使われません。

データ送信のスループットを上げるためには、64バイトのバッファになるべく多くのデータを入れなくてはなりません。
そこで考えられるのが、64バイトのデータが貯まるまで送信を遅らせる方法ですが、この方法ではデータが先方に届くまでの遅延時間が大きくなってしまいます。
例えば、63バイトのデータを送りたい場合、永遠にバッファがいっぱいにならないために全くデータが送りだされないという状態も起こりえます。

このような事態を防ぐため、ここで作成する**FIFO**では、以下の方針を取り入れました。

1. 送りたいデータは、バッファに積んでおく。
2. 周期的にバッファを監視して、送信可能な状態であればバッファからデータを送信する。

ここで重要になるのは、「バッファの監視周期」です。
**USB Full-Speed**規格の転送は、1ミリ秒の「フレーム」と呼ばれる時間単位で区切られており、このフレームにパケットを詰めます。
ここでは、ひとつのフレームに必ずひとつ以上のパケットを入れられるように監視周期をフレームの半分の時間0.5ミリ秒に設定しています。
具体的には、2kHzのクロックにより割り込み`int_uartQueue`を発行しています。


### クロックの設定

![クロックの設定](./images/clockConfig1.png)

クロックは、**USB**バスからクロック成分を取り出して **Internal Main Oscillator (IMO)** の周波数を微調整する方式を採用しています。
これにより、**IMO**の周波数を"24MHz&plusmn;0.25%"まで追い込むことができます。
"79.5MHz"のクロックを**PLL**により生成し、CPU等を駆動するためのクロック**BUS_CLK**として使用しています。
これがシステムで使用可能な最大周波数です。


### ファームウェア

ファームウェアは、以下のようになりました。

```c:main.c
#include "project.h"

// Uncomment when Disable the FIFO function.
//#define NOFIFO
```

冒頭、 include に続いてコメントアウトされた**NOFIFO**マクロ宣言があります。
このマクロを有効にすると、**FIFO**を使わない設定も試す事ができます。

```c:main.c
// The packet size of the USBUART
// is used for the FIFO buffer size too.
#define     UART_TX_QUEUE_SIZE      (64)

// TX buffer declaration
uint8       uartTxQueue[UART_TX_QUEUE_SIZE];    // Queue buffer for TX
uint8       uartTxCount = 0;                    // Number of data bytes contained in the TX buffer
CYBIT       uartZlpRequired = 0;                // Flag to indicate the ZLP is required
uint8       uartTxReject = 0;                   // The count of trial rejected by the TX endpoint
```

**USBUART**で使用するBULKパケットのサイズと**FIFO**で使用する変数を宣言しています。
**USBUART**コンポーネントには、ディスクリプタが含まれているのだから、パケットのサイズぐらい引っ張り出せそうなものなのですが、適当な方法が見つからなかったので、再定義しています。
このパケットサイズを上限としてバッファにデータを貯めておき、頃合いを見計らって**USBUART**コンポーネントから送り出します。

uartZlpRequiredフラグ（要ZLPフラグ）は、"Zero Length Packet" （ZLP: 長さゼロのパケット）が必要な状況であるかどうかを示します。
**USBUART**でデータを格納するパケットの最大サイズは64バイトです。
この大きさを超えるデータを送りたい場合、複数の64バイトパケットに続いて64バイト未満のパケット（Short Packet: ショートパケット）を送り出します
。このショートパケットでデータの終端を表します。

ところが、64バイトの倍数の長さのデータを送る場合は、すべてのパケットのサイズが64バイトなので、データの終端が見分けられません。
このような場合、データの終端を表すために使用されるのがZLPです。
「要ZLPフラグ」は、データの最後のパケットが64バイトであった場合にセットされ、次のパケットでZLPを送信する必要があるかどうかを判断します。

バッファに貯まったデータを**USB**パケットとして送信する時、**USBUART**コンポーネントの受け入れ準備が出来ていなければ、次の機会を待つことになります。
このような後回しの状態が続くとデータを送信しようとしているアプリケーション側の処理が滞ってしまいます。
この例では、処理の停滞を防ぐために、受け入れを拒否された回数を"uartTxReject"で数えておき、何度も受け入れを拒否された場合にはバッファのデータを廃棄して送信をあきらめて処理を先に進ませるようにしています。

```c:main.c
#ifdef NOFIFO
    
// Function to send one byte to USBUART
static void putch_sub(const int16 ch) {
    // PutChar() function is used if no FIFO used
    USBUART_PutChar(ch);
}
```
**FIFO**を使わない場合、1バイトのデータを送るには**USBUART_PutChar()**関数を使います。

```c:main.c
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
```

一方、**FIFO**を使う場合には、送信キューに文字を積んでいきます。
もし、送信キューに空きが無かったら、空きが出来るまで待ちます。

送信キューに空きができるのは、周期割り込みによりバッファを送り出した時です。
つまり、この関数の実行中には割り込みがかかる事が期待されており、タイミングによっては、送信キューを構成する変数などが意図せず書き換えられる可能性があります。

このような事態を防ぐためには、"Critical Section"（きわどい領域）という他のプログラムの介入を禁止する区間をつくって、変数などを保護してやります。
この Critical Section を確保するための関数が"CyEnterCriticalSection()"と"CyExitCriticalSection()"です。
これらの関数を使う事で、安全に割り込みを禁止する事ができます。

```c:main.c
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
```

周期割り込みの処理ルーチンでは、**USB**のパケットを送信しています。
この処理も"Critical Section"に入れてあります。
これは、周期割り込みよりも優先順位の高い割り込みからデータ送信関数が呼ばれる場合を想定したものです。

パケットを送信する条件は、バッファにデータが入っているかまたは「要ZLPフラグ」がセットされている事です。
**USBUART**が受け入れられる場合にはバッファの中身を送信します。
**USBUART**が拒否した場合には"uartTxReject"をインクリメントし、規定の回数以上拒否されていたらパケットを破棄します。

```c:main.c
// Send one character to USBUART
void putch(const int16 ch) {
    if (ch == '\n') {
        // Convert LF to CRLF
        putch_sub('\r');
    }
    putch_sub(ch);
}
```

実際にアプリケーションから呼ばれる一文字送信関数は、 putch() です。
この関数の中では、**LF**を**CRLF**に変換する処理が入っています。

```c:main.c
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
```

出力処理を行うユーティリティ関数として、文字列を出力する**putstr()**と十進数を表示する**putdec32()**を用意しました。

```c:main.c
#ifndef NOFIFO
    
// Periodically check the TX and RX of USBUART
CY_ISR(int_uartQueue_isr) {
    uartTxIsr();
}

#endif // !define(NOFIFO)
```

**FIFO**を使う場合に使用される割り込み処理ルーチンが定義されます。
この中から送信バッファの処理ルーチンを呼び出します。

```c:main.c
int main(void) {
    uint32 nLine = 0;           // Line number
    
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

            // CDC-IN : Send a message to the HOST
            putdec32(nLine++, 7);
            putstr(" - HELLO WORLD HELLO WORLD HELLO WORLD HELLO WORLD\n");
            
            // CDC-Control : Ignore all control commands
            (void)USBUART_IsLineChanged();
        }
    }
}
```

**USBFS**コンポーネントの初期化が終わったら、メインループに入ります。
メインループは、二重構造になっていて、外側のループではホストからの接続要求を待っています。
内側のループでは、ホストからの切断要求、アプリケーションの処理、そして**CDC**デバイスの制御コマンドの処理を行っています。

アプリケーションとして行っているのは、全体で59バイトの"HELLO WORLD"文字列を永遠に出力し続ける処理です。
このとき、先頭に行番号を追加しているので、何番目の出力なのかがわかるようになっています。


### 実行してみたら

![FIFOを使ったら](./images/outputWithFifo1.png)

プロジェクトが出来たら、実行してみます。
**PSoC 5LP**を**USB**ケーブルを介してPCに接続し、ターミナルソフトを接続すると、このスクリーンショットのように、文字列が延々と表示されます。

一行表示するごとに59バイトのデータが送信されていることになります。
10万行の送信を行った時に必要な時間を測定したところ、43秒かかりました。
ここから、実効スループットは134kiB/sと計算されました。

周期割り込みの周期を短くすると、スループットは上がりました。
送信するデータの量にしたがって、割り込み周期を決めてやるとよいでしょう。


### FIFO を使わなかったら

![FIFOを使わない](./images/outputWithoutFifo1.png)

マクロ**NOFIFO**を使って、**FIFO**を使わない場合の動作も確認しました。
すると、表示が乱れて使い物になりません。
どこに問題が有るのか究明はしていませんが、スループットが高い場合には**FIFO**を入れないと話にならないという事がわかりました。


[PSoC Advent Calendar 2016]:https://www.adventar.org/calendars/1796
[GitHub Repository]:https://github.com/noritan/Design307
