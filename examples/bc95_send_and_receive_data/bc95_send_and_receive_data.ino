#include "Arduino.h"

#include <NBIoT_BC95.h>

#define BC95_BAUDRATE               (9600)
#define PIN_ENABLE                  (1)

#define BUFFER_SIZE                 (128)

// debug serial
extern HardwareSerial Serial;
// module communication serial
extern HardwareSerial Serial1;

NBIoT_BC95 bc95(&Serial1, &Serial);

char dest_ip[15]  = "127.0.0.1";
char dest_port    = 12321;

uint8_t send_buffer[BUFFER_SIZE];
uint8_t send_buffer_size = 0;

uint8_t receive_buffer[BUFFER_SIZE];
uint16_t receive_buffer_size = 0;

uint8_t sent_bytes      = 0;
uint16_t pending_bytes  = 0;

void setup() {
    Serial.begin(BC95_BAUDRATE);

    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);

    bc95.initialize();
    bc95.config_psm();

    // check if the module is functional
    if (bc95.is_assigned_ip()) {
        // open socket with random port number and receive incoming messages
        bc95.open_socket();
        // once socket is created it is not necessary to close it
    }
}

void loop() {
    sent_bytes = bc95.send_UDP_datagram(dest_ip, dest_port, send_buffer, send_buffer_size, &pending_bytes);
    Serial.printf("Sent bytes %u\r\n", sent_bytes);

    if (pending_bytes > 0) {
        bc95.receive_UDP_datagram(receive_buffer, &receive_buffer_size);

        Serial.printf("Received message of size %u stored in 'receive_buffer'\r\n", receive_buffer_size);
    }

    delay(1000);
}
