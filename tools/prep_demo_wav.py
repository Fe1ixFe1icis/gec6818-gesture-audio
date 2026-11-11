#!/usr/bin/env python3
"""
prep_demo_wav.py -- 把大 WAV 裁成适合串口部署的板端素材

播放器只接受 PCM(1) / 16bit / 1~2 声道；本工具做三件事：
  1) 按秒裁剪（默认 30s）
  2) 可选降为单声道
  3) 可选重采样到 44100 / 22050（板端输出固定 44.1k，重采样会一并处理）

用法：
  python tools/prep_demo_wav.py demo.wav demo_30s_44k_mono.wav --seconds 30 --rate 44100 --mono
  python tools/prep_demo_wav.py demo.wav demo_30s_22k_mono.wav --seconds 30 --rate 22050 --mono
  python tools/prep_demo_wav.py demo.wav demo_30s_44k_stereo.wav --seconds 30 --rate 44100

输出始终是 PCM 16bit；文件名建议带参数，避免和源文件混淆。
"""
import argparse
import warnings

# audioop 在 Python 3.13 被移除；本项目开发机是 3.11，先压掉弃用提示
warnings.filterwarnings("ignore", category=DeprecationWarning)
import audioop
import wave


def main():
    ap = argparse.ArgumentParser(description="裁剪/降混/重采样 WAV（PCM 16bit）")
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--seconds", type=float, default=30.0, help="保留时长，默认 30s")
    ap.add_argument("--start", type=float, default=0.0, help="起始秒，默认 0")
    ap.add_argument("--rate", type=int, default=44100, help="目标采样率，默认 44100")
    ap.add_argument("--mono", action="store_true", help="降为单声道")
    args = ap.parse_args()

    with wave.open(args.src, "rb") as r:
        if r.getsampwidth() != 2:
            raise SystemExit("只支持 16bit PCM WAV")
        ch, rate, n = r.getnchannels(), r.getframerate(), r.getnframes()
        start = int(args.start * rate)
        frames = int(args.seconds * rate)
        r.setpos(min(start, n))
        raw = r.readframes(min(frames, max(0, n - start)))

    if args.mono and ch == 2:
        raw = audioop.tomono(raw, 2, 0.5, 0.5)
        ch = 1

    if args.rate != rate:
        raw, _ = audioop.ratecv(raw, 2, ch, rate, args.rate, None)
        rate = args.rate

    with wave.open(args.dst, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(raw)

    dur = len(raw) / (ch * 2 * rate)
    print(f"{args.dst}: {ch}ch / {rate}Hz / {dur:.1f}s / {len(raw)/1024/1024:.2f} MiB")


if __name__ == "__main__":
    main()
