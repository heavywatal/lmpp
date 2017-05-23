// -*- mode: c++; coding: utf-8 -*-
/*! @file gradient_descent.cpp
    @brief Implementation of GradientDescent class
*/
#include "gradient_descent.hpp"
#include "util.hpp"

#include <wtl/debug.hpp>
#include <wtl/exception.hpp>
#include <wtl/iostr.hpp>
#include <wtl/zfstream.hpp>
#include <wtl/prandom.hpp>
#include <wtl/itertools.hpp>

namespace likeligrid {

GradientDescent::GradientDescent(const std::string& infile,
    const size_t max_sites,
    const unsigned int concurrency)
    : GradientDescent(wtl::izfstream(infile), max_sites, concurrency) {HERE;}

GradientDescent::GradientDescent(
    std::istream& ist,
    const size_t max_sites,
    const unsigned int concurrency)
    : model_(ist, max_sites),
      concurrency_(concurrency) {HERE;

}

void GradientDescent::run() {HERE;
    const size_t dimensions = model_.names().size();
    const std::valarray<double> initial_values(0.90, dimensions);
    run(initial_values);
}

void GradientDescent::run(const std::valarray<double>& initial_values) {HERE;
    const double previous_loglik = model_.calc_loglik(initial_values);

    for (auto it = history_.emplace(initial_values, previous_loglik).first;
         it != history_.end();
         it = find_better(it)) {
        if (SIGINT_RAISED) {throw wtl::KeyboardInterrupt();}
    }

    write(std::cout);
}

MapGrid::iterator GradientDescent::find_better(const MapGrid::const_iterator& it) {
    const double previous_loglik = it->second;
    auto next_nodes = empty_neighbors_of(it->first);
    std::shuffle(std::begin(next_nodes), std::end(next_nodes), wtl::sfmt());
    for (const auto& x: next_nodes) {
        const double loglik = model_.calc_loglik(x);
        if (loglik > previous_loglik) {
            return history_.emplace(x, loglik).first;
        } else {
            history_.emplace(x, loglik);
        }
    }
    return history_.end();
}

std::vector<std::valarray<double>> GradientDescent::empty_neighbors_of(const std::valarray<double>& center) {
    const auto axes = make_vicinity(center, 3, 0.01);
    auto iter = wtl::itertools::product(axes);
    std::vector<std::valarray<double>> empty_neighbors;
    empty_neighbors.reserve(iter.max_count());
    for (const auto& x: iter()) {
        if (history_.find(x) == history_.end()) {
            empty_neighbors.push_back(x);
        }
    }
    return empty_neighbors;
}

MapGrid::const_iterator GradientDescent::mle_params() const {HERE;
    return std::max_element(std::begin(history_), std::end(history_),
        [](const MapGrid::value_type& x, const MapGrid::value_type& y){
            return x.second < y.second;
        });
}


void GradientDescent::write(std::ostream& ost) {HERE;
    ost << "##max_count=" << 0U << "\n";
    ost << "##max_sites=" << model_.max_sites() << "\n";
    ost << "##step=" << 0.01 << "\n";
    ost << "loglik\t" << wtl::join(model_.names(), "\t") << "\n";
    for (const auto& p: history_) {
        ost << p.second << "\t"
            << wtl::str_join(p.first, "\t") << "\n";
    }
}

void GradientDescent::unit_test() {HERE;
    std::stringstream sst;
    sst <<
R"({
  "pathway": ["A", "B"],
  "annotation": ["0011", "1100"],
  "sample": ["0011", "0101", "1001", "0110", "1010", "1100"]
})";
    GradientDescent searcher(sst, 4);
    searcher.run();
}

} // namespace likeligrid
