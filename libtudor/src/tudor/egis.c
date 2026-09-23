//Native IOCTL backend for the EgisTec EH575 (USB 1c7a:0575).
//
//This file replaces EgisTouchFP0575.dll - the UMDF driver - entirely. That DLL
//existed only to turn the biometric IOCTLs issued by the sensor and engine
//adapters into "EGIS"-framed bulk transfers, and it was the single component in
//the stack that needed the WDF/UMDF emulation layer. Neither
//EgisTouchFPSensor0575.dll nor EgisTouchFPEngine0575.dll imports a single WDF
//symbol: between them the entire device I/O surface is CreateFile,
//DeviceIoControl, GetOverlappedResult and CancelIoEx. Since libtudor already
//hands the adapters a synthetic SensorHandle (see tudor_open in device.c), we
//can service those IOCTLs here against libusb and skip the UMDF driver.
//
//The register programming below is transcribed from the working Python driver
//in python-egistec-eh575 (egis_driver/egis_driver.py), which recovered it from
//USB captures of the Windows stack.

#include <string.h>
#include <unistd.h>
#include "internal.h"
#include "egis.h"

#define EGIS_USB_IFACE 0
#define EGIS_CMD_TIMEOUT_MS 1000
#define EGIS_FRAME_TIMEOUT_MS 1500

//Poll interval used while waiting for a finger, so that a cancelled capture
//notices reasonably quickly.
#define EGIS_POLL_INTERVAL_US 50000

//A frame whose standard deviation is below this is taken to be an empty
//platen. Tuned in the Python driver against this sensor. Compared as a
//variance so no square root - and hence no libm - is needed.
#define EGIS_TOUCH_THRESHOLD 31.0
#define EGIS_TOUCH_VARIANCE (EGIS_TOUCH_THRESHOLD * EGIS_TOUCH_THRESHOLD)

//--- USB transport -----------------------------------------------------------

//Writes one "EGIS" command and drains the short status reply. Reply contents
//are not interpreted; the Windows driver ignores them on these commands too.
static bool egis_cmd(struct egis_device *dev, const uint8_t *cmd, size_t cmd_size) {
    int transferred = 0;
    int err = libusb_bulk_transfer(dev->usb_dev, EGIS_EP_OUT, (unsigned char*) cmd, (int) cmd_size, &transferred, EGIS_CMD_TIMEOUT_MS);
    if(err != 0) {
        log_error("EGIS command write failed: %d [%s]", err, libusb_error_name(err));
        return false;
    }

    //The device answers every command with a short "SIGE" status packet. A
    //timeout here is not fatal - several of the register writes below are
    //answered lazily - so only a hard error is reported.
    uint8_t resp[64];
    err = libusb_bulk_transfer(dev->usb_dev, EGIS_EP_IN, resp, sizeof(resp), &transferred, EGIS_CMD_TIMEOUT_MS);
    if(err != 0 && err != LIBUSB_ERROR_TIMEOUT) {
        log_verbose("EGIS command status read failed: %d [%s]", err, libusb_error_name(err));
    }

    return true;
}

static bool egis_cmd_seq(struct egis_device *dev, const uint8_t *const *cmds, const size_t *sizes, size_t count, useconds_t delay_us) {
    for(size_t i = 0; i < count; i++) {
        if(!egis_cmd(dev, cmds[i], sizes[i])) return false;
        if(delay_us) usleep(delay_us);
    }
    return true;
}

//Command tables. Each entry is a raw packet: "EGIS" magic followed by an
//opcode and its operands.
#define EGIS_CMD(...) (const uint8_t[]) { 0x45, 0x47, 0x49, 0x53, __VA_ARGS__ }

static const uint8_t *const egis_patch_cmds[] = {
    EGIS_CMD(0x60, 0x00, 0x06), EGIS_CMD(0x60, 0x01, 0x06), EGIS_CMD(0x60, 0x40, 0x06),
    EGIS_CMD(0x61, 0x0a, 0xf4), EGIS_CMD(0x61, 0x0c, 0x44), EGIS_CMD(0x61, 0x40, 0x00),
    EGIS_CMD(0x60, 0x40, 0x00), EGIS_CMD(0x71, 0x02, 0x02, 0x01, 0x0c), EGIS_CMD(0x61, 0x0c, 0x22),
    EGIS_CMD(0x61, 0x0b, 0x03), EGIS_CMD(0x61, 0x0a, 0xfc),
    EGIS_CMD(0x60, 0x00, 0xfc), EGIS_CMD(0x60, 0x01, 0xfc), EGIS_CMD(0x60, 0x41, 0xfc)
};
static const size_t egis_patch_sizes[] = {
    7, 7, 7, 7, 7, 7, 7, 9, 7, 7, 7, 7, 7, 7
};

