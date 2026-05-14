#include "Tet10Element.h"
#include "IMaterial.h"

// =============================================================================
// Gauss quadrature data for the reference tetrahedron.
//
// 4-point symmetric rule:
//   a = (5 + 3*sqrt(5)) / 20 = 0.58541020...
//   b = (5 -   sqrt(5)) / 20 = 0.13819660...
//   weight = 1/24 each   (the reference tet has volume 1/6, so sum = 4/24 = 1/6)
// =============================================================================
static constexpr double GP_A = 0.5854101966249685;
static constexpr double GP_B = 0.1381966011250105;

const double Tet10Element::gaussPts[kNumGauss][3] = {
    { GP_A, GP_B, GP_B },
    { GP_B, GP_A, GP_B },
    { GP_B, GP_B, GP_A },
    { GP_B, GP_B, GP_B }
};

const double Tet10Element::gaussWts[kNumGauss] = {
    1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0, 1.0 / 24.0
};

// =============================================================================
// EvalShapeFunctions — 10 quadratic shape functions at reference point (ξ,η,ζ)
//
// Barycentric coordinates: L0 = 1-ξ-η-ζ, L1 = ξ, L2 = η, L3 = ζ.
// The 10 functions are the unique degree-2 polynomials satisfying N_i(x_j) = δ_{ij}:
//
//   Corners  (i=0..3): N_i = L_i (2 L_i - 1)    — quadratic, vanishes at all other nodes
//   Mid-edges:         N_{ij} = 4 L_i L_j        — peaks at 1 on edge midpoint
//     N_4 = 4 L0 L1  (edge 0-1),  N_5 = 4 L1 L2  (edge 1-2),  N_6 = 4 L0 L2  (edge 0-2)
//     N_7 = 4 L0 L3  (edge 0-3),  N_8 = 4 L1 L3  (edge 1-3),  N_9 = 4 L2 L3  (edge 2-3)
//
// Partition of unity: Σ N_i = 1 for all (ξ,η,ζ) in the reference tetrahedron.
// =============================================================================
void Tet10Element::EvalShapeFunctions(double xi, double eta, double zeta,
                                      Eigen::Matrix<double, kNumNodes, 1>& N)
{
    const double L0 = 1.0 - xi - eta - zeta;
    const double L1 = xi;
    const double L2 = eta;
    const double L3 = zeta;

    N(0) = L0 * (2.0 * L0 - 1.0);
    N(1) = L1 * (2.0 * L1 - 1.0);
    N(2) = L2 * (2.0 * L2 - 1.0);
    N(3) = L3 * (2.0 * L3 - 1.0);

    N(4) = 4.0 * L0 * L1;
    N(5) = 4.0 * L1 * L2;
    N(6) = 4.0 * L0 * L2;
    N(7) = 4.0 * L0 * L3;
    N(8) = 4.0 * L1 * L3;
    N(9) = 4.0 * L2 * L3;
}

