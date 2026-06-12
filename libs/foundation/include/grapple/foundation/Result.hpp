#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace grapple::foundation {

struct Error {
  std::string code;
  std::string message;
};

template <typename T>
class Result {
public:
  Result(T value)
    : value_(std::move(value)) {}

  Result(Error error)
    : value_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept {
    return std::holds_alternative<T>(value_);
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return ok();
  }

  [[nodiscard]] const T& value() const {
    return std::get<T>(value_);
  }

  [[nodiscard]] T& value() {
    return std::get<T>(value_);
  }

  [[nodiscard]] const Error& error() const {
    return std::get<Error>(value_);
  }

private:
  std::variant<T, Error> value_;
};

template <>
class Result<void> {
public:
  Result() = default;

  Result(Error error)
    : error_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept {
    return !error_.has_value();
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return ok();
  }

  [[nodiscard]] const Error& error() const {
    return *error_;
  }

private:
  std::optional<Error> error_;
};

} // namespace grapple::foundation