static const uint8_t *const egis_init_cmds[] = {
    EGIS_CMD(0x97, 0x00, 0x00),
    EGIS_CMD(0x60, 0x00, 0x00), EGIS_CMD(0x60, 0x00, 0x00), EGIS_CMD(0x60, 0x00, 0x00),
    EGIS_CMD(0x60, 0x00, 0x00), EGIS_CMD(0x60, 0x00, 0x00),
    EGIS_CMD(0x60, 0x01, 0x00), EGIS_CMD(0x61, 0x0a, 0xfd), EGIS_CMD(0x61, 0x35, 0x02),
    EGIS_CMD(0x61, 0x80, 0x00), EGIS_CMD(0x60, 0x80, 0x00), EGIS_CMD(0x61, 0x0a, 0xfc),
    EGIS_CMD(0x63, 0x01, 0x02, 0x0f, 0x03), EGIS_CMD(0x61, 0x0c, 0x22), EGIS_CMD(0x61, 0x09, 0x83),
    EGIS_CMD(0x63, 0x26, 0x06, 0x06, 0x60, 0x06, 0x05, 0x2f, 0x06), EGIS_CMD(0x61, 0x0a, 0xf4),
    EGIS_CMD(0x61, 0x0c, 0x44), EGIS_CMD(0x61, 0x50, 0x03), EGIS_CMD(0x60, 0x50, 0x03)
};
static const size_t egis_init_sizes[] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 9, 7, 7, 12, 7, 7, 7, 7
};

static const uint8_t *const egis_final_cmds[] = {
    EGIS_CMD(0x60, 0x40, 0xec), EGIS_CMD(0x61, 0x0c, 0x22), EGIS_CMD(0x61, 0x0b, 0x03),
    EGIS_CMD(0x61, 0x0a, 0xfc), EGIS_CMD(0x60, 0x40, 0xfc),
    EGIS_CMD(0x63, 0x09, 0x0b, 0x83, 0x24, 0x00, 0x44, 0x0f, 0x08, 0x20, 0x20, 0x01, 0x05, 0x12),
    EGIS_CMD(0x63, 0x26, 0x06, 0x06, 0x60, 0x06, 0x05, 0x2f, 0x06), EGIS_CMD(0x61, 0x23, 0x00),
    EGIS_CMD(0x61, 0x24, 0x33), EGIS_CMD(0x61, 0x20, 0x00), EGIS_CMD(0x61, 0x21, 0x66),
    EGIS_CMD(0x60, 0x00, 0x66), EGIS_CMD(0x60, 0x01, 0x66)
};
static const size_t egis_final_sizes[] = {
    7, 7, 7, 7, 7, 18, 12, 7, 7, 7, 7, 7, 7
};

//Re-arms the sensor front end. Must run immediately before every frame trigger.
static const uint8_t *const egis_rearm_cmds[] = {
    EGIS_CMD(0x61, 0x2d, 0x20), EGIS_CMD(0x60, 0x00, 0x20), EGIS_CMD(0x60, 0x01, 0x20),
    EGIS_CMD(0x63, 0x2c, 0x02, 0x00, 0x57), EGIS_CMD(0x60, 0x2d, 0x02),
    EGIS_CMD(0x62, 0x67, 0x03), EGIS_CMD(0x63, 0x2c, 0x02, 0x00, 0x13),
    EGIS_CMD(0x60, 0x00, 0x02)
};
static const size_t egis_rearm_sizes[] = { 7, 7, 7, 9, 7, 7, 9, 7 };

static const uint8_t egis_trigger_cmd[] = { 0x45, 0x47, 0x49, 0x53, 0x64, 0x14, 0xec };

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

bool egis_init_sensor(struct egis_device *dev) {
    log_debug("Initializing EGIS sensor...");

    if(!egis_cmd_seq(dev, egis_patch_cmds, egis_patch_sizes, ARRAY_LEN(egis_patch_cmds), 0)) return false;
    if(!egis_cmd_seq(dev, egis_init_cmds, egis_init_sizes, ARRAY_LEN(egis_init_cmds), 2000)) return false;
    if(!egis_cmd_seq(dev, egis_final_cmds, egis_final_sizes, ARRAY_LEN(egis_final_cmds), 0)) return false;

    log_debug("EGIS sensor ready");
    return true;
}

