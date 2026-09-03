#include "domain/model-catalog/model_catalog.h"

#include <gtest/gtest.h>

TEST(ModelCatalog, CatalogIsNonEmpty) {
    EXPECT_FALSE(model_catalog().empty());
}

TEST(ModelCatalog, DefaultModelIsFirst) {
    const auto &catalog = model_catalog();
    const ModelCatalogEntry *def = default_model();
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->id, catalog.front().id);
    EXPECT_EQ(def->filename, catalog.front().filename);
}

TEST(ModelCatalog, FindByIdReturnsEntry) {
    const auto &catalog = model_catalog();
    ASSERT_FALSE(catalog.empty());
    const std::string id = catalog.front().id;
    const ModelCatalogEntry *entry = find_model_by_id(id);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, id);
}

TEST(ModelCatalog, FindByIdReturnsNullForUnknown) {
    EXPECT_EQ(find_model_by_id("this-id-does-not-exist"), nullptr);
}

TEST(ModelCatalog, FindByFilenameReturnsEntry) {
    const auto &catalog = model_catalog();
    ASSERT_FALSE(catalog.empty());
    const ModelCatalogEntry *entry = find_model_by_filename(catalog.front().filename);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->filename, catalog.front().filename);
}

TEST(ModelCatalog, FindByFilenameAcceptsFullPath) {
    const auto &catalog = model_catalog();
    ASSERT_FALSE(catalog.empty());
    const std::string full = "/some/where/" + catalog.front().filename;
    const ModelCatalogEntry *entry = find_model_by_filename(full);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->filename, catalog.front().filename);
}

TEST(ModelCatalog, FindByFilenameReturnsNullForUnknown) {
    EXPECT_EQ(find_model_by_filename("not-a-known-file.gguf"), nullptr);
}

TEST(ModelCatalog, AllEntriesHaveNonEmptyFields) {
    for (const ModelCatalogEntry &entry : model_catalog()) {
        EXPECT_FALSE(entry.id.empty()) << "id empty";
        EXPECT_FALSE(entry.display_name.empty()) << "display_name empty for " << entry.id;
        EXPECT_FALSE(entry.filename.empty()) << "filename empty for " << entry.id;
        EXPECT_FALSE(entry.remote_spec.empty()) << "remote_spec empty for " << entry.id;
        EXPECT_FALSE(entry.modelscope_remote_spec.empty())
            << "modelscope_remote_spec empty for " << entry.id;
        EXPECT_EQ(entry.sha256.size(), 64U) << "sha256 missing for " << entry.id;
        EXPECT_FALSE(entry.backend_priority.empty()) << "backend_priority empty for " << entry.id;
    }
}
