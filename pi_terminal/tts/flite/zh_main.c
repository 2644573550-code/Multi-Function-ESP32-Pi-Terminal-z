/*
 * zh_main.c — flite_zh 命令行：把中文文本合成为 16 kHz 单声道 WAV。
 *
 *   flite_zh -t "关闭当前应用" -o out.wav
 *   FLITECN_DATA=../lexicon FLITECN_BANK=../voices/xiaoxiao/voice.zhvox flite_zh "你好"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "flite.h"

extern cst_voice *zh_voice(const char *data_dir, const char *unit_dir);
extern cst_voice *zh_english_voice(const char *voice_path);
extern int zh_text_is_chinese(const char *text);

static void usage(void)
{
    fprintf(stderr,
        "用法: flite_zh [-o 输出.wav] [-t 文本] [文本...]\n"
        "环境变量:\n"
        "  FLITECN_DATA   词典目录（char_dict.txt / engine_dict.txt），默认 ./data\n"
        "  FLITECN_BANK   单文件声库（voice.zhvox），优先于 FLITECN_UNITS\n"
        "  FLITECN_UNITS  开发用音节目录（*.wav），默认 ./units\n"
        "  FLITECN_EN_VOICE  英文 flitevox（cmu_us_slt）；纯英文文本走它\n");
}

int main(int argc, char **argv)
{
    const char *output = "out.wav";
    const char *text = NULL;
    const char *data_dir = getenv("FLITECN_DATA");
    const char *unit_dir = getenv("FLITECN_UNITS");
    const char *en_voice_path = getenv("FLITECN_EN_VOICE");
    cst_voice *voice;
    cst_wave *wave;
    struct timespec start, finish;
    int index;
    double elapsed_ms;

    flite_init();
    if (data_dir == NULL)
        data_dir = "./data";
    if (unit_dir == NULL)
        unit_dir = "./units";

    for (index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-o") == 0 && index + 1 < argc) {
            output = argv[++index];
        } else if (strcmp(argv[index], "-t") == 0 && index + 1 < argc) {
            text = argv[++index];
        } else if (strcmp(argv[index], "-h") == 0 || strcmp(argv[index], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[index][0] != '-') {
            text = argv[index];
        } else {
            usage();
            return 1;
        }
    }
    if (text == NULL) {
        usage();
        return 1;
    }

    if (!zh_text_is_chinese(text) && en_voice_path != NULL && en_voice_path[0] != '\0') {
        voice = zh_english_voice(en_voice_path);
    } else {
        voice = zh_voice(data_dir, unit_dir);
    }
    if (voice == NULL) {
        fprintf(stderr, "flite_zh: 初始化中文语音失败\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    wave = flite_text_to_wave(text, voice);
    clock_gettime(CLOCK_MONOTONIC, &finish);
    if (wave == NULL) {
        fprintf(stderr, "flite_zh: 合成失败\n");
        return 1;
    }
    if (cst_wave_save_riff(wave, output) != CST_OK_FORMAT) {
        fprintf(stderr, "flite_zh: 写不了 %s\n", output);
        return 1;
    }
    elapsed_ms = (finish.tv_sec - start.tv_sec) * 1e3 + (finish.tv_nsec - start.tv_nsec) / 1e6;
    fprintf(stderr, "flite_zh: %s -> %s  %.2f s 音频，耗时 %.0f ms，RTF %.3f\n",
            text, output, (double)wave->num_samples / wave->sample_rate,
            elapsed_ms, elapsed_ms / 1000.0 / ((double)wave->num_samples / wave->sample_rate));
    return 0;
}