//Reads one raw frame off the sensor. buf receives EGIS_IMG_BYTES of pixels,
//already stripped of the 73 byte partial leading row.
static bool egis_read_frame_locked(struct egis_device *dev, uint8_t *buf) {
    if(!egis_cmd_seq(dev, egis_rearm_cmds, egis_rearm_sizes, ARRAY_LEN(egis_rearm_cmds), 0)) return false;

    int transferred = 0;
    int err = libusb_bulk_transfer(dev->usb_dev, EGIS_EP_OUT, (unsigned char*) egis_trigger_cmd, sizeof(egis_trigger_cmd), &transferred, EGIS_CMD_TIMEOUT_MS);
    if(err != 0) {
        log_error("EGIS frame trigger failed: %d [%s]", err, libusb_error_name(err));
        return false;
    }

    uint8_t frame[EGIS_FRAME_BYTES];
    err = libusb_bulk_transfer(dev->usb_dev, EGIS_EP_IN, frame, sizeof(frame), &transferred, EGIS_FRAME_TIMEOUT_MS);
    if(err != 0) {
        log_error("EGIS frame read failed: %d [%s]", err, libusb_error_name(err));
        return false;
    }
    if(transferred < EGIS_FRAME_OFFSET + EGIS_IMG_BYTES) {
        log_warn("Short EGIS frame: %d bytes, expected %d", transferred, EGIS_FRAME_OFFSET + EGIS_IMG_BYTES);
        return false;
    }

    memcpy(buf, frame + EGIS_FRAME_OFFSET, EGIS_IMG_BYTES);
    return true;
}

bool egis_capture_frame(struct egis_device *dev, uint8_t *buf) {
    cant_fail_ret(pthread_mutex_lock(&dev->dev_lock));
    bool ok = egis_read_frame_locked(dev, buf);
    cant_fail_ret(pthread_mutex_unlock(&dev->dev_lock));
    return ok;
}

//Variance of a frame, used as the finger-present heuristic. The Windows driver
//has a dedicated finger-detect register path (the INF sets FingerOnThreshold=6
/// FingerOnThresholdLoose=2); until that is mapped, this mirrors what the
//Python driver does, comparing against the squared threshold so that libtudor
//does not have to pull in libm.
static double egis_frame_variance(const uint8_t *buf, size_t size) {
    double sum = 0.0;
    for(size_t i = 0; i < size; i++) sum += buf[i];
    double mean = sum / (double) size;

    double var = 0.0;
    for(size_t i = 0; i < size; i++) {
        double d = (double) buf[i] - mean;
        var += d * d;
    }
    return var / (double) size;
}

//--- Device lifecycle --------------------------------------------------------

bool egis_open(struct egis_device *dev, libusb_device_handle *usb_dev) {
    *dev = (struct egis_device) {
        .usb_dev = usb_dev,
        .iface_claimed = false,
        .cap_running = false,
        .cap_cancel = false,
        .indicator = 0
    };
    cant_fail_ret(pthread_mutex_init(&dev->dev_lock, NULL));
    cant_fail_ret(pthread_mutex_init(&dev->cap_lock, NULL));
    cant_fail_ret(pthread_cond_init(&dev->cap_cond, NULL));

    int err;
    if(libusb_kernel_driver_active(usb_dev, EGIS_USB_IFACE) == 1) {
        if((err = libusb_detach_kernel_driver(usb_dev, EGIS_USB_IFACE)) != 0) {
            log_warn("Couldn't detach kernel driver: %d [%s]", err, libusb_error_name(err));
        }
    }

    if((err = libusb_claim_interface(usb_dev, EGIS_USB_IFACE)) != 0) {
        log_error("Couldn't claim EGIS interface: %d [%s]", err, libusb_error_name(err));
        return false;
    }
    dev->iface_claimed = true;

    return egis_init_sensor(dev);
}

void egis_close(struct egis_device *dev) {
    //Stop any capture still in flight before the handle goes away. The capture
    //thread is detached, so wait on the condvar rather than joining it.
    cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
    dev->cap_cancel = true;
    while(dev->cap_running) cant_fail_ret(pthread_cond_wait(&dev->cap_cond, &dev->cap_lock));
    cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));

    if(dev->iface_claimed) {
        libusb_release_interface(dev->usb_dev, EGIS_USB_IFACE);
        dev->iface_claimed = false;
    }

    cant_fail_ret(pthread_mutex_destroy(&dev->dev_lock));
    cant_fail_ret(pthread_mutex_destroy(&dev->cap_lock));
    cant_fail_ret(pthread_cond_destroy(&dev->cap_cond));
}

