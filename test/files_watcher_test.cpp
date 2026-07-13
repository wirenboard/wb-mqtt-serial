#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <wblib/testing/testlog.h>

#include "files_watcher.h"
#include "templates_map.h"
#include "test_utils.h"

namespace
{
    const std::string MOVED_TYPE = "MovedDevice";
}

class TFilesWatcherTest: public testing::Test
{
protected:
    std::filesystem::path BaseDir;
    std::filesystem::path WatchedDir; // acts as the user templates directory
    std::filesystem::path OutsideDir;

    std::mutex Mutex;
    std::condition_variable EventsCv;
    std::vector<std::pair<std::string, TFilesWatcher::TEvent>> Events;

    PTemplateMap Templates;
    std::unique_ptr<TFilesWatcher> Watcher;

    void SetUp() override
    {
        BaseDir = std::filesystem::temp_directory_path() / "wb-mqtt-serial-files-watcher-test" /
                  ::testing::UnitTest::GetInstance()->current_test_info()->name();
        WatchedDir = BaseDir / "watched";
        OutsideDir = BaseDir / "outside";
        std::filesystem::remove_all(BaseDir);
        std::filesystem::create_directories(WatchedDir);
        std::filesystem::create_directories(OutsideDir);

        Templates = std::make_shared<TTemplateMap>(GetTemplatesSchema(), WatchedDir.string());
        Templates->AddTemplatesDir(WatchedDir.string());

        Watcher = std::make_unique<TFilesWatcher>(
            std::vector<std::string>{WatchedDir.string()},
            [this](std::string fileName, TFilesWatcher::TEvent event) { HandleEvent(fileName, event); });
        WaitWatcherReady();
    }

    void TearDown() override
    {
        Watcher.reset();
        std::filesystem::remove_all(BaseDir);
    }

    //! Mirrors HandleTemplateChangeEvent from main.cpp and records the event
    void HandleEvent(const std::string& fileName, TFilesWatcher::TEvent event)
    {
        if (event == TFilesWatcher::TEvent::CloseWrite) {
            try {
                Templates->UpdateTemplate(fileName);
            } catch (const std::exception&) {
            }
        }
        if (event == TFilesWatcher::TEvent::Delete) {
            Templates->DeleteTemplate(fileName);
        }
        std::unique_lock lock(Mutex);
        Events.emplace_back(fileName, event);
        EventsCv.notify_all();
    }

    bool WaitForEvent(const std::string& fileName,
                      TFilesWatcher::TEvent event,
                      std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::unique_lock lock(Mutex);
        return EventsCv.wait_for(lock, timeout, [&] {
            return std::find(Events.begin(), Events.end(), std::make_pair(fileName, event)) != Events.end();
        });
    }

    //! The inotify watch is installed asynchronously by the watcher thread.
    //! Write probe files until one of them produces an event.
    void WaitWatcherReady()
    {
        auto probePath = (WatchedDir / "probe.txt").string();
        for (size_t i = 0; i < 100; ++i) {
            {
                std::ofstream probe(probePath);
                probe << i;
            }
            if (WaitForEvent(probePath, TFilesWatcher::TEvent::CloseWrite, std::chrono::milliseconds(200))) {
                break;
            }
        }
        std::filesystem::remove(probePath);
    }

    void WriteTemplateFile(const std::filesystem::path& path, const std::string& deviceType, const std::string& title)
    {
        Json::Value root;
        root["device_type"] = deviceType;
        root["title"] = title;
        root["device"]["name"] = title;
        root["device"]["id"] = "moved-device";
        Json::Value channel;
        channel["name"] = "Input 1";
        channel["reg_type"] = "input";
        channel["address"] = 0;
        root["device"]["channels"].append(channel);
        Json::StreamWriterBuilder builder;
        std::ofstream file(path);
        file << Json::writeString(builder, root);
    }
};

TEST_F(TFilesWatcherTest, MoveIntoWatchedDir)
{
    auto srcPath = OutsideDir / "moved-device.json";
    auto dstPath = WatchedDir / "moved-device.json";
    WriteTemplateFile(srcPath, MOVED_TYPE, "Moved device");
    std::filesystem::rename(srcPath, dstPath);

    EXPECT_TRUE(WaitForEvent(dstPath.string(), TFilesWatcher::TEvent::CloseWrite));
    auto deviceTemplate = Templates->GetTemplate(MOVED_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    EXPECT_EQ(dstPath.string(), deviceTemplate->GetFilePath());
}

TEST_F(TFilesWatcherTest, MoveOutOfWatchedDir)
{
    auto path = WatchedDir / "moved-device.json";
    WriteTemplateFile(path, MOVED_TYPE, "Moved device");
    ASSERT_TRUE(WaitForEvent(path.string(), TFilesWatcher::TEvent::CloseWrite));

    std::filesystem::rename(path, OutsideDir / "moved-device.json");

    EXPECT_TRUE(WaitForEvent(path.string(), TFilesWatcher::TEvent::Delete));
    EXPECT_THROW(Templates->GetTemplate(MOVED_TYPE), std::out_of_range);
}

TEST_F(TFilesWatcherTest, DeleteFromWatchedDir)
{
    auto path = WatchedDir / "deleted-device.json";
    WriteTemplateFile(path, MOVED_TYPE, "Deleted device");
    ASSERT_TRUE(WaitForEvent(path.string(), TFilesWatcher::TEvent::CloseWrite));
    ASSERT_NO_THROW(Templates->GetTemplate(MOVED_TYPE));

    std::filesystem::remove(path);

    EXPECT_TRUE(WaitForEvent(path.string(), TFilesWatcher::TEvent::Delete));
    EXPECT_THROW(Templates->GetTemplate(MOVED_TYPE), std::out_of_range);
}

TEST_F(TFilesWatcherTest, RenameWithinWatchedDir)
{
    auto oldPath = WatchedDir / "old-name.json";
    auto newPath = WatchedDir / "new-name.json";
    WriteTemplateFile(oldPath, MOVED_TYPE, "Moved device");
    ASSERT_TRUE(WaitForEvent(oldPath.string(), TFilesWatcher::TEvent::CloseWrite));

    std::filesystem::rename(oldPath, newPath);

    EXPECT_TRUE(WaitForEvent(oldPath.string(), TFilesWatcher::TEvent::Delete));
    EXPECT_TRUE(WaitForEvent(newPath.string(), TFilesWatcher::TEvent::CloseWrite));
    auto deviceTemplate = Templates->GetTemplate(MOVED_TYPE);
    EXPECT_TRUE(deviceTemplate->IsUserDefined());
    EXPECT_EQ(newPath.string(), deviceTemplate->GetFilePath());
}
