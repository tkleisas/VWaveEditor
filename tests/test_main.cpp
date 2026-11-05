#include "test_framework.h"

#include <string>

int main(int argc, char** argv) {
    std::string fileFilter;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--file" && i + 1 < argc) {
            fileFilter = argv[++i];
        }
    }
    return vwtest::runAll(fileFilter);
}
