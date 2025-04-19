#include <iostream>
#include <vector>

// best case time complexity: O(1)
// worst case time complexity: O(N)

int search(std::vector<int> &arr, int x, int *loc) {
    
    for (int i = 0; i < arr.size(); i++){
        if(arr[i] == x) {
            *loc = i;
            return 0;
        }
    }
    return -1;
}

int main()
{
    // to be searched in
    std::vector<int> v = {1, 2, 3, 4, 5};

    int x = 3;
    int loc = 0;

    int resp = search(v, x, &loc);

    if (resp == 0) {
        std::cout << "The sequence is present at index " << loc << std::endl;
    }
    else {
        std::cout << "Not present" << std::endl;
    }

    return 0;
}
