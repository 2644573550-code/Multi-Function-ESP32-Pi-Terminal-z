/*
 * zh_engine.c — 给 flite 加一个中文“音节拼接”语音。
 *
 * 设计要点（与 ENGINE_STATUS.md 对齐）：
 *   - 不走 flite 的 diphone/clustergen 路线：中文没有音素对齐数据；
 *   - 文本 -> 音节（带调数字拼音，如 bian4）-> units/<音节>.wav -> 交叉淡化拼接；
 *   - 声调烧在录音里，不做 F0 变换；
 *   - 单元目录是 voice feature，换老师/换角色 = 换目录。
 *
 * 依赖：flite 源码树（include/ 与 libflite.a），不依赖 Festival。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "flite.h"
#include "cst_alloc.h"
#include "cst_wave.h"
#include "cst_utterance.h"
#include "cst_cg.h"
#include <stdint.h>
#include "cst_lexicon.h"
#include "cst_relation.h"
#include "cst_item.h"
#include "cst_features.h"

#define MAX_WORD_CHARS 8
#define FADE_SAMPLES 80          /* 5 ms @ 16 kHz */
#define OUTPUT_RATE 16000

typedef struct {
    const char *key;
    unsigned key_len;
    const char *value;
} DictEntry;

typedef struct {
    char *blob;
    size_t blob_size;
    DictEntry *entries;
    unsigned count;
} BinaryDict;

static BinaryDict char_dict;
static BinaryDict phrase_dict;

/* 读取二进制词典：一次 read + 指针索引，启动成本与词条数无关 */
static int load_binary_dict(const char *path, BinaryDict *dict)
{
    FILE *handle;
    long size;
    unsigned char *buffer;
    const unsigned char *header;
    unsigned count;
    size_t index_off, pool_off;
    size_t index_size;
    unsigned i;
    handle = fopen(path, "rb");
    if (handle == NULL) {
        cst_errmsg("zh: 打不开词典 %s（先跑 build_binary_dict.py）\n", path);
        return -1;
    }
    fseek(handle, 0, SEEK_END);
    size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    if (size < 52) {
        fclose(handle);
        cst_errmsg("zh: 词典 %s 太小\n", path);
        return -1;
    }
    buffer = cst_alloc(unsigned char, (size_t)size);
    if (fread(buffer, 1, (size_t)size, handle) != (size_t)size) {
        fclose(handle);
        cst_free(buffer);
        cst_errmsg("zh: 词典 %s 读取失败\n", path);
        return -1;
    }
    fclose(handle);
    if (memcmp(buffer, "ZHD1", 4) != 0) {
        cst_free(buffer);
        cst_errmsg("zh: 词典 %s 版本不对\n", path);
        return -1;
    }
    header = buffer;
    count = *(const unsigned int *)(header + 8);
    index_off = (size_t)*(const unsigned long long *)(header + 20);   /* chars_off */
    pool_off = (size_t)*(const unsigned long long *)(header + 36);    /* pool_off */
    index_size = (size_t)count * 16;
    if (index_off + index_size > (size_t)size || pool_off > (size_t)size) {
        cst_free(buffer);
        cst_errmsg("zh: 词典 %s 结构损坏\n", path);
        return -1;
    }
    dict->entries = cst_alloc(DictEntry, count ? count : 1);
    for (i = 0; i < count; ++i) {
        const unsigned char *entry = header + index_off + (size_t)i * 16;
        unsigned key_off = *(const unsigned int *)(entry + 0);
        unsigned key_len = *(const unsigned int *)(entry + 4);
        unsigned val_off = *(const unsigned int *)(entry + 8);
        dict->entries[i].key = (const char *)header + pool_off + key_off;
        dict->entries[i].key_len = key_len;
        dict->entries[i].value = (const char *)header + pool_off + val_off;
    }
    dict->blob = (char *)buffer;
    dict->blob_size = (size_t)size;
    dict->count = count;
    return 0;
}

