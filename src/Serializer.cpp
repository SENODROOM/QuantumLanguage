#include "Serializer.h"
#include <stdexcept>
#include <cstring>

enum class ValueType : uint8_t {
    VAL_NIL = 0,
    VAL_BOOL = 1,
    VAL_NUMBER = 2,
    VAL_STRING = 3,
    VAL_ARRAY = 4,
    VAL_CLOSURE = 5,
    VAL_LONG = 6
};

template <typename T>
T Serializer::readRaw(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + sizeof(T) > data.size()) {
        throw std::runtime_error("Unexpected end of file while deserializing");
    }
    T t;
    std::memcpy(&t, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return t;
}

std::string Serializer::readString(const std::vector<uint8_t>& data, size_t& offset) {
    uint32_t len = readRaw<uint32_t>(data, offset);
    if (offset + len > data.size()) {
        throw std::runtime_error("Unexpected string length while deserializing");
    }
    std::string s(reinterpret_cast<const char*>(data.data() + offset), len);
    offset += len;
    return s;
}

QuantumValue Serializer::readValue(const std::vector<uint8_t>& data, size_t& offset) {
    ValueType type = readRaw<ValueType>(data, offset);
    switch (type) {
        case ValueType::VAL_NIL:
            return QuantumValue();
        case ValueType::VAL_BOOL:
            return QuantumValue(readRaw<uint8_t>(data, offset) != 0);
        case ValueType::VAL_NUMBER:
            return QuantumValue(readRaw<double>(data, offset));
        case ValueType::VAL_LONG:
            return QuantumValue::ofLong(readRaw<int64_t>(data, offset));
        case ValueType::VAL_STRING:
            return QuantumValue(readString(data, offset));
        case ValueType::VAL_ARRAY: {
            uint32_t size = readRaw<uint32_t>(data, offset);
            auto arr = std::make_shared<Array>(size);
            for (uint32_t i = 0; i < size; ++i) {
                (*arr)[i] = readValue(data, offset);
            }
            return QuantumValue(arr);
        }
        case ValueType::VAL_CLOSURE: {
            auto chunk = readChunk(data, offset);
            return QuantumValue(std::make_shared<Closure>(chunk));
        }
        default:
            throw std::runtime_error("Unknown value type while deserializing");
    }
}

std::shared_ptr<Chunk> Serializer::readChunk(const std::vector<uint8_t>& data, size_t& offset) {
    auto chunk = std::make_shared<Chunk>();
    chunk->name = readString(data, offset);
    
    // code
    uint32_t codeSize = readRaw<uint32_t>(data, offset);
    chunk->code.reserve(codeSize);
    for (uint32_t i = 0; i < codeSize; ++i) {
        Instruction instr;
        instr.op = readRaw<Op>(data, offset);
        instr.operand = readRaw<int32_t>(data, offset);
        instr.line = readRaw<int>(data, offset);
        chunk->code.push_back(instr);
    }
    
    // params
    uint32_t paramsSize = readRaw<uint32_t>(data, offset);
    chunk->params.reserve(paramsSize);
    for (uint32_t i = 0; i < paramsSize; ++i) {
        chunk->params.push_back(readString(data, offset));
    }
    
    // paramIsRef
    uint32_t refSize = readRaw<uint32_t>(data, offset);
    chunk->paramIsRef.reserve(refSize);
    for (uint32_t i = 0; i < refSize; ++i) {
        chunk->paramIsRef.push_back(readRaw<uint8_t>(data, offset) != 0);
    }
    
    // upvalueCount
    chunk->upvalueCount = readRaw<int>(data, offset);
    
    // constants
    uint32_t constSize = readRaw<uint32_t>(data, offset);
    chunk->constants.reserve(constSize);
    for (uint32_t i = 0; i < constSize; ++i) {
        chunk->constants.push_back(readValue(data, offset));
    }
    
    return chunk;
}

std::shared_ptr<Chunk> Serializer::deserialize(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    return readChunk(data, offset);
}
