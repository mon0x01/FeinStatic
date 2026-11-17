#pragma once

#include "feinstat/consensus/abci/application.hpp"
#include "feinstat/consensus/abci/types.hpp"

#include "tendermint/abci/types.pb.h"

#include <cstdint>
#include <string>
#include <vector>

namespace feinstat::consensus::abci {

//------------------------------------------------------------------------------
// Conversion: FST-native <-> protobuf wire format
//------------------------------------------------------------------------------

// Convert FST-native Request to protobuf Request.
tendermint::abci::Request to_protobuf(const Request& req);

// Convert protobuf Request to FST-native Request.
Request from_protobuf(const tendermint::abci::Request& req);

// Convert FST-native Response to protobuf Response.
tendermint::abci::Response to_protobuf(const Response& resp);

// Convert protobuf Response to FST-native Response.
Response from_protobuf(const tendermint::abci::Response& resp);

//------------------------------------------------------------------------------
// Length-delimited framing (Tendermint Socket Protocol)
//------------------------------------------------------------------------------

// Read a varint length prefix, then read that many bytes as a protobuf
// message. Returns the parsed message. Throws on truncation or oversized
// messages.
template <typename Message>
Message read_framed_message(int fd, std::size_t max_message_size);

// Write a protobuf message as varint length + serialized bytes.
template <typename Message>
void write_framed_message(int fd, const Message& msg);

// Read exactly n bytes from fd into buf. Throws on EOF or error.
void read_exact(int fd, void* buf, std::size_t n);

// Write exactly n bytes from buf to fd. Throws on error.
void write_exact(int fd, const void* buf, std::size_t n);

// Read exactly n bytes from fd into buf. Throws on EOF or error.
void read_exact(int fd, void* buf, std::size_t n);

// Write exactly n bytes from buf to fd. Throws on error.
void write_exact(int fd, const void* buf, std::size_t n);

// Encode an unsigned varint into a byte vector.
std::vector<std::uint8_t> encode_varint(std::uint64_t value);

// Decode an unsigned varint from a byte reader.
std::uint64_t decode_varint(const std::vector<std::uint8_t>& data,
                            std::size_t& pos);

} // namespace feinstat::consensus::abci
