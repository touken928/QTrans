#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace qtrans::download_detail {

inline std::uint32_t rotr(std::uint32_t value, std::uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

inline constexpr std::uint32_t kSha256RoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Minimal SHA-256 so the download domain stays Qt-free. Output matches
// QCryptographicHash::Sha256 + QByteArray::toHex(): lowercase, 64 hex chars.
// Shared by both the streaming download write path and the on-disk verifier so
// there is a single implementation.
class Sha256 {
public:
    void update(const std::uint8_t *data, std::size_t length) {
        total_length_ += length;
        while (length > 0) {
            const std::size_t space = kBlockSize - buffer_length_;
            const std::size_t take = length < space ? length : space;
            std::memcpy(buffer_ + buffer_length_, data, take);
            buffer_length_ += take;
            data += take;
            length -= take;
            if (buffer_length_ == kBlockSize) {
                process_block(buffer_);
                buffer_length_ = 0;
            }
        }
    }

    std::string hex_digest() {
        finalize();
        static constexpr char kHex[] = "0123456789abcdef";
        std::string result;
        result.resize(64);
        for (int word = 0; word < 8; ++word) {
            for (int byte = 0; byte < 4; ++byte) {
                const std::uint8_t value =
                    static_cast<std::uint8_t>((state_[word] >> (24 - byte * 8)) & 0xffU);
                result[static_cast<std::size_t>(word) * 8 + byte * 2] = kHex[value >> 4];
                result[static_cast<std::size_t>(word) * 8 + byte * 2 + 1] = kHex[value & 0x0f];
            }
        }
        return result;
    }

private:
    static constexpr std::size_t kBlockSize = 64;

    void process_block(const std::uint8_t *block) {
        std::uint32_t schedule[64];
        for (int i = 0; i < 16; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                          (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                          (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                          (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(schedule[i - 15], 7) ^ rotr(schedule[i - 15], 18) ^
                                     (schedule[i - 15] >> 3);
            const std::uint32_t s1 = rotr(schedule[i - 2], 17) ^ rotr(schedule[i - 2], 19) ^
                                     (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + ch + kSha256RoundConstants[i] + schedule[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    void finalize() {
        const std::uint64_t bit_length = total_length_ * 8;
        buffer_[buffer_length_++] = 0x80;
        if (buffer_length_ > 56) {
            while (buffer_length_ < kBlockSize) {
                buffer_[buffer_length_++] = 0;
            }
            process_block(buffer_);
            buffer_length_ = 0;
        }
        while (buffer_length_ < 56) {
            buffer_[buffer_length_++] = 0;
        }
        for (int i = 0; i < 8; ++i) {
            buffer_[56 + i] = static_cast<std::uint8_t>((bit_length >> (56 - i * 8)) & 0xffU);
        }
        process_block(buffer_);
        buffer_length_ = 0;
    }

    std::uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t buffer_[kBlockSize] = {};
    std::size_t buffer_length_ = 0;
    std::uint64_t total_length_ = 0;
};

}  // namespace qtrans::download_detail
