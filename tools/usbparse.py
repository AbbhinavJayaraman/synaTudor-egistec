import struct, sys

def pcapng_blocks(path):
    d = open(path,'rb').read()
    off = 0; endian = '<'; linktype = None
    while off + 12 <= len(d):
        btype = struct.unpack_from(endian+'I', d, off)[0]
        if btype == 0x0A0D0D0A:
            bom = struct.unpack_from('<I', d, off+8)[0]
            endian = '<' if bom == 0x1A2B3C4D else '>'
            btype = struct.unpack_from(endian+'I', d, off)[0]
        blen = struct.unpack_from(endian+'I', d, off+4)[0]
        if blen < 12 or off+blen > len(d): break
        body = d[off+8:off+blen-4]
        if btype == 0x00000001:      # IDB
            linktype = struct.unpack_from(endian+'H', body, 0)[0]
        elif btype == 0x00000006:    # EPB
            caplen = struct.unpack_from(endian+'I', body, 12)[0]
            ts = (struct.unpack_from(endian+'I', body, 4)[0] << 32) | struct.unpack_from(endian+'I', body, 8)[0]
            yield linktype, ts, body[20:20+caplen]
        elif btype == 0x00000003:    # simple packet
            yield linktype, 0, body[4:]
        off += blen

def usbpcap(pkt):
    # USBPCAP_BUFFER_PACKET_HEADER
    if len(pkt) < 27: return None
    hlen = struct.unpack_from('<H', pkt, 0)[0]
    if hlen > len(pkt): return None
    irp, status, func, info = struct.unpack_from('<QIHB', pkt, 2)
    bus, dev = struct.unpack_from('<HH', pkt, 17)
    ep, xfer = struct.unpack_from('<BB', pkt, 21)
    dlen = struct.unpack_from('<I', pkt, 23)[0]
    data = pkt[hlen:hlen+dlen]
    return dict(hlen=hlen, irp=irp, status=status, info=info, bus=bus, dev=dev,
                ep=ep, xfer=xfer, dlen=dlen, data=data)

def usb_linux(pkt, mmapped=True):
    # linux usbmon header (48 bytes base)
    if len(pkt) < 48: return None
    urb, ev, xfer, ep, devnum, busnum = struct.unpack_from('<QBBBBH', pkt, 0)
    dlen = struct.unpack_from('<i', pkt, 36)[0]
    data = pkt[64:] if mmapped else pkt[48:]
    XT = {0:'ISO',1:'INT',2:'CTL',3:'BULK'}
    return dict(irp=urb, info=(1 if ev == 0x43 else 0), ep=ep, xfer=xfer,
                dlen=max(dlen,0), data=data, bus=busnum, dev=devnum, status=0)

def read(path):
    out = []
    for lt, ts, pkt in pcapng_blocks(path):
        if lt == 249:   r = usbpcap(pkt)
        elif lt == 220: r = usb_linux(pkt, True)
        elif lt == 189: r = usb_linux(pkt, False)
        else: continue
        if r: r['ts'] = ts; r['lt'] = lt; out.append(r)
    return out

if __name__ == '__main__':
    for p in sys.argv[1:]:
        pk = read(p)
        lts = set(r['lt'] for r in pk)
        print(f"{p}: {len(pk)} usb packets, linktype(s) {lts}")
        from collections import Counter
        print("  endpoints:", Counter((hex(r['ep']), r['xfer']) for r in pk).most_common(8))

# Kept because thread 7 was settled with it and the next capture question will
# want it again. There is no tshark or scapy in this project's toolchain.
#
#   python3 tools/usbparse.py ../python-egistec-eh575/wireshark/*.pcapng
#
# A frame is a 5356-byte bulk IN on EP 0x82, which USBPcap reports as a
# 5120-byte read plus a 236-byte read sharing one IRP id. Reassemble with:
#
#   ins = [r for r in read(path)
#          if r['ep'] == 0x82 and r['xfer'] == 3 and r['info'] & 1 and r['data']]
#   frames = [bytes(ins[i]['data'] + ins[i+1]['data'])
#             for i in range(len(ins)-1)
#             if len(ins[i]['data']) == 5120 and len(ins[i+1]['data']) == 236]
#
# Each frame is then 103 x 52 at offset 0.