static const char *dict_lookup(const BinaryDict *dict, const char *key, unsigned key_len)
{
    unsigned low = 0, high = dict->count;
    while (low < high) {
        unsigned middle = low + (high - low) / 2;
        const DictEntry *entry = &dict->entries[middle];
        unsigned common = entry->key_len < key_len ? entry->key_len : key_len;
        int order = memcmp(entry->key, key, common);
        if (order == 0)
            order = (entry->key_len < key_len) ? -1 : (entry->key_len > key_len ? 1 : 0);
        if (order == 0)
            return entry->value;
        if (order < 0)
            low = middle + 1;
        else
            high = middle;
    }
    return NULL;
}

static int ensure_dicts(const char *data_dir)
{
    char path[2048];
    if (char_dict.count != 0)
        return 0;
    snprintf(path, sizeof(path), "%s/zh_dict.bin", data_dir);
    if (load_binary_dict(path, &char_dict) != 0)
        return -1;
    /* 字表与词表在同一个文件里：把词表那一段重新指向同一块内存 */
    {
        const unsigned char *header = (const unsigned char *)char_dict.blob;
        unsigned count = *(const unsigned int *)(header + 12);            /* n_phrases */
        size_t index_off = (size_t)*(const unsigned long long *)(header + 28);  /* phrases_off */
        size_t pool_off = (size_t)*(const unsigned long long *)(header + 36);   /* pool_off */
        unsigned i;
        phrase_dict.blob = char_dict.blob;
        phrase_dict.blob_size = char_dict.blob_size;
        phrase_dict.count = count;
        phrase_dict.entries = cst_alloc(DictEntry, count ? count : 1);
        for (i = 0; i < count; ++i) {
            const unsigned char *entry = header + index_off + (size_t)i * 16;
            phrase_dict.entries[i].key = (const char *)header + pool_off + *(const unsigned int *)(entry + 0);
            phrase_dict.entries[i].key_len = *(const unsigned int *)(entry + 4);
            phrase_dict.entries[i].value = (const char *)header + pool_off + *(const unsigned int *)(entry + 8);
        }
    }
    if (getenv("FLITECN_VERBOSE"))
        fprintf(stderr, "zh: 词典载入 %u 词 / %u 字\n", phrase_dict.count, char_dict.count);
    return 0;
}

/* ---------- 文本 -> 音节片段 ---------- */

typedef struct {
    char **syllables;
    int *pauses;
    int count;
    int capacity;
} SyllableList;

static void list_init(SyllableList *list)
{
    list->syllables = NULL;
    list->pauses = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void list_push(SyllableList *list, const char *syllable, int pause)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->syllables = cst_realloc(list->syllables, char *, list->capacity);
        list->pauses = cst_realloc(list->pauses, int, list->capacity);
    }
    list->syllables[list->count] = cst_strdup(syllable);
    list->pauses[list->count] = pause;
    ++list->count;
}

static void list_free(SyllableList *list)
{
    int index;
    for (index = 0; index < list->count; ++index)
        cst_free(list->syllables[index]);
    cst_free(list->syllables);
    cst_free(list->pauses);
    list_init(list);
}

static int utf8_char_length(const char *text)
{
    unsigned char first = (unsigned char)text[0];
    if (first < 0x80)
        return 1;
    if ((first & 0xE0) == 0xC0)
        return 2;
    if ((first & 0xF0) == 0xE0)
        return 3;
    if ((first & 0xF8) == 0xF0)
        return 4;
    return 1;
}

static int is_han(const char *text)
{
    unsigned char first = (unsigned char)text[0];
    return first >= 0xE4 && first <= 0xE9;
}

static int pause_for(const char *text, int length)
{
    if (length == 3) {
        if (strncmp(text, "，", 3) == 0)
            return 120;
        if (strncmp(text, "、", 3) == 0)
            return 100;
        if (strncmp(text, "。", 3) == 0 || strncmp(text, "！", 3) == 0 || strncmp(text, "？", 3) == 0)
            return 250;
        if (strncmp(text, "；", 3) == 0 || strncmp(text, "：", 3) == 0)
            return 150;
        return 0;
    }
    if (length == 1) {
        switch (text[0]) {
        case ',': return 120;
        case '.': case '!': case '?': return 250;
        case ';': case ':': return 150;
        default: return 0;
        }
    }
    return 0;
}

