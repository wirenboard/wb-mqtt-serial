#include <gtest/gtest.h>
#include "testlog.h"
#include "global_variables.h"

int main(int argc, char **argv)
{
    Global::Debug = true;
    TLoggedFixture::SetExecutableName(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
