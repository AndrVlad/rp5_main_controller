#include <bits/stdc++.h>
#include <regex>
using namespace std;

int main() {
    std::string str = "+CPMS: \"SM\",5,5,\"SM\",5,5,\"SM\",4,5\r\nOK\r\n";
    std::regex pattern(
        R"(^\+CPMS:\s*\"([^\"]+)\",(\d+),(\d+),\"([^\"]+)\",(\d+),(\d+),\"([^\"]+)\",(\d+),(\d+)\s*)"
    );
    std::smatch match;

    std::string mem1, used1, total1;

    if (std::regex_search(str, match, pattern)) {
        mem1 = match[7].str();
        used1 = match[8].str();
        total1 = match[9].str();
        
        std::cout << "mem3: " << mem1 << std::endl;
        std::cout << "used3: " << used1 << std::endl;
        std::cout << "total3: " << total1 << std::endl;

    } else {
        std::cerr << "Ошибка: строка не соответствует формату +CPMS" << std::endl;
    }
    
    if (stoi(used1) == stoi (total1)) {
        std::cout << "Заполнено" << std::endl;
    } else {
        std::cout << "Не заполнено" << std::endl;
    }
    
    return 0;
}
