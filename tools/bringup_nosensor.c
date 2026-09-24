// Drives the real tudor_open() against a simulated EH575.
//
// Every byte egis.c sends leaves through libusb_bulk_transfer, so defining the
// libusb entry points here is enough to stand the sensor in: symbols in the
// executable win over libusb.so for lookups coming from libtudor.so. That makes
// the whole DLL bring-up path - Attach, PipelineInit, Activate, QueryStatus -
// runnable without the hardware present.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libusb.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <tudor/log.h>
#include <tudor/tudor.h>

static int verbose_usb = 0;

int libusb_reset_device(libusb_device_handle *h) { return 0; }
int libusb_kernel_driver_active(libusb_device_handle *h, int i) { return 0; }
int libusb_detach_kernel_driver(libusb_device_handle *h, int i) { return 0; }
int libusb_claim_interface(libusb_device_handle *h, int i) { return 0; }
int libusb_release_interface(libusb_device_handle *h, int i) { return 0; }
int libusb_set_configuration(libusb_device_handle *h, int c) { return 0; }
int libusb_clear_halt(libusb_device_handle *h, unsigned char ep) { return 0; }
const char *libusb_error_name(int e) { return e ? "MOCK_ERR" : "MOCK_OK"; }
libusb_device *libusb_get_device(libusb_device_handle *h) { return (libusb_device*) 0x1234; }
uint8_t libusb_get_bus_number(libusb_device *d) { return 1; }
uint8_t libusb_get_device_address(libusb_device *d) { return 7; }

// Last opcode seen on EP OUT, so the IN reply can echo it the way the sensor does.
static uint8_t last_op = 0;

int libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep,
                         unsigned char *data, int len, int *transferred,
                         unsigned int timeout) {
    if((ep & 0x80) == 0) {
        if(len >= 5) last_op = data[4];
        if(verbose_usb) {
            fprintf(stderr, "[USB] OUT %d bytes:", len);
            for(int i = 0; i < len && i < 24; i++) fprintf(stderr, " %02x", data[i]);
            fprintf(stderr, "\n");
        }
        *transferred = len;
        return 0;
    }

    // IN: a frame read asks for a big buffer, anything else gets the short
    // "SIGE" status packet the device answers commands with.
    if(len >= 5120) {
        memcpy(data, "SIGE", 4);
        for(int i = 4; i < len; i++) data[i] = (unsigned char) (0x40 + ((i * 7 + (i / 103) * 3) % 0x50));
        *transferred = len;
        return 0;
    }
    int n = len < 8 ? len : 8;
    memcpy(data, "SIGE", 4);
    if(n > 4) data[4] = last_op;
    for(int i = 5; i < n; i++) data[i] = 0;
    *transferred = n;
    return 0;
}

static const struct tudor_pair_data *get_pdata(const char *name) { return NULL; }
static void set_pdata(const char *name, const struct tudor_pair_data *p) { }

int main(int argc, char **argv) {
    LOG_LEVEL = LOG_VERBOSE;
    tudor_log_traces = true;
    for(int i = 1; i < argc; i++) if(!strcmp(argv[i], "-u")) verbose_usb = 1;

    // Unbuffered so nothing is lost if this dies mid-write.
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
    tudor_get_pdata_fnc = get_pdata;
    tudor_set_pdata_fnc = set_pdata;

    printf("=== tudor_init() ===\n");
    if(!tudor_init()) { printf("!!! tudor_init FAILED\n"); return 2; }

    printf("=== tudor_open() ===\n");
    struct tudor_device device;
    if(!tudor_open(&device, (libusb_device_handle*) 0x5678, NULL)) {
        printf("!!! tudor_open FAILED (clean failure, no crash)\n");
        return 3;
    }
    printf("=== tudor_open SUCCEEDED ===\n");
    return 0;
}
