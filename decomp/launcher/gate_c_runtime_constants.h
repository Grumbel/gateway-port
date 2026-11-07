/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: LicenseRef-Gateway-Derived */

#ifndef GATE_LAUNCHER_C_RUNTIME_CONSTANTS_H
#define GATE_LAUNCHER_C_RUNTIME_CONSTANTS_H

/* Original Microsoft C 5.1 rand.c constants, linked at 12a5:3afa/3b0c
 * from GATE.RTL:91d8/91ea. The LCG wraps to 32 bits, returns the masked
 * high WORD and is used by RTL:13115/12f6b for frame-delay variation. */
enum {
    GATE_CRT_RANDOM_MULTIPLIER = 0x000343fd,
    GATE_CRT_RANDOM_INCREMENT = 0x00269ec3,
    GATE_CRT_RANDOM_RESULT_MASK = 0x7fff,
    GATE_CRT_WORD_BITS = 16,
};

/* Original GATE.RTL:8298 strlen, linked at 12a5:2bba. Its REPNE SCASB
 * starts at CX=ffff and the NOT/DEC epilogue yields fffe when the scan
 * exhausts the 16-bit count without encountering a NUL. */
enum {
    GATE_CRT_FAR_STRING_SCAN_LIMIT = 0xffff,
    GATE_CRT_FAR_STRING_UNTERMINATED_LENGTH = 0xfffe,
};

/* GATE.RTL:8cc8..8d9a timezone parser and 8fa2..8ff8 elapsed-clock
 * original immediates. The CRT _DIGIT class flag comes from the linked
 * character classification table at DS:33f7. Sample displacements refer
 * to the ten-byte SS stack object, not DS globals or file offsets. */
enum {
    GATE_CRT_TIMEZONE_ABBREVIATION_BYTES = 3,
    GATE_CRT_TIMEZONE_DIGIT_CLASS_BIT = 0x04,
    GATE_CRT_SECONDS_PER_HOUR = 3600,
    GATE_CRT_MILLISECONDS_PER_SECOND = 1000,
    GATE_CRT_TIME_SAMPLE_BYTES = 10,
    GATE_CRT_TIME_SAMPLE_HIGH_WORD_OFFSET = 2,
    GATE_CRT_TIME_SAMPLE_FRACTION_WORD_OFFSET = 4,
};

#endif
