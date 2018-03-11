#pragma once
#include "RRPG_Player.h"
#include "RRPG_MessageIdentifiers.h"

#include "RakPeerInterface.h"
#include <string>
#include <mutex>
#include <map>

class Server
{
public:
	Server();
	void Start();

	static Server& Get()
	{
		if (instance == nullptr)
			instance = new Server();

		return *instance;
	}

private:
	enum NetworkState
	{
		NS_INITIALIZATION,
		NS_CREATE_SOCKET,
		NS_LOBBY,
		NS_GAME_STARTED
	};

	void PacketHandler();
	void InputHandler();
	bool IsLowLevelPacketHandled(RakNet::Packet* p);

	void OnIncomingConnection(RakNet::Packet* p);
	// OnConnectionAccepted ->
	void OnClientIntro(RakNet::Packet* p);
	void OnClientChatReceived(RakNet::Packet* p);
	void OnPlayerReady(RakNet::Packet* p);
	void OnPlayerUnready(RakNet::Packet* p);
	// RequestPlayersFromServer ->
	void OnPlayerListRequest(RakNet::Packet* p);
	void OnPlayerJobChosen(RakNet::Packet* p);
	// RequestPlayerStatsFromServer ->
	void OnPlayerStatsRequest(RakNet::Packet* p);
	void OnPlayerActionTaken(RakNet::Packet* p);

	// -> OnTakeTurn
	void NextTurn();
	// -> OnPlayersHealthUpdated 
	void ModifyHealth(Player& player, int diff);

	void GameLoop();
	void StartGame();
	void StartMainGame();
	void GameOver(unsigned long winnerId);
	Player& GetPlayer(RakNet::RakNetGUID id);
	Player& GetPlayer(unsigned long id);
	Player* GetPlayerWithName(const char* name);
	RakNet::SystemAddress GetAddressFromID(unsigned long id);
	void BroadcastMessage(const char* input);

	bool IsRunning() const;

private:
	static Server* instance;

	RakNet::RakPeerInterface* rpi;
	std::mutex networkState_mutex;
	NetworkState networkState;
	GameState gameState;
	unsigned int port;
	std::mutex totalPlayers_mutex;
	unsigned short totalConnections;
	static unsigned int EXPECTED_PLAYERS;
	std::mutex players_mutex;
	std::map<unsigned long, Player> players;
	std::map<unsigned long, RakNet::SystemAddress> playerAddresses;
	unsigned long currentPlayerTurn;
	bool isQuitting;
};

