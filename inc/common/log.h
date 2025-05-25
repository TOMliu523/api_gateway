/************************************************
 * filename: log.h
 * function:
 * description:
 ***********************************************/

#ifndef __LOG_H__
#define __LOG_H__

#if (LOG_LEVEL == DEBUG)
#define LOG_DEBUG(format, ...) log_write("DEBUG: " format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) log_write("INFO: " format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log_write("ERROR" format, ##__VA_ARGS__)
#elif (LOG_LEVEL == INFO)
#define LOG_DEBUG(format, ...)
#define LOG_INFO(format, ...) log_write("INFO: " format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log_write("ERROR" format, ##__VA_ARGS__)
#elif (LOG_LEVEL == ERROR)
#define LOG_DEBUG(format, ...)
#define LOG_INFO(format, ...)
#define LOG_ERROR(format, ...) log_write("ERROR" format, ##__VA_ARGS__)
#endif

extern void log_write(const char *, ...);

#endif // __LOG_H__