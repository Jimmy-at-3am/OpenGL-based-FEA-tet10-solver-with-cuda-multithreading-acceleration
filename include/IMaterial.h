#pragma once

#include <Eigen/Dense>

// -----------------------------------------------------------------------------
// IMaterial
// -----------------------------------------------------------------------------
// Abstract constitutive model interface.
//
// The 6x6 matrix D is the small-strain constitutive (tangent) matrix written
// in Voigt notation with the following ordering:
//     { e_xx, e_yy, e_zz, 2*e_xy, 2*e_yz, 2*e_xz } -> { s_xx, s_yy, s_zz, s_xy, s_yz, s_xz }
//
// For the current linear-elastic baseline the tangent is constant. The
// interface is intentionally minimal now; once we move to Newton-Raphson and
// hyperelasticity it will be extended with a `ComputeStressAndTangent(F, ...)`
// virtual so that Neo-Hookean / damage models can report history-dependent
// stress together with a consistent tangent. Do not change this signature
// without also updating the assembler and all derived materials.
// -----------------------------------------------------------------------------
class IMaterial {
public:
    virtual ~IMaterial() = default;

    // Fills the 6×6 constitutive matrix D relating engineering Voigt strain ε
    // to Cauchy stress σ:   σ = D ε,
    //   ε = [ε_xx, ε_yy, ε_zz, γ_xy, γ_yz, γ_xz]^T  (γ_ij = 2ε_ij)
    //
    // D must be symmetric positive-definite for the global stiffness K to be SPD.
    // For small-strain linear elasticity D is constant; for hyperelastic or
    // damage models D is the consistent material tangent at the current strain.
    // The output matrix is fixed-size and stack-allocated in the caller.
    virtual void ComputeConstitutive(Eigen::Matrix<double, 6, 6>& D) const = 0;
};

// -----------------------------------------------------------------------------
// LinearElastic
// -----------------------------------------------------------------------------
// Isotropic, small-strain linear elasticity. Wraps the original inline logic
// that lived in FEASolver::solveLinearStatic. The arithmetic here is kept
// byte-identical to the pre-refactor baseline so the linear static simulation
// produces the same nodal displacements.
// -----------------------------------------------------------------------------
class LinearElastic : public IMaterial {
public:
    double youngsModulus;
    double poissonRatio;

    LinearElastic(double E, double nu)
        : youngsModulus(E), poissonRatio(nu) {}

    // Computes the isotropic linear elastic constitutive matrix D using Lamé
    // parameters λ and shear modulus μ = G:
    //   λ = E ν / ((1+ν)(1-2ν)),   μ = G = E / (2(1+ν))
    //
    // Explicit form (engineering shear strain convention γ_ij = 2ε_ij):
    //   D_ii = λ + 2μ  for i ∈ {0,1,2}  (normal-strain diagonal)
    //   D_ij = λ       for i≠j, i,j ∈ {0,1,2}  (normal-normal coupling)
    //   D_ii = μ        for i ∈ {3,4,5}  (shear diagonal; value = G = μ)
    // All other entries zero (no normal–shear coupling for isotropy).
    void ComputeConstitutive(Eigen::Matrix<double, 6, 6>& D) const override {
        const double E  = youngsModulus;
        const double nu = poissonRatio;
        const double c1 = E / ((1.0 + nu) * (1.0 - 2.0 * nu));

        D.setZero();
        D(0,0) = c1 * (1.0 - nu); D(0,1) = c1 * nu;         D(0,2) = c1 * nu;
        D(1,0) = c1 * nu;         D(1,1) = c1 * (1.0 - nu); D(1,2) = c1 * nu;
        D(2,0) = c1 * nu;         D(2,1) = c1 * nu;         D(2,2) = c1 * (1.0 - nu);
        D(3,3) = c1 * (0.5 - nu);
        D(4,4) = c1 * (0.5 - nu);
        D(5,5) = c1 * (0.5 - nu);
    }
};
