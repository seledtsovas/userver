#include <formats/yaml/value.hpp>

#include <formats/yaml/exception.hpp>

#include <utils/assert.hpp>
#include <utils/demangle.hpp>

#include <yaml-cpp/yaml.h>

namespace formats {
namespace yaml {

namespace {

// Helper structure for YAML conversions. YAML has built in conversion logic and
// an `T Node::as<T>(U default_value)` function that uses it. We provide
// `IsConvertibleChecker<T>{}` as a `default_value`, and if the
// `IsConvertibleChecker` was converted to T (an implicit conversion operator
// was called), then the conversion failed.
template <class T>
struct IsConvertibleChecker {
  bool& convertible;

  operator T() const {
    convertible = false;
    return {};
  }
};

Type FromNative(YAML::NodeType::value t) {
  switch (t) {
    case YAML::NodeType::Sequence:
      return Type::kArray;
    case YAML::NodeType::Map:
      return Type::kMap;
    case YAML::NodeType::Undefined:
      return Type::kMissing;
    case YAML::NodeType::Null:
      return Type::kNull;
    case YAML::NodeType::Scalar:
      return Type::kObject;
  }
}

auto MakeMissingNode() { return YAML::Node{}[0]; }

}  // namespace

Value::Value() noexcept : Value(YAML::Node()) {}

Value::Value(Value&&) = default;
Value::Value(const Value&) = default;
Value& Value::operator=(Value&&) = default;
Value& Value::operator=(const Value&) = default;

Value::~Value() = default;

Value::Value(const YAML::Node& root) noexcept
    : is_root_(true), value_pimpl_(root) {}

Value Value::MakeNonRoot(const YAML::Node& value,
                         const formats::yaml::Path& path,
                         const std::string& key) {
  Value ret{value};
  ret.is_root_ = false;
  ret.path_ = path;
  ret.path_.push_back(key);
  return ret;
}

Value Value::MakeNonRoot(const YAML::Node& value,
                         const formats::yaml::Path& path, uint32_t index) {
  Value ret{value};
  ret.is_root_ = false;
  ret.path_ = path;
  ret.path_.push_back('[' + std::to_string(index) + ']');
  return ret;
}

Value Value::operator[](const std::string& key) const {
  if (!IsMissing()) {
    CheckObjectOrNull();
    return MakeNonRoot(GetNative()[key], path_, key);
  }
  return MakeNonRoot(MakeMissingNode(), path_, key);
}

Value Value::operator[](uint32_t index) const {
  if (!IsMissing()) {
    CheckInBounds(index);
    return MakeNonRoot(GetNative()[index], path_, index);
  }
  return MakeNonRoot(MakeMissingNode(), path_, index);
}

Value::const_iterator Value::begin() const {
  CheckObjectOrArrayOrNull();
  return {GetNative().begin(), GetNative().IsSequence() ? 0 : -1, path_};
}

Value::const_iterator Value::end() const {
  CheckObjectOrArrayOrNull();
  return {GetNative().end(),
          GetNative().IsSequence() ? static_cast<int>(GetNative().size()) : -1,
          path_};
}

uint32_t Value::GetSize() const {
  CheckObjectOrArrayOrNull();
  return GetNative().size();
}

bool Value::operator==(const Value& other) const {
  return GetNative().Scalar() == other.GetNative().Scalar();
}

bool Value::operator!=(const Value& other) const { return !(*this == other); }

bool Value::IsMissing() const { return !*value_pimpl_; }

template <class T>
bool Value::IsConvertible() const {
  if (IsMissing()) {
    return false;
  }

  bool ok = true;
  value_pimpl_->as<T>(IsConvertibleChecker<T>{ok});
  return ok;
}

template <class T>
T Value::ValueAs() const {
  CheckNotMissing();

  bool ok = true;
  auto res = value_pimpl_->as<T>(IsConvertibleChecker<T>{ok});
  if (!ok) {
    throw TypeMismatchException(*value_pimpl_, utils::GetTypeName<T>(),
                                GetPath());
  }
  return res;
}

bool Value::IsNull() const { return !IsMissing() && value_pimpl_->IsNull(); }
bool Value::IsBool() const { return IsConvertible<bool>(); }
bool Value::IsInt() const { return IsConvertible<int32_t>(); }
bool Value::IsInt64() const { return IsConvertible<long long>(); }
bool Value::IsUInt64() const { return IsConvertible<unsigned long long>(); }
bool Value::IsDouble() const { return IsConvertible<double>(); }
bool Value::IsString() const {
  return !IsMissing() && value_pimpl_->IsScalar();
}
bool Value::IsArray() const {
  return !IsMissing() && value_pimpl_->IsSequence();
}
bool Value::IsObject() const { return !IsMissing() && value_pimpl_->IsMap(); }

template <>
bool Value::As<bool>() const {
  return ValueAs<bool>();
}

template <>
int32_t Value::As<int32_t>() const {
  return ValueAs<int32_t>();
}

template <>
int64_t Value::As<int64_t>() const {
  return ValueAs<int64_t>();
}

template <>
uint64_t Value::As<uint64_t>() const {
  return ValueAs<uint64_t>();
}

template <>
double Value::As<double>() const {
  return ValueAs<double>();
}

template <>
std::string Value::As<std::string>() const {
  return ValueAs<std::string>();
}

bool Value::HasMember(const char* key) const {
  return !IsMissing() && (*value_pimpl_)[key];
}

bool Value::HasMember(const std::string& key) const {
  return !IsMissing() && (*value_pimpl_)[key];
}

std::string Value::GetPath() const { return PathToString(path_); }

Value Value::Clone() const {
  Value v;
  *v.value_pimpl_ = YAML::Clone(GetNative());
  v.is_root_ = is_root_;
  v.path_ = path_;
  return v;
}

void Value::SetNonRoot(const YAML::Node& val, const formats::yaml::Path& path,
                       const std::string& key) {
  UASSERT_MSG(val, "Assigining a missing node");
  *this = MakeNonRoot(val, path, key);
}

void Value::SetNonRoot(const YAML::Node& val, const formats::yaml::Path& path,
                       uint32_t index) {
  UASSERT_MSG(val, "Assigining a missing node");
  *this = MakeNonRoot(val, path, index);
}

void Value::EnsureValid() const { CheckNotMissing(); }

bool Value::IsRoot() const { return is_root_; }

bool Value::DebugIsReferencingSameMemory(const Value& other) const {
  return value_pimpl_->is(*other.value_pimpl_);
}

const YAML::Node& Value::GetNative() const {
  EnsureValid();
  return *value_pimpl_;
}

YAML::Node& Value::GetNative() {
  EnsureValid();
  return *value_pimpl_;
}

void Value::CheckNotMissing() const {
  if (IsMissing()) {
    throw MemberMissingException(GetPath());
  }
}

void Value::CheckArrayOrNull() const {
  if (!IsArray() && !IsNull()) {
    throw TypeMismatchException(FromNative(GetNative().Type()), Type::kArray,
                                GetPath());
  }
}

void Value::CheckObjectOrNull() const {
  if (!IsObject() && !IsNull()) {
    throw TypeMismatchException(FromNative(GetNative().Type()), Type::kObject,
                                GetPath());
  }
}

void Value::CheckObjectOrArrayOrNull() const {
  if (!IsObject() && !IsArray() && !IsNull()) {
    throw TypeMismatchException(FromNative(GetNative().Type()), Type::kArray,
                                GetPath());
  }
}

void Value::CheckInBounds(uint32_t index) const {
  CheckArrayOrNull();
  if (!GetNative()[index]) {
    throw OutOfBoundsException(index, GetSize(), GetPath());
  }
}

}  // namespace yaml
}  // namespace formats
