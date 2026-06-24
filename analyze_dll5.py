import struct

data = open(r'D:/car/Follow the box/zhiliao/资料/EaiLidarTest-V1.12.3-20241220/EaiSdk.dll', 'rb').read()

# waitPackage at 0x238b0 - find the checksum computation loop
# Starting with the XOR R8D, R8D at +116, look for the main loop
wp = data[0x238b0:0x238b0+0x500]
print("=== waitPackage bytes +100 to +300 ===")
print(' '.join(f'{b:02X}' for b in wp[100:300]))
print()

# Look for 0F B6 (MOVZX byte read) followed by XOR/ADD - typical checksum accumulation
print("=== Likely checksum ops in waitPackage ===")
for i in range(100, min(0x500, len(wp))-10):
    # MOVZX eax, byte[...] followed by XOR
    if wp[i] in (0x0F,) and wp[i+1] == 0xB6:  # MOVZX byte
        ctx = wp[i:i+15]
        print(f"  MOVZX byte at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    # XOR r16, r16 or XOR reg,[mem] for 16-bit values
    if wp[i] == 0x66 and wp[i+1] in (0x31, 0x33):
        ctx = wp[i:i+8]
        print(f"  XOR16 at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    # 66 45 33 - XOR r16, [r64+]
    if wp[i] == 0x66 and wp[i+1] == 0x45:
        ctx = wp[i:i+8]
        print(f"  66 45 at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    # 45 33 - XOR R?D, [R?+] (REX prefix)
    if wp[i] == 0x45 and wp[i+1] in (0x33,):
        ctx = wp[i:i+8]
        print(f"  45 33 XOR at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")

print()
# Let's also look at the 0xAA55 comparison in waitPackage
# and the bytes around it for the packet header check
print("=== AA55 related ops in waitPackage ===")
for i in range(len(wp)-4):
    if wp[i] in (0x3D, 0x81, 0x66, 0x41) and (
        (wp[i+1:i+3] == bytes([0x55, 0xAA])) or  
        (wp[i+1:i+3] == bytes([0xAA, 0x55]))):
        ctx = wp[max(0,i-4):i+12]
        print(f"  AA55/55AA related at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")

print()
# Full waitPackage from +280 to +500
print("=== waitPackage +280 to +500 ===")
print(' '.join(f'{b:02X}' for b in wp[280:500]))
