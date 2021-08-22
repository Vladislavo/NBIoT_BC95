#include <NBIoT_BC95.h>

/******* Defines *******/
#define BC95_DEFAULT_REBOOT_TIMEOUT         (10000)
#define BC95_DEFAULT_CFUN_RESPONSE_TIMEOUT  (10000)

#define BC95_MIN_RSP_BUF_LEN                (16)
#define BC95_MIN_CMD_BUF_LEN                (40)

/* BC95_MAX_PACKET_SIZE * 2 (payload HEX representation) */
#define BC95_NSOST_BUFFER_LEN               (BC95_MAX_PACKET_SIZE << 1)
#define BC95_NSORF_BUFFER_LEN               (BC95_MAX_PACKET_SIZE << 1)
#define BC95_NSORF_MAX_BUFFER_LEN           (1358) // real max 1358 (Note: See BC95 AT Commands Manual)

// BC95::Modem::readResponse() return values
enum bc95_response_type_t {
    BC95_RESPONSE_TYPE_DATA                                    = 0,
    BC95_RESPONSE_TYPE_OK                                         ,
    BC95_RESPONSE_TYPE_ERROR                                      ,
    BC95_RESPONSE_TYPE_TIMEOUT                                    ,
    BC95_RESPONSE_TYPE_UNKNOWN
};

// EPS Network Registration Status
enum bc95_network_stat_t {
    BC95_NETWORK_STAT_NOT_REGISTERED                           = 0,
    BC95_NETWORK_STAT_REGISTERED_HOME_NETWORK                     ,
    BC95_NETWORK_STAT_SEARCHING                                   ,
    BC95_NETWORK_STAT_REGISTRATION_DENIED                         ,
    BC95_NETWORK_STAT_UNKNOWN                                     ,
    BC95_NETWORK_STAT_REGISTERED_ROAMING
};

/***** Utility Functions Definitions *****/

uint8_t _is_valid_listen_port(uint16_t port);

uint8_t _hex_char_to_int(const char c);
uint8_t inline _get_bit(uint32_t num, uint8_t bit);
uint32_t _hr_time_2_epoch(char *hr_time_str); // convert human readable time to epoch

/***** BC95 Modem Public Functions *****/

uint8_t NBIoT_BC95::initialize(void) {
    _flushInput();

    if (_ping_module(5)) {
        // set echo off, led on, error report off, automatic network autoconnect on
        _is_init  = _send_command(F("ATE0"))                        && _wait_for_OK();
        _is_init &= _send_command(F("AT+NCONFIG=autoconnect,true")) && _wait_for_OK();
        _is_init &= _send_command(F("AT+CSCON=0"))                  && _wait_for_OK();
        _is_init &= _send_command(F("AT+QLEDMODE=0"))               && _wait_for_OK();
        delay(5000);
        _is_init &= set_modem_functionality();

        #if BC95_DEBUG_MODE > 0
            _send_command(F("AT+CMEE=1"));
            _wait_for_OK();
        #endif
    }

    return _is_init;
}

/******* Data Transmission Funcions *******/

uint8_t NBIoT_BC95::open_socket(const uint16_t listen_port, const uint8_t recv_msg) {
    if (_ping_module(5)) {
        if (!_open_soc && is_assigned_ip() && _is_valid_listen_port(listen_port)) {
            char response_buffer[BC95_MIN_RSP_BUF_LEN];
            char command[BC95_MIN_CMD_BUF_LEN];
            uint16_t resp_buf_len = 0;

            sprintf_P(command, (PGM_P)F("AT+NSOCR=DGRAM,17,%u,%u"), listen_port, recv_msg);

            _send_command(command);

            if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len) &&
               (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
            {
                _open_soc = 1;
            }
        }
    }

    return _open_soc;
}

uint8_t NBIoT_BC95::close_socket(void) {
    uint8_t ret = 0;

    _send_command(F("AT+NSOCL=1"));

    if(_wait_for_OK()){
        _open_soc = 0;
        ret = 1;
    }

    return ret;
}

