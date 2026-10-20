"""Create an optional native-rate test file externally; never runs on ESP boot."""
import argparse,math,struct,wave
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description='1 kHz mono PCM16 test segments with fades and silent gaps; start playback at low device volume.')
    parser.add_argument('output',type=Path)
    parser.add_argument('--levels',type=float,nargs='+',default=[-36,-24,-12])
    args=parser.parse_args();rate=44100
    if any(not math.isfinite(db) or db>0 or db< -90 for db in args.levels):parser.error('levels must be between -90 and 0 dBFS')
    with wave.open(str(args.output),'wb') as out:
        out.setparams((1,2,rate,0,'NONE','not compressed'))
        for db in args.levels:
            amplitude=32767*10**(db/20);frames=rate*3;fade=int(rate*.005)
            for i in range(frames):
                envelope=min(1,i/fade,(frames-1-i)/fade)
                out.writeframesraw(struct.pack('<h',round(amplitude*envelope*math.sin(2*math.pi*1000*i/rate))))
            out.writeframesraw(bytes(rate*2))
    print(f'Created {args.output}: PCM16 mono 44.1 kHz, 1 kHz, {args.levels} dBFS. Upload manually to SD if needed.')

if __name__=='__main__':main()
