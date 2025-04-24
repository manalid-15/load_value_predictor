#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>

#define N 10000

int main() {
    std::vector<int> arr(N);

    // Context-based pattern: load value depends on previous control flow
        int context_sum = 0;
        for (int i = 0; i < N; i++) {
            if (i % 2 == 0)
                context_sum += arr[i];      // Even branch
            else
                context_sum -= arr[i - 1];  // Odd branch, related to previous load
        }

    return 0;
}