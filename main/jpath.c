// main/jpath.c —— jpath.h 的实现。纯 C,无外部依赖,可在主机上测试。
#include "jpath.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------- 内部:极简 JSON 只读游标 ----------

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// 跳过一个 JSON 值(对象/数组/字符串/数字/true/false/null),返回其结束位置。
// 结构损坏返回 NULL。深度由递归调用方约束(JSON 文档很小,不做显式上限)。
static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '{' || *p == '[') {
        const char open = *p;
        const char close = (open == '{') ? '}' : ']';
        p = skip_ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            p = skip_ws(p);
            if (open == '{') {
                if (*p != '"') return NULL;
                p = skip_value(p); // key
                if (!p) return NULL;
                p = skip_ws(p);
                if (*p != ':') return NULL;
                p = skip_ws(p + 1);
            }
            p = skip_value(p); // value / element
            if (!p) return NULL;
            p = skip_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == close) return p + 1;
            return NULL;
        }
    }
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') {
                if (!p[1]) return NULL;
                // \uXXXX 至少 4 个十六进制位
                if (p[1] == 'u') {
                    for (int i = 2; i <= 5; i++)
                        if (!isxdigit((unsigned char)p[i])) return NULL;
                    p += 6;
                } else {
                    p += 2;
                }
            } else {
                p++;
            }
        }
        return (*p == '"') ? p + 1 : NULL;
    }
    if (strncmp(p, "true", 4) == 0) return p + 4;
    if (strncmp(p, "false", 5) == 0) return p + 5;
    if (strncmp(p, "null", 4) == 0) return p + 4;
    // 数字:JSON 语法允许的完整形态
    {
        const char *start = p;
        if (*p == '-' || *p == '+') p++;
        if (!isdigit((unsigned char)*p)) return NULL;
        while (isdigit((unsigned char)*p)) p++;
        if (*p == '.') { p++; if (!isdigit((unsigned char)*p)) return NULL; while (isdigit((unsigned char)*p)) p++; }
        if (*p == 'e' || *p == 'E') {
            p++;
            if (*p == '+' || *p == '-') p++;
            if (!isdigit((unsigned char)*p)) return NULL;
            while (isdigit((unsigned char)*p)) p++;
        }
        return (p > start) ? p : NULL;
    }
}

// 在当前位置读一个原始 token 的拷贝(数字或去引号字符串)到 buf。
// 返回 0 成功;buf 太小返回 JPATH_ERR_RANGE。
static int copy_raw_token(const char *begin, const char *end, char *buf, size_t buf_len)
{
    size_t n = (size_t)(end - begin);
    if (n + 1 > buf_len) return JPATH_ERR_RANGE;
    memcpy(buf, begin, n);
    buf[n] = '\0';
    return JPATH_OK;
}

static int hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

