#include "cli.hpp"
#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    using namespace odysseus::ingestion;
    try {

        // parse cli args
        const auto options = parse_cli(argc, argv);
        if (options.help) {
            print_usage(std::cout);
            return 0;
        }

        // get configuration
        const auto config = load_config(options);
    
        // initialize the ingestion service
        auto ingestion_service = initialize_service(config, std::cout);

        // perform ingestion
        const auto result = ingestion_service.ingest(options.source);
        print_result(std::cout, result, options);
        return 0;

    } catch (const CliError& error) {
        std::cerr << error.what() << '\n';
        print_usage(std::cerr);
        return 2;

    } catch (const std::exception& error) {
        std::cerr << "Ingestion failed: " << error.what() << '\n';
        return 1;
    }
}
