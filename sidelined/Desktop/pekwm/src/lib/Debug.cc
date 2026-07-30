//
// Debug.cc for pekwm
// Copyright (C) 2021-2024 Claes Nästén <pekdon@gmail.com>
// Copyright (C) 2012 Andreas Schlick <ioerror@lavabit.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Debug.hh"
#include "LibNames.hh"
#include "Util.hh"

#include <cstdlib>
#include <ctime>

static DebugLevel _level = DEBUG_LEVEL_WARNING;
static bool _use_cerr = true;
std::ofstream _log("/dev/null");


/**
 * Output current timestamp to log stream.
 */
static void
addTimestamp(std::ostream &log)
{
	std::time_t t = std::time(nullptr);
	std::tm tm;
	localtime_r(&t, &tm);
	log << std::put_time(&tm, "%Y-%m-%d %H:%M:%S ");
}

namespace Debug
{
	/**
	 * Return true if current level includes level.
	 */
	bool
	isLevel(DebugLevel level)
	{
		return Debug::getLevel() >= level;
	}

	/**
	 * Get current log level.
	 */
	DebugLevel
	getLevel()
	{
		return _level;
	}

	/**
	 * Set log level.
	 */
	void
	setLevel(DebugLevel level)
	{
		_level = level;
	}

	std::ostream&
	getStream(const char* prefix)
	{
		if (_use_cerr) {
			addTimestamp(std::cerr);
			std::cerr << prefix;
			return std::cerr;
		} else {
			addTimestamp(_log);
			_log << prefix;
			return _log;
		}
	}

	std::ostream&
	getStream(const char* fun, int line, const char* prefix)
	{
		if (_use_cerr) {
			addTimestamp(std::cerr);
			std::cerr << fun << '@' << line << ":\n    " << prefix;
			return std::cerr;
		} else {
			addTimestamp(_log);
			_log << fun << '@' << line << ":\n    " << prefix;
			return _log;
		}
	}

	/**
	 * Set log file.
	 */
	bool
	setLogFile(const std::string& path)
	{
		_log.close();
		if (path == "-") {
			_use_cerr = true;
			return true;
		}
		_log.open(path.c_str(), std::ios_base::out|std::ios_base::app);
		_use_cerr = ! _log.good();
		return _log.good();
	}

	/**
	 * Debug Commands:
	 *
	 * logfile <filename> - set log file, use - for stderr.
	 * level [err|warn|info|debug|trace] - sets log level.
	 */
	void
	doAction(const std::string &cmd)
	{
		std::vector<std::string> args;
		if (Util::splitString(cmd, args, " \t") != 2) {
			return;
		}

		Util::to_lower(args[0]);
		if (args[0] == "logfile") {
			if (args[1] == "-") {
				_log.close();
				_use_cerr = true;
			} else {
				setLogFile(args[1]);
			}
		} else if (args[0] == "level") {
			_level = pekwm::str_to_debug_level(args[1]);
		}
	}
}
