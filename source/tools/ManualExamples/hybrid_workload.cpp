#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>

#define N 10000

int main() {
    std::srand(time(0));
    
    std::vector<int> arr(N);
    std::vector<int> copy_arr(N);
    
    // Generate a stride pattern
    for (int i = 0; i < N; i++) {
        arr[i] = i * 4;  // Stride of 4
    }

    // Access in a loop — triggers stride predictor
    int stride_sum = 0;
    for (int i = 0; i < N; i++) {
        stride_sum += arr[i];
    }

    // Context-based pattern: load value depends on previous control flow
    int context_sum = 0;
    for (int i = 0; i < N; i++) {
        if (i % 2 == 0)
            context_sum += arr[i];      // Even branch
        else
            context_sum -= arr[i - 1];  // Odd branch, related to previous load
    }

    // Store-to-load forwarding simulation
    for (int i = 0; i < N; i++) {
        copy_arr[i] = arr[i];           // Store
    }

    int sl_sum = 0;
    for (int i = 0; i < N; i++) {
        sl_sum += copy_arr[i];          // Load, value should match recent store
    }

    std::cout << "Stride sum: " << stride_sum << std::endl;
    std::cout << "Context sum: " << context_sum << std::endl;
    std::cout << "Store-to-load sum: " << sl_sum << std::endl;

    return 0;
}
