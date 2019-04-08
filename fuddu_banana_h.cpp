//fuddu banana h
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

//#include "Distance_vector.h"

//Distance_vector.h begins----------------------------------------------------------------------------------------



#define no_of_routers 6

struct dist_vec_records
{
public:	
	int nextport() const { return (invalid() ? -1 : next_port); }
	char nextname() const { return (invalid() ? '0' : next_name); }
	int cost() const { return (invalid() ? -1 : cost); }
	bool invalid() const { return not_valid; }

	void set_next_port(int n) { next_port = n; }
	void set_next_name(char n) { next_name = n; }
	void set_cost(int c) { cost = c; }
	void set_valid() { not_valid = false; }
	void set_invalid() { not_valid = true; }
//private hataya
	bool not_valid;
	int next_port; // port number of next hop router
	char next_name;
	int cost; // link cost to destination
};

struct node
{
	char name;
	int port_no;
	timespec initial_time;
	sockaddr_in addr;
};

class Distance_vector
{
public:
	//Distance_vector() {}
	Distance_vector(const char *filename, const char *self);
	//~Distance_vector() {}
	
	void reset(char bhand);
	dist_vec_records *get_entries() { return entries1; }
	int get_size() const { return sizeof(entries1); }
	char get_name() const { return name_of(self1); }
	void bellman_ford(const void *incoming_table, char src);
	dist_vec_records routeTo(const char dest) const { return entries1[index_of(dest)]; };
	std::vector<node> neighbours() const { return padosi; };
	int port_no_of(char router);
	char name_of(int index) const;
	int index_of(char router) const;
	void initial_addr(int port_no);
	sockaddr_in myaddr() const { return my_addr; }
	void initial_timer(node &n);
	bool time_khatam(node &n) const;
	int port() { return port_no_of(get_name()); }

    //private hataya
	// member variables
	int self1; // index of self
	int size1;
	dist_vec_records entries1[no_of_routers]; // each router's distance vectors
	dist_vec_records entries1_backup[no_of_routers]; // initial distance vectors (for resetting)
	vector<node> padosi; // port numbers of self's neighbours
	sockaddr_in my_addr;
	map<char, int> port_nos1;

	// helper functions
	int min(int original_cost, int self_via_cost, int via_desti_cost, char original_name, char new_name, bool &updated) const;
	void print(dist_vec_records dv[], char name, std::string msg, bool timestamp) const;
};











//Distance_vector.h finished----------------------------------------------------------------------------------------





















//Distance_vector.cpp begins----------------------------------------------------------------------------------------






Distance_vector::Distance_vector(const char *filename, const char *self)
{
	fstream topology(filename);

	string line; // current line of file
	string field; // current token (to be put into entry's field)
	char my_name = self[0]; // name of self
	self1 = index_of(self[0]);

	// initialize entries1
	for (int dest = 0; dest < no_of_routers; dest++)
	{
		entries1[dest].set_next_name('0');
		entries1[dest].set_next_port(-1);
		entries1[dest].set_cost(-1);
		entries1[dest].set_valid();
	}

	while (getline(topology, line)) // parse file line by line
	{
		stringstream linestream(line);
		dist_vec_records entry;

		entry.set_valid();

		// source router
		getline(linestream, field, ',');
		char name = field[0];

		// destination router
		getline(linestream, field, ',');
		int dest = index_of(field[0]);
		node n;
		n.name = field[0];
		entry.set_next_name(field[0]);

		// destination port number
		getline(linestream, field, ',');
		int port = atoi(field.c_str());
		entry.set_next_port(port);
		n.port_no = port;

		memset((char *)&n.addr, 0, sizeof(n.addr));
		n.addr.sin_family = AF_INET;
		n.addr.sin_addr.s_addr = inet_addr("127.0.0.1");
		n.addr.sin_port = htons(port);

		// link cost
		getline(linestream, field, ',');
		entry.set_cost(atoi(field.c_str()));

		if (my_name == 'H')
		{
			int i;
			for (i = 0; i < padosi.size(); i++)
			{
				if (padosi[i].name == n.name)
					break;
			}
			if (i == padosi.size())
				padosi.push_back(n);
		}
		else if (name == my_name)
		{
			initial_timer(n);
			padosi.push_back(n); // store neighbor
			entries1[dest] = entry;
		}

		port_nos1[n.name] = n.port_no;
	}

	// special port number for sending data packet
	port_nos1['H'] = 11111;

	memcpy((void*)entries1_backup, (void*)entries1, sizeof(entries1));

	if (name_of(self1) != 'H')
		print(entries1, name_of(self1), "Initial routing table", true);
}

