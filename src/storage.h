#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
namespace storage {
std::filesystem::path root();
void setTestRoot(const std::filesystem::path& path);
std::vector<uint8_t> read(const std::string& name);
void write(const std::string& name, const void* data, size_t size);
std::vector<uint8_t> readRom(const std::string& path);
std::string pickedRom();
void pickRom();
void pickDirectory();
std::array<int,4> safeInsets();
void rememberRom(const std::string& path);
}
