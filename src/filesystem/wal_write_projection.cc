// Copyright 2017 Alexander Gallego
//

#include "filesystem/wal_write_projection.h"

#include "hashing/hashing_utils.h"
#include "platform/log.h"
#include "platform/macros.h"
#include "utils/compression.h"

namespace smf {
using namespace smf::wal;  // NOLINT
static const uint64_t kMinCompressionSize = 512;


seastar::lw_shared_ptr<wal_write_projection::item> xform(
  const tx_put_fragment &f) {
  static thread_local flatbuffers::FlatBufferBuilder fbb{};
  static thread_local auto                           compression =
    codec::make_unique(codec_type::lz4, compression_level::fastest);

  // unfortunatealy, we need to copy mem, then compress it :( - so 2 memcpy.
  fbb.Clear();  // MUST happen first
  auto _op       = f.op();
  auto _type     = f.type();
  auto _epoch_ms = f.epoch_ms();
  // key=val must be inline to reduce data size
  auto _key =
    f.key()->size() ? fbb.CreateVector(f.key()->Data(), f.key()->Length()) : 0;
  auto _value = f.value()->size() ?
                  fbb.CreateVector(f.value()->Data(), f.value()->Length()) :
                  0;
  // invalidations must be inline to reduce data size
  auto _invalidation_reason = f.invalidation_reason();
  auto _invalidation_offset = f.invalidation_offset();
  fbb.Finish(Createtx_put_fragment(fbb, _op, _type, _epoch_ms, _key, _value,
                                   _invalidation_reason, _invalidation_offset));

  auto retval = seastar::make_lw_shared<wal_write_projection::item>();

  if (fbb.GetSize() > kMinCompressionSize) {
    retval->fragment =
      compression->compress((char *)fbb.GetBufferPointer(), fbb.GetSize());
    retval->hdr.mutable_ptr()->mutate_compression(
      wal_entry_compression_type::wal_entry_compression_type_lz4);
  } else {
    retval->fragment = seastar::temporary_buffer<char>(fbb.GetSize());
    std::memcpy(retval->fragment.get_write(), fbb.GetBufferPointer(),
                fbb.GetSize());
  }

  retval->hdr.mutable_ptr()->mutate_size(retval->fragment.size());
  retval->hdr.mutable_ptr()->mutate_checksum(
    xxhash_32(retval->fragment.get(), retval->fragment.size()));
  return retval;
}

seastar::lw_shared_ptr<wal_write_projection> wal_write_projection::translate(
  const tx_put_partition_pair *req) {
  auto ret = seastar::make_lw_shared<wal_write_projection>();
  std::for_each(req->txs()->begin(), req->txs()->end(), [ret](auto it) {
    // even though it's a list, push_back is O( 1 )
    ret->projection.push_back(xform(*it));
  });
  return ret;
}

}  // namespace smf
