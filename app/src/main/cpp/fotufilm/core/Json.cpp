// Minimal JSON DOM and the Foundation JSONDecoder semantics FotufilmCore's Codable types rely on.
#include "Json.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace fotufilm {

const JsonValue* JsonValue::find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    const JsonValue* found = nullptr;
    for (const auto& member : object) {
        if (member.first == key) found = &member.second;
    }
    return found;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonValue parseDocument() {
        // A UTF-8 byte-order mark is tolerated, as JSONDecoder does.
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        }
        skipWhitespace();
        JsonValue value = parseValue(0);
        skipWhitespace();
        if (pos_ != text_.size()) fail("unexpected trailing characters");
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;
    static constexpr int kMaxDepth = 512;

    [[noreturn]] void fail(const std::string& message) const {
        throw DecodingError("malformed JSON at offset " + std::to_string(pos_) + ": " + message);
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool consumeLiteral(const char* literal) {
        size_t i = 0;
        while (literal[i] != '\0') {
            if (pos_ + i >= text_.size() || text_[pos_ + i] != literal[i]) return false;
            ++i;
        }
        pos_ += i;
        return true;
    }

    JsonValue parseValue(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        if (pos_ >= text_.size()) fail("unexpected end of input");
        char c = text_[pos_];
        JsonValue value;
        if (c == '{') {
            value.type = JsonValue::Type::Object;
            ++pos_;
            skipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return value;
            }
            while (true) {
                skipWhitespace();
                if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected object key");
                std::string key = parseString();
                skipWhitespace();
                if (pos_ >= text_.size() || text_[pos_] != ':') fail("expected ':'");
                ++pos_;
                skipWhitespace();
                JsonValue member = parseValue(depth + 1);
                value.object.emplace_back(std::move(key), std::move(member));
                skipWhitespace();
                if (pos_ >= text_.size()) fail("unterminated object");
                if (text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (text_[pos_] == '}') {
                    ++pos_;
                    return value;
                }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            value.type = JsonValue::Type::Array;
            ++pos_;
            skipWhitespace();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return value;
            }
            while (true) {
                skipWhitespace();
                value.array.push_back(parseValue(depth + 1));
                skipWhitespace();
                if (pos_ >= text_.size()) fail("unterminated array");
                if (text_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (text_[pos_] == ']') {
                    ++pos_;
                    return value;
                }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            value.type = JsonValue::Type::String;
            value.string = parseString();
            return value;
        }
        if (c == 't') {
            if (!consumeLiteral("true")) fail("invalid literal");
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
            return value;
        }
        if (c == 'f') {
            if (!consumeLiteral("false")) fail("invalid literal");
            value.type = JsonValue::Type::Bool;
            value.boolean = false;
            return value;
        }
        if (c == 'n') {
            if (!consumeLiteral("null")) fail("invalid literal");
            value.type = JsonValue::Type::Null;
            return value;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            value.type = JsonValue::Type::Number;
            value.numberText = parseNumber();
            return value;
        }
        fail("unexpected character");
    }

    std::string parseNumber() {
        size_t start = pos_;
        auto digit = [&](size_t p) { return p < text_.size() && text_[p] >= '0' && text_[p] <= '9'; };
        if (text_[pos_] == '-') ++pos_;
        if (!digit(pos_)) fail("invalid number");
        if (text_[pos_] == '0') {
            ++pos_;
            if (digit(pos_)) fail("leading zero in number");
        } else {
            while (digit(pos_)) ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (!digit(pos_)) fail("invalid fraction");
            while (digit(pos_)) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (!digit(pos_)) fail("invalid exponent");
            while (digit(pos_)) ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    unsigned parseHex4() {
        if (pos_ + 4 > text_.size()) fail("truncated unicode escape");
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("invalid unicode escape");
            }
        }
        return code;
    }

    static void appendUTF8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    std::string parseString() {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("unescaped control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) fail("unterminated escape");
            char e = text_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned code = parseHex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos_ + 6 <= text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            unsigned low = parseHex4();
                            if (low < 0xDC00 || low > 0xDFFF) fail("invalid surrogate pair");
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            fail("unpaired surrogate");
                        }
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        fail("unpaired surrogate");
                    }
                    appendUTF8(out, code);
                    break;
                }
                default:
                    fail("invalid escape");
            }
        }
    }
};

