import struct

data = open(r'D:/car/Follow the box/zhiliao/资料/EaiLidarTest-V1.12.3-20241220/EaiSdk.dll', 'rb').read()

# waitPackage - this is the key function that parses incoming scan packets
# Located at 0x238b0
print("=== waitPackage (0x238b0) - first 300 bytes ===")
wp = data[0x238b0:0x238b0+400]
print(' '.join(f'{b:02X}' for b in wp[:200]))
print()

# Look for checksum-related patterns in waitPackage
# XOR operations: 31 XX (XOR reg,reg), 33 XX (XOR reg,[mem]), 66 33 XX (XOR r16,[mem])
# Also look for 0xAA55 = 55 AA constants
print("=== waitPackage checksum/XOR patterns ===")
for i in range(len(wp)-5):
    if wp[i] == 0x33:  # XOR r32, [mem]
        ctx = wp[max(0,i-4):i+8]
        print(f"  XOR at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    if wp[i] == 0x66 and wp[i+1] == 0x33:  # XOR r16, [mem]
        ctx = wp[max(0,i-4):i+8]
        print(f"  XOR16 at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    if wp[i:i+2] == bytes([0x31, 0xC0]) or wp[i:i+2] == bytes([0x31, 0xDB]) or wp[i:i+2] == bytes([0x31, 0xD2]):
        ctx = wp[max(0,i-2):i+6]
        print(f"  XOR_r_r at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")

print()
# grab Scan data at 0x27a40
print("=== grabScanData (0x27a40) - OR EBX,0x55AA context ===")
gs = data[0x27a40:0x27a40+0xE00]
target = bytes([0x81, 0xCB, 0xAA, 0x55, 0x00, 0x00])  # OR EBX, 0x55AA
idx = gs.find(target)
if idx >= 0:
    ctx = gs[max(0,idx-40):idx+40]
    print(f"OR EBX,0x55AA at +{idx}:")
    print(' '.join(f'{b:02X}' for b in ctx))
    
print()
# Look at the OR EBX,0x55AA in broader context of grabScanData
print("=== All OR/XOR operations in grabScanData ===")
for i in range(min(len(gs), 0xE00)):
    # OR reg16, imm16 (66 81 C? XX XX)
    if gs[i] == 0x66 and gs[i+1] == 0x33:
        ctx = gs[max(0,i-4):i+8]
        print(f"  66 33 XOR16 at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    # XOR reg, [mem+offset] 
    if gs[i] == 0x33 and gs[i+1] in (0x44, 0x4C, 0x54, 0x5C, 0x84, 0x8C, 0x94, 0x9C):
        ctx = gs[max(0,i-4):i+12]
        print(f"  33 XOR at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
    # OR EBX, imm32
    if gs[i] == 0x81 and gs[i+1] == 0xCB:
        val = struct.unpack_from('<I', gs, i+2)[0]
        ctx = gs[max(0,i-8):i+12]
        print(f"  OR EBX,{val:#010x} at +{i}: {' '.join(f'{b:02X}' for b in ctx)}")
