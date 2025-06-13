/************************************************
 * filename: log.h
 * function:
 * description:
 ***********************************************/

#ifndef __LOG_H__
#define __LOG_H__

#include <stdlib.h>

#define DEBUG 8U
#define INFO 7U
#define WARN 5U
#define ERROR 4U

#ifndef LOG_LEVEL
#define LOG_LEVEL DEBUG
#endif // LOG_LEVEL

#if (LOG_LEVEL == DEBUG)
#define LOG_DEBUG(format, ...) log_write("DEBUG %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(format, ...) log_write("INFO %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(format, ...) log_write("WARN %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log_write("ERROR %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_PANIC(format, ...) {log_write("PANIC %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__); abort();}

#ifndef RUNTIME_ASSERT
#define RUNTIME_ASSERT(expr) do {if (!(expr)) { LOG_PANIC("%s", #expr); } } while (0)
#endif // RUNTIME_ASSERT

#elif (LOG_LEVEL == INFO)
#define LOG_DEBUG(format, ...)
#define LOG_INFO(format, ...) log_write("INFO %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(format, ...) log_write("WARN %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log_write("ERROR %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_PANIC(format, ...) {log_write("PANIC %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__); abort();}
#ifndef RUNTIME_ASSERT
#define RUNTIME_ASSERT(expr)
#endif // RUNTIME_ASSERT
#elif (LOG_LEVEL == WARN)
#define LOG_DEBUG(format, ...)
#define LOG_INFO(format, ...)
#define LOG_WARN(format, ...) log_write("WARN %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) log_write("ERROR %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_PANIC(format, ...) {log_write("PANIC %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__); abort(); }
#ifndef RUNTIME_ASSERT
#define RUNTIME_ASSERT(expr)
#endif // RUNTIME_ASSERT
#elif (LOG_LEVEL == ERROR)
#define LOG_DEBUG(format, ...)
#define LOG_INFO(format, ...)
#define LOG_WARN(format, ...)
#define LOG_ERROR(format, ...) log_write("ERROR %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define LOG_PANIC(format, ...) {log_write("PANIC %s %05d: " format "\n", __func__, __LINE__, ##__VA_ARGS__); abort();}
#ifndef RUNTIME_ASSERT
#define RUNTIME_ASSERT(expr)
#endif // RUNTIME_ASSERT
#endif

extern void log_write(const char *, ...);

#endif // __LOG_H__