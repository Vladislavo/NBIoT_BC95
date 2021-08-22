#include "Arduino.h"

#include <NBIoT_BC95.h>

#define BC95_BAUDRATE               (9600)
#define PIN_ENABLE                  (1)

// debug serial
extern HardwareSerial Serial;
// module communication serial
extern HardwareSerial Serial1;

NBIoT_BC95 bc95(&Serial1, &Serial);

char dest_url[32] = "www.google.com";
char dest_ip[15]  = "";

void setup() {
    Serial.begin(BC95_BAUDRATE);

    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);

    bc95.initialize();
    bc95.config_psm();
}

void loop() {
    // check if the module is functional
    if (bc95.is_assigned_ip()) {
        // query dns and clean assiciated entry in the cache
        if(bc95.query_dns(dest_url, dest_ip) && bc95.flush_dns_cache(dest_url)) {
            Serial.printf("DNS query success : %s -> %s\r\n", dest_url, dest_ip);
        }
    }
    delay(1000);
}
