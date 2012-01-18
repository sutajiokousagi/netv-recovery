
#include <stdio.h>

#define DEBUG_MESSAGE_SIZE 1024

#define PERROR(format, arg...)            \
  do { \
    fprintf(stderr, "%s - %s():%d - " format ": %s\n", __FILE__, __func__, __LINE__, ## arg, strerror(errno)); \
    snprintf(current_debug_message, sizeof(current_debug_message), "%s - %s():%d - " format ": %s\n", __FILE__, __func__, __LINE__, ## arg, strerror(errno)); \
    if (serial_output) \
        fputs(current_debug_message, serial_output); \
  } while(0)

#define ERROR(format, arg...)            \
  do { \
    fprintf(stderr, "%s - %s():%d - " format "\n", __FILE__, __func__, __LINE__, ## arg); \
    snprintf(current_debug_message, sizeof(current_debug_message), "%s - %s():%d - " format "\n", __FILE__, __func__, __LINE__, ## arg); \
    if (serial_output) \
        fputs(current_debug_message, serial_output); \
  } while(0)

#define NOTE(format, arg...)            \
  do { \
    fprintf(stderr, "%s - %s():%d - " format "\n", __FILE__, __func__, __LINE__, ## arg); \
    snprintf(current_debug_message, sizeof(current_debug_message), "%s - %s():%d - " format "\n", __FILE__, __func__, __LINE__, ## arg); \
    if (serial_output) \
        fputs(current_debug_message, serial_output); \
  } while(0)

#ifndef __LOG_H__
#define __LOG_H__
extern char current_debug_message[DEBUG_MESSAGE_SIZE];
extern FILE *serial_output;
#endif /* __LOG_H__ */
