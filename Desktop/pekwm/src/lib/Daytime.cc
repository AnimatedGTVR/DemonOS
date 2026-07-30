//
// Daytime.cc for pekwm
// Copyright (C) 2025 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Calendar.hh"
#include "Daytime.hh"
#include "Debug.hh"
#include "String.hh"
#include "Types.hh"

extern "C" {
#include <math.h>
#include <time.h>
}

static const double SUNRISE = -0.833;
static const double CIVIL_TWILIGHT = -6.0;

static const double SECONDS_PER_DAY = 86400.0;
static const double JULIAN_DATE_EPOCH = 2440587.5;
static const double J2000 = 2451545.0;
static const double MEAN_ANOMALY_J2000_DEG = 357.5291;
static const double SUN_DAILY_MOTION_DEG = 0.98560028;
static const double OBLIQUITY = 23.44;

static double
_ts_to_j(double ts)
{
	return (ts / SECONDS_PER_DAY) + JULIAN_DATE_EPOCH;
}

static time_t
_j_to_ts(double julian)
{
	return static_cast<time_t>(
		(julian - JULIAN_DATE_EPOCH) * SECONDS_PER_DAY);
}

static double
_to_rad(double degrees)
{
	return degrees * (M_PI / 180.0);
}

static double
_to_deg(double radians)
{
	return radians * (180.0 / M_PI);
}

static time_t
_get_day_length(DayType type, time_t sun_rise, time_t sun_set)
{
	if (type == DAY_TYPE_REGULAR) {
		return sun_set - sun_rise;
	} else if (type == DAY_TYPE_POLAR_DAY) {
		return static_cast<time_t>(SECONDS_PER_DAY);
	} else {
		return 0;
	}
}

/**
 * Get string representation of TimeOfDay.
 */
const char*
time_of_day_to_string(enum TimeOfDay tod)
{
	switch (tod) {
	case TIME_OF_DAY_DAWN:
		return "dawn";
	case TIME_OF_DAY_DAY:
		return "day";
	case TIME_OF_DAY_DUSK:
		return "dusk";
	case TIME_OF_DAY_NIGHT:
	default:
		return "night";
	}
}

/**
 * Get TimeOfDay from string.
 */
bool
time_of_day_from_string(const std::string &str, enum TimeOfDay &tod)
{
	if (pekwm::ascii_ncase_equal(str, "DAWN")) {
		tod = TIME_OF_DAY_DAWN;
	} else if (pekwm::ascii_ncase_equal(str, "DAY")) {
		tod = TIME_OF_DAY_DAY;
	} else if (pekwm::ascii_ncase_equal(str, "DUSK")) {
		tod = TIME_OF_DAY_DUSK;
	} else if (pekwm::ascii_ncase_equal(str, "NIGHT")) {
		tod = TIME_OF_DAY_NIGHT;
	} else {
		return false;
	}
	return true;
}

/**
 * Default constructor, inits 0 daytime object
 */
Daytime::Daytime()
	: _valid(false),
	  _now(0),
	  _dawn(0),
	  _sun_rise(0),
	  _sun_set(0),
	  _night(0),
	  _day_length_s(0)
{
}

/**
 * Create daytime object for the provided day, location and elevation.
 */
Daytime::Daytime(time_t ts, double latitude, double longitude,
		 double elevation_m)
	: _valid(true),
	  _now(ts),
	  _dawn(0),
	  _sun_rise(0),
	  _sun_set(0),
	  _night(0),
	  _day_length_s(0)
{
	if (isnan(latitude) || latitude < -90.0 || latitude > 90.0
	    || isnan(longitude) || longitude < -180.0 || longitude > 180.0) {
		_valid = false;
		return;
	}

	Calendar cal = Calendar(ts).nextDay();
	double julian = _ts_to_j(cal.getTimestamp());
	_day_type = calculate(julian, latitude, longitude, elevation_m,
			      SUNRISE, _sun_rise, _sun_set);
	_day_length_s = _get_day_length(_day_type, _sun_rise, _sun_set);
	if (_day_type == DAY_TYPE_REGULAR) {
		calculate(julian, latitude, longitude, elevation_m,
			  CIVIL_TWILIGHT, _dawn, _night);
	} else {
		_dawn = 0;
		_night = 0;
	}

	P_TRACE("DayTime ts: " << ts << " dawn: " << _dawn
		<< " sun_rise: " << _sun_rise << " sun_set: " << _sun_set
		<< " night: " << _night);
}

static double
_normalize_deg(double deg)
{
	deg = fmod(deg, 360.0);
	if (deg < 0) {
		deg += 360.0;
	}
	return deg;
}

