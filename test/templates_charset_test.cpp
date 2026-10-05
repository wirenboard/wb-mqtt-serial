#include <algorithm>
#include <gtest/gtest.h>
#include <wblib/json_utils.h>
#include <wblib/testing/testlog.h>
#include <wblib/utils.h>

#include "file_utils.h"

using WBMQTT::Testing::TLoggedFixture;

namespace
{
    bool HasCyrillic(const std::string& str)
    {
        // UTF-8 lead bytes of U+0400..U+04FF
        return std::any_of(str.begin(), str.end(), [](char c) {
            auto b = static_cast<unsigned char>(c);
            return b >= 0xD0 && b <= 0xD3;
        });
    }

    class TCyrillicChecker
    {
        std::vector<std::string>& Errors;

        void Check(const std::string& path, const std::string& str)
        {
            if (HasCyrillic(str)) {
                Errors.push_back(path + ": \"" + str + "\"");
            }
        }

        void CheckKeys(const Json::Value& node, const std::string& path)
        {
            for (const auto& key: node.getMemberNames()) {
                Check(path + "/" + key + " (key)", key);
            }
        }

    public:
        TCyrillicChecker(std::vector<std::string>& errors): Errors(errors)
        {}

        void CheckNode(const Json::Value& node, const std::string& path)
        {
            if (node.isString()) {
                Check(path, node.asString());
            } else if (node.isArray()) {
                for (Json::ArrayIndex i = 0; i < node.size(); ++i) {
                    CheckNode(node[i], path + "[" + std::to_string(i) + "]");
                }
            } else if (node.isObject()) {
                CheckKeys(node, path);
                for (const auto& key: node.getMemberNames()) {
                    CheckNode(node[key], path + "/" + key);
                }
            }
        }

        void CheckTemplate(const Json::Value& root)
        {
            CheckKeys(root, "");
            for (const auto& key: root.getMemberNames()) {
                if (key != "device") {
                    CheckNode(root[key], "/" + key);
                }
            }
            const auto& device = root["device"];
            CheckKeys(device, "/device");
            for (const auto& key: device.getMemberNames()) {
                if (key == "name") {
                    continue;
                }
                if (key != "translations") {
                    CheckNode(device[key], "/device/" + key);
                    continue;
                }
                const auto& translations = device[key];
                CheckKeys(translations, "/device/translations");
                for (const auto& lang: translations.getMemberNames()) {
                    auto langPath = "/device/translations/" + lang;
                    if (lang == "en") {
                        CheckNode(translations[lang], langPath);
                    } else {
                        CheckKeys(translations[lang], langPath);
                    }
                }
            }
        }
    };

    void CheckTemplatesDir(const std::string& dir)
    {
        IterateDirByPattern(
            dir,
            ".json",
            [](const std::string& filePath) {
                if (!WBMQTT::StringHasSuffix(filePath, ".json")) {
                    return false;
                }
                std::vector<std::string> errors;
                TCyrillicChecker(errors).CheckTemplate(WBMQTT::JSON::Parse(filePath));
                for (const auto& error: errors) {
                    ADD_FAILURE() << filePath << ": " << error;
                }
                return false;
            },
            true);
    }
}

TEST(TTemplatesCharsetTest, CyrillicUsage)
{
    CheckTemplatesDir(TLoggedFixture::GetDataFilePath("../templates"));
    CheckTemplatesDir(TLoggedFixture::GetDataFilePath("../build/templates"));
}
