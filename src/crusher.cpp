// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "crusher.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace {
using Bytes = std::span<const std::uint8_t>;

// Reverse engineered from the Crusher! 3.0 routines linked into MISSION.EXE:
//   a8578 / a84b0: MSB-first bits; af640: code-length tree;
//   af728: literal/match tree; af914: distance classes;
//   af860: block headers and symbols; a88f0: 4096-byte LZ history.
// Addresses are virtual 32-bit LE object addresses (the image starts at 0x10000).
class Bits {
  public:
    explicit Bits(Bytes stream) : stream_(stream) {}

    unsigned read(unsigned count) {
        if (count > 16 || at_ > stream_.size() * 8 ||
            count > stream_.size() * 8 - at_)
            throw std::runtime_error("Crusher stream ended early");
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i, ++at_)
            value = value << 1 | ((stream_[at_ / 8] >> (7 - at_ % 8)) & 1);
        return value;
    }

    std::size_t remaining() const { return stream_.size() * 8 - at_; }

  private:
    Bytes stream_;
    std::size_t at_ = 0;
};

// The original library materializes a 256-entry prefix table and left/right branches. A
// canonical Huffman trie is equivalent and avoids its large mutable global lookup tables.
class Huffman {
  public:
    static Huffman constant(unsigned value) {
        Huffman tree;
        tree.constant_ = static_cast<int>(value);
        return tree;
    }

    explicit Huffman(std::span<const std::uint8_t> lengths) {
        std::array<unsigned, 17> counts{};
        for (auto length : lengths) {
            if (length > 16)
                throw std::runtime_error("Crusher Huffman code longer than 16 bits");
            if (length)
                ++counts[length];
        }
        unsigned next = 0;
        std::array<unsigned, 17> codes{};
        for (unsigned length = 1; length <= 16; ++length) {
            next = (next + counts[length - 1]) * 2;
            codes[length] = length == 1 ? 0 : next;
            if (codes[length] + counts[length] > (1u << length))
                throw std::runtime_error("oversubscribed Crusher Huffman tree");
        }

        nodes_.emplace_back();
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
            const unsigned length = lengths[symbol];
            if (!length)
                continue;
            const unsigned code = codes[length]++;
            int node = 0;
            for (unsigned bit = length; bit > 0; --bit) {
                if (nodes_[node].symbol >= 0)
                    throw std::runtime_error("overlapping Crusher Huffman codes");
                const unsigned branch = (code >> (bit - 1)) & 1;
                int child = nodes_[node].child[branch];
                if (child < 0) {
                    child = static_cast<int>(nodes_.size());
                    nodes_[node].child[branch] = child;
                    nodes_.emplace_back();
                }
                node = child;
            }
            if (nodes_[node].symbol >= 0 || nodes_[node].child[0] >= 0 ||
                nodes_[node].child[1] >= 0)
                throw std::runtime_error("overlapping Crusher Huffman codes");
            nodes_[node].symbol = static_cast<int>(symbol);
        }
        if (nodes_.size() == 1)
            throw std::runtime_error("empty Crusher Huffman tree");
    }

    unsigned read(Bits &bits) const {
        if (constant_ >= 0)
            return static_cast<unsigned>(constant_);
        int node = 0;
        while (nodes_[node].symbol < 0) {
            node = nodes_[node].child[bits.read(1)];
            if (node < 0)
                throw std::runtime_error("invalid Crusher Huffman code");
        }
        return static_cast<unsigned>(nodes_[node].symbol);
    }

  private:
    Huffman() = default;
    struct Node {
        std::array<int, 2> child{-1, -1};
        int symbol = -1;
    };
    int constant_ = -1;
    std::vector<Node> nodes_;
};

unsigned read_code_length(Bits &bits) {
    unsigned value = bits.read(3);
    if (value == 7) {
        // 000..110 are three-bit lengths; 1110, 11110, ... extend lengths from 7.
        while (bits.read(1)) {
            if (++value > 16)
                throw std::runtime_error("Crusher Huffman code longer than 16 bits");
        }
    }
    return value;
}

