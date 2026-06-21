#include <functional>
#include <iostream>

int main() 
{
    unsigned int target_mod;
    std::string ip = "127.0.0.1";
    unsigned int port = 0;

    std::cout << "Enter target hash mod 256: ";
    std::cin >> target_mod;

    while (port < 20000)
    {
        std::string input = ip + ":" + std::to_string(port++);

        size_t hash = std::hash<std::string>{}(input);

        size_t hash_mod = hash % 256;
        
        if (hash_mod == target_mod)
            std::cout << "Input: " << input << " => Hash: " << hash << " => Hash mod 256: " << hash_mod << std::endl;
    }

    return 0;
}