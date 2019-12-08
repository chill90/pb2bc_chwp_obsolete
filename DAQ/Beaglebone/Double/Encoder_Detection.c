#include <stdlib.h>

// Shared addresses between counter and IRIG
// Address for shared variable 'on' to tell that the PRU is still sampling
#define ON_ADDRESS 0x00010000
// Address for shared variable 'counter_overflow' to count the overflows
#define OVERFLOW_ADDRESS 0x00010008

// Counter-specific addresses
// Address to where the packet identifier will be stored
#define ENCODER_READY_ADDRESS 0x00010010
// Address for Counter Packets to start being written to
#define ENCODER_ADDRESS 0x00010018

// Counter packet format data
// Counter header
#define ENCODER_HEADER 0x1EAF
// Size of edges to sample before sending packet
#define ENCODER_COUNTER_SIZE 150
// ~75% of max counter value
#define MAX_LOOP_TIME 0x5FFFFFFF

// IEP (Industrial Ethernet Peripheral) Registers
// IEP base address
#define IEP 0x0002e000
// Register IEP Timer configuration
#define IEP_TMR_GLB_CFG ((volatile unsigned long int *)(IEP + 0x00))
// Register to check for counter overflows
#define IEP_TMR_GLB_STS ((volatile unsigned long int *)(IEP + 0x04))
// Register to configure compensation counter
#define IEP_TMR_COMPEN ((volatile unsigned long int *)(IEP + 0x08))
// Register for the IEP counter (32-bit, 200MHz)
#define IEP_TMR_CNT ((volatile unsigned long int *)(IEP + 0x0c))

// Structure to sample PRU input and determine edges
struct ECAP {
    // Previous sample of the input register __R31
    unsigned long int p_sample;
    // Time stamp of edge seen
    unsigned long int ts;
};

// Structure to store clock count of edges and
// the number of times the counter has overflowed
struct EncoderInfo {
    unsigned long int header;
    unsigned long int quad;
    unsigned long int clock[ENCODER_COUNTER_SIZE];
    unsigned long int clock_overflow[ENCODER_COUNTER_SIZE];
    unsigned long int count[ENCODER_COUNTER_SIZE];
};

// Pointer to the 'on' variable
//volatile unsigned short int* on = (volatile unsigned short int *) ON_ADDRESS;
volatile unsigned long int* on = (volatile unsigned long int *) ON_ADDRESS;
// Pointer to the overflow variable
// Overflow variable is updated by IRIG code, incremented everytime the counter overflows
volatile unsigned long int* counter_overflow = (volatile unsigned long int *) OVERFLOW_ADDRESS;

// Pointer to packet identifier and overflow variable
//volatile unsigned short int* packet_ready = (volatile unsigned short int *) PACKET_READY_ADDRESS;
volatile unsigned long int* encoder_ready = (volatile unsigned long int *) ENCODER_READY_ADDRESS;
// Pointer to complete packet structure
volatile struct EncoderInfo* encoder_packets = (volatile struct EncoderInfo *) ENCODER_ADDRESS;

//  ***** LOCAL VARIABLES *****

// Variable to let PRU know that a quadrature sample
// is needed on rising edge
//unsigned short int quad_encoder_needed;
unsigned long int quad_encoder_needed;
// Variable to count number of edges seen
unsigned long int input_capture_count;
// Variable used to write to two different blocks of memory allocated
// to an individual instantiation of counter struct
unsigned long int i;
// Variable used to write to entirety of counter struct
// One struct contains ENCODER_COUNTER_STRUCT edges
unsigned long int x;
// Variable for sampling input registers
unsigned long int edge_sample;
unsigned long int quad_sample;
// Struct for storing captures
// Initialize ECAP struct to determine edges
volatile struct ECAP ECAP;
    
// Registers to use for PRU input/output
// __R31 is input, __R30 is output
volatile register unsigned int __R31, __R30;

int main(void) {
    // No edges counted to start
    input_capture_count = 0;

    // Clears Overflow Flags
    *IEP_TMR_GLB_STS = 1;
    // Enables IEP counter to increment by 1 every cycle
    *IEP_TMR_GLB_CFG = 0x11;
    // Disables compensation counter
    *IEP_TMR_COMPEN = 0;

    // Packet address is 0 when no counter packets are ready to be sent,
    // Otherwise, it's 1 or 2 depending on which packet is ready
    *encoder_ready = 0;

    // Initialize ECAP struct to determine edges
    volatile struct ECAP ECAP;
    // Previous sample
    ECAP.p_sample = 0;
    // Current time
    ECAP.ts = *IEP_TMR_CNT;

    // Reset the overflow variable when code starts
    *counter_overflow = 0;

    // Maintain two packets simultaneously, alternating between them
    // Write headers for packets
    encoder_packets[0].header = ENCODER_HEADER;
    encoder_packets[1].header = ENCODER_HEADER;

    // IRIG controls on variable
    // Once the IRIG code has sampled for a given time, it will set *on to 1
    while(*on == 0){
        // Alternate between packets
        i = 0;
        while(i < 2){
            // Only sample for quadrature once per packet
            quad_encoder_needed = 1;

            // Loop until packet is filled
            x = 0;
            while(x < ENCODER_COUNTER_SIZE){
                // Record new counter value if changed
		if ((__R31 & (1 << 10)) ^ ECAP.p_sample) {
                    // Stores new time stamp
                    ECAP.ts = *IEP_TMR_CNT;
		    // Stores timing sample
		    edge_sample = (__R31 & (1 << 10));
		    // Stores quad sample
		    quad_sample = (__R31  & (1 << 8));
                    // Stores current sample as previous sample
                    ECAP.p_sample = (__R31 & (1 << 10));
                    // Increments number of edges that have been detected
                    input_capture_count += 1;

                    // Record quadrature if needed and if a rising edge
                    if (edge_sample && quad_encoder_needed) {
                        // Reading of quadrature pin
                        encoder_packets[i].quad = (quad_sample >> 8);
                        // No more quadrature reading until next packet
                        quad_encoder_needed = 0;
                    }

                    // Write the data
                    // Stores time stamp
                    encoder_packets[i].clock[x] = ECAP.ts;
                    // Writes the number of overflows
                    encoder_packets[i].clock_overflow[x] =
                    *counter_overflow + ((*IEP_TMR_GLB_STS & 1) &&
                    (encoder_packets[i].clock[x] < MAX_LOOP_TIME));
                    // Writes number of edges detected
                    encoder_packets[i].count[x] = input_capture_count;

                    x += 1;
                }
            }
            // Sets packet identifier variable to 1 or 2 and to notify ARM a packet is ready
            *encoder_ready = (i + 1);
            
            i += 1;
        }
    }
    // Reset PRU input
    __R31 = 0x28;
    // Stop PRU data taking
    __halt();
}
