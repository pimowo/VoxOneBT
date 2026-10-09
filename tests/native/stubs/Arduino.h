#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

#define SERIAL_8N1 0

class HardwareSerial {
 public:
  size_t setRxBufferSize(size_t size) {
    if (begun_) return 0;
    rxBufferSize_ = size;
    return rxBufferSize_;
  }
  void begin(uint32_t, int, int, int) {
    begun_ = true;
    rxBufferAtBegin_ = rxBufferSize_;
  }
  size_t rxBufferAtBegin() const { return rxBufferAtBegin_; }
  int available() const { return static_cast<int>(input_.size() - readOffset_); }
  int read() { return static_cast<unsigned char>(input_[readOffset_++]); }
  void feed(const std::string& input) { input_ += input; }
  void clearOutput() { output_.clear(); }
  const std::string& output() const { return output_; }

  void print(const char* text) { output_ += text; }
  void print(char value) { output_ += value; }
  void print(uint32_t value) { output_ += std::to_string(value); }
  void println(const char* text) { print(text); write('\n'); }
  void println(uint32_t value) { print(value); write('\n'); }
  size_t write(char value) { output_ += value; return 1; }
  void flush(bool = false) {}

 private:
  size_t rxBufferSize_ = 256;
  size_t rxBufferAtBegin_ = 0;
  bool begun_ = false;
  std::string input_;
  size_t readOffset_ = 0;
  std::string output_;
};

struct FakeESP {
  uint32_t freeHeap = 123456;
  uint32_t minFreeHeap = 120000;
  uint32_t getFreeHeap() const { return freeHeap; }
  uint32_t getMinFreeHeap() const { return minFreeHeap; }
};

extern FakeESP ESP;
uint32_t millis();
