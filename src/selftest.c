/*
 * selftest.c —— 内置自检
 *
 * 覆盖的是「逻辑正确性」，不碰硬件：
 *   参数范围、消息队列、指令翻译、协议解析、回声时序、EQ 旁通、WAV 头解析
 * 现场测试机跑通这一组，就能确认「程序本身没坏」，剩下的问题都在硬件侧。
 *
 * 判定：每条打印 [PASS]/[FAIL]/[SKIP]，退出码 = 失败条数。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "selftest.h"
#include "ctrl.h"
#include "effect.h"
#include "server.h"
#include "player.h"
#include "common.h"
#include "config.h"

static int s_pass, s_fail, s_skip;

static void ok(int cond, const char *name, const char *detail)
{
    if (cond) {
        printf("[PASS] %s\n", name);
        s_pass++;
    } else {
        printf("[FAIL] %s  (%s)\n", name, detail ? detail : "-");
        s_fail++;
    }
}

/* ---------------- 1. 参数默认值与 clamp ---------------- */
static void t_params(void)
{
    audio_params_t p;
    char d[96];

    ctrl_init();
    params_get(&p);
    snprintf(d, sizeof d, "vol=%d speed_x10=%d echo=%d",
             p.volume, p.speed_x10, p.echo_on);
    ok(p.volume == DEF_VOLUME && p.speed_x10 == DEF_SPEED_X10 &&
       p.echo_on == DEF_ECHO_ON, "01 参数默认值", d);

    ok(params_set_volume(150) == VOL_MAX && params_set_volume(-5) == VOL_MIN,
       "02 音量 clamp 到 0..100", "上溢/下溢未夹紧");

    ok(params_set_speed_x10(30) == SPEED_MAX_X10 &&
       params_set_speed_x10(1) == SPEED_MIN_X10,
       "03 速度 clamp 到 0.5..2.0", "上溢/下溢未夹紧");
}

/* ---------------- 2. 消息队列 ---------------- */
static void t_queue(void)
{
    int i, okcnt = 1, order_ok = 1, got = 0;
    ctrl_msg_t m;
    unsigned long dropped;

    ctrl_init();
    for (i = 0; i < CTRL_QUEUE_LEN + 8; i++)
        ctrl_post_cmd(CMD_VOL, SRC_NET, i, 0, 0);

    dropped = ctrl_dropped();
    okcnt = (ctrl_queue_len() == CTRL_QUEUE_LEN);
    ok(okcnt && dropped == 8, "04 队列满时丢弃最旧 8 条",
       "容量或丢弃计数不符");

    /* 队首应是第 8 条（前 8 条被挤掉），且严格递增 */
    {
        int expect = 8;
        while (ctrl_take(&m, 0) == 0) {
            if (m.a != expect) order_ok = 0;
            expect++;
            got++;
        }
    }
    ok(order_ok && got == CTRL_QUEUE_LEN, "05 队列 FIFO 顺序正确", "顺序错乱或取空异常");

    ctrl_take(&m, 0);
    ok(ctrl_take(&m, 50) == -1, "06 空队列超时返回 -1", "空队列未正确超时");
}

/* ---------------- 3. 指令 → 参数翻译 ---------------- */
static void t_apply(void)
{
    ctrl_msg_t m;
    audio_params_t p;

    ctrl_init();

    memset(&m, 0, sizeof m);
    m.id = CMD_ECHO; m.a = 1; m.b = 300; m.c = 55;
    params_apply(&m, NULL);
    m.id = CMD_EQ;   m.a = 1; m.b = -4;
    params_apply(&m, NULL);
    params_get(&p);
    ok(p.echo_on == 1 && p.echo_delay_ms == 300 && p.echo_fb == 55 && p.eq_mid == -4,
       "07 ECHO / EQ 指令正确落到参数", "参数未按指令更新");

    memset(&m, 0, sizeof m);
    m.id = CMD_EQ; m.a = 9; m.b = 3;
    ok(params_apply(&m, NULL) == -2, "08 非法 EQ 频段被拒", "越界频段未被拒绝");

    memset(&m, 0, sizeof m);
    m.id = CMD_PLAY;
    ok(params_apply(&m, NULL) == -1, "09 非参数类指令不误改参数", "PLAY 被当成参数指令");
}

/* ---------------- 4. 协议解析 ---------------- */
static void t_proto(void)
{
    ctrl_msg_t m;
    char err[96];

    memset(&m, 0, sizeof m);
    ok(server_parse_line("VOL 70", &m, err, sizeof err) == 0 &&
       m.id == CMD_VOL && m.a == 70 && m.src == SRC_NET,
       "10 解析 VOL 70", err);

    ok(server_parse_line("speed 1.2", &m, err, sizeof err) == 0 &&
       m.id == CMD_SPEED && m.a == 12,
       "11 解析 speed 1.2 → 12（大小写不敏感）", err);

    ok(server_parse_line("ECHO on 250 40", &m, err, sizeof err) == 0 &&
       m.id == CMD_ECHO && m.a == 1 && m.b == 250 && m.c == 40,
       "12 解析 ECHO on 250 40", err);

    ok(server_parse_line("EQ mid -3", &m, err, sizeof err) == 0 &&
       m.id == CMD_EQ && m.a == 1 && m.b == -3,
       "13 解析 EQ mid -3", err);

    ok(server_parse_line("VOL 999", &m, err, sizeof err) == -2,
       "14 越界指令返回 -2", "越界未被拒绝");

    ok(server_parse_line("EQ xxx 1", &m, err, sizeof err) == -1,
       "15 非法频段返回 -1", "非法参数未被拒绝");
}

