import struct

data = open(r'D:/car/Follow the box/zhiliao/资料/EaiLidarTest-V1.12.3-20241220/EaiSdk.dll', 'rb').read()

# ---- enterIntensityMode analysis ----
print("=== enterIntensityMode (0x1e760) ===")
func = data[0x1e760:0x1e760+200]
print('Bytes:', ' '.join(f'{b:02X}' for b in func[:80]))
# The buf AA 55 F1 0E sent via vtable[0xF8/8] 
# AA=0xAA, 55=0x55, F1=0xF1, 0E=0x0E
# In YDLIDAR context, if this is a command: A5 F1 0E ?? or response check AA 55 F1 0E
# Actually comparing with YDLIDAR cmd format: A5 CMD SIZE DATA
# The RESPONSE to A5 F1 command would start with A5 5A ...
# The enterIntensityMode writes AA 55 F1 0E to a response header buffer
# and checks against received response. This means:
# enterIntensityMode sends: A5 F1 (command) and expects response starting A5 5A F1 0E (?)

# Find sendCommand near enterIntensityMode to see what command is being sent
# Looking at YDLidar::enterIntensityMode at 0x19b80
print()
print("=== YDLidar::enterIntensityMode (0x19b80) ===")
func2 = data[0x19b80:0x19b80+200]
print('Bytes:', ' '.join(f'{b:02X}' for b in func2[:150]))
for i in range(len(func2)-1):
    if func2[i] == 0xA5:
        ctx = func2[max(0,i-4):i+4]
        print(f'  A5 at +{i}: {" ".join(f"{b:02X}" for b in ctx)}')

print()
print("=== sendCommand (0x1f750) ===")
func3 = data[0x1f750:0x1f750+200]
print('Bytes:', ' '.join(f'{b:02X}' for b in func3[:150]))
for i in range(len(func3)-1):
    if func3[i] == 0xA5:
        ctx = func3[max(0,i-4):i+4]
        print(f'  A5 at +{i}: {" ".join(f"{b:02X}" for b in ctx)}')

print()
print("=== startScan YDLidarDriver (0x26240) - A5 cmd search ===")
func4 = data[0x26240:0x26240+800]
for i in range(len(func4)-1):
    if func4[i] == 0xA5:
        ctx = func4[max(0,i-8):i+8]
        print(f'  A5 at +{i}: {" ".join(f"{b:02X}" for b in ctx)}')

print()
print("=== YDLidar::startScan (0x16cb0) ===")
func5 = data[0x16cb0:0x16cb0+300]
print('Bytes:', ' '.join(f'{b:02X}' for b in func5[:200]))
