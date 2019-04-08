#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>	// for fprintf
#include <string.h>	// for memcpy
#include <string>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <time.h>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <stdlib.h>


using namespace std;

//#include "DV.h"

//DV.h begins----------------------------------------------------------------------------------------



#define NROUTERS 6

struct dv_entry
{
public:	
	int nexthopPort() const { return (invalid() ? -1 : m_nexthopPort); }
	char nexthopName() const { return (invalid() ? '0' : m_nexthopName); }
	int cost() const { return (invalid() ? -1 : m_cost); }
	bool invalid() const { return m_invalid; }

	void setNexthopPort(int n) { m_nexthopPort = n; }
	void setNexthopName(char n) { m_nexthopName = n; }
	void setCost(int c) { m_cost = c; }
	void setValid() { m_invalid = false; }
	void setInvalid() { m_invalid = true; }
private:
	bool m_invalid;
	int m_nexthopPort; // port number of next hop router
	char m_nexthopName;
	int m_cost; // link cost to destination
};

struct node
{
	char name;
	int portno;
	timespec startTime;
	sockaddr_in addr;
};

class DV
{
public:
	DV() {}
	DV(const char *filename, const char *self);
	~DV() {}
	
	void reset(char dead);
	dv_entry *getEntries() { return m_entries; }
	int getSize() const { return sizeof(m_entries); }
	char getName() const { return nameOf(m_self); }
	void update(const void *advertisement, char src);
	dv_entry routeTo(const char dest) const { return m_entries[indexOf(dest)]; };
	std::vector<node> neighbors() const { return m_neighbors; };
	int portNoOf(char router);
	char nameOf(int index) const;
	int indexOf(char router) const;
	void initMyaddr(int portno);
	sockaddr_in myaddr() const { return m_myaddr; }
	void startTimer(node &n);
	bool timerExpired(node &n) const;
	int port() { return portNoOf(getName()); }

private:
	// member variables
	int m_self; // index of self
	int m_size;
	dv_entry m_entries[NROUTERS]; // each router's distance vectors
	dv_entry m_entries_backup[NROUTERS]; // initial distance vectors (for resetting)
	vector<node> m_neighbors; // port numbers of self's neighbors
	sockaddr_in m_myaddr;
	map<char, int> m_portnos;

	// helper functions
	int min(int originalCost, int selfToIntermediateCost, int intermediateToDestCost, char originalName, char newName, bool &updated) const;
	void print(dv_entry dv[], char name, std::string msg, bool timestamp) const;
};











//DV.h finished----------------------------------------------------------------------------------------





















//DV.cpp begins----------------------------------------------------------------------------------------






DV::DV(const char *filename, const char *self)
{
	fstream topology(filename);

	string line; // current line of file
	string field; // current token (to be put into entry's field)
	char selfName = self[0]; // name of self
	m_self = indexOf(self[0]);

	// initialize m_entries
	for (int dest = 0; dest < NROUTERS; dest++)
	{
		m_entries[dest].setNexthopName('0');
		m_entries[dest].setNexthopPort(-1);
		m_entries[dest].setCost(-1);
		m_entries[dest].setValid();
	}

	while (getline(topology, line)) // parse file line by line
	{
		stringstream linestream(line);
		dv_entry entry;

		entry.setValid();

		// source router
		getline(linestream, field, ',');
		char name = field[0];

		// destination router
		getline(linestream, field, ',');
		int dest = indexOf(field[0]);
		node n;
		n.name = field[0];
		entry.setNexthopName(field[0]);

		// destination port number
		getline(linestream, field, ',');
		int port = atoi(field.c_str());
		entry.setNexthopPort(port);
		n.portno = port;

		memset((char *)&n.addr, 0, sizeof(n.addr));
		n.addr.sin_family = AF_INET;
		n.addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		n.addr.sin_port = htons(port);

		// link cost
		getline(linestream, field, ',');
		entry.setCost(atoi(field.c_str()));

		if (selfName == 'H')
		{
			int i;
			for (i = 0; i < m_neighbors.size(); i++)
			{
				if (m_neighbors[i].name == n.name)
					break;
			}
			if (i == m_neighbors.size())
				m_neighbors.push_back(n);
		}
		else if (name == selfName)
		{
			startTimer(n);
			m_neighbors.push_back(n); // store neighbor
			m_entries[dest] = entry;
		}

		m_portnos[n.name] = n.portno;
	}

	// special port number for sending data packet
	m_portnos['H'] = 11111;

	memcpy((void*)m_entries_backup, (void*)m_entries, sizeof(m_entries));

	if (nameOf(m_self) != 'H')
		print(m_entries, nameOf(m_self), "Initial routing table", true);
}