uint16_t NBIoT_BC95::send_UDP_datagram(
        const char *remote_host,
        const uint16_t remote_port,
        const uint8_t *payload_out,
        const uint16_t payload_out_size,
        uint16_t *bytes_pending,
        const uint32_t response_timeout)
{
    uint16_t bytes_sent = 0;
    uint16_t resp_buf_len = 0;
    if (bytes_pending != NULL) {
        *bytes_pending = 0;
    }

    if (_ping_module(5)) {
        if (_is_init && _open_soc && is_registered() && is_attached()) {
            char response_buffer[BC95_MIN_RSP_BUF_LEN];
            char command_buffer[BC95_NSOST_BUFFER_LEN +BC95_MIN_CMD_BUF_LEN];
            char hbyte[3], *pbytes;

            sprintf_P(command_buffer, (PGM_P)F("AT+NSOST=1,%s,%u,%u,"), remote_host, remote_port, payload_out_size);

            for (uint16_t i = 0; i < payload_out_size; i++) {
                sprintf_P(hbyte, (PGM_P)F("%02X"), payload_out[i]);
                strcat(command_buffer, hbyte);
            }

            _send_command(command_buffer);

            if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len) &&
               (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
            {
                pbytes = strstr_P(response_buffer, (PGM_P)F(","));
                bytes_sent = strtoul(++pbytes, NULL, 10);
                // msg received. check incoming data
                memset(response_buffer, 0x0, BC95_MIN_RSP_BUF_LEN);
                if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len, response_timeout) &&
                   (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA))
                {
                    if (bytes_pending != NULL) {
                        pbytes = strstr_P(response_buffer, (PGM_P)F(","));
                        *bytes_pending = strtoul(++pbytes, NULL, 10);
                    }
                }
            }
        }
    }

    return bytes_sent;
}

