#pragma once
#include "MessageIdentifiers.h"

enum RRPG_ID : unsigned char
{
	SERVER_PLAYER_CONNECTED = ID_USER_PACKET_ENUM,
	SERVER_GAME_STARTED,
	CLIENT_INTRO,
	CLIENT_READY,
	CLIENT_UNREADY
};