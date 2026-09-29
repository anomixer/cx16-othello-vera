import sys
from pathlib import Path
import mido

NOTE_MAP = {
    14: 0,   # C
    0:  1,   # C#
    1:  2,   # D
    2:  3,   # D#
    4:  4,   # E
    5:  5,   # F
    6:  6,   # F#
    8:  7,   # G
    9:  8,   # G#
    10: 9,   # A
    12: 10,  # A#
    13: 11   # B
}

def convert(zsm_path, out_path):
    raw = Path(zsm_path).read_bytes()
    
    mid = mido.MidiFile(ticks_per_beat=96)
    track = mido.MidiTrack()
    mid.tracks.append(track)
    track.append(mido.MetaMessage('set_tempo', tempo=mido.bpm2tempo(120)))
    
    ptr = 16
    tick_count = 0
    notes = {}
    
    # map channel to current note playing, so we can emit note_off
    playing_note = {}
    
    # 60Hz tick duration in MIDI ticks (assuming 120bpm, 96 TPB -> 192 ticks/sec)
    # 192 ticks / 60 = 3.2 MIDI ticks per 60Hz tick?
    # Better: 120 BPM = 2 beats per second. If TPB=480, 960 ticks/sec. 960/60 = 16 ticks per frame.
    mid.ticks_per_beat = 480
    ticks_per_frame = 16
    
    pending_midi_ticks = 0
    
    while ptr < len(raw):
        cmd = raw[ptr]
        ptr += 1
        
        if cmd < 0x40:
            ptr += 1
        elif cmd == 0x40:
            ext = raw[ptr]
            ptr += 1 + (ext & 0x3F)
        elif cmd < 0x80:
            count = cmd & 0x3F
            for _ in range(count):
                reg = raw[ptr]
                val = raw[ptr+1]
                ptr += 2
                
                if 0x28 <= reg <= 0x2F:
                    notes[reg - 0x28] = val
                elif reg == 0x08:
                    ch = val & 7
                    ops = (val >> 3) & 0x0F
                    if ops > 0:
                        kc = notes.get(ch, 0)
                        octave = (kc >> 4) & 7
                        n_code = kc & 0x0F
                        semi = NOTE_MAP.get(n_code, 0)
                        midi = (octave + 1) * 12 + semi
                        
                        if midi > 0:
                            # Note off previous if needed
                            if ch in playing_note:
                                track.append(mido.Message('note_off', note=playing_note[ch], velocity=64, time=pending_midi_ticks, channel=ch))
                                pending_midi_ticks = 0
                            
                            track.append(mido.Message('note_on', note=midi, velocity=100, time=pending_midi_ticks, channel=ch))
                            pending_midi_ticks = 0
                            playing_note[ch] = midi
                    else:
                        # Key off
                        if ch in playing_note:
                            track.append(mido.Message('note_off', note=playing_note[ch], velocity=64, time=pending_midi_ticks, channel=ch))
                            pending_midi_ticks = 0
                            del playing_note[ch]
        elif cmd == 0x80:
            break
        else:
            frames = cmd & 0x7F
            pending_midi_ticks += frames * ticks_per_frame
            tick_count += frames
            
    track.append(mido.MetaMessage('end_of_track', time=pending_midi_ticks))
    mid.save(out_path)
    print(f"Saved {out_path}")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
