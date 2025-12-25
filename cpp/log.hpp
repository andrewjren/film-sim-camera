#ifndef LOG_HPP
#define LOG_HPP

#include <iostream>

#define LOG std::cout << "log(" __FILE__ << ":" << __LINE__ << "): "

#define LOG_ERR std::cerr << "err(" __FILE__ << ":" << __LINE__ << "): "

#endif // LOG_HPP