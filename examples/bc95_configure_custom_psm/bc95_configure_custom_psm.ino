#include "Arduino.h"

#include <NBIoT_BC95.h>

#define BC95_BAUDRATE               (9600)
#define PIN_ENABLE                  (1)

// debug serial
extern HardwareSerial Serial;
// module communication serial
extern HardwareSerial Serial1;

NBIoT_BC95 bc95(&Serial1, &Serial);

void bc95_config_custom_psm(void);


void setup() {
    Serial.begin(BC95_BAUDRATE);

    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);

    bc95.initialize();

    bc95_config_custom_psm();
}

void loop() {

}

void bc95_config_custom_psm(void) {
    bc95_psm_config_t psm_conf;
    psm_conf.psm_mode                            = BC95_PSM_MODE_ENABLED;

    // request operator sleep 10*7 hours
    tau_timer_t tau_timer;
    tau_timer.config.tau_multiple               = BC95_TAU_10_HOURS;
    tau_timer.config.tau_value                  = 7;

    // report operator 2*1 seconds active time
    active_time_timer_t at_timer;
    at_timer.config.active_time_multiple        = BC95_AT_2_SECONDS;
    at_timer.config.active_time_value           = 1;

    psm_conf.tau_timer_config                    = tau_timer;
    psm_conf.active_time_timer_config            = at_timer;

    bc95.config_psm(&psm_conf);
}