static double
_sqrt(double val)
{
	return sqrt(std::max(0.0, val));
}

static double
_cap(double val, double min, double max)
{
	if (val > max) {
		return max;
	}
	if (val < min) {
		return min;
	}
	return val;
}

DayType
Daytime::calculate(double julian, double latitude_deg,
		   double longitude_deg, double elevation_m,
		   double solar_altitude_deg, time_t& rise, time_t& set) const
{
	rise = 0;
	set = 0;

	const double lat = _to_rad(latitude_deg);
	const double sin_lat = sin(lat);
	const double cos_lat = cos(lat);

	// Elevation correction
	const double elevation_corr = -2.076 * _sqrt(elevation_m) / 60.0;

	// Effective solar altitude
	const double h0 = _to_rad(solar_altitude_deg + elevation_corr);

	// Mean solar time using days since J2000
	const double n = floor(julian - J2000 + 0.0008);
	const double jstar = n - longitude_deg / 360.0;

	// Solar mean anomaly
	const double M =
		_to_rad(_normalize_deg(MEAN_ANOMALY_J2000_DEG
				       + SUN_DAILY_MOTION_DEG * jstar));

	// Equation of center
	const double C =
	    1.9148 * sin(M) +
	    0.0200 * sin(2 * M) +
	    0.0003 * sin(3 * M);

	// Ecliptic longitude
	const double lambda =
	    _to_rad(_normalize_deg(_to_deg(M) + C + 180.0 + 102.9372));

	// Solar transit (noon)
	const double jtransit =
	    J2000 + jstar + 0.0053 * sin(M) - 0.0069 * sin(2 * lambda);

	// Declination of the sun
	const double sin_delta = sin(lambda) * sin(_to_rad(OBLIQUITY));
	const double cos_delta = _sqrt(1.0 - sin_delta * sin_delta);

	// Hour angle
	double denom = cos_lat * cos_delta;
	if (fabs(denom) < 1e-12) {
		if (sin(h0) > sin_lat * sin_delta) {
			return DAY_TYPE_POLAR_NIGHT;
		}
		return DAY_TYPE_POLAR_DAY;
	}
	double cos_omega = (sin(h0) - sin_lat * sin_delta) / denom;
	if (cos_omega < -1.0) {
		return DAY_TYPE_POLAR_DAY;
	} else if (cos_omega > 1.0) {
		return DAY_TYPE_POLAR_NIGHT;
	}

	// Hour angle
	const double omega = acos(_cap(cos_omega, -1.0, 1.0));

	rise = _j_to_ts(jtransit - omega / (2 * M_PI));
	set  = _j_to_ts(jtransit + omega / (2 * M_PI));

	return DAY_TYPE_REGULAR;
}

Daytime::~Daytime()
{
}

Daytime&
Daytime::operator=(const Daytime &rhs)
{
	_now = rhs._now;
	_valid = rhs.isValid();
	_day_type = rhs.getDayType();
	_dawn = rhs.getDawn();
	_sun_rise = rhs.getSunRise();
	_sun_set = rhs.getSunSet();
	_night = rhs.getNight();
	_day_length_s = rhs.getDayLengthS();
	return *this;
}

/**
 * Get time of day for the given timestamp, if ts is outside of the range of
 * the given day Daytime was created for, TIME_OF_DAY_NIGHT is returned.
 */
enum TimeOfDay
Daytime::getTimeOfDay(time_t ts)
{
	if (ts == 0) {
		ts = _now;
	}
	if (ts < _dawn) {
		return TIME_OF_DAY_NIGHT;
	} else if (ts < _sun_rise) {
		return TIME_OF_DAY_DAWN;
	} else if (ts < _sun_set) {
		return TIME_OF_DAY_DAY;
	} else if (ts < _night) {
		return TIME_OF_DAY_DUSK;
	} else if (_day_type == DAY_TYPE_POLAR_DAY) {
		return TIME_OF_DAY_DAY;
	} else {
		return TIME_OF_DAY_NIGHT;
	}
}

/**
 * Get time of day when the current time of day state ends.
 */
time_t
Daytime::getTimeOfDayEnd(time_t ts)
{
	if (ts == 0) {
		ts = _now;
	}
	if (!_valid || _sun_rise == 0 || _sun_set == 0) {
		// calculation failed or the sun won't set or rise, use
		// calendar to get the next date
		Calendar now(_now);
		Calendar next_day = now.nextDay();
		return next_day.getTimestamp();
	} else  if (ts > _sun_set) {
		// next day (without change in calculation)
		return _sun_rise + int(SECONDS_PER_DAY);
	} else if (ts > _sun_rise) {
		return _sun_set;
	}
	return _sun_rise;
}
