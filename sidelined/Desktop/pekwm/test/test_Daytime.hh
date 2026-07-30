//
// test_Daytime.cc for pekwm
// Copyright (C) 2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "test.hh"
#include "Daytime.hh"

extern "C" {
#include <math.h>
}

class TestDaytime : public TestSuite {
public:
	TestDaytime();
	~TestDaytime();

	virtual bool run_test(TestSpec spec, bool status);

private:
	static void testIsValid();
	static void testDaytime();
	static void testGetTimeOfDay();
};

TestDaytime::TestDaytime()
	: TestSuite("Daytime")
{
}

TestDaytime::~TestDaytime()
{
}

bool
TestDaytime::run_test(TestSpec spec, bool status)
{
	TEST_FN(spec, "isValid", testIsValid());
	TEST_FN(spec, "daytime", testDaytime());
	TEST_FN(spec, "getTimeOfDay", testGetTimeOfDay());
	return status;
}

void
TestDaytime::testIsValid()
{
	time_t now = time(NULL);
	ASSERT_TRUE("valid", Daytime(now, 0, 0.0, 0.0).isValid());

	ASSERT_FALSE("latitude < -90.0",
		     Daytime(now, -91.0, 0.0, 0.0).isValid());
	ASSERT_FALSE("latitude > 90.0",
		     Daytime(now, 91.0, 0.0, 0.0).isValid());
	ASSERT_FALSE("latitude NAN",
		     Daytime(now, NAN, 0.0, 0.0).isValid());
	ASSERT_FALSE("longitude < -180.0",
		     Daytime(now, -181.0, 0.0, 0.0).isValid());
	ASSERT_FALSE("longitude > 180.0",
		     Daytime(now, 181.0, 0.0, 0.0).isValid());
	ASSERT_FALSE("longitude NAN",
		     Daytime(now, NAN, 0.0, 0.0).isValid());

}

void
TestDaytime::testDaytime()
{
	// 2025-01-11 09:33:48 (rise 08:34:44, set 15:59:08)
	Daytime dt1(1736588028, 56.0449, 12.692);
	ASSERT_TRUE("sunrise before time", 1736588028 > dt1.getSunRise());
	ASSERT_EQUAL("daytime (56.0449, 12.692)", 1736580884,
		     dt1.getSunRise());
	ASSERT_TRUE("sunset after time", 1736588028 < dt1.getSunSet());
	ASSERT_EQUAL("daytime (56.0449, 12.692)", 1736607548,
		     dt1.getSunSet());

	// 2025-03-23T13:53:40 (rise 05:33:52 set 18:08:21)
	Daytime dt2(1742734420, 65.5191, 18.8572);
	ASSERT_TRUE("sunrise before time", 1742734420 > dt2.getSunRise());
	ASSERT_EQUAL("daytime (64.5191, 18.8572)", 1742704432,
		     dt2.getSunRise());
	ASSERT_TRUE("sunset after time", 1742734420 < dt2.getSunSet());
	ASSERT_EQUAL("daytime (64.5191, 18.8572)", 1742749701,
		     dt2.getSunSet());

	// 2024-12-31T00:00:00 (rise 09:04:09 set 12:31:27)
	for (time_t t = 1735603200; t < 1735689600; t += 10800) {
		Daytime dt3(t, 65.5191, 18.8572);
		ASSERT_EQUAL("daytime (64.5191, 18.8572)", 1735635771,
			     dt3.getSunRise());
		ASSERT_EQUAL("daytime (64.5191, 18.8572)", 1735648329,
			     dt3.getSunSet());
	}

	// 1993-01-01 (rise 10:01:02 set 13:35:11)
	Daytime dt4(725846400, 65.5191, 18.8572);
	ASSERT_EQUAL("daytime 1993-01-01 (65.5191, 18.8572)", 725878862,
		     dt4.getSunRise());
	ASSERT_EQUAL("daytime 1993-01-01 (65.5191, 18.8572)", 725891711,
		     dt4.getSunSet());

	// 1994-12-20, Honningsvåg
	Daytime dt5(787878061, 70.978611, 25.976667);
	ASSERT_EQUAL("no day", DAY_TYPE_POLAR_NIGHT, dt5.getDayType());
	ASSERT_EQUAL("no day", 0, dt5.getSunRise());
	ASSERT_EQUAL("no day", 0, dt5.getSunSet());
	ASSERT_EQUAL("no day", 0, dt5.getDayLengthS());

	// 2025-06-30, Honningsvåg
	Daytime dt6(1751234400, 70.978611, 25.976667);
	ASSERT_EQUAL("no night", DAY_TYPE_POLAR_DAY, dt6.getDayType());
	ASSERT_EQUAL("no night", dt6.getDawn(), dt6.getSunRise());
	ASSERT_EQUAL("no night", dt6.getNight(), dt6.getSunSet());
	ASSERT_EQUAL("no night", 86400, dt6.getDayLengthS());
}

void
TestDaytime::testGetTimeOfDay()
{
	Daytime dt(1736588028, 56.0449, 12.692);
	ASSERT_EQUAL("dawn (before)", TIME_OF_DAY_DAWN,
		     dt.getTimeOfDay(1736578158));
	ASSERT_EQUAL("dusk (after)", TIME_OF_DAY_DUSK,
		     dt.getTimeOfDay(1736607627));
	ASSERT_EQUAL("day", TIME_OF_DAY_DAY,
		     dt.getTimeOfDay(1736580961));
}
