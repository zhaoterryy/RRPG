#pragma once
#include "MessageIdentifiers.h"

enum RRPG_ID : unsigned char
{
	S_GAME_STARTED = ID_USER_PACKET_ENUM,
	S_REPLY_PLAYER_LIST_REQUEST,
	S_BROADCAST_CHAT,
	S_TAKE_TURN,
	S_UPDATE_GAME_STATE,
	S_UPDATE_PLAYER_HP,
	S_REPLY_PLAYER_STATS_REQUEST,
	C_INTRO,
	C_READY,
	C_UNREADY,
	C_PLAYER_LIST_REQUEST,
	C_PLAYER_STATS_REQUEST,
	C_CHAT,
	C_JOB_CHOSEN,
	C_ACTION_TAKEN
};

enum GameState : unsigned char
{
	GS_PENDING,
	GS_CHARACTER_SELECT,
	GS_MAIN
};

