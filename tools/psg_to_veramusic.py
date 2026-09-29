import sys
from pathlib import Path
import struct

def convert(othello_psg, veramusic_psg):
    raw = Path(othello_psg).read_bytes()
    out = bytearray()
    
    ptr = 0
    current_frame_writes = []
    
    while ptr < len(raw):
        cmd = raw[ptr]
        ptr += 1
        
        if cmd >= 0x80 and cmd < 0xFF:
            reg = cmd & 0x3F
            val = raw[ptr]
            ptr += 1
            current_frame_writes.append((reg, val))
        elif cmd == 0xFF:
            # flush last frame
            out.append(len(current_frame_writes))
            for reg, val in current_frame_writes:
                out.append(reg)
                out.append(val)
            # terminator
            out.append(0xFF)
            out.extend(struct.pack('<H', 0)) # loopFrame = 0 for now
            break
        else:
            delay = cmd
            # flush current frame writes first
            out.append(len(current_frame_writes))
            for reg, val in current_frame_writes:
                out.append(reg)
                out.append(val)
            current_frame_writes = []
            
            # emit empty frames for the remaining delay
            for _ in range(delay - 1):
                out.append(0)
                
    Path(veramusic_psg).write_bytes(out)
    print(f"Converted to {veramusic_psg}")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
