
#include "tcp.h"

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*************** Sender Functions ***************/
TCP::TCP(char * hostname, char * hostUDPport)
{
	struct addrinfo hints, *servinfo, *p;
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(hostname, hostUDPport, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	if (servinfo == NULL) {
		fprintf(stderr, "talker: failed to resolve addr\n");
		exit(2);
	}

	sendAddr  = *(servinfo->ai_addr);
	sendAddrLen = servinfo->ai_addrlen;

	freeaddrinfo(servinfo);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(NULL, hostUDPport, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("listener: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "listener: failed to bind socket\n");
		exit(2);
	}

	freeaddrinfo(servinfo);

	// Initial time out estimation
	rtt.tv_sec = 1;
	rtt.tv_usec = 0;
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &rtt, sizeof(rtt)) < 0) {
		perror("setsockopt");
		exit(3);
	}

}

int TCP::receiveSynAck()
{
	struct sockaddr_storage receiverAddr;
    socklen_t receiverAddrLen = sizeof(receiverAddr);
	msg_header_t syn, syn_ack;

	syn.type = SYN_HEADER;
	int seqNum = 1;

	// TODO: RTT
	while(1){
		if(recvfrom(sockfd, (char *)&syn_ack, sizeof(msg_header_t), 0, (struct sockaddr*)&receiverAddr, &receiverAddrLen) == -1){
			syn.seqNum = htonl(seqNum++);
			sendto(sockfd, (char *)&syn, sizeof(msg_header_t), 0, &sendAddr, sendAddrLen);
		}
		else break;
	}
	// TODO: RTT

#ifdef DEBUG
	printf("SYN ACK received, with seqNum: %d\n", ntohl(syn_ack.seqNum));
#endif

	return syn_ack.seqNum;
}

void TCP::senderSetupConnection()
{
	// construct buffer
	msg_header_t syn;
	syn.type = SYN_HEADER;
	syn.seqNum = htonl(0);

	// send
	sendto(sockfd, (char *)&syn, sizeof(msg_header_t), 0, &sendAddr, sendAddrLen);

	// wait for ack + syn
	ack_packet_t ack;
	ack.type = ACK_HEADER;
	ack.seqNum = receiveSynAck();

	// send ack
	sendto(sockfd, (char *)&ack, sizeof(ack_packet_t), 0, &sendAddr, sendAddrLen);
}

void TCP::reliableSend(char * filename, unsigned long long int bytesToTransfer)
{
    buffer = new CircularBuffer(SWS, filename, bytesToTransfer);

	// Set up TCP connection
	senderSetupConnection();

	while(1){
		// fill
		buffer->fill();

		// send
		sendWindow();

		break;

		// wait for ack
		// TODO: increment sIdx
	}

	// tear down TCP connection
	senderTearDownConnection();

}

void TCP::senderTearDownConnection()
{

}

void TCP::sendWindow()
{
	size_t j = buffer->sIdx;
	for(size_t i = 0; i < buffer->data.size(); i++) {
		if(buffer->state[j] == filled){
			sendto(sockfd, (char *)&(buffer->data[j]), buffer->length[j], 0, &sendAddr, sizeof(sendAddr));
			buffer->state[j] = sent;
			j = (j + 1) % buffer->data.size();
		}
	}
}

/*************** Receiver Functions ***************/
TCP::TCP(char * hostUDPport)
{
	struct addrinfo hints, *servinfo, *p;
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(NULL, hostUDPport, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("listener: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "listener: failed to bind socket\n");
		exit(2);
	}

	freeaddrinfo(servinfo);
}

void TCP::receiverSetupConnection()
{
	int numbytes;
	struct sockaddr_storage their_addr;
	socklen_t addr_len;
	msg_header_t syn, syn_ack;
	msg_packet_t packet;
	addr_len = sizeof(their_addr);

	/********** receive SYN **********/
	if ((numbytes = recvfrom(sockfd, (char *)&syn, sizeof(msg_header_t) , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
		exit(1);
	}

#ifdef DEBUG
	printf("SYN RECEIVED, SEQ NUM: %d\n", ntohl(syn.seqNum));
#endif

	/********** Send SYN + ACK **********/
	syn_ack.type = SYN_ACK_HEADER;
	syn_ack.seqNum = syn.seqNum;
	if ((numbytes = sendto(sockfd, (char *)&syn_ack, sizeof(msg_header_t), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
		perror("talker: sendto");
		exit(1);
	}

	/********** receive ACK or MSG and treat it accordingly **********/
	if ((numbytes = recvfrom(sockfd, (char *)&packet, sizeof(msg_packet_t) , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
		exit(1);
	}

	if((uint)numbytes > sizeof(msg_header_t)){
		// write message into buffer if ACK lost and message seen first
		buffer->storeReceivedPacket(packet, numbytes);

#ifdef DEBUG
		((char *)&packet)[numbytes] = '\0';
		printf("Packet received %s\n", (char *)&packet.msg);
#endif
	}else{
#ifdef DEBUG
		printf("ACK RECEIVED, SEQ NUM: %d\n", ntohl(packet.header.seqNum));
#endif
	}
}

void TCP::reliableReceive(char * filename)
{
	buffer = new CircularBuffer(SWS, filename);

	// Set up TCP connection
	receiverSetupConnection();

	while(1){
		printf("GOT HERE\n\n\n");
		// receive
		receiveWindow();

		// send acks

		// flush
		buffer->flush();

	}

	// tear down TCP connection

}



void TCP::receiveWindow()
{
	int numbytes;
	struct sockaddr_storage their_addr;
	socklen_t addr_len;
	msg_packet_t packet;
	addr_len = sizeof(their_addr);

	if ((numbytes = recvfrom(sockfd, (char *)&packet, sizeof(msg_packet_t) , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("recvfrom");
		exit(1);
	}

	buffer->storeReceivedPacket(packet, numbytes);
}
