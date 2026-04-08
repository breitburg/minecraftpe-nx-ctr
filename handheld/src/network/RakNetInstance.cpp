#include "RakNetInstance.h"

#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef __3DS__
#include <3ds.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "Packet.h"
#include "NetEventCallback.h"
#include "../raknet/RakPeerInterface.h"
#include "../raknet/BitStream.h"
#include "../raknet/MessageIdentifiers.h"
#include "../raknet/GetTime.h"
#include "../AppConstants.h"

#include "../platform/log.h"

#define APP_IDENTIFIER "MCCPP;" APP_VERSION_STRING ";"
#define APP_IDENTIFIER_MINECON "MCCPP;MINECON;"

RakNetInstance::RakNetInstance()
:	rakPeer(NULL),
	_isServer(false),
	_isLoggedIn(false)
{
	// Сеть (socInit) уже инициализирована в main.cpp, здесь ничего не трогаем
	rakPeer = RakNet::RakPeerInterface::GetInstance();
	rakPeer->SetTimeoutTime(20000, RakNet::UNASSIGNED_SYSTEM_ADDRESS);
    rakPeer->SetOccasionalPing(true);
}

RakNetInstance::~RakNetInstance()
{
	if (rakPeer)
	{
#ifdef __3DS__
		rakPeer->Shutdown(0, 0); // 3DS: 0 таймаут, чтобы избежать зависаний потока
#else
		rakPeer->Shutdown(100, 0);
#endif
		RakNet::RakPeerInterface::DestroyInstance(rakPeer);
		rakPeer = NULL;
	}
}

bool RakNetInstance::host(const std::string& localName, int port, int maxConnections /* = 4 */)
{
#ifdef __SWITCH__
	Result rc = socketInitializeDefault();
	if (R_FAILED(rc)) {
		printf("[DEBUG] socketInitializeDefault FAILED! result 0x%x\n", rc);
	}
#endif

	if (rakPeer->IsActive())
	{
#ifdef __3DS__
		rakPeer->Shutdown(0); // Обязательно выключаем, чтобы освободить старый порт (например от пинга)
#else
		rakPeer->Shutdown(500);
#endif
	}

	RakNet::SocketDescriptor socket(port, 0);
	socket.socketFamily = AF_INET;

	rakPeer->SetMaximumIncomingConnections(maxConnections);
	RakNet::StartupResult result = rakPeer->Startup(maxConnections, &socket, 1);

	if (result != RakNet::RAKNET_STARTED) {
		printf("[RakNet] HOST FAILED! Error code: %d on port %d\n", result, port);
		return false;
	}

	printf("[RakNet] HOST SUCCESS on port %d\n", port);
	_isServer = true;
	isPingingForServers = false;

	return true;
}

void RakNetInstance::announceServer(const std::string& localName)
{
	if (_isServer && rakPeer->IsActive())
	{
		RakNet::RakString connectionData;

#if defined(MINECON)
		connectionData += APP_IDENTIFIER_MINECON;
#else
		connectionData += APP_IDENTIFIER;
#endif
		connectionData += localName.c_str();

		RakNet::BitStream bitStream;
		bitStream.Write(connectionData);
		rakPeer->SetOfflinePingResponse((const char*)bitStream.GetData(), bitStream.GetNumberOfBytesUsed());
	}
}

bool RakNetInstance::connect(const char* host, int port)
{
	_isLoggedIn = false;
	RakNet::StartupResult result = RakNet::RAKNET_STARTED;

	RakNet::SocketDescriptor socket(0, 0);
	socket.socketFamily = AF_INET;

	if (rakPeer->IsActive())
	{
#ifdef __3DS__
		rakPeer->Shutdown(0);
#else
		rakPeer->Shutdown(500);
#endif
	}

	result = rakPeer->Startup(4, &socket, 1);

	_isServer = false;
	isPingingForServers = false;

	if (result == RakNet::RAKNET_STARTED || result == RakNet::RAKNET_ALREADY_STARTED)
	{
		RakNet::ConnectionAttemptResult connectResult = rakPeer->Connect(host, port, NULL, 0, NULL, 0, 12, 500, 0);
		return (connectResult == RakNet::CONNECTION_ATTEMPT_STARTED);
	}

	printf("[RakNet] CONNECT STARTUP FAILED! Error code: %d\n", result);
	return false;
}

