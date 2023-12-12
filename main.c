#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"

static const char SSID[] = "ssid";
static const char PASSWORD[] = "password";
static const char SERVER_NAME[] = "server";
static const uint16_t SERVER_PORT = 50000; 
static const int POLL_PERIOD = 5;     // Interval between poll_callback calls in seconds
#define BUFFER_SIZE 256

enum STATE_ENUM_T {
    INIT,
    WIFI,
    DNS,
    CONN,
    RX,
    TX
};

// The APP_STATE_T type is used to track the program state
typedef struct APP_STATE_T APP_STATE_T;
struct APP_STATE_T {
    ip_addr_t server_addr;  // IPv4 address of server
    struct tcp_pcb *pcb;    // Pointer to Protocol Control Block
    volatile enum STATE_ENUM_T state;
    int8_t err;
    uint8_t buffer[BUFFER_SIZE];
    uint16_t buffer_len;
    uint16_t buffer_pos;
};

// Callbacks are static so they are only callable from within this file

// DNS callback
// Called when DNS is complete if the record was not in the DNS cache
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    APP_STATE_T *app_state = callback_arg;

    if ((ipaddr) && (ipaddr->addr)) {
        // DNS resolved successfully
        app_state->server_addr = *ipaddr;
        app_state->err = 0;
        app_state->state = DNS;
    } else {
        app_state->err = -1;
    }
}

// Connected callback
// Called ONLY if the connection was successful
static err_t connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    APP_STATE_T *app_state = arg;
    app_state->state = CONN;
}

// Error callback
// Called if an error occurs
static void err_callback(void *arg, err_t err) {
    APP_STATE_T *app_state = arg;
    app_state->err = err;
}

// Receive callback
// Called if data is received or is the connection is closed
static err_t recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    APP_STATE_T *app_state = arg;

    // Check for errors
    if (err != ERR_OK) {
        app_state->err = err;
        return err;
    }

    // Check if connection has been closed    
    if (!p) {
        // NULL pbuf indicates remote host has closed the connection
        app_state->err = ERR_CLSD;
        app_state->state = DNS;
        return ERR_CLSD;
    }

    // Get length of packet
    app_state->buffer_len = p->tot_len;

    if (p->tot_len > 0) {
        // Copy up to BUFFER_SIZE bytes from the incoming packet
        app_state->buffer_pos = pbuf_copy_partial(p, app_state->buffer, BUFFER_SIZE, 0);

        // Free up the 
        tcp_recved(tpcb, p->tot_len);

    }
    pbuf_free(p);

    app_state->state = RX;

    return ERR_OK;
}

// Sent callback
// Called if queued data is sent
static err_t sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    APP_STATE_T *app_state = arg;

    app_state->buffer_pos += len;

    if (app_state->buffer_len == app_state->buffer_pos) {
        app_state->state = TX;
    }

    return ERR_OK;
}

// Application poll callback
// Called periodically
static err_t poll_callback(void *arg, struct tcp_pcb *tpcb) {
    return ERR_OK;
}

