#pragma once

#include <string>
#include <iostream>

#define PRINT_EXIT(message, err_code) \
    std::cerr << message << std::endl; \
    return err_code;
