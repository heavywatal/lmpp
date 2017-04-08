// -*- mode: c++; coding: utf-8 -*-
/*! @file exact.cpp
    @brief Inplementation of ExactModel class
*/
#include "exact.hpp"
#include "util.hpp"

#include <functional>

#include <json.hpp>

#include <wtl/debug.hpp>
#include <wtl/exception.hpp>
#include <wtl/iostr.hpp>
#include <wtl/zfstream.hpp>
#include <wtl/algorithm.hpp>
#include <wtl/numeric.hpp>
#include <wtl/math.hpp>
#include <wtl/os.hpp>

namespace likeligrid {

const std::vector<double> ExactModel::STEPS_ = {0.4, 0.2, 0.1, 0.05, 0.02, 0.01};
const std::vector<size_t> ExactModel::BREAKS_ = {5, 5, 5, 5, 6, 5};
bool ExactModel::SIGINT_RAISED_ = false;

ExactModel::ExactModel(const std::string& infile, const size_t max_sites):
    ExactModel(wtl::izfstream(infile), max_sites) {HERE;}

ExactModel::ExactModel(std::istream&& ist, const size_t max_sites) {HERE;
    nlohmann::json jso;
    ist >> jso;
    names_ = jso["pathway"].get<std::vector<std::string>>();
    const size_t npath = names_.size();
    annot_.reserve(npath);
    for (const std::string& s: jso["annotation"]) {
        annot_.emplace_back(s);
    }
    std::cerr << "annot_: " << annot_ << std::endl;

    const size_t nsam = jso["sample"].size();
    genot_.reserve(nsam);
    for (const std::string& s: jso["sample"]) {
        genot_.emplace_back(s);
    }
    // std::cerr << "genot_: " << genot_ << std::endl;

    const size_t ngene = genot_[0].size();
    nsam_with_s_.assign(ngene + 1, 0);  // at most
    std::valarray<double> s_gene(ngene);
    for (const auto& bits: genot_) {
        const size_t s = bits.count();
        ++nsam_with_s_[s];
        if (s > max_sites) continue;
        bits_t::size_type j = bits.find_first();
        while (j < bits_t::npos) {
            ++s_gene[j];
            j = bits.find_next(j);
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
    const auto final_max_s = nsam_with_s_.size() - 1;
    for (size_t s=2; s<=final_max_s; ++s) {
        lnp_const_ += nsam_with_s_[s] * std::log(wtl::factorial(s));
    }
    w_gene_ = s_gene / s_gene.sum();
    std::cerr << "s_gene : " << s_gene << std::endl;
    std::cerr << "w_gene_: " << w_gene_ << std::endl;
    for (size_t j=0; j<s_gene.size(); ++j) {
        if (s_gene[j] > 0) {
            lnp_const_ += s_gene[j] * std::log(w_gene_[j]);
        }
    }
    std::cerr << "lnp_const_: " << lnp_const_ << std::endl;

    // TODO
    a_pathway_.resize(npath);
    for (size_t j=0; j<npath; ++j) {
        for (size_t i=0; i<nsam; ++i) {
            size_t s = (genot_[i] & annot_[j]).count();
            if (s > 0) {
                a_pathway_[j] += --s;
            }
        }
    }
    std::cerr << "a_pathway_: " << a_pathway_ << std::endl;
    mle_params_.resize(a_pathway_.size());
    mle_params_ = 1.2;
}

void ExactModel::run(const std::string& infile) {HERE;
    const std::string outfile = init_meta(infile);
    std::cerr << "mle_params_: " << mle_params_ << std::endl;
    if (outfile.empty()) return;
    const auto axes = make_vicinity(mle_params_, BREAKS_.at(stage_), 2.0 * STEPS_.at(stage_));
    for (size_t j=0; j<names_.size(); ++j) {
        std::cerr << names_[j] << ": " << axes[j] << std::endl;
    }
    {
        wtl::ozfstream fout(outfile, std::ios::out | std::ios::app);
        std::cerr << "Writing: " << outfile << std::endl;
        run_impl(fout, wtl::itertools::product(axes));
    }
    if (outfile != "/dev/stdout") {
        run(outfile);
    }
}

void ExactModel::run_impl(std::ostream& ost, wtl::itertools::Generator<std::valarray<double>>&& gen) const {HERE;
    auto buffer = wtl::make_oss();
    std::cerr << skip_ << " to " << gen.max_count() << std::endl;
    if (skip_ == 0) {
        buffer << "##max_count=" << gen.max_count() << "\n";
        buffer << "##max_sites=" << nsam_with_s_.size() - 1 << "\n";
        buffer << "##step=" << STEPS_.at(stage_) << "\n";
        buffer << "loglik\t" << wtl::join(names_, "\t") << "\n";
    }
    for (const auto& th_path: gen(skip_)) {
        buffer << calc_loglik(th_path) << "\t"
               << wtl::str_join(th_path, "\t") << "\n";
        if (gen.count() % 100 == 0) {  // snapshot for long run
            std::cerr << "*" << std::flush;
            ost << buffer.str();
            ost.flush();
            buffer.str("");
        }
        if (SIGINT_RAISED_) {throw wtl::ExitSuccess("KeyboardInterrupt");}
    }
    std::cerr << "\n";
    ost << buffer.str();
}

class Denoms {
  public:
    Denoms() = delete;
    Denoms(const std::valarray<double>& w_gene,
        const std::valarray<double>& th_path,
        const std::vector<bits_t>& annot,
        const size_t max_sites):
        w_gene_(w_gene),
        th_path_(th_path),
        annot_(annot),
        max_sites_(max_sites),
        denoms_(max_sites + 1)
    {
        const size_t ngene = w_gene.size();
        effects_.reserve(ngene);
        for (bits_t mut_gene(ngene, 1); mut_gene.any(); mut_gene <<= 1) {
            effects_.emplace_back(translate(mut_gene));
        }
        // std::cerr << "effects_: " << effects_ << std::endl;
        mutate(bits_t(ngene, 0), bits_t(annot.size(), 0), 1.0);
        // std::cerr << "denoms_: " << denoms_ << std::endl;
    }
    const std::valarray<double> log() const {return std::log(denoms_);}

  private:
    void mutate(const bits_t& genotype, const bits_t& pathtype, const double anc_p) {
        const size_t s = genotype.count() + 1;
        for (bits_t mut_gene(genotype.size(), 1); mut_gene.any(); mut_gene <<= 1) {
            if ((genotype & mut_gene).any()) continue;
            const size_t pos = mut_gene.find_first();
            const bits_t& mut_path = effects_[pos];
            double p = anc_p;
            p *= w_gene_[pos];
            p *= discount(pathtype, mut_path);
            // std::cout << (genotype | mut_gene) << " " << (pathtype | mut_path) << " " << p << std::endl;
            denoms_[s] += p;
            if (s < max_sites_) {
                mutate(genotype | mut_gene, pathtype | mut_path, p);
            }
        }
    }

    double discount(const bits_t& pathtype, const bits_t& mut_path) const {
        if (mut_path.is_subset_of(pathtype)) {
            double p = 1.0;
            const bits_t recurrent = mut_path & pathtype;
            bits_t::size_type j = recurrent.find_first();
            while (j < bits_t::npos) {
                p *= th_path_[j];
                j = recurrent.find_next(j);
            }
            return p;
        } else {return 1.0;}
    }

    bits_t translate(const bits_t& mut_gene) const {
        bits_t mut_path(annot_.size(), 0);
        for (size_t j=0; j<annot_.size(); ++j) {
            mut_path.set(j, (annot_[j] & mut_gene).any());
        }
        return mut_path;
    }
    const std::valarray<double>& w_gene_;
    const std::valarray<double>& th_path_;
    const std::vector<bits_t>& annot_;
    const size_t max_sites_;
    std::valarray<double> denoms_;
    std::vector<bits_t> effects_;
};

double ExactModel::calc_loglik(const std::valarray<double>& th_path) const {
    const size_t max_sites = nsam_with_s_.size() - 1;
    // TODO
    double loglik = (a_pathway_ * std::log(th_path)).sum();
    const auto lnD = Denoms(w_gene_, th_path, annot_, max_sites).log();
    // std::cout << "lnD: " << lnD << std::endl;
    // -inf, 0, D2, D3, ...
    for (size_t s=2; s<=max_sites; ++s) {
        loglik -= nsam_with_s_[s] * lnD[s];
    }
    return loglik += lnp_const_;
}

std::string ExactModel::init_meta(const std::string& infile) {HERE;
    if (infile == "/dev/null") return "/dev/stdout";
    if (stage_ >= STEPS_.size()) return "";
    auto oss = wtl::make_oss(2, std::ios::fixed);
    oss << "grid-" << STEPS_.at(stage_) << ".tsv.gz";
    std::string outfile = oss.str();
    if (read_results(outfile) && skip_ == 0) {
        ++stage_;
        outfile = init_meta(outfile);
    }
    return outfile;
}

bool ExactModel::read_results(const std::string& infile) {HERE;
    if (infile == "/dev/null")
        return false;
    try {
        wtl::izfstream ist(infile);
        std::cerr << "Reading: " << infile << std::endl;
        size_t max_count;
        double step;
        std::tie(max_count, std::ignore, step) = read_metadata(ist);
        stage_ = guess_stage(STEPS_, step);
        std::vector<std::string> colnames;
        std::valarray<double> mle_params;
        std::tie(skip_, colnames, mle_params) = read_body(ist);
        if (skip_ == max_count) {  // is complete file
            skip_ = 0;
            mle_params_.swap(mle_params);
        }
        if (names_ != colnames) {
            std::ostringstream oss;
            oss << "Contradiction in column names:\n"
                << "genotype file: " << names_ << "\n"
                << "result file:" << colnames;
            throw std::runtime_error(oss.str());
        }
        return true;
    } catch (std::ios::failure& e) {
        if (errno != 2) throw;
        return false;
    }
}

void ExactModel::unit_test() {HERE;
    std::stringstream sst;
    sst <<
R"({
  "pathway": ["A", "B"],
  "annotation": ["0011", "1100"],
  "sample": ["0011", "0101", "1001", "0110", "1010", "1100"]
})";
    ExactModel model(std::move(sst), 2);
    model.run("/dev/null");
}

} // namespace likeligrid