int main() {
    stdio_init_all();
    sleep_ms(5000);

    printf("TCP Client Starting...\n");

    // Create application state structure and set contents to zeros
    APP_STATE_T *app_state = calloc(1, sizeof(APP_STATE_T));
    app_state->state = INIT;

    int counter = 0;

    while (true) {
        // ------------------------------------------------------------------------
        // Connect to Wi-Fi
        // ------------------------------------------------------------------------

        // Initialise the CYW43 for use in the UK
        if (cyw43_arch_init_with_country(CYW43_COUNTRY_UK)) {
            printf("Failed to initialise WiFi\n");
        } else {
            // Enable Wi-Fi Station Mode
            cyw43_arch_enable_sta_mode();

            // Connect to Wi-Fi Access Point but timeout after 10 seconds if unsuccessful
            if (cyw43_arch_wifi_connect_timeout_ms(SSID, PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
                printf("Failed to connect to %s\n", SSID);
            } else {
                app_state->state = WIFI;
                printf("Connected to %s\n", SSID);
            }
        }

        // ------------------------------------------------------------------------
        // Resolve host name to IP address using DNS
        // ------------------------------------------------------------------------

        if (app_state->state == WIFI) {
            // Get the server IP address using DNS
            app_state->err = dns_gethostbyname(SERVER_NAME, &app_state->server_addr, dns_callback, app_state);

            if (app_state->err == ERR_OK) {
                // Address was in the DNS cache
                app_state->state = DNS;
            } else {
                // Wait for resolution of DNS
                app_state->err = 0;

                while (app_state->err == ERR_OK && app_state->state != DNS) {
                    cyw43_arch_poll();
                }

                if (app_state->state != DNS) {
                    printf("Failed to resolve host %s\n", SERVER_NAME);
                } else {
                     printf("Host %s resolved to %s\n", SERVER_NAME, ip4addr_ntoa(&app_state->server_addr));
                }
            }
        }

        // ------------------------------------------------------------------------
        // Connect to the TCP server
        // ------------------------------------------------------------------------

        if (app_state->state == DNS) {
            // Create TCP Protocol Control Block (PCB)
            app_state->pcb = tcp_new_ip_type(IP_GET_TYPE(app_state->server_addr));

            if (!app_state->pcb) {
                printf("Failed to create Protocol Control Block\n");
            } else {
                // Set the TCP callback function argument to our application state
                tcp_arg(app_state->pcb, app_state);

                // Set the polling interval and callback
                // The polling interval is specified in TCP coarse grained timer slots which are typically twice a second
                tcp_poll(app_state->pcb, poll_callback, POLL_PERIOD * 2); 

                // Set the callbacks for successful transmissions, incoming data, and errors
                tcp_sent(app_state->pcb, sent_callback);
                tcp_recv(app_state->pcb, recv_callback);
                tcp_err(app_state->pcb, err_callback);

                // Connect to the TCP server
                printf("Connecting to server\n");
                app_state->err = tcp_connect(app_state->pcb, &app_state->server_addr, SERVER_PORT, connected_callback);

                if (app_state->err != ERR_OK) {
                    // Function call failed
                    printf("TCP connection failed: Error %d\n", app_state->err);
                } else {
                    // Wait for connection attempt to finish
                    while (app_state->err == ERR_OK && app_state->state != CONN) {
                        cyw43_arch_poll();
                    }

                    if (app_state->state != CONN) {
                        printf("TCP connection failed: Error %d\n", app_state->err);
                    } else {
                        printf("Connected to server\n");
                    }
                }
            }
        }

        
        // ------------------------------------------------------------------------
        // Receive data from server
        // ------------------------------------------------------------------------
        if (app_state->state == CONN) {
            app_state->buffer_len = 0;
            app_state->buffer_pos = 0;

            while (app_state->err == ERR_OK && app_state->state != RX) {
                cyw43_arch_poll();
            }

            if (app_state->state != RX) {
                // Error
                if (app_state->err == ERR_CLSD) {
                    // TCP connection closed
                    printf("TCP connection closed\n");
                } else {
                    printf("Failed to receive data: Error %d\n", app_state->err);
                }
            } else {
                printf("Received data \"%s\" from server\n", app_state->buffer);
            }
        }

        // ------------------------------------------------------------------------
        // Send data to server
        // ------------------------------------------------------------------------

        if (app_state->state == RX) {
            size_t len = sprintf(app_state->buffer, "Hello server %d", counter);
            app_state->buffer_len = len;
            app_state->buffer_pos = 0;

            // Queue the data to send
            app_state->err = tcp_write(app_state->pcb, app_state->buffer, app_state->buffer_len, TCP_WRITE_FLAG_COPY);

            if (app_state->err != ERR_OK) {
                printf("Failed to write data: Error %d\n", app_state->err);
            } else {
                while (app_state->err == ERR_OK && app_state->state != TX) {
                    cyw43_arch_poll();
                }

                if (app_state->state != TX) {
                    // Error
                    if (app_state->err == ERR_CLSD) {
                        // TCP connection closed
                        printf("TCP connection closed\n");
                    } else {
                        printf("Failed to send data: Error %d\n", app_state->err);
                    }
                } else {
                    printf("Sent data \"%s\" to server\n", app_state->buffer);

                    counter += 1;
                }
            }            
        }

        // ------------------------------------------------------------------------
        // Close server connection
        // ------------------------------------------------------------------------

        if (app_state->state == CONN || app_state->state == RX || app_state->state == TX) {
            app_state->err = tcp_close(app_state->pcb);

            if (app_state->err != ERR_OK) {
                printf("Failed to disconnect, aborting: Error %d\n", app_state->err);

                // Send a RST to the remote host and deallocate the PCB
                tcp_abort(app_state->pcb);
            } else {
                printf("Disconnected from server\n");
            }

            app_state->state = WIFI;
        }

        // ------------------------------------------------------------------------
        // Disconnect from Wi-Fi
        // ------------------------------------------------------------------------

        if (app_state->state == DNS || app_state->state == WIFI) {
            // De-initialise and power off Wi-Fi chip
            cyw43_arch_deinit();
            printf("Disconnected from Wi-Fi\n");
        }

        memset(app_state, 0, sizeof(APP_STATE_T));
        app_state->state = INIT;

        sleep_ms(5000);
    }
}
