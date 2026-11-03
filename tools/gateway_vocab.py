# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Decode Gateway VOCAB.DAT words, classifications and unresolved link groups.

The link-pair meanings are intentionally not inferred from numeric coincidence.
"""
import argparse
import pathlib
import struct


def decode_word_stream(tree, compressed):
    node = len(tree) - 2
    root = node
    result = bytearray()
    for byte in compressed:
        for bit in range(8):
            next_node = node | ((byte >> bit) & 1)
            if next_node >= len(tree):
                raise ValueError('vocabulary Huffman edge outside tree')
            node = tree[next_node]
            if node & 0x8000:
                character = node ^ 0xffff
                if character > 255:
                    raise ValueError('vocabulary symbol outside a byte')
                result.append(character)
                node = root
    return bytes(result)


class GatewayVocabulary:
    def __init__(self, data):
        cursor = 0

        def word():
            nonlocal cursor
            if cursor + 2 > len(data):
                raise ValueError('truncated VOCAB.DAT')
            value = struct.unpack_from('<H', data, cursor)[0]
            cursor += 2
            return value

        count = word()
        if not 2 <= count <= 4096 or count & 1:
            raise ValueError('invalid vocabulary tree size')
        tree = [word() for _ in range(count)]
        compressed_size = word()
        if cursor + compressed_size > len(data):
            raise ValueError('truncated vocabulary compressed stream')
        words = decode_word_stream(tree, data[cursor:cursor + compressed_size])
        cursor += compressed_size
        self.entries = []
        count = word()
        if count > 20000:
            raise ValueError('implausible vocabulary word count')
        for number in range(count):
            offset, flags = word(), word()
            if offset >= len(words) or (offset and words[offset - 1] != 0):
                raise ValueError(f'word {number} is not at a string boundary')
            end = words.find(b'\0', offset)
            if end < 0:
                raise ValueError(f'word {number} is unterminated')
            self.entries.append(dict(index=number, text=words[offset:end].decode('cp437'),
                                     flags=flags, offset=offset))
        count = word()
        if count > 1024:
            raise ValueError('implausible vocabulary link-group count')
        self.groups = []
        for _ in range(count):
            key, length = word(), word()
            if length > 4096:
                raise ValueError('implausible vocabulary link-group size')
            pairs = [(word(), word()) for _ in range(length)]
            self.groups.append(dict(key=key, pairs=pairs))
        if cursor != len(data):
            raise ValueError('trailing vocabulary bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=pathlib.Path)
    parser.add_argument('words', nargs='*', help='exact vocabulary words to show')
    parser.add_argument('--groups', action='store_true', help='show the unresolved link groups')
    args = parser.parse_args()
    vocabulary = GatewayVocabulary(args.file.read_bytes())
    if args.words:
        wanted = {word.lower() for word in args.words}
        selected = [item for item in vocabulary.entries if item['text'].lower() in wanted]
    else:
        selected = vocabulary.entries
    for item in selected:
        print(f"{item['index']:4} flags=0x{item['flags']:04x} offset=0x{item['offset']:04x} {item['text']}")
    if args.groups:
        by_index = {item['index']: item['text'] for item in vocabulary.entries}
        for group in vocabulary.groups:
            indices = [group['key']] + [left for left, _ in group['pairs']]
            if args.words and not any(by_index.get(index, '').lower() in wanted
                                      for index in indices):
                continue
            print(f"group key=0x{group['key']:04x}"
                  f" ({by_index.get(group['key'], '<invalid>')}) pairs=" +
                  ' '.join(f"({left:04x}:{by_index.get(left, '<invalid>')},"
                           f'{right:04x})' for left, right in group['pairs']))


if __name__ == '__main__':
    main()