void Distance_vector::reset(char bhand)
{
	for (int i = 0; i < padosi.size(); i++)
	{
		if (padosi[i].name == bhand)
		{
			if (entries1_backup[index_of(bhand)].cost() != -1)
				entries1_backup[index_of(bhand)].set_invalid();
		}
	}
	memcpy((void*)entries1, (void*)entries1_backup, sizeof(entries1));
	print(entries1, name_of(self1), "Reset routing table", true);
}

// update this router's distance vector based on received incoming_table from source
// return false if this router's distance vector was not changed
void Distance_vector::bellman_ford(const void *incoming_table_buffer, char source)
{
	dist_vec_records original_entries[no_of_routers];
	memcpy((void*)original_entries, (void*)entries1, sizeof(entries1));

	bool new_distance_vector = false;

	int intermediate = index_of(source);
	if (entries1_backup[intermediate].invalid())
	{
		entries1_backup[intermediate].set_valid();
		entries1[intermediate].set_valid();

		new_distance_vector = true;
	}

	// load advertised distance vector
	dist_vec_records incoming_table[no_of_routers];
	memcpy((void*)incoming_table, incoming_table_buffer, sizeof(incoming_table));
 
	// recalculate self's distance vector
	for (int dest = 0; dest < no_of_routers; dest++)
	{
		if (dest == self1)
			continue;
		bool new_entry = false;
		entries1[dest].set_cost(min(entries1[dest].cost(), entries1[intermediate].cost(), incoming_table[dest].cost(), entries1[dest].nextname(), source, new_entry));
		if (new_entry)
		{
			new_distance_vector = true;
			entries1[dest].set_next_port(port_no_of(source));
			entries1[dest].set_next_name(source);
		}
	}
	entries1[intermediate].set_cost(incoming_table[self1].cost());

	if (new_distance_vector)
	{
		print(original_entries, name_of(self1), "Change detected!\nRouting table before change", true);
		print(incoming_table, source, "Distance_vector that caused the change", false);
		print(entries1, name_of(self1), "Routing table after change", false);
	}
}

// return index of router
int Distance_vector::index_of(char router) const
{
	return router - 'A';
}

// return name of indexed router
char Distance_vector::name_of(int index) const
{
	return (char)index + 'A';
}

// return port number of router
int Distance_vector::port_no_of(char router)
{
	return port_nos1[router];
}

