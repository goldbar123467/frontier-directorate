#include "result.hpp"

#include <cstddef>
#include <new>
#include <stdexcept>

namespace frontier_directorate {

Error error_from(const fd_result code,
                 const fd_diagnostic& diagnostic) noexcept {
    std::size_t length = 0U;
    while (length < sizeof(diagnostic.message) &&
           diagnostic.message[length] != '\0') {
        ++length;
    }

    Error result{};
    result.code = code;
    result.field_id = diagnostic.field_id;
    result.item_index = diagnostic.item_index;
    try {
        result.message.assign(diagnostic.message, length);
    } catch (const std::bad_alloc&) {
        result.message.clear();
    } catch (const std::length_error&) {
        result.message.clear();
    }
    return result;
}

}  // namespace frontier_directorate