void DV::reset(char dead)
{
	for (int i = 0; i < m_neighbors.size(); i++)
	{
		if (m_neighbors[i].name == dead)
		{
			if (m_entries_backup[indexOf(dead)].cost() != -1)
				m_entries_backup[indexOf(dead)].setInvalid();
		}
	}
	memcpy((void*)m_entries, (void*)m_entries_backup, sizeof(m_entries));
	print(m_entries, nameOf(m_self), "Reset routing table", true);
}

// update this router's distance vector based on received advertisement from source
// return false if this router's distance vector was not changed
void DV::update(const void *advertisementBuf, char source)
{
	dv_entry originalEntries[NROUTERS];
	memcpy((void*)originalEntries, (void*)m_entries, sizeof(m_entries));

	bool updatedDV = false;

	int intermediate = indexOf(source);
	if (m_entries_backup[intermediate].invalid())
	{
		m_entries_backup[intermediate].setValid();
		m_entries[intermediate].setValid();

		updatedDV = true;
	}

	// load advertised distance vector
	dv_entry advertisement[NROUTERS];
	memcpy((void*)advertisement, advertisementBuf, sizeof(advertisement));
 
	// recalculate self's distance vector
	for (int dest = 0; dest < NROUTERS; dest++)
	{
		if (dest == m_self)
			continue;
		bool updatedEntry = false;
		m_entries[dest].setCost(min(m_entries[dest].cost(), m_entries[intermediate].cost(), advertisement[dest].cost(), m_entries[dest].nexthopName(), source, updatedEntry));
		if (updatedEntry)
		{
			updatedDV = true;
			m_entries[dest].setNexthopPort(portNoOf(source));
			m_entries[dest].setNexthopName(source);
		}
	}
	m_entries[intermediate].setCost(advertisement[m_self].cost());

	if (updatedDV)
	{
		print(originalEntries, nameOf(m_self), "Change detected!\nRouting table before change", true);
		print(advertisement, source, "DV that caused the change", false);
		print(m_entries, nameOf(m_self), "Routing table after change", false);
	}
}

// return index of router
int DV::indexOf(char router) const
{
	return router - 'A';
}

// return name of indexed router
char DV::nameOf(int index) const
{
	return (char)index + 'A';
}

// return port number of router
int DV::portNoOf(char router)
{
	return m_portnos[router];
}

void DV::initMyaddr(int portno)
{
	memset((char *)&m_myaddr, 0, sizeof(m_myaddr));
	m_myaddr.sin_family = AF_INET;
	m_myaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	m_myaddr.sin_port = htons(portno);
}

void DV::startTimer(node &n)
{
	clock_gettime(CLOCK_MONOTONIC, &n.startTime);
}

bool DV::timerExpired(node &n) const
{
	timespec tend={0,0};
	clock_gettime(CLOCK_MONOTONIC, &tend);

	if (((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)n.startTime.tv_sec + 1.0e-9*n.startTime.tv_nsec) > 5)
		return true;
	else
		return false;
}

//-----------------
// HELPER FUNCTIONS
//-----------------

// return minimum cost and set updated flag
int DV::min(int originalCost, int selfToIntermediateCost, int intermediateToDestCost, char originalName, char newName, bool &updated) const {
	int newCost = selfToIntermediateCost + intermediateToDestCost;

	if (selfToIntermediateCost == -1 || intermediateToDestCost == -1)
	{
		return originalCost;
	}
	else if (originalCost == -1)
	{
		updated = true;
		return newCost;
	}
	else if (newCost < originalCost)
	{
		updated = true;
		return newCost;
	}
	else if (originalCost == newCost)
	{
		if (originalName <= newName)
			updated = false;
		else
			updated = true;
		return newCost;
	}
	else
	{
		return originalCost;
	}
}

