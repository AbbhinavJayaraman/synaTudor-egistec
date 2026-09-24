#ifndef LIBTUDOR_TUDOR_EGIS_H
#define LIBTUDOR_TUDOR_EGIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <libusb.h>
#include "../winapi/api.h"

//The EgisTec EH575 speaks a simple framed protocol over two bulk endpoints.
//Host packets start with the ASCII magic "EGIS", device replies with "SIGE".
#define EGIS_USB_VID 0x1c7a
#define EGIS_USB_PID 0x0575

#define EGIS_EP_OUT 0x01
#define EGIS_EP_IN  0x82

#define EGIS_MAGIC_OUT "\x45\x47\x49\x53"
#define EGIS_MAGIC_IN  "\x53\x49\x47\x45"

//A full frame is one bulk IN transfer of exactly 5356 bytes, which is
//103 x 52 with no header and no padding.
//
//USBPcap reports it as a 5120-byte read followed by a 236-byte read, but those
//two always carry the same IRP id - it is one logical transfer that the host
//controller split, and 5120 + 236 = 5356. In all 639 frames across the three
//captures in python-egistec-eh575/wireshark the pairing holds, and every frame
//is preceded by the "EGIS" 64 14 ec trigger.
//
//Width 103 is solid: scanning widths 95..115 over the highest-variance frames,
//103 gives a mean consecutive-row correlation of 0.874 against 0.819 for its
//nearest neighbours. Offset 0 then ranks first of all 103 candidate offsets,
//which it should, since 5356 = 103 * 52 exactly.
//
//An earlier reading of this used only the first 5120 bytes at an offset of 73,
//giving 49 rows. That was an artifact: 5120 mod 103 is exactly 73, so dropping
//73 bytes is simply what it takes to make a truncated buffer divide evenly. The
//images it produced are visibly torn. The 103 x 52 reading also matches the
//engine adapter independently - its CET510Sensor class allocates 103 x 52 (see
//the ModelName section in CLAUDE.md).
#define EGIS_FRAME_BYTES  5356
#define EGIS_FRAME_OFFSET 0
#define EGIS_IMG_WIDTH    103
#define EGIS_IMG_HEIGHT   52
#define EGIS_IMG_BYTES    (EGIS_IMG_WIDTH * EGIS_IMG_HEIGHT)

_Static_assert(EGIS_FRAME_OFFSET + EGIS_IMG_BYTES == EGIS_FRAME_BYTES,
    "the frame is exactly the image - no header, no trailer");

//Sensor resolution in pixels per inch, as reported to the engine adapter.
//Egis' own INF does not carry this; 508 is the usual value for this part.
#define EGIS_IMG_DPI 508

struct egis_device {
    libusb_device_handle *usb_dev;
    bool iface_claimed;

    //Serialises everything that touches the bulk endpoints.
    pthread_mutex_t dev_lock;

    //Capture state. Guarded by cap_lock; cap_cond is broadcast whenever
    //cap_running clears, so that close can wait a capture out.
    pthread_mutex_t cap_lock;
    pthread_cond_t cap_cond;
    bool cap_running;
    bool cap_cancel;

    //Last indicator (LED) state the adapter asked for.
    uint32_t indicator;
};

//A single in-flight IOCTL. Handed back to the winio layer as the op context so
//that CancelIoEx can reach an async capture.
struct egis_request {
    struct egis_device *dev;
    OVERLAPPED *ovlp;
    void *out_buf;
    size_t out_size;
    uint32_t code;
};

bool egis_open(struct egis_device *dev, libusb_device_handle *usb_dev);
void egis_close(struct egis_device *dev);

//Runs the power-on register programming recovered from the Windows driver.
bool egis_init_sensor(struct egis_device *dev);

//Arms the sensor and reads one frame. buf must hold at least EGIS_IMG_BYTES.
//Returns false on USB error or cancellation.
bool egis_capture_frame(struct egis_device *dev, uint8_t *buf);

//Native replacement for the UMDF driver's IOCTL dispatch. Returns
//STATUS_SUCCESS once the request has been accepted; the request completes the
//OVERLAPPED itself (synchronously for everything except CAPTURE_DATA).
NTSTATUS egis_devctrl(struct egis_device *dev, OVERLAPPED *ovlp, ULONG code, const void *in_buf, size_t in_size, void *out_buf, size_t out_size, struct egis_request **req);
NTSTATUS egis_cancel(struct egis_device *dev, OVERLAPPED *ovlp, struct egis_request *req);
void egis_cleanup(struct egis_device *dev, OVERLAPPED *ovlp, struct egis_request *req);

#endif
