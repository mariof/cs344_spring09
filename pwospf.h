#ifndef PWOSPF_H
#define PWOSPF_H

#define ALLSPFRouters 0xe0000005 // 224.0.0.5 in nbo
#define HELLOINT 10
#define NEIGHBOR_TIMEOUT 3 // timeout time =  NEIGHBOR_TIMEOUT x HELLOINT
#define LSUINT 30

#define PWOSPF_HELLO_REFRESH 2 // check for hello timeout every PWOSPF_HELLO_REFRESH seconds

#define LSU_DEFAULT_TTL 64

#define AREA_ID 1

struct pwospf_router{
	uint32_t routerID;
	uint32_t areaID;
	uint16_t lsuint;
	struct pwospf_if* if_list; 
};

struct pwospf_neighbor{
	uint32_t id;
	uint32_t ip;
	time_t lastHelloTime;
	struct pwospf_neighbor* next;
};

struct pwospf_if{
	uint32_t ip;
	uint32_t netmask;
	uint16_t helloint;
	struct pwospf_if* next;
	struct pwospf_neighbor* neighbor_list;
	pthread_mutex_t neighbor_lock;		
};


void initPWOSPF(struct sr_instance* sr);
void pwospfSendHelloThread(void* arg);
void pwospfSendLSUThread(void* dummy);
void sendHello(uint32_t ifIP);
void sendLSU();
void processPWOSPF(const char* interface, uint8_t* packet, unsigned len);
struct pwospf_if* findPWOSPFif(struct pwospf_router* router, uint32_t ip);
struct pwospf_neighbor* findOSPFNeighbor(struct pwospf_if* interface, uint32_t ip);
void forwardLSUpacket(const char* incoming_if, uint8_t* packet, unsigned len);
void pwospfTimeoutHelloThread(void *dummy);
int findNeighbor(uint32_t routerID, char* if_name, uint32_t *ip);

#endif // PWOSPF_H