const char* typeName(JsonValue::Type type) {
    switch (type) {
        case JsonValue::Type::Null: return "null";
        case JsonValue::Type::Bool: return "bool";
        case JsonValue::Type::Number: return "number";
        case JsonValue::Type::String: return "string";
        case JsonValue::Type::Array: return "array";
        case JsonValue::Type::Object: return "object";
    }
    return "?";
}

[[noreturn]] void typeMismatch(const JsonValue& value, const char* expected, const std::string& path) {
    if (value.isNull()) {
        throw DecodingError("valueNotFound: expected " + std::string(expected) + " at " + path +
                            " but found null");
    }
    throw DecodingError("typeMismatch: expected " + std::string(expected) + " at " + path + " but found " +
                        typeName(value.type));
}

bool isIntegerText(const std::string& text) {
    for (char c : text) {
        if (c == '.' || c == 'e' || c == 'E') return false;
    }
    return true;
}

}  // namespace

JsonValue parseJson(const std::string& text) {
    Parser parser(text);
    return parser.parseDocument();
}

namespace json {

std::string join(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

std::string index(const std::string& path, size_t i) { return path + "[" + std::to_string(i) + "]"; }

float asFloat(const JsonValue& value, const std::string& path) {
    if (!value.isNumber()) typeMismatch(value, "Float", path);
    // JSONDecoder converts the number text directly at Float precision (strtof), and refuses a
    // number that does not fit. For the decimal precision any pack carries this is the same
    // value as rounding through Double first.
    errno = 0;
    const char* begin = value.numberText.c_str();
    char* end = nullptr;
    float result = std::strtof(begin, &end);
    if (end == begin || !std::isfinite(result)) {
        throw DecodingError("dataCorrupted: parsed JSON number <" + value.numberText + "> at " + path +
                            " does not fit in Float");
    }
    return result;
}

double asDouble(const JsonValue& value, const std::string& path) {
    if (!value.isNumber()) typeMismatch(value, "Double", path);
    const char* begin = value.numberText.c_str();
    char* end = nullptr;
    double result = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(result)) {
        throw DecodingError("dataCorrupted: parsed JSON number <" + value.numberText + "> at " + path +
                            " does not fit in Double");
    }
    return result;
}

int64_t asInt(const JsonValue& value, const std::string& path) {
    if (!value.isNumber()) typeMismatch(value, "Int", path);
    if (isIntegerText(value.numberText)) {
        errno = 0;
        char* end = nullptr;
        long long parsed = std::strtoll(value.numberText.c_str(), &end, 10);
        if (errno == ERANGE) {
            throw DecodingError("dataCorrupted: parsed JSON number <" + value.numberText + "> at " + path +
                                " does not fit in Int");
        }
        return static_cast<int64_t>(parsed);
    }
    double d = asDouble(value, path);
    if (d != std::floor(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) {
        throw DecodingError("dataCorrupted: parsed JSON number <" + value.numberText + "> at " + path +
                            " does not fit in Int");
    }
    return static_cast<int64_t>(d);
}

int32_t asInt32(const JsonValue& value, const std::string& path) {
    int64_t v = asInt(value, path);
    if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max()) {
        throw DecodingError("dataCorrupted: parsed JSON number <" + value.numberText + "> at " + path +
                            " does not fit in Int32");
    }
    return static_cast<int32_t>(v);
}

bool asBool(const JsonValue& value, const std::string& path) {
    if (value.type != JsonValue::Type::Bool) typeMismatch(value, "Bool", path);
    return value.boolean;
}

std::string asString(const JsonValue& value, const std::string& path) {
    if (!value.isString()) typeMismatch(value, "String", path);
    return value.string;
}

std::vector<float> asFloatArray(const JsonValue& value, const std::string& path) {
    if (!value.isArray()) typeMismatch(value, "Array<Float>", path);
    std::vector<float> out;
    out.reserve(value.array.size());
    for (size_t i = 0; i < value.array.size(); ++i) out.push_back(asFloat(value.array[i], index(path, i)));
    return out;
}

std::vector<std::vector<float>> asFloatMatrix(const JsonValue& value, const std::string& path) {
    if (!value.isArray()) typeMismatch(value, "Array<Array<Float>>", path);
    std::vector<std::vector<float>> out;
    out.reserve(value.array.size());
    for (size_t i = 0; i < value.array.size(); ++i) out.push_back(asFloatArray(value.array[i], index(path, i)));
    return out;
}

std::vector<double> asDoubleArray(const JsonValue& value, const std::string& path) {
    if (!value.isArray()) typeMismatch(value, "Array<Double>", path);
    std::vector<double> out;
    out.reserve(value.array.size());
    for (size_t i = 0; i < value.array.size(); ++i) out.push_back(asDouble(value.array[i], index(path, i)));
    return out;
}

std::vector<std::vector<double>> asDoubleMatrix(const JsonValue& value, const std::string& path) {
    if (!value.isArray()) typeMismatch(value, "Array<Array<Double>>", path);
    std::vector<std::vector<double>> out;
    out.reserve(value.array.size());
    for (size_t i = 0; i < value.array.size(); ++i) out.push_back(asDoubleArray(value.array[i], index(path, i)));
    return out;
}

const JsonValue& object(const JsonValue& value, const std::string& path) {
    if (!value.isObject()) typeMismatch(value, "keyed container", path);
    return value;
}

const JsonValue& required(const JsonValue& o, const std::string& key, const std::string& path) {
    const JsonValue* found = o.find(key);
    if (found == nullptr) throw DecodingError("keyNotFound: no value for key '" + key + "' at " + path);
    if (found->isNull()) throw DecodingError("valueNotFound: null for key '" + key + "' at " + path);
    return *found;
}

const JsonValue* optional(const JsonValue& o, const std::string& key) {
    const JsonValue* found = o.find(key);
    if (found == nullptr || found->isNull()) return nullptr;
    return found;
}

float requiredFloat(const JsonValue& o, const std::string& key, const std::string& path) {
    return asFloat(required(o, key, path), join(path, key));
}

std::optional<float> optionalFloat(const JsonValue& o, const std::string& key, const std::string& path) {
    const JsonValue* v = optional(o, key);
    if (v == nullptr) return std::nullopt;
    return asFloat(*v, join(path, key));
}

std::vector<float> requiredFloatArray(const JsonValue& o, const std::string& key, const std::string& path) {
    return asFloatArray(required(o, key, path), join(path, key));
}

std::optional<std::vector<float>> optionalFloatArray(const JsonValue& o, const std::string& key,
                                                     const std::string& path) {
    const JsonValue* v = optional(o, key);
    if (v == nullptr) return std::nullopt;
    return asFloatArray(*v, join(path, key));
}

std::vector<std::vector<float>> requiredFloatMatrix(const JsonValue& o, const std::string& key,
                                                    const std::string& path) {
    return asFloatMatrix(required(o, key, path), join(path, key));
}

std::optional<std::vector<std::vector<float>>> optionalFloatMatrix(const JsonValue& o, const std::string& key,
                                                                   const std::string& path) {
    const JsonValue* v = optional(o, key);
    if (v == nullptr) return std::nullopt;
    return asFloatMatrix(*v, join(path, key));
}

std::string requiredString(const JsonValue& o, const std::string& key, const std::string& path) {
    return asString(required(o, key, path), join(path, key));
}

std::optional<std::string> optionalString(const JsonValue& o, const std::string& key, const std::string& path) {
    const JsonValue* v = optional(o, key);
    if (v == nullptr) return std::nullopt;
    return asString(*v, join(path, key));
}

std::optional<bool> optionalBool(const JsonValue& o, const std::string& key, const std::string& path) {
    const JsonValue* v = optional(o, key);
    if (v == nullptr) return std::nullopt;
    return asBool(*v, join(path, key));
}

}  // namespace json
}  // namespace fotufilm
