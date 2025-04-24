#include <iostream>
#include <array>
#include <cstdint>

// store values in an array
// This code is used to test the store-to-load value predictor 
int main() {
    std::array<uint64_t, 1024> store_array = {};

    store_array[0] = 0;
    
    for (int i=1; i < 1024; i++) {
        store_array[i] = store_array[i-1] + i;
    }

    return 0;
}
