"""Cut a FluidSynth render of a MIDI file at the MIDI's own end: tools/midi_trim.py <mid> <wav>.
FluidSynth keeps rendering until the reverb dies out (3-10 s of near silence), but DirectMusic loops
the segment at the MIDI's end, so untrimmed renders leave a gap before the music restarts."""
import struct, sys, wave


def midi_len(path):
    d=open(path,'rb').read()
    assert d[:4]==b'MThd'
    fmt,ntrk,div=struct.unpack('>HHH',d[8:14]); p=14
    events=[]  # (tick, tempo or None)
    end=0
    for t in range(ntrk):
        assert d[p:p+4]==b'MTrk'; ln=struct.unpack('>I',d[p+4:p+8])[0]; q=p+8; e=q+ln; tick=0; rs=0
        while q<e:
            v=0
            while True:
                b=d[q]; q+=1; v=(v<<7)|(b&0x7f)
                if b<0x80: break
            tick+=v; st=d[q]
            if st==0xff:
                typ=d[q+1]; q+=2; l=0
                while True:
                    b=d[q]; q+=1; l=(l<<7)|(b&0x7f)
                    if b<0x80: break
                if typ==0x51: events.append((tick,int.from_bytes(d[q:q+3],'big')))
                q+=l
            elif st in (0xf0,0xf7):
                q+=1; l=0
                while True:
                    b=d[q]; q+=1; l=(l<<7)|(b&0x7f)
                    if b<0x80: break
                q+=l
            else:
                if st&0x80: rs=st; q+=1
                q+= 1 if (rs&0xf0) in (0xc0,0xd0) else 2
        end=max(end,tick); p=e
    events.sort(); tempo=500000; last=0; sec=0
    for tk,tp in events:
        if tk>end: break
        sec+=(tk-last)*tempo/div/1e6; last=tk; tempo=tp
    return sec+(end-last)*tempo/div/1e6


def main():
    mid, path = sys.argv[1], sys.argv[2]
    w = wave.open(path)
    params, frames = w.getparams(), w.readframes(w.getnframes())
    w.close()
    n = min(params.nframes, int(round(midi_len(mid) * params.framerate)))
    w = wave.open(path, "wb")
    w.setparams(params)
    w.writeframes(frames[:n * params.sampwidth * params.nchannels])
    w.close()


main()