void Distance_vector::initial_addr(int port_no)
{
	memset((char *)&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	my_addr.sin_port = htons(port_no);
}

void Distance_vector::initial_timer(node &n)
{
	clock_gettime(CLOCK_MONOTONIC, &n.initial_time);
}

bool Distance_vector::time_khatam(node &n) const
{
	timespec tend={0,0};
	clock_gettime(CLOCK_MONOTONIC, &tend);

	if (((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)n.initial_time.tv_sec + 1.0e-9*n.initial_time.tv_nsec) > 5)
		return true;
	else
		return false;
}

//-----------------
// HELPER FUNCTIONS
//-----------------

// return minimum cost and set updated flag
int Distance_vector::min(int original_cost, int self_via_cost, int via_desti_cost, char original_name, char new_name, bool &updated) const {
	int new_cost = self_via_cost + via_desti_cost;

	if (self_via_cost == -1 || via_desti_cost == -1)
	{
		return original_cost;
	}
	else if (original_cost == -1)
	{
		updated = true;
		return new_cost;
	}
	else if (new_cost < original_cost)
	{
		updated = true;
		return new_cost;
	}
	else if (original_cost == new_cost)
	{
		if (original_name <= new_name)
			updated = false;
		else
			updated = true;
		return new_cost;
	}
	else
	{
		return original_cost;
	}
}

// print a Distance_vector
// format: source, destination, port number of nexthop router, cost to destination
void Distance_vector::print(dist_vec_records dv[], char name, string msg, bool timestamp) const {
	if (timestamp)
	{
		time_t basic_time;
		time(&basic_time);
		cout << ctime(&basic_time);
	}
	cout << msg << ": " << name << endl;
	cout << "dst nexthop cost" << endl;
	for (int dest = 0; dest < no_of_routers; dest++)
	{
		cout << "  " << name_of(dest) << "   ";
		if (dv[dest].nextport() == -1)
			cout << "   ";
		cout << dv[dest].nextport() << "   ";
		if (dv[dest].cost() != -1)
			cout << " ";
		cout << dv[dest].cost();
		cout << endl;
	}
	cout << endl;
}














//Distance_vector.cpp ends----------------------------------------------------------------------------------------









#define buffer_length 2048

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
	datatype, incoming_tabletype, check_waketype, resettype
};

void *create_packet(int type, char source, char dest, int payload_length, void *payload);
header get_header(void *packet);
void *get_length(void *packet, int length);
void send_to_all(Distance_vector &dv, int socketfd);
void repeated_check(Distance_vector &dv, int socketfd, int type, char source = 0, char dest = 0, int payload_length = 0, void *payload = 0);

