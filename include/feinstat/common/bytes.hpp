#pragma once

#include "feinstat/common/types.hpp"

#include <cstring>
#include <stdexcept>
#include <string_view>

namespace feinstat {

class ByteWriter {
public:
    void u8(Byte value) { data_.push_back(value); }

    void u32(std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            data_.push_back(static_cast<Byte>((value >> shift) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            data_.push_back(static_cast<Byte>((value >> shift) & 0xffULL));
        }
    }

    void raw(const Byte* data, std::size_t size) {
        if (size == 0) return;
        if (data == nullptr) throw std::invalid_argument("cannot append a null byte range");
        if (size > data_.max_size() - data_.size()) {
            throw std::length_error("serialized byte buffer exceeds maximum size");
        }
        const std::size_t offset = data_.size();
        data_.resize(offset + size);
        std::memcpy(data_.data() + offset, data, size);
    }

    template <std::size_t N>
    void fixed(const std::array<Byte, N>& value) {
        raw(value.data(), value.size());
    }

    void string(std::string_view value) {
        if (value.size() > UINT32_MAX) {
            throw std::length_error("string too large to serialize");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        raw(reinterpret_cast<const Byte*>(value.data()), value.size());
    }

    void blob(const Bytes& value) {
        if (value.size() > UINT32_MAX) {
            throw std::length_error("blob too large to serialize");
        }
        u32(static_cast<std::uint32_t>(value.size()));
        raw(value.data(), value.size());
    }

    [[nodiscard]] const Bytes& data() const noexcept { return data_; }
    [[nodiscard]] Bytes take() && { return std::move(data_); }

private:
    Bytes data_;
};

class ByteReader {
public:
    explicit ByteReader(const Bytes& data) : data_(data) {}

    Byte u8() { require(1); return data_[position_++]; }

    std::uint32_t u32() {
        require(4);
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value = (value << 8U) | data_[position_++];
        return value;
    }

    std::uint64_t u64() {
        require(8);
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value = (value << 8U) | data_[position_++];
        return value;
    }

    Bytes bytes(std::size_t size) {
        require(size);
        Bytes result(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                     data_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return result;
    }

    template <std::size_t N>
    std::array<Byte, N> fixed() {
        require(N);
        std::array<Byte, N> result{};
        for (std::size_t i = 0; i < N; ++i) result[i] = data_[position_++];
        return result;
    }

    std::string string(std::size_t maximum_size = 1U << 20U) {
        const auto size = u32();
        if (size > maximum_size) throw std::runtime_error("serialized string exceeds size limit");
        require(size);
        std::string result(reinterpret_cast<const char*>(data_.data() + position_), size);
        position_ += size;
        return result;
    }

    Bytes blob(std::size_t maximum_size = 4U << 20U) {
        const auto size = u32();
        if (size > maximum_size) throw std::runtime_error("serialized blob exceeds size limit");
        return bytes(size);
    }

    [[nodiscard]] bool empty() const noexcept { return position_ == data_.size(); }

private:
    void require(std::size_t count) const {
        if (count > data_.size() - position_) {
            throw std::runtime_error("truncated canonical data");
        }
    }

    const Bytes& data_;
    std::size_t position_{0};
};

} // namespace feinstat
