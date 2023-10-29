#include "printer.hpp"

#include <fstream>

Printer::Printer(const std::filesystem::path& path)
	: _os(std::make_unique<std::ofstream>(path)) {}

Printer::Printer(Printer&& other) noexcept
	: _os(std::move(other._os))
	, _indents(std::move(other._indents)) {}

Printer& Printer::indent() {
	_indents.push_back('\t');
	return *this;
}
Printer& Printer::outdent() {
	_indents.pop_back();
	return *this;
}

Printer& Printer::endl() {
	(*_os) << std::endl;
	return *this;
}

Printer& Printer::rewind(unsigned n) {
	(*_os).seekp(-static_cast<std::streamoff>(n), std::ios::end);
	return *this;
}