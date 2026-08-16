# アーキテクチャ概要

他のAI/エンジニアへの引き継ぎ用に、このリポジトリの全体構成をまとめたもの。開発の経緯や「なぜそうしたか」の詳細な記録は`docs/history.md`・`docs/blog.md`・`docs/threadx_construction.md`を参照。このファイルは「今のコードが何をしているか」のスナップショット。

## 1. プロジェクト概要

- **対象ボード**: Arduino UNO R4 WiFi (Renesas RA4M1, Cortex-M4, 48MHz, RAM 32KB, Flash 256KB)
- **ビルドシステム**: PlatformIO、`platformio.ini`で`framework = fsp`を指定。**Arduino core(`digitalWrite`等)は一切使わず、Renesas FSP(Flexible Software Package)のHAL APIを直接叩いている**
- **RTOS**: ThreadX(Eclipse Foundation管理、MITライセンス)を自前で`lib/threadx/`に移植して同梱。詳細な移植手順は`docs/threadx_construction.md`
- **用途**: UART経由でLEDマトリクス表示切り替え・パルス出力・トリガ信号出力をコマンド制御できる実験用ファームウェア。将来的に他のマイコンとUARTで連携する想定(コマンド応答はするが、対話端末向けのエコーバックは無効化済み)

### ビルド・書き込み

```sh
pio run                 # ビルド
pio run -t upload       # 書き込み
```

## 2. ディレクトリ構成

```
src/                  アプリケーションコード(下記モジュール一覧を参照)
lib/threadx/           ThreadXカーネル本体 + Renesasポート層(移植の詳細はthreadx_construction.md)
reference/              移植の元ネタにした参考コード(uart_cmd.c/h, log_task.c/h)。ビルド対象外、設計を踏襲する際の参照用
docs/                   ドキュメント(このファイル、history.md、blog.md、threadx_construction.md)
include/, test/         PlatformIO標準の空ディレクトリ(未使用、READMEのみ)
```

## 3. 重要な前提知識(FSPプロジェクト特有の罠)

- **`hal_data.h`の大半のextern宣言は実体を持たない。** `framework-renesas-fsp`パッケージが生成する`hal_data.h`には`g_uart0`, `g_uart1`, `g_timer0`〜`g_timer3`, `g_spi0`〜`g_spi3`, `g_adc0`, `g_dac0`等たくさんのペリフェラルインスタンスがextern宣言されているが、これらを実際に定義する`.c`ファイルはこのパッケージ構成には存在しない(e2studioのSmart Configuratorが生成するはずのものが欠けている)。**これらの名前をそのまま使おうとするとリンクエラーになる。** 実際にペリフェラルを使う場合は、`uart.c`や`pulse_out.c`がやっているように、**自分で`xxx_instance_ctrl_t`・`xxx_cfg_t`等を`static`変数として定義し、`R_XXX_Open()`に渡す**必要がある。
- **`R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg)`を呼ばないと、ボードのデフォルトピン設定(`g_bsp_pin_cfg`)は一切反映されない。** `hal_entry()`の冒頭(`copy_vectors_to_ram()`の直後)で必ず呼ぶ。
- **割り込みベクタテーブルも自動生成されない。** `vector_table.c`が独自に`g_vector_table[]`を定義し、`copy_vectors_to_ram()`でRAMにコピーして`SCB->VTOR`を書き換えている。新しい割り込み(例: 別のSCIチャンネルやGPTのcycle end割り込み)を使う場合はここに追記が必要。現状はSCI2(UART)の4本のみ登録済み。GPT6(`pulse_out.c`)は割り込みを使わない設定にしているため登録不要。
- **`main()`はアプリ側に書かない。** FSPフレームワークが生成する`main()`が`hal_entry()`を呼ぶだけの構造になっている。アプリのエントリポイントは`hal_entry()`。
- **GPIO出力にする前に`R_BSP_PinAccessEnable()`が必要**(`led_mtx_init()`内で1回呼んでいる。プロジェクト全体でここ1箇所のみで足りている)。

## 4. スレッド構成

`src/main.c`の`tx_application_define()`で3スレッドを生成。**ThreadXは数値が小さいほど高優先度**。「自分からブロックせず回り続けるスレッドほど優先度を低くする」のが鉄則(詳細は`docs/threadx_construction.md`の「ハマりどころ」参照)。

| スレッド | 優先度 | スタック | ブロックするか | 役割 |
|---|---|---|---|---|
| `led_thread` | 10(最低) | 512B | しない(`led_mtx_refresh()`を回し続ける) | LEDマトリクスの走査表示。形状変更要求はキュー(`TX_QUEUE`)経由で受け取る |
| `uart_cmd_thread` | 5 | 4096B(元1024B、経緯は後述) | する(`uart_rx_getc_blocking()`で受信待ち) | UART受信1文字ずつをラインバッファに蓄積し、改行でコマンド解釈・実行。`pulse`/`trg`コマンドの実処理もこのスレッド上で行われる |
| `log_thread` | 4(最高) | 1024B | する(`tx_thread_sleep()`で50ms周期) | ログリングバッファを50ms周期でUARTにflush |

