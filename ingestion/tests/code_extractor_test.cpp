#include "code_extractor.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace odysseus::ingestion;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const std::string sample = R"cpp(
#include <iostream>
#include "my_header.hpp"

// Single-line comment: void fake1() { bar(); }
/* Multi-line comment:
   class FakeClass {};
*/

class Greeter {
public:
    void greet(const std::string& name) {
        log_message(name);
        std::cout << name << "\n";
    }
};

struct Point {
    int x;
    int y;
};

static int compute(int a, int b) {
    int res = helper(a);
    return res + b;
}

int main(int argc, char* argv[]) {
    Greeter g;
    g.greet("world");
    compute(1, 2);
    const char* str = "fake_call() inside string";
    return 0;
}
)cpp";

        auto model = CodeExtractor::extract(sample);

        // Check includes
        check(model.includes.size() == 2, "Expected 2 includes");
        check(model.includes[0].target == "iostream", "Include 0 mismatch");
        check(model.includes[0].line == 2, "Include 0 line mismatch");
        check(model.includes[1].target == "my_header.hpp", "Include 1 mismatch");
        check(model.includes[1].line == 3, "Include 1 line mismatch");

        // Check classes
        check(model.classes.size() == 2, "Expected 2 classes/structs");
        check(model.classes[0].name == "Greeter", "Class 0 name mismatch");
        check(model.classes[1].name == "Point", "Class 1 name mismatch");

        // Check functions
        check(model.functions.size() == 3, "Expected 3 functions (greet, compute, main)");
        
        // Greeter::greet
        check(model.functions[0].name == "greet", "Function 0 name mismatch");
        check(model.functions[0].qualified_name == "Greeter::greet", "Function 0 qualified name mismatch");
        check(model.functions[0].return_type == "void", "Function 0 return type mismatch");

        // compute
        check(model.functions[1].name == "compute", "Function 1 name mismatch");
        check(model.functions[1].qualified_name == "compute", "Function 1 qualified name mismatch");
        check(model.functions[1].return_type == "static int", "Function 1 return type mismatch");

        // main
        check(model.functions[2].name == "main", "Function 2 name mismatch");
        check(model.functions[2].qualified_name == "main", "Function 2 qualified name mismatch");
        check(model.functions[2].return_type == "int", "Function 2 return type mismatch");

        // Check function calls
        // In greet: log_message
        // In compute: helper
        // In main: greet, compute (note: return is a keyword so not called)
        bool found_log = false;
        bool found_helper = false;
        bool found_compute_call = false;
        bool found_greet_call = false;
        bool found_fake_call = false;

        for (const auto& call : model.calls) {
            if (call.caller == "Greeter::greet" && call.callee == "log_message") found_log = true;
            if (call.caller == "compute" && call.callee == "helper") found_helper = true;
            if (call.caller == "main" && call.callee == "compute") found_compute_call = true;
            if (call.caller == "main" && call.callee == "greet") found_greet_call = true;
            if (call.callee == "fake_call" || call.callee == "fake1") found_fake_call = true;
        }

        check(found_log, "Missing log_message call in Greeter::greet");
        check(found_helper, "Missing helper call in compute");
        check(found_compute_call, "Missing compute call in main");
        check(found_greet_call, "Missing greet call in main");
        check(!found_fake_call, "Falsely captured comment or string as function call");

        std::cout << "All code extractor tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