static void append_syllables(SyllableList *list, const char *readings)
{
    const char *cursor = readings;
    while (*cursor != '\0') {
        char buffer[64];
        int length = 0;
        while (*cursor == ' ')
            ++cursor;
        if (*cursor == '\0')
            break;
        while (*cursor != '\0' && *cursor != ' ' && length < (int)sizeof(buffer) - 1)
            buffer[length++] = *cursor++;
        buffer[length] = '\0';
        if (length > 0)
            list_push(list, buffer, 0);
    }
}


/* ---------- 数字 / 百分比 / 时间 ---------- */

static const char *DIGIT_SYLLABLE[10] = {
    "ling2", "yi1", "er4", "san1", "si4", "wu3", "liu4", "qi1", "ba1", "jiu3"
};

static void read_group_4(SyllableList *list, int value, int bare_ten)
{
    static const int weight[4] = {1000, 100, 10, 1};
    static const char *position[4] = {"qian1", "bai3", "shi2", ""};
    int index, pending_zero = 0, emitted = 0;
    if (value <= 0) {
        list_push(list, DIGIT_SYLLABLE[0], 0);
        return;
    }
    for (index = 0; index < 4; ++index) {
        int digit = (value / weight[index]) % 10;
        if (digit == 0) {
            if (emitted)
                pending_zero = 1;
            continue;
        }
        if (pending_zero) {
            list_push(list, DIGIT_SYLLABLE[0], 0);
            pending_zero = 0;
        }
        if (!(bare_ten && index == 2 && digit == 1 && !emitted))
            list_push(list, DIGIT_SYLLABLE[digit], 0);
        if (position[index][0] != '\0')
            list_push(list, position[index], 0);
        emitted = 1;
    }
}

static void read_number(SyllableList *list, unsigned long long value)
{
    unsigned long long yi = value / 100000000ULL;
    unsigned long long wan = (value / 10000ULL) % 10000ULL;
    unsigned long long rest = value % 10000ULL;
    int need_zero = 0;
    if (value == 0) {
        list_push(list, DIGIT_SYLLABLE[0], 0);
        return;
    }
    if (yi) {
        read_group_4(list, (int)yi, yi < 20);
        list_push(list, "yi4", 0);
        need_zero = (wan == 0 || wan < 1000);
    }
    if (wan) {
        if (need_zero)
            list_push(list, DIGIT_SYLLABLE[0], 0);
        read_group_4(list, (int)wan, yi == 0 && wan < 20);
        list_push(list, "wan4", 0);
        need_zero = (rest < 1000);
    }
    if (rest) {
        if (need_zero && (yi || wan))
            list_push(list, DIGIT_SYLLABLE[0], 0);
        read_group_4(list, (int)rest, (yi == 0 && wan == 0 && value < 20));
    }
}