ティックレートは`TX_TIMER_TICKS_PER_SECOND = 100`(`lib/threadx/include/tx_user.h`)、つまり1tick = 10ms。

`lib/threadx/include/tx_user.h`で`TX_ENABLE_STACK_CHECKING`を有効化済み(元はコメントアウトされていた標準テンプレート)。各スレッドのスタックに既知パターンを敷き詰め、オーバーフローを検知したら`main.c`で登録した`stack_error_handler()`経由でUARTに報告する。

## 5. モジュール一覧

### `main.c`
`hal_entry()`(エントリポイント、ベクタテーブル・IOPORT・LEDマトリクス初期化 → `tx_kernel_enter()`)と`tx_application_define()`(各モジュールの`xxx_init()`呼び出し + 3スレッド生成)。起動直後に`log_printf("Firmware version: V1.01")`をキューイング。ファームウェアバージョンは`FW_VERSION`マクロで管理。

`tx_thread_stack_error_notify(stack_error_handler)`も登録している。ThreadXのランタイムスタックチェック(下記`tx_user.h`参照)が任意のスレッドでオーバーフローを検知した場合、そのスレッド名を`log_printf()`で報告する。

### `vector_table.c` / `vector_table.h`
独自の割り込みベクタテーブルをRAMにコピーする`copy_vectors_to_ram()`。SCI2(UART)の4割り込みのみ登録。新しい割り込みペリフェラルを追加する際はここも更新すること。

### `uart.c` / `uart.h`
SCI2チャンネルをD0(RX)/D1(TX)に割り当て、115200bps 8N1で初期化。送信は割り込み完了をセマフォで待つブロッキング方式(複数スレッドから呼ばれても`tx_mutex`で直列化)。受信は1バイトずつ`TX_QUEUE`にためて`uart_rx_getc_blocking()`で取り出す。D1は`g_bsp_pin_cfg`のデフォルトテーブルに含まれないため`R_BSP_PinCfg()`で個別に設定している。

### `log_thread.c` / `log_thread.h`
ログ用リングバッファ(1024バイト)。`log_printf()`/`log_write_isr()`はバッファに積むだけで即座に返り(UARTをブロックしない)、`log_thread`が50ms周期でまとめて吸い出し`uart_write_blocking()`で送信する。`reference/log_task.c`のsyslogキュー方式を踏襲。

### `uart_cmd_thread.c` / `uart_cmd_thread.h`
UART受信バイトを1文字ずつラインバッファに溜め、改行(`\r`/`\n`)でコマンドとして解釈する。バックスペース対応あり。**文字エコーバック・改行エコーバックは現在すべてコメントアウト済み**(他マイコンとのUART連携で不要になったため。コードは残してあり、該当`uart_write_blocking()`行のコメントを外せば復活)。コマンド応答(`OK:`/`ERR:`)は`log_printf()`経由でそのまま出る。

