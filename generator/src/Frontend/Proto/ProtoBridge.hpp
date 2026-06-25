#pragma once
/// ProtoFile → NormalizedAST + RPCMethod converter.
/// Translates the proto3 AST into the shared RPC IR.

#include "Frontend/AST.hpp"
#include "IR/RPCIR.hpp"
#include "Frontend/Proto/AST.hpp"
#include "Support/Utils.hpp"
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace driver::proto {

using codegen::sanitize;
using codegen::rpc::RPCMethod;
using codegen::rpc::RPCParam;
using codegen::rpc::RPCError;
using codegen::rpc::StreamingMode;

// ── primitive type mapping ────────────────────────────────────────

inline std::string protoPrimitiveToCpp(std::string_view proto_type) {
	if (proto_type == "double")   return "double";
	if (proto_type == "float")    return "float";
	if (proto_type == "int32")    return "int32_t";
	if (proto_type == "int64")    return "int64_t";
	if (proto_type == "uint32")   return "uint32_t";
	if (proto_type == "uint64")   return "uint64_t";
	if (proto_type == "sint32")   return "int32_t";
	if (proto_type == "sint64")   return "int64_t";
	if (proto_type == "fixed32")  return "uint32_t";
	if (proto_type == "fixed64")  return "uint64_t";
	if (proto_type == "sfixed32") return "int32_t";
	if (proto_type == "sfixed64") return "int64_t";
	if (proto_type == "bool")     return "bool";
	if (proto_type == "string")   return "std::string";
	if (proto_type == "bytes")    return "std::string";
	return std::string(proto_type);
}

inline bool isProtoPrimitive(std::string_view t) {
	static const char* prims[] = {
		"double","float","int32","int64","uint32","uint64",
		"sint32","sint64","fixed32","fixed64","sfixed32","sfixed64",
		"bool","string","bytes",nullptr
	};
	for (int i = 0; prims[i]; ++i)
		if (t == prims[i]) return true;
	return false;
}

// ── recursive conversion ──────────────────────────────────────────

inline void convertMessage(
	const siesta::protobuf::ProtoMessage& pm,
	schema::NormalizedAST& ast,
	std::unordered_set<std::string>& added,
	const std::string& parent_prefix) {

	std::string full_name = parent_prefix.empty()
		? pm.name : parent_prefix + "_" + pm.name;
	full_name = sanitize(std::string_view(full_name));

	// avoid re-entering
	if (added.count(full_name)) return;
	added.insert(full_name);

	schema::StructType st;
	st.name = full_name;

	// fields — handle repeated, optional, map
	for (auto& f : pm.fields) {
		if (f.type == "map") continue; // handled by map_fields

		schema::Member m;
		std::string safe_name = sanitize(std::string_view(f.name));
		m.name = safe_name;
		m.required = (f.label != siesta::protobuf::FieldLabel::optional);

		if (isProtoPrimitive(f.type)) {
			m.type.name = protoPrimitiveToCpp(f.type);
			m.type.is_inline = true;
		} else {
			// resolve message type name (may be dotted: .package.Type)
			std::string tname = f.type;
			if (tname.find('.') != std::string::npos) {
				auto pos = tname.rfind('.');
				tname = tname.substr(pos + 1);
			}
			m.type.name = sanitize(std::string_view(tname));
			m.type.is_inline = false;
		}

		if (f.label == siesta::protobuf::FieldLabel::repeated) {
			// wrap in anonymous array type
			std::string arr_name = full_name + "_" + safe_name + "_array";
			if (!added.count(arr_name)) {
				added.insert(arr_name);
				schema::ArrayType at;
				at.element_type = m.type;
				ast.addType(arr_name, std::move(at));
			}
			m.type.name = arr_name;
			m.type.is_inline = false;
		}

		st.fields.push_back(std::move(m));
	}

	// oneofs → VariantType
	for (auto& oo : pm.oneofs) {
		std::string vname = full_name + "_" + sanitize(std::string_view(oo.name)) + "_oneof";
		if (!added.count(vname)) {
			added.insert(vname);
			schema::VariantType vt;
			vt.name = vname;
			vt.is_nullable = false;
			for (auto& of : oo.fields) {
				schema::TypeRef alt;
				std::string tname = of.type;
				if (isProtoPrimitive(tname)) {
					alt.name = protoPrimitiveToCpp(tname);
					alt.is_inline = true;
				} else {
					if (tname.find('.') != std::string::npos) {
						auto pos = tname.rfind('.');
						tname = tname.substr(pos + 1);
					}
					alt.name = sanitize(std::string_view(tname));
					alt.is_inline = false;
				}
				vt.alternatives.push_back(alt);
			}
			ast.addType(vname, std::move(vt));
		}

		schema::Member m;
		m.name = sanitize(std::string_view(oo.name));
		m.type.name = vname;
		m.type.is_inline = false;
		m.required = true;
		st.fields.push_back(std::move(m));
	}

	// map fields
	for (auto& mf : pm.map_fields) {
		schema::TypeRef val_ref;
		if (isProtoPrimitive(mf.value_type)) {
			val_ref.name = protoPrimitiveToCpp(mf.value_type);
			val_ref.is_inline = true;
		} else {
			std::string tname = mf.value_type;
			if (tname.find('.') != std::string::npos) {
				auto pos = tname.rfind('.');
				tname = tname.substr(pos + 1);
			}
			val_ref.name = sanitize(std::string_view(tname));
			val_ref.is_inline = false;
		}

		std::string map_name = full_name + "_" + sanitize(std::string_view(mf.name)) + "_map";
		if (!added.count(map_name)) {
			added.insert(map_name);
			schema::MapType mt;
			mt.value_type = val_ref;
			ast.addType(map_name, std::move(mt));
		}

		schema::Member m;
		m.name = sanitize(std::string_view(mf.name));
		m.type.name = map_name;
		m.type.is_inline = false;
		m.required = true;
		st.fields.push_back(std::move(m));
	}

	ast.addType(full_name, std::move(st));

	// nested enums
	for (auto& e : pm.nested_enums) {
		std::string ename = full_name + "_" + sanitize(std::string_view(e.name));
		if (added.count(ename)) continue;
		added.insert(ename);
		schema::EnumType et;
		et.name = ename;
		et.is_string = false;
		for (auto& v : e.values) {
			schema::EnumValue ev;
			ev.name = sanitize(std::string_view(v.name));
			ev.value = std::to_string(v.number);
			et.values.push_back(std::move(ev));
		}
		ast.addType(ename, std::move(et));
	}

	// nested messages (recurse)
	for (auto& nm : pm.nested_messages)
		convertMessage(nm, ast, added, full_name);
}

// ── top-level conversion ─────────────────────────────────────────

inline void convertFile(
	const siesta::protobuf::ProtoFile& pf,
	schema::NormalizedAST& ast,
	std::vector<RPCMethod>& methods) {

	std::unordered_set<std::string> added;

	// enums
	for (auto& e : pf.enums) {
		std::string name = sanitize(std::string_view(e.name));
		if (added.count(name)) continue;
		added.insert(name);
		schema::EnumType et;
		et.name = name;
		et.is_string = false;
		for (auto& v : e.values) {
			schema::EnumValue ev;
			ev.name = sanitize(std::string_view(v.name));
			ev.value = std::to_string(v.number);
			et.values.push_back(std::move(ev));
		}
		ast.addType(name, std::move(et));
	}

	// messages
	for (auto& m : pf.messages)
		convertMessage(m, ast, added, "");

	// services → RPCMethod vector
	for (auto& svc : pf.services) {
		for (auto& r : svc.rpcs) {
			RPCMethod rm;
			rm.name = svc.name + "/" + r.name;
			rm.param_structure = "by-name";

			// request param
			RPCParam rp;
			rp.name = "request";
			std::string req_type = r.request_type;
			if (req_type.find('.') != std::string::npos) {
				auto pos = req_type.rfind('.');
				req_type = req_type.substr(pos + 1);
			}
			rp.cpp_type = sanitize(std::string_view(req_type));
			rp.schema_ref.name = sanitize(std::string_view(req_type));
			rp.required = true;
			rm.params.push_back(std::move(rp));

			// result type
			std::string resp_type = r.response_type;
			if (resp_type.find('.') != std::string::npos) {
				auto pos = resp_type.rfind('.');
				resp_type = resp_type.substr(pos + 1);
			}
			rm.result_type.name = sanitize(std::string_view(resp_type));
			rm.is_notification = false;

			// streaming mode
			if (r.client_streaming && r.server_streaming)
				rm.streaming = StreamingMode::Bidirectional;
			else if (r.client_streaming)
				rm.streaming = StreamingMode::ClientStreaming;
			else if (r.server_streaming)
				rm.streaming = StreamingMode::ServerStreaming;

			methods.push_back(std::move(rm));
		}
	}
}

} // namespace driver::proto
