#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "linux_sdl/sdl_app.hpp"

int main(int argc, char* argv[]) {
    std::string rom_path;
    if (argc > 1) {
        rom_path = argv[1];
    }

    try {
        kairo::linux_sdl::SdlApp app;
        return app.run(rom_path);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
