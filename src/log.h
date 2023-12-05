#ifndef RDMA_LOG_H
#define RDMA_LOG_H

enum log_level {
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_TRACE,
  LOG_LEVEL_MID,
  LOG_LEVEL_LAST
};

extern enum log_level global_log_level;

void log_impl(const char *file, unsigned line, const char *func,
              unsigned log_level, const char *fmt, ...);

#define log(level, fmt, ...)                                             \
  do {                                                                   \
    if (level < LOG_LEVEL_LAST && level <= global_log_level)             \
      log_impl(__FILE__, __LINE__, __func__, level, fmt, ##__VA_ARGS__); \
  } while (0)

#define ERROR_LOG(fmt, ...) log(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define WARN_LOG(fmt, ...) log(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define INFO_LOG(fmt, ...) log(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define DEBUG_LOG(fmt, ...) log(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define TRACE_LOG(fmt, ...) log(LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
#define MID_LOG(fmt, ...) log(LOG_LEVEL_MID, fmt, ##__VA_ARGS__)

#endif