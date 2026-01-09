#include "Labs/4-Animation/tasks.h"
#include "CustomFunc.inl"
#include "IKSystem.h"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <spdlog/spdlog.h>

namespace VCX::Labs::Animation {
    void ForwardKinematics(IKSystem & ik, int StartIndex) {
        if (StartIndex == 0) {
            ik.JointGlobalRotation[0] = ik.JointLocalRotation[0];
            ik.JointGlobalPosition[0] = ik.JointLocalOffset[0];
            StartIndex                = 1;
        }

        for (int i = StartIndex; i < ik.JointLocalOffset.size(); i++) {
            // your code here: forward kinematics, update JointGlobalPosition and JointGlobalRotation
            ik.JointGlobalRotation[i] = ik.JointGlobalRotation[i - 1] * ik.JointLocalRotation[i];
            ik.JointGlobalPosition[i] = ik.JointGlobalPosition[i - 1] + glm::rotate(ik.JointGlobalRotation[i - 1], ik.JointLocalOffset[i]);
        }
    }

    void InverseKinematicsCCD(IKSystem & ik, const glm::vec3 & EndPosition, int maxCCDIKIteration, float eps) {
        ForwardKinematics(ik, 0);
        // These functions will be useful: glm::normalize, glm::rotation, glm::quat * glm::quat
        for (int CCDIKIteration = 0; CCDIKIteration < maxCCDIKIteration && glm::l2Norm(ik.EndEffectorPosition() - EndPosition) > eps; CCDIKIteration++) {
            // your code here: ccd ik
            for (int i = ik.JointLocalOffset.size() - 2; i >= 0; i--) {
                const glm::vec3 & jointPos      = ik.JointGlobalPosition[i];
                const glm::vec3 & currentEndPos = ik.EndEffectorPosition();
                const glm::vec3 & targetPos     = EndPosition;
                glm::vec3         vectorC       = currentEndPos - jointPos;
                glm::vec3         vectorT       = targetPos - jointPos;
                glm::vec3         normalizedC   = glm::normalize(vectorC);
                glm::vec3         normalizedT   = glm::normalize(vectorT);
                glm::quat         rotationQuat  = glm::rotation(normalizedC, normalizedT);
                ik.JointLocalRotation[i]        = rotationQuat * ik.JointLocalRotation[i];
                ForwardKinematics(ik, i);
            }
        }
    }

    void InverseKinematicsFABR(IKSystem & ik, const glm::vec3 & EndPosition, int maxFABRIKIteration, float eps) {
        ForwardKinematics(ik, 0);
        int                    nJoints = ik.NumJoints();
        std::vector<glm::vec3> backward_positions(nJoints, glm::vec3(0, 0, 0)), forward_positions(nJoints, glm::vec3(0, 0, 0));
        for (int IKIteration = 0; IKIteration < maxFABRIKIteration && glm::l2Norm(ik.EndEffectorPosition() - EndPosition) > eps; IKIteration++) {
            // task: fabr ik
            // backward update
            glm::vec3 next_position         = EndPosition;
            backward_positions[nJoints - 1] = EndPosition;

            for (int i = nJoints - 2; i >= 0; i--) {
                // your code here
                const glm::vec3 & P_i       = ik.JointGlobalPosition[i];
                float             L_i       = ik.JointOffsetLength[i + 1];
                glm::vec3         V         = P_i - next_position;
                float             dist      = glm::length(V);
                glm::vec3         direction = V / dist;
                backward_positions[i]       = next_position + direction * L_i;
                next_position               = backward_positions[i];
            }

            // forward update
            glm::vec3 now_position = ik.JointGlobalPosition[0];
            forward_positions[0]   = ik.JointGlobalPosition[0];
            for (int i = 0; i < nJoints - 1; i++) {
                // your code here
                const glm::vec3 & P_double_prime_i_plus_1 = backward_positions[i + 1];
                float             L_i                     = ik.JointOffsetLength[i + 1];
                glm::vec3         V                       = P_double_prime_i_plus_1 - now_position;
                float             dist                    = glm::length(V);
                glm::vec3         direction               = V / dist;
                forward_positions[i + 1]                  = now_position + direction * L_i;
                now_position                              = forward_positions[i + 1];
            }
            ik.JointGlobalPosition = forward_positions; // copy forward positions to joint_positions
        }

        // Compute joint rotation by position here.
        for (int i = 0; i < nJoints - 1; i++) {
            ik.JointGlobalRotation[i] = glm::rotation(glm::normalize(ik.JointLocalOffset[i + 1]), glm::normalize(ik.JointGlobalPosition[i + 1] - ik.JointGlobalPosition[i]));
        }
        ik.JointLocalRotation[0] = ik.JointGlobalRotation[0];
        for (int i = 1; i < nJoints - 1; i++) {
            ik.JointLocalRotation[i] = glm::inverse(ik.JointGlobalRotation[i - 1]) * ik.JointGlobalRotation[i];
        }
        ForwardKinematics(ik, 0);
    }

