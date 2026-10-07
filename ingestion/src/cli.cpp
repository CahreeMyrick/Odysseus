#include <iostream>
#include "schema.hpp"
#include "pipeline.hpp"
#include "utils.hpp"


int main(int argc, char * argv) {
    
    // accept args
    if (argc != 4) {
        print_usage();
        return 1;
    }

    std::string source_type = argv[2];
    std::string uri = argv[3];

    // create request
    SourceRequest request{parse_source_type(source_type), uri};

    // try ingestion
    try {
        AdapterFactory adapter_factory;
        ArtifactProcessor processor;
        Storage storage;

        IngestionPipeline pipeline(adapter_factory, processor, storage);

        pipeline.ingest(request);
    }

    catch (const std::exception& e) {
        std::cerr << "Ingestion Error: " <<
            e.what() << std::endl;
        return 1;
    }

    return 0;
}
