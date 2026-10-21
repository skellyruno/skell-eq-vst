#pragma once
#include <JuceHeader.h>
#include <array>
namespace SkellyDSP {
// SPSC ring. Producer: audio thread, consumer: editor timer. No allocation in processBlock.
class Analyzer {
 static constexpr uint32_t capacity=32768, mask=capacity-1;
 std::array<float,capacity> fifo{};std::atomic<uint32_t> write{0},read{0};
public:
 // Consumer may skip stale history between editor sessions without accessing
 // or modifying the writer. Safe even while the audio thread produces samples.
 void discardPending() noexcept { read.store(write.load(std::memory_order_acquire), std::memory_order_release); }
 void push(float sample) noexcept {auto w=write.load(std::memory_order_relaxed);auto r=read.load(std::memory_order_acquire);if(w-r>=capacity)return;fifo[w&mask]=sample;write.store(w+1,std::memory_order_release);}
 int pop(float* dest,int count) noexcept {auto r=read.load(std::memory_order_relaxed);auto w=write.load(std::memory_order_acquire);int n=juce::jmin(count,(int)(w-r));for(int i=0;i<n;++i)dest[i]=fifo[(r+i)&mask];read.store(r+n,std::memory_order_release);return n;}
};
}
