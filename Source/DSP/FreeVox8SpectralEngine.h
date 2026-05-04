#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Spectral Ghost 1024: high-resolution STFT/OLA reference backend.
// Prepared storage only; process() does no allocation, file I/O, locks, or threads.
class FreeVox8SpectralEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kFftSize = 1024;
    static constexpr int kHopSize = 256;
    static constexpr int kDisplayBins = 128;
    struct Parameters { float mix=1, maskDepth=.5f, clarity=.55f, formantShift=0, ghost=0, air=.15f, freeze=0, transientProtect=.35f, stereoWidth=1, outputGainDb=0; int mode=0; };
    void prepare(double sampleRate, int, int numChannels){ sr=sampleRate>0?sampleRate:44100.0; channels=juce::jlimit(1,kMaxChannels,numChannels); for(int i=0;i<kFftSize;++i){double t=(double)i/(double)(kFftSize-1); window[(size_t)i]=(float)(0.5-0.5*std::cos(juce::MathConstants<double>::twoPi*t));} reset(); }
    void reset(){ for(auto& a:inputRing)a.fill(0); for(auto& a:modRing)a.fill(0); for(auto& a:olaRing)a.fill(0); for(auto& a:env)a.fill(0); for(auto& a:frozen)a.fill(0); for(auto& b:display)b.store(0,std::memory_order_relaxed); phase={0,0}; writeIndex=0; hopCountdown=kHopSize; }
    void setMidiNote(int n) noexcept { midiNote=juce::jlimit(0,127,n); }
    void setParameters(const Parameters& p) noexcept { params=p; }
    void process(juce::AudioBuffer<float>& buffer,const juce::AudioBuffer<float>* sidechain){ const int ns=buffer.getNumSamples(), nc=juce::jmin(channels,buffer.getNumChannels()); const float mix=juce::jlimit(0.f,1.f,params.mix), dryMix=1-mix, out=juce::Decibels::decibelsToGain(params.outputGainDb); for(int n=0;n<ns;++n){ for(int ch=0;ch<nc;++ch){auto*x=buffer.getWritePointer(ch); float dry=x[n]; float mod=(sidechain&&sidechain->getNumChannels()>0)?sidechain->getReadPointer(juce::jmin(ch,sidechain->getNumChannels()-1))[n]:dry; inputRing[(size_t)ch][(size_t)writeIndex]=makeCarrier(dry,ch); modRing[(size_t)ch][(size_t)writeIndex]=mod; float wet=olaRing[(size_t)ch][(size_t)writeIndex]; olaRing[(size_t)ch][(size_t)writeIndex]=0; x[n]=sat((dry*dryMix+wet*mix)*out);} writeIndex=(writeIndex+1)&(kFftSize-1); if(--hopCountdown<=0){hopCountdown=kHopSize; renderFrame(nc);} } applyWidth(buffer,ns,nc,juce::jlimit(0.f,2.f,params.stereoWidth)); }
    float getDisplayBin(int i) const noexcept { i=juce::jlimit(0,kDisplayBins-1,i); return display[(size_t)i].load(std::memory_order_relaxed); }
    int getDisplayBinCount() const noexcept { return kDisplayBins; }
