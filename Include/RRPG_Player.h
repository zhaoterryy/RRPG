#pragma once
#include <string>

#pragma pack(push, 1)
struct Player
{
	std::string name = "";
	unsigned int health = 100;
	bool ready = false;
};
#pragma pack(pop)
