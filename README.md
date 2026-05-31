# MagicHID — 透過HIDブリッジ（ESP32-S3）

操作PCから、被操作デバイス（PC / スマホ / Switch 等）を **任意のHID** として操作するための
「透過プロキシ」ブリッジです。ESP32-S3 は中身を解釈しない中継に徹し、ロジックは操作PC側の
クライアントに置きます。設計の全体像は **[DESIGN.md](DESIGN.md)** を参照。

中核の `hid_descriptor.h` は、USB-IF「HID Usage Tables (HUT) 1.7」が定義する **全35 Usage Page**
を個別 Report ID として単一の `uint8_t const desc_hid_report[]` にまとめた “キメラ” HID レポート
記述子です。各ページに代表的な Input / Output / Feature レイアウトを実装し、全バイトに HUT 章番号
付きの注釈を付け、原典と1つずつ照合済みです。

- ターゲット: **ESP32-S3**（ネイティブUSB = Full-Speed, 12 Mbps）
- USBスタック: **Adafruit TinyUSB**
- 全 Usage Page と Usage ID は `docs/hut1_7.ocr.md`（HUT 1.7）と1つずつ照合済み。

---

## システム構成（透過HIDブリッジ）

```
 操作PC + client ──UART(framed)──▶ ESP32-S3 ──USB HID(native)──▶ 被操作デバイス
   (任意言語; 脳)                    (中継+安全装置)                (PC/スマホ/Switch)
```

**契約（contract）＝ `spec/` が本体**で、これさえあれば誰でも任意言語でクライアントを作れます。
`reference-client/`（Python）は**参考実装であり、削除可能**です。

| 区分 | ファイル | 役割 |
|---|---|---|
| **契約** | `spec/PROTOCOL.md` | ワイヤ仕様（言語非依存・権威）。これだけでクライアント実装可能 |
| **契約** | `spec/protocol.yaml` | 定数の**単一ソース**。`gen_protocol.py` が各言語へ展開 |
| **契約** | `spec/reports.json` | Report ID のサイズ表（`GET_CAPS` で実行時取得も可） |
| **契約** | `spec/protocol_vectors.txt` | 適合性ベクタ（自作 codec を byte 単位で検証） |
| **ファーム** | `magichid.ino` | 本体：列挙・中継・双方向・安全装置・identity/プロファイル |
| **ファーム** | `hid_descriptor.h` | 被操作に提示する“正体”（全35 Report ID の記述子） |
| **ファーム** | `mh_protocol.h` ＋ `mh_protocol_defs.h`[生成] | UART codec（COBS+CRC16）＋生成定数 |
| **ファーム** | `mh_reports.h` [生成] | レポートサイズ表（C） |
| **ツール** | `tools/gen_protocol.py` | `protocol.yaml` → `mh_protocol_defs.h` / `_defs.py` / vectors |
| **ツール** | `tools/gen_reports.py` | `hid_descriptor.h` → `mh_reports.h` / `spec/reports.json` |
| **参考(削除可)** | `reference-client/` | Python 参考クライアント（framing・セッション・信頼配送・統一API） |
| **設計** | `DESIGN.md` | 設計書（プロトコル・責務境界・全部入りの決定） |

## ビルドと実行

### 1. 生成（記述子 or プロトコルを変えたら毎回）
```
python tools/gen_reports.py     # hid_descriptor.h  -> mh_reports.h, spec/reports.json
python tools/gen_protocol.py    # spec/protocol.yaml -> mh_protocol_defs.h, _defs.py, vectors
```

### 2. ファームウェア（ESP32-S3）— ★実機ビルド＆列挙 検証済み

**Arduino IDE の Tools 設定：**
- **USB Mode: `USB-OTG (TinyUSB)`**
- **USB CDC On Boot: `Enabled`** ← ⚠**必須**。`Disabled` だと ESP32-S3 では USB-OTG が起動せず、
  native USB が ROM の USB-Serial-JTAG のままで **HID が列挙されません**（Adafruit の `begin()` は ESP32 で
  USBハードを初期化せず、起動はコア任せ＝CDC On Boot 有効時のみ作動）。有効化すると native USB は CDC＋HID 複合になります。
- Flash Size / PSRAM はボードに合わせる（例: 8MB / OPI PSRAM）。
- 接続：**操作PC → UART0**（基板の UART/COM ポート, 1 Mbps）、**被操作デバイス → ネイティブ USB** ポート。接続順は不問。

