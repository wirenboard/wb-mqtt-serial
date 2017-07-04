#include <gtest/gtest.h>

#include "serial_config.h"

class TDeviceTemplateFileExtensionTest: public ::testing::Test {
protected:
	void VerifyTemplates(const std::string& directory, const std::string& bad_device_type);
};

void TDeviceTemplateFileExtensionTest::VerifyTemplates(const std::string& directory, const std::string& bad_device_type)
{
	try {
		TConfigTemplateParser device_parser(directory, true);
		auto templates = device_parser.Parse();
		ASSERT_FALSE(templates->count(bad_device_type));
	} catch (const TConfigParserException& e) {
		ADD_FAILURE() << "Parsing failed: " << e.what();
	}
}

TEST_F(TDeviceTemplateFileExtensionTest, WrongExtension)
{
	VerifyTemplates("./test/device-templates", "MSU34_BAD");
}
