// Copyright 2018 Google LLC. All Rights Reserved.
/*
  Copyright (C) 2007 Steven L. Scott

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/

#include "Models/PosteriorSamplers/MvnConjSampler.hpp"
#include "Models/MvnModel.hpp"
#include "cpputil/math_utils.hpp"
#include "distributions.hpp"
#include "math/special_functions.hpp"

namespace BOOM {

  namespace NormalInverseWishart {
    NormalInverseWishartParameters::NormalInverseWishartParameters(
        const MvnGivenSigma *mean_prior, const WishartModel *precision_prior)
        : mean_model_(mean_prior),
          precision_model_(precision_prior),
          sum_of_squares_(mean_prior->dim()),
          variance_sample_size_(0),
          mean_sample_size_(0),
          mean_(mean_model_->dim()) {
      reset_to_prior();
    }

    void NormalInverseWishartParameters::reset_to_prior() {
      sum_of_squares_ = precision_model_->sumsq();
      variance_sample_size_ = precision_model_->nu();
      mean_sample_size_ = mean_model_->kappa();
      mean_ = mean_model_->mu();
    }

    void NormalInverseWishartParameters::compute_mvn_posterior(
        const MvnSuf &suf) {
      reset_to_prior();
      if (suf.n() > 0) {
        variance_sample_size_ += suf.n();
        mean_sample_size_ += suf.n();
        double shrinkage =
            mean_model_->kappa() / (mean_model_->kappa() + suf.n());
        mean_ *= shrinkage;
        mean_.axpy(suf.ybar(), 1 - shrinkage);

        sum_of_squares_ += suf.center_sumsq();
        workspace_ = suf.ybar();
        workspace_ -= mean_;
        sum_of_squares_.add_outer(workspace_, suf.n(), false);

        workspace_ = mean_model_->mu();
        workspace_ -= mean_;
        sum_of_squares_.add_outer(workspace_, mean_model_->kappa(), false);
        sum_of_squares_.reflect();
      }
    }

  }  // namespace NormalInverseWishart

  namespace {
    typedef MvnConjSampler MCS;
  }  // namespace

  MCS::MvnConjSampler(MvnModel *mod, const Vector &mu0, double kappa,
                      const SpdMatrix &SigmaHat, double prior_df,
                      RNG &seeding_rng)
      : ConjugateHierarchicalPosteriorSampler(seeding_rng),
        mod_(mod),
        mu_(new MvnGivenSigma(mu0, kappa, mod_->Sigma_prm())),
        siginv_(new WishartModel(prior_df, SigmaHat)),
        prior_(mu_.get(), siginv_.get()),
        posterior_(mu_.get(), siginv_.get()) {}

  MCS::MvnConjSampler(MvnModel *mod, const Ptr<MvnGivenSigma> &mu,
                      const Ptr<WishartModel> &Siginv, RNG &seeding_rng)
      : ConjugateHierarchicalPosteriorSampler(seeding_rng),
        mod_(mod),
        mu_(mu),
        siginv_(Siginv),
        prior_(mu_.get(), siginv_.get()),
        posterior_(mu_.get(), siginv_.get()) {
    if (mod_) {
      mu_->set_Sigma(mod_->Sigma_prm());
    }
  }

  double MCS::logpri() const {
    return mod_ ? log_prior_density(*mod_) : negative_infinity();
  }

  const Vector &MCS::mu0() const { return mu_->mu(); }
  double MCS::kappa() const { return mu_->kappa(); }
  double MCS::prior_df() const { return siginv_->nu(); }
  const SpdMatrix &MCS::prior_SS() const { return siginv_->sumsq(); }

  void MCS::draw_model_parameters(Model &model) {
    draw_model_parameters(dynamic_cast<MvnModel &>(model));
  }

  void MCS::draw_model_parameters(MvnModel &model) {
    posterior_.compute_mvn_posterior(*(model.suf()));

    SpdMatrix Siginv = rWish_mt(rng(), posterior_.variance_sample_size(),
                                posterior_.sum_of_squares().inv());
    Vector mu = rmvn_ivar_mt(rng(), posterior_.mean(),
                             posterior_.mean_sample_size() * Siginv);
    model.set_siginv(Siginv);
    model.set_mu(mu);
  }

  double MCS::log_prior_density(const ConstVectorView &parameters) const {
    int dim = mu_->dim();
    Vector mu(dim);
    SpdMatrix Sigma(dim);
    std::copy(parameters.begin(), parameters.end(), mu.begin());
    Sigma.unvectorize(parameters.begin() + dim, true);
    return mu_->logp(mu) + siginv_->logp(Sigma.inv());
  }

  double MCS::log_prior_density(const Model &model) const {
    return log_prior_density(dynamic_cast<const MvnModel &>(model));
  }

  double MCS::log_prior_density(const MvnModel &model) const {
    return siginv_->logp(model.siginv()) + mu_->logp(model.mu());
  }

  void MCS::draw() {
    if (mod_) {
      draw_model_parameters(*mod_);
    }
  }

  void MCS::find_posterior_mode(double) {
    if (mod_) {
      posterior_.compute_mvn_posterior(*(mod_->suf()));
      mod_->set_mu(posterior_.mean());
      double scale_factor =
          posterior_.variance_sample_size() - mod_->dim() - 1.0;
      if (scale_factor < 0) scale_factor = 0.0;
      mod_->set_siginv(posterior_.sum_of_squares() * scale_factor);
    }
  }

  double MCS::log_marginal_density(const Ptr<Data> &dp,
                                   const ConjugateModel *abstract_model) const {
    const MvnModel *model(dynamic_cast<const MvnModel *>(abstract_model));
    if (!model) {
      report_error(
          "The MvnConjSampler is only conjugate with "
          "MvnModel objects.");
    }
    return log_marginal_density(*model->DAT(dp), model);
  }

  double MCS::log_marginal_density(const VectorData &data_point,
                                   const MvnModel *model) const {
    const MvnSuf &suf(*model->suf());
    prior_.compute_mvn_posterior(suf);
    MvnSuf posterior_suf(suf);
    posterior_suf.update_raw(data_point.value());
    posterior_.compute_mvn_posterior(posterior_suf);
    int dim = prior_.mean().size();
    double ans =
        0.5 * dim *
            log(prior_.mean_sample_size() / posterior_.mean_sample_size()) +
        0.5 * prior_.variance_sample_size() * prior_.sum_of_squares().logdet() -
        0.5 * posterior_.variance_sample_size() *
            posterior_.sum_of_squares().logdet() +
        lmultigamma_ratio(prior_.variance_sample_size() / 2.0, 1, dim);
    return ans;
  }

}  // namespace BOOM
