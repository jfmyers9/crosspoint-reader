#pragma once
inline void logStub(const char*, const char*, ...) {}
#define LOG_ERR(...) logStub(__VA_ARGS__)
#define LOG_INF(...) logStub(__VA_ARGS__)
