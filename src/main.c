/*
 * main.c —— 主控：启动参数、线程装配、并发仲裁
 *
 * 线程分工（每个模块一个线程，模块间只通过 ctrl.h 通信）：
 *   sensor  生产者：手势 → 指令
 *   server  生产者：网络 → 指令
 *   player  消费者：参数 → 声音
 *   display 读者：状态快照 → LCD
 *   main    仲裁者：消费指令队列，决定「谁的话算数」，再落地
 *
 * 为什么仲裁放在主线程而不是各模块里：
 *   双通道（手势 / 远程）会同时对同一参数下指令。若各自直接改参数，
 *   音频侧会听到音量抖动、切歌乱跳。集中到一个消费者，策略只有一处，
 *   出了问题也只查这一个 switch。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "app.h"
#include "common.h"
#include "config.h"
#include "ctrl.h"
#include "sensor.h"
#include "player.h"
#include "effect.h"
#include "effect_ext.h"
#include "display.h"
#include "touch.h"
#include "server.h"
#include "selftest.h"

#define TAG "main"

app_ctx_t    g_app;
volatile int g_quit;

/* 仲裁状态 */
static int64_t    s_last_net_ms;
static ctrl_id_t  s_prev_id;
static ctrl_src_t s_prev_src;
static int64_t    s_prev_ms;
static unsigned long s_suppressed;

