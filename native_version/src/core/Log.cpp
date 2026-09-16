/* ============================================================================
 * core/Log.cpp —— 分级日志实现
 * ========================================================================== */
#include "core/Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

/* ---- 模块内部状态（文件作用域，不导出） ---- */
static LogLevel   g_level      = LOG_LEVEL_INFO;
static const char *g_prefix    = "elfcfg";
static unsigned long g_counters[4] = { 0, 0, 0, 0 };

static const char *levelName(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_ERROR: return "ERROR";
    case LOG_LEVEL_WARN:  return "WARN ";
    case LOG_LEVEL_INFO:  return "INFO ";
    case LOG_LEVEL_DEBUG: return "DEBUG";
    default:              return "?????";
    }
}

void logSetLevel(LogLevel level)
{
    if (level < LOG_LEVEL_ERROR) level = LOG_LEVEL_ERROR;
    if (level > LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    g_level = level;
}

LogLevel logGetLevel(void)
{
    return g_level;
}

int logParseLevel(const char *text, LogLevel *out)
{
    static const struct { const char *name; LogLevel lv; } table[] = {
        { "error", LOG_LEVEL_ERROR },
        { "warn",  LOG_LEVEL_WARN  },
        { "info",  LOG_LEVEL_INFO  },
        { "debug", LOG_LEVEL_DEBUG }
    };
    if (!text || !out) return 0;

    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (strcmp(text, table[i].name) == 0) {
            *out = table[i].lv;
            return 1;
        }
    }
    return 0;
}

void logSetPrefix(const char *prefix)
{
    g_prefix = (prefix && *prefix) ? prefix : "elfcfg";
}

unsigned long logCount(LogLevel level)
{
    if (level < LOG_LEVEL_ERROR || level > LOG_LEVEL_DEBUG) return 0;
    return g_counters[level];
}

/* ---- 统一的输出函数 ---- */
static void logWrite(LogLevel level, const char *fmt, va_list ap)
{
    if (level > g_level) return;

    g_counters[level]++;

    /* 固定缓冲区：日志内容不需要无限长，截断即可，避免动态分配 */
    char body[1024];
    vsnprintf(body, sizeof(body), fmt, ap);

    fprintf(stderr, "[%s] %s: %s\n", g_prefix, levelName(level), body);
}

void logError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWrite(LOG_LEVEL_ERROR, fmt, ap);
    va_end(ap);
}

void logWarn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWrite(LOG_LEVEL_WARN, fmt, ap);
    va_end(ap);
}

void logInfo(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWrite(LOG_LEVEL_INFO, fmt, ap);
    va_end(ap);
}

void logDebug(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logWrite(LOG_LEVEL_DEBUG, fmt, ap);
    va_end(ap);
}
