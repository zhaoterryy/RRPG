#include "server.h"

#include "RakNetSocket2.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <random>

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
	
	std::random_device rd;
	std::mt19937 rng(rd());
	int GetRandomInteger(int min, int max)
	{
		std::uniform_int_distribution<int> uni(min, max);
		return uni(rng);
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
				case RRPG_ID::C_INTRO:
					OnClientIntro(p);
					break;
				case RRPG_ID::C_READY:
					OnPlayerReady(p);
					break;
				case RRPG_ID::C_UNREADY:
					OnPlayerUnready(p);
					break;
				case RRPG_ID::C_PLAYER_LIST_REQUEST:
					OnPlayerListRequest(p);
					break;
				case RRPG_ID::C_PLAYER_STATS_REQUEST:
					OnPlayerStatsRequest(p);
					break;
				case RRPG_ID::C_CHAT:
					OnClientChatReceived(p);
					break;
				case RRPG_ID::C_JOB_CHOSEN:
					OnPlayerJobChosen(p);
					break;
				case RRPG_ID::C_ACTION_TAKEN:
					OnPlayerActionTaken(p);
					break;
				default:
					printf("client packet with no ID: %s\n", p->data);
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
		if (input == ".quit")
			isQuitting = true;
		else
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
		printf("ID_ALREADY_CONNECTED");
// 		printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", p->guid);
		break;
	case ID_INCOMPATIBLE_PROTOCOL_VERSION:
		printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
		break;
	case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
		totalConnections--;
		break;
	case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_CONNECTION_LOST\n");
		totalConnections--;
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
	
	playerAddresses.emplace(RakNet::RakNetGUID::ToUint32(p->guid), p->systemAddress);

	char* name = new char[256];
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	RakNet::StringCompressor::Instance()->DecodeString(name, 256, &bs);
	bool ready;
	bs.Read(ready);

	players.emplace(RakNet::RakNetGUID::ToUint32(p->guid), Player{ std::string(name) });
	memcpy(name + strlen(name), " has joined.", 13);
	BroadcastMessage(name);

	if (totalConnections != EXPECTED_PLAYERS)
	{
		char buffer[40];
		snprintf(buffer, 40, "Waiting for %i more player%s..", 
			EXPECTED_PLAYERS - totalConnections, 
			(EXPECTED_PLAYERS - totalConnections == 1 ? "." : "s.")
		);
		BroadcastMessage(&buffer[0]);
	}
	delete[] name;
}

void Server::OnClientChatReceived(RakNet::Packet* p)
{
	Player player = GetPlayer(p->guid);
	char* cmsg = new char[2048];
	char* message = new char[2048 + player.name.length()];

	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	RakNet::StringCompressor::Instance()->DecodeString(cmsg, 2048, &bs);

	memcpy(message, player.name.c_str(), player.name.length());
	memcpy(message + player.name.length(), ": ", 2);
	memcpy(message + player.name.length() + 2, cmsg, strlen(cmsg) + 1);

	std::cout << message << std::endl;
	rpi->Send(message, (const int)strlen(message) + 1, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);

	delete[] cmsg;
	delete[] message;
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

	if (totalConnections == EXPECTED_PLAYERS)
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

void Server::OnPlayerListRequest(RakNet::Packet* p)
{
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::S_REPLY_PLAYER_LIST_REQUEST);
	bs.Write((int)players.size());
	for (const auto& it : players)
	{
		RakNet::StringCompressor::Instance()->EncodeString(it.second.name.c_str(), 256, &bs);
		bs.Write(it.second.ready);
	}

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, p->systemAddress, false);
}

void Server::OnPlayerJobChosen(RakNet::Packet* p)
{
	Player& player = GetPlayer(p->guid);
	player.ready = true;

	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	bs.Read(player.job);
	
	char buffer[1024];
	snprintf(buffer, 1024, "%s has chosen to be a %s\n", player.name.c_str(),
		(player.job == CharacterClass::Wizard ? "Wizard" :
			player.job == CharacterClass::Warrior ? "Warrior" :
			player.job == CharacterClass::Assassin ? "Assassin" :
			"Jobless"));

	BroadcastMessage(&buffer[0]);

	auto it = players.find(currentPlayerTurn);
	it++;

	if (it == players.end())
		StartMainGame();
	else
	{
		currentPlayerTurn = it->first;
		RakNet::BitStream ttBs;
		ttBs.Write((unsigned char)RRPG_ID::S_TAKE_TURN);
		rpi->Send(&ttBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, GetAddressFromID(currentPlayerTurn), false);
	}
}

void Server::OnPlayerStatsRequest(RakNet::Packet* p)
{
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::S_REPLY_PLAYER_STATS_REQUEST);
	bs.Write((int)players.size());
	for (const auto& it : players)
	{
		RakNet::StringCompressor::Instance()->EncodeString(it.second.name.c_str(), 256, &bs);
		bs.Write(it.second.job);
		bs.Write(it.second.health);
	}

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, p->systemAddress, false);
}

