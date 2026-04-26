#include "src/schema_types.hpp"

#include <fstream>
#include <iostream>
#include <string>

std::string load_file(const std::string& path) {
	std::ifstream ifs(path, std::ios::ate);
	if (!ifs) {
		throw std::runtime_error("Cannot open file: " + path);
	}
	std::string file;
	file.resize(ifs.tellg());
	ifs.seekg(0);
	ifs.read(file.data(), file.size());
	return file;
}

void callback(const jsonschema::SchemaNode& /*node*/, const std::string& /*instance_path*/) {
	// Callback for schema iteration - currently disabled
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		std::cerr << "Usage: " << argv[0] << " <schema.json> <instance.json>\n";
		return 1;
	}

	try {
		std::string schema_json = load_file(argv[1]);
		std::string instance_json = load_file(argv[2]);

		jsonschema::SchemaCompiler compiler(callback);

		if (!compiler.compile(schema_json)) {
			std::cerr << "Failed to compile schema\n";
			return 1;
		}

		bool valid = compiler.validate(instance_json);

		if (valid) {
			std::cout << "Valid\n";
			return 0;
		} else {
			std::cout << "Invalid:\n";
			for (const auto& err : compiler.errors()) {
				std::cout << "  Instance: " << err.instance_location.to_string() << ", Schema: " << err.schema_uri
						  << ", Message: " << err.message << "\n";
			}
			return 1;
		}
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
