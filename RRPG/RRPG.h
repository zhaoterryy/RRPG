#pragma once
#include "RRPG_Player.h"

#include "RakPeerInterface.h"
#include <string>
#include <mutex>


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
		NS_LOBBY
	};

	void InputHandler();
	void PacketHandler();
	bool IsLowLevelPacketHandled(RakNet::Packet* packet);
	void OnConnectionAccepted(RakNet::Packet* packet);

	bool IsRunning() const;

private:
	static RRPG* instance;

	RakNet::RakPeerInterface* rpi;
	std::mutex networkState_mutex;
	NetworkState networkState;
	unsigned short totalPlayers;
	unsigned int clientPort;
	unsigned int serverPort;
	std::string serverAddress;
	Player player;
};