**arduino-cli での検証済みコマンド（ESP32-S3, 8MB / OPI PSRAM の例）：**
```
arduino-cli compile --upload \
  --fqbn esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=8M,PSRAM=opi,UploadMode=cdc \
  --libraries "<Adafruit lib を含む libraries フォルダ>" -p COMxx .
```
- `CDCOnBoot=cdc` ＝ USB CDC On Boot 有効（上記の必須設定）。
- ネイティブUSB経由で焼くときは `UploadMode=cdc`（1200bpsタッチで自動リセット）。UARTブリッジ経由なら不要。
- 実機（ESP32-S3 / arduino-esp32 3.3.8）で **全35ページが Windows に HID として列挙**・PING/STATUS/GET_CAPS 往復まで確認済み。

### 3. クライアント
- **自作する場合**：`spec/PROTOCOL.md` ＋ `spec/protocol_vectors.txt` だけで任意言語で実装可。
- **参考実装(Python)を使う場合**：
```
pip install -r reference-client/requirements.txt
python reference-client/example.py COM5     # COM5 は ESP32 の UART ポート
```
```python
from magichid_bridge import HIDBridge, BY_NAME
with HIDBridge("COM5") as b:
    b.wait_ready()
    b.send_report("KEYBOARD", bytes([0,0,0x04,0,0,0,0,0]))  # 'a' を押す
    b.send_report("KEYBOARD", bytes(8))                     # 離す（全状態送信）
```
- `send_report(report, data)` は Report ID（番号 or 名前）＋生バイト。**全35種を同一API**で扱う。
- 信頼配送（ACK/再送）は既定 ON、`reliable=False` で投げっぱなしも可。終了時に自動 `release_all()`。

### 4. 適合性検証（任意）
```
gcc -I. tools/test_protocol_parity.c -o parity && ./parity spec/protocol_vectors.txt
PYTHONPATH=reference-client python tools/test_protocol_parity.py
```
両者が golden ベクタ（`spec/protocol_vectors.txt`）に一致 ＝ C/Python/サードパーティが同一契約に適合。

## 動作確認した環境

| 項目 | 値 |
|---|---|
| ボード | ESP32-S3 |
| IDE | Arduino IDE |
| USBスタック | Adafruit TinyUSB Library（`Documents/Arduino/libraries/Adafruit_TinyUSB_Library`） |
| コア | arduino-esp32 **3.3.8**（`packages/esp32/tools/esp32s3-libs/3.3.8`） |

---

## 使い方（概略）

```cpp
#include "Adafruit_TinyUSB.h"
#include "hid_descriptor.h"

Adafruit_USBD_HID usb_hid;

void setup() {
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.begin();
}

// 各レポートは Report ID を指定して送信する
//   usb_hid.sendReport(REPORT_ID_KEYBOARD, &kbd, sizeof(kbd));
//   usb_hid.sendReport(REPORT_ID_GAMEPAD ... )  等
```

Report ID は `hid_descriptor.h` 内の `enum`（`REPORT_ID_GENERIC_DESKTOP` … `REPORT_ID_FIDO`、値は 1..35）で定義済み。

---

## ⚠ 実機での注意（重要）

### 1. HID 1レポートの最大サイズは 64 バイト（既定値）

ESP32-S3 + arduino-esp32 3.3.8 では、TinyUSB の HID バッファが **`CONFIG_TINYUSB_HID_BUFSIZE = 64`** です。
（確認元: `tools/esp32s3-libs/3.3.8/sdkconfig` および `…/dio_qspi/include/sdkconfig.h` に
`#define CONFIG_TINYUSB_HID_BUFSIZE 64`）

このバッファは **「1バイトの Report ID + データ」** を格納するため、**実際に送れるデータは最大 63 バイト** です
（`hid_device.c`：`tu_memcpy_s(epin + 1, CFG_TUD_HID_EP_BUFSIZE - 1, report, len)`）。
Feature / Output（GET/SET_REPORT, 制御転送経由）も同じバッファで上限 64 に制限されます。

本記述子で **63 バイトを超える＝そのままでは送れない** のは次の **3レポートだけ**。残り 32 レポートは無改造で動作します。

