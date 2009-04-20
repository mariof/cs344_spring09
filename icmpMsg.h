
void processICMP(const char* interface, const uint8_t* packet, unsigned len);
void processEchoReply(const uint8_t* packet, unsigned len);
void sendICMPEchoReply(const char* interface, const uint8_t* requestPacket, unsigned len);
void sendICMPDestinationUnreachable(const char* interface, const uint8_t* originalPacket, unsigned len, int code);
void sendICMPTimeExceeded(const char* interface, const uint8_t* originalPacket, unsigned len);
void sendICMPEchoRequest(const char* interface, uint32_t dstIP, uint16_t identifier, uint16_t seqNum);

uint16_t checksum(uint16_t* data, unsigned len);


struct pingRequestNode{
	time_t time;
	int fd;
	uint16_t identifier;
	uint16_t seqNum;
	struct pingRequestNode *next;
};

struct pingRequestNode *pingListHead;
pthread_mutex_t ping_lock;
