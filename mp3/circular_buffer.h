#ifndef SEND_BUFFER_H
#define SEND_BUFFER_H

#include "parameters.h"
#include "types.h"

class CircularBuffer
{
    public:
        // Constructor
        CircularBuffer(){}
        CircularBuffer(int size, char * filename, unsigned long long int bytesToSend);
        CircularBuffer(int size, char * filename);

        // sender member function
        bool fill();

        // receiver member function
        void flush();
        void storeReceivedPacket(msg_packet_t & packet, uint32_t packetLength);

        int sIdx;
        uint seqNum;
        vector<packet_state_t> state;
        vector<msg_packet_t> data;
        vector<uint32_t> length;
        unsigned long long int bytesToTransfer;

    private:
        ifstream sourcefile;
        ofstream destfile;
};


#endif