void Server::OnPlayerActionTaken(RakNet::Packet* p)
{
	Action action;
	char* tname = new char[256];
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	bs.Read(action);
	RakNet::StringCompressor::Instance()->DecodeString(tname, 256, &bs);

	Player* target = GetPlayerWithName(tname);
	Player& origin = GetPlayer(p->guid);
	assert(target != nullptr);

	char buffer[256];
	switch (action)
	{
		case Action::Heal:
		{			
			snprintf(buffer, 256, "%s healed %s for %i", origin.name.c_str(), target->name.c_str(), 10);
			BroadcastMessage(&buffer[0]);
			ModifyHealth(*target, 10);
			break;
		}
		case Action::HealRng:
		{
			int healAmount = GetRandomInteger(5, 15);			
			snprintf(buffer, 256, "%s randomly healed %s for %i", origin.name.c_str(), target->name.c_str(), healAmount);
			BroadcastMessage(&buffer[0]);
			ModifyHealth(*target, healAmount);
			break;
		}
		case Action::Attack:
		{			
			snprintf(buffer, 256, "%s attacked %s for %i", origin.name.c_str(), target->name.c_str(), 12);
			BroadcastMessage(&buffer[0]);
			ModifyHealth(*target, -12);
			break;
		}
		case Action::AtkRng:
		{
			int atkAmount = -GetRandomInteger(6, 18);			
			snprintf(buffer, 256, "%s randomly attacked %s for %i", origin.name.c_str(), target->name.c_str(), -atkAmount);
			BroadcastMessage(&buffer[0]);
			ModifyHealth(*target, atkAmount);
			break;
		}
	}

	NextTurn();
	delete[] tname;
}

void Server::NextTurn()
{
	auto it = players.find(currentPlayerTurn);
	it++;

	while (it == players.end() || it->second.dead)
	{
		if (it == players.end())
			it = players.begin();
		else
			it++;
	}

	currentPlayerTurn = it->first;

	if (std::count_if(players.begin(), players.end(), [](std::pair<unsigned long, Player> p) { return p.second.dead == false; }) == 1)
	{
		GameOver(currentPlayerTurn);
		return;
	}


	char buffer[256];
	snprintf(buffer, 256, "%s's turn", it->second.name.c_str());
	BroadcastMessage(&buffer[0]);

	RakNet::BitStream ttBs;
	ttBs.Write((unsigned char)RRPG_ID::S_TAKE_TURN);
	rpi->Send(&ttBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, GetAddressFromID(currentPlayerTurn), false);
}

void Server::ModifyHealth(Player& player, int diff)
{
	player.health += diff;
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::S_UPDATE_PLAYER_HP);
	RakNet::StringCompressor::Instance()->EncodeString(player.name.c_str(), (int) player.name.length() + 1, &bs);
	bs.Write(player.health);
	if (player.health > 0)
		printf("Internal: %s is now at %i health\n", player.name.c_str(), player.health);
	else
	{
		printf("Internal: %s is dead\n", player.name.c_str());
		player.dead = true;
	}
	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
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
	std::cout << "Internal: Game has started." << std::endl;
	networkState = NS_GAME_STARTED;
	gameState = GS_CHARACTER_SELECT;
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::S_GAME_STARTED);
	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);

	for (auto& it : players)
		it.second.ready = false;

	auto it = players.begin();
	currentPlayerTurn = it->first;
	
	RakNet::BitStream ttBs;
	ttBs.Write((unsigned char)RRPG_ID::S_TAKE_TURN);
	rpi->Send(&ttBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, GetAddressFromID(currentPlayerTurn), false);
}

void Server::StartMainGame()
{
	gameState = GS_MAIN;
	RakNet::BitStream gsBs;
	gsBs.Write((unsigned char)RRPG_ID::S_UPDATE_GAME_STATE);
	gsBs.Write(gameState);
	gsBs.Write((int)players.size());
	for (const auto& it : players)
	{
		RakNet::StringCompressor::Instance()->EncodeString(it.second.name.c_str(), 256, &gsBs);
		gsBs.Write(it.second.health);
		gsBs.Write(it.second.ready);
		gsBs.Write(it.second.job);
	}

	rpi->Send(&gsBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	NextTurn();
}

void Server::GameOver(unsigned long winnerId)
{
	gameState = GS_GAME_OVER;
	Player& player = GetPlayer(winnerId);
	char buffer[256];
	snprintf(buffer, 256, "%s wins!", player.name.c_str());
	BroadcastMessage(&buffer[0]);
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::S_UPDATE_GAME_STATE);
	bs.Write(gameState);
	
	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
}

Player& Server::GetPlayer(RakNet::RakNetGUID id)
{
	unsigned long guid = RakNet::RakNetGUID::ToUint32(id);
	return GetPlayer(guid);
}

Player& Server::GetPlayer(unsigned long id)
{
	auto it = players.find(id);
	assert(it != players.end());
	return it->second;
}

Player* Server::GetPlayerWithName(const char* name)
{
	for (auto& it : players)
		if (it.second.name == name)
			return &it.second;

	return nullptr;
}

RakNet::SystemAddress Server::GetAddressFromID(unsigned long id)
{
	auto it = playerAddresses.find(id);
	assert(it != playerAddresses.end());
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
	return !isQuitting;
}