// print a DV
// format: source, destination, port number of nexthop router, cost to destination
void DV::print(dv_entry dv[], char name, string msg, bool timestamp) const {
	if (timestamp)
	{
		time_t rawtime;
		time(&rawtime);
		cout << ctime(&rawtime);
	}
	cout << msg << ": " << name << endl;
	cout << "dst nexthop cost" << endl;
	for (int dest = 0; dest < NROUTERS; dest++)
	{
		cout << "  " << nameOf(dest) << "   ";
		if (dv[dest].nexthopPort() == -1)
			cout << "   ";
		cout << dv[dest].nexthopPort() << "   ";
		if (dv[dest].cost() != -1)
			cout << " ";
		cout << dv[dest].cost();
		cout << endl;
	}
	cout << endl;
}














//DV.cpp ends----------------------------------------------------------------------------------------









#define BUFSIZE 2048

using namespace std;

struct header
{
	int type;
	char source;
	char dest;
	int length;
};

enum type
{
	TYPE_DATA, TYPE_ADVERTISEMENT, TYPE_WAKEUP, TYPE_RESET
};

void *createPacket(int type, char source, char dest, int payloadLength, void *payload);
header getHeader(void *packet);
void *getPayload(void *packet, int length);
void multicast(DV &dv, int socketfd);
void selfcast(DV &dv, int socketfd, int type, char source = 0, char dest = 0, int payloadLength = 0, void *payload = 0);