    IKSystem::Vec3ArrPtr IKSystem::BuildCustomTargetPosition() {
        // get function from https://www.wolframalpha.com/input/?i=Albert+Einstein+curve
        int nums      = 4000;
        using Vec3Arr = std::vector<glm::vec3>;
        std::shared_ptr<Vec3Arr> custom(new Vec3Arr(nums));
        int                      index = 0;
        for (int i = 0; i < nums; i++) {
            // float x_val = 1.5e-3f * custom_x(92 * glm::pi<float>() * i / nums);
            // float y_val = 1.5e-3f * custom_y(92 * glm::pi<float>() * i / nums);
            float x_val = (float(i) / nums * 4) * 0.5;
            float y_val = (x_val - 1) * (x_val - 1);
            // if (std::abs(x_val) < 1e-3 || std::abs(y_val) < 1e-3) continue;
            (*custom)[index++] = glm::vec3(1.6f - x_val, 0.0f, y_val - 0.2f);
        }
        custom->resize(index);
        return custom;
    }

    static Eigen::VectorXf glm2eigen(std::vector<glm::vec3> const & glm_v) {
        Eigen::VectorXf v = Eigen::Map<Eigen::VectorXf const, Eigen::Aligned>(reinterpret_cast<float const *>(glm_v.data()), static_cast<int>(glm_v.size() * 3));
        return v;
    }

    static std::vector<glm::vec3> eigen2glm(Eigen::VectorXf const & eigen_v) {
        return std::vector<glm::vec3>(
            reinterpret_cast<glm::vec3 const *>(eigen_v.data()),
            reinterpret_cast<glm::vec3 const *>(eigen_v.data() + eigen_v.size()));
    }

    static Eigen::SparseMatrix<float> CreateEigenSparseMatrix(std::size_t n, std::vector<Eigen::Triplet<float>> const & triplets) {
        Eigen::SparseMatrix<float> matLinearized(n, n);
        matLinearized.setFromTriplets(triplets.begin(), triplets.end());
        return matLinearized;
    }

    // solve Ax = b and return x
    static Eigen::VectorXf ComputeSimplicialLLT(
        Eigen::SparseMatrix<float> const & A,
        Eigen::VectorXf const &            b) {
        auto solver = Eigen::SimplicialLLT<Eigen::SparseMatrix<float>>(A);
        return solver.solve(b);
    }

