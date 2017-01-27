// -*- mode: c++; coding: utf-8 -*-
/*! @file exclusivity.hpp
    @brief Interface of Exclusivity class
*/
#pragma once
#ifndef LIKELIGRID_EXCLUSIVITY_HPP_
#define LIKELIGRID_EXCLUSIVITY_HPP_

#include <iostream>
#include <string>
#include <vector>
#include <map>

#include <Eigen/Core>
#include <cxxwtils/itertools.hpp>

namespace likeligrid {

class ExclusivityModel {
  public:
    typedef Eigen::Array<size_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> ArrayXXu;
    typedef Eigen::Array<size_t, Eigen::Dynamic, 1> ArrayXu;

    ExclusivityModel(std::istream& infile,
        const size_t grid_density,
        const size_t max_sites=65535,
        const size_t max_results=65535);

    void run(const std::string& outfile="/dev/stdout");

    const std::vector<std::string>& names() const {return names_;}
    const ArrayXXu& genotypes() const {return genotypes_;}
    const std::vector<double>& best_result() const {return results_.crbegin()->second;}

    static void unit_test();

    /////1/////////2/////////3/////////4/////////5/////////6/////////7/////////
  private:
    double calc_denom(
        const Eigen::ArrayXd& weights,
        const Eigen::ArrayXd& exclusi,
        const size_t num_mutations);
    std::ostream& write_genotypes(std::ostream&, const bool header=true) const;
    std::ostream& write_results(std::ostream&, const bool header=true) const;
    void read_results(std::istream&);
    std::vector<Eigen::ArrayXd> read_axes(std::istream&) const;

    const std::vector<std::string> names_;
    Eigen::Array<size_t, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> genotypes_;
    const size_t grid_density_;
    const size_t max_results_;
    size_t start_ = 0;
    std::vector<wtl::itertools::Product<std::vector<size_t>>> index_iters_;
    std::multimap<double, std::vector<double>> results_;
};

} // namespace likeligrid

#endif /* LIKELIGRID_EXCLUSIVITY_HPP_ */