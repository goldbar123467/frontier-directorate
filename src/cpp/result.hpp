#ifndef FRONTIER_DIRECTORATE_CPP_RESULT_HPP
#define FRONTIER_DIRECTORATE_CPP_RESULT_HPP

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "frontier_directorate/fd.h"

namespace frontier_directorate {

// A stable, owned copy of a diagnostic returned by the C boundary.  The C
// diagnostic's character array is never retained by reference.
struct Error final {
    fd_result code{FD_ERR_INTERNAL};
    std::uint32_t field_id{};
    std::uint32_t item_index{};
    std::string message{};
};

[[nodiscard]] Error error_from(fd_result code,
                               const fd_diagnostic& diagnostic) noexcept;

// An intentionally small expected-like type for C++20.  Accessors assert when
// used on the wrong alternative rather than throwing std::bad_variant_access;
// callers are expected to inspect has_value() first.
template <class T>
class [[nodiscard]] Result final {
public:
    static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    static Result failure(Error error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return value_.index() == 0U;
    }

    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & noexcept {
        T* const result = std::get_if<0>(&value_);
        assert(result != nullptr);
        return *result;
    }

    [[nodiscard]] const T& value() const& noexcept {
        const T* const result = std::get_if<0>(&value_);
        assert(result != nullptr);
        return *result;
    }

    [[nodiscard]] T&& value() && noexcept {
        T* const result = std::get_if<0>(&value_);
        assert(result != nullptr);
        return std::move(*result);
    }

    [[nodiscard]] Error& error() & noexcept {
        Error* const result = std::get_if<1>(&value_);
        assert(result != nullptr);
        return *result;
    }

    [[nodiscard]] const Error& error() const& noexcept {
        const Error* const result = std::get_if<1>(&value_);
        assert(result != nullptr);
        return *result;
    }

private:
    template <class... Args>
    explicit Result(std::in_place_index_t<0> index, Args&&... args)
        : value_(index, std::forward<Args>(args)...) {}

    template <class... Args>
    explicit Result(std::in_place_index_t<1> index, Args&&... args)
        : value_(index, std::forward<Args>(args)...) {}

    std::variant<T, Error> value_;
};

template <>
class [[nodiscard]] Result<void> final {
public:
    static Result success() { return Result(true, Error{}); }
    static Result failure(Error error) {
        return Result(false, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept { return success_; }
    explicit operator bool() const noexcept { return success_; }

    [[nodiscard]] const Error& error() const noexcept {
        assert(!success_);
        return error_;
    }

private:
    explicit Result(bool success, Error error)
        : success_(success), error_(std::move(error)) {}

    bool success_{};
    Error error_{};
};

}  // namespace frontier_directorate

#endif
