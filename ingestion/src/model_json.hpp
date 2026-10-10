#pragma once

#include "models.hpp"

namespace odysseus {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SourceLocation, path, start_line, end_line, page)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Provenance, source_id, snapshot_id, retrieved_at, metadata)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileMetadata, name, path, mime_type, language, content_hash,
                                 size_bytes, location, provenance, metadata)
}
namespace odysseus::code {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SourceSpan, start_byte, end_byte, start_line, end_line)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParseDiagnostic, kind, message, span)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LexicalScope, id, kind, name, qualified_name, owner_id, span)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParserInfo, name, runtime_version, grammar, grammar_version, extractor_version)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Include, target, line, span, resolution_status)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClassDefinition, name, start_line, end_line, id, qualified_name, owner_id, kind, span)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FunctionDefinition, name, qualified_name, signature, return_type,
                                 start_line, end_line, id, owner_id, kind, span)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FunctionCall, caller, callee, line, id, caller_id, owner_id,
                                 expression, resolution_status, span)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FileModel, file, includes, classes, functions, calls,
                                 schema_version, parser, parse_status, diagnostics, scopes)
}
namespace odysseus::document {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Page, number, text)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Section, title, text, start_page, end_page)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Citation, text, target, page)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileModel, file, pages, sections, citations)
}
