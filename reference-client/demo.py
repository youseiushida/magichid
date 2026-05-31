#!/usr/bin/env python3
"""MagicHID live injection demo.

操作PC(UART) -> ESP32 -> 被操作デバイス(HID) へ実際にマウス/キーボードを注入します。

    python reference-client/demo.py COM8        # COM8 は ESP32 の UART(操作)ポート

⚠ 注意：被操作デバイス＝今あなたが操作しているPCの場合、入力は本当にこのPCに入ります。
   1) まずマウスを左右に振る（無害・フォーカス不要・カーソルが動けば成功）
   2) 5秒カウントダウン → その間に「メモ帳など入力欄」をクリックしてフォーカス
   3) キーボードで文章をタイプ
   暴走対策：終了時に自動 release_all。最悪は USB を抜けば停止。
"""
import sys
import time

from magichid_bridge import HIDBridge, protocol as P

# --- USB HID キーコード (HUT Keyboard/Keypad page 0x07) ---------------------------------
KEYMAP = {}
for i, c in enumerate("abcdefghijklmnopqrstuvwxyz"):
    KEYMAP[c] = (0x00, 0x04 + i)            # 小文字
    KEYMAP[c.upper()] = (0x02, 0x04 + i)    # Shift + 大文字
for i, c in enumerate("1234567890"):
    KEYMAP[c] = (0x00, 0x1E + i)
KEYMAP[" "] = (0x00, 0x2C)
KEYMAP["\n"] = (0x00, 0x28)                 # Enter
KEYMAP["."] = (0x00, 0x37)
KEYMAP["!"] = (0x02, 0x1E)                  # Shift + 1
KEYMAP["-"] = (0x00, 0x2D)


def kbd(b, mod=0, key=0):
    """8バイトのキーボード INPUT レポート [mod][reserved][k1..k6] を送る。"""
    b.send_report("KEYBOARD", bytes([mod, 0, key, 0, 0, 0, 0, 0]))


def type_text(b, text, cps=18):
    delay = 1.0 / cps
    for ch in text:
        mk = KEYMAP.get(ch)
        if mk is None:
            continue                        # マップ外の文字はスキップ
        kbd(b, mk[0], mk[1])                # 押す
        time.sleep(delay)
        kbd(b)                              # 離す（全状態送信＝押しっぱなし防止）
        time.sleep(delay)


def mouse(b, dx, dy):
    """マウス INPUT レポート [buttons][x][y][wheel][pan]（x,y は相対 int8）。"""
    b.send_report("GENERIC_DESKTOP", bytes([0, dx & 0xFF, dy & 0xFF, 0, 0]))


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None   # 引数なし -> 自動検出
    print("[*] " + ("ポート自動検出中 ..." if port is None else f"{port} に接続中 ..."))
    with HIDBridge(port) as b:
        print(f"[+] 接続先: {b.port}")
        if not b.wait_ready(timeout=10):
            print("[!] 被操作デバイス(native USB)が READY ではありません。両方挿さっているか確認してください。")
            print(f"    flags={b.flags} (MOUNTED={bool(b.flags & P.ST_MOUNTED)}, READY={bool(b.flags & P.ST_READY)})")
            return
        print(f"[+] READY  proto v{b.proto_version}  reports={len(b.get_caps())}")

        # --- 1) マウス：左右に振る（無害・フォーカス不要） ---
        print("\n[1] マウスデモ（3秒後）— カーソルが左右に動けば成功")
        time.sleep(3)
        for _ in range(3):
            for _ in range(8):
                mouse(b, 12, 0); time.sleep(0.02)
            for _ in range(8):
                mouse(b, -12, 0); time.sleep(0.02)
        print("    -> マウス注入 完了")

        # --- 2) キーボード：フォーカス用カウントダウン ---
        print("\n[2] キーボードデモ — いまから 5 秒以内に【メモ帳など入力欄】をクリックしてフォーカス！")
        for n in range(5, 0, -1):
            print(f"    {n} ...", end="\r", flush=True)
            time.sleep(1)
        print("    タイプ開始        ")
        type_text(b, "Hello from MagicHID! magichid works 123")
        kbd(b, 0, 0x28)  # Enter
        time.sleep(0.05); kbd(b)

        print("\n[+] 完了。終了時に release_all を自動送信します。")


if __name__ == "__main__":
    main()
