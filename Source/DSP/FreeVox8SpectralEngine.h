#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include "FreeVox8FFT.h"

// Spectral Ghost 1024: high-resolution STFT/OLA backend, FFT
// cross-synthesis resynthesis (Vox8 paper §6 rank 9, Decision 5).
//
// REDESIGN 2026-11-06 (rank 9, FFT cross-synthesis): the resynthesis
// as shipped swept ONE band's envelope across each frame
// (e = readEnv(t*127 - shift)), so the output did not follow the
// modulator — it failed all four resynthesis sub-tests
// (formant-shift centroid static, burst transfer 1.01x, morph 1.42 dB,
// Mask pocket ratio 4.34) and passed the carrier at a 0.72 floor with
// silence in (0.083 out, -21.6 dBFS — a golden-test failure). That
// resynthesis is gone. The analysis half (rings, frozen[] snapshot,
// display taps) is kept; the resynthesis is now true spectral
// cross-synthesis per frame (hop 256):
//   - Modulator: windowed FFT -> magnitudes -> 128 log-band envelope
//     (replaces the Goertzel analyseBins(); same envelope-follower
//     rates, same frozen[] leaky snapshot). Cheaper, too: 3 FFTs per
//     frame replace ~45k Goertzel iterations with a cos/sin each.
//   - Carrier: windowed FFT -> magnitudes + phase (phase is KEPT).
//   - Per FFT bin k: output magnitude = |carrier[k]| * gain[k],
//     gain from the modulator envelope evaluated at the bin's true
//     frequency. Vocoder: gain = e * drive. Ghost: x (1 + ghost*2.4).
//     Morph: two envelopes interpolated per log band, e = (1-a)*e1 +
//     a*e2 (a = ghost), imposed on one carrier — the real morph, not
//     the old blend. Mask: gain = 1 - min(0.96, e*depth*3.2) where e
//     is the peak of the formant-shifted envelope over a +-2 log-band
//     window around the bin (maskEnvelopeAtBin) -- a mask must duck
//     the region around a formant, not just the exact bin of a
//     harmonic. The Vocoder/Ghost/Morph use the unspread envelope.
//   - IFFT (carrier phase) -> Hann synthesis window -> OLA (÷1.5,
//     the Hann w^2 COLA constant at 75% overlap).
//   - NO 0.72 carrier floor: silence in -> silence out (the golden
//     test). Air stays a time-domain addition on the output frame.
//   - Formant shift is an exact frequency-ratio remap: the envelope
//     is evaluated at f * 2^(-shift/12), so +12 semitones moves the
//     formants a true octave — no bins/octave constant, which
//     dissolves Decision 1 (unify at ~15.25) for this engine. The
//     filterbank keeps its own 8 bins/octave scale.
//   - The carrier blend is unchanged (Ghost 0.46/0.32/0.18 x0.72 +
//     28% dry) — carrier unification (Decision 3) is Gary's call.
//   - Latency is unchanged: 1024-sample frame, hop 256 (~990 samples
//     / 22.45 ms measured). Freeze blur stays a known, bounded
//     limitation (the WASPAA correction failed its pilot, so it stays
//     out); the frozen snapshot is now FFT-derived, so the blur
//     NUMBER may differ from the Goertzel baseline — re-measure it
//     before quoting it.
// Prepared storage only; process() does no allocation, file I/O,
// locks, or threads.
class FreeVox8SpectralEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kFftSize = 1024;
    static constexpr int kHopSize = 256;
    static constexpr int kDisplayBins = 128;
    static constexpr int kNumBins = kFftSize / 2 + 1; // 513 (0..Nyquist)
    struct Parameters { float mix=1, maskDepth=.5f, clarity=.55f, formantShift=0, ghost=0, air=.15f, freeze=0, transientProtect=.35f, stereoWidth=1, outputGainDb=0; int mode=0; };
    void prepare(double sampleRate, int, int numChannels){ sr=sampleRate>0?sampleRate:44100.0; channels=juce::jlimit(1,kMaxChannels,numChannels); for(int i=0;i<kFftSize;++i){double t=(double)i/(double)(kFftSize-1); window[(size_t)i]=(float)(0.5-0.5*std::cos(juce::MathConstants<double>::twoPi*t));} buildBinTables(); reset(); }
    void reset(){ for(auto& a:inputRing)a.fill(0); for(auto& a:modRing)a.fill(0); for(auto& a:modRing2)a.fill(0); for(auto& a:olaRing)a.fill(0); for(auto& a:env)a.fill(0); for(auto& a:frozen)a.fill(0); for(auto& a:env2)a.fill(0); for(auto& a:frozen2)a.fill(0); for(auto& b:display)b.store(0,std::memory_order_relaxed); for(auto& b:maskDisplay)b.store(0,std::memory_order_relaxed); phase={0,0}; previousInput={0,0}; writeIndex=0; hopCountdown=kHopSize; sidechainPresent=false; }
    void setMidiNote(int n) noexcept { midiNote=juce::jlimit(0,127,n); }
    void setParameters(const Parameters& p) noexcept { params=p; }
    // Transient Protect mirrors the Low Latency 128 filterbank (§3.7): a
    // first-difference transient gate on the dry input, mixed back in before
    // the wet/dry mix so onsets pass through un-smeared by the OLA path.
    void process(juce::AudioBuffer<float>& buffer,const juce::AudioBuffer<float>* sidechain){ const int ns=buffer.getNumSamples(), nc=juce::jmin(channels,buffer.getNumChannels()); const float mix=juce::jlimit(0.f,1.f,params.mix), dryMix=1-mix, out=juce::Decibels::decibelsToGain(params.outputGainDb), tp=juce::jlimit(0.f,1.f,params.transientProtect); for(int n=0;n<ns;++n){ for(int ch=0;ch<nc;++ch){auto*x=buffer.getWritePointer(ch); float dry=x[n]; const float* scPtr=(sidechain&&sidechain->getNumChannels()>0)?sidechain->getReadPointer(juce::jmin(ch,sidechain->getNumChannels()-1)):nullptr; float transient=juce::jlimit(0.f,1.f,std::abs(dry-previousInput[(size_t)ch])*12.f); previousInput[(size_t)ch]=dry; float transientReturn=dry*tp*transient*.12f; inputRing[(size_t)ch][(size_t)writeIndex]=makeCarrier(dry,ch); modRing[(size_t)ch][(size_t)writeIndex]=dry; if(scPtr){modRing2[(size_t)ch][(size_t)writeIndex]=scPtr[n]; sidechainPresent=true;} else sidechainPresent=false; float wet=olaRing[(size_t)ch][(size_t)writeIndex]+transientReturn; olaRing[(size_t)ch][(size_t)writeIndex]=0; x[n]=sat((dry*dryMix+wet*mix)*out);} writeIndex=(writeIndex+1)&(kFftSize-1); if(--hopCountdown<=0){hopCountdown=kHopSize; renderFrame(nc);} } applyWidth(buffer,ns,nc,juce::jlimit(0.f,2.f,params.stereoWidth)); }
    float getDisplayBin(int i) const noexcept { i=juce::jlimit(0,kDisplayBins-1,i); return display[(size_t)i].load(std::memory_order_relaxed); }
    int getDisplayBinCount() const noexcept { return kDisplayBins; }
    // Per-bin Mask readout: the reduction the Ghost Mask applies in
    // this bin (0..0.96), written per frame in analyseRing, 0 in every
    // other mode. Same atomic-tap discipline as display. The renderFrame
    // Mask uses the formant-shifted envelope; this tap reads the
    // unshifted per-bin envelope, so it is the meter value, not the
    // exact per-sample gain -- a few bins off under formant shift.
    float getMaskBin(int i) const noexcept { i=juce::jlimit(0,kDisplayBins-1,i); return maskDisplay[(size_t)i].load(std::memory_order_relaxed); }
    int getMaskBinCount() const noexcept { return kDisplayBins; }
    float getMaskMeanReduction() const noexcept { float s=0; for(auto&v:maskDisplay)s+=v.load(std::memory_order_relaxed); return s/(float)kDisplayBins; }