// =============================================================================
// EvalShapeGradients — reference-space derivatives ∂N_i/∂(ξ,η,ζ) (10×3)
//
// Derived by chain rule through barycentric coordinates.  Barycentric
// derivatives w.r.t. (ξ,η,ζ):
//   ∂L0/∂ξ = -1,  ∂L0/∂η = -1,  ∂L0/∂ζ = -1
//   ∂L1/∂ξ =  1,  ∂L1/∂η =  0,  ∂L1/∂ζ =  0
//   ∂L2/∂ξ =  0,  ∂L2/∂η =  1,  ∂L2/∂ζ =  0
//   ∂L3/∂ξ =  0,  ∂L3/∂η =  0,  ∂L3/∂ζ =  1
//
// Corner example: ∂N_0/∂ξ = ∂[L0(2L0-1)]/∂ξ = (4L0-1) ∂L0/∂ξ = -(4L0-1)
// Mid-edge example: ∂N4/∂ξ = 4(∂L0/∂ξ · L1 + L0 · ∂L1/∂ξ) = 4(L0 - L1)
//
// Row i of dNdXi = [∂N_i/∂ξ, ∂N_i/∂η, ∂N_i/∂ζ].
// =============================================================================
void Tet10Element::EvalShapeGradients(double xi, double eta, double zeta,
                                      Eigen::Matrix<double, kNumNodes, 3>& dN)
{
    const double L0 = 1.0 - xi - eta - zeta;
    const double L1 = xi;
    const double L2 = eta;
    const double L3 = zeta;

    // dL0/dxi = -1,  dL0/deta = -1,  dL0/dzeta = -1
    // dL1/dxi =  1,  dL1/deta =  0,  dL1/dzeta =  0
    // dL2/dxi =  0,  dL2/deta =  1,  dL2/dzeta =  0
    // dL3/dxi =  0,  dL3/deta =  0,  dL3/dzeta =  1

    // Corner 0: N0 = L0(2L0-1)  ->  dN0/dLk = (4L0-1)*dL0/dLk
    dN(0, 0) = -(4.0 * L0 - 1.0);   // dN0/dxi
    dN(0, 1) = -(4.0 * L0 - 1.0);   // dN0/deta
    dN(0, 2) = -(4.0 * L0 - 1.0);   // dN0/dzeta

    // Corner 1: N1 = L1(2L1-1)
    dN(1, 0) =  (4.0 * L1 - 1.0);
    dN(1, 1) =  0.0;
    dN(1, 2) =  0.0;

    // Corner 2: N2 = L2(2L2-1)
    dN(2, 0) =  0.0;
    dN(2, 1) =  (4.0 * L2 - 1.0);
    dN(2, 2) =  0.0;

    // Corner 3: N3 = L3(2L3-1)
    dN(3, 0) =  0.0;
    dN(3, 1) =  0.0;
    dN(3, 2) =  (4.0 * L3 - 1.0);

    // Mid-edge 4: N4 = 4 L0 L1  -> dN4/dxi = 4(-L1 + L0) = 4(L0-L1)
    dN(4, 0) =  4.0 * (L0 - L1);    // d/dxi
    dN(4, 1) = -4.0 * L1;            // d/deta
    dN(4, 2) = -4.0 * L1;            // d/dzeta

    // Mid-edge 5: N5 = 4 L1 L2
    dN(5, 0) =  4.0 * L2;
    dN(5, 1) =  4.0 * L1;
    dN(5, 2) =  0.0;

    // Mid-edge 6: N6 = 4 L0 L2
    dN(6, 0) = -4.0 * L2;
    dN(6, 1) =  4.0 * (L0 - L2);
    dN(6, 2) = -4.0 * L2;

    // Mid-edge 7: N7 = 4 L0 L3
    dN(7, 0) = -4.0 * L3;
    dN(7, 1) = -4.0 * L3;
    dN(7, 2) =  4.0 * (L0 - L3);

    // Mid-edge 8: N8 = 4 L1 L3
    dN(8, 0) =  4.0 * L3;
    dN(8, 1) =  0.0;
    dN(8, 2) =  4.0 * L1;

    // Mid-edge 9: N9 = 4 L2 L3
    dN(9, 0) =  0.0;
    dN(9, 1) =  4.0 * L3;
    dN(9, 2) =  4.0 * L2;
}

// =============================================================================
// ComputeBAtGaussPoint — 6×30 strain-displacement matrix B at Gauss point gp.
// Returns det(J), the Jacobian determinant of the reference-to-physical map.
//
// Steps:
//   1. Evaluate ∂N/∂ξ (10×3) at the gp reference coordinates via EvalShapeGradients.
//   2. Form the isoparametric Jacobian:
//        J = (∂N/∂ξ)^T * X_nodes   (3×3),  J_ij = Σ_k (∂N_k/∂ξ_i) x_{kj}
//      mapping reference tangent vectors to physical tangent vectors.
//   3. Physical gradients via inverse Jacobian transpose:
//        ∂N/∂x = ∂N/∂ξ * J^{-T}   (10×3)
//   4. Assemble B (6×30) in engineering Voigt notation [ε_xx,ε_yy,ε_zz,γ_xy,γ_yz,γ_xz]:
//      for node i with physical derivatives (dNdx, dNdy, dNdz):
//        col 3i+0 carries u-DOF contributions: [dNdx, 0, 0, dNdy, 0, dNdz]
//        col 3i+1 carries v-DOF contributions: [0, dNdy, 0, dNdx, dNdz, 0]
//        col 3i+2 carries w-DOF contributions: [0, 0, dNdz, 0, dNdy, dNdx]
// =============================================================================
double Tet10Element::ComputeBAtGaussPoint(
    int gp,
    Eigen::Matrix<double, 6, kNumDOFs>& B) const
{
    const double xi   = gaussPts[gp][0];
    const double eta  = gaussPts[gp][1];
    const double zeta = gaussPts[gp][2];

    Eigen::Matrix<double, kNumNodes, 3> dNdXi;
    EvalShapeGradients(xi, eta, zeta, dNdXi);

    // Jacobian J = dNdXi^T * X    (3 x 3)
    // where X = nodeCoords^T (10 x 3) but we can compute as:
    //   J = (dNdXi)^T * (nodeCoords^T)   [3x10 * 10x3 = 3x3]
    Eigen::Matrix<double, 3, 3> J = dNdXi.transpose() * nodeCoords.transpose();
    const double detJ = J.determinant();

    Eigen::Matrix<double, 3, 3> Jinv = J.inverse();

    // dN/dx = dNdXi * Jinv^T   (10 x 3)
    Eigen::Matrix<double, kNumNodes, 3> dNdX = dNdXi * Jinv.transpose();

    // Assemble B (6 x 30)
    B.setZero();
    for (int i = 0; i < kNumNodes; ++i) {
        const double dNdx = dNdX(i, 0);
        const double dNdy = dNdX(i, 1);
        const double dNdz = dNdX(i, 2);

        B(0, i*3 + 0) = dNdx;
        B(1, i*3 + 1) = dNdy;
        B(2, i*3 + 2) = dNdz;

        B(3, i*3 + 0) = dNdy;   B(3, i*3 + 1) = dNdx;
        B(4, i*3 + 1) = dNdz;   B(4, i*3 + 2) = dNdy;
        B(5, i*3 + 0) = dNdz;   B(5, i*3 + 2) = dNdx;
    }

    return detJ;
}

