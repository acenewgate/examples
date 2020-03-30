#include <ceres/ceres.h>

#include <Eigen/Dense>
#include <iostream>

struct Pose {
  Eigen::Vector2d translation;
  Eigen::Rotation2Dd rotation;
};

struct Constraint {
  int source;
  int target;
  Pose relative_pose;
};

template <typename T>
T NormalizeAngleDifference(T difference) {
  const T kPi = T(M_PI);
  while (difference > kPi) difference -= 2. * kPi;
  while (difference < -kPi) difference += 2. * kPi;
  return difference;
}
class SpaCostFunctor {
 public:
  SpaCostFunctor(const Pose& observed, const Eigen::Matrix3d& sqrt_information)
      : x_(observed.translation.x()),
        y_(observed.translation.y()),
        theta_(observed.rotation.angle()),
        sqrt_information_(sqrt_information) {}
  ~SpaCostFunctor() {}

  template <typename T>
  bool operator()(const T* const source_x, const T* const source_y,
                  const T* const source_theta, const T* const target_x,
                  const T* const target_y, const T* const target_theta,
                  T* residual) const {
    const T source_cos = cos(*source_theta);
    const T source_sin = sin(*source_theta);
    const T delta_x = *target_x - *source_x;
    const T delta_y = *target_y - *source_y;
    Eigen::Map<Eigen::Matrix<T, 3, 1>> residual_map(residual);
    residual_map(0) =
        static_cast<T>(x_) - (source_cos * delta_x + source_sin * delta_y);
    residual_map(1) =
        static_cast<T>(y_) - (source_cos * delta_y - source_sin * delta_x);
    residual_map(2) = NormalizeAngleDifference(static_cast<T>(theta_) -
                                               (*target_theta - *source_theta));

    residual_map = sqrt_information_.template cast<T>() * residual_map;
    return true;
  }

 private:
  const double x_;
  const double y_;
  const double theta_;
  const Eigen::Matrix3d& sqrt_information_;
};

