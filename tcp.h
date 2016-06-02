#include <stdint.h>
#include <vector>

class TCPHeader {
    public:
        TCPHeader(){
            seq_num = 0;
            ack_num = 0;
            wnd_size = 0;
            flags = 0;
        };
        
        TCPHeader(uint16_t seq, uint16_t ack, uint16_t wnd, uint16_t f) {
            seq_num = seq;
            ack_num = ack;
            wnd_size = wnd;
            flags = f;
        };
        
        std::vector<char> encode() const {
            std::vector<char> header;
            header.push_back(uint8_t(seq_num >> 8));     // hi
            header.push_back(uint8_t(seq_num & 0xFF));   // lo
            
            header.push_back(uint8_t(ack_num >> 8));
            header.push_back(uint8_t(ack_num & 0xFF));
            
            header.push_back(uint8_t(wnd_size >> 8));
            header.push_back(uint8_t(wnd_size & 0xFF));
            
            header.push_back(uint8_t(flags >> 8));
            header.push_back(uint8_t(flags & 0xFF));
            
            return header;
        };
        
        void decode (std::vector<char> header) {
            seq_num  = uint16_t(header[0]) << 8 | (uint16_t(header[1]) & 0xFF);
            ack_num  = uint16_t(header[2]) << 8 | (uint16_t(header[3]) & 0xFF);
            wnd_size = uint16_t(header[4]) << 8 | (uint16_t(header[5]) & 0xFF);
            flags    = uint16_t(header[6]) << 8 | (uint16_t(header[7]) & 0xFF);
        };
        
        uint16_t getSEQ() const { return seq_num; };
        
        uint16_t getACK() const { return ack_num; };
        
        uint16_t getWND() const { return wnd_size; };
        
        uint16_t getFlags() const { return flags; };
    
    private:
        uint16_t seq_num;
        uint16_t ack_num;
        uint16_t wnd_size;
        uint16_t flags; // A S F
                        // 0 = none set
                        // 1 = F
                        // 2 = S
                        // 3 = SF
                        // 4 = A
                        // 5 = AF
                        // 6 = AS
                        // 7 = ASF
};