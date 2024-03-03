// SPDX-License-Identifier: Apache-2.0
#include "openapi3.hpp"

#include <fstream>

namespace fs = ::std::filesystem;

namespace openapi::v3 {

// RequestBody
std::string_view RequestBody::description() const { return _GetValueIfExist<std::string_view>("description"); }
RequestBody::Content RequestBody::content() const { return _GetObjectIfExist<RequestBody::Content>("content"); }
bool RequestBody::required() const { return _GetValueIfExist<bool>("required"); }

// Response
std::string_view Response::description() const { return _GetValueIfExist<std::string_view>("description"); }
Headers Response::headers() const { return _GetObjectIfExist<Headers>("headers"); }
Response::Content Response::content() const { return _GetObjectIfExist<Response::Content>("content"); }
Response::Links Response::links() const { return _GetObjectIfExist<Response::Links>("links"); }

// Operation
Operation::Parameters Operation::parameters() const { return _GetObjectIfExist<Operation::Parameters>("parameters"); }
RequestBody Operation::requestBody() const { return _GetObjectIfExist<RequestBody>("requestBody"); }
Operation::Responses Operation::responses() const { return _GetObjectIfExist<Operation::Responses>("responses"); }
Operation::Servers Operation::servers() const { return _GetObjectIfExist<Operation::Servers>("servers"); }

// Header
std::string_view Header::description() const { return _GetValueIfExist<std::string_view>("description"); }
bool Header::required() const { return _GetValueIfExist<bool>("required"); }
bool Header::deprecated() const { return _GetValueIfExist<bool>("deprecated"); }
bool Header::allowEmptyValue() const { return _GetValueIfExist<bool>("allowEmptyValue"); }

// Parameter
bool Parameter::deprecated() const { return _GetValueIfExist<bool>("deprecated"); }
bool Parameter::allowEmptyValue() const { return _GetValueIfExist<bool>("allowEmptyValue"); }

Parameter::Location Parameter::In() const noexcept {
	const auto sv = in();
	if (sv == "path") {
		return Location::path;
	}
	if (sv == "query") {
		return Location::query;
	}
	if (sv == "header") {
		return Location::header;
	}
	if (sv == "cookie") {
		return Location::cookie;
	}
	return Location::unknown;
}

Parameter::Style Parameter::Style_() const noexcept {
	const auto sv = style();
	if (sv == "matrix") {
		return Style::matrix;
	}
	if (sv == "label") {
		return Style::label;
	}
	if (sv == "form") {
		return Style::form;
	}
	if (sv == "simple") {
		return Style::simple;
	}
	if (sv == "simple") {
		return Style::simple;
	}
	if (sv == "spaceDelimited") {
		return Style::spaceDelimited;
	}
	if (sv == "pipeDelimited") {
		return Style::pipeDelimited;
	}
	return Style::unknown;
}

// Server
std::string_view Server::description() const { return _GetValueIfExist<std::string_view>("description"); }
std::string_view Server::url() const { return _GetValueIfExist<std::string_view>("url"); }

// OpenAPI 3
std::string_view OpenAPIv3::openapi() const { return _GetValueIfExist<std::string_view>("swagger"); }
Info OpenAPIv3::info() const { return _GetObjectIfExist<Info>("info"); }
OpenAPIv3::Servers OpenAPIv3::servers() const { return _GetObjectIfExist<Servers>("servers"); }
Paths OpenAPIv3::paths() const { return _GetObjectIfExist<Paths>("paths"); }
Components OpenAPIv3::components() const { return _GetObjectIfExist<Components>("components"); }
Tags OpenAPIv3::tags() const { return _GetObjectIfExist<Tags>("tags"); }
ExternalDocumentation OpenAPIv3::externalDocs() const {
	return _GetObjectIfExist<ExternalDocumentation>("externalDocs");
}

} // namespace openapi::v3