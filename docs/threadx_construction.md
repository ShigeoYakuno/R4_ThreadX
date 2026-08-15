# ThreadX移植記録

`docs/history.md`で触れていた「FSPの上にThreadX(だめならFreeRTOS)を載せる」計画の実施記録。LEDマトリクスドライバ(`led_mtx`)を2スレッドから叩く最小構成で動作確認した。

## 1. 現状調査: ポート層はあるが本体がない

PlatformIOの`framework = fsp`が使う`framework-renesas-fsp`パッケージ(`~/.platformio/packages/framework-renesas-fsp/`)の中を見ると、`fsp/src/rm_threadx_port/`にRenesas製のThreadX Cortex-M/CMSISポート層が既に同梱されていた。PendSVによるコンテキストスイッチ(`tx_thread_schedule.c`)、SysTickタイマ割り込み(同ファイル内の`SysTick_Handler`)、WFIによる低消費電力アイドル待機(`tx_port_wait_thread_ready.c`)、スタック構築(`tx_thread_stack_build.c`)など、Cortex-M4向けの下位層は一式揃っている。

ただしこれはビルドスクリプト(`~/.platformio/platforms/renesas-ra/builder/frameworks/fsp.py`)の`src_filter`には含まれておらず、標準ビルドでは一切コンパイルされない。さらに致命的なことに、**ThreadXカーネル本体(`tx_thread_create`や`tx_thread_sleep`などAPIを実装する`common/`以下、約190ファイル)はこのパッケージにそもそも存在しない**。ポート層はあくまで「OS本体からアーキ依存部分だけを差し替えるためのアダプタ」であり、OS本体は別途調達する必要がある。

グローバルなフレームワークパッケージやプラットフォームのビルドスクリプト自体は変更しない方針とした(全FSPプロジェクトに影響するため)。PlatformIOは`lib/*`配下を自動的にライブラリとしてビルドしてリンクする仕組みを持つので、`platformio.ini`にも一切手を加えず`lib/threadx/`を追加するだけで済む。

## 2. ThreadXカーネル本体の取得

ThreadXは元Microsoft Azure RTOS ThreadXで、現在はEclipse Foundationに移管されMITライセンスで公開されている: `https://github.com/eclipse-threadx/threadx`。

```sh
git clone --depth 1 --branch v6.5.1.202602a_rel https://github.com/eclipse-threadx/threadx.git
```

必要なのは以下のみ(アーキ非依存のコア実装):

- `common/inc/*.h` (`tx_api.h`ほか公開ヘッダ)
- `common/src/*.c` (`tx_thread_create`, `tx_thread_sleep`, `tx_timer_*`, `tx_semaphore_*`等の実装、約185ファイル)

`ports/`や`ports_module/`(Cortex-M4/M23/M33それぞれの純正ポート)は使わない。Renesas版ポート(`rm_threadx_port`)がその役割を代替するため。

なお、リポジトリを`git clone`すると`ports_module/`以下のサンプルプロジェクト(Keil用の深いパス)でWindowsの260文字パス長制限に引っかかりチェックアウトが一部失敗するが、必要な`common/`は無事に取得できるため無視してよい。

## 3. `lib/threadx/` の構成

PlatformIOの標準ライブラリレイアウト(`include/`にヘッダ、`src/`に実装)にまとめた。

```
lib/threadx/
├── LICENSE.txt          (ThreadX本体のMITライセンス)
├── include/
│   ├── tx_api.h ...      common/inc/*.h (12ファイル)
│   ├── tx_cmsis.h        Renesasポートのヘッダ
│   ├── tx_port.h         同上 (ThreadXの型定義・割り込みロック等をCM4向けに定義)
│   ├── tx_port_vendor.h  同上 (スタックモニタ等Renesas固有マクロ)
│   └── tx_user.h         common/inc/tx_user_sample.h をリネームしたもの(後述)
└── src/
    ├── tx_*.c            common/src/*.c (185ファイル、カーネル本体)
    ├── tx_initialize_low_level.c   Renesasポート(要改変、後述)
    ├── tx_isr_start.c / tx_isr_end.c
    ├── tx_port_wait_thread_ready.c
    ├── tx_thread_interrupt_control.c / _disable.c / _restore.c
    ├── tx_thread_schedule.c        PendSV_Handler / SysTick_Handler を含む
    ├── tx_thread_stack_build.c
    ├── tx_thread_system_return.c
    └── tx_timer_interrupt.c
```

`tx_api.h`が`#include "tx_port.h"`するため、ThreadX本体のヘッダとRenesasポートのヘッダは同じインクルードパス(`include/`)に置く必要がある。

`rm_threadx_port`から意図的に除外したファイル:

- `tx_iar.c` — IARコンパイラ専用
- `txe_thread_secure_stack_allocate.c` / `_free.c`、`tx_thread_secure_stack_allocate.c` / `_free.c`、`tx_secure_interface.h` — Arm TrustZoneのセキュア/非セキュア分離用。RA4M1のCortex-M4はTrustZone非対応なのでビルドしても無意味

### `tx_user.h`が必須になる理由

`tx_port_vendor.h`が

```c
#ifndef TX_INCLUDE_USER_DEFINE_FILE
 #define TX_INCLUDE_USER_DEFINE_FILE
#endif
```

を無条件に定義しているため、`tx_port.h`側の

```c
#ifdef TX_INCLUDE_USER_DEFINE_FILE
#include "tx_user.h"
#endif
```

