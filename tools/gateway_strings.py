"""Read Gateway (not Spellcasting 101/201) Huffman story-text resources."""
import struct


class GatewayStrings:
    def __init__(self, data):
        self.data = data
        position = 0

        def word():
            nonlocal position
            if position + 2 > len(data):
                raise ValueError('truncated GATESTR.DAT header')
            value = struct.unpack_from('<H', data, position)[0]
            position += 2
            return value

        segment_count = word()
        if not 1 <= segment_count <= 256:
            raise ValueError('invalid segment count')
        headers = [(word(), word()) for _ in range(segment_count)]
        if any(count > 1024 for count, _ in headers):
            raise ValueError('implausible string count')
        sizes = [[word() for _ in range(count)] for count, _ in headers]
        if any(sum(row) != compressed for row, (_, compressed) in zip(sizes, headers)):
            raise ValueError('inconsistent compressed string lengths')
        tree_length = word()
        if tree_length < 2 or tree_length > 4096 or tree_length & 1:
            raise ValueError('invalid Huffman tree length')
        self.tree = [word() for _ in range(tree_length)]
        dictionary_count = word()
        if dictionary_count > 2048:
            raise ValueError('implausible dictionary size')
        offsets = [word() for _ in range(dictionary_count)]
        dictionary_length = word()
        if position + dictionary_length > len(data):
            raise ValueError('truncated dictionary')
        dictionary = data[position:position + dictionary_length]
        position += dictionary_length
        self.dictionary = []
        for offset in offsets:
            if offset >= len(dictionary) or b'\0' not in dictionary[offset:]:
                raise ValueError('invalid dictionary entry')
            self.dictionary.append(dictionary[offset:dictionary.index(b'\0', offset)])
        self.segments = []
        for row in sizes:
            segment = []
            for size in row:
                if position + size > len(data):
                    raise ValueError('truncated compressed text')
                segment.append(data[position:position + size])
                position += size
            self.segments.append(segment)
        if position != len(data):
            raise ValueError('trailing GATESTR.DAT data')

    def get(self, segment, index):
        compressed = self.segments[segment][index]
        root = len(self.tree) - 2
        node = root
        result = bytearray()
        for byte in compressed:
            for bit_number in range(8):
                edge = node | ((byte >> bit_number) & 1)
                if edge >= len(self.tree):
                    raise ValueError('invalid Huffman node')
                node = self.tree[edge]
                if node & 0x8000:
                    leaf = node ^ 0xffff
                    if leaf < 0x80:
                        if leaf == 0:
                            return result.decode('cp437')
                        result.append(leaf)
                    else:
                        dictionary_index = leaf - 0x80
                        if dictionary_index >= len(self.dictionary):
                            raise ValueError('invalid dictionary symbol')
                        result.extend(self.dictionary[dictionary_index])
                    node = root
        return result.decode('cp437')
