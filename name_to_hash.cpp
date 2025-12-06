#include <functional>
#include <iostream>

int main() 
{
    while (true)
    {
        std::string input;
        std::cin >> input;

        size_t hash = std::hash<std::string>{}(input);
        std::cout << hash % 256 << std::endl;
    }

    return 0;
}