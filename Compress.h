#pragma once
#include <string>

std::string compress_string_zstd(const std::string& str);
std::string decompress_string_zstd(const std::string& str);

std::string compress_string(const std::string& str);
std::string decompress_string(const std::string& str);

