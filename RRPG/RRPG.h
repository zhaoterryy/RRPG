#pragma once
#include "RRPG_Player.h"
#include "RRPG_MessageIdentifiers.h"

#include "RakPeerInterface.h"
#include <string>
#include <mutex>
#include <vector>


class RRPG
{
public:
	RRPG();
	void Start();

	static RRPG& Get()
	{
		if (instance == nullptr)
			instance = new RRPG();

		return *instance;
	}

private:
	enum NetworkState
	{
		NS_INITIALIZATION,
		NS_CREATE_SOCKET,
		NS_PENDING_CONNECTION,
		NS_CONNECTED,
		NS_RUNNING,
		NS_LOBBY,
		NS_GAME_STARTED
	};

	void InputHandler();
	void PrintInstructions();
	bool HandleLobbyInput(std::string input);
	bool HandleGameInput(std::string input);

	void PacketHandler();
	bool IsLowLevelPacketHandled(RakNet::Packet* p);

	void OnConnectionAccepted(RakNet::Packet* p);
	void OnPlayersListReceived(RakNet::Packet* p);
	void OnPlayersStatsReceived(RakNet::Packet* p);
	void OnGameStart(RakNet::Packet* p);
	void OnMainGameStart(RakNet::Packet* p);
	void OnTakeTurn(RakNet::Packet* p);
	void OnGameStateUpdate(RakNet::Packet* p);

	void Ready();
	void Unready();
	void RequestPlayersFromServer();
	void RequestPlayerStatsFromServer();

	bool IsRunning() const;
	void GameLoop();

private:
	static RRPG* instance;

	RakNet::RakPeerInterface* rpi;
	std::mutex networkState_mutex;
	NetworkState networkState;
	GameState gameState;
	unsigned short totalPlayers;
	unsigned int clientPort;
	unsigned int serverPort;
	std::string serverAddress;
	std::mutex player_mutex;
	Player player;
	std::vector<Player> players;

	bool myTurn;
};