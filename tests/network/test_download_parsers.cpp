#include "network/download.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

TEST(DownloadParseSpec, ParsesRepoAndFilename) {
    DownloadSpec spec;
    ASSERT_TRUE(download_parse_spec("owner/repo/file.gguf", spec));
    EXPECT_EQ(spec.repo, "owner/repo");
    EXPECT_EQ(spec.filename, "file.gguf");
}

TEST(DownloadParseSpec, ParsesRepoWithSubpath) {
    DownloadSpec spec;
    // Repo is everything up to the second '/'; filename is the rest.
    ASSERT_TRUE(download_parse_spec("owner/repo/sub/dir/file.gguf", spec));
    EXPECT_EQ(spec.repo, "owner/repo");
    EXPECT_EQ(spec.filename, "sub/dir/file.gguf");
}

TEST(DownloadParseSpec, RejectsMissingRepoSeparator) {
    DownloadSpec spec;
    EXPECT_FALSE(download_parse_spec("only_one_segment", spec));
}

TEST(DownloadParseSpec, RejectsMissingFileSeparator) {
    DownloadSpec spec;
    EXPECT_FALSE(download_parse_spec("owner_only", spec));
}

TEST(DownloadParseSpec, RejectsEmptyRepoOrFilename) {
    DownloadSpec spec;
    EXPECT_FALSE(download_parse_spec("/file.gguf", spec));
    EXPECT_FALSE(download_parse_spec("repo/", spec));
}

TEST(DownloadParseUri, HuggingFaceSchemePopulatesSpec) {
    DownloadSpec spec;
    std::string local;
    ASSERT_TRUE(download_parse_uri("hf://owner/repo/file.gguf", spec, local));
    EXPECT_EQ(spec.hub, ModelHub::HuggingFace);
    EXPECT_EQ(spec.repo, "owner/repo");
    EXPECT_EQ(spec.filename, "file.gguf");
    EXPECT_EQ(local, "file.gguf");
}

TEST(DownloadParseUri, ModelScopeSchemePopulatesSpec) {
    DownloadSpec spec;
    std::string local;
    ASSERT_TRUE(download_parse_uri("ms://owner/repo/file.gguf", spec, local));
    EXPECT_EQ(spec.hub, ModelHub::ModelScope);
    EXPECT_EQ(spec.repo, "owner/repo");
    EXPECT_EQ(spec.filename, "file.gguf");
    EXPECT_EQ(local, "file.gguf");
}

TEST(DownloadParseUri, AutoSchemePopulatesSpec) {
    DownloadSpec spec;
    std::string local;
    ASSERT_TRUE(download_parse_uri("auto://owner/repo/file.gguf", spec, local));
    EXPECT_EQ(spec.hub, ModelHub::Auto);
    EXPECT_EQ(spec.repo, "owner/repo");
    EXPECT_EQ(spec.filename, "file.gguf");
}

TEST(DownloadParseUri, UnknownSchemeReturnsFalse) {
    DownloadSpec spec;
    std::string local;
    EXPECT_FALSE(download_parse_uri("http://example.com/file", spec, local));
}

TEST(DownloadParseUri, LocalNameIsLeafOfFilename) {
    DownloadSpec spec;
    std::string local;
    ASSERT_TRUE(download_parse_uri("hf://owner/repo/sub/dir/model.gguf", spec, local));
    EXPECT_EQ(local, "model.gguf");
}

TEST(DownloadParseUri, ThrowsOnMalformedSpecAfterScheme) {
    DownloadSpec spec;
    std::string local;
    EXPECT_THROW(download_parse_uri("hf://no_slash", spec, local), std::runtime_error);
}

TEST(DownloadDefaultRevision, HuggingFaceIsMain) {
    EXPECT_EQ(download_default_revision(ModelHub::HuggingFace), "main");
}

TEST(DownloadDefaultRevision, ModelScopeIsMaster) {
    EXPECT_EQ(download_default_revision(ModelHub::ModelScope), "master");
}

TEST(DownloadDefaultRevision, AutoIsMain) {
    EXPECT_EQ(download_default_revision(ModelHub::Auto), "main");
}

TEST(DownloadResolveUrl, HuggingFaceDefaultRevision) {
    DownloadSpec spec;
    spec.repo = "owner/repo";
    spec.filename = "file.gguf";
    EXPECT_EQ(
        download_resolve_url(spec, ModelHub::HuggingFace),
        "https://huggingface.co/owner/repo/resolve/main/file.gguf");
}

TEST(DownloadResolveUrl, ModelScopeDefaultRevision) {
    DownloadSpec spec;
    spec.repo = "owner/repo";
    spec.filename = "file.gguf";
    EXPECT_EQ(
        download_resolve_url(spec, ModelHub::ModelScope),
        "https://www.modelscope.cn/models/owner/repo/resolve/master/file.gguf");
}

TEST(DownloadResolveUrl, HonorsCustomRevision) {
    DownloadSpec spec;
    spec.repo = "owner/repo";
    spec.filename = "file.gguf";
    spec.revision = "v1.2.3";
    EXPECT_EQ(
        download_resolve_url(spec, ModelHub::HuggingFace),
        "https://huggingface.co/owner/repo/resolve/v1.2.3/file.gguf");
}

TEST(DownloadHubName, ReturnsExpectedNames) {
    EXPECT_STREQ(download_hub_name(ModelHub::HuggingFace), "HuggingFace");
    EXPECT_STREQ(download_hub_name(ModelHub::ModelScope), "ModelScope");
    EXPECT_STREQ(download_hub_name(ModelHub::Auto), "Auto");
}

TEST(DownloadFileExists, TrueForRegularFile) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("qtrans_dl_exists_" + std::to_string(::getpid()));
    {
        std::ofstream out(path);
        out << "x";
    }
    EXPECT_TRUE(download_file_exists(path.string()));
    std::filesystem::remove(path);
}

TEST(DownloadFileExists, FalseForMissing) {
    EXPECT_FALSE(download_file_exists("/this/path/should/not/exist/xyzzy_42"));
}
