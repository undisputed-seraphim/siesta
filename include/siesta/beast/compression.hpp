// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>
#include <zlib.h>

namespace siesta::beast {

inline std::string gzip_compress(std::string_view input) {
	z_stream zs{};
	deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);

	zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
	zs.avail_in = static_cast<uInt>(input.size());

	std::string output;
	output.resize(deflateBound(&zs, static_cast<uLong>(input.size())));

	zs.next_out = reinterpret_cast<Bytef*>(output.data());
	zs.avail_out = static_cast<uInt>(output.size());

	deflate(&zs, Z_FINISH);
	output.resize(zs.total_out);
	deflateEnd(&zs);

	return output;
}

} // namespace siesta::beast
