#pragma once
template <typename... Args>
inline void testLog(Args&&...) {}
#define LOG_DBG(...) testLog(__VA_ARGS__)
#define LOG_ERR(...) testLog(__VA_ARGS__)
