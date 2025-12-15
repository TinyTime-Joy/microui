#ifndef GLOBAL_H
#define GLOBAL_H

#include <android/log.h>

#define LOG_TAG "MicroUI"
#define LogDebug(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LogInfo(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LogError(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#endif