//--- IOCTL handlers ----------------------------------------------------------

static NTSTATUS egis_ioctl_get_attributes(struct egis_device *dev, void *out_buf, size_t out_size, size_t *transferred) {
    if(out_size < sizeof(WINBIO_SENSOR_ATTRIBUTES)) return STATUS_BUFFER_TOO_SMALL;

    WINBIO_SENSOR_ATTRIBUTES *attr = (WINBIO_SENSOR_ATTRIBUTES*) out_buf;
    memset(attr, 0, sizeof(*attr));

    attr->PayloadSize = (ULONG) sizeof(*attr);
    attr->WinBioHresult = ERROR_SUCCESS;
    attr->WinBioVersion = (WINBIO_VERSION) { .MajorVersion = 1, .MinorVersion = 0 };
    attr->WinBioType = WINBIO_TYPE_FINGERPRINT;
    attr->WinBioSensorSubType = WINBIO_FP_SENSOR_SUBTYPE_TOUCH;
    attr->WinBioCapabilities = WINBIO_CAPABILITY_SENSOR | WINBIO_CAPABILITY_MATCHING | WINBIO_CAPABILITY_PROCESSING;
    attr->FirmwareVersion = (WINBIO_VERSION) { .MajorVersion = 3, .MinorVersion = 7 };

    //The engine adapter reads the model name out of this buffer, so it has to
    //be a real NUL-terminated UTF-16 string rather than zeroes.
    static const char16_t manufacturer[] = u"Egis Technology Inc.";
    static const char16_t model[] = u"EgisTec EH575";
    static const char16_t serial[] = u"0000";
    memcpy(attr->ManufacturerName, manufacturer, sizeof(manufacturer));
    memcpy(attr->ModelName, model, sizeof(model));
    memcpy(attr->SerialNumber, serial, sizeof(serial));

    attr->SupportedFormatEntries = 1;
    attr->SupportedFormat[0].Owner = WINBIO_ANSI_381_FORMAT_OWNER;
    attr->SupportedFormat[0].Type = WINBIO_ANSI_381_FORMAT_TYPE;

    *transferred = sizeof(*attr);
    return STATUS_SUCCESS;
}

static NTSTATUS egis_ioctl_get_sensor_status(struct egis_device *dev, void *out_buf, size_t out_size, size_t *transferred) {
    //The adapter asks for 0x14 bytes here. Only the leading status word is
    //interpreted by EgisTouchFPSensor0575's QueryStatus path.
    if(out_size < 0x14) return STATUS_BUFFER_TOO_SMALL;

    memset(out_buf, 0, 0x14);

    cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
    bool busy = dev->cap_running;
    cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));

    ((ULONG*) out_buf)[0] = busy ? WINBIO_SENSOR_BUSY : WINBIO_SENSOR_READY;

    *transferred = 0x14;
    return STATUS_SUCCESS;
}

