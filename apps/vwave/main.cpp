#include "App.h"

#include <string>

int main(int argc, char** argv) {
    App app;
    const std::string initial = argc > 1 ? argv[1] : std::string{};
    if (!app.init(initial)) return 1;
    return app.run();
}
