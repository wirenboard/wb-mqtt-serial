#include <gtest/gtest.h>
#include "testlog.h"

int main(int argc, char **argv)
{
    TLoggedFixture::SetExecutableName(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
