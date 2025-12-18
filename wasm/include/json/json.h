// Minimal JsonCpp-compatible header for WASM builds
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cctype>
#include <sstream>
#include <functional>

namespace Json {
using String = std::string;

class Value
{
public:
    enum class Type { Null, Object, Array, Int, String };

    Value() : type(Type::Null) {}
    static Value object() { Value v; v.type = Type::Object; return v; }
    static Value array() { Value v; v.type = Type::Array; return v; }

    // object access
    Value& operator[](const std::string& key) { type = Type::Object; return objectMembers[key]; }
    const Value& operator[](const std::string& key) const { static Value nullv; auto it = objectMembers.find(key); return it==objectMembers.end()? nullv : it->second; }

    // array access
    Value& operator[](size_t idx) { type = Type::Array; return arrayValues[idx]; }
    const Value& operator[](size_t idx) const { return arrayValues[idx]; }
    size_t size() const { return type==Type::Array ? arrayValues.size() : objectMembers.size(); }

    void append(const Value& v) { type = Type::Array; arrayValues.push_back(v); }

    // introspection
    bool isArray() const { return type==Type::Array; }
    bool isObject() const { return type==Type::Object; }

    // conversions
    int asInt() const { return (int)intValue; }
    std::string asString() const { return stringValue; }

    // object iteration (compat shim)
    struct MemberIterator
    {
        using MapIt = std::map<std::string, Value>::iterator;
        MapIt it;
        MemberIterator(MapIt i) : it(i) {}
        MemberIterator& operator++() { ++it; return *this; }
        bool operator!=(MemberIterator const& o) const { return it != o.it; }
        // Return a Value that contains the key as a string so callers can use key().asString()
        Value key() const { Value v; v.type = Type::String; v.stringValue = it->first; return v; }
        Value& value() const { return const_cast<Value&>(it->second); }
    };
    MemberIterator begin() { return MemberIterator(objectMembers.begin()); }
    MemberIterator end() { return MemberIterator(objectMembers.end()); }

    // data
    Type type = Type::Null;
    std::map<std::string, Value> objectMembers;
    std::vector<Value> arrayValues;
    long long intValue = 0;
    std::string stringValue;
};

// Simple JSON parser sufficient for project's data files
class CharReader
{
public:
    bool parse(const char* begin, const char* end, Value* root, String* errs)
    {
        const char* p = begin;
        try { skip(p,end); *root = parseValue(p,end); skip(p,end); }
        catch (const std::string& e) { if (errs) *errs = e; return false; }
        return true;
    }

private:
    static void skip(const char*& p, const char* end)
    {
        while (p<end && std::isspace((unsigned char)*p)) ++p;
    }

    static std::string parseString(const char*& p, const char* end)
    {
        if (*p != '"') throw std::string("expected string");
        ++p; std::string s;
        while (p<end && *p != '"') { if (*p=='\\' && (p+1)<end) { ++p; s += *p++; } else s += *p++; }
        if (p>=end) throw std::string("unterminated string");
        ++p; return s;
    }

    static long long parseNumber(const char*& p, const char* end)
    {
        skip(p,end);
        long long sign = 1; if (*p=='-') { sign=-1; ++p; }
        long long v=0; bool any=false;
        while (p<end && std::isdigit((unsigned char)*p)) { any=true; v = v*10 + (*p - '0'); ++p; }
        if (!any) throw std::string("expected number");
        return v*sign;
    }

    static Value parseValue(const char*& p, const char* end)
    {
        skip(p,end);
        if (p>=end) throw std::string("unexpected end");
        if (*p=='{')
        {
            ++p; skip(p,end);
            Value obj = Value::object();
            while (p<end && *p!='}')
            {
                std::string key = parseString(p,end);
                skip(p,end);
                if (*p!=':') throw std::string("expected :"); ++p;
                skip(p,end);
                Value val = parseValue(p,end);
                obj.objectMembers[key] = val;
                skip(p,end);
                if (*p==',') { ++p; skip(p,end); }
            }
            if (p>=end) throw std::string("unterminated object"); ++p;
            return obj;
        }
        else if (*p=='[')
        {
            ++p; skip(p,end);
            Value arr = Value::array();
            while (p<end && *p!=']')
            {
                Value v = parseValue(p,end);
                arr.append(v);
                skip(p,end);
                if (*p==',') { ++p; skip(p,end); }
            }
            if (p>=end) throw std::string("unterminated array"); ++p;
            return arr;
        }
        else if (*p=='"')
        {
            Value v; v.type = Value::Type::String; v.stringValue = parseString(p,end); return v;
        }
        else if (std::isdigit((unsigned char)*p) || *p=='-')
        {
            Value v; v.type = Value::Type::Int; v.intValue = parseNumber(p,end); return v;
        }
        else if (std::strncmp(p,"true",4)==0) { p+=4; Value v; v.type=Value::Type::Int; v.intValue=1; return v; }
        else if (std::strncmp(p,"false",5)==0) { p+=5; Value v; v.type=Value::Type::Int; v.intValue=0; return v; }
        throw std::string("unexpected token");
    }
};

// Compatibility exceptions used by callers
struct LogicError : public std::runtime_error { LogicError(const std::string& s) : std::runtime_error(s) {} };

class CharReaderBuilder
{
public:
    std::unique_ptr<CharReader> newCharReader() const { return std::make_unique<CharReader>(); }
};

// Simple writer
class StreamWriterBuilder
{
public:
    std::map<std::string,std::string> settings;
    std::string& operator[](const std::string& k) { return settings[k]; }
};

static std::string writeString(const StreamWriterBuilder&, const Value& v)
{
    std::ostringstream os;
    std::function<void(const Value&)> write;
    write = [&](const Value& x){
        if (x.isObject()) {
            os << '{';
            bool first = true;
            for (auto& kv : x.objectMembers) {
                if (!first) os << ',';
                first = false;
                os << '"' << kv.first << "\":";
                write(kv.second);
            }
            os << '}';
        }
        else if (x.isArray()) {
            os << '[';
            bool first = true;
            for (auto& el : x.arrayValues) {
                if (!first) os << ',';
                first = false;
                write(el);
            }
            os << ']';
        }
        else if (x.type == Value::Type::String) os << '"' << x.stringValue << '"';
        else if (x.type == Value::Type::Int) os << x.intValue;
        else os << "null";
    };
    write(v);
    return os.str();
}

} // namespace Json