private:
    using Frame=std::array<float,kFftSize>; using Spec=std::array<float,kDisplayBins>;
    using ComplexFrame=std::array<std::complex<float>,kFftSize>;
    std::array<Frame,kMaxChannels> inputRing{},modRing{},modRing2{},olaRing{}; std::array<Spec,kMaxChannels> env{},frozen{},env2{},frozen2{}; std::array<float,kFftSize> window{}; std::array<std::atomic<float>,kDisplayBins> display{}; std::array<std::atomic<float>,kDisplayBins> maskDisplay{}; std::array<double,kMaxChannels> phase{0,0}; std::array<float,kMaxChannels> previousInput{0,0}; Parameters params; double sr=44100; int channels=2, writeIndex=0, hopCountdown=kHopSize, midiNote=48; uint32_t noiseState=0xA53C9E21u; bool sidechainPresent=false;
    FreeVox8FFT fft{kFftSize};
    ComplexFrame fftBuf{}, outBuf{};
    // Per-bin tables, built in prepare() (they depend on the sample rate):
    // binFreq[k] = k*sr/1024; binPos[k] = log-band position of bin k
    // (0..127, clamped); bandBinLo/Hi[b] = FFT bins inside log band b.
    std::array<float,kNumBins> binFreq{}, binPos{};
    std::array<int,kDisplayBins> bandBinLo{}, bandBinHi{};
    float bandsPerSemitone=1.261f; // 127 steps over log2(18500/55) octaves, /12
    void buildBinTables(){ const double logSpan=std::log2(18500.0/55.0); bandsPerSemitone=(float)(127.0/logSpan/12.0); for(int k=0;k<kNumBins;++k){ float f=(float)k*(float)sr/(float)kFftSize; binFreq[(size_t)k]=f; float pos=(f<=55.f)?0.f:(float)(127.0*std::log2((double)f/55.0)/logSpan); binPos[(size_t)k]=juce::jlimit(0.f,127.f,pos);} const double r=std::pow(18500.0/55.0,1.0/128.0); for(int b=0;b<kDisplayBins;++b){ float lo=55.f*(float)std::pow(r,b), hi=55.f*(float)std::pow(r,b+1); int first=-1,last=-1; for(int k=0;k<kNumBins;++k){ if(binFreq[(size_t)k]>=lo&&binFreq[(size_t)k]<hi){ if(first<0)first=k; last=k; } } if(first<0){ int nearest=0; float bd=1e9f; float centre=55.f*(float)std::pow(r,b+0.5); for(int k=0;k<kNumBins;++k){ float d=std::abs(binFreq[(size_t)k]-centre); if(d<bd){bd=d;nearest=k;} } first=last=nearest; } bandBinLo[(size_t)b]=first; bandBinHi[(size_t)b]=last; } }
    // FFT-based modulator analysis (replaces the Goertzel analyseBins):
    // windowed FFT of one ring -> per-bin magnitudes -> mean magnitude
    // per log band (amplitude scale: |M| * 4/N) -> envelope follower
    // (fast attack 0.24/0.76, slow release 0.92/0.08, once per frame) ->
    // env[] (+ frozen[] leaky average when freeze is off) + display and
    // Mask meter taps. `ring` is modRing (main input) or modRing2
    // (sidechain); `dst`/`dstFrozen` are env/frozen or env2/frozen2.
    void analyseRing(int ch,const Frame& ring,Spec& dst,Spec& dstFrozen,bool fr){ for(int i=0;i<kFftSize;++i){int idx=(writeIndex+i)&(kFftSize-1); fftBuf[(size_t)i]=std::complex<float>(ring[(size_t)idx]*window[(size_t)i],0.f);} fft.forward(fftBuf.data()); for(int b=0;b<kDisplayBins;++b){ float sum=0; int cnt=0; for(int k=bandBinLo[(size_t)b];k<=bandBinHi[(size_t)b];++k){sum+=std::abs(fftBuf[(size_t)k]);++cnt;} float m=(cnt>0)?sum/(float)cnt*(4.0f/(float)kFftSize):0.f; float& E=dst[(size_t)b]; E=m>E?.24f*E+.76f*m:.92f*E+.08f*m; if(!fr)dstFrozen[(size_t)b]=.998f*dstFrozen[(size_t)b]+.002f*E; float v=fr?dstFrozen[(size_t)b]:E; float old=display[(size_t)b].load(std::memory_order_relaxed); display[(size_t)b].store(old*.88f+juce::jlimit(0.f,1.f,v*4.f)*.12f,std::memory_order_relaxed); float mred=0.f; if(params.mode==1){ const Spec& src=fr?dstFrozen:dst; float mx=0.f; int lo2=juce::jmax(0,b-2), hi2=juce::jmin(kDisplayBins-1,b+2); for(int j=lo2;j<=hi2;++j)mx=juce::jmax(mx,src[(size_t)j]); mred=juce::jlimit(0.f,.96f,mx*params.maskDepth*3.2f); } float oldM=maskDisplay[(size_t)b].load(std::memory_order_relaxed); maskDisplay[(size_t)b].store(oldM*.88f+mred*.12f,std::memory_order_relaxed);} }
    // Envelope at FFT bin k, formant-shifted by exact semitone ratio:
    // the envelope is evaluated at f_k * 2^(-shift/12), i.e. the
    // log-band position moves down by shift*bandsPerSemitone. Reading
    // the envelope from a lower frequency and imposing it higher moves
    // the formants UP by the labelled amount -- a true octave at +12.
    float envelopeAtBin(const Spec& e,int k,float shift) const noexcept{ float pos=binPos[(size_t)k]-shift*bandsPerSemitone; pos=juce::jlimit(0.f,127.f,pos); int lo=(int)std::floor(pos),hi=juce::jmin(kDisplayBins-1,lo+1); float t=pos-(float)lo; return e[(size_t)lo]+(e[(size_t)hi]-e[(size_t)lo])*t; }
    // Mask envelope at FFT bin k: the peak of the formant-shifted
    // envelope over a +-2 log-band window (~+-0.13 octaves) around bin
    // k. A mask must duck the REGION around a formant, not just the
    // exact bin of a harmonic: the /a/ F1 formant sits at 730 Hz,
    // between the 660 Hz and 880 Hz harmonics, so the raw bin envelope
    // there is only the skirt (~0.05) and the pocket reads 0.88x.
    // Spreading the peak +-2 bands lets the formant-boosted 660 Hz
    // harmonic duck its own formant region. Mask only -- the
    // Vocoder/Ghost/Morph keep the unspread envelopeAtBin.
    float maskEnvelopeAtBin(const Spec& e,int k,float shift) const noexcept{ float pos=binPos[(size_t)k]-shift*bandsPerSemitone; pos=juce::jlimit(0.f,127.f,pos); int lo=juce::jmax(0,(int)std::floor(pos)-2), hi=juce::jmin(kDisplayBins-1,(int)std::floor(pos)+2); float m=0.f; for(int b=lo;b<=hi;++b)m=juce::jmax(m,e[(size_t)b]); return m; }
    void renderFrame(int nc){ bool fr=params.freeze>.5f; float clarity=juce::jlimit(0.f,1.f,params.clarity), ghost=juce::jlimit(0.f,1.f,params.ghost), air=juce::jlimit(0.f,1.f,params.air), shift=params.formantShift; float drive=2.8f+clarity*4.f; bool isMorph=params.mode==3; for(int ch=0;ch<nc;++ch){ // Modulator envelope(s): sidechain replaces the main input
        // as the modulator in Vocoder/Mask/Ghost (as before); Morph
        // analyses both (main = e1, sidechain = e2) and interpolates.
        if(isMorph){ analyseRing(ch,modRing[(size_t)ch],env[(size_t)ch],frozen[(size_t)ch],fr); if(sidechainPresent)analyseRing(ch,modRing2[(size_t)ch],env2[(size_t)ch],frozen2[(size_t)ch],fr); } else if(sidechainPresent){ analyseRing(ch,modRing2[(size_t)ch],env[(size_t)ch],frozen[(size_t)ch],fr); } else { analyseRing(ch,modRing[(size_t)ch],env[(size_t)ch],frozen[(size_t)ch],fr); } const Spec& e1=fr?frozen[(size_t)ch]:env[(size_t)ch]; const Spec& e2=(isMorph&&sidechainPresent)?(fr?frozen2[(size_t)ch]:env2[(size_t)ch]):(fr?frozen[(size_t)ch]:env[(size_t)ch]); // Carrier FFT (phase kept).
        for(int i=0;i<kFftSize;++i){int idx=(writeIndex+i)&(kFftSize-1); fftBuf[(size_t)i]=std::complex<float>(inputRing[(size_t)ch][(size_t)idx]*window[(size_t)i],0.f);} fft.forward(fftBuf.data()); // Per-bin cross-synthesis gains.
        for(int k=0;k<kNumBins;++k){ float g; if(params.mode==1){ float e=maskEnvelopeAtBin(e1,k,shift); g=1.f-juce::jlimit(0.f,.96f,e*params.maskDepth*3.2f); } else { float e; if(isMorph){ float a=envelopeAtBin(e1,k,shift), b=envelopeAtBin(e2,k,shift); e=(1.f-ghost)*a+ghost*b; } else e=envelopeAtBin(e1,k,shift); g=e*drive; if(params.mode==2)g*=1.f+ghost*2.4f; } outBuf[(size_t)k]=fftBuf[(size_t)k]*g; } for(int k=kNumBins;k<kFftSize;++k)outBuf[(size_t)k]=std::conj(outBuf[(size_t)(kFftSize-k)]); fft.inverse(outBuf.data()); for(int i=0;i<kFftSize;++i){ int idx=(writeIndex+i)&(kFftSize-1); float t=(float)i/(float)(kFftSize-1); float y=outBuf[(size_t)i].real()+air*t*t*whiteNoise()*.018f; olaRing[(size_t)ch][(size_t)idx]+=y*window[(size_t)i]*(1.f/1.5f); } } }
    static float bandFreq(int b) noexcept{ float t=(float)b/(float)(kDisplayBins-1); return 55.f*std::pow(18500.f/55.f,t); }
    float makeCarrier(float dry,int ch){ float hz=440.f*std::pow(2.f,((float)midiNote-69.f)/12.f); auto&ph=phase[(size_t)juce::jlimit(0,kMaxChannels-1,ch)]; ph+=juce::MathConstants<double>::twoPi*hz/sr; if(ph>juce::MathConstants<double>::twoPi)ph-=juce::MathConstants<double>::twoPi; float sine=(float)std::sin(ph+.13*ch), saw=(float)(ph/juce::MathConstants<double>::pi-1), pulse=sine>=0?.52f:-.52f; return (.46f*saw+.32f*sine+.18f*pulse)*.72f+dry*.28f; }
    float whiteNoise() noexcept{ noiseState=1664525u*noiseState+1013904223u; return ((float)((noiseState>>9)&0x7FFFFF)/4194304.f)-1.f; }
    static float sat(float x) noexcept { return std::tanh(x); }
    static void applyWidth(juce::AudioBuffer<float>&b,int ns,int nc,float w) noexcept{ if(nc<2||std::abs(w-1)<.001f)return; auto*l=b.getWritePointer(0); auto*r=b.getWritePointer(1); for(int i=0;i<ns;++i){float m=.5f*(l[i]+r[i]),s=.5f*(l[i]-r[i])*w; l[i]=m+s; r[i]=m-s;} }
};