| Report ID | ページ | サイズ | 状態 |
|---|---|---|---|
| 24 Monitor | EDID Feature | 130 B | 上限超過（切り詰め/拒否） |
| 31 MSR | Input | 229 B | 上限超過（送信不可） |
| 35 FIDO | In/Out | 64 B + ID 1 = 65 B | 上限超過（送信不可） |

> **通常用途（ゲームパッド / キーボード / マウス等の小さいレポート）では一切問題になりません。**

**バッファを増やしたい場合**（重要：Arduino IDE では実体がプリコンパイル済みコアライブラリ内にあるため、
スケッチ側の `-D CFG_TUD_HID_EP_BUFSIZE=…` フラグでは変わりません）:

- **ESP-IDF を使う**：`idf.py menuconfig` → *Component config → TinyUSB Stack → HID* の
  HID バッファサイズ（`CONFIG_TINYUSB_HID_BUFSIZE`）を 256 などに変更して再ビルド。
- もしくは **arduino-esp32 のコアライブラリを再ビルド**（esp32-arduino-lib-builder で sdkconfig を変更）。
- Arduino IDE のまま使うなら、**該当 3 レポートを使わない／63 バイト以下に分割・縮小** するのが手早い。

### 2. Power(27) / Battery(28) は OS の電源管理に直結する

ホスト（特に Windows）はこの 2 レポートを **システム UPS / バッテリーとして束縛** することがあります。

- 起動直後にデータを流さない、もしくは **「残量 100% / AC 接続済み」の安全値を最優先で送る** こと。
- `critical / on-battery / 0%` のような値（や起動時のゼロ）を送ると、OS がバッテリーアイコンを表示し、
  最悪スリープ / シャットダウンに動く余地があります。
- テスト中はこの Report ID を送信しないだけで実害はほぼ回避できます。

（同じ注意書きを `hid_descriptor.h` の Report 27 / 28 のコメントにも記載しています。）

### 3. FIDO(35) は本物の認証器としては “別インターフェース” が必須

CTAPHID 規格は **Report ID なし・正確に 64 バイト** のフレームを要求します。本記述子は複数 Report ID の
単一インターフェースのため ID 付与が強制され、列挙はできても **FIDO としては機能しません**。

実運用する場合は、FIDO だけ 2 つ目の HID インターフェース（`Adafruit_USBD_HID` をもう 1 基、
Report ID なし・64 バイト固定、`TUD_HID_REPORT_DESC_FIDO_U2F(64)`）に分離してください。
`CFG_TUD_HID = 2`（最大 2 HID インターフェース）は確保済みです。

### 4. （補足）全部を 1 インターフェースに載せる構成について

これは「全ページを正しく列挙・パースできるデモ／雛形カタログ」です。ホストによっては複合デバイスや
特定の名乗りに対して選り好みすることがあるため、実機ターゲット（Switch / スマホ / 他PC）ごとに
通るか **実測** し、必要なら **ターゲット別の単一記述子に切り出す** 運用が堅実です。

### 5. Barcode(29) は Windows の「POS HID Barcode scanner」ドライバが当たり Error になる

実機（Windows 11）で列挙すると、Report 29（Usage Page `0x8C` / Barcode Scanner）のノード**だけ**が
デバイスマネージャーで **`POS HID Barcode scanner`（class: BarcodeScanner）＝ Error** になります（他 34 ページは `OK`）。