static NTSTATUS egis_ioctl_reset(struct egis_device *dev, void *out_buf, size_t out_size, size_t *transferred) {
    cant_fail_ret(pthread_mutex_lock(&dev->dev_lock));
    bool ok = egis_init_sensor(dev);
    cant_fail_ret(pthread_mutex_unlock(&dev->dev_lock));
    if(!ok) return STATUS_UNSUCCESSFUL;

    if(out_size >= 8) {
        memset(out_buf, 0, 8);
        *transferred = 8;
    } else *transferred = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS egis_ioctl_calibrate(struct egis_device *dev, void *out_buf, size_t out_size, size_t *transferred) {
    //Calibration on this part is the register programming already done by
    //egis_init_sensor; there is no separate calibration exchange in the
    //captures. Report success with a zeroed result block.
    if(out_size >= 0x10) {
        memset(out_buf, 0, 0x10);
        *transferred = 0x10;
    } else *transferred = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS egis_ioctl_get_indicator(struct egis_device *dev, void *out_buf, size_t out_size, size_t *transferred) {
    if(out_size < 0xc) return STATUS_BUFFER_TOO_SMALL;
    memset(out_buf, 0, 0xc);
    ((ULONG*) out_buf)[0] = dev->indicator;
    *transferred = 0xc;
    return STATUS_SUCCESS;
}

static NTSTATUS egis_ioctl_set_indicator(struct egis_device *dev, const void *in_buf, size_t in_size, void *out_buf, size_t out_size, size_t *transferred) {
    //This sensor has no host-controllable indicator; remember the value so a
    //subsequent GET returns something consistent.
    if(in_size >= sizeof(ULONG)) dev->indicator = ((const ULONG*) in_buf)[0];

    if(out_size >= 0xc) {
        memset(out_buf, 0, 0xc);
        *transferred = 0xc;
    } else *transferred = 0;

    return STATUS_SUCCESS;
}

//--- Asynchronous capture ----------------------------------------------------

//Waits for the platen to clear, then for a finger, then returns that frame.
//Runs on its own thread so the adapter's overlapped DeviceIoControl behaves
//the way it does on Windows and stays cancellable.
static void *egis_capture_thread(void *arg) {
    struct egis_request *req = (struct egis_request*) arg;
    struct egis_device *dev = req->dev;

    NTSTATUS status = STATUS_UNSUCCESSFUL;
    size_t transferred = 0;
    uint8_t img[EGIS_IMG_BYTES];

    //Wait for the previous finger to come off, so a single press cannot be
    //consumed as two captures.
    int clear_streak = 0;
    while(clear_streak < 2) {
        cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
        bool cancelled = dev->cap_cancel;
        cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));
        if(cancelled) { status = STATUS_CANCELLED; goto done; }

        if(!egis_capture_frame(dev, img)) goto done;
        if(egis_frame_variance(img, sizeof(img)) < EGIS_TOUCH_VARIANCE) clear_streak++;
        else clear_streak = 0;

        usleep(EGIS_POLL_INTERVAL_US);
    }

    //Now wait for a finger.
    for(;;) {
        cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
        bool cancelled = dev->cap_cancel;
        cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));
        if(cancelled) { status = STATUS_CANCELLED; goto done; }

        if(!egis_capture_frame(dev, img)) goto done;
        if(egis_frame_variance(img, sizeof(img)) >= EGIS_TOUCH_VARIANCE) break;

        usleep(EGIS_POLL_INTERVAL_US);
    }

    //Hand the frame back in the WINBIO_CAPTURE_DATA layout the sensor adapter
    //expects. CaptureBuffer holds the raw 8bpp image; the adapter wraps it into
    //an ANSI 381 BIR before it reaches the engine.
    {
        WINBIO_CAPTURE_DATA *data = (WINBIO_CAPTURE_DATA*) req->out_buf;
        size_t needed = offsetof(WINBIO_CAPTURE_DATA, CaptureBuffer) + EGIS_IMG_BYTES;
        if(req->out_size < needed) { status = STATUS_BUFFER_TOO_SMALL; goto done; }

        memset(data, 0, offsetof(WINBIO_CAPTURE_DATA, CaptureBuffer));
        data->PayloadSize = (ULONG) needed;
        data->WinBioHresult = ERROR_SUCCESS;
        data->SensorStatus = WINBIO_SENSOR_ACCEPT;
        data->RejectDetail = 0;
        data->Format.Owner = WINBIO_ANSI_381_FORMAT_OWNER;
        data->Format.Type = WINBIO_ANSI_381_FORMAT_TYPE;
        data->CaptureBufferSize = EGIS_IMG_BYTES;
        memcpy(data->CaptureBuffer, img, EGIS_IMG_BYTES);

        transferred = needed;
        status = STATUS_SUCCESS;
    }

done:
    //Completing the OVERLAPPED can run the waiter's cleanup synchronously,
    //which frees req - so take everything needed off it first.
    {
        OVERLAPPED *ovlp = req->ovlp;

        cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
        dev->cap_running = false;
        dev->cap_cancel = false;
        cant_fail_ret(pthread_cond_broadcast(&dev->cap_cond));
        cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));

        winio_complete_overlapped(ovlp, status, transferred);
    }
    return NULL;
}

