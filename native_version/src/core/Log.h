#ifndef ELFCFG_CORE_LOG_H
#define ELFCFG_CORE_LOG_H

enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
};

void     logSetLevel(LogLevel level);
LogLevel logGetLevel(void);

int      logParseLevel(const char *text, LogLevel *out);

void     logSetPrefix(const char *prefix);

void logError(const char *fmt, ...);
void logWarn (const char *fmt, ...);
void logInfo (const char *fmt, ...);
void logDebug(const char *fmt, ...);

unsigned long logCount(LogLevel level);

#endif