int main(int argc, char **argv)
{
	// check for errors
	if (argc < 3)
	{
		perror("Not enough arguments.\nUsage: ./my_router <initialization file> <router name>\n");
		return 0;
	}

	DV dv(argv[1], argv[2]);

	vector<node> neighbors = dv.neighbors();

	int myPort = dv.portNoOf(argv[2][0]); // my port

	dv.initMyaddr(myPort);
	sockaddr_in myaddr = dv.myaddr();

	socklen_t addrlen = sizeof(sockaddr_in); // length of addresses

	// create a UDP socket
	int socketfd; // our socket
	if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("cannot create socket\n");
		return 0;
	}
	
	// bind the socket to localhost and myPort
	if (bind(socketfd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0)
	{
		perror("bind failed");
		return 0;
	}

	// send a data packet to router A
	if (dv.getName() == 'H')
	{
		
		char data[100];
		memset(data, 0, 100);
		cin.getline(data, 100);
		for (int i = 0; i < neighbors.size(); i++)
		{
			if (neighbors[i].name == 'A')
			{
				void *dataPacket = createPacket(TYPE_DATA, dv.getName(), 'D', strlen(data), (void*)data);
				sendto(socketfd, dataPacket, sizeof(header) + dv.getSize(), 0, (struct sockaddr *)&neighbors[i].addr, sizeof(sockaddr_in));

				// print info
				header h = getHeader(dataPacket);
				cout << "Sent data packet" << endl;
				cout << "Type: data" << endl;
				cout << "Source: " << h.source << endl;
				cout << "Destination: " << h.dest << endl;
				cout << "Length of packet: " << sizeof(header) + h.length << endl;
				cout << "Length of payload: " << h.length << endl;
				cout << "Payload: " << data << endl;

				free(dataPacket);
			}
		}
		exit(0);
	}

	// distance vector routing
	int pid = fork();
	if (pid < 0)
	{
		perror("fork failed");
		return 0;
	}
	else if (pid == 0) // send to each neighbor periodically
	{
		for (;;)
		{
			// periodically wake up parent process
			selfcast(dv, socketfd, TYPE_WAKEUP);
			sleep(1);
		}
	}
	else // listen for advertisements
	{
		void *rcvbuf = malloc(BUFSIZE);
		sockaddr_in remaddr;
		for (;;)
		{
			memset(rcvbuf, 0, BUFSIZE);
			int recvlen = recvfrom(socketfd, rcvbuf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
			
			header h = getHeader(rcvbuf);
			void *payload = getPayload(rcvbuf, h.length);
			switch(h.type)
			{
				case TYPE_DATA:{
					cout << "Received data packet" << endl;
					time_t rawtime;
					time(&rawtime);
					cout << "Timestamp: " << ctime(&rawtime);
					cout << "ID of source node: " << h.source << endl;
					cout << "ID of destination node: " << h.dest << endl;
					cout << "UDP port in which the packet arrived: " << myPort << endl;
					if (h.dest != dv.getName()) // only forward if this router is not the destination
					{
						if (dv.routeTo(h.dest).nexthopPort() == -1)
						{
							cout << "Error: packet could not be forwarded" << endl;
						}
						else
						{
							cout << "UDP port along which the packet was forwarded: " << dv.routeTo(h.dest).nexthopPort() << endl;
							cout << "ID of node that packet was forwarded to: " << dv.routeTo(h.dest).nexthopName() << endl;
							void *forwardPacket = createPacket(TYPE_DATA, h.source, h.dest, h.length, (void*)payload);
							for (int i = 0; i < neighbors.size(); i++)
							{
								if (neighbors[i].name == dv.routeTo(h.dest).nexthopName())
									sendto(socketfd, forwardPacket, sizeof(header) + dv.getSize(), 0, (struct sockaddr *)&neighbors[i].addr, sizeof(sockaddr_in));
							}
							free(forwardPacket);
						}
						cout << endl;
					}
					else
					{
						char data[100];
						memset(data, 0, 100);
						memcpy((void*)data, payload, h.length);
						cout << "Data payload: " << data << endl << endl;
					}}
					break;
				case TYPE_ADVERTISEMENT:{
					dv_entry entries[NROUTERS];
					memcpy((void*)entries, payload, h.length);
					for (int i = 0; i < neighbors.size(); i++)
					{
						if (neighbors[i].name == h.source)
							dv.startTimer(neighbors[i]);
					}
					dv.update(payload, h.source);}
					break;
				case TYPE_WAKEUP: // perform periodic tasks
					{for (int i = 0; i < neighbors.size(); i++)
					{
						node curNeighbor = neighbors[i];
						if ((dv.getEntries()[dv.indexOf(curNeighbor.name)].cost() != -1) && dv.timerExpired(neighbors[i]))
						{
							selfcast(dv, socketfd, TYPE_RESET, dv.getName(), neighbors[i].name, dv.getSize() / sizeof(dv_entry) - 2);
						}
					}
					multicast(dv, socketfd);}
					break;
				case TYPE_RESET:{
					int hopcount = (int)h.length - 1;
					dv.reset(h.dest);
					if (hopcount > 0)
					{
						void *forwardPacket = createPacket(TYPE_RESET, dv.getName(), h.dest, hopcount, (void*)0);
						for (int i = 0; i < neighbors.size(); i++)
						{
							if (neighbors[i].name != h.source)
								sendto(socketfd, forwardPacket, sizeof(header), 0, (struct sockaddr *)&neighbors[i].addr, sizeof(sockaddr_in));
						}
					}}
					break;
			}
		}
		free(rcvbuf);
	}
}

// create a packet with header and payload
void *createPacket(int type, char source, char dest, int payloadLength, void *payload)
{
	int allocatedPayloadLength = payloadLength;
	if ((type != TYPE_DATA) && (type != TYPE_ADVERTISEMENT))
		allocatedPayloadLength = 0;

	// create empty packet
	void *packet = malloc(sizeof(header)+allocatedPayloadLength);

	// create header
	header h;
	h.type = type;
	h.source = source;
	h.dest = dest;
	h.length = payloadLength;

	// fill in packet
	memcpy(packet, (void*)&h, sizeof(header));
	memcpy((void*)((char*)packet+sizeof(header)), payload, allocatedPayloadLength);

	return packet;
}

// extract the header from the packet
header getHeader(void *packet)
{
	header h;
	memcpy((void*)&h, packet, sizeof(header));
	return h;
}

// extract the payload from the packet
void *getPayload(void *packet, int length)
{
	void *payload = malloc(length);
	memcpy(payload, (void*)((char*)packet+sizeof(header)), length);
	return payload;
}

// multicast advertisement to all neighbors
void multicast(DV &dv, int socketfd)
{
	vector<node> neighbors = dv.neighbors();
	for (int i = 0; i < neighbors.size(); i++)
	{
		void *sendPacket = createPacket(TYPE_ADVERTISEMENT, dv.getName(), neighbors[i].name, dv.getSize(), (void*)dv.getEntries());
		sendto(socketfd, sendPacket, sizeof(header) + dv.getSize(), 0, (struct sockaddr *)&neighbors[i].addr, sizeof(sockaddr_in));
		free(sendPacket);
	}
}

// periodically wake yourself up to multicast advertisement
void selfcast(DV &dv, int socketfd, int type, char source, char dest, int payloadLength, void *payload)
{
	void *sendPacket = createPacket(type, source, dest, payloadLength, payload);
	sockaddr_in destAddr = dv.myaddr();
	sendto(socketfd, sendPacket, sizeof(header), 0, (struct sockaddr *)&destAddr, sizeof(sockaddr_in));
	free(sendPacket);
}