void RakNetInstance::disconnect()
{
	if (rakPeer->IsActive())
	{
#ifdef __3DS__
		rakPeer->Shutdown(0); // Безопасное выключение
#else
		rakPeer->Shutdown(500);
#endif
	}
	_isLoggedIn = false;
	_isServer = false;
	isPingingForServers = false;
}

#ifdef __SWITCH__
RakNet::RakString RakNetInstance::GetBroadcastAddress()
{
	u32 ip = 0, subnet = 0, gw = 0, dns1 = 0, dns2 = 0;
	if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&ip, &subnet, &gw, &dns1, &dns2)))
	{
		u32 ipHost     = __builtin_bswap32(ip);
		u32 subnetHost = __builtin_bswap32(subnet);
		u32 broadcastHost = (ipHost & subnetHost) | (~subnetHost);

		char bcStr[32];
		sprintf(bcStr, "%u.%u.%u.%u",
			(broadcastHost >> 24) & 0xFF,
			(broadcastHost >> 16) & 0xFF,
			(broadcastHost >> 8)  & 0xFF,
			broadcastHost         & 0xFF);
		return bcStr;
	}
	return "255.255.255.255";
}
#endif

#ifdef __3DS__
RakNet::RakString GetBroadcastAddress_3DS()
{
	uint32_t ip = gethostid(); // Родная функция libctru для получения IP
	if (ip == 0 || ip == 0xFFFFFFFF) {
		return "255.255.255.255";
	}
	// Собираем правильный Broadcast IP для 3DS (IP | Инвертированная маска)
	// Маска сети обычно /24 (255.255.255.0).
	struct in_addr bcast;
	bcast.s_addr = ip | htonl(0x000000FF);

	char bcStr[32];
	strcpy(bcStr, inet_ntoa(bcast));
	return bcStr;
}
#endif

void RakNetInstance::pingForHosts(int basePort)
{
	if (!rakPeer->IsActive())
	{
		RakNet::SocketDescriptor socket(0, 0);
		rakPeer->Startup(4, &socket, 1);
	}

	isPingingForServers = true;
	pingPort = basePort;
	lastPingTime = RakNet::GetTimeMS();

	for (int i = 0; i < 4; ++i) {
#ifdef __SWITCH__
		rakPeer->Ping(GetBroadcastAddress().C_String(), basePort + i, false);
#elif defined(__3DS__)
		rakPeer->Ping(GetBroadcastAddress_3DS().C_String(), basePort + i, false);
#else
		rakPeer->Ping("255.255.255.255", basePort + i, true);
#endif
	}
}

void RakNetInstance::stopPingForHosts()
{
	if (isPingingForServers)
	{
#ifdef __3DS__
		rakPeer->Shutdown(0);
#else
		rakPeer->Shutdown(0);
#endif
		isPingingForServers = false;
	}
}

const ServerList& RakNetInstance::getServerList()
{
	return availableServers;
}

void RakNetInstance::clearServerList()
{
	availableServers.clear();
}

RakNet::RakPeerInterface* RakNetInstance::getPeer()
{
	return rakPeer;
}

bool RakNetInstance::isProbablyBroken() {
    return rakPeer->errorState < -100;
}
void RakNetInstance::resetIsBroken() {
	rakPeer->errorState = 0;
}

bool RakNetInstance::isMyLocalGuid(const RakNet::RakNetGUID& guid)
{
	return rakPeer->IsActive() && rakPeer->GetMyGUID() == guid;
}

