# Arduino UNO R4 WiFi × PlatformIO 開発記

## Frameworkの選択（Arduino / Fsp / CMSIS）とFSPを選んだ理由

PlatformIOでArduino UNO R4 WiFi(Renesas RA4M1)用プロジェクトを作成する際、Frameworkには Arduino・Fsp・CMSIS の3択がある。Arduinoは`digitalWrite`などの高レベルAPIを提供するC++ベースの環境で手軽だが、低レベルなレジスタ制御には不向き。CMSISはコアヘッダとレジスタ定義のみを提供する最も低レベルな選択肢で、GPIOやUARTなどの周辺ドライバも自分で書く必要がある。FSPはRenesas純正のHALで、各種ドライバに加えてThreadXやFreeRTOSといったRTOSをスレッドセーフな形で統合できるよう設計されている。今回は最終的にThreadX、難しければFreeRTOSを載せる計画があるため、RTOS統合が公式にサポートされているFSPを選択した。

## FSPプロジェクトのエントリポイントと、ハマりどころだったR_IOPORT_Open

FSPフレームワークでは、`main()`はアプリ側ではなく自動生成されたファイル(`variants/UNOWIFIR4/tmp_gen_c_files/main.c`)側に用意されている。中身は次のようにシンプルで、`hal_entry()`を呼ぶだけになっている。

```c
/* generated main source file - do not edit */
#include "hal_data.h"
int main(void)
{
    hal_entry();
    return 0;
}
```

つまりアプリ側で書くべきエントリポイントは`main()`ではなく`hal_entry()`。ここに気づかずmain()を自分で定義しようとすると多重定義エラーになるので注意が必要。

もう一つのハマりどころが、ピンの初期設定が自動では反映されないという点。`bsp_pin_cfg.h`や`pin_data.c`には各ピンの方向(入力/出力)や初期レベルを定義した`g_bsp_pin_cfg`という設定データが用意されているが、これはあくまで「設定データ」であって、起動時に勝手に適用されるわけではない。実際に反映させるには、`hal_entry()`の冒頭で

```c
R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);
```

を明示的に呼ぶ必要がある。これを呼ばないと、コンフィグ上はピンを出力に設定したつもりでも実機ではHi-Z(フローティング)のままで、何をどう書いても反応しないという分かりにくい状態になる。E2 studioのようなGUI付きの開発環境だとこの初期化コードもテンプレートに含まれて意識せずに済むことが多いが、PlatformIOでゼロから書く場合は自分で気づいて呼ぶ必要がある。

## LEDマトリクス(チャープレキシング)を自前で駆動する

UNO R4 WiFiのオンボードLEDマトリクスは12×8=96個のLEDで構成されているが、これを個別のGPIOで1対1に駆動しているわけではない。使われているのは**チャープレキシング(Charlieplexing)**という手法で、わずか11本のGPIOだけで96個のLEDを制御している。

チャープレキシングの原理はシンプルで、「11本のうち2本を選び、片方をHigh出力・もう片方をLow出力にし、残りの9本は全てHi-Z(入力)にする」という組み合わせを作ると、その2本の間に挟まれたLED**1個だけ**が点灯する。2本の役割(どちらをHigh/Lowにするか)を入れ替えるとまた別のLEDが光るため、n本のピンで最大n×(n-1)個のLEDを個別アドレスできる計算になる(11本なら110通り、実機では96個を使用)。

ただしこの方式では、ある瞬間に本当に点灯できるのは1個のLEDだけ。複数のLEDが同時に光っているように見せるには、点灯させたいLEDを高速に切り替えながら次々光らせ、人間の目の残像効果(persistence of vision)で1枚の絵に見せる必要がある。`src/led_mtx.c`ではこれを次のような構成で実装した。

```c
/* 11本のGPIO(チャープレキシングの物理ピン) */
static const bsp_io_port_pin_t matrix_pins[11] = { ... };

/* LEDインデックス(0-95)ごとの { アノード側ピン番号, カソード側ピン番号 } */
static const uint8_t matrix_led_pins[96][2] = { ... };

/* 使わない9本を全部Hi-Zに戻す */
static void all_pins_hi_z(void) {
    for (uint8_t i = 0; i < 11; i++) {
        R_BSP_PinCfg(matrix_pins[i], IOPORT_CFG_PORT_DIRECTION_INPUT);
    }
}

/* 指定したLED 1個だけを点灯させる */
static void led_on(uint8_t led_index) {
    bsp_io_port_pin_t anode   = matrix_pins[matrix_led_pins[led_index][0]];
    bsp_io_port_pin_t cathode = matrix_pins[matrix_led_pins[led_index][1]];
    R_BSP_PinCfg(anode,   IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_BSP_PinCfg(cathode, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
}

/* フレームバッファ上で点灯すべきピクセルを1つずつ短時間光らせて回る */
void led_mtx_refresh(void) {
    for (uint8_t y = 0; y < LED_MTX_HEIGHT; y++) {
        for (uint8_t x = 0; x < LED_MTX_WIDTH; x++) {
            if (framebuffer[y][x]) {
                led_on((uint8_t)(y * LED_MTX_WIDTH + x));
                R_BSP_SoftwareDelay(300, BSP_DELAY_UNITS_MICROSECONDS);
                all_pins_hi_z();
            }
        }
    }
}
```

`led_mtx_refresh()`を`main.c`側の`while(1)`で回し続けることで、フレームバッファに書き込んだ形(今回はハート型のビットマップ)がちらつきなく表示され続ける。11本のピン順序と96個のLEDに対応するアノード/カソードの組み合わせ表は、FSPのピン設定情報だけからは物理結線までは分からなかったため、Arduino公式の`arduino/ArduinoCore-renesas`リポジトリ(`Arduino_LED_Matrix`ライブラリ)のソースを調べて移植した。
