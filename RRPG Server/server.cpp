#include "server.h"
#include "RRPG_MessageIdentifiers.h"

#include "RakNetSocket2.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <conio.h>


unsigned int Server::EXPECTED_PLAYERS = 3;
Server* Server::instance = nullptr;

namespace
{
	unsigned char GetPacketIdentifier(RakNet::Packet* packet)
	{
		if (packet == nullptr)
			return 255;

		if ((unsigned char)packet->data[0] == ID_TIMESTAMP)
		{
			RakAssert(packet->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
			return (unsigned char)packet->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
		}
		else
		{
			return (unsigned char)packet->data[0];
		}
	}
}

Server::Server()
{
	networkState = NS_INITIALIZATION;
	rpi = RakNet::RakPeerInterface::GetInstance();
}

void Server::Start()
{

	std::cout << "RRPG Server" << std::endl;
	std::cout << "Enter listening port: ";
	std::cin >> port;

	while (RakNet::IRNS2_Berkley::IsPortInUse(port, rpi->GetLocalIP(0), AF_INET, SOCK_DGRAM))
		port++;

	printf("IP Address: %s:%i\n", rpi->GetLocalIP(0), port);

	std::thread packetHandler(&Server::PacketHandler, this);
	std::thread inputHandler(&Server::InputHandler, this);
	networkState = NS_CREATE_SOCKET;

	while (IsRunning())
	{
		GameLoop();
	}

	packetHandler.join();
	inputHandler.join();

	rpi->Shutdown(300);
	RakNet::RakPeerInterface::DestroyInstance(rpi);
}

void Server::PacketHandler()
{
	while (IsRunning())
	{
		for (RakNet::Packet* p = rpi->Receive(); p; rpi->DeallocatePacket(p), p = rpi->Receive())
		{
			if (!IsLowLevelPacketHandled(p))
			{
				unsigned char packetIdentifier = GetPacketIdentifier(p);

				switch (packetIdentifier)
				{
				case RRPG_ID::CLIENT_INTRO:
					OnClientIntro(p);
					break;
				case RRPG_ID::CLIENT_READY:
					OnPlayerReady(p);
					break;
				case RRPG_ID::CLIENT_UNREADY:
					OnPlayerUnready(p);
					break;
				default:
					// It's a client, so just show the message
					printf("%s\n", p->data);
					break;
				}
			}
		}
	}
}

void Server::InputHandler()
{
	while (IsRunning())
	{
		char input[2048];
		std::cin.getline(input, sizeof(input));
		BroadcastMessage(&input[0]);
	}
}

bool Server::IsLowLevelPacketHandled(RakNet::Packet* p)
{
	unsigned char packetIdentifier = GetPacketIdentifier(p);

	switch (packetIdentifier)
	{
	case ID_DISCONNECTION_NOTIFICATION:
		// Connection lost normally
		printf("ID_DISCONNECTION_NOTIFICATION\n");
		break;
	case ID_ALREADY_CONNECTED:
		// Connection lost normally
		printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", p->guid);
		break;
	case ID_INCOMPATIBLE_PROTOCOL_VERSION:
		printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
		break;
	case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
		break;
	case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_CONNECTION_LOST\n");
		break;
	case ID_NEW_INCOMING_CONNECTION:
	case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
		OnIncomingConnection(p);
		break;
	case ID_CONNECTION_ATTEMPT_FAILED:
		printf("Connection attempt failed\n");
		break;
	case ID_CONNECTION_LOST:
		// Couldn't deliver a reliable packet - i.e. the other system was abnormally
		// terminated
		printf("ID_CONNECTION_LOST\n");
		break;
	case ID_CONNECTED_PING:
	case ID_UNCONNECTED_PING:
		printf("Ping from %s\n", p->systemAddress.ToString(true));
		break;
	default:
		return false;
		break;
	}
	return true;
}

void Server::OnIncomingConnection(RakNet::Packet* p)
{
	std::lock_guard<std::mutex> guard(totalPlayers_mutex);
	totalConnections++;
}

void Server::OnClientIntro(RakNet::Packet* p)
{
	std::lock_guard<std::mutex> guard(totalPlayers_mutex);
	if (totalConnections > EXPECTED_PLAYERS)
	{
		rpi->CloseConnection(p->systemAddress, true);
		totalConnections--;
	}
	char* name = new char[256];
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	RakNet::StringCompressor::Instance()->DecodeString(name, 256, &bs);
	bool ready;
	bs.Read(ready);

	players.emplace(RakNet::RakNetGUID::ToUint32(p->guid), Player{ std::string(name), 100, ready });
	memcpy(name + strlen(name), " has joined.", 13);
	BroadcastMessage(name);
	if (totalConnections == EXPECTED_PLAYERS)
	{
		StartGame();
	}
	else
	{
		char buffer[40];
		snprintf(buffer, 40, "Waiting for %i more players...", EXPECTED_PLAYERS - totalConnections);
		BroadcastMessage(&buffer[0]);
	}
	delete[] name;
}

void Server::OnPlayerReady(RakNet::Packet* p)
{
	std::lock_guard<std::mutex> guard(totalPlayers_mutex);
	Player& player = GetPlayer(p->guid);
	player.ready = true;
	std::string msg = player.name + " is ready.";
	BroadcastMessage(msg.c_str());

	for (const auto& it : players)
		if (!it.second.ready)
			return;

	StartGame();
}

void Server::OnPlayerUnready(RakNet::Packet* p)
{
	std::lock_guard<std::mutex> guard(totalPlayers_mutex);
	Player& player = GetPlayer(p->guid);
	player.ready = false;
	std::string msg = player.name + " is not ready.";
	BroadcastMessage(msg.c_str());
}

void Server::GameLoop()
{
	if (networkState == NS_CREATE_SOCKET)
	{
		RakNet::SocketDescriptor socketDescriptors[1];
		socketDescriptors[0].port = port;
		socketDescriptors[0].socketFamily = AF_INET;
		assert(rpi->Startup(EXPECTED_PLAYERS, socketDescriptors, 1) == RakNet::RAKNET_STARTED);
		rpi->SetMaximumIncomingConnections(EXPECTED_PLAYERS);
		networkState_mutex.lock();
		networkState = NS_LOBBY;
		networkState_mutex.unlock();
		std::cout << "Server waiting on connections..." << std::endl;
	}
}

void Server::StartGame()
{
}

Player& Server::GetPlayer(RakNet::RakNetGUID id)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(id);
	auto it = players.find(guid);
	assert(it != players.end());
	return it->second;
}

void Server::BroadcastMessage(const char* input)
{
	if (strlen(input) == 0)
		return;

	const static char prefix[] = "[Server] ";
	char* message = new char[2048 + strlen(prefix)];
	memcpy(message, prefix, strlen(prefix));
	memcpy(message + strlen(prefix), input, strlen(input) + 1);
	printf("Broadcast: %s\n", message);
	rpi->Send(message, (const int)strlen(message) + 1, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	delete[] message;
}

bool Server::IsRunning() const
{
	return true;
}