class SpaCostFunctorAnalytic
    : public ceres::SizedCostFunction<3, 1, 1, 1, 1, 1, 1> {
 public:
  SpaCostFunctorAnalytic(const Pose& observed,
                         const Eigen::Matrix3d& sqrt_information)
      : x_(observed.translation.x()),
        y_(observed.translation.y()),
        theta_(observed.rotation.angle()),
        sqrt_information_(sqrt_information) {}
  virtual ~SpaCostFunctorAnalytic() {}

  bool Evaluate(const double* const* parameters, double* residuals,
                double** jacobians) const {
    const double cos_source_theta = cos(parameters[2][0]);
    const double sin_source_theta = sin(parameters[2][0]);
    const double dx = parameters[3][0] - parameters[0][0];
    const double dy = parameters[4][0] - parameters[1][0];

    Eigen::Map<Eigen::Vector3d> residual_map(residuals);
    residual_map(0) = x_ - (cos_source_theta * dx + sin_source_theta * dy);
    residual_map(1) = y_ - (cos_source_theta * dy - sin_source_theta * dx);
    residual_map(2) = NormalizeAngleDifference(
        theta_ - (parameters[5][0] - parameters[2][0]));
    residual_map = sqrt_information_ * residual_map;

    if (!jacobians) return true;

    double* jacobian_source_x = jacobians[0];
    double* jacobian_source_y = jacobians[1];
    double* jacobian_source_theta = jacobians[2];
    double* jacobian_target_x = jacobians[3];
    double* jacobian_target_y = jacobians[4];
    double* jacobian_target_theta = jacobians[5];

    // Some sub-expressions
    const double unweighted_jacobians_02 =
        sin_source_theta * dx - cos_source_theta * dy;
    const double unweighted_jacobians_12 =
        cos_source_theta * dx + sin_source_theta * dy;

    if (jacobian_source_x) {
      jacobian_source_x[0] = sqrt_information_(0, 0) * cos_source_theta -
                             sqrt_information_(0, 1) * sin_source_theta;
      jacobian_source_x[1] = sqrt_information_(1, 0) * cos_source_theta -
                             sqrt_information_(1, 1) * sin_source_theta;
      jacobian_source_x[2] = sqrt_information_(2, 0) * cos_source_theta -
                             sqrt_information_(2, 1) * sin_source_theta;
    }
    if (jacobian_source_y) {
      jacobian_source_y[0] = sqrt_information_(0, 0) * sin_source_theta +
                             sqrt_information_(0, 1) * cos_source_theta;
      jacobian_source_y[1] = sqrt_information_(1, 0) * sin_source_theta +
                             sqrt_information_(1, 1) * cos_source_theta;
      jacobian_source_y[2] = sqrt_information_(2, 0) * sin_source_theta +
                             sqrt_information_(2, 2) * cos_source_theta;
    }
    if (jacobian_source_theta) {
      jacobian_source_theta[0] =
          sqrt_information_(0, 0) * unweighted_jacobians_02 +
          sqrt_information_(0, 1) * unweighted_jacobians_12;
      jacobian_source_theta[1] =
          sqrt_information_(1, 0) * unweighted_jacobians_02 +
          sqrt_information_(1, 1) * unweighted_jacobians_12;
      jacobian_source_theta[2] =
          sqrt_information_(2, 0) * unweighted_jacobians_02 +
          sqrt_information_(2, 1) * unweighted_jacobians_12;
    }
    if (jacobian_target_x) {
      if (jacobian_source_x) {
        jacobian_target_x[0] = -jacobian_source_x[0];
        jacobian_target_x[1] = -jacobian_source_x[1];
        jacobian_target_x[2] = -jacobian_source_x[2];
      } else {
        jacobian_target_x[0] = sqrt_information_(0, 1) * sin_source_theta -
                               sqrt_information_(0, 0) * cos_source_theta;
        jacobian_target_x[1] = sqrt_information_(1, 1) * sin_source_theta -
                               sqrt_information_(1, 0) * cos_source_theta;
        jacobian_target_x[2] = sqrt_information_(2, 1) * sin_source_theta -
                               sqrt_information_(2, 0) * cos_source_theta;
      }
    }
    if (jacobian_target_y) {
      if (jacobian_source_y) {
        jacobian_target_y[0] = -jacobian_source_y[0];
        jacobian_target_y[1] = -jacobian_source_y[1];
        jacobian_target_y[2] = -jacobian_source_y[2];
      } else {
        jacobian_target_y[0] = -sqrt_information_(0, 0) * sin_source_theta -
                               sqrt_information_(0, 1) * cos_source_theta;
        jacobian_target_y[1] = -sqrt_information_(1, 0) * sin_source_theta -
                               sqrt_information_(1, 1) * cos_source_theta;
        jacobian_target_y[2] = -sqrt_information_(2, 0) * sin_source_theta -
                               sqrt_information_(2, 1) * cos_source_theta;
      }
    }
    if (jacobian_target_theta) {
      jacobian_target_theta[0] = -sqrt_information_(0, 2);
      jacobian_target_theta[1] = -sqrt_information_(1, 2);
      jacobian_target_theta[2] = -sqrt_information_(2, 2);
    }
    return true;
  }

 private:
  const double x_;
  const double y_;
  const double theta_;
  const Eigen::Matrix3d sqrt_information_;
};

