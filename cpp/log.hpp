#ifndef LOG_HPP
#define LOG_HPP

#include <iostream>

#define LOG(text) std::cout << text << std::endl;

#define LOG_ERR(text) std::cerr << text << std::endl;

#endif // LOG_HPP