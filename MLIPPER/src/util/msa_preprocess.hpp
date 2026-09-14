#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "io/parse_file.hpp"
#include "tree/tree.hpp"

struct PreprocessedAlignments {
    parse::Alignment tree_alignment;
    parse::Alignment query_alignment;
    // Multiplicity of each retained combined reference/query site pattern.
    std::vector<unsigned> pattern_weights;
};

PreprocessedAlignments preprocess_alignments(
    const parse::Alignment& tree_alignment,
    const parse::Alignment& query_alignment = {});

void remove_repetitive_columns(
    std::vector<std::string>& rows,
    std::vector<mlipper::SequenceRecord>& queries,
    std::vector<unsigned>& pattern_weights,
    size_t& sites);
