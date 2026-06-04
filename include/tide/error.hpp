/**
 * @file error.hpp
 * @brief TIDE error codes and per-handle error message buffer.
 *
 * Defines the ErrorCode enumeration organized in category ranges (0-999)
 * and a fixed-size error message buffer that avoids heap allocation on
 * error paths.
 *
 * @section error_ranges Error Code Ranges
 * | Range   | Category              |
 * |---------|-----------------------|
 * | 0       | Success               |
 * | 1-99    | I/O errors            |
 * | 100-199 | Configuration errors  |
 * | 200-299 | Temporal errors       |
 * | 300-399 | Spatial errors        |
 * | 400-499 | Vertical errors       |
 * | 500-599 | Scaling errors        |
 * | 600-699 | Lifecycle errors      |
 * | 700-799 | Field errors          |
 * | 800-899 | Weight/Grid errors    |
 * | 900-999 | Multi-Stream errors   |
 */

#ifndef TIDE_ERROR_HPP
#define TIDE_ERROR_HPP

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace tide {

/**
 * @brief Error code enumeration covering all TIDE subsystems.
 *
 * Codes are organized into ranges by category, enabling quick identification
 * of which subsystem generated a particular error.
 */
enum class ErrorCode : int {
    // ─── Success ─────────────────────────────────────────────────────────
    /// @brief Operation completed successfully.
    Success = 0,

    // ─── I/O errors (1-99) ───────────────────────────────────────────────
    /// @brief File path is invalid or file does not exist.
    FileNotFound = 1,
    /// @brief File exists but cannot be read (permissions, corruption, etc.).
    FileUnreadable = 2,
    /// @brief File format is not supported (not NetCDF or GRIB2).
    UnsupportedFormat = 3,
    /// @brief File does not contain recognized grid metadata (CF, COARDS, or UGRID).
    MissingGridMetadata = 4,
    /// @brief MPI I/O initialization or read failure.
    MpiIoError = 5,

    // ─── Configuration errors (100-199) ──────────────────────────────────
    /// @brief Configuration file not found at specified path.
    ConfigFileNotFound = 100,
    /// @brief YAML syntax error during configuration parsing.
    ConfigParseError = 101,
    /// @brief A required configuration field is missing.
    ConfigMissingField = 102,
    /// @brief A configuration field has an invalid value.
    ConfigInvalidValue = 103,

    // ─── Temporal errors (200-299) ───────────────────────────────────────
    /// @brief Requested model time is outside the file's temporal range.
    TimeOutOfRange = 200,
    /// @brief Forcing data time range exhausted (no more time levels available).
    TimeLevelsExhausted = 201,

    // ─── Spatial / regridding errors (300-399) ───────────────────────────
    /// @brief Atlas failed to compute remapping weights.
    WeightComputationFailed = 300,
    /// @brief Source and target grids do not overlap spatially.
    NonOverlappingGrids = 301,

    // ─── Vertical interpolation errors (400-499) ─────────────────────────
    /// @brief Source column has fewer than 3 valid levels for interpolation.
    InsufficientLevels = 400,
    /// @brief Target level exceeds maximum allowed extrapolation distance.
    ExtrapolationExceeded = 401,

    // ─── Scaling errors (500-599) ────────────────────────────────────────
    /// @brief Scaling parameter M or B is NaN or Inf.
    InvalidScalingParam = 500,

    // ─── Lifecycle errors (600-699) ──────────────────────────────────────
    /// @brief Handle has not been initialized via init().
    UninitializedHandle = 600,
    /// @brief Handle has already been finalized.
    AlreadyFinalized = 601,

    // ─── Field errors (700-799) ──────────────────────────────────────────
    /// @brief Requested field name not registered or not found.
    FieldNotFound = 700,
    /// @brief Requested field has not been computed in the current pipeline cycle.
    FieldNotComputed = 701,

    // ─── Weight file errors (800-849) ────────────────────────────────────
    /// @brief SCRIP weight file not found or unreadable.
    WeightFileNotFound = 800,
    /// @brief SCRIP weight file dimension mismatch with grid.
    WeightFileDimensionMismatch = 801,
    /// @brief SCRIP weight file missing required variables.
    WeightFileMissingVariable = 802,

    // ─── ESMF grid file errors (850-899) ─────────────────────────────────
    /// @brief ESMF file not found or unreadable.
    EsmfFileNotFound = 850,
    /// @brief ESMF mesh file missing required variables.
    EsmfMeshMissingVariable = 851,
    /// @brief ESMF grid spec file missing required variables.
    EsmfGridSpecMissingVariable = 852,

    // ─── Multi-stream errors (900-949) ───────────────────────────────────
    /// @brief Memory budget exceeded during initialization.
    MemoryBudgetExceeded = 900,
    /// @brief Stream index out of range.
    StreamIndexOutOfRange = 901,

    // ─── Prefetch errors (950-999) ───────────────────────────────────────
    /// @brief Prefetch request failed (diagnostic, non-fatal).
    PrefetchFailed = 950,
};

/**
 * @brief Convert an ErrorCode to its underlying integer value.
 *
 * @param code The error code to convert.
 * @return Integer representation of the error code.
 */
[[nodiscard]] constexpr int to_int(ErrorCode code) noexcept {
    return static_cast<int>(code);
}

/**
 * @brief Construct an ErrorCode from a raw integer value.
 *
 * @param value Integer error code.
 * @return Corresponding ErrorCode enum value.
 *
 * @note No validation is performed; if the integer does not correspond
 *       to a named enumerator, the result is implementation-defined but
 *       safe to use with error_category().
 */
[[nodiscard]] constexpr ErrorCode from_int(int value) noexcept {
    return static_cast<ErrorCode>(value);
}

/**
 * @brief Determine the error category from a raw integer code.
 *
 * Maps error codes to human-readable category strings based on their
 * numeric range.
 *
 * @param code Raw integer error code.
 * @return A string_view naming the category, or "Unknown" if out of range.
 */
[[nodiscard]] constexpr std::string_view error_category(int code) noexcept {
    if (code == 0) return "Success";
    if (code >= 1 && code <= 99) return "I/O";
    if (code >= 100 && code <= 199) return "Configuration";
    if (code >= 200 && code <= 299) return "Temporal";
    if (code >= 300 && code <= 399) return "Spatial";
    if (code >= 400 && code <= 499) return "Vertical";
    if (code >= 500 && code <= 599) return "Scaling";
    if (code >= 600 && code <= 699) return "Lifecycle";
    if (code >= 700 && code <= 799) return "Field";
    if (code >= 800 && code <= 899) return "Weight/Grid";
    if (code >= 900 && code <= 999) return "Multi-Stream";
    return "Unknown";
}

/**
 * @brief Determine the error category from an ErrorCode enum value.
 *
 * @param code The ErrorCode to categorize.
 * @return A string_view naming the category.
 */
[[nodiscard]] constexpr std::string_view error_category(ErrorCode code) noexcept {
    return error_category(to_int(code));
}

/**
 * @brief Fixed-size buffer capacity for per-handle error messages.
 *
 * Set to 512 bytes to accommodate descriptive error messages including
 * file paths and field names, without requiring heap allocation.
 */
inline constexpr std::size_t kErrorBufferSize = 512;

/**
 * @brief Per-handle error message buffer with no heap allocation.
 *
 * Stores the most recent error message for a TIDE stream handle as a
 * fixed-size char array. This avoids dynamic allocation on error paths,
 * which is critical for robustness in low-memory or failure scenarios.
 *
 * The buffer is valid from the time an error is set until the next TIDE
 * API call on the same handle (matching the lifetime specified in tide.h).
 *
 * @note Thread safety: each handle owns its own ErrorBuffer instance;
 *       concurrent access to different handles is safe without synchronization.
 */
class ErrorBuffer {
public:
    /**
     * @brief Construct an empty error buffer (no error).
     */
    ErrorBuffer() noexcept {
        clear();
    }

    /**
     * @brief Set the error message from a string_view.
     *
     * The message is truncated to fit the fixed buffer (kErrorBufferSize - 1
     * characters) and null-terminated.
     *
     * @param code    The error code associated with this message.
     * @param message Human-readable error description.
     */
    void set(ErrorCode code, std::string_view message) noexcept {
        code_ = code;
        const std::size_t len = (message.size() < kErrorBufferSize - 1)
                                    ? message.size()
                                    : kErrorBufferSize - 1;
        std::memcpy(buffer_.data(), message.data(), len);
        buffer_[len] = '\0';
    }

    /**
     * @brief Set the error with a formatted context prefix.
     *
     * Produces a message of the form "[context] message", truncated to
     * fit the fixed buffer.
     *
     * @param code    The error code.
     * @param context The subsystem or component name generating the error.
     * @param message Detailed error description.
     */
    void set(ErrorCode code, std::string_view context,
             std::string_view message) noexcept {
        code_ = code;

        std::size_t pos = 0;

        // Write "[context] "
        if (!context.empty() && pos < kErrorBufferSize - 1) {
            buffer_[pos++] = '[';
            const std::size_t ctx_len =
                (context.size() < kErrorBufferSize - pos - 3)
                    ? context.size()
                    : kErrorBufferSize - pos - 3;
            std::memcpy(buffer_.data() + pos, context.data(), ctx_len);
            pos += ctx_len;
            if (pos < kErrorBufferSize - 2) {
                buffer_[pos++] = ']';
                buffer_[pos++] = ' ';
            }
        }

        // Write message
        const std::size_t msg_len =
            (message.size() < kErrorBufferSize - pos - 1)
                ? message.size()
                : kErrorBufferSize - pos - 1;
        std::memcpy(buffer_.data() + pos, message.data(), msg_len);
        pos += msg_len;
        buffer_[pos] = '\0';
    }

    /**
     * @brief Clear the error state.
     *
     * Resets the error code to Success and empties the message buffer.
     */
    void clear() noexcept {
        code_ = ErrorCode::Success;
        buffer_[0] = '\0';
    }

    /**
     * @brief Get the current error code.
     * @return The most recent error code, or ErrorCode::Success if none.
     */
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

    /**
     * @brief Get the current error code as an integer.
     * @return Integer error code (0 = success).
     */
    [[nodiscard]] int code_int() const noexcept { return to_int(code_); }

    /**
     * @brief Get a pointer to the null-terminated error message.
     *
     * Returns an empty string ("") if no error has been set.
     * The pointer remains valid for the lifetime of this ErrorBuffer
     * or until set() / clear() is called.
     *
     * @return Pointer to the error message string.
     */
    [[nodiscard]] const char* message() const noexcept {
        return buffer_.data();
    }

    /**
     * @brief Check whether an error is currently stored.
     * @return true if the error code is not Success.
     */
    [[nodiscard]] bool has_error() const noexcept {
        return code_ != ErrorCode::Success;
    }

private:
    /// @brief The most recent error code.
    ErrorCode code_ = ErrorCode::Success;

    /// @brief Fixed-size character buffer for the error message.
    std::array<char, kErrorBufferSize> buffer_{};
};

} // namespace tide

#endif // TIDE_ERROR_HPP