- **原因（一次資料）**：Windows は USB 接続バーコードスキャナ用の **在庫(in-box)クラスドライバ**を持ち、これは
  USB-IF の **「HID POS Scanner Usage Table (0x8C)」** に基づきます。Microsoft 公式曰く
  *“Windows contains an in-box class driver for USB connected barcode scanners which is based on the HID POS
  Scanner Usage Table (8c) specification … configure your scanner in **USB.HID.POS Scanner** mode.”*
  （[Point of Service Hardware Support](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/pos/device-support)）。
  本記述子はページ `0x8C` を名乗るため Windows が自動的にこの POS ドライバを束縛しますが、当方のレポートは
  **[HUT 1.7](https://www.usb.org/sites/default/files/hut1_7.pdf) §32 の代表的サブセット**であって
  *USB.HID.POS Scanner モードの完全な記述子*（§32.2 の *Scanned Data Report* 等の論理コレクション階層＋Symbology 等）
  ではないため、ドライバが弾いて Error になります。
- **これは HUT 準拠の不備ではなく、Windows POS クラスドライバ固有の要求**です。本プロジェクトはページを
  **HUT §32 に忠実**に保つ方針で、**ベンダー固有化も Windows-POS 向けの作り込みも行いません**。
- **影響範囲**：USB 列挙自体は成立し、firmware は Report 29 を送出可能。Error は「この Windows PC の POS
  クラスドライバが当たらない」だけで、**他 34 ページの動作には無関係**。被操作側が別OS / 汎用HID で読むアプリ /
  スマホ / Switch 等なら、この Windows 固有の POS ドライバ問題は発生しません。
- **唯一の是正（HUT 準拠として実施済み）**：`Aiming/Pointer Mode (0x30)` は §32.3 で **Static Flag (SF)＝
  スキャナ能力フラグ**、§32.2 で *Attribute Report = Feature report* と定義されます。誤って Output にしていたのを
  **Feature に修正**しました（Windows を満たすための変更ではなく、記述子としての正しさの修正）。

> POS スキャナとして **このPC上で** 動かしたい場合は、§32.2 の完全な論理コレクション構造（Scanned Data Report 等）
> ＋必要に応じてベンダー実装が要ります。ドライバ作成は Microsoft の
> [Barcode Scanner Driver Design Guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/_pos/index) 参照。

---

## 記述子の検証（実施済み）

`gcc` でスタブマクロと結合してコンパイルし、生成バイト列を自作 HID パーサで検査済みです。

- `gcc -std=c11 -Wall -Wextra` で **警告ゼロ** でコンパイル（C構文・カンマ抜けなし）
- Report ID は **1..35 が重複なく全揃い**
- **全 Collection が均衡**（`0xA1` ↔ `0xC0` 一致、最大ネスト 2）
- 全レポートの Input / Output / Feature ビット総和が **8 の倍数（バイト整列）**
- **グローバルアイテムの “漏れ” なし**（各レポートが ReportSize / Count / LogicalMin / Max を自前で再宣言）
- 全 Usage ID を HUT 1.7 原典と照合し、Usage 種別の取り違えを是正済み：
  Scales「Pound」値誤り 0x5B→0x5C／Barcode「Aiming/Pointer Mode」0x30 を Output→Feature
  （§32.3 の Static Flag、§32.2 で Attribute Report=Feature）に修正

---

## Sources

**一次資料（HID 仕様・OS ドライバ要件）**
- [HID Usage Tables for USB 1.7 — USB-IF（原典）](https://www.usb.org/document-library/hid-usage-tables-17) ／ [直リンク PDF](https://www.usb.org/sites/default/files/hut1_7.pdf) ／ [HID 仕様一覧](https://www.usb.org/hid) — §32 Barcode Scanner Page (0x8C) 含む全 Usage の定義元
- [Point of Service Hardware Support — Microsoft](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/pos/device-support) — Windows 在庫の「HID POS Scanner (0x8C)」barcode クラスドライバと `USB.HID.POS Scanner` モード要件（実機注意 #5 の根拠）
- [Barcode Scanner Driver Design Guide — Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/_pos/index) ／ [Barcode Scanner Driver Sample](https://learn.microsoft.com/en-us/windows-hardware/drivers/pos/barcode-scanner-driver) — POS バーコードドライバの作成 DDI

**ESP32-S3 / TinyUSB**
- [Developing with Native tinyusb — ESP-IoT-Solution](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/tinyusb_development_guide.html)
- [USB Device Stack (ESP32-S3) — ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_device.html)
- [espressif/esp_tinyusb — ESP Component Registry](https://components.espressif.com/components/espressif/esp_tinyusb)
- [TinyUSB Changelog（`CFG_TUD_HID_BUFSIZE` → `CFG_TUD_HID_EP_BUFSIZE` 改名）](https://docs.tinyusb.org/en/stable/info/changelog.html)
- ローカル確認: `…/esp32/tools/esp32s3-libs/3.3.8/sdkconfig`（`CONFIG_TINYUSB_HID_BUFSIZE=64`）、
  `Adafruit_TinyUSB_Library/src/arduino/ports/esp32/tusb_config_esp32.h`、
  `Adafruit_TinyUSB_Library/src/class/hid/hid_device.{c,h}`
