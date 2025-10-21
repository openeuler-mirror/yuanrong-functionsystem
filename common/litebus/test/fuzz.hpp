#include "string"
#include "Public.h"
#include "actor/buslog.hpp"

#define FuzzExceptionBlock(out, block)                     \
    try {                                                  \
        block out = std::string();                         \
    } catch (std::exception const &e) {                    \
        out = std::string(e.what());                       \
    } catch (...) {                                        \
        out = std::string("Unknown exception");            \
    }                                                      \
    if (!out.empty()) {                                    \
        BUSLOG_INFO("Fuzz throw an excepton: {}", out);    \
    }