uint8_t NBIoT_BC95::receive_UDP_datagram(uint8_t *payload_out, uint16_t *payload_out_size) {
    uint8_t ret = 0;
    if (payload_out_size != NULL) {
        *payload_out_size = 0;
    }
    if (_ping_module(5)) {
        if (_is_init && _open_soc && is_registered() && is_attached()) {
            char command[BC95_MIN_CMD_BUF_LEN];
            // 40 bytes for additional parameters like ip_address, socket_cid, etc.
            char receive_buffer[BC95_NSORF_BUFFER_LEN +BC95_MIN_CMD_BUF_LEN];
            char* pchr;
            uint16_t resp_buf_len = 0;

            uint16_t pload_ind = 0, payload_in_size = 0;

            sprintf_P(command, (PGM_P)F("AT+NSORF=1,%u"), BC95_MAX_PACKET_SIZE);

            _send_command(command);

            if (_read_line(receive_buffer, BC95_NSORF_BUFFER_LEN +64, &resp_buf_len) &&
               (_check_response(receive_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
            {
                // soc_cid
                strtok_P(receive_buffer, (PGM_P)F(","));
                for(uint8_t i = 0; i < 4; i++) {
                    // ip_addr, port, payload_len, payload
                    pchr = strtok_P(NULL, (PGM_P)F(","));
                    if (i == 2) {
                        payload_in_size = strtoul(pchr, NULL, 10);
                    }
                }

                for (uint16_t i = 0; i < payload_in_size; i++) {
                    pload_ind = i << 1;
                    payload_out[i] = (_hex_char_to_int(pchr[pload_ind]) << 4) | _hex_char_to_int(pchr[pload_ind+1]);
                }

                if (payload_out_size != NULL) {
                    *payload_out_size = payload_in_size;
                }

                ret = 1;
            }
        }
    }

    return ret;
}

uint16_t NBIoT_BC95::ping(const char *host, const uint32_t timeout) {
    uint16_t rtt = 0;

    if (_ping_module(5)) {
        if (_is_init && is_registered() && is_attached()) {
            char response_buffer[BC95_MIN_RSP_BUF_LEN*2];
            char command[BC95_MIN_CMD_BUF_LEN];
            const char *pchr;
            uint16_t resp_buf_len = 0;

            sprintf_P(command, (PGM_P)F("AT+NPING=%s"), host);

            _send_command(command);

            if (_wait_for_OK() && _read_line(response_buffer, sizeof(response_buffer), &resp_buf_len, BC95_CONNECTION_TIMEOUT) &&
               (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA))
            {
                pchr = strrchr(response_buffer, ',');
                rtt = strtoul(++pchr, NULL, 10);
            }
        }
    }

    return rtt;
}

/******* DNS-related Functions *******/

uint8_t NBIoT_BC95::query_dns(const char *host_url, char *ip_address) {
    uint8_t ret = 0;

    if (_is_init && is_registered() && is_attached()) {
        char command[BC95_MIN_CMD_BUF_LEN];
        char response_buffer[BC95_MIN_CMD_BUF_LEN];
        char *pchr;
        uint16_t resp_buf_len = 0;

        sprintf_P(command, (PGM_P)F("AT+QDNS=0,%s"), host_url);

        _send_command(command);

        if (_wait_for_OK() && _read_line(response_buffer, BC95_MIN_CMD_BUF_LEN, &resp_buf_len, BC95_CONNECTION_TIMEOUT) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA))
        {
            pchr = strstr_P(response_buffer, (PGM_P)F(":"));
            strcpy(ip_address, ++pchr);

            ret = 1;
        }
    }

    return ret;
}

uint8_t NBIoT_BC95::flush_dns_cache(const char *host_url) {
    char command[BC95_MIN_CMD_BUF_LEN];

    if (host_url == NULL) {
        strcpy_P(command, (PGM_P)F("AT+QDNS=1"));
    } else {
        sprintf_P(command, (PGM_P)F("AT+QDNS=1,%s"), host_url);
    }

    _send_command(command);

    return _wait_for_OK();
}

/******* Modem Configuration Functions *******/

uint8_t NBIoT_BC95::config_psm(const bc95_psm_config_t *psm_config) {
    char command[BC95_MIN_CMD_BUF_LEN];
    bc95_psm_config_t pconfig;

    if (psm_config == NULL) {
        // set up psm default config
        memset(&pconfig, 0x0, sizeof(bc95_psm_config_t));

        pconfig.psm_mode                            = BC95_PSM_MODE_ENABLED;

        // request operator sleep 10*7 hours
        tau_timer_t tau_timer;
        tau_timer.config.tau_multiple               = BC95_TAU_10_HOURS;
        tau_timer.config.tau_value                  = 7;

        // report operator 2*2 seconds active time
        active_time_timer_t at_timer;
        at_timer.config.active_time_multiple        = BC95_AT_2_SECONDS;
        at_timer.config.active_time_value           = 2;

        pconfig.tau_timer_config                    = tau_timer;
        pconfig.active_time_timer_config            = at_timer;

        psm_config = &pconfig;
    }

    sprintf_P(command, (PGM_P)F("AT+CPSMS=%u,,,"), psm_config->psm_mode);
    char bit[2];
    int8_t i;

    bit[1] = '\0';
    for (i = 7; i >= 0; i--) {
        bit[0] = '0' + _get_bit(psm_config->tau_timer_config.i, i);
        strcat(command, bit);
    }

    strcat_P(command, (PGM_P)F(","));

    for (i = 7; i >= 0; i--) {
        bit[0] = '0' + _get_bit(psm_config->active_time_timer_config.i, i);
        strcat(command, bit);
    }

    _send_command(command);

    return _wait_for_OK();
}

/******* Network Configuration Functions *******/

uint8_t NBIoT_BC95::force_network_attachment(const bc95_network_attachment_state_t state) {
    uint8_t ret = 0;

    if(_is_init) {
        char command[BC95_MIN_CMD_BUF_LEN];

        if((state == BC95_NETWORK_DETACH && !is_attached()) || (state == BC95_NETWORK_ATTACH && is_attached())) {
            ret = 1;
        } else {
            sprintf_P(command, (PGM_P)F("AT+CGATT=%u"), state == BC95_NETWORK_ATTACH);

            _send_command(command);
            ret = _wait_for_OK();
        }
    }

    return ret;
}

uint8_t NBIoT_BC95::set_bands(const bc95_band_t *bands, const uint8_t nbands) {
    uint8_t ret = 0;
    // Needs to be executed when CFUN=0. Note: See AT Commands Manual
    if (set_modem_functionality(BC95_MODEM_FUNCIONALITY_LEVEL_MINIMUM)) {
        char command[BC95_MIN_CMD_BUF_LEN] = "AT+NBAND=";
        char tmp[3];

        for(uint8_t i = 0; i < nbands; i++) {
            sprintf_P(tmp, (PGM_P)F("%u,"), bands[i]);
            strcat(command, tmp);
        }

        command[strlen(command)-1] = '\0';
        _send_command(command);

        ret = _wait_for_OK() && set_modem_functionality(BC95_MODEM_FUNCIONALITY_LEVEL_FULL);
    }

    return ret;
}

uint8_t NBIoT_BC95::set_modem_functionality(const bc95_modem_functionality_level_t level) {
    char command[BC95_MIN_CMD_BUF_LEN];

    sprintf_P(command, (PGM_P)F("AT+CFUN=%u"), level);

    _send_command(command);

    return _wait_for_OK(BC95_DEFAULT_CFUN_RESPONSE_TIMEOUT);
}

uint8_t NBIoT_BC95::set_led_mode(const bc95_led_mode_t mode) {
    char command[BC95_MIN_CMD_BUF_LEN];

    sprintf_P(command, (PGM_P)F("AT+QLEDMODE=%u"), mode);

    _send_command(command);

    return _wait_for_OK();
}


/* Checkers */

uint8_t NBIoT_BC95::is_registered(void) {
    char response_buffer[BC95_MIN_RSP_BUF_LEN];
    char *pchr;
    uint16_t resp_buf_len = 0;

    int net_state = 0;

    _send_command(F("AT+CEREG?"));

    if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len) &&
       (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
    {
        pchr = strstr_P(response_buffer, (PGM_P)F(","));
        net_state = strtoul(++pchr, NULL, 10);
    }

    return (net_state == BC95_NETWORK_STAT_REGISTERED_HOME_NETWORK) ||
           (net_state == BC95_NETWORK_STAT_REGISTERED_ROAMING);
}

uint8_t NBIoT_BC95::is_attached(void) {
    int attached = 0;

    if(_is_init) {
        char response_buffer[BC95_MIN_RSP_BUF_LEN];
        char *pchr;
        uint16_t resp_buf_len = 0;

        _send_command(F("AT+CGATT?"));

        if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
        {
            pchr = strstr_P(response_buffer, (PGM_P)F(":"));
            attached = strtoul(++pchr, NULL, 10);
        }
    }

    return attached;
}

uint8_t NBIoT_BC95::is_psm_enabled(void) {
    char response_buffer[BC95_MIN_RSP_BUF_LEN];
    char *pchr;

    int isPSM = 0;

    _send_command(F("AT+NPSMR=1"));

    if (_wait_for_OK()) {
        _send_command(F("AT+NPSMR?"));
        uint16_t resp_buf_len = 0;

        if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
        {
            pchr = strstr_P(response_buffer, (PGM_P)F(","));
            isPSM = strtoul(++pchr, NULL, 10);
        }

        _send_command(F("AT+NPSMR=0"));
        _wait_for_OK();
    }

    return isPSM;
}

#define MIN_IP_ADDRESS_LENGTH   (7)
uint8_t NBIoT_BC95::is_assigned_ip(void) {
    char ip_addr[15] = "";
    get_IP_address(ip_addr);

    return (strlen(ip_addr) > MIN_IP_ADDRESS_LENGTH);
}

/******* Info getters *******/

uint8_t NBIoT_BC95::get_current_date_and_time(char *date_and_time) {
    uint8_t ret = 0;

    if (_is_init && is_registered() && date_and_time != NULL) {
        char response_buffer[BC95_MIN_CMD_BUF_LEN];
        uint16_t resp_buf_len = 0;

        _send_command(F("AT+CCLK?"));

        if (_read_line(response_buffer, sizeof(response_buffer), &resp_buf_len) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
        {
            char *pchr;
            pchr = strstr_P(response_buffer, (PGM_P)F(":"));
            strncpy(date_and_time, pchr, strlen(pchr)-1);

            ret = 1;
        }
    }

    return ret;
}

int8_t NBIoT_BC95::get_signal_strength(void) {
    uint8_t rssi_db = 0;

    if (_is_init & is_registered()) {
        char response_buffer[BC95_MIN_RSP_BUF_LEN];
        char *pchr;
        uint16_t resp_buf_len = 0;

        uint8_t rssi_raw = 0;

        _send_command(F("AT+CSQ"));

        if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
        {
            pchr = strstr_P(response_buffer, (PGM_P)F(":"));
            rssi_raw = strtoul(++pchr, NULL, 10);
        }

        if(rssi_raw >= 0 && rssi_raw <= 31) {
            rssi_db =  -113 + (rssi_raw << 1);
        }
    }

    return rssi_db;
}

uint8_t NBIoT_BC95::get_IP_address(char *ip_address) {
    uint8_t ret = 0;

    if (_is_init && is_registered() && is_attached()) {
        char response_buffer[BC95_MIN_CMD_BUF_LEN];
        char *pchr;
        uint16_t resp_buf_len = 0;

        _send_command(F("AT+CGPADDR=0"));

        if (_read_line(response_buffer, BC95_MIN_CMD_BUF_LEN, &resp_buf_len) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA))
        {
            _wait_for_OK();

            pchr = strstr_P(response_buffer, (PGM_P)F(","));
            strcpy(ip_address, ++pchr);

            ret = 1;
        }
    }

    return ret;
}

uint8_t NBIoT_BC95::get_IMEI(char *imei) {
    uint8_t ret = 0;
    char *pchr;

    char response_buffer[BC95_MIN_CMD_BUF_LEN];

    uint16_t resp_buf_len;

    _send_command(F("AT+CGSN=1"));

    if (_read_line(response_buffer, BC95_MIN_CMD_BUF_LEN, &resp_buf_len) &&
       (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
    {
        pchr = strstr_P(response_buffer, (PGM_P)F(":"));
        strcpy(imei, ++pchr);

        ret = 1;
    }

    return ret;
}

uint8_t NBIoT_BC95::get_ICCID(char * iccid) {
    uint8_t ret = 0;

    char response_buffer[BC95_MIN_CMD_BUF_LEN];
    uint16_t resp_buf_len = 0;

    char *pchr;

    _send_command(F("AT+NCCID"));

    if (_read_line(response_buffer, BC95_MIN_CMD_BUF_LEN, &resp_buf_len) &&
       (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_DATA) && _wait_for_OK())
    {
        pchr = strstr_P(response_buffer, (PGM_P)F(":"));
        strcpy(iccid, ++pchr);
        ret = 1;
    }

    return ret;
}

/******* Misc Funcions *******/

uint8_t NBIoT_BC95::reboot(void) {
    uint8_t ret = 0;
    char response_buffer[BC95_MIN_RSP_BUF_LEN];
    uint16_t resp_buf_len = 0;

    _send_command(F("AT+NRB"));

    if (_read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len, BC95_DEFAULT_REBOOT_TIMEOUT) &&
        (_check_response(response_buffer, resp_buf_len) != BC95_RESPONSE_TYPE_DATA))
    {
        if (strstr_P(response_buffer, (PGM_P)F("REBOOTING")) != NULL) {
            ret = 1;
        }
    }

    return ret;
}


/* BC95 Modem Private Functions */

uint8_t NBIoT_BC95::_send_command(const char *cmd) {
    uint8_t ret = 0;

    #if BC95_DEBUG_MODE > 0
        _dbg->println("---->");
        _dbg->println(cmd);
    #endif

    _flushInput();

    ret += _stream->print(cmd);
    ret += _stream->write('\n');
    _stream->flush();

    return ret;
}

uint8_t NBIoT_BC95::_send_command(const __FlashStringHelper *cmd) {
    char str_tmp[75 +1];

    strcpy_P(str_tmp, (PGM_P)cmd);

    return _send_command(str_tmp);
}

uint8_t NBIoT_BC95::_read_line(
        char *response_buffer,
        const uint16_t response_buffer_len,
        uint16_t *resonse_len,
        const uint32_t timeout)
{
    uint8_t done = 0;
    bc95_cmd_parser_state_t cur_parser_state = START_CR;
    uint16_t parsed_str_len = 0;
    uint8_t read_byte = 0;

    uint32_t lastReceivedByteMillis = millis();

    while ((millis() - lastReceivedByteMillis < timeout) && !done) {
        if (_stream->available()) {
            read_byte = _stream->read();

            if (cur_parser_state == NBIoT_BC95::START_CR) {
                if (read_byte == '\r') {
                    cur_parser_state = START_LF;
                    lastReceivedByteMillis = millis();
                }
            } else if (cur_parser_state == NBIoT_BC95::START_LF) {
                if (read_byte == '\n') {
                    cur_parser_state = PAYLOAD;
                    lastReceivedByteMillis = millis();
                } else {
                    // wrong sequence
                    cur_parser_state = START_CR;
                    parsed_str_len = 0;
                }
            } else if (cur_parser_state == NBIoT_BC95::PAYLOAD) {
                if (read_byte == '\r') {
                    cur_parser_state = END_LF;
                    lastReceivedByteMillis = millis();
                } else if (parsed_str_len >= (response_buffer_len-1)) {
                    // buffer overflow
                    cur_parser_state = START_CR;
                    parsed_str_len = 0;
                } else {
                    response_buffer[parsed_str_len++] = read_byte;
                    lastReceivedByteMillis = millis();
                }
            } else if (cur_parser_state == NBIoT_BC95::END_LF) {
                if (read_byte == '\n') {
                    response_buffer[parsed_str_len] = '\0';

                    if (resonse_len != NULL) {
                        *resonse_len = parsed_str_len;
                    }

                    #if BC95_DEBUG_MODE > 0
                        _dbg->println("<----");
                        _dbg->println(response_buffer);
                    #endif

                    done = 1;
                } else {
                    // wrong sequence
                    cur_parser_state = START_CR;
                    parsed_str_len = 0;
                }
            }
        }
    }

    return done;
}

uint8_t NBIoT_BC95::_check_response(const char *response_buffer, const uint16_t response_len) {
    uint8_t ret = BC95_RESPONSE_TYPE_TIMEOUT;

    if (response_len == 2 && response_buffer[0] == 'O' && response_buffer[1] == 'K') {
        ret = BC95_RESPONSE_TYPE_OK;
    } else if (strstr_P(response_buffer, (PGM_P)F("ERR")) != NULL) {
        ret = BC95_RESPONSE_TYPE_ERROR;
    } else {
        ret = BC95_RESPONSE_TYPE_DATA;
    }

    return ret;
}

uint8_t NBIoT_BC95::_wait_for_OK(const uint32_t timeout) {
    char response_buffer[BC95_MIN_RSP_BUF_LEN];
    uint16_t resp_buf_len = 0;
    return _read_line(response_buffer, BC95_MIN_RSP_BUF_LEN, &resp_buf_len, timeout) &&
           (_check_response(response_buffer, resp_buf_len) == BC95_RESPONSE_TYPE_OK);
}

uint8_t NBIoT_BC95::_ping_module(uint8_t times) {
    uint8_t ret = 0;

    while(!ret && times) {
        _send_command(F("AT"));
        ret = _wait_for_OK();
        times--;
    }

    return ret;
}

void NBIoT_BC95::_flushInput(void) {
    while (_stream->available()) _stream->read();
}

uint8_t _hex_char_to_int(const char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    } else if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    } else {
        return 0;
    }
}

uint8_t _is_valid_listen_port(uint16_t port) {
    /* Note: consult AT Commands Manual, command AT+NSOCR */
    return (port != 5683 && port != 5684 && port != 56830 && port != 56831 && port != 56833);
}

inline uint8_t _get_bit(uint32_t num, uint8_t bit) {
    return (num >> bit) & 0x01;
}

/***********/