double Optimize(const std::vector<Constraint>& constraints,
                std::map<int, Pose>& poses, bool use_analytic_cost) {
  Eigen::Matrix3d information_matrix = Eigen::Matrix3d::Identity();
  Eigen::Matrix3d sqrt_information = information_matrix.llt().matrixU();

  ceres::Problem problem;
  for (const auto& constraint : constraints) {
    auto& source = poses[constraint.source];
    auto& target = poses[constraint.target];
    if (use_analytic_cost) {
      problem.AddResidualBlock(new SpaCostFunctorAnalytic(
                                   constraint.relative_pose, sqrt_information),
                               new ceres::HuberLoss(1.0),
                               &source.translation.x(), &source.translation.y(),
                               &source.rotation.angle(),
                               &target.translation.x(), &target.translation.y(),
                               &target.rotation.angle());
    } else {
      problem.AddResidualBlock(
          new ceres::AutoDiffCostFunction<SpaCostFunctor, 3, 1, 1, 1, 1, 1, 1>(
              new SpaCostFunctor(constraint.relative_pose, sqrt_information)),
          new ceres::HuberLoss(1.0), &source.translation.x(),
          &source.translation.y(), &source.rotation.angle(),
          &target.translation.x(), &target.translation.y(),
          &target.rotation.angle());
    }
  }
  auto& reference_pose = poses[0];
  problem.SetParameterBlockConstant(&reference_pose.translation.x());
  problem.SetParameterBlockConstant(&reference_pose.translation.y());
  problem.SetParameterBlockConstant(&reference_pose.rotation.angle());
  ceres::Solver::Options options;
  ceres::Solver::Summary summary;
  options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;

  bool verbose = false;
  ceres::Solve(options, &problem, &summary);
  if (verbose) {
    std::cout << "used "
              << (use_analytic_cost ? "analytic cost\n" : "autodiff cost\n");
    std::cout << "total time: " << summary.total_time_in_seconds << std::endl;
    std::cout << "num residuals: " << summary.num_residuals << std::endl;
    std::cout << "num parameters: " << summary.num_parameters << std::endl;
    std::cout << "num effective parameters: "
              << summary.num_effective_parameters << std::endl;
    std::cout << "num successful steps: " << summary.num_successful_steps
              << std::endl;
    std::cout << "initial/final costs: " << summary.initial_cost << ", "
              << summary.final_cost << std::endl;
  }

  bool print_output = false;
  if (print_output) {
    for (const auto& pair : poses) {
      const auto& pose = pair.second;
      std::cout << "Pose " << pair.first << " is " << pose.translation.x()
                << ", " << pose.translation.y() << ", " << pose.rotation.angle()
                << std::endl;
    }
  }
  return summary.total_time_in_seconds;
}

double run(bool use_anayltic_cost) {
  Constraint constraint01, constraint12, constraint20;
  constraint01.source = 0;
  constraint01.target = 1;
  constraint12.source = 1;
  constraint12.target = 2;
  constraint20.source = 2;
  constraint20.target = 0;

  constraint01.relative_pose.translation = Eigen::Vector2d(4.0, 0.0);
  constraint01.relative_pose.rotation = Eigen::Rotation2Dd(M_PI / 2);
  constraint12.relative_pose.translation = Eigen::Vector2d(4.0, 4.0);
  constraint12.relative_pose.rotation = Eigen::Rotation2Dd(M_PI);
  constraint20.relative_pose.translation = Eigen::Vector2d(4.0, 0.0);
  constraint20.relative_pose.rotation = Eigen::Rotation2Dd(M_PI / 2);

  std::vector<Constraint> constraints;
  constraints.push_back(constraint01);
  constraints.push_back(constraint12);
  constraints.push_back(constraint20);

  std::map<int, Pose> poses;

  Pose p0, p1, p2;
  p0.translation = Eigen::Vector2d(0.0, 0.0);
  p0.rotation = Eigen::Rotation2Dd(0.0);
  p1.translation = Eigen::Vector2d(4.3, -0.2);
  p1.rotation = Eigen::Rotation2Dd(1.4208);
  p2.translation = Eigen::Vector2d(-0.5, 4.4);
  p2.rotation = Eigen::Rotation2Dd(-1.3708);

  poses[0] = p0;
  poses[1] = p1;
  poses[2] = p2;

  return Optimize(constraints, poses, use_anayltic_cost);
}

int main(void) {
  int num_iterations = 1000;
  double mean_time_auto, max_time_auto;
  mean_time_auto = max_time_auto = 0.0;
  double mean_time_anal, max_time_anal;
  mean_time_anal = max_time_anal = 0.0;

  for (int i = 0; i < num_iterations; i++) {
    double run_time = run(false);
    if (run_time > max_time_auto) max_time_auto = run_time;
    mean_time_auto += run_time;
  }
  mean_time_auto /= num_iterations;

  for (int i = 0; i < num_iterations; i++) {
    double run_time = run(true);
    if (run_time > max_time_anal) max_time_anal = run_time;
    mean_time_anal += run_time;
  }
  mean_time_anal /= num_iterations;

  std::cout << "Mean time auto: " << mean_time_auto << std::endl;
  std::cout << "Max time auto: " << max_time_auto << std::endl;

  std::cout << "Mean time anal: " << mean_time_anal << std::endl;
  std::cout << "Max time anal: " << max_time_anal << std::endl;

  return 0;
}