/* 返回消费掉的字节数 */
static int append_number(SyllableList *list, const char *text)
{
    const char *cursor = text;
    unsigned long long value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (unsigned long long)(*cursor - '0');
        ++cursor;
    }
    /* 四位数字 + “年”：按位读（2026 -> 二零二六），更符合中文习惯 */
    if (cursor - text == 4) {
        const char *probe = cursor;
        while (*probe == ' ')
            ++probe;
        if ((unsigned char)probe[0] == 0xE5 && (unsigned char)probe[1] == 0xB9 &&
            (unsigned char)probe[2] == 0xB4) {
        const char *scan = text;
        while (scan < cursor) {
            list_push(list, DIGIT_SYLLABLE[(int)(*scan - '0')], 0);
            ++scan;
        }
        return (int)(cursor - text);
        }
    }
    if (*cursor == ':' && cursor[1] >= '0' && cursor[1] <= '9') {
        const char *scan = cursor + 1;
        unsigned long long minutes = 0;
        while (*scan >= '0' && *scan <= '9') {
            minutes = minutes * 10 + (unsigned long long)(*scan - '0');
            ++scan;
        }
        read_number(list, value);
        list_push(list, "dian3", 0);
        if (minutes < 10) {
            list_push(list, DIGIT_SYLLABLE[0], 0);
            list_push(list, DIGIT_SYLLABLE[(int)minutes], 0);
        } else {
            read_number(list, minutes);
        }
        /* HH:MM:SS：补“分”，再读秒并补“秒” */
        if (*scan == ':' && scan[1] >= '0' && scan[1] <= '9') {
            const char *third = scan + 1;
            unsigned long long seconds = 0;
            while (*third >= '0' && *third <= '9') {
                seconds = seconds * 10 + (unsigned long long)(*third - '0');
                ++third;
            }
            list_push(list, "fen1", 0);
            if (seconds < 10) {
                list_push(list, DIGIT_SYLLABLE[0], 0);
                list_push(list, DIGIT_SYLLABLE[(int)seconds], 0);
            } else {
                read_number(list, seconds);
            }
            list_push(list, "miao3", 0);
            return (int)(third - text);
        }
        return (int)(scan - text);
    }
    {
        const char *scan = cursor;
        int is_percent = 0;
        if (*scan == '.' && scan[1] >= '0' && scan[1] <= '9') {
            ++scan;
            while (*scan >= '0' && *scan <= '9')
                ++scan;
        }
        if (*scan == '%')
            is_percent = 1;
        if (is_percent) {
            list_push(list, "bai3", 0);
            list_push(list, "fen1", 0);
            list_push(list, "zhi1", 0);
        }
        read_number(list, value);
        if (*cursor == '.' && cursor[1] >= '0' && cursor[1] <= '9') {
            list_push(list, "dian3", 0);
            ++cursor;
            while (*cursor >= '0' && *cursor <= '9') {
                list_push(list, DIGIT_SYLLABLE[(int)(*cursor - '0')], 0);
                ++cursor;
            }
        }
        if (is_percent && *cursor == '%')
            ++cursor;
    }
    return (int)(cursor - text);
}

static void text_to_syllables(const char *text, SyllableList *list)
{
    const char *cursor = text;
    list_init(list);
    while (*cursor != '\0') {
        int length = utf8_char_length(cursor);
        int pause;
        if ((cursor[0] == '-' || (unsigned char)cursor[0] == 0xEF) &&
            cursor[1] >= '0' && cursor[1] <= '9') {
            list_push(list, "fu4", 0);
            ++cursor;
            continue;
        }
        if (cursor[0] >= '0' && cursor[0] <= '9') {
            cursor += append_number(list, cursor);
            continue;
        }
        if ((cursor[0] == 'U' || cursor[0] == 'u') && (cursor[1] == 'T' || cursor[1] == 't') &&
            (cursor[2] == 'C' || cursor[2] == 'c') &&
            (cursor[3] == '+' || cursor[3] == '-' || (cursor[3] >= '0' && cursor[3] <= '9'))) {
            const char *probe = cursor + 3;
            int west = 0;
            if (*probe == '+') ++probe;
            else if (*probe == '-') { west = 1; ++probe; }
            if (*probe >= '0' && *probe <= '9') {
                const char *scan = probe;
                unsigned long long hour = 0, minute = 0;
                while (*scan >= '0' && *scan <= '9') {
                    hour = hour * 10 + (unsigned long long)(*scan - '0');
                    ++scan;
                }
                if (*scan == ':' && scan[1] >= '0' && scan[1] <= '9') {
                    const char *rest = scan + 1;
                    while (*rest >= '0' && *rest <= '9') {
                        minute = minute * 10 + (unsigned long long)(*rest - '0');
                        ++rest;
                    }
                    scan = rest;
                }
                list_push(list, "xie2", 0);
                list_push(list, "tiao2", 0);
                list_push(list, "shi4", 0);
                list_push(list, "jie4", 0);
                list_push(list, "shi2", 0);
                list_push(list, west ? "xi1" : "dong1", 0);
                read_number(list, hour);
                list_push(list, "qu1", 0);
                if (minute) {
                    read_number(list, minute);
                    list_push(list, "fen1", 0);
                }
                cursor = scan;
                continue;
            }
        }
        if ((cursor[0] >= 'A' && cursor[0] <= 'Z') || (cursor[0] >= 'a' && cursor[0] <= 'z')) {
            char name[16];
            snprintf(name, sizeof(name), "ltr_%c", tolower((unsigned char)cursor[0]));
            list_push(list, name, 0);
            ++cursor;
            continue;
        }
        /* 最长匹配：先试多字词，再退到单字 */
        if (is_han(cursor)) {
            char buffer[64];
            int best_chars = 0;
            int best_bytes = 0;
            const char *best_value = NULL;
            int chars = 0, bytes = 0;
            const char *probe = cursor;
            while (*probe != '\0' && chars < MAX_WORD_CHARS && is_han(probe)) {
                int step = utf8_char_length(probe);
                memcpy(buffer + bytes, probe, step);
                bytes += step;
                chars += 1;
                probe += step;
                buffer[bytes] = '\0';
                {
                    const char *value = dict_lookup(&phrase_dict, buffer, (unsigned)strlen(buffer));
                    if (value != NULL) {
                        best_chars = chars;
                        best_bytes = bytes;
                        best_value = value;
                    }
                }
            }
            if (best_value == NULL) {
                char single[8];
                memcpy(single, cursor, length);
                single[length] = '\0';
                best_value = dict_lookup(&char_dict, single, (unsigned)strlen(single));
                best_chars = 1;
                best_bytes = length;
            }
            if (best_value != NULL) {
                append_syllables(list, best_value);
                cursor += best_bytes;
                (void)best_chars;
                continue;
            }
            cursor += length;   /* 词典没有的字：跳过 */
            continue;
        }
        pause = pause_for(cursor, length);
        if (pause > 0 && list->count > 0)
            list->pauses[list->count - 1] += pause;
        cursor += length;
    }
}