// 把 JSON 字符串字面量(含首尾引号)还原成 UTF-8 写入 out。
static int unescape_string(const char *begin, const char *end, char *out, size_t out_len)
{
    // begin 指向开引号,end 指向闭引号后。
    const char *p = begin + 1;
    const char *last = end - 1;
    size_t used = 0;
    while (p < last) {
        uint32_t cp;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"': cp = '"'; break;
            case '\\': cp = '\\'; break;
            case '/': cp = '/'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'n': cp = '\n'; break;
            case 'r': cp = '\r'; break;
            case 't': cp = '\t'; break;
            case 'u': {
                if (last - p < 5) return JPATH_ERR_MALFORMED;
                cp = (uint32_t)hex4(p + 1);
                if ((int)cp < 0) return JPATH_ERR_MALFORMED;
                p += 4;
                // 代理对:UTF-16 高代理后必须跟 \uXXXX 低代理
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (last - p < 7 || p[1] != '\\' || p[2] != 'u') return JPATH_ERR_MALFORMED;
                    int lo = hex4(p + 3);
                    if (lo < 0 || lo < 0xDC00 || lo > 0xDFFF) return JPATH_ERR_MALFORMED;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
                    p += 6;
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return JPATH_ERR_MALFORMED; // 孤立低代理
                }
                break;
            }
            default: return JPATH_ERR_MALFORMED;
            }
            p++;
        } else {
            // 源要求是合法 UTF-8;此处按字节透传,由调用方保证编码
            cp = (unsigned char)*p;
            size_t seq = 1;
            unsigned char b = (unsigned char)*p;
            if ((b & 0xE0) == 0xC0) seq = 2;
            else if ((b & 0xF0) == 0xE0) seq = 3;
            else if ((b & 0xF8) == 0xF0) seq = 4;
            if (seq > (size_t)(last - p)) return JPATH_ERR_MALFORMED;
            // 校验续字节
            for (size_t k = 1; k < seq; k++)
                if (((unsigned char)p[k] & 0xC0) != 0x80) return JPATH_ERR_MALFORMED;
            if (used + seq >= out_len) return JPATH_ERR_RANGE;
            memcpy(out + used, p, seq);
            used += seq;
            p += seq;
            continue;
        }
        // cp → UTF-8
        char tmp[4];
        size_t seq;
        if (cp < 0x80) { tmp[0] = (char)cp; seq = 1; }
        else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); seq = 2; }
        else if (cp < 0x10000) { tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[2] = (char)(0x80 | (cp & 0x3F)); seq = 3; }
        else { tmp[0] = (char)(0xF0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); seq = 4; }
        if (used + seq >= out_len) return JPATH_ERR_RANGE;
        memcpy(out + used, tmp, seq);
        used += seq;
    }
    out[used] = '\0';
    return JPATH_OK;
}

// 定位到 path 指向的值,返回值起始指针(指向值的第一个非空白字符);
// *value_end 回填值结束位置。失败返回 NULL。
static const char *locate(const char *json, const char *path, const char **value_end)
{
    if (!json || !path) return NULL;
    const char *p = skip_ws(json);
    const char *seg = path;
    bool first = true;

    while (*seg) {
        // 取一段 key 或下标
        const char *dot = strchr(seg, '.');
        size_t seg_len = dot ? (size_t)(dot - seg) : strlen(seg);
        if (seg_len == 0) return NULL;
        char key[64];
        if (seg_len >= sizeof(key)) return NULL;
        memcpy(key, seg, seg_len);
        key[seg_len] = '\0';

        bool is_index = (key[0] >= '0' && key[0] <= '9');
        for (size_t i = 0; is_index && i < seg_len; i++)
            if (key[i] < '0' || key[i] > '9') is_index = false;

        p = skip_ws(p);
        if (is_index) {
            if (*p != '[') return NULL;
            size_t want = (size_t)strtoul(key, NULL, 10);
            p = skip_ws(p + 1);
            if (*p == ']') return NULL; // 空数组
            for (size_t idx = 0;; idx++) {
                if (idx == want) {
                    // p 已在元素起点;下方统一结束本段
                    break;
                }
                p = skip_value(p);
                if (!p) return NULL;
                p = skip_ws(p);
                if (*p == ']') return NULL; // 越界
                if (*p != ',') return NULL;
                p = skip_ws(p + 1);
            }
        } else {
            if (*p != '{') return NULL;
            if (!first) {
                // 上一个值如果是对象/数组,skip 已经推进;这里 p 必须是 '{'
            }
            p = skip_ws(p + 1);
            if (*p == '}') return NULL; // 空对象
            for (;;) {
                p = skip_ws(p);
                if (*p != '"') return NULL;
                const char *kbegin = p;
                const char *kend = skip_value(p);
                if (!kend) return NULL;
                p = skip_ws(kend);
                if (*p != ':') return NULL;
                p = skip_ws(p + 1);
                if ((size_t)(kend - kbegin - 2) == strlen(key) && memcmp(kbegin + 1, key, strlen(key)) == 0) {
                    break; // 命中
                }
                p = skip_value(p);
                if (!p) return NULL;
                p = skip_ws(p);
                if (*p == '}') return NULL; // 键不存在
                if (*p != ',') return NULL;
                p = skip_ws(p + 1);
            }
        }
        first = false;
        // 前进到下一段
        if (!dot) break;
        seg = dot + 1;
    }

    p = skip_ws(p);
    const char *end = skip_value(p);
    if (!end) return NULL;
    if (value_end) *value_end = end;
    return p;
}

