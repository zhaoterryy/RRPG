#pragma once
#include <string>

enum class CharacterClass : unsigned char
{
	Wizard,
	Warrior,
	Assassin
};

enum class Action : unsigned char
{
	Heal,
	HealRng,
	Attack,
	AtkRng
};
#pragma pack(push, 1)
struct Player
{
	std::string name = "";
	unsigned int health = 100;
	bool ready = false;
	CharacterClass job;
};
#pragma pack(pop)
