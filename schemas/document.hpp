#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common.hpp"

namespace odysseus::document {

struct Page {
    std::size_t number = 0;
    std::string text;
};

struct Section {
    std::string title;
    std::string text;

    std::size_t start_page = 0;
    std::size_t end_page = 0;
};

struct Citation {
    std::string text;
    std::string target;

    std::size_t page = 0;
};

struct FileModel {
    FileMetadata file;

    std::vector<Page> pages;
    std::vector<Section> sections;
    std::vector<Citation> citations;
};

}
