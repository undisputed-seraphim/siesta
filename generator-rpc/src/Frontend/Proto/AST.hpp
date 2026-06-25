#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace siesta::protobuf {

enum class FieldLabel { singular, optional, repeated };

struct ProtoField {
	FieldLabel label = FieldLabel::singular;
	std::string type;
	std::string name;
	int32_t number = 0;
};

struct ProtoMapField {
	std::string key_type;
	std::string value_type;
	std::string name;
	int32_t number = 0;
};

struct ProtoEnumValue {
	std::string name;
	int32_t number = 0;
};

struct ProtoEnum {
	std::string name;
	std::vector<ProtoEnumValue> values;
};

struct ProtoReserved {
	enum Kind { numbers, names } kind;
	std::vector<int32_t> number_ranges;
	std::vector<std::string> field_names;
};

struct ProtoOneof {
	std::string name;
	std::vector<ProtoField> fields;
};

struct ProtoMessage {
	std::string name;
	std::vector<ProtoField> fields;
	std::vector<ProtoOneof> oneofs;
	std::vector<ProtoMapField> map_fields;
	std::vector<ProtoReserved> reserved;
	std::vector<ProtoMessage> nested_messages;
	std::vector<ProtoEnum> nested_enums;
};

struct ProtoRPC {
	std::string name;
	bool client_streaming = false;
	std::string request_type;
	bool server_streaming = false;
	std::string response_type;
};

struct ProtoService {
	std::string name;
	std::vector<ProtoRPC> rpcs;
};

struct ProtoImport {
	enum Modifier { none, weak, public_ } modifier = none;
	std::string path;
};

struct ProtoOption {
	std::string name;
	std::string value;
};

struct ProtoFile {
	std::string syntax;
	std::string package;
	std::vector<ProtoImport> imports;
	std::vector<ProtoOption> options;
	std::vector<ProtoMessage> messages;
	std::vector<ProtoEnum> enums;
	std::vector<ProtoService> services;
};

} // namespace siesta::protobuf
