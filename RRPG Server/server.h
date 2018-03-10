#pragma once
#include "RRPG_Player.h"

#include "RakPeerInterface.h"
#include <string>
#include <mutex>
#include <unordered_map>

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
	void OnClientIntro(RakNet::Packet* p);
	void OnPlayerReady(RakNet::Packet* p);
	void OnPlayerUnready(RakNet::Packet* p);
	void OnPlayerListRequest(RakNet::Packet* p);
	void GameLoop();
	void StartGame();
	Player& GetPlayer(RakNet::RakNetGUID id);
	void BroadcastMessage(const char* input);

	bool IsRunning() const;

private:
	static Server* instance;

	RakNet::RakPeerInterface* rpi;
	std::mutex networkState_mutex;
	NetworkState networkState;
	unsigned int port;
	std::mutex totalPlayers_mutex;
	unsigned short totalConnections;
	static unsigned int EXPECTED_PLAYERS;
	std::mutex players_mutex;
	std::unordered_map<unsigned long, Player> players;
};