int main(int argc, char **argv)
{
	// check for errors
	if (argc < 3)
	{
		perror("Not enough arguments.\nUsage: ./my_router <initialization file> <router name>\n");
		return 0;
	}

	Distance_vector dv(argv[1], argv[2]);

	vector<node> neighbours = dv.neighbours();

	int myPort = dv.port_no_of(argv[2][0]); // my port

	dv.initial_addr(myPort);
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
	if (dv.get_name() == 'H')
	{
		
		char data[100];
		memset(data, 0, 100);
		cin.getline(data, 100);
		for (int i = 0; i < neighbours.size(); i++)
		{
			if (neighbours[i].name == 'A')
			{
				void *dataPacket = create_packet(datatype, dv.get_name(), 'D', strlen(data), (void*)data);
				sendto(socketfd, dataPacket, sizeof(header) + dv.get_size(), 0, (struct sockaddr *)&neighbours[i].addr, sizeof(sockaddr_in));

				// print info
				header h = get_header(dataPacket);
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
			repeated_check(dv, socketfd, check_waketype);
			sleep(1);
		}
	}
	else // listen for incoming_tables
	{
		void *rcvbuf = malloc(buffer_length);
		sockaddr_in remaddr;
		for (;;)
		{
			memset(rcvbuf, 0, buffer_length);
			int recvlen = recvfrom(socketfd, rcvbuf, buffer_length, 0, (struct sockaddr *)&remaddr, &addrlen);
			
			header h = get_header(rcvbuf);
			void *payload = get_length(rcvbuf, h.length);
			switch(h.type)
			{
				case datatype:{
					cout << "Received data packet" << endl;
					time_t basic_time;
					time(&basic_time);
					cout << "Timestamp: " << ctime(&basic_time);
					cout << "ID of source node: " << h.source << endl;
					cout << "ID of destination node: " << h.dest << endl;
					cout << "UDP port in which the packet arrived: " << myPort << endl;
					if (h.dest != dv.get_name()) // only forward if this router is not the destination
					{
						if (dv.routeTo(h.dest).nextport() == -1)
						{
							cout << "Error: packet could not be forwarded" << endl;
						}
						else
						{
							cout << "UDP port along which the packet was forwarded: " << dv.routeTo(h.dest).nextport() << endl;
							cout << "ID of node that packet was forwarded to: " << dv.routeTo(h.dest).nextname() << endl;
							void *forwardPacket = create_packet(datatype, h.source, h.dest, h.length, (void*)payload);
							for (int i = 0; i < neighbours.size(); i++)
							{
								if (neighbours[i].name == dv.routeTo(h.dest).nextname())
									sendto(socketfd, forwardPacket, sizeof(header) + dv.get_size(), 0, (struct sockaddr *)&neighbours[i].addr, sizeof(sockaddr_in));
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
				case TYPE_ADistance_vectorERTISEMENT:{
					dist_vec_records entries[no_of_routers];
					memcpy((void*)entries, payload, h.length);
					for (int i = 0; i < neighbours.size(); i++)
					{
						if (neighbours[i].name == h.source)
							dv.initial_timer(neighbours[i]);
					}
					dv.bellman_ford(payload, h.source);}
					break;
				case check_waketype: // perform periodic tasks
					{for (int i = 0; i < neighbours.size(); i++)
					{
						node curNeighbor = neighbours[i];
						if ((dv.get_entries()[dv.index_of(curNeighbor.name)].cost() != -1) && dv.time_khatam(neighbours[i]))
						{
							repeated_check(dv, socketfd, resettype, dv.get_name(), neighbours[i].name, dv.get_size() / sizeof(dist_vec_records) - 2);
						}
					}
					send_to_all(dv, socketfd);}
					break;
				case resettype:{
					int hopcount = (int)h.length - 1;
					dv.reset(h.dest);
					if (hopcount > 0)
					{
						void *forwardPacket = create_packet(resettype, dv.get_name(), h.dest, hopcount, (void*)0);
						for (int i = 0; i < neighbours.size(); i++)
						{
							if (neighbours[i].name != h.source)
								sendto(socketfd, forwardPacket, sizeof(header), 0, (struct sockaddr *)&neighbours[i].addr, sizeof(sockaddr_in));
						}
					}}
					break;
			}
		}
		free(rcvbuf);
	}
}

// create a packet with header and payload
void *create_packet(int type, char source, char dest, int payload_length, void *payload)
{
	int allocatedPayloadLength = payload_length;
	if ((type != datatype) && (type != TYPE_ADistance_vectorERTISEMENT))
		allocatedPayloadLength = 0;

	// create empty packet
	void *packet = malloc(sizeof(header)+allocatedPayloadLength);

	// create header
	header h;
	h.type = type;
	h.source = source;
	h.dest = dest;
	h.length = payload_length;

	// fill in packet
	memcpy(packet, (void*)&h, sizeof(header));
	memcpy((void*)((char*)packet+sizeof(header)), payload, allocatedPayloadLength);

	return packet;
}

// extract the header from the packet
header get_header(void *packet)
{
	header h;
	memcpy((void*)&h, packet, sizeof(header));
	return h;
}

// extract the payload from the packet
void *get_length(void *packet, int length)
{
	void *payload = malloc(length);
	memcpy(payload, (void*)((char*)packet+sizeof(header)), length);
	return payload;
}

// send_to_all incoming_table to all neighbours
void send_to_all(Distance_vector &dv, int socketfd)
{
	vector<node> neighbours = dv.neighbours();
	for (int i = 0; i < neighbours.size(); i++)
	{
		void *sendPacket = create_packet(TYPE_ADistance_vectorERTISEMENT, dv.get_name(), neighbours[i].name, dv.get_size(), (void*)dv.get_entries());
		sendto(socketfd, sendPacket, sizeof(header) + dv.get_size(), 0, (struct sockaddr *)&neighbours[i].addr, sizeof(sockaddr_in));
		free(sendPacket);
	}
}

// periodically wake yourself up to send_to_all incoming_table
void repeated_check(Distance_vector &dv, int socketfd, int type, char source, char dest, int payload_length, void *payload)
{
	void *sendPacket = create_packet(type, source, dest, payload_length, payload);
	sockaddr_in destAddr = dv.myaddr();
	sendto(socketfd, sendPacket, sizeof(header), 0, (struct sockaddr *)&destAddr, sizeof(sockaddr_in));
	free(sendPacket);
}

//fuddu bana diya