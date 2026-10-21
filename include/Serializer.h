#pragma once
// Reads the bytecode file format: a chunk as selfhost/serializer.sa writes
// it (what the self-hosted compiler produces, `qrun --emit-qbc` files, and
// the payload a bundled .exe carries).

#include "Vm.h"
#include <vector>
#include <string>
#include <cstdint>

class Serializer {
public:
    static std::shared_ptr<Chunk> deserialize(const std::vector<uint8_t>& data);

private:
    static std::shared_ptr<Chunk> readChunk(const std::vector<uint8_t>& data, size_t& offset);
    static QuantumValue readValue(const std::vector<uint8_t>& data, size_t& offset);
    static std::string readString(const std::vector<uint8_t>& data, size_t& offset);

    template <typename T>
    static T readRaw(const std::vector<uint8_t>& data, size_t& offset);
};
