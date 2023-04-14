#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>

#include <fmt/format.h>

template <typename... Args>
auto format(Args&&... args) -> decltype(fmt::format(std::forward<Args>(args)...)) {
	return fmt::format(std::forward<Args>(args)...);
}

class Printer {
public:
	Printer(const std::filesystem::path& path);
	Printer(const Printer&) = delete;
	Printer(Printer&& other) noexcept;

	Printer& indent();
	Printer& outdent();
	Printer& endl();
	Printer& rewind(unsigned);

	template <typename... Args>
	Printer& write(Args&&... args) {
		(*_os) << _indents;
		((*_os) << ... << args);
		(*_os) << '\n';
		return *this;
	}

	template <typename... Args>
	Printer& write_fmt(std::string_view fmt, Args&&... args) {
		(*_os) << _indents << format(std::move(fmt), std::forward<Args>(args)...) << '\n';
		return *this;
	}

	template <typename T>
	Printer& operator<<(const T& t) {
		(*_os) << _indents << t;
		return *this;
	}

	template <typename T>
	friend Printer& operator<<(Printer&, const T&);

private:
	std::unique_ptr<std::ostream> _os;
	std::string _indents;
};

template <typename T>
Printer& operator<<(Printer& p, const T& t) {
	p << t;
	return p;
}