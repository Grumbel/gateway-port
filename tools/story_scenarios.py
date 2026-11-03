# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

"""Deterministic keyboard scripts for the bounded Gateway first-room replay.

The seven configurable slots are spaced three seconds apart. They are useful
for comparing action/state transitions, not for automated gameplay testing.
"""

OBJECT_SLOTS = (18000, 21000, 25000, 28000, 31000, 34000, 37000)
DEFAULT_OBJECT_COMMANDS = (
    'open drawer', 'look', 'take book', 'inventory',
    'drop book', 'close drawer', 'look',
)
PUZZLE_FIRST_COMMAND_MS = 18000
PUZZLE_COMMAND_SPACING_MS = 3000
PUZZLE_MAX_COMMANDS = 48
PUZZLE_KEYS = {'@space': 0x3920, '@enter': 0x1c0d, '@escape': 0x011b}


def parse_sequence(sequence):
    """Parse exactly seven printable ASCII commands, in chronological order."""
    commands = tuple(command.strip() for command in sequence.split(','))
    if len(commands) != len(OBJECT_SLOTS):
        raise ValueError(f'expected {len(OBJECT_SLOTS)} comma-separated commands')
    for command in commands:
        if not command or len(command) > 64 or any(not 32 <= ord(ch) <= 126
                                                    for ch in command):
            raise ValueError('commands must contain 1–64 printable ASCII characters')
    return commands


def parse_puzzle_sequence(sequence):
    """Up to 48 commands in a bounded native-only experiment (3 seconds apart)."""
    commands = tuple(command.strip() for command in sequence.split(','))
    if not 1 <= len(commands) <= PUZZLE_MAX_COMMANDS:
        raise ValueError('puzzle scenario requires 1–48 comma-separated commands')
    for command in commands:
        if not command or len(command) > 64 or any(not 32 <= ord(ch) <= 126
                                                  for ch in command):
            raise ValueError('commands must contain 1–64 printable ASCII characters')
        if command.startswith('@') and command not in PUZZLE_KEYS:
            raise ValueError('unknown puzzle key directive: ' + command)
    return commands


def puzzle_duration(commands):
    if not 1 <= len(commands) <= PUZZLE_MAX_COMMANDS:
        raise ValueError('puzzle scenario requires 1–48 commands')
    return PUZZLE_FIRST_COMMAND_MS + (len(commands) - 1) * PUZZLE_COMMAND_SPACING_MS + 2500


def schedule(scenario, commands=None):
    """Return the exact BIOS-key schedule passed to gateway-probe."""
    if scenario not in ('look', 'drawer', 'objects', 'puzzle'):
        raise ValueError(f'unknown scenario: {scenario}')
    if commands is not None and scenario not in ('objects', 'puzzle'):
        raise ValueError('custom commands require the objects or puzzle scenario')
    if scenario == 'puzzle':
        if commands is None or not 1 <= len(commands) <= PUZZLE_MAX_COMMANDS:
            raise ValueError('puzzle scenario requires 1–48 commands')
        if any(not command or len(command) > 64 for command in commands):
            raise ValueError('puzzle commands must contain 1–64 characters')
        keys = schedule('look') + [(time, 0x3920) for time in (15000, 16000, 17000)]
        for index, command in enumerate(commands):
            start = PUZZLE_FIRST_COMMAND_MS + index * PUZZLE_COMMAND_SPACING_MS
            if command in PUZZLE_KEYS:
                keys.append((start, PUZZLE_KEYS[command]))
            else:
                keys += [(start + i * 30, ord(char)) for i, char in enumerate(command)]
                keys.append((start + len(command) * 30, 0x1c0d))
        return sorted(keys)
    keys = [(10000, 0x3920), (11000, 0x3920), (12000, 0x3920)]
    keys += [(14000, 0x266c), (14100, 0x186f), (14200, 0x186f),
             (14300, 0x256b), (14400, 0x1c0d)]
    if scenario in ('drawer', 'objects'):
        keys += [(15000, 0x3920), (16000, 0x3920), (17000, 0x3920)]
        scripted = (list(zip(OBJECT_SLOTS, commands or DEFAULT_OBJECT_COMMANDS))
                    if scenario == 'objects' else [(18000, 'open drawer'), (21000, 'look')])
        for start, command in scripted:
            keys += [(start + i * 30, ord(char)) for i, char in enumerate(command)]
            keys.append((start + len(command) * 30, 0x1c0d))
        keys += [(22000, 0x3920), (23000, 0x3920)]
    if scenario == 'objects':
        keys += [(39000, 0x3920), (40000, 0x3920), (41000, 0x3920)]
    return sorted(keys)
