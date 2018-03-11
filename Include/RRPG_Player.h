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

inline const char* GetStringFromClass(CharacterClass cc)
{
	switch (cc)
	{
	case CharacterClass::Wizard:
		return "Wizard";
	case CharacterClass::Warrior:
		return "Warrior";
	case CharacterClass::Assassin:
		return "Assassin";
	default:
		return "Jobless";
	}
}


#pragma pack(push, 1)
struct Player
{
	std::string name = "";
	int health = 100;
	bool ready = false;
	CharacterClass job;
	bool dead = false;
};
#pragma pack(pop)
