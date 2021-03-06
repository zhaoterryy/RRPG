#include "RRPG.h"

#include "RakNetSocket2.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include <iostream>
#include <thread>
#include <algorithm>
#include <sstream>
#include <iterator>

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
		std::string userInput;
		bool sendAsChatMessage = false;
		std::getline(std::cin, userInput);
		std::string userInput_lower = userInput;
		std::transform(userInput_lower.begin(), userInput_lower.end(), userInput_lower.begin(), ::tolower);

		if (userInput_lower == ".whoami")
		{
			std::cout << player.name << std::endl;
			continue;
		}

		if (networkState == NS_LOBBY)
		{
			if (!HandleLobbyInput(userInput_lower))
				sendAsChatMessage = true;
		}
		else if (networkState == NS_GAME_STARTED)
		{
			if (userInput_lower == ".stats")
				RequestPlayerStatsFromServer();
			else if (userInput_lower == ".localstats")
				PrintLocalPlayerStats();
			else if (myTurn == true)
				if (!HandleGameInput(userInput_lower))
					sendAsChatMessage = true;
		}

		if (sendAsChatMessage && !userInput.empty())
		{
			if (userInput_lower[0] == '.')
			{
				std::cout << "Whoops! Try again!" << std::endl;
				PrintInstructions();
			}
			else
			{
				RakNet::BitStream bs;
				bs.Write((unsigned char)RRPG_ID::C_CHAT);
				RakNet::StringCompressor::Instance()->EncodeString(userInput.c_str(), (int)userInput.length() + 1, &bs);
				rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, rpi->GetSystemAddressFromIndex(0), false);
			}
		}
	}
}

void RRPG::PrintInstructions()
{
	if (networkState == NS_LOBBY)
	{
		std::cout << "You can either...\n.ready\n.notready\n.players\n.whoami" << std::endl;
	}
	else if (networkState == NS_GAME_STARTED)
	{
		if (gameState == GS_CHARACTER_SELECT)
		{
			std::cout << "Select a character." << std::endl;
			std::cout << "1. .wizard\n2. .warrior\n3. .assassin" << std::endl;
			std::cout << "Prepend with a period (.) for example: .1/.wizard or .2/.warrior" << std::endl;
		}
		else if (gameState == GS_MAIN)
		{
			std::cout << "Select an action." << std::endl;
			std::cout << "1. .heal(10)\n2. .healrng(5~15)\n3. .atk(12)\n4. .atkrng(6~18)" << std::endl;
			std::cout << ".stats/.localstats" << std::endl;
			std::cout << "Prepend with a period (.) for example: .1/.heal [name] or .4/.atkrng [name]" << std::endl;
		}
	}
}

bool RRPG::HandleLobbyInput(std::string input)
{
	if (input == ".ready")
		Ready();
	else if (input == ".notready")
		Unready();
	else if (input == ".players")
		RequestPlayersFromServer();
	else
		return false;

	return true;
}

bool RRPG::HandleGameInput(std::string input)
{
	RakNet::BitStream bs;

	if (gameState == GS_CHARACTER_SELECT)
	{
		bs.Write((unsigned char)RRPG_ID::C_JOB_CHOSEN);
		if (input == ".1" || input == ".wizard")
			bs.Write(CharacterClass::Wizard);
		else if (input == ".2" || input == ".warrior")
			bs.Write(CharacterClass::Warrior);
		else if (input == ".3" || input == ".assassin")
			bs.Write(CharacterClass::Assassin);
		else
			return false;

	}
	else if (gameState == GS_MAIN)
	{
		std::istringstream iss(input);
		std::vector<std::string> result{
			std::istream_iterator<std::string>(iss), {}
		};
		if (result.size() < 2)
			return false;

		bs.Write((unsigned char)RRPG_ID::C_ACTION_TAKEN);
		if (result[0] == ".1" || result[0] == ".heal")
			bs.Write(Action::Heal);
		else if (result[0] == ".2" || result[0] == ".healrng")
			bs.Write(Action::HealRng);
		else if (result[0] == ".3" || result[0] == ".atk")
			bs.Write(Action::Attack);
		else if (result[0] == ".4" || result[0] == ".atkrng")
			bs.Write(Action::AtkRng);
		else
			return false;

		auto ValidPlayer = [result](Player p) -> bool
		{
			return p.name == result[1] && p.dead == false;
		};

		if (result[1] == "me")
			result[1] = player.name;
		else if (!std::any_of(players.begin(), players.end(), ValidPlayer))
			return false;

		RakNet::StringCompressor::Instance()->EncodeString(result[1].c_str(), (int)result[1].length() + 1, &bs);
	}

	myTurn = false;
	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, rpi->GetSystemAddressFromIndex(0), false);
	return true;
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
				case RRPG_ID::S_REPLY_PLAYER_LIST_REQUEST:
					OnPlayersListReceived(p);
					break;
				case RRPG_ID::S_REPLY_PLAYER_STATS_REQUEST:
					OnPlayersStatsReceived(p);
					break;
				case RRPG_ID::S_GAME_STARTED:
					OnGameStart(p);
					break;
				case RRPG_ID::S_TAKE_TURN:
					OnTakeTurn(p);
					break;
				case RRPG_ID::S_UPDATE_GAME_STATE:
					OnGameStateUpdate(p);
					break;
				case RRPG_ID::S_UPDATE_PLAYER_HP:
					OnPlayersHealthUpdated(p);
					break;
				default:
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
	PrintInstructions();

	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::C_INTRO);
	RakNet::StringCompressor::Instance()->EncodeString(player.name.c_str(), (int)player.name.length() + 1, &bs);
	bs.Write(player.ready);

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, p->systemAddress, false);
}

