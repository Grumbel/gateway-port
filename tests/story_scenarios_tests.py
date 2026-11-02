"""Input-schedule regression tests; no original Gateway data is required."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from story_scenarios import (DEFAULT_OBJECT_COMMANDS, parse_puzzle_sequence, parse_sequence,
                             puzzle_duration, schedule)


class StoryScenariosTests(unittest.TestCase):
    def test_default_objects_schedule_preserves_legacy_replay(self):
        keys = [(10000, 0x3920), (11000, 0x3920), (12000, 0x3920)]
        keys += [(14000, 0x266c), (14100, 0x186f), (14200, 0x186f),
                 (14300, 0x256b), (14400, 0x1c0d)]
        keys += [(15000, 0x3920), (16000, 0x3920), (17000, 0x3920)]
        for time, command in zip((18000, 21000, 25000, 28000, 31000, 34000, 37000),
                                 DEFAULT_OBJECT_COMMANDS):
            keys += [(time + i * 30, ord(char)) for i, char in enumerate(command)]
            keys.append((time + len(command) * 30, 0x1c0d))
        keys += [(22000, 0x3920), (23000, 0x3920)]
        keys += [(39000, 0x3920), (40000, 0x3920), (41000, 0x3920)]
        self.assertEqual(schedule('objects'), sorted(keys))

    def test_drawer_and_look_schedules_stay_unchanged(self):
        look = schedule('look')
        self.assertEqual(look, [(10000, 0x3920), (11000, 0x3920), (12000, 0x3920),
                                (14000, 0x266c), (14100, 0x186f), (14200, 0x186f),
                                (14300, 0x256b), (14400, 0x1c0d)])
        drawer = schedule('drawer')
        self.assertIn((18000, ord('o')), drawer)
        self.assertIn((21120, 0x1c0d), drawer)
        self.assertNotIn((25000, ord('t')), drawer)

    def test_custom_sequence_uses_original_slots(self):
        values = ('open drawer', 'close drawer', 'open drawer', 'take book',
                  'drop book', 'take book', 'inventory')
        sequence = ', '.join(values)
        self.assertEqual(parse_sequence(sequence), values)
        keys = schedule('objects', parse_sequence(sequence))
        self.assertIn((21000, ord('c')), keys)
        self.assertIn((25000, ord('o')), keys)
        self.assertIn((34000, ord('t')), keys)
        self.assertIn((37270, 0x1c0d), keys)  # nine-letter "inventory"
        self.assertNotIn((21000, ord('l')), keys)

    def test_sequence_is_bounded_and_rejects_nonascii_and_wrong_scenario(self):
        for value in ('look', 'a,b,c,d,e,f,', 'a,b,c,d,e,f,é',
                      'a,b,c,d,e,f,' + 'x' * 65, 'a,b,c,d,e,f,\n'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_sequence(value)
        with self.assertRaisesRegex(ValueError, 'objects or puzzle scenario'):
            schedule('drawer', DEFAULT_OBJECT_COMMANDS)

    def test_long_native_puzzle_script_preserves_startup_and_bounds_duration(self):
        commands = parse_puzzle_sequence('east,northwest,up,take pin,wait,look')
        keys = schedule('puzzle', commands)
        self.assertEqual(keys[:8], schedule('look')[:8])
        self.assertIn((18000, ord('e')), keys)
        self.assertIn((27000, ord('t')), keys)
        self.assertIn((33000, ord('l')), keys)
        self.assertNotIn((22000, 0x3920), keys)  # no background pagination keys
        self.assertEqual(puzzle_duration(commands), 35500)
        many = parse_puzzle_sequence(','.join('wait' for _ in range(40)))
        self.assertEqual(puzzle_duration(many), 137500)
        self.assertEqual(schedule('puzzle', many)[-1][0], 135120)

    def test_puzzle_rejects_empty_invalid_and_overlong_sequences(self):
        for sequence in ('', ',look', 'look,', 'look,é', 'a' * 65,
                         'look,@unverified',
                         ','.join('wait' for _ in range(41))):
            with self.subTest(sequence=sequence[:30]), self.assertRaises(ValueError):
                parse_puzzle_sequence(sequence)
        with self.assertRaisesRegex(ValueError, 'requires 1–40 commands'):
            schedule('puzzle')

    def test_puzzle_supports_one_shot_keys_without_typing_their_names(self):
        commands = parse_puzzle_sequence('lie on couch,@space,@enter,@escape,wait')
        keys = schedule('puzzle', commands)
        self.assertIn((21000, 0x3920), keys)
        self.assertIn((24000, 0x1c0d), keys)
        self.assertIn((27000, 0x011b), keys)
        self.assertNotIn((21000, ord('@')), keys)


if __name__ == '__main__':
    unittest.main()
