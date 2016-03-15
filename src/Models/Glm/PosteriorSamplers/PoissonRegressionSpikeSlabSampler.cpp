/*
  Copyright (C) 2005-2014 Steven L. Scott

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

#include <Models/Glm/PosteriorSamplers/PoissonRegressionSpikeSlabSampler.hpp>
#include <distributions.hpp>
#include <cpputil/math_utils.hpp>
#include <numopt.hpp>
#include <TargetFun/TargetFun.hpp>
#include <boost/bind.hpp>

namespace BOOM {

  namespace {
    typedef PoissonRegressionSpikeSlabSampler PRSS;
  }

  PRSS::PoissonRegressionSpikeSlabSampler(
      PoissonRegressionModel *model,
      Ptr<MvnBase> slab_prior,
      Ptr<VariableSelectionPrior> spike_prior,
      int number_of_threads,
      RNG &seeding_rng)
      : PoissonRegressionAuxMixSampler(model, slab_prior, number_of_threads,
                                       seeding_rng),
        model_(model),
        sam_(model_, slab_prior, spike_prior),
        slab_prior_(slab_prior),
        spike_prior_(spike_prior),
        log_posterior_at_mode_(negative_infinity())
  {}

  void PRSS::draw() {
    impute_latent_data();
    sam_.draw_model_indicators(rng(), complete_data_sufficient_statistics());
    sam_.draw_beta(rng(), complete_data_sufficient_statistics());
  }

  double PRSS::logpri() const {
    return sam_.logpri();
  }

  void PRSS::allow_model_selection(bool tf) {
    sam_.allow_model_selection(tf);
  }

  void PRSS::limit_model_selection(int max_flips) {
    sam_.limit_model_selection(max_flips);
  }

  void PRSS::find_posterior_mode(double) {
    log_posterior_at_mode_ = negative_infinity();
    const Selector &included(model_->inc());
    d2TargetFunPointerAdapter logpost(
        boost::bind(&PoissonRegressionModel::log_likelihood, model_,
                    _1, _2, _3, _4),
        boost::bind(&MvnBase::logp_given_inclusion, slab_prior_.get(),
                    _1, _2, _3, included, _4));
    Vector beta = model_->included_coefficients();
    int dim = beta.size();
    if (dim == 0) {
      return;
      // TODO: This logic prohibits an empty model.  Better to return
      // the actual value of the un-normalized log posterior, which in
      // this case would just be the likelihood portion.
    }

    Vector gradient(dim);
    SpdMatrix hessian(dim);
    std::string error_message;
    bool ok = max_nd2_careful(beta,
                              gradient,
                              hessian,
                              log_posterior_at_mode_,
                              Target(logpost),
                              dTarget(logpost),
                              d2Target(logpost),
                              1e-5,
                              error_message);
    if (ok) {
      model_->set_included_coefficients(beta, included);
      return;
    } else {
      log_posterior_at_mode_ = negative_infinity();
      return;
    }
  }

}  // namespace BOOM