// =============================================================================
// ComputeTangentStiffness — element stiffness K_e (30×30) via Gauss quadrature
//
//   K_e = Σ_{g=1}^{4} w_g |det(J_g)| B_g^T D B_g
//
// The 4-point Hammer rule (w_g = 1/24, sum = 1/6 = volume of reference tet)
// integrates degree-2 polynomial integrands exactly. B_g is degree-1 in
// (ξ,η,ζ), so B^T D B is degree-2 — quadrature is exact for Tet10.
// For a linear-elastic material D is constant, so K_e is independent of ue.
// =============================================================================
void Tet10Element::ComputeTangentStiffness(const Eigen::VectorXd& /*ue*/,
                                           Eigen::MatrixXd&       Ke) const
{
    Eigen::Matrix<double, 6, 6> D;
    material->ComputeConstitutive(D);

    Eigen::Matrix<double, kNumDOFs, kNumDOFs> KeLocal;
    KeLocal.setZero();

    Eigen::Matrix<double, 6, kNumDOFs> B;

    for (int gp = 0; gp < kNumGauss; ++gp) {
        const double detJ = ComputeBAtGaussPoint(gp, B);
        const double w    = gaussWts[gp] * std::abs(detJ);
        KeLocal.noalias() += w * (B.transpose() * D * B);
    }

    Ke = KeLocal;
}

// =============================================================================
// ComputeInternalForceVector — internal force f_e (30×1) via Gauss quadrature
//
//   f_e = Σ_{g=1}^{4} w_g |det(J_g)| B_g^T σ_g,   σ_g = D B_g u_e
//
// For linear elasticity this equals K_e u_e. The per-Gauss-point structure
// is preserved for future hyperelastic readiness: when σ is computed from a
// nonlinear constitutive law F(ε) rather than D ε, only the σ evaluation line
// changes — the quadrature loop and B assembly remain unchanged.
// =============================================================================
void Tet10Element::ComputeInternalForceVector(const Eigen::VectorXd& ue,
                                              Eigen::VectorXd&       fe) const
{
    Eigen::Matrix<double, 6, 6> D;
    material->ComputeConstitutive(D);

    Eigen::Matrix<double, kNumDOFs, 1> ueLocal;
    for (int i = 0; i < kNumDOFs; ++i) ueLocal(i) = ue(i);

    Eigen::Matrix<double, kNumDOFs, 1> feLocal;
    feLocal.setZero();

    Eigen::Matrix<double, 6, kNumDOFs> B;

    for (int gp = 0; gp < kNumGauss; ++gp) {
        const double detJ = ComputeBAtGaussPoint(gp, B);
        const double w    = gaussWts[gp] * std::abs(detJ);

        // strain at GP
        Eigen::Matrix<double, 6, 1> eps = B * ueLocal;
        // stress at GP
        Eigen::Matrix<double, 6, 1> sigma = D * eps;
        // accumulate
        feLocal.noalias() += w * (B.transpose() * sigma);
    }

    fe.resize(kNumDOFs);
    for (int i = 0; i < kNumDOFs; ++i) fe(i) = feLocal(i);
}

void Tet10Element::ComputeStressVoigt(const Eigen::VectorXd& U_global,
                                       Eigen::Matrix<double,6,1>& sigma_out) const
{
    // Gather element displacement (30 entries)
    Eigen::Matrix<double, kNumDOFs, 1> ue;
    for (int n = 0; n < kNumNodes; ++n) {
        int base = nodeIds[n] * 3;
        ue(n*3+0) = U_global(base+0);
        ue(n*3+1) = U_global(base+1);
        ue(n*3+2) = U_global(base+2);
    }

    Eigen::Matrix<double,6,6> D;
    material->ComputeConstitutive(D);

    // Average stress over the 4 Gauss points
    sigma_out.setZero();
    for (int gp = 0; gp < kNumGauss; ++gp) {
        Eigen::Matrix<double, 6, kNumDOFs> B;
        ComputeBAtGaussPoint(gp, B);
        sigma_out += D * (B * ue);
    }
    sigma_out *= (1.0 / kNumGauss);
}

Eigen::Matrix<double,6,Tet10Element::kNumDOFs> Tet10Element::ComputeAvgDB() const
{
    Eigen::Matrix<double,6,6> D;
    material->ComputeConstitutive(D);
    Eigen::Matrix<double,6,kNumDOFs> avgDB;
    avgDB.setZero();
    for (int gp = 0; gp < kNumGauss; ++gp) {
        Eigen::Matrix<double,6,kNumDOFs> B;
        ComputeBAtGaussPoint(gp, B);
        avgDB += D * B;
    }
    return avgDB * (1.0 / kNumGauss);
}
