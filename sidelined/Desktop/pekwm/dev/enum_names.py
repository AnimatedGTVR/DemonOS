#!/usr/bin/env python3

import collections
import os
import re
import sys


class Enum:
    def __init__(self, name):
        self.name = name
        self.idx = len(re.findall('[A-Z]', name))
        self._values = collections.OrderedDict()

    @property
    def keys(self):
        return list(self._values.keys())

    def __getitem__(self, key):
        return self._values[key]

    def __setitem__(self, key, value):
        self._values[key] = value

    def __str__(self):
        return '%s(%s)' % (self.name, ",".join(self.values.values()))


def load_enums(fp):
    enums = []

    enum_re = re.compile(r'^enum\s+([^\s]+)\s+\{\s+// Map')
    value_re = re.compile(r'\s+([A-Z0-9_]+)[\s=,](.*//\s*)?(\w+)?')

    enum = None
    for line in fp:
        if enum is not None:
            if '}' in line:
                enums.append(enum)
                enum = None
            else:
                m = value_re.match(line)
                if m is not None:
                    value = m.group(1)
                    name = m.group(3)
                    if not name:
                        name = underscore_to_camel(value, enum.idx)
                    enum[name] = value
        else:
            m = enum_re.match(line)
            if m is not None:
                enum = Enum(m.group(1))

    return enums


def camel_to_underscore(name):
    def add_underscore(m):
        return '_%s' % (m.group(0).lower(), )
    name = '%s%s' % (name[0].lower(), name[1:])
    return re.sub('[A-Z]', add_underscore, name)


def underscore_to_camel(name, idx):
    def captialize(s):
        if not s:
            return s
        return '%s%s' % (s[0].upper(), s[1:])
    name = name.lower()
    return ''.join([captialize(s) for s in name.split('_')[idx:]])


def mk_string_to_map(enum, fp):
    array_name = camel_to_underscore(enum.name)
    fp.write('static Util::StringTo<%s> %s_map[] = {\n' % (
            enum.name, array_name))
    for name in sorted(enum.keys[:-1]):
        fp.write('\t{"%s", %s},\n' % (name, enum[name]))
    fp.write('\t{nullptr, %s},\n' % (enum[enum.keys[-1]], ))
    fp.write('};\n\n')


def mk_string_conv_fun(enum, fp):
    name = camel_to_underscore(enum.name)
    fp.write('%s\n' % (enum.name, ))
    fp.write('str_to_%s(const std::string &str)\n' % (name, ))
    fp.write('{\n')
    fp.write('\treturn Util::StringToGet(%s_map, str);\n' % (name, ))
    fp.write('}\n\n')
    fp.write('const char*\n')
    fp.write('%s_to_str(%s value)\n' % (name, enum.name))
    fp.write('{\n')
    fp.write('\treturn Util::StringToGetStr(%s_map, value);\n' % (name, ))
    fp.write('}\n\n')
    fp.write('void\n')
    fp.write('get_%s_names(std::vector<const char*> &names)\n' % (name, ))
    fp.write('{\n')
    fp.write('\tfor (int i = 0; %s_map[i].name != nullptr; i++) {\n' % (
        name, ))
    fp.write('\t\tnames.push_back(%s_map[i].name);\n' % (name, ))
    fp.write('\t}\n')
    fp.write('}\n\n')


def mk_string_conv_fun_hdr(enum, fp):
    name = camel_to_underscore(enum.name)
    fp.write('%s str_to_%s(const std::string &str);\n' % (enum.name, name))
    fp.write('const char *%s_to_str(%s value);\n' % (name, enum.name))
    fp.write('void get_%s_names(std::vector<const char*> &names);\n' % (
        name, ))


def mk_enum_name_to_names_fun(name, all_enums, fp):
    fp.write('bool\n')
    fp.write('%s_enum_name_to_names(const char *name, ' % (name, ))
    fp.write('std::vector<const char*> &names)\n')
    fp.write('{\n')
    for enum in all_enums:
        enum_name = camel_to_underscore(enum)
        fp.write('\tif (pekwm::ascii_ncase_equal(name, "%s")) {\n'
                 % (enum, ))
        fp.write('\t    get_%s_names(names);\n' % (enum_name))
        fp.write('\t    return true;\n')
        fp.write('\t}\n')
    fp.write('\treturn false;\n')
    fp.write('}\n\n')


def mk_enum_name_to_names_fun_hdr(name, fp):
    fp.write('bool %s_enum_name_to_names(const char *name, ' % (name, ))
    fp.write('std::vector<const char*> &names);\n')


def mk_guard(hh_file):
    hh_file = hh_file[:-3]
    hh_file = re.sub(r'([A-Z])', '_\\1', hh_file)
    return hh_file.upper()


def generate(name, cc_file, cc_fp, hh_file, hh_fp, paths):
    cc_fp.write('// NOTE: this file is auto-generated, do not edit\n')
    cc_fp.write('\n')
    cc_fp.write('#include "Util.hh"\n')
    cc_fp.write('#include "%s"\n' % (hh_file, ))
    cc_fp.write('\nnamespace pekwm {\n\n')

    guard = mk_guard(hh_file)

    hh_fp.write('// NOTE: this file is auto-generated, do not edit\n')
    hh_fp.write('\n')
    hh_fp.write('#ifndef _PEKWM%s_HH_\n' % (guard, ))
    hh_fp.write('#define _PEKWM%s_HH_\n' % (guard, ))
    hh_fp.write('\n')
    hh_fp.write('#include <string>\n')
    hh_fp.write('#include <vector>\n')
    hh_fp.write('\n')

    # include files before entering pekwm namespace
    for path in paths:
        with open(path) as fp:
            hh_fp.write('#include "%s"\n' % (os.path.basename(path), ))

    hh_fp.write('\nnamespace pekwm {\n\n')

    all_enums = []
    for path in paths:
        with open(path) as fp:
            enums = load_enums(fp)
            for enum in enums:
                all_enums.append(enum.name)
                mk_string_to_map(enum, cc_fp)
                mk_string_conv_fun(enum, cc_fp)
                mk_string_conv_fun_hdr(enum, hh_fp)

    mk_enum_name_to_names_fun(name, all_enums, cc_fp)
    mk_enum_name_to_names_fun_hdr(name, hh_fp)

    cc_fp.write('\n};\n')

    hh_fp.write('\n};\n')
    hh_fp.write('\n#endif // _PEKWM_%s_HH_\n' % (guard, ))


if __name__ == '__main__':
    if len(sys.argv) < 5:
        print('usage: %s name out.cc out.hh file1.hh [...]' % (sys.argv[0], ))
        sys.exit(1)

    name = sys.argv[1]
    cc_file = sys.argv[2]
    hh_file = sys.argv[3]

    with open(cc_file, 'w') as cc_fp:
        with open(hh_file, 'w') as hh_fp:
            generate(name,
                     os.path.basename(cc_file), cc_fp,
                     os.path.basename(hh_file), hh_fp,
                     sys.argv[3:])
