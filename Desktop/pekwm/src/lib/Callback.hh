//
// Callback.hh for pekwm
// Copyright (C) 2026 Claes Nästen <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifndef _PEKWM_CALLBACK_HH_
#define _PEKWM_CALLBACK_HH_

#include <string>
#include <vector>

class Callback {
public:
	virtual ~Callback() { }
};

template<typename T>
class CallbackValue : public Callback {
public:
	CallbackValue() { }
	virtual ~CallbackValue() { }

	virtual void callback(T value) = 0;
};

typedef std::vector<std::string> callback_string_vector;
typedef CallbackValue<callback_string_vector> CallbackStringVector;

#endif // _PEKWM_CALLBACK_HH_