static NTSTATUS egis_ioctl_capture_data(struct egis_device *dev, OVERLAPPED *ovlp, const void *in_buf, size_t in_size, void *out_buf, size_t out_size, struct egis_request **out_req) {
    if(in_size < 0x20) return STATUS_INVALID_PARAMETER;
    if(out_size < offsetof(WINBIO_CAPTURE_DATA, CaptureBuffer) + EGIS_IMG_BYTES) return STATUS_BUFFER_TOO_SMALL;

    const WINBIO_CAPTURE_PARAMETERS *params = (const WINBIO_CAPTURE_PARAMETERS*) in_buf;
    log_debug("EGIS capture requested [purpose 0x%x, subtype 0x%x, format %04x:%04x]", params->Purpose, params->Subtype, params->Format.Owner, params->Format.Type);

    cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
    if(dev->cap_running) {
        cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));
        return STATUS_DEVICE_NOT_READY;
    }

    struct egis_request *req = (struct egis_request*) malloc(sizeof(struct egis_request));
    if(!req) {
        cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));
        abort_perror("Couldn't allocate EGIS request");
    }
    *req = (struct egis_request) {
        .dev = dev,
        .ovlp = ovlp,
        .out_buf = out_buf,
        .out_size = out_size,
        .code = IOCTL_BIOMETRIC_CAPTURE_DATA
    };

    dev->cap_running = true;
    dev->cap_cancel = false;

    pthread_t thread;
    int err = pthread_create(&thread, NULL, egis_capture_thread, req);
    if(err != 0) {
        dev->cap_running = false;
        cant_fail_ret(pthread_cond_broadcast(&dev->cap_cond));
        cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));
        free(req);
        log_error("Couldn't start EGIS capture thread: %d [%s]", err, strerror(err));
        return STATUS_UNSUCCESSFUL;
    }
    cant_fail_ret(pthread_detach(thread));
    cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));

    *out_req = req;
    return STATUS_SUCCESS;
}

//--- Dispatch ----------------------------------------------------------------

NTSTATUS egis_devctrl(struct egis_device *dev, OVERLAPPED *ovlp, ULONG code, const void *in_buf, size_t in_size, void *out_buf, size_t out_size, struct egis_request **req) {
    *req = NULL;

    //CAPTURE_DATA is the only one that blocks on the user, so it is the only
    //one that runs off-thread. Everything else completes inline.
    if(code == IOCTL_BIOMETRIC_CAPTURE_DATA) {
        return egis_ioctl_capture_data(dev, ovlp, in_buf, in_size, out_buf, out_size, req);
    }

    NTSTATUS status;
    size_t transferred = 0;

    switch(code) {
        case IOCTL_BIOMETRIC_GET_ATTRIBUTES:
            status = egis_ioctl_get_attributes(dev, out_buf, out_size, &transferred);
            break;
        case IOCTL_BIOMETRIC_GET_SENSOR_STATUS:
            status = egis_ioctl_get_sensor_status(dev, out_buf, out_size, &transferred);
            break;
        case IOCTL_BIOMETRIC_RESET:
            status = egis_ioctl_reset(dev, out_buf, out_size, &transferred);
            break;
        case IOCTL_BIOMETRIC_CALIBRATE:
            status = egis_ioctl_calibrate(dev, out_buf, out_size, &transferred);
            break;
        case IOCTL_BIOMETRIC_GET_INDICATOR:
            status = egis_ioctl_get_indicator(dev, out_buf, out_size, &transferred);
            break;
        case IOCTL_BIOMETRIC_SET_INDICATOR:
            status = egis_ioctl_set_indicator(dev, in_buf, in_size, out_buf, out_size, &transferred);
            break;
        default:
            //Everything in the 0x442xxx range is an Egis-private control code.
            //The Windows driver has a named handler for each one
            //(EgisTouchFP0575.dll, dispatch table at 0x18001a1xx); none of them
            //has been mapped yet. Log loudly rather than failing silently, so
            //the DEVCTRL trace shows exactly which ones the adapters actually
            //need.
            log_warn("Unimplemented EGIS IOCTL 0x%x [in %zu bytes, out %zu bytes]", code, in_size, out_size);
            status = STATUS_NOT_SUPPORTED;
            break;
    }

    winio_complete_overlapped(ovlp, status, transferred);
    return STATUS_SUCCESS;
}

NTSTATUS egis_cancel(struct egis_device *dev, OVERLAPPED *ovlp, struct egis_request *req) {
    cant_fail_ret(pthread_mutex_lock(&dev->cap_lock));
    if(dev->cap_running) dev->cap_cancel = true;
    cant_fail_ret(pthread_mutex_unlock(&dev->cap_lock));
    return STATUS_SUCCESS;
}

void egis_cleanup(struct egis_device *dev, OVERLAPPED *ovlp, struct egis_request *req) {
    free(req);
}
