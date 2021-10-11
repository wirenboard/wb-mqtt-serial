#include "log.h"
#include <gtest/gtest.h>
#include <wblib/log.h>
#include <wblib/testing/testlog.h>
#include <wblib/utils.h>

using WBMQTT::Testing::TLoggedFixture;

void ParseCommadLine(int argc, char** argv)
{
    int debugLevel = 0;
    int c;

    while ((c = getopt(argc, argv, "d:")) != -1) {
        if (c == 'd') {
            debugLevel = std::atoi(optarg);
        }
    }

    switch (debugLevel) {
        case 1:
            Debug.SetEnabled(true);
            break;

        case 2:
            WBMQTT::Debug.SetEnabled(true);
            break;

        case 3:
            WBMQTT::Debug.SetEnabled(true);
            Debug.SetEnabled(true);
            break;

        default:
            break;
    }
}

int main(int argc, char** argv)
{
    WBMQTT::SetThreadName(argv[0]);
    TLoggedFixture::SetExecutableName(argv[0]);
    ::testing::InitGoogleTest(&argc, argv);
    ParseCommadLine(argc, argv);
    return RUN_ALL_TESTS();
}
