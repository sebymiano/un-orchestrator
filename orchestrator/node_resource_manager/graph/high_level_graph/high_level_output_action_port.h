#ifndef HIGH_LEVEL_ACTION_OUTPUT_H_
#define HIGH_LEVEL_ACTION_OUTPUT_H_ 1

#include "high_level_output_action.h"
#include "../../../utils/logger.h"

#include <iostream>

using namespace std;

namespace highlevel
{

class ActionPort : public Action
{
private:
	string port;

	string input_port;

public:
	~ActionPort();
	ActionPort(string port, string input_port);
	string getInfo();
	string toString();

	bool operator==(const ActionPort &other) const;

	Object toJSON();
};

}

#endif //HIGH_LEVEL_ACTION_OUTPUT_H_
