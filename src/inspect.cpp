// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later WITH GPL-3.0-linking-exception

#include "mz.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: gateway-inspect <DOS executable>\n";
        return 2;
    }
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot open executable");
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
        const auto image = gateway::load_mz(bytes);
        std::cout << "Image bytes: " << image.image_size << "\nEntry CS:IP: " << std::hex
                  << image.cs << ':' << image.ip << "\nStack SS:SP: " << image.ss << ':' << image.sp
                  << "\nPSP segment: " << image.ds << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
