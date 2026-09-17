/* ============================================================================
 * core/Log.h —— 分级日志
 * ----------------------------------------------------------------------------
 * 提供 ERROR / WARN / INFO / DEBUG 四级日志。
 * 全部输出到 stderr，避免污染 stdout 上的 DOT 数据。
 * 不使用 std::string / iostream。
 *
 * 用法：
 *      logSetLevel(LOG_LEVEL_DEBUG);      // 打开调试输出
 *      logInfo("找到函数 %s，地址 0x%llx", name, (unsigned long long)addr);
 * ========================================================================== */
#ifndef ELFCFG_CORE_LOG_H
#define ELFCFG_CORE_LOG_H

enum LogLevel {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3
};

/* 设置日志级别：低于该级别的日志被丢弃 */
void     logSetLevel(LogLevel level);
LogLevel logGetLevel(void);

/* 解析 "--debug" / "debug" 等字符串为日志级别；无法识别返回 0 */
int      logParseLevel(const char *text, LogLevel *out);

/* 设置日志前缀（通常是程序名），默认 "elfcfg" */
void     logSetPrefix(const char *prefix);

/* 各级日志。格式化规则同 printf，输出格式：
 *      [elfcfg] INFO : 消息
 */
void logError(const char *fmt, ...);
void logWarn (const char *fmt, ...);
void logInfo (const char *fmt, ...);
void logDebug(const char *fmt, ...);

/* 统计各级日志条数，便于测试与后续分析 */
unsigned long logCount(LogLevel level);

#endif /* ELFCFG_CORE_LOG_H */