/* ---------------- 5. 效果链 ---------------- */
static void t_effect(void)
{
    /* 20k 帧 ≈ 454ms：要能容纳 100ms 延迟 + 余量，不能只用一个 period */
    static int16_t buf[20000 * AUDIO_CHANNELS];
    static int16_t ref[20000 * AUDIO_CHANNELS];
    effect_cfg_t cfg;
    int frames = ARRAY_LEN(buf) / AUDIO_CHANNELS;
    int delay_frames, i;

    effect_init(AUDIO_RATE_HZ, AUDIO_CHANNELS);

    /* 回声：单点脉冲后，延迟位置应出现能量，之前应为静音 */
    memset(buf, 0, sizeof buf);
    buf[0] = 20000;
    buf[1] = 20000;

    memset(&cfg, 0, sizeof cfg);
    cfg.echo_on = 1;
    cfg.echo_delay_ms = 100;
    cfg.echo_fb = 0;
    effect_reset();
    effect_set_cfg(&cfg);
    effect_process(buf, frames);

    delay_frames = AUDIO_RATE_HZ / 10;
    ok(delay_frames < frames && buf[delay_frames * AUDIO_CHANNELS] > 5000,
       "16 回声在 100ms 处出现（幅度 > 5000）", "延迟位置无能量");

    ok(buf[delay_frames / 2 * AUDIO_CHANNELS] == 0,
       "17 回声延迟前的样本保持静音", "提前出现回声，延迟线错位");

    /* 全关旁通：EQ 增益 0 且回声关，输出必须与输入逐字节一致 */
    memset(&cfg, 0, sizeof cfg);
    effect_set_cfg(&cfg);
    for (i = 0; i < (int)ARRAY_LEN(buf); i++) buf[i] = (int16_t)((i * 37) % 4000 - 2000);
    memcpy(ref, buf, sizeof buf);
    effect_process(buf, frames);
    ok(memcmp(buf, ref, sizeof buf) == 0, "18 效果全关时逐字节旁通", "旁通路径改动了信号");

    /* EQ 生效：+12dB 中频必须让 1kHz 正弦的能量显著变大 */
    {
        const double PI2 = 6.283185307179586;
        double rms_in = 0, rms_out = 0;
        for (i = 0; i < frames; i++) {
            double v = 8000.0 * sin(PI2 * 1000.0 * i / AUDIO_RATE_HZ);
            buf[i * 2] = buf[i * 2 + 1] = (int16_t)v;
            rms_in += v * v;
        }
        cfg.eq_mid = 12;
        cfg.echo_on = 0;
        effect_reset();
        effect_set_cfg(&cfg);
        effect_process(buf, frames);
        for (i = 0; i < frames; i++) {
            double v = buf[i * 2];
            rms_out += v * v;
        }
        ok(rms_out > rms_in * 4.0, "19 EQ +12dB 使 1kHz 能量明显增大",
           "EQ 未生效或增益过低");
    }

    effect_finish();
}

/* ---------------- 6. WAV 素材体检 ---------------- */
static void t_wav(const char *path)
{
    wav_info_t info;
    int rc;
    char d[128];

    if (!path) {
        printf("[SKIP] 20 WAV 素材体检（未指定文件，可用 --selftest <文件.wav> 启用）\n");
        s_skip++;
        return;
    }
    rc = player_probe_wav(path, &info);
    snprintf(d, sizeof d, "rc=%d", rc);
    if (rc != 0) {
        ok(0, "20 WAV 素材体检", d);
        return;
    }
    snprintf(d, sizeof d, "%dHz/%dch/%dbit/%.1fs",
             info.rate, info.channels, info.bits, info.seconds);
    ok(info.bits == 16 && info.channels >= 1 && info.channels <= 2 &&
       info.frames > 0, "20 WAV 素材体检", d);
}

int selftest_run(const char *wav_path)
{
    printf("\n===== %s 自检 =====\n", APP_NAME);

    s_pass = s_fail = s_skip = 0;

    t_params();
    t_queue();
    t_apply();
    t_proto();
    t_effect();
    t_wav(wav_path);

    printf("----- 结果：%d 通过 / %d 失败 / %d 跳过 -----\n",
           s_pass, s_fail, s_skip);
    if (s_fail == 0) printf("结论：逻辑层正常，可继续做硬件验证\n");
    else             printf("结论：存在失败项，先修代码再上板，不要怀疑硬件\n");
    return s_fail;
}
