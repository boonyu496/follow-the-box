import struct

data = open(r'D:/car/Follow the box/zhiliao/资料/EaiLidarTest-V1.12.3-20241220/EaiSdk.dll', 'rb').read()

# Find the startScan command sequence more carefully
# startScan at 0x26240, A5 at +291
func_off = 0x26240
func = data[func_off:func_off+800]

# Find all "C6 44 24 ?? A5" patterns (MOV BYTE[RSP+N], 0xA5)
# followed by what comes next for the command byte
print("=== startScan command sequences ===")
i = 0
while i < len(func) - 10:
    # Pattern: C6 44 24 ?? A5 (MOV BYTE[RSP+offset], A5)
    if func[i] == 0xC6 and func[i+1] == 0x44 and func[i+2] == 0x24 and func[i+4] == 0xA5:
        offset_byte = func[i+3]
        # Look for C6 44 24 (offset+1) CMD to get the command byte
        cmd_byte = None
        for j in range(i+5, min(i+30, len(func))):
            if (func[j] == 0xC6 and func[j+1] == 0x44 and func[j+2] == 0x24 and 
                func[j+3] == (offset_byte + 1) & 0xFF):
                cmd_byte = func[j+4]
                break
            # Also look for 44 88 XX 24 YY pattern (MOV BYTE[RSP+Y], R??B)
        ctx = func[i:i+20]
        cmd_str = f"0x{cmd_byte:02X}" if cmd_byte is not None else "?? (from register)"
        print(f"  +{i}: A5 CMD={cmd_str}, ctx={' '.join(f'{b:02X}' for b in ctx)}")
    i += 1

# Also look for direct A5 command byte pairs in code
print()
print("=== Direct A5 pairs in startScan ===")
for i in range(len(func) - 5):
    if func[i] == 0xA5:
        ctx = func[max(0,i-10):i+10]
        if any(func[i+1:i+2] == bytes([b]) for b in [0x60, 0x65, 0x90, 0x40, 0x96, 0xF1, 0x41, 0x00, 0x03, 0x70]):
            print(f"  +{i}: A5 {func[i+1]:02X}, ctx={' '.join(f'{b:02X}' for b in ctx)}")

print()
# Now look at enterIntensityMode at YDLidarDriver level (0x1e760) more carefully
# The constant AA 55 F1 0E stored - let's check what vtable slot F8 does
# F8/8 = 31 = vtable[31]. The function at vtable+F8 is likely sendCommand
# Let's read the vtable of YDLidarDriver to find slot 31
# Actually let's find the vtable by looking at the class construction

# Search for the byte pattern that builds the 4-byte struct in enterIntensityMode
# C7 44 24 2C AA 55 F1 0E -> the 4 bytes AA 55 F1 0E
# If this is checking the response header (0xA5 0x5A), the A5 5A pattern should be nearby
print("=== enterIntensityMode analysis ===")
eim = data[0x1e760:0x1e760+100]
print(' '.join(f'{b:02X}' for b in eim))
print()

# A YDLIDAR response header is: A5 5A [payload_len 4B] [type 1B]
# If enterIntensityMode SENDS a command A5 F1 and reads back A5 5A ... data
# The function stores AA 55 F1 0E as "expected response" and checks it
# A5 5A vs AA 55 ? or is it checking specific bytes?

# More likely: the function is using sendCommand through vtable [RAX+F8]
# The first arg after RCX=this is: RDX = pointer to 4-byte struct {0xAA, 0x55, 0xF1, 0x0E}
# The vtable slot [RAX+0xF8] = ???
# YDLIDAR sendCommand format: sends A5 CMD 00 (2 or 3 bytes)
# The response check: waits for A5 5A [type] [len] [data]

# Let's look at what sendCommand does when called with the 4-byte buffer
# sendCommand at 0x1f750 stores 0xA5 at [RSP+0x26] then the cmd byte
# But it receives (this, data_ptr, len) 
# Actually if it's NOT sendCommand but readData or waitForHeader

# Let me check the waitRespHeader at 0x1f750+offset... 
# The exported functions show: waitRespHeader and waitRespHeaderEx
print("=== Looking for waitRespHeader ===")
# Find in exports
import re
strs = re.findall(b'[ -~]{8,}', data)
for s in strs:
    sd = s.decode('latin1')
    if 'waitResp' in sd or 'waitForResp' in sd:
        print(repr(sd))
