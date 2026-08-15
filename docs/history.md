# 開発記録

## 1. プロジェクト作成とFramework選定

PlatformIOのProject WizardでArduino UNO R4 WiFi向けプロジェクトを作成。Frameworkの選択肢はArduino・Fsp・CMISの3つ。

最終的にThreadX(だめならFreeRTOS)を載せる計画があったため、RTOS統合が公式にサポートされているFSPを選択した(詳細はblog.md参照)。

その後、デフォルトの保存先(`OneDrive\ドキュメント\PlatformIO\Projects\blink_led`)に作成されてしまっていたプロジェクトを、作業用ディレクトリ`workspace\arduino_r4\led_blink`に移動。`.pio`(ビルドキャッシュ)は移動せず削除し、次回ビルド時に再生成させた。

## 2. 最初のLチカ(失敗)

`src/main.c`に`hal_entry()`を実装し、`bsp_pin_cfg.h`で定義されている`LED_BLUE`(`BSP_IO_PORT_00_PIN_13`)を`R_BSP_PinWrite()`でHigh/Lowトグルする、素朴なLチカを書いた。

```c
R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);
R_BSP_PinAccessEnable();
while (1) {
    R_BSP_PinWrite(LED_BLUE, BSP_IO_LEVEL_HIGH);
    R_BSP_SoftwareDelay(500, BSP_DELAY_UNITS_MILLISECONDS);
    R_BSP_PinWrite(LED_BLUE, BSP_IO_LEVEL_LOW);
    R_BSP_SoftwareDelay(500, BSP_DELAY_UNITS_MILLISECONDS);
}
```

ビルド・書き込みは成功したが、実機で確認すると単独のLEDではなく、**LEDマトリクス上の2個が交互点滅+1個が常時点灯**という想定外の挙動になった。

### 原因調査

UNO R4 WiFiには昔のUnoにあったような単独の「L」LEDは存在せず、オンボードLEDは全て12×8=96個のLEDを**チャープレキシング(Charlieplexing)**で駆動するマトリクスに集約されている。`bsp_pin_cfg.h`で`LED_RED`/`TX_LED`/`LED_BLUE`/`RX_LED`という名前が付いていたピン(`P011`, `P012`, `P013`, `P201`)は、実体はこのマトリクスの駆動ピンの一部だった。

チャープレキシングは「1本をHigh出力、別の1本をLow出力にし、それ以外は全てHi-Z(入力)にする」という組み合わせで初めて1個のLEDだけが点灯する仕組み。`pin_data.c`で複数のマトリクスピンが同時に出力状態(Low/High固定)にされていたため、複数の電流経路ができて誤点灯していた。

## 3. シルク「L」を試す(こちらも点灯せず)

クラシックUnoでは「L」LEDはピン13(SPIのSCK共有)に直結されている。同じ構成を期待して`D13_SCK`(`BSP_IO_PORT_01_PIN_11`)をGPIO出力に再設定してトグルしてみたが、光らなかった。

Arduino公式リポジトリ`arduino/ArduinoCore-renesas`の`variants/UNOWIFIR4/pins_arduino.h`を確認したところ、`LED_BUILTIN`は`13`と定義されてはいるものの、実体はGPIO直結ではなく、Arduino本体コアの`digitalWrite()`がpin 13への書き込みを特殊対応してLEDマトリクスの1ピクセルとして処理している(=そもそも独立したハードウェアLEDが存在しない)ことを確認。ここで「Lは深追いしない」方針に転換。

## 4. LEDマトリクス駆動ドライバの自作(成功)

`src/led_mtx.h` / `src/led_mtx.c`として、チャープレキシング方式のLEDマトリクスを直接制御する自前ドライバを実装。

