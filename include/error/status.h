#pragma once

#include <string>
#include <cstdint>

namespace kv_engine {

enum class ErrorCode : uint8_t {
    OK = 0,
    NOT_FOUND = 1,
    CORRUPTION = 2,
    NOT_SUPPORTED = 3,
    INVALID_ARGUMENT = 4,
    IO_ERROR = 5,
    ALREADY_EXISTS = 6,
    ABORTED = 7,
    BUSY = 8,
    EXPIRED = 9,
    FULL = 10,
    OUT_OF_MEMORY = 11,
    INTERNAL_ERROR = 12,
};

class Status {
public:
    Status() : code_(ErrorCode::OK) {}
    Status(ErrorCode code, const std::string& msg = "") : code_(code), msg_(msg) {}
    
    static Status OK() { return Status(); }
    static Status NotFound(const std::string& msg = "") { return Status(ErrorCode::NOT_FOUND, msg); }
    static Status Corruption(const std::string& msg = "") { return Status(ErrorCode::CORRUPTION, msg); }
    static Status NotSupported(const std::string& msg = "") { return Status(ErrorCode::NOT_SUPPORTED, msg); }
    static Status InvalidArgument(const std::string& msg = "") { return Status(ErrorCode::INVALID_ARGUMENT, msg); }
    static Status IOError(const std::string& msg = "") { return Status(ErrorCode::IO_ERROR, msg); }
    static Status AlreadyExists(const std::string& msg = "") { return Status(ErrorCode::ALREADY_EXISTS, msg); }
    static Status Aborted(const std::string& msg = "") { return Status(ErrorCode::ABORTED, msg); }
    static Status Busy(const std::string& msg = "") { return Status(ErrorCode::BUSY, msg); }
    static Status Expired(const std::string& msg = "") { return Status(ErrorCode::EXPIRED, msg); }
    static Status Full(const std::string& msg = "") { return Status(ErrorCode::FULL, msg); }
    static Status OutOfMemory(const std::string& msg = "") { return Status(ErrorCode::OUT_OF_MEMORY, msg); }
    static Status InternalError(const std::string& msg = "") { return Status(ErrorCode::INTERNAL_ERROR, msg); }

    bool ok() const { return code_ == ErrorCode::OK; }
    ErrorCode code() const { return code_; }
    const std::string& message() const { return msg_; }
    std::string ToString() const;

    bool operator==(const Status& other) const { return code_ == other.code_ && msg_ == other.msg_; }
    bool operator!=(const Status& other) const { return !(*this == other); }

private:
    ErrorCode code_;
    std::string msg_;
};

inline std::string Status::ToString() const {
    if (ok()) return "OK";
    const char* type;
    switch (code_) {
        case ErrorCode::NOT_FOUND: type = "NotFound"; break;
        case ErrorCode::CORRUPTION: type = "Corruption"; break;
        case ErrorCode::NOT_SUPPORTED: type = "NotSupported"; break;
        case ErrorCode::INVALID_ARGUMENT: type = "InvalidArgument"; break;
        case ErrorCode::IO_ERROR: type = "IOError"; break;
        case ErrorCode::ALREADY_EXISTS: type = "AlreadyExists"; break;
        case ErrorCode::ABORTED: type = "Aborted"; break;
        case ErrorCode::BUSY: type = "Busy"; break;
        case ErrorCode::EXPIRED: type = "Expired"; break;
        case ErrorCode::FULL: type = "Full"; break;
        case ErrorCode::OUT_OF_MEMORY: type = "OutOfMemory"; break;
        case ErrorCode::INTERNAL_ERROR: type = "InternalError"; break;
        default: type = "Unknown"; break;
    }
    return msg_.empty() ? type : (std::string(type) + ": " + msg_);
}

} // namespace kv_engine