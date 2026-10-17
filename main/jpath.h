// main/jpath.h —— 面向固定小响应的极简 JSON 按路径取值器(纯逻辑,可主机测试)。
//
// 设计取舍:本应用只从各家 API 的响应里提取少量字段,不需要完整 DOM。
// 这里实现一个"跳读式"导航器:按 dotted path(如 "data.limits.0.percentage")
// 逐段下钻,遇到不匹配即失败。字符串数值("110.00")与数字都按 double 读出。
#pragma once

#include <stdbool.h>
#include <stddef.h>

// 错误码:0 成功;负数见下。解析器尽量容错,但结构性损坏一定返回错误。
#define JPATH_OK 0
#define JPATH_ERR_NOT_FOUND (-1) // 路径不存在或类型不符
#define JPATH_ERR_MALFORMED (-2) // JSON 结构损坏
#define JPATH_ERR_RANGE (-3)     // 输出缓冲区不足
#define JPATH_ERR_ARGS (-4)      // 参数非法(NULL 等)

// 在 json 文本中按 path 取字符串值并写入 out(NUL 结尾)。
// path 语法:以 "." 分段;对象按键名下钻;数组按十进制下标下钻。
// 允许值是字符串或数字;字符串会做 \" \\ \/ \n \t \r \b \f \uXXXX 转义还原。
int jpath_get_string(const char *json, const char *path, char *out, size_t out_len);

// 同上,但按数值读取:JSON 数字直接读;字符串里含十进制数字也读(如 "110.00")。
int jpath_get_number(const char *json, const char *path, double *out);

// 在 path 定位到的数组里统计元素个数,写入 *count。
int jpath_get_array_len(const char *json, const char *path, size_t *count);

// 遍历 path 定位到的对象数组:对每个元素,取其 "type_key" 字段的字符串值,
// 与 want_type 相等则把该元素下标写入 *out_index。用于在 limits[] 里找
// TOKENS_LIMIT / TIME_LIMIT 这类条目。找不到返回 JPATH_ERR_NOT_FOUND。
int jpath_find_in_array_by_string(const char *json, const char *array_path,
                                  const char *type_key, const char *want_type,
                                  size_t *out_index);
