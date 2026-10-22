#pragma once

#include "Vm.h"
#include <vector>
#include <string>
#include <cstdint>

class Serializer {
public:
    static std::vector<uint8_t> serialize(std::shared_ptr<Chunk> chunk);
    static std::shared_ptr<Chunk> deserialize(const std::vector<uint8_t>& data);

private:
    static void writeChunk(std::vector<uint8_t>& out, std::shared_ptr<Chunk> chunk);
    static std::shared_ptr<Chunk> readChunk(const std::vector<uint8_t>& data, size_t& offset);

    static void writeValue(std::vector<uint8_t>& out, const QuantumValue& val);
    static QuantumValue readValue(const std::vector<uint8_t>& data, size_t& offset);

    static void writeString(std::vector<uint8_t>& out, const std::string& s);
    static std::string readString(const std::vector<uint8_t>& data, size_t& offset);

    // Fixed-width little-endian encoders (the byte format of the payload).
    static void putU8(std::vector<uint8_t>& out, uint8_t v);
    static void putU32(std::vector<uint8_t>& out, uint32_t v);
    static void putI32(std::vector<uint8_t>& out, int32_t v);
    static void putF64(std::vector<uint8_t>& out, double v);
    static uint8_t getU8(const std::vector<uint8_t>& data, size_t& offset);
    static uint32_t getU32(const std::vector<uint8_t>& data, size_t& offset);
    static int32_t getI32(const std::vector<uint8_t>& data, size_t& offset);
    static double getF64(const std::vector<uint8_t>& data, size_t& offset);
};