- マトリクスは`P003, P004, P011, P012, P013, P015, P204, P205, P206, P212, P213`の11本のGPIOで駆動されている
- 11本のピン順序と、LEDインデックス(0〜95)ごとの{アノード, カソード}対応表は、Arduino公式の`arduino/ArduinoCore-renesas`リポジトリ(`libraries/Arduino_LED_Matrix/src/Arduino_LED_Matrix.h`、`variants/UNOWIFIR4/variant.cpp`の`g_pin_cfg`)を調査して移植した。FSPのピン設定だけでは物理結線までは分からないため
- `led_mtx_init()`: 11本を一旦全てHi-Zにする
- `led_mtx_set(x, y, on)`: 12×8のフレームバッファに点灯状態を書き込む
- `led_mtx_refresh()`: 点灯すべきピクセルを1つずつ「アノードHigh・カソードLow、他は全部Hi-Z」にして短時間(300us)光らせては戻す、を繰り返す。`main.c`の`while(1)`で回し続けることで残像効果により形が見える

`main.c`ではハート形のビットマップを`led_mtx_set()`で流し込み、`led_mtx_refresh()`を無限ループで呼ぶだけのシンプルな構成にした。実機で書き込み、ハート形の表示に成功。

## 5. ThreadX移植とスレッド化 (2026/07/17)

かねての計画通り、FSPの上にThreadXを移植した。詳細な手順は`docs/threadx_construction.md`に別途まとめたので、ここでは要点のみ記録する。

`framework-renesas-fsp`パッケージにはRenesas製のThreadX Cortex-M4ポート層(`rm_threadx_port`、PendSVスケジューラやSysTickタイマ割り込みなど)が同梱されていたが、ビルドスクリプトの対象外で未使用だった。加えてThreadXカーネル本体(`tx_thread_create`等を実装する`common/`以下)はそもそもパッケージに含まれていなかったため、`https://github.com/eclipse-threadx/threadx`(Eclipse Foundation管理・MITライセンス、`v6.5.1.202602a_rel`)から別途取得した。両者を新規`lib/threadx/`ライブラリとしてまとめ、`platformio.ini`やフレームワークパッケージ本体には手を加えずに済ませた。

Renesas製ポートの`tx_initialize_low_level.c`は、e2studioのSmart Configuratorが生成する前提のシンボル(`__RAM_segment_used_end__`によるリンカ由来の空きRAM取得、`TX_PORT_CFG_SYSTICK_IPL`)に依存していたが、いずれもこのプロジェクトには存在しないため、固定サイズの静的バッファとデフォルト優先度値で代替した。

`src/main.c`は`hal_entry()`内の`while(1) { led_mtx_refresh(); }`を`tx_kernel_enter()`に置き換え、`tx_application_define()`で2スレッドを生成する構成にした: `led_mtx_refresh()`を回し続ける`refresh_thread`と、`tx_thread_sleep()`で500msごとにハート表示のON/OFFを切り替える`blink_thread`。

### ハマりどころ: スレッド優先度の付け方

最初は「常に動かしたい`refresh_thread`」を優先度1(高)、「たまに起きる`blink_thread`」を優先度5(低)にしたところ、ビルドは通るが実機では何も光らずハングしたように見えた。原因はThreadXの優先度の理解不足で、数値が小さいほど高優先度のプリエンプティブスケジューラでは、自分からブロックしない`refresh_thread`を高優先度にした時点で、同等以下の優先度の`blink_thread`は`tx_thread_sleep`から目覚めても永久にCPUをもらえなくなる。「自分からブロックせず回り続けるスレッドほど優先度を低くする」のが正しく、`refresh_thread`を優先度10、`blink_thread`を優先度5に入れ替えて解決した。

実機書き込みでハートが約1Hzで点滅することを確認。2スレッドがプリエンプティブに並行動作していることの最小限の動作確認ができた。

## 今後

- LEDマトリクスドライバの排他制御なしの共有(`refresh_thread`と`blink_thread`が同じフレームバッファを触っている)は、今回程度の用途では実害ないが、より複雑な描画を行う場合は`tx_mutex`等の導入を検討する
- ミューテックス・キュー・イベントフラグなど、ThreadXの他機能はまだ未検証