/* ---------- 音节 -> 波形 ---------- */

static void wave_append(cst_wave *out, const short *samples, int count)
{
    int old = out->num_samples;
    out->samples = cst_realloc(out->samples, short, old + count);
    memcpy(out->samples + old, samples, sizeof(short) * count);
    out->num_samples = old + count;
}

static void wave_append_silence(cst_wave *out, int count)
{
    int index;
    if (count <= 0)
        return;
    out->samples = cst_realloc(out->samples, short, out->num_samples + count);
    for (index = 0; index < count; ++index)
        out->samples[out->num_samples + index] = 0;
    out->num_samples += count;
}

static void wave_append_faded(cst_wave *out, const cst_wave *unit)
{
    int count = unit->num_samples;
    int fade = FADE_SAMPLES;
    int index;
    if (count <= 0)
        return;
    if (out->num_samples < fade || count <= fade) {
        wave_append(out, unit->samples, count);
        return;
    }
    /* 先把新单元的前 fade 段与已有尾部叠加，再追加剩余部分 */
    for (index = 0; index < fade; ++index) {
        int old_index = out->num_samples - fade + index;
        double weight = (double)index / (double)fade;
        out->samples[old_index] = (short)(out->samples[old_index] * (1.0 - weight) + unit->samples[index] * weight);
    }
    wave_append(out, unit->samples + fade, count - fade);
}


/* ---- 单文件声库（.zhvox）：header + 索引 + PCM，全内存拼接 ---- */
typedef struct {
    char name[16];
    uint32_t offset;
    uint32_t samples;
} BankEntry;
static unsigned char *bank_bytes = NULL;
static BankEntry *bank_entries = NULL;
static uint32_t bank_count_ = 0;
static const short *bank_pcm = NULL;
static int bank_loaded = 0;

