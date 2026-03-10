import math; // 引入刚才定义的 math 模块
#include <iostream>

int main()
{
    math::add(1, 2);
    std::cout << "1 + 2 = " << math::add(1, 2) << std::endl;
    std::cout << "C++ Modules are working!" << std::endl;
    return 0;
}