#pragma once

#include <array>
#include <Eigen/Dense>

#include "IElement.h"
#include "IMaterial.h"

// -----------------------------------------------------------------------------
// Tet10Element (quadratic tet10, 3D, 3 DOFs / node)
// -----------------------------------------------------------------------------
// 10-node isoparametric quadratic tetrahedron.
//
// Nodes:
//   0-3   : corner vertices  (same as the parent Tet4)
//   4-9   : mid-edge nodes   (one per edge of the tet)
//
// Edge-to-midnode mapping (ABAQUS convention):
//   edge(0,1) -> node 4       edge(0,2) -> node 6
//   edge(1,2) -> node 5       edge(0,3) -> node 7
//   edge(1,3) -> node 8       edge(2,3) -> node 9
//
// Barycentric coordinates:
//   L0 = 1 - xi - eta - zeta,   L1 = xi,   L2 = eta,   L3 = zeta
//
// Shape functions:
//   Corners (i=0..3):      N_i = L_i (2 L_i - 1)
//   Mid-edges (i<j):       N_{ij} = 4 L_i L_j
//
// Gauss quadrature: 4-point symmetric rule on the reference tetrahedron.
//   a = 0.58541020, b = 0.13819660, weight = 1/24 each.
//   Points: (a,b,b), (b,a,b), (b,b,a), (b,b,b)
//
// Stiffness matrix size: 30 x 30.
//
// NOTE: All heavy-path arithmetic uses fixed-size Eigen types; the single
// copy into dynamic output happens at the IElement boundary.
// -----------------------------------------------------------------------------
class Tet10Element : public IElement {
public:
    static constexpr int kNumNodes = 10;
    static constexpr int kNumDOFs  = 30;
    static constexpr int kNumGauss = 4;

    // Reference-configuration nodal positions. Column i = node i's (x,y,z).
    Eigen::Matrix<double, 3, kNumNodes> nodeCoords;
    // Global node indices in the same order as nodeCoords columns.
    std::array<int, kNumNodes>          nodeIds;
    // Non-owning pointer to the constitutive model.
    const IMaterial*                    material = nullptr;

    Tet10Element(const Eigen::Matrix<double, 3, kNumNodes>& coords,
                 const std::array<int, kNumNodes>&           ids,
                 const IMaterial*                            mat)
        : nodeCoords(coords), nodeIds(ids), material(mat) {}

    int NumDOFs() const override { return kNumDOFs; }

    // Fills the 30-entry scatter index vector I_e:
    //   I_e[3n+k] = 3*nodeIds[n] + k,  n ∈ {0..9},  k ∈ {0,1,2}
    // Consistent with the row/column ordering of K_e from ComputeTangentStiffness.
    void GetGlobalDOFs(std::vector<int>& outDofs) const override {
        outDofs.resize(kNumDOFs);
        for (int n = 0; n < kNumNodes; ++n) {
            outDofs[n * 3 + 0] = nodeIds[n] * 3 + 0;
            outDofs[n * 3 + 1] = nodeIds[n] * 3 + 1;
            outDofs[n * 3 + 2] = nodeIds[n] * 3 + 2;
        }
    }

    // Computes K_e via 4-point Gauss quadrature on the reference tetrahedron (30×30):
    //   K_e = Σ_{g=1}^{4} w_g |det(J_g)| B_g^T D B_g
    //
    // The 4-point Hammer rule integrates degree-2 polynomial integrands exactly —
    // sufficient because B is degree-1 in (ξ,η,ζ) (first derivatives of
    // degree-2 shape functions), so B^T D B is degree 2. Weight w_g = 1/24 for all g.
    void ComputeTangentStiffness(const Eigen::VectorXd& ue,
                                 Eigen::MatrixXd&       Ke) const override;

    // Computes the internal force vector via Gauss quadrature (30×1):
    //   f_e = Σ_{g=1}^{4} w_g |det(J_g)| B_g^T σ_g,   σ_g = D B_g u_e
    // Structured per-Gauss-point for future hyperelastic readiness: when σ is
    // computed from a nonlinear constitutive law, only the stress evaluation changes.
    void ComputeInternalForceVector(const Eigen::VectorXd& ue,
                                    Eigen::VectorXd&       fe) const override;

private:
    // Evaluates the 10 quadratic shape functions at reference point (ξ, η, ζ).
    // Barycentric: L0 = 1-ξ-η-ζ, L1 = ξ, L2 = η, L3 = ζ.
    // Corner nodes:   N_i = L_i (2 L_i - 1)         (i = 0..3)
    // Mid-edge nodes: N_ij     = 4 L_i L_j           (nodes 4..9)
    // These are the unique degree-2 polynomials satisfying N_i(x_j) = δ_{ij}.
    static void EvalShapeFunctions(double xi, double eta, double zeta,
                                   Eigen::Matrix<double, kNumNodes, 1>& N);

    // Evaluates ∂N_i/∂(ξ,η,ζ) analytically for all 10 shape functions.
    // dNdXi (10×3): row i = [∂N_i/∂ξ, ∂N_i/∂η, ∂N_i/∂ζ].
    // Derived via chain rule: ∂L0/∂ξ = -1, ∂L1/∂ξ = 1, ∂L2/∂η = 1, ∂L3/∂ζ = 1.
    // e.g. ∂N4/∂ξ = 4(∂L0/∂ξ · L1 + L0 · ∂L1/∂ξ) = 4(L0 - L1).
    static void EvalShapeGradients(double xi, double eta, double zeta,
                                   Eigen::Matrix<double, kNumNodes, 3>& dNdXi);

    // Assembles the 6×30 strain-displacement B at Gauss point gp; returns det(J).
    // Steps:
    //   1. ∂N/∂ξ (10×3) via EvalShapeGradients at gp reference coordinates.
    //   2. Isoparametric Jacobian: J = (∂N/∂ξ)^T * X_nodes  (3×3).
    //   3. Physical gradients: ∂N/∂x = ∂N/∂ξ * J^{-T}  (10×3).
    //   4. Assemble B in engineering Voigt notation [ε_xx, ε_yy, ε_zz, γ_xy, γ_yz, γ_xz].
    double ComputeBAtGaussPoint(int gp,
                                Eigen::Matrix<double, 6, kNumDOFs>& B) const;

    // Gauss point coordinates in (xi, eta, zeta).
    static const double gaussPts[kNumGauss][3];
    // Gauss weights (for the reference tet with volume 1/6).
    static const double gaussWts[kNumGauss];
};