private:
    using Frame=std::array<float,kFftSize>; using Spec=std::array<float,kDisplayBins>;
    std::array<Frame,kMaxChannels> inputRing{},modRing{},olaRing{}; std::array<Spec,kMaxChannels> env{},frozen{}; std::array<float,kFftSize> window{}; std::array<std::atomic<float>,kDisplayBins> display{}; std::array<double,kMaxChannels> phase{0,0}; Parameters params; double sr=44100; int channels=2, writeIndex=0, hopCountdown=kHopSize, midiNote=48; uint32_t noiseState=0xA53C9E21u;
    void renderFrame(int nc){ bool fr=params.freeze>.5f; float clarity=juce::jlimit(0.f,1.f,params.clarity), ghost=juce::jlimit(0.f,1.f,params.ghost), air=juce::jlimit(0.f,1.f,params.air), shift=params.formantShift/12.f*10.f; for(int ch=0;ch<nc;++ch){ analyseBins(ch,fr); for(int i=0;i<kFftSize;++i){ int idx=(writeIndex+i)&(kFftSize-1); float t=(float)i/(float)(kFftSize-1); float e=readEnv(env[(size_t)ch],frozen[(size_t)ch],t*(kDisplayBins-1)-shift,fr); float car=inputRing[(size_t)ch][(size_t)idx]*window[(size_t)i]; float y=car*(.72f+e*(2.8f+clarity*4.f))+air*t*t*whiteNoise()*.018f; if(params.mode==1)y=car*(1-juce::jlimit(0.f,.96f,e*params.maskDepth*3.2f)); else if(params.mode==2)y*=1+ghost*2.4f; else if(params.mode==3)y=sat(y*.72f+car*(.55f+ghost)); olaRing[(size_t)ch][(size_t)idx]+=y*window[(size_t)i]*.18f; } } }
    void analyseBins(int ch,bool fr){ for(int b=0;b<kDisplayBins;++b){ float freq=bandFreq(b); double w=juce::MathConstants<double>::twoPi*(double)freq/sr,re=0,im=0; for(int i=0;i<kFftSize;i+=4){int idx=(writeIndex+i)&(kFftSize-1); double a=w*(double)i; float s=modRing[(size_t)ch][(size_t)idx]*window[(size_t)i]; re+=s*std::cos(a); im-=s*std::sin(a);} float mag=(float)std::sqrt(re*re+im*im)*.0018f; auto&E=env[(size_t)ch][(size_t)b]; E=mag>E?.24f*E+.76f*mag:.92f*E+.08f*mag; if(!fr)frozen[(size_t)ch][(size_t)b]=.998f*frozen[(size_t)ch][(size_t)b]+.002f*E; float v=fr?frozen[(size_t)ch][(size_t)b]:E; float old=display[(size_t)b].load(std::memory_order_relaxed); display[(size_t)b].store(old*.88f+juce::jlimit(0.f,1.f,v*8.f)*.12f,std::memory_order_relaxed);} }
    static float readEnv(const Spec&e,const Spec&f,float pos,bool fr) noexcept{ const auto&s=fr?f:e; float p=juce::jlimit(0.f,(float)(kDisplayBins-1),pos); int lo=(int)std::floor(p),hi=juce::jmin(kDisplayBins-1,lo+1); float t=p-(float)lo; return s[(size_t)lo]+(s[(size_t)hi]-s[(size_t)lo])*t; }
    static float bandFreq(int b) noexcept{ float t=(float)b/(float)(kDisplayBins-1); return 55.f*std::pow(18500.f/55.f,t); }
    float makeCarrier(float dry,int ch){ float hz=440.f*std::pow(2.f,((float)midiNote-69.f)/12.f); auto&ph=phase[(size_t)juce::jlimit(0,kMaxChannels-1,ch)]; ph+=juce::MathConstants<double>::twoPi*hz/sr; if(ph>juce::MathConstants<double>::twoPi)ph-=juce::MathConstants<double>::twoPi; float sine=(float)std::sin(ph+.13*ch), saw=(float)(ph/juce::MathConstants<double>::pi-1), pulse=sine>=0?.52f:-.52f; return (.46f*saw+.32f*sine+.18f*pulse)*.72f+dry*.28f; }
    float whiteNoise() noexcept{ noiseState=1664525u*noiseState+1013904223u; return ((float)((noiseState>>9)&0x7FFFFF)/4194304.f)-1.f; }
    static float sat(float x) noexcept { return std::tanh(x); }
    static void applyWidth(juce::AudioBuffer<float>&b,int ns,int nc,float w) noexcept{ if(nc<2||std::abs(w-1)<.001f)return; auto*l=b.getWritePointer(0); auto*r=b.getWritePointer(1); for(int i=0;i<ns;++i){float m=.5f*(l[i]+r[i]),s=.5f*(l[i]-r[i])*w; l[i]=m+s; r[i]=m-s;} }
};
