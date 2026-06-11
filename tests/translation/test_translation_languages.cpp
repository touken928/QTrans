#include "catalog/language_list.h"

#include <gtest/gtest.h>

TEST(TranslationLanguages, CountMatchesArray) {
    int count = translation_language_count();
    EXPECT_GT(count, 0);
    const TranslationLanguage *arr = translation_languages();
    ASSERT_NE(arr, nullptr);
    // Linear walk to validate count consistency.
    int walked = 0;
    while (arr[walked].id != nullptr) {
        ++walked;
    }
    EXPECT_EQ(walked, count);
}

TEST(TranslationLanguages, FindByModelNameExact) {
    const TranslationLanguage *zh = find_translation_language_by_model_name("Chinese");
    ASSERT_NE(zh, nullptr);
    EXPECT_STREQ(zh->id, "zh");
    EXPECT_STREQ(zh->chinese_name, "中文");
}

TEST(TranslationLanguages, FindByModelNameCaseInsensitive) {
    const TranslationLanguage *zh = find_translation_language_by_model_name("CHINESE");
    ASSERT_NE(zh, nullptr);
    EXPECT_STREQ(zh->id, "zh");

    const TranslationLanguage *en = find_translation_language_by_model_name("english");
    ASSERT_NE(en, nullptr);
    EXPECT_STREQ(en->id, "en");
}

TEST(TranslationLanguages, FindByModelNameUnknownReturnsNull) {
    EXPECT_EQ(find_translation_language_by_model_name("Klingon"), nullptr);
    EXPECT_EQ(find_translation_language_by_model_name(""), nullptr);
}

TEST(TranslationLanguages, ChineseNameReturnsKnownName) {
    EXPECT_EQ(translation_chinese_name("Chinese"), "中文");
    EXPECT_EQ(translation_chinese_name("Japanese"), "日语");
    EXPECT_EQ(translation_chinese_name("ENGLISH"), "英语");
}

TEST(TranslationLanguages, ChineseNameReturnsInputForUnknown) {
    EXPECT_EQ(translation_chinese_name("Klingon"), "Klingon");
    EXPECT_EQ(translation_chinese_name(""), "");
}

TEST(IsChineseLanguageName, RecognizesChineseVariants) {
    EXPECT_TRUE(is_chinese_language_name("Chinese"));
    EXPECT_TRUE(is_chinese_language_name("中文"));
    EXPECT_TRUE(is_chinese_language_name("繁体中文"));
    EXPECT_TRUE(is_chinese_language_name("Traditional Chinese"));
    EXPECT_TRUE(is_chinese_language_name("zh"));
    EXPECT_TRUE(is_chinese_language_name("zh-Hant"));
    EXPECT_TRUE(is_chinese_language_name("CHINESE"));
}

TEST(IsChineseLanguageName, RejectsOtherLanguages) {
    EXPECT_FALSE(is_chinese_language_name("English"));
    EXPECT_FALSE(is_chinese_language_name("日本語"));
    EXPECT_FALSE(is_chinese_language_name(""));
    EXPECT_FALSE(is_chinese_language_name("Klingon"));
}
