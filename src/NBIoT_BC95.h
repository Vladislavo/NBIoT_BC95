#ifndef __NBIoT_BC95_H__
#define __NBIoT_BC95_H__

#include <Arduino.h>

/******* Defines *******/
#define BC95_MODULE_DEBUG                       (0)

#define BC95_MAX_PACKET_SIZE                    (255)
#define BC95_CONNECTION_TIMEOUT                 (30000)
#define BC95_READ_RESPONSE_TIMEOUT              (300)

// Power saving modes
enum bc95_psm_mode_t {
    BC95_PSM_MODE_DISABLED                                      = 0,  // no error repot
    BC95_PSM_MODE_ENABLED                                          ,  // automatic error report
    BC95_PSM_MODE_DISABLED_AND_DISCARD_CURRENT_CONFIG                 // discard current config and reset to default if exists
};

// T3412 timer value multiple
enum requested_periodic_tau_timer_multiple_t {
    BC95_TAU_10_MIN                                             = 0,
    BC95_TAU_1_HOUR                                                ,
    BC95_TAU_10_HOURS                                              ,
    BC95_TAU_2_SECONDS                                             ,
    BC95_TAU_30_SECONDS                                            ,
    BC95_TAU_1_MINUTE                                              ,
    BC95_TAU_320_HOURS                                             ,
    BC95_TAU_TIMER_DEACTIVATED
};

// T3324 timer value multiple
enum requested_active_time_timer_multiple_t {
    BC95_AT_2_SECONDS                                           = 0,
    BC95_AT_1_MINUTE                                               ,
    BC95_AT_6_MINUTES                                              ,  // 1 decihour
    BC95_AT_TIMER_DEACTIVATED                                   = 7
};

enum bc95_band_t {
    BC95_BAND_1                                                 = 1,
    BC95_BAND_3                                                 = 3,
    BC95_BAND_5                                                 = 5,
    BC95_BAND_8                                                 = 8,
    BC95_BAND_20                                                = 20,
    BC95_BAND_28                                                = 28
};

enum bc95_network_attachment_state_t {
    BC95_NETWORK_DETACH                                         = 0,
    BC95_NETWORK_ATTACH
};

enum bc95_modem_functionality_level_t {
    BC95_MODEM_FUNCIONALITY_LEVEL_MINIMUM                       = 0,
    BC95_MODEM_FUNCIONALITY_LEVEL_FULL
};

enum bc95_led_mode_t {
    BC95_LED_DISABLED = 0,
    BC95_LED_ENABLED
};

typedef union {
    struct {
        unsigned char tau_value:5;
        unsigned char tau_multiple:3;
    } config;
    unsigned char i;             // uint8_t access
} tau_timer_t;

typedef union {
    struct {
        unsigned char active_time_value:5;
        unsigned char active_time_multiple:3;
    } config;
    unsigned char i;             // uint8_t access
} active_time_timer_t;

typedef struct {
    bc95_psm_mode_t         psm_mode;
    tau_timer_t             tau_timer_config;
    active_time_timer_t     active_time_timer_config;
} bc95_psm_config_t;


class NBIoT_BC95 {

    public:

         /**
         * Class constructor
         * @param stream        [IN] Stream to be used to communicate with bc95 board
         * @param dbg           [IN] Stream to be used as output for debug
         */
        NBIoT_BC95(Stream *stream, Stream *dbg = NULL) : _stream(stream), _dbg(dbg) { }

        /*
         * Initialize modem. If psm is NULL, configuration is set to default values.
         * @return              0 on failure, 1 on success
         */
        uint8_t initialize(void);

        /******* Data Transmission Funcions *******/

        /*
         * Create UDP socket for data transmission.
         * @param  listen_port  [IN] Listen port.
         * @param  recv_msg     [IN] Enable receiving incoming messages (1 - should be received, 0 - should be ignored)
         * @return              0 on failure, 1 on success or if socket has been already created
         */
        uint8_t open_socket(const uint16_t listen_port = 0, const uint8_t recv_msg = 1);

        /*
         * Close UDP socket.
         * @return              0 on failure or if no socket was open, 1 on success
         */
        uint8_t close_socket(void);

        /*
         * Send UDP datagram.
         * @param  remote_host      [IN]  Remote host IP address
         * @param  remote_port      [IN]  Remote host port
         * @param  payload_out      [IN]  Byte buffer to be sent
         * @param  payload_out_size [IN]  Size of byte buffer
         * @param  bytes_pending    [OUT] Number of bytes to be received [if socket_create(..., recv_msg = 1)]
         * @param  response_timeout [IN]  Timeout to check response message
         * @return                  0 on failure, number of sent bytes on success
         */
        uint16_t send_UDP_datagram(
            const char *remote_host,
            const uint16_t remote_port,
            const uint8_t *payload_out,
            const uint16_t payload_out_size,
            uint16_t *bytes_pending = NULL,
            const uint32_t response_timeout = BC95_CONNECTION_TIMEOUT);

        /*
         * Receive UDP datagram.
         * @param  payload_out      [OUT] Data buffer for received data
         * @param  payload_out_size [OUT] Size of received data
         * @return                  0 on failure, 1 on success
         */
        uint8_t receive_UDP_datagram(uint8_t *payload_out, uint16_t *payload_out_size);

