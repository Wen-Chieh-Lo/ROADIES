#pragma once

#include <string>
#include <vector>

namespace parse {

struct Alignment {
    // names[i] and sequences[i] describe one row; every sequence has sites
    // characters after input validation and preprocessing.
    std::vector<std::string> names;
    std::vector<std::string> sequences;
    size_t sites = 0;
};

struct ModelConfig {
    int states = 4;
    std::string subst_model;
    int ncat = 1;
    double alpha = 1.0;
    double pinv = 0.0;
    std::vector<double> freqs;
    std::vector<double> rates;
    bool per_rate_scaling = true;
};

Alignment read_alignment_file(const std::string& path);

} // namespace parse
