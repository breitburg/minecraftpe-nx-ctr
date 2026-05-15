#include "RakNetInstance.h"

#include <cstdio>
#include <cstring>
#include <string>

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

namespace {

const char* const kLanAnnouncementPrefix = "MCCPP;";

bool isUsableIpv4Address(const char* value)
{
	if (value == NULL || value[0] == '\0')
		return false;

	if (strcmp(value, "UNASSIGNED_SYSTEM_ADDRESS") == 0)
		return false;

	if (strcmp(value, "127.0.0.1") == 0 || strcmp(value, "0.0.0.0") == 0)
		return false;

	int a, b, c, d;
	return sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) == 4;
}

RakNet::RakString getSlash24BroadcastAddress(const char* value)
{
	int a, b, c, d;
	if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
		return "";

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%d.%d.%d.255", a, b, c);
	return buffer;
}

void appendUniqueBroadcast(std::vector<RakNet::RakString>& targets, const RakNet::RakString& candidate)
{
	if (candidate.IsEmpty())
		return;

	for (size_t i = 0; i < targets.size(); ++i)
	{
		if (targets[i] == candidate)
			return;
	}

	targets.push_back(candidate);
}

bool parseLanAnnouncement(const RakNet::RakString& data, RakNet::RakString& name, bool& isSpecial)
{
	const std::string raw = data.C_String();
	const std::string prefix(kLanAnnouncementPrefix);

	if (raw.compare(0, prefix.size(), prefix) != 0)
		return false;

	const size_t flavorSep = raw.find(';', prefix.size());
	if (flavorSep == std::string::npos)
		return false;

	const std::string flavor = raw.substr(prefix.size(), flavorSep - prefix.size());
	name = raw.substr(flavorSep + 1).c_str();
	isSpecial = flavor == "MINECON";
	return true;
}

} // namespace

RakNetInstance::RakNetInstance()
:	rakPeer(NULL),
	serverGuid(RakNet::UNASSIGNED_RAKNET_GUID),
	isPingingForServers(false),
	pingPort(0),
	lastPingTime(0),
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

	_isServer = false;
	_isLoggedIn = false;
	serverGuid = RakNet::UNASSIGNED_RAKNET_GUID;

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
	announceServer(localName);

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
	serverGuid = RakNet::UNASSIGNED_RAKNET_GUID;
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
	serverGuid = RakNet::UNASSIGNED_RAKNET_GUID;
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
		RakNet::StartupResult result = rakPeer->Startup(4, &socket, 1);
		if (result != RakNet::RAKNET_STARTED && result != RakNet::RAKNET_ALREADY_STARTED)
		{
			printf("[RakNet] PING STARTUP FAILED! Error code: %d\n", result);
			isPingingForServers = false;
			return;
		}
	}

	isPingingForServers = true;
	pingPort = basePort;
	lastPingTime = RakNet::GetTimeMS();

	std::vector<RakNet::RakString> broadcastTargets;
	appendUniqueBroadcast(broadcastTargets, "255.255.255.255");

	for (unsigned int i = 0; i < MAXIMUM_NUMBER_OF_INTERNAL_IDS; ++i)
	{
		const char* localIp = rakPeer->GetLocalIP(i);
		if (!isUsableIpv4Address(localIp))
			continue;

		appendUniqueBroadcast(broadcastTargets, getSlash24BroadcastAddress(localIp));
	}

#ifdef __SWITCH__
	appendUniqueBroadcast(broadcastTargets, GetBroadcastAddress());
#elif defined(__3DS__)
	appendUniqueBroadcast(broadcastTargets, GetBroadcastAddress_3DS());
#endif

	for (size_t targetIndex = 0; targetIndex < broadcastTargets.size(); ++targetIndex)
	{
		for (int i = 0; i < 4; ++i)
		{
			rakPeer->Ping(broadcastTargets[targetIndex].C_String(), basePort + i, false);
		}
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
						handleUnconnectedPong(data, currentEvent);
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
	if (!rakPeer || !rakPeer->IsActive())
		return;
	if (!_isServer && serverGuid == RakNet::UNASSIGNED_RAKNET_GUID)
		return;

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
	if (!rakPeer || !rakPeer->IsActive() || guid == RakNet::UNASSIGNED_RAKNET_GUID)
		return;

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

int RakNetInstance::handleUnconnectedPong(const RakNet::RakString& data, const RakNet::Packet* p)
{
	RakNet::RakString parsedName;
	bool isSpecial = false;
	if (!parseLanAnnouncement(data, parsedName, isSpecial))
		return -1;

	for (unsigned int i = 0; i < availableServers.size(); i++) {
		if (availableServers[i].address == p->systemAddress) {
			availableServers[i].pingTime = RakNet::GetTimeMS();
			availableServers[i].name = parsedName;
			availableServers[i].isSpecial = isSpecial;
			return i;
		}
	}
	PingedCompatibleServer server;
	server.address = p->systemAddress;
	server.pingTime = RakNet::GetTimeMS();
	server.isSpecial = isSpecial;
	server.name = parsedName;

	if (isSpecial) {
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