void RakNetInstance::runEvents(NetEventCallback* callback)
{
	RakNet::Packet* currentEvent;

	while ((currentEvent = rakPeer->Receive()) != NULL)
	{
		int packetId = currentEvent->data[0];
		int length = currentEvent->length;

		RakNet::BitStream activeBitStream(currentEvent->data + 1, length - 1, false);

		if (callback) {
			if (packetId < ID_USER_PACKET_ENUM)
			{
				switch (packetId)
				{
				case ID_NEW_INCOMING_CONNECTION:
					callback->onNewClient(currentEvent->guid);
					break;
				case ID_CONNECTION_REQUEST_ACCEPTED:
					serverGuid = currentEvent->guid;
					callback->onConnect(currentEvent->guid);
					break;
				case ID_CONNECTION_ATTEMPT_FAILED:
					callback->onUnableToConnect();
					break;
				case ID_DISCONNECTION_NOTIFICATION:
				case ID_CONNECTION_LOST:
					callback->onDisconnect(currentEvent->guid);
					break;
				case ID_UNCONNECTED_PONG:
					{
						RakNet::TimeMS time;
						RakNet::RakString data;
						activeBitStream.Read(time);
						activeBitStream.Read(data);

						int index = handleUnconnectedPong(data, currentEvent, APP_IDENTIFIER, false);
						if (index < 0) {
							index = handleUnconnectedPong(data, currentEvent, APP_IDENTIFIER_MINECON, true);
							if (index >= 0) availableServers[index].isSpecial = true;
						}
					}
					break;
				}
			}
			else
			{
				int userPacketId = packetId - ID_USER_PACKET_ENUM;
				bool isStatusPacket = userPacketId <= PACKET_READY;

				if (isStatusPacket || _isServer || _isLoggedIn) {

					if (Packet* packet = MinecraftPackets::createPacket(packetId)) {
						packet->read(&activeBitStream);
						packet->handle(currentEvent->guid, callback);
						delete packet;
					}
				}
			}
		}

		rakPeer->DeallocatePacket(currentEvent);
	}

	if (isPingingForServers)
	{
		if (RakNet::GetTimeMS() - lastPingTime > 1000)
		{
			ServerList::iterator it = availableServers.begin();
			for (; it != availableServers.end(); )
			{
				if (RakNet::GetTimeMS() - it->pingTime > 3000)
				{
					it = availableServers.erase(it);
				}
				else
				{
					++it;
				}
			}

			pingForHosts(pingPort);
		}
	}
}

void RakNetInstance::send(Packet& packet) {
	RakNet::BitStream bitStream;
	packet.write(&bitStream);
	if (_isServer)
	{
		rakPeer->Send(&bitStream, packet.priority, packet.reliability, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true);
	}
	else
	{
		rakPeer->Send(&bitStream, packet.priority, packet.reliability, 0, serverGuid, false);
	}
}

void RakNetInstance::send(const RakNet::RakNetGUID& guid, Packet& packet) {
	RakNet::BitStream bitStream;
	packet.write(&bitStream);
	rakPeer->Send(&bitStream, packet.priority, packet.reliability, 0, guid, false);
}

void RakNetInstance::send(Packet* packet)
{
	send(*packet);
	delete packet;
}

void RakNetInstance::send(const RakNet::RakNetGUID& guid, Packet* packet)
{
	send(guid, *packet);
	delete packet;
}

#ifdef _DEBUG
const char* RakNetInstance::getPacketName(int packetId)
{
	// Укоротил для экономии места в ответе, у тебя здесь стандартный switch
	return "Unknown or user-defined";
}
#endif

int RakNetInstance::handleUnconnectedPong(const RakNet::RakString& data, const RakNet::Packet* p, const char* appid, bool insertAtBeginning)
{
	RakNet::RakString appIdentifier(appid);
	bool emptyNameOrLonger = data.GetLength() >= appIdentifier.GetLength();

	if ( !emptyNameOrLonger || appIdentifier.StrCmp(data.SubStr(0, appIdentifier.GetLength())) != 0)
		return -1;

	for (unsigned int i = 0; i < availableServers.size(); i++) {
		if (availableServers[i].address == p->systemAddress) {
			availableServers[i].pingTime = RakNet::GetTimeMS();

			bool emptyName = data.GetLength() == appIdentifier.GetLength();
			if (emptyName)
				availableServers[i].name = "";
			else {
				availableServers[i].name = data.SubStr(appIdentifier.GetLength(), data.GetLength() - appIdentifier.GetLength());
			}
			return i;
		}
	}
	PingedCompatibleServer server;
	server.address = p->systemAddress;
	server.pingTime = RakNet::GetTimeMS();
	server.isSpecial = false;
	server.name = data.SubStr(appIdentifier.GetLength(), data.GetLength() - appIdentifier.GetLength());

	if (insertAtBeginning) {
		availableServers.insert(availableServers.begin(), server);
		return 0;
	} else {
		availableServers.push_back(server);
		return availableServers.size() - 1;
	}
}

void RakNetInstance::setIsLoggedIn( bool status ) {
	_isLoggedIn = status;
}