void RRPG::OnPlayersListReceived(RakNet::Packet* p)
{
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	int numPlayers;
	bs.Read(numPlayers);

	char* name = new char[256];
	bool ready;
	for (int i = 1; i <= numPlayers; i++)
	{
		RakNet::StringCompressor::Instance()->DecodeString(name, 256, &bs);
		bs.Read(ready);

		printf("%i. %s | %s\n", i, name, (ready ? "Ready" : "Not Ready"));
	}
	delete[] name;
}

void RRPG::OnPlayersStatsReceived(RakNet::Packet* p)
{
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	int numPlayers;
	bs.Read(numPlayers);

	char* name = new char[256];
	CharacterClass cc;
	int health;
	for (int i = 1; i <= numPlayers; i++)
	{
		RakNet::StringCompressor::Instance()->DecodeString(name, 256, &bs);
		bs.Read(cc);
		bs.Read(health);

		printf("%i. %s The %s has %i health\n", i, name, GetStringFromClass(cc), health);
	}
	delete[] name;
}

void RRPG::OnGameStart(RakNet::Packet* p)
{
	networkState = NS_GAME_STARTED;
	gameState = GS_CHARACTER_SELECT;
	std::cout << "WE HAVE BEEGUNN!!!" << std::endl;
}

void RRPG::OnMainGameStart(RakNet::Packet* p)
{
	int numPlayers;
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(16);
	bs.Read(numPlayers);
	players.reserve(numPlayers);
	char* name = new char[256];
	int health;
	bool ready;
	CharacterClass job;
	for (int i = 0; i < numPlayers; i++)
	{
		RakNet::StringCompressor::Instance()->DecodeString(name, 256, &bs);
		bs.Read(health);
		bs.Read(ready);
		bs.Read(job);
		players.push_back({ name, health, ready, job });

		if (name == player.name)
			player.job = job;
	}
	delete[] name;
}

void RRPG::OnGameOver(RakNet::Packet* p)
{
	gameState = GS_GAME_OVER;
	std::cout << "Game over.\n";
}

void RRPG::OnTakeTurn(RakNet::Packet* p)
{
	myTurn = true;
	PrintInstructions();
}

void RRPG::OnGameStateUpdate(RakNet::Packet* p)
{
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	bs.Read(gameState);

	if (gameState == GS_MAIN)
		OnMainGameStart(p);

	if (gameState == GS_GAME_OVER)
		OnGameOver(p);
}

void RRPG::OnPlayersHealthUpdated(RakNet::Packet* p)
{
	char* name = new char[256];
	int newHp;
	RakNet::BitStream bs(p->data, p->length, false);
	bs.IgnoreBits(8);
	RakNet::StringCompressor::Instance()->DecodeString(name, 256, &bs);
	bs.Read(newHp);
	
	for (Player& p : players)
	{
		if (p.name == name)
		{
			p.health = newHp;
			if (p.health > 0)
				printf("%s is now at %i health\n\n", p.name.c_str(), p.health);
			else
			{
				printf("%s is dead\n\n", p.name.c_str());
				p.dead = true;
			}
		}
	}

	if (player.name == name)
	{
		player.health = newHp;
		if (player.health < 1)
		{
			player.dead = true;
			printf("You are dead.\n");
		}

	}

	delete[] name;
}

void RRPG::Ready()
{
	std::lock_guard<std::mutex> guard(player_mutex);
	player.ready = true;

	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::C_READY);

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, rpi->GetSystemAddressFromIndex(0), false);
}

void RRPG::Unready()
{
	std::lock_guard<std::mutex> guard(player_mutex);
	player.ready = false;

	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::C_UNREADY);

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, rpi->GetSystemAddressFromIndex(0), false);
}

void RRPG::RequestPlayersFromServer()
{
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::C_PLAYER_LIST_REQUEST);

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, rpi->GetSystemAddressFromIndex(0), false);
}



void RRPG::RequestPlayerStatsFromServer()
{
	RakNet::BitStream bs;
	bs.Write((unsigned char)RRPG_ID::C_PLAYER_STATS_REQUEST);

	rpi->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, rpi->GetSystemAddressFromIndex(0), false);
}

void RRPG::PrintLocalPlayerStats()
{
	std::cout << "Local player stats:" << std::endl;
	for (const Player& player : players)
		printf("%s The %s has %i health\n", player.name.c_str(), GetStringFromClass(player.job), player.health);
}

void RRPG::Start()
{
	std::cout << "RRPG Client" << std::endl;
	std::cout << "Enter name: ";
	std::cin >> player.name;
	std::cout << "Enter listening port: ";
	std::cin >> clientPort;
	std::cout << "Enter server port: ";
	std::cin >> serverPort;
	std::cout << "Enter IP to connect to: ";
	std::cin >> serverAddress;

	networkState = NS_CREATE_SOCKET;

	std::thread inputHandler(&RRPG::InputHandler, this);
	std::thread packetHandler(&RRPG::PacketHandler, this);

	while (IsRunning())
	{
		GameLoop();
	}

	inputHandler.join();
	packetHandler.join();
}

bool RRPG::IsRunning() const
{
	return true;
}

void RRPG::GameLoop()
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

int main()
{
	RRPG::Get().Start();
	return 0;
}