#pragma once

#include <string>
#include <unistd.h>
#include <iostream>
#include <sys/wait.h>
#include <regex>
#include <string>
#include <set>

class CgiHandler
{
public:

	CgiHandler();
	~CgiHandler();

	std::string executeCGI(std::string scriptPath, std::string queryString, std::string body, int method);
};