static void on_signal(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void usage(const char *argv0)
{
    printf(
        "用法: %s [选项] [音频1.wav 音频2.wav ...]\n"
        "\n"
        "选项:\n"
        "  --port <端口>        TCP 监听端口（默认 %d）\n"
        "  --sensor sim|i2c     传感器后端（默认 sim；i2c 不可用时自动降级）\n"
        "  --no-display         不启动显示线程\n"
        "  --eq-preset <名称>   EQ 内部预设：standard|vocal|demo（默认 standard）\n"
        "  --pitch <半音>       变调不变速（音高移调，±12，默认 0 旁通）\n"
        "  --run <秒>           运行指定秒数后自动退出（自动化测试用）\n"
        "  --log error|warn|info|debug   日志级别（默认 info）\n"
        "  --selftest           跑内置自检后退出（不需要声卡/LCD/传感器）\n"
        "  --dump-bmp <路径>    渲染一帧界面写成 BMP 后退出（检查版式）\n"
        "  --help               显示本帮助\n"
        "\n"
        "示例:\n"
        "  %s --selftest\n"
        "  %s --sensor sim test_44100.wav\n"
        "  %s --port 8888 --log debug /mnt/nfs/audio/demo.wav\n",
        argv0, SERVER_PORT_DEFAULT, argv0, argv0, argv0);
}

static int parse_level(const char *s)
{
    if (!strcmp(s, "error")) return LOG_ERROR;
    if (!strcmp(s, "warn"))  return LOG_WARN;
    if (!strcmp(s, "info"))  return LOG_INFO;
    if (!strcmp(s, "debug")) return LOG_DEBUG;
    return -1;
}

static void banner(void)
{
    printf("\n");
    printf("==================================================\n");
    printf(" %s %s  (%s)\n", APP_NAME, APP_VERSION,
           g_app.board_build ? "BOARD 交叉编译 - 板端固件" : "HOST 构建 - 模拟后端");
    printf(" 队列 %d · 采样 %d Hz/%dch · period %d · 仲裁窗口 %dms\n",
           CTRL_QUEUE_LEN, AUDIO_RATE_HZ, AUDIO_CHANNELS,
           AUDIO_PERIOD_FRAMES, ARB_NET_HOLD_MS);
    printf(" 端口 %d · 传感器 %s · 显示 %s · EQ 预设 %s · 变调 %+.1f 半音 · 曲目 %d 首\n",
           g_app.port, sensor_backend_name(g_app.sensor_backend),
           g_app.display_enabled ? "on" : "off",
           effect_eq_preset_name(effect_eq_preset()),
           effect_pitch_semitones(), g_app.track_count);
    printf("==================================================\n\n");
}

/* ---------------- 仲裁 ---------------- */
static int arbitrate(const ctrl_msg_t *m)
{
    int64_t now = now_ms();

    if (m->src == SRC_NET) s_last_net_ms = now;

    /* 规则 1：远程命令后 ARB_NET_HOLD_MS 内，手势降权 */
    if (m->src == SRC_GESTURE && now - s_last_net_ms < ARB_NET_HOLD_MS) {
        s_suppressed++;
        LOGD(TAG, "手势 %d 被抑制（远程优先窗口内，累计 %lu 次）",
             (int)m->id, s_suppressed);
        return 0;
    }

    /* 规则 2：同源同类指令去抖，防止传感器/网络抖动连击 */
    if (m->id == s_prev_id && m->src == s_prev_src &&
        now - s_prev_ms < ARB_DUP_GUARD_MS) {
        LOGD(TAG, "指令 %d 去抖丢弃（%lld ms 内重复）",
             (int)m->id, (long long)(now - s_prev_ms));
        return 0;
    }

    s_prev_id = m->id;
    s_prev_src = m->src;
    s_prev_ms = now;
    return 1;
}

static void dispatch(const ctrl_msg_t *m)
{
    audio_params_t p;
    int changed = 0;

    switch (m->id) {
    case CMD_PLAY:
    case CMD_RESUME:
        player_request(PLAY_PLAYING);
        LOGT(TAG, "[%s] 播放", ctrl_src_name(m->src));
        break;
    case CMD_PAUSE:
        player_request(PLAY_PAUSED);
        LOGT(TAG, "[%s] 暂停", ctrl_src_name(m->src));
        break;
    case CMD_TOGGLE:
        if (player_current_state() == PLAY_PLAYING) {
            player_request(PLAY_PAUSED);
            LOGT(TAG, "[%s] 暂停", ctrl_src_name(m->src));
        } else {
            player_request(PLAY_PLAYING);
            LOGT(TAG, "[%s] 播放", ctrl_src_name(m->src));
        }
        break;
    case CMD_NEXT:
    case CMD_PREV: {
        int idx = player_skip(m->id == CMD_NEXT ? 1 : -1);
        LOGT(TAG, "[%s] %s → 曲目 %d", ctrl_src_name(m->src),
             m->id == CMD_NEXT ? "下一首" : "上一首", idx + 1);
        break;
    }
    case CMD_VOL:
    case CMD_SPEED:
    case CMD_ECHO:
    case CMD_EQ:
        if (params_apply(m, &changed) == 0 && changed) {
            params_get(&p);
            LOGT(TAG, "[%s] 参数 vol=%d spd=%.1f echo=%d/%d/%d eq=%+d/%+d/%+d",
                 ctrl_src_name(m->src), p.volume, p.speed_x10 / 10.0,
                 p.echo_on, p.echo_delay_ms, p.echo_fb,
                 p.eq_low, p.eq_mid, p.eq_high);
        }
        break;
    case CMD_STATUS_REQ:
        /* 状态查询由 server 就地应答，不进仲裁通道（只读，不产生副作用） */
        break;
    case CMD_QUIT:
        LOGT(TAG, "[%s] 请求退出", ctrl_src_name(m->src));
        g_quit = 1;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    pthread_t th_sensor, th_player, th_display, th_server, th_touch;
    int has_sensor = 0, has_player = 0, has_display = 0, has_server = 0, has_touch = 0;
    int touch_ok = 0;
    int eq_preset = -1;
    float pitch_semitones = 0.0f;
    int i;
    int64_t start;

    memset(&g_app, 0, sizeof g_app);
    g_app.port = SERVER_PORT_DEFAULT;
    g_app.sensor_backend = SENSOR_BACKEND_SIM;
    g_app.display_enabled = 1;
#if defined(BOARD) && BOARD
    g_app.board_build = 1;
#else
    g_app.board_build = 0;
#endif
    g_app.log_level = LOG_INFO;

    /* ---- 参数解析 ---- */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--selftest")) {
            /* 可选跟一个 WAV 路径，用于校验素材参数 */
            const char *wav = NULL;
            if (i + 1 < argc && argv[i + 1][0] != '-') wav = argv[i + 1];
            log_set_level(LOG_ERROR);
            return selftest_run(wav);
        } else if (!strcmp(a, "--port") && i + 1 < argc) {
            g_app.port = atoi(argv[++i]);
        } else if (!strcmp(a, "--sensor") && i + 1 < argc) {
            const char *b = argv[++i];
            g_app.sensor_backend = (!strcmp(b, "i2c")) ? SENSOR_BACKEND_I2C : SENSOR_BACKEND_SIM;
        } else if (!strcmp(a, "--no-display")) {
            g_app.display_enabled = 0;
        } else if (!strcmp(a, "--eq-preset") && i + 1 < argc) {
            const char *n = argv[++i];
            int k, found = -1;
            for (k = 0; k < effect_eq_preset_count(); k++)
                if (!strcmp(n, effect_eq_preset_name(k))) { found = k; break; }
            if (found < 0) {
                fprintf(stderr, "未知 EQ 预设：%s（可选 standard|vocal|demo）\n", n);
                return 2;
            }
            eq_preset = found;
        } else if (!strcmp(a, "--pitch") && i + 1 < argc) {
            pitch_semitones = (float)atof(argv[++i]);
        } else if (!strcmp(a, "--run") && i + 1 < argc) {
            g_app.run_seconds = atoi(argv[++i]);
        } else if (!strcmp(a, "--log") && i + 1 < argc) {
            int lv = parse_level(argv[++i]);
            if (lv < 0) { fprintf(stderr, "未知日志级别：%s\n", argv[i]); return 2; }
            g_app.log_level = lv;
        } else if (!strcmp(a, "--dump-bmp") && i + 1 < argc) {
            g_app.dump_bmp = argv[++i];
        } else if (a[0] == '-') {
            fprintf(stderr, "未知选项：%s\n", a);
            usage(argv[0]);
            return 2;
        } else {
            if (g_app.track_count >= MAX_TRACKS) {
                fprintf(stderr, "曲目超过 %d 个，忽略：%s\n", MAX_TRACKS, a);
            } else {
                g_app.track[g_app.track_count++] = a;
            }
        }
    }

    log_set_level(g_app.log_level);
    os_signal_install(on_signal);

    /* ---- 初始化 ---- */
    ctrl_init();
    if (player_init() != 0) {
        LOGE(TAG, "播放模块初始化失败");
        return 1;
    }
    /* M5Q-06：EQ 预设是内部选项，不进协议；必须在 effect_init 之后设置 */
    if (eq_preset >= 0)
        effect_eq_set_preset(eq_preset);
    /* M5Q-10：变调是内部选项，不进协议；默认 0 旁通 */
    if (pitch_semitones != 0.0f)
        effect_pitch_set_semitones(pitch_semitones);

    /* ---- 版式预览模式：渲染一帧写 BMP 后退出，不起线程 ---- */
    if (g_app.dump_bmp) {
        audio_params_t p;
        status_t *st;

        params_get(&p);
        p.volume = 72;
        p.speed_x10 = 12;
        p.echo_on = 1;
        p.echo_delay_ms = 250;
        p.echo_fb = 40;
        p.eq_low = 3;
        p.eq_mid = -2;
        p.eq_high = 4;
        params_set(&p);

        st = status_lock();
        st->params = p;                       /* 状态快照也要跟着变，否则界面显示旧值 */
        st->state = PLAY_PLAYING;
        st->track_index = 0;
        st->track_count = g_app.track_count > 0 ? g_app.track_count : 1;
        st->pos_ms = 12000;
        st->len_ms = 38000;
        st->distance_mm = 240;
        st->gesture = GESTURE_UP;
        st->clients = 1;
        for (i = 0; i < SCOPE_POINTS; i++) {
            /* 造一段衰减正弦，只为检查波形绘制，不参与播放 */
            double env = 1.0 - (double)i / (SCOPE_POINTS * 1.2);
            st->scope[i] = (int16_t)(12000.0 * env * sin((double)i * 0.32));
        }
        status_unlock();

        display_init();
        if (display_dump_bmp(g_app.dump_bmp) != 0) {
            LOGE(TAG, "写 BMP 失败：%s", g_app.dump_bmp);
            return 1;
        }
        printf("已导出界面预览：%s\n", g_app.dump_bmp);
        display_finish();
        player_finish();
        return 0;
    }

    banner();

    /* ---- 启动模块 ---- */
    if (sensor_init(g_app.sensor_backend) < 0) {
        LOGE(TAG, "传感器初始化失败");
    }
    if (server_init(g_app.port) != 0) {
        LOGE(TAG, "TCP 服务启动失败（端口 %d），网络通道不可用", g_app.port);
    } else {
        has_server = 1;
    }
    display_init();
    has_display = g_app.display_enabled;

    /* 触摸是 M4 的可选输入通道：找不到 gslX680 只告警，不影响播放/显示 */
    touch_ok = (touch_init() == 0);

    if (g_app.track_count > 0)
        player_request(PLAY_PLAYING);
    else
        LOGW(TAG, "未指定音频文件，播放线程空转。示例：%s --sensor sim test_44100.wav", argv[0]);

    /* ---- 起线程 ---- */
    if (pthread_create(&th_sensor, NULL, sensor_thread, NULL) == 0) has_sensor = 1;
    if (pthread_create(&th_player, NULL, player_thread, NULL) == 0) has_player = 1;
    if (has_server && pthread_create(&th_server, NULL, server_thread, NULL) == 0) has_server = 2;
    if (has_display && pthread_create(&th_display, NULL, display_thread, NULL) == 0) has_display = 2;
    if (touch_ok && pthread_create(&th_touch, NULL, touch_thread, NULL) == 0) has_touch = 1;

    LOGT(TAG, "主循环启动（Ctrl-C 退出）");
    start = now_ms();

    /* ---- 主循环：仲裁 + 到期退出 ---- */
    while (!g_quit) {
        ctrl_msg_t m;

        if (ctrl_take(&m, 100) == 0) {
            if (arbitrate(&m)) dispatch(&m);
        }
        if (g_app.run_seconds > 0 &&
            now_ms() - start >= (int64_t)g_app.run_seconds * 1000) {
            LOGT(TAG, "达到 --run %d 秒，自动退出", g_app.run_seconds);
            break;
        }
    }

    g_quit = 1;
    ctrl_finish();

    if (has_display == 2) pthread_join(th_display, NULL);
    if (has_touch)        pthread_join(th_touch, NULL);
    if (has_server == 2)  pthread_join(th_server, NULL);
    if (has_player)       pthread_join(th_player, NULL);
    if (has_sensor)       pthread_join(th_sensor, NULL);

    server_finish();
    display_finish();
    touch_finish();
    sensor_finish();
    player_finish();
    ctrl_finish();

    printf("\n运行结束：抑制手势 %lu 次，队列丢弃 %lu 条\n",
           s_suppressed, ctrl_dropped());
    return 0;
}
