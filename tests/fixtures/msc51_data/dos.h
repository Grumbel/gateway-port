/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> */
/* SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception */

/* Minimal host header to syntax-check the MSC /AL guest-only probe. */
#ifndef GATE_MSC51_DATA_HOST_DOS_H
#define GATE_MSC51_DATA_HOST_DOS_H
unsigned int _dos_allocmem(unsigned int paragraphs, unsigned int *segment);
unsigned int _dos_freemem(unsigned int segment);
#endif