が必ず有効になり、`tx_user.h`が存在しないとインクルードエラーになる。`common/inc/tx_user_sample.h`(全設定がコメントアウトされたテンプレート)を`tx_user.h`にリネームして配置することで解決した。

## 4. `tx_initialize_low_level.c` の改変

Renesas版オリジナルのまま使うと2箇所でビルドが通らなかった。

### (a) `__RAM_segment_used_end__` が未定義

オリジナルは空きRAM先頭アドレスをGCCのリンカ生成シンボル`__RAM_segment_used_end__`から取得する設計だが、このボードのリンカスクリプト(`variants/UNOWIFIR4/fsp.ld`, `memory_regions.ld`)にはそのシンボルは定義されていない(grepで確認済み)。これは通常、e2studioのRA Smart Configuratorがプロジェクトごとに生成するシンボルで、PlatformIOの素のFSPパッケージには含まれていない。

リンカスクリプトを書き換える代わりに、`_tx_initialize_unused_memory`用の固定サイズ静的バッファを用意して置き換えた:

```c
static UCHAR tx_free_memory_pool[64];
#define TX_FREE_MEMORY_START    ((void *) tx_free_memory_pool)
```

このポインタは`tx_application_define()`に`first_unused_memory`として渡されるだけの値であり、今回のテストコードはスレッドスタックを静的配列で確保するため実際には使用しない。

### (b) `TX_PORT_CFG_SYSTICK_IPL` が未定義

SysTickの割り込み優先度を指定するこのマクロも、本来はe2studioのFSPコンフィグレータが`rm_threadx_port_cfg.h`に生成するものだが、同様に存在しない。デフォルト値を自前で補った:

```c
#ifndef TX_PORT_CFG_SYSTICK_IPL
 #define TX_PORT_CFG_SYSTICK_IPL    (14)
#endif
```

それ以外(`SysTick_Config`, `NVIC_SetPriority`, `DWT->CTRL`, ベクタテーブルからのシステムスタックポインタ取得等)はRenesas実装のまま変更していない。`__Vectors`・`SystemCoreClock`は`fsp/src/bsp/cmsis/Device/RENESAS/Source/{startup.c,system.c}`で定義済みで、これらは常時ビルドされるため追加対応不要だった。`R_MPU_SPMON`(スタックポインタ監視ハードウェア)もRA4M1のデバイスヘッダ(`R7FA4M1AB.h`)に定義があり問題なし。

## 5. `src/main.c`: スレッド化

`hal_entry()`の`while(1) { led_mtx_refresh(); }`を`tx_kernel_enter()`に置き換え、`tx_application_define()`で2スレッドを生成する構成にした。

```c
void hal_entry(void)
{
    R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);
    led_mtx_init();
    tx_kernel_enter(); /* 戻らない */
}
```

- `refresh_thread`: `led_mtx_refresh()`を`while(1)`で回し続け、表示を維持する
- `blink_thread`: `tx_thread_sleep(50)`(TX_TIMER_TICKS_PER_SECOND=100のデフォルトで500ms)ごとにハート形を出したり消したりする

## 6. ハマったポイント: 優先度の付け方を逆にした

最初の実装では`refresh_thread`を優先度1(高)、`blink_thread`を優先度5(低)にしていた。ビルドは通り、書き込んでも一見ハングしたように何も光らなかった。

原因はThreadXの優先度の性質を誤解していたこと。ThreadXは**数値が小さいほど高優先度のプリエンプティブスケジューラ**で、同等以上の優先度のスレッドがready状態である限り、低優先度スレッドには一切CPU時間が回らない。`refresh_thread`は`tx_thread_sleep`も`tx_thread_relinquish`も呼ばず`led_mtx_refresh()`を無限に回し続けるだけなので、これを高優先度にしてしまうと、`blink_thread`が`tx_thread_sleep`から目覚めてready状態になっても**永久にCPUを渡してもらえない**。結果、`draw_heart()`が一度も呼ばれずフレームバッファは空のまま、LEDは何も光らずハングしたように見える。

修正: **常に動き続ける(自分からブロックしない)スレッドほど優先度を低く**する。

```c
#define BLINK_THREAD_PRIORITY   5   /* 短時間だけ起きて描画、すぐ寝る → 高優先度 */
#define REFRESH_THREAD_PRIORITY 10  /* 永久に回り続ける → 低優先度 */
```

こうすることで、`blink_thread`が起きた瞬間に`refresh_thread`をプリエンプトして即座に描画し、`tx_thread_sleep`で再度寝ると`refresh_thread`に制御が戻る、という想定通りの動きになる。

## 7. 動作確認

`pio run`でビルド成功(RAM使用量 3364/32768 bytes = 10.3%、Flash使用量 6428/262144 bytes = 2.5%)。実機書き込み後、ハートが約1Hz(500ms周期)で点滅すれば、2スレッドが正しくプリエンプティブに並行動作していることの確認になる。

## 今後

- 今回はスレッド生成+`tx_thread_sleep`のみの最小構成。ミューテックス・キュー・イベントフラグなど他のThreadX機能は未検証
- `led_mtx`のフレームバッファは`refresh_thread`と`blink_thread`から排他制御なしに共有している。今回の用途(短時間の描画がまれに割り込むだけ)では実害はないが、より複雑な描画を並行して行う場合は`tx_mutex`等での保護を検討する