        /*
         * Ping remote host
         * @param  host            [IN] IP address of a remote host
         * @param  timeout         [IN] Ping timeout.
         * @return                 0 on failure, RTT (Round Trip Time) on success
         */
        uint16_t ping(const char *host, const uint32_t timeout = BC95_CONNECTION_TIMEOUT);

        /******* DNS-related Functions *******/

        /*
         * Request a DNS translation.
         * @param  host_url        [IN]  Host URL. IP addresses are also valid (host_url -> ip_address)
         * @param  ip_address      [OUT] Translated IP address.
         * @return                 0 on failure, RTT (Round Trip Time) on success
         */
        uint8_t query_dns(const char *host_url, char *ip_address);

        /*
         * Flush DNS buffer.
         * @param  host_url        [IN]  Host URL. If host_url != NULL, then flushes only memory for this entry, otherwise flushes all dns memory.
         * @param  ip_address      [OUT] Translated IP address.
         * @return                 0 on failure, RTT (Round Trip Time) on success
         */
        uint8_t flush_dns_cache(const char *host_url = NULL);

        /******* Modem Configuration Functions *******/

        /*
         * Configure Power Saving Mode
         * @param  psm_config      [IN] Pointer to PSM configuration structure
         * @return                 0 on failure, 1 on success
         */
        uint8_t config_psm(const bc95_psm_config_t *psm_config = NULL);

        /******* Network Configuration Functions *******/

        /*
         * Force attach to or detach from the nework
         * @param  state           [IN] Desired state
         * @return                 0 on failure, 1 on success
         */
        uint8_t force_network_attachment(const bc95_network_attachment_state_t state = BC95_NETWORK_ATTACH);

        /*
         * Set radio band
         * @param  bands           [IN] Radio frequency bands
         * @param  nbands          [IN] Amount of Radio frequency bands to set
         * @return                 0 on failure, 1 on success
         */
        uint8_t set_bands(const bc95_band_t *bands, const uint8_t nbands);

        /*
         * Set modem functionality
         * @param  level           [IN] Modem functionality level
         * @return                 0 on failure, 1 on success
         */
        uint8_t set_modem_functionality(const bc95_modem_functionality_level_t level = BC95_MODEM_FUNCIONALITY_LEVEL_FULL);

        /*
         * Set net LED mode
         * @param  mode            [IN] Net LED mode
         * @return                 0 on failure, 1 on success
         */
        uint8_t set_led_mode(const bc95_led_mode_t mode = BC95_LED_ENABLED);


        /* Checkers */

        /*
         * Check if UE registered in the movile networked
         * @return                 0 on false, 1 on true
         */
        uint8_t is_registered(void);

        /*
         * Check if UE attached to the packet domain service
         * @return                 0 on false, 1 on true
         */
        uint8_t is_attached(void);

        /*
         * Check if PSM is enabled on ME.
         * @return                 0 on false, 1 on true
         */
        uint8_t is_psm_enabled(void);

        /*
         * Check if IP address was assigned by the operator and device can transmit data.
         * @return                 0 on false, 1 on true
         */
        uint8_t is_assigned_ip(void);

        /******* Info getters *******/

        /*
         * Retrieves current date and time from the operator.
         * @param  date_and_time   [OUT] Pointer to buffer
         * @return                 0 on false, 1 on true
         */
        uint8_t get_current_date_and_time(char *date_and_time);

        /*
         * Get RSSI.
         * @return                 0 on failure, RSSI on success
         */
        int8_t get_signal_strength(void);

        /*
         * Get ME IP address.
         * @param  ip_address      [OUT] Pointer to buffer
         * @return                 0 on failure, 1 on success
         */
        uint8_t get_IP_address(char *ip_address);

        /*
         * Get UTC timestamp. Does not work properly at the moment
         * @param  epoch           [OUT] Pointer to buffer
         * @return                 0 on failure, 1 on success
         */
        // uint8_t get_epoch(uint32_t * epoch); // epoch conversion issue

        /*
         * Get IMEI
         * @param  imei            [OUT] Pointer to buffer
         * @return                 0 on failure, 1 on success
         */
        uint8_t get_IMEI(char *imei);

        /*
         * Get IMEI
         * @param  imei            [OUT] Pointer to buffer
         * @return                 0 on failure, 1 on success
         */
        uint8_t get_ICCID(char *iccid);

        /******* Misc Funcions *******/

        /*
         * Reboot module
         * @return                 0 on failure, 1 on success
         */
        uint8_t reboot(void);

    private:

        /* Command parser states */
        enum bc95_cmd_parser_state_t {
            START_CR    = 0,
            START_LF       ,
            PAYLOAD        ,
            END_LF
        };

        /* Serial */
        Stream * _stream;
        Stream * _dbg;

        /* socket open */
        uint8_t _open_soc = 0;

        uint8_t _is_init = 0;

        /* Communication with BC95 */
        uint8_t _send_command(const char  *cmd);
        uint8_t _send_command(const __FlashStringHelper *cmd);
        uint8_t _read_line(
                char *resonse_buffer,
                const uint16_t resonse_buffer_len,
                uint16_t *response_len = NULL,
                const uint32_t timeout = BC95_READ_RESPONSE_TIMEOUT);
        uint8_t _check_response(const char *response_buffer, const uint16_t response_len);
        uint8_t _wait_for_OK(const uint32_t timeout = BC95_READ_RESPONSE_TIMEOUT);
        uint8_t _ping_module(uint8_t times);
        void _flushInput(void);
};


#endif // __NBIoT_BC95_H__
