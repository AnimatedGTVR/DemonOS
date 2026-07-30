//
// test_pekwm_cfg.cc for pekwm
// Copyright (C) 2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Compat.hh"
#include "Json.hh"

#include "test.hh"

#include <sstream>

#define UNITTEST
#include "cfg/pekwm_cfg.cc"

class TestPekwmCfg : public TestSuite {
public:
	TestPekwmCfg();
	virtual ~TestPekwmCfg();

	virtual bool run_test(TestSpec spec, bool status);

	static void testCfgGet();
	static void testJsonDump();
};

TestPekwmCfg::TestPekwmCfg()
	: TestSuite("pekwm_cfg")
{
}

TestPekwmCfg::~TestPekwmCfg(void)
{
}

bool
TestPekwmCfg::run_test(TestSpec spec, bool status)
{
	TEST_FN(spec, "cfgGet", testCfgGet());
	TEST_FN(spec, "jsonDump", testJsonDump());
	return status;
}

void
TestPekwmCfg::testCfgGet()
{
	CfgParser cfg(CfgParserOpt(""));
	std::map<std::string, std::string> cfg_env;
	parseCfg(cfg, "data/config.pekwm_sys", cfg_env);

	bool found;
	std::ostringstream value;
	found = cfgGet(cfg.getEntryRoot(), "Sys.LocationCommands.Location1",
		       value);
	ASSERT_TRUE("failed to get value", found);
	ASSERT_EQUAL("value mismatch", "location1", value.str());
}

void
TestPekwmCfg::testJsonDump()
{
	CfgParser cfg(CfgParserOpt(""));
	std::map<std::string, std::string> cfg_env;
	parseCfg(cfg, "data/config.pekwm_sys", cfg_env);
	std::ostringstream value;
	CfgParser::Entry *entry  =
		cfg.getEntryRoot()->findSection("Sys")
			->findSection("DaytimeCommands");
	jsonDump(entry, value);
	std::string expected = "{\n" \
		"\"Daytime1\": \"daytime1\",\n" \
		"\"Daytime2\": \"daytime2\"\n" \
		"}\n";
	ASSERT_EQUAL("output mismatch", expected, value.str());


	std::ostringstream value_all;
	jsonDump(cfg.getEntryRoot(), value_all);
	JsonParser parser(value_all.str());
	JsonValueObject *obj = parser.parse();
	ASSERT_FALSE("invalid json produced", parser.isError());
	ASSERT_TRUE("invalid json produced", obj != nullptr);
}

int
main(int argc, char *argv[])
{
	TestPekwmCfg testPekwmCfg;
	return TestSuite::main(argc, argv);
}
