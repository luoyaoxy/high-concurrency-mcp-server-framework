#include <gtest/gtest.h>

TEST(ExampleTest, BasicAssertions) {
    EXPECT_EQ(7 * 6, 42);
    EXPECT_TRUE(true);
    EXPECT_STRNE("hello", "world");
}

TEST(ExampleTest, StringOperations) {
    std::string str = "Hello Google Test";
    EXPECT_EQ(str.size(), 17);
    EXPECT_EQ(str.substr(0, 5), "Hello");
    EXPECT_NE(str.find("Google"), std::string::npos);
}