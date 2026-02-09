#include "app.h"
#include <cstdio>

int main() {
    Watercan::App app;
    
    if (!app.init()) {
        fprintf(stderr, "Failed to initialize Watercan\n");
        return 1;
    }
    
    app.run();
    
    return 0;
}
