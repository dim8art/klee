#include <cstdint>

extern "C" {
    struct FuzzInfo {
        void(*harness)();
    };
    void testfuzz(FuzzInfo fi);
}