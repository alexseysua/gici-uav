/*********************************************************************************
 *  OKVIS - Open Keyframe-based Visual-Inertial SLAM
 *  Copyright (c) 2015, Autonomous Systems Lab / ETH Zurich
 *  Copyright (c) 2016, ETH Zurich, Wyss Zurich, Zurich Eye
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 * 
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of Autonomous Systems Lab / ETH Zurich nor the names of
 *     its contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Created on: Sep 3, 2013
 *      Author: Stefan Leutenegger (s.leutenegger@imperial.ac.uk)
 *    Modified: Zurich Eye
 *********************************************************************************/

/**
 * @file OdomError.hpp
 * @brief Header file for the OdomError class.
 * @author Stefan Leutenegger
 */

#pragma once

#include <mutex>

#pragma diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// Eigen 3.2.7 uses std::binder1st and std::binder2nd which are deprecated since
// c++11.
// Fix is in 3.3 devel (http://eigen.tuxfamily.org/bz/show_bug.cgi?id=872).
#include <ceres/ceres.h>
#pragma diagnostic pop

#include "gici/utility/svo.h"
#include "gici/estimate/error_interface.h"
#include "gici/imu/odom_types.h"

namespace gici {

// to make things a bit faster than using angle-axis conversion:
__inline__ double sinc(double x)
{
  if (fabs(x) > 1e-6)
  {
   return sin(x) / x;
  }
  else
  {
    static const double c_2 = 1.0 / 6.0;
    static const double c_4 = 1.0 / 120.0;
    static const double c_6 = 1.0 / 5040.0;
    const double x_2 = x * x;
    const double x_4 = x_2 * x_2;
    const double x_6 = x_2 * x_2 * x_2;
    return 1.0 - c_2 * x_2 + c_4 * x_4 - c_6 * x_6;
  }
}

__inline__ Eigen::Quaterniond deltaQ(const Eigen::Vector3d& dAlpha)
{
  Eigen::Vector4d dq;
  double halfnorm = 0.5 * dAlpha.template tail<3>().norm();
  dq.template head<3>() = sinc(halfnorm) * 0.5 * dAlpha.template tail<3>();
  dq[3] = cos(halfnorm);
  return Eigen::Quaterniond(dq);
}

/// \brief Implements a nonlinear ODOM factor.
class OdomError :
    public ceres::SizedCostFunction<6 /* number of residuals */,
        7 /* size of first parameter (PoseParameterBlock k) */,
        // 2 /* size of second parameter (ScalerParameterBlock k) */,
        // 9 /* size of second parameter (SpeedAndBiasParameterBlock k) */,
        7 /* size of third parameter (PoseParameterBlock k+1) */>,
        // 2 /* size of fourth parameter (ScalerParameterBlock k+1) */>,
        // 9 /* size of fourth parameter (SpeedAndBiasParameterBlock k+1) */>,
    public ErrorInterface
{
public:

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  /// \brief The base in ceres we derive from
  typedef ceres::SizedCostFunction<6, 7, 7> base_t;

  /// \brief The number of residuals
  static const int kNumResiduals = 6;

  /// \brief The type of the covariance.
  typedef Eigen::Matrix<double, 6, 6> covariance_t;

  /// \brief The type of the information (same matrix dimension as covariance).
  typedef covariance_t information_t;

  /// \brief The type of hte overall Jacobian.
  typedef Eigen::Matrix<double, 6, 6> jacobian_t;

  /// \brief The type of the Jacobian w.r.t. poses --
  /// \warning This is w.r.t. minimal tangential space coordinates...
  typedef Eigen::Matrix<double, 6, 7> jacobian0_t;

  /// \brief The type of Jacobian w.r.t. Scaler
  typedef Eigen::Matrix<double, 6, 2> jacobian1_t;

  /// \brief Default constructor -- assumes information recomputation.
  OdomError() = default;

  /// \brief Trivial destructor.
  virtual ~OdomError() = default;

  /// \brief Construct with measurements and parameters.
  /// \@param[in] odom_measurements The ODOM measurements including timestamp
  /// \@param[in] odom_parameters The parameters to be used.
  /// \@param[in] t_0 Start time.
  /// \@param[in] t_1 End time.
  OdomError(const OdomMeasurements &odom_measurements,
           const OdomParameters& odom_parameters, const double &t_0,
           const double &t_1);

  /**
   * @brief Propagates pose, speeds and biases with given ODOM measurements.
   * @warning This is not actually const, since the re-propagation must somehow
   *          be stored...
   * @param[in] T_WS Start pose.
   * @param[in] speed_and_biases Start speed and biases.
   * @return Number of integration steps.
   */
  int redoPreintegration(const Transformation& T_WS) const;

  // setters
  void setRedo(const bool redo = true) const
  {
    redo_ = redo;
  }

  /// \brief (Re)set the parameters.
  /// \@param[in] odomParameters The parameters to be used.
  void setOdomParameters(const OdomParameters& odomParameters)
  {
    odom_parameters_ = odomParameters;
  }

  /// \brief (Re)set the measurements
  void setOdomMeasurements(const OdomMeasurements& odom_measurements)
  {
    odom_measurements_.clear();
    auto it_odom = odom_measurements.rbegin();
    for (; it_odom != odom_measurements.rend(); it_odom++) {
      const OdomMeasurement& odom = *it_odom;
      if (odom.timestamp < t0_ - 0.1 || 
          odom.timestamp > t1_ + 0.1) continue;
      odom_measurements_.push_front(odom);
    }
  }

  /// \brief (Re)set the start time.
  /// \@param[in] t_0 Start time.
  void setT0(const double& t_0) { t0_ = t_0; }

  /// \brief (Re)set the start time.
  /// \@param[in] t_1 End time.
  void setT1(const double& t_1) { t1_ = t_1; }

  // getters

  /// \brief Get the ODOM Parameters.
  /// \return the ODOM parameters.
  const OdomParameters& odomParameters() const
  {
    return odom_parameters_;
  }

  /// \brief Get the ODOM measurements.
  const OdomMeasurements odomMeasurements() const
  {
    return odom_measurements_;
  }

  /// \brief Get the start time.
  double t0() const { return t0_; }

  /// \brief Get the end time.
  double t1() const { return t1_; }

  // error term and Jacobian implementation
  /**
   * @brief This evaluates the error term and additionally computes the Jacobians.
   * @param parameters Pointer to the parameters (see ceres)
   * @param residuals Pointer to the residual vector (see ceres)
   * @param jacobians Pointer to the Jacobians (see ceres)
   * @return success of th evaluation.
   */
  virtual bool Evaluate(double const* const * parameters, double* residuals,
                        double** jacobians) const;

  /**
   * @brief This evaluates the error term and additionally computes
   *        the Jacobians in the minimal internal representation.
   * @param parameters Pointer to the parameters (see ceres)
   * @param residuals Pointer to the residual vector (see ceres)
   * @param jacobians Pointer to the Jacobians (see ceres)
   * @param jacobians_minimal Pointer to the minimal Jacobians
   *        (equivalent to jacobians).
   * @return Success of the evaluation.
   */
  bool EvaluateWithMinimalJacobians(double const* const * parameters,
                                    double* residuals, double** jacobians,
                                    double** jacobians_minimal) const;

  // sizes
  /// \brief Residual dimension.
  size_t residualDim() const
  {
    return kNumResiduals;
  }

  /// \brief Number of parameter blocks.
  virtual size_t parameterBlocks() const
  {
    return parameter_block_sizes().size();
  }

  /// \brief Dimension of an individual parameter block.
  /// @param[in] parameter_block_idx Index of the parameter block of interest.
  /// \return The dimension.
  size_t parameterBlockDim(size_t parameter_block_idx) const
  {
    return base_t::parameter_block_sizes().at(parameter_block_idx);
  }

  /// @brief Return parameter block type as string
  virtual ErrorType typeInfo() const
  {
    return ErrorType::kOdometerError;
  }

  // Down-weight current resiudal
  void downWeight(const double factor) { down_weight_factor_ = factor; }

  // Convert normalized residual to raw residual
  virtual void deNormalizeResidual(double *residuals) const {}

protected:
  // parameters
  OdomParameters odom_parameters_; ///< The ODOM parameters.

  // measurements
  OdomMeasurements odom_measurements_;

  // times
  double t0_; ///< The start time (i.e. time of the first set of states).
  double t1_; ///< The end time (i.e. time of the sedond set of states).

  // preintegration stuff. the mutable is a TERRIBLE HACK, but what can I do.
  mutable std::mutex preintegration_mutex_;
  ///< Protect access of intermediate results.

  // increments (initialise with identity)
  mutable Eigen::Quaterniond Delta_q_ = Eigen::Quaterniond(1,0,0,0);
  ///< Intermediate result
  mutable Eigen::Matrix3d C_integral_ = Eigen::Matrix3d::Zero();
  ///< Intermediate result
  // mutable Eigen::Matrix3d C_doubleintegral_ = Eigen::Matrix3d::Zero();
  ///< Intermediate result
  mutable Eigen::Vector3d vel_integral_ = Eigen::Vector3d::Zero();
  ///< Intermediate result
  // mutable Eigen::Vector3d acc_doubleintegral_ = Eigen::Vector3d::Zero();
  ///< Intermediate result

  // sub-Jacobians
  mutable Eigen::Vector3d dalpha_ds_w_ = Eigen::Vector3d::Zero();
  ///< Intermediate result
  // mutable Eigen::Matrix3d dv_db_g_ = Eigen::Matrix3d::Zero();
  ///< Intermediate result
  mutable Eigen::Vector3d dp_ds_w_ = Eigen::Vector3d::Zero();
  mutable Eigen::Vector3d dp_ds_v_ = Eigen::Vector3d::Zero();
  
  ///< Intermediate result

  /// \brief The Jacobian of the increment (w/o biases).
  mutable Eigen::Matrix<double,6,6> P_delta_ =
      Eigen::Matrix<double,6,6>::Zero();

  /// \brief Reference biases that are updated when called redoPreintegration.
  mutable OdomScaler scaler_ref_ = OdomScaler::Zero();

  mutable bool redo_ = true;
  ///< Keeps track of whether or not this redoPreintegration() needs to be done.
  mutable int redoCounter_ = 0;
  ///< Counts the number of preintegrations for statistics.

  // information matrix and its square root
  mutable information_t information_;
  ///< The information matrix for this error term.
  mutable information_t square_root_information_;
  ///< The square root information matrix for this error term.
  mutable double down_weight_factor_ = 1.0;

};

}  // namespace gici
