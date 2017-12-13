// Copyright (c) 2016 Alexander Gallego. All rights reserved.
//
#include "rpc/rpc_envelope.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "rpc/rpc_generated.h"

// smf
#include "platform/log.h"
#include "rpc/rpc_header_utils.h"


namespace smf {

seastar::future<>
rpc_envelope::send(seastar::output_stream<char> *out, rpc_envelope e) {
  seastar::temporary_buffer<char> header_buf(kHeaderSize);
  DLOG_THROW_IF(e.letter.header.size() == 0, "Invalid header size");
  DLOG_THROW_IF(e.letter.header.session() == 0, "Invalid session");
  DLOG_ERROR_IF(e.letter.body.size() == 0, "Invalid payload. 0-size");
  // use 0 copy iface in seastar
  // prepare the header locally
  //
  std::memcpy(header_buf.get_write(),
              reinterpret_cast<char *>(&e.letter.header), header_buf.size());

  // needs to be moved so we can do zero copy output buffer
  return out->write(std::move(header_buf))
    .then([ out, e = std::move(e) ]() mutable {
      // TODO(agalleg) - need to see if we need to write headers
      return out->write(std::move(e.letter.body));
    })
    .then([out] { return out->flush(); });
}

rpc_envelope::rpc_envelope(rpc_letter &&l) : letter(std::move(l)) {}
rpc_envelope::~rpc_envelope() {}
rpc_envelope::rpc_envelope() {}

rpc_envelope &
rpc_envelope::operator=(rpc_envelope &&o) noexcept {
  if (this != &o) { letter = std::move(o.letter); }
  return *this;
}

rpc_envelope::rpc_envelope(rpc_envelope &&o) noexcept
  : letter(std::move(o.letter)) {}

void
rpc_envelope::add_dynamic_header(const char *header, const char *value) {
  DLOG_THROW_IF(header != nullptr, "Cannot add header with empty key");
  DLOG_THROW_IF(value != nullptr, "Cannot add header with empty value");
  letter.dynamic_headers.emplace(header, value);
}
}  // namespace smf
