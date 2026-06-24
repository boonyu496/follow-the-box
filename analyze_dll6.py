import struct

data = open(r'D:/car/Follow the box/zhiliao/资料/EaiLidarTest-V1.12.3-20241220/EaiSdk.dll', 'rb').read()

wp = data[0x238b0:0x238b0+0x1600]

# Find the 0x66 handling section
# We found: 3C 66 0F 85 ED 06 00 00 at around +462
# If AL==0x66, fall through. Let's trace the code after the 0x66 check

idx = 0
while idx < len(wp):
    if wp[idx:idx+2] == bytes([0x3C, 0x66]):
        print(f"Found 0x66 compare at +{idx}")
        # Look at the next 200 bytes (the 0x66 handler)
        handler = wp[idx:idx+200]
        print(' '.join(f'{b:02X}' for b in handler))
        break
    idx += 1

print()
# Also find what happens after the 0x55 check at +441
# MOV R10D, 0x55AA; CMP AL, 0x55; ... 74 6D (JE +0x6D)
# The JE target is at +441+5+2 + 0x6D = ???
# Let me find the 3C 55 comparison
for i in range(len(wp)-10):
    if wp[i:i+2] == bytes([0x3C, 0x55]) and wp[i+2:i+4] == bytes([0x66, 0x44]):
        jmp_offset = wp[i+2+6+2]  # find JE after the MOV WORD instruction
        print(f"Found 0x55 compare sequence at +{i}")
        ctx = wp[i:i+100]
        print(' '.join(f'{b:02X}' for b in ctx))
        break

print()
# Let's look at the section starting at +441 and trace the jump targets
section = wp[441:441+200]
print(f"Section at +441 to +641:")
print(' '.join(f'{b:02X}' for b in section))
print()

# Find JE at +462: 74 6D = JE +109 relative to next instruction
# Next instr after 74 6D is at +464
# Jump target = 464 + 0x6D = 0x1D1 = +465
# But let's also trace the 0xAA and 0x66 comparisons

# 3C 55 is at position within section
for i in range(len(section)):
    if section[i] == 0x3C and section[i+1] in (0x55, 0xAA, 0x66):
        print(f"  CMP AL, {section[i+1]:02X} at +{441+i}")
        # Next instruction
        if section[i+2] in (0x74, 0x75):  # JE/JNE rel8
            target = 441+i+2+2 + (section[i+3] if section[i+2] == 0x74 else -section[i+3])
            print(f"    -> JE/JNZ to +{target}")
        elif section[i+2:i+4] == bytes([0x0F, 0x84]) or section[i+2:i+4] == bytes([0x0F, 0x85]):
            off = struct.unpack_from('<i', section, i+4)[0]
            target = 441+i+2+6 + off
            print(f"    -> J?? to +{target}")