// ---------- 公开接口 ----------

int jpath_get_string(const char *json, const char *path, char *out, size_t out_len)
{
    if (!json || !path || !out || out_len == 0) return JPATH_ERR_ARGS;
    const char *end = NULL;
    const char *v = locate(json, path, &end);
    if (!v) return JPATH_ERR_NOT_FOUND;
    if (*v == '"') {
        // end 指向闭引号之后
        const char *close = end - 1;
        if (close < v || *close != '"') return JPATH_ERR_MALFORMED;
        return unescape_string(v, end, out, out_len);
    }
    // 数字/布尔按原文拷贝(便于统一 "110.00" / 110.00 两种写法)
    return copy_raw_token(v, end, out, out_len);
}

int jpath_get_number(const char *json, const char *path, double *out)
{
    if (!json || !path || !out) return JPATH_ERR_ARGS;
    char raw[64];
    int rc = jpath_get_string(json, path, raw, sizeof(raw));
    if (rc != JPATH_OK) return rc;
    char *endp = NULL;
    double d = strtod(raw, &endp);
    if (endp == raw || (endp && *endp != '\0')) {
        // "available" 之类的布尔/词也按不可解析处理
        if (strcmp(raw, "true") == 0) { *out = 1.0; return JPATH_OK; }
        if (strcmp(raw, "false") == 0) { *out = 0.0; return JPATH_OK; }
        return JPATH_ERR_NOT_FOUND;
    }
    *out = d;
    return JPATH_OK;
}

int jpath_get_array_len(const char *json, const char *path, size_t *count)
{
    if (!json || !path || !count) return JPATH_ERR_ARGS;
    const char *end = NULL;
    const char *v = locate(json, path, &end);
    if (!v) return JPATH_ERR_NOT_FOUND;
    if (*v != '[') return JPATH_ERR_NOT_FOUND;
    const char *p = skip_ws(v + 1);
    size_t n = 0;
    if (*p == ']') { *count = 0; return JPATH_OK; }
    for (;;) {
        p = skip_value(p);
        if (!p) return JPATH_ERR_MALFORMED;
        n++;
        p = skip_ws(p);
        if (*p == ']') break;
        if (*p != ',') return JPATH_ERR_MALFORMED;
        p = skip_ws(p + 1);
    }
    *count = n;
    return JPATH_OK;
}

int jpath_find_in_array_by_string(const char *json, const char *array_path,
                                  const char *type_key, const char *want_type,
                                  size_t *out_index)
{
    if (!json || !array_path || !type_key || !want_type || !out_index) return JPATH_ERR_ARGS;
    size_t count = 0;
    int rc = jpath_get_array_len(json, array_path, &count);
    if (rc != JPATH_OK) return rc;
    char elem_path[96];
    char got[40];
    for (size_t i = 0; i < count; i++) {
        if (snprintf(elem_path, sizeof(elem_path), "%s.%zu.%s", array_path, i, type_key) >= (int)sizeof(elem_path))
            return JPATH_ERR_RANGE;
        rc = jpath_get_string(json, elem_path, got, sizeof(got));
        if (rc == JPATH_OK && strcmp(got, want_type) == 0) {
            *out_index = i;
            return JPATH_OK;
        }
    }
    return JPATH_ERR_NOT_FOUND;
}
