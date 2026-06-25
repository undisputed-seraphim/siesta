#include "Parser.hpp"
#include "Skipper.hpp"

#include <boost/spirit/home/x3.hpp>
#include <cstdint>
#include <memory>
#include <stack>
#include <string>

namespace x3 = boost::spirit::x3;

namespace siesta::protobuf {
using namespace parser;

namespace {

// ── keyword & builtin symbol tables ────────────────────────────────────

x3::symbols<std::string> builtin_type;
x3::symbols<FieldLabel> field_label;
x3::symbols<ProtoImport::Modifier> import_mod;
x3::symbols<std::string> map_key_type;

struct init_syms_ {
	init_syms_() {
		builtin_type.add
			("double","double")("float","float")("int32","int32")
			("int64","int64")("uint32","uint32")("uint64","uint64")
			("sint32","sint32")("sint64","sint64")
			("fixed32","fixed32")("fixed64","fixed64")
			("sfixed32","sfixed32")("sfixed64","sfixed64")
			("bool","bool")("string","string")("bytes","bytes");
		field_label.add
			("optional", FieldLabel::optional)
			("repeated", FieldLabel::repeated);
		import_mod.add
			("weak",   ProtoImport::weak)
			("public", ProtoImport::public_);
		map_key_type.add
			("int32","int32")("int64","int64")("uint32","uint32")
			("uint64","uint64")("sint32","sint32")("sint64","sint64")
			("fixed32","fixed32")("fixed64","fixed64")
			("sfixed32","sfixed32")("sfixed64","sfixed64")
			("bool","bool")("string","string");
	}
} _init;

// ── leaf rules with synthesized attributes ─────────────────────────

x3::rule<struct ident_, std::string> const ident{"ident"};
auto const ident_def = x3::raw[x3::lexeme[x3::alpha >> *(x3::alnum | '_')]];

x3::rule<struct full_ident_, std::string> const full_ident{"full_ident"};
auto const full_ident_def = x3::raw[x3::lexeme[ident_def % '.']];

x3::rule<struct str_lit_, std::string> const str_lit{"str_lit"};
auto const str_lit_def =
	x3::lexeme[x3::omit['"'] >> x3::raw[*(('\\' >> x3::char_) | (x3::char_ - '"'))] >> x3::omit['"']]
	| x3::lexeme[x3::omit['\''] >> x3::raw[*(('\\' >> x3::char_) | (x3::char_ - '\''))] >> x3::omit['\'']];

x3::rule<struct int_lit_, int64_t> const int_lit{"int_lit"};
auto const int_lit_def = x3::lexeme[x3::int64];

x3::rule<struct type_name_, std::string> const type_name{"type_name"};
auto const type_name_def = x3::lexeme[builtin_type | x3::raw[(-x3::lit('.') >> ident % '.')]];

// ── AST builder context ─────────────────────────────────────────────

struct ctx_tag {};

struct PCtx {
	ProtoFile* file = nullptr;

	// stacks for recursive ownership
	std::stack<ProtoMessage*> msg_stack;
	std::deque<std::unique_ptr<ProtoMessage>> msg_pool;

	std::stack<ProtoEnum*> enum_stack;
	std::deque<std::unique_ptr<ProtoEnum>> enum_pool;

	// scratch areas for in-progress entities
	ProtoField     sf;
	ProtoOneof     so;
	ProtoMapField  sm;
	ProtoEnumValue se;
	ProtoRPC       sr;

	// scratch areas for top-level statements
	std::string       scratch_str;
	ProtoImport::Modifier scratch_mod = ProtoImport::none;

	std::vector<ProtoRPC> svc_rpcs;

	// helper to enter a message
	void begin_msg(const std::string& name) {
		auto p = std::make_unique<ProtoMessage>();
		p->name = name;
		msg_stack.push(p.get());
		msg_pool.push_back(std::move(p));
	}
	void end_msg() {
		auto m = std::move(msg_pool.back());
		msg_pool.pop_back();
		msg_stack.pop();
		if (!msg_stack.empty())
			msg_stack.top()->nested_messages.push_back(std::move(*m));
		else
			file->messages.push_back(std::move(*m));
	}

