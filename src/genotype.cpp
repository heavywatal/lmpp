/*! @file genotype.cpp
    @brief Implementation of GenotypeModel class
*/
#include "genotype.hpp"

#include <json.hpp>

#include <wtl/debug.hpp>
#include <wtl/iostr.hpp>
#include <wtl/zfstream.hpp>
#include <wtl/algorithm.hpp>
#include <wtl/math.hpp>

namespace likeligrid {

GenotypeModel::GenotypeModel(const std::string& infile, const size_t max_sites)
: filename_(infile) {
    HERE;
    wtl::izfstream ist(filename_);
    init(ist, max_sites);
}

void GenotypeModel::init(std::istream& ist, const size_t max_sites) {HERE;
    nlohmann::json jso;
    ist >> jso;
    names_ = jso["pathway"].get<std::vector<std::string>>();
    num_pathways_ = names_.size();
    annot_.reserve(num_pathways_);
    for (const std::string& s: jso["annotation"]) {
        annot_.emplace_back(s);
    }
    std::cerr << "annot_: " << annot_ << std::endl;

    const size_t nsam = jso["sample"].size();
    std::vector<bits_t> all_genotypes;
    all_genotypes.reserve(nsam);
    for (const std::string& s: jso["sample"]) {
        all_genotypes.emplace_back(s);
    }

    genot_.reserve(nsam);
    num_genes_ = jso["sample"].at(0).get<std::string>().size();
    nsam_with_s_.assign(num_genes_ + 1, 0);  // at most
    std::valarray<double> s_gene(num_genes_);
    for (const auto& bits: all_genotypes) {
        const size_t s = bits.count();
        ++nsam_with_s_[s];
        if (s > max_sites) continue;
        genot_.push_back(bits);
        for (size_t j=0; j<num_genes_; ++j) {
            if (bits[j]) ++s_gene[j];
        }
    }
    wtl::rstrip(&nsam_with_s_);
    std::cerr << "Original N_s: " << nsam_with_s_ << std::endl;
    if (max_sites + 1 < nsam_with_s_.size()) {
        nsam_with_s_.resize(max_sites + 1);
        std::cerr << "Using N_s: " << nsam_with_s_ << std::endl;
    } else {
        std::cerr << "Note: -s is too large" << std::endl;
    }
    w_gene_ = s_gene / s_gene.sum();
    std::cerr << "s_gene : " << s_gene << std::endl;
    std::cerr << "w_gene_: " << w_gene_ << std::endl;

    max_sites_ = nsam_with_s_.size() - 1;
    effects_.reserve(num_genes_);
    for (size_t j=0; j<num_genes_; ++j) {
        effects_.emplace_back(translate(j));
    }
    // std::cerr << "effects_: " << effects_ << std::endl;
}

void GenotypeModel::set_epistasis(const std::pair<size_t, size_t>& pair) {HERE;
    std::ostringstream oss;
    oss << names_.at(pair.first) << ":" << names_.at(pair.second);
    names_.push_back(oss.str());
    std::cerr << "epistasis: " << names_.back() << std::endl;
    if (pair.first == pair.second) {
        throw std::runtime_error("pair.first == pair.second");
    }
    epistasis_pair_ = pair;
    with_epistasis_ = true;
}

double GenotypeModel::calc_loglik(const std::valarray<double>& theta) {
    theta_ = theta;
    if (with_epistasis_ && theta_.size() <= num_pathways_) {
        throw std::runtime_error("theta_.size() <= num_pathways_");
    }
    denoms_.resize(max_sites_ + 1);
    denoms_ = 0.0;
    mutate(bits_t(), bits_t(), 1.0);
    // std::cerr << "denoms_: " << denoms_ << std::endl;
    double loglik = 0.0;
    for (const auto& genotype: genot_) {
        loglik += lnp_sample(genotype);
    }
    const auto lnD = std::log(denoms_);
    // std::cerr << "lnD: " << lnD << std::endl;
    // -inf, 0, D2, D3, ...
    for (size_t s=2; s<=max_sites_; ++s) {
        loglik -= nsam_with_s_[s] * lnD[s];
    }
    return loglik;
}

inline std::valarray<size_t> to_indices(const bits_t& bits) {
    std::valarray<size_t> indices(bits.count());
    for (size_t i=0, j=0; i<indices.size(); ++j) {
        if (bits[j]) {
            indices[i] = j;
            ++i;
        }
    }
    return indices;
}

inline double slice_prod(const std::valarray<double>& coefs, const bits_t& bits) {
    double p = 1.0;
    for (size_t i=0; i<coefs.size(); ++i) {
        if (bits[i]) p *= coefs[i];
    }
    return p;
}

double GenotypeModel::lnp_sample(const bits_t& genotype) const {
    double p = 0.0;
    const double p_basic = slice_prod(w_gene_, genotype);
    auto mut_route = to_indices(genotype);
    do {
        p += p_basic * discount(mut_route);
    } while (std::next_permutation(std::begin(mut_route), std::end(mut_route)));
    return std::log(p);
}

void GenotypeModel::benchmark(const size_t n) {
    const std::valarray<double> param(0.9, num_pathways_);
    double leaves = wtl::pow(static_cast<double>(num_genes_), max_sites_);
    std::cerr << "# parameters: " << num_pathways_ << std::endl;
    std::cerr << "width: " << num_genes_ << std::endl;
    std::cerr << "depth: " << max_sites_ << std::endl;
    std::cerr << "w ^ d: " << leaves * 1e-6 << " M" <<std::endl;
    wtl::benchmark([&]() {
        calc_loglik(param);
    }, "", n);
}

void GenotypeModel::test() {HERE;
    std::stringstream sst;
    sst <<
R"({
  "pathway": ["A", "B"],
  "annotation": ["0011", "1100"],
  "sample": ["0011", "0101", "1001", "0110", "1010", "1100"]
})";
    GenotypeModel model(sst, 4);
    std::cerr << model.calc_loglik({1.0, 1.0}) << std::endl;
}

} // namespace likeligrid