対応コマンド一覧は[6. UARTコマンド一覧](#6-uartコマンド一覧)を参照。

### `led_mtx.c` / `led_mtx.h`
UNO R4 WiFiのオンボード12×8 LEDマトリクス(96個)は個別GPIO駆動ではなく、11本のGPIO(`P003,P004,P011,P012,P013,P015,P204,P205,P206,P212,P213`)によるチャープレキシング(Charlieplexing)。ピン順序とLEDごとのアノード/カソード対応表はArduino公式`ArduinoCore-renesas`から移植(FSPのピン設定だけでは物理結線が分からないため)。`led_mtx_refresh()`を呼び続けることで残像効果により静止画に見える。

### `led_thread.c` / `led_thread.h`
ハート/ダイヤ/三角/四角の4種類のビットマップを持ち、`TX_QUEUE`経由で受けた形状変更要求に応じて`led_mtx`のフレームバッファを書き換えつつ、`led_mtx_refresh()`を無限に回し続ける。

### `pulse_out.c` / `pulse_out.h`
D12(P4.10 = GPT6のGTIOCB)からパルス列を出力する。GPIOトグルではなく**GPT6をハードウェアPWM(周期モード)で駆動**しており、1μs〜10msの周期を指定可能。

- `TIMER_MODE_PERIODIC`はFSPのGPTドライバが自動的にちょうど50%デューティを維持してくれるため、デューティ計算は不要。
- **`R_GPT_Open()`は1回だけ呼ばれる。** ただし`pulse_out_init()`の時点ではまだ呼ばない — `pulse_out_init()`はD12のピン設定のみ行い、`R_GPT_Open()`自体は**最初の`pulse_out_set_period_ns()`呼び出し時に、実際に要求された周期を使って**遅延実行する。2回目以降の呼び出しは`R_GPT_PeriodSet()`だけで完結し、`Close`/`Open`は二度と呼ばれない。
  - (最初は起動時に仮の周期値でOpenしておき、実際の値は毎回`R_GPT_PeriodSet()`で反映する設計にしていたが、それだとD12がLow固定のまま一切トグルしない不具合が出た。`R_GPT_PeriodSet()`の「停止中ならレジスタを強制転送する」処理だけでは、Open時に構築される出力比較レジスタ周りを完全に代替できなかったとみられる。「毎回実際の値でOpenしていた設計は波形が正しく出ていた」という実績を活かすため、Open自体は1回だけ・かつ実際の値でという今の形に変更した。)
  - 分周比は**固定で/16(PCLKD 48MHz÷16=3MHz)**。この1つの分周比だけで1μs(3カウント)〜10ms(30000カウント)の全範囲が16bitカウンタの上限(65535)に収まり、かつUARTコマンドが受け付ける「整数us/ms」はすべて整数カウントに丸め誤差なく変換できる。
- **経緯(重要、同じ轍を踏まないこと)**: 当初は要求周期ごとに分周比を動的に選び直し、変更のたびに`R_GPT_Close()`→`R_GPT_Open()`で再構成する設計だった(分解能を最良化する狙い)。この再構成パスで、`pulse`コマンド送信直後にボード全体が完全フリーズする(LEDマトリクスも無応答、`SysTick`が止まりThreadXごと停止した挙動)不具合が発生した。
  - 最初は特定の周期の値で発生すると誤認したが、**同じ値(`pulse 10us`)を2回連続で送るだけで再現**することから、値依存ではなく「Close+Openの再構成パスに入るかどうか」が本質と判明。
  - `uart_cmd_thread`のスタック増量(1024→4096B)、`R_GPT_Stop()`/`Close()`/`Open()`の各ステップ間への`R_BSP_SoftwareDelay()`、さらに`tx_thread_sleep()`によるスケジューラ経由の待ち合わせ、と順番に対策したが、**いずれも再発を完全には防げなかった**(`pulse_dbg()`による同期UARTログを挟むと発生しない、という再現条件だけがはっきりしていた)。
  - 最終的に、原因を追い切るのではなく**再構成パス自体をなくす方向に設計変更**して解決した。固定分周比により`Close`/`Open`を起動時の1回に限定でき、以後は`R_GPT_PeriodSet()`(動作中でも安全に呼べる)だけで周期変更が完結する。**この設計変更以降は根本原因を追わずに済んでいるため、`R_GPT_Close()`→`R_GPT_Open()`の再構成そのものに何らかの問題があった、という以上のことは特定できていない。**
  - **教訓**: GPT6(や他のGPTチャンネル)を頻繁に`Close()`/`Open()`し直す設計は避けること。分周比を変えたい場合でも、要求レンジ全体をカバーできる固定の分周比が選べないか先に検討する。
- デバッグ用に`pulse_dbg()`という同期UART直書きログ関数(`log_thread`のバッファリングを経由せず、即座にUARTへ出す)が`pulse_out.c`に残っているが、**現在は`#if 0`で無効化済み**(他マイコンとの通信の邪魔になるため)。再度有効化する場合は関数内の`#if 0`/`#endif`を外すだけでよい。
- **注意**: D12はこのボード上でESP32(WiFiコプロセッサ)とのSPI通信線(MISO)と共用のピン。WiFi機能を使わない前提での実装。

### `trigger_out.c` / `trigger_out.h`
D13(P1.02)を単純なプッシュプルGPIO出力として使い、トリガ信号のON/OFFを行う。GPTのような専用タイマは不要なので`R_BSP_PinCfg`で出力設定後は`R_BSP_PinWrite()`でレベルだけ変更する(毎回ピン設定をやり直さない)。初期状態はLow。

## 6. UARTコマンド一覧

D0(RX)/D1(TX)、115200bps 8N1。改行(`\r`または`\n`)でコマンド確定。大文字小文字を区別しない。

| コマンド | 効果 |
|---|---|
| `led1` | LEDマトリクスにハート形を表示 |
| `led2` | ダイヤ形を表示 |
| `led3` | 三角形を表示 |
| `led4` | 四角形を表示 |
| `pulse <n>us` | D12から周期n μsのパルス(50%デューティ)を出力開始(範囲: 1〜10000) |
| `pulse <n>ms` | D12から周期n msのパルスを出力開始(範囲: 1〜10) |
| `pulse off` | D12のパルス出力を停止 |
| `trg on` | D13をHighに |
| `trg off` | D13をLowに |
| `help` / `?` | コマンド一覧を返す |

応答は`OK: ...`または`ERR: ...`の形式で`log_printf()`経由(非同期、最大50ms遅延)。

## 7. 既知の注意点

- **ピン競合**: D0/D1=UART(SCI2)、D12=GPT6(ESP32とのSPI MISO共用)、D13=GPIO。LEDマトリクス用の11本(`led_mtx.c`)とは重複なし。
- **`pulse_out_set_period_ns()`はスレッドセーフではない**(内部ロックなし)。現状は`uart_cmd_thread`からのみ呼ばれる前提。
- **`led_mtx`のフレームバッファ**は`led_thread`単独からしか触られていない(以前は2スレッド共有で排他制御なしだったが、現在は`led_thread`に統合済み)。
- ThreadXのミューテックス・イベントフラグ等、`uart.c`と`led_thread.c`以外ではまだ本格的に使っていない。
