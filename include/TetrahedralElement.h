#pragma once

#include <array>
#include <Eigen/Dense>

#include "IElement.h"
#include "IMaterial.h"

// -----------------------------------------------------------------------------
// TetrahedralElement (linear tet4, 3D, 3 DOFs / node)
// -----------------------------------------------------------------------------
// Classical constant-strain tetrahedron. All hot-path arithmetic uses
// fixed-size Eigen matrices so the per-element work is entirely stack-resident:
//
//     P   : Eigen::Matrix4d                (4 x 4)
//     C   : Eigen::Matrix4d                (4 x 4)     = P^{-1}
//     B   : Eigen::Matrix<double, 6, 12>   (6 x 12)
//     D   : Eigen::Matrix<double, 6, 6>    (6 x 6)
//     Ke  : Eigen::Matrix<double, 12, 12>  (12 x 12)
//
// Only the single copy into the dynamic `Eigen::MatrixXd&` output at the end
// of ComputeStiffnessMatrix / ComputeInternalForceVector touches the heap --
// and that output buffer is owned and reused by the global assembler.
//
// This implementation is the faithful decomposition of the original inline
// block that lived in FEASolver::solveLinearStatic (pre-refactor): same P
// construction, same |det(P)| for vol6, same C = P^{-1}, same B layout,
// same `volume * B^T * D * B` expression. This guarantees byte-identical
// element stiffness matrices relative to the pre-refactor baseline.
// -----------------------------------------------------------------------------
class TetrahedralElement : public IElement {
public:
    static constexpr int kNumNodes = 4;
    static constexpr int kNumDOFs  = 12;

    // Reference-configuration nodal positions. Column i = node i's (x, y, z).
    Eigen::Matrix<double, 3, 4>     nodeCoords;
    // Global node indices for this element, in the same order as the columns
    // of nodeCoords.
    std::array<int, kNumNodes>      nodeIds;
    // Non-owning pointer to the constitutive model. Lifetime is managed by
    // the caller (typically FEASolver owns a std::vector<IMaterial*> ).
    const IMaterial*                material = nullptr;

    TetrahedralElement(const Eigen::Matrix<double, 3, 4>& coords,
                       const std::array<int, kNumNodes>&  ids,
                       const IMaterial*                   mat)
        : nodeCoords(coords), nodeIds(ids), material(mat) {}

    int NumDOFs() const override { return kNumDOFs; }

    // Fills the 12-entry scatter index vector I_e:
    //   I_e[3n+k] = 3*nodeIds[n] + k,  n ∈ {0..3},  k ∈ {0,1,2}
    // This maps local DOF index to global DOF in the assembled system, consistent
    // with the row/column ordering of K_e from ComputeTangentStiffness.
    void GetGlobalDOFs(std::vector<int>& outDofs) const override {
        outDofs.resize(kNumDOFs);
        for (int n = 0; n < kNumNodes; ++n) {
            outDofs[n * 3 + 0] = nodeIds[n] * 3 + 0;
            outDofs[n * 3 + 1] = nodeIds[n] * 3 + 1;
            outDofs[n * 3 + 2] = nodeIds[n] * 3 + 2;
        }
    }

    // Computes K_e = V_e * B^T * D * B  (12×12, constant for linear Tet4).
    //
    // Because B is constant throughout the element (constant-strain tet),
    // the volume integral ∫_{Ω_e} B^T D B dV reduces to the closed-form
    // product V_e B^T D B — no numerical quadrature is needed.
    // The input ue is intentionally ignored; it is present only to satisfy
    // the IElement interface, which supports state-dependent tangents.
    void ComputeTangentStiffness(const Eigen::VectorXd& /*ue*/,
                                 Eigen::MatrixXd&       Ke) const override {
        Eigen::Matrix<double, 6, 12>  B;
        const double volume = ComputeBMatrixAndVolume(B);

        Eigen::Matrix<double, 6, 6>   D;
        material->ComputeConstitutive(D);

        // NOTE: expression order is preserved from the pre-refactor baseline
        // (`volume * B.transpose() * D * B`) to keep bit-identical arithmetic.
        Eigen::Matrix<double, 12, 12> KeLocal = volume * B.transpose() * D * B;

        Ke = KeLocal;
    }