    // void AdvanceMassSpringSystem(MassSpringSystem & system, float const dt) {
    //     // your code here: rewrite following code
    //     int const   steps = 1000;
    //     float const ddt   = dt / steps;
    //     for (std::size_t s = 0; s < steps; s++) {
    //         std::vector<glm::vec3> forces(system.Positions.size(), glm::vec3(0));
    //         for (auto const spring : system.Springs) {
    //             auto const      p0  = spring.AdjIdx.first;
    //             auto const      p1  = spring.AdjIdx.second;
    //             glm::vec3 const x01 = system.Positions[p1] - system.Positions[p0];
    //             glm::vec3 const v01 = system.Velocities[p1] - system.Velocities[p0];
    //             glm::vec3 const e01 = glm::normalize(x01);
    //             glm::vec3       f   = (system.Stiffness * (glm::length(x01) - spring.RestLength) + system.Damping * glm::dot(v01, e01)) * e01;
    //             forces[p0] += f;
    //             forces[p1] -= f;
    //         }
    //         for (std::size_t i = 0; i < system.Positions.size(); i++) {
    //             if (system.Fixed[i]) continue;
    //             system.Velocities[i] += (glm::vec3(0, -system.Gravity, 0) + forces[i] / system.Mass) * ddt;
    //             system.Positions[i] += system.Velocities[i] * ddt;
    //         }
    //     }
    // }
    void AdvanceMassSpringSystem(MassSpringSystem & system, float const dt) {
        std::size_t const N           = system.Positions.size();
        float const       inv_dt2     = 1.0f / (dt * dt);
        int const         Dim         = 3;
        size_t const      matrix_size = N * Dim;

        // 当前状态
        std::vector<glm::vec3> p_t = system.Positions;
        std::vector<glm::vec3> v_t = system.Velocities;

        // 牛顿迭代的初始猜测：p_{t+1}^0 = p_t + v_t * dt (隐式欧拉初始值)
        std::vector<glm::vec3> p_new = p_t;
        for (size_t i = 0; i < N; ++i) {
            if (! system.Fixed[i]) {
                p_new[i] += v_t[i] * dt;
            }
        }

        const int   MAX_ITERATIONS = 20;    // 增加迭代次数
        const float TOLERANCE_SQ   = 1e-8f; // 更合理的容忍度（无需平方两次）
        const float DAMPING        = 0.98f; // 增加数值阻尼（关键！）
        const float MAX_STEP       = 0.1f;  // 步长限制

        for (int k = 0; k < MAX_ITERATIONS; ++k) {
            // --- 1. 构建残差函数 G(p_new) ---
            // 正确残差：G(p) = M/dt²*(p - p_t - v_t*dt) - (F_int + F_ext) = 0
            std::vector<glm::vec3> G_vec(N, glm::vec3(0.0f));

            // 惯性项：M/dt²*(p_new - (p_t + v_t*dt))
            for (size_t i = 0; i < N; ++i) {
                if (system.Fixed[i]) {
                    G_vec[i] = p_new[i] - p_t[i]; // 固定点残差：强制位移不变
                    continue;
                }
                G_vec[i] = system.Mass * inv_dt2 * (p_new[i] - (p_t[i] + v_t[i] * dt));

                // 外部力 F_ext (重力)
                glm::vec3 F_ext = glm::vec3(0, -system.Mass * system.Gravity, 0);

                // 残差 = 惯性项 - (内力 + 外力)
                G_vec[i] -= F_ext;
            }

            // 内部力 F_int(p_new)
            for (auto const & spring : system.Springs) {
                auto const p0_idx = spring.AdjIdx.first;
                auto const p1_idx = spring.AdjIdx.second;

                if (system.Fixed[p0_idx] && system.Fixed[p1_idx]) continue;

                glm::vec3 const x01 = p_new[p1_idx] - p_new[p0_idx];
                float const     L   = glm::length(x01);

                if (L < 1e-6f) continue;

                glm::vec3 const e01 = x01 / L;
                float const     k_s = system.Stiffness;

                // 弹簧力：F_spring = k_s*(L - L0)*e01
                glm::vec3 const F_spring = k_s * (L - spring.RestLength) * e01;

                // 残差中减去内力（G = 惯性项 - (内力+外力)）
                if (! system.Fixed[p0_idx]) G_vec[p0_idx] -= F_spring;
                if (! system.Fixed[p1_idx]) G_vec[p1_idx] += F_spring; // F_1 = -F_0
            }

            // 检查残差范数
            Eigen::VectorXf G_eigen          = glm2eigen(G_vec);
            float           residual_norm_sq = G_eigen.squaredNorm();
            if (residual_norm_sq < TOLERANCE_SQ) {
                break; // 收敛
            }

            // --- 2. 构建雅可比矩阵 J = dG/dp ---
            std::vector<Eigen::Triplet<float>> triplets_J;

            // 惯性项的雅可比：M/dt² * I
            for (size_t i = 0; i < N; ++i) {
                if (system.Fixed[i]) {
                    // 固定点：J=1，强制dp=0
                    for (int d = 0; d < Dim; ++d) {
                        triplets_J.emplace_back(i * Dim + d, i * Dim + d, 1.0f);
                    }
                } else {
                    float const mass_term = system.Mass * inv_dt2;
                    for (int d = 0; d < Dim; ++d) {
                        triplets_J.emplace_back(i * Dim + d, i * Dim + d, mass_term);
                    }
                }
            }

            // 内力的雅可比：-dF_int/dp（因为G = 惯性项 - F_int，所以J包含 -dF_int/dp）
            for (auto const & spring : system.Springs) {
                auto const p0_idx = spring.AdjIdx.first;
                auto const p1_idx = spring.AdjIdx.second;

                if (system.Fixed[p0_idx] && system.Fixed[p1_idx]) continue;

                glm::vec3 const x01 = p_new[p1_idx] - p_new[p0_idx];
                float const     L   = glm::length(x01);

                if (L < 1e-6f) continue;

                glm::vec3 const e01 = x01 / L;
                float const     k_s = system.Stiffness;
                float const     L0  = spring.RestLength;

                // 正确的切线刚度矩阵：dF_int/dp
                // 推导：dF0/dp0 = k_s*[(1-L0/L)(I - ee^T) + ee^T]
                //      dF0/dp1 = -dF0/dp0
                //      dF1/dp0 = -dF0/dp0
                //      dF1/dp1 = dF0/dp0
                glm::mat3 eeT     = glm::outerProduct(e01, e01);
                glm::mat3 I       = glm::mat3(1.0f);
                glm::mat3 dF0_dp0 = k_s * ((1.0f - L0 / L) * (I - eeT) + eeT);

                // 雅可比J包含 -dF_int/dp，所以符号反转
                auto add_block = [&](size_t row_idx, size_t col_idx, const glm::mat3 & mat) {
                    if (system.Fixed[row_idx] || system.Fixed[col_idx]) return;
                    for (int r = 0; r < 3; ++r) {
                        for (int c = 0; c < 3; ++c) {
                            int global_row = row_idx * 3 + r;
                            int global_col = col_idx * 3 + c;
                            triplets_J.emplace_back(global_row, global_col, mat[r][c]);
                        }
                    }
                };

                // J = 惯性项 - dF_int/dp → 所以：
                add_block(p0_idx, p0_idx, dF0_dp0);  // -dF0/dp0
                add_block(p0_idx, p1_idx, -dF0_dp0); // -(-dF0/dp0) = +dF0/dp0
                add_block(p1_idx, p0_idx, -dF0_dp0); // -(-dF0/dp0) = +dF0/dp0
                add_block(p1_idx, p1_idx, dF0_dp0);  // -dF1/dp1 = -dF0/dp0
            }

            // 构建雅可比矩阵和右侧向量
            Eigen::SparseMatrix<float> J   = CreateEigenSparseMatrix(matrix_size, triplets_J);
            Eigen::VectorXf            rhs = -G_eigen; // 求解 J*dp = -G

            // 固定点强制rhs=0，确保dp=0
            for (size_t i = 0; i < N; ++i) {
                if (system.Fixed[i]) {
                    for (int d = 0; d < 3; ++d) {
                        rhs(i * 3 + d) = 0.0f;
                    }
                }
            }

            // --- 3. 求解线性系统 ---
            Eigen::VectorXf        dp_eigen = ComputeSimplicialLLT(J, rhs);
            std::vector<glm::vec3> dp       = eigen2glm(dp_eigen);

            // --- 4. 步长限制（防止发散）---
            float dp_norm    = std::sqrt(dp_eigen.squaredNorm());
            float step_scale = 1.0f;
            if (dp_norm > MAX_STEP) {
                step_scale = MAX_STEP / dp_norm;
            }

            // --- 5. 更新p_new ---
            for (size_t i = 0; i < N; ++i) {
                if (! system.Fixed[i]) {
                    p_new[i] += dp[i] * step_scale * DAMPING;
                }
            }

            // 步长太小则提前退出
            if (dp_norm * step_scale < 1e-8f) {
                break;
            }
        } // end Newton loop

        // --- 6. 更新系统状态 ---
        for (size_t i = 0; i < N; ++i) {
            if (! system.Fixed[i]) {
                // 速度更新：隐式欧拉 v_{t+1} = (p_{t+1} - p_t)/dt
                system.Velocities[i] = (p_new[i] - p_t[i]) / dt;
                // 位置更新
                system.Positions[i] = p_new[i];
            }
        }
    }
} // namespace VCX::Labs::Animation