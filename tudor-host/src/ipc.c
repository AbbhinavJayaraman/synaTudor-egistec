#include <unistd.h>
#include <sys/socket.h>
#include <tudor/log.h>
#include "ipc.h"
#include "handler.h"

enum ipc_msg_type ipc_peek_msg(int sock) {
    enum ipc_msg_type msg_type;

    struct iovec iov = {
        .iov_base = &msg_type,
        .iov_len = sizeof(msg_type)
    };

    struct msghdr msg_hdr = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    // Use recvmsg directly, do not use cant_fail
    if (recvmsg(sock, &msg_hdr, MSG_PEEK) < 0) {
        return (enum ipc_msg_type) -1; 
    }

    return msg_type;
}

size_t ipc_recv_msg(int sock, void *buf, enum ipc_msg_type type, size_t min_sz, size_t max_sz, int *fd) {
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = max_sz
    };

    struct {
        struct cmsghdr hdr;
        int fd;
    } cmsg = {
        .hdr.cmsg_len = CMSG_LEN(sizeof(int)),
        .hdr.cmsg_level = SOL_SOCKET,
        .hdr.cmsg_type = SCM_RIGHTS
    };

    struct msghdr msg_hdr = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = &cmsg,
        .msg_controllen = sizeof(cmsg)
    };

    ssize_t msg_size = recvmsg(sock, &msg_hdr, 0);

    // 1. Handle Receive Errors Gracefully
    if (msg_size <= 0) {
        // Return 0 to indicate failure/EOF instead of aborting
        // This allows main.c to detect "no data" and proceed
        return 0; 
    }

    // 2. Check Size Bounds
    if((size_t)msg_size < min_sz || max_sz < (size_t)msg_size) {
        log_error("Invalid IPC message size: 0x%lx (min 0x%lx, max 0x%lx)", msg_size, min_sz, max_sz);
        return 0; // Return 0 instead of abort
    }

    // 3. Check Type
    if(*((enum ipc_msg_type*) buf) != type) {
        log_error("Unexpected IPC message type: 0x%x (expected 0x%x)", *((enum ipc_msg_type*) buf), type);
        return 0; // Return 0 instead of abort
    }

    //Transfer FD
    if(msg_hdr.msg_controllen > 0) {
        if(msg_hdr.msg_controllen != sizeof(cmsg) || cmsg.hdr.cmsg_len != CMSG_LEN(sizeof(int)) || cmsg.hdr.cmsg_level != SOL_SOCKET || cmsg.hdr.cmsg_type != SCM_RIGHTS) {
            log_error("Invalid IPC control message");
            return 0; // Return 0 instead of abort
        }

        if(fd) *fd = cmsg.fd;
        else close(cmsg.fd); // Use simple close, not cant_fail
    } else if(fd) *fd = -1;

    return (size_t)msg_size;
}

void ipc_send_msg(int sock, void *buf, size_t size) {
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = size
    };

    struct msghdr msg_hdr = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0
    };

    // sendmsg failure is usually critical (broken pipe), so we can keep cant_fail here
    // or relax it if you prefer logging.
    if (sendmsg(sock, &msg_hdr, 0) < 0) {
        log_error("Failed to send IPC message");
        // We can't easily recover from send failure, so aborting or returning is fine.
        // Keeping behavior similar to original for sends.
    }
}