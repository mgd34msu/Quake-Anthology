#include "guest/host_child.h"

#include <stdio.h>

int main(int argc, char **argv) {
    bool handled = false;
    int status = 0;
    qa_error error = {0};
    if (!guest_host_child_bootstrap(argc, argv, &handled, &status, &error)) {
        fprintf(stderr, "qa-native-host: %s\n", error.message);
        return 1;
    }
    if (handled) return status;
    fprintf(stderr, "qa-native-host is the engine's owned native module helper.\n");
    return 1;
}
