import struct

data = open(r'D:/car/Follow the box/zhiliao/资料/EaiLidarTest-V1.12.3-20241220/EaiSdk.dll', 'rb').read()

print("=== startScan full command context (+200 to +350) ===")
func_off = 0x26240
func = data[func_off:func_off+800]

# Print bytes +200 to +350 for context
chunk = func[200:360]
print(' '.join(f'{b:02X}' for b in chunk))
print()

# Look for C6 44 24 51 XX (storing CMD byte at [RSP+0x51])
print("=== MOV BYTE[RSP+0x51] instructions in startScan ===")
for i in range(len(func)-6):
    if func[i:i+4] == bytes([0xC6, 0x44, 0x24, 0x51]):
        print(f"  +{i}: C6 44 24 51 {func[i+4]:02X} -> cmd=0x{func[i+4]:02X}")

# Also look for reg-based stores near the A5 sequence
print()
print("=== Near +287 detailed context ===")
ctx = func[250:340]
print(' '.join(f'{b:02X}' for b in ctx))
print()

# The 0xA5 0x41 sequence was found at +291. 
# But in the disassembly, 0xA5 at +291 is inside "C6 44 24 50 A5" which ends at +292.
# So A5 is the CONSTANT STORED (the value 0xA5). The 0x41 that follows is NOT a command byte,
# it's the start of "41 B8 02 00 00 00" = MOV R8D,2.
# 
# That means the SECOND BYTE of the 2-byte command comes from a REGISTER.
# We need to find what was stored in [RSP+0x51] before this point.
# 
# Actually, the sendCommand might use different convention:
# - buf[0] = 0xA5 (hardcoded prefix byte, stored at [RSP+0x50])
# - buf[1] = CMD (from a register, likely filled before the call)
# 
# Let's look for the cmd value in the broader context
# The startScan function checks the model/type at various offsets
# model offset in the struct might be at [RBX+0x98] etc.

# Let's look for all branch conditions in startScan that lead to different commands
# by examining compares and jumps
print("=== Conditional branches in startScan (first 600 bytes) ===")
i = 0
while i < 600:
    b = func[i]
    # CMP instruction patterns
    if b == 0x83 and func[i+1] == 0xFE:  # CMP ESI/EDX/etc, imm8
        reg_op = func[i+1]
        imm = func[i+2]
        print(f"  +{i}: CMP reg, {imm:#04x}")
        i += 3
        continue
    if b == 0x3D:  # CMP EAX, imm32
        imm = struct.unpack_from('<I', func, i+1)[0]
        print(f"  +{i}: CMP EAX, {imm:#010x}")
        i += 5
        continue
    if b == 0x80 and func[i+1] in (0x79, 0x7B, 0x7C, 0x7D):  # CMP BYTE[reg+off], imm8
        print(f"  +{i}: CMP BYTE[...], {func[i+3]:#04x}")
    i += 1

print()
print("=== All bytes written to cmd buffer (C6 44 24 XX) in startScan ===")
for i in range(len(func)-5):
    if func[i] == 0xC6 and func[i+1] == 0x44 and func[i+2] == 0x24:
        offset = func[i+3]
        val = func[i+4]
        print(f"  +{i}: MOV BYTE[RSP+{offset:#04x}], {val:#04x}")