	// helper to enter an enum
	void begin_enum(const std::string& name) {
		auto p = std::make_unique<ProtoEnum>();
		p->name = name;
		enum_stack.push(p.get());
		enum_pool.push_back(std::move(p));
	}
	void end_enum() {
		auto e = std::move(enum_pool.back());
		enum_pool.pop_back();
		enum_stack.pop();
		if (!msg_stack.empty())
			msg_stack.top()->nested_enums.push_back(std::move(*e));
		else
			file->enums.push_back(std::move(*e));
	}
};

// For semantic actions: retrieve context from a narrow context tree.
// The X3 context is a linked list; we walk it to find ctx_tag.
template <typename X3Ctx>
PCtx& pctx(X3Ctx const& x3c) { return *x3::get<ctx_tag>(x3c); }

// convenience
#define PC pctx(c)

// ── forward-declare all structural rules ────────────────────────────

x3::rule<struct _fld>        const field{"field"};
x3::rule<struct _ofld>       const oneof_field{"oneof_field"};
x3::rule<struct _onf>        const oneof{"oneof"};
x3::rule<struct _map>        const map_field{"map_field"};
x3::rule<struct _res>        const reserved{"reserved"};
x3::rule<struct _eval>       const enum_value{"enum_value"};
x3::rule<struct _enm>        const enum_rule{"enum"};
x3::rule<struct _msg>        const message{"message"};
x3::rule<struct _mbody>      const message_body{"message_body"};
x3::rule<struct _rpc>        const rpc{"rpc"};
x3::rule<struct _svc>        const service{"service"};
x3::rule<struct _syn>        const syntax_stmt{"syntax"};
x3::rule<struct _imp>        const import_stmt{"import"};
x3::rule<struct _pkg>        const package_stmt{"package"};
x3::rule<struct _opt>        const option_stmt{"option"};
x3::rule<struct _pro>        const proto_file{"proto"};

auto const empty_stmt = x3::lit(';');

// ── field ───────────────────────────────────────────────────────────

auto const field_def = (
	( -field_label[([](auto& c){ PC.sf.label = x3::_attr(c); })]
	  >> type_name[([](auto& c){ PC.sf.type = std::move(x3::_attr(c)); })]
	  >> ident[([](auto& c){ PC.sf.name = std::move(x3::_attr(c)); })]
	  >> '=' >> int_lit[([](auto& c){ PC.sf.number = static_cast<int32_t>(x3::_attr(c)); })]
	  >> -('[' >> *(x3::char_ - ']') >> ']')
	  >> ';'
	)[([](auto& c){
		PC.msg_stack.top()->fields.push_back(std::move(PC.sf));
		PC.sf = ProtoField{};
	})]
);

// ── oneof ───────────────────────────────────────────────────────────

auto const oneof_field_def = (
	( type_name[([](auto& c){ PC.sf = ProtoField{}; PC.sf.type = std::move(x3::_attr(c)); })]
	  >> ident[([](auto& c){ PC.sf.name = std::move(x3::_attr(c)); })]
	  >> '=' >> int_lit[([](auto& c){ PC.sf.number = static_cast<int32_t>(x3::_attr(c)); })]
	  >> -('[' >> *(x3::char_ - ']') >> ']')
	  >> ';'
	)[([](auto& c){
		PC.so.fields.push_back(std::move(PC.sf));
	})]
);

auto const oneof_def = (
	( x3::lit("oneof") >> ident[([](auto& c){ PC.so = ProtoOneof{}; PC.so.name = std::move(x3::_attr(c)); })]
	  >> '{' >> *oneof_field >> '}'
	)[([](auto& c){
		PC.msg_stack.top()->oneofs.push_back(std::move(PC.so));
	})]
);

// ── map ─────────────────────────────────────────────────────────────

auto const map_field_def = (
	( x3::lit("map") >> '<'
	  >> map_key_type[([](auto& c){ PC.sm = ProtoMapField{}; PC.sm.key_type = std::move(x3::_attr(c)); })]
	  >> ',' >> type_name[([](auto& c){ PC.sm.value_type = std::move(x3::_attr(c)); })]
	  >> '>' >> ident[([](auto& c){ PC.sm.name = std::move(x3::_attr(c)); })]
	  >> '=' >> int_lit[([](auto& c){ PC.sm.number = static_cast<int32_t>(x3::_attr(c)); })]
	  >> -('[' >> *(x3::char_ - ']') >> ']')
	  >> ';'
	)[([](auto& c){
		PC.msg_stack.top()->map_fields.push_back(std::move(PC.sm));
	})]
);

// ── reserved (opaque) ───────────────────────────────────────────────

auto const reserved_def = x3::lit("reserved") >> (+(x3::char_ - ';')) >> ';';

// ── enum ────────────────────────────────────────────────────────────

auto const enum_value_def = (
	( ident[([](auto& c){ PC.se = ProtoEnumValue{}; PC.se.name = std::move(x3::_attr(c)); })]
	  >> '=' >> -x3::lit('-') >> int_lit[([](auto& c){ PC.se.number = static_cast<int32_t>(x3::_attr(c)); })]
	  >> -('[' >> *(x3::char_ - ']') >> ']')
	  >> ';'
	)[([](auto& c){
		PC.enum_stack.top()->values.push_back(std::move(PC.se));
	})]
);

auto const enum_rule_def = (
	( x3::lit("enum") >> ident[([](auto& c){ PC.begin_enum(std::move(x3::_attr(c))); })]
	  >> '{' >> *(enum_value | reserved | empty_stmt) >> '}'
	)[([](auto& c){ PC.end_enum(); })]
);

// ── message (recursive) ─────────────────────────────────────────────

auto const message_body_def =
	'{' >> *(field | oneof | map_field | message | enum_rule | reserved | empty_stmt) >> '}';

auto const message_def = (
	( x3::lit("message") >> ident[([](auto& c){ PC.begin_msg(std::move(x3::_attr(c))); })]
	  >> message_body
	)[([](auto& c){ PC.end_msg(); })]
);

// ── service / rpc ───────────────────────────────────────────────────

auto const rpc_def = (
	( x3::lit("rpc") >> ident[([](auto& c){ PC.sr = ProtoRPC{}; PC.sr.name = std::move(x3::_attr(c)); })]
	  >> '(' >> -x3::lit("stream")[([](auto& c){ PC.sr.client_streaming = true; })]
	  >> type_name[([](auto& c){ PC.sr.request_type = std::move(x3::_attr(c)); })]
	  >> ')' >> x3::lit("returns")
	  >> '(' >> -x3::lit("stream")[([](auto& c){ PC.sr.server_streaming = true; })]
	  >> type_name[([](auto& c){ PC.sr.response_type = std::move(x3::_attr(c)); })]
	  >> ')' >> ('{' >> *empty_stmt >> '}' | ';')
	)[([](auto& c){
		PC.svc_rpcs.push_back(std::move(PC.sr));
	})]
);

auto const service_def = (
	( x3::lit("service") >> ident[([](auto& c){ PC.svc_rpcs.clear(); PC.so.name = std::move(x3::_attr(c)); })]
	  >> '{' >> *(rpc | empty_stmt) >> '}'
	)[([](auto& c){
		auto& svc = PC.file->services.emplace_back();
		svc.name = std::move(PC.so.name);
		svc.rpcs = std::move(PC.svc_rpcs);
	})]
);

// ── top-level statements ────────────────────────────────────────────

auto const syntax_stmt_def = (
	x3::lit("syntax") >> '=' >> str_lit[([](auto& c){
		PC.file->syntax = std::move(x3::_attr(c));
	})] >> ';'
);

auto const import_stmt_def = (
	x3::lit("import") >>
	-import_mod[([](auto& c){ PC.scratch_mod = x3::_attr(c); })] >>
	str_lit[([](auto& c){ PC.scratch_str = std::move(x3::_attr(c)); })] >>
	';'
)[([](auto& c){
	PC.file->imports.push_back({PC.scratch_mod, std::move(PC.scratch_str)});
	PC.scratch_mod = ProtoImport::none;
})];

auto const package_stmt_def = (
	x3::lit("package") >> full_ident[([](auto& c){
		PC.file->package = std::move(x3::_attr(c));
	})] >> ';'
);

auto const option_stmt_def = x3::lit("option") >> +(str_lit | (x3::char_ - ';')) >> ';';

// ── proto file (root) ───────────────────────────────────────────────

auto const proto_file_def = (
	- syntax_stmt
	>> *( import_stmt | package_stmt | option_stmt
	    | message | enum_rule | service | empty_stmt )
	>> x3::eoi
);

// ── BOOST_SPIRIT_DEFINE ────────────────────────────────────────────

BOOST_SPIRIT_DEFINE(
	ident, full_ident, str_lit, int_lit, type_name,
	field, oneof_field, oneof, map_field, reserved, enum_value, enum_rule,
	message, message_body, rpc, service,
	syntax_stmt, import_stmt, package_stmt, option_stmt,
	proto_file
)

} // anonymous namespace

// ── public entry point ─────────────────────────────────────────────────

ParseResult parse_proto(std::string_view source) {
	ParseResult result;
	result.file.syntax = "proto3";

	PCtx p;
	p.file = &result.file;

	auto begin = source.begin();
	auto end   = source.end();

	auto const parser = x3::with<ctx_tag>(&p)[proto_file];

	bool ok = x3::phrase_parse(begin, end, parser, skipper);

	if (!ok || begin != end) {
		result.ok = false;
		auto pos = static_cast<size_t>(begin - source.begin());
		int line = 1;
		for (size_t i = 0; i < pos && i < source.size(); ++i)
			if (source[i] == '\n') ++line;
		result.error = "parse error near line " + std::to_string(line);
	}

	return result;
}

} // namespace siesta::protobuf
