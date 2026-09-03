#include "domain/download/model_downloader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(DownloadIntegrity, VerifiesPinnedSha256) {
    const auto path = std::filesystem::temp_directory_path() /
                      "qtrans_sha256_contract.txt";
    {
        std::ofstream output(path, std::ios::binary);
        output << "abc";
    }
    std::string actual;
    EXPECT_TRUE(download_file_matches_sha256(
        path.u8string(),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        &actual));
    EXPECT_EQ(actual,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_FALSE(download_file_matches_sha256(path.u8string(), std::string(64, '0')));
    std::filesystem::remove(path);
}
