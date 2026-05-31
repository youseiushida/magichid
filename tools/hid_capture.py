#!/usr/bin/env python3
"""実機HIDコントローラの入力レポートを採取し、ボタン→ビットの対応を割り出す。

用途: Nintendo Switch Pro Controller など実機を PC に挿し、各ボタンを順に押すと
      「どのバイトのどのビットが立つか」を自動マッピングして JSON / ログに残す。
      その記録をもとに MagicHID 側のクローン記述子・送信レイアウトを起こす。

準備:
    pip install hidapi          # Windows は wheel に DLL 同梱、これだけでOK

使い方:
    python tools/hid_capture.py                 # 既定 057E:2009 (Pro Controller) をガイド採取
    python tools/hid_capture.py 0F0D 00C1        # 別の VID PID を指定
    python tools/hid_capture.py --raw            # 変化したレポートを垂れ流す(スティック等)

ガイド採取は「押して保持→Enter / 離して→Enter」を各ボタンで繰り返すだけ。
読み取り専用(書き込みしない)ので実機の状態は変えない。Ctrl+C でいつでも中断。
"""
import json
import sys
import time

try:
    import hid
except ImportError:
    sys.exit("hidapi が無い。  pip install hidapi  を実行してから再試行。")

# Switch Pro Controller のボタン(押す順)。横/肩ボタンや +/- も含め一通り。
BUTTONS = [
    "A", "B", "X", "Y",
    "L", "R", "ZL", "ZR",
    "Minus(-)", "Plus(+)", "Home", "Capture",
    "LStick-press", "RStick-press",
    "Dpad-Up", "Dpad-Down", "Dpad-Left", "Dpad-Right",
]

REPORT_LEN = 64


# ---- hid パッケージ差異の吸収 (trezor 'hidapi' / apmorton 'hid' どちらでも) ----
def open_device(vid, pid):
    if hasattr(hid, "device"):                       # trezor hidapi
        d = hid.device()
        d.open(vid, pid)
        try:
            d.set_nonblocking(True)
        except Exception:
            pass
        info = {
            "manufacturer": _safe(d.get_manufacturer_string),
            "product": _safe(d.get_product_string),
            "serial": _safe(d.get_serial_number_string),
        }
        return "trezor", d, info
    if hasattr(hid, "Device"):                       # apmorton hid
        d = hid.Device(vid=vid, pid=pid)
        info = {
            "manufacturer": getattr(d, "manufacturer", None),
            "product": getattr(d, "product", None),
            "serial": getattr(d, "serial", None),
        }
        return "apmorton", d, info
    sys.exit("未知の hid パッケージ。 pip install hidapi を入れ直して。")


def _safe(fn):
    try:
        return fn()
    except Exception:
        return None


def read_report(kind, d):
    if kind == "trezor":
        return d.read(REPORT_LEN)                     # 非ブロッキング: [] か list[int]
    try:
        data = d.read(REPORT_LEN, timeout=10)         # apmorton: bytes, timeout ms
        return list(data)
    except Exception:
        return []


def drain_latest(kind, d, want_id=None, settle=0.18):
    """settle 秒読み続け、最後に来たレポートを返す。want_id 指定時はその先頭IDのみ採用。"""
    last = None
    t_end = time.time() + settle
    while time.time() < t_end:
        r = read_report(kind, d)
        if r and (want_id is None or (r and r[0] == want_id)):
            last = r
    return last


def changed(rest, held):
    """rest を基準に held で変化したバイトを列挙。"""
    out = []
    n = max(len(rest or []), len(held or []))
    for i in range(n):
        a = rest[i] if i < len(rest) else 0
        b = held[i] if i < len(held) else 0
        if a != b:
            out.append({"byte": i, "rest": a, "held": b, "xor": a ^ b})
    return out


def hexs(r):
    return " ".join(f"{x:02x}" for x in r) if r else "(none)"


def describe(diff):
    if not diff:
        return "変化なし"
    parts = []
    for c in diff:
        x = c["xor"]
        bits = [f"bit{b}" for b in range(8) if x & (1 << b)]
        tag = f"byte{c['byte']}=0x{c['xor']:02x}"
        if bits and (x & (x - 1)) == 0:              # 単一ビット = デジタルボタンらしい
            tag += f"({bits[0]})"
        elif bits:
            tag += f"({'+'.join(bits)})"
        parts.append(tag)
    return ", ".join(parts)


def raw_mode(kind, d):
    print("[raw] 変化したレポートを表示。Ctrl+C で終了。")
    prev = None
    t0 = time.time()
    try:
        while True:
            r = read_report(kind, d)
            if r and r != prev:
                print(f"[+{time.time()-t0:7.2f}s] {hexs(r)}")
                prev = r
            elif not r:
                time.sleep(0.002)
    except KeyboardInterrupt:
        print("\n[raw] 終了")


def guided(kind, d, info):
    print("=== MagicHID button mapper ===")
    print(f"  product   : {info.get('product')}")
    print(f"  manufact. : {info.get('manufacturer')}")
    print("各ボタンで [押して保持→Enter] → [離して→Enter] を繰り返します。")
    print("※スティックは触らないで(ノイズ防止)。最後に --raw で別途取れます。\n")
    input("何も押してない状態にして Enter ...")
    rest = drain_latest(kind, d)
    if not rest:
        sys.exit("レポートが流れてこない。USB挿し直し or --init が要るかも。先頭で相談して。")
    stream_id = rest[0]
    print(f"基準レポート(id=0x{stream_id:02x}, {len(rest)}B): {hexs(rest)}\n")

    results = {}
    for name in BUTTONS:
        input(f"[{name}] 押して保持したまま Enter ...")
        held = drain_latest(kind, d, want_id=stream_id)
        input(f"[{name}] 離して Enter ...")
        rel = drain_latest(kind, d, want_id=stream_id) or rest
        diff = changed(rel, held)                    # 直前の離し状態を基準に差分
        results[name] = {"held": held, "diff": diff}
        print(f"   -> {describe(diff)}\n")

    out = {
        "device": info,
        "stream_report_id": stream_id,
        "baseline": rest,
        "buttons": results,
    }
    path = "tools/proc_buttons.json"
    with open(path, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print("=== まとめ ===")
    for name in BUTTONS:
        print(f"  {name:14s}: {describe(results[name]['diff'])}")
    print(f"\n保存しました: {path}  (これを見せて or 自分で確認)")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    vid = int(args[0], 16) if len(args) >= 1 else 0x057E
    pid = int(args[1], 16) if len(args) >= 2 else 0x2009
    print(f"[*] open VID=0x{vid:04X} PID=0x{pid:04X} ...")
    kind, d, info = open_device(vid, pid)
    print(f"[+] opened ({kind})  product={info.get('product')}")
    try:
        if "--raw" in flags:
            raw_mode(kind, d)
        else:
            guided(kind, d, info)
    finally:
        try:
            d.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
