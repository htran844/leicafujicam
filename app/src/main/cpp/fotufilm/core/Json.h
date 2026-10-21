// Minimal JSON DOM and the Foundation JSONDecoder semantics FotufilmCore's Codable types rely on.
#ifndef FOTUFILM_CORE_JSON_H
#define FOTUFILM_CORE_JSON_H

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fotufilm {

/// A decoding failure: malformed text, a missing key, a type mismatch, or a value a Codable
/// initialiser rejected (Swift `DecodingError`).
class DecodingError : public std::runtime_error {
public:
    explicit DecodingError(const std::string& message) : std::runtime_error(message) {}
};

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    /// The number exactly as written, so each decode can convert it at its own precision.
    std::string numberText;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool isNull() const { return type == Type::Null; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    /// Member lookup; the last occurrence wins on a duplicated key. nullptr when absent.
    const JsonValue* find(const std::string& key) const;
};

/// Parses one complete JSON document (RFC 8259). Throws DecodingError on malformed text.
JsonValue parseJson(const std::string& text);

namespace json {

// Scalar decodes, matching JSONDecoder: numbers must be JSON numbers (no string coercion),
// a Float is the correctly rounded single-precision value of the text and must be finite,
// a Double likewise in double precision, an Int must be integral and in range.
float asFloat(const JsonValue& value, const std::string& path);
double asDouble(const JsonValue& value, const std::string& path);
int64_t asInt(const JsonValue& value, const std::string& path);
int32_t asInt32(const JsonValue& value, const std::string& path);
bool asBool(const JsonValue& value, const std::string& path);
std::string asString(const JsonValue& value, const std::string& path);

std::vector<float> asFloatArray(const JsonValue& value, const std::string& path);
std::vector<std::vector<float>> asFloatMatrix(const JsonValue& value, const std::string& path);
std::vector<double> asDoubleArray(const JsonValue& value, const std::string& path);
std::vector<std::vector<double>> asDoubleMatrix(const JsonValue& value, const std::string& path);

/// Keyed container: the object itself, or a typeMismatch.
const JsonValue& object(const JsonValue& value, const std::string& path);
/// `decode(forKey:)`: keyNotFound when absent, valueNotFound when null.
const JsonValue& required(const JsonValue& object, const std::string& key, const std::string& path);
/// `decodeIfPresent(forKey:)`: nullptr when absent or null.
const JsonValue* optional(const JsonValue& object, const std::string& key);

std::string join(const std::string& path, const std::string& key);
std::string index(const std::string& path, size_t i);

// Convenience forms for the common keyed decodes.
float requiredFloat(const JsonValue& o, const std::string& key, const std::string& path);
std::optional<float> optionalFloat(const JsonValue& o, const std::string& key, const std::string& path);
std::vector<float> requiredFloatArray(const JsonValue& o, const std::string& key, const std::string& path);
std::optional<std::vector<float>> optionalFloatArray(const JsonValue& o, const std::string& key,
                                                     const std::string& path);
std::vector<std::vector<float>> requiredFloatMatrix(const JsonValue& o, const std::string& key,
                                                    const std::string& path);
std::optional<std::vector<std::vector<float>>> optionalFloatMatrix(const JsonValue& o, const std::string& key,
                                                                   const std::string& path);
std::string requiredString(const JsonValue& o, const std::string& key, const std::string& path);
std::optional<std::string> optionalString(const JsonValue& o, const std::string& key, const std::string& path);
std::optional<bool> optionalBool(const JsonValue& o, const std::string& key, const std::string& path);

}  // namespace json
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_JSON_H
