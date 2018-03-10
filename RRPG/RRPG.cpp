#include "RRPG.h"
#include "RRPG_MessageIdentifiers.h"

#include "RakNetSocket2.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include <iostream>
#include <thread>

RRPG* RRPG::instance = nullptr;

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

RRPG::RRPG()
{
	networkState = NS_INITIALIZATION;
	rpi = RakNet::RakPeerInterface::GetInstance();
}

void RRPG::InputHandler()
{
	while (IsRunning())
	{
		char userInput[255];
		if (networkState == NS_INITIALIZATION)
		{
			std::cout << "Enter name: ";
			std::cin >> player.name;
			std::cout << "Enter listening port: ";
			std::cin >> clientPort;
			std::cout << "Enter server port: ";
			std::cin >> serverPort;
			std::cout << "Enter IP to connect to: ";
			std::cin >> serverAddress;

			networkState_mutex.lock();
			networkState = NS_CREATE_SOCKET;
			networkState_mutex.unlock();
		}
		else
		{
			std::cin >> userInput;
		}
	}
}

void RRPG::PacketHandler()
{
	while (IsRunning())
	{
		for (RakNet::Packet* p = rpi->Receive(); p != nullptr; rpi->DeallocatePacket(p), p = rpi->Receive())
		{
			if (!IsLowLevelPacketHandled(p))
			{
				unsigned char packetIdentifier = GetPacketIdentifier(p);

				switch (packetIdentifier)
				{
				default:
					// It's a client, so just show the message
					printf("%s\n", p->data);
					break;
				}
			}
		}
	}
}

bool RRPG::IsLowLevelPacketHandled(RakNet::Packet* p)
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
	case ID_CONNECTION_BANNED: // Banned from this server
		printf("We are banned from this server.\n");
		break;
	case ID_CONNECTION_ATTEMPT_FAILED:
		printf("Connection attempt failed\n");
		break;
	case ID_NO_FREE_INCOMING_CONNECTIONS:
		// printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
		std::cout << "Server is full." << std::endl;
		break;

	case ID_INVALID_PASSWORD:
		printf("ID_INVALID_PASSWORD\n");
		break;

	case ID_CONNECTION_LOST:
		// Couldn't deliver a reliable packet - i.e. the other system was abnormally
		// terminated
		printf("ID_CONNECTION_LOST\n");
		break;

	case ID_CONNECTION_REQUEST_ACCEPTED:
		// This tells the client they have connected
// 		printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", p->systemAddress.ToString(true), p->guid.ToString());
// 		printf("My external address is %s\n", rpi->GetExternalID(p->systemAddress).ToString(true));
		OnConnectionAccepted(p);
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

void RRPG::OnConnectionAccepted(RakNet::Packet* p)
{
	std::cout << "Connection accepted!" << std::endl;
	std::lock_guard<std::mutex> guard(networkState_mutex);
	networkState = NS_LOBBY;

	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::CLIENT_INTRO);
	RakNet::StringCompressor::Instance()->EncodeString(player.name.c_str(), player.name.length() + 1, &bs);
	bs.Write(player.ready);
// 	bs.Write((char*)&player, sizeof(Player));

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, p->systemAddress, false);
}

void RRPG::Start()
{
	std::cout << "RRPG Client" << std::endl;

	std::thread inputHandler(&RRPG::InputHandler, this);
	std::thread packetHandler(&RRPG::PacketHandler, this);

	while (IsRunning())
	{
		if (networkState == NS_CREATE_SOCKET)
		{
			RakNet::SocketDescriptor socketDescriptor(clientPort, nullptr);
			socketDescriptor.socketFamily = AF_INET;

			while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptor.port, socketDescriptor.hostAddress, socketDescriptor.socketFamily, SOCK_DGRAM) == true)
				socketDescriptor.port++;

			assert(rpi->Startup(8, &socketDescriptor, 1) == RakNet::RAKNET_STARTED);
			rpi->SetOccasionalPing(true);

			RakNet::ConnectionAttemptResult car = rpi->Connect(serverAddress.c_str(), serverPort, nullptr, 0);
			RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
			printf("Client attempting connection to %s:%i.. awaiting response\n", serverAddress.c_str(), serverPort);
			networkState_mutex.lock();
			networkState = NS_PENDING_CONNECTION;
			networkState_mutex.unlock();
		}
	}

	inputHandler.join();
	packetHandler.join();
}

bool RRPG::IsRunning() const
{
	return true;
}

int main()
{
	RRPG::Get().Start();
	return 0;
}


