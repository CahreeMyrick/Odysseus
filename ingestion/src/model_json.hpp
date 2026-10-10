#pragma once

#include "models.hpp"

namespace odysseus {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SourceLocation, path, start_line, end_line, page)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Provenance, source_id, snapshot_id, retrieved_at, metadata)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileMetadata, name, path, mime_type, language, content_hash,
                                 size_bytes, location, provenance, metadata)
}
namespace odysseus::code {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Include, target, line)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ClassDefinition, name, start_line, end_line)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FunctionDefinition, name, qualified_name, signature, return_type,
                                 start_line, end_line)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FunctionCall, caller, callee, line)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileModel, file, includes, classes, functions, calls)
}
namespace odysseus::document {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Page, number, text)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Section, title, text, start_page, end_page)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Citation, text, target, page)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileModel, file, pages, sections, citations)
}
