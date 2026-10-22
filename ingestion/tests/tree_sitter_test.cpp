#include "code_extractor.hpp"
#include <iostream>
#include <stdexcept>
using namespace odysseus::ingestion;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        auto c = CodeExtractor::extract("int f(void) { return external(); }", CodeLanguage::C);
        check(c.parse_status == "complete" && c.parser.grammar == "c", "C grammar failed");
        check(c.functions.size() == 1 && c.calls.size() == 1, "C extraction failed");
        check(c.calls[0].caller_id == c.functions[0].id, "C caller ownership lost");
        check(c.calls[0].resolution_status == "unresolved", "Invented call resolution");
        const std::string source = "namespace n { int f(int x) { return x; } double f(double x) { return x; } void g() { auto l = [] { f(1); }; l(); } }";
        auto cpp = CodeExtractor::extract(source);
        check(cpp.parse_status == "complete" && cpp.parser.grammar == "cpp", "C++ grammar failed");
        check(cpp.functions.size() == 4, "Overloads or lambda missing");
        check(cpp.functions[0].id != cpp.functions[1].id, "Overloads share identity");
        check(cpp.functions[0].qualified_name == "n::f", "Namespace lost");
        check(cpp.calls.size() == 2, "Lambda calls missing");
        check(cpp.calls[0].caller_id == cpp.functions[3].id, "Lambda ownership lost");
        check(cpp.calls[1].caller_id == cpp.functions[2].id, "Enclosing ownership lost");
        const auto& span = cpp.calls[0].span;
        check(source.substr(span.start_byte, span.end_byte-span.start_byte) == "f(1)", "Incorrect byte span");
        auto broken = CodeExtractor::extract("int f( { return;");
        check(broken.parse_status == "partial" && !broken.diagnostics.empty(), "Malformed input lacks diagnostics");
        check(CodeExtractor::extract("").parse_status == "complete", "Empty input failed");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
