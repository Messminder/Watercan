#include "app.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    Watercan::App app;
    
    if (!app.init()) {
        fprintf(stderr, "Failed to initialize Watercan\n");
        return 1;
    }
    
    // If a file path was provided as argument, load it
    if (argc > 1) {
        // The app will need a method to load from command line
        // For now, user can use File > Open
    }
    
    app.run();
    
    return 0;
}
