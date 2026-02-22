/**
 * @file test_helpers.h
 * @brief Cross-platform test helpers for Nova/Flare tests
 */

#pragma once

#include <filesystem>
#include <string>
#include <chrono>
#include <cstdlib>

namespace test_helpers
{

/**
 * @brief Cross-platform temporary directory creation
 * 
 * Creates a unique temporary directory that works on both Windows and POSIX systems.
 * Uses C++17 std::filesystem for portability.
 */
inline std::filesystem::path createTempDirectory()
{
    std::filesystem::path tempBase = std::filesystem::temp_directory_path();
    std::string uniqueName = "nova_test_" + std::to_string(std::rand()) + 
                             "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::path tempDir = tempBase / uniqueName;
    
    if (!std::filesystem::create_directory(tempDir))
    {
        return std::filesystem::path();  // Return empty path on failure
    }
    
    return tempDir;
}

/**
 * @brief Remove a directory and all its contents
 */
inline bool removeTempDirectory(const std::filesystem::path& dir)
{
    if (std::filesystem::exists(dir))
    {
        return std::filesystem::remove_all(dir) > 0;
    }
    return true;
}

} // namespace test_helpers