static uint32_t bank_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int load_voice_bank(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    uint32_t count, index;
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 16 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return -1;
    }
    bank_bytes = malloc((size_t)size);
    if (bank_bytes == NULL || fread(bank_bytes, 1, (size_t)size, file) != (size_t)size) {
        fclose(file); return -1;
    }
    fclose(file);
    if (memcmp(bank_bytes, "ZHV1", 4) != 0 || bank_u32(bank_bytes + 4) != 1 ||
        bank_u32(bank_bytes + 8) != OUTPUT_RATE) {
        cst_errmsg("zh: 声库格式不支持：%s\n", path);
        return -1;
    }
    count = bank_u32(bank_bytes + 12);
    if (count == 0 || 16 + (size_t)count * 24 > (size_t)size) {
        cst_errmsg("zh: 声库索引损坏：%s\n", path);
        return -1;
    }
    bank_entries = malloc((size_t)count * sizeof(BankEntry));
    if (bank_entries == NULL) return -1;
    for (index = 0; index < count; ++index) {
        const unsigned char *item = bank_bytes + 16 + index * 24;
        memcpy(bank_entries[index].name, item, 16);
        bank_entries[index].name[15] = '\0';
        bank_entries[index].offset = bank_u32(item + 16);
        bank_entries[index].samples = bank_u32(item + 20);
        if ((uint64_t)bank_entries[index].offset + bank_entries[index].samples >
            (uint64_t)(size - (long)(16 + count * 24)) / 2) {
            cst_errmsg("zh: 声库条目越界：%s\n", path);
            return -1;
        }
    }
    bank_count_ = count;
    bank_pcm = (const short *)(bank_bytes + 16 + count * 24);
    bank_loaded = 1;
    if (getenv("FLITECN_VERBOSE"))
        fprintf(stderr, "zh: 声库 %s，%u 单元\n", path, count);
    return 0;
}

static const BankEntry *bank_find(const char *name)
{
    uint32_t index;
    for (index = 0; index < bank_count_; ++index)
        if (strcmp(bank_entries[index].name, name) == 0) return &bank_entries[index];
    return NULL;
}

static void wave_append_pcm(cst_wave *out, const short *samples, int count)
{
    int old_index, index;
    const int fade = FADE_SAMPLES;
    if (count <= 0) return;
    if (count > fade && out->num_samples > fade) {
        for (index = 0; index < fade; ++index) {
            old_index = out->num_samples - fade + index;
            out->samples[old_index] = (short)(out->samples[old_index] * (1.0 - (double)index / fade) +
                                              samples[index] * ((double)index / fade));
        }
        wave_append(out, samples + fade, count - fade);
        return;
    }
    wave_append(out, samples, count);
}

