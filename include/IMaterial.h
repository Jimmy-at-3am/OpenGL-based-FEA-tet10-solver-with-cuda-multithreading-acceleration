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

// -----------------------------------------------------------------------------
// TransverseIsotropicMaterial
// -----------------------------------------------------------------------------
// Models FDM-printed PLA where the build axis is the weak symmetry axis and the
// perpendicular plane is the isotropic strong plane (parallel to deposited layers).
//
// Five independent constants: E_p, E_z, nu_p, nu_pz, G_pz (see below).
// buildAxis selects which global axis is the weak (build / through-thickness) axis:
//   0 = X weak,  YZ is in-plane strong
//   1 = Y weak,  XZ is in-plane strong  (default — matches "Y is up/top" convention)
//   2 = Z weak,  XY is in-plane strong
//
// Fields:
//   E_p   = Young's modulus along in-plane directions (strong)
//   E_z   = Young's modulus along the build axis (weak); naming kept as E_z for brevity
//   nu_p  = in-plane Poisson's ratio (strong-strong)
//   nu_pz = out-of-plane Poisson's ratio (strong-weak coupling)
//   G_pz  = transverse shear modulus (strong-weak planes; independent of E_p, E_z, nu_*)
// In-plane shear G_p = E_p/(2(1+nu_p)) is derived from isotropy in the strong plane.
//
// The 6×6 D matrix is built from the 3×3 compliance block S (whose entries depend on
// which axis is weak) inverted for the normal-stress part, plus the shear diagonal.
// Voigt order: [σ_xx, σ_yy, σ_zz, τ_xy, τ_yz, τ_xz] / [ε_xx, ε_yy, ε_zz, γ_xy, γ_yz, γ_xz].
//
// Validated against measured FDM-PLA data:
//   Perez, Celik & Karkkainen (2021), PMC9828590:
//     E_z / E_p ≈ 0.67,  sigma_z / sigma_p ≈ 0.52
// -----------------------------------------------------------------------------
class TransverseIsotropicMaterial : public IMaterial {
public:
    double E_p;    // in-plane (strong-plane) Young's modulus
    double E_z;    // build-axis (weak) Young's modulus
    double nu_p;   // in-plane Poisson's ratio
    double nu_pz;  // out-of-plane Poisson's ratio (strong-to-weak)
    double G_pz;   // transverse shear modulus (strong-weak planes)
    int    buildAxis; // 0=X weak, 1=Y weak, 2=Z weak

    TransverseIsotropicMaterial(double Ep, double Ez, double nup, double nupz, double Gpz,
                                 int axis = 1)
        : E_p(Ep), E_z(Ez), nu_p(nup), nu_pz(nupz), G_pz(Gpz), buildAxis(axis) {}

    void ComputeConstitutive(Eigen::Matrix<double, 6, 6>& D) const override {
        // 3x3 normal-stress compliance S in Voigt [XX, YY, ZZ] row/col order.
        // Symmetry constraint nu_pz/E_p = nu_zp/E_z ensures S is symmetric.
        Eigen::Matrix3d S;
        const double G_p = E_p / (2.0 * (1.0 + nu_p));

        D.setZero();

        if (buildAxis == 2) {
            // Z is weak; XY is the strong in-plane.
            //   XX=strong, YY=strong, ZZ=weak
            S << 1.0/E_p,     -nu_p/E_p,   -nu_pz/E_p,
                -nu_p/E_p,    1.0/E_p,     -nu_pz/E_p,
                -nu_pz/E_p,  -nu_pz/E_p,   1.0/E_z;
            D(3,3) = G_p;   // XY in-plane
            D(4,4) = G_pz;  // YZ transverse
            D(5,5) = G_pz;  // XZ transverse
        } else if (buildAxis == 1) {
            // Y is weak; XZ is the strong in-plane.
            //   XX=strong, YY=weak, ZZ=strong
            S << 1.0/E_p,    -nu_pz/E_p,  -nu_p/E_p,
                -nu_pz/E_p,  1.0/E_z,     -nu_pz/E_p,
                -nu_p/E_p,  -nu_pz/E_p,   1.0/E_p;
            D(3,3) = G_pz;  // XY transverse (X strong, Y weak)
            D(4,4) = G_pz;  // YZ transverse (Y weak, Z strong)
            D(5,5) = G_p;   // XZ in-plane
        } else {
            // X is weak; YZ is the strong in-plane.
            //   XX=weak, YY=strong, ZZ=strong
            S << 1.0/E_z,    -nu_pz/E_p,  -nu_pz/E_p,
                -nu_pz/E_p,  1.0/E_p,     -nu_p/E_p,
                -nu_pz/E_p,  -nu_p/E_p,   1.0/E_p;
            D(3,3) = G_pz;  // XY transverse (X weak, Y strong)
            D(4,4) = G_p;   // YZ in-plane
            D(5,5) = G_pz;  // XZ transverse (X weak, Z strong)
        }

        D.topLeftCorner<3,3>() = S.inverse();
    }
};
