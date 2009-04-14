
void processICMP(const char* interface, const uint8_t* packet, unsigned len);
void sendICMPEchoReply(const char* interface, const uint8_t* requestPacket, unsigned len);
void sendICMPDestinationUnreachable(const char* interface, const uint8_t* originalPacket, unsigned len, int code);
void sendICMPTimeExceeded(const char* interface, const uint8_t* originalPacket, unsigned len);
