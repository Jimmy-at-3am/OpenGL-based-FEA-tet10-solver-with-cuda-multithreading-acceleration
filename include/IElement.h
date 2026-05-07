#pragma once

#include <Eigen/Dense>
#include <vector>

// -----------------------------------------------------------------------------
// IElement
// -----------------------------------------------------------------------------
// Abstract finite element interface.
//
// Derived classes are responsible for:
//   * Storing their reference-configuration geometry and material handle.
//   * Computing their local stiffness matrix (NumDOFs x NumDOFs).
//   * Computing their local internal force vector given a local displacement
//     increment (size NumDOFs). For linear elasticity this reduces to
//     f_e = K_e * u_e, but the API is future-proofed for Newton-Raphson,
//     arc-length, and hyperelastic kernels where f_e(u) is the nonlinear
//     internal force evaluated from the current deformation state.
//
// Design notes:
//   * The interface exposes dense Eigen::MatrixXd / Eigen::VectorXd so that the
//     assembler can stay element-agnostic (tet4 today, hex8 / tendon beams
//     tomorrow). Derived classes MUST use fixed-size stack-allocated Eigen
//     matrices for the hot-path arithmetic (B, D, Ke) and only copy into the
//     dynamic output at the end. This keeps the heap quiet while leaving the
//     global assembler generic.
//   * GetGlobalDOFs must return global DOF indices in the same order as the
//     rows/columns of the matrix written by ComputeStiffnessMatrix.
// -----------------------------------------------------------------------------
class IElement {
public:
    virtual ~IElement() = default;

    // Returns n_e, the total number of scalar DOFs for this element.
    // For a 3D solid element with n_nodes nodes: n_e = 3 * n_nodes.
    // n_e is the dimension of the local displacement vector u_e ∈ R^{n_e}
    // and the side-length of the local stiffness matrix K_e ∈ R^{n_e × n_e}.
    virtual int NumDOFs() const = 0;

    // Fills the global DOF index vector I_e (resized to NumDOFs()) such that:
    //   u_e[k]           = U[I_e[k]]              (extraction from global U)
    //   K[I_e[r], I_e[c]] += K_e[r,c]             (scatter via triplet list)
    // For a 3D solid element the mapping is:
    //   I_e[3*n + 0] = 3*nodeId[n] + 0   (x-DOF of node n)
    //   I_e[3*n + 1] = 3*nodeId[n] + 1   (y-DOF)
    //   I_e[3*n + 2] = 3*nodeId[n] + 2   (z-DOF)
    // Ordering must be consistent with the rows/columns of K_e.
    virtual void GetGlobalDOFs(std::vector<int>& outDofs) const = 0;

    // Computes the element tangent stiffness matrix K_T(u_e) = ∂F_int/∂u_e,
    // the Jacobian of the internal force vector with respect to nodal DOFs.
    //
    // For linear elasticity K_T is independent of u_e:
    //   K_e = ∫_{Ω_e} B^T D B dV
    // where B is the strain-displacement matrix and D the constitutive tensor.
    // For nonlinear materials (Neo-Hookean, damage, ...) K_T depends on the
    // current deformation state; derived classes evaluate K_T at u_e.
    //
    // Output: Ke resized to NumDOFs() × NumDOFs() and filled.
    virtual void ComputeTangentStiffness(const Eigen::VectorXd& ue,
                                         Eigen::MatrixXd&       Ke) const = 0;

    // Returns K_e = K_T(0), the tangent stiffness at zero element displacement.
    // For all linear-elastic materials K_T(u_e) is constant, so this is
    // bit-for-bit identical to ComputeTangentStiffness with any u_e.
    void ComputeStiffnessMatrix(Eigen::MatrixXd& Ke) const {
        Eigen::VectorXd ueZero = Eigen::VectorXd::Zero(NumDOFs());
        ComputeTangentStiffness(ueZero, Ke);
    }

    // Computes the element internal force vector (resized to NumDOFs()):
    //   f_{int,e}(u_e) = ∫_{Ω_e} B^T σ(ε(u_e)) dV
    //
    // For linear elasticity σ = D ε = D B u_e, so f_{int,e} = K_e u_e.
    // For nonlinear materials σ is evaluated from the current deformation
    // gradient, making f_{int,e} a nonlinear function of u_e.
    virtual void ComputeInternalForceVector(const Eigen::VectorXd& ue,
                                            Eigen::VectorXd&       fe) const = 0;
};
