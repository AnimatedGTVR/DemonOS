//
// pekwm_cfg.cc for pekwm
// Copyright (C) 2021-2026 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Compat.hh"
#include "CfgParser.hh"
#include "Util.hh"

#include <map>
#include <vector>
#include <string>

extern "C" {
#include <getopt.h>
}

enum CfgAction {
	PEKWM_CFG_ACTION_GET,
	PEKWM_CFG_ACTION_JSON,
	PEKWM_CFG_ACTION_NO
};

static void
parseCfg(CfgParser &cfg, const std::string& path,
	 const std::map<std::string, std::string> &cfg_env)
{
	std::map<std::string, std::string>::const_iterator it =
		cfg_env.begin();
	for (; it != cfg_env.end(); ++it) {
		cfg.setVar(it->first, it->second);
	}
	cfg.parse(path);
}

static bool
cfgGet(CfgParser::Entry *entry, const std::string &cfg_spec, std::ostream &out)
{
	std::vector<std::string> toks;
	Util::splitString(cfg_spec, toks, ".");
	std::vector<std::string>::iterator it(toks.begin());
	std::vector<std::string>::iterator last = toks.end() - 1;
	for (; entry != nullptr && it != toks.end(); ++it) {
		if (it == last) {
			CfgParser::Entry *value = entry->findEntry(*it);
			if (value != nullptr) {
				out << value->getValue();
				return true;
			}
		} else {
			entry = entry->findSection(*it);
		}
	}
	return false;
}

static void
jsonDumpSection(CfgParser::Entry *entry, std::ostream &out)
{
	// map keeping track of seen section names to ensure unique names
	// in the output.
	std::map<std::string, int> sections;

	CfgParser::Entry::entry_cit it = entry->begin();
	CfgParser::Entry::entry_cit last = entry->end() - 1;
	for (; it != entry->end(); ++it) {
		if ((*it)->getSection()) {
			std::string name = (*it)->getName();
			if (! (*it)->getValue().empty()) {
				name += "-" + (*it)->getValue();
			}

			std::map<std::string, int>::iterator s_it =
				sections.find(name);
			if (s_it == sections.end()) {
				sections[name] = 0;
			} else {
				name += "-" + std::to_string(s_it->second);
				s_it->second = s_it->second + 1;
			}

			out << "\"" << name << "\": {" << std::endl;
			jsonDumpSection((*it)->getSection(), out);
			out << "}";
		} else {
			out << "\"" << (*it)->getName() << "\"";
			out << ": \"" << (*it)->getValue()
				  << "\"";
		}

		if (it != last) {
			out << ",";
		}
		out << std::endl;
	}
}

static void
jsonDump(CfgParser::Entry *root, std::ostream &out)
{
	out << "{" << std::endl;
	jsonDumpSection(root, out);
	out << "}" << std::endl;
}

#ifndef UNITTEST

static void
usage(const char* name, int ret)
{
	std::cout << "usage: " << name << " [-j]" << std::endl;
	std::cout << "  -f --file         File to operate on, default is "
		  << "$PEKWM_CONFIG_FILE" << std::endl;
	std::cout << "  -g --get Var.Name Get value from configuration"
		  << std::endl;
	std::cout << "  -j --json         Dump file as JSON" << std::endl;
	exit(ret);
}


int
main(int argc, char* argv[])
{
	// Limit access, this will not allow execution of commands in
	// configuration files.
	pledge_x("stdio rpath", "");

	CfgAction action = PEKWM_CFG_ACTION_NO;
	std::string cfg_path;
	std::string cfg_spec;
	std::map<std::string, std::string> cfg_env;

	static struct option opts[] = {
		{const_cast<char*>("get"), no_argument, nullptr, 'g'},
		{const_cast<char*>("file"), no_argument, nullptr, 'f'},
		{const_cast<char*>("json"), no_argument, nullptr, 'j'},
		{const_cast<char*>("env"), required_argument, nullptr, 'e'},
		{const_cast<char*>("help"), no_argument, nullptr, 'h'},
		{nullptr, 0, nullptr, 0}
	};

	int ch;
	while ((ch = getopt_long(argc, argv, "e:f:g:jh", opts, nullptr)) != -1) {
		switch (ch) {
		case 'e': {
			std::vector<std::string> vals;
			if (Util::splitString(optarg, vals, "=", 2) == 2) {
				cfg_env["$" + vals[0]] = vals[1];
			} else {
				usage(argv[0], 1);
			}
		}
		case 'g':
			action = PEKWM_CFG_ACTION_GET;
			cfg_spec = optarg;
			break;
		case 'j':
			action = PEKWM_CFG_ACTION_JSON;
			break;
		case 'h':
			usage(argv[0], 0);
			break;
		default:
			usage(argv[0], 1);
			break;
		}
	}

	if (cfg_path.empty()) {
		cfg_path = Util::getEnv("PEKWM_CONFIG_FILE");
		if (cfg_path.empty()) {
			std::cerr << "-f/--file not specified and "
				  << "$PEKWM_CONFIG_FILE is not set"
				  << std::endl;
			exit(1);
		}
	}

	int ret = 0;
	CfgParser cfg(CfgParserOpt(""));
	switch (action) {
	case PEKWM_CFG_ACTION_GET:
		parseCfg(cfg, cfg_path, cfg_env);
		ret = cfgGet(cfg.getEntryRoot(), cfg_spec, std::cout) ? 0 : 1;
		break;
	case PEKWM_CFG_ACTION_JSON:
		parseCfg(cfg, cfg_path, cfg_env);
		jsonDump(cfg.getEntryRoot(), std::cout);
		break;
	default:
		usage(argv[0], 1);
		break;
	}


	return ret;
}

#endif // UNITTEST
