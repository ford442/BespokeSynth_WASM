#pragma once
#include "json.h"

namespace Json {
    using StreamWriterBuilder = Json::StreamWriterBuilder;
    static inline std::string writeString(const StreamWriterBuilder& b, const Value& v) { return Json::writeString(b, v); }
}
