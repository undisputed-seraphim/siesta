#pragma once

#include <boost/spirit/home/x3.hpp>

namespace siesta::protobuf::parser {

namespace x3 = boost::spirit::x3;

inline auto const comment_line   = x3::lit("//") >> *(x3::char_ - x3::eol) >> (x3::eol | x3::eoi);
inline auto const comment_block  = x3::lit("/*") >> *(x3::char_ - x3::lit("*/")) >> x3::lit("*/");
inline auto const skipper        = x3::space | comment_line | comment_block;

} // namespace siesta::protobuf::parser