    // Computes f_e = K_e * u_e = V_e * B^T * D * B * u_e  (linear elasticity).
    //
    // Mathematically: evaluates stress σ = D * B * u_e (constant over the
    // element for Tet4) and integrates B^T σ over V_e. For a future
    // hyperelastic override, σ would be computed from the deformation gradient
    // F = I + ∇u instead of the linear approximation B u_e.
    void ComputeInternalForceVector(const Eigen::VectorXd& ue,
                                    Eigen::VectorXd&       fe) const override {
        Eigen::Matrix<double, 12, 1>  ueLocal;
        for (int i = 0; i < kNumDOFs; ++i) ueLocal(i) = ue(i);

        Eigen::Matrix<double, 6, 12>  B;
        const double volume = ComputeBMatrixAndVolume(B);

        Eigen::Matrix<double, 6, 6>   D;
        material->ComputeConstitutive(D);

        // Linear-elastic internal force: f_e = K_e * u_e.
        // For future Neo-Hookean / damage elements this routine will be
        // overridden (or a derived `HyperelasticTetrahedralElement` introduced)
        // to evaluate the physically meaningful f_int from F = I + grad(u).
        Eigen::Matrix<double, 12, 12> KeLocal = volume * B.transpose() * D * B;
        Eigen::Matrix<double, 12, 1>  feLocal = KeLocal * ueLocal;

        fe.resize(kNumDOFs);
        for (int i = 0; i < kNumDOFs; ++i) fe(i) = feLocal(i);
    }

private:
    // Constructs the constant 6×12 strain-displacement matrix B and returns
    // V_e = |det(P)| / 6, the element volume.
    //
    // Algorithm — Vandermonde inversion:
    //   1. Assemble P ∈ R^{4×4} with columns [1, x_i, y_i, z_i]^T:
    //        row 0: ones;  row 1: x-coords;  row 2: y-coords;  row 3: z-coords
    //   2. Invert: C = P^{-1}.  Shape functions: N_i = C_{0i} + C_{1i}x + C_{2i}y + C_{3i}z.
    //      Spatial derivatives (constant throughout element):
    //        β_i = C_{1i} = ∂N_i/∂x,   γ_i = C_{2i} = ∂N_i/∂y,   δ_i = C_{3i} = ∂N_i/∂z
    //   3. Assemble B in engineering Voigt order [ε_xx, ε_yy, ε_zz, γ_xy, γ_yz, γ_xz]:
    //        ε_xx row: [β_i,  0,    0  ]  per node i
    //        ε_yy row: [0,    γ_i,  0  ]
    //        ε_zz row: [0,    0,    δ_i]
    //        γ_xy row: [γ_i,  β_i,  0  ]
    //        γ_yz row: [0,    δ_i,  γ_i]
    //        γ_xz row: [δ_i,  0,    β_i]
    //   4. V_e = |det(P)| / 6  (signed tet volume = det(P)/6).
    double ComputeBMatrixAndVolume(Eigen::Matrix<double, 6, 12>& B) const {
        Eigen::Matrix4d P;
        for (int i = 0; i < kNumNodes; ++i) {
            P(0, i) = 1.0;
            P(1, i) = nodeCoords(0, i);
            P(2, i) = nodeCoords(1, i);
            P(3, i) = nodeCoords(2, i);
        }

        double vol6 = P.determinant();
        if (vol6 < 0.0) vol6 = -vol6;

        Eigen::Matrix4d C = P.inverse();

        B.setZero();
        for (int i = 0; i < kNumNodes; ++i) {
            const double beta  = C(1, i);
            const double gamma = C(2, i);
            const double delta = C(3, i);

            B(0, i * 3 + 0) = beta;
            B(1, i * 3 + 1) = gamma;
            B(2, i * 3 + 2) = delta;

            B(3, i * 3 + 0) = gamma;
            B(3, i * 3 + 1) = beta;

            B(4, i * 3 + 1) = delta;
            B(4, i * 3 + 2) = gamma;

            B(5, i * 3 + 0) = delta;
            B(5, i * 3 + 2) = beta;
        }

        return vol6 / 6.0;
    }
};