static cst_utterance *zh_wave_synth(cst_utterance *u)
{
    const char *unit_dir = get_param_string(u->features, "unit_dir", NULL);
    const char *fallback_dir = get_param_string(u->features, "fallback_unit_dir", NULL);
    cst_relation *segments = utt_relation(u, "Segment");
    cst_wave *out;
    cst_item *segment;
    int produced = 0;

    if (!bank_loaded && unit_dir == NULL) {
        cst_errmsg("zh: voice 缺少 unit_dir 或 FLITECN_BANK\n");
        return u;
    }
    out = new_wave();
    out->sample_rate = OUTPUT_RATE;
    out->num_channels = 1;
    out->num_samples = 0;
    out->samples = NULL;

    for (segment = relation_head(segments); segment != NULL; segment = item_next(segment)) {
        const char *name = item_feat_string(segment, "name");
        int pause = item_feat(segment, "zh_pause") != NULL
            ? item_feat_int(segment, "zh_pause") : 0;
        char path[2048];
        cst_wave *unit;
        const BankEntry *entry;
        if (name == NULL || name[0] == '\0')
            continue;
        if (bank_loaded) {
            entry = bank_find(name);
            if (entry == NULL) {
                if (getenv("FLITECN_VERBOSE")) fprintf(stderr, "zh: 声库缺少 %s\n", name);
            } else {
                wave_append_pcm(out, bank_pcm + entry->offset, (int)entry->samples);
                produced += 1;
                if (pause > 0)
                    wave_append_silence(out, pause * OUTPUT_RATE / 1000);
            }
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s.wav", unit_dir, name);
        if (access(path, R_OK) != 0 && fallback_dir != NULL) {
            snprintf(path, sizeof(path), "%s/%s.wav", fallback_dir, name);
        }
        unit = new_wave();
        if (access(path, R_OK) != 0 || cst_wave_load_riff(unit, path) != CST_OK_FORMAT) {
            cst_errmsg("zh: 缺少单元 %s\n", path);
            delete_wave(unit);
            continue;
        }
        wave_append_faded(out, unit);
        delete_wave(unit);
        produced += 1;
        if (pause > 0)
            wave_append_silence(out, pause * OUTPUT_RATE / 1000);
    }
    if (getenv("FLITECN_VERBOSE"))
        fprintf(stderr, "zh: 拼接 %d 个音节，%d 采样\n", produced, out->num_samples);
    utt_set_wave(u, out);
    return u;
}

static cst_utterance *zh_noop(cst_utterance *u)
{
    return u;
}

static cst_utterance *zh_textanalysis(cst_utterance *u)
{
    SyllableList list;
    cst_relation *segments;
    cst_item *item;
    int index;
    text_to_syllables(utt_input_text(u), &list);
    if (getenv("FLITECN_VERBOSE")) {
        int probe;
        fprintf(stderr, "zh: 音节序列");
        for (probe = 0; probe < list.count; ++probe)
            fprintf(stderr, " %s", list.syllables[probe]);
        fprintf(stderr, "\n");
    }
    utt_relation_create(u, "Syllable");   /* cg 合成要求该关系存在 */
    segments = utt_relation_create(u, "Segment");
    for (index = 0; index < list.count; ++index) {
        item = relation_append(segments, NULL);
        item_set_string(item, "name", list.syllables[index]);
        if (list.pauses[index] > 0)
            item_set_int(item, "zh_pause", list.pauses[index]);
    }
    list_free(&list);
    return u;
}

static void zh_install_stage_hooks(cst_voice *voice)
{
    /* 除了文本分析和波形合成，其余 flite 阶段一律跳过 */
    feat_set(voice->features, "tokenizer_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "textanalysis_func", uttfunc_val(&zh_textanalysis));
    feat_set(voice->features, "pos_tagger_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "phrasing_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "lexical_insertion_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "pause_insertion_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "intonation_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "duration_model_func", uttfunc_val(&zh_noop));
    feat_set(voice->features, "f0_model_func", uttfunc_val(&zh_noop));
}

cst_voice *zh_voice(const char *data_dir, const char *unit_dir)
{
    cst_voice *voice;
    if (ensure_dicts(data_dir) != 0)
        return NULL;
    voice = new_voice();
    feat_set_string(voice->features, "name", "zh_syllable");
    if (getenv("FLITECN_BANK") != NULL && load_voice_bank(getenv("FLITECN_BANK")) == 0)
        feat_set_int(voice->features, "has_bank", 1);
    else if (unit_dir != NULL)
        feat_set_string(voice->features, "unit_dir", unit_dir);
    if (getenv("FLITECN_FALLBACK") != NULL)
        feat_set_string(voice->features, "fallback_unit_dir", getenv("FLITECN_FALLBACK"));
    feat_set_string(voice->features, "data_dir", data_dir);
    zh_install_stage_hooks(voice);
    feat_set(voice->features, "wave_synth_func", uttfunc_val(&zh_wave_synth));
    return voice;
}

/* 英文 slt flitevox 需要真实的英文语言/词典实现；中文 cg 路径已删除。 */
extern void usenglish_init(cst_voice *v);
extern cst_lexicon *cmulex_init(void);
static const cst_lang en_lang_table[] = {
    {"eng", usenglish_init, cmulex_init},
    {NULL, NULL, NULL}
};

cst_voice *zh_english_voice(const char *voice_path)
{
    return cst_cg_load_voice(voice_path, en_lang_table);
}

int zh_text_is_chinese(const char *text)
{
    int index = 0;
    while (text[index] != '\0') {
        int length = utf8_char_length(text + index);
        if (length > 0 && is_han(text + index))
            return 1;
        index += (length > 0) ? length : 1;
    }
    return 0;
}

