/*
  Copyright (C) 2005-2015 Steven L. Scott

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

#include <Models/StateSpace/StateSpaceStudentRegressionModel.hpp>
#include <Models/StateSpace/Filters/SparseKalmanTools.hpp>
#include <Models/Glm/PosteriorSamplers/TDataImputer.hpp>
#include <distributions.hpp>
#include <cpputil/math_utils.hpp>
#include <stats/moments.hpp>


namespace BOOM {
  namespace {
    typedef StateSpaceStudentRegressionModel SSSRM;
    typedef StateSpace::AugmentedStudentRegressionData AugmentedData;
  }  // namespace

  AugmentedData::AugmentedStudentRegressionData() {}

  AugmentedData::AugmentedStudentRegressionData(double y, const Vector &x)
      : regression_data_(1, new RegressionData(y, x)),
        weights_(1, 1.0),
        state_model_offset_(0.0)
  {}

  AugmentedData::AugmentedStudentRegressionData(
      const std::vector<Ptr<RegressionData>> & data)
      : regression_data_(data),
        weights_(data.size(), 1.0),
        state_model_offset_(0.0)
  {}

  AugmentedData * AugmentedData::clone() const {
    return new AugmentedData(*this);
  }

  std::ostream & AugmentedData::display(std::ostream &out) const {
    out << "state model offset: " << state_model_offset_ << std::endl;
    out << std::setw(10) << "response"
        << std::setw(10) << " weight"
        << " predictors" << std::endl;
    for (int i = 0; i < regression_data_.size(); ++i) {
      out << std::setw(10) << regression_data_[i]->y()
          << std::setw(10) << weights_[i]
          << regression_data_[i]->x()
          << std::endl;
    }
    return out;
  }

  void AugmentedData::add_data(const Ptr<RegressionData> &observation) {
    regression_data_.push_back(observation);
    weights_.push_back(1.0);
  }

  void AugmentedData::set_weight(double weight, int observation) {
    if (weight < 0 || !std::isfinite(weight)) {
      report_error("Weights must be finite and non-negative.");
    }
    weights_[observation] = weight;
  }

  double AugmentedData::adjusted_observation(
      const GlmCoefs &coefficients) const {
    double ans = 0;
    double total_precision = 0;
    for (int i = 0; i < regression_data_.size(); ++i) {
      const RegressionData &data(regression_data(i));
      ans += weights_[i] * (data.y() - coefficients.predict(data.x()));
      total_precision += weights_[i];
    }
    return ans / total_precision;
  }

  double AugmentedData::sum_of_weights() const {
    return sum(weights_);
  }

  void AugmentedData::set_state_model_offset(double offset) {
    state_model_offset_ = offset;
  }

  //======================================================================
  SSSRM::StateSpaceStudentRegressionModel(int xdim)
      : StateSpaceNormalMixture(xdim > 1),
        observation_model_(new TRegressionModel(xdim))
  {
    set_observers();
  }

  SSSRM::StateSpaceStudentRegressionModel(
      const Vector &response,
      const Matrix &predictors,
      const std::vector<bool> &observed)
      : StateSpaceNormalMixture(ncol(predictors) > 0),
        observation_model_(new TRegressionModel(ncol(predictors)))
  {
    set_observers();
    if ((ncol(predictors) == 1)
        && (var(predictors.col(0)) < std::numeric_limits<double>::epsilon())) {
      set_regression_flag(false);
    }

    if (!observed.empty()) {
      if (observed.size() != response.size()) {
        report_error("Argument size mismatch between response and observed in "
                     "StateSpaceStudentRegressionModel constructor.");
      }
    }
    for (int i = 0; i < response.size(); ++i) {
      NEW(AugmentedData, data_point)(response[i], predictors.row(i));
      if (!observed.empty() && !observed[i]) {
        data_point->set_missing_status(
            Data::missing_status::completely_missing);
      }
      add_data(data_point);
    }
  }

  SSSRM::StateSpaceStudentRegressionModel(const SSSRM &rhs)
      : Model(rhs),
        StateSpaceNormalMixture(rhs),
        DataPolicy(rhs),
        PriorPolicy(rhs),
        observation_model_(rhs.observation_model_->clone())
  {}

  SSSRM * SSSRM::clone() const {return new SSSRM(*this);}

  int SSSRM::time_dimension() const {
    return dat().size();
  }

  int SSSRM::sample_size() const {
    int ans = 0;
    for (int i = 0; i < dat().size(); ++i) {
      ans += dat()[i]->sample_size();
    }
    return ans;
  }

  double SSSRM::observation_variance(int t) const {
    return observation_model_->sigsq() / dat()[t]->sum_of_weights();
  }

  double SSSRM::adjusted_observation(int t) const {
    if (is_missing_observation(t)) {
      return negative_infinity();
    }
    return dat()[t]->adjusted_observation(observation_model_->coef());
  }

  bool SSSRM::is_missing_observation(int t) const {
    return dat()[t]->missing() != Data::observed;
  }

  void SSSRM::observe_data_given_state(int t)  {
    if (!is_missing_observation(t)) {
      dat()[t]->set_state_model_offset(observation_matrix(t).dot(state(t)));
      signal_complete_data_change(t);
    }
  }

  Vector SSSRM::simulate_forecast(
      const Matrix &predictors,
      const Vector &final_state) {
    set_state_model_behavior(StateModel::MARGINAL);
    Vector state = final_state;
    Vector ans(nrow(predictors));
    int t0 = dat().size();
    double sigma = observation_model_->sigma();
    double nu = observation_model_->nu();
    for (int t = 0; t < nrow(predictors); ++t) {
      state = simulate_next_state(state, t + t0);
      double mu = observation_model_->predict(predictors.row(t))
          + observation_matrix(t+t0).dot(state);
      ans[t] = rstudent(mu, sigma, nu);
    }
    return ans;
  }

  Vector SSSRM::one_step_holdout_prediction_errors(
      RNG &rng,
      const Vector &response,
      const Matrix &predictors,
      const Vector &final_state) {
    TDataImputer data_imputer;

    if (nrow(predictors) != response.size()) {
      report_error("Size mismatch in arguments provided to "
                   "one_step_holdout_prediction_errors.");
    }
    Vector ans(response.size());
    int t0 = dat().size();
    ScalarKalmanStorage ks(state_dimension());
    ks.a = *state_transition_matrix(t0 - 1) * final_state;
    ks.P = SpdMatrix(state_variance_matrix(t0 - 1)->dense());
    double sigma = observation_model_->sigma();
    double sigsq = observation_model_->sigsq();
    double nu = observation_model_->nu();
    for (int t = 0; t < ans.size(); ++t) {
      bool missing = false;
      // 1) simulate next state.
      // 2) simulate w_t given state
      // 3) kalman update state given w_t.
      double state_contribution = observation_matrix(t+t0).dot(ks.a);
      double regression_contribution =
          observation_model_->predict(predictors.row(t));
      double mu = state_contribution + regression_contribution;
      ans[t] = response[t] - mu;

      // ans[t] is a random draw of the one step ahead prediction
      // error at time t0+t given observed data to time t0+t-1.  We
      // now proceed with the steps needed to update the Kalman filter
      // so we can compute ans[t+1].

      double weight = data_imputer.impute(rng, response[t] - mu, sigma, nu);
      double latent_variance = sigsq / weight;

      // The latent state was drawn from its predictive distribution
      // given Y[t0 + t -1] and used to impute the latent data for
      // y[t0+t].  That latent data is now used to update the Kalman
      // filter for the next time period.  It is important that we
      // discard the imputed state at this point.
      sparse_scalar_kalman_update(response[t] - regression_contribution,
                                  ks.a,
                                  ks.P,
                                  ks.K,
                                  ks.F,
                                  ks.v,
                                  missing,
                                  observation_matrix(t + t0),
                                  latent_variance,
                                  *state_transition_matrix(t + t0),
                                  *state_variance_matrix(t + t0));
    }
    return ans;
  }

  void SSSRM::set_observers() {
    observe(observation_model_->coef_prm());
    observe(observation_model_->Sigsq_prm());
    observe(observation_model_->Nu_prm());
  }

}  // namespace BOOM