Huffman read_short_tree(Bits &bits, unsigned alphabet, unsigned count_bits, int zeros_after) {
    const unsigned count = bits.read(count_bits);
    if (!count) {
        const unsigned symbol = bits.read(count_bits);
        if (symbol >= alphabet)
            throw std::runtime_error("Crusher fixed symbol outside alphabet");
        return Huffman::constant(symbol);
    }
    std::vector<std::uint8_t> lengths;
    while (lengths.size() < count) {
        lengths.push_back(static_cast<std::uint8_t>(read_code_length(bits)));
        if (static_cast<int>(lengths.size()) == zeros_after) {
            const unsigned zeros = bits.read(2);
            lengths.insert(lengths.end(), zeros, 0);
        }
        if (lengths.size() > alphabet)
            throw std::runtime_error("Crusher code-length table too large");
    }
    lengths.resize(alphabet);
    return Huffman(lengths);
}

Huffman read_literal_tree(Bits &bits, const Huffman &meta) {
    constexpr unsigned alphabet = 510;
    const unsigned count = bits.read(9);
    if (!count) {
        const unsigned symbol = bits.read(9);
        if (symbol >= alphabet)
            throw std::runtime_error("Crusher fixed literal outside alphabet");
        return Huffman::constant(symbol);
    }
    std::vector<std::uint8_t> lengths;
    while (lengths.size() < count) {
        const unsigned symbol = meta.read(bits);
        if (symbol == 0)
            lengths.push_back(0);
        else if (symbol == 1)
            lengths.insert(lengths.end(), 3 + bits.read(4), 0);
        else if (symbol == 2)
            lengths.insert(lengths.end(), 20 + bits.read(9), 0);
        else
            lengths.push_back(static_cast<std::uint8_t>(symbol - 2));
        if (lengths.size() > alphabet)
            throw std::runtime_error("Crusher literal table too large");
    }
    lengths.resize(alphabet);
    return Huffman(lengths);
}
} // namespace

namespace gateway {
std::vector<std::uint8_t> decode_crusher(Bytes stream, std::size_t uncompressed_size) {
    if (uncompressed_size > 64 * 1024 * 1024)
        throw std::runtime_error("Crusher output exceeds safety limit");
    if (!uncompressed_size)
        return {};
    Bits bits(stream);
    std::vector<std::uint8_t> out;
    out.reserve(uncompressed_size);
    std::array<std::uint8_t, 4096> history{};

    unsigned block_tokens = 0;
    auto literals = Huffman::constant(0);
    auto distances = Huffman::constant(0);
    while (out.size() < uncompressed_size) {
        if (!block_tokens) {
            block_tokens = bits.read(16);
            if (!block_tokens)
                throw std::runtime_error("Crusher block has no symbols");
            const auto meta = read_short_tree(bits, 19, 5, 3);
            literals = read_literal_tree(bits, meta);
            distances = read_short_tree(bits, 13, 4, -1);
        }
        --block_tokens;
        const unsigned symbol = literals.read(bits);
        if (symbol < 256) {
            const auto pixel = static_cast<std::uint8_t>(symbol);
            history[out.size() & 4095] = pixel;
            out.push_back(pixel);
            continue;
        }
        const unsigned length = symbol - 253; // 256 is a three-byte match.
        const unsigned distance_class = distances.read(bits);
        const unsigned distance = distance_class == 0
                                      ? 0
                                      : (1u << (distance_class - 1)) + bits.read(distance_class - 1);
        if (distance >= history.size() || length > uncompressed_size - out.size())
            throw std::runtime_error("Crusher LZ match outside output window");
        for (unsigned j = 0; j < length; ++j) {
            const auto pixel = history[(out.size() - distance - 1) & 4095];
            history[out.size() & 4095] = pixel;
            out.push_back(pixel);
        }
    }
    if (block_tokens)
        throw std::runtime_error("Crusher output size ends within a compressed block");
    // A compressed PIC record is byte-aligned. Additional whole bytes indicate that the
    // expected decompressed size (usually width * height) was not the stream's full output.
    if (bits.remaining() >= 8)
        throw std::runtime_error("Crusher stream has unconsumed compressed bytes");
    return out;
}
} // namespace gateway
