/**
 * BespokeSynth WASM - Small JSON reader shared by patch I/O, module help and desktop import
 *
 * A DOM parser (objects as std::map, numbers as double) plus the lookup helpers the
 * loaders use. UI thread only; it allocates freely.
 *
 * Copyright (C) 2024
 * Licensed under GNU GPL v3
 */

#pragma once

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bespoke
{
   namespace wasm
   {
      namespace json
      {
         struct JsonValue
         {
            enum class Type
            {
               Null,
               Bool,
               Number,
               String,
               Array,
               Object
            };

            Type type = Type::Null;
            bool boolValue = false;
            double numberValue = 0.0;
            std::string stringValue;
            std::vector<JsonValue> arrayValue;
            std::map<std::string, JsonValue> objectValue;

            const JsonValue* get(const std::string& key) const
            {
               auto it = objectValue.find(key);
               return it == objectValue.end() ? nullptr : &it->second;
            }
         };

         class JsonParser
         {
         public:
            explicit JsonParser(const std::string& text)
            : mText(text)
            {
            }

            bool parse(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               if (!parseValue(out, error))
                  return false;
               skipWhitespace();
               if (mPos != mText.size())
               {
                  error = "Unexpected trailing data";
                  return false;
               }
               return true;
            }

         private:
            void skipWhitespace()
            {
               while (mPos < mText.size() && std::isspace(static_cast<unsigned char>(mText[mPos])))
                  ++mPos;
            }

            bool consume(char expected)
            {
               skipWhitespace();
               if (mPos >= mText.size() || mText[mPos] != expected)
                  return false;
               ++mPos;
               return true;
            }

            bool parseValue(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               if (mPos >= mText.size())
               {
                  error = "Unexpected end of JSON";
                  return false;
               }

               const char c = mText[mPos];
               if (c == '{')
                  return parseObject(out, error);
               if (c == '[')
                  return parseArray(out, error);
               if (c == '"')
                  return parseStringValue(out, error);
               if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
                  return parseNumber(out, error);
               if (mText.compare(mPos, 4, "true") == 0)
               {
                  mPos += 4;
                  out.type = JsonValue::Type::Bool;
                  out.boolValue = true;
                  return true;
               }
               if (mText.compare(mPos, 5, "false") == 0)
               {
                  mPos += 5;
                  out.type = JsonValue::Type::Bool;
                  out.boolValue = false;
                  return true;
               }
               if (mText.compare(mPos, 4, "null") == 0)
               {
                  mPos += 4;
                  out.type = JsonValue::Type::Null;
                  return true;
               }

               error = "Unexpected JSON token";
               return false;
            }

            bool parseObject(JsonValue& out, std::string& error)
            {
               if (!consume('{'))
                  return false;
               out.type = JsonValue::Type::Object;
               skipWhitespace();
               if (consume('}'))
                  return true;

               while (true)
               {
                  JsonValue key;
                  if (!parseStringValue(key, error))
                     return false;
                  if (!consume(':'))
                  {
                     error = "Expected ':' after object key";
                     return false;
                  }
                  JsonValue value;
                  if (!parseValue(value, error))
                     return false;
                  out.objectValue[key.stringValue] = value;

                  if (consume('}'))
                     return true;
                  if (!consume(','))
                  {
                     error = "Expected ',' or '}' in object";
                     return false;
                  }
               }
            }

            bool parseArray(JsonValue& out, std::string& error)
            {
               if (!consume('['))
                  return false;
               out.type = JsonValue::Type::Array;
               skipWhitespace();
               if (consume(']'))
                  return true;

               while (true)
               {
                  JsonValue value;
                  if (!parseValue(value, error))
                     return false;
                  out.arrayValue.push_back(value);

                  if (consume(']'))
                     return true;
                  if (!consume(','))
                  {
                     error = "Expected ',' or ']' in array";
                     return false;
                  }
               }
            }

            bool parseStringValue(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               if (mPos >= mText.size() || mText[mPos] != '"')
               {
                  error = "Expected string";
                  return false;
               }
               ++mPos;
               out.type = JsonValue::Type::String;
               out.stringValue.clear();

               while (mPos < mText.size())
               {
                  const char c = mText[mPos++];
                  if (c == '"')
                     return true;
                  if (c != '\\')
                  {
                     out.stringValue.push_back(c);
                     continue;
                  }
                  if (mPos >= mText.size())
                  {
                     error = "Unterminated escape sequence";
                     return false;
                  }
                  const char esc = mText[mPos++];
                  switch (esc)
                  {
                     case '"':
                     case '\\':
                     case '/':
                        out.stringValue.push_back(esc);
                        break;
                     case 'b':
                        out.stringValue.push_back('\b');
                        break;
                     case 'f':
                        out.stringValue.push_back('\f');
                        break;
                     case 'n':
                        out.stringValue.push_back('\n');
                        break;
                     case 'r':
                        out.stringValue.push_back('\r');
                        break;
                     case 't':
                        out.stringValue.push_back('\t');
                        break;
                     default:
                        error = "Unsupported string escape";
                        return false;
                  }
               }

               error = "Unterminated string";
               return false;
            }

            bool parseNumber(JsonValue& out, std::string& error)
            {
               skipWhitespace();
               const size_t start = mPos;
               if (mText[mPos] == '-')
                  ++mPos;
               while (mPos < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos])))
                  ++mPos;
               if (mPos < mText.size() && mText[mPos] == '.')
               {
                  ++mPos;
                  while (mPos < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos])))
                     ++mPos;
               }
               if (mPos < mText.size() && (mText[mPos] == 'e' || mText[mPos] == 'E'))
               {
                  ++mPos;
                  if (mPos < mText.size() && (mText[mPos] == '-' || mText[mPos] == '+'))
                     ++mPos;
                  while (mPos < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mPos])))
                     ++mPos;
               }

               try
               {
                  out.type = JsonValue::Type::Number;
                  out.numberValue = std::stod(mText.substr(start, mPos - start));
                  return true;
               }
               catch (...)
               {
                  error = "Invalid number";
                  return false;
               }
            }

            const std::string& mText;
            size_t mPos = 0;
         };

         inline std::string escapeJson(const std::string& value)
         {
            std::ostringstream out;
            for (char c : value)
            {
               switch (c)
               {
                  case '"':
                     out << "\\\"";
                     break;
                  case '\\':
                     out << "\\\\";
                     break;
                  case '\n':
                     out << "\\n";
                     break;
                  case '\r':
                     out << "\\r";
                     break;
                  case '\t':
                     out << "\\t";
                     break;
                  default:
                     out << c;
                     break;
               }
            }
            return out.str();
         }

         inline double numberOr(const JsonValue* value, double fallback)
         {
            return value && value->type == JsonValue::Type::Number ? value->numberValue : fallback;
         }

         inline bool boolOr(const JsonValue* value, bool fallback)
         {
            return value && value->type == JsonValue::Type::Bool ? value->boolValue : fallback;
         }

         inline std::string stringOr(const JsonValue* value, const std::string& fallback)
         {
            return value && value->type == JsonValue::Type::String ? value->stringValue : fallback;
         }

         inline int intOr(const JsonValue* value, int fallback)
         {
            return static_cast<int>(numberOr(value, fallback));
         }

         inline const JsonValue* objectChild(const JsonValue* value, const std::string& key)
         {
            return value && value->type == JsonValue::Type::Object ? value->get(key) : nullptr;
         }

         inline int nestedIntOr(const JsonValue& object,
                                const std::string& objectKey,
                                const std::string& valueKey,
                                const std::string& flatKey,
                                int fallback)
         {
            if (const JsonValue* nested = objectChild(object.get(objectKey), valueKey))
               return intOr(nested, fallback);
            return intOr(object.get(flatKey), fallback);
         }

      } // namespace json
   } // namespace wasm
} // namespace bespoke
